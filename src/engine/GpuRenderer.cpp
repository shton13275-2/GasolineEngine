#include "engine/GpuRenderer.hpp"
#include "engine/GpuGeometryRules.hpp"
#include "engine/TouchOverlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <iterator>
#include <vector>

namespace originalengine {
namespace {

struct GpuVertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
    float hasTexCoord;
};

using Matrix4 = std::array<float, 16>;

struct ObjectConstants {
    Matrix4 mvp{};
    Matrix4 model{};
    Matrix4 normalMatrix{};
    float cameraPosition[4]{};
    float materialFlags[4]{};
    float pointLightPositionRange[4]{};
    float pointLightColorIntensity[4]{};
};

constexpr char vertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float2 uv [[attribute(3)]];
    float hasTexCoord [[attribute(4)]];
};

struct VertexOutput {
    float4 position [[position]];
    float3 normal;
    float3 color;
    float2 uv;
    float hasTexCoord;
    float3 viewDirection;
    float3 worldPosition;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
    float isFloor;
};

struct CameraConstants {
    float4x4 mvp;
    float4x4 model;
    float4x4 normalMatrix;
    float4 cameraPosition;
    float4 materialFlags;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
};

vertex VertexOutput main_vertex(
    VertexInput input [[stage_in]],
    constant CameraConstants& camera [[buffer(0)]])
{
    VertexOutput output;
    const float4 worldPosition = camera.model * float4(input.position, 1.0);
    output.position = camera.mvp * float4(input.position, 1.0);
    output.normal = normalize((camera.normalMatrix * float4(input.normal, 0.0)).xyz);
    output.color = input.color;
    output.uv = input.uv;
    output.hasTexCoord = input.hasTexCoord;
    output.viewDirection = normalize(camera.cameraPosition.xyz - worldPosition.xyz);
    output.worldPosition = worldPosition.xyz;
    output.pointLightPositionRange = camera.pointLightPositionRange;
    output.pointLightColorIntensity = camera.pointLightColorIntensity;
    output.isFloor = camera.materialFlags.x;
    return output;
}
)metal";

constexpr char fragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexOutput {
    float4 position [[position]];
    float3 normal;
    float3 color;
    float2 uv;
    float hasTexCoord;
    float3 viewDirection;
    float3 worldPosition;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
    float isFloor;
};

struct CameraConstants {
    float4x4 mvp;
    float4x4 model;
    float4x4 normalMatrix;
    float4 cameraPosition;
    float4 materialFlags;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
};

fragment float4 main_fragment(
    VertexOutput input [[stage_in]],
    texture2d<float> baseColorTexture [[texture(0)]],
    sampler baseColorSampler [[sampler(0)]])
{
    const float3 sampledColor = input.hasTexCoord < 0.5
        ? input.color
        : baseColorTexture.sample(baseColorSampler, input.uv).rgb * input.color;
    const float3 normal = normalize(input.normal);
    const bool floorSurface = input.isFloor > 0.5;
    const float3 cyanTint = floorSurface
        ? float3(0.025, 0.20, 0.32)
        : float3(0.12, 0.78, 1.0);
    const float3 albedo = sampledColor * cyanTint;
    const float3 directionalLight = normalize(float3(-0.35, 0.8, -0.45));
    const float directionalDiffuse = max(dot(normal, directionalLight), 0.0);
    const float3 directionalHalf = normalize(directionalLight + normalize(input.viewDirection));
    const float directionalSpecular = pow(
        max(dot(normal, directionalHalf), 0.0), 32.0) * (floorSurface ? 0.04 : 0.16);

    const float3 toPointLight = input.pointLightPositionRange.xyz - input.worldPosition;
    const float lightDistance = length(toPointLight);
    const float3 pointDirection = lightDistance > 0.0001
        ? toPointLight / lightDistance : normal;
    const float lightRange = max(input.pointLightPositionRange.w, 0.001);
    const float rangeFade = max(1.0 - lightDistance / lightRange, 0.0);
    const float attenuation = rangeFade * rangeFade /
        (1.0 + 0.18 * lightDistance * lightDistance);
    const float pointDiffuse = max(dot(normal, pointDirection), 0.0);
    const float3 pointHalf = normalize(pointDirection + normalize(input.viewDirection));
    const float pointSpecular = pow(
        max(dot(normal, pointHalf), 0.0), 48.0) * (floorSurface ? 0.18 : 0.28);
    const float3 pointLight = input.pointLightColorIntensity.rgb *
        input.pointLightColorIntensity.w * attenuation *
        (albedo * pointDiffuse * 1.8 + pointSpecular);

    const float3 emission = floorSurface ? albedo * 0.035 : albedo * 1.35;
    const float ambientStrength = floorSurface ? 0.16 : 0.24;
    const float directionalStrength = floorSurface ? 0.16 : 0.30;
    const float3 lighting = albedo * (ambientStrength + directionalDiffuse * directionalStrength) +
        emission + float3(directionalSpecular) + pointLight;
    return float4(lighting, 1.0);
}
)metal";


struct OverlayConstants {
    float center[2];
    float radius[2];
    float color[4];
    float viewport[2];
    Uint32 shape;
    Uint32 padding;
};


