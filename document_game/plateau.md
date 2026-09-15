# PLATEAU

## 1. この資料の目的

この資料は、`expogame`で利用するProject PLATEAUのデータについて、
出典、利用条件、3D Tiles、変換手順、実行時の配置と座標系をまとめたものである。

ゲームの目的、シーン、操作、触るファイルは
[game_specification.md](game_specification.md)を参照する。
次に何をするかは
[task_list.md](task_list.md)を参照する。

## 2. データの出典

- データセット名: 2025年大阪・関西万博会場 3D都市モデル（Project PLATEAU）
- 作成者: 国土交通省都市局 国際・デジタル政策課
- 配布元: G空間情報センター
- データセットページ:
  <https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025>
- 3D Tiles / MVTリソース:
  <https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025/resource/c3cb4e3a-9190-4c71-a8d4-cc519464700b>
- CityGMLリソース（建築物・地形・都市設備）:
  データセットページの CityGML ZIP（v4）
- GeoTIFFリソース（会期中オルソ、40cm、EPSG:6668）:
  データセットページの GeoTIFF ZIP（v4）
- 取得日: 2026年9月9日（日本時間）

今回取得した公式ZIPの配布URL:

- 3D Tiles / MVT:
  <https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025/resource/c3cb4e3a-9190-4c71-a8d4-cc519464700b/download/27999_osaka-shi_city_2025_3dtiles_mvt_1_op.zip>
- CityGML:
  <https://assets.cms.plateau.reearth.io/assets/9d/d092b9-a371-499d-832e-de6fdf538b22/27999_osaka-shi_city_2025_citygml_1_op.zip>
- GeoTIFF オルソ:
  <https://assets.cms.plateau.reearth.io/assets/72/3ad7c7-896b-455b-b3b1-7799fae342e4/27999_osaka-shi_city_2025_ortho_1_op.zip>

作業データは `data_original/` に置く。ZIPと展開物は配布しない。

CityGML ZIPは再取得しない。既にあればそれを使う。

```text
data_original/27999_osaka-shi_city_2025_citygml_1_op.zip
```

再ダウンロードが必要なときだけ:

```powershell
curl.exe -L -o "data_original/27999_osaka-shi_city_2025_citygml_1_op.zip" "https://assets.cms.plateau.reearth.io/assets/9d/d092b9-a371-499d-832e-de6fdf538b22/27999_osaka-shi_city_2025_citygml_1_op.zip"
```

ファイル名の検索:

```powershell
python -c "import zipfile; z=zipfile.ZipFile('data_original/27999_osaka-shi_city_2025_citygml_1_op.zip');
[print(n) for n in z.namelist() if 'Czech' in n or 'Ochiai' in n]"
```

## 3. 利用条件と出典表示

このデータには、博覧会協会その他第三者が保有する著作権・商標などの
知的財産が含まれる。利用時は公式ページの独自利用規約を確認し、
規約に同意したうえで利用する。

公式案内では無償で利用できる一方、商業目的、販売促進、広告利用、
商品化などの営利目的での使用は認められていない。公開時は必ず最新の
利用規約を再確認する。

- 元データのZIP、`tileset.json`、`b3dm`、元テクスチャを配布物へ同梱しない。
- Project PLATEAU、大阪・関西万博、博覧会協会、各出展者・設計者の
  公式作品と誤認させない。
- ロゴ、キャラクター、展示映像、企業名、商標を別途利用する場合は、
  それぞれの権利条件を確認する。
- 将来、販売・広告・企業案件へ転用する場合は利用許諾を改めて確認する。

作品や動画へ記載する文:

```text
本作品は、国土交通省「Project PLATEAU
2025年大阪・関西万博会場3D都市モデル」を利用しています。

公式データを作者が独自に変換・加工し、
自作DX11フレームワーク上でゲーム用に実装した非公式作品です。

大阪・関西万博、公益社団法人2025年日本国際博覧会協会、
各パビリオン出展者・設計者とは関係ありません。

元データ自体の再配布は行っていません。
```

## 4. 3D Tilesの基本

3D Tilesは、都市モデルをタイル単位で配信・描画する形式である。
`tileset.json`はモデル本体ではなく、次の情報を持つ入口である。

- タイル階層
- `children`
- `content.uri` / `content.url`
- `boundingVolume`
- `geometricError`
- `refine`
- 座標変換行列

