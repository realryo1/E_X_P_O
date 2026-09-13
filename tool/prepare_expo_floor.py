# -*- coding: utf-8 -*-
"""公式GeoTIFFオルソを床クアッドGLBにする。

第三者XYZタイルは使わない。地理範囲はデータセット記載の
北34.658333 / 西135.3625 / 東135.4 / 南34.641667。
"""

from __future__ import annotations

import argparse
import io
import json
import sys
from pathlib import Path

from PIL import Image

TOOL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOL_DIR))

from expo_glb_util import (  # noqa: E402
    bake_ecef_triangle_normals_gltf,
    ecef_to_gltf_yup,
    geodetic_to_ecef,
    save_triangle_glb,
    write_field_manifest,
)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
ORTHO_DIR = PROJECT_ROOT / "data_original" / "ortho" / "27999_osaka-shi_city_2025_ortho_1_op"
LOD2_MANIFEST = PROJECT_ROOT / "asset" / "expomodel" / "expo_tiles_lod2.txt"
NORTH = 34.658333
WEST = 135.3625
EAST = 135.4
SOUTH = 34.641667
FLOOR_HEIGHT = 2.0
MAX_EDGE = 8192


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GeoTIFFオルソから床GLBを作る")
    parser.add_argument("--ortho-dir", type=Path, default=ORTHO_DIR)
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "data_converted" / "meshes")
    parser.add_argument("--runtime-output", type=Path, default=PROJECT_ROOT / "asset" / "expomodel" / "expo_floor.glb")
    parser.add_argument("--height", type=float, default=FLOOR_HEIGHT)
    parser.add_argument("--max-edge", type=int, default=MAX_EDGE)
    return parser.parse_args()


def read_tfw(path: Path) -> tuple[float, float, float, float]:
    lines = path.read_text(encoding="utf-8").splitlines()
    xres = float(lines[0])
    yres = float(lines[3])
    x_ul = float(lines[4])
    y_ul = float(lines[5])
    return xres, yres, x_ul, y_ul


def load_lod2_ref_rtc(path: Path) -> list[float] | None:
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("ref_rtc "):
            parts = line.split()
            return [float(parts[1]), float(parts[2]), float(parts[3])]
    return None


def stitch_ortho(ortho_dir: Path, max_edge: int) -> tuple[Image.Image, dict[str, object]]:
    tiles: list[dict[str, object]] = []
    min_lon = min_lat = 1e9
    max_lon = max_lat = -1e9
    for tif_path in sorted(ortho_dir.glob("*.tif")):
        tfw_path = tif_path.with_suffix(".tfw")
        xres, yres, x_ul, y_ul = read_tfw(tfw_path)
        image = Image.open(tif_path)
        width, height = image.size
        lon0 = x_ul
        lat0 = y_ul + yres * (height - 1)
        lon1 = x_ul + xres * (width - 1)
        lat1 = y_ul
        min_lon = min(min_lon, lon0, lon1)
        max_lon = max(max_lon, lon0, lon1)
        min_lat = min(min_lat, lat0, lat1)
        max_lat = max(max_lat, lat0, lat1)
        tiles.append(
            {
                "path": tif_path,
                "image": image,
                "xres": xres,
                "yres": yres,
                "x_ul": x_ul,
                "y_ul": y_ul,
                "width": width,
                "height": height,
            }
        )

    xres = float(tiles[0]["xres"])
    yres = abs(float(tiles[0]["yres"]))
    mosaic_w = int(round((max_lon - min_lon) / xres)) + 1
    mosaic_h = int(round((max_lat - min_lat) / yres)) + 1
    mosaic = Image.new("RGB", (mosaic_w, mosaic_h), (0, 0, 0))
    for tile in tiles:
        image = tile["image"].convert("RGB")
        px = int(round((float(tile["x_ul"]) - min_lon) / xres))
        py = int(round((max_lat - float(tile["y_ul"])) / yres))
        mosaic.paste(image, (px, py))
        image.close()
        tile["image"].close()

    left = max(0, int(round((WEST - min_lon) / xres)))
    right = min(mosaic_w, int(round((EAST - min_lon) / xres)) + 1)
    top = max(0, int(round((max_lat - NORTH) / yres)))
    bottom = min(mosaic_h, int(round((max_lat - SOUTH) / yres)) + 1)
    cropped = mosaic.crop((left, top, right, bottom))
    mosaic.close()

    scale = min(1.0, max_edge / max(cropped.size))
    if scale < 1.0:
        new_size = (max(1, int(cropped.size[0] * scale)), max(1, int(cropped.size[1] * scale)))
        cropped = cropped.resize(new_size, Image.Resampling.LANCZOS)

    stats = {
        "tile_count": len(tiles),
        "mosaic_bbox": [min_lon, min_lat, max_lon, max_lat],
        "crop": [WEST, SOUTH, EAST, NORTH],
        "output_size": list(cropped.size),
    }
    return cropped, stats


