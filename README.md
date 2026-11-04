# Gasoline Engine

A cross-platform game-engine project, starting with a C++20 foundation.

## AI development handoff

For a complete, source-oriented handoff covering verified implementation status, branch strategy, iOS builds, GPU renderer status, roadmap, and rules for AI agents, see [docs/AI_HANDOFF.md](docs/AI_HANDOFF.md). Read this before continuing development in a new chat or with another AI.

## Current status

- CMake-based C++20 project
- SDL3 window and event loop
- First-person camera with WASD movement and mouse-look
- Axis-aligned collision world; axis-wise camera movement stops at the first obstacle boundary, preventing large steps from passing through an AABB while still allowing wall sliding
- Basic scene registry with unique object names, transform data, visibility flags, lookup, and removal
- Wavefront OBJ loader for positions, optional UV coordinates and normals, polygon triangulation, negative indices, and per-corner attribute indexing
- Q/E vertical movement; Shift for faster movement
- Right-click toggles mouse capture; Esc releases the mouse or exits
- iOS touch: left half moves with a virtual joystick; right half drag controls camera look
- On-screen debug overlay showing smoothed FPS, camera coordinates, and control hints
- Near-plane clipping for cube faces and loaded mesh triangles, preserving the visible portion of geometry that crosses the camera's near plane
- Animated perspective-projected 3D cube with six face colors and all three rotation axes
- Renders multiple visible scene objects from shared OBJ mesh data, applying each object's position, rotation, scale, and visibility flag
- Experimental SDL_GPU backend renders indexed OBJ meshes with depth testing, P3 PPM texture sampling, metallic-roughness Cook-Torrance PBR, a directional shadow map, depth-neighborhood SSAO, experimental floor-only screen-space reflections with asynchronous per-pixel GPU diagnostic readback (valid-depth, floor receiver, ray-step and hit-pixel counts), capability-gated HDR/ACES tone mapping, optional 4x MSAA, screen-space bloom, mipmap fallback, and a GPU touch/FPS overlay; physical-device Metal behavior and the current SSR image remain unverified
- Press 1 or 2 to toggle the visibility of each demo pyramid at runtime
- Press F3 to toggle wireframe rendering for the cube and loaded OBJ meshes in both CPU and GPU modes
- Press F11 to toggle between windowed and fullscreen display
- Press R to reset the camera; press Space to pause or resume demo animation
- Press - / + to decrease or increase mouse-look sensitivity
- Filled triangles and floor-grid lines use a CPU-side Z-buffer for per-pixel visibility
- Software filled rendering is capped at 360,000 pixels per frame and scaled by SDL, preventing high-DPI windows and resizing from causing unbounded CPU and memory use
- World-space perspective floor grid that responds to camera movement and is occluded by nearer geometry
- GitHub Actions builds the Windows executable, runs all 12 CTest targets (camera, collision, scene management, shared scene render items, OBJ loader, PPM image, software renderer, GPU geometry rules, GPU frame recovery, touch-overlay hit testing, backend-switch input, and procedural primitive meshes) plus a rendering smoke test, and uploads the executable as an artifact

> The CPU renderer is an initial CPU-side projection and SDL rasterization prototype, not a modern GPU 3D pipeline. Filled triangles and floor-grid lines use a CPU-side depth buffer; wireframe mode is still drawn as SDL lines without depth testing. Geometry crossing the near plane is clipped instead of being discarded wholesale. To keep frame time and memory bounded when a shape fills the view or the window is high-DPI, filled rendering uses at most 800 x 450 pixels and SDL scales it to the window. The separate experimental GPU backend is described below.

## Camera controls

- **W / S:** move forward / backward
- **A / D:** strafe left / right
- **Q / E:** move down / up
- **Left or right Shift:** move faster
- **1:** show or hide the right demo pyramid; **2:** show or hide the left demo pyramid
- **F3:** toggle wireframe mode for the cube and OBJ meshes
- **F11:** toggle windowed/fullscreen mode
- **R:** reset camera position and view direction
- **Space:** pause/resume demo animation
- **- / +:** decrease/increase mouse-look sensitivity (bounded between 0.0005 and 0.01)
- Filled 3D geometry and floor-grid lines use a CPU-side Z-buffer for depth testing
- **Right mouse button:** capture/release the mouse for looking around
- **Mouse movement:** look around while captured
- **iPhone/iPad touch:** drag on the left half to move; drag on the right half to look around. Both can be used simultaneously.
- The on-screen CPU/GPU switch responds only inside its visible button rectangle.
- Switching render backends cancels active touch movement/look input and ignores held keyboard movement keys until they are released.
- **Esc:** release the mouse; press again to exit

