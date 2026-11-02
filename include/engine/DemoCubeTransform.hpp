#pragma once

#include "engine/Scene.hpp"

#include <cmath>

namespace originalengine {

// Shared transform for the demo cube so CPU and GPU backends use identical
// world-space position, scale, and animation at the same elapsed time.
[[nodiscard]] inline Transform makeDemoCubeTransform(double elapsedSeconds) noexcept {
    const float time = static_cast<float>(elapsedSeconds);
    Transform transform;
    transform.position = {0.0F, 0.0F, 4.2F};
    transform.rotation = {
        std::sin(time * 0.43F) * 0.28F,
        time * 0.65F,
        std::sin(time * 0.31F) * 0.12F
    };
    transform.scale = {1.0F, 1.0F, 1.0F};
    return transform;
}

} // namespace originalengine
