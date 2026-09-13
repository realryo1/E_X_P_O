# -*- coding: utf-8 -*-
"""PLATEAU 3D Tilesから最初の描画用GLBを準備するツール。

公式データを実行時にダウンロードする機能は持たない。
tileset.jsonを再帰走査し、参照切れを検出したうえで、最初のb3dm/glbを
既存のAssimp経路で扱えるローカルGLBとして取り出す。
配布されているDraco圧縮メッシュは、同梱のNode.js補助ツールで非圧縮化する。

使用例:
    python tool/prepare_expo_model.py ^
      --tileset data_original/3dtiles/<LOD1ディレクトリ>/tileset.json

出力:
    data_converted/meshes/expo_tile.glb
    data_converted/meshes/expo_tile.metadata.json
    asset/expomodel/expo_tile.glb

LOD2の例:
    python tool/prepare_expo_model.py --tileset data_original/3dtiles/<LOD2ディレクトリ>/tileset.json --output-name expo_tile_lod2.glb --runtime-output asset/expomodel/expo_tile_lod2.glb

LOD2の隣接葉タイル（親タイルとのREPLACE重複を避ける）:
    python tool/prepare_expo_model.py --tileset data_original/3dtiles/<LOD2ディレクトリ>/tileset.json --output-name expo_tile_lod2.glb --runtime-output asset/expomodel/expo_tile_lod2.glb --max-tiles 4 --leaf-only
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import unquote, urlsplit


PROJECT_ROOT = Path(__file__).resolve().parent.parent
TOOL_DIR = Path(__file__).resolve().parent
B3DM_HEADER = struct.Struct("<4s6I")
GLB_HEADER = struct.Struct("<4sII")
GLB_CHUNK_HEADER = struct.Struct("<II")
GLB_JSON_CHUNK = 0x4E4F534A


class ExpoModelError(RuntimeError):
    """入力データが想定する3D Tiles形式でない場合のエラー。"""


@dataclass
class TileContent:
    """tileset内で見つかったcontentの情報。"""

    source_tileset: Path
    path: Path
    reference: str
    geometric_error: float | None
    bounding_volume: dict[str, Any] | None
    transform_chain: list[list[float]] = field(default_factory=list)


@dataclass
class ScanResult:
    """tileset走査結果。"""

    contents: list[TileContent] = field(default_factory=list)
    missing: list[dict[str, str]] = field(default_factory=list)
    unsupported: list[dict[str, str]] = field(default_factory=list)
    invalid_tilesets: list[dict[str, str]] = field(default_factory=list)
    visited_tilesets: list[Path] = field(default_factory=list)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="3D Tilesを走査し、最初のb3dm/glbをGLBとして抽出します。"
    )
    parser.add_argument(
        "--tileset",
        type=Path,
        required=True,
        help="走査するルートtileset.json",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=PROJECT_ROOT / "data_converted" / "meshes",
        help="変換GLBとメタデータの出力先",
    )
    parser.add_argument(
        "--output-name",
        default="expo_tile.glb",
        help="変換GLBのファイル名",
    )
    parser.add_argument(
        "--runtime-output",
        type=Path,
        default=PROJECT_ROOT / "asset" / "expomodel" / "expo_tile.glb",
        help="SCENE_GAMEから読む実行時GLBのコピー先",
    )
    parser.add_argument(
        "--no-runtime-copy",
        action="store_true",
        help="asset/expomodelへの実行時コピーを行わない",
    )
    parser.add_argument(
        "--keep-draco",
        action="store_true",
        help="Draco圧縮を解除せずに出力する（Assimp非対応環境では読み込めない）",
    )
    parser.add_argument(
        "--node-command",
        default="node",
        help="Draco解除に使うNode.jsコマンド",
    )
    parser.add_argument(
        "--max-tiles",
        type=int,
        default=1,
        help="変換するタイル数。1なら従来どおり最初のcontentのみ",
    )
    parser.add_argument(
        "--leaf-only",
        action="store_true",
        help="geometricErrorが0の葉タイルだけを選ぶ（親contentとの重ね表示を避ける）",
    )
    return parser.parse_args()


def project_path(path: Path) -> Path:
    """表示用にプロジェクト相対パスを返す。"""
    try:
        return path.resolve().relative_to(PROJECT_ROOT.resolve())
    except ValueError:
        return path


def read_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except OSError as exc:
        raise ExpoModelError(f"JSONを読み込めません: {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ExpoModelError(f"JSONの形式が不正です: {path}: {exc}") from exc


def decode_json_region(data: bytes, label: str) -> dict[str, Any]:
    trimmed = data.rstrip(b" \t\r\n\x00")
    if not trimmed:
        return {}
    try:
        value = json.loads(trimmed.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ExpoModelError(f"{label}のJSONを読み込めません: {exc}") from exc
    if not isinstance(value, dict):
        raise ExpoModelError(f"{label}のJSONがオブジェクトではありません")
    return value


def clean_reference(reference: str) -> str:
    """URL形式でない相対参照からクエリ・フラグメントを除去する。"""
    parsed = urlsplit(reference)
    if parsed.scheme or parsed.netloc:
        raise ExpoModelError(f"HTTP等の外部参照には対応していません: {reference}")
    return unquote(parsed.path)


def safe_resolve(reference: str, base_dir: Path, source_root: Path) -> Path:
    cleaned = clean_reference(reference)
    if not cleaned:
        raise ExpoModelError("空のcontent参照です")

    resolved = (base_dir / Path(cleaned)).resolve()
    try:
        resolved.relative_to(source_root.resolve())
    except ValueError as exc:
        raise ExpoModelError(
            f"source_root外を参照しています: {reference} -> {resolved}"
        ) from exc
    return resolved


def transform_chain(value: Any, parent: list[list[float]]) -> list[list[float]]:
    if not isinstance(value, list) or len(value) != 16:
        return parent.copy()
    try:
        matrix = [float(item) for item in value]
    except (TypeError, ValueError):
        return parent.copy()
    return parent + [matrix]


def content_reference(content: Any) -> str | None:
    if isinstance(content, str):
        return content
    if not isinstance(content, dict):
        return None
    for key in ("uri", "url"):
        value = content.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def iter_contents(
    node: Any,
    source_tileset: Path,
    source_root: Path,
    inherited_transform: list[list[float]],
    result: ScanResult,
    visited_tilesets: set[Path],
    depth: int,
) -> Iterable[TileContent]:
    if not isinstance(node, dict):
        return

    current_transform = transform_chain(
        node.get("transform"), inherited_transform
    )
    geometric_error = node.get("geometricError")
    if not isinstance(geometric_error, (int, float)):
        geometric_error = None
    bounding_volume = node.get("boundingVolume")
    if not isinstance(bounding_volume, dict):
        bounding_volume = None

    contents: list[Any] = []
    if "content" in node:
        contents.append(node["content"])
    if isinstance(node.get("contents"), list):
        contents.extend(node["contents"])

    for content in contents:
        reference = content_reference(content)
        if reference is None:
            result.unsupported.append(
                {
                    "source": str(project_path(source_tileset)),
                    "reason": "content.uri/content.urlがありません",
                }
            )
            continue

        try:
            content_path = safe_resolve(
                reference, source_tileset.parent, source_root
            )
        except ExpoModelError as exc:
            result.unsupported.append(
                {
                    "source": str(project_path(source_tileset)),
                    "reference": reference,
                    "reason": str(exc),
                }
            )
            continue

        if not content_path.is_file():
            result.missing.append(
                {
                    "source": str(project_path(source_tileset)),
                    "reference": reference,
                    "resolved": str(project_path(content_path)),
                }
            )
            continue

        suffix = content_path.suffix.lower()
        if suffix == ".json":
            if content_path not in visited_tilesets:
                walk_tileset(
                    content_path,
                    source_root,
                    result,
                    visited_tilesets,
                    inherited_transform=current_transform,
                    depth=depth + 1,
                )
            continue

        if suffix in {".b3dm", ".glb", ".gltf"}:
            item = TileContent(
                source_tileset=source_tileset,
                path=content_path,
                reference=reference,
                geometric_error=geometric_error,
                bounding_volume=bounding_volume,
                transform_chain=current_transform,
            )
            result.contents.append(item)
            yield item
            continue

        result.unsupported.append(
            {
                "source": str(project_path(source_tileset)),
                "reference": reference,
                "resolved": str(project_path(content_path)),
                "reason": f"未対応のcontent拡張子: {suffix or '<なし>'}",
            }
        )

    children = node.get("children")
    if isinstance(children, list):
        for child in children:
            yield from iter_contents(
                child,
                source_tileset,
                source_root,
                current_transform,
                result,
                visited_tilesets,
                depth,
            )


def walk_tileset(
    path: Path,
    source_root: Path,
    result: ScanResult,
    visited_tilesets: set[Path],
    inherited_transform: list[list[float]] | None = None,
    depth: int = 0,
) -> None:
    if depth > 64:
        result.invalid_tilesets.append(
            {
                "path": str(project_path(path)),
                "reason": "tilesetの階層が深すぎます（最大64）",
            }
        )
        return

    resolved = path.resolve()
    if resolved in visited_tilesets:
        return
    visited_tilesets.add(resolved)
    result.visited_tilesets.append(resolved)

    try:
        tileset = read_json(resolved)
    except ExpoModelError as exc:
        result.invalid_tilesets.append(
            {"path": str(project_path(resolved)), "reason": str(exc)}
        )
        return

    asset = tileset.get("asset")
    if not isinstance(asset, dict) or not asset.get("version"):
        result.invalid_tilesets.append(
            {
                "path": str(project_path(resolved)),
                "reason": "asset.versionがありません",
            }
        )

    root = tileset.get("root")
    if not isinstance(root, dict):
        result.invalid_tilesets.append(
            {
                "path": str(project_path(resolved)),
                "reason": "rootがありません",
            }
        )
        return

    list(
        iter_contents(
            root,
            resolved,
            source_root,
            inherited_transform or [],
            result,
            visited_tilesets,
            depth,
        )
    )


def read_glb_json(glb: bytes) -> dict[str, Any]:
    if len(glb) < GLB_HEADER.size:
        raise ExpoModelError("GLBヘッダーが短すぎます")

    magic, version, declared_length = GLB_HEADER.unpack_from(glb, 0)
    if magic != b"glTF":
        raise ExpoModelError(f"GLB magicが不正です: {magic!r}")
    if version != 2:
        raise ExpoModelError(f"未対応のGLBバージョンです: {version}")
    if declared_length > len(glb):
        raise ExpoModelError(
            f"GLBの宣言サイズが実サイズを超えています: "
            f"{declared_length} > {len(glb)}"
        )

    chunk_offset = GLB_HEADER.size
    if chunk_offset + GLB_CHUNK_HEADER.size > len(glb):
        raise ExpoModelError("GLB JSONチャンクヘッダーがありません")
    chunk_length, chunk_type = GLB_CHUNK_HEADER.unpack_from(glb, chunk_offset)
    if chunk_type != GLB_JSON_CHUNK:
        raise ExpoModelError("GLBの先頭チャンクがJSONではありません")
    json_start = chunk_offset + GLB_CHUNK_HEADER.size
    json_end = json_start + chunk_length
    if json_end > len(glb):
        raise ExpoModelError("GLB JSONチャンクが実サイズを超えています")
    return decode_json_region(glb[json_start:json_end], "GLB JSON")


def extract_b3dm(path: Path) -> tuple[bytes, dict[str, Any]]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ExpoModelError(f"b3dmを読み込めません: {path}: {exc}") from exc

    if len(data) < B3DM_HEADER.size:
        raise ExpoModelError("b3dmヘッダーが短すぎます")
    (
        magic,
        version,
        byte_length,
        feature_json_length,
        feature_binary_length,
        batch_json_length,
        batch_binary_length,
    ) = B3DM_HEADER.unpack_from(data, 0)

    if magic != b"b3dm":
        raise ExpoModelError(f"b3dm magicが不正です: {magic!r}")
    if version != 1:
        raise ExpoModelError(f"未対応のb3dmバージョンです: {version}")
    if byte_length > len(data):
        raise ExpoModelError(
            f"b3dmの宣言サイズが実サイズを超えています: "
            f"{byte_length} > {len(data)}"
        )

    feature_start = B3DM_HEADER.size
    feature_end = feature_start + feature_json_length
    binary_end = feature_end + feature_binary_length
    batch_end = binary_end + batch_json_length
    payload_start = batch_end + batch_binary_length
    if payload_start > len(data):
        raise ExpoModelError("b3dmのテーブル長が実サイズを超えています")

    feature_table = decode_json_region(
        data[feature_start:feature_end], "Feature Table"
    )
    batch_table = decode_json_region(
        data[binary_end:batch_end], "Batch Table"
    )
    glb = data[payload_start:byte_length]
    gltf = read_glb_json(glb)

    extensions = gltf.get("extensions")
    rtc_center = None
    if isinstance(extensions, dict):
        cesium_rtc = extensions.get("CESIUM_RTC")
        if isinstance(cesium_rtc, dict):
            rtc_center = cesium_rtc.get("center")

    metadata = {
        "container": {
            "format": "b3dm",
            "version": version,
            "byte_length": byte_length,
            "feature_table_json_length": feature_json_length,
            "feature_table_binary_length": feature_binary_length,
            "batch_table_json_length": batch_json_length,
            "batch_table_binary_length": batch_binary_length,
        },
        "feature_table": feature_table,
        "batch_table": batch_table,
        "gltf": {
            "asset": gltf.get("asset"),
            "extensions_used": gltf.get("extensionsUsed", []),
            "extensions_required": gltf.get("extensionsRequired", []),
            "mesh_count": len(gltf.get("meshes", [])),
            "node_count": len(gltf.get("nodes", [])),
            "material_count": len(gltf.get("materials", [])),
            "image_count": len(gltf.get("images", [])),
        },
        "rtc_center": rtc_center,
    }
    return glb, metadata


def read_content(path: Path) -> tuple[bytes, dict[str, Any]]:
    suffix = path.suffix.lower()
    if suffix == ".b3dm":
        return extract_b3dm(path)
    if suffix == ".glb":
        data = path.read_bytes()
        gltf = read_glb_json(data)
        return data, {
            "container": {"format": "glb"},
            "gltf": {
                "asset": gltf.get("asset"),
                "extensions_used": gltf.get("extensionsUsed", []),
                "extensions_required": gltf.get("extensionsRequired", []),
                "mesh_count": len(gltf.get("meshes", [])),
                "node_count": len(gltf.get("nodes", [])),
                "material_count": len(gltf.get("materials", [])),
                "image_count": len(gltf.get("images", [])),
            },
            "rtc_center": (
                gltf.get("extensions", {})
                .get("CESIUM_RTC", {})
                .get("center")
                if isinstance(gltf.get("extensions"), dict)
                else None
            ),
        }
    raise ExpoModelError(
        f"{path} は直接GLBへ変換できません。b3dmまたはglbを指定してください"
    )


def gltf_summary(gltf: dict[str, Any]) -> dict[str, Any]:
    return {
        "asset": gltf.get("asset"),
        "extensions_used": gltf.get("extensionsUsed", []),
        "extensions_required": gltf.get("extensionsRequired", []),
        "mesh_count": len(gltf.get("meshes", [])),
        "node_count": len(gltf.get("nodes", [])),
        "material_count": len(gltf.get("materials", [])),
        "image_count": len(gltf.get("images", [])),
    }


def has_draco_extension(content_metadata: dict[str, Any]) -> bool:
    gltf = content_metadata.get("gltf")
    if not isinstance(gltf, dict):
        return False
    extension_names = set(gltf.get("extensions_used", []))
    extension_names.update(gltf.get("extensions_required", []))
    return "KHR_draco_mesh_compression" in extension_names


def decompress_draco(
    glb: bytes,
    content_metadata: dict[str, Any],
    node_command: str,
) -> bytes:
    """Node.js補助ツールでDracoを解除し、Assimp対応GLBを返す。"""
    helper = TOOL_DIR / "decode_draco_glb.cjs"
    if not helper.is_file():
        raise ExpoModelError(f"Draco解除ツールが見つかりません: {helper}")

    with tempfile.TemporaryDirectory(prefix="expo_draco_") as temp_dir:
        temp_root = Path(temp_dir)
        input_path = temp_root / "input.glb"
        output_path = temp_root / "output.glb"
        input_path.write_bytes(glb)
        try:
            completed = subprocess.run(
                [
                    node_command,
                    str(helper),
                    str(input_path),
                    str(output_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as exc:
            raise ExpoModelError(
                f"Node.jsを実行できません: {node_command}. "
                "Node.jsをインストールするか--keep-dracoを指定してください"
            ) from exc

        if completed.returncode != 0:
            details = (completed.stderr or completed.stdout).strip()
            raise ExpoModelError(
                "Draco圧縮の解除に失敗しました"
                + (f": {details}" if details else "")
                + "\n依存関係を `npm install --prefix tool` で準備してください"
            )
        if not output_path.is_file():
            raise ExpoModelError("Draco解除後のGLBが生成されませんでした")

        decoded = output_path.read_bytes()

    decoded_gltf = read_glb_json(decoded)
    content_metadata["runtime"] = {
        "draco_decompressed": True,
        "removed_extensions": [
            "CESIUM_RTC",
            "KHR_draco_mesh_compression",
            "EXT_texture_webp",
        ],
        "kept_embedded_images": True,
        "gltf": gltf_summary(decoded_gltf),
        "sha256": sha256(decoded),
    }
    return decoded


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def mesh_candidates(contents: list[TileContent]) -> list[TileContent]:
    return [
        item
        for item in contents
        if item.path.suffix.lower() in {".b3dm", ".glb"}
    ]


def choose_candidate(contents: list[TileContent]) -> TileContent:
    candidates = mesh_candidates(contents)
    if not candidates:
        raise ExpoModelError(
            "描画対象が見つかりません。b3dmまたはglbのcontentを確認してください"
        )
    return candidates[0]


def choose_candidates(
    contents: list[TileContent],
    max_tiles: int,
    leaf_only: bool,
) -> list[TileContent]:
    candidates = mesh_candidates(contents)
    if not candidates:
        raise ExpoModelError(
            "描画対象が見つかりません。b3dmまたはglbのcontentを確認してください"
        )
    if max_tiles < 1:
        raise ExpoModelError("--max-tiles は1以上にしてください")

    selected = candidates
    if leaf_only:
        leaves = [
            item
            for item in candidates
            if isinstance(item.geometric_error, (int, float))
            and item.geometric_error == 0
        ]
        if leaves:
            selected = leaves

    selected = sorted(selected, key=lambda item: item.path.name.lower())
    return selected[:max_tiles]


def tile_output_name(output_name: str, content_path: Path) -> str:
    stem = Path(output_name).stem
    suffix = Path(output_name).suffix or ".glb"
    return f"{stem}_{content_path.stem}{suffix}"


def manifest_file_name(output_name: str) -> str:
    stem = Path(output_name).stem
    if stem.startswith("expo_tile"):
        return stem.replace("expo_tile", "expo_tiles", 1) + ".txt"
    return f"{stem}_tiles.txt"


def parse_rtc_center(value: Any) -> list[float] | None:
    if not isinstance(value, list) or len(value) != 3:
        return None
    try:
        return [float(value[0]), float(value[1]), float(value[2])]
    except (TypeError, ValueError):
        return None


def mean_rtc_center(centers: list[list[float]]) -> list[float] | None:
    if not centers:
        return None
    return [
        sum(item[0] for item in centers) / len(centers),
        sum(item[1] for item in centers) / len(centers),
        sum(item[2] for item in centers) / len(centers),
    ]


def convert_tile(
    candidate: TileContent,
    node_command: str,
    keep_draco: bool,
) -> tuple[bytes, dict[str, Any]]:
    glb, content_metadata = read_content(candidate.path)
    content_metadata["source_glb_sha256"] = sha256(glb)
    if has_draco_extension(content_metadata) and not keep_draco:
        glb = decompress_draco(glb, content_metadata, node_command)
    return glb, content_metadata


def write_manifest(
    path: Path,
    reference_rtc: list[float],
    tiles: list[dict[str, Any]],
) -> None:
    lines = [
        "EXPO_TILE_SET 2",
        (
            "ref_rtc "
            f"{reference_rtc[0]:.15f} {reference_rtc[1]:.15f} {reference_rtc[2]:.15f}"
        ),
        "model_scale 0.002",
        "glb_global_scale 100",
        "lhs_flip_z 1",
        f"count {len(tiles)}",
    ]
    for tile in tiles:
        rtc = tile["rtc_center"]
        line = (
            "tile "
            f"{tile['runtime_path']} "
            f"{rtc[0]:.15f} {rtc[1]:.15f} {rtc[2]:.15f}"
        )
        bounding_volume = tile.get("bounding_volume")
        region = (
            bounding_volume.get("region")
            if isinstance(bounding_volume, dict)
            else None
        )
        if isinstance(region, list) and len(region) == 6:
            try:
                region_values = [float(value) for value in region]
            except (TypeError, ValueError):
                region_values = []
            if len(region_values) == 6 and all(
                math.isfinite(value) for value in region_values
            ):
                line += " " + " ".join(
                    f"{value:.15f}" for value in region_values
                )
        lines.append(line)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def runtime_tile_path(runtime_dir: Path | None, file_name: str) -> str:
    if runtime_dir is None:
        return str(Path("asset") / "model" / file_name)
    try:
        relative = (runtime_dir / file_name).resolve().relative_to(
            PROJECT_ROOT.resolve()
        )
        return str(relative)
    except ValueError:
        return str(runtime_dir / file_name)


def write_tile_files(
    candidate: TileContent,
    scan: ScanResult,
    glb: bytes,
    content_metadata: dict[str, Any],
    output_dir: Path,
    output_name: str,
    runtime_dir: Path | None,
    include_scan: bool,
) -> tuple[Path, Path, dict[str, Any]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / output_name
    metadata_path = output_dir / f"{Path(output_name).stem}.metadata.json"
    output_path.write_bytes(glb)

    rtc_center = parse_rtc_center(content_metadata.get("rtc_center"))
    metadata: dict[str, Any] = {
        "source": {
            "tileset": str(project_path(candidate.source_tileset)),
            "reference": candidate.reference,
            "content": str(project_path(candidate.path)),
            "output_sha256": sha256(glb),
        },
        "tile": {
            "geometric_error": candidate.geometric_error,
            "bounding_volume": candidate.bounding_volume,
            "transform_chain": candidate.transform_chain,
        },
        "extracted": content_metadata,
    }
    if include_scan:
        metadata["scan"] = {
            "visited_tilesets": [
                str(project_path(path)) for path in scan.visited_tilesets
            ],
            "content_count": len(scan.contents),
            "missing_count": len(scan.missing),
            "unsupported_count": len(scan.unsupported),
            "invalid_tileset_count": len(scan.invalid_tilesets),
            "missing": scan.missing,
            "unsupported": scan.unsupported,
            "invalid_tilesets": scan.invalid_tilesets,
        }
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    if runtime_dir is not None:
        runtime_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(output_path, runtime_dir / output_name)

    tile_info = {
        "id": candidate.path.stem,
        "source": str(project_path(candidate.path)),
        "output": str(project_path(output_path)),
        "runtime_path": runtime_tile_path(runtime_dir, output_name).replace("/", "\\"),
        "geometric_error": candidate.geometric_error,
        "rtc_center": rtc_center,
        "bounding_volume": candidate.bounding_volume,
    }
    return output_path, metadata_path, tile_info


def write_outputs(
    candidate: TileContent,
    scan: ScanResult,
    glb: bytes,
    content_metadata: dict[str, Any],
    output_dir: Path,
    output_name: str,
    runtime_output: Path | None,
) -> tuple[Path, Path]:
    runtime_dir = runtime_output.parent if runtime_output is not None else None
    output_path, metadata_path, _tile_info = write_tile_files(
        candidate,
        scan,
        glb,
        content_metadata,
        output_dir,
        output_name,
        runtime_dir,
        include_scan=True,
    )
    if runtime_output is not None and runtime_output.name != output_name:
        runtime_output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(output_path, runtime_output)
    return output_path, metadata_path


def write_multi_outputs(
    candidates: list[TileContent],
    scan: ScanResult,
    converted: list[tuple[bytes, dict[str, Any]]],
    output_dir: Path,
    output_name: str,
    runtime_output: Path | None,
    leaf_only: bool,
) -> tuple[Path, Path, list[dict[str, Any]]]:
    runtime_dir = runtime_output.parent if runtime_output is not None else None
    tile_infos: list[dict[str, Any]] = []

    for index, (candidate, (glb, content_metadata)) in enumerate(
        zip(candidates, converted)
    ):
        tile_name = tile_output_name(output_name, candidate.path)
        _output_path, _metadata_path, tile_info = write_tile_files(
            candidate,
            scan,
            glb,
            content_metadata,
            output_dir,
            tile_name,
            runtime_dir,
            include_scan=(index == 0),
        )
        if tile_info["rtc_center"] is None:
            raise ExpoModelError(
                f"CESIUM_RTC中心がありません: {project_path(candidate.path)}"
            )
        tile_infos.append(tile_info)

    rtc_centers = [item["rtc_center"] for item in tile_infos]
    reference_rtc = mean_rtc_center(rtc_centers)
    if reference_rtc is None:
        raise ExpoModelError("参照用RTC中心を計算できません")

    set_metadata = {
        "reference_rtc_center": reference_rtc,
        "selection": {
            "max_tiles": len(candidates),
            "leaf_only": leaf_only,
            "note": "REPLACE親タイルは含めず、葉タイルだけを相対配置する",
        },
        "scan": {
            "visited_tilesets": [
                str(project_path(path)) for path in scan.visited_tilesets
            ],
            "content_count": len(scan.contents),
            "missing_count": len(scan.missing),
            "unsupported_count": len(scan.unsupported),
            "invalid_tileset_count": len(scan.invalid_tilesets),
        },
        "tiles": tile_infos,
    }
    set_metadata_path = output_dir / f"{Path(manifest_file_name(output_name)).stem}.metadata.json"
    set_metadata_path.write_text(
        json.dumps(set_metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    manifest_name = manifest_file_name(output_name)
    converted_manifest = output_dir / manifest_name
    write_manifest(converted_manifest, reference_rtc, tile_infos)
    if runtime_dir is not None:
        shutil.copyfile(converted_manifest, runtime_dir / manifest_name)

    return converted_manifest, set_metadata_path, tile_infos


def print_summary(
    root_tileset: Path,
    scan: ScanResult,
    candidate: TileContent,
    output_path: Path,
    metadata_path: Path,
    runtime_output: Path | None,
    content_metadata: dict[str, Any],
) -> None:
    print("=== Expo 3D Tiles preparation ===")
    print(f"Root tileset : {project_path(root_tileset)}")
    print(f"Tilesets     : {len(scan.visited_tilesets)}")
    print(f"Contents     : {len(scan.contents)}")
    print(f"Missing      : {len(scan.missing)}")
    print(f"Unsupported  : {len(scan.unsupported)}")
    print(f"Invalid JSON : {len(scan.invalid_tilesets)}")
    print(f"Selected     : {project_path(candidate.path)}")
    print(
        "Container    : "
        f"{content_metadata.get('container', {}).get('format', 'unknown')}"
    )
    print(
        "GLB          : "
        f"{content_metadata.get('gltf', {}).get('mesh_count', 0)} mesh(es), "
        f"{content_metadata.get('gltf', {}).get('material_count', 0)} material(s), "
        f"{content_metadata.get('gltf', {}).get('image_count', 0)} image(s)"
    )
    print(
        "Extensions   : "
        + ", ".join(content_metadata.get("gltf", {}).get("extensions_required", []))
    )
    runtime = content_metadata.get("runtime")
    if isinstance(runtime, dict) and runtime.get("draco_decompressed"):
        print("Runtime      : Draco解除済み / CESIUM_RTC除去済み")
    print(f"Output       : {project_path(output_path)}")
    print(f"Metadata     : {project_path(metadata_path)}")
    if runtime_output is not None:
        print(f"Runtime copy : {project_path(runtime_output)}")

    if scan.missing:
        print("\n参照切れ:")
        for item in scan.missing[:20]:
            print(f"  - {item['reference']} -> {item['resolved']}")
        if len(scan.missing) > 20:
            print(f"  ... {len(scan.missing) - 20}件省略")

    if scan.unsupported:
        print("\n未対応参照:")
        for item in scan.unsupported[:20]:
            print(f"  - {item.get('reference', '<content>')}: {item['reason']}")
        if len(scan.unsupported) > 20:
            print(f"  ... {len(scan.unsupported) - 20}件省略")


def print_multi_summary(
    root_tileset: Path,
    scan: ScanResult,
    tile_infos: list[dict[str, Any]],
    set_metadata_path: Path,
    runtime_output: Path | None,
) -> None:
    print("=== Expo 3D Tiles preparation (multi) ===")
    print(f"Root tileset : {project_path(root_tileset)}")
    print(f"Tilesets     : {len(scan.visited_tilesets)}")
    print(f"Contents     : {len(scan.contents)}")
    print(f"Missing      : {len(scan.missing)}")
    print(f"Unsupported  : {len(scan.unsupported)}")
    print(f"Invalid JSON : {len(scan.invalid_tilesets)}")
    print(f"Selected     : {len(tile_infos)} tile(s)")
    for tile in tile_infos:
        rtc = tile.get("rtc_center")
        rtc_text = (
            f" rtc=({rtc[0]:.3f}, {rtc[1]:.3f}, {rtc[2]:.3f})"
            if isinstance(rtc, list)
            else ""
        )
        print(f"  - {tile['id']}: {tile['runtime_path']}{rtc_text}")
    print("Runtime      : Draco解除済み / CESIUM_RTC除去済み / 相対配置用マニフェスト出力")
    print(f"Set metadata : {project_path(set_metadata_path)}")
    if runtime_output is not None:
        manifest_name = manifest_file_name(runtime_output.name)
        print(f"Manifest     : {project_path(runtime_output.parent / manifest_name)}")

    if scan.missing:
        print("\n参照切れ:")
        for item in scan.missing[:20]:
            print(f"  - {item['reference']} -> {item['resolved']}")
        if len(scan.missing) > 20:
            print(f"  ... {len(scan.missing) - 20}件省略")

    if scan.unsupported:
        print("\n未対応参照:")
        for item in scan.unsupported[:20]:
            print(f"  - {item.get('reference', '<content>')}: {item['reason']}")
        if len(scan.unsupported) > 20:
            print(f"  ... {len(scan.unsupported) - 20}件省略")


def main() -> int:
    args = parse_args()
    root_tileset = args.tileset
    if not root_tileset.is_absolute():
        root_tileset = (PROJECT_ROOT / root_tileset).resolve()
    else:
        root_tileset = root_tileset.resolve()
    if not root_tileset.is_file():
        print(f"ERROR: tileset.jsonが見つかりません: {root_tileset}", file=sys.stderr)
        return 2

    output_dir = args.output_dir
    if not output_dir.is_absolute():
        output_dir = (PROJECT_ROOT / output_dir).resolve()
    else:
        output_dir = output_dir.resolve()

    runtime_output: Path | None = None
    if not args.no_runtime_copy:
        runtime_output = args.runtime_output
        if not runtime_output.is_absolute():
            runtime_output = (PROJECT_ROOT / runtime_output).resolve()
        else:
            runtime_output = runtime_output.resolve()

    source_root = root_tileset.parent.resolve()
    scan = ScanResult()
    walk_tileset(root_tileset, source_root, scan, set())

    try:
        if args.max_tiles > 1:
            candidates = choose_candidates(
                scan.contents, args.max_tiles, args.leaf_only
            )
            converted: list[tuple[bytes, dict[str, Any]]] = []
            for candidate in candidates:
                converted.append(
                    convert_tile(candidate, args.node_command, args.keep_draco)
                )
            _manifest_path, set_metadata_path, tile_infos = write_multi_outputs(
                candidates,
                scan,
                converted,
                output_dir,
                args.output_name,
                runtime_output,
                args.leaf_only,
            )
            print_multi_summary(
                root_tileset,
                scan,
                tile_infos,
                set_metadata_path,
                runtime_output,
            )
            return 0

        candidate = choose_candidate(scan.contents)
        glb, content_metadata = convert_tile(
            candidate, args.node_command, args.keep_draco
        )
        output_path, metadata_path = write_outputs(
            candidate,
            scan,
            glb,
            content_metadata,
            output_dir,
            args.output_name,
            runtime_output,
        )
    except (OSError, ExpoModelError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print_summary(
        root_tileset,
        scan,
        candidate,
        output_path,
        metadata_path,
        runtime_output,
        content_metadata,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