典型的な構成:

```text
3dtiles/
├─ tileset.json
├─ .../tileset.json
├─ .../*.b3dm
├─ .../*.glb
└─ .../*.(png|jpg|webp|ktx2)
```

`tileset.json`だけでは表示できない。相対参照をたどり、
子tileset、`b3dm`、`glb`、テクスチャを解決する必要がある。

LOD2の実行時マニフェスト `asset/expomodel/expo_tiles_lod2.txt` では、
各葉タイルの `boundingVolume.region` を `tile` 行へ保持する。
`region` は `[west, south, east, north, minHeight, maxHeight]` の6値で、
経度・緯度はラジアン、高さはメートルである。`SCENE_GAME` は8隅を
GRS80 ECEFへ変換し、既存の参照RTC基準のENUと `0.002 * 100` のスケールを
適用してワールド境界を作る。建物の固定Yオフセット `-9.010` も適用し、
保守的な境界球としてLOD2本体と未パンチLOD2遠景の描画時カリングに使う。
このカリングは描画呼び出しを省略するだけで、LOD3の距離ストリーミングは変更しない。

## 5. 今回確認した実データ

公式ZIPを`data_original/3dtiles/`へ展開した。
元ZIPは作業用であり、ソースや配布物へ含めない。

- `tileset.json`: 5個
- `b3dm`: 201個
- 選択したLOD: `lod2`（フォールバック用に`lod1`も保持）
- 選択したtileset:
  `27999_osaka-shi_city_2025_citygml_1_op_bldg_lod2/tileset.json`
- 選択したタイル: 葉タイル `data/data0.b3dm`〜`data/data3.b3dm`（親`data/data4.b3dm`はREPLACE重複を避けるため複数表示から除外）
- 選択タイルの参照切れ: 0件
- 選択タイルの画像数: 各1（埋め込みWebP）
- 選択タイルの元GLB拡張: `CESIUM_RTC`、`EXT_texture_webp`、`KHR_draco_mesh_compression`

LOD1はテクスチャを持たない軽量タイルとして残している。フォールバック時は白色の簡易表示になる。

手元の3D Tiles ZIPを再確認した結果、`frn`（都市設備）フォルダは含まれない。
中身は建築物 `bldg` の LOD1 / LOD2 / LOD3 である。公式説明では3D Tilesに
`frn` LOD3が含まれる場合があるが、このZIPには無かった。
LOD2の`gml:name`にもリング本体は無い。

CityGML ZIP（v4）の内訳（エントリ約1161、うち appearance 画像866）:

- 手元ZIP: `data_original/27999_osaka-shi_city_2025_citygml_1_op.zip`
- 部分展開: `data_original/citygml/`（必要なJPGだけ。ZIP全展開はしない）
- `udx/bldg/`: 建築物GML 5本。`gml:name`に「大屋根リング」は無い
- `udx/frn/`: 都市設備。`51357370` と `51357380` に `gml:name`「大屋根リング」が156件
- `udx/dem/`: 地形。床の初回実装では使わない（夢洲はほぼ平坦）

GML:

```text
udx/bldg/51357289_bldg_6697_op.gml
udx/bldg/51357370_bldg_6697_op.gml
udx/bldg/51357371_bldg_6697_op.gml
udx/bldg/51357380_bldg_6697_op.gml
udx/bldg/51357381_bldg_6697_op.gml
udx/dem/513572_dem_6697.gml
udx/dem/513573_dem_6697.gml
udx/frn/51357370_frn_6697_op.gml
udx/frn/51357380_frn_6697_op.gml
```

appearance フォルダ（LOD3の手持ちカメラ／専用写真の一次出所。3D Tilesの共有アトラスより先にここを見る）:

```text
udx/bldg/51357289_bldg_6697_appearance/   17
udx/bldg/51357370_bldg_6697_appearance/  229   ← チェコ・null2（Ochiai）・マルタ・国連 など
udx/bldg/51357371_bldg_6697_appearance/   71
udx/bldg/51357380_bldg_6697_appearance/  241
udx/bldg/51357381_bldg_6697_appearance/   17
udx/frn/51357370_frn_6697_appearance/     224   ← 大屋根リング本体・周辺設備の画像
udx/frn/51357380_frn_6697_appearance/    67    ← 大屋根リング周辺設備の画像
```

ファイル名の読み方:

- `NNN_国名または施設名.jpg`: パビリオン専用の parameterized texture（例 `021_Czech.jpg`）
- `NNN_SN_人名_*.jpg`: シグネチャーパビリオン。`SN` は Signature。`Ochiai` は落合陽一の **null²**（`gml:name` は `null2`）
- `expoNNNN.jpg` / `expoNNNN_lod3.jpg`: 空中写真寄りの LOD2/LOD3 外観
- 1棟が `_a` `_b` `_c` に分かれることがある（null2 は a=本体、b=小さい方）

`51357370` の番号付き専用JPG（近景の探索用。同じZIPの他メッシュも同じ規則）:

```text
004_Commons-C.jpg
005_SN_Kawamori_a.jpg / _b.jpg / _c.jpg
006_Commons-D.jpg
008_UnitedStates.jpg
009_EuropeanUnion.jpg
010_Belgium.jpg
011_France_a.jpg / _b.jpg
012_SN_Kawase_a.jpg / _b.jpg
013_Vietnam_etc.jpg
014_SN_Miyata.jpg
015_Syougyoushisetsu_1.jpg
016_SatelliteStudio_2.jpg
017_Turkmenistan.jpg
018_Bahrain.jpg
019_NordicCircle.jpg
020_Malta.jpg
021_Czech.jpg
022_Kokusaikikan.jpg
023_SN_Nakajima.jpg
024_SN_Ochiai_a.jpg / _b.jpg
025_SN_Fukuoka_a.jpg / _b.jpg
026_Angola.jpg
027_UnitedKingdom.jpg
028_Romania.jpg
029_Poland.jpg
030_SatelliteStudio_1.jpg
031_Syougyoushisetsu_2.jpg
032_Netherlands.jpg
033_Bulgaria.jpg
034_Singapore.jpg
035_Oman.jpg
036_Hungary.jpg
038_Commons-E.jpg
039_SN_Koyama.jpg
041_SN_Ishiguro.jpg
042_China.jpg
043_Kuwait.jpg
044_Brasil_a.jpg / _b.jpg
045_RestArea_2.jpg
046_Austria.jpg
047_Switzerland.jpg
048_Columbia.jpg
049_Portugal.jpg
050_Canada.jpg
052_Qatar.jpg
053_FestivalStage.jpg
054_Philippines.jpg
055_Jordan_Peru_Mozanbique.jpg
077_Italia_Vatican.jpg
```

`51357380` 側の番号付き専用JPG（同じ規則。ZIP内を国名や `SN_` で検索する）:

```text
002_RestArea_1.jpg
003_Commons-B.jpg
007_Commons-A.jpg
056_Malaysia.jpg
057_Ireland.jpg
058_Nepal_a.jpg / _b.jpg / _c.jpg
059_Luxembourg_a.jpg 〜 _d.jpg（LOD2派生もあり）
060_Germany_a.jpg / _b.jpg
062_Korea.jpg
063_Commons-F.jpg
064_Azerbaijan.jpg
065_EarthatNight.jpg
066_Monaco_a.jpg / _b.jpg / _c.jpg
067_Turkey.jpg
068_Thailand.jpg
069_Spain.jpg
070_SaudiArabia_a.jpg 〜 _d.jpg
071_Australia.jpg
072_Indonesia.jpg
073_India.jpg
074_Uzbekistan.jpg
075_Serbia.jpg
078_RestArea_3.jpg
079_Egypt_Senegal_Bangladesh.jpg
```

3D Tiles の埋め込みWebPは、これらのJPGをタイル単位の共有アトラスへ詰めた派生である。専用マテリアル名（`021_Czech`）が残る棟と、無名アトラスへ押し込まれる棟がある。近景のアルベドは appearance JPG を一次出所にする。

GeoTIFF ZIP（v4）の内訳:

- タイル: `51357279` / `289` / `370` / `371` / `380` / `381` の `.tif` + `.tfw`
- 公式範囲: 北34.658333 / 西135.3625 / 東135.4 / 南34.641667、EPSG:6668、解像度40cm

出所確認用に第三者が XYZ 配信している例（`eda-hotori.github.io` など）があるが、
実行時フェッチはしない。床テクスチャの一次出所は公式GeoTIFFのみである。

## 6. 変換パイプライン

### 6.1 走査・抽出