The top-left debug overlay displays a smoothed FPS estimate, camera position (X/Y/Z), mouse-look sensitivity, and a reminder of the main controls. F3 switches the cube and loaded OBJ meshes between filled polygons and wireframe outlines. Filled 3D triangles and floor-grid lines use a CPU-side Z-buffer, so nearer surfaces win per pixel instead of relying only on polygon draw order. Wireframe lines are not depth-tested yet. It uses SDL3's built-in debug text renderer, so no font or SDL_ttf asset is required.

## OBJ mesh loader prototype

`ObjMeshLoader` reads Wavefront OBJ positions (`v`), texture coordinates (`vt`), normals (`vn`), and faces (`f`), triangulates polygon faces, and supports positive and negative indices for each attribute. Vertices are split when a single position is referenced with different UV or normal indices. It validates face indices, rejects incomplete trailing-slash face-vertex forms, and leaves the caller's existing mesh unchanged on failure. The bundled `assets/demo.obj` pyramid is loaded once and instanced twice in the demo scene; each visible scene object is rendered with its own position, rotation, and scale. Scene objects marked invisible are skipped by the renderer. OBJ UV coordinates are used for perspective-correct per-pixel texture mapping when a texture is assigned to a mesh instance. The renderer currently supports ASCII P3 PPM and BMP textures and keeps the CPU Z-buffer and existing vertex lighting. MTL material parsing and PNG/JPEG decoding are not implemented yet. Triangles crossing the camera's near plane are clipped in camera space before projection.

## Collision prototype

The collision module supports static axis-aligned bounding boxes (AABBs). Camera movement resolves X, Y and Z separately. Each axis step stops at the first AABB contact boundary, including when a large displacement would otherwise pass completely through an obstacle; unobstructed axes can still move so the camera can slide along faces. This is a prototype; rotating meshes, slopes, stair stepping and a full capsule controller are not implemented yet.

## Requirements

- CMake 3.24+
- A C++20 compiler
- Git and internet access during the first configure (SDL3 is downloaded by CMake)

## iOS support (in progress)

The current GitHub Actions workflow builds an arm64 iOS Device app bundle with the Xcode generator and packages it as an unsigned IPA. It includes the demo OBJ and P3 PPM assets in the app bundle. The deployment target is iOS 15.0. The workflow does not produce an iOS Simulator artifact.

The app includes an initial virtual joystick on the left and a drag-to-look area on the right, with simultaneous movement and camera look. The IPA is unsigned and is not ready for ordinary device installation or App Store distribution. Physical-device startup, Metal rendering, touch behavior, and performance still require separate on-device verification.

Before the iOS app build, the workflow extracts all eight embedded MSL source strings from `GpuRenderer.cpp`, compiles them with Xcode's Metal command-line compiler, and links a temporary Metal library. This catches MSL source compilation errors in CI; it does not prove runtime pipeline creation, device-specific format support, or correct rendering on an iPhone.

## GPU renderer (experimental)

The app contains a separate `GpuRenderer` implementation using SDL_GPU. It packs the demo OBJ pyramid mesh, procedural rotating cube mesh, and floor mesh into shared vertex/index buffers, then draws visible scene instances with their own mesh ranges and transforms. The GPU material shader uses metallic-roughness Cook-Torrance PBR (GGX distribution, Smith-Schlick geometry, and Schlick Fresnel) with directional and attenuated emissive point lighting. A 1024×1024 directional shadow map uses a 3×3 PCF comparison. A single-sample camera-depth prepass feeds a depth-neighborhood SSAO approximation and an experimental floor-only screen-space reflection (SSR) pass. The scene prefers a sampleable `R16G16B16A16_FLOAT` HDR target when supported and applies ACES-fitted tone mapping in the final composite; otherwise it falls back to the swapchain color format. When the selected scene and depth formats support it, the scene uses 4x MSAA and resolves into the sampleable intermediate target; otherwise it uses a single-sample target. A 9-tap screen-space bloom pass runs after reflection and ambient occlusion, then the FPS label and touch-control overlay are drawn with a single-sample pipeline. The bloom is a lightweight glow approximation, not ray tracing. Texture mipmaps are generated when the device reports the required format support; otherwise the renderer keeps a single-level texture. The depth prepass and shadow map prefer `R32_FLOAT`; the `R8G8B8A8_UNORM` fallback packs normalized depth into 24-bit RGB.

Press **G** on a keyboard, or use the on-screen backend switch control on touch devices, to request a CPU/GPU switch. If GPU initialization fails, the app attempts to recreate the CPU renderer. If a GPU frame fails, the app releases the GPU renderer and its window claim, then attempts to recreate the CPU renderer; the app exits only if CPU renderer recreation also fails.

