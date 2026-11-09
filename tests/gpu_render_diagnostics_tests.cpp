#include "engine/GpuRenderDiagnostics.hpp"

#include <iostream>
#include <string_view>

namespace {

bool expect(std::string_view actual, std::string_view expected, const char* message) {
    if (actual == expected) return true;
    std::cerr << message << " Expected '" << expected << "', got '" << actual << "'.\n";
    return false;
}

} // namespace

int main() {
    using namespace originalengine::gpu_render_diagnostics;
    bool allPassed = true;

    allPassed &= expect(
        depthColorFormatName(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, false),
        "R32_FLOAT",
        "R32 depth encoding label is incorrect.");
    allPassed &= expect(
        depthColorFormatName(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true),
        "R8G8B8A8_UNORM_PACKED24",
        "Packed 24-bit RGBA8 depth label is incorrect.");
    allPassed &= expect(
        depthColorFormatName(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false),
        "R8G8B8A8_UNORM",
        "Unpacked RGBA8 label is incorrect.");
    allPassed &= expect(
        depthColorFormatName(SDL_GPU_TEXTUREFORMAT_INVALID, false),
        "UNKNOWN",
        "Unknown depth color format must be identified.");

    allPassed &= expect(
        depthStencilFormatName(SDL_GPU_TEXTUREFORMAT_D32_FLOAT),
        "D32_FLOAT",
        "D32 depth-stencil label is incorrect.");
    allPassed &= expect(
        depthStencilFormatName(SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT),
        "D24_UNORM_S8_UINT",
        "D24/S8 depth-stencil label is incorrect.");
    allPassed &= expect(
        depthStencilFormatName(SDL_GPU_TEXTUREFORMAT_INVALID),
        "UNKNOWN",
        "Unknown depth-stencil format must be identified.");

    allPassed &= expect(
        sceneColorFormatName(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT),
        "R16G16B16A16_FLOAT",
        "HDR scene color label is incorrect.");
    allPassed &= expect(
        sceneColorFormatName(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM),
        "B8G8R8A8_UNORM",
        "BGRA8 scene color label is incorrect.");
    allPassed &= expect(
        sceneColorFormatName(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM),
        "R8G8B8A8_UNORM",
        "RGBA8 scene color label is incorrect.");
    allPassed &= expect(
        sceneColorFormatName(SDL_GPU_TEXTUREFORMAT_INVALID),
        "UNKNOWN",
        "Unknown scene color format must be identified.");

    allPassed &= expect(sampleCountName(SDL_GPU_SAMPLECOUNT_1), "1x",
                        "Single-sample label is incorrect.");
    allPassed &= expect(sampleCountName(SDL_GPU_SAMPLECOUNT_2), "2x",
                        "2x MSAA label is incorrect.");
    allPassed &= expect(sampleCountName(SDL_GPU_SAMPLECOUNT_4), "4x",
                        "4x MSAA label is incorrect.");
    allPassed &= expect(sampleCountName(SDL_GPU_SAMPLECOUNT_8), "8x",
                        "8x MSAA label is incorrect.");
    allPassed &= expect(
        sampleCountName(static_cast<SDL_GPUSampleCount>(99)),
        "UNKNOWN",
        "Unknown sample count must be identified.");

    if (!allPassed) return 1;
    std::cout << "GPU render diagnostics label tests passed.\n";
    return 0;
}
