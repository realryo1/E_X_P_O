# -*- coding: utf-8 -*-
"""
シーンの追加・削除を対話式で行うスクリプト。

対象:
  - template/ をコピーして SCENE_XXX/ を生成（追加時）
  - app/scene.h の enum
  - app/scene.cpp の include / switch case
  - tool/setupdirectory.txt
  - ルート *.vcxproj の AdditionalIncludeDirectories / ClCompile / ClInclude
  - ルート *.vcxproj.filters の Filter / ファイル登録

使い方:
  python tool/manage_scene.py
"""

from __future__ import annotations

import re
import shutil
import sys
import traceback
import uuid
from dataclasses import dataclass
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
ROOT = TOOL_DIR.parent

SCENE_H = ROOT / 'app' / 'scene.h'
SCENE_CPP = ROOT / 'app' / 'scene.cpp'
SETUPDIR = TOOL_DIR / 'setupdirectory.txt'
TEMPLATE_DIR = ROOT / 'template'

# SCENE_MAX / SCENE_NONE: 番兵
# SCENE_DEBUG: 例外シーン（追加・削除・挿入位置の対象外。Release 非対応）
SKIP_ENUM = {'SCENE_MAX', 'SCENE_NONE', 'SCENE_DEBUG'}


@dataclass(frozen=True)
class SceneNames:
	raw: str
	stem: str          # pause
	enum: str          # SCENE_PAUSE
	folder: str        # SCENE_PAUSE
	prefix: str        # Pause
	header: str        # pause.h
	source: str        # pause.cpp
	display: str       # PAUSE


def project_root() -> Path:
	return ROOT


def read_text(path: Path) -> tuple[str, str, bool]:
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


def write_text(path: Path, content: str, encoding: str, has_bom: bool) -> None:
	if encoding.startswith('utf-16'):
		bom = b'\xff\xfe' if encoding == 'utf-16-le' else b'\xfe\xff'
		path.write_bytes((bom if has_bom else b'') + content.encode(encoding))
		return
	if encoding == 'shift_jis':
		path.write_bytes(content.encode('cp932'))
		return
	data = content.encode('utf-8')
	if has_bom:
		data = b'\xef\xbb\xbf' + data
	path.write_bytes(data)


def detect_newline(text: str) -> str:
	return '\r\n' if '\r\n' in text else '\n'


def normalize_name(raw: str) -> SceneNames:
	token = raw.strip()
	if token.upper().startswith('SCENE_'):
		token = token[6:]
	if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', token):
		raise ValueError('英数字とアンダースコアのみ、先頭は英字または _ にしてください')
	stem = token.lower()
	upper = token.upper()
	prefix = stem[:1].upper() + stem[1:]
	return SceneNames(
		raw=raw.strip(),
		stem=stem,
		enum=f'SCENE_{upper}',
		folder=f'SCENE_{upper}',
		prefix=prefix,
		header=f'{stem}.h',
		source=f'{stem}.cpp',
		display=upper,
	)


def parse_scene_enums(scene_h_text: str) -> list[str]:
	m = re.search(r'enum\s+SCENE\s*\{(.*?)\};', scene_h_text, re.S)
	if not m:
		raise RuntimeError('app/scene.h に enum SCENE が見つかりません')
	enums: list[str] = []
	for line in m.group(1).splitlines():
		s = line.strip().rstrip(',')
		if not s or s.startswith('//') or s.startswith('/*'):
			continue
		name = s.split('=')[0].strip()
		if re.fullmatch(r'SCENE_[A-Z0-9_]+', name):
			enums.append(name)
	return enums


def active_scenes(enums: list[str]) -> list[str]:
	return [e for e in enums if e not in SKIP_ENUM]


def find_vcxproj_files(root: Path) -> tuple[Path, Path]:
	projs = sorted(p for p in root.glob('*.vcxproj') if p.is_file())
	if not projs:
		raise RuntimeError('ルートに *.vcxproj がありません')
	proj = projs[0]
	filters = proj.with_suffix('.vcxproj.filters')
	if not filters.exists():
		# *.vcxproj.filters は stem が二重になるので別名も試す
		alt = Path(str(proj) + '.filters')
		if alt.exists():
			filters = alt
		else:
			raise RuntimeError(f'{filters.name} が見つかりません')
	return proj, filters