The GPU renderer provides a separate line-fill pipeline for the F3 wireframe toggle; depth testing remains enabled in GPU wireframe mode. The GPU mesh path tracks UV presence separately from UV values, so valid negative or out-of-range OBJ UV coordinates are still sampled using the texture's repeat addressing. A triangle is textured only when all three corners have UV coordinates; mixed-UV triangles use vertex color for the whole primitive to avoid interpolating the UV-presence flag. Before upload, RGBA8 texture dimensions and the exact width × height × 4 byte count are validated against the SDL_GPU transfer-buffer limit. The Metal fragment-stage input now declares the `hasTexCoord` varying passed by the vertex stage; physical-device shader compilation and rendering still require on-device verification. The current GPU path requests MSL shaders and is intended for Metal-capable devices. Windows GPU runtime activation and physical iPhone/iPad Metal rendering have not been verified by the repository's CI. The CPU software renderer remains available as a separate backend. MTL materials, per-material texture selection, PNG/JPEG GPU texture loading, and a shared CPU/GPU renderer interface are not implemented. The GPU backend currently shares one P3 PPM texture across its uploaded meshes.

The fullscreen composite writes a second RGBA8 diagnostic target. About once per second, the renderer downloads this per-pixel classification asynchronously and appends aggregate valid-depth, reflective-receiver, ray-march-step, and hit-pixel counts to ssr.log; the readback is processed only after its GPU fence signals. This is diagnostic instrumentation, not fragment-stage atomic counters, and it does not identify the exact visual root cause by itself.\n\nThe separate `--gpu-smoke-test` command only checks SDL_GPU device/window/swapchain submission; it does not exercise the indexed mesh renderer. It is not part of the current CI workflow.

## Build on Windows

