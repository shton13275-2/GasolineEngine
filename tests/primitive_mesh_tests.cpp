#include "engine/PrimitiveMeshes.hpp"
#include "engine/DemoCubeTransform.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

int main() {
    const originalengine::Mesh cube = originalengine::makeTexturedCubeMesh();
    assert(cube.vertices.size() == 24);
    assert(cube.triangles.size() == 12);
    for (const auto& vertex : cube.vertices) {
        assert(vertex.hasNormal);
        assert(vertex.hasTexCoord);
        const float normalLength = std::sqrt(
            vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
        assert(std::abs(normalLength - 1.0F) < 1.0e-5F);
        assert(vertex.u >= 0.0F && vertex.u <= 1.0F);
        assert(vertex.v >= 0.0F && vertex.v <= 1.0F);
    }
    for (const auto& triangle : cube.triangles) {
        for (const std::size_t index : triangle.indices) {
            assert(index < cube.vertices.size());
        }
    }
    const auto initial = originalengine::makeDemoCubeTransform(0.0);
    assert(std::abs(initial.position.x) < 1.0e-6F);
    assert(std::abs(initial.position.y) < 1.0e-6F);
    assert(std::abs(initial.position.z - 4.2F) < 1.0e-6F);
    assert(std::abs(initial.scale.x - 1.0F) < 1.0e-6F);
    assert(std::abs(initial.scale.y - 1.0F) < 1.0e-6F);
    assert(std::abs(initial.scale.z - 1.0F) < 1.0e-6F);
    assert(std::abs(initial.rotation.x) < 1.0e-6F);
    assert(std::abs(initial.rotation.y) < 1.0e-6F);
    assert(std::abs(initial.rotation.z) < 1.0e-6F);

    const auto animated = originalengine::makeDemoCubeTransform(1.0);
    assert(std::abs(animated.rotation.x - std::sin(0.43F) * 0.28F) < 1.0e-6F);
    assert(std::abs(animated.rotation.y - 0.65F) < 1.0e-6F);
    assert(std::abs(animated.rotation.z - std::sin(0.31F) * 0.12F) < 1.0e-6F);
    return 0;
}