def ask_choice(prompt: str, options: list[str]) -> int:
	"""1-based index を返す"""
	print(prompt)
	for i, opt in enumerate(options, 1):
		print(f'  {i}) {opt}')
	while True:
		raw = input('番号: ').strip()
		if raw.isdigit():
			n = int(raw)
			if 1 <= n <= len(options):
				return n - 1
		print(f'1〜{len(options)} の番号を入力してください')


def ask_insert_position(scenes: list[str]) -> str | None:
	"""
	挿入位置を選ばせる。
	戻り値は「この enum の直前に挿入」、末尾なら None。

	表示例:
	  1) 先頭
	SCENE_TITLE
	  2)
	SCENE_GAME
	  ...
	  N) 末尾
	SCENE_MAX
	"""
	print()
	print('挿入位置を選んでください:')
	print()
	slots = list(scenes) + ['SCENE_MAX']
	for i, name in enumerate(slots, 1):
		if i == 1:
			label = f'先頭（{name}の前）'
		elif i == len(slots):
			label = '末尾'
		else:
			label = ''
		print(f'  {i}) {label}')
		print(name)
	print()
	while True:
		raw = input('番号: ').strip()
		if raw.isdigit():
			n = int(raw)
			if 1 <= n <= len(slots):
				# 末尾 (= SCENE_MAX の直前) なら None
				if n == len(slots):
					return None
				return slots[n - 1]
		print(f'1〜{len(slots)} の番号を入力してください')


def ask_yes_no(prompt: str, default: bool = False) -> bool:
	hint = 'Y/n' if default else 'y/N'
	raw = input(f'{prompt} [{hint}]: ').strip().lower()
	if not raw:
		return default
	return raw in ('y', 'yes')


# ---------------------------------------------------------------------------
# scene.h
# ---------------------------------------------------------------------------

def insert_enum(scene_h: str, enum_name: str, insert_before: str | None) -> str:
	"""insert_before が None なら SCENE_MAX の直前。それ以外は指定 enum の直前。"""
	nl = detect_newline(scene_h)
	target = insert_before if insert_before else 'SCENE_MAX'
	pattern = re.compile(rf'^(\t| )*{re.escape(target)}\b.*$', re.M)
	m = pattern.search(scene_h)
	if not m:
		raise RuntimeError(f'{target} が scene.h に見つかりません')
	indent = '\t'
	line = f'{indent}{enum_name},{nl}'
	return scene_h[:m.start()] + line + scene_h[m.start():]


def remove_enum(scene_h: str, enum_name: str) -> str:
	lines = scene_h.splitlines(keepends=True)
	out: list[str] = []
	removed = False
	for line in lines:
		if re.match(rf'^(\t| )*{re.escape(enum_name)}\b', line):
			removed = True
			continue
		out.append(line)
	if not removed:
		raise RuntimeError(f'{enum_name} が scene.h に見つかりません')
	return ''.join(out)


# ---------------------------------------------------------------------------
# scene.cpp
# ---------------------------------------------------------------------------

def _case_block_pattern(enum_name: str) -> re.Pattern[str]:
	# case SCENE_XXX: ... break;
	return re.compile(
		rf'(^[ \t]*case[ \t]+{re.escape(enum_name)}\s*:.*?^[ \t]*break\s*;[ \t]*\r?\n)',
		re.M | re.S,
	)


def _include_inside_defined_debug(scene_cpp: str, header: str) -> bool:
	"""#include \"header\" が #if defined(_DEBUG) ブロック内にあるか。"""
	pat = re.compile(
		rf'#if\s+defined\(_DEBUG\).*?#include\s+"{re.escape(header)}".*?#endif',
		re.S,
	)
	return pat.search(scene_cpp) is not None


