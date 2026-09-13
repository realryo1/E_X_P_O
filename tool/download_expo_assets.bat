@echo off
setlocal EnableExtensions
chcp 65001 >nul

set "ROOT=%~dp0.."
pushd "%ROOT%"
if errorlevel 1 (
    echo Cannot move to the project root.
    echo.
    pause
    exit /b 1
)

echo.
echo Expo asset downloader
echo.
echo This downloads official Project PLATEAU Expo data and converts it locally.
echo.
echo Review the latest terms before continuing:
echo https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025
echo.
echo NOTICE:
echo - The data may contain third-party copyrights and trademarks.
echo - Follow the official terms, including non-commercial use restrictions.
echo - Do not redistribute source data or imply official endorsement.
echo - Recheck the latest terms before publishing or commercial use.
echo.
set /p "AGREE=Type AGREE after reviewing and accepting the terms: "
if /I not "%AGREE%"=="AGREE" (
    echo Agreement was not confirmed. Aborting.
    set "EXIT_CODE=2"
    goto :finish
)

where python >nul 2>&1
if errorlevel 1 (
    echo Python was not found. Install Python 3.10 or later.
    set "EXIT_CODE=3"
    goto :finish
)
where node >nul 2>&1
if errorlevel 1 (
    echo Node.js was not found. Install Node.js 18 or later.
    set "EXIT_CODE=3"
    goto :finish
)
where npm >nul 2>&1
if errorlevel 1 (
    echo npm was not found. Check the Node.js installation.
    set "EXIT_CODE=3"
    goto :finish
)
where curl.exe >nul 2>&1
if errorlevel 1 (
    echo curl.exe was not found. A Windows curl installation is required.
    set "EXIT_CODE=3"
    goto :finish
)

if not exist "data_original" mkdir "data_original"
if not exist "data_original\27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip" (
    echo [1/8] Downloading official 3D Tiles...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip" "https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025/resource/c3cb4e3a-9190-4c71-a8d4-cc519464700b/download/27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [1/8] Official 3D Tiles already exist.
)
if not exist "data_original\27999_osaka-shi_city_2025_citygml_1_op.zip" (
    echo [2/8] Downloading official CityGML...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_citygml_1_op.zip" "https://assets.cms.plateau.reearth.io/assets/9d/d092b9-a371-499d-832e-de6fdf538b22/27999_osaka-shi_city_2025_citygml_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [2/8] Official CityGML already exists.
)
if not exist "data_original\27999_osaka-shi_city_2025_ortho_1_op.zip" (
    echo [3/8] Downloading official orthophoto...
    curl.exe --fail --location --retry 3 -o "data_original\27999_osaka-shi_city_2025_ortho_1_op.zip" "https://assets.cms.plateau.reearth.io/assets/72/3ad7c7-896b-455b-b3b1-7799fae342e4/27999_osaka-shi_city_2025_ortho_1_op.zip"
    if errorlevel 1 goto :download_failed
) else (
    echo [3/8] Official orthophoto already exists.
)

echo [4/8] Extracting ZIP files...
python "tool\download_expo_assets.py"
if errorlevel 1 goto :failed

echo [5/8] Installing Node.js dependencies...
call npm install --prefix tool
if errorlevel 1 goto :failed

echo [6/8] Generating LOD1, LOD2, floor, and roof ring...
python "tool\prepare_expo_model.py" --tileset "data_original\3dtiles\27999_osaka-shi_city_2025_citygml_1_op_bldg_lod1\tileset.json" --output-name "expo_tile.glb" --runtime-output "asset\expomodel\expo_tile.glb"
if errorlevel 1 goto :failed
python "tool\prepare_expo_model.py" --tileset "data_original\3dtiles\27999_osaka-shi_city_2025_citygml_1_op_bldg_lod2\tileset.json" --output-name "expo_tile_lod2.glb" --runtime-output "asset\expomodel\expo_tile_lod2.glb" --max-tiles 4 --leaf-only
if errorlevel 1 goto :failed
python "tool\prepare_expo_floor.py"
if errorlevel 1 goto :failed
python "tool\prepare_expo_ring.py"
if errorlevel 1 goto :failed

echo [7/8] Generating LOD3 pavilions...
python "tool\prepare_expo_pavilion.py" --all-names
if errorlevel 1 goto :failed

echo [8/8] Simplifying Better Co-Being...
node "tool\simplify_glb.cjs" --config "tool\expo_simplify.json"
if errorlevel 1 goto :failed

echo.
echo Expo assets were generated successfully.
echo Runtime assets: asset\expomodel
set "EXIT_CODE=0"
goto :finish

:download_failed
echo Download failed. Check the network and official URL.
set "EXIT_CODE=4"
goto :finish

:failed
echo Asset conversion failed.
set "EXIT_CODE=5"

:finish
popd
echo.
pause
exit /b %EXIT_CODE%