```text
data_original/3dtiles/
  -> tileset.jsonを再帰走査
  -> content.uri / content.urlを相対解決
  -> 参照切れを報告
  -> 単一タイル時は最初のb3dmまたはglbを選択
  -> 複数タイル時はgeometricErrorが0の葉タイルを選択
  -> b3dmから埋め込みGLBを抽出
```

実装:

- `tool/prepare_expo_model.py`
- `data_converted/meshes/expo_tile.metadata.json`
- `data_converted/meshes/expo_tiles_lod2.metadata.json`

メタデータには、選択元、参照先、`boundingVolume`、`geometricError`、
Feature Table、Batch Table、`CESIUM_RTC`中心座標、変換前後のGLB情報を記録する。
複数タイル時は参照RTC中心と各タイルの実行時パスも記録する。

### 6.2 Dracoの非圧縮化とWebP

現在の`assimp-vc143-mt.dll`はDracoデコーダーを含まないため、
公式GLBをそのまま読み込むと次のエラーになる。

```text
GLTF: Draco mesh compression not supported.
```

そのため、抽出後に次の補助ツールで変換する。

- `tool/decode_draco_glb.cjs`
- `tool/package.json`
- `@gltf-transform/core`
- `@gltf-transform/extensions`
- `draco3dgltf`

依存関係の準備:

```powershell
npm install --prefix tool
```

変換時には、元データの`KHR_draco_mesh_compression`を頂点属性へ展開する。
また、実行時デコーダーが解釈しない`CESIUM_RTC`を出力GLBから除去する。
元の中心座標はメタデータと実行時マニフェストへ残し、`SCENE_GAME`が相対配置に使う。

LOD2側は埋め込みWebPを残したまま、実行時は DirectXTex の WIC 経路で読む。

### 6.3 出力先

```text
data_converted/meshes/expo_tile.glb
data_converted/meshes/expo_tile.metadata.json
asset/expomodel/expo_tile.glb
data_converted/meshes/expo_tile_lod2.glb
data_converted/meshes/expo_tile_lod2.metadata.json
asset/expomodel/expo_tile_lod2.glb
data_converted/meshes/expo_tile_lod2_data0.glb
data_converted/meshes/expo_tile_lod2_data1.glb
data_converted/meshes/expo_tile_lod2_data2.glb
data_converted/meshes/expo_tile_lod2_data3.glb
data_converted/meshes/expo_tile_lod2_far_data0.glb
data_converted/meshes/expo_tile_lod2_far_data1.glb
data_converted/meshes/expo_tile_lod2_far_data2.glb
data_converted/meshes/expo_tile_lod2_far_data3.glb
data_converted/meshes/expo_tiles_lod2.txt
data_converted/meshes/expo_tiles_lod2.metadata.json
asset/expomodel/expo_tile_lod2_data0.glb
asset/expomodel/expo_tile_lod2_data1.glb
asset/expomodel/expo_tile_lod2_data2.glb
asset/expomodel/expo_tile_lod2_data3.glb
asset/expomodel/expo_tiles_lod2.txt
data_converted/meshes/expo_floor.glb
data_converted/meshes/expo_floor.metadata.json
data_converted/meshes/expo_ring.glb
data_converted/meshes/expo_ring.metadata.json
data_converted/meshes/expo_field.txt
asset/expomodel/expo_floor.glb
asset/expomodel/expo_ring.glb
asset/expomodel/expo_pavilion_null2.glb
asset/expomodel/expo_pavilion_czech.glb
asset/expomodel/expo_field.txt
```

`asset/expomodel` のGLBは変換済みの派生アセットであり、
公式ZIPや元の`b3dm`ではない。

### 6.4 再現コマンド

Draco解除用依存関係:

```powershell
npm install --prefix tool
```

LOD1タイルの再生成:

```powershell
python tool/prepare_expo_model.py --tileset "data_original/3dtiles/27999_osaka-shi_city_2025_citygml_1_op_bldg_lod1/tileset.json"
```

LOD2隣接葉タイルの再生成:

```powershell
python tool/prepare_expo_model.py --tileset "data_original/3dtiles/27999_osaka-shi_city_2025_citygml_1_op_bldg_lod2/tileset.json" --output-name expo_tile_lod2.glb --runtime-output "asset/expomodel/expo_tile_lod2.glb" --max-tiles 4 --leaf-only
```

複数枚表示では親タイル`data4.b3dm`を含めず、`geometricError`が0の葉タイルだけを並べる。
`tileset.json`の`refine: REPLACE`では親と子を同時に出すと重なるためである。