def main() -> int:
    args = parse_args()
    if not args.ortho_dir.is_dir():
        print(f"オルソディレクトリがありません: {args.ortho_dir}")
        return 1

    image, stats = stitch_ortho(args.ortho_dir, args.max_edge)
    jpeg_buf = io.BytesIO()
    image.save(jpeg_buf, format="JPEG", quality=85, optimize=True)
    image_bytes = jpeg_buf.getvalue()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    png_preview = args.output_dir / "expo_ortho.jpg"
    png_preview.write_bytes(image_bytes)

    corners = [
        (SOUTH, WEST, args.height, 0.0, 1.0),
        (SOUTH, EAST, args.height, 1.0, 1.0),
        (NORTH, EAST, args.height, 1.0, 0.0),
        (NORTH, WEST, args.height, 0.0, 0.0),
    ]
    ecef = [geodetic_to_ecef(lat, lon, h) for lat, lon, h, _, _ in corners]
    rtc = [
        sum(p[0] for p in ecef) / 4.0,
        sum(p[1] for p in ecef) / 4.0,
        sum(p[2] for p in ecef) / 4.0,
    ]
    ecef_rel = [
        (p[0] - rtc[0], p[1] - rtc[1], p[2] - rtc[2])
        for p in ecef
    ]
    positions = [ecef_to_gltf_yup(*p) for p in ecef_rel]
    uvs = [(u, v) for _, _, _, u, v in corners]
    indices = [0, 1, 2, 0, 2, 3]
    # 大屋根リングと同じ。glTF の (0,1,0) は ECEF 床では実行時に横を向く。
    normals = bake_ecef_triangle_normals_gltf(ecef_rel, indices, outward_ecef=tuple(rtc))

    converted = args.output_dir / "expo_floor.glb"
    save_triangle_glb(
        converted,
        positions,
        indices,
        uvs=uvs,
        normals=normals,
        image_bytes=image_bytes,
        image_mime="image/jpeg",
        double_sided=True,
    )
    args.runtime_output.parent.mkdir(parents=True, exist_ok=True)
    args.runtime_output.write_bytes(converted.read_bytes())

    metadata = {
        "source": "GeoTIFF ortho v4",
        "ortho_dir": str(args.ortho_dir.relative_to(PROJECT_ROOT)),
        "extent": {"north": NORTH, "west": WEST, "east": EAST, "south": SOUTH},
        "height_m": args.height,
        "rtc_center": rtc,
        "lod2_ref_rtc": load_lod2_ref_rtc(LOD2_MANIFEST),
        "runtime_path": str(args.runtime_output.relative_to(PROJECT_ROOT)),
        "stats": stats,
    }
    (args.output_dir / "expo_floor.metadata.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_field_manifest(
        str(args.runtime_output.relative_to(PROJECT_ROOT)).replace("/", "\\"),
        rtc,
        "",
        rtc,
    )
    from prepare_collision import export_collision_bin  # noqa: E402

    bin_path = export_collision_bin(args.runtime_output)
    print(f"ortho {stats['output_size']} jpeg {len(image_bytes)} bytes")
    print(f"wrote {converted}")
    print(f"runtime {args.runtime_output}")
    if bin_path:
        print(f"collision {bin_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