Run in a terminal with CMake and MSVC available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\originalengine_app.exe
```

## Build through GitHub Actions (no local Windows PC required)

1. Open the repository's Actions tab.
2. Select the Build workflow and choose Run workflow, or push a commit to `ios/initial-support`.
3. Open the completed run and download the Gasoline Engine-Windows-x64 artifact.
4. Extract the ZIP and run originalengine_app.exe on a compatible Windows PC.

GitHub Actions can compile the Windows app and run automated tests, but it cannot show the interactive window inside the build log. You need a Windows device to run and visually inspect the executable.

On `ios/initial-support`, the workflow also sends a Discord build report after the iOS unsigned IPA job succeeds. The report includes the Windows and iOS job results, excludes the notification job itself, and keeps embed fields within Discord's message limits. A Discord delivery error can fail the overall workflow even when both build jobs succeed; inspect the individual job results to distinguish build status from notification status.

## Project layout

```text
include/engine/       Public engine headers
src/engine/           Engine implementations
src/main.cpp          Sample application entry point
tests/                12 CTest targets plus the application smoke test, including shared scene-render-item, backend-switch input, and procedural primitive-mesh coverage
.github/workflows/    GitHub Actions workflow
```

## Roadmap

1. Initial application and build foundation
2. First 3D projection and colored cube
3. First-person camera and movement
4. Validate GPU resource lifecycle, failure recovery, and Metal rendering on a physical iPhone
5. Expand GPU material support, image formats, and normal mapping
6. Improve physics, audio, and platform support


---

# 日本語

Gasoline Engine は、C++20 を基盤として開発しているクロスプラットフォーム向けゲームエンジンのプロジェクトです。

## AI開発引き継ぎ資料

別のAIや新しいチャットで開発を継続する場合は、実装状況・ブランチ・iOSビルド・GPU描画の状態・ロードマップ・開発ルールをまとめた [docs/AI_HANDOFF.md](docs/AI_HANDOFF.md) を最初に読んでください。

## 現在の機能

- CMake を使用した C++20 プロジェクト
- SDL3 によるウィンドウ管理とイベントループ
- WASD 移動とマウス視点操作に対応した一人称カメラ
- 軸平行境界ボックス（AABB）による衝突判定。各軸の移動を最初の接触位置で止め、大きな移動量でも障害物をすり抜けず、壁沿いの移動も可能
- オブジェクト名の一意性、Transform、表示・非表示、検索、削除に対応した基本的なシーン管理
- Wavefront OBJ の頂点・UV 座標・法線・面データ読み込み。多角形の三角形分割と各属性の負のインデックスに対応
- Q/E キーによる上下移動、Shift キーによる高速移動
- 右クリックでマウス捕捉を切り替え、Esc キーで捕捉解除または終了
- FPS、カメラ座標、操作ヒントを表示するデバッグ HUD
- カメラの近接平面を横切るキューブや読み込み済みメッシュのポリゴンをクリッピングし、見える部分を描画
- 3 軸で回転する、6 色の面を持つ透視投影 3D キューブ
- 同じ OBJ メッシュデータを複数のシーンオブジェクトで共有し、それぞれの位置・回転・拡大縮小・表示状態を反映して描画
- 1 キーと 2 キーでデモ用ピラミッドの表示・非表示を切り替え
- カメラの移動に応じて見え方が変わり、近いメッシュに隠れる、ワールド座標系の透視投影フロアグリッド
- ソフトウェア描画は 1 フレームあたり 360,000 ピクセルに制限され、SDL により拡大表示されるため、高 DPI ウィンドウやリサイズで CPU・メモリ使用量が無制限に増加しない
- 塗りつぶしポリゴンとフロアグリッド線に対応した CPU 側 Z バッファによるピクセル単位の奥行き判定
- GitHub Actions による Windows 実行ファイルのビルド、12件の CTest（カメラ・衝突判定・シーン管理・共通シーン描画項目・OBJ ローダー・PPM 画像・ソフトウェアレンダラー・GPU ジオメトリ判定・GPU フレーム復旧・タッチボタンのヒット判定・バックエンド切替入力・プロシージャルメッシュ）、描画スモークテスト、および成果物のアップロード

> CPU 側には透視投影、Z バッファ、透視補正付き UV テクスチャ描画、近接平面クリッピング、ワイヤーフレームに対応したソフトウェアレンダラーがあります。実験的な SDL_GPU レンダラーは、PBR、方向シャドウマップ、深度近傍 SSAO、床専用の実験的 SSR、対応時の HDR/ACES 合成、4x MSAA、9タップのブルーム、タッチオーバーレイを実装しています。深度形式の RGBA8 fallback では24-bit RGB packingを使用します。iOS CI は埋め込み MSL のコンパイルを検証しますが、iPhone 実機の Metal runtime と描画結果は未確認です。SSR は以前の実機テストでアーティファクトが確認されており、最新修正で解消したとはまだ確認できていません。

## カメラ操作

- **iPhone/iPad の切替ボタン：** 表示されている CPU/GPU ボタンの矩形内をタップすると描画バックエンドを切り替えます。切替時には進行中の移動・視点タッチを解除します。
- **W / S：** 前進 / 後退
- **A / D：** 左右に平行移動
- **Q / E：** 下 / 上に移動
- **左または右 Shift：** 高速移動
- **1：** 右側のデモ用ピラミッドを表示 / 非表示
- **2：** 左側のデモ用ピラミッドを表示 / 非表示
- **マウス右ボタン：** 視点操作用のマウス捕捉を切り替え
- **マウス移動：** マウス捕捉中に視点を操作
- **Esc：** マウス捕捉を解除。もう一度押すと終了

画面左上のデバッグ HUD には、平滑化した FPS の推定値、カメラ座標（X/Y/Z）、主な操作方法が表示されます。塗りつぶしポリゴンとフロアグリッド線は CPU 側の Z バッファで奥行き判定されます。ワイヤーフレーム線はまだ深度判定されません。SDL3 標準のデバッグ文字描画機能を使うため、フォントファイルや SDL_ttf は必要ありません。

## OBJ メッシュローダー（試作）

`ObjMeshLoader` は Wavefront OBJ の `v`（頂点）と `f`（面）レコードを読み込みます。多角形の面を三角形に分割し、正の頂点インデックスと負の頂点インデックスの両方に対応します。OBJ の UV 座標を使った透視補正付きのピクセル単位テクスチャ描画に対応しました。現時点では ASCII 形式の P3 PPM と BMP テクスチャを読み込み、CPU 側の Z バッファと既存の頂点ライティングを維持します。MTL マテリアル解析や PNG/JPEG のデコードは未実装です。面のインデックスを検証し、UVインデックスのない不正な末尾スラッシュ形式も拒否します。読み込みに失敗した場合は呼び出し元の既存メッシュを変更しません。

同梱の `assets/demo.obj` にあるピラミッドは一度読み込まれ、デモシーン内の 2 つのオブジェクトで共有されます。表示状態のオブジェクトは、それぞれの位置・回転・拡大縮小を反映して描画されます。非表示に設定されたオブジェクトは描画されません。OBJ の UV 座標を使った透視補正付きのピクセル単位テクスチャ描画に対応しました。現時点では ASCII 形式の P3 PPM と BMP テクスチャを読み込み、CPU 側の Z バッファと既存の頂点ライティングを維持します。MTL マテリアル解析や PNG/JPEG のデコードは未実装です。カメラの近接平面を横切る三角形は、投影の前にカメラ座標系でクリッピングされます。

## 衝突判定（試作）

衝突判定モジュールは、静的な軸平行境界ボックス（AABB）に対応しています。カメラの移動を X・Y・Z 軸ごとに解決し、各軸の移動量を最初の接触位置で止めます。移動量が大きく、障害物を完全に通り抜ける位置まで進もうとした場合も、手前で停止します。衝突していない軸の移動は続けられるため、壁に沿った移動も可能です。回転したメッシュとの衝突、斜面、階段昇降、カプセル形状を使った本格的なキャラクターコントローラーは未実装です。

## 必要なもの

- CMake 3.24 以降
- C++20 対応コンパイラ
- Git と初回構成時のインターネット接続（CMake が SDL3 をダウンロードします）

CPU/GPUの描画対象は `SceneRenderItem` の共通リストで管理します。各項目はメッシュ、シーンオブジェクト、テクスチャパス、GPU mesh index、およびCPU/GPUそれぞれの描画対象フラグを保持し、両レンダラーは同じリストを参照して自身に割り当てられた項目だけを描画します。GPU専用デモキューブはCPU側で重複描画しないようCPU対象フラグを無効にしています。これは描画対象データの共有であり、CPU/GPUの描画APIや機能そのものを統合したものではありません。

## GPU レンダラー（実験段階）

`GpuRenderer` は SDL_GPU を使い、OBJ ピラミッド、回転キューブ、床のメッシュを共有 vertex/index buffer に格納して描画します。GPU マテリアルは GGX 分布・Smith-Schlick 幾何項・Schlick Fresnel を使う metallic-roughness Cook-Torrance PBR です。方向光と減衰付き発光ポイントライトに加え、1024×1024 の方向シャドウマップと3×3 PCFを使用します。単一サンプルのカメラ深度 prepass は、深度近傍 SSAO と床専用の実験的 SSR に使われます。対応デバイスでは `R16G16B16A16_FLOAT` の HDR scene target を優先し、最終合成で ACES fitted tone mapping を適用します。必要な color/depth format が対応する場合は4x MSAAを使い、非対応時は single-sample に fallback します。反射・SSAOの後に9タップのスクリーンスペースブルームを適用し、FPSとタッチオーバーレイを最後に描画します。ブルームは軽量な発光表現であり、レイトレーシングではありません。`R32_FLOAT` が使えない深度形式では、`R8G8B8A8_UNORM` に正規化深度を24-bit RGBとして格納します。テクスチャ mipmap は必要な形式が対応する場合に生成し、非対応時は単一レベルで動作します。

キーボードの **G** キー、またはタッチ画面上のバックエンド切替操作で CPU / GPU の切替を要求できます。GPU 初期化に失敗した場合は CPU レンダラーの再生成を試みます。GPU のフレーム描画に失敗した場合も GPU リソースとウィンドウの所有権を解放して CPU レンダラーへの復帰を試みます。CPU レンダラーの再生成にも失敗した場合はアプリを終了します。

GPU メッシュ描画では UV の有無を UV 数値とは別に管理するため、負の値や 0〜1 の範囲外にある有効な OBJ UV 座標も、テクスチャの repeat アドレス指定でサンプリングされます。1つの三角形で3頂点すべてにUVがある場合だけテクスチャを使用し、UVの有無が混在する三角形は全体を頂点カラーで描画して、フラグメント間のUV有無フラグ補間を避けます。GPUへのRGBA8テクスチャ入力は、幅×高さ×4バイトとの完全一致とSDL_GPU転送サイズ上限を検証します。GPU 経路は MSL シェーダーを要求し、Metal 対応デバイスを主な対象としています。Windows での GPU 実行と iPhone 実機での Metal 描画は未確認です。CPU ソフトウェアレンダラーは別経路として維持されています。MTL マテリアル、複数マテリアル / テクスチャ、PNG / JPEG の GPU テクスチャ読み込み、CPU / GPU 共通 Renderer API は未実装です。GPU ワイヤーフレームは F3 キーで切り替えられます。

別途用意されている `--gpu-smoke-test` は SDL_GPU デバイス・ウィンドウ・スワップチェーンの基本的な送信経路を確認するだけで、OBJ メッシュレンダラーの検証ではありません。現在の CI ワークフローには含まれていません。

## Windows でビルドする

CMake と MSVC が使えるターミナルで、次のコマンドを実行します。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\\build\\Release\\originalengine_app.exe
```

