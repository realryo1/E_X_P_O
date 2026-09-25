# -*- coding: utf-8 -*-
"""公式GeoTIFFオルソを床グリッドGLBにする。

床アルベドの一次出所は公式GeoTIFFのみで、第三者XYZタイルは使わない。
地理範囲はデータセット記載の北34.658333 / 西135.3625 / 東135.4 / 南34.641667。

高低差は CityGML `udx/dem/` の TINRelief から相対起伏だけを焼く。
会場範囲内サンプルの中央値を基準面にし、頂点高は `height + (dem - 中央値)` とする。
建物 Y `-9.010` とリング Y `-1.550` は平坦床 `height=2.0` に合わせてあるため、
DEM の絶対標高はそのまま足さない。DEM が無い点の起伏は 0。
橋梁などの TIN スパイクが壁にならないよう起伏は ±`relief-limit` m で切り詰め、
DEM 被覆端では `feather-m` m 幅で起伏を 0 へなじませる。
"""

from __future__ import annotations

import argparse
import io
import json
import math
import re
import statistics
import sys
import zipfile
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
CITYGML_ZIP = PROJECT_ROOT / "data_original" / "27999_osaka-shi_city_2025_citygml_1_op.zip"
DEM_MEMBERS = (
    "udx/dem/513572_dem_6697.gml",
    "udx/dem/513573_dem_6697.gml",
)
POSLIST_RE = re.compile(r"<gml:posList[^>]*>([^<]+)</gml:posList>")
SRSNAME_RE = re.compile(r'srsName="([^"]+)"')
# DEM 格子セルの一辺（度）。TIN 三角形は数十 m 規模なので 0.002 度で十分細かい。
DEM_CELL_DEG = 0.002
LOD2_MANIFEST = PROJECT_ROOT / "asset" / "expomodel" / "expo_tiles_lod2.txt"
NORTH = 34.658333
WEST = 135.3625
EAST = 135.4
SOUTH = 34.641667
FLOOR_HEIGHT = 2.0
FLOOR_SPACING_M = 4.0
# 相対起伏の上限（m）。橋梁などの TIN スパイクが壁にならないようにする。
# 会場の地面は中央値±3m に収まる。超えた分は平らに切り詰める。
RELIEF_LIMIT_M = 3.0
# DEM 被覆端からのなじませ幅（m）。被覆外は起伏 0 のため、そのままでは
# 境界に段差ができる。端からこの距離で起伏を 0 へ落とす。
FEATHER_M = 40.0
MAX_EDGE = 8192


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GeoTIFFオルソから床GLBを作る")
    parser.add_argument("--ortho-dir", type=Path, default=ORTHO_DIR)
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "data_converted" / "meshes")
    parser.add_argument("--runtime-output", type=Path, default=PROJECT_ROOT / "asset" / "expomodel" / "expo_floor.glb")
    parser.add_argument("--height", type=float, default=FLOOR_HEIGHT)
    parser.add_argument("--max-edge", type=int, default=MAX_EDGE)
    parser.add_argument("--citygml-zip", type=Path, default=CITYGML_ZIP)
    parser.add_argument("--spacing", type=float, default=FLOOR_SPACING_M)
    parser.add_argument("--relief-limit", type=float, default=RELIEF_LIMIT_M)
    parser.add_argument("--feather-m", type=float, default=FEATHER_M)
    parser.add_argument("--no-dem", action="store_true", help="DEM起伏を焼かず水平床にする")
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