def insert_include(scene_cpp: str, header: str, anchor_enum: str | None, scenes: list[str]) -> str:
	"""隣接シーンの include の近く、または define.h の直前に挿入。"""
	nl = detect_newline(scene_cpp)
	inc_line = f'#include "{header}"{nl}'
	if f'#include "{header}"' in scene_cpp:
		return scene_cpp

	# 直前シーンの include を探す（Debug 専用ブロック内は避ける）
	prev_header = None
	if anchor_enum is None:
		# 末尾挿入 → 最後のアクティブシーンの include の後
		if scenes:
			prev_header = _guess_header_for_enum(scene_cpp, scenes[-1])
	else:
		idx = scenes.index(anchor_enum) if anchor_enum in scenes else -1
		if idx > 0:
			prev_header = _guess_header_for_enum(scene_cpp, scenes[idx - 1])

	if prev_header and not _include_inside_defined_debug(scene_cpp, prev_header):
		pat = re.compile(rf'(#include\s+"{re.escape(prev_header)}"\s*\r?\n)')
		m = pat.search(scene_cpp)
		if m:
			return scene_cpp[:m.end()] + inc_line + scene_cpp[m.end():]

	# fallback: define.h の前
	m = re.search(r'#include\s+"define\.h"\s*\r?\n', scene_cpp)
	if m:
		return scene_cpp[:m.start()] + inc_line + scene_cpp[m.start():]

	# さらに fallback: 先頭 include 群の末尾（Debug 専用ブロック内は避ける）
	includes = list(re.finditer(r'#include\s+"([^"]+)"\s*\r?\n', scene_cpp))
	for m in reversed(includes):
		if not _include_inside_defined_debug(scene_cpp, m.group(1)):
			return scene_cpp[: m.end()] + inc_line + scene_cpp[m.end() :]
	raise RuntimeError('scene.cpp に include 挿入位置が見つかりません')


def _guess_header_for_enum(scene_cpp: str, enum_name: str) -> str | None:
	"""case から関数接頭辞を取り、対応しそうな include を探す。"""
	m = re.search(
		rf'case[ \t]+{re.escape(enum_name)}\s*:\s*\r?\n[ \t]*(\w+)_(?:Initialize|Update|Draw|Finalize)\s*\(',
		scene_cpp,
	)
	if not m:
		return None
	prefix = m.group(1)
	candidates = [
		f'{prefix.lower()}.h',
		f'{_camel_to_snake(prefix)}.h',
		f'{prefix}.h',
	]
	for c in candidates:
		if f'#include "{c}"' in scene_cpp:
			return c
	# SCENE_DEBUG / DebugScene → debugscene.h
	folded = prefix.lower()
	for inc in re.findall(r'#include\s+"([^"]+\.h)"', scene_cpp):
		if inc.lower().replace('_', '') == folded.replace('_', '') + '.h' or inc.lower() == folded + '.h':
			return inc
	return None


def _camel_to_snake(name: str) -> str:
	s = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name)
	return s.lower()


def insert_cases(scene_cpp: str, enum_name: str, prefix: str, insert_before: str | None) -> str:
	nl = detect_newline(scene_cpp)
	funcs = {
		'Initialize': f'{prefix}_Initialize',
		'Update': f'{prefix}_Update',
		'Draw': f'{prefix}_Draw',
		'Finalize': f'{prefix}_Finalize',
	}

	# 各 void Xxx( void ) 内の switch に対して挿入
	# 関数本体を特定してから case を差し込む
	func_pat = re.compile(
		r'(void\s+(Init|Update|Draw|Finalize)\s*\([^)]*\)\s*\{)',
		re.M,
	)

	result = scene_cpp
	# 後ろから置換すると位置がずれないので、マッチを逆順で処理
	matches = list(func_pat.finditer(result))
	# 各関数ブロックの終端を見つけて処理
	edits: list[tuple[int, int, str]] = []
	for m in matches:
		func_name = m.group(2)
		lifecycle = {
			'Init': 'Initialize',
			'Update': 'Update',
			'Draw': 'Draw',
			'Finalize': 'Finalize',
		}[func_name]
		block_start = m.end()
		# 対応する関数の閉じ括弧（簡易: 次の void か EOF まで）
		next_m = None
		for nm in matches:
			if nm.start() > m.start():
				next_m = nm
				break
		block_end = next_m.start() if next_m else len(result)
		block = result[block_start:block_end]

		case_text = (
			f'\t\tcase {enum_name}:{nl}'
			f'\t\t{funcs[lifecycle]}();{nl}'
			f'\t\tbreak;{nl}'
		)

		if insert_before:
			anchor = re.search(
				rf'^[ \t]*case[ \t]+{re.escape(insert_before)}\s*:',
				block,
				re.M,
			)
			if not anchor:
				raise RuntimeError(f'{func_name}: case {insert_before} が見つかりません')
			pos = block_start + anchor.start()
			edits.append((pos, pos, case_text))
		else:
			# default: の直前
			anchor = re.search(r'^[ \t]*default\s*:', block, re.M)
			if not anchor:
				raise RuntimeError(f'{func_name}: default が見つかりません')
			pos = block_start + anchor.start()
			edits.append((pos, pos, case_text))

	for start, end, text in reversed(edits):
		result = result[:start] + text + result[end:]
	return result