## GitHub Actions でビルドする（Windows PC が手元になくても可）

1. GitHub リポジトリの **Actions** タブを開きます。
2. **Build** ワークフローを選び、**Run workflow** を実行するか、`main` ブランチにコミットを push します。
3. ワークフローの実行が完了したら、成果物 `Gasoline Engine-Windows-x64` をダウンロードします。
4. ZIP を展開し、対応する Windows PC で `originalengine_app.exe` を実行します。

GitHub Actions は Windows アプリのビルドと自動テストを実行できますが、ビルドログ内に操作可能なゲームウィンドウを表示することはできません。実際の見た目を確認するには、Windows 端末で実行する必要があります。

## ディレクトリ構成

```text
include/engine/       公開用エンジンヘッダー
src/engine/           エンジンの実装
src/main.cpp          サンプルアプリケーションのエントリーポイント
tests/                カメラ・衝突判定・シーンなどのテスト
.github/workflows/    GitHub Actions のワークフロー
```

## 開発ロードマップ

1. GPU リソースの失敗経路・解放・切替安定性の検証
2. iPhone 実機での Metal 描画とタッチ操作の確認
3. GPU マテリアル、追加画像形式、ワイヤーフレームへの対応
4. 音声・物理演算・対応プラットフォームの拡張


## GPU frame recovery test

