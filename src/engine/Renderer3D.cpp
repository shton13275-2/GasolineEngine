#include "engine/Renderer3D.hpp"
#include "engine/DemoCubeTransform.hpp"

#include "engine/Camera.hpp"
#include "engine/Mesh.hpp"
#include "engine/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <cctype>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace originalengine {
namespace {

struct Face {
    std::array<int, 4> indices;
    SDL_Color color;
};

bool readPpmToken(std::istream& input, std::string& token) {
    token.clear();
    char ch = 0;
    while (input.get(ch)) {
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            token.push_back(ch);
            break;
        }
    }
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) break;
        token.push_back(ch);
    }
    return !token.empty();
}

bool loadPpmTexture(const std::string& path, TextureImage& texture) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string token;
    if (!readPpmToken(input, token) || token != "P3") return false;
    auto readPositiveInt = [&](int& value) {
        if (!readPpmToken(input, token)) return false;
        try {
            std::size_t consumed = 0;
            const int parsed = std::stoi(token, &consumed);
            if (consumed != token.size() || parsed <= 0) return false;
            value = parsed;
            return true;
        } catch (...) { return false; }
    };
    int width = 0, height = 0, maxValue = 0;
    if (!readPositiveInt(width) || !readPositiveInt(height) ||
        !readPositiveInt(maxValue) || maxValue > 65535) return false;
    const auto pixelCount = static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height);
    if (pixelCount > 16U * 1024U * 1024U) return false;
    TextureImage loaded;
    loaded.width = width;
    loaded.height = height;
    loaded.pixels.resize(pixelCount * 4U);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (!readPpmToken(input, token)) return false;
            int value = 0;
            try {
                std::size_t consumed = 0;
                value = std::stoi(token, &consumed);
                if (consumed != token.size() || value < 0 || value > maxValue) return false;
            } catch (...) { return false; }
            loaded.pixels[i * 4U + channel] = static_cast<Uint8>(
                (static_cast<unsigned int>(value) * 255U) /
                static_cast<unsigned int>(maxValue));
        }
        loaded.pixels[i * 4U + 3U] = 255;
    }
    texture = std::move(loaded);
    return true;
}

bool loadBmpTexture(const std::string& path, TextureImage& texture) {
    SDL_Surface* loaded = SDL_LoadBMP(path.c_str());
    if (loaded == nullptr) {
        return false;
    }

    SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (converted == nullptr) {
        return false;
    }

    const auto destroyConverted = [&]() { SDL_DestroySurface(converted); };
    if (converted->w <= 0 || converted->h <= 0 ||
        converted->w > 16384 || converted->h > 16384) {
        destroyConverted();
        return false;
    }

    const auto pixelCount = static_cast<std::size_t>(converted->w) *
                            static_cast<std::size_t>(converted->h);
    if (pixelCount > 16U * 1024U * 1024U ||
        converted->pitch < converted->w * 4) {
        destroyConverted();
        return false;
    }

    TextureImage loadedTexture;
    loadedTexture.width = converted->w;
    loadedTexture.height = converted->h;
    loadedTexture.pixels.resize(pixelCount * 4U);
    const auto* source = static_cast<const Uint8*>(converted->pixels);
    for (int y = 0; y < converted->h; ++y) {
        const auto* sourceRow = source + static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(converted->pitch);
        auto* destinationRow = loadedTexture.pixels.data() +
                               static_cast<std::size_t>(y) *
                               static_cast<std::size_t>(converted->w) * 4U;
        std::copy_n(sourceRow, static_cast<std::size_t>(converted->w) * 4U,
                    destinationRow);
    }
    destroyConverted();
    texture = std::move(loadedTexture);
    return true;
}

bool loadTexture(const std::string& path, TextureImage& texture) {
    const std::filesystem::path filePath(path);
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (extension == ".ppm" || extension == ".pnm") {
        return loadPpmTexture(path, texture);
    }
    if (extension == ".bmp") {
        return loadBmpTexture(path, texture);
    }
    return false;
}

SDL_Color sampleTexture(const TextureImage& texture, float u, float v) {
    if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty() ||
        !std::isfinite(u) || !std::isfinite(v)) {
        return {255, 255, 255, 255};
    }
    u -= std::floor(u);
    v -= std::floor(v);
    const int x = std::clamp(static_cast<int>(u * texture.width), 0, texture.width - 1);
    const int y = std::clamp(static_cast<int>((1.0F - v) * texture.height),
                             0, texture.height - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(texture.width) +
                                static_cast<std::size_t>(x)) * 4U;
    return {texture.pixels[offset], texture.pixels[offset + 1U],
            texture.pixels[offset + 2U], texture.pixels[offset + 3U]};
}

