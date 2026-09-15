@echo off
setlocal EnableExtensions
chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

set "ROOT=%~dp0.."
pushd "%ROOT%"
if errorlevel 1 (
    echo プロジェクトルートへ移動できませんでした。
    echo.
    pause
    exit /b 1
)

echo.
echo 万博アセット取得ツール
echo.
echo Project PLATEAU の公式データをダウンロードし、ゲーム用モデルへ変換します。
echo.
echo 必要な環境:
echo   - Python 3.10 以降
echo   - Node.js 18 以降（npm を含む）
echo   - curl.exe（Windows 標準）
echo.
echo 検出結果:
set "DEP_OK=1"
where python >nul 2>&1
if errorlevel 1 (
    echo   Python : 見つかりません
    set "DEP_OK=0"
) else (
    for /f "delims=" %%V in ('python --version 2^>^&1') do echo   Python : %%V
)
where node >nul 2>&1
if errorlevel 1 (
    echo   Node.js: 見つかりません
    set "DEP_OK=0"
) else (
    for /f "delims=" %%V in ('node --version 2^>^&1') do echo   Node.js: %%V
)
where npm >nul 2>&1
if errorlevel 1 (
    echo   npm    : 見つかりません
    set "DEP_OK=0"
) else (
    for /f "delims=" %%V in ('npm --version 2^>^&1') do echo   npm    : %%V
)
where curl.exe >nul 2>&1
if errorlevel 1 (
    echo   curl   : 見つかりません
    set "DEP_OK=0"
) else (
    for /f "tokens=1,2 delims= " %%A in ('curl.exe --version 2^>^&1') do (
        echo   curl   : %%A %%B
        goto :curl_version_done
    )
)
:curl_version_done
if "%DEP_OK%"=="0" (
    echo.
    echo 必要な環境が不足しています。インストールしてから再実行してください。
    set "EXIT_CODE=3"
    goto :finish
)
echo.
echo 続ける前に、最新の利用条件を確認してください:
echo https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025
echo.
echo 注意:
echo - データには第三者の著作権・商標が含まれる場合があります。
echo - 公式の利用条件（非営利などの制限を含む）に従ってください。
echo - 元データの再配布や、公式作品であるかのような表示はしないでください。
echo - 公開や営利利用の前に、最新の利用条件を再確認してください。
echo.
set /p "AGREE=利用条件を確認・同意したら AGREE と入力: "
if /I not "%AGREE%"=="AGREE" (
    echo 同意が確認できませんでした。中止します。
    set "EXIT_CODE=2"
    goto :finish
)

if not exist "data_original" mkdir "data_original"
if not exist "data_original\27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip" (
    echo [1/8] 公式 3D Tiles をダウンロードしています...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip" "https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025/resource/c3cb4e3a-9190-4c71-a8d4-cc519464700b/download/27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [1/8] 公式 3D Tiles は既にあります。
)
if not exist "data_original\27999_osaka-shi_city_2025_citygml_1_op.zip" (
    echo [2/8] 公式 CityGML をダウンロードしています...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_citygml_1_op.zip" "https://assets.cms.plateau.reearth.io/assets/9d/d092b9-a371-499d-832e-de6fdf538b22/27999_osaka-shi_city_2025_citygml_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [2/8] 公式 CityGML は既にあります。
)
if not exist "data_original\27999_osaka-shi_city_2025_ortho_1_op.zip" (
    echo [3/8] 公式オルソ画像をダウンロードしています...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_ortho_1_op.zip" "https://assets.cms.plateau.reearth.io/assets/72/3ad7c7-896b-455b-b3b1-7799fae342e4/27999_osaka-shi_city_2025_ortho_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [3/8] 公式オルソ画像は既にあります。
)

echo [4/8] ZIP を展開しています...
python "tool\download_expo_assets.py"
if errorlevel 1 goto :failed

echo [5/8] Node.js の依存関係をインストールしています...
call npm install --prefix tool
if errorlevel 1 goto :failed

echo [6/8] LOD1、LOD2、床、大屋根リングを生成しています...
python "tool\prepare_expo_model.py" --tileset "data_original\3dtiles\27999_osaka-shi_city_2025_citygml_1_op_bldg_lod1\tileset.json" --output-name "expo_tile.glb" --runtime-output "asset\expomodel\expo_tile.glb"
if errorlevel 1 goto :failed
python "tool\prepare_expo_model.py" --tileset "data_original\3dtiles\27999_osaka-shi_city_2025_citygml_1_op_bldg_lod2\tileset.json" --output-name "expo_tile_lod2.glb" --runtime-output "asset\expomodel\expo_tile_lod2.glb" --max-tiles 4 --leaf-only
if errorlevel 1 goto :failed
python "tool\prepare_expo_floor.py"
if errorlevel 1 goto :failed
python "tool\prepare_expo_ring.py"
if errorlevel 1 goto :failed

echo [7/8] LOD3 パビリオンを生成しています...
python "tool\prepare_expo_pavilion.py" --all-names
if errorlevel 1 goto :failed

echo [8/8] Better Co-Being を簡略化しています...
node "tool\simplify_glb.cjs" --config "tool\expo_simplify.json"
if errorlevel 1 goto :failed

echo.
echo 万博アセットの生成が完了しました。
echo ゲーム用モデル: asset\expomodel
echo.
python "tool\download_expo_assets.py" --report-data-dirs
if errorlevel 1 (
    set "EXIT_CODE=0"
    goto :finish
)
echo.
echo 削除しても asset\expomodel のゲーム用モデルは残ります。
echo 残すと、再変換時のダウンロードを省略できます。
set /p "CLEAN=一時フォルダ data_* を削除しますか？ (Y/N): "
if /I "%CLEAN%"=="Y" (
    python "tool\download_expo_assets.py" --delete-data-dirs
    if errorlevel 1 (
        echo 一時フォルダの削除に失敗しました。
        set "EXIT_CODE=6"
        goto :finish
    )
    echo 一時フォルダを削除しました。
) else (
    echo 一時フォルダは残しました。
)
set "EXIT_CODE=0"
goto :finish

:download_failed
echo ダウンロードに失敗しました。ネットワークと公式URLを確認してください。
set "EXIT_CODE=4"
goto :finish

:failed
echo アセット変換に失敗しました。
set "EXIT_CODE=5"

:finish
popd
echo.
pause
exit /b %EXIT_CODE%
