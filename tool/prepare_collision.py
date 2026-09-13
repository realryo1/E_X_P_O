# -*- coding: utf-8 -*-
"""描画GLBから衝突用バイナリ（EXCL）を書き出す。

実行時は Assimp を使わず fread する。座標は glTF そのまま（メートル、右手 Y-up）。
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path
from typing import Any

TOOL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOL_DIR))

from expo_glb_util import read_glb  # noqa: E402

PROJECT_ROOT = Path(__file__).resolve().parent.parent
COLLISION_DIR = PROJECT_ROOT / "asset" / "collision"
DEFAULT_GLBS = (
    PROJECT_ROOT / "asset" / "expomodel" / "expo_ring.glb",
    PROJECT_ROOT / "asset" / "expomodel" / "expo_floor.glb",
)

MAGIC = b"EXCL"
VERSION = 1
COMPONENT_BYTES = {
    5120: 1,
    5121: 1,
    5122: 2,
    5123: 2,
    5125: 4,
    5126: 4,
}
TYPE_COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GLBから衝突バイナリを作る")
    parser.add_argument("glb", nargs="*", type=Path, help="入力GLB。省略時はリングと床")
    parser.add_argument("--output-dir", type=Path, default=COLLISION_DIR)
    return parser.parse_args()


def collision_path_for_glb(glb_path: Path, output_dir: Path | None = None) -> Path:
    dest = output_dir if output_dir is not None else COLLISION_DIR
    return dest / (glb_path.stem + ".bin")


def mat4_identity() -> list[float]:
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def mat4_mul(a: list[float], b: list[float]) -> list[float]:
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = (
                a[0 * 4 + row] * b[col * 4 + 0]
                + a[1 * 4 + row] * b[col * 4 + 1]
                + a[2 * 4 + row] * b[col * 4 + 2]
                + a[3 * 4 + row] * b[col * 4 + 3]
            )
    return out


def mat4_from_trs(
    translation: list[float] | None,
    rotation: list[float] | None,
    scale: list[float] | None,
) -> list[float]:
    tx, ty, tz = translation if translation else (0.0, 0.0, 0.0)
    sx, sy, sz = scale if scale else (1.0, 1.0, 1.0)
    qx, qy, qz, qw = rotation if rotation else (0.0, 0.0, 0.0, 1.0)
    n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if n > 1e-12:
        qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
    xx, yy, zz = qx * qx, qy * qy, qz * qz
    xy, xz, yz = qx * qy, qx * qz, qy * qz
    wx, wy, wz = qw * qx, qw * qy, qw * qz
    return [
        (1.0 - 2.0 * (yy + zz)) * sx, (2.0 * (xy + wz)) * sx, (2.0 * (xz - wy)) * sx, 0.0,
        (2.0 * (xy - wz)) * sy, (1.0 - 2.0 * (xx + zz)) * sy, (2.0 * (yz + wx)) * sy, 0.0,
        (2.0 * (xz + wy)) * sz, (2.0 * (yz - wx)) * sz, (1.0 - 2.0 * (xx + yy)) * sz, 0.0,
        tx, ty, tz, 1.0,
    ]


def node_matrix(node: dict[str, Any]) -> list[float]:
    if "matrix" in node:
        values = [float(v) for v in node["matrix"]]
        if len(values) == 16:
            return values
    return mat4_from_trs(
        node.get("translation"),
        node.get("rotation"),
        node.get("scale"),
    )


def transform_point(m: list[float], x: float, y: float, z: float) -> tuple[float, float, float]:
    return (
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    )


def unpack_values(gltf: dict[str, Any], blob: bytes, index: int) -> list[tuple[float, ...]]:
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    comp = int(acc["componentType"])
    type_name = str(acc["type"])
    count = int(acc["count"])
    comps = TYPE_COMPONENTS[type_name]
    elem_size = COMPONENT_BYTES[comp] * comps
    offset = int(view.get("byteOffset", 0)) + int(acc.get("byteOffset", 0))
    stride = int(view.get("byteStride", elem_size))
    fmt = {
        5120: "b",
        5121: "B",
        5122: "h",
        5123: "H",
        5125: "I",
        5126: "f",
    }[comp]
    out: list[tuple[float, ...]] = []
    for i in range(count):
        start = offset + i * stride
        values = struct.unpack_from("<" + fmt * comps, blob, start)
        out.append(tuple(float(v) for v in values))
    return out


def unpack_indices(gltf: dict[str, Any], blob: bytes, index: int) -> list[int]:
    values = unpack_values(gltf, blob, index)
    return [int(v[0]) for v in values]


def extract_mesh(
    gltf: dict[str, Any],
    blob: bytes,
) -> tuple[list[tuple[float, float, float]], list[int]]:
    nodes = gltf.get("nodes", [])
    meshes = gltf.get("meshes", [])
    scenes = gltf.get("scenes", [])
    scene_index = int(gltf.get("scene", 0))
    roots = scenes[scene_index]["nodes"] if scenes else list(range(len(nodes)))

    positions: list[tuple[float, float, float]] = []
    indices: list[int] = []

    def visit(node_index: int, parent: list[float]) -> None:
        node = nodes[node_index]
        world = mat4_mul(parent, node_matrix(node))
        if "mesh" in node:
            mesh = meshes[int(node["mesh"])]
            for prim in mesh.get("primitives", []):
                mode = int(prim.get("mode", 4))
                if mode != 4:
                    continue
                attrs = prim.get("attributes", {})
                if "POSITION" not in attrs:
                    continue
                pos_vals = unpack_values(gltf, blob, int(attrs["POSITION"]))
                base = len(positions)
                for px, py, pz, *rest in pos_vals:
                    positions.append(transform_point(world, px, py, pz))
                if "indices" in prim:
                    local = unpack_indices(gltf, blob, int(prim["indices"]))
                else:
                    local = list(range(len(pos_vals)))
                if len(local) % 3 != 0:
                    continue
                indices.extend(base + i for i in local)
        for child in node.get("children", []):
            visit(int(child), world)

    identity = mat4_identity()
    for root in roots:
        visit(int(root), identity)
    return positions, indices


def write_collision_bin(
    path: Path,
    positions: list[tuple[float, float, float]],
    indices: list[int],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    vertex_count = len(positions)
    triangle_count = len(indices) // 3
    with path.open("wb") as file:
        file.write(struct.pack("<4sIIII", MAGIC, VERSION, vertex_count, triangle_count, 0))
        for x, y, z in positions:
            file.write(struct.pack("<fff", x, y, z))
        for index in indices[: triangle_count * 3]:
            file.write(struct.pack("<I", index))


def export_collision_bin(glb_path: Path, output_dir: Path | None = None) -> Path | None:
    glb_path = Path(glb_path)
    if not glb_path.is_file():
        print(f"GLBがありません: {glb_path}")
        return None
    gltf, blob = read_glb(glb_path)
    positions, indices = extract_mesh(gltf, blob)
    if not positions or len(indices) < 3:
        print(f"三角形がありません: {glb_path}")
        return None
    dest = collision_path_for_glb(glb_path, output_dir)
    write_collision_bin(dest, positions, indices)
    print(f"{glb_path.name}: verts {len(positions)} tris {len(indices) // 3} -> {dest}")
    return dest


def main() -> int:
    args = parse_args()
    targets = [Path(p) for p in args.glb] if args.glb else list(DEFAULT_GLBS)
    wrote = 0
    for glb in targets:
        if export_collision_bin(glb, args.output_dir) is not None:
            wrote += 1
    return 0 if wrote else 1


if __name__ == "__main__":
    raise SystemExit(main())