constexpr char overlayVertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct OverlayConstants {
    float2 center;
    float2 radius;
    float4 color;
    float2 viewport;
    uint shape;
    uint padding;
};
struct OverlayOutput {
    float4 position [[position]];
    float4 color;
};
vertex OverlayOutput overlay_vertex(
    uint vertexId [[vertex_id]],
    constant OverlayConstants& constants [[buffer(0)]])
{
    OverlayOutput output;
    float2 local;
    if (constants.shape == 0) {
        const uint segment = vertexId / 6;
        const uint corner = vertexId % 6;
        const float angles[4] = {
            float(segment) / 32.0 * 6.28318530718,
            float(segment) / 32.0 * 6.28318530718,
            float(segment + 1) / 32.0 * 6.28318530718,
            float(segment) / 32.0 * 6.28318530718
        };
        const float angle0 = angles[0];
        const float angle1 = angles[2];
        const float2 outer0 = float2(cos(angle0), sin(angle0));
        const float thickness = max(1.0 / max(constants.radius.x, constants.radius.y), 0.001);
        const float innerScale = 1.0 - thickness;
        const float2 inner0 = outer0 * innerScale;
        const float2 outer1 = float2(cos(angle1), sin(angle1));
        const float2 inner1 = outer1 * innerScale;
        if (corner == 0 || corner == 3) local = outer0;
        else if (corner == 1) local = inner0;
        else if (corner == 2 || corner == 4) local = inner1;
        else local = outer1;
    } else {
        const float2 quad[6] = {
            float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
            float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
        };
        local = quad[vertexId];
    }
    const float2 pixel = constants.center + local * constants.radius;
    output.position = float4(
        pixel.x / constants.viewport.x * 2.0 - 1.0,
        1.0 - pixel.y / constants.viewport.y * 2.0, 0.0, 1.0);
    output.color = constants.color;
    return output;
}
)metal";

constexpr char overlayFragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct OverlayOutput {
    float4 position [[position]];
    float4 color;
};
fragment float4 overlay_fragment(OverlayOutput input [[stage_in]])
{
    return input.color;
}
)metal";

