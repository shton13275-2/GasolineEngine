#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

#include "engine/Camera.hpp"
#include "engine/Mesh.hpp"
#include "engine/PpmImageLoader.hpp"
#include "engine/Scene.hpp"
#include "engine/SceneRenderItem.hpp"

namespace originalengine {

// SDL_GPU renderer for indexed triangle meshes and the shared scene camera.
// Mesh data is uploaded once during initialization; per-object MVP uniforms are
// pushed while recording each frame.
class GpuRenderer {
public:
    GpuRenderer() = default;
    ~GpuRenderer();

    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    [[nodiscard]] bool initialize(SDL_Window* window, const std::vector<const Mesh*>& meshes, const RgbaImage& texture);
    [[nodiscard]] bool renderFrame(
        const Camera& camera, const std::vector<SceneRenderItem>& renderItems,
        bool showTouchControls = true, bool movementActive = false,
        float movementX = 0.0F, float movementY = 0.0F,
        float framesPerSecond = 0.0F, bool wireframe = false);
    void shutdown() noexcept;

    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] const std::string& driverName() const noexcept { return driverName_; }

private:
    void setError(const char* operation);
    [[nodiscard]] bool createDepthTexture(Uint32 width, Uint32 height);
    [[nodiscard]] bool createShadowMap();
    void drawOverlayRect(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
                         float x, float y, float width, float height,
                         Uint32 viewportWidth, Uint32 viewportHeight,
                         const float color[4]);
    void drawOverlayCircle(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
                           float x, float y, float radiusX, float radiusY,
                           Uint32 viewportWidth, Uint32 viewportHeight,
                           const float color[4]);
    void drawOverlay(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
                     Uint32 width, Uint32 height, bool movementActive,
                     float movementX, float movementY, float framesPerSecond);
    void drawOverlayText(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
                         float x, float y, const char* text, Uint32 viewportWidth,
                         Uint32 viewportHeight, const float color[4]);

    SDL_Window* window_{nullptr};
    SDL_GPUDevice* device_{nullptr};
    SDL_GPUGraphicsPipeline* pipeline_{nullptr};
    SDL_GPUGraphicsPipeline* wireframePipeline_{nullptr};
    SDL_GPUGraphicsPipeline* overlayPipeline_{nullptr};
    SDL_GPUGraphicsPipeline* bloomPipeline_{nullptr};
    SDL_GPUGraphicsPipeline* shadowPipeline_{nullptr};
    SDL_GPUTexture* shadowColorTexture_{nullptr};
    SDL_GPUTexture* shadowDepthTexture_{nullptr};
    SDL_GPUTexture* ssaoDepthTexture_{nullptr};
    SDL_GPUTexture* ssaoDepthAttachmentTexture_{nullptr};
    SDL_GPUTextureFormat shadowColorFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
    bool shadowDepthPacked_{false};
    SDL_GPUSampler* shadowSampler_{nullptr};
    static constexpr Uint32 shadowMapSize_ = 1024;
    SDL_GPUTexture* sceneColorTexture_{nullptr};
    SDL_GPUSampler* bloomSampler_{nullptr};
    SDL_GPUBuffer* vertexBuffer_{nullptr};
    SDL_GPUBuffer* indexBuffer_{nullptr};
    SDL_GPUTexture* depthTexture_{nullptr};
    SDL_GPUTexture* multisampleColorTexture_{nullptr};
    SDL_GPUTexture* colorTexture_{nullptr};
    SDL_GPUSampler* colorSampler_{nullptr};
    SDL_GPUTextureFormat depthFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
    SDL_GPUTextureFormat sceneColorFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
    bool shadowMappingEnabled_{false};
    Uint32 depthWidth_{0};
    Uint32 depthHeight_{0};
    SDL_GPUSampleCount sampleCount_{SDL_GPU_SAMPLECOUNT_1};
    struct MeshRange {
        Uint32 firstIndex{0};
        Uint32 indexCount{0};
    };
    std::vector<MeshRange> meshRanges_;
    std::string lastError_;
    std::string driverName_;
};

} // namespace originalengine
