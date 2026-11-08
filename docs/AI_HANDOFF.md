# GasolineEngine — AI開発引き継ぎ資料

> この文書は、別のAI・開発者が会話履歴なしで作業を再開できるようにするための、リポジトリ基準の引き継ぎ資料です。
> **最初に必ずGitHubの対象ブランチと実ファイルを再確認してください。** この文書は調査時点のスナップショットであり、コードより優先されません。

- Repository: https://github.com/shton13275-2/GasolineEngine
- 開発対象ブランチ: `ios/initial-support`
- GPUテクスチャ/ライティング実装の開始時に確認したHEAD: `4afbfa25df8aac349285beef6b41bc980a8fa10d`（作業ブランチは常にGitHubで再確認すること）
- デフォルトブランチ: `main`（iOS作業ブランチとは別系統）
- 言語: C++20
- ウィンドウ・入力・GPU API: SDL 3.2.10（CMake FetchContent、`release-3.2.10`）
- ビルド: CMake 3.24+ / GitHub Actions
- ユーザーの主な実機: iPhone。Macを常用できず、GitHub ActionsのmacOS runnerでiOSビルドする。未署名IPAをLiveContainerで試す運用。
- 重要: ユーザーは「確認済みの事実だけ」を希望。推測を事実のように書かない。絵文字は使わない。会話は堅くしすぎず簡潔に。

## 目次

1. プロジェクトの目的
2. ブランチ・作業対象の注意
3. 現在実装されている機能
4. GPUバックエンドの正確な状態
5. iOS対応・ビルド・配布
6. ディレクトリと主要ファイル
7. 操作方法
8. 未実装・既知の制限
9. ロードマップと優先順位
10. 開発・検証ルール
11. AIエージェント向け作業手順
12. 完了報告の形式
13. 参考リンク

---

## 1. プロジェクトの目的

GasolineEngineは、C++20とSDL3を使ったクロスプラットフォーム3Dゲームエンジンの試作プロジェクト。

長期目標:
- iOSとWindowsを含む複数プラットフォームで動く3Dエンジンにする。
- iPhone上で操作できるデモを維持する。
- 現在のCPUソフトウェアレンダラーを維持しつつ、SDL_GPUを使う本格的なGPUレンダラーを実装する。
- GPU/CPU描画を実行時に切り替えられるようにする。
- 最終的にはOBJメッシュ、深度、テクスチャ、シーンオブジェクトをGPUで描画する。
- パフォーマンス、操作性、安定性を段階的に改善する。

重要な現状認識:
- CPUソフトウェアレンダラーは既に多くの機能を持つ。
- GPU経路は試験的な段階だが、独立したGpuRendererでOBJメッシュ、複数SceneObject、深度テスト、タッチオーバーレイをSDL_GPU経由で描画する。
- 現在のGPU経路はP3 PPMテクスチャ、Cook-Torrance metallic-roughness PBR（GGX / Smith-Schlick / Schlick Fresnel）、1024×1024方向シャドウマップ（3×3 PCF）、camera-depth prepassによるdepth-neighborhood SSAO、床専用の実験的SSR、対応時のR16G16B16A16_FLOAT HDR scene targetとACES fitted tone mapping、9-tap bloomを実装している。R32_FLOAT非対応時の深度fallbackは24-bit RGB packingを使う。iOS CIでは埋め込みMSLをMetal compilerでコンパイルするが、実機Metal pipeline/runtimeと描画結果は未確認。以前の実機スクリーンショットではSSR artifactが残っており、最新のdepth packing修正で解消したとはまだ確認できない。
- 「GPU描画対応済み」「Metalシェーダーが実際にロードされる」といった表現は、コードと実機で確認できるまでは使わない。

## 2. ブランチ・作業対象の注意

GitHubには少なくとも次のブランチが存在する。

| ブランチ | 役割 |
|---|---|
| `main` | デフォルトブランチ。CPUレンダラー中心の基礎版 |
| `ios/initial-support` | iOS対応、未署名IPA、タッチ操作、GPU実験を進めている開発対象 |

**通常の継続作業は `ios/initial-support` を対象にする。** `main` に誤ってコミットしないこと。作業開始時に必ずブランチHEADを読み直す。

調査時点で確認した差:
- `main` のREADMEはWindows中心の初期CPUレンダラー説明。
- `ios/initial-support` のREADMEにはiOS Device unsigned IPA、タッチ操作、GPUスモークテストの説明がある。
- 両ブランチのコードとREADMEを混ぜないこと。

ブランチHEADがこの文書記載のSHAと違う場合は、新しいHEADを正としてファイルを再確認する。

## 3. 現在実装されている機能

以下は `ios/initial-support` のREADMEおよびソースを調査して確認した内容。

### エンジン基盤
- CMakeベースのC++20プロジェクト。
- SDL3によるウィンドウ、イベント、入力、描画表示。
- 一人称カメラ。
- カメラ移動と視点操作。
- デバッグログ。iOSではSDLのPreference Path配下に `startup.log` を作る実装。
- 起動時のエラーをログに書き、メッセージボックスでも通知する実装。

### カメラ・操作
- Windows/PC: WASD移動、Q/E上下移動、Shift高速移動。
- 右クリックでマウス捕捉、マウス移動で視点変更。
- Escでマウス捕捉解除、解除済みなら終了。
- Rでカメラリセット。
- Spaceでデモアニメーション一時停止/再開。
- F3でワイヤーフレーム切替。
- F11でウィンドウ/フルスクリーン切替。
- 1/2でデモ用ピラミッドの表示切替。
- -/+でマウス感度調整。
- iOS: 左半分のドラッグ領域で移動、右半分のドラッグ領域で視点操作。両方同時操作できる設計。
- CPU/GPU切替ボタンのタッチ判定は、CPU/GPU両方の描画に使う共通ボタン矩形から計算する。
- iOS上のGPU/CPU切替ボタンはGPU/CPUバックエンドの切替要求に結び付けられている。タッチ判定は `touch_overlay::containsBackendButton` で、CPU/GPU両方の描画に使う共通ボタン矩形と一致させる。

### シーン・メッシュ
- 名前が一意なシーンオブジェクト登録。
- Transform（位置・回転・スケール）。
- 表示/非表示、検索、削除。
- Wavefront OBJローダー。
- OBJの位置 `v`、UV `vt`、法線 `vn`、面 `f` の読み込み。
- 多角形面の三角形分割。
- 正/負インデックス。
- 同じ位置でもUV/法線インデックスが異なる場合の頂点分離。
- 読み込み時のインデックス検証。失敗時に呼び出し元の既存メッシュを破壊しない設計。
- `assets/demo.obj` のピラミッドを読み込み、2つのシーンオブジェクトで共有するデモ。

### CPUソフトウェアレンダラー
- 透視投影。
- キューブの3軸回転デモ。
- 複数OBJインスタンス描画。
- カメラ近接平面を横切るポリゴンのクリッピング。
- CPU側Zバッファによる塗りつぶし三角形のピクセル単位深度判定。
- フロアグリッドの描画と深度判定。
- 透視補正付きUVテクスチャマッピング。
- ASCII P3 PPMとBMPテクスチャの読み込み。BMPは `SDL_LoadBMP` / `SDL_ConvertSurface` を使うCPU側ローダーで確認済み。
- ソフトウェア描画サイズに上限を設け、SDLでウィンドウサイズに拡大表示する。
- READMEには最大800×450（360,000ピクセル）と記載。定数変更時はREADMEも更新。
- デバッグHUDにFPS推定値、カメラ座標、操作ヒントを表示。
- CPUワイヤーフレームは `SDL_RenderLine` で描画され、深度バッファによる遮蔽判定は行わない。塗りつぶし三角形とフロアグリッド線はCPU側Zバッファを使用する。

### 衝突判定
- 静的AABB（軸平行境界ボックス）。
- X/Y/Z軸ごとに移動を解決し、各軸の移動量を最初のAABB接触位置で止める。
- 移動量が大きく障害物を完全に通り抜ける場合も手前で停止し、衝突していない軸では壁沿いの移動を許可する。
- 回転メッシュ、斜面、階段、カプセルコントローラーは未実装。

## 4. GPUバックエンドの正確な状態

### 現在の実装
- GPU描画処理は `include/engine/GpuRenderer.hpp` / `src/engine/GpuRenderer.cpp` に分離されている。
- SDL_GPU device、window claim、MSL shader、graphics pipeline、vertex/index buffer、depth texture、color texture、samplerの生成と解放をGpuRendererが管理する。
- OBJ Meshのposition、normal、color、UVをGPU頂点形式へ変換し、SDL_GPU transfer buffer経由でアップロードする。
- カメラのview/projectionと各SceneObjectのmodel transformを使い、複数オブジェクトをindexed drawする。
- Depth-Stencil Attachmentを作成し、depth test/writeを有効化。画面サイズ変更時はdepth textureを再作成する。
- P3 ASCII PPMをRGBA8へ変換するPpmImageLoaderを追加。GPU側でSDL_GPUTextureへ転送し、fragment sampler slot 0へbindする。
- OBJのUVはGPUへ渡し、画像の行方向に合わせてVを反転する。UVの有無は専用の頂点属性で管理し、負のUV値や0〜1範囲外の有効な座標を欠落扱いしない。UVがない頂点のみテクスチャなしfallbackにする。
- OBJにvnがある場合はその法線を使用し、ない場合は三角形ごとのflat normalを生成する。
- per-object uniformにMVP、model、inverse-transpose normal matrix、camera position、material parameters、shadow transform、depth-format flagを渡す。fragment shaderはGGX distribution、Smith-Schlick geometry、Schlick FresnelによるCook-Torrance metallic-roughness PBRを方向光と減衰付きポイントライトに適用し、1024×1024 directional shadow mapを3×3 PCFで参照する。回転キューブはUVなしのemissive cyan materialを使う。
- 単一サンプルcamera-depth prepassを行い、fullscreen compositeで8近傍depth SSAOと床専用の実験的SSRを計算する。SSRは64ステップのscreen-space ray marchと4段階のbinary refinementを使用するが、screen-edge/disocclusion制限があり、以前の実機スクリーンショットでは反射の繰り返し・櫛状artifactが確認されている。最新の24-bit depth fallback修正を含むIPAでの再確認は未実施。
- scene colorは対応時にR16G16B16A16_FLOATのsampleable HDR targetを使い、final compositeでACES fitted tone mappingを適用する。4x MSAAはscene color/depth両formatが対応する場合だけ使い、非対応時はsingle-sampleへfallbackする。compositeは反射・SSAO後に9-tap bloomを適用し、single-sample overlay pipelineでUIを描画する。
- camera-depth prepassとdirectional shadow mapはR32_FLOATを優先し、非対応時はR8G8B8A8_UNORMへnormalized depthを24-bit RGBでpack/unpackする。iOS CIではGpuRenderer.cppから8つの埋め込みMSL文字列を抽出し、Xcode Metal CLIでコンパイル・library linkする。この検証はMSL source compilationを確認するもので、runtime pipeline作成や実機描画を保証しない。