def load_dem_triangles(
    zip_path: Path,
    members: tuple[str, ...] = DEM_MEMBERS,
) -> tuple[list[tuple[float, float, float, float, float, float, float, float, float]], dict[str, object]]:
    """CityGML dem の TIN 三角形を (lat, lon, h)×3 のリストで返す。

    srsName は EPSG:6697 と書かれているが、実データの座標は緯度・経度・標高の
    順（約34 / 約135 / m）である。最初の2値の桁で判定し、緯度経度だけを使う。
    平面直角座標系（メートルの大きな値）の点は使わず数えるだけにする。
    """
    triangles: list[tuple[float, float, float, float, float, float, float, float, float]] = []
    srs_names: list[str] = []
    plane_points = 0
    broken = 0
    for member in members:
        try:
            with zipfile.ZipFile(zip_path) as bag:
                raw = bag.read(member).decode("utf-8")
        except KeyError:
            print(f"DEMメンバがありません: {member}")
            continue
        for found in SRSNAME_RE.findall(raw):
            if found not in srs_names:
                srs_names.append(found)
        for match in POSLIST_RE.finditer(raw):
            values = [float(v) for v in match.group(1).split()]
            if len(values) < 9 or len(values) % 3 != 0:
                broken += 1
                continue
            pts = [(values[i], values[i + 1], values[i + 2]) for i in range(0, len(values), 3)]
            if pts[0] == pts[-1]:
                pts = pts[:-1]
            if len(pts) < 3:
                broken += 1
                continue
            (lat0, lon0, h0), (lat1, lon1, h1), (lat2, lon2, h2) = pts[0], pts[1], pts[2]
            if 20.0 <= lat0 <= 50.0 and 100.0 <= lon0 <= 160.0:
                triangles.append((lat0, lon0, h0, lat1, lon1, h1, lat2, lon2, h2))
            else:
                plane_points += 1
    stats: dict[str, object] = {
        "members": list(members),
        "triangles": len(triangles),
        "srs_names": srs_names,
        "skipped_plane_points": plane_points,
        "skipped_broken": broken,
    }
    return triangles, stats


def build_dem_index(
    triangles: list[tuple[float, float, float, float, float, float, float, float, float]],
    cell_deg: float = DEM_CELL_DEG,
) -> tuple[dict[tuple[int, int], list[int]], float, float]:
    """三角形を緯度経度の均一格子へ入れる。戻り値は (格子, 南端, 西端)。"""
    index: dict[tuple[int, int], list[int]] = {}
    if not triangles:
        return index, SOUTH, WEST
    min_lat = min(min(t[0], t[3], t[6]) for t in triangles)
    min_lon = min(min(t[1], t[4], t[7]) for t in triangles)
    for number, (lat0, lon0, _h0, lat1, lon1, _h1, lat2, lon2, _h2) in enumerate(triangles):
        x0 = int(math.floor((min(lon0, lon1, lon2) - min_lon) / cell_deg))
        x1 = int(math.floor((max(lon0, lon1, lon2) - min_lon) / cell_deg))
        y0 = int(math.floor((min(lat0, lat1, lat2) - min_lat) / cell_deg))
        y1 = int(math.floor((max(lat0, lat1, lat2) - min_lat) / cell_deg))
        for cy in range(y0, y1 + 1):
            for cx in range(x0, x1 + 1):
                index.setdefault((cx, cy), []).append(number)
    return index, min_lat, min_lon


def sample_dem_relief(
    triangles: list[tuple[float, float, float, float, float, float, float, float, float]],
    index: dict[tuple[int, int], list[int]],
    index_lat0: float,
    index_lon0: float,
    lats: list[float],
    lons: list[float],
    cell_deg: float = DEM_CELL_DEG,
) -> list[float | None]:
    """床グリッド各点の DEM 標高を重心補間で求める。範囲外は None。"""
    # 三角形ごとに経度・緯度の辺ベクトルと分母を先に作る。
    ax: list[float] = []
    ay: list[float] = []
    bx: list[float] = []
    by: list[float] = []
    cx: list[float] = []
    cy: list[float] = []
    h0: list[float] = []
    h1: list[float] = []
    h2: list[float] = []
    denom: list[float] = []
    for lat_a, lon_a, ha, lat_b, lon_b, hb, lat_c, lon_c, hc in triangles:
        ax.append(lon_a)
        ay.append(lat_a)
        bx.append(lon_b - lon_a)
        by.append(lat_b - lat_a)
        cx.append(lon_c - lon_a)
        cy.append(lat_c - lat_a)
        h0.append(ha)
        h1.append(hb)
        h2.append(hc)
        d = by[-1] * cx[-1] - bx[-1] * cy[-1]
        denom.append(d)
    out: list[float | None] = []
    for lat, lon in zip(lats, lons):
        key = (
            int(math.floor((lon - index_lon0) / cell_deg)),
            int(math.floor((lat - index_lat0) / cell_deg)),
        )
        found: float | None = None
        for number in index.get(key, ()):
            d = denom[number]
            if abs(d) < 1e-18:
                continue
            dx = lon - ax[number]
            dy = lat - ay[number]
            # p = a + s*b + t*c を解く（2D 逆行列）。
            s = (dy * cx[number] - dx * cy[number]) / d
            t = (by[number] * dx - bx[number] * dy) / d
            if s >= -1e-9 and t >= -1e-9 and s + t <= 1.0 + 1e-9:
                found = h0[number] + s * (h1[number] - h0[number]) + t * (h2[number] - h0[number])
                break
        out.append(found)
    return out


