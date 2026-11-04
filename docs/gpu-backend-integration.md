# SDL GPU backend integration notes

This document describes the implementation on `ios/initial-support`, not an unimplemented integration plan.

## Current implementation

- `GpuRenderer` is declared in `include/engine/GpuRenderer.hpp` and implemented in `src/engine/GpuRenderer.cpp`.
- `src/main.cpp` owns CPU/GPU backend transitions. Before GPU initialization it destroys the active `SDL_Renderer`; if GPU initialization fails, it releases the partial GPU renderer and attempts to recreate the CPU renderer.
- `GpuRenderer::initialize` creates an SDL_GPU device requesting `SDL_GPU_SHADERFORMAT_MSL`, claims the window, creates graphics pipelines and a depth attachment, uploads mesh vertex/index buffers, and creates the texture/sampler.
- Mesh upload converts loaded OBJ triangles to indexed GPU geometry and uses visible scene-object transforms.
- UV presence is carried as a dedicated vertex attribute; negative and out-of-range OBJ UV values remain valid inputs to the repeat-addressed sampler.
- A triangle is textured only if all three corners have UV coordinates. The production path uses the shared `gpu_detail::triangleHasCompleteTexCoords` rule in `include/engine/GpuGeometryRules.hpp`; otherwise all three vertices use the untextured vertex-color path. This keeps the varying constant across the primitive.
- Per-object constants include model, MVP, inverse-transpose normal matrix, and camera position. OBJ vertex normals are used when valid; missing normals use generated flat face normals.
- The mesh pass uses depth testing/writes and a depth-stencil texture. The depth texture is recreated when swapchain dimensions change.
- The GPU fragment shader samples the P3 PPM-derived RGBA texture and applies metallic-roughness Cook-Torrance PBR (GGX normal distribution, Smith-Schlick geometry, and Schlick Fresnel) to directional and distance-attenuated point lighting from the emissive rotating cube. A 1024×1024 directional shadow map modulates directional lighting with a 3×3 PCF comparison.
- A GPU overlay pass draws touch-control guides, movement knob, FPS text, and CPU backend switch control. It does not use the CPU `SDL_Renderer` while the window is claimed by SDL_GPU.
- The backend-switch touch hit region is computed from the same shared pixel rectangle constants used by CPU and GPU overlay drawing (`touch_overlay::containsBackendButton`).
- The texture path requests a full mip chain when the device reports support for the required RGBA8 sampler + color-target usage. Otherwise it retains a single-level texture.
- The scene color target prefers sampleable `R16G16B16A16_FLOAT` when the device reports color-target and sampler support; otherwise it uses the swapchain format. The final composite applies ACES-fitted tone mapping. 4x MSAA is enabled only when the selected scene color and depth formats support it; the multisample target resolves into a single-sample intermediate, otherwise the scene uses a single-sample target.
- A single-sample camera-depth prepass writes normalized depth into the same selected depth-color format. The fullscreen composite evaluates an eight-neighbor depth-neighborhood SSAO approximation and an experimental floor-only SSR ray march (64 samples, four binary-refinement iterations), then applies the existing 9-tap screen-space bloom and tone map. The touch/FPS overlay is drawn last with a single-sample pipeline. SSR remains limited by screen edges and disocclusions; an earlier physical-device screenshot showed repeated/combed reflection artifacts, and the latest depth-precision correction has not yet been checked on-device.
- The depth prepass and directional shadow map prefer `R32_FLOAT`. When the renderer falls back to `R8G8B8A8_UNORM`, normalized depth is packed into RGB at 24-bit precision and decoded by the SSAO, SSR, and shadow-sampling paths. The fallback path is source-level and CI-compiled; the actual format selected by an iPhone GPU is not yet recorded.
- Before texture allocation/upload, `RgbaImage::hasValidRgba8Data()` checks nonzero dimensions, exact tightly packed RGBA8 byte count, and the SDL_GPU transfer-buffer size limit.

## Backend switching and failure behavior

1. CPU-to-GPU switching stops CPU rendering, destroys the active `SDL_Renderer`, then initializes `GpuRenderer` and claims the window.
2. If GPU initialization fails, partial GPU resources are released and `SDL_CreateRenderer` is attempted to restore CPU rendering.
3. GPU-to-CPU switching destroys `GpuRenderer`, which releases the window and GPU resources, then creates a new `SDL_Renderer`.
4. If a GPU frame fails, `src/main.cpp` records the error, destroys `GpuRenderer` (releasing its window claim), switches out of GPU mode, and attempts to recreate `SDL_Renderer`. If CPU renderer recreation succeeds, the same main-loop iteration continues through the CPU render path. The application exits only if CPU renderer recreation also fails.

Keyboard input **G** requests a backend switch. The touch interface also has a top-right switch hit region.

## Shader and platform constraints

