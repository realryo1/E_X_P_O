# -*- coding: utf-8 -*-
"""
ソリューション / プロジェクト名を一括置換するスクリプト。

対象:
  - {Name}.sln / .vcxproj / .vcxproj.filters / .vcxproj.user / .rc のリネーム
  - テキストファイル内の旧名称置換（.sln, .vcxproj, define.h, docs など）
  - .github/workflows/*.yml（build.yml など CI ワークフロー）

使い方:
  python tool/rename_project.py NewName
  python tool/rename_project.py NewName --old TextGames
  python tool/rename_project.py NewName --also TextGame
  python tool/rename_project.py NewName --dry-run
  python tool/rename_project.py          # 対話モード
"""

from __future__ import annotations

import argparse
import re
import sys
import traceback
from pathlib import Path

# 走査対象外ディレクトリ（.github は含めない = workflows を置換対象にする）
SKIP_DIRS = {
    '.git',
    '.vs',
    '__pycache__',
    'node_modules',
    'x64',
    'Win32',
    'Debug',
    'Release',
    'ipch',
}

# 必ず置換対象にするパス（リポジトリルート相対・glob）
# rglob がドットディレクトリを拾い損ねる環境向けの保険
ALWAYS_INCLUDE_GLOBS = (
    '.github/workflows/*.yml',
    '.github/workflows/*.yaml',
)

# テキストとして扱う拡張子
TEXT_EXTS = {
    '.sln',
    '.vcxproj',
    '.filters',
    '.user',
    '.rc',
    '.h',
    '.hpp',
    '.c',
    '.cpp',
    '.cc',
    '.cxx',
    '.hlsl',
    '.hlsli',
    '.yml',
    '.yaml',
    '.md',
    '.txt',
    '.ps1',
    '.py',
    '.json',
    '.editorconfig',
    '.gitignore',
    '.gitattributes',
    '.xml',
    '.props',
    '.targets',
}

# リネーム対象のプロジェクト関連サフィックス（長い順）
PROJECT_SUFFIXES = (
    '.vcxproj.filters',
    '.vcxproj.user',
    '.vcxproj',
    '.sln',
    '.rc',
)


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def detect_old_name(root: Path) -> str | None:
    slns = sorted(root.glob('*.sln'))
    if not slns:
        return None
    return slns[0].stem


def is_valid_name(name: str) -> bool:
    # VS / C++ 識別子として無難な英数字・アンダースコア
    return bool(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name))


def read_text_preserve(path: Path) -> tuple[str, str, bool]:
    """content, encoding_label('utf-8'|'utf-16-le'|'utf-16-be'|'shift_jis'), has_bom"""
    raw = path.read_bytes()
    if not raw:
        return '', 'utf-8', False

    if raw[:3] == b'\xef\xbb\xbf':
        return raw[3:].decode('utf-8'), 'utf-8', True
    if raw[:2] == b'\xff\xfe':
        return raw[2:].decode('utf-16-le'), 'utf-16-le', True
    if raw[:2] == b'\xfe\xff':
        return raw[2:].decode('utf-16-be'), 'utf-16-be', True

    try:
        return raw.decode('utf-8'), 'utf-8', False
    except UnicodeDecodeError:
        return raw.decode('shift_jis'), 'shift_jis', False


def write_text_preserve(path: Path, content: str, encoding: str, has_bom: bool) -> None:
    if encoding.startswith('utf-16'):
        bom = b'\xff\xfe' if encoding == 'utf-16-le' else b'\xfe\xff'
        data = content.encode(encoding)
        path.write_bytes((bom if has_bom else b'') + data)
        return

    if encoding == 'shift_jis':
        path.write_bytes(content.encode('cp932'))
        return

    data = content.encode('utf-8')
    if has_bom:
        data = b'\xef\xbb\xbf' + data
    path.write_bytes(data)


def should_skip_dir(name: str) -> bool:
    return name in SKIP_DIRS or name.endswith('.tlog')


