#include "engine/PpmImageLoader.hpp"

#include <iostream>
#include <limits>
#include <sstream>
#include <string>

int main() {
    std::istringstream ppm(
        "P3\n"
        "# header comment\n"
        "2 1\n"
        "15\n"
        "15 0 0  0 7 15\n");
    originalengine::RgbaImage image;
    std::string error;
    if (!originalengine::PpmImageLoader::loadFromStream(ppm, image, &error)) {
        std::cerr << "P3 load failed: " << error << '\n';
        return 1;
    }
    const unsigned char expected[]{255, 0, 0, 255, 0, 119, 255, 255};
    if (image.width != 2 || image.height != 1 || image.pixels.size() != sizeof(expected)) {
        std::cerr << "Unexpected decoded image dimensions or byte count.\n";
        return 1;
    }
    for (std::size_t i = 0; i < sizeof(expected); ++i) {
        if (image.pixels[i] != expected[i]) {
            std::cerr << "Unexpected RGBA channel at index " << i << ".\n";
            return 1;
        }
    }

    if (!image.hasValidRgba8Data()) {
        std::cerr << "Decoded image should contain tightly packed RGBA8 data.\n";
        return 1;
    }
    originalengine::RgbaImage shortImage = image;
    shortImage.pixels.pop_back();
    if (shortImage.hasValidRgba8Data()) {
        std::cerr << "RGBA8 data with an incorrect byte count should be rejected.\n";
        return 1;
    }
    originalengine::RgbaImage oversizedImage;
    oversizedImage.width = std::numeric_limits<std::uint32_t>::max();
    oversizedImage.height = std::numeric_limits<std::uint32_t>::max();
    if (oversizedImage.hasValidRgba8Data()) {
        std::cerr << "RGBA8 dimensions exceeding the SDL upload limit should be rejected.\n";
        return 1;
    }

    const originalengine::RgbaImage preserved = image;
    std::istringstream invalid("P6\n1 1\n255\n");
    if (originalengine::PpmImageLoader::loadFromStream(invalid, image, &error) ||
        image.width != preserved.width || image.height != preserved.height ||
        image.pixels != preserved.pixels) {
        std::cerr << "Unsupported PPM data should fail without replacing output.\n";
        return 1;
    }

    std::cout << "PPM image loader tests passed.\n";
    return 0;
}