- Runtime mesh and overlay shader sources are embedded as MSL strings in `src/engine/GpuRenderer.cpp`.
- `shaders/Metal/Basic3D.*.metal` are bundled source assets; the current `GpuRenderer` creates runtime shaders from embedded strings rather than loading those files.
- GPU device creation explicitly requests MSL. A successful Windows compile does not establish Windows GPU runtime compatibility.
- The iOS workflow builds an arm64 iOS Device app bundle and packages it as an unsigned IPA. CI build/package success does not prove Metal shader compilation or rendering on a physical iPhone.

## Tests and remaining verification

- `scene_render_items` verifies shared CPU/GPU render-item mesh/object/texture/index bindings, backend participation flags, and material values/defaults.
- `gpu_geometry_rules` tests all eight boolean combinations for UV presence across the three triangle corners. It verifies the shared policy predicate used by `buildGpuGeometry`; it does not execute SDL_GPU geometry upload or a shader.
- `touch_overlay_hit_test` verifies that normalized touch coordinates are tested against the same backend-switch rectangle drawn by both renderers, including landscape/portrait dimensions and exclusive right/bottom edges.
- `ppm_image_loader` tests decoded RGBA8 data validation, including incorrect byte counts and dimensions exceeding the supported upload size.
- GPU frame-failure CPU fallback uses `runGpuFrameWithCpuFallback` from `include/engine/GpuFrameRecovery.hpp`. `gpu_frame_recovery` tests GPU success, successful CPU fallback, fatal CPU fallback, and callback ordering. It does not inject an SDL_GPU failure or verify real SDL window-claim/resource transitions.
- `backend_switch_input` tests clearing active touch state and suppressing held keyboard movement until release during backend transitions.
- `primitive_meshes` tests the procedural cube's vertex/triangle structure, UVs, normals, and index bounds.
- MTL material parsing and per-material texture selection are not implemented.
- GPU PNG/JPEG/BMP texture loading is not implemented; the GPU texture input currently comes from the P3 PPM loader.
- GPU wireframe rendering is implemented with a separate `SDL_GPU_FILLMODE_LINE` pipeline and retains depth testing. A shared CPU/GPU renderer interface is not implemented.
- The iOS workflow now extracts and compiles all eight embedded MSL sources with Xcode's Metal command-line compiler and links a temporary Metal library. This catches source compilation errors but does not verify SDL_GPU runtime pipeline creation or device behavior.
- Physical iPhone verification of runtime Metal pipeline creation, texture sampling, PBR/shadow/SSAO/SSR output, touch interaction, and repeated backend switching remains outstanding.
- Long-running GPU resource lifecycle and leak testing remains outstanding.

## Verification guidance

- The Windows workflow configures and builds the project, runs the application smoke test, and executes CTest. It does not establish that the MSL GPU backend runs on Windows.
- The iOS workflow configures an iOS Device arm64 target, builds the unsigned app, packages an IPA, and checks archive contents. It does not run the app on a physical device.
- Keep CI build success, IPA packaging success, physical-device startup, and successful Metal rendering as separate verification results.
- The standalone `--gpu-smoke-test` path checks SDL_GPU device/window/swapchain submission only; it is not a mesh-rendering test and is not part of the current build workflow.


## GPU frame recovery control-flow test

- `include/engine/GpuFrameRecovery.hpp` contains the shared `runGpuFrameWithCpuFallback` control flow used by the production main loop and its unit test.
- On GPU success, the helper returns `GpuRendered` without invoking release or CPU creation callbacks.
- On GPU failure, it invokes GPU release first, then CPU renderer creation, returning `CpuFallbackReady` or `Fatal`.
- `gpu_frame_recovery` tests all three result paths and the callback ordering.
- This is a deterministic control-flow test. It does not inject an SDL_GPU failure, validate SDL window-claim release/reclaim, or establish physical-device Metal recovery.


## Input state during backend transitions

- `src/main.cpp` resets active movement/look touch state and pending look deltas whenever a CPU/GPU switch is requested, so the new renderer does not inherit an active joystick gesture.
- Keyboard movement keys held during the transition are ignored until all movement keys (W/A/S/D/Q/E/Shift) are released. New keyboard movement is accepted after that release boundary.
- `backend_switch_input` tests the shared reset and keyboard-gating logic. It does not replace physical iPhone touch testing.


## GPU rotating cube

- The application creates a procedural cube mesh with 24 face-local vertices and 12 triangles. Each face has its own outward normal and full UV square, allowing the existing GPU P3 checker texture and lighting shader to shade all six sides.
- GPU mode initializes its mesh buffers from this cube and submits a dedicated scene object whose X/Y/Z rotation is updated from the shared elapsed-time clock each frame.
- The existing Space animation pause control freezes that clock. CPU mesh instances and the CPU software-rendered demo remain unchanged.
- `primitive_meshes` checks the generated mesh structure; it does not execute the GPU pipeline or validate the visual result on Metal.


### Physical-device SSR verification — 2026-11-04