def is_text_target(path: Path) -> bool:
    name_lower = path.name.lower()
    if name_lower.endswith('.vcxproj.filters') or name_lower.endswith('.vcxproj.user'):
        return True
    if path.name in {'.editorconfig', '.gitignore', '.gitattributes'}:
        return True
    return path.suffix.lower() in TEXT_EXTS


def iter_always_include_files(root: Path):
    for pattern in ALWAYS_INCLUDE_GLOBS:
        for path in root.glob(pattern):
            if path.is_file():
                yield path


def iter_text_files(root: Path, self_path: Path | None = None):
    self_resolved = self_path.resolve() if self_path else None
    seen: set[Path] = set()

    def accept(path: Path) -> bool:
        if not path.is_file():
            return False
        resolved = path.resolve()
        if self_resolved and resolved == self_resolved:
            return False
        if resolved in seen:
            return False
        seen.add(resolved)
        return True

    # CI ワークフローなどは必ず先頭で列挙
    for path in iter_always_include_files(root):
        if accept(path):
            yield path

    for path in root.rglob('*'):
        if not accept(path):
            continue
        # .git のみ除外。.github は置換対象
        if any(should_skip_dir(p.name) for p in path.relative_to(root).parents):
            continue
        if path.name.startswith('.') and not is_text_target(path):
            continue
        if is_text_target(path):
            yield path


def build_replacements(pairs: list[tuple[str, str]]) -> list[tuple[str, str]]:
    """長い旧名から置換するようソート。同一 old は先勝ち。"""
    seen = set()
    unique = []
    for old, new in pairs:
        if not old or old == new:
            continue
        if old in seen:
            continue
        seen.add(old)
        unique.append((old, new))
    unique.sort(key=lambda x: len(x[0]), reverse=True)
    return unique


def apply_replacements(text: str, replacements: list[tuple[str, str]]) -> tuple[str, int]:
    total = 0
    for old, new in replacements:
        count = text.count(old)
        if count:
            text = text.replace(old, new)
            total += count
    return text, total


def rename_project_files(root: Path, old: str, new: str, dry_run: bool) -> list[tuple[Path, Path]]:
    renamed = []
    for suffix in PROJECT_SUFFIXES:
        src = root / f'{old}{suffix}'
        if not src.exists():
            continue
        dst = root / f'{new}{suffix}'
        renamed.append((src, dst))
        if dry_run:
            continue
        if dst.exists() and dst.resolve() != src.resolve():
            raise FileExistsError(f'Already exists: {dst}')
        src.rename(dst)
    return renamed


def replace_in_files(
    root: Path,
    replacements: list[tuple[str, str]],
    dry_run: bool,
    self_path: Path | None = None,
) -> list[tuple[Path, int]]:
    changed = []
    for path in iter_text_files(root, self_path=self_path):
        try:
            content, encoding, has_bom = read_text_preserve(path)
        except (UnicodeDecodeError, OSError) as e:
            print(f'  [skip read] {path.relative_to(root)}: {e}')
            continue

        new_content, count = apply_replacements(content, replacements)
        if count == 0:
            continue

        changed.append((path, count))
        if dry_run:
            continue

        try:
            write_text_preserve(path, new_content, encoding, has_bom)
        except OSError as e:
            print(f'  [skip write] {path.relative_to(root)}: {e}')

    return changed


def pause_exit() -> None:
    if not sys.stdin.isatty():
        return
    try:
        input('\nPress Enter to exit...')
    except EOFError:
        pass


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description='Rename Visual Studio solution/project name across the repository.',
    )
    p.add_argument('new_name', nargs='?', help='New project / solution name')
    p.add_argument('--old', dest='old_name', help='Current project name (default: detect from *.sln)')
    p.add_argument(
        '--also',
        action='append',
        default=[],
        metavar='NAME',
        help='Additional old name to replace with new_name (repeatable). '
             'Example: --also TextGame',
    )
    p.add_argument('--dry-run', action='store_true', help='Show changes without writing')
    p.add_argument('-y', '--yes', action='store_true', help='Skip confirmation prompt')
    return p.parse_args(argv)