### 6.5 床と大屋根リング

```powershell
python tool/prepare_expo_floor.py
python tool/prepare_expo_ring.py
```

- 床: GeoTIFF 6枚を地理参照でつなぎ、公式範囲へ切り出してJPEG化した水平クアッドGLB。頂点は ECEF 相対を glTF Y-up に入れ、法線は大屋根リングと同じく ECEF 三角形から変換する。定数 `(0,1,0)` は実行時 ENU 後に横を向き太陽光が乗らないため使わない
- リング: 同じ CityGML ZIP の `frn`。`gml:name`「大屋根リング」156件の `lod3Geometry` をECEF相対GLB化し、`appearance` の `ParameterizedTexture` / `TexCoordList` を `gml:id` で結合する。`LinearRing` に `gml:id` がある屋根面も対象にし、外形のIDが省略された面は `Polygon` IDから補完する
- リングの画像は `4000_all.jpg` など、対象面が実際に参照する公式JPEGだけをZIPから読み、GLBへ埋め込む。画像付き面は公式UV、画像のない柵・階段などは `X3DMaterial` の `diffuseColor` を使う
- CityGMLのUVは画像左下原点。glTFと実行時DirectXは左上原点なので、変換時に `v' = 1 - v` する。床オルソは最初から左上原点で書いている
- 現行生成結果は326,938面（テクスチャ272,573面、単色54,365面）、公式画像22枚、マテリアル26個で、UVが欠落する面はない
- 参照RTCはLOD2葉4枚の平均のまま（`asset/expomodel/expo_tiles_lod2.txt`）
- 実行時マニフェスト: `asset/expomodel/expo_field.txt`
- Unity / PLATEAU SDK、CityGMLのゲーム内直接読み込み、AssimpでのCityGML読込は使わない
- リングは形状とテクスチャを変換時に結合する。テクスチャ画像は外部ファイル参照にせず、GLB内の `bufferView` へ格納する

出力先の追加:

```text
data_converted/meshes/expo_floor.glb
data_converted/meshes/expo_floor.metadata.json
data_converted/meshes/expo_ring.glb
data_converted/meshes/expo_ring.metadata.json
data_converted/meshes/expo_field.txt
asset/expomodel/expo_floor.glb
asset/expomodel/expo_ring.glb
asset/expomodel/expo_field.txt
```

リング用の公式画像は `asset/expomodel/expo_ring.glb` に埋め込むため、
`asset/texture` へ289枚をコピーしない。

### 6.6 近景パビリオン（LOD3全 `gml:name`）

```powershell
python tool/prepare_expo_pavilion.py --all-names
```

- 出所: 形状はテクスチャ付きLOD3 3D Tiles（`bldg_lod3`）。ゲーム内で CityGML を直接は読まない。CityGML `bldg`は使わない
- LOD3の葉タイル63枚から、空でない全124種の `gml:name` を `_BATCHID` で切り出す。同名が複数タイルにある場合はタイルごとに別GLBにするため、出力は約162件
- 出力名は `expo_pavilion_英単語.glb`。国名・施設名を小文字ASCIIのスラグにし、同名の複数タイルだけ `_1` 以降を付ける。対応表を別途参照しなくてもファイル名で管理できる
- 変換スクリプトに未登録の `gml:name` がある場合は、連番や日本語名へフォールバックせず停止する
- LOD3外観は空中写真に加え手持ちカメラ写真を含む
- UVの無い面は、名前付き建物の `--keep` 対象なら残す（ルクセンブルクの天井やオーストリアの木製モニュメントなど）。名前付き建物に属さないLidar箱だけを捨てる
- 実行時のLOD2タイルからはLOD3対象の同名建物を**面ごと**除く（空中写真の壁を残すとLOD3の壁と重なる）
- パンチした面は未パンチ原本から同じバッチIDだけを`--keep`で抽出し、
  `expo_tile_lod2_far_data0.glb`〜`data3.glb`の4枚へまとめる。建物単位のGLBへ
  分割せず、タイル内でバッチ単位のプリミティブとして保持するため、埋め込みWebPを
  建物数ぶん複製しない
- 遠景4枚はパンチ済みLOD2と同じRTCで配置する。対応するLOD3がGPU化されるまでは
  遠景を表示し、LOD3が常駐したバッチだけを非表示にする。LOD3の破棄後は遠景を再表示する