### Metal shaderの扱い
- 現在実行時に使うmesh shaderは `src/engine/GpuRenderer.cpp` 内のMSL文字列。
- `shaders/Metal/Basic3D.*.metal` はbundleへ同梱されるが、現行GpuRendererの実行時shader sourceではない。
- SDL 3.2.10のshader resource binding規則に合わせ、fragment texture/sampler slot 0を使用する。

### 未実装・未確認
- MTL material parsing、materialごとの複数texture。
- PNG/JPEG/BMPのGPU texture loader（現GPU loaderはP3 PPMのみ）。
- mipmapは対応形式で生成し、非対応時は単一レベルへfallbackする。anisotropic filteringとnormal mapは未実装。
- CPU/GPU共通Renderer API。GPU wireframeはSDL_GPUのline-fill pipelineで実装済み。
- iPhone実機でのMetal shader compile、texture sampling、lightingの見た目、タッチ操作。
- GPU/CPU切替を繰り返した場合の長時間安定性・resource leak。
- GPU側のFPS/frame-time計測の精度検証。

## 5. iOS対応・ビルド・配布

### iOSビルド
- GitHub ActionsのmacOS runnerでXcode/CMakeを使用。
- iOS Device arm64。
- Deployment Target: iOS 15.0。
- コード署名なしで.appをビルドし、`Payload/` に入れてunsigned IPAを作成。
- IPAをzipし、ZIP integrityとInfo.plist、実行ファイルの存在を検証。
- 署名なしIPAは通常のApp Store配布用ではない。ユーザーはLiveContainerでテストする運用。
- LiveContainerで起動できた過去実績があるが、毎回の新しいIPAで動作する保証はない。新ビルドごとに実機テストする。

### iOS CMake
- `CMAKE_SYSTEM_NAME=iOS`
- `CMAKE_OSX_SYSROOT=iphoneos`
- `CMAKE_OSX_ARCHITECTURES=arm64`
- `CMAKE_OSX_DEPLOYMENT_TARGET=15.0`
- iOSではSDL_SHARED=OFF、SDL_STATIC=ON。
- `SDL3::SDL3-static` にリンク。
- `ORIGINALENGINE_BUILD_APP=OFF` はサンプル実行ファイルだけを無効にする。エンジンライブラリの `Renderer3D` / `GpuRenderer` がSDL3 APIを使うため、SDL3 FetchContentとリンクはライブラリ単体ビルドでも必要。`ORIGINALENGINE_USE_SDL3=OFF` は現在サポートされず、CMake構成時に明示的なエラーにする。
- App Bundle identifier: `com.gasolineengine.demo`
- Xcodeの`PRODUCT_BUNDLE_IDENTIFIER`も同じ値に設定し、CIでbuild settingとIPA内`Info.plist`の両方を検証する。
- Bundle name: `Gasoline Engine`
- `SDL_main.h` をmain.cppでincludeすること。過去にSDL_Initのmain entrypoint関連エラーが発生した。
- iOS向けにOBJ/PPM資産とMetal shader sourceをResourcesに配置する設定がある。

### Workflow
- `.github/workflows/build.yml`
- Windows/MSVC job:
  - Configure CMake
  - Build Release
  - Configure/build a second Windows directory with `ORIGINALENGINE_BUILD_APP=OFF` and `BUILD_TESTING=OFF`, reusing the already fetched SDL3 source to verify the engine-library-only configuration.
  - `--smoke-test`
  - CTest
  - Windows artifact upload
- iOS unsigned IPA job:
  - macOS runner
  - Xcode generatorでiOS Device arm64 build
  - `scripts/extract_msl_shaders.py` でGpuRenderer.cpp内の8つのMSL文字列を抽出し、`xcrun -sdk iphoneos metal`でAIRへコンパイル、Metal libraryへリンク
  - unsigned .appをIPA化
  - IPA integrity / bundle contents verify
  - artifact upload
- 実際のworkflow runとjob結果は作業のたびにGitHub Actionsで確認する。古い成功runを最新変更の成功として扱わない。iOS jobのMSL compile stepは埋め込みshaderのsource compilationを検証するが、Metal runtime pipelineやiPhoneの見た目を検証するものではない。
- `ios/initial-support` のworkflowにあるDiscord通知jobは `github.event_name == 'push'` かつ `github.ref == 'refs/heads/ios/initial-support'` の場合だけ実行する。Pull Requestと手動実行では送信しない。他ブランチの通知動作は各ブランチのworkflow定義に従う。
- 同jobはWindowsとiOSの両jobに依存し、両方の完了後に実行する。`always()` 条件により、片方または両方のビルドが失敗した場合も各jobの結果を通知する。
- 確認時点で `main` のworkflowにはDiscord通知jobがなく、`docs/sync-to-main` のworkflowには別の `discord-notify` job（`if: always()`）がある。ブランチ間でworkflow定義は独立しているため、通知動作を全ブランチ共通として記述しない。
- Discord通知の詳細欄はWindows/iOSのjobだけを含め、Discord通知job自身は除外する。詳細文字列は1,000文字以下に制限し、Embedの各フィールド・説明・総文字数を送信前に検証する。DiscordへのHTTPエラー時はレスポンス本文をActionsログへ出す。Discord通知jobが失敗すると、Windows/iOSのビルドが成功していてもworkflow全体の結論はfailureになるため、個別jobの結果を分けて確認する。

## 6. ディレクトリと主要ファイル

| Path | 役割 |
|---|---|
| `CMakeLists.txt` | C++20、SDL3 FetchContent、Windows/iOSターゲット、テスト |
| `src/main.cpp` | デモアプリ、入力、iOSタッチ、GPU/CPU切替、CPU fallback |
| `include/engine/Camera.hpp` / `src/engine/Camera.cpp` | カメラ |
| `include/engine/CollisionWorld.hpp` / `src/engine/CollisionWorld.cpp` | AABB衝突 |
| `include/engine/Scene.hpp` / `src/engine/Scene.cpp` | シーンとTransform |
| `include/engine/Mesh.hpp` | MeshVertex/MeshTriangle/Mesh |
| `include/engine/ObjMeshLoader.hpp` / `src/engine/ObjMeshLoader.cpp` | OBJロード |
| `include/engine/Renderer3D.hpp` / `src/engine/Renderer3D.cpp` | CPUソフトウェアレンダラー |
| `include/engine/TouchOverlay.hpp` | CPU/GPU共通のオーバーレイ配置・配色・bitmap font定義 |
| `include/engine/GpuDeviceProbe.hpp` / `src/engine/GpuDeviceProbe.cpp` | SDL_GPU利用可否のProbe |
| `include/engine/GpuFrameRecovery.hpp` | GPUフレーム失敗後のCPU復帰制御フロー |
| `include/engine/GpuGeometryRules.hpp` | GPU三角形のUV有無判定ルール |
| `include/engine/GpuRenderer.hpp` / `src/engine/GpuRenderer.cpp` | SDL_GPU indexed mesh、depth、PPM texture、point lighting、4x MSAA fallback、screen-space bloom、overlay |
| `shaders/Metal/` | iOS向けMSL shader source（まだmain.cppのGPU previewからは未使用） |
| `shaders/Source/` | HLSL shader source |
| `assets/demo.obj` | デモ用ピラミッド |
| `assets/demo_texture.ppm` | デモ用テクスチャ |
| `tests/` | 12 CTest targets: カメラ、衝突、シーン、shared scene render items、OBJ、PPM、Renderer、GPU復旧、GPUジオメトリ、タッチオーバーレイ、backend switch input、primitive meshes |
| `.github/workflows/build.yml` | Windows build/testとiOS unsigned IPA |

## 7. 操作方法

### Windows / PC
| 入力 | 操作 |
|---|---|
| W/S | 前進/後退 |
| A/D | 左右移動 |
| Q/E | 下/上 |
| Shift | 高速移動 |
| 右クリック | マウス捕捉切替 |
| マウス移動 | 視点操作（捕捉中） |
| Esc | 捕捉解除 / 終了 |
| R | カメラリセット |
| Space | デモアニメーション停止/再開 |
| F3 | ワイヤーフレーム切替 |
| F11 | フルスクリーン切替 |
| 1/2 | ピラミッド表示切替 |
| -/+ | マウス感度 |

### iOS
- 画面左半分のドラッグ: 仮想ジョイスティックによる移動。
- 画面右半分のドラッグ: 視点操作。
- 左右を同時に操作可能な設計。
- 画面上のGPU/CPU切替ボタン: 描画バックエンド切替。GPU側はOBJメッシュ/PPMテクスチャを描画する試験的な経路。実機動作は未確認。

## 8. 未実装・既知の制限

- GPU rendererはCPU rendererと同じ機能を持たない。
- GPU mesh描画はOBJとプロシージャルメッシュを複数mesh rangeとして1組のvertex/index bufferへ格納し、SceneObjectごとにmesh rangeを選択して描画する。GPU wireframeは実装済み。GPU floor gridや全CPU描画機能の移植は未完了。
- GPUの実行時mesh shaderはGpuRenderer.cpp内のMSL文字列。shaders/Metal内の別ファイルは実行時ロードしていない。
- GPU texture samplingはP3 ASCII PPMに対応。現在はアップロード済みmeshで1つのP3テクスチャを共有する。PNG/JPEG/BMPのGPU upload、MTL material parser、materialごとの複数textureは未実装。
- mipmap生成は対応形式で実装済み。normal mapとanisotropic filteringは未実装。
- iOSの物理端末GPU/Metal検証はCIだけではできない。
- AABB以外の本格的な物理・衝突は未実装。
- iOSの物理端末GPU検証はCIだけではできない。
- 未署名IPAは端末への通常インストールを保証しない。現在のユーザー環境で使えるLiveContainer等の方法をユーザーの実績に沿って確認する。