def remove_include_for_folder(scene_cpp: str, folder: Path) -> str:
	"""SCENE_XXX フォルダ内の .h に対応する #include を除去。"""
	headers = {p.name for p in folder.glob('*.h')} if folder.is_dir() else set()
	# case から推測したヘッダも候補に
	nl = detect_newline(scene_cpp)
	lines = scene_cpp.splitlines(keepends=True)
	out: list[str] = []
	for line in lines:
		m = re.match(r'#include\s+"([^"]+)"', line)
		if m and m.group(1) in headers:
			continue
		out.append(line)
	return ''.join(out)


def remove_cases(scene_cpp: str, enum_name: str) -> str:
	pat = _case_block_pattern(enum_name)
	new_text, n = pat.subn('', scene_cpp)
	if n == 0:
		raise RuntimeError(f'case {enum_name} が scene.cpp に見つかりません')
	return new_text


# ---------------------------------------------------------------------------
# setupdirectory.txt
# ---------------------------------------------------------------------------

def insert_setupdir(text: str, folder: str) -> str:
	nl = detect_newline(text) if text else '\n'
	entry = f'/{folder}'
	lines = text.splitlines()
	if any(l.strip() == entry for l in lines):
		return text if text.endswith(('\n', '\r\n')) or not text else text + nl

	# 既存 SCENE_* の最後の後、なければ /template の前、それもなければ末尾
	last_scene_idx = -1
	template_idx = -1
	for i, line in enumerate(lines):
		s = line.strip()
		if s.startswith('/SCENE_'):
			last_scene_idx = i
		elif s == '/template':
			template_idx = i

	if last_scene_idx >= 0:
		lines.insert(last_scene_idx + 1, entry)
	elif template_idx >= 0:
		lines.insert(template_idx, entry)
	else:
		lines.append(entry)

	body = nl.join(lines)
	if text.endswith(('\n', '\r\n')):
		body += nl
	return body


def remove_setupdir(text: str, folder: str) -> str:
	nl = detect_newline(text) if text else '\n'
	entry = f'/{folder}'
	lines = [l for l in text.splitlines() if l.strip() != entry]
	body = nl.join(lines)
	if text.endswith(('\n', '\r\n')):
		body += nl
	return body


# ---------------------------------------------------------------------------
# vcxproj / filters
# ---------------------------------------------------------------------------

def insert_include_dirs(vcxproj: str, folder: str, insert_after_folder: str | None) -> str:
	token = f'$(ProjectDir){folder};'
	if token in vcxproj:
		return vcxproj

	def repl(m: re.Match[str]) -> str:
		inner = m.group(1)
		if token in inner:
			return m.group(0)
		if insert_after_folder:
			anchor = f'$(ProjectDir){insert_after_folder};'
			if anchor in inner:
				inner = inner.replace(anchor, anchor + token, 1)
				return f'<AdditionalIncludeDirectories>{inner}</AdditionalIncludeDirectories>'
		# SCENE_* の最後の後
		parts = inner.split(';')
		insert_at = 0
		for i, p in enumerate(parts):
			if 'SCENE_' in p:
				insert_at = i + 1
		parts.insert(insert_at, f'$(ProjectDir){folder}')
		# 空要素を整理せず結合（末尾の %(Additional...) を維持）
		new_inner = ';'.join(p for p in parts if p != '')
		# 元が ; 区切りで空があった場合の簡易復元: join で十分
		return f'<AdditionalIncludeDirectories>{new_inner}</AdditionalIncludeDirectories>'

	return re.sub(
		r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',
		repl,
		vcxproj,
		flags=re.S,
	)


