# DX11前提・万博PLATEAU床面の高低差実装リサーチ

> 採用経路: Unity / PLATEAU SDK は使わない。実装は自作DX11フレームワークの
> [`tool/prepare_expo_floor.py`](../tool/prepare_expo_floor.py) で、CityGML `udx/dem/`
> の TINRelief から相対起伏だけをオルソ床GLBへ焼く（4mグリッド、中央値基準、
> ±3.0m切詰、被覆端40mなじませ）。詳細は [plateau.md](plateau.md) 6.5節。
> 以下は調査時の Unity 前提メモとして残す。

## 結論

最優先で高低差を追加するなら、**PLATEAUの「土地起伏」をUnity Terrainへ変換し、パビリオン入口・縁石・階段・スロープだけ別メッシュで補正するハイブリッド方式**が最短かつ安全です。高低差の生成・Terrain描画・通常メッシュ・コライダーはDX11で実行できるため、この工程にDX12固有機能は不要です。

一方、万博会場は自然地形より人工的な平面・段差が多いため、Terrainだけですべて再現すると縁石や建物際が丸くなります。**Terrainは広域の標高と緩勾配、通常メッシュは硬い段差**という分担にします。

## 利用するデータ

万博PLATEAUデータには、建築モデルLOD0～3.0、都市設備LOD3.0、地形モデルLOD1が含まれます。 PLATEAU SDKでは、インポート時に「土地起伏」を含めることで、地形モデルの平滑化、Terrain変換、道路などの高さ合わせを実行できます。[^1][^2][^3][^4]

また、会場内をLidar SLAMで計測した3次元点群も公開されています。 点群は全面をランタイム表示するのではなく、パビリオン入口、階段、スロープ、デッキ、道路境界の標高を確認する編集用資料として使います。[^5][^6]

## DX11での実装方針

| 対象 | 実装 | 理由 |
|---|---|---|
| 会場全体の標高 | Unity Terrain | 広域の緩い起伏を軽量に表現できる |
| パビリオン入口 | 局所補正メッシュ | 建物と地面の隙間・めり込みを確実に隠せる |
| 縁石・側溝 | 通常メッシュ／Spline Mesh | 高さが急変する形状を保てる |
| 階段 | 通常メッシュ | ハイトマップでは段鼻が丸くなる |
| スロープ | 通常メッシュまたは細分化した床メッシュ | 勾配と接続高さを制御しやすい |
| 大屋根リング下 | 床メッシュ | Terrainより平面性と境界を維持しやすい |
| 遠景・進入不能範囲 | Terrainのみ | 工数を抑えられる |

Unity Terrainは各点の高さを矩形ハイトマップで保持します。 そのため1地点に複数の高さを持つ立体交差、垂直面、オーバーハングはTerrain単体では表現できず、Unity公式も穴を開けた箇所の複雑な形状にはProBuilder等のジオメトリを使う方法を案内しています。[^7][^8][^9][^10]

## 最短手順

### 1. 現在のシーンを複製

作業前にシーンとTerrainDataを複製します。TerrainDataはアセットとして参照されるため、シーンだけ複製しても同じTerrainDataを編集してしまう可能性があります。

```text
Assets/Scenes/Expo_Flat.unity
Assets/Scenes/Expo_HeightTest.unity
Assets/Terrain/ExpoTerrain_Test.asset
```

### 2. 土地起伏をインポート

Unityで次を開きます。

```text
PLATEAU
  → PLATEAU SDK
  → インポート
```

対象範囲に土地起伏が存在すれば、地物設定に「土地起伏」が表示されます。 最初は会場全域ではなく、代表パビリオンと周囲100～300m程度をインポートして確認します。[^2]

推奨初期設定は次の通りです。

```text
建築物        : 既存LOD3を維持
土地起伏      : ON
航空写真      : 必要ならON（高さ処理とは独立）
座標系        : 既存シーンと同一
基準座標      : 既存PLATEAUモデルと同一
```

### 3. Terrainへ変換

次を開きます。

```text
PLATEAU
  → PLATEAU SDK
  → モデル調整
  → 地形変換 / 高さ合わせ
```

PLATEAU SDKは地形メッシュからHeight Mapを生成し、平滑化してUnity Terrainとして出力できます。 内部的には16bitグレースケールのハイトマップを生成し、3×3の平均化フィルタで平滑化する方式が説明されています。[^11][^12][^1]

最初の変換では、過剰な平滑化を避けます。人工地盤の微妙な勾配まで消える場合は、平滑化を弱くするか、変換前の地形メッシュと比較してください。

