#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace originalengine::touch_overlay {

// Shared screen-space layout and palette used by both CPU and GPU renderers.
inline constexpr float movementCenterX = 0.20F;
inline constexpr float controlCenterY = 0.78F;
inline constexpr float movementRadius = 0.14F;
inline constexpr float movementGuideRadius = 0.58F;
inline constexpr float movementInnerRadius = 0.72F;
inline constexpr float knobRadius = 0.22F;
inline constexpr float lookCenterX = 0.82F;
inline constexpr float lookRadius = 0.12F;
inline constexpr float lookGuideRadius = 0.48F;
inline constexpr float lookInnerRadius = 0.78F;

inline constexpr float fpsX = 20.0F;
inline constexpr float fpsY = 20.0F;
inline constexpr float backendButtonRight = 18.0F;
inline constexpr float backendButtonY = 18.0F;
inline constexpr float backendButtonWidth = 98.0F;
inline constexpr float backendButtonHeight = 42.0F;
inline constexpr float ssrButtonRight = backendButtonRight + backendButtonWidth + 8.0F;
inline constexpr float ssrButtonWidth = 72.0F;
inline constexpr float fontPixelSize = 2.0F;
inline constexpr float fontAdvance = 12.0F;
inline constexpr float fontHeight = 10.0F;

// Tests normalized SDL finger coordinates against the visible backend-switch
// rectangle. The right and bottom edges are exclusive, matching pixel bounds.
inline constexpr bool containsSsrButton(
    float normalizedX, float normalizedY,
    float viewportWidth, float viewportHeight) noexcept {
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F) return false;
    const float left = (viewportWidth - ssrButtonRight - ssrButtonWidth) / viewportWidth;
    const float right = (viewportWidth - ssrButtonRight) / viewportWidth;
    const float top = backendButtonY / viewportHeight;
    const float bottom = (backendButtonY + backendButtonHeight) / viewportHeight;
    return normalizedX >= left && normalizedX < right &&
           normalizedY >= top && normalizedY < bottom;
}

inline constexpr bool containsBackendButton(
    float normalizedX, float normalizedY,
    float viewportWidth, float viewportHeight) noexcept {
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F) {
        return false;
    }
    const float left = (viewportWidth - backendButtonRight - backendButtonWidth) /
                       viewportWidth;
    const float right = (viewportWidth - backendButtonRight) / viewportWidth;
    const float top = backendButtonY / viewportHeight;
    const float bottom = (backendButtonY + backendButtonHeight) / viewportHeight;
    return normalizedX >= left && normalizedX < right &&
           normalizedY >= top && normalizedY < bottom;
}

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

inline constexpr Color ringColor{90, 178, 232, 150};
inline constexpr Color guideColor{90, 178, 232, 105};
inline constexpr Color knobColor{120, 205, 255, 230};
inline constexpr Color buttonColor{28, 112, 160, 220};
inline constexpr Color ssrDisabledButtonColor{70, 82, 92, 210};
inline constexpr Color textColor{245, 250, 255, 255};

inline constexpr std::array<float, 4> normalized(Color color) {
    constexpr float scale = 1.0F / 255.0F;
    return {color.r * scale, color.g * scale, color.b * scale, color.a * scale};
}

// 3x5 bitmap glyphs are intentionally shared so text is pixel-identical.
inline constexpr std::string_view glyph(char character) {
    switch (character) {
    case 'C': return "011/100/100/100/011";
    case 'U': return "101/101/101/101/111";
    case 'F': return "111/100/110/100/100";
    case 'G': return "011/100/101/101/011";
    case 'P': return "110/101/110/100/100";
    case 'R': return "110/101/110/101/101";
    case 'S': return "111/100/111/001/111";
    case ':': return "000/010/000/010/000";
    case '0': return "111/101/101/101/111";
    case '1': return "010/110/010/010/111";
    case '2': return "110/001/010/100/111";
    case '3': return "110/001/010/001/110";
    case '4': return "101/101/111/001/001";
    case '5': return "111/100/110/001/110";
    case '6': return "011/100/110/101/010";
    case '7': return "111/001/010/010/010";
    case '8': return "010/101/010/101/010";
    case '9': return "010/101/011/001/110";
    default:  return "000/000/000/000/000";
    }
}

} // namespace originalengine::touch_overlay