import sys
import re
import traceback
from pathlib import Path

EDITORCONFIG_DEFAULT = """root = true

[*]
end_of_line = crlf
indent_style = tab

[*.{h,hpp,c,cpp,cc,cxx}]
charset = utf-8-bom

[*.{hlsl,hlsli}]
charset = utf-8
"""

SETUPDIR_DEFAULT = (
    "# Target directories for encoding conversion\n"
    "# / = project root (expogame) - only root-level files, not subdirs\n"
    "# One directory per line. Subdirectories are NOT recursed.\n"
    "# relative: /framework  /shader\n"
    "# absolute: C:\\path\\to\\dir\n"
    "/\n"
)


def read_file_content(path):
    raw = path.read_bytes()
    if not raw:
        return ""
    if raw[:3] == b'\xef\xbb\xbf':
        return raw[3:].decode('utf-8')
    if raw[:2] == b'\xff\xfe':
        return raw[2:].decode('utf-16-le')
    if raw[:2] == b'\xfe\xff':
        return raw[2:].decode('utf-16-be')
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('shift_jis')


def normalize_line_endings(text, eol):
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    if eol == 'lf':
        return text
    if eol == 'cr':
        return text.replace('\n', '\r')
    return text.replace('\n', '\r\n')


def parse_editorconfig(path):
    result = {}
    current = None
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        s = line.strip()
        if not s or s[0] in ('#', ';'):
            continue
        if s.startswith('[') and s.endswith(']'):
            current = s[1:-1]
            result.setdefault(current, {})
            continue
        if current and '=' in s:
            k, _, v = s.partition('=')
            result[current][k.strip().lower()] = v.strip()
    return result


def pattern_to_regex(pattern):
    # ** を退避 → . をエスケープ → * を変換 → ** を復元 → ? を変換 → {} を展開
    p = pattern.replace('**', '\x00DS\x00')
    p = p.replace('.', r'\.')
    p = p.replace('*', r'[^/\\]*')
    p = p.replace('\x00DS\x00', '.*')
    p = p.replace('?', '.')
    def brace_replace(m):
        choices = m.group(1).split(',')
        return '(?:' + '|'.join(c.strip() for c in choices) + ')'
    p = re.sub(r'\{([^}]+)\}', brace_replace, p)
    return re.compile(p + '$', re.IGNORECASE)


def get_properties_for_file(filename, config):
    result = {}
    for pat, props in config.items():
        try:
            if pattern_to_regex(pat).search(filename):
                result.update(props)
        except re.error:
            pass
    return result


def get_encoding_info(charset):
    cs = charset.lower()
    if cs == 'utf-8-bom':
        return ('utf-8-sig', 'UTF-8 with BOM')
    if cs == 'utf-8':
        return ('utf-8', 'UTF-8 no BOM')
    return None


def write_file(path, text, encoding, eol):
    normalized = normalize_line_endings(text, eol)
    if encoding == 'utf-8-sig':
        raw = b'\xef\xbb\xbf' + normalized.encode('utf-8')
    else:
        raw = normalized.encode(encoding)

    if path.read_bytes() == raw:
        return False
    path.write_bytes(raw)
    return True


def has_expected_format(path, encoding, eol):
    """指定されたエンコーディングと改行コードになっているか確認する。"""
    raw = path.read_bytes()

    if encoding == 'utf-8-sig':
        if not raw.startswith(b'\xef\xbb\xbf'):
            return False
        body = raw[3:]
        decode_encoding = 'utf-8'
    else:
        if raw.startswith(b'\xef\xbb\xbf'):
            return False
        body = raw
        decode_encoding = encoding

    try:
        text = body.decode(decode_encoding)
    except UnicodeDecodeError:
        return False

    if eol == 'lf':
        return '\r' not in text
    if eol == 'cr':
        return '\r\n' not in text and '\n' not in text
    return not text.replace('\r\n', '').replace('\r', '').replace('\n', '')