def remove_include_dirs(vcxproj: str, folder: str) -> str:
	token = f'$(ProjectDir){folder};'
	text = vcxproj.replace(token, '')
	text = re.sub(
		rf'\$\(ProjectDir\){re.escape(folder)}(?=;|</AdditionalIncludeDirectories>)',
		'',
		text,
	)
	text = re.sub(r';;+', ';', text)
	return text


def insert_cl_items(vcxproj: str, folder: str, stem: str) -> str:
	cpp_path = f'{folder}\\{stem}.cpp'
	h_path = f'{folder}\\{stem}.h'
	nl = detect_newline(vcxproj)
	scene_compile_re = re.compile(
		r'[ \t]*<ClCompile Include="SCENE_[^"]+"[^>]*(?:/>|>.*?</ClCompile>)\r?\n',
		re.S,
	)
	scene_include_re = re.compile(
		r'[ \t]*<ClInclude Include="SCENE_[^"]+"[^>]*(?:/>|>.*?</ClInclude>)\r?\n',
		re.S,
	)

	if f'Include="{cpp_path}"' not in vcxproj:
		matches = list(scene_compile_re.finditer(vcxproj))
		entry = f'    <ClCompile Include="{cpp_path}" />{nl}'
		if matches:
			pos = matches[-1].end()
			vcxproj = vcxproj[:pos] + entry + vcxproj[pos:]
		else:
			m2 = re.search(r'[ \t]*<ClCompile Include="app\\scene\.cpp"', vcxproj)
			if not m2:
				raise RuntimeError('vcxproj に ClCompile 挿入位置がありません')
			vcxproj = vcxproj[:m2.start()] + entry + vcxproj[m2.start():]

	if f'Include="{h_path}"' not in vcxproj:
		matches = list(scene_include_re.finditer(vcxproj))
		entry = f'    <ClInclude Include="{h_path}" />{nl}'
		if matches:
			pos = matches[-1].end()
			vcxproj = vcxproj[:pos] + entry + vcxproj[pos:]
		else:
			m2 = re.search(r'[ \t]*<ClInclude Include="app\\scene\.h"', vcxproj)
			if not m2:
				raise RuntimeError('vcxproj に ClInclude 挿入位置がありません')
			vcxproj = vcxproj[:m2.start()] + entry + vcxproj[m2.start():]

	return vcxproj


def remove_cl_items_for_folder(vcxproj: str, folder: str) -> str:
	esc = re.escape(folder)
	pat_compile = re.compile(
		r'[ \t]*<ClCompile Include="' + esc + r'\\[^"]+"[^>]*(?:/>|>.*?</ClCompile>)\r?\n',
		re.S,
	)
	pat_include = re.compile(
		r'[ \t]*<ClInclude Include="' + esc + r'\\[^"]+"[^>]*(?:/>|>.*?</ClInclude>)\r?\n',
		re.S,
	)
	vcxproj = pat_compile.sub('', vcxproj)
	vcxproj = pat_include.sub('', vcxproj)
	return vcxproj