Matrix4 identityMatrix() {
    Matrix4 result{};
    result[0] = 1.0F;
    result[5] = 1.0F;
    result[10] = 1.0F;
    result[15] = 1.0F;
    return result;
}

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    a[static_cast<std::size_t>(k * 4 + row)] *
                    b[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Matrix4 translationMatrix(const Vec3& position) {
    Matrix4 result = identityMatrix();
    result[12] = position.x;
    result[13] = position.y;
    result[14] = position.z;
    return result;
}

Matrix4 scaleMatrix(const Vec3& scale) {
    Matrix4 result{};
    result[0] = scale.x;
    result[5] = scale.y;
    result[10] = scale.z;
    result[15] = 1.0F;
    return result;
}

Matrix4 rotationX(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[5] = c;
    result[9] = -s;
    result[6] = s;
    result[10] = c;
    return result;
}

Matrix4 rotationY(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[0] = c;
    result[8] = s;
    result[2] = -s;
    result[10] = c;
    return result;
}

Matrix4 rotationZ(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[0] = c;
    result[4] = -s;
    result[1] = s;
    result[5] = c;
    return result;
}

Matrix4 modelMatrix(const Transform& transform) {
    const Matrix4 rotations = multiply(
        rotationZ(transform.rotation.z),
        multiply(rotationY(transform.rotation.y), rotationX(transform.rotation.x)));
    return multiply(translationMatrix(transform.position),
                    multiply(rotations, scaleMatrix(transform.scale)));
}

Matrix4 normalMatrix(const Transform& transform) {
    const Matrix4 model = modelMatrix(transform);
    const float a00 = model[0], a01 = model[4], a02 = model[8];
    const float a10 = model[1], a11 = model[5], a12 = model[9];
    const float a20 = model[2], a21 = model[6], a22 = model[10];
    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = a12 * a20 - a10 * a22;
    const float c02 = a10 * a21 - a11 * a20;
    const float c10 = a02 * a21 - a01 * a22;
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = a01 * a20 - a00 * a21;
    const float c20 = a01 * a12 - a02 * a11;
    const float c21 = a02 * a10 - a00 * a12;
    const float c22 = a00 * a11 - a01 * a10;
    const float determinant = a00 * c00 + a01 * c01 + a02 * c02;

    Matrix4 result = identityMatrix();
    if (std::abs(determinant) <= 1.0e-8F) {
        return result;
    }
    const float inverseDeterminant = 1.0F / determinant;
    result[0] = c00 * inverseDeterminant;
    result[4] = c01 * inverseDeterminant;
    result[8] = c02 * inverseDeterminant;
    result[1] = c10 * inverseDeterminant;
    result[5] = c11 * inverseDeterminant;
    result[9] = c12 * inverseDeterminant;
    result[2] = c20 * inverseDeterminant;
    result[6] = c21 * inverseDeterminant;
    result[10] = c22 * inverseDeterminant;
    return result;
}

Matrix4 viewMatrix(const Camera& camera) {
    const float yaw = camera.yaw();
    const float pitch = camera.pitch();
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    const float sinPitch = std::sin(pitch);
    const float cosPitch = std::cos(pitch);

    const Vec3 right{cosYaw, 0.0F, -sinYaw};
    const Vec3 up{-sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch};
    const Vec3 forward{sinYaw * cosPitch, sinPitch, cosYaw * cosPitch};
    const Vec3 position{camera.x(), camera.y(), camera.z()};

    Matrix4 result = identityMatrix();
    result[0] = right.x;   result[4] = right.y;   result[8] = right.z;
    result[1] = up.x;      result[5] = up.y;      result[9] = up.z;
    result[2] = forward.x; result[6] = forward.y; result[10] = forward.z;
    result[12] = -(right.x * position.x + right.y * position.y + right.z * position.z);
    result[13] = -(up.x * position.x + up.y * position.y + up.z * position.z);
    result[14] = -(forward.x * position.x + forward.y * position.y + forward.z * position.z);
    return result;
}

Matrix4 projectionMatrix(float aspectRatio) {
    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;
    // Match Renderer3D's CPU projection: min(width, height) * 0.42 * 2.4.
    // SDL_GPU uses normalized device coordinates, so this is the equivalent
    // vertical focal length for the same screen-space cube size.
    constexpr float focalLength = 2.016F;

    Matrix4 result{};
    result[0] = focalLength / std::max(aspectRatio, 0.001F);
    result[5] = focalLength;
    result[10] = farPlane / (farPlane - nearPlane);
    result[11] = 1.0F;
    result[14] = -(nearPlane * farPlane) / (farPlane - nearPlane);
    return result;
}

bool buildGpuGeometry(const Mesh& mesh, std::vector<GpuVertex>& vertices,
                      std::vector<std::uint32_t>& indices, std::string& error) {
    if (mesh.empty()) {
        error = "Cannot upload an empty mesh";
        return false;
    }

    vertices.clear();
    indices.clear();
    if (mesh.triangles.size() > std::numeric_limits<std::uint32_t>::max() / 3U) {
        error = "Mesh has too many triangle indices for SDL_GPU";
        return false;
    }
    vertices.reserve(mesh.triangles.size() * 3U);
    indices.reserve(mesh.triangles.size() * 3U);

    for (const MeshTriangle& triangle : mesh.triangles) {
        const std::size_t ia = triangle.indices[0];
        const std::size_t ib = triangle.indices[1];
        const std::size_t ic = triangle.indices[2];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
            ic >= mesh.vertices.size()) {
            error = "Mesh triangle contains an out-of-range vertex index";
            return false;
        }

        const MeshVertex& a = mesh.vertices[ia];
        const MeshVertex& b = mesh.vertices[ib];
        const MeshVertex& c = mesh.vertices[ic];
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float abz = b.z - a.z;
        const float acx = c.x - a.x;
        const float acy = c.y - a.y;
        const float acz = c.z - a.z;
        const float nx = aby * acz - abz * acy;
        const float ny = abz * acx - abx * acz;
        const float nz = abx * acy - aby * acx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length <= 1.0e-7F) {
            continue;
        }

        const float faceNormal[3]{nx / length, ny / length, nz / length};
        // A varying is interpolated across a primitive. Keep UV presence constant
        // for all three duplicated vertices so mixed OBJ corners cannot create
        // partially textured fragments within one triangle.
        const bool triangleHasTexCoords = gpu_detail::triangleHasCompleteTexCoords(
            a.hasTexCoord, b.hasTexCoord, c.hasTexCoord);
        const MeshVertex* corners[3]{&a, &b, &c};
        for (const MeshVertex* corner : corners) {
            GpuVertex vertex{};
            vertex.position[0] = corner->x;
            vertex.position[1] = corner->y;
            vertex.position[2] = corner->z;
            float selectedNormal[3]{faceNormal[0], faceNormal[1], faceNormal[2]};
            if (corner->hasNormal) {
                const float normalLength = std::sqrt(
                    corner->nx * corner->nx + corner->ny * corner->ny + corner->nz * corner->nz);
                if (normalLength > 1.0e-7F) {
                    selectedNormal[0] = corner->nx / normalLength;
                    selectedNormal[1] = corner->ny / normalLength;
                    selectedNormal[2] = corner->nz / normalLength;
                }
            }
            std::copy(std::begin(selectedNormal), std::end(selectedNormal),
                      std::begin(vertex.normal));
            vertex.color[0] = 1.0F;
            vertex.color[1] = 1.0F;
            vertex.color[2] = 1.0F;
            vertex.uv[0] = triangleHasTexCoords ? corner->u : 0.0F;
            vertex.uv[1] = triangleHasTexCoords ? 1.0F - corner->v : 0.0F;
            vertex.hasTexCoord = triangleHasTexCoords ? 1.0F : 0.0F;
            const auto index = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(vertex);
            indices.push_back(index);
        }
    }

    if (indices.empty()) {
        error = "Mesh contains no non-degenerate triangles";
        return false;
    }
    if (vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(GpuVertex) ||
        indices.size() > std::numeric_limits<Uint32>::max() / sizeof(std::uint32_t)) {
        error = "Mesh GPU buffers exceed SDL_GPU buffer size limits";
        return false;
    }
    return true;
}