def apply_relief_guards(
    reliefs: list[float],
    covered: list[bool],
    nx: int,
    ny: int,
    spacing_m: float,
    limit_m: float,
    feather_m: float,
) -> tuple[list[float], int]:
    """起伏の上限と被覆端なじませを適用する。戻り値は (起伏, 切詰点数)。"""
    clamped = 0
    guarded: list[float] = []
    for value in reliefs:
        if value > limit_m:
            clamped += 1
            guarded.append(limit_m)
        elif value < -limit_m:
            clamped += 1
            guarded.append(-limit_m)
        else:
            guarded.append(value)
    if feather_m <= 0.0 or spacing_m <= 0.0:
        return guarded, clamped
    # 被覆外からの chamfer 距離（m）。2パスで十分な近似になる。
    import numpy as np

    INF = 1e9
    dist = np.where(np.array(covered, dtype=bool).reshape(ny, nx), INF, 0.0)
    step_x = spacing_m
    step_y = spacing_m
    diag = math.hypot(step_x, step_y)
    for iy in range(ny):
        row = dist[iy]
        prev = dist[iy - 1] if iy > 0 else None
        for ix in range(nx):
            if row[ix] == 0.0:
                continue
            best = row[ix]
            if ix > 0:
                best = min(best, row[ix - 1] + step_x)
            if prev is not None:
                best = min(best, prev[ix] + step_y)
                if ix > 0:
                    best = min(best, prev[ix - 1] + diag)
                if ix + 1 < nx:
                    best = min(best, prev[ix + 1] + diag)
            row[ix] = best
    for iy in range(ny - 1, -1, -1):
        row = dist[iy]
        nxt = dist[iy + 1] if iy + 1 < ny else None
        for ix in range(nx - 1, -1, -1):
            if row[ix] == 0.0:
                continue
            best = row[ix]
            if ix + 1 < nx:
                best = min(best, row[ix + 1] + step_x)
            if nxt is not None:
                best = min(best, nxt[ix] + step_y)
                if ix + 1 < nx:
                    best = min(best, nxt[ix + 1] + diag)
                if ix > 0:
                    best = min(best, nxt[ix - 1] + diag)
            row[ix] = best
    flat = dist.reshape(-1)
    out: list[float] = []
    for value, distance in zip(guarded, flat.tolist()):
        t = min(1.0, distance / feather_m)
        weight = t * t * (3.0 - 2.0 * t)
        out.append(value * weight)
    return out, clamped