- The user confirmed that the supplied iPhone screenshot was captured with an IPA containing commit `2e2c73a` (the floor-only SSR receiver mask correction).
- The screenshot still shows a repeated/combed cyan reflection image below the cube, with strong horizontal/vertical banding and dark rectangular patches on the floor. The reflection remains visibly incorrect; do not describe SSR as fixed.
- This physical result demonstrates that restricting the reflective receiver to the known floor plane did not resolve the artifact. The earlier hypothesis that cube faces were being selected as reflection receivers is insufficient as the sole explanation.
- Keep the issue open. Next investigation should validate the camera-depth prepass encoding/format and sampling, screen-UV to clip/NDC conventions, inverse view-projection reconstruction, and the ray-depth intersection comparison against the exact projection/depth convention. Add temporary debug views for raw depth, reconstructed world position, ray UV, and hit mask before further tuning.
- The later source change that increases the SSR march density (32 to 64 steps, spacing 0.075 to 0.035) is a separate source-level change; this screenshot does not verify that later change unless the tested IPA is confirmed to contain it.
- CI/IPA packaging does not compile embedded MSL at runtime or validate the rendered image. Record physical-device validation separately from CI success.


### SSR depth precision fallback — 2026-11-04

- `createShadowMap()` prefers `R32_FLOAT`; when that format is unavailable, it uses `R8G8B8A8_UNORM` for both the camera-depth prepass and directional shadow map.
- The RGBA8 fallback now packs normalized depth into RGB at 24-bit precision rather than writing depth only to the 8-bit red channel. The packed-format flag is passed through the relevant GPU uniforms/varyings.
- The composite shader decodes packed depth for the SSR receiver depth, each ray-march sample, binary-refinement samples, and SSAO neighbors. The mesh fragment shader decodes the packed directional shadow-map samples. R32_FLOAT remains unmodified.
- The change addresses a confirmed precision weakness in the RGBA8 fallback, but the selected format on the target iPhone is not yet known. Embedded MSL runtime compilation and the visual reflection result still require physical-device verification.


## Embedded Metal source compilation in CI

The iOS job extracts the eight raw MSL strings embedded in `src/engine/GpuRenderer.cpp` with `scripts/extract_msl_shaders.py`, compiles each source to Metal IR with `xcrun -sdk iphoneos metal -c`, and links the outputs into a temporary `.metallib`. The application continues to pass embedded MSL strings to SDL_GPU at runtime; the temporary library is only a CI validation artifact and is not bundled into the IPA. Apple's command-line workflow for compiling MSL to IR and linking a Metal library is documented in [Building a shader library by precompiling source files](https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files). This CI step validates shader source compilation, not runtime pipeline creation, device-specific format support, or the rendered result.


## SSR runtime isolation toggle

- `GpuRenderer::renderFrame` accepts an `ssrEnabled` flag (defaulting to enabled for existing callers). The value is passed through `CompositeConstants.depthFlags.y`.
- When the flag is disabled, the fullscreen MSL composite skips the SSR receiver reconstruction and ray-march. SSAO still uses the same camera-depth prepass, and bloom/tone mapping still run.
- In GPU mode, the overlay presents an `SSR` button immediately left of the CPU backend switch. A tap toggles the flag; the button uses the normal blue color when enabled and a muted color when disabled. The main loop also maps keyboard **T** to the same toggle.
- `touch_overlay_hit_test` covers the SSR button center, exclusive right edge, separation from the backend button, and portrait layout.
- Use the toggle to compare the exact same camera/scene with SSR on and off. If the repeated floor pattern disappears only when SSR is off, that isolates the artifact to the SSR contribution; it does not by itself identify the faulty SSR sub-operation. This comparison still requires a new IPA and physical iPhone test.


## Overlay Metal shader compile correction

The iOS shader compile job found that the embedded `overlayVertexSource` concatenated `#include <metal_stdlib>` and `using namespace metal;` on one line. The source now separates them with a newline, restoring the Metal namespace directive required for unqualified math functions such as `cos`, `sin`, and `max`. Confirm the fix with a new iOS Actions run; do not treat the source edit alone as a successful Metal compile.

## SSR diagnostic log

The application creates `ssr.log` beside `startup.log` in the directory returned by `SDL_GetPrefPath("com.gasolineengine", "GasolineEngine")`. This uses the same platform-specific writable preference directory on iOS and desktop; it does not write into the read-only iOS app bundle. The file is recreated at launch. SSR toggle changes are recorded immediately, and while the GPU renderer is active a compact frame-state record is appended at most once per second with SSR enabled state, pixel dimensions, smoothed FPS, camera position, and GPU driver. GPU frame errors are recorded as well.

This first diagnostic version records CPU-visible renderer state only. It does **not** report per-pixel depth validity, ray-march iterations, ray hits/misses, or reflection weights because those values are calculated inside the GPU fragment shader and are not currently read back to the CPU. Do not interpret these log fields as GPU hit statistics. Reading shader counters back would require a separate GPU instrumentation/readback implementation.
