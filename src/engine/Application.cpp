#include "engine/Application.hpp"

#include <stdexcept>
#include <utility>

namespace originalengine {

Application::Application(ApplicationConfig config)
    : config_(std::move(config)) {
    if (config_.width <= 0 || config_.height <= 0) {
        throw std::invalid_argument("Application width and height must be positive.");
    }
}

} // namespace originalengine