- 参照RTCはLOD2葉4枚の平均のまま
- LOD2葉4枚の `boundingVolume.region` は `expo_tiles_lod2.txt` の各 `tile` 行へ
  保存し、タイル境界を使った描画時の視錐台カリングへ利用する
- 実行時マニフェスト: `asset/expomodel/expo_field.txt` の `pavilion` 行（実行のたびに置き換え）
- 当たり判定はLOD2ベースで行う。床、大屋根リング、パンチ済みLOD2、遠景LOD2を `Collision_StartAdd` へ渡す。ただし東西ゲート本体だけは高精細LOD3モデルも `Collision_StartAdd` へ渡し、プレイヤーAABBがゲートのワールドAABB内にある間は重複するLOD2判定をスキップする。それ以外のLOD3近景は描画専用とする。`asset/collision/*.bin` があれば優先して使い、無ければGLBからワーカーでベイクする
- 現状: `null2` は写真アルベドのグレーを使わず、起動既定 512² の動的キューブマップと膜の揺れでミラーメンブレンとして描画する（解像度は Debug の ImGui で変更できる）
- セルビア館（`gml:name`「セルビア共和国パビリオン」、`075_Serbia.jpg`）の実行時GLBは 3D Tiles アトラスの UV と埋め込み WebP アルベド、glTF の `metallicFactor` / `roughnessFactor` を持つ。metallic-roughness / normal テクスチャは公式LOD3に含まれない。写真アルベドでは金属度を 0 とし、粗さだけ会場既定と混ぜる。単体再生成は `python tool/prepare_expo_pavilion.py --name セルビア共和国パビリオン` だが、`--all-names` と同様に `expo_field.txt` の pavilion 行と LOD2 パンチを書き換えるため、マップ追加が無い限り再パックしない
- LOD3近景はカメラ周辺、視線方向、移動方向の先読み範囲を距離ストリーミングする。開始はロード半径、GPU化は破棄半径まで進め、視線方向（半頂角60°）は半径を32足して先行する。初期優先ロードは `null2`、`dynamic_equilibrium`、`expo_related_31`、`expo_related_25`、`angola`、`czech` をこの順で床・LOD2・リングと一緒にGPU化し、明転前に最優先で描画する。ヒステリシス帯のREADYが新規インポートを止めない。距離判定には既知のモデルXZ半径を足す。視線先の棟は開始・GPU化の優先度を上げる。万博GLBはワーカーでGLB 2.0のAccessor/BufferViewを直接展開し、埋め込みテクスチャのCPUデコードもワーカーで行う。同時インポートとデコードは各1スレッド。CPU側では同一マテリアルのプリミティブを結合し、実行時の `DrawIndexed` を減らす。失敗ジョブは最大3回、5秒単位の間隔で再試行する。ローカルVRAMが予算の85%を超えたら Present 後の追加GPU化を止める。初期ロード中のGPU化と破棄は1フレーム6ms、完了後は `Present` 後3ms（DrawまたはGPUが8ms超ならスキップ。GPU時間はReleaseでも計測）。頂点・インデックス・テクスチャの転送はチャンク化する。GPU化中は`cube.fbx`のプレースホルダーを表示する
- 大屋根リング外側のランドマーク（日本館、パナソニック、住友、三菱、電力館、迎賓館、ウーマンズパビリオン、JAPANマルシェ）は、同一LOD3葉タイル内のバッチを1本の `expo_pavilion_west_outer.glb` に統合する。配置RTCは元タイルRTCのまま保持し、GLBから求めたECEF重心とXZ半径をマニフェストへ別記して、リング付近からもLOD3が開始されるようにする。NTT Pavilionは個別GLBのままランドマーク半径を適用する。統合後も `MergePreparedMeshesByMaterial` とバッチ範囲描画を使い、棟数分の `DrawIndexed` を発行しない。
- モデル単位視錐台カリングは描画を省略し、距離ストリーミングは範囲外のGLBやテクスチャを解放する
- 範囲外になった未完了ジョブは世代番号で無効化し、遅れて完了したGLBを採用しない

出力先の追加:

```text
data_converted/meshes/expo_pavilion_*.glb
data_converted/meshes/expo_pavilion.metadata.json
asset/expomodel/expo_pavilion_null2.glb
asset/expomodel/expo_pavilion_czech.glb
```

