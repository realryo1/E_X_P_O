# -*- coding: utf-8 -*-
"""CityGMLの都市設備から「大屋根リング」をGLB化する。

3D Tiles ZIPには frn が含まれないため、同一データセットのCityGML frn を使う。
頂点は既存タイルと同じ ECEF相対→glTF Y-up に揃え、SCENE_GAMEのENU経路で置く。
CityGMLのテクスチャ座標は画像左下原点なので、glTF／DirectXの左上原点へ V を反転する。
"""

from __future__ import annotations

import argparse
import json
import math
import posixpath
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOL_DIR))

from expo_glb_util import (  # noqa: E402
    ecef_to_gltf_yup,
    geodetic_to_ecef,
    save_multi_primitive_glb,
    write_field_manifest,
)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CITYGML_ZIP = PROJECT_ROOT / "data_original" / "27999_osaka-shi_city_2025_citygml_1_op.zip"
FRN_MEMBERS = (
    "udx/frn/51357370_frn_6697_op.gml",
    "udx/frn/51357380_frn_6697_op.gml",
)
FEATURE_NAME = "大屋根リング"
LOD2_MANIFEST = PROJECT_ROOT / "asset" / "expomodel" / "expo_tiles_lod2.txt"
POLYGON_BLOCK = re.compile(
    r"<gml:Polygon\b[^>]*>.*?</gml:Polygon>",
    re.S,
)
EXTERIOR_RING = re.compile(
    r"<gml:exterior>\s*<gml:LinearRing(?P<attributes>[^>]*)>\s*"
    r"<gml:posList[^>]*>(?P<coordinates>[^<]+)</gml:posList>",
    re.S,
)
PARAMETERIZED_TEXTURE = re.compile(
    r"<app:ParameterizedTexture\b[^>]*>(.*?)</app:ParameterizedTexture>",
    re.S,
)
TEXTURE_TARGET = re.compile(
    r"<app:target\b[^>]*\buri=\"#(?P<polygon_id>[^\"]+)\"[^>]*>"
    r"(?P<body>.*?)</app:target>",
    re.S,
)
TEXTURE_COORDINATES = re.compile(
    r"<app:textureCoordinates\b[^>]*\bring=\"#(?P<ring_id>[^\"]+)\"[^>]*>"
    r"(?P<coordinates>[^<]+)</app:textureCoordinates>",
    re.S,
)
X3D_MATERIAL = re.compile(
    r"<app:X3DMaterial\b[^>]*>(.*?)</app:X3DMaterial>",
    re.S,
)
X3D_TARGET = re.compile(
    r"<app:target\b[^>]*>(?P<target>[^<]+)</app:target>",
    re.S,
)

Point = tuple[float, float, float]
UV = tuple[float, float]
Color = tuple[float, float, float, float]


@dataclass
class RingPolygon:
    polygon_id: str
    ring_id: str
    points: list[Point]
    texture_image: str | None = None
    texture_uvs: list[UV] | None = None
    material_color: Color | None = None


@dataclass
class RingFeature:
    gml_id: str
    polygons: list[RingPolygon]


@dataclass
class TextureAssignment:
    image_member: str
    uvs: list[UV]


@dataclass
class MeshGroup:
    key: tuple[object, ...]
    image_member: str | None
    base_color: Color
    ecef_points: list[Point]
    indices: list[int]
    uvs: list[UV] | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CityGML frn から大屋根リングGLBを作る")
    parser.add_argument("--citygml-zip", type=Path, default=CITYGML_ZIP)
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "data_converted" / "meshes")
    parser.add_argument("--runtime-output", type=Path, default=PROJECT_ROOT / "asset" / "expomodel" / "expo_ring.glb")
    return parser.parse_args()


def parse_pos_list(text: str) -> list[Point]:
    values = [float(v) for v in text.split()]
    if len(values) < 9 or len(values) % 3 != 0:
        return []
    points = [
        (values[i], values[i + 1], values[i + 2])
        for i in range(0, len(values), 3)
    ]
    if points[0] == points[-1]:
        points = points[:-1]
    return points


