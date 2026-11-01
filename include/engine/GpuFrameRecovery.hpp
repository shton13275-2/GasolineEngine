#pragma once

namespace originalengine {

enum class GpuFrameRecoveryResult {
    GpuRendered,
    CpuFallbackReady,
    Fatal
};

// Runs one GPU frame and, on failure, releases GPU ownership before asking
// the caller to recreate its CPU renderer. Callbacks keep platform resources
// owned by the application while making the recovery order testable.
template <typename RenderGpu, typename ReleaseGpu, typename CreateCpu>
[[nodiscard]] GpuFrameRecoveryResult runGpuFrameWithCpuFallback(
    RenderGpu&& renderGpu,
    ReleaseGpu&& releaseGpu,
    CreateCpu&& createCpu) {
    if (renderGpu()) {
        return GpuFrameRecoveryResult::GpuRendered;
    }

    releaseGpu();
    return createCpu()
        ? GpuFrameRecoveryResult::CpuFallbackReady
        : GpuFrameRecoveryResult::Fatal;
}

} // namespace originalengine