### 6.7 公開リポジトリからの取得と再生成

万博由来の派生GLBはGitHubへ登録せず、`.gitignore`で除外した
`asset/expomodel/`へ生成する。clone後に次を実行すると、公式ZIPを取得する前に
利用者へ規約確認と明示的な`AGREE`入力を求め、同意後だけダウンロードと変換を行う。

```bat
tool\download_expo_assets.bat
```

この処理は公式3D Tiles、CityGML、GeoTIFFを`data_original/`へ保存し、
LOD1/LOD2、床、大屋根リング、LOD3パビリオンをローカル生成する。
ゲーム起動時に `asset/expomodel/` が無い、または空なら確認ウィンドウを出し、
「はい」でゲームを終了して同じバッチを起動する。
変換完了後は `data_*` の実測サイズ（GB）を出し、一時フォルダを消すか確認する。
`data_original/`、`data_converted/`、`asset/expomodel/`、万博用の衝突バイナリは
公開リポジトリへ含めない。配布ZIPにもモデル本体は含めず、変換ツールだけを同梱する。

`prepare_expo_pavilion.py`が書いた`expo_pavilion_better_co_being.glb`へ、
公開環境ではBlenderを要求せず、`meshoptimizer`を使う`tool/simplify_glb.cjs`が
`tool/expo_simplify.json`の島内比率（`0.43`）で上書きする。
このモデルは三角形ごとに頂点が分かれており、位置だけで全頂点を共有すると
同じ座標の別UVが混ざる。UV差が小さい隣接点だけを島としてつなぎ、中実な島だけ削減する。
ほぼ全頂点が穴の縁である網目は面を減らさず、位置とUVが同じ頂点だけ結合する。
穴の縁は`LockBorder`で固定する。
全体の面数は網目を残すため変換直後に近く、旧来の約40%削減とは一致しない。
BlenderのDecimateとアルゴリズムは同一ではない。
ゲームのマニフェストは`expo_pavilion_better_co_being.glb`を参照する。

## 7. 実行時アセットと配置

シーンの操作・プレイヤー・衝突は [game_specification.md](game_specification.md)。
ここでは派生GLBの種類と、座標系・スケールを書く。

### 7.1 実行時ファイル

```text
asset/expomodel/expo_tiles_lod2.txt
asset/expomodel/expo_field.txt
asset/expomodel/expo_tile_lod2_data0.glb
asset/expomodel/expo_tile_lod2_data1.glb
asset/expomodel/expo_tile_lod2_data2.glb
asset/expomodel/expo_tile_lod2_data3.glb
asset/expomodel/expo_tile_lod2_far_data0.glb
asset/expomodel/expo_tile_lod2_far_data1.glb
asset/expomodel/expo_tile_lod2_far_data2.glb
asset/expomodel/expo_tile_lod2_far_data3.glb
asset/expomodel/expo_floor.glb
asset/expomodel/expo_ring.glb
asset/expomodel/expo_pavilion_*.glb
asset/expomodel/expo_tile_lod2.glb
asset/expomodel/expo_tile.glb
```

衝突バイナリは `asset/collision/expo_floor.bin`、`expo_ring.bin`、LOD2タイルの `expo_tile_lod2_dataN.bin` / `expo_tile_lod2_far_dataN.bin`、東西ゲート高精細モデルの `expo_pavilion_east_gate.bin` / `expo_pavilion_west_gate.bin`。作り方は [collision.md](../document_framework/collision.md)。

`SCENE_GAME` は次の順で重ねる。

1. `asset/expomodel/expo_tiles_lod2.txt` があれば LOD2 葉タイル
2. `asset/expomodel/expo_field.txt` があれば床、近景パビリオン、大屋根リングを同じ参照RTCで追加
3. マニフェストが無い、または1枚も読めない場合は `asset/expomodel/expo_tile_lod2.glb`、さらに `asset/expomodel/expo_tile.glb`

描画順は床 → LOD2 → パビリオン → リング。床は建物よりわずかに沈める。
カメラ Far は 2000（会場オルソが既定 Far 500 では欠ける）。

リングと床の描画GLBには `NORMAL` を焼いてある。リングは公式appearanceのUV画像と
`X3DMaterial`の単色を複数マテリアルへ分けて埋め込む。LOD2タイルとパビリオンは元データに法線がある。
万博GLBの実行時インポートはAssimpを経由せず、Accessorの頂点を結合・削減せずに展開する。
出力インデックスはCPU上で検証し、GPU側では常に32bitへ統一する。