def resolve_target_dir(raw, default_root):
    raw = raw.strip().strip('"').strip("'")
    if raw == '/':
        return default_root
    if raw.startswith('/'):
        return (default_root / raw[1:]).resolve()
    p = Path(raw)
    if p.is_absolute():
        return p.resolve()
    return (default_root / p).resolve()


def load_or_create_setupdirectory(tool_dir, default_root):
    setupdir_file = tool_dir / 'setupdirectory.txt'
    if not setupdir_file.exists():
        setupdir_file.write_text(SETUPDIR_DEFAULT, encoding='utf-8')
        print(f'  Created: setupdirectory.txt  (default: /)')

    dirs = []
    for line in setupdir_file.read_text(encoding='utf-8', errors='replace').splitlines():
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        target = resolve_target_dir(s, default_root)
        if not target.exists() or not target.is_dir():
            print(f'  WARNING : Directory not found, skipped: {target}')
            continue
        dirs.append(target)

    if not dirs:
        print('ERROR: No valid directories found in setupdirectory.txt')
        input('\nPress Enter to exit...')
        sys.exit(1)

    return dirs


def convert_directory(target_dir, config, project_root):
    ok = skip = err = 0
    for filepath in sorted(target_dir.iterdir()):
        if not filepath.is_file():
            continue
        if filepath.name == '.editorconfig':
            continue
        try:
            try:
                rel = filepath.relative_to(project_root)
            except ValueError:
                rel = filepath

            props = get_properties_for_file(filepath.name, config)

            if 'charset' not in props:
                print(f'  SKIP    : {rel} (no charset rule)')
                skip += 1
                continue

            enc_info = get_encoding_info(props['charset'])
            if enc_info is None:
                print(f'  SKIP    : {rel} (unsupported charset: {props["charset"]})')
                skip += 1
                continue

            encoding, desc = enc_info
            eol = props.get('end_of_line', 'crlf')
            if has_expected_format(filepath, encoding, eol):
                print(f'  SKIP    : {rel} (already {desc}, EOL={eol})')
                skip += 1
                continue

            content = read_file_content(filepath)
            if write_file(filepath, content, encoding, eol):
                print(f'  OK      : {rel} -> {desc}, EOL={eol}')
                ok += 1
            else:
                print(f'  SKIP    : {rel} (already {desc}, EOL={eol})')
                skip += 1
        except Exception as e:
            print(f'  ERROR   : {filepath.name} - {e}')
            err += 1
    return ok, skip, err


def main():
    tool_dir     = Path(__file__).resolve().parent
    default_root = tool_dir.parent

    print('=== Encoding Converter ===')
    print()
    print('Checking setupdirectory.txt...')

    if len(sys.argv) > 1:
        target_dirs = [resolve_target_dir(sys.argv[1], default_root)]
        print(f'  Target (from arg): {target_dirs[0]}')
    else:
        target_dirs = load_or_create_setupdirectory(tool_dir, default_root)
        for d in target_dirs:
            print(f'  Target: {d}')

    print()

    editorconfig_path = default_root / '.editorconfig'
    print('[1/2] Checking .editorconfig...')
    if not editorconfig_path.exists():
        editorconfig_path.write_text(EDITORCONFIG_DEFAULT, encoding='utf-8')
        print('  Created: .editorconfig')
    else:
        print('  Skipped: already exists')

    config = parse_editorconfig(editorconfig_path)

    print()
    print('[2/2] Converting files (non-recursive)...')
    total_ok = total_skip = total_err = 0

    for target_dir in target_dirs:
        print(f'  --- {target_dir} ---')
        ok, skip, err = convert_directory(target_dir, config, default_root)
        total_ok   += ok
        total_skip += skip
        total_err  += err

    print()
    print(f'Converted : {total_ok} file(s)')
    print(f'Skipped   : {total_skip} file(s)')
    if total_err:
        print(f'Errors    : {total_err} file(s)')
    print()
    print('Done.')
    input('\nPress Enter to exit...')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        print()
        print('=== Unexpected Error ===')
        traceback.print_exc()
        input('\nPress Enter to exit...')
        sys.exit(1)