The `gpu_frame_recovery` unit test exercises the shared control flow used by the application when a GPU frame succeeds or fails. It verifies that GPU resources are not released on success, that GPU release happens before CPU renderer recreation after a failure, and that a failed CPU recreation is reported as fatal. The test validates callback ordering and result selection; it does not simulate SDL_GPU device loss, a real window-claim transition, or Metal runtime behavior.


## GPU rotating cube

When the GPU backend is active, it renders a dedicated six-face cube mesh with per-face UVs and normals. The cube rotates around all three axes; pressing Space pauses or resumes its animation. The GPU cube uses a solid emissive cyan material on all six faces and has no UV texture coordinates; the bundled P3 PPM texture remains available to the pyramid meshes and does not replace the CPU renderer's existing demo scene. The procedural cube mesh is covered by the `primitive_meshes` CTest.


### GPU multisample anti-aliasing

The experimental SDL_GPU renderer checks runtime format and sample-count support for 4x MSAA on its scene color and depth targets. The scene resolves into a sampleable intermediate color texture, which is then composited with the screen-space bloom pass into the swapchain. Devices that do not support the required 4x formats retain the single-sample scene path. Actual Metal rendering, edge quality, bloom appearance, and performance must still be checked on a physical iPhone.

## Cyan scene lighting

The CPU and experimental GPU renderers share a dark cyan sky, a cyan floor surface, and a bright cyan rotating cube. The CPU renderer overlays its existing depth-tested cyan floor grid; the GPU renderer draws the shared floor plane as scene geometry. The GPU mesh fragment shader adds an emissive cyan contribution, and the full-screen composite adds a thresholded screen-space bloom effect. Physical iPhone appearance and performance still require device testing.


### GPU emissive point lighting

The GPU renderer no longer draws an expanded cube shell for a silhouette rim. The rotating cube uses a uniformly emissive cyan material and acts as a moving point light. The embedded Metal shader calculates distance attenuation, diffuse response, and specular response from the cube's world position, so nearby surfaces such as the floor receive cyan illumination. The scene renders to a sampleable intermediate color target before a full-screen bloom composite. The composite uses a 9-tap neighborhood filter, extracts bright samples with a luminance threshold, and adds a softened glow to the original scene. This is a lightweight screen-space bloom approximation, not HDR multi-pass blur or ray tracing; it does not trace reflected light or multiple light bounces. Physical iPhone appearance, Metal shader compilation, and performance still require device verification.


### Floor and cube color separation

The floor uses a darker blue-teal material, while the rotating cube keeps the bright cyan treatment. The GPU renderer selects the floor material through a per-instance uniform flag rather than inferring it from surface normals, so horizontal cube faces keep the cube material. The CPU floor mesh and grid are also darker than the cube.


## GPU bloom and soft shadow update

- Expanded the screen-space bloom composite from a compact 9-tap cross to a 13-tap kernel with diagonal and wider-radius samples, a slightly lower highlight threshold, and a stronger glow contribution. The effect remains a single-pass screen-space approximation; it does not add an HDR target or a multi-pass Gaussian blur.
- Added a soft analytic shadow approximation to the GPU floor shader. It projects an elliptical shadow from the rotating cube onto the horizontal floor opposite the directional light, with a smooth edge and partial opacity. This is not a shadow map and does not cast shadows onto arbitrary meshes.
- The changes are source-level only until the matching GitHub Actions run completes and the resulting IPA is checked on a physical iPhone. CI does not compile the embedded MSL at runtime or verify the visual result.


### GPU bloom and emissive material correction

- Reduced the GPU screen-space bloom from the wide 13-tap kernel to a restrained 9-tap cross, raised the highlight threshold from 0.55–1.0 to 0.82–1.20, and reduced the composite gain from 1.55 to 0.55.
- Added a separate per-object emissive flag. Only the rotating cube mesh (mesh range 1, also used as the point-light position source) receives the strong cyan emission. Pyramid meshes no longer receive the generic non-floor emission; they use ambient, directional and attenuated point-light shading instead.
- The change is source-level. The current iOS/Windows CI does not compile the embedded MSL at runtime or verify the visual appearance on a physical iPhone.


## GPU metallic-roughness PBR — 2026-11-03