def parse_uv_list(text: str) -> list[UV]:
    values = [float(v) for v in text.split()]
    if len(values) < 6 or len(values) % 2 != 0:
        return []
    uvs = [(values[i], 1.0 - values[i + 1]) for i in range(0, len(values), 2)]
    if len(uvs) >= 2 and abs(uvs[0][0] - uvs[-1][0]) < 1e-7 and abs(
        uvs[0][1] - uvs[-1][1]
    ) < 1e-7:
        uvs = uvs[:-1]
    return uvs


def close_enough(a: Point, b: Point) -> bool:
    return abs(a[0] - b[0]) < 1e-12 and abs(a[1] - b[1]) < 1e-12 and abs(a[2] - b[2]) < 1e-7


def dedupe_ring(points: list[Point]) -> list[Point]:
    unique: list[Point] = []
    for point in points:
        if unique and close_enough(unique[-1], point):
            continue
        unique.append(point)
    if len(unique) >= 3 and close_enough(unique[0], unique[-1]):
        unique = unique[:-1]
    return unique


def dedupe_ring_with_uv(points: list[Point], uvs: list[UV]) -> tuple[list[Point], list[UV]]:
    unique_points: list[Point] = []
    unique_uvs: list[UV] = []
    for point, uv in zip(points, uvs):
        if unique_points and close_enough(unique_points[-1], point):
            continue
        unique_points.append(point)
        unique_uvs.append(uv)
    if len(unique_points) >= 3 and close_enough(unique_points[0], unique_points[-1]):
        unique_points.pop()
        unique_uvs.pop()
    return unique_points, unique_uvs


def vec_sub(a: Point, b: Point) -> Point:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_cross(a: Point, b: Point) -> Point:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vec_len(a: Point) -> float:
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def vec_dot(a: Point, b: Point) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def newell_normal(points: list[Point]) -> Point:
    nx = ny = nz = 0.0
    for i, point in enumerate(points):
        nxt = points[(i + 1) % len(points)]
        nx += (point[1] - nxt[1]) * (point[2] + nxt[2])
        ny += (point[2] - nxt[2]) * (point[0] + nxt[0])
        nz += (point[0] - nxt[0]) * (point[1] + nxt[1])
    length = vec_len((nx, ny, nz))
    if length < 1e-12:
        return (0.0, 0.0, 1.0)
    return (nx / length, ny / length, nz / length)


def make_basis(normal: Point) -> tuple[Point, Point]:
    axis = (1.0, 0.0, 0.0) if abs(normal[0]) < 0.9 else (0.0, 1.0, 0.0)
    tangent = vec_cross(normal, axis)
    tlen = vec_len(tangent)
    if tlen < 1e-12:
        tangent = vec_cross(normal, (0.0, 0.0, 1.0))
        tlen = vec_len(tangent)
    tangent = (tangent[0] / tlen, tangent[1] / tlen, tangent[2] / tlen)
    bitangent = vec_cross(normal, tangent)
    return tangent, bitangent


def project_ring(
    points: list[Point],
    origin: Point,
    tangent: Point,
    bitangent: Point,
) -> list[tuple[float, float]]:
    projected: list[tuple[float, float]] = []
    for point in points:
        delta = vec_sub(point, origin)
        projected.append((vec_dot(delta, tangent), vec_dot(delta, bitangent)))
    return projected


