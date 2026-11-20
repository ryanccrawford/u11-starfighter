#include "camera_system.hpp"
#include "raymath.h"
#include <cmath>
#include <algorithm>

Vector3 CameraSystem::worldToRender(WorldPosition position) {
    return {position.x, position.z, position.y};
}

Camera3D CameraSystem::makeWorldCamera(WorldPosition target, float topBlend, float distance, float viewHeight) {
    // Side: look along world -Y with +X to screen-right. Top: look along -Z with
    // +X pointing screen-up. Rotate the up vector continuously with the camera so
    // the same world-space forward direction has the correct top-view orientation.
    const float angle = Clamp(topBlend, 0.f, 1.f) * PI * .5f;
    const float d = std::max(distance, .01f);
    Camera3D camera{};
    camera.target = worldToRender(target);
    camera.position = Vector3Add(camera.target, {0.f, d * sinf(angle), d * cosf(angle)});
    const float c = cosf(angle), s = sinf(angle);
    camera.up = {s, c*c, -s*c};
    camera.fovy = fovyDegrees(d, viewHeight);
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

float CameraSystem::fovyDegrees(float distance, float viewHeight) {
    return 2.f * atanf(std::max(viewHeight, .01f) * .5f / std::max(distance, .01f)) * RAD2DEG;
}

void ViewTransition::changeView(Mode view, float durationSeconds) {
    if (view == to_) return;
    startBlend_ = topBlend();
    to_ = view; elapsed_ = 0.f;
    duration_ = std::max(0.f, durationSeconds);
}

void ViewTransition::update(float dt) {
    elapsed_ = std::min(duration_, elapsed_ + std::max(0.f, dt));
}

float ViewTransition::topBlend() const {
    if (duration_ <= 0.f) return to_ == Mode::TopDown ? 1.f : 0.f;
    const float t = Clamp(elapsed_ / duration_, 0.f, 1.f);
    const float eased = t * t * (3.f - 2.f * t);
    const float end = to_ == Mode::TopDown ? 1.f : 0.f;
    return startBlend_ + (end - startBlend_) * eased;
}

Camera3D CameraSystem::makeCamera(const View& view, float turn) {
    Camera3D camera{};
    camera.projection = CAMERA_PERSPECTIVE;
    camera.fovy = fovyDegrees(kCameraDistance, kViewHeight);
    camera.target = {0, 0, 0};
    // Same viewing directions as before (side: slight elevation, top: overhead; both swing with
    // the throttle turn), now at kCameraDistance from the focal plane.
    Vector3 dir;
    if (view.mode == Mode::TopDown) {
        dir = {0, 40, turn * 8.f}; camera.up = {0, 0, -1};
    } else {
        dir = {turn * 6.f, 5, 30}; camera.up = {0, 1, 0};
    }
    camera.position = Vector3Scale(Vector3Normalize(dir), kCameraDistance);
    return camera;
}

float CameraSystem::worldPerLocal(const View& view) {
    return kViewHeight / fmaxf(1.f, view.pf.height);
}

float CameraSystem::depthScale(const View& view, float depth) {
    const float d = Clamp(depth * worldPerLocal(view), -0.9f * kCameraDistance, 0.9f * kCameraDistance);
    return kCameraDistance / (kCameraDistance - d);
}

Matrix CameraSystem::backdropLayer(const Camera3D& camera, float behindWorld) {
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    const float dist = Vector3Distance(camera.position, camera.target);
    const float k = (dist + std::max(0.f, behindWorld)) / std::max(dist, .01f);
    // M = T(target) * B diag(k,k,1) B^T * T(-target), B = [right up forward] (column vectors).
    float m[3][3];
    const float r[3] = {right.x, right.y, right.z}, u[3] = {up.x, up.y, up.z}, f[3] = {forward.x, forward.y, forward.z};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m[i][j] = k * (r[i] * r[j] + u[i] * u[j]) + f[i] * f[j];
    const Vector3 t = camera.target;
    Matrix M = MatrixIdentity();
    M.m0 = m[0][0]; M.m4 = m[0][1]; M.m8 = m[0][2];
    M.m1 = m[1][0]; M.m5 = m[1][1]; M.m9 = m[1][2];
    M.m2 = m[2][0]; M.m6 = m[2][1]; M.m10 = m[2][2];
    M.m12 = t.x - (m[0][0] * t.x + m[0][1] * t.y + m[0][2] * t.z);
    M.m13 = t.y - (m[1][0] * t.x + m[1][1] * t.y + m[1][2] * t.z);
    M.m14 = t.z - (m[2][0] * t.x + m[2][1] * t.y + m[2][2] * t.z);
    return M;
}

Vector3 CameraSystem::toWorld(const View& view, const Camera3D& camera, Vector2 local, float depth) {
    const float width = fmaxf(1.f, view.pf.width), height = fmaxf(1.f, view.pf.height);
    const float halfH = kViewHeight * .5f, halfW = halfH * width / height;
    const Vector2 screen = view.toScreen(local);
    const float x = Clamp((screen.x - view.pf.x) / width, 0.f, 1.f);
    const float y = Clamp((screen.y - view.pf.y) / height, 0.f, 1.f);
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    const Vector3 focal = Vector3Add(camera.target, Vector3Add(Vector3Scale(right, -halfW + x * 2.f * halfW),
                                                               Vector3Scale(up, halfH - y * 2.f * halfH)));
    if (camera.projection != CAMERA_PERSPECTIVE || depth == 0.f) return focal;
    // Slide along the eye ray: forward distance becomes dist - depth, screen point unchanged.
    const float dist = Vector3Distance(camera.position, camera.target);
    const float d = Clamp(depth * worldPerLocal(view), -0.9f * dist, 0.9f * dist);
    return Vector3Add(camera.position, Vector3Scale(Vector3Subtract(focal, camera.position), (dist - d) / dist));
}