### 4. Terrain設定を確認

ハイトマップ解像度は2の累乗＋1で設定します。Unity公式の例では513が挙げられており、解像度を高くするほど細かな地形輪郭を保持できます。[^13][^14]

初期値の目安は次です。

| Terrain一辺 | Heightmap | 水平1サンプルの目安 | 用途 |
|---:|---:|---:|---|
| 512m | 513 | 約1m | 軽量な全体検証 |
| 512m | 1025 | 約0.5m | 推奨 |
| 1024m | 1025 | 約1m | 広域用 |
| 1024m | 2049 | 約0.5m | 高品質だが編集負荷増 |

水平間隔の概算は `Terrain幅 / (HeightmapResolution - 1)` です。会場全域を1枚の巨大Terrainにすると入口周辺の精度が落ちるため、512～1024m程度のタイルへ分割する方が扱いやすくなります。

```text
Terrain Width / Length : 実寸
Terrain Height         : 実データの標高差＋余白
Heightmap Resolution   : まず1025
Pixel Error            : 低めから検証
Draw Instanced         : ON
Cast Shadows           : 必要区画のみ
```

Unity Terrainにはインスタンス描画設定が用意されています。 DX11環境でも、高さ表現そのものは標準Terrainで完結します。[^14][^15]

## 高さ合わせの順序

### 基準は建物入口

全体の地形を生成したら、次の順で位置を確認します。

1. 代表パビリオンの入口
2. 大屋根リング下の床
3. 主要道路と歩道
4. 階段・デッキ
5. 植栽帯と遠景

建物を地形へ無条件に沈めるのではなく、**LOD3建物の入口床面を固定し、地形側を合わせる**のが基本です。パビリオン全体のY座標を個別に変更すると、隣接設備や大屋根リングとの位置関係が崩れるためです。

### 自動高さ合わせ

PLATEAU SDKの高さ合わせは、高さを持たないLOD1道路を地形へ追従させるほか、高さを持つLOD3道路が地形へめり込む場合に地形形状を修正できます。 対象に道路モデルが含まれる場合は、手修正前にこの機能を試します。[^1]

ただし、入口、階段、縁石などの接続品質までは自動処理に任せず、目視で確認します。

## 局所補正

### Terrainで直す部分

以下はTerrain Sculptで修正できます。

- 緩い坂
- 道路全体の上下
- 広場の数十m単位の傾斜
- 植栽地の盛り上がり
- 海側へ向かう大域的な勾配

使用ツールは主に次です。

- `Set Height`：広場やリング下を一定高さへ合わせる
- `Smooth Height`：土地起伏由来の不要なノイズを除去する
- `Raise or Lower Terrain`：緩い局所勾配を追加する

Unity TerrainにはRaise/Lower、Set Height、Smooth Height、Stamp Terrainが用意されています。[^16]

### メッシュで直す部分

以下はTerrainではなく通常メッシュにします。

- 縁石
- 排水溝
- 階段
- 垂直に近い擁壁
- 数十cm幅の段差
- 建物入口直前のスロープ
- デッキと地面の接合部

Terrainの一部を隠したい場合はPaint Holesを使い、置き換えメッシュを配置できます。URPではビルド時にも穴を有効にするため、URP AssetのTerrain Holes設定が必要です。[^17][^7]

## 点群を使う場合

点群は必要区画だけCloudCompare等で切り出します。目的は高密度メッシュ化ではなく、標高の基準点取得です。

```text
1. パビリオン前を矩形選択
2. 地面以外の点を削除
3. 入口中心、左右端、道路中心、歩道端の高さを取得
4. Unity内の対応地点へEmptyを配置
5. EmptyのY値を基準にTerrainまたは床メッシュを修正
```

点群は移動体除去などのノイズ処理済みですが、会場内の侵入可能範囲を150コース以上計測した大容量データであり、ファイルは平均約1.6GB、最大約6.8GBと案内されています。したがって全点をUnityへ常駐させる構成は避け、編集時のみ使用します。[^5]

点群座標系は平面直角座標系第6系です。 Unityへ直接持ち込む場合は原点からの巨大座標による精度低下を避け、PLATEAUモデルと同じローカル原点へ変換してください。PLATEAU Maps Toolkitも、大きな座標値による浮動小数点誤差を避けるため、座標中心を対象モデル付近へ置くよう案内しています。[^18][^5]

## DX11の制約と影響

今回の高低差追加では、DX11は実質的な障害になりません。