def insert_filters(filters_text: str, folder: str, stem: str) -> str:
	cpp_path = f'{folder}\\{stem}.cpp'
	h_path = f'{folder}\\{stem}.h'
	uid = str(uuid.uuid4())
	nl = detect_newline(filters_text)

	if f'Filter Include="{folder}"' not in filters_text:
		filter_entry = (
			f'    <Filter Include="{folder}">{nl}'
			f'      <UniqueIdentifier>{{{uid}}}</UniqueIdentifier>{nl}'
			f'    </Filter>{nl}'
		)
		matches = list(re.finditer(
			r'[ \t]*<Filter Include="SCENE_[^"]+">\s*<UniqueIdentifier>\{[^}]+\}</UniqueIdentifier>\s*</Filter>\r?\n',
			filters_text,
			re.S,
		))
		if matches:
			pos = matches[-1].end()
			filters_text = filters_text[:pos] + filter_entry + filters_text[pos:]
		else:
			m = re.search(r'[ \t]*</ItemGroup>', filters_text)
			if not m:
				raise RuntimeError('filters に Filter 挿入位置がありません')
			filters_text = filters_text[:m.start()] + filter_entry + filters_text[m.start():]

	cpp_entry = (
		f'    <ClCompile Include="{cpp_path}">{nl}'
		f'      <Filter>{folder}</Filter>{nl}'
		f'    </ClCompile>{nl}'
	)
	h_entry = (
		f'    <ClInclude Include="{h_path}">{nl}'
		f'      <Filter>{folder}</Filter>{nl}'
		f'    </ClInclude>{nl}'
	)

	if f'Include="{cpp_path}"' not in filters_text:
		matches = list(re.finditer(
			r'[ \t]*<ClCompile Include="SCENE_[^"]+">\s*<Filter>SCENE_[^<]+</Filter>\s*</ClCompile>\r?\n',
			filters_text,
			re.S,
		))
		if matches:
			pos = matches[-1].end()
			filters_text = filters_text[:pos] + cpp_entry + filters_text[pos:]
		else:
			m = re.search(r'[ \t]*<ClCompile Include="app\\scene\.cpp">', filters_text)
			if not m:
				raise RuntimeError('filters に ClCompile 挿入位置がありません')
			filters_text = filters_text[:m.start()] + cpp_entry + filters_text[m.start():]

	if f'Include="{h_path}"' not in filters_text:
		matches = list(re.finditer(
			r'[ \t]*<ClInclude Include="SCENE_[^"]+">\s*<Filter>SCENE_[^<]+</Filter>\s*</ClInclude>\r?\n',
			filters_text,
			re.S,
		))
		if matches:
			pos = matches[-1].end()
			filters_text = filters_text[:pos] + h_entry + filters_text[pos:]
		else:
			m = re.search(r'[ \t]*<ClInclude Include="app\\scene\.h">', filters_text)
			if not m:
				raise RuntimeError('filters に ClInclude 挿入位置がありません')
			filters_text = filters_text[:m.start()] + h_entry + filters_text[m.start():]

	return filters_text


def remove_filters_for_folder(filters_text: str, folder: str) -> str:
	esc = re.escape(folder)
	# 末尾は改行のみ（次行のインデントを食わない）
	filters_text = re.sub(
		r'[ \t]*<Filter Include="' + esc + r'">\s*<UniqueIdentifier>\{[^}]+\}</UniqueIdentifier>\s*</Filter>\r?\n',
		'',
		filters_text,
		flags=re.S,
	)
	filters_text = re.sub(
		r'[ \t]*<ClCompile Include="' + esc + r'\\[^"]+">\s*<Filter>' + esc + r'</Filter>\s*</ClCompile>\r?\n',
		'',
		filters_text,
		flags=re.S,
	)
	filters_text = re.sub(
		r'[ \t]*<ClInclude Include="' + esc + r'\\[^"]+">\s*<Filter>' + esc + r'</Filter>\s*</ClInclude>\r?\n',
		'',
		filters_text,
		flags=re.S,
	)
	filters_text = re.sub(
		r'[ \t]*<ClCompile Include="' + esc + r'\\[^"]+"\s*/>\r?\n',
		'',
		filters_text,
	)
	filters_text = re.sub(
		r'[ \t]*<ClInclude Include="' + esc + r'\\[^"]+"\s*/>\r?\n',
		'',
		filters_text,
	)
	return filters_text


# ---------------------------------------------------------------------------
# template copy
# ---------------------------------------------------------------------------

def create_scene_files(names: SceneNames) -> Path:
	src_h = TEMPLATE_DIR / 'template.h'
	src_cpp = TEMPLATE_DIR / 'template.cpp'
	if not src_h.exists() or not src_cpp.exists():
		raise RuntimeError('template/template.h または template.cpp がありません')

	dest_dir = ROOT / names.folder
	if dest_dir.exists():
		raise RuntimeError(f'フォルダが既に存在します: {names.folder}')
	dest_dir.mkdir(parents=True)

	h_text, h_enc, h_bom = read_text(src_h)
	cpp_text, cpp_enc, cpp_bom = read_text(src_cpp)

	def transform(text: str) -> str:
		# template.cpp 先頭は "#include \"Template.h\""（ファイル名 template.h とは大文字小文字が異なる）
		text = text.replace('Template.h', names.header)
		text = text.replace('template.h', names.header)
		text = text.replace('Template_', f'{names.prefix}_')
		text = text.replace('g_pTemplateText', f'g_p{names.prefix}Text')
		text = text.replace('"Template"', f'"{names.display}"')
		text = text.replace('SCENE_Template', names.enum)
		return text

	write_text(dest_dir / names.header, transform(h_text), h_enc, h_bom)
	write_text(dest_dir / names.source, transform(cpp_text), cpp_enc, cpp_bom)
	return dest_dir


