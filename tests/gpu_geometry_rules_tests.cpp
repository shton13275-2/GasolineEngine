#include "engine/GpuGeometryRules.hpp"

#include <array>
#include <iostream>

int main() {
    constexpr std::array<std::array<bool, 3>, 8> cases{{
        {{false, false, false}},
        {{false, false, true}},
        {{false, true, false}},
        {{false, true, true}},
        {{true, false, false}},
        {{true, false, true}},
        {{true, true, false}},
        {{true, true, true}},
    }};

    for (const auto& triangle : cases) {
        const bool expected = triangle[0] && triangle[1] && triangle[2];
        const bool actual = originalengine::gpu_detail::triangleHasCompleteTexCoords(
            triangle[0], triangle[1], triangle[2]);
        if (actual != expected) {
            std::cerr << "Unexpected texture eligibility for UV-presence combination.\n";
            return 1;
        }
    }

    std::cout << "GPU geometry rule tests passed.\n";
    return 0;
}
