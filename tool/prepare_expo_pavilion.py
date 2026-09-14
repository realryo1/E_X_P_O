# -*- coding: utf-8 -*-
"""LOD3の3D Tilesから名前付きパビリオンを切り出し、近景GLBにする。

CityGMLの無テクスチャ再構築は使わない。テクスチャ付きLOD3葉タイルの
Batch Tableと_BATCHIDで建物単位に残す。同じ建物のLOD2は葉タイルから全部除き、
LOD3の写真面で置き換える。未パンチLOD2からは除いたバッチだけをタイル単位の
遠景GLBへ分け、LOD3と排他表示できるメタデータも出力する。UVの無いLidar箱だけは捨てる。
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

TOOL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOL_DIR))

import prepare_expo_model as expo_model  # noqa: E402
from expo_glb_util import write_field_manifest, write_glb  # noqa: E402

PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOD3_TILESET = (
    PROJECT_ROOT
    / "data_original"
    / "3dtiles"
    / "27999_osaka-shi_city_2025_citygml_1_op_bldg_lod3"
    / "tileset.json"
)
DEFAULT_NAMES = ["null2", "チェコパビリオン"]
OUTER_LANDMARK_NAMES = [
    "電力館 可能性のタマゴたち",
    "迎賓館",
    "ウーマンズパビリオン",
    "パナソニックグループパビリオン「ノモの国」",
    "日本館",
    "JAPANマルシェ",
    "住友館",
    "三菱未来館",
]
OUTER_LANDMARK_FILE = "expo_pavilion_west_outer.glb"
RUNTIME_MODEL_SCALE = 0.2
OUTPUT_STEM = {
    "BLUE OCEAN DOME （ブルーオーシャン・ドーム）": "blue_ocean_dome",
    "Better Co Being": "better_co_being",
    "Dialogue Theater –いのちのあかし–": "dialogue_theater",
    "EARTH TABLE": "earth_table",
    "EARTH MART": "earth_mart",
    "EXPO アリーナ「Matsuri」": "expo_arena_matsuri",
    "EXPO ナショナルデーホール「レイガーデン」": "expo_national_day_hall",
    "EXPOホール「シャインハット」": "expo_hall_shine_hat",
    "EXPOメッセ「WASSE」": "expo_messe_wasse",
    "EXPO2025デジタルウオレットパーク": "digital_wallet_park",
    "GUNDAM NEXT FUTURE PAVILION": "gundam_next_future",
    "JAPANマルシェ": "japan_marche",
    "MANEKI FUTURE STUDIO JAPAN": "maneki_future_studio",
    "NTT Pavilion": "ntt",
    "ORA外食パビリオン『宴～UTAGE～』": "ora_utage",
    "PASONA NATUREVERSE": "pasona_natureverse",
    "TECH WORLD、テーマウィークスタジオ": "tech_world_theme_week",
    "UAEパビリオン": "uae",
    "eMover 東ゲート南停留所": "emover_south",
    "eMover東ゲート北停留所": "emover_north",
    "null2": "null2",
    "いのちの未来": "future_of_life",
    "いのちの遊び場クラゲ館": "jellyfish_playground",
    "いのちめぐる冒険": "adventure_of_life",
    "いのち動的平衡館": "dynamic_equilibrium",
    "よしもとwaraii myraii館": "yoshimoto_waraii_myraii",
    "らぽっぽファーム": "lapoppo_farm",
    "アイルランドパビリオン": "ireland",
    "アゼルバイジャンパビリオン": "azerbaijan",
    "アメリカパビリオン": "united_states",
    "アンゴラパビリオン": "angola",
    "イタリアパビリオン/バチカンパビリオン": "italy_vatican",
    "インドネシアパビリオン": "indonesia",
    "インドパビリオン": "india",
    "ウォータープラザマーケットプレイス東": "water_plaza_market_east",
    "ウォータープラザマーケットプレイス西": "water_plaza_market_west",
    "ウズベキスタンパビリオン": "uzbekistan",
    "ウーマンズパビリオン": "womens",
    "エジプトパビリオン、セネガルパビリオン、バングラデシュパビリオン": "egypt_senegal_bangladesh",
    "オマーンパビリオン": "oman",
    "オランダパビリオン": "netherlands",
    "オーストラリアパビリオン": "australia",
    "オーストリアパビリオン": "austria",
    "カタールパビリオン": "qatar",
    "カナダパビリオン": "canada",
    "ガスパビリオンおばけワンダーランド": "gas_ghost_wonderland",
    "ギャラリーWEST": "gallery_west",
    "クウェートパビリオン": "kuwait",
    "コモンズ-A": "commons_a",
    "コモンズ-B": "commons_b",
    "コモンズ-C": "commons_c",
    "コモンズ-D": "commons_d",
    "コモンズ-F": "commons_f",
    "コモンズE館": "commons_e",
    "コロンビアパビリオン": "colombia",
    "サウジアラビアパビリオン": "saudi_arabia",
    "サテライトスタジオ東": "satellite_studio_east",
    "サテライトスタジオ西": "satellite_studio_west",
    "シンガポールパビリオン": "singapore",
    "ジュニアSDGsキャンプ": "junior_sdgs_camp",
    "スイスパビリオン": "switzerland",
    "スシロー未来型万博店": "sushiro_future_store",
    "スペインパビリオン": "spain",
    "セルビア共和国パビリオン": "serbia",
    "タイパビリオン": "thailand",
    "チェコパビリオン": "czech",
    "ドイツパビリオン": "germany",
    "トルクメニスタンパビリオン": "turkmenistan",
    "トルコパビリオン": "turkey",
    "ネパールパビリオン": "nepal",
    "モナコパビリオン": "monaco",
    "ハンガリーパビリオン": "hungary",
    "バルト館（ラトビア共和国、リトアニア共和国）": "baltic",
    "バーレーンパビリオン": "bahrain",
    "パナソニックグループパビリオン「ノモの国」": "panasonic_nomo",
    "フィリピンパビリオン": "philippines",
    "フィーチャーライフヴィレッジ": "feature_life_village",
    "フェスティバル・ステーション": "festival_station",
    "フランスパビリオン": "france",
    "ブラジルパビリオン": "brazil",
    "ブルガリアパビリオン": "bulgaria",
    "ベトナムパビリオン、ロボット＆モビリティステーション、ミャクミャクハウス": "vietnam_etc",
    "ベルギーパビリオン": "belgium",
    "ペルーパビリオン、モザンビークパビリオン、ヨルダンパビリオン、空飛ぶクルマステーション": "peru_mozambique_jordan",
    "ポップアップステージ北": "popup_stage_north",
    "ポップアップステージ東外": "popup_stage_east_outer",
    "ポルトガルパビリオン": "portugal",
    "ポーランドパビリオン": "poland",
    "マルタパビリオン": "malta",
    "マレーシアパビリオン": "malaysia",
    "リングサイドマーケットプレイス東": "ringside_market_east",
    "リングサイドマーケットプレイス西": "ringside_market_west",
    "ルクセンブルクパビリオン": "luxembourg",
    "ルーマニアパビリオン": "romania",
    "万博サウナ「太陽のつぼみ」": "expo_sauna_sunbud",
    "三菱未来館": "mitsubishi",
    "中国パビリオン": "china",
    "住友館": "sumitomo",
    "免税センター": "tax_free_center",
    "北欧館（デンマーク王国、フィンランド共和国、アイスランド、ノルウェー王国、スウェーデン王国）": "nordic",
    "国際機関パビリオン、国際赤十字・赤新月運動館、国連パビリオン": "international_organizations",
    "夜の地球": "earth_at_night",
    "夜の地球EarthatNight": "earth_at_night_earth",
    "夢洲駅": "yumeshima_station",
    "大阪ヘルスケアパビリオン": "osaka_healthcare",
    "大阪万博関連施設": "expo_related",
    "日本館": "japan",
    "未来の都市": "future_city",
    "東ゲート": "east_gate",
    "東ゲートアクセシビリティセンター": "east_gate_accessibility",
    "東ゲートマーケットプレイス": "east_gate_market",
    "英国パビリオン": "united_kingdom",
    "西ゲート": "west_gate",
    "西ゲートアクセシビリティセンター": "west_gate_accessibility",
    "西ゲートマーケットプレイス": "west_gate_market",
    "迎賓館": "guest_house",
    "進歩の広場": "progress_square",
    "関西パビリオン": "kansai",
    "団体休憩所": "group_rest_area",
    "団体休憩所西": "group_rest_area_west",
    "電力館 可能性のタマゴたち": "electricity",
    "韓国パビリオン": "korea",
    "風の広場マーケットプレイス": "wind_plaza_market",
    "飯田グループ×大阪公立大学共同出展館": "iida_osaka_metropolitan",
}
FILTER_GLB = TOOL_DIR / "filter_glb_batches.cjs"
LOD2_CONVERTED = PROJECT_ROOT / "data_converted" / "meshes"

COMPONENT_SIZE = {
    5120: 1,
    5121: 1,
    5122: 2,
    5123: 2,
    5125: 4,
    5126: 4,
}
TYPE_COUNT = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}
PACK_FORMAT = {
    5120: "b",
    5121: "B",
    5122: "h",
    5123: "H",
    5125: "I",
    5126: "f",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="LOD3 3D Tilesから近景パビリオンGLBを作る")
    parser.add_argument("--tileset", type=Path, default=LOD3_TILESET)
    parser.add_argument("--name", nargs="+", default=DEFAULT_NAMES)
    parser.add_argument(
        "--all-names",
        action="store_true",
        help="葉タイルの全ての非空gml:nameを切り出す",
    )
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "data_converted" / "meshes")
    parser.add_argument("--output-name", default=None, help="1件だけのとき出力ファイル名を固定する")
    parser.add_argument(
        "--runtime-dir",
        type=Path,
        default=PROJECT_ROOT / "asset" / "expomodel",
    )
    parser.add_argument("--node-command", default="node")
    parser.add_argument("--keep-draco", action="store_true")
    return parser.parse_args()


def pavilion_file_name(name: str, match_index: int = 0) -> str:
    stem = OUTPUT_STEM.get(name)
    if not stem:
        raise KeyError(
            f"OUTPUT_STEMに英単語名がありません: {name!r}。"
            "自動連番や日本語名へのフォールバックは行いません。"
        )
    suffix = ".glb"
    if match_index == 0:
        return f"expo_pavilion_{stem}{suffix}"
    return f"expo_pavilion_{stem}_{match_index}{suffix}"


def lod2_far_file_name(stem: str) -> str:
    prefix = "expo_tile_lod2_"
    suffix = stem[len(prefix):] if stem.startswith(prefix) else stem
    return f"expo_tile_lod2_far_{suffix}.glb"


def split_glb(data: bytes) -> tuple[dict[str, Any], bytes]:
    gltf = expo_model.read_glb_json(data)
    offset = 12
    bin_chunk = b""
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        start = offset + 8
        end = start + chunk_len
        if end > len(data):
            break
        if chunk_type == 0x004E4942:
            bin_chunk = data[start:end]
        offset = end
    return gltf, bin_chunk


def component_int(component_type: int, values: tuple[Any, ...]) -> int:
    if component_type == 5126:
        return int(round(float(values[0])))
    return int(values[0])


def read_accessor_values(gltf: dict[str, Any], bin_blob: bytes, accessor_index: int) -> list[tuple[Any, ...]]:
    accessor = gltf["accessors"][accessor_index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    buffer_offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = int(accessor["count"])
    ncomp = TYPE_COUNT[str(accessor["type"])]
    ctype = int(accessor["componentType"])
    size = COMPONENT_SIZE[ctype]
    stride = int(view.get("byteStride") or size * ncomp)
    fmt = "<" + PACK_FORMAT[ctype] * ncomp
    rows: list[tuple[Any, ...]] = []
    for i in range(count):
        start = buffer_offset + i * stride
        rows.append(struct.unpack_from(fmt, bin_blob, start))
    return rows


def pack_rows(rows: list[tuple[Any, ...]], component_type: int) -> bytes:
    fmt = "<" + PACK_FORMAT[component_type] * len(rows[0])
    return b"".join(struct.pack(fmt, *row) for row in rows)


def min_max_float_rows(rows: list[tuple[Any, ...]]) -> tuple[list[float], list[float]]:
    mins = [float(v) for v in rows[0]]
    maxs = [float(v) for v in rows[0]]
    for row in rows[1:]:
        for i, value in enumerate(row):
            mins[i] = min(mins[i], float(value))
            maxs[i] = max(maxs[i], float(value))
    return mins, maxs


def read_indices(gltf: dict[str, Any], bin_blob: bytes, accessor_index: int) -> list[int]:
    rows = read_accessor_values(gltf, bin_blob, accessor_index)
    return [int(row[0]) for row in rows]


def glb_position_bounds(glb: bytes) -> tuple[list[float], list[float]]:
    gltf, bin_blob = split_glb(glb)
    mins = [float("inf")] * 3
    maxs = [float("-inf")] * 3
    for mesh in gltf.get("meshes") or []:
        for primitive in mesh.get("primitives") or []:
            position_accessor = (primitive.get("attributes") or {}).get("POSITION")
            if position_accessor is None:
                continue
            for row in read_accessor_values(gltf, bin_blob, int(position_accessor)):
                for axis, value in enumerate(row[:3]):
                    mins[axis] = min(mins[axis], float(value))
                    maxs[axis] = max(maxs[axis], float(value))
    if not all(math.isfinite(value) for value in mins + maxs):
        raise RuntimeError("切り出しGLBのPOSITION境界を取得できません")
    return mins, maxs


def batch_id_attr(attributes: dict[str, Any]) -> str | None:
    for key in ("_BATCHID", "_FEATURE_ID_0", "FEATURE_ID_0"):
        if key in attributes:
            return key
    return None


def filter_primitive(
    gltf: dict[str, Any],
    bin_blob: bytes,
    primitive: dict[str, Any],
    keep_ids: set[int],
) -> dict[str, Any] | None:
    attributes = primitive.get("attributes")
    if not isinstance(attributes, dict):
        return None
    attr_name = batch_id_attr(attributes)
    if attr_name is None:
        return None
    batch_accessor = int(attributes[attr_name])
    batch_rows = read_accessor_values(gltf, bin_blob, batch_accessor)
    ctype = int(gltf["accessors"][batch_accessor]["componentType"])
    vertex_ids = [component_int(ctype, row) for row in batch_rows]
    if "indices" not in primitive:
        used = [i for i, batch_id in enumerate(vertex_ids) if batch_id in keep_ids]
        if not used:
            return None
        new_indices = list(range(len(used)))
    else:
        if int(primitive.get("mode", 4)) != 4:
            return None
        indices = read_indices(gltf, bin_blob, int(primitive["indices"]))
        kept: list[int] = []
        for i in range(0, len(indices) - 2, 3):
            a, b, c = indices[i], indices[i + 1], indices[i + 2]
            if vertex_ids[a] in keep_ids or vertex_ids[b] in keep_ids or vertex_ids[c] in keep_ids:
                kept.extend((a, b, c))
        if not kept:
            return None
        used = sorted(set(kept))
        remap = {old: new for new, old in enumerate(used)}
        new_indices = [remap[i] for i in kept]

    packed_attrs: dict[str, dict[str, Any]] = {}
    for name, accessor_index in attributes.items():
        if name.startswith("_"):
            continue
        accessor = gltf["accessors"][int(accessor_index)]
        rows = read_accessor_values(gltf, bin_blob, int(accessor_index))
        packed_attrs[name] = {
            "componentType": int(accessor["componentType"]),
            "type": accessor["type"],
            "normalized": bool(accessor.get("normalized", False)),
            "rows": [rows[i] for i in used],
        }
    if "TEXCOORD_0" not in packed_attrs:
        return None
    return {
        "material": primitive.get("material"),
        "attributes": packed_attrs,
        "indices": new_indices,
        "indexComponentType": 5123 if len(used) <= 65535 else 5125,
    }


def copy_images(gltf: dict[str, Any], bin_blob: bytes) -> list[dict[str, Any]]:
    images: list[dict[str, Any]] = []
    for image in gltf.get("images") or []:
        if not isinstance(image, dict):
            continue
        copied = {key: value for key, value in image.items() if key != "bufferView"}
        if "bufferView" in image:
            view = gltf["bufferViews"][int(image["bufferView"])]
            start = int(view.get("byteOffset", 0))
            copied["_blob"] = bin_blob[start : start + int(view["byteLength"])]
        images.append(copied)
    return images


def unique_keep_order(values: list[int]) -> list[int]:
    seen: set[int] = set()
    ordered: list[int] = []
    for value in values:
        if value not in seen:
            seen.add(value)
            ordered.append(value)
    return ordered


def texture_index_of(material: dict[str, Any]) -> int | None:
    pbr = material.get("pbrMetallicRoughness")
    if not isinstance(pbr, dict):
        return None
    tex = pbr.get("baseColorTexture")
    if not isinstance(tex, dict) or "index" not in tex:
        return None
    return int(tex["index"])


def remap_used_materials(
    gltf_src: dict[str, Any],
    primitives: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[int], list[dict[str, Any]]]:
    old_mats = list(gltf_src.get("materials") or [])
    old_texs = list(gltf_src.get("textures") or [])
    old_samps = list(gltf_src.get("samplers") or [])
    used_mat_ids = unique_keep_order(
        [int(p["material"]) for p in primitives if p.get("material") is not None]
    )
    used_tex_ids: list[int] = []
    for mat_id in used_mat_ids:
        if 0 <= mat_id < len(old_mats):
            tex_id = texture_index_of(old_mats[mat_id])
            if tex_id is not None:
                used_tex_ids.append(tex_id)
    used_tex_ids = unique_keep_order(used_tex_ids)
    used_img_ids: list[int] = []
    used_samp_ids: list[int] = []
    for tex_id in used_tex_ids:
        if 0 <= tex_id < len(old_texs):
            tex = old_texs[tex_id]
            if "source" in tex:
                used_img_ids.append(int(tex["source"]))
            if "sampler" in tex:
                used_samp_ids.append(int(tex["sampler"]))
    used_img_ids = unique_keep_order(used_img_ids)
    used_samp_ids = unique_keep_order(used_samp_ids)
    mat_map = {old: new for new, old in enumerate(used_mat_ids)}
    tex_map = {old: new for new, old in enumerate(used_tex_ids)}
    img_map = {old: new for new, old in enumerate(used_img_ids)}
    samp_map = {old: new for new, old in enumerate(used_samp_ids)}

    new_mats: list[dict[str, Any]] = []
    for mat_id in used_mat_ids:
        mat = json.loads(json.dumps(old_mats[mat_id]))
        tex_id = texture_index_of(mat)
        if tex_id is not None and tex_id in tex_map:
            mat["pbrMetallicRoughness"]["baseColorTexture"]["index"] = tex_map[tex_id]
        new_mats.append(mat)
    new_texs: list[dict[str, Any]] = []
    for tex_id in used_tex_ids:
        tex = dict(old_texs[tex_id])
        if "source" in tex:
            tex["source"] = img_map[int(tex["source"])]
        if "sampler" in tex:
            tex["sampler"] = samp_map[int(tex["sampler"])]
        new_texs.append(tex)
    new_samps = [dict(old_samps[i]) for i in used_samp_ids if 0 <= i < len(old_samps)]
    for primitive in primitives:
        if primitive.get("material") is not None:
            primitive["material"] = mat_map[int(primitive["material"])]
    return new_mats, new_texs, used_img_ids, new_samps


def rebuild_glb(gltf_src: dict[str, Any], bin_blob: bytes, primitives: list[dict[str, Any]]) -> tuple[dict[str, Any], bytes]:
    primitives = [p for p in primitives if "TEXCOORD_0" in p["attributes"]]
    if not primitives:
        raise RuntimeError("UV付きプリミティブがありません")
    new_mats, new_texs, used_img_ids, new_samps = remap_used_materials(gltf_src, primitives)
    src_images = copy_images(gltf_src, bin_blob)

    chunks: list[bytes] = []
    views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []
    out_prims: list[dict[str, Any]] = []

    def push(blob: bytes, target: int | None) -> int:
        pad = (4 - (sum(len(item) for item in chunks) % 4)) % 4
        if pad:
            chunks.append(b"\x00" * pad)
        view: dict[str, Any] = {
            "buffer": 0,
            "byteOffset": sum(len(item) for item in chunks),
            "byteLength": len(blob),
        }
        if target is not None:
            view["target"] = target
        views.append(view)
        chunks.append(blob)
        return len(views) - 1

    images_out: list[dict[str, Any]] = []
    for img_id in used_img_ids:
        image = src_images[img_id]
        out = {key: value for key, value in image.items() if key != "_blob"}
        if "_blob" in image:
            out["bufferView"] = push(image["_blob"], None)
        images_out.append(out)

    for primitive in primitives:
        attributes: dict[str, int] = {}
        for name, payload in primitive["attributes"].items():
            rows = payload["rows"]
            view_index = push(pack_rows(rows, payload["componentType"]), 34962)
            accessor: dict[str, Any] = {
                "bufferView": view_index,
                "componentType": payload["componentType"],
                "count": len(rows),
                "type": payload["type"],
            }
            if payload["normalized"]:
                accessor["normalized"] = True
            if payload["componentType"] == 5126:
                mins, maxs = min_max_float_rows(rows)
                accessor["min"] = mins
                accessor["max"] = maxs
            attributes[name] = len(accessors)
            accessors.append(accessor)
        index_fmt = "<H" if primitive["indexComponentType"] == 5123 else "<I"
        index_view = push(
            b"".join(struct.pack(index_fmt, value) for value in primitive["indices"]),
            34963,
        )
        index_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": index_view,
                "componentType": primitive["indexComponentType"],
                "count": len(primitive["indices"]),
                "type": "SCALAR",
            }
        )
        out_prim: dict[str, Any] = {
            "attributes": attributes,
            "indices": index_accessor,
            "mode": 4,
        }
        if primitive.get("material") is not None:
            out_prim["material"] = primitive["material"]
        out_prims.append(out_prim)

    bin_out = b"".join(chunks)
    gltf: dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "expogame-prepare-pavilion"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": out_prims}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_out)}],
        "materials": new_mats,
        "textures": new_texs,
        "images": images_out,
    }
    if new_samps:
        gltf["samplers"] = new_samps
    return gltf, bin_out


def extract_filtered(
    glb: bytes,
    keep_ids: set[int],
) -> tuple[list[dict[str, Any]] | None, dict[str, Any], bytes, str]:
    gltf, bin_blob = split_glb(glb)
    primitives: list[dict[str, Any]] = []
    saw_batch = False
    for mesh in gltf.get("meshes") or []:
        for primitive in mesh.get("primitives") or []:
            if batch_id_attr(primitive.get("attributes") or {}):
                saw_batch = True
            filtered = filter_primitive(gltf, bin_blob, primitive, keep_ids)
            if filtered is not None:
                primitives.append(filtered)
    if not saw_batch:
        return None, gltf, bin_blob, "missing_batch_id"
    if not primitives:
        return None, gltf, bin_blob, "no_matching_triangles"
    return primitives, gltf, bin_blob, "filtered"


def batch_names(batch_table: dict[str, Any]) -> list[str]:
    names = batch_table.get("gml:name")
    if not isinstance(names, list):
        return []
    return ["" if item is None else str(item) for item in names]


def matching_batch_ids(batch_table: dict[str, Any], name: str) -> list[int]:
    return [index for index, value in enumerate(batch_names(batch_table)) if value == name]


def run_batch_filter(
    input_glb: Path,
    output_glb: Path,
    node_command: str,
    keep: list[int] | None = None,
    drop: list[int] | None = None,
    drop_no_uv: bool = False,
    drop_untextured_only: bool = False,
    split_batch: bool = False,
) -> None:
    if not FILTER_GLB.is_file():
        raise RuntimeError(f"filter_glb_batches.cjs がありません: {FILTER_GLB}")
    command = [node_command, str(FILTER_GLB), str(input_glb), str(output_glb)]
    if keep:
        command.extend(["--keep", ",".join(str(value) for value in keep)])
    if drop:
        command.extend(["--drop", ",".join(str(value) for value in drop)])
    if drop_no_uv:
        command.append("--drop-no-uv")
    if drop_untextured_only:
        command.append("--drop-untextured-only")
    if split_batch:
        command.append("--split-batch")
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout).strip()
        raise RuntimeError(details or "filter_glb_batches.cjs が失敗しました")


def strip_names_from_lod2(names: list[str], node_command: str) -> list[dict[str, Any]]:
    """LOD2のパンチ済みタイルと、穴埋め遠景タイルを生成する。"""
    notes: list[dict[str, Any]] = []
    for meta_path in sorted(LOD2_CONVERTED.glob("expo_tile_lod2_data*.metadata.json")):
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        batch_table = (meta.get("extracted") or {}).get("batch_table") or {}
        if not isinstance(batch_table, dict):
            continue
        dropped: dict[str, list[int]] = {}
        ids: list[int] = []
        for name in names:
            found = matching_batch_ids(batch_table, name)
            if found:
                dropped[name] = found
                ids.extend(found)
        ids = unique_keep_order(ids)
        if not ids:
            continue
        stem = meta_path.name.replace(".metadata.json", "")
        source_glb = LOD2_CONVERTED / f"{stem}.glb"
        runtime_glb = PROJECT_ROOT / "asset" / "expomodel" / f"{stem}.glb"
        far_name = lod2_far_file_name(stem)
        far_converted_glb = LOD2_CONVERTED / far_name
        far_runtime_glb = PROJECT_ROOT / "asset" / "expomodel" / far_name
        if not source_glb.is_file():
            print(f"LOD2原本がありません: {source_glb}")
            continue
        run_batch_filter(
            source_glb,
            runtime_glb,
            node_command,
            drop=ids,
        )
        run_batch_filter(
            source_glb,
            far_converted_glb,
            node_command,
            keep=ids,
            split_batch=True,
        )
        far_runtime_glb.write_bytes(far_converted_glb.read_bytes())
        rtc = expo_model.parse_rtc_center(
            (meta.get("extracted") or {}).get("rtc_center")
        ) or [0.0, 0.0, 0.0]
        notes.append(
            {
                "lod2": stem,
                "dropped_batch_ids": ids,
                "dropped_by_name": dropped,
                "runtime": str(runtime_glb),
                "far_runtime": str(far_runtime_glb.relative_to(PROJECT_ROOT)).replace(
                    "/", "\\"
                ),
                "far_rtc_center": rtc,
            }
        )
        print(
            f"LOD2 {stem} から batches {ids} を削除し、"
            f"遠景 {far_name} へ保持 {dropped}"
        )
    return notes


def scan_named_leaves(
    tileset: Path,
    names: list[str],
) -> dict[str, list[tuple[expo_model.TileContent, dict[str, Any], list[int]]]]:
    scan = expo_model.ScanResult()
    expo_model.walk_tileset(tileset, tileset.parent, scan, set())
    leaves = [
        item
        for item in expo_model.mesh_candidates(scan.contents)
        if isinstance(item.geometric_error, (int, float)) and item.geometric_error == 0
    ]
    found: dict[str, list[tuple[expo_model.TileContent, dict[str, Any], list[int]]]] = {
        name: [] for name in names
    }
    for item in leaves:
        _glb, metadata = expo_model.read_content(item.path)
        batch_table = metadata.get("batch_table")
        if not isinstance(batch_table, dict):
            continue
        for name in names:
            ids = matching_batch_ids(batch_table, name)
            if ids:
                found[name].append((item, metadata, ids))
    return found


def scan_all_named_leaves(
    tileset: Path,
) -> tuple[list[str], dict[str, list[tuple[expo_model.TileContent, dict[str, Any], list[int]]]]]:
    scan = expo_model.ScanResult()
    expo_model.walk_tileset(tileset, tileset.parent, scan, set())
    leaves = [
        item
        for item in expo_model.mesh_candidates(scan.contents)
        if isinstance(item.geometric_error, (int, float)) and item.geometric_error == 0
    ]
    names: list[str] = []
    found: dict[str, list[tuple[expo_model.TileContent, dict[str, Any], list[int]]]] = {}
    for item in leaves:
        _glb, metadata = expo_model.read_content(item.path)
        batch_table = metadata.get("batch_table")
        if not isinstance(batch_table, dict):
            continue
        for name in batch_names(batch_table):
            if not name:
                continue
            if name not in found:
                names.append(name)
                found[name] = []
            if not found[name] or found[name][-1][0].path != item.path:
                ids = matching_batch_ids(batch_table, name)
                if ids:
                    found[name].append((item, metadata, ids))
    return names, found


def main() -> int:
    args = parse_args()
    if not args.tileset.is_file():
        print(f"tilesetがありません: {args.tileset}")
        return 1

    if args.all_names:
        names, found = scan_all_named_leaves(args.tileset)
    else:
        names = list(args.name)
        found = scan_named_leaves(args.tileset, names)
    missing_stems = [name for name in names if name not in OUTPUT_STEM]
    if missing_stems:
        print("OUTPUT_STEMに英単語名がありません: " + ", ".join(missing_stems))
        return 1
    missing = [name for name in names if not found[name]]
    if missing:
        print("LOD3葉タイルに見つかりません: " + ", ".join(missing))
        return 1

    notes: list[dict[str, Any]] = []
    written: list[
        tuple[str, list[float], str, str, str, list[float] | None, float]
    ] = []
    args.output_dir.mkdir(parents=True, exist_ok=True)
    runtime_dir = args.runtime_dir
    runtime_dir.mkdir(parents=True, exist_ok=True)
    decoded_cache: dict[Path, tuple[bytes, list[float]]] = {}
    lod2_stripped = strip_names_from_lod2(names, args.node_command)

    cluster_names = [name for name in OUTER_LANDMARK_NAMES if name in found]
    cluster_candidate = found[cluster_names[0]][0] if cluster_names else None
    cluster_ids: list[int] = []
    if cluster_candidate:
        for name in cluster_names:
            for candidate, _metadata, ids in found[name]:
                if candidate.path == cluster_candidate[0].path:
                    cluster_ids.extend(ids)
        cluster_ids = unique_keep_order(cluster_ids)

    for name in names:
        if name in cluster_names:
            continue
        for match_index, (candidate, metadata, ids) in enumerate(found[name]):
            if candidate.path not in decoded_cache:
                glb, _content_metadata = expo_model.convert_tile(
                    candidate, args.node_command, args.keep_draco
                )
                rtc = expo_model.parse_rtc_center(metadata.get("rtc_center")) or [0.0, 0.0, 0.0]
                decoded_cache[candidate.path] = (glb, rtc)
            glb, rtc = decoded_cache[candidate.path]
            if args.output_name and len(names) == 1:
                stem = Path(args.output_name).stem
                suffix = Path(args.output_name).suffix or ".glb"
                file_name = args.output_name if match_index == 0 else f"{stem}_{match_index}{suffix}"
            else:
                file_name = pavilion_file_name(name, match_index)
            output_path = args.output_dir / file_name
            try:
                with tempfile.TemporaryDirectory(prefix="expo_pav_") as temp_dir:
                    decoded = Path(temp_dir) / "decoded.glb"
                    decoded.write_bytes(glb)
                    run_batch_filter(
                        decoded,
                        output_path,
                        args.node_command,
                        keep=ids,
                        drop_no_uv=True,
                    )
            except (OSError, RuntimeError) as exc:
                notes.append(
                    {
                        "name": name,
                        "source": str(expo_model.project_path(candidate.path)),
                        "batch_ids": ids,
                        "rtc_center": rtc,
                        "reason": "failed",
                        "error": str(exc),
                    }
                )
                print(f"{name} {candidate.path.name} の切り出しをスキップ: {exc}")
                continue
            filter_mode = "node_keep_uv"
            note = {
                "name": name,
                "source": str(expo_model.project_path(candidate.path)),
                "batch_ids": ids,
                "rtc_center": rtc,
                "reason": "filtered",
            }
            notes.append(note)
            print(f"{name} {candidate.path.name} batches {ids} -> {filter_mode}")
            runtime_path = runtime_dir / file_name
            runtime_path.write_bytes(output_path.read_bytes())
            runtime_rel = str(runtime_path.relative_to(PROJECT_ROOT)).replace("/", "\\")
            written.append((runtime_rel, rtc, filter_mode, file_name, name, None, 0.0))
            print(f"wrote {output_path}")
            print(f"runtime {runtime_path}")

    if cluster_candidate and cluster_ids:
        candidate, metadata, _ids = cluster_candidate
        if candidate.path not in decoded_cache:
            glb, _content_metadata = expo_model.convert_tile(
                candidate, args.node_command, args.keep_draco
            )
            rtc = expo_model.parse_rtc_center(metadata.get("rtc_center")) or [0.0, 0.0, 0.0]
            decoded_cache[candidate.path] = (glb, rtc)
        glb, rtc = decoded_cache[candidate.path]
        output_path = args.output_dir / OUTER_LANDMARK_FILE
        try:
            with tempfile.TemporaryDirectory(prefix="expo_pav_cluster_") as temp_dir:
                decoded = Path(temp_dir) / "decoded.glb"
                decoded.write_bytes(glb)
                run_batch_filter(
                    decoded,
                    output_path,
                    args.node_command,
                    keep=cluster_ids,
                    drop_no_uv=True,
                )
        except (OSError, RuntimeError) as exc:
            print(f"外周ランドマーク統合GLBの切り出しをスキップ: {exc}")
        else:
            runtime_path = runtime_dir / OUTER_LANDMARK_FILE
            runtime_path.write_bytes(output_path.read_bytes())
            mins, maxs = glb_position_bounds(output_path.read_bytes())
            center_yup = [(mins[i] + maxs[i]) * 0.5 for i in range(3)]
            stream_center = [
                rtc[0] + center_yup[0],
                rtc[1] - center_yup[2],
                rtc[2] + center_yup[1],
            ]
            stream_radius = (
                0.5
                * math.sqrt(
                    (maxs[0] - mins[0]) ** 2 + (maxs[2] - mins[2]) ** 2
                )
                * RUNTIME_MODEL_SCALE
            )
            runtime_rel = str(runtime_path.relative_to(PROJECT_ROOT)).replace("/", "\\")
            written.append(
                (
                    runtime_rel,
                    rtc,
                    "node_keep_uv_cluster",
                    OUTER_LANDMARK_FILE,
                    "__outer_landmarks__",
                    stream_center,
                    stream_radius,
                )
            )
            notes.append(
                {
                    "name": "__outer_landmarks__",
                    "source": str(expo_model.project_path(candidate.path)),
                    "batch_ids": cluster_ids,
                    "names": cluster_names,
                    "rtc_center": rtc,
                    "stream_center": stream_center,
                    "stream_radius": stream_radius,
                    "reason": "filtered_cluster",
                }
            )
            print(
                f"外周ランドマーク {cluster_names} batches {cluster_ids} "
                f"-> {OUTER_LANDMARK_FILE}"
            )

    if not written:
        print("一致するバッチを切り出せませんでした")
        return 1

    first_rtc = written[0][1]
    lod2_far = [
        (entry["far_runtime"], entry["far_rtc_center"])
        for entry in lod2_stripped
        if entry.get("far_runtime")
    ]
    lod2_far_batches: list[tuple[str, str, int]] = []
    for runtime_path, _rtc, _mode, _file_name, name, _stream_center, _stream_radius in written:
        for entry in lod2_stripped:
            for batch_id in (entry.get("dropped_by_name") or {}).get(name, []):
                lod2_far_batches.append(
                    (runtime_path, entry["far_runtime"], int(batch_id))
                )
    cluster_runtime = next(
        (item[0] for item in written if item[4] == "__outer_landmarks__"),
        None,
    )
    if cluster_runtime:
        for entry in lod2_stripped:
            for name in cluster_names:
                for batch_id in (entry.get("dropped_by_name") or {}).get(name, []):
                    lod2_far_batches.append(
                        (cluster_runtime, entry["far_runtime"], int(batch_id))
                    )

    metadata_path = args.output_dir / "expo_pavilion.metadata.json"
    metadata = {
        "names": names,
        "source": "3D Tiles bldg LOD3 (textured). CityGML bldgは使わない",
        "tileset": str(args.tileset.relative_to(PROJECT_ROOT)),
        "runtime_paths": [item[0] for item in written],
        "rtc_centers": [item[1] for item in written],
        "filter": [item[2] for item in written],
        "matches": notes,
        "note": "LOD2実行時タイルから同名建物を面ごと除き、LOD3の写真付き本体で置き換える",
        "lod2_stripped": lod2_stripped,
        "lod2_far": lod2_far,
        "lod2_far_batches": lod2_far_batches,
    }
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_field_manifest(
        "",
        first_rtc,
        "",
        first_rtc,
        pavilions=[
            (path, rtc, stream_center, stream_radius)
            for path, rtc, _mode, _name, _name_key, stream_center, stream_radius in written
        ],
        lod2_far=lod2_far,
        lod2_far_batches=lod2_far_batches,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