Vec3 rotateX(Vec3 p, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}

Vec3 rotateY(Vec3 p, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

Vec3 rotateZ(Vec3 p, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

Vec3 toCameraSpace(Vec3 world, const Camera& camera) {
    Vec3 p{world.x - camera.x(), world.y - camera.y(), world.z - camera.z()};
    p = rotateY(p, -camera.yaw());
    p = rotateX(p, camera.pitch());
    return p;
}

struct TexturedClipVertex {
    Vec3 position;
    float u{0.0F};
    float v{0.0F};
};

std::vector<TexturedClipVertex> clipTexturedTriangleAgainstNearPlane(
    const std::array<TexturedClipVertex, 3>& triangle, float nearPlane) {
    std::vector<TexturedClipVertex> clipped;
    clipped.reserve(4);
    TexturedClipVertex previous = triangle.back();
    bool previousInside = previous.position.z > nearPlane;
    for (const TexturedClipVertex& current : triangle) {
        const bool currentInside = current.position.z > nearPlane;
        if (currentInside != previousInside) {
            const float denominator = current.position.z - previous.position.z;
            if (std::abs(denominator) > 1.0e-7F) {
                const float t = (nearPlane - previous.position.z) / denominator;
                clipped.push_back({
                    {previous.position.x + (current.position.x - previous.position.x) * t,
                     previous.position.y + (current.position.y - previous.position.y) * t,
                     nearPlane},
                    previous.u + (current.u - previous.u) * t,
                    previous.v + (current.v - previous.v) * t
                });
            }
        }
        if (currentInside) clipped.push_back(current);
        previous = current;
        previousInside = currentInside;
    }
    return clipped;
}

std::vector<Vec3> clipAgainstNearPlane(const std::vector<Vec3>& polygon, float nearPlane) {
    std::vector<Vec3> clipped;
    if (polygon.empty()) {
        return clipped;
    }

    Vec3 previous = polygon.back();
    bool previousInside = previous.z > nearPlane;
    for (const Vec3 current : polygon) {
        const bool currentInside = current.z > nearPlane;
        if (currentInside != previousInside) {
            const float denominator = current.z - previous.z;
            if (std::abs(denominator) > 1.0e-7F) {
                const float t = (nearPlane - previous.z) / denominator;
                clipped.push_back({
                    previous.x + (current.x - previous.x) * t,
                    previous.y + (current.y - previous.y) * t,
                    nearPlane
                });
            }
        }
        if (currentInside) {
            clipped.push_back(current);
        }
        previous = current;
        previousInside = currentInside;
    }
    return clipped;
}

SDL_Vertex makeVertex(Vec3 point, SDL_Color color,
                      const auto& project) {
    SDL_Vertex vertex{};
    vertex.position = project(point);
    vertex.color = {
        static_cast<float>(color.r) / 255.0F,
        static_cast<float>(color.g) / 255.0F,
        static_cast<float>(color.b) / 255.0F,
        static_cast<float>(color.a) / 255.0F
    };
    vertex.tex_coord = {0.0F, 0.0F};
    return vertex;
}


void rasterizeTriangle(
    int width, int height,
    const std::array<SDL_FPoint, 3>& points,
    const std::array<float, 3>& depths,
    const std::array<SDL_Color, 3>& colors,
    std::vector<Uint8>& colorBuffer,
    std::vector<float>& depthBuffer,
    const std::array<SDL_FPoint, 3>* texCoords = nullptr,
    const TextureImage* texture = nullptr) {
    const float area =
        (points[1].x - points[0].x) * (points[2].y - points[0].y) -
        (points[1].y - points[0].y) * (points[2].x - points[0].x);
    if (std::abs(area) < 1.0e-6F) {
        return;
    }

    const float minX = std::min({points[0].x, points[1].x, points[2].x});
    const float maxX = std::max({points[0].x, points[1].x, points[2].x});
    const float minY = std::min({points[0].y, points[1].y, points[2].y});
    const float maxY = std::max({points[0].y, points[1].y, points[2].y});

    // Reject off-screen or non-finite bounds before float-to-int conversion.
    // Near-plane clipping can still create extremely large projected coordinates.
    if (!std::isfinite(minX) || !std::isfinite(maxX) ||
        !std::isfinite(minY) || !std::isfinite(maxY) ||
        maxX < 0.0F || maxY < 0.0F ||
        minX >= static_cast<float>(width) ||
        minY >= static_cast<float>(height)) {
        return;
    }
    const int left = static_cast<int>(std::floor(std::clamp(minX, 0.0F,
                                                            static_cast<float>(width - 1))));
    const int right = static_cast<int>(std::ceil(std::clamp(maxX, 0.0F,
                                                            static_cast<float>(width - 1))));
    const int top = static_cast<int>(std::floor(std::clamp(minY, 0.0F,
                                                           static_cast<float>(height - 1))));
    const int bottom = static_cast<int>(std::ceil(std::clamp(maxY, 0.0F,
                                                             static_cast<float>(height - 1))));
    if (left > right || top > bottom) {
        return;
    }

    if (depths[0] <= 0.0F || depths[1] <= 0.0F || depths[2] <= 0.0F) {
        return;
    }

    const float inverseArea = 1.0F / area;
    const float invZ0 = 1.0F / depths[0];
    const float invZ1 = 1.0F / depths[1];
    const float invZ2 = 1.0F / depths[2];
    const float w0StepX = (points[1].y - points[2].y) * inverseArea;
    const float w0StepY = (points[2].x - points[1].x) * inverseArea;
    const float w1StepX = (points[2].y - points[0].y) * inverseArea;
    const float w1StepY = (points[0].x - points[2].x) * inverseArea;
    const float startX = static_cast<float>(left) + 0.5F;
    const float startY = static_cast<float>(top) + 0.5F;
    float rowW0 = ((points[1].x - startX) * (points[2].y - startY) -
                   (points[1].y - startY) * (points[2].x - startX)) * inverseArea;
    float rowW1 = ((points[2].x - startX) * (points[0].y - startY) -
                   (points[2].y - startY) * (points[0].x - startX)) * inverseArea;
    for (int y = top; y <= bottom; ++y) {
        float w0 = rowW0;
        float w1 = rowW1;
        for (int x = left; x <= right; ++x) {
            const float w2 = 1.0F - w0 - w1;
            if (w0 < -1.0e-5F || w1 < -1.0e-5F || w2 < -1.0e-5F) {
                w0 += w0StepX;
                w1 += w1StepX;
                continue;
            }

            const float reciprocalDepth = w0 * invZ0 + w1 * invZ1 + w2 * invZ2;
            if (reciprocalDepth <= 0.0F) {
                w0 += w0StepX;
                w1 += w1StepX;
                continue;
            }
            const float depth = 1.0F / reciprocalDepth;
            const std::size_t pixel = static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x);
            if (depth >= depthBuffer[pixel]) {
                w0 += w0StepX;
                w1 += w1StepX;
                continue;
            }
            depthBuffer[pixel] = depth;
            const std::size_t offset = pixel * 4U;
            const float colorWeight0 = w0 * invZ0 / reciprocalDepth;
            const float colorWeight1 = w1 * invZ1 / reciprocalDepth;
            const float colorWeight2 = w2 * invZ2 / reciprocalDepth;
            const auto interpolateChannel = [colorWeight0, colorWeight1, colorWeight2](
                                                Uint8 first, Uint8 second, Uint8 third) {
                return static_cast<Uint8>(std::clamp(
                    static_cast<float>(first) * colorWeight0 +
                    static_cast<float>(second) * colorWeight1 +
                    static_cast<float>(third) * colorWeight2, 0.0F, 255.0F));
            };
            SDL_Color texel{255, 255, 255, 255};
            if (texture != nullptr && texCoords != nullptr) {
                const float u = ((*texCoords)[0].x * w0 * invZ0 +
                                 (*texCoords)[1].x * w1 * invZ1 +
                                 (*texCoords)[2].x * w2 * invZ2) / reciprocalDepth;
                const float v = ((*texCoords)[0].y * w0 * invZ0 +
                                 (*texCoords)[1].y * w1 * invZ1 +
                                 (*texCoords)[2].y * w2 * invZ2) / reciprocalDepth;
                texel = sampleTexture(*texture, u, v);
            }
            colorBuffer[offset] = static_cast<Uint8>(
                static_cast<unsigned int>(interpolateChannel(colors[0].r, colors[1].r, colors[2].r)) * texel.r / 255U);
            colorBuffer[offset + 1U] = static_cast<Uint8>(
                static_cast<unsigned int>(interpolateChannel(colors[0].g, colors[1].g, colors[2].g)) * texel.g / 255U);
            colorBuffer[offset + 2U] = static_cast<Uint8>(
                static_cast<unsigned int>(interpolateChannel(colors[0].b, colors[1].b, colors[2].b)) * texel.b / 255U);
            colorBuffer[offset + 3U] = static_cast<Uint8>(
                static_cast<unsigned int>(interpolateChannel(colors[0].a, colors[1].a, colors[2].a)) * texel.a / 255U);
            w0 += w0StepX;
            w1 += w1StepX;
        }
        rowW0 += w0StepY;
        rowW1 += w1StepY;
    }
}


void rasterizeDepthLine(
    int width, int height,
    const SDL_FPoint& a, float depthA,
    const SDL_FPoint& b, float depthB,
    SDL_Color color,
    std::vector<Uint8>& colorBuffer,
    std::vector<float>& depthBuffer) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const int steps = static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy))));
    if (steps <= 0) {
        return;
    }

    const float invDepthA = 1.0F / depthA;
    const float invDepthB = 1.0F / depthB;
    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const int x = static_cast<int>(std::floor(a.x + dx * t));
        const int y = static_cast<int>(std::floor(a.y + dy * t));
        if (x < 0 || x >= width || y < 0 || y >= height) {
            continue;
        }

        const float reciprocalDepth = invDepthA + (invDepthB - invDepthA) * t;
        if (reciprocalDepth <= 0.0F) {
            continue;
        }
        const float depth = 1.0F / reciprocalDepth;
        const std::size_t pixel = static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x);
        if (depth >= depthBuffer[pixel]) {
            continue;
        }
        depthBuffer[pixel] = depth;
        const std::size_t offset = pixel * 4U;
        colorBuffer[offset] = color.r;
        colorBuffer[offset + 1U] = color.g;
        colorBuffer[offset + 2U] = color.b;
        colorBuffer[offset + 3U] = color.a;
    }
}

} // namespace

