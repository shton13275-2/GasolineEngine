#include "engine/CollisionWorld.hpp"

#include <cmath>
#include <iostream>

namespace {
bool nearlyEqual(float a, float b, float epsilon = 0.0001F) {
    return std::fabs(a - b) <= epsilon;
}
}

int main() {
    originalengine::CollisionWorld world;
    world.addObstacle({-1.0F, -1.0F, 3.0F, 1.0F, 1.0F, 5.0F});

    if (!world.overlaps(0.0F, 0.0F, 4.0F, 0.25F)) {
        std::cerr << "Obstacle overlap was not detected.\n";
        return 1;
    }
    if (world.overlaps(0.0F, 0.0F, 2.0F, 0.25F)) {
        std::cerr << "A non-overlapping camera was reported as colliding.\n";
        return 1;
    }

    const float blocked = world.resolveAxis(0.0F, 0.0F, 2.0F, 1.0F, 0.25F, 2);
    if (!nearlyEqual(blocked, 0.75F)) {
        std::cerr << "Movement should stop at the first wall-contact boundary.\n";
        return 1;
    }

    const float positiveTunneling = world.resolveAxis(0.0F, 0.0F, 0.0F, 10.0F, 0.25F, 2);
    if (!nearlyEqual(positiveTunneling, 2.75F)) {
        std::cerr << "A large positive displacement passed through an obstacle.\n";
        return 1;
    }

    const float negativeTunneling = world.resolveAxis(0.0F, 0.0F, 6.0F, -10.0F, 0.25F, 2);
    if (!nearlyEqual(negativeTunneling, -0.75F)) {
        std::cerr << "A large negative displacement passed through an obstacle.\n";
        return 1;
    }

    const float escapeOverlap = world.resolveAxis(0.0F, 0.0F, 4.0F, -4.0F, 0.25F, 2);
    if (!nearlyEqual(escapeOverlap, -4.0F)) {
        std::cerr << "Movement out of an existing overlap should remain possible.\n";
        return 1;
    }

    const float slide = world.resolveAxis(0.0F, 0.0F, 2.0F, 1.0F, 0.25F, 0);
    if (!nearlyEqual(slide, 1.0F)) {
        std::cerr << "Movement parallel to a wall should be allowed.\n";
        return 1;
    }

    if (!nearlyEqual(world.resolveAxis(0.0F, 0.0F, 2.0F, 1.0F, 0.25F, 3), 0.0F)) {
        std::cerr << "An invalid collision axis should be rejected.\n";
        return 1;
    }

    std::cout << "Collision tests passed.\n";
    return 0;
}