- Extended the shared `SceneRenderItem` with per-item metallic and roughness values. CPU rendering continues to consume the same shared item list and ignores GPU-only material parameters.
- Replaced the GPU renderer's Blinn-Phong direct-light terms with a single-pass Cook-Torrance BRDF using GGX normal distribution, Smith-Schlick geometry, and Schlick Fresnel for directional and point lighting.
- Demo material settings: pyramids metallic 0.05 / roughness 0.72, emissive cube metallic 0.35 / roughness 0.24, floor metallic 0.0 / roughness 0.92. Values are clamped before being sent to the shader.
- This is metallic-roughness PBR lighting with the current single shared base-color texture and cyan material tint. It does not add an environment map, image-based lighting, normal maps, tone mapping, or per-material texture loading. Metal shader runtime compilation and appearance remain unverified on a physical iPhone.


## GPU HDR scene target — 2026-11-03

- The GPU renderer now prefers an `R16G16B16A16_FLOAT` intermediate scene color target when the device reports support for both color-target and sampler usage. If unavailable, it retains the swapchain-format intermediate target.
- The scene pipeline and optional 4x MSAA color target use the selected scene format; the final composite remains in the swapchain format. The composite applies an ACES-fitted tone map after the existing bloom contribution.
- This is a capability-gated HDR render-target path. It does not add exposure adaptation or a separate multi-pass bloom pyramid. Metal format support, HDR highlight appearance, and memory/performance on iPhone hardware still require device verification.

### GPU shadow mapping — 2026-11-03

- Added a 1024×1024 directional shadow map for the SDL_GPU renderer. A light-space depth pass renders visible GPU scene items into an `R32_FLOAT` color target with a depth attachment, then the main PBR fragment shader samples it using a 3×3 percentage-closer filtering (PCF) kernel.
- Shadow visibility modulates the directional-light contribution; ambient and point-light terms remain unshadowed. The previous floor-only analytic shadow approximation was removed.
- The shadow map uses a fixed orthographic light camera aligned with the directional light. Devices without sampleable `R32_FLOAT` color-target support fail GPU initialization with an explicit error, allowing the existing application CPU fallback to take over.
- This is a single directional shadow map, not cascaded or point-light shadowing. Shadow acne, peter-panning, light-frustum coverage, Metal runtime shader compilation, visual quality, and iPhone performance still require CI and physical-device verification.


### GPU shadow target compatibility

The directional shadow map prefers `R32_FLOAT` when the GPU supports it as a sampleable color target. Devices without that format use `R8G8B8A8_UNORM` and store depth in the red channel, avoiding an unnecessary GPU-backend failure caused only by the single-channel format. If neither format is supported, the existing CPU fallback remains available. Runtime Metal format support and physical-device shadow appearance still require iPhone verification.


## GPU screen-space ambient occlusion (SSAO) — 2026-11-03

- Added a single-sample camera-depth prepass using the existing GPU depth pipeline. The pass writes projected camera depth to a sampleable color texture and uses its own depth attachment, independent of the scene's optional 4x MSAA depth target.
- The final screen-space composite samples that depth texture and evaluates eight neighboring depth samples. Samples that are closer than the current pixel reduce ambient visibility; the result modulates scene color before the existing bloom and ACES-fitted tone mapping.
- This is a lightweight depth-neighborhood SSAO approximation. It does not reconstruct full view-space positions/normals, use a hemisphere kernel, or implement temporal filtering. The depth pass adds an extra geometry pass and two screen-sized textures.
- Embedded MSL compilation, Metal texture sampling, visual quality, and iPhone performance still require physical-device verification. CI compilation alone does not validate the shader runtime.


## GPU screen-space reflections (SSR) — 2026-11-03

- Added a screen-space reflection ray-march to the GPU fullscreen composite. It reconstructs world positions from the camera-depth prepass, estimates normals from neighboring depth samples, reflects the view ray, and searches up to 24 projected steps for visible scene-depth intersections.
- The first implementation intentionally focuses on near-horizontal surfaces (the demo floor), uses a conservative fixed reflection strength, and fades hits near screen edges and at longer ray distances. It reuses the existing scene-color and depth textures; no additional render target or geometry pass is added.
- Because the current prepass stores depth only, this version does not read per-pixel PBR roughness/metallic values and does not produce reflections for off-screen or occluded objects. Screen-space edge/disocclusion artifacts are inherent limitations.
- Existing SSAO, bloom, and ACES tone mapping remain in the composite. Embedded MSL compilation is not covered by the normal C++ CI; iPhone runtime shader compilation, visual quality, and performance require device verification.


### SSR depth-hit stability correction — 2026-11-03