bool uploadBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* destination,
                  const void* data, Uint32 size, std::string& error) {
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr) {
        error = std::string("SDL_CreateGPUTransferBuffer failed: ") + SDL_GetError();
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        error = std::string("SDL_MapGPUTransferBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (commandBuffer == nullptr) {
        error = std::string("SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr) {
        error = std::string("SDL_BeginGPUCopyPass failed: ") + SDL_GetError();
        (void)SDL_CancelGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion target{};
    target.buffer = destination;
    target.size = size;
    SDL_UploadToGPUBuffer(copyPass, &source, &target, false);
    SDL_EndGPUCopyPass(copyPass);

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        error = std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

} // namespace

GpuRenderer::~GpuRenderer() {
    shutdown();
}

void GpuRenderer::setError(const char* operation) {
    lastError_ = std::string(operation) + " failed: " + SDL_GetError();
}

bool GpuRenderer::initialize(SDL_Window* window, const std::vector<const Mesh*>& meshes, const RgbaImage& texture) {
    shutdown();
    lastError_.clear();
    driverName_.clear();
    if (window == nullptr) {
        lastError_ = "Cannot initialize GPU renderer without a window";
        return false;
    }
    if (meshes.empty()) {
        lastError_ = "Cannot initialize GPU renderer without any meshes";
        return false;
    }

    std::vector<GpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    meshRanges_.clear();
    for (const Mesh* mesh : meshes) {
        if (mesh == nullptr) {
            lastError_ = "GPU mesh list contains a null mesh";
            return false;
        }
        std::vector<GpuVertex> meshVertices;
        std::vector<std::uint32_t> meshIndices;
        if (!buildGpuGeometry(*mesh, meshVertices, meshIndices, lastError_)) {
            return false;
        }
        if (vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(GpuVertex) -
                meshVertices.size() ||
            indices.size() > std::numeric_limits<Uint32>::max() / sizeof(std::uint32_t) -
                meshIndices.size()) {
            lastError_ = "Combined GPU mesh buffers exceed SDL_GPU buffer size limits";
            return false;
        }
        const auto firstIndex = static_cast<Uint32>(indices.size());
        const auto baseVertex = static_cast<std::uint32_t>(vertices.size());
        for (const std::uint32_t index : meshIndices) {
            if (index > std::numeric_limits<std::uint32_t>::max() - baseVertex) {
                lastError_ = "Combined GPU mesh vertex index overflow";
                return false;
            }
            indices.push_back(index + baseVertex);
        }
        vertices.insert(vertices.end(), meshVertices.begin(), meshVertices.end());
        meshRanges_.push_back({firstIndex, static_cast<Uint32>(meshIndices.size())});
    }
    if (!texture.hasValidRgba8Data()) {
        lastError_ =
            "GPU texture must contain exactly width * height * 4 RGBA8 bytes "
            "within SDL_GPU transfer buffer size limits";
        return false;
    }

    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (device_ == nullptr) {
        setError("SDL_CreateGPUDevice");
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
        setError("SDL_ClaimWindowForGPUDevice");
        shutdown();
        return false;
    }
    window_ = window;
    const char* driver = SDL_GetGPUDeviceDriver(device_);
    driverName_ = driver != nullptr ? driver : "Unknown";

    if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    } else if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else {
        lastError_ = "GPU device does not support a usable depth-stencil texture format";
        shutdown();
        return false;
    }

    const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    sampleCount_ = SDL_GPU_SAMPLECOUNT_1;
    if (SDL_GPUTextureSupportsFormat(device_, swapchainFormat, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) &&
        SDL_GPUTextureSupportsSampleCount(device_, swapchainFormat, SDL_GPU_SAMPLECOUNT_4) &&
        SDL_GPUTextureSupportsSampleCount(device_, depthFormat_, SDL_GPU_SAMPLECOUNT_4)) {
        sampleCount_ = SDL_GPU_SAMPLECOUNT_4;
    }

    SDL_GPUBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertexBufferInfo.size = static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
    vertexBuffer_ = SDL_CreateGPUBuffer(device_, &vertexBufferInfo);
    if (vertexBuffer_ == nullptr) {
        setError("SDL_CreateGPUBuffer(vertex)");
        shutdown();
        return false;
    }
    if (!uploadBuffer(device_, vertexBuffer_, vertices.data(), vertexBufferInfo.size, lastError_)) {
        shutdown();
        return false;
    }

    SDL_GPUBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indexBufferInfo.size = static_cast<Uint32>(indices.size() * sizeof(std::uint32_t));
    indexBuffer_ = SDL_CreateGPUBuffer(device_, &indexBufferInfo);
    if (indexBuffer_ == nullptr) {
        setError("SDL_CreateGPUBuffer(index)");
        shutdown();
        return false;
    }
    if (!uploadBuffer(device_, indexBuffer_, indices.data(), indexBufferInfo.size, lastError_)) {
        shutdown();
        return false;
    }

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = texture.width;
    textureInfo.height = texture.height;
    textureInfo.layer_count_or_depth = 1;

    // Mipmaps reduce aliasing and shimmering when the texture is viewed at a distance.
    Uint32 mipLevels = 1;
    for (Uint32 dimension = std::max(texture.width, texture.height);
         dimension > 1; dimension /= 2) {
        ++mipLevels;
    }
    const SDL_GPUTextureUsageFlags mipmapUsage =
        SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    const bool supportsMipmaps = SDL_GPUTextureSupportsFormat(
        device_, textureInfo.format, textureInfo.type, mipmapUsage);
    if (supportsMipmaps) {
        textureInfo.usage = mipmapUsage;
        textureInfo.num_levels = mipLevels;
    } else {
        textureInfo.num_levels = 1;
        std::fprintf(stderr,
            "[OriginalEngine] GPU format does not support render-target mipmaps; using base level only.\n");
    }
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    colorTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
    if (colorTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(color)");
        shutdown();
        return false;
    }

    SDL_GPUTransferBufferCreateInfo textureTransferInfo{};
    textureTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    textureTransferInfo.size = static_cast<Uint32>(texture.pixels.size());
    SDL_GPUTransferBuffer* textureTransfer =
        SDL_CreateGPUTransferBuffer(device_, &textureTransferInfo);
    if (textureTransfer == nullptr) {
        setError("SDL_CreateGPUTransferBuffer(texture)");
        shutdown();
        return false;
    }
    void* textureMapped = SDL_MapGPUTransferBuffer(device_, textureTransfer, false);
    if (textureMapped == nullptr) {
        setError("SDL_MapGPUTransferBuffer(texture)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    std::memcpy(textureMapped, texture.pixels.data(), texture.pixels.size());
    SDL_UnmapGPUTransferBuffer(device_, textureTransfer);

    SDL_GPUCommandBuffer* textureCommandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (textureCommandBuffer == nullptr) {
        setError("SDL_AcquireGPUCommandBuffer(texture)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_GPUCopyPass* textureCopyPass = SDL_BeginGPUCopyPass(textureCommandBuffer);
    if (textureCopyPass == nullptr) {
        setError("SDL_BeginGPUCopyPass(texture)");
        (void)SDL_CancelGPUCommandBuffer(textureCommandBuffer);
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_GPUTextureTransferInfo textureSource{};
    textureSource.transfer_buffer = textureTransfer;
    textureSource.pixels_per_row = texture.width;
    textureSource.rows_per_layer = texture.height;
    SDL_GPUTextureRegion textureDestination{};
    textureDestination.texture = colorTexture_;
    textureDestination.w = texture.width;
    textureDestination.h = texture.height;
    textureDestination.d = 1;
    SDL_UploadToGPUTexture(textureCopyPass, &textureSource, &textureDestination, false);
    SDL_EndGPUCopyPass(textureCopyPass);
    if (supportsMipmaps) {
        SDL_GenerateMipmapsForGPUTexture(textureCommandBuffer, colorTexture_);
    }
    if (!SDL_SubmitGPUCommandBuffer(textureCommandBuffer)) {
        setError("SDL_SubmitGPUCommandBuffer(texture upload)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.max_lod = static_cast<float>(textureInfo.num_levels - 1U);
    colorSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (colorSampler_ == nullptr) {
        setError("SDL_CreateGPUSampler");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code_size = std::strlen(vertexSource);
    vertexInfo.code = reinterpret_cast<const Uint8*>(vertexSource);
    vertexInfo.entrypoint = "main_vertex";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1;
    SDL_GPUShader* vertexShader = SDL_CreateGPUShader(device_, &vertexInfo);
    if (vertexShader == nullptr) {
        setError("SDL_CreateGPUShader(vertex)");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code_size = std::strlen(fragmentSource);
    fragmentInfo.code = reinterpret_cast<const Uint8*>(fragmentSource);
    fragmentInfo.entrypoint = "main_fragment";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 1;
    SDL_GPUShader* fragmentShader = SDL_CreateGPUShader(device_, &fragmentInfo);
    if (fragmentShader == nullptr) {
        setError("SDL_CreateGPUShader(fragment)");
        SDL_ReleaseGPUShader(device_, vertexShader);
        shutdown();
        return false;
    }

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = static_cast<Uint32>(sizeof(GpuVertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const SDL_GPUVertexAttribute attributes[]{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, position))},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, normal))},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, color))},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(GpuVertex, uv))},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(GpuVertex, hasTexCoord))}
    };

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDescription;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = attributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 5;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = sampleCount_;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = true;
    pipelineInfo.target_info.depth_stencil_format = depthFormat_;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
    if (pipeline_ != nullptr) {
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        wireframePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
    }
    SDL_ReleaseGPUShader(device_, fragmentShader);
    SDL_ReleaseGPUShader(device_, vertexShader);
    if (pipeline_ == nullptr || wireframePipeline_ == nullptr) {
        setError("SDL_CreateGPUGraphicsPipeline");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo overlayVertexInfo{};
    overlayVertexInfo.code_size = std::strlen(overlayVertexSource);
    overlayVertexInfo.code = reinterpret_cast<const Uint8*>(overlayVertexSource);
    overlayVertexInfo.entrypoint = "overlay_vertex";
    overlayVertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    overlayVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    overlayVertexInfo.num_uniform_buffers = 1;
    SDL_GPUShader* overlayVertexShader = SDL_CreateGPUShader(device_, &overlayVertexInfo);
    if (overlayVertexShader == nullptr) {
        setError("SDL_CreateGPUShader(overlay vertex)");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo overlayFragmentInfo{};
    overlayFragmentInfo.code_size = std::strlen(overlayFragmentSource);
    overlayFragmentInfo.code = reinterpret_cast<const Uint8*>(overlayFragmentSource);
    overlayFragmentInfo.entrypoint = "overlay_fragment";
    overlayFragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    overlayFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    SDL_GPUShader* overlayFragmentShader = SDL_CreateGPUShader(device_, &overlayFragmentInfo);
    if (overlayFragmentShader == nullptr) {
        setError("SDL_CreateGPUShader(overlay fragment)");
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        shutdown();
        return false;
    }

    SDL_GPUColorTargetDescription overlayColorTarget{};
    overlayColorTarget.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    overlayColorTarget.blend_state.enable_blend = true;
    overlayColorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    overlayColorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    overlayColorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    overlayColorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    overlayColorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    overlayColorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo overlayPipelineInfo{};
    overlayPipelineInfo.vertex_shader = overlayVertexShader;
    overlayPipelineInfo.fragment_shader = overlayFragmentShader;
    overlayPipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    overlayPipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    overlayPipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    overlayPipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    overlayPipelineInfo.multisample_state.sample_count = sampleCount_;
    overlayPipelineInfo.depth_stencil_state.enable_depth_test = false;
    overlayPipelineInfo.depth_stencil_state.enable_depth_write = false;
    overlayPipelineInfo.target_info.color_target_descriptions = &overlayColorTarget;
    overlayPipelineInfo.target_info.num_color_targets = 1;
    overlayPipelineInfo.target_info.has_depth_stencil_target = true;
    overlayPipelineInfo.target_info.depth_stencil_format = depthFormat_;
    overlayPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &overlayPipelineInfo);
    SDL_ReleaseGPUShader(device_, overlayFragmentShader);
    SDL_ReleaseGPUShader(device_, overlayVertexShader);
    if (overlayPipeline_ == nullptr) {
        setError("SDL_CreateGPUGraphicsPipeline(overlay)");
        shutdown();
        return false;
    }
    return true;
}

