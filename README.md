# Gasoline Engine

A cross-platform game-engine project, starting with a C++20 foundation.

## AI development handoff\n\nFor a complete, source-oriented handoff covering verified implementation status, branch strategy, iOS builds, GPU renderer status, roadmap, and rules for AI agents, see [docs/AI_HANDOFF.md](docs/AI_HANDOFF.md). Read this before continuing development in a new chat or with another AI.\n\n## Current status

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
- Press 1 or 2 to toggle the visibility of each demo pyramid at runtime
- Press F3 to toggle wireframe rendering for the cube and loaded OBJ meshes
- Press F11 to toggle between windowed and fullscreen display
- Press R to reset the camera; press Space to pause or resume demo animation
- Press - / + to decrease or increase mouse-look sensitivity
- Filled triangles and floor-grid lines use a CPU-side Z-buffer for per-pixel visibility
- Software filled rendering is capped at 360,000 pixels per frame and scaled by SDL, preventing high-DPI windows and resizing from causing unbounded CPU and memory use
- World-space perspective floor grid that responds to camera movement and is occluded by nearer geometry
- GitHub Actions builds the Windows executable, runs camera, collision, scene-management, and OBJ-loader unit tests plus a rendering smoke test, and uploads the executable as an artifact

> The current renderer is an initial CPU-side projection and SDL rasterization prototype, not yet a modern GPU 3D pipeline. Filled triangles and floor-grid lines use a CPU-side depth buffer; wireframe mode is still drawn as SDL lines without depth testing. Geometry crossing the near plane is clipped instead of being discarded wholesale. To keep frame time and memory bounded when a shape fills the view or the window is high-DPI, filled rendering uses at most 800 x 450 pixels and SDL scales it to the window.

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
- **Esc:** release the mouse; press again to exit

The top-left debug overlay displays a smoothed FPS estimate, camera position (X/Y/Z), mouse-look sensitivity, and a reminder of the main controls. F3 switches the cube and loaded OBJ meshes between filled polygons and wireframe outlines. Filled 3D triangles and floor-grid lines use a CPU-side Z-buffer, so nearer surfaces win per pixel instead of relying only on polygon draw order. Wireframe lines are not depth-tested yet. It uses SDL3's built-in debug text renderer, so no font or SDL_ttf asset is required.

## OBJ mesh loader prototype

`ObjMeshLoader` reads Wavefront OBJ positions (`v`), texture coordinates (`vt`), normals (`vn`), and faces (`f`), triangulates polygon faces, and supports positive and negative indices for each attribute. Vertices are split when a single position is referenced with different UV or normal indices. It validates face indices and leaves the caller's existing mesh unchanged on failure. The bundled `assets/demo.obj` pyramid is loaded once and instanced twice in the demo scene; each visible scene object is rendered with its own position, rotation, and scale. Scene objects marked invisible are skipped by the renderer. OBJ UV coordinates are used for perspective-correct per-pixel texture mapping when a texture is assigned to a mesh instance. The renderer currently supports ASCII P3 PPM and BMP textures and keeps the CPU Z-buffer and existing vertex lighting. MTL material parsing and PNG/JPEG decoding are not implemented yet. Triangles crossing the camera's near plane are clipped in camera space before projection.

## Collision prototype

The collision module supports static axis-aligned bounding boxes (AABBs). Camera movement resolves X, Y and Z separately. Each axis step stops at the first AABB contact boundary, including when a large displacement would otherwise pass completely through an obstacle; unobstructed axes can still move so the camera can slide along faces. This is a prototype; rotating meshes, slopes, stair stepping and a full capsule controller are not implemented yet.

## Requirements

- CMake 3.24+
- A C++20 compiler
- Git and internet access during the first configure (SDL3 is downloaded by CMake)

## iOS support (in progress)

An iOS Simulator build is being added alongside the Windows build. The iOS target is packaged as an unsigned Xcode app bundle and includes the demo OBJ and PPM assets in the app's Resources directory. GitHub Actions builds an arm64 iOS Simulator artifact without requiring an Apple signing certificate.

The iOS build includes an initial virtual joystick on the left and drag-to-look area on the right, with simultaneous movement and camera look. On-device performance tuning, physical-device testing, and App Store signing/distribution remain incomplete. The workflow currently builds an arm64 iOS Device app bundle and packages it as an unsigned IPA. It is not signed for installation; physical-device execution still requires an appropriate signing/install workflow and on-device testing.

## GPU backend smoke test (experimental)

The app has an opt-in `--gpu-smoke-test` mode that bypasses `SDL_Renderer`, creates an SDL GPU device, claims the window, acquires one swapchain texture, clears it, and submits one GPU command buffer. This checks the basic window/swapchain presentation path only; it does not load the HLSL shaders or render 3D geometry.

On a GPU-capable Windows machine, build the app and run:

```powershell
.\\build\\Release\\originalengine_app.exe --gpu-smoke-test
```

A successful run is reported in the startup diagnostic log. This test is not currently part of CI and has not been verified on a physical iPhone/iPad.

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
2. Select the Build workflow and choose Run workflow, or push a commit to main.
3. Open the completed run and download the Gasoline Engine-Windows-x64 artifact.
4. Extract the ZIP and run originalengine_app.exe on a compatible Windows PC.

GitHub Actions can compile the Windows app and run automated tests, but it cannot show the interactive window inside the build log. You need a Windows device to run and visually inspect the executable.

## Project layout

```text
include/engine/       Public engine headers
src/engine/           Engine implementations
src/main.cpp          Sample application entry point
tests/                Automated camera, collision, and scene tests
.github/workflows/    GitHub Actions workflow
```