## 9. ロードマップと優先順位

### P0 — 既存機能とCIを壊さない
1. 作業開始時にブランチ/HEAD/差分を確認。
2. Windows jobとiOS unsigned IPA jobの両方を成功させる。
3. CPU rendererへのフォールバックを維持。
4. GPUフレーム復旧のコールバック順序テストは `gpu_frame_recovery` で実施済み。実際のSDL_GPU障害注入、window claimの解放/再取得、実機Metal復旧は未検証。

### P1 — GPU texture/lightingの実機検証
1. 最新iOS unsigned IPAを作成し、iPhone/LiveContainerで起動確認。
2. Metal shader compile、PPM texture sampling、UV方向、ambient/diffuse/specularの表示を確認。
3. GPU/CPU切替とアプリ終了を繰り返し、resource lifecycleを確認。
4. 非均一スケールとOBJ vnの有無で法線表示を確認。

### P2 — GPU materialと品質向上
1. MTLのKd/Ks/Nsとmaterialごとのtexture参照を追加。
2. PNG/JPEG等の画像形式を追加（既存依存関係とiOS bundleを確認して選定）。
3. mipmap生成とanisotropic filteringを追加。
4. normal mapとanisotropic filteringを検討。
5. CPU/GPU共通Renderer APIと描画品質の比較を進める。

### P3 — 安定性・性能
1. GPU resourceの失敗経路と解放をテスト。
2. GPU/CPU切替の長時間テスト。
3. FPS/frame-timeの計測を整備。
4. 実機で負荷・発熱・メモリ使用量を測定。


## 10. 開発・検証ルール

ユーザーが特に重視するルール:

1. **事実確認を優先。** ソースで確認できないことを実装済みと書かない。
2. **コードを読んでから編集。** 既存コードを推測で置き換えない。
3. **小さい差分。** 一度に大量変更せず、ビルド/テストしやすい単位で進める。
4. **CPU rendererを壊さない。** GPUは別経路で追加し、失敗時はCPUに戻せるようにする。
5. **iOSを最優先の実機対象にする。** Windows CIも維持する。
6. **未署名IPAを直接生成する。** ZIPだけを成果物として渡すのではなく、artifact内に.ipaを生成する。
7. **CI成功と実機成功を区別。** CI成功はコンパイル/パッケージ確認であり、iPhoneのMetal描画成功ではない。
8. **コミットの有無を正直に報告。** commit SHAとリンクを確認できた場合のみ「コミット済み」と言う。
9. **ビルド結果を実際に確認。** 最新commit SHAとworkflow runのhead_shaが一致しているか確認。
10. **失敗を隠さない。** workflow failure時は失敗job/stepを特定し、原因が分からなければ未特定とする。
11. **ユーザーに不要な確認を何度も求めない。** リポジトリ情報から分かることは自分で調べる。
12. **バックアップと既存動作を守る。** 大きな変更は別branch/commitに分ける。
13. **ドキュメントも更新。** 実装内容・操作・制限が変わったらREADME/この資料を更新。
14. **絵文字なし。** 会話は自然な日本語で、過度に堅い表現にしない。
15. **引き継ぎ資料を継続更新。** 機能・設計・ビルド設定・CI結果・既知の問題・次の作業が変わったら、作業と同じコミット、または直後のコミットで `docs/AI_HANDOFF.md` を更新する。未確認事項は未確認と明記し、古い状態・完了済みタスク・古いHEAD情報を放置しない。

### 変更前チェック
- `git branch --show-current` 相当のGitHub branch確認
- 最新HEAD SHA確認
- 対象ファイルの現在内容を取得
- 関連APIは実際に使っているSDL 3.2.10のヘッダーで確認
- 既存のCPU経路、iOS bundle、Windows CIへの影響を検討

### 変更後チェック
- CMake configure/build
- CTest
- Windows smoke test
- iOS unsigned IPA build/package/verify
- GitHub Actions runのcommit SHA確認
- 可能ならiPhone/LiveContainerで実機テスト
- READMEとこの資料を更新

## 11. AIエージェント向け作業手順

新しいAIは、以下を順番に行うこと。

### Step 1: 状態確認
- リポジトリ `shton13275-2/GasolineEngine` を開く。
- `ios/initial-support` をcheckout対象にする。
- branch HEADを取得。
- `git status` 相当で未コミット変更を確認。
- この文書とREADMEを読む。
- `src/main.cpp`、`Renderer3D.cpp`、`GpuDeviceProbe.cpp`、`GpuRenderer.cpp`、CMake、workflowを読む。
- ここに書かれた説明と現行コードが異なる場合、コードを正とし、この文書を修正する。

### Step 2: 既存テストと直近CI
- 最新workflow runを確認。
- 最新runのhead_shaとbranch HEADが一致するか確認。
- Windows/iOS各jobの結果を確認。
- 失敗があれば実装に入る前に原因を特定する。

### Step 3: 次の作業を一つ選ぶ
GPU Rendererの分離、indexed mesh描画、depth、PPM texture、lighting、overlay、backend-switch input reset、GPU frame-recovery control flowは実装済みで、関連するunit testsも存在する。残るP0は、実際のSDL_GPU failure injection、window-claimのrelease/reclaim、resource lifecycleの検証。P1ではiPhone実機でMetal shader、texture sampling、point lighting、bloom、touch操作を確認する。実機確認はCIの成功で代替できない。

### Step 4: 実装
- SDL 3.2.10の実際のAPIヘッダーを確認してから使う。
- MSLを使う場合はSDL_GPUが期待する形式、entrypoint、shader stage、resource bindingを確認。
- shader source fileがbundleにあるだけで実行時ロードできたと判断しない。
- まず最小三角形を実機で確認し、次にOBJへ進む。
- GPU resourceは所有者を明確にし、失敗時もすべて解放する。
- Window Claim中にSDL_Rendererを使わない。切替時のrenderer/device/window所有権を明確にする。
- GPUが利用できない場合CPUへ戻る経路を維持する。

### Step 5: コミット・検証
- 小さい変更単位でcommit。
- workflow runが新commitに紐づいているか確認。
- CIが成功するまで「完成」と言わない。
- 実機で未確認ならその旨を明記する。
- 完了報告にcommit SHA、CI URL、テスト結果、実機確認の有無を記載。

### Step 6: ユーザーへの報告
日本語で短く、以下を明確に:
- 何を変更したか
- commit済みか（SHA付き）
- CIの状態（成功/失敗/実行中）
- iPhone実機で確認したか
- 次の課題

## 12. 完了報告の形式

例:

```text
変更:
- GPU rendererを独立クラスへ分離
- CPU fallbackを維持

Commit:
- <SHA>
- <GitHub commit URL>

CI:
- Windows: success
- iOS unsigned IPA: success
- Artifact: <workflow URL>

実機:
- iPhone / LiveContainer: 未確認（または確認済みの具体的結果）

次:
- OBJ vertex/index buffer upload
```

「CI成功」「IPA生成成功」「iPhoneで起動成功」「GPUでOBJ描画成功」は別々の判定項目である。まとめて一つの「成功」としない。

## 13. 参考リンク

- Repository: https://github.com/shton13275-2/GasolineEngine
- iOS branch: https://github.com/shton13275-2/GasolineEngine/tree/ios/initial-support
- Actions: https://github.com/shton13275-2/GasolineEngine/actions
- SDL 3.2.10 GPU API header: https://github.com/libsdl-org/SDL/blob/release-3.2.10/include/SDL3/SDL_gpu.h
- SDL_shadercross: https://github.com/libsdl-org/SDL_shadercross
- SDL GPU examples: https://github.com/TheSpydog/SDL_gpu_examples
- GitHub README guidance: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes

---

## 追記履歴

### 2026-10-31 — CPU/GPUオーバーレイ共通化
- `include/engine/TouchOverlay.hpp` に画面比率、サイズ、色、3x5 bitmap fontを集約。CPU/GPU両方が同じ定義を使用。
- CPU側のFPS/バックエンドラベルも独自bitmap glyphで描画し、GPU側と文字形状・ピクセル寸法を統一。
- FPSラベルは両方とも `FPS:<値>`。GPU側のFPS背景カードをなくし、CPUと同じテキスト表示に変更。
- ジョイスティック/Look guideの中心位置・半径・ガイド範囲・色・ノブサイズを共通化。
- GPU側の円リングを約1pxの輪郭にし、CPU側の線描画に近づけた。GPU overlay pipelineにはSDL_GPUのstraight-alpha blend stateを設定。
- バックエンド切替ボタンの矩形サイズ・位置・文字位置を共通化。CPU側はGPU、GPU側はCPUを表示。
- CIでのビルドとiPhone実機での視覚的な一致は、コミット後に別途確認する。

- 2026-10-31: `ios/initial-support` のソース、README、CMake、workflow、GPU実装を確認して初版作成。GPU三角形プレビュー、iOS unsigned IPA workflow、CPU software renderer、iOSタッチ操作の状態を整理。
- 2026-10-31: iOS buildのSDL3 source cache、Xcode versionを含むcache key、`FETCHCONTENT_UPDATES_DISCONNECTED`、`.app`自動検出を追記。Windows/iOS両jobで`docs/`をsparse checkout対象外にした設定を追記。
- 2026-10-31: Actionsの失敗runを調査。Xcode version出力が複数行のため`$GITHUB_OUTPUT`で不正形式になっていた問題、およびCMake経由の`xcodebuild`に`-derivedDataPath`を渡してscheme要求エラーになっていた問題を確認。workflowを修正し、Xcode versionを1行化、DerivedData cache/引数を一旦除去してSDL3 source cacheのみ残した。修正後CIの結果は未確認。


## GPU renderer extraction — 2026-10-31