- Tightened the SSR surface mask to favor near-horizontal surfaces, increased the ray-origin normal offset, skipped the near-field region, and reduced reflection strength.
- Replaced the previous loose single-sample depth-thickness acceptance with a front-to-back depth-delta crossing test and four binary-refinement steps. The ray now requires a valid previous sample before accepting a hit, reducing false intersections around object silhouettes that appeared as stacked reflection layers.
- This is a source-level correction. The resulting reflection placement and the visible floor gap beneath the cube still require physical iPhone verification.


### SSR surface-normal stability correction — 2026-11-03

- Replaced the one-sided depth-neighbor normal estimate with four-neighbor tangent selection. For each screen axis, the shader chooses the shorter valid world-space neighbor segment rather than blindly crossing a foreground/background silhouette.
- SSR is rejected for pixels whose selected tangent lengths exceed 0.35 world units, preventing depth-discontinuity-derived normals from being treated as reflective floor surfaces.
- This is a source-level stability guard. It does not remove inherent SSR limitations, and the visible result still requires testing on the target iPhone.


### SSR reflective-surface mask correction — 2026-11-04

- The demo scene's only intended reflective surface is the explicit floor mesh at world Y = -1.5. SSR now uses that known plane and a fixed upward normal instead of treating every screen-space near-horizontal surface as reflective.
- This prevents horizontal faces of the rotating cube and other mesh facets from being selected as reflection receivers. Reflection hit tracing still uses the camera-depth prepass and retains normal SSR screen-space limitations.
- This is source-level only; Metal runtime compilation and the visual result require a new IPA and physical-device verification.


### SSR ray-march density correction — 2026-11-04

- Increased the screen-space reflection march from 32 to 64 samples and reduced world-space spacing from 0.075 to 0.035, with a 0.12 near-origin offset. The maximum trace distance remains approximately 2.3 world units.
- This targets discontinuous/combed reflection silhouettes caused by coarse ray steps. It does not change the depth reconstruction or intersection model; visual improvement and iPhone GPU cost still require device verification.


### Physical-device SSR verification — 2026-11-04

- The user confirmed that the supplied iPhone screenshot was captured with an IPA containing commit `2e2c73a` (the floor-only SSR receiver mask correction).
- The screenshot still shows a repeated/combed cyan reflection image below the cube, with strong horizontal/vertical banding and dark rectangular patches on the floor. The reflection remains visibly incorrect; do not describe SSR as fixed.
- This physical result demonstrates that restricting the reflective receiver to the known floor plane did not resolve the artifact. The earlier hypothesis that cube faces were being selected as reflection receivers is insufficient as the sole explanation.
- Keep the issue open. Next investigation should validate the camera-depth prepass encoding/format and sampling, screen-UV to clip/NDC conventions, inverse view-projection reconstruction, and the ray-depth intersection comparison against the exact projection/depth convention. Add temporary debug views for raw depth, reconstructed world position, ray UV, and hit mask before further tuning.
- The later source change that increases the SSR march density (32 to 64 steps, spacing 0.075 to 0.035) is a separate source-level change; this screenshot does not verify that later change unless the tested IPA is confirmed to contain it.
- CI/IPA packaging does not compile embedded MSL at runtime or validate the rendered image. Record physical-device validation separately from CI success.


## GPU screen-space reflections (experimental)

The GPU fullscreen composite includes a floor-only screen-space reflection pass. It reconstructs world position from the camera-depth prepass, traces a reflected view ray against the sampled scene depth, and samples the scene color at the first accepted hit. SSR remains limited to visible on-screen geometry and can fail at screen edges or depth discontinuities; it is not ray tracing or a complete material-aware reflection system.

The camera-depth prepass and directional shadow map prefer `R32_FLOAT`. On devices that require the `R8G8B8A8_UNORM` fallback, normalized depth is packed into RGB at 24-bit precision and decoded by SSR, SSAO, and shadow sampling. The precision correction is source-level until a new IPA is tested on a physical iPhone. Embedded MSL runtime compilation and the visible reflection result are not verified by ordinary C++ CI.


## GPU SSR toggle

In GPU mode, tap the **SSR** button at the top-right (immediately left of **CPU**) to enable or disable screen-space reflections. The keyboard shortcut **T** toggles the same setting on desktop. Disabling SSR leaves the GPU scene, lighting, shadows, SSAO, bloom, and tone mapping active; it skips only the SSR reflection contribution. This is a diagnostic comparison control, not a correction for the existing reflection artifact. Verify the visual difference on the target iPhone.

## SSR diagnostic log

The app writes `ssr.log` beside `startup.log` in SDL's platform-specific writable preference directory. On iOS this avoids writing into the read-only app bundle. The file is recreated at launch and records SSR toggle changes, plus once-per-second GPU frame state (SSR state, resolution, FPS, camera position, and GPU driver) and GPU frame errors. These are CPU-visible diagnostics only; per-pixel GPU ray hit/miss counters are not currently collected.
