#pragma once
#include "raylib.h"

// A position in the level's fixed world frame: X forward, Y across, Z height.
// Camera or view changes must never mutate these coordinates.
struct WorldPosition {
    float x = 0.f, y = 0.f, z = 0.f;
};

// A provider lets static, attached and moving objects supply locations in the same frame
// without requiring the camera or targeting system to know how they are moved.
class WorldPositionProvider {
public:
    virtual ~WorldPositionProvider() = default;
    virtual WorldPosition worldPosition() const = 0;
};

// Projection into the gameplay plane. X is always forward; lateral is screen-down in
// side view and screen-right in top view. Hidden world coordinates remain unchanged.
enum class WorldProjection { Side, Top };
inline Vector2 projectWorld(WorldPosition position, WorldProjection projection) {
    return projection == WorldProjection::Side ? Vector2{position.x, -position.z}
                                               : Vector2{position.x, position.y};
}

// Compatibility bridge for legacy levels until their objects store full world coordinates.
// The hidden coordinate is explicitly supplied rather than inferred from camera position.
inline WorldPosition worldFromLegacy(Vector2 local, WorldProjection projection, float hidden = 0.f) {
    return projection == WorldProjection::Side ? WorldPosition{local.x, hidden, -local.y}
                                               : WorldPosition{local.x, local.y, hidden};
}