bool GpuRenderer::createDepthTexture(Uint32 width, Uint32 height) {
    if (depthTexture_ != nullptr && depthWidth_ == width && depthHeight_ == height &&
        (sampleCount_ == SDL_GPU_SAMPLECOUNT_1 || multisampleColorTexture_ != nullptr)) {
        return true;
    }
    if (depthTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
    }
    if (multisampleColorTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, multisampleColorTexture_);
        multisampleColorTexture_ = nullptr;
    }

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = depthFormat_;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = sampleCount_;
    depthTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
    if (depthTexture_ == nullptr) {
        depthWidth_ = 0;
        depthHeight_ = 0;
        setError("SDL_CreateGPUTexture(depth)");
        return false;
    }

    if (sampleCount_ != SDL_GPU_SAMPLECOUNT_1) {
        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        colorInfo.width = width;
        colorInfo.height = height;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = sampleCount_;
        multisampleColorTexture_ = SDL_CreateGPUTexture(device_, &colorInfo);
        if (multisampleColorTexture_ == nullptr) {
            SDL_ReleaseGPUTexture(device_, depthTexture_);
            depthTexture_ = nullptr;
            depthWidth_ = 0;
            depthHeight_ = 0;
            setError("SDL_CreateGPUTexture(multisample color)");
            return false;
        }
    }
    depthWidth_ = width;
    depthHeight_ = height;
    return true;
}