def build_floor_grid(spacing_m: float) -> tuple[list[float], list[float], int, int]:
    """会場範囲を spacing_m 間隔の緯度経度グリッドにする。戻り値は (lats, lons, nx, ny)。"""
    mid_lat = (NORTH + SOUTH) * 0.5
    dlat = spacing_m / 111000.0
    dlon = spacing_m / (111320.0 * math.cos(math.radians(mid_lat)))
    nx = max(2, int(round((EAST - WEST) / dlon)) + 1)
    ny = max(2, int(round((NORTH - SOUTH) / dlat)) + 1)
    lats: list[float] = []
    lons: list[float] = []
    for iy in range(ny):
        lat = SOUTH + (NORTH - SOUTH) * iy / (ny - 1)
        for ix in range(nx):
            lon = WEST + (EAST - WEST) * ix / (nx - 1)
            lats.append(lat)
            lons.append(lon)
    return lats, lons, nx, ny


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

    dem_stats: dict[str, object] = {"enabled": False}
    if args.no_dem or not args.citygml_zip.is_file():
        if not args.no_dem:
            print(f"CityGML ZIPが無いため水平床にします: {args.citygml_zip}")
        lats = [lat for lat, _lon, _h, _u, _v in corners]
        lons = [lon for _lat, lon, _h, _u, _v in corners]
        heights = [args.height] * 4
        grid_nx, grid_ny = 2, 2
        uvs = [(u, v) for _, _, _, u, v in corners]
        indices = [0, 1, 2, 0, 2, 3]
    else:
        triangles, dem_stats = load_dem_triangles(args.citygml_zip)
        dem_stats["enabled"] = True
        lats, lons, grid_nx, grid_ny = build_floor_grid(args.spacing)
        dem_heights: list[float | None] = []
        if triangles:
            dem_index, index_lat0, index_lon0 = build_dem_index(triangles)
            dem_heights = sample_dem_relief(triangles, dem_index, index_lat0, index_lon0, lats, lons)
        else:
            dem_heights = [None] * len(lats)
            print("DEM三角形が無いため水平床にします")
        covered = [h for h in dem_heights if h is not None]
        median = statistics.median(covered) if covered else 0.0
        raw_reliefs = [(h - median) if h is not None else 0.0 for h in dem_heights]
        reliefs, clamped = apply_relief_guards(
            raw_reliefs,
            [h is not None for h in dem_heights],
            grid_nx,
            grid_ny,
            args.spacing,
            args.relief_limit,
            args.feather_m,
        )
        heights = [args.height + r for r in reliefs]
        uvs = [
            ((lon - WEST) / (EAST - WEST), (NORTH - lat) / (NORTH - SOUTH))
            for lat, lon in zip(lats, lons)
        ]
        indices = []
        for iy in range(grid_ny - 1):
            for ix in range(grid_nx - 1):
                a = iy * grid_nx + ix
                b = a + 1
                c = a + grid_nx + 1
                d = a + grid_nx
                indices.extend((a, b, c, a, c, d))
        if covered:
            dem_stats["median_m"] = median
            dem_stats["relief_min_m"] = min(reliefs)
            dem_stats["relief_max_m"] = max(reliefs)
            dem_stats["covered_points"] = len(covered)
            dem_stats["clamped_points"] = clamped
        dem_stats["relief_limit_m"] = args.relief_limit
        dem_stats["feather_m"] = args.feather_m
        dem_stats["grid"] = [grid_nx, grid_ny]
        dem_stats["spacing_m"] = args.spacing
        dem_stats["total_points"] = len(lats)
        print(
            f"DEM三角形 {dem_stats['triangles']} / "
            f"グリッド {grid_nx}x{grid_ny} / "
            f"起伏 {dem_stats.get('relief_min_m', 0.0):.3f}..{dem_stats.get('relief_max_m', 0.0):.3f}m / "
            f"基準 {dem_stats.get('median_m', 0.0):.3f}m / "
            f"被覆 {dem_stats.get('covered_points', 0)}/{len(lats)}"
        )

    ecef_rel = [
        (p[0] - rtc[0], p[1] - rtc[1], p[2] - rtc[2])
        for p in (geodetic_to_ecef(lat, lon, h) for lat, lon, h in zip(lats, lons, heights))
    ]
    positions = [ecef_to_gltf_yup(*p) for p in ecef_rel]
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
        "source": "GeoTIFF ortho v4 + CityGML dem TINRelief",
        "ortho_dir": str(args.ortho_dir.relative_to(PROJECT_ROOT)),
        "extent": {"north": NORTH, "west": WEST, "east": EAST, "south": SOUTH},
        "height_m": args.height,
        "rtc_center": rtc,
        "lod2_ref_rtc": load_lod2_ref_rtc(LOD2_MANIFEST),
        "runtime_path": str(args.runtime_output.relative_to(PROJECT_ROOT)),
        "stats": stats,
        "dem": dem_stats,
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
