#include "engine/SceneRenderItem.hpp"

#include <iostream>

int main() {
    originalengine::Mesh mesh;
    originalengine::Scene scene;
    if (!scene.addObject("SharedObject")) {
        std::cerr << "Could not create scene object.\n";
        return 1;
    }

    const originalengine::SceneObject* object = scene.findObject("SharedObject");
    if (object == nullptr) {
        std::cerr << "Could not find scene object.\n";
        return 1;
    }

    const originalengine::SceneRenderItem shared{
        &mesh, object, "assets/shared.ppm", 3, true, true
    };
    const originalengine::SceneRenderItem gpuOnly{
        &mesh, object, {}, 4, false, true
    };
    const originalengine::SceneRenderItem cpuOnly{
        &mesh, object, {}, 0, true, false
    };

    if (shared.mesh != &mesh || shared.object != object ||
        shared.texturePath != "assets/shared.ppm" || shared.gpuMeshIndex != 3 ||
        !shared.renderOnCpu || !shared.renderOnGpu) {
        std::cerr << "Shared render item did not preserve its bindings.\n";
        return 1;
    }
    if (gpuOnly.renderOnCpu || !gpuOnly.renderOnGpu || gpuOnly.gpuMeshIndex != 4) {
        std::cerr << "GPU-only render item flags are incorrect.\n";
        return 1;
    }
    if (!cpuOnly.renderOnCpu || cpuOnly.renderOnGpu) {
        std::cerr << "CPU-only render item flags are incorrect.\n";
        return 1;
    }

    std::cout << "Scene render item tests passed.\n";
    return 0;
}
