#include "engine/Camera.hpp"
#include "engine/CollisionWorld.hpp"
#include "engine/Renderer3D.hpp"
#include "engine/GpuDeviceProbe.hpp"
#include "engine/GpuFrameRecovery.hpp"
#include "engine/GpuRenderer.hpp"
#include "engine/ObjMeshLoader.hpp"
#include "engine/PpmImageLoader.hpp"
#include "engine/Scene.hpp"
#include "engine/TouchOverlay.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string diagnosticLogPath;

void writeDiagnostic(const std::string& message) {
    std::cerr << "[OriginalEngine] " << message << '\n';
    if (!diagnosticLogPath.empty()) {
        std::ofstream log(diagnosticLogPath, std::ios::app);
        if (log) log << message << '\n';
    }
}

void initializeDiagnosticLog() {
    char* prefPath = SDL_GetPrefPath("com.gasolineengine", "GasolineEngine");
    if (prefPath != nullptr) {
        diagnosticLogPath = std::string(prefPath) + "startup.log";
        SDL_free(prefPath);
        std::ofstream log(diagnosticLogPath, std::ios::trunc);
        if (log) log << "Gasoline Engine startup diagnostics\n";
    }
}

void logSdlError(const char* operation) {
    writeDiagnostic(std::string(operation) + " failed: " + SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gasoline Engine startup error",
        (std::string(operation) + " failed. Details saved to startup.log.").c_str(), nullptr);
}

bool runGpuClearSmokeTest(SDL_Window* window) {
    constexpr SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_MSL;
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, false, nullptr);
    if (device == nullptr) {
        writeDiagnostic(std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError());
        return false;
    }

    bool success = false;
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        writeDiagnostic(std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError());
    } else {
        SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        if (commandBuffer == nullptr) {
            writeDiagnostic(std::string("SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        } else {
            SDL_GPUTexture* swapchainTexture = nullptr;
            Uint32 width = 0;
            Uint32 height = 0;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                    commandBuffer, window, &swapchainTexture, &width, &height)) {
                writeDiagnostic(std::string("SDL_WaitAndAcquireGPUSwapchainTexture failed: ") + SDL_GetError());
                (void)SDL_CancelGPUCommandBuffer(commandBuffer);
            } else if (swapchainTexture == nullptr || width == 0 || height == 0) {
                writeDiagnostic("GPU smoke test skipped drawing because the swapchain has no drawable texture");
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
                    writeDiagnostic(std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
                }
            } else {
                SDL_GPUColorTargetInfo colorTarget{};
                colorTarget.texture = swapchainTexture;
                colorTarget.clear_color = SDL_FColor{0.035F, 0.055F, 0.09F, 1.0F};
                colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
                colorTarget.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass* renderPass =
                    SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
                if (renderPass == nullptr) {
                    writeDiagnostic(std::string("SDL_BeginGPURenderPass failed: ") + SDL_GetError());
                    // A swapchain texture has already been acquired, so the
                    // command buffer must be submitted rather than cancelled.
                    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
                        writeDiagnostic(std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
                    }
                } else {
                    SDL_EndGPURenderPass(renderPass);
                    if (SDL_SubmitGPUCommandBuffer(commandBuffer)) {
                        writeDiagnostic("GPU smoke test passed: swapchain acquired, cleared, and submitted");
                        success = true;
                    } else {
                        writeDiagnostic(std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
                    }
                }
            }
        }
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
    SDL_DestroyGPUDevice(device);
    return success;
}

void drawTouchCircle(SDL_Renderer* renderer, float cx, float cy, float radius) {
    constexpr int segments = 32;
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) * 6.28318530718F / static_cast<float>(segments);
        const float a1 = static_cast<float>(i + 1) * 6.28318530718F / static_cast<float>(segments);
        (void)SDL_RenderLine(renderer, cx + std::cos(a0) * radius,
            cy + std::sin(a0) * radius, cx + std::cos(a1) * radius,
            cy + std::sin(a1) * radius);
    }
}

void drawTouchText(SDL_Renderer* renderer, float x, float y, const char* text) {
    using namespace originalengine::touch_overlay;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const std::string_view pattern = glyph(*cursor);
        int row = 0;
        int column = 0;
        for (const char pixel : pattern) {
            if (pixel == '/') { ++row; column = 0; continue; }
            if (pixel == '1') {
                SDL_FRect rect{
                    x + static_cast<float>(column) * fontPixelSize,
                    y + static_cast<float>(row) * fontPixelSize,
                    fontPixelSize, fontPixelSize
                };
                (void)SDL_RenderFillRect(renderer, &rect);
            }
            ++column;
        }
        x += fontAdvance;
    }
}


