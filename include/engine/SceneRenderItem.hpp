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
};

} // namespace originalengine