def area2(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def point_in_triangle(
    p: tuple[float, float],
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
) -> bool:
    a1 = area2(p, a, b)
    a2 = area2(p, b, c)
    a3 = area2(p, c, a)
    has_neg = (a1 < 0) or (a2 < 0) or (a3 < 0)
    has_pos = (a1 > 0) or (a2 > 0) or (a3 > 0)
    return not (has_neg and has_pos)


def ring_area(points: list[tuple[float, float]]) -> float:
    total = 0.0
    for i, point in enumerate(points):
        nxt = points[(i + 1) % len(points)]
        total += point[0] * nxt[1] - nxt[0] * point[1]
    return 0.5 * total


def earclip(points: list[tuple[float, float]]) -> list[int]:
    n = len(points)
    if n < 3:
        return []
    if n == 3:
        return [0, 1, 2]
    indices = list(range(n))
    if ring_area(points) < 0:
        indices.reverse()
    triangles: list[int] = []
    guard = 0
    while len(indices) > 3 and guard < n * n:
        guard += 1
        ear_found = False
        count = len(indices)
        for i in range(count):
            prev_i = indices[(i - 1) % count]
            cur_i = indices[i]
            next_i = indices[(i + 1) % count]
            a = points[prev_i]
            b = points[cur_i]
            c = points[next_i]
            if area2(a, b, c) <= 1e-18:
                continue
            has_point = False
            for other in indices:
                if other in (prev_i, cur_i, next_i):
                    continue
                if point_in_triangle(points[other], a, b, c):
                    has_point = True
                    break
            if has_point:
                continue
            triangles.extend([prev_i, cur_i, next_i])
            del indices[i]
            ear_found = True
            break
        if not ear_found:
            break
    if len(indices) == 3:
        triangles.extend(indices)
    return triangles


def fan_indices(count: int) -> list[int]:
    if count < 3:
        return []
    indices: list[int] = []
    for i in range(1, count - 1):
        indices.extend([0, i, i + 1])
    return indices


def is_mostly_convex(points: list[tuple[float, float]]) -> bool:
    pos = 0
    neg = 0
    count = len(points)
    for i in range(count):
        cross = area2(points[(i - 1) % count], points[i], points[(i + 1) % count])
        if cross > 1e-12:
            pos += 1
        elif cross < -1e-12:
            neg += 1
    return min(pos, neg) <= 2


def tessellate_ring(
    points: list[Point],
) -> tuple[list[Point], list[int]]:
    unique = dedupe_ring(points)
    if len(unique) < 3:
        return [], []
    if len(unique) <= 4:
        return unique, fan_indices(len(unique))
    origin = unique[0]
    tangent, bitangent = make_basis(newell_normal(unique))
    projected = project_ring(unique, origin, tangent, bitangent)
    if len(unique) > 24 and is_mostly_convex(projected):
        return unique, fan_indices(len(unique))
    indices = earclip(projected)
    if len(indices) < 3:
        indices = fan_indices(len(unique))
    return unique, indices


def tessellate_ring_with_uv(
    points: list[Point],
    uvs: list[UV],
) -> tuple[list[Point], list[int], list[UV]]:
    unique_points, unique_uvs = dedupe_ring_with_uv(points, uvs)
    if len(unique_points) < 3 or len(unique_points) != len(unique_uvs):
        return [], [], []
    unique, indices = tessellate_ring(unique_points)
    if len(unique) != len(unique_uvs):
        return [], [], []
    return unique, indices, unique_uvs


def resolve_image_member(gml_member: str, image_uri: str) -> str:
    gml_directory = posixpath.dirname(gml_member)
    return posixpath.normpath(
        posixpath.join(gml_directory, image_uri.strip().replace("\\", "/"))
    )


def extract_features(text: str) -> list[RingFeature]:
    needle = f"<gml:name>{FEATURE_NAME}</gml:name>"
    features: list[RingFeature] = []
    pos = 0
    while True:
        name_at = text.find(needle, pos)
        if name_at < 0:
            break
        start = text.rfind("<frn:CityFurniture", 0, name_at)
        end = text.find("</frn:CityFurniture>", name_at)
        if start < 0 or end < 0:
            pos = name_at + len(needle)
            continue
        block = text[start : end + len("</frn:CityFurniture>")]
        id_match = re.search(r'\bgml:id="([^"]+)"', block[:200])
        gml_id = id_match.group(1) if id_match else ""
        polygons: list[RingPolygon] = []
        for polygon_match in POLYGON_BLOCK.finditer(block):
            polygon_block = polygon_match.group(0)
            polygon_id_match = re.search(
                r'\bgml:id="([^"]+)"',
                polygon_block[:300],
            )
            polygon_id = polygon_id_match.group(1) if polygon_id_match else ""
            if not polygon_id:
                continue
            for exterior_index, exterior_match in enumerate(
                EXTERIOR_RING.finditer(polygon_block)
            ):
                points = parse_pos_list(exterior_match.group("coordinates"))
                if len(points) < 3:
                    continue
                ring_id_match = re.search(
                    r'\bgml:id="([^"]+)"',
                    exterior_match.group("attributes"),
                )
                ring_id = (
                    ring_id_match.group(1)
                    if ring_id_match
                    else f"{polygon_id}_{exterior_index}"
                )
                polygons.append(
                    RingPolygon(
                        polygon_id=polygon_id,
                        ring_id=ring_id,
                        points=points,
                    )
                )
        features.append(RingFeature(gml_id=gml_id, polygons=polygons))
        pos = end + 1
    return features


def parse_appearance(
    text: str,
    gml_member: str,
    polygon_ids: set[str],
) -> tuple[dict[str, TextureAssignment], dict[str, Color]]:
    texture_assignments: dict[str, TextureAssignment] = {}
    for texture_match in PARAMETERIZED_TEXTURE.finditer(text):
        texture_block = texture_match.group(1)
        image_match = re.search(
            r"<app:imageURI>\s*([^<]+?)\s*</app:imageURI>",
            texture_block,
        )
        if not image_match:
            continue
        image_member = resolve_image_member(gml_member, image_match.group(1))
        for target_match in TEXTURE_TARGET.finditer(texture_block):
            polygon_id = target_match.group("polygon_id")
            if polygon_id not in polygon_ids:
                continue
            for coordinates_match in TEXTURE_COORDINATES.finditer(
                target_match.group("body")
            ):
                uvs = parse_uv_list(coordinates_match.group("coordinates"))
                if uvs:
                    texture_assignments[coordinates_match.group("ring_id")] = (
                        TextureAssignment(image_member=image_member, uvs=uvs)
                    )

    material_colors: dict[str, Color] = {}
    for material_match in X3D_MATERIAL.finditer(text):
        material_block = material_match.group(1)
        color_match = re.search(
            r"<app:diffuseColor>\s*([^<]+?)\s*</app:diffuseColor>",
            material_block,
        )
        if not color_match:
            continue
        values = [float(value) for value in color_match.group(1).split()]
        if len(values) < 3:
            continue
        color: Color = (values[0], values[1], values[2], 1.0)
        for target_match in X3D_TARGET.finditer(material_block):
            polygon_id = target_match.group("target").strip().lstrip("#")
            if polygon_id in polygon_ids:
                material_colors[polygon_id] = color
    return texture_assignments, material_colors


def attach_appearance(
    features: list[RingFeature],
    text: str,
    gml_member: str,
) -> dict[str, int]:
    polygon_ids = {
        polygon.polygon_id
        for feature in features
        for polygon in feature.polygons
    }
    textures, colors = parse_appearance(text, gml_member, polygon_ids)
    textured_polygon_count = 0
    colored_polygon_count = 0
    missing_uv_count = 0
    for feature in features:
        for polygon in feature.polygons:
            assignment = textures.get(polygon.ring_id)
            if assignment is not None and len(assignment.uvs) == len(polygon.points):
                polygon.texture_image = assignment.image_member
                polygon.texture_uvs = assignment.uvs
                textured_polygon_count += 1
            else:
                if assignment is not None:
                    missing_uv_count += 1
                polygon.material_color = colors.get(polygon.polygon_id)
                colored_polygon_count += 1
    return {
        "textured_polygon_count": textured_polygon_count,
        "colored_polygon_count": colored_polygon_count,
        "missing_uv_count": missing_uv_count,
        "appearance_image_count": len(
            {item.image_member for item in textures.values()}
        ),
    }


def load_lod2_ref_rtc(path: Path) -> list[float] | None:
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("ref_rtc "):
            parts = line.split()
            return [float(parts[1]), float(parts[2]), float(parts[3])]
    return None


def bake_gltf_normals(
    ecef_points: list[Point],
    indices: list[int],
) -> list[tuple[float, float, float]]:
    acc = [(0.0, 0.0, 0.0) for _ in ecef_points]
    for i in range(0, len(indices), 3):
        ia, ib, ic = indices[i], indices[i + 1], indices[i + 2]
        a = ecef_points[ia]
        b = ecef_points[ib]
        c = ecef_points[ic]
        n = vec_cross(vec_sub(b, a), vec_sub(c, a))
        # ECEF相対 → glTF Y-up と同じ線形変換 (dx, dy, dz) -> (dx, dz, -dy)
        ng = (n[0], n[2], -n[1])
        for idx in (ia, ib, ic):
            cur = acc[idx]
            acc[idx] = (cur[0] + ng[0], cur[1] + ng[1], cur[2] + ng[2])
    normals: list[tuple[float, float, float]] = []
    for value in acc:
        length = vec_len(value)
        if length < 1e-12:
            normals.append((0.0, 1.0, 0.0))
        else:
            normals.append((value[0] / length, value[1] / length, value[2] / length))
    return normals


def build_mesh(
    features: list[RingFeature],
) -> tuple[
    list[dict[str, object]],
    list[dict[str, object]],
    list[str],
    list[float],
    dict[str, object],
]:
    groups: dict[tuple[object, ...], MeshGroup] = {}
    all_ecef_points: list[Point] = []
    skipped = 0
    polygon_count = 0
    max_ring = 0
    textured_polygon_count = 0
    colored_polygon_count = 0
    uv_fallback_count = 0

    for feature in features:
        for polygon in feature.polygons:
            unique_uvs: list[UV] | None = None
            if (
                polygon.texture_image is not None
                and polygon.texture_uvs is not None
                and len(polygon.texture_uvs) == len(polygon.points)
            ):
                unique, local, unique_uvs = tessellate_ring_with_uv(
                    polygon.points,
                    polygon.texture_uvs,
                )
            else:
                unique, local = tessellate_ring(polygon.points)
            if len(unique) < 3 or len(local) < 3:
                skipped += 1
                continue
            max_ring = max(max_ring, len(unique))

            image_member = (
                polygon.texture_image
                if unique_uvs is not None and len(unique_uvs) == len(unique)
                else None
            )
            if polygon.texture_image is not None and image_member is None:
                uv_fallback_count += 1
            if image_member is not None:
                key: tuple[object, ...] = ("texture", image_member)
                base_color: Color = (1.0, 1.0, 1.0, 1.0)
                textured_polygon_count += 1
            else:
                color = polygon.material_color or (0.62, 0.52, 0.40, 1.0)
                key = ("color", *color)
                base_color = color
                colored_polygon_count += 1

            group = groups.get(key)
            if group is None:
                group = MeshGroup(
                    key=key,
                    image_member=image_member,
                    base_color=base_color,
                    ecef_points=[],
                    indices=[],
                    uvs=[] if image_member is not None else None,
                )
                groups[key] = group
            base = len(group.ecef_points)
            ecef_points = [geodetic_to_ecef(*point) for point in unique]
            group.ecef_points.extend(ecef_points)
            group.indices.extend(base + index for index in local)
            if group.uvs is not None and unique_uvs is not None:
                group.uvs.extend(unique_uvs)
            all_ecef_points.extend(ecef_points)
            polygon_count += 1

    if not all_ecef_points:
        raise RuntimeError("大屋根リングの頂点を抽出できませんでした")

    rtc = [
        sum(point[index] for point in all_ecef_points)
        / len(all_ecef_points)
        for index in range(3)
    ]
    primitives: list[dict[str, object]] = []
    materials: list[dict[str, object]] = []
    image_members: list[str] = []
    total_vertex_count = 0
    total_triangle_count = 0
    for material_index, group in enumerate(groups.values()):
        positions = [
            ecef_to_gltf_yup(
                point[0] - rtc[0],
                point[1] - rtc[1],
                point[2] - rtc[2],
            )
            for point in group.ecef_points
        ]
        normals = bake_gltf_normals(group.ecef_points, group.indices)
        materials.append(
            {
                "name": (
                    "ring_texture_"
                    + posixpath.splitext(posixpath.basename(group.image_member))[0]
                    if group.image_member is not None
                    else "ring_color_"
                    + "_".join(f"{value:.6f}" for value in group.base_color[:3])
                ),
                "base_color": group.base_color,
                "image_key": group.image_member,
                "double_sided": True,
            }
        )
        primitives.append(
            {
                "positions": positions,
                "indices": group.indices,
                "normals": normals,
                "uvs": group.uvs,
                "material": material_index,
            }
        )
        if group.image_member is not None and group.image_member not in image_members:
            image_members.append(group.image_member)
        total_vertex_count += len(positions)
        total_triangle_count += len(group.indices) // 3

    stats = {
        "feature_count": len(features),
        "polygon_count": polygon_count,
        "skipped_polygons": skipped,
        "vertex_count": total_vertex_count,
        "triangle_count": total_triangle_count,
        "max_ring_vertices": max_ring,
        "gml_id_count": len(features),
        "textured_polygon_count": textured_polygon_count,
        "colored_polygon_count": colored_polygon_count,
        "uv_fallback_count": uv_fallback_count,
        "material_count": len(materials),
        "texture_image_count": len(image_members),
        "texture_images": image_members,
        "shading": "official CityGML appearance textures and X3DMaterial colors",
        "uv": "CityGML lower-left origin flipped to glTF/DirectX upper-left",
        "normals": "per-vertex from ECEF triangle normals in glTF Y-up",
    }
    return primitives, materials, image_members, rtc, stats


def main() -> int:
    args = parse_args()
    if not args.citygml_zip.is_file():
        print(f"CityGML ZIPがありません: {args.citygml_zip}")
        return 1

    features: list[RingFeature] = []
    appearance_stats = {
        "textured_polygon_count": 0,
        "colored_polygon_count": 0,
        "missing_uv_count": 0,
        "appearance_image_count": 0,
    }
    with zipfile.ZipFile(args.citygml_zip) as archive:
        for member in FRN_MEMBERS:
            print(f"reading {member}")
            text = archive.read(member).decode("utf-8-sig")
            found = extract_features(text)
            member_appearance_stats = attach_appearance(found, text, member)
            for key, value in member_appearance_stats.items():
                appearance_stats[key] += value
            poly_total = sum(len(feature.polygons) for feature in found)
            print(f"  {len(found)} CityFurniture / {poly_total} exteriors")
            features.extend(found)

    primitives, materials, image_members, rtc, stats = build_mesh(features)
    stats.update(appearance_stats)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    converted = args.output_dir / "expo_ring.glb"
    image_specs = []
    with zipfile.ZipFile(args.citygml_zip) as archive:
        for image_member in image_members:
            try:
                image_bytes = archive.read(image_member)
            except KeyError as error:
                raise RuntimeError(
                    f"リングのテクスチャ画像がZIPにありません: {image_member}"
                ) from error
            image_specs.append(
                {
                    "key": image_member,
                    "bytes": image_bytes,
                    "mime_type": "image/jpeg",
                }
            )
    save_multi_primitive_glb(
        converted,
        primitives,
        materials,
        image_specs,
    )
    args.runtime_output.parent.mkdir(parents=True, exist_ok=True)
    args.runtime_output.write_bytes(converted.read_bytes())

    ref_rtc = load_lod2_ref_rtc(LOD2_MANIFEST)
    metadata = {
        "name": FEATURE_NAME,
        "source": "CityGML frn + appearance (3D Tiles ZIPには frn なし)",
        "citygml_zip": str(args.citygml_zip.relative_to(PROJECT_ROOT)),
        "members": list(FRN_MEMBERS),
        "rtc_center": rtc,
        "lod2_ref_rtc": ref_rtc,
        "runtime_path": str(args.runtime_output.relative_to(PROJECT_ROOT)),
        "stats": stats,
        "note": "ParameterizedTextureの公式UV画像とX3DMaterialの単色を埋め込む",
    }
    meta_path = args.output_dir / "expo_ring.metadata.json"
    meta_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    write_field_manifest(
        "",
        rtc,
        str(args.runtime_output.relative_to(PROJECT_ROOT)).replace("/", "\\"),
        rtc,
    )
    from prepare_collision import export_collision_bin  # noqa: E402

    bin_path = export_collision_bin(args.runtime_output)
    print(
        f"features {stats['feature_count']} polys {stats['polygon_count']} "
        f"tris {stats['triangle_count']} max_ring {stats['max_ring_vertices']}"
    )
    print(
        f"textures {stats['texture_image_count']} materials {stats['material_count']} "
        f"textured_polys {stats['textured_polygon_count']} "
        f"colored_polys {stats['colored_polygon_count']}"
    )
    print(f"wrote {converted}")
    print(f"runtime {args.runtime_output}")
    if bin_path:
        print(f"collision {bin_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