- Added `include/engine/GpuRenderer.hpp` and `src/engine/GpuRenderer.cpp`.
- Moved SDL_GPU device creation, window claim, MSL shader creation, graphics pipeline ownership, swapchain frame submission, and teardown out of `src/main.cpp`.
- The application now owns a `GpuRenderer` object and uses its `initialize()`, `renderFrame()`, and RAII destructor for backend switching.
- CPU renderer recreation on GPU initialization failure is retained.
- The current GPU output is **still the original hard-coded triangle preview**. This commit is an architecture/refactoring milestone, not yet OBJ/scene GPU rendering.
- The GPU backend currently requests MSL because its embedded shaders are MSL. Cross-platform shader compilation/format selection remains unresolved; validate Windows GPU activation separately.
- CI must verify this refactor before further GPU feature work. No physical iPhone Metal test is implied.


## GPU indexed mesh rendering — 2026-10-31

- Replaced the hard-coded GPU triangle preview with an SDL_GPU indexed-mesh path in `GpuRenderer`.
- The renderer now converts the existing `Mesh` triangles into GPU vertex data (position, generated flat face normal, and face color), uploads vertex/index buffers through SDL_GPU transfer buffers, and reuses those buffers for each frame.
- Added column-major model, view, and perspective projection matrices. Camera position/yaw/pitch and each visible `SceneObject` transform are used to produce per-object MVP uniforms.
- The app passes the already-loaded `assets/demo.obj` mesh and both existing pyramid scene objects into the GPU backend. CPU rendering and the CPU fallback remain separate.
- Added a depth-stencil attachment with runtime format selection between supported D32_FLOAT and D24_UNORM_S8_UINT formats, plus resize-aware depth texture recreation.
- GPU shading currently uses generated flat normals and simple directional diffuse lighting. GPU texture sampling, material/MTL support, GPU wireframe toggle, and a shared CPU/GPU renderer interface remain unimplemented.
- The current GPU path requests MSL and is intended for Metal-capable devices; the Windows CI job validates compilation but does not prove Windows GPU runtime activation.
- CI for the preceding backend extraction (commit `3d242d2cfe177496bbb0da89b575aac31863aafb`) has Windows success; its iOS job was still running at the time this implementation began. This new mesh-rendering commit requires its own Windows/iOS CI run and a physical iPhone Metal test.


## GPU overlay UI — 2026-10-31

- GPU mode now draws the on-screen touch guide and backend-switch button inside the SDL_GPU render pass instead of relying on SDL_Renderer.
- The overlay includes the movement joystick rings, active movement knob, look-control ring, and a CPU-switch button label rendered with a tiny built-in bitmap glyph set.
- The overlay pipeline uses screen-space coordinates and does not depth-test against the 3D scene.
- CPU mode retains its existing SDL_Renderer overlay path.
- The GPU renderer receives touch-control visibility and current movement pointer state from the main loop.
- CI compilation is required; actual Metal rendering and touch interaction still need physical iPhone verification.


## FPS and touch-control visual refresh — 2026-10-31

- Main loop calculates a smoothed FPS value once per frame and passes it to both render backends.
- CPU mode displays FPS using SDL_RenderDebugText.
- GPU mode displays FPS through a screen-space bitmap glyph renderer without SDL_Renderer.
- CPU and GPU touch controls share a refreshed layout: concentric movement rings, crosshair guides, and a responsive center knob; the look-control guide uses the same visual language.
- GPU backend-switch control is labeled CPU.
- CI validates compilation and existing smoke/unit tests; visual appearance and touch interaction still require device/runtime verification.


## GPU texture and lighting — 2026-10-31

- Added PpmImageLoader for the repository's existing ASCII P3 PPM assets. It validates dimensions, maximum channel value, pixel count, and channel ranges; failed loads preserve the previous output image.
- GPU vertex data now includes UV coordinates. OBJ UVs are passed to the GPU, with V flipped to match the top-left row order of the PPM image. Vertices without UVs use a sentinel and retain a white untextured fallback.
- GpuRenderer::initialize now receives the decoded RGBA image, creates an SDL_GPU sampler texture, uploads its pixels with SDL_UploadToGPUTexture, and binds the texture/sampler to fragment slot 0.
- The embedded Metal fragment shader samples the texture and applies ambient, directional diffuse, and Blinn-Phong specular lighting.
- OBJ-provided vertex normals are used when present; missing normals continue to use generated flat face normals.
- Per-object constants now include model, MVP, inverse-transpose normal matrix, and camera position. The normal matrix handles non-uniform scale and falls back to identity for singular transforms to avoid NaNs.
- The sample PPM loader supports P3 ASCII only. PNG/JPEG/BMP loading and MTL material parsing are not included in this step.
- Added PPM loader unit tests. CI and physical iPhone Metal runtime still need to validate this change; a successful compile does not prove texture sampling on device.


## Texture filtering and overlay glyph fix — 2026-11-01

- The GPU demo texture asset was only 8x8 pixels. Replaced it with a 128x128 P3 checker texture, preserving the two-color checker design while adding per-tile bevel shading and subtle surface variation.
- GPU texture creation now requests a complete mip chain when the device supports RGBA8 sampler + color-target usage. Mipmaps are generated after the base-level upload and sampled with linear mipmap filtering. Devices without that format capability safely retain the single-level texture path.
- Increased the sampler max LOD to the final allocated mip level instead of forcing level 0.
- Added the missing uppercase G glyph to the shared 3x5 bitmap font. This fixes the CPU-renderer button label GPU being rendered without its first character; both CPU and GPU overlay paths use the same glyph table.
- CI must verify the code and asset packaging. Actual visual sharpness and button appearance still require iPhone runtime confirmation.

## Nightly verification automation — 2026-11-01

- Added `.github/workflows/nightly-verification.yml` on the default `main` branch because GitHub scheduled workflows are only triggered from the default branch.
- The scheduled workflow checks out `ios/initial-support`, then runs the Windows CMake configure/build, application smoke test, and CTest.
- Schedule is 18:17 UTC (03:17 JST). GitHub may delay scheduled runs during high load; this is not an exact start-time guarantee.
- The run summary is written to the GitHub Actions run summary. See `docs/NIGHTLY_AUTOMATION.md` for operating details.
- The repository is public and the workflow uses a standard GitHub-hosted runner. GitHub documents standard hosted runners as free for public repositories.
- This automation is deterministic CI, **not an AI coding agent**. It does not call an LLM/API, invent fixes, change source files, or semantically update documentation. Fixes and documentation changes remain part of the normal development process.
- The workflow has not yet been manually dispatched or verified by a completed run at the time of this entry. Do not report the schedule as tested until a run completes.


## AABB axis movement collision fix — 2026-11-01

- Updated `CollisionWorld::resolveAxis` to clamp axis movement to the nearest AABB contact boundary instead of checking only the final position.
- This prevents a large positive or negative displacement from crossing an entire obstacle without an endpoint overlap.
- Movement parallel to a wall remains available, and movement out of an existing overlap is not blocked.
- Added regression tests for first-contact stopping, positive/negative tunneling, escape from an existing overlap, wall sliding, and invalid axis input.
- Build and CI results for this change must be verified against the commit that contains it; this entry does not imply a passing build or physical-device test.


## Documentation synchronization — 2026-11-01

- Updated the root README to describe the current arm64 iOS Device unsigned IPA workflow rather than an iOS Simulator artifact.
- Corrected the GPU backend description to match the current indexed OBJ mesh, depth, P3 PPM texture, lighting, mipmap capability fallback, and GPU overlay implementation.
- Replaced the obsolete GPU integration plan with the current ownership, switching, failure, shader, and verification behavior.
- Updated the AI handoff's file map and next-work guidance so GPU renderer extraction and indexed mesh rendering are not listed as pending work.
- These are documentation-only changes. No source behavior, build configuration, or public API was changed. CI results for the documentation commit must be checked separately.


## GPU negative UV coordinate fix — 2026-11-01

- Fixed the GPU fragment shader's missing-UV detection. It previously treated any negative U or V value as a missing texture coordinate, even though negative/out-of-range OBJ UV coordinates are valid and the sampler uses repeat addressing.
- Added a dedicated per-vertex UV-presence attribute and use it to select the untextured fallback. UV numeric values are no longer overloaded as a presence sentinel.
- Extended the OBJ loader regression fixture to include negative U/V values and assert they are preserved.
- The CI build can compile the C++ integration and run the OBJ parser regression test. The repository CI does not compile the embedded MSL shader or verify Metal sampling on a physical iPhone; that runtime behavior remains to be checked on device.

## OBJ face-token validation fix — 2026-11-01

- Fixed `ObjMeshLoader` accepting malformed face-vertex tokens with a trailing slash and no texture-coordinate index (for example, `1/`).
- Added a regression test confirming that such input is rejected and that a previously loaded output mesh remains unchanged.
- Updated the root README to document the stricter face-token validation.
- The source and test changes were reviewed, but no local compiler/test environment was run for this edit. The matching GitHub Actions run must be checked before treating the fix as build-verified.

## Metal fragment input interface fix — 2026-11-01

- Fixed a confirmed interface mismatch in the embedded Metal shaders: the vertex stage wrote the `hasTexCoord` varying, and the fragment stage read `input.hasTexCoord`, but the fragment input structure did not declare that member.
- Added `float hasTexCoord;` to the fragment-stage input structure so the varying is declared at both shader stages.
- Updated the root README to record the fix. Physical-device Metal shader compilation and rendering still require verification on an iPhone or iPad; the regular CI build does not establish that runtime result.


## GPU renderer robustness fixes — 2026-11-01

- Fixed mixed-UV triangles in `buildGpuGeometry`: UV presence is now consistent across all three duplicated vertices of a triangle. Texturing is enabled only when all three corners have UV coordinates; otherwise the whole triangle uses vertex color. This prevents the UV-presence varying from interpolating between textured and untextured states inside one primitive.
- Added `RgbaImage::hasValidRgba8Data()` to validate nonzero dimensions, exact tightly packed RGBA8 byte count, and the SDL_GPU transfer-buffer size limit before texture allocation/upload.
- Added regression checks for valid decoded RGBA8 data, a mismatched pixel byte count, and dimensions that exceed the supported upload size.
- GPU frame-render failure now destroys `GpuRenderer` (releasing its window claim) and attempts to recreate `SDL_Renderer`. If CPU renderer recreation also fails, the app exits with an error. This is recovery behavior in source code; runtime failure injection and device-loss recovery have not yet been tested.
- These source changes were made on `ios/initial-support`. Verify the matching GitHub Actions run before treating them as build-tested. Physical iPhone Metal shader compilation and visual rendering remain unverified.


