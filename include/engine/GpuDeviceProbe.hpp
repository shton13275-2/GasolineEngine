#pragma once

#include <SDL3/SDL_gpu.h>

#include <string>

namespace originalengine {

// Creates an unclaimed SDL_GPU device to report driver and shader capabilities
// without changing window ownership. GpuRenderer claims the window only when
// the application requests a CPU/GPU backend switch.
class GpuDeviceProbe {
public:
    GpuDeviceProbe() = default;
    ~GpuDeviceProbe();

    GpuDeviceProbe(const GpuDeviceProbe&) = delete;
    GpuDeviceProbe& operator=(const GpuDeviceProbe&) = delete;

    bool initialize(std::string* error = nullptr);
    void shutdown();

    [[nodiscard]] bool available() const noexcept { return device_ != nullptr; }
    [[nodiscard]] const std::string& driverName() const noexcept { return driverName_; }
    [[nodiscard]] SDL_GPUShaderFormat shaderFormats() const noexcept { return shaderFormats_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormats_ = SDL_GPU_SHADERFORMAT_INVALID;
    std::string driverName_;
};

} // namespace originalengine
