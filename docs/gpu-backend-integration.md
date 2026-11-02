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
- The GPU fragment shader samples the P3 PPM-derived RGBA texture and applies ambient, directional diffuse, and Blinn-Phong specular lighting.
- A GPU overlay pass draws touch-control guides, movement knob, FPS text, and CPU backend switch control. It does not use the CPU `SDL_Renderer` while the window is claimed by SDL_GPU.
- The backend-switch touch hit region is computed from the same shared pixel rectangle constants used by CPU and GPU overlay drawing (`touch_overlay::containsBackendButton`).
- The texture path requests a full mip chain when the device reports support for the required RGBA8 sampler + color-target usage. Otherwise it retains a single-level texture.
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

- `gpu_geometry_rules` tests all eight boolean combinations for UV presence across the three triangle corners. It verifies the shared policy predicate used by `buildGpuGeometry`; it does not execute SDL_GPU geometry upload or a shader.
- `touch_overlay_hit_test` verifies that normalized touch coordinates are tested against the same backend-switch rectangle drawn by both renderers, including landscape/portrait dimensions and exclusive right/bottom edges.
- `ppm_image_loader` tests decoded RGBA8 data validation, including incorrect byte counts and dimensions exceeding the supported upload size.
- GPU frame-failure CPU fallback uses `runGpuFrameWithCpuFallback` from `include/engine/GpuFrameRecovery.hpp`. `gpu_frame_recovery` tests GPU success, successful CPU fallback, fatal CPU fallback, and callback ordering. It does not inject an SDL_GPU failure or verify real SDL window-claim/resource transitions.
- MTL material parsing and per-material texture selection are not implemented.
- GPU PNG/JPEG/BMP texture loading is not implemented; the GPU texture input currently comes from the P3 PPM loader.
- GPU wireframe rendering and a shared CPU/GPU renderer interface are not implemented.
- Physical iPhone verification of Metal shader compilation, texture sampling, lighting, touch interaction, and repeated backend switching remains outstanding.
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
