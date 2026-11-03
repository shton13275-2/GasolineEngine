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
- 現在のGPU経路はP3 PPMテクスチャのUVサンプリングと環境光・方向光・Blinn-Phongスペキュラを実装済み。iPhone実機でのMetal描画は未確認。
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
- per-object uniformにMVP、model、inverse-transpose normal matrix、camera positionを渡す。fragment shaderはambient、directional diffuse、Blinn-Phong specularに加え、回転キューブの位置を使った減衰付きemissive point lightingを計算する。回転キューブはUVを持たず、PPM textureをサンプリングせずに全6面へ発光シアンを適用する。
- GPU overlayはタッチガイド、FPS、CPU切替ボタンを描画する。シーンはサンプル可能な中間カラーtextureへ描画され、9-tap screen-space bloomでswapchainへ合成した後、single-sample overlay pipelineでUIを描画する。CPU側のfallbackは維持する。
- GPU初期化はMSLを要求するため、Metal対応GPUを主対象とする。swapchain color/depth formatが対応する場合は4x MSAAを使い、非対応時はsingle-sampleへfallbackする。Windows CIのコンパイル成功はWindowsでのGPU runtime対応を意味しない。

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
  - `--smoke-test`
  - CTest
  - Windows artifact upload
- iOS unsigned IPA job:
  - macOS runner
  - Xcode generatorでiOS Device arm64 build
  - unsigned .appをIPA化
  - IPA integrity / bundle contents verify
  - artifact upload
- 実際のworkflow runとjob結果は作業のたびにGitHub Actionsで確認する。古い成功runを最新変更の成功として扱わない。
- Discord通知は `ios/initial-support` のワークフローだけで送信する。`main` ではDiscord通知jobを実行しない。
- iOS側のDiscord通知jobはWindowsとiOSの両jobに依存し、iOS unsigned IPA jobが成功した場合にのみ実行する。通知本文にはWindows/iOS両方の結果を含める。Windowsが失敗した場合もiOSが成功すれば結果をまとめて通知する。
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
| `tests/` | 11 CTest targets: カメラ、衝突、シーン、OBJ、PPM、Renderer、GPU復旧、GPUジオメトリ、タッチオーバーレイ、backend switch input、primitive meshes |
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
- 2026-10-31: iOS buildのSDL3 source cache、Xcode versionを含むcache key、`FETCHCONTENT_UPDATES_DISCONNECTED`、`.app`自動検出を追記。Windows/iOS両jobで`docs/`をsparse checkout対象外にした設定を追記。\n- 2026-10-31: Actionsの失敗runを調査。Xcode version出力が複数行のため`$GITHUB_OUTPUT`で不正形式になっていた問題、およびCMake経由の`xcodebuild`に`-derivedDataPath`を渡してscheme要求エラーになっていた問題を確認。workflowを修正し、Xcode versionを1行化、DerivedData cache/引数を一旦除去してSDL3 source cacheのみ残した。修正後CIの結果は未確認。


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
