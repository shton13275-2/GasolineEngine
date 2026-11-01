#pragma once

#include <vector>

namespace originalengine {

struct Aabb {
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

// Axis-aligned collision volumes with a simple sphere-radius approximation.
class CollisionWorld {
public:
    void addObstacle(Aabb obstacle);
    [[nodiscard]] bool overlaps(float x, float y, float z, float radius) const noexcept;

    // Resolves one axis at a time, stopping at first contact to prevent AABB tunneling.
    [[nodiscard]] float resolveAxis(float currentX, float currentY, float currentZ,
                                    float displacement, float radius,
                                    int axis) const noexcept;

private:
    std::vector<Aabb> obstacles_;
};

} // namespace originalengine
