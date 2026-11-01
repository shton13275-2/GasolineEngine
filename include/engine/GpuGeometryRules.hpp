#pragma once

namespace originalengine::gpu_detail {

// A triangle can use the texture only when every corner has valid UV coordinates.
// Keep this decision primitive-wide because vertex varyings are interpolated.
[[nodiscard]] constexpr bool triangleHasCompleteTexCoords(
    bool first, bool second, bool third) noexcept {
    return first && second && third;
}

} // namespace originalengine::gpu_detail
