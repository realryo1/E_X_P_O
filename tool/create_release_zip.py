"""GitHub Actions の build.yml に準拠したローカル配布ZIP作成ツール。"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
WORKFLOW_PATH = ROOT_DIR / ".github" / "workflows" / "build.yml"


def read_workflow() -> str:
    if not WORKFLOW_PATH.is_file():
        raise RuntimeError(f"ワークフローが見つかりません: {WORKFLOW_PATH}")
    return WORKFLOW_PATH.read_text(encoding="utf-8-sig")


def get_run_block(workflow: str, step_name: str) -> str:
    lines = workflow.splitlines()
    step_index = next(
        (
            index
            for index, line in enumerate(lines)
            if re.match(r"^\s*-\s+name:\s*" + re.escape(step_name) + r"\s*$", line)
        ),
        None,
    )
    if step_index is None:
        raise RuntimeError(f"build.yml にステップがありません: {step_name}")

    run_index = next(
        (index for index in range(step_index + 1, len(lines))
         if re.match(r"^\s*run:", lines[index])),
        None,
    )
    if run_index is None:
        raise RuntimeError(f"ステップに run がありません: {step_name}")

    run_value = lines[run_index].split("run:", 1)[1].strip()
    if run_value and run_value != "|":
        return run_value

    end_index = next(
        (
            index
            for index in range(run_index + 1, len(lines))
            if re.match(r"^\s*-\s+(?:name|uses):", lines[index])
        ),
        len(lines),
    )
    return "\n".join(lines[run_index + 1:end_index])


def get_msbuild() -> Path:
    program_files_x86 = os.environ.get("ProgramFiles(x86)", "")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-requires", "Microsoft.Component.MSBuild",
             "-property", "installationPath"],
            capture_output=True,
            text=True,
            check=False,
        )
        installation_path = result.stdout.strip()
        if installation_path:
            for relative_path in (
                "MSBuild/Current/Bin/MSBuild.exe",
                "MSBuild/15.0/Bin/MSBuild.exe",
            ):
                candidate = Path(installation_path) / relative_path
                if candidate.is_file():
                    return candidate

    where_result = shutil.which("msbuild")
    if where_result:
        return Path(where_result)

    fallback_paths = (
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community/MSBuild/Current/Bin/MSBuild.exe"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Professional/MSBuild/Current/Bin/MSBuild.exe"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise/MSBuild/Current/Bin/MSBuild.exe"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community/MSBuild/Current/Bin/MSBuild.exe"),
    )
    for candidate in fallback_paths:
        if candidate.is_file():
            return candidate
    raise RuntimeError("MSBuild.exe が見つかりません。Visual Studio をインストールしてください。")


def command_arguments(command_block: str) -> list[str]:
    command = next(
        (line.strip() for line in command_block.splitlines()
         if re.match(r"^\s*msbuild(?:\s|$)", line, re.IGNORECASE)),
        None,
    )
    if command is None:
        raise RuntimeError("build.yml から msbuild コマンドを取得できません。")
    rest = re.sub(r"^\s*msbuild\s*", "", command, flags=re.IGNORECASE)
    tokens = re.findall(r'"[^"]*"|[^\s"]+="[^"]*"|\S+', rest)
    return [token.replace('"', "") for token in tokens]


def run_msbuild(msbuild: Path, block: str, label: str) -> None:
    arguments = command_arguments(block)
    print(f"[{label}] {' '.join([str(msbuild), *arguments])}", flush=True)
    completed = subprocess.run([str(msbuild), *arguments], cwd=ROOT_DIR, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{label} に失敗しました (終了コード: {completed.returncode})")


def workflow_value(workflow: str, key: str) -> str:
    match = re.search(r"^\s*" + re.escape(key) + r":\s*(\S+)\s*$", workflow, re.MULTILINE)
    if not match:
        raise RuntimeError(f"build.yml から値を取得できません: {key}")
    return match.group(1).strip("\"'")


def get_archive_name(workflow: str, version: str) -> str:
    version_block = get_run_block(workflow, "Get Version")
    match = re.search(r'\$archiveName\s*=\s*"([^"]+)"', version_block)
    if not match:
        raise RuntimeError("build.yml からアーカイブ名の規則を取得できません。")
    template = match.group(1)
    return template.replace("$version", version).replace("${version}", version)


def staging_destination(destination_text: str, staging_dir: Path) -> Path:
    relative = destination_text.replace("\\", "/").strip("/")
    if relative == "release":
        return staging_dir
    prefix = "release/"
    if relative.startswith(prefix):
        return staging_dir / relative[len(prefix):]
    raise RuntimeError(f"想定外のコピー先です: {destination_text}")


def copy_item(source_text: str, destination_text: str, staging_dir: Path, optional: bool) -> None:
    source = (ROOT_DIR / source_text).resolve()
    destination = staging_destination(destination_text, staging_dir)
    matches = list(source.parent.glob(source.name)) if any(char in source.name for char in "*?[") else [source]
    matches = [path for path in matches if path.exists()]
    if not matches:
        if optional:
            print(f"[Collect] スキップ（未検出）: {source_text}", flush=True)
            return
        raise RuntimeError(f"収集対象が見つかりません: {source_text}")

    print(f"[Collect] {source_text} -> {destination_text}", flush=True)
    for match in matches:
        if match.is_dir():
            target = destination / match.name
            shutil.copytree(match, target, dirs_exist_ok=True)
        else:
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(match, destination / match.name)


def collect_files(workflow: str, staging_dir: Path) -> None:
    block = get_run_block(workflow, "Collect files")
    new_items = re.findall(
        r'-ItemType\s+Directory\s+-Path\s+"([^"]+)"',
        block,
        re.IGNORECASE,
    )
    if not new_items:
        raise RuntimeError("build.yml から staging ディレクトリを取得できません。")
    staging_name = new_items[0].replace("\\", "/")
    if staging_name != "release":
        raise RuntimeError(f"想定外の staging ディレクトリです: {staging_name}")
    staging_dir.mkdir(parents=True, exist_ok=True)
    for directory in new_items:
        relative_directory = directory.replace("\\", "/")
        if relative_directory == staging_name or relative_directory.startswith(f"{staging_name}/"):
            (staging_dir.parent / relative_directory).mkdir(parents=True, exist_ok=True)

    optional = False
    for line in block.splitlines():
        if re.search(r"\bif\s*\(\s*Test-Path\b", line, re.IGNORECASE):
            optional = True
        copy_match = re.search(
            r"Copy-Item\s+-Path\s+\"([^\"]+)\"\s+-Destination\s+\"([^\"]+)\"",
            line,
            re.IGNORECASE,
        )
        if copy_match:
            copy_item(copy_match.group(1), copy_match.group(2), staging_dir, optional)
        if optional and re.search(r"\}", line):
            optional = False


def remove_expo_assets_from_release(staging_dir: Path) -> None:
    """公開用ZIPへ万博由来の生成物を混入させない。"""
    expomodel_dir = staging_dir / "asset" / "expomodel"
    if expomodel_dir.is_dir():
        shutil.rmtree(expomodel_dir)
        print("[Collect] asset/expomodel をリリースから除外", flush=True)

    collision_dir = staging_dir / "asset" / "collision"
    for collision_file in collision_dir.glob("expo_*.bin"):
        collision_file.unlink()
        print(f"[Collect] {collision_file.relative_to(staging_dir)} をリリースから除外", flush=True)


def unique_output_path(directory: Path, archive_name: str) -> Path:
    candidate = directory / archive_name
    if not candidate.exists():
        return candidate
    stem = Path(archive_name).stem
    suffix = Path(archive_name).suffix
    counter = 1
    while True:
        candidate = directory / f"{stem}.{counter}{suffix}"
        if not candidate.exists():
            return candidate
        counter += 1


STORED_SUFFIXES = {".bin", ".cso", ".dll", ".exe", ".glb", ".jpeg", ".jpg", ".png", ".zip"}


def create_zip(staging_dir: Path, output_path: Path) -> None:
    paths = sorted(staging_dir.rglob("*"))
    total = len(paths)
    print(f"[Zip] {output_path.name} を作成します ({total} 件)", flush=True)
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as archive:
        for index, path in enumerate(paths, start=1):
            archive_name = path.relative_to(staging_dir).as_posix()
            if path.is_dir():
                archive.writestr(f"{archive_name}/", b"")
            else:
                compression = (
                    zipfile.ZIP_STORED
                    if path.suffix.lower() in STORED_SUFFIXES
                    else zipfile.ZIP_DEFLATED
                )
                archive.write(path, archive_name, compress_type=compression)
            if index == total or index % 50 == 0:
                print(f"[Zip] {index}/{total}", flush=True)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)

    print("配布ZIP作成を開始します", flush=True)
    workflow = read_workflow()
    version_file = ROOT_DIR / workflow_value(workflow, "VERSION_FILE")
    version = version_file.read_text(encoding="utf-8-sig").strip() if version_file.is_file() else "1"
    if not version:
        version = "1"

    msbuild = get_msbuild()
    run_msbuild(msbuild, get_run_block(workflow, "Clean"), "Clean")
    run_msbuild(msbuild, get_run_block(workflow, "Build with MSBuild"), "Build")
    print("[Build] 完了。ファイル収集を開始します", flush=True)

    archive_name = get_archive_name(workflow, version)
    output_path = unique_output_path(Path(__file__).resolve().parent, archive_name)
    with tempfile.TemporaryDirectory(prefix="expogame_release_") as temporary_directory:
        staging_dir = Path(temporary_directory) / "release"
        collect_files(workflow, staging_dir)
        remove_expo_assets_from_release(staging_dir)
        create_zip(staging_dir, output_path)

    print(f"作成しました: {output_path}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"エラー: {error}", file=sys.stderr, flush=True)
        raise SystemExit(1)
