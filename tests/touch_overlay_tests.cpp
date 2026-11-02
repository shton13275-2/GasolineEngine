#include "engine/TouchOverlay.hpp"

#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace originalengine::touch_overlay;
    bool allPassed = true;

    constexpr float landscapeWidth = 1280.0F;
    constexpr float landscapeHeight = 720.0F;
    const float left = (landscapeWidth - backendButtonRight - backendButtonWidth) /
                       landscapeWidth;
    const float right = (landscapeWidth - backendButtonRight) / landscapeWidth;
    const float top = backendButtonY / landscapeHeight;
    const float bottom = (backendButtonY + backendButtonHeight) / landscapeHeight;
    const float centerX = (left + right) * 0.5F;
    const float centerY = (top + bottom) * 0.5F;

    allPassed &= expect(
        containsBackendButton(centerX, centerY, landscapeWidth, landscapeHeight),
        "The visible backend button center must be clickable.");
    allPassed &= expect(
        containsBackendButton(left, top, landscapeWidth, landscapeHeight),
        "The top-left button edge must be included.");
    allPassed &= expect(
        !containsBackendButton(left - 0.001F, centerY, landscapeWidth, landscapeHeight),
        "A tap immediately left of the button must not toggle the backend.");
    allPassed &= expect(
        !containsBackendButton(centerX, top - 0.001F, landscapeWidth, landscapeHeight),
        "A tap immediately above the button must not toggle the backend.");
    allPassed &= expect(
        !containsBackendButton(right, centerY, landscapeWidth, landscapeHeight),
        "The right button edge must be exclusive.");
    allPassed &= expect(
        !containsBackendButton(centerX, bottom, landscapeWidth, landscapeHeight),
        "The bottom button edge must be exclusive.");
    allPassed &= expect(
        !containsBackendButton(0.80F, 0.10F, landscapeWidth, landscapeHeight),
        "A point in the old broad hit region but outside the button must not toggle.");

    constexpr float portraitWidth = 1170.0F;
    constexpr float portraitHeight = 2532.0F;
    const float portraitCenterX =
        (portraitWidth - backendButtonRight - backendButtonWidth * 0.5F) / portraitWidth;
    const float portraitCenterY =
        (backendButtonY + backendButtonHeight * 0.5F) / portraitHeight;
    allPassed &= expect(
        containsBackendButton(portraitCenterX, portraitCenterY,
                              portraitWidth, portraitHeight),
        "The visible backend button center must remain clickable in portrait layout.");
    allPassed &= expect(
        !containsBackendButton(0.5F, 0.5F, 0.0F, portraitHeight),
        "A zero-width viewport must not produce a hit.");
    allPassed &= expect(
        !containsBackendButton(0.5F, 0.5F, portraitWidth, 0.0F),
        "A zero-height viewport must not produce a hit.");

    if (!allPassed) return 1;
    std::cout << "Touch overlay hit-test tests passed.\n";
    return 0;
}
