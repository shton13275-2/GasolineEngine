#include "engine/ObjMeshLoader.hpp"

#include <array>

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    originalengine::Mesh mesh;
    std::string error;
    std::istringstream obj(
        "# tiny quad\n"
        "o TestQuad\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n");

    if (!originalengine::ObjMeshLoader::loadFromStream(obj, mesh, &error)) {
        std::cerr << "OBJ load failed: " << error << '\n';
        return 1;
    }
    if (mesh.vertices.size() != 4 || mesh.triangles.size() != 2 ||
        mesh.triangles[0].indices != std::array<std::size_t, 3>{0, 1, 2} ||
        mesh.triangles[1].indices != std::array<std::size_t, 3>{0, 2, 3}) {
        std::cerr << "Quad was not parsed and triangulated correctly.\n";
        return 1;
    }
    for (const auto& vertex : mesh.vertices) {
        if (!vertex.hasTexCoord || !vertex.hasNormal ||
            std::abs(vertex.nz - 1.0F) > 1.0e-6F) {
            std::cerr << "Texture coordinates or normals were not preserved.\n";
            return 1;
        }
    }
    if (std::abs(mesh.vertices[2].u - 1.0F) > 1.0e-6F ||
        std::abs(mesh.vertices[2].v - 1.0F) > 1.0e-6F) {
        std::cerr << "UV values were not parsed correctly.\n";
        return 1;
    }

    std::istringstream negativeIndices(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt -0.25 -0.75\nvn 0 1 0\nf -3/-1/-1 -2/-1/-1 -1/-1/-1\n");
    originalengine::Mesh negativeMesh;
    if (!originalengine::ObjMeshLoader::loadFromStream(negativeIndices, negativeMesh, &error) ||
        negativeMesh.triangles.size() != 1 ||
        negativeMesh.triangles[0].indices != std::array<std::size_t, 3>{0, 1, 2} ||
        !negativeMesh.vertices[0].hasTexCoord || !negativeMesh.vertices[0].hasNormal ||
        std::abs(negativeMesh.vertices[0].u + 0.25F) > 1.0e-6F ||
        std::abs(negativeMesh.vertices[0].v + 0.75F) > 1.0e-6F) {
        std::cerr << "Negative OBJ indices or UV coordinates were not handled correctly: " << error << '\n';
        return 1;
    }

    std::istringstream splitAttributes(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nvt 0.5 0.5\n"
        "f 1/1 2/2 3/3\nf 1/4 3/3 2/2\n");
    originalengine::Mesh splitMesh;
    if (!originalengine::ObjMeshLoader::loadFromStream(splitAttributes, splitMesh, &error) ||
        splitMesh.vertices.size() != 4) {
        std::cerr << "Shared positions with different UVs should split vertices.\n";
        return 1;
    }

    originalengine::Mesh preserved = mesh;
    std::istringstream malformedTrailingSlash(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/ 2/ 3/\n");
    if (originalengine::ObjMeshLoader::loadFromStream(malformedTrailingSlash, mesh, &error) ||
        mesh.vertices.size() != preserved.vertices.size() ||
        mesh.triangles.size() != preserved.triangles.size()) {
        std::cerr << "OBJ face vertices with a trailing slash but no UV index must be rejected "
                     "without replacing the output mesh.\n";
        return 1;
    }

    std::istringstream invalid("v 0 0 0\nf 1 2 3\n");
    if (originalengine::ObjMeshLoader::loadFromStream(invalid, mesh, &error) ||
        mesh.vertices.size() != preserved.vertices.size() ||
        mesh.triangles.size() != preserved.triangles.size()) {
        std::cerr << "Invalid input should fail without replacing the output mesh.\n";
        return 1;
    }

    std::cout << "OBJ mesh loader tests passed.\n";
    return 0;
}
