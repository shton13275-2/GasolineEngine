#pragma once

namespace originalengine {

// Input state that must be reset when changing render backends. The keyboard
// gate prevents keys held during the transition from continuing movement in
// the newly selected backend until all movement keys have been released.
struct BackendSwitchInputState {
    bool hasMovementFinger{false};
    bool hasLookFinger{false};
    float movementX{0.0F};
    float movementY{0.0F};
    float lookDeltaX{0.0F};
    float lookDeltaY{0.0F};

    void resetForBackendSwitch() noexcept {
        hasMovementFinger = false;
        hasLookFinger = false;
        movementX = 0.0F;
        movementY = 0.0F;
        lookDeltaX = 0.0F;
        lookDeltaY = 0.0F;
        suppressKeyboardMovementUntilReleased_ = true;
    }

    [[nodiscard]] bool shouldSuppressKeyboardMovement(
        bool anyMovementKeyDown) noexcept {
        if (!suppressKeyboardMovementUntilReleased_) {
            return false;
        }
        if (!anyMovementKeyDown) {
            suppressKeyboardMovementUntilReleased_ = false;
            return false;
        }
        return true;
    }

private:
    bool suppressKeyboardMovementUntilReleased_{false};
};

} // namespace originalengine