Renderer3D::Renderer3D(SDL_Renderer* renderer)
    : renderer_(renderer) {
    if (renderer_ == nullptr) {
        throw std::invalid_argument("Renderer3D requires a valid SDL_Renderer.");
    }
}

Renderer3D::~Renderer3D() {
    if (depthTexture_ != nullptr) {
        SDL_DestroyTexture(depthTexture_);
    }
}

SDL_Point Renderer3D::softwareRenderSize(int width, int height) noexcept {
    constexpr std::size_t maximumPixels = 800U * 450U;
    if (width <= 0 || height <= 0) {
        return {0, 0};
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height);
    if (pixelCount <= maximumPixels) {
        return {width, height};
    }

    const float scale = std::sqrt(static_cast<float>(maximumPixels) /
                                  static_cast<float>(pixelCount));
    return {
        std::max(1, static_cast<int>(std::floor(static_cast<float>(width) * scale))),
        std::max(1, static_cast<int>(std::floor(static_cast<float>(height) * scale)))
    };
}

bool Renderer3D::render(int width, int height, double elapsedSeconds,
                        const Camera& camera,
                        const std::vector<MeshInstance>& meshInstances, bool wireframe) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (!wireframe) {
        const SDL_Point softwareSize = softwareRenderSize(width, height);
        width = softwareSize.x;
        height = softwareSize.y;
    }

    if (previousElapsedSeconds_ > 0.0) {
        const double frameSeconds = elapsedSeconds - previousElapsedSeconds_;
        if (frameSeconds > 0.0001) {
            const float instantaneousFps = static_cast<float>(1.0 / frameSeconds);
            smoothedFramesPerSecond_ = smoothedFramesPerSecond_ <= 0.0F
                ? instantaneousFps
                : smoothedFramesPerSecond_ * 0.9F + instantaneousFps * 0.1F;
        }
    }
    previousElapsedSeconds_ = elapsedSeconds;

    // Deep cyan atmosphere shared with the GPU backend.
    SDL_SetRenderDrawColor(renderer_, 5, 28, 48, 255);
    SDL_RenderClear(renderer_);

    const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height);
    if (colorBuffer_.size() != pixelCount * 4U ||
        depthBuffer_.size() != pixelCount) {
        try {
            colorBuffer_.resize(pixelCount * 4U);
            depthBuffer_.resize(pixelCount);
        } catch (const std::bad_alloc&) {
            SDL_Log("Could not allocate software render buffers.");
            return false;
        } catch (const std::length_error&) {
            SDL_Log("Software render buffer dimensions exceed the supported size.");
            return false;
        }
    }

    // Wireframe rendering also uses the software depth buffer for the rotating
    // sample cube. Allocate that buffer independently from the SDL texture,
    // which is only needed by the filled software-rendering path.
    if (!wireframe && (depthTexture_ == nullptr ||
                       textureWidth_ != width || textureHeight_ != height)) {
        if (depthTexture_ != nullptr) {
            SDL_DestroyTexture(depthTexture_);
            depthTexture_ = nullptr;
        }
        depthTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, width, height);
        if (depthTexture_ == nullptr) {
            SDL_Log("Could not create depth-buffer texture: %s", SDL_GetError());
            return false;
        }
        if (!SDL_SetTextureBlendMode(depthTexture_, SDL_BLENDMODE_BLEND)) {
            SDL_Log("Could not set depth-buffer texture blend mode: %s", SDL_GetError());
            SDL_DestroyTexture(depthTexture_);
            depthTexture_ = nullptr;
            return false;
        }
        textureWidth_ = width;
        textureHeight_ = height;
    }

    std::fill(colorBuffer_.begin(), colorBuffer_.end(), Uint8{0});
    std::fill(depthBuffer_.begin(), depthBuffer_.end(),
              std::numeric_limits<float>::infinity());

    const float scale = static_cast<float>(std::min(width, height)) * 0.42F;
    const float centerX = static_cast<float>(width) * 0.5F;
    const float centerY = static_cast<float>(height) * 0.5F;
    constexpr float focalLength = 2.4F;
    constexpr float nearPlane = 0.20F;

    auto project = [&](Vec3 p) -> SDL_FPoint {
        const float perspective = focalLength / p.z;
        return {centerX + p.x * scale * perspective,
                centerY - p.y * scale * perspective};
    };

    // Collect world-space floor-grid lines; filled mode depth-tests them with meshes.
    struct GridLine {
        SDL_FPoint a;
        SDL_FPoint b;
        float depthA;
        float depthB;
    };
    std::vector<GridLine> gridLines;
    auto addGridLine = [&](Vec3 a, Vec3 b) {
        Vec3 cameraA = toCameraSpace(a, camera);
        Vec3 cameraB = toCameraSpace(b, camera);
        if (cameraA.z <= nearPlane && cameraB.z <= nearPlane) {
            return;
        }
        if (cameraA.z <= nearPlane || cameraB.z <= nearPlane) {
            Vec3& outside = cameraA.z <= nearPlane ? cameraA : cameraB;
            const Vec3& inside = cameraA.z <= nearPlane ? cameraB : cameraA;
            const float t = (nearPlane - outside.z) / (inside.z - outside.z);
            outside = {
                outside.x + (inside.x - outside.x) * t,
                outside.y + (inside.y - outside.y) * t,
                nearPlane
            };
        }
        gridLines.push_back({project(cameraA), project(cameraB), cameraA.z, cameraB.z});
    };

    constexpr float floorY = -1.5F;
    for (int i = -20; i <= 20; ++i) {
        const float coordinate = static_cast<float>(i);
        addGridLine({coordinate, floorY, -20.0F}, {coordinate, floorY, 40.0F});
    }
    for (int i = -2; i <= 40; ++i) {
        const float z = static_cast<float>(i);
        addGridLine({-20.0F, floorY, z}, {20.0F, floorY, z});
    }

    const std::array<Vec3, 8> cube{{
        {-1.0F, -1.0F, -1.0F}, { 1.0F, -1.0F, -1.0F},
        { 1.0F,  1.0F, -1.0F}, {-1.0F,  1.0F, -1.0F},
        {-1.0F, -1.0F,  1.0F}, { 1.0F, -1.0F,  1.0F},
        { 1.0F,  1.0F,  1.0F}, {-1.0F,  1.0F,  1.0F}
    }};

    std::array<Vec3, 8> transformed{};
    const Transform cubeTransform = makeDemoCubeTransform(elapsedSeconds);
    for (std::size_t i = 0; i < cube.size(); ++i) {
        Vec3 p{
            cube[i].x * cubeTransform.scale.x,
            cube[i].y * cubeTransform.scale.y,
            cube[i].z * cubeTransform.scale.z
        };
        p = rotateX(p, cubeTransform.rotation.x);
        p = rotateY(p, cubeTransform.rotation.y);
        p = rotateZ(p, cubeTransform.rotation.z);
        p.x += cubeTransform.position.x;
        p.y += cubeTransform.position.y;
        p.z += cubeTransform.position.z;
        transformed[i] = toCameraSpace(p, camera);
    }

    const std::array<Face, 6> baseFaces{{
        {{{0, 1, 2, 3}}, {36, 205, 255, 255}},
        {{{5, 4, 7, 6}}, {72, 235, 255, 255}},
        {{{4, 0, 3, 7}}, {18, 145, 205, 255}},
        {{{1, 5, 6, 2}}, {45, 220, 255, 255}},
        {{{3, 2, 6, 7}}, {95, 245, 255, 255}},
        {{{4, 5, 1, 0}}, {24, 180, 235, 255}}
    }};

    const std::array<Face, 6>& faces = baseFaces;
    std::size_t renderedTriangles = 0;
    std::size_t culledTriangles = 0;
    for (const Face& face : faces) {
        std::vector<Vec3> polygon;
        polygon.reserve(face.indices.size());
        for (const int index : face.indices) {
            polygon.push_back(transformed[static_cast<std::size_t>(index)]);
        }
        polygon = clipAgainstNearPlane(polygon, nearPlane);
        if (polygon.size() < 3) {
            continue;
        }

        if (wireframe) {
            const SDL_Color wireColor{220, 232, 250, 255};
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Vec3& pointA = polygon[i];
                const Vec3& pointB = polygon[(i + 1) % polygon.size()];
                rasterizeDepthLine(width, height, project(pointA), pointA.z,
                                   project(pointB), pointB.z, wireColor,
                                   colorBuffer_, depthBuffer_);
            }
        } else {
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                const std::array<Vec3, 3> points{polygon[0], polygon[i], polygon[i + 1]};
                std::array<SDL_FPoint, 3> projected{};
                std::array<float, 3> depths{};
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    projected[corner] = project(points[corner]);
                    depths[corner] = points[corner].z;
                }
                const std::array<SDL_Color, 3> faceColors{
                    face.color, face.color, face.color
                };
                rasterizeTriangle(width, height, projected, depths, faceColors,
                                  colorBuffer_, depthBuffer_);
                ++renderedTriangles;
            }
        }
    }

    // Draw every visible scene object that has a valid mesh and transform.
    for (const MeshInstance& instance : meshInstances) {
        if (instance.mesh == nullptr || instance.object == nullptr ||
            !instance.object->visible || instance.mesh->empty()) {
            continue;
        }
        const Mesh& mesh = *instance.mesh;
        const Transform& meshTransform = instance.object->transform;
        const TextureImage* meshTexture = nullptr;
        if (!instance.texturePath.empty()) {
            auto cached = textureCache_.find(instance.texturePath);
            if (cached == textureCache_.end()) {
                TextureImage loaded;
                if (loadTexture(instance.texturePath, loaded)) {
                    cached = textureCache_.emplace(instance.texturePath, std::move(loaded)).first;
                } else {
                    SDL_Log("Could not load texture '%s' (working directory: '%s'); "
                            "supported formats are P3 PPM and BMP. Rendering without texture.",
                            instance.texturePath.c_str(), std::filesystem::current_path().string().c_str());
                    cached = textureCache_.emplace(instance.texturePath, TextureImage{}).first;
                }
            }
            if (cached->second.width > 0) meshTexture = &cached->second;
        }
        struct TriangleToDraw {
            std::array<SDL_Vertex, 3> vertices;
            std::array<float, 3> depths;
            std::array<SDL_Color, 3> colors{};
            std::array<SDL_FPoint, 3> texCoords{};
            bool hasTexCoords{false};
        };
        std::vector<TriangleToDraw> triangles;
        triangles.reserve(mesh.triangles.size());

        for (const MeshTriangle& triangle : mesh.triangles) {
            std::array<TexturedClipVertex, 3> sourcePolygon{};
            bool valid = true;
            std::array<const MeshVertex*, 3> sourceVertices{};
            std::size_t sourceCorner = 0;
            for (const std::size_t index : triangle.indices) {
                if (index >= mesh.vertices.size()) {
                    valid = false;
                    break;
                }
                const MeshVertex& vertex = mesh.vertices[index];
                sourceVertices[sourceCorner++] = &vertex;
                Vec3 point{
                    vertex.x * meshTransform.scale.x,
                    vertex.y * meshTransform.scale.y,
                    vertex.z * meshTransform.scale.z
                };
                point = rotateX(point, meshTransform.rotation.x);
                point = rotateY(point, meshTransform.rotation.y);
                point = rotateZ(point, meshTransform.rotation.z);
                point.x += meshTransform.position.x;
                point.y += meshTransform.position.y;
                point.z += meshTransform.position.z;
                point = toCameraSpace(point, camera);
                sourcePolygon[sourceCorner - 1] = {point, vertex.u, vertex.v};
            }
            if (!valid) {
                continue;
            }
            const bool triangleFullyInFront = std::all_of(
                sourcePolygon.begin(), sourcePolygon.end(),
                [nearPlane](const TexturedClipVertex& vertex) {
                    return vertex.position.z > nearPlane;
                });
            const bool triangleHasTexCoords = meshTexture != nullptr &&
                sourceVertices[0]->hasTexCoord &&
                sourceVertices[1]->hasTexCoord &&
                sourceVertices[2]->hasTexCoord;
            const std::vector<TexturedClipVertex> polygon =
                clipTexturedTriangleAgainstNearPlane(sourcePolygon, nearPlane);
            if (polygon.size() < 3) {
                continue;
            }

            for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                const TexturedClipVertex clippedVertices[3] = {
                    polygon[0], polygon[i], polygon[i + 1]
                };
                const Vec3 points[3] = {
                    clippedVertices[0].position,
                    clippedVertices[1].position,
                    clippedVertices[2].position
                };

                // Flat diffuse lighting from the triangle's camera-space face normal.
                const Vec3 edgeA{
                    points[1].x - points[0].x,
                    points[1].y - points[0].y,
                    points[1].z - points[0].z
                };
                const Vec3 edgeB{
                    points[2].x - points[0].x,
                    points[2].y - points[0].y,
                    points[2].z - points[0].z
                };
                Vec3 normal{
                    edgeA.y * edgeB.z - edgeA.z * edgeB.y,
                    edgeA.z * edgeB.x - edgeA.x * edgeB.z,
                    edgeA.x * edgeB.y - edgeA.y * edgeB.x
                };
                const float normalLength = std::sqrt(
                    normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);

                // Back-face culling: the outward face normal must point toward
                // the camera. Keep wireframe double-sided for debugging.
                if (!wireframe && normalLength > 1.0e-6F) {
                    const Vec3 toCamera{
                        -(points[0].x + points[1].x + points[2].x) / 3.0F,
                        -(points[0].y + points[1].y + points[2].y) / 3.0F,
                        -(points[0].z + points[1].z + points[2].z) / 3.0F
                    };
                    const float facing = normal.x * toCamera.x +
                                         normal.y * toCamera.y +
                                         normal.z * toCamera.z;
                    if (facing <= 0.0F) {
                        ++culledTriangles;
                        continue;
                    }
                }

                if (normalLength > 1.0e-6F) {
                    normal.x /= normalLength;
                    normal.y /= normalLength;
                    normal.z /= normalLength;
                }

                constexpr Vec3 lightDirection{0.35F, 0.75F, -0.55F};
                constexpr float lightLength = 0.99624294F;
                const float diffuse = std::abs(
                    normal.x * lightDirection.x +
                    normal.y * lightDirection.y +
                    normal.z * lightDirection.z) / lightLength;
                const float intensity = 0.24F + 0.76F * std::clamp(diffuse, 0.0F, 1.0F);

                TriangleToDraw item{};
                constexpr SDL_Color baseColor{72, 190, 238, 255};
                const SDL_Color flatColor{
                    static_cast<Uint8>(static_cast<float>(baseColor.r) * intensity),
                    static_cast<Uint8>(static_cast<float>(baseColor.g) * intensity),
                    static_cast<Uint8>(static_cast<float>(baseColor.b) * intensity),
                    baseColor.a
                };

                // Use OBJ vn normals for Gouraud shading when the original triangle
                // is fully in front of the near plane. Clipped triangles retain the
                // robust flat-shading path because clipping also creates new vertices.
                const bool hasVertexNormals =
                    triangleFullyInFront &&
                    polygon.size() == 3 &&
                    sourceVertices[0] != nullptr && sourceVertices[0]->hasNormal &&
                    sourceVertices[1] != nullptr && sourceVertices[1]->hasNormal &&
                    sourceVertices[2] != nullptr && sourceVertices[2]->hasNormal;
                item.hasTexCoords = triangleHasTexCoords;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    SDL_Color vertexColor = flatColor;
                    if (hasVertexNormals) {
                        const MeshVertex& source = *sourceVertices[corner];
                        Vec3 normal{
                            std::abs(meshTransform.scale.x) > 1.0e-6F
                                ? source.nx / meshTransform.scale.x : 0.0F,
                            std::abs(meshTransform.scale.y) > 1.0e-6F
                                ? source.ny / meshTransform.scale.y : 0.0F,
                            std::abs(meshTransform.scale.z) > 1.0e-6F
                                ? source.nz / meshTransform.scale.z : 0.0F
                        };
                        normal = rotateX(normal, meshTransform.rotation.x);
                        normal = rotateY(normal, meshTransform.rotation.y);
                        normal = rotateZ(normal, meshTransform.rotation.z);
                        normal = rotateY(normal, -camera.yaw());
                        normal = rotateX(normal, camera.pitch());
                        const float length = std::sqrt(
                            normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                        if (length > 1.0e-6F) {
                            normal.x /= length;
                            normal.y /= length;
                            normal.z /= length;
                            constexpr Vec3 lightDirection{0.35F, 0.75F, -0.55F};
                            constexpr float lightLength = 0.99624294F;
                            const float vertexDiffuse = std::abs(
                                normal.x * lightDirection.x +
                                normal.y * lightDirection.y +
                                normal.z * lightDirection.z) / lightLength;
                            const float vertexIntensity =
                                0.24F + 0.76F * std::clamp(vertexDiffuse, 0.0F, 1.0F);
                            vertexColor = {
                                static_cast<Uint8>(static_cast<float>(baseColor.r) * vertexIntensity),
                                static_cast<Uint8>(static_cast<float>(baseColor.g) * vertexIntensity),
                                static_cast<Uint8>(static_cast<float>(baseColor.b) * vertexIntensity),
                                baseColor.a
                            };
                        }
                    }
                    item.colors[corner] = vertexColor;
                    item.vertices[corner] = makeVertex(points[corner], vertexColor, project);
                    item.depths[corner] = points[corner].z;
                    item.texCoords[corner] = {
                        clippedVertices[corner].u, clippedVertices[corner].v
                    };
                }
                triangles.push_back(item);
                ++renderedTriangles;
            }
        }

        for (const TriangleToDraw& triangle : triangles) {
            if (wireframe) {
                SDL_SetRenderDrawColor(renderer_, 120, 220, 255, 255);
                for (std::size_t i = 0; i < triangle.vertices.size(); ++i) {
                    const SDL_FPoint a = triangle.vertices[i].position;
                    const SDL_FPoint b = triangle.vertices[(i + 1) % triangle.vertices.size()].position;
                    if (!SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y)) {
                        SDL_Log("SDL_RenderLine for wireframe mesh failed: %s", SDL_GetError());
                        return false;
                    }
                }
            } else {
                std::array<SDL_FPoint, 3> projected{};
                std::array<float, 3> depths{};
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    projected[corner] = triangle.vertices[corner].position;
                    depths[corner] = triangle.depths[corner];
                }
                rasterizeTriangle(width, height, projected, depths, triangle.colors,
                                  colorBuffer_, depthBuffer_,
                                  triangle.hasTexCoords ? &triangle.texCoords : nullptr,
                                  triangle.hasTexCoords ? meshTexture : nullptr);
            }
        }
    }

    if (!wireframe) {
        const SDL_Color gridColor{35, 190, 235, 255};
        for (const GridLine& line : gridLines) {
            rasterizeDepthLine(width, height, line.a, line.depthA,
                               line.b, line.depthB, gridColor,
                               colorBuffer_, depthBuffer_);
        }
        if (!SDL_UpdateTexture(depthTexture_, nullptr, colorBuffer_.data(), width * 4)) {
            SDL_Log("SDL_UpdateTexture for depth buffer failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_RenderTexture(renderer_, depthTexture_, nullptr, nullptr)) {
            SDL_Log("SDL_RenderTexture for depth buffer failed: %s", SDL_GetError());
            return false;
        }
    }

    // SDL3's built-in debug font keeps the overlay dependency-free.
    SDL_SetRenderDrawColor(renderer_, 232, 239, 250, 255);
    char fpsText[64]{};
    char positionText[96]{};
    char sensitivityText[64]{};
    std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", smoothedFramesPerSecond_);
    std::snprintf(positionText, sizeof(positionText),
                  "Camera: X %.2f  Y %.2f  Z %.2f",
                  static_cast<double>(camera.x()),
                  static_cast<double>(camera.y()),
                  static_cast<double>(camera.z()));
    (void)SDL_RenderDebugText(renderer_, 12.0F, 12.0F, fpsText);
    (void)SDL_RenderDebugText(renderer_, 12.0F, 28.0F, positionText);
    std::snprintf(sensitivityText, sizeof(sensitivityText), "Mouse sensitivity: %.4f",
                  static_cast<double>(camera.mouseSensitivity()));
    (void)SDL_RenderDebugText(renderer_, 12.0F, 44.0F, sensitivityText);
    char statsText[128]{};
    const std::size_t visibleObjects = static_cast<std::size_t>(std::count_if(
        meshInstances.begin(), meshInstances.end(), [](const MeshInstance& instance) {
            return instance.mesh != nullptr && instance.object != nullptr &&
                   instance.object->visible && !instance.mesh->empty();
        }));
    std::snprintf(statsText, sizeof(statsText),
                  "Triangles: %zu  Culled: %zu  Objects: %zu  Render: %dx%d",
                  renderedTriangles, culledTriangles, visibleObjects, width, height);
    (void)SDL_RenderDebugText(renderer_, 12.0F, 60.0F, statsText);
    (void)SDL_RenderDebugText(renderer_, 12.0F, 76.0F,
                              "WASD Move | Q/E Up/Down | Shift Sprint");
    (void)SDL_RenderDebugText(renderer_, 12.0F, 92.0F,
                              "+/- Mouse Sensitivity | 1/2 Toggle | F3 Wireframe | F11 Fullscreen");
    if (wireframe) {
        (void)SDL_RenderDebugText(renderer_, 12.0F, 108.0F, "Wireframe: ON");
    }

    // The application owns frame presentation so 2D overlays can be drawn first.
    return true;
}

} // namespace originalengine
