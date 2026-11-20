#pragma once
#include "view.hpp"
#include "world_position.hpp"

// Owns the shared camera projection and conversion from gameplay coordinates to world space.
class CameraSystem {
public:
    // Perspective camera shared by the live game and the world camera. The focal plane (through
    // the camera target, perpendicular to the view) is kViewHeight world units tall and maps 1:1
    // onto the playfield, so depth 0 lands exactly where the old orthographic view put it.
    static constexpr float kViewHeight = 14.f;      // world units across the playfield height
    static constexpr float kCameraDistance = 20.f;  // eye -> focal plane (world_camera_test convention)
    static constexpr float kMaxDepthWorld = 5.0f;   // ship depth range, +/- world units about the focal plane
    static float fovyDegrees(float distance, float viewHeight);
    static Camera3D makeCamera(const View& view, float turn);
    // Gameplay-local position -> render space. `depth` is the ship's hidden axis in gameplay units
    // (positive = toward the camera). The point slides along the camera ray through the local
    // position's screen point, so projection, aim and collision stay in the playfield coordinates
    // while perspective makes nearer objects larger and farther ones smaller.
    static Vector3 toWorld(const View& view, const Camera3D& camera, Vector2 local, float depth = 0.f);
    // World units per gameplay unit at the focal plane, and on-screen size multiplier at a depth.
    static float worldPerLocal(const View& view);
    static float depthScale(const View& view, float depth);
    static float maxDepthLocal(const View& view) { return kMaxDepthWorld / worldPerLocal(view); }
    // Scenery drawn `behindWorld` units beyond the focal plane keeps its former screen layout:
    // the returned transform magnifies it across the view (not along it) about the camera target.
    static Matrix backdropLayer(const Camera3D& camera, float behindWorld);
    // Stable coordinate conversion for true world-space assets (raylib uses Y as up).
    static Vector3 worldToRender(WorldPosition position);
    // World camera uses one fixed coordinate frame throughout a side/top transition.
    static Camera3D makeWorldCamera(WorldPosition target, float topBlend, float distance, float viewHeight);
};

// Stores the camera transition independently from an object's world position.
// Integration into a live level requires world-space movement and collision first.
class ViewTransition {
public:
    explicit ViewTransition(Mode initial = Mode::SideScroll) : to_(initial) {}
    void changeView(Mode view, float durationSeconds);
    void update(float dt);
    float topBlend() const;
    Mode target() const { return to_; }
private:
    Mode to_;
    float startBlend_ = 0.f, elapsed_ = 0.f, duration_ = 0.f;
};
