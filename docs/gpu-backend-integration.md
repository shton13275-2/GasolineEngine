# SDL GPU backend integration notes

This document describes the implementation on `ios/initial-support`, not an unimplemented integration plan.

## Current implementation

- `GpuRenderer` is declared in `include/engine/GpuRenderer.hpp` and implemented in `src/engine/GpuRenderer.cpp`.
- `src/main.cpp` owns the CPU/GPU backend transition. Before GPU initialization it destroys the active `SDL_Renderer`; if GPU initialization fails, it destroys the partial GPU renderer and attempts to recreate the CPU renderer.
- `GpuRenderer::initialize` creates an SDL_GPU device requesting `SDL_GPU_SHADERFORMAT_MSL`, claims the window, creates the graphics pipelines and depth attachment, uploads mesh vertex/index buffers, and creates the texture/sampler.
- Mesh upload converts the loaded OBJ triangles to indexed GPU geometry. The current demo uses the existing mesh and visible scene-object transforms.
- UV presence is carried as a dedicated vertex attribute instead of using a negative UV sentinel. Negative and out-of-range OBJ UV values therefore remain valid inputs to the repeat-addressed texture sampler.
- Per-object constants include model, MVP, inverse-transpose normal matrix, and camera position. OBJ vertex normals are used when valid; missing normals use generated flat face normals.
- The mesh pass uses depth testing/writes and a depth-stencil texture. The depth texture is recreated when the swapchain dimensions change.
- The GPU fragment shader samples the P3 PPM-derived RGBA texture and applies ambient, directional diffuse, and Blinn-Phong specular lighting.
- A GPU overlay pass draws the touch-control guide, movement knob, FPS text, and CPU backend switch control. It does not use the CPU `SDL_Renderer` while the window is claimed by SDL_GPU.
- The texture path requests a full mip chain when the device reports support for the required RGBA8 sampler + color-target usage. Otherwise it retains a single-level texture. Mipmap generation and sampling are conditional on that capability.

## Backend switching and failure behavior

1. CPU-to-GPU switching stops CPU rendering, destroys the active `SDL_Renderer`, then initializes `GpuRenderer` and claims the window.
2. If GPU initialization fails, the partial GPU resources are released and `SDL_CreateRenderer` is attempted to restore CPU rendering.
3. GPU-to-CPU switching destroys `GpuRenderer`, which releases the window and GPU resources, then creates a new `SDL_Renderer`.
4. A GPU frame-rendering failure is currently treated as a fatal application error in `src/main.cpp`; automatic CPU fallback is implemented for initialization failure, not for a later frame failure.

Keyboard input **G** requests a backend switch. The touch interface also has a top-right switch hit region.

## Shader and platform constraints

- Runtime mesh and overlay shader sources are embedded as MSL strings in `src/engine/GpuRenderer.cpp`.
- `shaders/Metal/Basic3D.*.metal` are bundled source assets; the current `GpuRenderer` creates its runtime shaders from the embedded strings rather than loading those files.
- The current GPU device creation explicitly requests MSL. Do not infer Windows GPU runtime compatibility from a successful Windows compile.
- The iOS workflow builds an arm64 iOS Device app bundle and packages it as an unsigned IPA. CI build/package success does not prove Metal shader compilation or rendering on a physical iPhone.

## Not implemented or not verified

- MTL material parsing and per-material texture selection.
- GPU PNG/JPEG/BMP texture loading; the GPU texture input currently comes from the P3 PPM loader.
- GPU wireframe rendering and a shared CPU/GPU renderer interface.
- Physical iPhone verification of Metal shader compilation, texture sampling, lighting, touch interaction, and repeated backend switching.
- Long-running GPU resource lifecycle and leak testing.
- Automatic CPU fallback after a GPU frame-rendering failure.

## Verification guidance

- The Windows workflow configures and builds the project, runs the application smoke test, and executes CTest. It does not establish that the MSL GPU backend runs on Windows.
- The iOS workflow configures an iOS Device arm64 target, builds the unsigned app, packages an IPA, and checks the archive contents. It does not run the app on a physical device.
- Keep CI build success, IPA packaging success, physical-device startup, and successful Metal rendering as separate verification results.
- The standalone `--gpu-smoke-test` path checks SDL_GPU device/window/swapchain submission only; it is not a mesh-rendering test and is not part of the current build workflow.
