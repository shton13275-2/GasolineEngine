#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

#include "engine/Camera.hpp"
#include "engine/Mesh.hpp"
#include "engine/PpmImageLoader.hpp"
#include "engine/Scene.hpp"

namespace originalengine {

struct GpuMeshInstance {
    std::size_t meshIndex{0};
    const SceneObject* object{nullptr};
};

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
        const Camera& camera, const std::vector<GpuMeshInstance>& instances,
        bool showTouchControls = true, bool movementActive = false,
        float movementX = 0.0F, float movementY = 0.0F,
        float framesPerSecond = 0.0F);
    void shutdown() noexcept;

    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] const std::string& driverName() const noexcept { return driverName_; }

private:
    void setError(const char* operation);
    [[nodiscard]] bool createDepthTexture(Uint32 width, Uint32 height);
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
    SDL_GPUGraphicsPipeline* overlayPipeline_{nullptr};
    SDL_GPUBuffer* vertexBuffer_{nullptr};
    SDL_GPUBuffer* indexBuffer_{nullptr};
    SDL_GPUTexture* depthTexture_{nullptr};
    SDL_GPUTexture* colorTexture_{nullptr};
    SDL_GPUSampler* colorSampler_{nullptr};
    SDL_GPUTextureFormat depthFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
    Uint32 depthWidth_{0};
    Uint32 depthHeight_{0};
    struct MeshRange {
        Uint32 firstIndex{0};
        Uint32 indexCount{0};
    };
    std::vector<MeshRange> meshRanges_;
    std::string lastError_;
    std::string driverName_;
};

} // namespace originalengine
