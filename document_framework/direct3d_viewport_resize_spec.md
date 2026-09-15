# Direct3D のビューポート/リサイズ仕様

対象: `shader/renderer.cpp` / `shader/renderer.h`  
（旧 `framework/direct3d.cpp` は統合・削除済み。公開 API 名に `Direct3D_` プレフィックスが残る）

## 1. 結論

**ウィンドウサイズ変更時は、クライアントサイズに合わせてバックバッファを再構築し、そのサイズを基準に 2D/3D のビューポートを再設定**している。
3D はバックバッファ全体をビューポートにし、カメラの Projection 行列でターゲットアスペクトをカバーする。

- `Direct3D_SetViewport2D()` / `Direct3D_SetViewport3D()` は **static 内部関数**。描画先ビューポートのみ設定し、バッファの生成・破棄はしない
- これらは `SetDepthEnable(true/false)` から呼ばれる（公開 API ではない）
- バックバッファ / 深度バッファの再構築は `configureBackBuffer()` / `releaseBackBuffer()` 経由で、`Direct3D_Resize(...)` が担当
- `Direct3D_ResizeWindow()` は **クライアント領域サイズの記録のみ**（D3D リソースは変更しない）
- スワップチェーンは DXGI の Flip モデル（`DXGI_SWAP_EFFECT_FLIP_DISCARD`、2 バッファ）を使う
- Flip モデルではバックバッファごとに RTV を作り、`Present()` 後に表示対象のバックバッファへ RTV を切り替える

## 2. 関数別仕様

### 2.1 `Direct3D_SetViewport2D()`（内部）

**役割**
- 2D 向けに `DRAW_SCREEN_X x DRAW_SCREEN_Y` の比率を保つビューポートを設定
- ウィンドウアスペクトに応じて中央寄せのレターボックス/ピラーボックスを作る

**挙動**
- `targetAspect = DRAW_SCREEN_X / DRAW_SCREEN_Y`
- `windowAspect = g_ClientWidth / g_ClientHeight`
- `windowAspect > targetAspect`（横長）:
  - 縦基準 → `vpH = g_ClientHeight`, `vpW = g_ClientHeight * targetAspect`（左右余白）
- それ以外（縦長または同等）:
  - 横基準 → `vpW = g_ClientWidth`, `vpH = g_ClientWidth / targetAspect`（上下余白）

**重要点**
- 最終 `D3D11_VIEWPORT` は `g_BackBufferDesc.Width/Height` とクライアントサイズの比率でスケーリングされる
- **バッファ再作成はしない**

### 2.2 `Direct3D_SetViewport3D()`（内部）

**役割**
- 3D の射影行列でアスペクトを調整するため、バックバッファ全体を描画領域にする

**挙動**
- `TopLeftX/Y = 0`
- `Width/Height = g_BackBufferDesc.Width/Height`
- 3D のカバー表示は負のビューポートやバックバッファ外のサイズではなく、カメラの Projection 行列で行う
- 横長ウィンドウでは水平画角を維持して上下をクロップし、縦長ウィンドウでは垂直画角を維持して左右をクロップする

**重要点**
- ビューポート設定のみ
- `SetDepthEnable(true)` 時に呼ばれる。3D のアスペクト調整は Depth ON とセット

### 2.3 `Direct3D_ResizeWindow(unsigned int clientW, unsigned int clientH)`（公開）

**役割**
- クライアント領域サイズを内部変数へ保存

**挙動**
- `g_ClientWidth` / `g_ClientHeight` を更新。0 以下相当なら `1.0f` に補正

**重要点**
- **D3D バッファはリサイズしない**
- `main.cpp` の初期化時と `WM_SIZE` 処理から呼ばれる想定

### 2.4 `Direct3D_GetClientWidth()` / `Direct3D_GetClientHeight()`（公開）

- 戻り値は `float`
- `Direct3D_ResizeWindow()` で更新される
- 初期値はそれぞれ `DRAW_SCREEN_X` / `DRAW_SCREEN_Y`

## 3. バックバッファと深度バッファのリサイズ仕様

### 3.1 初期化時（`InitRenderer`）

`InitRenderer()` は `GetClientRect()` で取得したクライアント領域サイズを初期サイズにして、`CreateSwapChainForHwnd()` でスワップチェーンを生成する。

- `BufferDesc.Width/Height` 相当: 初期クライアント領域サイズ
- `BufferCount = 2`
- `SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD`
- `Format = DXGI_FORMAT_R8G8B8A8_UNORM`

アダプターはソフトウェアを除外し、`EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` を優先する。だめなら専用ビデオメモリが最も大きいハードウェアアダプターを選ぶ。`EnumOutputs()` の有無では選ばない。ハイブリッドノートでは内蔵パネルを持つ iGPU より dGPU を選ぶ。

その後 `configureBackBuffer()` で以下を生成する。