| 機能 | DX11での方針 |
|---|---|
| Terrain高さ | 標準Terrainを使用 |
| ハイトマップ | 16bit RAWまたはSDK生成データ |
| 縁石・階段 | 通常メッシュ |
| Terrain Collider | 標準機能 |
| 床メッシュCollider | 簡略版Mesh Collider |
| レイトレーシング | 不使用 |
| テッセレーション | 前提にしない |
| Nanite相当 | 前提にしない |
| 高さのランタイム更新 | 原則行わず、Editorでベイク |

`TerrainData.SetHeights`は呼び出すたびにTerrainのLODと植生情報を再計算するため、ランタイムやブラシ操作中の頻繁な更新は高コストです。インタラクティブ編集では`SetHeightsDelayLOD`を使い、編集完了後に`SyncHeightmap`する方法が公式に案内されています。[^19][^20]

今回の用途では、地形はEditor上で確定し、実行時は静的に扱うのが適切です。これによりDX11でも地形更新コストをほぼ回避できます。

## 推奨シーン構造

```text
ExpoWorld
├─ PLATEAU_Buildings_LOD3
├─ Ground
│  ├─ Terrain_Base
│  ├─ TerrainCollider
│  ├─ HardSurface
│  │  ├─ PavilionEntrance
│  │  ├─ RingFloor
│  │  ├─ Roads
│  │  ├─ Sidewalks
│  │  └─ Decks
│  ├─ HeightDetails
│  │  ├─ Curbs
│  │  ├─ Drains
│  │  ├─ Stairs
│  │  └─ Ramps
│  └─ Reference
│     ├─ OrthoPhoto
│     └─ PointCloud_EditorOnly
└─ Props
```

`Reference`はビルドから除外します。点群、測量用マーカー、高さ確認用の補助オブジェクトはEditorOnlyタグまたは専用Assembly／シーンへ分離します。

## 作業優先順位

### フェーズ1：地形復旧

目標は「完全な平面をやめる」ことです。

- 土地起伏をインポート
- Terrainへ変換
- LOD3建物との全体的なY位置を確認
- 主要道路と広場の緩勾配を復元
- 不要なノイズだけ平滑化

この段階ではテクスチャを作り直しません。航空写真をそのまま維持して、高低差だけの効果を確認します。

### フェーズ2：接地破綻の修正

- 入口の隙間・めり込みを修正
- リング下を一定高さへ調整
- 階段とスロープを追加
- 縁石を主要導線だけ追加
- ColliderとNavMeshを再生成

### フェーズ3：床品質

- PBR舗装
- 白線・目地デカール
- マンホール・排水口
- 舗装境界
- 小物

高低差を先に確定させることで、後からデカールや道路メッシュが浮く問題を避けられます。

## 最初に試す区画

最初の検証は、代表パビリオン入口から主要通路までの100m四方が適しています。

```text
Terrain範囲       : 約512m四方
Heightmap         : 1025
地形用途           : 緩勾配のみ
入口・階段         : 別メッシュ
点群               : 高さ確認時のみ
航空写真           : 現状維持
Graphics API      : Direct3D11
ランタイム地形変形 : なし
```

比較用に次の4枚を同じカメラ位置で撮影します。

1. 完全な平面
2. PLATEAU Terrain変換直後
3. Terrain平滑化・高さ修正後
4. 入口・縁石メッシュ追加後

LOD3との相性改善は、遠景では緩い地形、近景では入口・縁石・階段の接地によって判断できます。改善が小さい場所へ細密な点群処理を追加するより、プレイヤーが歩く範囲へ局所メッシュを集中させる方が効率的です。

## 実施チェックリスト

- [ ] 既存シーンとTerrainDataを複製した
- [ ] PLATEAUインポートで土地起伏を含めた
- [ ] 座標系・基準座標を既存LOD3と統一した
- [ ] 地形変換／高さ合わせを実行した
- [ ] Terrainの実寸サイズを確認した
- [ ] Heightmap Resolutionを1025から試した
- [ ] パビリオン入口を基準高さとして確認した
- [ ] 不要な地形ノイズだけ平滑化した
- [ ] 硬い段差をTerrainではなくメッシュ化した
- [ ] 点群をビルド対象から除外した
- [ ] Colliderを表示メッシュと分離した
- [ ] NavMeshを地形確定後に再ベイクした
- [ ] Direct3D11実機ビルドでLOD・影・接地を確認した

## 注意点