## Roadmap

1. Initial application and build foundation
2. First 3D projection and colored cube
3. First-person camera and movement
4. GPU-backed rendering and depth testing
5. Scene object instances and loaded mesh rendering
6. Texture/material loading, audio, and physics
7. Platform support expansion


---

# 日本語

Gasoline Engine は、C++20 を基盤として開発しているクロスプラットフォーム向けゲームエンジンのプロジェクトです。

## AI開発引き継ぎ資料\n\n別のAIや新しいチャットで開発を継続する場合は、実装状況・ブランチ・iOSビルド・GPU描画の状態・ロードマップ・開発ルールをまとめた [docs/AI_HANDOFF.md](docs/AI_HANDOFF.md) を最初に読んでください。\n\n## 現在の機能

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
- GitHub Actions による Windows 実行ファイルのビルド、カメラ・衝突判定・シーン管理・OBJ ローダーのユニットテスト、描画スモークテスト、および成果物のアップロード

> 現在の描画機能は CPU 側のソフトウェアラスタライザーです。透視投影、ピクセル単位の Z バッファ、透視補正付き UV テクスチャ描画、近接平面クリッピング、深度判定付きワイヤーフレームに対応しています。SDL_Renderer は完成した CPU 描画バッファの表示に使用しています。まだ SDL GPU API を使った GPU 3D パイプラインは実装されていません。

## カメラ操作

- **W / S：** 前進 / 後退
- **A / D：** 左右に平行移動
- **Q / E：** 下 / 上に移動
- **左または右 Shift：** 高速移動
- **1：** 右側のデモ用ピラミッドを表示 / 非表示
- **2：** 左側のデモ用ピラミッドを表示 / 非表示
- **マウス右ボタン：** 視点操作用のマウス捕捉を切り替え
- **マウス移動：** マウス捕捉中に視点を操作
- **Esc：** マウス捕捉を解除。もう一度押すと終了

画面左上のデバッグ HUD には、平滑化した FPS の推定値、カメラ座標（X/Y/Z）、主な操作方法が表示されます。塗りつぶしポリゴン、フロアグリッド線、ワイヤーフレーム線は CPU 側の Z バッファで奥行き判定されます。SDL3 標準のデバッグ文字描画機能を使うため、フォントファイルや SDL_ttf は必要ありません。

## OBJ メッシュローダー（試作）

`ObjMeshLoader` は Wavefront OBJ の `v`（頂点）と `f`（面）レコードを読み込みます。多角形の面を三角形に分割し、正の頂点インデックスと負の頂点インデックスの両方に対応します。OBJ の UV 座標を使った透視補正付きのピクセル単位テクスチャ描画に対応しました。現時点では ASCII 形式の P3 PPM と BMP テクスチャを読み込み、CPU 側の Z バッファと既存の頂点ライティングを維持します。MTL マテリアル解析や PNG/JPEG のデコードは未実装です。面のインデックスを検証し、読み込みに失敗した場合は呼び出し元の既存メッシュを変更しません。

同梱の `assets/demo.obj` にあるピラミッドは一度読み込まれ、デモシーン内の 2 つのオブジェクトで共有されます。表示状態のオブジェクトは、それぞれの位置・回転・拡大縮小を反映して描画されます。非表示に設定されたオブジェクトは描画されません。OBJ の UV 座標を使った透視補正付きのピクセル単位テクスチャ描画に対応しました。現時点では ASCII 形式の P3 PPM テクスチャを読み込み、CPU 側の Z バッファと既存の頂点ライティングを維持します。MTL マテリアル解析や PNG/JPEG/BMP のデコードは未実装です。カメラの近接平面を横切る三角形は、投影の前にカメラ座標系でクリッピングされます。

## 衝突判定（試作）

衝突判定モジュールは、静的な軸平行境界ボックス（AABB）に対応しています。カメラの移動を X・Y・Z 軸ごとに解決し、各軸の移動量を最初の接触位置で止めます。移動量が大きく、障害物を完全に通り抜ける位置まで進もうとした場合も、手前で停止します。衝突していない軸の移動は続けられるため、壁に沿った移動も可能です。回転したメッシュとの衝突、斜面、階段昇降、カプセル形状を使った本格的なキャラクターコントローラーは未実装です。

## 必要なもの

- CMake 3.24 以降
- C++20 対応コンパイラ
- Git と初回構成時のインターネット接続（CMake が SDL3 をダウンロードします）

## GPU バックエンドのスモークテスト（実験段階）

`--gpu-smoke-test` を指定すると、通常の `SDL_Renderer` を作成せずに SDL GPU デバイスを作成し、ウィンドウを GPU に割り当て、スワップチェーンのテクスチャを取得して単色クリアした後、GPU コマンドバッファを送信します。これはウィンドウとスワップチェーンの基本的な表示経路を確認するテストです。HLSL シェーダーの読み込みや 3D メッシュの描画は行いません。

Windows でビルドした後、次のコマンドで実行できます。

```powershell
.\\build\\Release\\originalengine_app.exe --gpu-smoke-test
```

成功・失敗は起動診断ログに記録されます。現時点では CI のテスト対象ではなく、実機 iPhone / iPad での動作も未確認です。

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

1. アプリケーションとビルド環境の基礎
2. 3D 透視投影とカラーキューブ
3. 一人称カメラと移動操作
4. SDL GPU API による GPU 描画（CPU 描画へのフォールバックを維持）
5. シーンオブジェクトと読み込んだメッシュの描画
6. テクスチャ / マテリアル、音声、物理演算
7. 対応プラットフォームの拡張
