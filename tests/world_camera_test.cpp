#include "../camera_system.hpp"
#include "raymath.h"
#include <cassert>
#include <cmath>

static bool near(float a, float b) { return std::fabs(a - b) < .0001f; }
int main() {
    const WorldPosition target{4.f, 3.f, 2.f};
    const Camera3D side = CameraSystem::makeWorldCamera(target, 0.f, 20.f, 14.f);
    const Camera3D top = CameraSystem::makeWorldCamera(target, 1.f, 20.f, 14.f);
    assert(near(side.target.x, 4.f) && near(side.target.y, 2.f) && near(side.target.z, 3.f));
    assert(near(top.target.x, side.target.x) && near(top.target.y, side.target.y) && near(top.target.z, side.target.z));
    assert(near(side.position.z, 23.f) && near(top.position.y, 22.f));
    // The same world-forward vector points right in side view and up in top view.
    const Vector3 forwardWorld{1, 0, 0};
    const Vector3 sideView = Vector3Normalize(Vector3Subtract(side.target, side.position));
    const Vector3 sideRight = Vector3Normalize(Vector3CrossProduct(sideView, side.up));
    assert(Vector3DotProduct(forwardWorld, sideRight) > .999f);
    assert(Vector3DotProduct(forwardWorld, top.up) > .999f);
    for (int i = 0; i <= 20; ++i) {
        const Camera3D during = CameraSystem::makeWorldCamera(target, i / 20.f, 20.f, 14.f);
        const Vector3 viewDir = Vector3Normalize(Vector3Subtract(during.target, during.position));
        assert(std::fabs(Vector3DotProduct(viewDir, during.up)) < .0001f);
        assert(std::fabs(Vector3Length(during.up) - 1.f) < .0001f);
    }
    ViewTransition transition;
    transition.changeView(Mode::TopDown, 2.f);
    transition.update(1.f);
    assert(near(transition.topBlend(), .5f));
    transition.changeView(Mode::SideScroll, 1.f);
    assert(near(transition.topBlend(), .5f));
    transition.update(1.f);
    assert(near(transition.topBlend(), 0.f));

    // Perspective: depth slides a point along the eye ray (same screen point), nearer = larger.
    assert(side.projection == CAMERA_PERSPECTIVE);
    View view; view.mode = Mode::SideScroll; view.pf = {0, 0, 1400, 700};
    for (int m = 0; m < 2; ++m) {
        view.mode = m ? Mode::TopDown : Mode::SideScroll;
        const Camera3D cam = CameraSystem::makeCamera(view, m ? .4f : -.3f);
        assert(cam.projection == CAMERA_PERSPECTIVE);
        const Vector2 local{300.f, 200.f};
        const Vector3 focal = CameraSystem::toWorld(view, cam, local);
        const float depth = CameraSystem::maxDepthLocal(view);
        const Vector3 nearP = CameraSystem::toWorld(view, cam, local, depth);
        const Vector3 farP = CameraSystem::toWorld(view, cam, local, -depth);
        const Vector3 a = Vector3Normalize(Vector3Subtract(focal, cam.position));
        const Vector3 b = Vector3Normalize(Vector3Subtract(nearP, cam.position));
        const Vector3 c = Vector3Normalize(Vector3Subtract(farP, cam.position));
        assert(Vector3Distance(a, b) < 1e-4f && Vector3Distance(a, c) < 1e-4f);
        assert(Vector3Distance(nearP, cam.position) < Vector3Distance(focal, cam.position));
        assert(CameraSystem::depthScale(view, depth) > 1.f && CameraSystem::depthScale(view, -depth) < 1.f);
        assert(near(CameraSystem::depthScale(view, 0.f), 1.f));
    }
}