bool GpuRenderer::renderFrame(
    const Camera& camera, const std::vector<GpuMeshInstance>& instances,
    bool showTouchControls, bool movementActive, float movementX, float movementY,
    float framesPerSecond, bool wireframe) {
    if (device_ == nullptr || window_ == nullptr || pipeline_ == nullptr ||
        wireframePipeline_ == nullptr ||
        vertexBuffer_ == nullptr || indexBuffer_ == nullptr || meshRanges_.empty()) {
        lastError_ = "GPU renderer is not initialized";
        return false;
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
        setError("SDL_AcquireGPUCommandBuffer");
        return false;
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commandBuffer, window_, &swapchainTexture, &width, &height)) {
        setError("SDL_WaitAndAcquireGPUSwapchainTexture");
        (void)SDL_CancelGPUCommandBuffer(commandBuffer);
        return false;
    }
    if (swapchainTexture == nullptr || width == 0 || height == 0) {
        if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
            setError("SDL_SubmitGPUCommandBuffer");
            return false;
        }
        return true;
    }
    if (!createDepthTexture(width, height)) {
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? swapchainTexture : multisampleColorTexture_;
    colorTarget.clear_color = SDL_FColor{0.02F, 0.11F, 0.19F, 1.0F};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_RESOLVE;
    colorTarget.resolve_texture = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? nullptr : swapchainTexture;

    SDL_GPUDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = depthTexture_;
    depthTarget.clear_depth = 1.0F;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
        commandBuffer, &colorTarget, 1, &depthTarget);
    if (pass == nullptr) {
        setError("SDL_BeginGPURenderPass");
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }

    SDL_BindGPUGraphicsPipeline(pass, wireframe ? wireframePipeline_ : pipeline_);
    const SDL_GPUBufferBinding vertexBinding{vertexBuffer_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    const SDL_GPUBufferBinding indexBinding{indexBuffer_, 0};
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const SDL_GPUTextureSamplerBinding colorBinding{colorTexture_, colorSampler_};
    SDL_BindGPUFragmentSamplers(pass, 0, &colorBinding, 1);

    const Matrix4 view = viewMatrix(camera);
    const Matrix4 projection = projectionMatrix(
        static_cast<float>(width) / static_cast<float>(height));
    const Matrix4 viewProjection = multiply(projection, view);
    Vec3 pointLightPosition{0.0F, 0.0F, 4.2F};
    for (const GpuMeshInstance& instance : instances) {
        if (instance.object != nullptr && instance.meshIndex == 1U) {
            pointLightPosition = instance.object->transform.position;
            break;
        }
    }

    SDL_BindGPUGraphicsPipeline(pass, wireframe ? wireframePipeline_ : pipeline_);
    for (const GpuMeshInstance& instance : instances) {
        const SceneObject* object = instance.object;
        if (object == nullptr || !object->visible || instance.meshIndex >= meshRanges_.size()) {
            continue;
        }
        const Matrix4 model = modelMatrix(object->transform);
        ObjectConstants constants{};
        constants.model = model;
        constants.mvp = multiply(viewProjection, model);
        constants.normalMatrix = normalMatrix(object->transform);
        constants.cameraPosition[0] = camera.x();
        constants.cameraPosition[1] = camera.y();
        constants.cameraPosition[2] = camera.z();
        constants.cameraPosition[3] = 1.0F;
        constants.materialFlags[0] = instance.meshIndex == 2U ? 1.0F : 0.0F;
        constants.pointLightPositionRange[0] = pointLightPosition.x;
        constants.pointLightPositionRange[1] = pointLightPosition.y;
        constants.pointLightPositionRange[2] = pointLightPosition.z;
        constants.pointLightPositionRange[3] = 8.0F;
        constants.pointLightColorIntensity[0] = 0.02F;
        constants.pointLightColorIntensity[1] = 0.72F;
        constants.pointLightColorIntensity[2] = 1.0F;
        constants.pointLightColorIntensity[3] = 3.2F;
        SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                     static_cast<Uint32>(sizeof(constants)));
        const MeshRange& range = meshRanges_[instance.meshIndex];
        SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.firstIndex, 0, 0);
    }

    if (showTouchControls && overlayPipeline_ != nullptr) {
        drawOverlay(commandBuffer, pass, width, height, movementActive, movementX, movementY,
                    framesPerSecond);
    }

    SDL_EndGPURenderPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        setError("SDL_SubmitGPUCommandBuffer");
        return false;
    }
    return true;
}


