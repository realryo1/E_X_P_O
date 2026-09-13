# -*- coding: utf-8 -*-
"""最小GLB書き出しとGRS80測地→ECEF変換。"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path
from typing import Any

GLB_MAGIC = b"glTF"
GLB_JSON = 0x4E4F534A
GLB_BIN = 0x004E4942
GRS80_A = 6378137.0
GRS80_F = 1.0 / 298.257222101
GRS80_E2 = GRS80_F * (2.0 - GRS80_F)


def geodetic_to_ecef(lat_deg: float, lon_deg: float, height: float) -> tuple[float, float, float]:
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    sin_lat = math.sin(lat)
    cos_lat = math.cos(lat)
    sin_lon = math.sin(lon)
    cos_lon = math.cos(lon)
    n = GRS80_A / math.sqrt(1.0 - GRS80_E2 * sin_lat * sin_lat)
    x = (n + height) * cos_lat * cos_lon
    y = (n + height) * cos_lat * sin_lon
    z = (n * (1.0 - GRS80_E2) + height) * sin_lat
    return x, y, z


LOD2_MANIFEST = Path(__file__).resolve().parent.parent / "asset" / "expomodel" / "expo_tiles_lod2.txt"
FIELD_MANIFEST = Path(__file__).resolve().parent.parent / "asset" / "expomodel" / "expo_field.txt"


def read_ref_rtc(path: Path) -> list[float] | None:
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("ref_rtc "):
            parts = line.split()
            return [float(parts[1]), float(parts[2]), float(parts[3])]
    return None


def _rtc_parts(kind: str, path: str, rtc: list[float]) -> list[str]:
    return [
        kind,
        path,
        f"{rtc[0]:.15f}",
        f"{rtc[1]:.15f}",
        f"{rtc[2]:.15f}",
    ]


def write_field_manifest(
    floor_path: str,
    floor_rtc: list[float],
    ring_path: str,
    ring_rtc: list[float],
    pavilions: list[tuple[str, list[float]]] | None = None,
    lod2_far: list[tuple[str, list[float]]] | None = None,
    lod2_far_batches: list[tuple[str, str, int]] | None = None,
) -> None:
    ref = read_ref_rtc(LOD2_MANIFEST)
    if ref is None:
        ref = [
            (floor_rtc[0] + ring_rtc[0]) * 0.5,
            (floor_rtc[1] + ring_rtc[1]) * 0.5,
            (floor_rtc[2] + ring_rtc[2]) * 0.5,
        ]
    existing_floor = None
    existing_ring = None
    existing_lod2_far: list[list[str]] = []
    existing_lod2_far_batches: list[list[str]] = []
    existing_pavilions: list[list[str]] = []
    if FIELD_MANIFEST.is_file():
        for line in FIELD_MANIFEST.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if parts and parts[0] == "floor" and len(parts) >= 5:
                existing_floor = parts
            if parts and parts[0] == "ring" and len(parts) >= 5:
                existing_ring = parts
            if parts and parts[0] == "lod2far" and len(parts) >= 5:
                existing_lod2_far.append(parts)
            if parts and parts[0] == "lod2far_batch" and len(parts) >= 4:
                existing_lod2_far_batches.append(parts)
            if parts and parts[0] == "pavilion" and len(parts) >= 5:
                existing_pavilions.append(parts)
    if floor_path:
        existing_floor = _rtc_parts("floor", floor_path, floor_rtc)
    if ring_path:
        existing_ring = _rtc_parts("ring", ring_path, ring_rtc)
    if pavilions is not None:
        existing_pavilions = [_rtc_parts("pavilion", path, rtc) for path, rtc in pavilions]
    if lod2_far is not None:
        existing_lod2_far = [_rtc_parts("lod2far", path, rtc) for path, rtc in lod2_far]
    if lod2_far_batches is not None:
        existing_lod2_far_batches = [
            ["lod2far_batch", pavilion_path, far_path, str(batch_id)]
            for pavilion_path, far_path, batch_id in lod2_far_batches
        ]
    lines = [
        "EXPO_FIELD 1",
        f"ref_rtc {ref[0]:.15f} {ref[1]:.15f} {ref[2]:.15f}",
    ]
    if existing_floor:
        lines.append(" ".join(existing_floor))
    if existing_ring:
        lines.append(" ".join(existing_ring))
    for item in existing_lod2_far:
        lines.append(" ".join(item))
    for item in existing_pavilions:
        lines.append(" ".join(item))
    for item in existing_lod2_far_batches:
        lines.append(" ".join(item))
    FIELD_MANIFEST.write_text("\n".join(lines) + "\n", encoding="utf-8")
    converted = Path(__file__).resolve().parent.parent / "data_converted" / "meshes" / "expo_field.txt"
    converted.parent.mkdir(parents=True, exist_ok=True)
    converted.write_text("\n".join(lines) + "\n", encoding="utf-8")


def ecef_to_gltf_yup(dx: float, dy: float, dz: float) -> tuple[float, float, float]:
    """ECEF相対座標を、CesiumのY_UP_TO_Z_UP前提のglTF頂点へ入れる。"""
    return dx, dz, -dy


def gltf_yup_to_ecef(x: float, y: float, z: float) -> tuple[float, float, float]:
    return x, -z, y


def _vec_sub(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def _vec_add(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return a[0] + b[0], a[1] + b[1], a[2] + b[2]


def _vec_cross(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _vec_len(v: tuple[float, float, float]) -> float:
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def _vec_normalize(
    v: tuple[float, float, float],
    fallback: tuple[float, float, float] = (0.0, 1.0, 0.0),
) -> tuple[float, float, float]:
    length = _vec_len(v)
    if length < 1e-12:
        return fallback
    return v[0] / length, v[1] / length, v[2] / length


def bake_ecef_triangle_normals_gltf(
    ecef_points: list[tuple[float, float, float]],
    indices: list[int],
    outward_ecef: tuple[float, float, float] | None = None,
) -> list[tuple[float, float, float]]:
    """ECEF相対の三角形から、大屋根リングと同じ glTF Y-up 法線を積む。

    定数 (0,1,0) は ECEF 床では実行時 ENU 変換後に横を向き、太陽光が乗らない。
    """
    acc = [(0.0, 0.0, 0.0) for _ in ecef_points]
    for i in range(0, len(indices), 3):
        ia, ib, ic = indices[i], indices[i + 1], indices[i + 2]
        a = ecef_points[ia]
        b = ecef_points[ib]
        c = ecef_points[ic]
        n = _vec_cross(_vec_sub(b, a), _vec_sub(c, a))
        ng = ecef_to_gltf_yup(n[0], n[1], n[2])
        for idx in (ia, ib, ic):
            acc[idx] = _vec_add(acc[idx], ng)
    up = None
    if outward_ecef is not None:
        up = _vec_normalize(
            ecef_to_gltf_yup(outward_ecef[0], outward_ecef[1], outward_ecef[2]),
            fallback=(0.0, 0.0, 0.0),
        )
        if _vec_len(up) < 1e-12:
            up = None
    normals: list[tuple[float, float, float]] = []
    for value in acc:
        n = _vec_normalize(value)
        if up is not None and n[0] * up[0] + n[1] * up[1] + n[2] * up[2] < 0.0:
            n = (-n[0], -n[1], -n[2])
        normals.append(n)
    return normals


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"GLBが短すぎます: {path}")
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError(f"glTFバイナリではありません: {path}")
    offset = 12
    gltf: dict[str, Any] | None = None
    bin_blob = b""
    while offset + 8 <= min(length, len(data)):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_len]
        offset += chunk_len
        if chunk_type == GLB_JSON:
            gltf = json.loads(chunk.decode("utf-8"))
        elif chunk_type == GLB_BIN:
            bin_blob = chunk
    if gltf is None:
        raise ValueError(f"JSONチャンクがありません: {path}")
    return gltf, bin_blob


def _component_size(component_type: int) -> int:
    sizes = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    if component_type not in sizes:
        raise ValueError(f"未対応の componentType: {component_type}")
    return sizes[component_type]


def _type_count(accessor_type: str) -> int:
    counts = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    if accessor_type not in counts:
        raise ValueError(f"未対応の accessor type: {accessor_type}")
    return counts[accessor_type]


def _read_accessor(gltf: dict[str, Any], blob: bytes, accessor_index: int) -> list[tuple[float, ...]]:
    acc = gltf["accessors"][accessor_index]
    view = gltf["bufferViews"][acc["bufferView"]]
    offset = int(view.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
    count = int(acc["count"])
    ncomp = _type_count(str(acc["type"]))
    ctype = int(acc["componentType"])
    stride = int(view.get("byteStride", 0)) or (_component_size(ctype) * ncomp)
    fmt = {5123: "<H", 5125: "<I", 5126: "<f"}[ctype]
    values: list[tuple[float, ...]] = []
    for i in range(count):
        start = offset + i * stride
        comps = []
        for k in range(ncomp):
            (value,) = struct.unpack_from(fmt, blob, start + k * _component_size(ctype))
            comps.append(float(value))
        values.append(tuple(comps))
    return values


def rewrite_glb_normals_from_triangles(path: Path) -> bool:
    """POSITION と indices から ECEF 三角形法線を書き戻す。既存テクスチャは触らない。"""
    gltf, blob = read_glb(path)
    blob_arr = bytearray(blob)
    changed = False
    for mesh in gltf.get("meshes", []):
        for prim in mesh.get("primitives", []):
            attributes = prim.get("attributes", {})
            if "POSITION" not in attributes or "indices" not in prim:
                continue
            pos_acc = int(attributes["POSITION"])
            idx_acc = int(prim["indices"])
            positions_gltf = _read_accessor(gltf, blob, pos_acc)
            index_rows = _read_accessor(gltf, blob, idx_acc)
            positions_ecef = [gltf_yup_to_ecef(p[0], p[1], p[2]) for p in positions_gltf]
            indices = [int(row[0]) for row in index_rows]
            normals = bake_ecef_triangle_normals_gltf(positions_ecef, indices)
            nrm_bytes = b"".join(struct.pack("<fff", *n) for n in normals)
            if "NORMAL" in attributes:
                nacc = gltf["accessors"][int(attributes["NORMAL"])]
                view = gltf["bufferViews"][nacc["bufferView"]]
                offset = int(view.get("byteOffset", 0)) + int(nacc.get("byteOffset", 0))
                if int(nacc["count"]) != len(normals) or int(nacc["componentType"]) != 5126:
                    raise ValueError(f"NORMAL accessor を上書きできません: {path}")
                blob_arr[offset : offset + len(nrm_bytes)] = nrm_bytes
            else:
                blob = bytes(blob_arr)
                pad = (4 - (len(blob) % 4)) % 4
                if pad:
                    blob += b"\x00" * pad
                views = gltf.setdefault("bufferViews", [])
                view_index = len(views)
                views.append(
                    {
                        "buffer": 0,
                        "byteOffset": len(blob),
                        "byteLength": len(nrm_bytes),
                        "target": 34962,
                    }
                )
                blob += nrm_bytes
                accessors = gltf.setdefault("accessors", [])
                acc_index = len(accessors)
                accessors.append(
                    {
                        "bufferView": view_index,
                        "componentType": 5126,
                        "count": len(normals),
                        "type": "VEC3",
                    }
                )
                attributes["NORMAL"] = acc_index
                blob_arr = bytearray(blob)
            changed = True
    if not changed:
        return False
    buffers = gltf.setdefault("buffers", [{"byteLength": 0}])
    buffers[0]["byteLength"] = len(blob_arr)
    write_glb(path, gltf, bytes(blob_arr))
    return True


def patch_glb_constant_normal(
    path: Path,
    normal: tuple[float, float, float] = (0.0, 1.0, 0.0),
) -> bool:
    """NORMALが無いGLBへ定数法線を足す。既にある場合は何もしない。"""
    gltf, blob = read_glb(path)
    primitives = []
    for mesh in gltf.get("meshes", []):
        primitives.extend(mesh.get("primitives", []))
    if not primitives:
        return False
    if all("NORMAL" in prim.get("attributes", {}) for prim in primitives):
        return False
    pos_index = primitives[0]["attributes"]["POSITION"]
    count = int(gltf["accessors"][pos_index]["count"])
    nrm_bytes = b"".join(struct.pack("<fff", *normal) for _ in range(count))
    pad = (4 - (len(blob) % 4)) % 4
    if pad:
        blob += b"\x00" * pad
    views = gltf.setdefault("bufferViews", [])
    view_index = len(views)
    views.append(
        {
            "buffer": 0,
            "byteOffset": len(blob),
            "byteLength": len(nrm_bytes),
            "target": 34962,
        }
    )
    blob += nrm_bytes
    accessors = gltf.setdefault("accessors", [])
    acc_index = len(accessors)
    accessors.append(
        {
            "bufferView": view_index,
            "componentType": 5126,
            "count": count,
            "type": "VEC3",
        }
    )
    for prim in primitives:
        prim.setdefault("attributes", {})["NORMAL"] = acc_index
    buffers = gltf.setdefault("buffers", [{"byteLength": 0}])
    buffers[0]["byteLength"] = len(blob)
    write_glb(path, gltf, blob)
    return True


def write_glb(path: Path, gltf: dict[str, Any], bin_blob: bytes) -> None:
    json_bytes = json.dumps(gltf, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    json_pad = (4 - (len(json_bytes) % 4)) % 4
    json_bytes = json_bytes + (b" " * json_pad)
    bin_pad = (4 - (len(bin_blob) % 4)) % 4
    bin_bytes = bin_blob + (b"\x00" * bin_pad)

    length = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    with path.open("wb") as file:
        file.write(struct.pack("<4sII", GLB_MAGIC, 2, length))
        file.write(struct.pack("<II", len(json_bytes), GLB_JSON))
        file.write(json_bytes)
        file.write(struct.pack("<II", len(bin_bytes), GLB_BIN))
        file.write(bin_bytes)


def save_triangle_glb(
    path: Path,
    positions: list[tuple[float, float, float]],
    indices: list[int],
    uvs: list[tuple[float, float]] | None = None,
    colors: list[tuple[float, float, float, float]] | None = None,
    normals: list[tuple[float, float, float]] | None = None,
    image_bytes: bytes | None = None,
    image_mime: str = "image/jpeg",
    base_color: tuple[float, float, float, float] = (0.75, 0.68, 0.55, 1.0),
    double_sided: bool = True,
) -> None:
    pos_bytes = b"".join(struct.pack("<fff", *p) for p in positions)
    idx_bytes = b"".join(struct.pack("<I", i) for i in indices)
    chunks: list[bytes] = []
    views: list[dict[str, Any]] = []

    def push(blob: bytes, target: int | None) -> int:
        pad = (4 - (sum(len(c) for c in chunks) % 4)) % 4
        if pad:
            chunks.append(b"\x00" * pad)
        view_index = len(views)
        view: dict[str, Any] = {
            "buffer": 0,
            "byteOffset": sum(len(c) for c in chunks),
            "byteLength": len(blob),
        }
        if target is not None:
            view["target"] = target
        views.append(view)
        chunks.append(blob)
        return view_index

    pos_view = push(pos_bytes, 34962)
    idx_view = push(idx_bytes, 34963)
    color_view = (
        push(b"".join(struct.pack("<ffff", *c) for c in colors), 34962)
        if colors
        else None
    )
    nrm_view = (
        push(b"".join(struct.pack("<fff", *n) for n in normals), 34962)
        if normals
        else None
    )
    uv_view = push(b"".join(struct.pack("<ff", *uv) for uv in uvs), 34962) if uvs else None
    image_view = push(image_bytes, None) if image_bytes else None
    bin_blob = b"".join(chunks)

    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    accessors: list[dict[str, Any]] = [
        {
            "bufferView": pos_view,
            "componentType": 5126,
            "count": len(positions),
            "type": "VEC3",
            "min": [min(xs), min(ys), min(zs)],
            "max": [max(xs), max(ys), max(zs)],
        },
        {
            "bufferView": idx_view,
            "componentType": 5125,
            "count": len(indices),
            "type": "SCALAR",
        },
    ]
    attributes: dict[str, int] = {"POSITION": 0}
    if color_view is not None and colors is not None:
        accessors.append(
            {
                "bufferView": color_view,
                "componentType": 5126,
                "count": len(colors),
                "type": "VEC4",
            }
        )
        attributes["COLOR_0"] = len(accessors) - 1
    if nrm_view is not None and normals is not None:
        accessors.append(
            {
                "bufferView": nrm_view,
                "componentType": 5126,
                "count": len(normals),
                "type": "VEC3",
            }
        )
        attributes["NORMAL"] = len(accessors) - 1
    if uv_view is not None and uvs is not None:
        accessors.append(
            {
                "bufferView": uv_view,
                "componentType": 5126,
                "count": len(uvs),
                "type": "VEC2",
            }
        )
        attributes["TEXCOORD_0"] = len(accessors) - 1

    materials: list[dict[str, Any]] = [
        {
            "name": "expo",
            "pbrMetallicRoughness": {
                "baseColorFactor": list(base_color),
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": double_sided,
        }
    ]
    gltf: dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "expogame-prepare"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": attributes, "indices": 1, "material": 0}]}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_blob)}],
    }
    if image_view is not None:
        gltf["images"] = [{"bufferView": image_view, "mimeType": image_mime}]
        gltf["textures"] = [{"source": 0}]
        materials[0]["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
        materials[0]["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]

    path.parent.mkdir(parents=True, exist_ok=True)
    write_glb(path, gltf, bin_blob)


def save_multi_primitive_glb(
    path: Path,
    primitives: list[dict[str, Any]],
    material_specs: list[dict[str, Any]],
    image_specs: list[dict[str, Any]] | None = None,
) -> None:
    """複数プリミティブと埋め込み画像を持つGLBを書き出す。"""
    chunks: list[bytes] = []
    views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []

    def push(blob: bytes, target: int | None) -> int:
        pad = (4 - (sum(len(chunk) for chunk in chunks) % 4)) % 4
        if pad:
            chunks.append(b"\x00" * pad)
        view_index = len(views)
        view: dict[str, Any] = {
            "buffer": 0,
            "byteOffset": sum(len(chunk) for chunk in chunks),
            "byteLength": len(blob),
        }
        if target is not None:
            view["target"] = target
        views.append(view)
        chunks.append(blob)
        return view_index

    def add_accessor(
        view_index: int,
        count: int,
        value_type: str,
        component_type: int = 5126,
        minimum: list[float] | None = None,
        maximum: list[float] | None = None,
    ) -> int:
        accessor: dict[str, Any] = {
            "bufferView": view_index,
            "componentType": component_type,
            "count": count,
            "type": value_type,
        }
        if minimum is not None:
            accessor["min"] = minimum
        if maximum is not None:
            accessor["max"] = maximum
        accessors.append(accessor)
        return len(accessors) - 1

    primitive_json: list[dict[str, Any]] = []
    for primitive in primitives:
        positions = primitive["positions"]
        indices = primitive["indices"]
        if not positions or not indices:
            raise ValueError("空のプリミティブはGLBへ書き出せません")
        xs = [position[0] for position in positions]
        ys = [position[1] for position in positions]
        zs = [position[2] for position in positions]
        position_view = push(
            b"".join(struct.pack("<fff", *position) for position in positions),
            34962,
        )
        position_accessor = add_accessor(
            position_view,
            len(positions),
            "VEC3",
            minimum=[min(xs), min(ys), min(zs)],
            maximum=[max(xs), max(ys), max(zs)],
        )
        index_view = push(
            b"".join(struct.pack("<I", index) for index in indices),
            34963,
        )
        index_accessor = add_accessor(
            index_view,
            len(indices),
            "SCALAR",
            component_type=5125,
        )
        attributes: dict[str, int] = {"POSITION": position_accessor}
        normals = primitive.get("normals")
        if normals:
            normal_view = push(
                b"".join(struct.pack("<fff", *normal) for normal in normals),
                34962,
            )
            attributes["NORMAL"] = add_accessor(
                normal_view,
                len(normals),
                "VEC3",
            )
        uvs = primitive.get("uvs")
        if uvs:
            uv_view = push(
                b"".join(struct.pack("<ff", *uv) for uv in uvs),
                34962,
            )
            attributes["TEXCOORD_0"] = add_accessor(
                uv_view,
                len(uvs),
                "VEC2",
            )
        colors = primitive.get("colors")
        if colors:
            color_view = push(
                b"".join(struct.pack("<ffff", *color) for color in colors),
                34962,
            )
            attributes["COLOR_0"] = add_accessor(
                color_view,
                len(colors),
                "VEC4",
            )
        primitive_json.append(
            {
                "attributes": attributes,
                "indices": index_accessor,
                "material": int(primitive.get("material", 0)),
            }
        )

    images = image_specs or []
    image_index_by_key: dict[str, int] = {}
    image_json: list[dict[str, Any]] = []
    texture_json: list[dict[str, Any]] = []
    for image in images:
        key = str(image["key"])
        if key in image_index_by_key:
            continue
        image_view = push(image["bytes"], None)
        image_index = len(image_json)
        image_index_by_key[key] = image_index
        image_json.append(
            {
                "bufferView": image_view,
                "mimeType": str(image.get("mime_type", "image/jpeg")),
            }
        )
        texture_json.append({"source": image_index})

    materials: list[dict[str, Any]] = []
    for material in material_specs:
        base_color = material.get("base_color", (1.0, 1.0, 1.0, 1.0))
        output: dict[str, Any] = {
            "name": str(material.get("name", "material")),
            "pbrMetallicRoughness": {
                "baseColorFactor": list(base_color),
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": bool(material.get("double_sided", True)),
        }
        image_key = material.get("image_key")
        if image_key is not None:
            if str(image_key) not in image_index_by_key:
                raise ValueError(f"マテリアルの画像が見つかりません: {image_key}")
            output["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": image_index_by_key[str(image_key)]
            }
        materials.append(output)

    bin_blob = b"".join(chunks)
    gltf: dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "expogame-prepare"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": primitive_json}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_blob)}],
    }
    if image_json:
        gltf["images"] = image_json
        gltf["textures"] = texture_json
    path.parent.mkdir(parents=True, exist_ok=True)
    write_glb(path, gltf, bin_blob)