## GPU renderer follow-up verification — 2026-11-01

- Extracted the all-three-corners-have-UV decision into `include/engine/GpuGeometryRules.hpp`; `buildGpuGeometry` uses this shared rule so the production path and unit test exercise the same predicate.
- Added the `gpu_geometry_rules` CTest target covering all eight combinations of UV presence across a triangle. This is a focused policy test; it does not instantiate SDL_GPU buffers or execute the Metal shader.
- The latest verified source baseline before this follow-up was `9431a9354adfb634abcd30c996250aab908517e2`. GitHub Actions run [41300623759](https://github.com/shton13275-2/GasolineEngine/actions/runs/41300623759) completed successfully: Windows configure/build, one-frame CPU smoke test, all 6 existing CTest tests, and iOS arm64 unsigned IPA build/package/verification. The run's head SHA matched the baseline.
- The documentation-only merge into `main` also passed its Windows workflow [41301235618](https://github.com/shton13275-2/GasolineEngine/actions/runs/41301235618). That run does not verify the GPU source branch.
- GPU frame-failure fallback remains source-reviewed but has no failure-injection test. SDL_GPU mesh rendering, embedded MSL compilation, Metal texture sampling, and visual output remain unverified on a physical iPhone/iPad.
- After this follow-up commit, re-check the new commit's own Windows and iOS workflow results before describing the new test as CI-passing.


## GPU frame recovery regression test — 2026-11-01

- Added `include/engine/GpuFrameRecovery.hpp` with a small callback-driven recovery helper shared by the production main loop and the unit test.
- The helper returns `GpuRendered` without releasing resources after a successful GPU frame.
- After a failed GPU frame, it invokes GPU release before CPU renderer creation and returns `CpuFallbackReady` or `Fatal` depending on CPU renderer recreation.
- Replaced the inline GPU-frame recovery branch in `src/main.cpp` with the shared helper while keeping SDL resource ownership in the application callbacks.
- Added `tests/gpu_frame_recovery_tests.cpp` and the `gpu_frame_recovery` CTest target. It covers GPU success, successful CPU fallback, failed CPU fallback, and callback order.
- The test validates control flow only. It does not inject a real SDL_GPU failure or verify SDL window-claim transitions, Metal runtime behavior, or physical iPhone recovery.
- CI must run against this commit before this change is considered verified. Physical iPhone/LiveContainer testing remains outstanding.

## Touch backend-switch hit-test and documentation corrections — 2026-11-02

- Confirmed that the old touch hit region (`x >= 0.78`, `y <= 0.16`) was substantially larger than the visible backend-switch rectangle defined in `TouchOverlay.hpp`.
- Added `touch_overlay::containsBackendButton`, deriving the normalized hit bounds from the same pixel constants used by both CPU and GPU overlay drawing. The right and bottom edges are exclusive.
- Updated `src/main.cpp` to use the shared hit-test helper; button placement, dimensions, keyboard shortcut, and backend-switch behavior inside the visible rectangle are unchanged.
- Added `touch_overlay_hit_test` coverage for landscape and portrait dimensions, inside/outside points, all rectangle edges, the previous overly broad hit region, and zero-sized viewports.
- Corrected README and handoff statements: CPU wireframe lines are not depth-tested; CPU BMP texture loading is implemented through SDL_LoadBMP; the GPU frame-recovery control-flow test exists but does not inject a real SDL_GPU failure.
- The new code must be verified by the workflow run for the resulting commit. Physical iPhone touch behavior and Metal rendering remain unverified.


## Backend-switch movement persistence fix — 2026-11-02

- Confirmed that `src/main.cpp` retained `hasMovementFinger` and the last movement coordinates across CPU/GPU transitions. The main loop continued to feed that state into `Camera::move` after the renderer changed.
- Added `BackendSwitchInputState` to clear active movement/look touch state and pending look deltas whenever a backend switch is requested.
- Added a keyboard movement gate: if WASD/QE/Shift is held during the transition, keyboard movement remains suppressed until all those keys are released, preventing a held key from carrying movement into the new backend.
- Added `backend_switch_input` unit tests for touch-state clearing and keyboard release gating.
- This is a source-level fix with deterministic unit coverage. Physical iPhone touch behavior must still be confirmed with the resulting IPA; Windows/iOS CI must complete for this commit before calling it build-verified.


## GPU rotating cube — 2026-11-02

- Added `makeTexturedCubeMesh()` in `include/engine/PrimitiveMeshes.hpp`: 24 face-local vertices, 12 triangles, outward face normals, and complete per-face UVs.
- The GPU backend now uploads this dedicated cube mesh instead of the OBJ pyramid mesh and draws a dedicated `GpuRotatingCube` scene object at the center of the camera view.
- The GPU cube is created with `makeEmissiveCubeMesh()`, which clears UV-coordinate presence on all 24 face-local vertices; the fragment shader therefore uses the vertex color and does not sample the PPM texture for the cube. Its material applies the same emissive cyan treatment to all six faces. The pyramid meshes continue to use the shared P3 PPM texture.
- The GPU cube rotates around X/Y/Z using the existing animation clock. Space pauses/resumes the same clock, so the GPU cube respects the existing animation pause control.
- The CPU renderer's existing OBJ pyramid instances and software cube path are left unchanged. The GPU mode's scene list contains only the dedicated cube.
- Added `primitive_meshes` CTest coverage for vertex/triangle counts, UV presence/range, unit normals, and index bounds.
- Windows CI can verify compilation and the mesh factory test. The regular workflow does not compile embedded MSL at runtime or prove Metal rendering; physical iPhone visual verification is still required.


### 2026-11-02 — GPU複数メッシュ描画
- GPU rendererの単一mesh前提を解消し、OBJピラミッドmeshとプロシージャルcube meshを同じvertex/index bufferへ格納する。
- meshごとのindex rangeを保持し、各GPU scene instanceがmesh indexを指定して描画する。
- GPUモードで左右のピラミッド2個と3軸回転cubeを同じフレームに描画する。1/2キーの可視切替とSpaceによるcubeアニメーション停止は既存SceneObjectを共有する。
- ピラミッドmeshはP3 PPM textureを使用する。回転cubeはUVなしの発光シアンmeshとして描画し、texture samplingを行わない。material別texture、GPU floor grid、wireframe、CPU HUD全機能のGPU移植はこの変更の対象外。
- CIと実機Metal描画の確認状態はコミット後に追記する。


### 2026-11-02 — CPU/GPUキューブTransform統一
- CPUキューブの基準は中心(0, 0, 4.2)、半径1（各辺長2）、回転X=sin(t*0.43)*0.28、Y=t*0.65、Z=sin(t*0.31)*0.12。
- GPU側にあった中心Z=6、scale=1.25、異なる回転速度がCPUと一致しない原因。
- `makeDemoCubeTransform` を追加し、CPU/GPU双方が同じ経過時間から同じTransformを取得する。
- primitive_meshesテストで初期位置・スケール・初期回転とt=1の回転式を検証。
- 実機での見た目確認は未実施。CI結果は作業後に追記。

- GPU投影の垂直焦点係数もCPUの `min(width,height) * 0.42 * 2.4` と一致する `2.016` に統一。以前のGPU 60度FOVではCPU表示より小さく見える差が残っていた。


## GPU wireframe mode — 2026-11-02

- Added a second SDL_GPU graphics pipeline using `SDL_GPU_FILLMODE_LINE`, alongside the existing filled-triangle pipeline.
- The existing F3 `wireframe` state is now passed into `GpuRenderer::renderFrame`, so the same keyboard toggle selects filled or wireframe rendering in both CPU and GPU modes.
- GPU wireframe retains the GPU pipeline's depth test and depth-write settings. This differs from CPU wireframe, whose SDL_RenderLine path is not depth-tested.
- SDL 3.2.10 defines `SDL_GPU_FILLMODE_LINE`; the SDL documentation notes automatic fallback to fill mode on some Android devices. The project currently targets iOS/Windows, but actual Metal pipeline creation and appearance still require runtime verification.
- Build and test results for this change must be checked against its GitHub Actions run. CI compilation does not verify Metal pipeline creation or the visual result on an iPhone.


## GPU multisample anti-aliasing — 2026-11-02

- Added runtime capability selection for 4x MSAA in the SDL_GPU renderer. The renderer checks color-target format support and 4x sample-count support for both the swapchain color format and selected depth format.
- When supported, scene and overlay pipelines use 4x multisampling. Rendering targets a size-matched multisample color texture and resolves into the swapchain texture at the end of the render pass; the depth texture uses the same sample count.
- When 4x MSAA is not supported, the renderer retains the existing single-sample swapchain path.
- Multisample color/depth targets are recreated when the swapchain dimensions change and released during shutdown.
- This source change requires its own Windows/iOS CI run. CI compilation does not verify Metal runtime support, visual edge quality, or performance on a physical iPhone.

## Cyan scene styling

- CPU renderer clear color is a dark cyan/blue sky tone; the procedural six-face cube uses cyan shades; the existing floor grid uses a brighter cyan color and retains the CPU depth buffer.
- `src/main.cpp` creates a shared cyan floor plane mesh and `CyanFloor` scene object. The CPU renderer draws it with its existing mesh path and overlays the depth-tested floor grid. The GPU renderer uploads it as a third mesh and draws it as a scene instance.
- GPU fragment shader applies a cyan tint, increased specular contribution, emissive cyan term, and view-angle rim highlight. This is shader-based emissive styling, not a bloom post-process.
- The GPU clear color is a matching dark blue/cyan tone. Actual Metal appearance and performance remain unverified until tested on a physical iPhone.


## GPU cube glow update — superseded

The earlier implementation drew an expanded cube shell at 1.16x scale with front-face culling and additive blending. It produced an unwanted glass-like silhouette and has been removed by the emissive point-lighting update below.


## Floor/cube color separation

- CPU: the untextured floor mesh uses a dark blue material; the cyan floor grid is also darker and less saturated than the cube.
- GPU: a per-instance material flag identifies the floor mesh and selects a dark blue-teal tint with reduced emission/specular. The cube keeps its bright cyan emissive material and acts as a point light. Material selection is explicit by mesh instance index, not based on normal direction, so cube top/bottom faces are not treated as floor.
- Visual appearance remains unverified on physical iPhone hardware.


## GPU emissive point lighting — 2026-11-02

- Removed the expanded 1.16x cube shell and its dedicated glow pipeline. The shell was a silhouette effect and did not create illumination on nearby surfaces.
- The rotating cube now uses a uniformly emissive cyan surface and supplies its animated world position as a point-light position to the mesh shader.
- The fragment shader calculates point-light distance attenuation, diffuse response, and specular response for rendered surfaces. The floor therefore receives cyan illumination according to its distance and normal relative to the cube.
- The point-light data is passed through vertex-stage uniforms and interpolated varyings to the fragment stage; no additional fragment uniform binding is required.
- This is a real-time local-light approximation, not ray tracing. It does not trace reflections or multiple light bounces. A screen-space bloom composite is now implemented as a separate post-process.
- The embedded MSL shader is not compiled by the normal C++ CI build. The matching GitHub Actions run must verify C++ compilation/tests and iOS packaging; actual Metal shader compilation, lighting appearance, and performance still require a physical iPhone test.


## GPU screen-space bloom — 2026-11-02

- The scene color is now rendered into a sampleable intermediate texture. When 4x MSAA is supported, the multisample scene target resolves into this texture; otherwise the scene renders directly into the single-sample intermediate.
- A full-screen triangle composite samples the scene texture with a 9-tap neighborhood filter. Bright samples are selected with a luminance threshold and added to the original scene to create a soft cyan/white glow.
- The touch-control/FPS overlay is drawn after the bloom composite, so the UI itself is not blurred or included in the bright-pixel extraction.
- The post-process currently uses the swapchain color format (not a dedicated HDR floating-point target) and a compact 9-tap filter. It is a lightweight screen-space bloom approximation, not a multi-pass Gaussian blur, ray tracing, reflected-light simulation, or global illumination.
- The intermediate scene texture is recreated when the swapchain dimensions change and released during renderer shutdown. The bloom sampler and pipeline are also released during shutdown.
- Runtime MSL shader compilation, actual Metal render-target sampling/resolve behavior, visual output, and performance have not been verified on a physical iPhone. The Windows CI job does not validate Metal shader compilation.

## iOS bundle identifier alignment and compiler warning cleanup — 2026-11-03

- The previous iOS workflow log reported that the generated Info.plist used `com.gasolineengine.demo` while the Xcode `PRODUCT_BUNDLE_IDENTIFIER` build setting was empty.
- CMake now sets `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER` to the same identifier as `MACOSX_BUNDLE_GUI_IDENTIFIER`.
- The iOS workflow checks the Xcode build setting before compiling and checks the packaged IPA's `CFBundleIdentifier` after packaging.
- Removed the unnecessary `nearPlane` lambda capture reported by the previous iOS compiler log.
- Verify the workflow run for the resulting commit before treating the warning as resolved; physical-device startup and Metal rendering remain separate checks.



## Discord notification embed limit fix — 2026-11-03

- GitHub Actions run [#317](https://github.com/shton13275-2/GasolineEngine/actions/runs/41554604547) completed the Windows and iOS unsigned IPA jobs successfully, but the Discord Notification job failed because Discord returned HTTP 400.
- Reconstructed the exact job/step report from the run's Jobs API: it contained 1,060 characters, exceeding Discord's 1,024-character maximum for one embed field. The report also included the Discord notification job itself, which was unnecessary.
- Updated `.github/workflows/build.yml` to exclude the notification job from its own report, use actual newline characters, cap the details field at 1,000 characters, cap the description, and validate embed field/count/total-length limits before sending.
- Changed curl to `--fail-with-body` so a future Discord HTTP error includes the response body in the Actions log.
- The fix requires a new workflow run. Do not consider Discord delivery fixed until that run's Discord Notification job succeeds. Run #317's Windows and iOS jobs were both successful; its overall workflow failed only at notification delivery.


## GPU bloom and soft floor shadow — 2026-11-03

- Replaced the 9-tap bloom cross with a 13-tap kernel using diagonal and wider-radius samples. The highlight threshold was lowered slightly and the glow contribution increased to spread cyan highlights farther without adding render targets or extra passes.
- Added an analytic soft elliptical shadow to the GPU floor fragment shader. Its center is projected from the rotating cube's world position opposite the directional light; the edge is softened with smoothstep and opacity is partial.
- This is a floor-only approximation, not a depth shadow map: it does not create shadows on arbitrary objects or account for occluders. The bloom remains a single-pass screen-space approximation, not HDR multi-pass Gaussian blur or ray tracing.
- The embedded MSL is not compiled by the normal C++ CI build. Verify the resulting Windows/iOS workflow and then inspect the IPA on a physical iPhone for Metal shader compilation, shadow placement, bloom appearance, and performance before claiming runtime success.


## GPU bloom and pyramid lighting correction — 2026-11-03

- Root cause found in the fragment shader: emission was selected as floor-vs-non-floor, so every non-floor mesh (including both pyramid instances) received `albedo * 1.35` emission. Added `materialFlags.y` / `isEmissive` and set it only for mesh range 1, which is also the mesh selected as the moving cube point-light source.
- Non-emissive non-floor meshes now have zero material emission and use ambient + directional + attenuated point-light shading. The cube retains strong cyan emission.
- Reduced bloom to a 9-tap cross, moved threshold to `smoothstep(0.82, 1.20, brightness)`, and lowered composite gain to 0.55. This is intended to keep the halo closer to the cube and reduce washout.
- This corrects the identified material-emission logic; it does not implement a general shadow map or physically based global illumination. Validate the actual MSL runtime compilation and visual result on a physical iPhone after CI.


## Shared CPU/GPU scene render items — 2026-11-03

- Added `SceneRenderItem` as the common render-item record containing the mesh pointer, SceneObject pointer, CPU texture path, GPU mesh-range index, and explicit CPU/GPU participation flags.
- The sample application now builds one `renderItems` vector and passes it to both renderers. The CPU renderer skips items not marked for CPU; the GPU renderer skips items not marked for GPU. This removes the duplicated CPU `MeshInstance` and GPU `GpuMeshInstance` lists from the application.
- The dedicated GPU cube is marked GPU-only so it does not get drawn again by the CPU mesh path. Existing CPU procedural cube rendering remains unchanged.
- Added the `scene_render_items` CTest to verify shared mesh/object/texture/index bindings and CPU-only/GPU-only flags.
- This is shared scene-render-item data, not yet a unified renderer interface or a complete parity of CPU/GPU features. The GPU mesh upload table remains a backend resource list.
- Verify the matching Windows and iOS GitHub Actions jobs before considering the change build-verified. Physical iPhone Metal rendering remains a separate manual check.


## GPU metallic-roughness PBR — 2026-11-03

- Added `metallic` and `roughness` fields to the common `SceneRenderItem`. These fields are backend-neutral scene material data; the CPU renderer leaves them unused while the GPU renderer clamps and uploads them in the existing per-object material uniform.
- Replaced GPU Blinn-Phong direct lighting with a forward Cook-Torrance implementation: GGX normal distribution, Smith-Schlick geometry, and Schlick Fresnel. Both directional and attenuated point-light contributions use the BRDF. Ambient and emissive contributions remain separate.
- Set demo values explicitly: pyramid 0.05/0.72, emissive cube 0.35/0.24, floor 0.0/0.92 (metallic/roughness).
- Extended the shared-scene-item test to verify material values and defaults.
- This is the first PBR stage. Environment-map/image-based lighting, normal maps, tone mapping, and distinct GPU texture/material resources remain unimplemented. CI does not compile the embedded MSL at runtime; actual Metal shader compilation and visual validation require a physical iPhone.


## GPU HDR scene target — 2026-11-03

- The GPU renderer now prefers an `R16G16B16A16_FLOAT` intermediate scene color target when the device reports support for both color-target and sampler usage. If unavailable, it retains the swapchain-format intermediate target.
- The scene pipeline and optional 4x MSAA color target use the selected scene format; the final composite remains in the swapchain format. The composite applies an ACES-fitted tone map after the existing bloom contribution.
- This is a capability-gated HDR render-target path. It does not add exposure adaptation or a separate multi-pass bloom pyramid. Metal format support, HDR highlight appearance, and memory/performance on iPhone hardware still require device verification.

### GPU shadow mapping — 2026-11-03

- Added a 1024×1024 directional shadow map for the SDL_GPU renderer. A light-space depth pass renders visible GPU scene items into an `R32_FLOAT` color target with a depth attachment, then the main PBR fragment shader samples it using a 3×3 percentage-closer filtering (PCF) kernel.
- Shadow visibility modulates the directional-light contribution; ambient and point-light terms remain unshadowed. The previous floor-only analytic shadow approximation was removed.
- The shadow map uses a fixed orthographic light camera aligned with the directional light. Devices without sampleable `R32_FLOAT` color-target support fail GPU initialization with an explicit error, allowing the existing application CPU fallback to take over.
- This is a single directional shadow map, not cascaded or point-light shadowing. Shadow acne, peter-panning, light-frustum coverage, Metal runtime shader compilation, visual quality, and iPhone performance still require CI and physical-device verification.


GPU shadow target format fallback — 2026-11-03

- GPU shadow-map creation now prefers `R32_FLOAT` when the device supports it as both a color target and sampler.
- If that format is unavailable, it selects `R8G8B8A8_UNORM` with the same color-target and sampler requirements and stores the shadow depth in the red channel.
- GPU activation no longer fails solely because `R32_FLOAT` is unsupported. If neither format is supported, initialization still reports a clear error and the existing CPU fallback is used.
- This change is source-level until the matching Windows/iOS CI completes. The CI build does not validate runtime Metal format support or visual shadow output on an iPhone.


## GPU shadow shader varying fix — 2026-11-03

- The fragment-stage `VertexOutput` structure was missing `lightSpacePosition`, although the vertex shader writes it and the fragment shader reads it for shadow-map projection. Added the matching `float4 lightSpacePosition` member to the fragment input structure.
- This is a concrete MSL interface mismatch found by source inspection. The normal C++ CI build does not compile embedded MSL, so only an actual Metal shader compilation/runtime test on iPhone can confirm the GPU backend now initializes successfully.


## GPU screen-space ambient occlusion (SSAO) — 2026-11-03

- Added a single-sample camera-depth prepass using the existing shadow/depth graphics pipeline. The prepass writes projected camera depth into a screen-sized sampleable color texture and uses a dedicated single-sample depth attachment; this is independent of the scene's optional 4x MSAA depth buffer.
- Extended the final fullscreen composite to sample eight neighboring depth positions. Neighbor samples that are closer than the center pixel contribute occlusion, and the resulting ambient-visibility factor darkens the scene before the existing bloom and ACES-fitted tone mapping.
- This is a lightweight screen-space depth-neighborhood AO approximation, not a full view-space SSAO implementation: it does not reconstruct view-space positions/normals, use a hemisphere sampling kernel, or apply temporal filtering.
- The added pass costs one extra geometry depth pass per frame and two screen-sized textures. The normal C++ CI build does not compile embedded MSL; Metal runtime shader compilation, sampling, visual quality, and iPhone performance remain to be checked on-device.


## GPU screen-space reflections (SSR) — 2026-11-03

- Added an SSR ray-march to the existing fullscreen GPU composite. The shader reconstructs world position from the camera-depth texture using an inverse view-projection matrix, estimates a world-space normal from adjacent depth samples, reflects the camera ray, and marches up to 24 screen-projected steps against the scene depth buffer.
- The initial pass is deliberately conservative and focuses on near-horizontal surfaces, allowing the demo floor to reflect visible scene color. Reflection intensity is fixed and fades toward screen edges and longer ray distances. It reuses the existing scene color/depth textures and does not add another render target or geometry pass.
- This is a depth-derived screen-space approximation, not a full material-aware SSR implementation: it does not have a per-pixel normal/roughness buffer, does not use the PBR roughness values to blur/refine reflections, and cannot reflect off-screen or occluded geometry. Depth discontinuities and screen-edge clipping can produce missing/unstable reflections.
- The composite now receives inverse view-projection, view-projection, and camera position through one fragment uniform buffer. Existing SSAO, bloom, and ACES tone mapping are retained after reflection composition.
- The normal C++ CI does not compile embedded MSL at runtime. Verify the new shader on a physical iPhone before claiming Metal runtime or visual success. The preceding Shadow Mapping and SSAO visuals were reported by the user as drawing normally on-device.


### SSR depth-hit stability correction — 2026-11-03

- User reported that the first SSR result looked like multiple stacked reflection layers and visually filled the gap directly below the cube.
- Source inspection found that the first ray marcher accepted any single projected depth sample behind scene depth within a relatively broad 0.018 depth interval. It did not require a front-to-back crossing between consecutive ray samples, making silhouette/depth-discontinuity false hits possible.
- Tightened the surface mask toward horizontal surfaces, increased the normal origin bias, skipped the near-field region, reduced reflection intensity, and changed hit detection to require a consecutive-sample depth-delta crossing. Four binary-refinement steps refine the first crossing.
- Background samples are treated as being in front of the scene geometry; a hit cannot be accepted until a prior ray sample exists. This avoids accepting the first visible depth sample as an intersection by default.
- This is a source-level correction only. Verify the resulting reflection on a physical iPhone; CI does not compile the embedded MSL shader at runtime or judge reflection appearance.


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


### SSR depth precision fallback correction — 2026-11-04

- The camera-depth prepass and directional shadow map share a sampleable depth-color format. The preferred format is `R32_FLOAT`; the portable fallback is `R8G8B8A8_UNORM`.
- The previous fallback wrote projected depth only to the red channel, reducing the sampled depth to 8-bit precision. That precision is too coarse for the SSR crossing thresholds (`0.0008` minimum crossing and `0.006` maximum accepted delta) and could create unstable hit classification when the fallback is selected.
- The fallback now packs normalized depth into RGB as a 24-bit value and decodes it for SSR center/ray/refinement samples, SSAO neighbor samples, and directional shadow comparisons. The `R32_FLOAT` path continues to store/read the red channel directly.
- This is a source-level precision correction. It does not yet prove that the iPhone selected the fallback format or that the reflection artifact is resolved. Embedded MSL compilation is not covered by the normal C++ CI; verify the new iOS IPA on a physical iPhone and record the visual result separately.


## Embedded MSL compile in iOS CI — 2026-11-04

- Added `scripts/extract_msl_shaders.py` to extract the eight raw MSL string constants from `src/engine/GpuRenderer.cpp`: mesh vertex/fragment, shadow vertex/fragment, bloom vertex/fragment, and overlay vertex/fragment.
- The iOS workflow compiles each extracted source with `xcrun -sdk iphoneos metal -c` and links the AIR outputs into a temporary `.metallib` before building the unsigned app. This is a CI source-compilation check; the runtime still creates SDL_GPU shaders from embedded MSL strings.
- This check is intended to catch MSL syntax/type/compiler errors that the C++ compiler cannot see. It does not verify SDL_GPU pipeline creation, device-specific texture-format support, shader binding compatibility at runtime, or visual rendering on a physical iPhone.
- Updated the current implementation summary to reflect Cook-Torrance PBR, directional shadow mapping, depth-neighborhood SSAO, experimental floor-only SSR, HDR/ACES compositing, and 24-bit packed depth fallback. The physical screenshot from the earlier IPA still showed repeated/combed SSR artifacts; the latest packed-depth change has not yet been verified on-device.
- CMakeLists.txt currently registers 12 CTest targets, including `scene_render_items`; README and handoff counts now match the actual registrations.


### GPU SSR manual toggle — 2026-11-04

- Added a runtime SSR enable flag to the GPU composite. When disabled, the fullscreen composite skips only the SSR world-position reconstruction and reflection ray-march; the camera-depth prepass, SSAO, bloom, ACES tone mapping, scene lighting, and shadow mapping remain active.
- On iOS/GPU mode, a dedicated `SSR` button is drawn to the left of the existing `CPU` backend-switch button. Its blue state indicates SSR enabled; its muted state indicates SSR disabled. The hit region is defined in `include/engine/TouchOverlay.hpp` and covered by `touch_overlay_hit_test`.
- Keyboard **T** toggles SSR on desktop. The toggle state is held by `src/main.cpp` and passed to `GpuRenderer::renderFrame`; it resets to enabled on application startup.
- This is a diagnostic isolation control, not a fix for the reflection artifact. Compare the same scene with SSR enabled and disabled on the physical iPhone. No visual result is claimed until that test is performed.


### Overlay MSL compile fix — 2026-11-04

- The iOS Metal source compilation check exposed a malformed directive in the embedded `overlayVertexSource`: `#include <metal_stdlib>using namespace metal;` had no newline between the include and namespace directive. This prevented Metal's standard math functions (`cos`, `sin`, `max`) from resolving in that shader.
- Restored the newline so the extracted source contains separate include and `using namespace metal;` lines. This is a source-level correction; the follow-up Actions run must confirm Metal compilation.

## SSR diagnostic log — 2026-11-04

- Added an `ssr.log` file beside `startup.log` using the same SDL `SDL_GetPrefPath("com.gasolineengine", "GasolineEngine")` directory, including on iOS. This is a writable app preference directory, not the bundled app resources directory.
- The file is truncated/recreated at launch. SSR ON/OFF transitions are logged immediately. While GPU rendering succeeds, at most one state record per second is written with SSR state, pixel resolution, smoothed FPS, camera coordinates, and GPU driver. GPU frame errors are recorded.
- This entry records the initial CPU-side logging implementation only. It was superseded by the later SSR GPU diagnostic readback change below: the current renderer writes a second RGBA8 color target, asynchronously downloads it with a GPU fence about once per second, and appends aggregate depth-valid, reflective-receiver, ray-march-step, and hit-pixel counts to `ssr.log`. These are image-derived and quantized aggregates, not fragment-stage atomic counters; see the later section for the exact channels and limitations.
- iOS IPA build and physical iPhone file access still need verification. The exact visible location can be retrieved from the app's SDL preference directory; it is not a folder inside the installed app bundle.


### SSR GPU diagnostic readback — 2026-11-04

- Extended the fullscreen composite fragment shader to write a second RGBA8 diagnostic color target alongside the normal swapchain color output.
- Per-pixel channels encode: valid camera-depth sample, floor receiver mask, ray-march step count normalized to 64 steps, and ray-hit flag.
- The renderer downloads the diagnostic texture to a SDL GPU download transfer buffer at most once per second. It submits that copy with SDL_SubmitGPUCommandBufferAndAcquireFence, polls the fence on later frames, and only maps/aggregates the transfer buffer after the fence signals. This avoids a synchronous per-frame readback.
- ssr.log now receives aggregate depth_valid_pixels, reflective_pixels, ray_march_pixels, ray_march_steps, avg_ray_steps, and ray_hit_pixels records, in addition to the existing CPU-side state.
- Runtime `SDL_BeginGPUCopyPass` and `SDL_MapGPUTransferBuffer` failures are stored by `GpuRenderer`, consumed once by the main loop, and appended to `ssr.log` as `gpu_diagnostics_error=...`. This diagnostic failure is separate from a rendered-frame failure.
- This is an aggregate per-pixel diagnostic classification, not a fragment-stage atomic counter. The RGBA8 step channel is quantized. It does not prove that the hit color sampled is visually correct, and the physical iPhone result must still be inspected.
- The diagnostic MRT/readback path and the updated embedded MSL require the matching Windows/iOS CI run and a new physical iPhone IPA test. Do not report the SSR artifact as fixed.


### SSR diagnostic log analysis and receiver-only tracing — 2026-11-04

- The user supplied nine SSR-on GPU samples and SSR-off samples from an iPhone using the Metal driver at 852×393. The GPU reported about 60 FPS. With SSR off, reflective_pixels, ray_march_pixels, ray_march_steps, and ray_hit_pixels were zero. With SSR on, depth-valid pixels ranged from 256,067 to 280,186; reflective-mask pixels ranged from 186,820 to 237,444; reported ray-hit pixels ranged from 36,934 to 45,783.
- In every SSR-on sample, ray_march_pixels exactly equaled depth_valid_pixels. Source inspection confirmed the shader ran the ray loop for every valid depth pixel, regardless of whether the pixel belonged to the reflective floor. The final reflection weight was multiplied by surfaceMask, so non-floor hits did not contribute to the final image, but they inflated hit statistics and caused unnecessary ray tracing.
- Commit 65b444a25c7871f811eaabfce5282872e5599395 now guards the ray-tracing block with surfaceMask > 0.001. New logs should show ray-march pixels restricted to floor receiver candidates and hit pixels restricted to that same set.
- This is a confirmed source-level correction to tracing scope and diagnostic validity, not proof that the repeated/combed reflection artifact is fixed. The submitted log predates the change. Next verification requires the workflow for the new commit, a fresh IPA, and a new iPhone screenshot/log. If the artifact remains, continue with debug views for raw depth, reconstructed world position/floor mask, ray UV, and hit mask rather than blindly tuning ray step sizes.

### SSR receiver-only tracing and debug views — 2026-11-04

- Commit 65b444a25c7871f811eaabfce5282872e5599395 adds a floor receiver guard (surfaceMask > 0.001) around the ray trace. This fixes the diagnostic mismatch where ray_march_pixels equaled depth_valid_pixels because every visible geometry pixel was traced. The earlier iPhone log predates this correction.
- Added a temporary debug-view selector to GpuRenderer. The fullscreen composite reads the debug-view index from CompositeConstants.depthFlags.z: 0 normal output, 1 raw depth, 2 floor receiver mask, 3 hit mask over receiver mask, 4 hit UV.
- The DBG touch button immediately left of SSR cycles modes on iPhone. Desktop V cycles the same modes. The SSR ON/OFF control remains independent.
- The new debug UI/code changes were committed in subsequent commits on ios/initial-support. Verify the latest branch HEAD and matching CI before distributing an IPA.
- This does not yet prove the reflection artifact is fixed. Use the debug views on a fresh iPhone build to determine whether depth, floor reconstruction, ray hits, or sampled hit UVs are spatially incorrect. Keep CPU renderer unchanged.

### Discord commit notifications — 2026-11-04

- Updated `.github/workflows/build.yml` on `ios/initial-support`: the existing Discord detailed build-report job now runs for every `push` event, regardless of whether the iOS build succeeds. It still waits for the Windows and iOS jobs so the message can include their results.
- Pull-request and manual workflow runs no longer trigger this Discord report.
- The workflow uses the repository Actions secret `DISCORD_WEBHOOK_URL`. If that secret is absent, the notification step emits a warning and skips sending; no webhook URL is stored in the repository.
- This change configures push-triggered notification; actual Discord delivery must be verified by a subsequent push with the secret configured.


## Documentation and Metal warning cleanup — 2026-11-05

- Corrected stale SSR log descriptions in the README, AI handoff, and GPU integration notes. The current implementation uses an RGBA8 diagnostic target and asynchronous GPU-fence readback; the counts are image-derived classifications, not fragment-stage atomic counters.
- Replaced literal backslash-n escape sequences that had been displayed in the README and AI handoff with actual Markdown line breaks.
- Removed the unused local `lighting` value from the embedded mesh fragment shader. The returned lighting expression is unchanged; this addresses the unused-variable warning reported by the iOS Metal compiler in Actions run [#434](https://github.com/shton13275-2/GasolineEngine/actions/runs/41832461155).
- Renamed the Windows workflow step to `Run CTest suite (12 tests)`; the CTest command and test selection are unchanged.
- These documentation and warning-cleanup changes require their own CI run. They do not claim a new physical-device SSR result.


## CMake library-only path and Discord branch guard — 2026-11-06

- Fixed the `ORIGINALENGINE_BUILD_APP=OFF` path. The engine library itself includes SDL3-dependent renderer/GPU sources, but SDL3 FetchContent and target linking were previously nested under the sample-app condition. SDL3 setup/linking now occurs whenever `ORIGINALENGINE_USE_SDL3=ON`; the app option controls only creation of `originalengine_app`.
- `ORIGINALENGINE_USE_SDL3=OFF` now fails at CMake configure with an explicit message because the current engine library is not SDL-independent.
- Added a Windows workflow configure/build check for the engine library with the sample app and CTest disabled. It reuses the SDL3 source fetched by the normal Windows configure.
- Restricted the notification job in the `ios/initial-support` workflow to push events whose ref is exactly `refs/heads/ios/initial-support`. Pull requests and manual dispatch do not trigger this branch's notification job. Other branches use their own workflow files; at review time `main` had no Discord job, while `docs/sync-to-main` had a separate `discord-notify` job with `if: always()`.
- Verification: Actions runs #436 and #437 completed successfully, including Windows, iOS unsigned IPA, and Discord Notification jobs. The Windows engine-only build and CTest 12/12 passed in both runs. Run #438's Windows job also passed; its iOS job was still at Configure iOS Device when this note was written. No change was made to `main` or `docs/sync-to-main`.


## SSR diagnostic error visibility — 2026-11-06

- The renderer already stored asynchronous readback failures in `ssrDiagnosticsError_`, but no public accessor or main-loop consumer existed, so copy-pass and transfer-buffer mapping errors were not written to `ssr.log` when frame rendering continued.
- Added `GpuRenderer::takeSsrDiagnosticsError()` to return and clear the pending message. The main loop writes each consumed message once as `gpu_diagnostics_error=...`.
- This improves diagnostics only; it does not change the GPU readback, fence, shader, or reflection calculation. Runtime failure injection and physical-device validation remain outstanding.


## MSVC shadow-warning cleanup and Application API review — 2026-11-06

- Renamed the inner Gouraud-shading locals in `Renderer3D.cpp` (`normal`, `lightDirection`, and `lightLength`) to `vertexNormal`, `vertexLightDirection`, and `vertexLightLength`. This removes the three project-owned MSVC C4456 shadow warnings without changing the shading expressions.
- Confirmed that `Application::run()` is declared in `include/engine/Application.hpp` but has no definition in the repository, and the current sample entry point does not call it. No behavior contract or call site defines what this method should do. Do not invent an event-loop implementation; define the API contract or remove the declaration in a deliberate API change before using it.
- The SDL3 C5286 warnings seen in the Windows build originate in the fetched SDL3 dependency, not project source; they are left unchanged.


## SSR world-space intersection and thickness update — 2026-11-07

- Updated the embedded `bloom_fragment` SSR ray/depth intersection in `src/engine/GpuRenderer.cpp`. The previous implementation compared projected NDC depth deltas against fixed thresholds (`0.0008` / `0.006`), even though perspective-projected depth is nonlinear with camera distance.
- The ray point and reconstructed scene point are now compared by their camera-to-point distances in world units. A front-to-back crossing is refined with the existing four binary-search iterations.
- The final refined high endpoint is reprojected and depth-sampled again. A hit is accepted only when its world-space separation is greater than 0.002 and no greater than 0.08 world units. These are initial scene-scale thresholds, not a claim of visually optimal values; adjust only from new device evidence.
- Corrected the hit-distance bookkeeping: distance fade now uses the refined ray distance associated with the final hit UV, rather than the original coarse march distance.
- The no-geometry sentinel is now `-1.0` in the same world-distance-delta domain as valid samples. The old background path stored `projected.z - 1.0`, mixing projected-depth and world-distance units.
- The four-step refinement and 64-step march remain; no duplicate binary-search stage was added. This source change is not yet confirmed to fix the repeated/combed reflection artifact. A matching iOS Metal compile/IPA run and physical iPhone comparison are required.
- CI CTest does not exercise this embedded shader logic. Record Windows build, iOS MSL compilation/IPA packaging, and physical-device visual validation as separate results.


## Application API recheck and standard header hygiene — 2026-11-07

- Rechecked `include/engine/Application.hpp` and `src/engine/Application.cpp` at HEAD `6557f7eae6e1a88305c22acf700c4e1dc84e6ed7`. The public header declares `int Application::run()`, but the implementation still defines only the constructor. `CMakeLists.txt` includes `Application.cpp` in the `originalengine` library.
- Confirmed that neither `src/main.cpp` nor any of the 12 current test source files references `Application`; the sample application owns a separate event loop.
- Reproduced the link failure with GCC 14.2.0 using the current header/implementation and a minimal caller: the implementation object compiles, while the final link fails with `undefined reference to originalengine::Application::run()`.
- Added the direct `<utility>` include to `Application.cpp` for its use of `std::move`, avoiding reliance on transitive standard-library includes. A standalone C++20 compile with `-Wall -Wextra -Wpedantic -Werror` passed after this include correction.
- `Application::run()` remains intentionally unimplemented in this run. The repository does not define whether it owns SDL initialization/window creation, an event loop, rendering callbacks, shutdown, or error/return semantics. Implementing a blank or guessed loop would create undocumented public behavior; removing the declaration would be a public API break. Define that contract before making either change.


## SSR diagnostics fence-wait failure handling and Node.js 24 actions — 2026-11-08

- On the current source, `createDepthTexture()` waits for an outstanding `ssrDiagnosticsFence_` before replacing size-dependent diagnostic/depth targets. SDL 3.2.10 documents `SDL_WaitForGPUFences` as returning false on failure and requiring `SDL_GetError()`. The previous failure branch released the fence and continued without reporting the failed wait.
- The resize path now captures the SDL error in both `ssrDiagnosticsError_` and `lastError_`, then returns false. `renderFrame()` submits the already-acquired swapchain command buffer and reports the GPU frame failure; the existing main-loop recovery releases the GPU renderer and attempts CPU renderer recreation. The pending diagnostic resources remain owned by the renderer until shutdown cleanup.
- No SDL_GPU fence-wait failure-injection test exists in the current 12 CTest targets. The change requires the next Windows/iOS CI run; CI does not prove the physical-device failure path.
- Actions run #445 on 2026-11-07 used `actions/checkout@v4`, `actions/cache@v4`, and `actions/upload-artifact@v4`, and emitted the Node.js 20 deprecation warning. Updated this branch's build workflow to `actions/checkout@v7.0.1`, `actions/cache@v6.1.0`, and `actions/upload-artifact@v7.0.2`; their action manifests declare Node.js 24. Run #445 used hosted runner version 2.338.2, above the cache v6 minimum 2.327.1. Verify the new versions in the next workflow run.
- Run #445's Discord Notification job completed with success but its log explicitly reported that `DISCORD_WEBHOOK_URL` was not configured and delivery was skipped. A successful job conclusion therefore did not prove a Discord message was delivered. The README now records this distinction.