void GpuRenderer::drawOverlayRect(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, float rectWidth, float rectHeight,
    Uint32 viewportWidth, Uint32 viewportHeight, const float color[4]) {
    OverlayConstants constants{};
    constants.center[0] = x + rectWidth * 0.5F;
    constants.center[1] = y + rectHeight * 0.5F;
    constants.radius[0] = rectWidth * 0.5F;
    constants.radius[1] = rectHeight * 0.5F;
    std::copy_n(color, 4, constants.color);
    constants.viewport[0] = static_cast<float>(viewportWidth);
    constants.viewport[1] = static_cast<float>(viewportHeight);
    constants.shape = 1;
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                 static_cast<Uint32>(sizeof(constants)));
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

void GpuRenderer::drawOverlayCircle(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, float radiusX, float radiusY,
    Uint32 viewportWidth, Uint32 viewportHeight, const float color[4]) {
    OverlayConstants constants{};
    constants.center[0] = x;
    constants.center[1] = y;
    constants.radius[0] = radiusX;
    constants.radius[1] = radiusY;
    std::copy_n(color, 4, constants.color);
    constants.viewport[0] = static_cast<float>(viewportWidth);
    constants.viewport[1] = static_cast<float>(viewportHeight);
    constants.shape = 0;
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                 static_cast<Uint32>(sizeof(constants)));
    SDL_DrawGPUPrimitives(pass, 32U * 6U, 1, 0, 0);
}

void GpuRenderer::drawOverlayText(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, const char* text, Uint32 viewportWidth,
    Uint32 viewportHeight, const float color[4]) {
    using namespace touch_overlay;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const std::string_view pattern = glyph(*cursor);
        int row = 0;
        int column = 0;
        for (const char pixel : pattern) {
            if (pixel == '/') { ++row; column = 0; continue; }
            if (pixel == '1') {
                drawOverlayRect(commandBuffer, pass,
                    x + static_cast<float>(column) * fontPixelSize,
                    y + static_cast<float>(row) * fontPixelSize,
                    fontPixelSize, fontPixelSize, viewportWidth, viewportHeight, color);
            }
            ++column;
        }
        x += fontAdvance;
    }
}