# ---------------------------------------------------------------------------
# encoding converter
# ---------------------------------------------------------------------------

def run_encoding_converter(dirs: list[Path]) -> None:
	sys.path.insert(0, str(TOOL_DIR))
	try:
		import encoding_converter as ec
	except ImportError:
		print('WARNING: encoding_converter を import できませんでした')
		return

	editorconfig_path = ROOT / '.editorconfig'
	if not editorconfig_path.exists():
		editorconfig_path.write_text(ec.EDITORCONFIG_DEFAULT, encoding='utf-8')
	config = ec.parse_editorconfig(editorconfig_path)

	print()
	print('--- encoding_converter ---')
	for d in dirs:
		if not d.is_dir():
			continue
		print(f'  --- {d} ---')
		ec.convert_directory(d, config, ROOT)


# ---------------------------------------------------------------------------
# add / delete flows
# ---------------------------------------------------------------------------

def cmd_add() -> None:
	scene_h_text, h_enc, h_bom = read_text(SCENE_H)
	enums = parse_scene_enums(scene_h_text)
	scenes = active_scenes(enums)

	raw = input('新規シーン名 (例: pause / PAUSE): ').strip()
	if not raw:
		print('キャンセルしました')
		return
	names = normalize_name(raw)

	if names.enum in SKIP_ENUM:
		raise RuntimeError(f'{names.enum} は予約・例外シーンのため作成できません')
	if names.enum in enums:
		raise RuntimeError(f'{names.enum} は既に定義されています')
	if (ROOT / names.folder).exists():
		raise RuntimeError(f'フォルダ {names.folder} は既に存在します')

	print()
	insert_before = ask_insert_position(scenes)

	# 直前フォルダ（AdditionalIncludeDirectories 用）
	if insert_before is None:
		after_folder = scenes[-1] if scenes else None  # SCENE_TITLE → folder name same
	else:
		idx = scenes.index(insert_before)
		after_folder = scenes[idx - 1] if idx > 0 else None
	# enum 名とフォルダ名は通常一致
	after_folder_name = after_folder  # SCENE_GAME など

	left = after_folder if after_folder else '先頭'
	right = insert_before if insert_before else 'SCENE_MAX'
	if after_folder is None and insert_before is not None:
		insert_label = f'先頭（{insert_before}の前）'
	else:
		insert_label = f'{left} と {right} の間'

	print()
	print('=== 変更予定 ===')
	print(f'  フォルダ : {names.folder}/')
	print(f'  ファイル : {names.header}, {names.source}')
	print(f'  定数     : {names.enum}')
	print(f'  関数     : {names.prefix}_Initialize など')
	print(f'  挿入位置 : {insert_label}')
	print(f'  更新     : scene.h / scene.cpp / setupdirectory.txt / *.vcxproj / *.filters')
	if not ask_yes_no('実行しますか?', default=False):
		print('キャンセルしました')
		return

	proj, filters_path = find_vcxproj_files(ROOT)
	scene_cpp_text, c_enc, c_bom = read_text(SCENE_CPP)
	setup_text, s_enc, s_bom = read_text(SETUPDIR) if SETUPDIR.exists() else ('', 'utf-8', False)
	proj_text, p_enc, p_bom = read_text(proj)
	filt_text, f_enc, f_bom = read_text(filters_path)

	create_scene_files(names)

	new_h = insert_enum(scene_h_text, names.enum, insert_before)
	new_cpp = insert_include(scene_cpp_text, names.header, insert_before, scenes)
	new_cpp = insert_cases(new_cpp, names.enum, names.prefix, insert_before)
	new_setup = insert_setupdir(setup_text, names.folder)
	new_proj = insert_include_dirs(proj_text, names.folder, after_folder_name)
	new_proj = insert_cl_items(new_proj, names.folder, names.stem)
	new_filt = insert_filters(filt_text, names.folder, names.stem)

	write_text(SCENE_H, new_h, h_enc, h_bom)
	write_text(SCENE_CPP, new_cpp, c_enc, c_bom)
	write_text(SETUPDIR, new_setup, s_enc, s_bom)
	write_text(proj, new_proj, p_enc, p_bom)
	write_text(filters_path, new_filt, f_enc, f_bom)

	run_encoding_converter([ROOT / 'app', ROOT / names.folder])

	print()
	print(f'完了: {names.enum} を追加しました')


