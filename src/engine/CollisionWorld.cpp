#include "engine/CollisionWorld.hpp"

#include <algorithm>

namespace originalengine {

void CollisionWorld::addObstacle(Aabb obstacle) {
    if (obstacle.minX > obstacle.maxX || obstacle.minY > obstacle.maxY ||
        obstacle.minZ > obstacle.maxZ) {
        return;
    }
    obstacles_.push_back(obstacle);
}

bool CollisionWorld::overlaps(float x, float y, float z, float radius) const noexcept {
    radius = std::max(radius, 0.0F);
    for (const Aabb& box : obstacles_) {
        if (x + radius > box.minX && x - radius < box.maxX &&
            y + radius > box.minY && y - radius < box.maxY &&
            z + radius > box.minZ && z - radius < box.maxZ) {
            return true;
        }
    }
    return false;
}

float CollisionWorld::resolveAxis(float currentX, float currentY, float currentZ,
                                  float displacement, float radius,
                                  int axis) const noexcept {
    if (axis < 0 || axis > 2) {
        return 0.0F;
    }

    radius = std::max(radius, 0.0F);
    const float current = axis == 0 ? currentX : (axis == 1 ? currentY : currentZ);
    const float target = current + displacement;
    float resolvedDisplacement = displacement;

    const auto overlapsRange = [radius](float center, float minimum, float maximum) {
        return center + radius > minimum && center - radius < maximum;
    };

    for (const Aabb& box : obstacles_) {
        float boxMinimum = 0.0F;
        float boxMaximum = 0.0F;
        bool overlapsOtherAxes = false;

        if (axis == 0) {
            boxMinimum = box.minX;
            boxMaximum = box.maxX;
            overlapsOtherAxes =
                overlapsRange(currentY, box.minY, box.maxY) &&
                overlapsRange(currentZ, box.minZ, box.maxZ);
        } else if (axis == 1) {
            boxMinimum = box.minY;
            boxMaximum = box.maxY;
            overlapsOtherAxes =
                overlapsRange(currentX, box.minX, box.maxX) &&
                overlapsRange(currentZ, box.minZ, box.maxZ);
        } else {
            boxMinimum = box.minZ;
            boxMaximum = box.maxZ;
            overlapsOtherAxes =
                overlapsRange(currentX, box.minX, box.maxX) &&
                overlapsRange(currentY, box.minY, box.maxY);
        }

        if (!overlapsOtherAxes) {
            continue;
        }

        const bool startsInsideAxis =
            current + radius > boxMinimum && current - radius < boxMaximum;
        const bool endsInsideAxis =
            target + radius > boxMinimum && target - radius < boxMaximum;

        // Preserve the ability to move out of an existing overlap, but do not
        // allow another step that would leave the camera inside the obstacle.
        if (startsInsideAxis) {
            if (endsInsideAxis) {
                return 0.0F;
            }
            continue;
        }

        if (displacement > 0.0F) {
            const float contactDistance = boxMinimum - radius - current;
            if (contactDistance >= 0.0F && displacement > contactDistance) {
                resolvedDisplacement = std::min(resolvedDisplacement, contactDistance);
            }
        } else if (displacement < 0.0F) {
            const float contactDistance = boxMaximum + radius - current;
            if (contactDistance <= 0.0F && displacement < contactDistance) {
                resolvedDisplacement = std::max(resolvedDisplacement, contactDistance);
            }
        }
    }

    return resolvedDisplacement;
}

} // namespace originalengine