### 7.2 スケール

既存の`GlbModel`はGLB読み込み時にGlobalScaleを100倍にする。
そのため`SCENE_GAME`では表示側スケールに`0.002`を指定している。
メートルからワールド単位への係数は `0.002 * 100 = 0.2` である。
RTC差分メートルにも同じ係数を適用する。
これは公式データを変更する処理ではなく、フレームワーク側のスケール仕様を
表示側で補正する設定である。

### 7.3 座標変換

出力GLBから`CESIUM_RTC`は除去済みで、中心座標はマニフェストに残している。
`SCENE_GAME`は参照RTC（選択タイル中心の平均）を原点とし、次の順で置く。

1. 位置: ECEF差分メートル × `0.2` を、参照点のENU（X=東、Y=上、Z=北）へ変換する。
2. メッシュ: Cesiumと同じ`Y_UP_TO_Z_UP`（X軸+90度）を掛け、続けて同じENU回転を掛ける。
3. 実行時左手変換によるZ反転を、メッシュ回転の手前で打ち消す。

直接デコードする万博GLBの頂点は、衝突 bin の実行時と同じくメートル座標を 100 倍し Z を反転する。
UVはglTFの値をそのまま使用する。glTFとDirectXのテクスチャ座標は上端原点のため、V反転は行わない。

RTC中心差だけをECEFのまま足すと全タイルが原点に重なるか、斜めの帯になる。
ENUなしだと地面が傾き、`Y_UP_TO_Z_UP`なしだと建物が横倒しになる。

実装の入口は `SCENE_GAME/field.cpp`。シーン進行は `SCENE_GAME/game.cpp`（呼び出し列のみ）。HUD は `SCENE_GAME/ui.cpp`、三人称は `SCENE_GAME/playercamera.cpp`。

## 8. 未実装の技術要素

次の機能は今後のタスクである。チェックリストは [task_list.md](task_list.md)。

- `tileset.json`のLOD選択と、LOD3ストリーミングへの`boundingVolume`連携
- CityGML `dem` による起伏
- `null2` のミラーメンブレン（起動既定 512² 動的キューブマップと膜の揺れ。実装済み）

複数タイルの相対配置とENU水平化は実装済みである。読み込み済みの各 `Sprite3D` には
モデルサイズから求めた境界球による保守的な視錐台カリングを適用する。
ただし、`tileset.json` の `geometricError` に基づくLOD選択や、
`boundingVolume` のLOD3ストリーミング連携は未実装である。
LOD2本体と未パンチLOD2遠景では、`boundingVolume.region` を実行時マニフェストへ保存し、
描画時のタイル単位カリングに利用している。これは描画専用であり、LOD選択やLOD3の
距離ストリーミング判定には使わない。
現在は距離、視線方向、移動方向の先読みでLOD3のロード対象を絞り、不要になったGPUバッファと
テクスチャを破棄する。開始はロード半径 48、GPU化は破棄半径 72、視線方向は半頂角60°で半径を32足す。
距離には既知のモデルXZ半径を足す。
CPUデコードはワーカーへ移し、同一マテリアルのメッシュ結合もワーカーで行う。
初期ロード中のGPUアップロードと破棄はゲーム更新中の6ms予算、完了後は Present 後へ分割する。
ロード中は`cube.fbx`のプレースホルダーを表示する。
未パンチLOD2遠景は4枚のGLBを常駐させ、LOD3のGPU化状態に応じてバッチ単位で排他表示する。
実行時はマテリアル結合とバッチ範囲描画で発行をまとめ、影パスには載せない。

## 9. 参考リンク

- 国土交通省 万博3D都市モデル発表:
  <https://www.mlit.go.jp/report/press/toshi03_hh_000205.html>
- 万博公式データ案内:
  <https://www.expo2025.or.jp/news/news-20260306-01/>
- PLATEAU配信サービス:
  <https://docs.plateauview.mlit.go.jp/>
- PLATEAU 3D Tiles / MVT仕様:
  <https://docs.plateauview.mlit.go.jp/datasets/3d-tiles/>
- PLATEAU SDK for Unity:
  <https://github.com/Project-PLATEAU/PLATEAU-SDK-for-Unity>