def cmd_delete() -> None:
	scene_h_text, h_enc, h_bom = read_text(SCENE_H)
	enums = parse_scene_enums(scene_h_text)
	scenes = active_scenes(enums)
	if not scenes:
		print('削除できるシーンがありません')
		return

	choice = ask_choice('削除するシーン:', scenes)
	enum_name = scenes[choice]
	folder_name = enum_name  # SCENE_XXX
	folder = ROOT / folder_name

	if enum_name == 'SCENE_TITLE':
		print()
		print('WARNING: SCENE_TITLE はデフォルト初期シーンです。削除すると起動時に問題が起きる可能性があります。')

	print()
	print('=== 削除予定 ===')
	print(f'  定数     : {enum_name}')
	print(f'  フォルダ : {folder_name}/ {"(存在)" if folder.is_dir() else "(なし — 登録のみ削除)"}')
	print(f'  更新     : scene.h / scene.cpp / setupdirectory.txt / *.vcxproj / *.filters')
	if not ask_yes_no('本当に削除しますか?', default=False):
		print('キャンセルしました')
		return

	proj, filters_path = find_vcxproj_files(ROOT)
	scene_cpp_text, c_enc, c_bom = read_text(SCENE_CPP)
	setup_text, s_enc, s_bom = read_text(SETUPDIR) if SETUPDIR.exists() else ('', 'utf-8', False)
	proj_text, p_enc, p_bom = read_text(proj)
	filt_text, f_enc, f_bom = read_text(filters_path)

	# include 除去はフォルダ削除前に
	new_cpp = remove_include_for_folder(scene_cpp_text, folder) if folder.is_dir() else scene_cpp_text
	# フォルダが無い場合、case からヘッダ推測
	if not folder.is_dir():
		guessed = _guess_header_for_enum(scene_cpp_text, enum_name)
		if guessed and f'#include "{guessed}"' in new_cpp:
			new_cpp = new_cpp.replace(f'#include "{guessed}"\r\n', '').replace(f'#include "{guessed}"\n', '')

	new_cpp = remove_cases(new_cpp, enum_name)
	new_h = remove_enum(scene_h_text, enum_name)
	new_setup = remove_setupdir(setup_text, folder_name)
	new_proj = remove_include_dirs(proj_text, folder_name)
	new_proj = remove_cl_items_for_folder(new_proj, folder_name)
	new_filt = remove_filters_for_folder(filt_text, folder_name)

	write_text(SCENE_H, new_h, h_enc, h_bom)
	write_text(SCENE_CPP, new_cpp, c_enc, c_bom)
	write_text(SETUPDIR, new_setup, s_enc, s_bom)
	write_text(proj, new_proj, p_enc, p_bom)
	write_text(filters_path, new_filt, f_enc, f_bom)

	if folder.is_dir():
		shutil.rmtree(folder)
		print(f'  削除: {folder_name}/')

	run_encoding_converter([ROOT / 'app'])

	print()
	print(f'完了: {enum_name} を削除しました')


def main() -> None:
	print('=== シーン管理 ===')
	print()
	mode = ask_choice('操作を選んでください:', ['新規作成', '削除'])
	print()
	if mode == 0:
		cmd_add()
	else:
		cmd_delete()


if __name__ == '__main__':
	try:
		main()
	except KeyboardInterrupt:
		print()
		print('中断しました')
		sys.exit(1)
	except Exception:
		print()
		print('=== Error ===')
		traceback.print_exc()
		input('\nPress Enter to exit...')
		sys.exit(1)
