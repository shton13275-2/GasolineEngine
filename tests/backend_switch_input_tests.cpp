#include "engine/BackendSwitchInput.hpp"

#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    bool allPassed = true;
    originalengine::BackendSwitchInputState input;

    input.hasMovementFinger = true;
    input.hasLookFinger = true;
    input.movementX = 0.31F;
    input.movementY = 0.72F;
    input.lookDeltaX = 12.0F;
    input.lookDeltaY = -8.0F;
    input.resetForBackendSwitch();

    allPassed &= expect(!input.hasMovementFinger && !input.hasLookFinger,
        "Backend switching must cancel active touch controls.");
    allPassed &= expect(input.movementX == 0.0F && input.movementY == 0.0F,
        "Backend switching must clear the stored movement pointer position.");
    allPassed &= expect(input.lookDeltaX == 0.0F && input.lookDeltaY == 0.0F,
        "Backend switching must clear pending camera-look deltas.");

    allPassed &= expect(input.shouldSuppressKeyboardMovement(true),
        "Held movement keys must be suppressed immediately after a backend switch.");
    allPassed &= expect(input.shouldSuppressKeyboardMovement(true),
        "Keyboard movement must remain suppressed while any movement key is held.");
    allPassed &= expect(!input.shouldSuppressKeyboardMovement(false),
        "Keyboard movement suppression must end after all movement keys are released.");
    allPassed &= expect(!input.shouldSuppressKeyboardMovement(true),
        "New keyboard movement input must work after the release boundary.");

    input.resetForBackendSwitch();
    allPassed &= expect(!input.shouldSuppressKeyboardMovement(false),
        "A switch with no held movement keys must release the keyboard gate immediately.");
    allPassed &= expect(input.shouldSuppressKeyboardMovement(true) == false,
        "Keyboard movement must remain available after an idle backend switch.");

    if (!allPassed) return 1;
    std::cout << "Backend switch input tests passed.\n";
    return 0;
}