万博3D都市モデルと点群は無償公開されていますが、商業目的、販売促進、広告利用、商品化などの営利目的での利用は禁止されています。 就職活動用ポートフォリオへの掲載方法は利用規約上の扱いを事前に確認し、判断できない場合は配布元へ照会してください。[^3][^5]

---

## References

1. [3D都市モデルの編集 | PLATEAU SDK for Unity](https://project-plateau.github.io/PLATEAU-SDK-for-Unity/manual/ModelAdjust.html)

2. [3D都市モデルのインポート | PLATEAU SDK for Unity](https://project-plateau.github.io/PLATEAU-SDK-for-Unity/manual/ImportCityModels.html)

3. [2025年大阪・関西万博会場 3D都市モデル（Project PLATEAU） - G空間情報センター](https://www.geospatial.jp/ckan/dataset/plateau-27999-osaka-shi-2025) - どなたでも無償で利用できますが、商業目的、販売促進、広告利用、商品化などの営利目的での使用は一切認められていません。 必ず<a href="https://gic-plateau.s3.ap-nort...

4. [大阪・関西万博のレガシーをProject PLATEAU において整備 ...](https://www.mlit.go.jp/report/press/toshi03_hh_000205.html) - 「大阪・関西万博2025」 国土交通省では、都市空間をデジタル空間上に再現した三次元データ「3D 都市モデル」の整備・活用・オープンデータ化の取組「 ...

5. [デジタル空間上に再現した大阪・関西万博会場の三次元データ ...](https://www.expo2025.or.jp/news/news-20260306-01/) - また、3D都市モデルの整備に当たって取得された「点群データ」も同時公開されます。 本データは、どなたでも無償で閲覧・活用できますが、商業目的、販売 ...

6. [【公開】国土交通省が大阪・関西万博の3D都市モデル及び3 ...](https://front.geospatial.jp/news/2026/03/6893/) - 国土交通省から、「大阪・関西万博2025」の会場内の建築物をPrject PLATEAU 3D都市モデル及び3次元点群データをデジタルアーカイブとして整備、G空間情報 ...

7. [Paint holes in the terrain - Unity - Manual](https://docs.unity3d.com/6000.6/Documentation/Manual/terrain-PaintHoles.html)

8. [Manual: Paint holes in the terrain](https://docs.unity3d.com/Manual/terrain-PaintHoles.html)

9. [Working with Heightmaps - Unity - Manual](https://docs.unity3d.com/6000.3/Documentation/Manual/terrain-Heightmaps.html)

10. [Working with Heightmaps - Unity - Manual](https://docs.unity3d.com/6000.6/Documentation/Manual/terrain-Heightmaps.html)

11. [PLATEAU SDK for Unityを活用する | How To Use](https://www.mlit.go.jp/plateau/learning/tpc17-1/) - PLATEAU SDK for Unityとは、PLATEAUの豊富なデータを活用して実世界を舞台にしたアプリの開発や都市シミュレーション等を行うためのツールキットです。このトピックでは、PLATEA...

12. [plateau_doc_0012_ver02.pdf](https://www.mlit.go.jp/plateau/file/libraries/doc/plateau_doc_0012_ver02.pdf)

13. [Manual: Terrain settings](https://docs.unity.cn/Manual/terrain-OtherSettings.html)

14. [Terrain Settings reference](https://docs.unity3d.com/6000.2/Documentation/Manual/terrain-OtherSettings.html) - Draw Instanced, Enable instanced rendering. For more information, refer to Optimizing draw calls. En...

15. [Terrain.drawInstanced](https://docs.unity3d.com/2020.1/Documentation/ScriptReference/Terrain-drawInstanced.html)

16. [Terrain tools - Unity - Manual](https://docs.unity3d.com/6000.0/Documentation/Manual/terrain-Tools.html)

17. [Paint holes in the terrain - Unity - Manual](https://docs.unity3d.com/6000.2/Documentation/Manual/terrain-PaintHoles.html)

18. [PLATEAU-SDK-Maps-Toolkit-for-Unity/README.md at main · Project-PLATEAU/PLATEAU-SDK-Maps-Toolkit-for-Unity](https://github.com/Project-PLATEAU/PLATEAU-SDK-Maps-Toolkit-for-Unity/blob/main/README.md) - Contribute to Project-PLATEAU/PLATEAU-SDK-Maps-Toolkit-for-Unity development by creating an account ...

19. [Scripting API: TerrainData.SetHeights - Unity - Manual](https://docs.unity3d.com/6000.5/Documentation/ScriptReference/TerrainData.SetHeights.html)

20. [TerrainData.SetHeights](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/TerrainData.SetHeights.html)

