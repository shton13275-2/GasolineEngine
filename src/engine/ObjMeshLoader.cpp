#include "engine/ObjMeshLoader.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace originalengine {
namespace {

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool parseInteger(const std::string& text, long long& value) {
    if (text.empty()) {
        return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last && value != 0;
}

bool resolveIndex(long long objIndex, std::size_t count, std::size_t& index) {
    if (objIndex > 0) {
        const auto positive = static_cast<unsigned long long>(objIndex);
        if (positive > count) {
            return false;
        }
        index = static_cast<std::size_t>(positive - 1ULL);
        return true;
    }

    const auto magnitude = static_cast<unsigned long long>(-(objIndex + 1LL)) + 1ULL;
    if (magnitude > count) {
        return false;
    }
    index = count - static_cast<std::size_t>(magnitude);
    return true;
}

struct FaceVertex {
    std::size_t position{0};
    std::size_t texCoord{0};
    std::size_t normal{0};
    bool hasTexCoord{false};
    bool hasNormal{false};
};

bool parseFaceVertex(const std::string& token, std::size_t positionCount,
                     std::size_t texCoordCount, std::size_t normalCount,
                     FaceVertex& result) {
    const std::size_t firstSlash = token.find('/');
    const std::string positionText = token.substr(0, firstSlash);
    long long positionIndex = 0;
    if (!parseInteger(positionText, positionIndex) ||
        !resolveIndex(positionIndex, positionCount, result.position)) {
        return false;
    }
    if (firstSlash == std::string::npos) {
        return true;
    }

    const std::size_t secondSlash = token.find('/', firstSlash + 1);
    const std::string texText = token.substr(
        firstSlash + 1,
        (secondSlash == std::string::npos ? token.size() : secondSlash) - firstSlash - 1);
    if (secondSlash == std::string::npos && texText.empty()) {
        // A trailing slash without a texture-coordinate index is not a valid
        // OBJ face-vertex form (for example, "1/").
        return false;
    }

    if (!texText.empty()) {
        long long texIndex = 0;
        if (!parseInteger(texText, texIndex) ||
            !resolveIndex(texIndex, texCoordCount, result.texCoord)) {
            return false;
        }
        result.hasTexCoord = true;
    }

    if (secondSlash != std::string::npos) {
        const std::string normalText = token.substr(secondSlash + 1);
        long long normalIndex = 0;
        if (!parseInteger(normalText, normalIndex) ||
            !resolveIndex(normalIndex, normalCount, result.normal)) {
            return false;
        }
        result.hasNormal = true;
    }
    return true;
}

struct VertexKey {
    std::size_t position;
    std::size_t texCoord;
    std::size_t normal;
    bool hasTexCoord;
    bool hasNormal;

    bool operator==(const VertexKey&) const = default;
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& key) const noexcept {
        std::size_t value = key.position + 0x9e3779b9U;
        const auto mix = [&value](std::size_t part) {
            value ^= part + 0x9e3779b9U + (value << 6U) + (value >> 2U);
        };
        mix(key.texCoord);
        mix(key.normal);
        mix(key.hasTexCoord ? 1U : 0U);
        mix(key.hasNormal ? 1U : 0U);
        return value;
    }
};

} // namespace

bool ObjMeshLoader::loadFromFile(
    const std::filesystem::path& path, Mesh& output, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        setError(error, "Could not open OBJ file: " + path.string());
        return false;
    }
    return loadFromStream(file, output, error);
}

bool ObjMeshLoader::loadFromStream(
    std::istream& input, Mesh& output, std::string* error) {
    Mesh parsed;
    std::vector<MeshVertex> positions;
    std::vector<std::array<float, 2>> texCoords;
    std::vector<std::array<float, 3>> normals;
    std::unordered_map<VertexKey, std::size_t, VertexKeyHash> vertexLookup;
    std::string line;
    std::size_t lineNumber = 0;

    const auto getVertexIndex = [&](const FaceVertex& faceVertex) -> std::size_t {
        const VertexKey key{faceVertex.position, faceVertex.texCoord, faceVertex.normal,
                            faceVertex.hasTexCoord, faceVertex.hasNormal};
        const auto found = vertexLookup.find(key);
        if (found != vertexLookup.end()) {
            return found->second;
        }

        MeshVertex vertex = positions[faceVertex.position];
        if (faceVertex.hasTexCoord) {
            vertex.u = texCoords[faceVertex.texCoord][0];
            vertex.v = texCoords[faceVertex.texCoord][1];
            vertex.hasTexCoord = true;
        }
        if (faceVertex.hasNormal) {
            const auto& normal = normals[faceVertex.normal];
            vertex.nx = normal[0];
            vertex.ny = normal[1];
            vertex.nz = normal[2];
            vertex.hasNormal = true;
        }
        const std::size_t index = parsed.vertices.size();
        parsed.vertices.push_back(vertex);
        vertexLookup.emplace(key, index);
        return index;
    };

    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream row(line);
        std::string kind;
        if (!(row >> kind) || kind.starts_with('#')) {
            continue;
        }

        if (kind == "v") {
            MeshVertex vertex;
            if (!(row >> vertex.x >> vertex.y >> vertex.z)) {
                setError(error, "Invalid vertex at line " + std::to_string(lineNumber));
                return false;
            }
            positions.push_back(vertex);
        } else if (kind == "vt") {
            std::array<float, 2> uv{};
            if (!(row >> uv[0] >> uv[1])) {
                setError(error, "Invalid texture coordinate at line " + std::to_string(lineNumber));
                return false;
            }
            texCoords.push_back(uv);
        } else if (kind == "vn") {
            std::array<float, 3> normal{};
            if (!(row >> normal[0] >> normal[1] >> normal[2])) {
                setError(error, "Invalid normal at line " + std::to_string(lineNumber));
                return false;
            }
            normals.push_back(normal);
        } else if (kind == "f") {
            std::vector<std::size_t> face;
            std::string token;
            while (row >> token) {
                if (token.starts_with('#')) {
                    break;
                }
                FaceVertex faceVertex;
                if (!parseFaceVertex(token, positions.size(), texCoords.size(),
                                     normals.size(), faceVertex)) {
                    setError(error, "Invalid face index at line " + std::to_string(lineNumber));
                    return false;
                }
                face.push_back(getVertexIndex(faceVertex));
            }

            if (face.size() < 3) {
                setError(error, "Face has fewer than three vertices at line " +
                                std::to_string(lineNumber));
                return false;
            }

            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                parsed.triangles.push_back({{face[0], face[i], face[i + 1]}});
            }
        }
        // Object/group/material declarations are intentionally ignored for now.
    }

    if (input.bad()) {
        setError(error, "I/O error while reading OBJ data");
        return false;
    }
    if (positions.empty() || parsed.vertices.empty() || parsed.triangles.empty()) {
        setError(error, "OBJ contains no usable triangle geometry");
        return false;
    }

    output = std::move(parsed);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace originalengine
