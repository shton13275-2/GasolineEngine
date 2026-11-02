#pragma once

#include "engine/Mesh.hpp"

#include <array>
#include <cstddef>

namespace originalengine {

// Creates a six-sided cube with independent face vertices, outward normals,
// and a complete 0..1 UV square on every face.
[[nodiscard]] inline Mesh makeTexturedCubeMesh(float halfExtent = 1.0F) {
    struct Face {
        std::array<std::array<float, 3>, 4> positions;
        std::array<float, 3> normal;
    };

    const float n = -halfExtent;
    const float p = halfExtent;
    const std::array<Face, 6> faces{{
        Face{ {{{n,n,p}, {p,n,p}, {p,p,p}, {n,p,p}}}, {{0.0F,0.0F,1.0F}} },
        Face{ {{{p,n,n}, {n,n,n}, {n,p,n}, {p,p,n}}}, {{0.0F,0.0F,-1.0F}} },
        Face{ {{{p,n,p}, {p,n,n}, {p,p,n}, {p,p,p}}}, {{1.0F,0.0F,0.0F}} },
        Face{ {{{n,n,n}, {n,n,p}, {n,p,p}, {n,p,n}}}, {{-1.0F,0.0F,0.0F}} },
        Face{ {{{n,p,p}, {p,p,p}, {p,p,n}, {n,p,n}}}, {{0.0F,1.0F,0.0F}} },
        Face{ {{{n,n,n}, {p,n,n}, {p,n,p}, {n,n,p}}}, {{0.0F,-1.0F,0.0F}} }
    }};
    constexpr std::array<std::array<float, 2>, 4> uv{{
        {{0.0F, 1.0F}}, {{1.0F, 1.0F}}, {{1.0F, 0.0F}}, {{0.0F, 0.0F}}
    }};

    Mesh mesh;
    mesh.vertices.reserve(24);
    mesh.triangles.reserve(12);
    for (const Face& face : faces) {
        const std::size_t base = mesh.vertices.size();
        for (std::size_t corner = 0; corner < 4; ++corner) {
            MeshVertex vertex{};
            vertex.x = face.positions[corner][0];
            vertex.y = face.positions[corner][1];
            vertex.z = face.positions[corner][2];
            vertex.nx = face.normal[0];
            vertex.ny = face.normal[1];
            vertex.nz = face.normal[2];
            vertex.hasNormal = true;
            vertex.u = uv[corner][0];
            vertex.v = uv[corner][1];
            vertex.hasTexCoord = true;
            mesh.vertices.push_back(vertex);
        }
        mesh.triangles.push_back({{{base, base + 1, base + 2}}});
        mesh.triangles.push_back({{{base, base + 2, base + 3}}});
    }
    return mesh;
}

} // namespace originalengine