std::filesystem::path resolveAssetPath(const char* relativePath) {
    const std::filesystem::path relative(relativePath);
    // SDL3 returns a cached path string owned by SDL.
    const char* basePath = SDL_GetBasePath();
    if (basePath != nullptr) {
        const std::filesystem::path executablePath(basePath);
        const std::filesystem::path besideExecutable = executablePath / relative;
        std::error_code error;
        if (std::filesystem::is_regular_file(besideExecutable, error)) {
            return besideExecutable;
        }
    }

    std::error_code error;
    if (std::filesystem::is_regular_file(relative, error)) {
        return std::filesystem::absolute(relative, error);
    }
    std::filesystem::path fallback = relative;
    if (basePath != nullptr) {
        fallback = std::filesystem::path(basePath) / relative;
    }
    return fallback;
}

} // namespace

int main(int argc, char* argv[]) {
    const bool gpuSmokeTest = argc > 1 && std::string_view(argv[1]) == "--gpu-smoke-test";
    const bool smokeTest = argc > 1 && std::string_view(argv[1]) == "--smoke-test";
    initializeDiagnosticLog();
    writeDiagnostic("Stage: entering main");
    constexpr int windowWidth = 1280;
    constexpr int windowHeight = 720;

    writeDiagnostic("Stage: SDL_Init");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logSdlError("SDL_Init");
        return 1;
    }

    writeDiagnostic("Stage: SDL_Init succeeded");
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
    // The window must be Metal-capable for runtime SDL_GPU activation.
    windowFlags |= SDL_WINDOW_METAL;