- 2 枚すべてのバックバッファ用 `RenderTargetView`
- 現在のバックバッファインデックス（`IDXGISwapChain3::GetCurrentBackBufferIndex()`）
- `g_BackBufferDesc`
- バックバッファ実サイズと一致する内部シーン幅・高さ
- シーンサイズの深度ステンシルバッファ / ビューとシーン色 RT
- バックバッファ全体を覆う 2D 用ビューポート。3D もシーン＝バックバッファサイズ

したがって、`DRAW_SCREEN_X` × `DRAW_SCREEN_Y`（3840 × 2160）は論理的な基準アスペクトであり、バックバッファの固定サイズではない。3D と UI はいずれもウィンドウ実サイズでドットバイドットに描く。

### 3.2 `Direct3D_Resize(UINT width, UINT height)`（公開）

バックバッファ / 深度バッファの再構築を担当する。

**手順**
1. `releaseBackBuffer()` で既存 RTV / DSV 等を解放
2. `g_SwapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0)`
3. `configureBackBuffer()` で再生成

**重要点**
- `WM_SIZE` で `Direct3D_ResizeWindow()` と `Direct3D_Resize()` の両方を呼ぶ
- `Direct3D_Resize()` が呼ばれない限り描画先バッファサイズは変わらない
- `Direct3D_ResizeWindow()` だけでは実バッファは変わらない

### 3.3 `configureBackBuffer()` / `releaseBackBuffer()`（内部）

生成: `g_RenderTargetViews[2]`, `g_RenderTargetView`, `g_BackBufferDesc`, 内部シーン色 RT、1/4 解像度 AO RT、シーン解像度の深度バッファ / ビュー, 2D/3D ビューポート  
解放: 2 枚の RTV / シーン色 RT / AO RT / 深度バッファ / DSV

`g_RenderTargetView` は現在描画するバックバッファ 1 枚を指すエイリアスである。`Present()` が成功すると
`GetCurrentBackBufferIndex()` の値を読み直し、次の描画先へ `OMSetRenderTargets()` で切り替える。
Present 後および 2D はバックバッファ RTV のみをバインドする。3D は `Direct3D_BeginScene()` でシーン色とシーン深度へ切り替える。シーン色・深度はバックバッファと同じ解像度である。

## 4. ウィンドウサイズ変更時の流れ

### 4.1 クライアントサイズ通知

- `Direct3D_ResizeWindow(newClientW, newClientH)`
- `g_ClientWidth` / `g_ClientHeight` 更新 → 以降の 2D ビューポート計算に反映
- `WM_SIZE` ではカメラが存在する場合に `Camera_SetAspect(clientW / clientH)` を呼び、3D Projection も更新

### 4.2 必要に応じた D3D リソース再構築

- `Direct3D_Resize(newWidth, newHeight)`
- バックバッファ、内部シーン色 RT、AO RT、シーン深度バッファを作り直す

現行の `WM_SIZE`（`framework/main.cpp`）は `Direct3D_ResizeWindow` と `Direct3D_Resize` の両方を呼ぶ。
最小化中はリサイズを行わず、復帰時に再構築する。

## 5. 2D/3D の使い分け

### 2D
- `SetDepthEnable(false)` → 内部で `Direct3D_SetViewport2D()`
- アスペクト維持の中央領域（レター/ピラーボックス）

### 3D
- `SetDepthEnable(true)` → 内部で `Direct3D_SetViewport3D()`
- バックバッファと同じ解像度へ描画し、カメラ Projection でターゲットアスペクトのカバー表示を行う

※ 旧ドキュメントの `SetDepthTest` は現行では `SetDepthEnable`。

## 6. 実装上の注意点

- ビューポート計算は `g_ClientWidth` / `g_ClientHeight` 前提 → `Direct3D_ResizeWindow()` が必須
- クライアントサイズは最低 `1.0f` に丸められる
- バックバッファ実サイズとクライアントサイズは別概念
- 3D では負の `TopLeft` やバックバッファを超えるビューポートを使わない
- UI 配置は常に `SCREEN_X/HEIGHT`（1280×720）基準。描画解像度 `DRAW_SCREEN_*` を位置計算に使わない

## 7. まとめ

| 層 | API |
|---|---|
| クライアントサイズ管理 | `Direct3D_ResizeWindow` / `GetClientWidth` / `GetClientHeight` |
| 描画領域調整 | `SetDepthEnable` → 内部 `SetViewport2D/3D` |
| 実バッファ再生成 | `Direct3D_Resize` + `ResizeBuffers` + `configureBackBuffer` |

特に重要なのは、**`Direct3D_ResizeWindow()` はビューポート計算の基準値を更新し、
`Direct3D_Resize()` は 2 枚のバックバッファ RTV、内部シーン RT、AO RT、シーン深度を再構築する**という役割分担である。
また、Flip モデルでは `Present()` 後のバックバッファインデックスに合わせて、次フレームの描画先 RTV を更新する。