void GpuRenderer::drawOverlay(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    Uint32 width, Uint32 height, bool movementActive, float movementX, float movementY,
    float framesPerSecond) {
    SDL_BindGPUGraphicsPipeline(pass, overlayPipeline_);
    using namespace touch_overlay;
    const float dimension = static_cast<float>(std::min(width, height));
    const float joystickX = static_cast<float>(width) * movementCenterX;
    const float joystickY = static_cast<float>(height) * controlCenterY;
    const float joystickRadius = dimension * movementRadius;
    const float lookX = static_cast<float>(width) * lookCenterX;
    const float lookY = static_cast<float>(height) * controlCenterY;
    const float lookRadiusValue = dimension * lookRadius;
    const auto ring = normalized(ringColor);
    const auto guide = normalized(guideColor);
    const auto knob = normalized(knobColor);
    const auto button = normalized(buttonColor);
    const auto text = normalized(textColor);

    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius, joystickRadius, width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius * movementInnerRadius, joystickRadius * movementInnerRadius,
                      width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius * knobRadius, joystickRadius * knobRadius,
                      width, height, ring.data());
    drawOverlayRect(commandBuffer, pass, joystickX - joystickRadius * movementGuideRadius,
                    joystickY - 0.5F, joystickRadius * movementGuideRadius * 2.0F, 1.0F,
                    width, height, guide.data());
    drawOverlayRect(commandBuffer, pass, joystickX - 0.5F,
                    joystickY - joystickRadius * movementGuideRadius, 1.0F,
                    joystickRadius * movementGuideRadius * 2.0F, width, height, guide.data());
    const float knobX = movementActive
        ? joystickX + std::clamp(movementX * static_cast<float>(width) - joystickX,
                                 -joystickRadius * movementGuideRadius, joystickRadius * movementGuideRadius)
        : joystickX;
    const float knobY = movementActive
        ? joystickY + std::clamp(movementY * static_cast<float>(height) - joystickY,
                                 -joystickRadius * movementGuideRadius, joystickRadius * movementGuideRadius)
        : joystickY;
    drawOverlayCircle(commandBuffer, pass, knobX, knobY,
                      joystickRadius * knobRadius, joystickRadius * knobRadius,
                      width, height, knob.data());

    drawOverlayCircle(commandBuffer, pass, lookX, lookY, lookRadiusValue, lookRadiusValue,
                      width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, lookX, lookY,
                      lookRadiusValue * lookInnerRadius, lookRadiusValue * lookInnerRadius,
                      width, height, ring.data());
    drawOverlayRect(commandBuffer, pass, lookX - lookRadiusValue * lookGuideRadius,
                    lookY - 0.5F, lookRadiusValue * lookGuideRadius * 2.0F, 1.0F,
                    width, height, guide.data());
    drawOverlayRect(commandBuffer, pass, lookX - 0.5F,
                    lookY - lookRadiusValue * lookGuideRadius, 1.0F,
                    lookRadiusValue * lookGuideRadius * 2.0F, width, height, guide.data());

    char fpsText[24]{};
    std::snprintf(fpsText, sizeof(fpsText), "FPS:%d",
                  std::max(0, static_cast<int>(std::round(framesPerSecond))));
    drawOverlayText(commandBuffer, pass, fpsX, fpsY, fpsText, width, height, text.data());

    const float buttonX = static_cast<float>(width) - backendButtonRight - backendButtonWidth;
    drawOverlayRect(commandBuffer, pass, buttonX, backendButtonY,
                    backendButtonWidth, backendButtonHeight, width, height, button.data());
    drawOverlayText(commandBuffer, pass, buttonX + 33.0F,
                    backendButtonY + (backendButtonHeight - fontHeight) * 0.5F,
                    "CPU", width, height, text.data());
}

void GpuRenderer::shutdown() noexcept {
    if (device_ != nullptr) {
        if (depthTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, depthTexture_);
            depthTexture_ = nullptr;
        }
        if (multisampleColorTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, multisampleColorTexture_);
            multisampleColorTexture_ = nullptr;
        }
        if (colorSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, colorSampler_);
            colorSampler_ = nullptr;
        }
        if (colorTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, colorTexture_);
            colorTexture_ = nullptr;
        }
        depthWidth_ = 0;
        depthHeight_ = 0;
        if (overlayPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, overlayPipeline_);
            overlayPipeline_ = nullptr;
        }
        if (wireframePipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, wireframePipeline_);
            wireframePipeline_ = nullptr;
        }
        if (pipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
            pipeline_ = nullptr;
        }
        if (vertexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
            vertexBuffer_ = nullptr;
        }
        if (indexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, indexBuffer_);
            indexBuffer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            window_ = nullptr;
        }
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
    }
    depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    sampleCount_ = SDL_GPU_SAMPLECOUNT_1;
    meshRanges_.clear();
}

} // namespace originalengine
