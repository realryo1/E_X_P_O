# -*- coding: utf-8 -*-
"""ダウンロード済みの公式ZIPを、変換ツールが読む配置へ展開する。"""

from __future__ import annotations

import argparse
import shutil
import sys
import zipfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent


def configure_stdio() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8")
DATA_ORIGINAL = PROJECT_ROOT / "data_original"
THREE_D_TILES_DIR = DATA_ORIGINAL / "3dtiles"
ORTHO_DIR = DATA_ORIGINAL / "ortho"
ORTHO_NAME = "27999_osaka-shi_city_2025_ortho_1_op"


def safe_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            target = (destination / member.filename).resolve()
            if target != root and root not in target.parents:
                raise RuntimeError(f"ZIP内の不正なパスです: {member.filename}")
            source.extract(member, destination)


def find_directory(root: Path, predicate) -> Path | None:
    for path in root.rglob("*"):
        if path.is_dir() and predicate(path):
            return path
    return None


def move_to_expected(source: Path, expected: Path) -> None:
    if expected.is_dir():
        return
    expected.parent.mkdir(parents=True, exist_ok=True)
    if source.resolve() == expected.resolve():
        return
    shutil.move(str(source), str(expected))


def normalize_tiles() -> None:
    expected_names = (
        "27999_osaka-shi_city_2025_citygml_1_op_bldg_lod1",
        "27999_osaka-shi_city_2025_citygml_1_op_bldg_lod2",
        "27999_osaka-shi_city_2025_citygml_1_op_bldg_lod3",
    )
    for name in expected_names:
        expected = THREE_D_TILES_DIR / name
        if expected.is_dir():
            continue
        source = find_directory(
            THREE_D_TILES_DIR,
            lambda path, name=name: path.name == name and (path / "tileset.json").is_file(),
        )
        if source is None:
            raise RuntimeError(f"3D Tilesの展開先が見つかりません: {name}")
        move_to_expected(source, expected)


def normalize_ortho() -> None:
    expected = ORTHO_DIR / ORTHO_NAME
    if expected.is_dir():
        return
    source = find_directory(
        ORTHO_DIR,
        lambda path: path.name == ORTHO_NAME and any(path.glob("*.tif")),
    )
    if source is None:
        raise RuntimeError(f"オルソ画像の展開先が見つかりません: {ORTHO_NAME}")
    move_to_expected(source, expected)


def data_dirs(root: Path | None = None) -> list[Path]:
    base = PROJECT_ROOT if root is None else root
    return sorted(path for path in base.glob("data_*") if path.is_dir())


def dir_size_bytes(path: Path) -> int:
    total = 0
    for file_path in path.rglob("*"):
        if not file_path.is_file():
            continue
        try:
            total += file_path.stat().st_size
        except OSError:
            pass
    return total


def format_gb(byte_count: int) -> str:
    return f"{byte_count / (1024 ** 3):.2f}"


def report_data_dirs() -> int:
    dirs = data_dirs()
    if not dirs:
        print("一時フォルダ data_* はありません。")
        return 1

    total = 0
    print("一時フォルダの削減目安（実測）:")
    for path in dirs:
        size = dir_size_bytes(path)
        total += size
        print(f"  {path.name}/ : {format_gb(size)} GB")
    print(f"  合計       : {format_gb(total)} GB")
    return 0


def delete_data_dirs() -> int:
    dirs = data_dirs()
    if not dirs:
        print("削除対象の data_* はありません。")
        return 0

    for path in dirs:
        print(f"削除中: {path.name}/")
        shutil.rmtree(path)
        print(f"削除しました: {path.name}/")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="公式ZIPを変換ツール用に展開する")
    parser.add_argument(
        "--tiles-zip",
        type=Path,
        default=DATA_ORIGINAL / "27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip",
    )
    parser.add_argument(
        "--ortho-zip",
        type=Path,
        default=DATA_ORIGINAL / "27999_osaka-shi_city_2025_ortho_1_op.zip",
    )
    parser.add_argument(
        "--report-data-dirs",
        action="store_true",
        help="data_* フォルダの実測サイズを表示する",
    )
    parser.add_argument(
        "--delete-data-dirs",
        action="store_true",
        help="data_* フォルダを削除する",
    )
    return parser.parse_args()


def main() -> int:
    configure_stdio()
    args = parse_args()
    if args.report_data_dirs:
        return report_data_dirs()
    if args.delete_data_dirs:
        return delete_data_dirs()

    for archive in (args.tiles_zip, args.ortho_zip):
        if not archive.is_file():
            print(f"ZIPがありません: {archive}", file=sys.stderr)
            return 2

    if not (THREE_D_TILES_DIR / "27999_osaka-shi_city_2025_citygml_1_op_bldg_lod3").is_dir():
        print(f"展開: {args.tiles_zip}")
        safe_extract(args.tiles_zip, THREE_D_TILES_DIR)
    normalize_tiles()

    if not (ORTHO_DIR / ORTHO_NAME).is_dir():
        print(f"展開: {args.ortho_zip}")
        safe_extract(args.ortho_zip, ORTHO_DIR)
    normalize_ortho()
    print("公式3D Tiles / オルソの展開が完了しました")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
