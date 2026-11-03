#pragma once

#include <cstddef>
#include <string>

#include "engine/Mesh.hpp"
#include "engine/Scene.hpp"

namespace originalengine {

// A single render-item list is shared by the CPU and GPU backends. Flags keep
// backend-specific demo geometry explicit without duplicating object bindings.
struct SceneRenderItem {
    const Mesh* mesh{nullptr};
    const SceneObject* object{nullptr};
    std::string texturePath{};
    std::size_t gpuMeshIndex{0};
    bool renderOnCpu{true};
    bool renderOnGpu{true};

    // Metallic-roughness material inputs used by the GPU PBR shader.
    // Values are clamped to [0, 1] before being sent to the GPU.
    float metallic{0.0F};
    float roughness{0.55F};
};

} // namespace originalengine
