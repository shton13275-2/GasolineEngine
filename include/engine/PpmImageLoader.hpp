#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

namespace originalengine {

struct RgbaImage {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const noexcept {
        return width == 0 || height == 0 || pixels.empty();
    }

    [[nodiscard]] bool hasValidRgba8Data() const noexcept {
        if (width == 0 || height == 0) return false;
        const std::uint64_t pixelCount =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (pixelCount > std::numeric_limits<std::uint32_t>::max() / 4U) {
            return false;
        }
        const std::uint64_t expectedBytes = pixelCount * 4U;
        return static_cast<std::uint64_t>(pixels.size()) == expectedBytes;
    }
};

// Loads ASCII P3 Portable Pixmap images into tightly packed RGBA8 pixels.
class PpmImageLoader {
public:
    [[nodiscard]] static bool loadFromFile(
        const std::filesystem::path& path, RgbaImage& output, std::string* error = nullptr);
    [[nodiscard]] static bool loadFromStream(
        std::istream& input, RgbaImage& output, std::string* error = nullptr);
};

} // namespace originalengine
