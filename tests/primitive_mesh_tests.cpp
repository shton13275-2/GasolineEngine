#include "engine/PrimitiveMeshes.hpp"

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
    return 0;
}
