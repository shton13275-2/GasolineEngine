#pragma once

#include <SDL3/SDL.h>

#include <string_view>

namespace originalengine::gpu_render_diagnostics {

constexpr std::string_view depthColorFormatName(
    SDL_GPUTextureFormat format, bool packedDepth24) noexcept {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
        return "R32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return packedDepth24 ? "R8G8B8A8_UNORM_PACKED24" : "R8G8B8A8_UNORM";
    default:
        return "UNKNOWN";
    }
}

template <typename IsSupported>
SDL_GPUTextureFormat selectDepthStencilFormat(IsSupported isSupported) {
    constexpr SDL_GPUTextureFormat candidates[]{
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM
    };
    for (const SDL_GPUTextureFormat format : candidates) {
        if (isSupported(format)) {
            return format;
        }
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

constexpr std::string_view depthStencilFormatName(
    SDL_GPUTextureFormat format) noexcept {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
        return "D32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
        return "D32_FLOAT_S8_UINT";
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
        return "D24_UNORM";
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return "D16_UNORM";
    default:
        return "UNKNOWN";
    }
}

constexpr std::string_view sceneColorFormatName(
    SDL_GPUTextureFormat format) noexcept {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        return "R16G16B16A16_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        return "R8G8B8A8_UNORM_SRGB";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
        return "B8G8R8A8_UNORM_SRGB";
    case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
        return "R10G10B10A2_UNORM";
    default:
        return "UNKNOWN";
    }
}

constexpr std::string_view sampleCountName(SDL_GPUSampleCount sampleCount) noexcept {
    switch (sampleCount) {
    case SDL_GPU_SAMPLECOUNT_1:
        return "1x";
    case SDL_GPU_SAMPLECOUNT_2:
        return "2x";
    case SDL_GPU_SAMPLECOUNT_4:
        return "4x";
    case SDL_GPU_SAMPLECOUNT_8:
        return "8x";
    default:
        return "UNKNOWN";
    }
}

} // namespace originalengine::gpu_render_diagnostics
