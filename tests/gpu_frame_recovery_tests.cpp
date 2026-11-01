#include "engine/GpuFrameRecovery.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    bool allPassed = true;

    {
        std::vector<std::string> calls;
        const auto result = originalengine::runGpuFrameWithCpuFallback(
            [&]() { calls.emplace_back("render"); return true; },
            [&]() { calls.emplace_back("release"); },
            [&]() { calls.emplace_back("create"); return true; });

        allPassed &= expect(
            result == originalengine::GpuFrameRecoveryResult::GpuRendered,
            "GPU success must report GpuRendered.");
        allPassed &= expect(
            calls == std::vector<std::string>{"render"},
            "GPU success must not release GPU resources or create a CPU renderer.");
    }

    {
        std::vector<std::string> calls;
        const auto result = originalengine::runGpuFrameWithCpuFallback(
            [&]() { calls.emplace_back("render"); return false; },
            [&]() { calls.emplace_back("release"); },
            [&]() { calls.emplace_back("create"); return true; });

        allPassed &= expect(
            result == originalengine::GpuFrameRecoveryResult::CpuFallbackReady,
            "Successful CPU recreation must report CpuFallbackReady.");
        allPassed &= expect(
            calls == std::vector<std::string>{"render", "release", "create"},
            "GPU release must happen before CPU renderer creation.");
    }

    {
        std::vector<std::string> calls;
        const auto result = originalengine::runGpuFrameWithCpuFallback(
            [&]() { calls.emplace_back("render"); return false; },
            [&]() { calls.emplace_back("release"); },
            [&]() { calls.emplace_back("create"); return false; });

        allPassed &= expect(
            result == originalengine::GpuFrameRecoveryResult::Fatal,
            "Failed CPU recreation must report Fatal.");
        allPassed &= expect(
            calls == std::vector<std::string>{"render", "release", "create"},
            "Fatal recovery must still release GPU resources before CPU creation.");
    }

    if (!allPassed) return 1;
    std::cout << "GPU frame recovery tests passed.\n";
    return 0;
}