def interactive_names(root: Path, args: argparse.Namespace) -> tuple[str, str, list[str]]:
    old = args.old_name or detect_old_name(root)
    if not old:
        print('No *.sln found in project root. Specify --old.')
        sys.exit(1)

    print(f'Detected / current name: {old}')
    also = list(args.also)

    # よくある短縮形（末尾 s なし）を提案
    if old.endswith('s') and len(old) > 1:
        candidate = old[:-1]
        if candidate and candidate not in also:
            ans = input(f'Also replace "{candidate}" ? [Y/n]: ').strip().lower()
            if ans in ('', 'y', 'yes'):
                also.append(candidate)

    new = args.new_name
    if not new:
        new = input('New project name: ').strip()
    if not new:
        print('New name is empty.')
        sys.exit(1)

    return old, new, also


def main(argv: list[str] | None = None) -> int:
    root = project_root()
    args = parse_args(argv if argv is not None else sys.argv[1:])

    print('=== Project Rename ===')
    print(f'Root: {root}')
    print()

    # new_name 指定 + (--yes / --dry-run / 非TTY) なら対話をスキップ
    skip_prompt = args.yes or args.dry_run or not sys.stdin.isatty()
    if args.new_name and (args.old_name or skip_prompt):
        old = args.old_name or detect_old_name(root)
        if not old:
            print('No *.sln found. Specify --old.')
            return 1
        new, also = args.new_name, list(args.also)
    else:
        old, new, also = interactive_names(root, args)

    if not is_valid_name(old):
        print(f'Invalid old name: {old}')
        return 1
    if not is_valid_name(new):
        print(f'Invalid new name (use [A-Za-z_][A-Za-z0-9_]*): {new}')
        return 1
    if old == new and not also:
        print('Old and new names are the same. Nothing to do.')
        return 0

    pairs = [(old, new)] + [(a, new) for a in also]
    replacements = build_replacements(pairs)

    print('Replacements:')
    for o, n in replacements:
        print(f'  {o}  ->  {n}')
    if args.dry_run:
        print('Mode: DRY-RUN')
    print()

    if not args.yes and not args.dry_run:
        ans = input('Proceed? [y/N]: ').strip().lower()
        if ans not in ('y', 'yes'):
            print('Cancelled.')
            return 0

    print('[1/2] Updating file contents...')
    changed = replace_in_files(
        root,
        replacements,
        args.dry_run,
        self_path=Path(__file__),
    )
    for path, count in changed:
        mark = '(dry-run) ' if args.dry_run else ''
        print(f'  {mark}{path.relative_to(root)}  ({count} replacement(s))')
    print(f'  Content files: {len(changed)}')
    print()

    print('[2/2] Renaming project files...')
    try:
        renamed = rename_project_files(root, old, new, args.dry_run)
    except FileExistsError as e:
        print(f'  Error: {e}')
        return 1

    for src, dst in renamed:
        mark = '(dry-run) ' if args.dry_run else ''
        print(f'  {mark}{src.name}  ->  {dst.name}')
    if not renamed:
        print('  (no project files to rename)')
    print()

    print('Done.')
    if args.dry_run:
        print('No files were modified (dry-run).')
    else:
        print('Tip: close Visual Studio before renaming, then reopen the new .sln.')
        print('     Delete leftover .vs/ folder if the IDE still shows the old name.')

    return 0


if __name__ == '__main__':
    try:
        code = main()
        pause_exit()
        sys.exit(code)
    except KeyboardInterrupt:
        print('\nCancelled.')
        sys.exit(130)
    except Exception:
        print()
        print('=== Unexpected Error ===')
        traceback.print_exc()
        pause_exit()
        sys.exit(1)