#endif

    SDL_Window* window = SDL_CreateWindow(
        smokeTest ? "OriginalEngine - CI Smoke Test" : "OriginalEngine - 3D Camera Demo",
        windowWidth,
        windowHeight,
        windowFlags
    );

    if (window == nullptr) {
        logSdlError("SDL_CreateWindow");
        SDL_Quit();
        return 1;
    }

    writeDiagnostic("Stage: SDL_CreateWindow succeeded");
    if (gpuSmokeTest) {
        const bool passed = runGpuClearSmokeTest(window);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return passed ? 0 : 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        logSdlError("SDL_CreateRenderer");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    writeDiagnostic("Stage: SDL_CreateRenderer succeeded");
    if (!smokeTest) {
        SDL_SetRenderVSync(renderer, 1);
        SDL_SetWindowTitle(window,
            "Gasoline Engine | WASD move, Q/E up/down, Shift fast | R: reset camera | +/-: mouse sensitivity | Space: pause animation | F11: fullscreen | 1/2: toggle objects | F3: wireframe");
    }

    int exitCode = 0;
    std::unique_ptr<originalengine::Renderer3D> renderer3D;
    std::unique_ptr<originalengine::GpuRenderer> gpuRenderer;
    try {
        writeDiagnostic("Stage: creating Renderer3D");
        renderer3D = std::make_unique<originalengine::Renderer3D>(renderer);
        bool gpuMode = false;
        bool backendToggleRequested = false;
        writeDiagnostic("Stage: Renderer3D created");
        originalengine::GpuDeviceProbe gpuProbe;
        std::string gpuError;
        if (gpuProbe.initialize(&gpuError)) {
            writeDiagnostic("SDL_GPU available: driver=" + gpuProbe.driverName() +
                ", shaderFormats=" + std::to_string(
                    static_cast<unsigned int>(gpuProbe.shaderFormats())));
            // This is only a capability probe. Release the unclaimed GPU device
            // immediately so it does not reserve GPU resources while SDL_Renderer
            // continues to own and present the window.
            gpuProbe.shutdown();
        } else {
            writeDiagnostic("SDL_GPU unavailable; CPU renderer remains active: " + gpuError);
        }
        originalengine::Camera camera;
        originalengine::CollisionWorld collisionWorld;
        originalengine::Mesh demoMesh;
        std::string meshError;
        const std::filesystem::path meshPath = resolveAssetPath("assets/demo.obj");
        const std::filesystem::path texturePath = resolveAssetPath("assets/demo_texture.ppm");
        originalengine::RgbaImage demoTexture;
        std::string textureError;
        if (!originalengine::PpmImageLoader::loadFromFile(texturePath, demoTexture, &textureError)) {
            writeDiagnostic("Could not load GPU texture " + texturePath.string() + ": " + textureError);
        } else {
            writeDiagnostic("Loaded PPM texture: " + std::to_string(demoTexture.width) + "x" +
                            std::to_string(demoTexture.height));
        }
        writeDiagnostic("Stage: loading OBJ: " + meshPath.string());
        if (!originalengine::ObjMeshLoader::loadFromFile(meshPath.string(), demoMesh, &meshError)) {
            writeDiagnostic("Could not load mesh " + meshPath.string() + ": " + meshError);
            exitCode = 1;
        }
        writeDiagnostic("Stage: OBJ load completed");
        originalengine::Scene scene;
        originalengine::Transform demoTransform;
        demoTransform.position = {2.6F, -0.2F, 7.0F};
        demoTransform.rotation = {0.0F, 0.25F, 0.0F};
        demoTransform.scale = {0.9F, 0.9F, 0.9F};
        if (!scene.addObject("DemoPyramid", demoTransform)) {
            std::cerr << "[OriginalEngine] Could not create demo scene object.\n";
            exitCode = 1;
        }
        originalengine::Transform secondTransform = demoTransform;
        secondTransform.position.x = -2.6F;
        secondTransform.rotation.y = -0.35F;
        if (!scene.addObject("DemoPyramidLeft", secondTransform)) {
            std::cerr << "[OriginalEngine] Could not create the second demo scene object.\n";
            exitCode = 1;
        }

        // Resolve object pointers after all additions so vector reallocation cannot invalidate them.
        originalengine::SceneObject* demoObject = scene.findObject("DemoPyramid");
        originalengine::SceneObject* leftObject = scene.findObject("DemoPyramidLeft");
        if (demoObject == nullptr || leftObject == nullptr) {
            std::cerr << "[OriginalEngine] A demo scene object was not found.\n";
            exitCode = 1;
        }
        const std::vector<originalengine::MeshInstance> meshInstances{
            {&demoMesh, demoObject, texturePath.string()},
            {&demoMesh, leftObject, texturePath.string()}
        };
        const std::vector<const originalengine::SceneObject*> gpuSceneObjects{
            demoObject, leftObject
        };
        // Conservative static bounds around the demo cube.
        collisionWorld.addObstacle({-1.4F, -1.4F, 2.8F, 1.4F, 1.4F, 5.6F});
        bool running = true;
        bool mouseCaptured = false;
        bool wireframe = false;
        bool animationPaused = false;
        bool fullscreen = false;
        SDL_FingerID movementFinger = 0;
        SDL_FingerID lookFinger = 0;
        bool hasMovementFinger = false;
        bool hasLookFinger = false;
        float movementX = 0.0F;
        float movementY = 0.0F;
        float lookDeltaX = 0.0F;
        float lookDeltaY = 0.0F;
        Uint64 previousTicks = SDL_GetTicks();
        double elapsedSeconds = 0.0;
        float smoothedFps = 0.0F;

        writeDiagnostic("Stage: entering main loop");
        while (running && exitCode == 0) {
            int touchWidth = 0;
            int touchHeight = 0;
            (void)SDL_GetWindowSizeInPixels(window, &touchWidth, &touchHeight);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (!smokeTest && touchWidth > 0 && touchHeight > 0 &&
                    (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
                     event.type == SDL_EVENT_FINGER_UP)) {
                    if (event.type == SDL_EVENT_FINGER_DOWN) {
                        if (event.tfinger.x >= 0.78F && event.tfinger.y <= 0.16F) {
                            backendToggleRequested = true;
                        } else if (event.tfinger.x < 0.5F && !hasMovementFinger) {
                            movementFinger = event.tfinger.fingerID;
                            hasMovementFinger = true;
                            movementX = event.tfinger.x;
                            movementY = event.tfinger.y;
                        } else if (event.tfinger.x >= 0.5F && !hasLookFinger) {
                            lookFinger = event.tfinger.fingerID;
                            hasLookFinger = true;
                        }
                    } else if (event.type == SDL_EVENT_FINGER_MOTION) {
                        if (hasMovementFinger && event.tfinger.fingerID == movementFinger) {
                            movementX = event.tfinger.x;
                            movementY = event.tfinger.y;
                        } else if (hasLookFinger && event.tfinger.fingerID == lookFinger) {
                            lookDeltaX += event.tfinger.dx * static_cast<float>(touchWidth);
                            lookDeltaY += event.tfinger.dy * static_cast<float>(touchHeight);
                        }
                    } else {
                        if (hasMovementFinger && event.tfinger.fingerID == movementFinger) {
                            hasMovementFinger = false;
                            movementX = 0.0F;
                            movementY = 0.0F;
                        }
                        if (hasLookFinger && event.tfinger.fingerID == lookFinger) {
                            hasLookFinger = false;
                        }
                    }
                } else if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_G) {
                    backendToggleRequested = true;
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_R) {
                    camera.reset();
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_MINUS) {
                    camera.adjustMouseSensitivity(-0.0005F);
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_EQUALS) {
                    camera.adjustMouseSensitivity(0.0005F);
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_SPACE) {
                    animationPaused = !animationPaused;
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_F11) {
                    const bool requestedFullscreen = !fullscreen;
                    if (SDL_SetWindowFullscreen(window, requestedFullscreen)) {
                        fullscreen = requestedFullscreen;
                    } else {
                        logSdlError("SDL_SetWindowFullscreen");
                    }
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_F3) {
                    wireframe = !wireframe;
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_1 &&
                           demoObject != nullptr) {
                    demoObject->visible = !demoObject->visible;
                } else if (!smokeTest && event.type == SDL_EVENT_KEY_DOWN &&
                           !event.key.repeat && event.key.key == SDLK_2 &&
                           leftObject != nullptr) {
                    leftObject->visible = !leftObject->visible;
                } else if (event.type == SDL_EVENT_KEY_DOWN &&
                           event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                    if (mouseCaptured) {
                        SDL_SetWindowRelativeMouseMode(window, false);
                        mouseCaptured = false;
                    } else {
                        running = false;
                    }
                } else if (!smokeTest &&
                           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                           event.button.button == SDL_BUTTON_RIGHT) {
                    mouseCaptured = !mouseCaptured;
                    if (!SDL_SetWindowRelativeMouseMode(window, mouseCaptured)) {
                        logSdlError("SDL_SetWindowRelativeMouseMode");
                        mouseCaptured = false;
                    }
                } else if (!smokeTest && mouseCaptured &&
                           event.type == SDL_EVENT_MOUSE_MOTION) {
                    camera.rotate(static_cast<float>(event.motion.xrel),
                                  static_cast<float>(event.motion.yrel));
                }
            }

            if (!running) {
                break;
            }

            if (backendToggleRequested) {
                backendToggleRequested = false;
                if (!gpuMode) {
                    renderer3D.reset();
                    SDL_DestroyRenderer(renderer);
                    renderer = nullptr;
                    gpuRenderer = std::make_unique<originalengine::GpuRenderer>();
                    if (gpuRenderer->initialize(window, demoMesh, demoTexture)) {
                        gpuMode = true;
                        writeDiagnostic("GPU renderer active: " + gpuRenderer->driverName());
                        SDL_SetWindowTitle(window, "Gasoline Engine | GPU preview | G: switch to CPU");
                    } else {
                        writeDiagnostic("GPU renderer initialization failed: " + gpuRenderer->lastError());
                        gpuRenderer.reset();
                        renderer = SDL_CreateRenderer(window, nullptr);
                        if (renderer != nullptr) {
                            renderer3D = std::make_unique<originalengine::Renderer3D>(renderer);
                            writeDiagnostic("GPU activation failed; CPU renderer restored");
                        } else {
                            logSdlError("SDL_CreateRenderer after GPU activation failure");
                            exitCode = 1;
                        }
                    }
                } else {
                    gpuRenderer.reset();
                    gpuMode = false;
                    renderer = SDL_CreateRenderer(window, nullptr);
                    if (renderer != nullptr) {
                        (void)SDL_SetRenderVSync(renderer, 1);
                        renderer3D = std::make_unique<originalengine::Renderer3D>(renderer);
                        SDL_SetWindowTitle(window, "Gasoline Engine | G: switch to GPU");
                        writeDiagnostic("CPU renderer restored");
                    } else {
                        logSdlError("SDL_CreateRenderer after GPU mode");
                        exitCode = 1;
                    }
                }
            }
            if (exitCode != 0) break;

            const Uint64 currentTicks = SDL_GetTicks();
            const float frameSeconds =
                static_cast<float>(currentTicks - previousTicks) / 1000.0F;
            const float deltaSeconds = std::min(frameSeconds, 0.1F);
            previousTicks = currentTicks;
            if (frameSeconds > 0.0001F) {
                const float instantaneousFps = 1.0F / frameSeconds;
                smoothedFps = smoothedFps <= 0.0F
                    ? instantaneousFps
                    : smoothedFps * 0.88F + instantaneousFps * 0.12F;
            }
            if (!animationPaused) {
                elapsedSeconds += static_cast<double>(deltaSeconds);
            }

            if (!smokeTest) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                float forward = static_cast<float>(keys[SDL_SCANCODE_W]) -
                                      static_cast<float>(keys[SDL_SCANCODE_S]);
                float right = static_cast<float>(keys[SDL_SCANCODE_D]) -
                                    static_cast<float>(keys[SDL_SCANCODE_A]);
                const float vertical = static_cast<float>(keys[SDL_SCANCODE_E]) -
                                       static_cast<float>(keys[SDL_SCANCODE_Q]);
                const bool fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                if (hasMovementFinger && touchWidth > 0 && touchHeight > 0) {
                    const float anchorX = static_cast<float>(touchWidth) * 0.20F;
                    const float anchorY = static_cast<float>(touchHeight) * 0.78F;
                    const float radius = static_cast<float>(std::min(touchWidth, touchHeight)) * 0.14F;
                    right = std::clamp((movementX * static_cast<float>(touchWidth) - anchorX) / radius, -1.0F, 1.0F);
                    forward = std::clamp(-(movementY * static_cast<float>(touchHeight) - anchorY) / radius, -1.0F, 1.0F);
                }
                camera.rotate(lookDeltaX, lookDeltaY);
                lookDeltaX = 0.0F;
                lookDeltaY = 0.0F;
                camera.move(forward, right, vertical, deltaSeconds, fast, &collisionWorld);
            }

            if (gpuMode) {
                std::string gpuFailure = "renderer unavailable";
                const auto recovery = originalengine::runGpuFrameWithCpuFallback(
                    [&]() {
                        if (gpuRenderer == nullptr) return false;
                        const bool rendered = gpuRenderer->renderFrame(
                            camera, gpuSceneObjects, !smokeTest, hasMovementFinger,
                            movementX, movementY, smoothedFps);
                        if (!rendered) gpuFailure = gpuRenderer->lastError();
                        return rendered;
                    },
                    [&]() {
                        // Destroying GpuRenderer releases its window claim before
                        // SDL_Renderer attempts to take ownership again.
                        gpuRenderer.reset();
                        gpuMode = false;
                    },
                    [&]() {
                        renderer = SDL_CreateRenderer(window, nullptr);
                        if (renderer == nullptr) return false;
                        (void)SDL_SetRenderVSync(renderer, 1);
                        renderer3D = std::make_unique<originalengine::Renderer3D>(renderer);
                        return true;
                    });

                if (recovery == originalengine::GpuFrameRecoveryResult::GpuRendered) {
                    continue;
                }
                if (recovery == originalengine::GpuFrameRecoveryResult::Fatal) {
                    writeDiagnostic("GPU frame failed: " + gpuFailure);
                    logSdlError("SDL_CreateRenderer after GPU frame failure");
                    exitCode = 1;
                    break;
                }

                SDL_SetWindowTitle(window, "Gasoline Engine | G: switch to GPU");
                writeDiagnostic("GPU frame failed: " + gpuFailure);
                writeDiagnostic("GPU frame failed; CPU renderer restored");
            }

            if (exitCode != 0) break;

            int width = 0;
            int height = 0;
            if (!SDL_GetRenderOutputSize(renderer, &width, &height)) {
                logSdlError("SDL_GetRenderOutputSize");
                exitCode = 1;
                break;
            }

            if (!renderer3D->render(width, height, elapsedSeconds, camera,
                                  meshInstances, wireframe)) {
                writeDiagnostic("Rendering a frame failed");
                exitCode = 1;
                break;
            }

            if (!smokeTest) {
                using namespace originalengine::touch_overlay;
                const float minDimension = static_cast<float>(std::min(width, height));
                const float joyX = static_cast<float>(width) * movementCenterX;
                const float joyY = static_cast<float>(height) * controlCenterY;
                const float joyRadius = minDimension * movementRadius;
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

                SDL_SetRenderDrawColor(renderer, ringColor.r, ringColor.g, ringColor.b, ringColor.a);
                drawTouchCircle(renderer, joyX, joyY, joyRadius);
                drawTouchCircle(renderer, joyX, joyY, joyRadius * movementInnerRadius);
                drawTouchCircle(renderer, joyX, joyY, joyRadius * knobRadius);
                SDL_SetRenderDrawColor(renderer, guideColor.r, guideColor.g, guideColor.b, guideColor.a);
                (void)SDL_RenderLine(renderer, joyX - joyRadius * movementGuideRadius, joyY,
                    joyX + joyRadius * movementGuideRadius, joyY);
                (void)SDL_RenderLine(renderer, joyX, joyY - joyRadius * movementGuideRadius,
                    joyX, joyY + joyRadius * movementGuideRadius);
                const float knobX = hasMovementFinger
                    ? joyX + std::clamp(movementX * static_cast<float>(width) - joyX,
                                        -joyRadius * movementGuideRadius, joyRadius * movementGuideRadius)
                    : joyX;
                const float knobY = hasMovementFinger
                    ? joyY + std::clamp(movementY * static_cast<float>(height) - joyY,
                                        -joyRadius * movementGuideRadius, joyRadius * movementGuideRadius)
                    : joyY;
                SDL_SetRenderDrawColor(renderer, knobColor.r, knobColor.g, knobColor.b, knobColor.a);
                drawTouchCircle(renderer, knobX, knobY, joyRadius * knobRadius);

                const float lookX = static_cast<float>(width) * lookCenterX;
                const float lookY = static_cast<float>(height) * controlCenterY;
                const float lookRadiusValue = minDimension * lookRadius;
                SDL_SetRenderDrawColor(renderer, ringColor.r, ringColor.g, ringColor.b, ringColor.a);
                drawTouchCircle(renderer, lookX, lookY, lookRadiusValue);
                drawTouchCircle(renderer, lookX, lookY, lookRadiusValue * lookInnerRadius);
                SDL_SetRenderDrawColor(renderer, guideColor.r, guideColor.g, guideColor.b, guideColor.a);
                (void)SDL_RenderLine(renderer, lookX - lookRadiusValue * lookGuideRadius, lookY,
                    lookX + lookRadiusValue * lookGuideRadius, lookY);
                (void)SDL_RenderLine(renderer, lookX, lookY - lookRadiusValue * lookGuideRadius,
                    lookX, lookY + lookRadiusValue * lookGuideRadius);

                SDL_SetRenderDrawColor(renderer, textColor.r, textColor.g, textColor.b, textColor.a);
                const std::string fpsLabel = "FPS:" +
                    std::to_string(static_cast<int>(std::round(smoothedFps)));
                drawTouchText(renderer, fpsX, fpsY, fpsLabel.c_str());

                SDL_FRect backendButton{
                    static_cast<float>(width) - backendButtonRight - backendButtonWidth,
                    backendButtonY, backendButtonWidth, backendButtonHeight
                };
                SDL_SetRenderDrawColor(renderer, buttonColor.r, buttonColor.g, buttonColor.b, buttonColor.a);
                (void)SDL_RenderFillRect(renderer, &backendButton);
                SDL_SetRenderDrawColor(renderer, textColor.r, textColor.g, textColor.b, textColor.a);
                drawTouchText(renderer, backendButton.x + 33.0F,
                    backendButton.y + (backendButtonHeight - fontHeight) * 0.5F, "GPU");
            }

            // Present exactly once per frame, after the 2D touch controls.
            (void)SDL_RenderPresent(renderer);

            if (smokeTest) {
                writeDiagnostic("Smoke test passed: one frame rendered");
                running = false;
            }
        }
    } catch (const std::exception& error) {
        writeDiagnostic(std::string("Exception: ") + error.what());
        exitCode = 1;
    }

    writeDiagnostic(std::string("Stage: shutting down with exit code ") + std::to_string(exitCode));
    renderer3D.reset();
    gpuRenderer.reset();
    if (renderer != nullptr) SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
