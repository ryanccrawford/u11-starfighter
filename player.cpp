#include <cstdlib>
#include <cmath>
#include "player.hpp"
#include "config.hpp"
#include "config.hpp"
#include "raymath.h"
#include <cmath>
#include <cstdio>

int shipRow(Mode mode, Vector2 vel, float facing, float th) {
    float fwd = vel.x * facing, lat = vel.y;
    if (mode == Mode::SideScroll) {  // rows: cruise, accelerate, brake, turn_up, turn_down
        if (lat < -th) return 3;
        if (lat > th) return 4;
        if (fwd > th) return 1;
        if (fwd < -th) return 2;
        return 0;
    }
    // top-down rows: N, NE, E, SE, S, SW, W, NW, idle  (banking poses, ship always faces north)
    if (std::fabs(fwd) < th && std::fabs(lat) < th) return 8;
    float a = std::atan2(lat, fwd);  // 0 = forward (north), +90deg = east
    int idx = (int)std::lround(a / (PI / 4));
    return (idx + 8) % 8;
}

Vector2 g_autopilotHold{-1.f, -1.f};   // t251 (player.hpp)
Vector2 g_flightStick{0.f, 0.f};       // t260 (player.hpp)
bool g_invertPitch = false;            // t260 (player.hpp)
#if defined(__aarch64__)
int g_groundShadows = 0;               // tablet: off (keep >= 30 FPS)
int g_reflections = 0;                 // tablet: original look (not verified on device)
#else
int g_groundShadows = 2;               // PC default: high
int g_reflections = 2;                 // PC default: high
#endif

namespace {
// Free flight: keyboard (screen directions) + touch/mouse drag (ship eases toward finger).
class FlightController : public Controller {
public:
    void update(Player& p, float dt, const View& v, const PlayerInput& input) override {
        const float s = v.s, speed = p.def->speed * (cfg::dev.playerSpeed / cfg::kPlayerSpeed) * s;
        Vector2 prev = p.pos, dir{};
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) dir.x -= 1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dir.x += 1;
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) dir.y -= 1;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) dir.y += 1;
        const Vector2 keyDirection = v.dirToLocal(Vector2Normalize(dir));
        float forwardIntent = keyDirection.x;
        p.pos = Vector2Add(p.pos, Vector2Scale(keyDirection, speed * dt));
        // Location-independent virtual joystick. Touchdown establishes neutral wherever the
        // thumb lands; holding a displacement applies velocity until the ship reaches a boundary.
        Vector2 stick = dir;   // t260: screen-space stick for the trench run (keys first, the touch joystick below)
        const bool down = input.movementPositionValid || input.movementTouch >= 0 ||
                          (input.mouseMovement && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        if (down) {
            const Vector2 screenPosition = input.movementPositionValid ? input.movementPosition :
                (input.movementTouch >= 0 ? GetTouchPosition(input.movementTouch) : GetMousePosition());
            Vector2 f = v.toLocal(screenPosition);
            if (!dragging_ || input.movementId != dragId_) {
                dragging_ = true;
                dragId_ = input.movementId;
                anchorF_ = f;
            } else {
                const Vector2 delta = Vector2Subtract(f, anchorF_);
                const float distance = Vector2Length(delta);
                const float deadzone = cfg::kTouchJoystickDeadzone * s;
                const float radius = cfg::kTouchJoystickRadius * s;
                if (distance > deadzone) {
                    const float strength = Clamp((distance - deadzone) / (radius - deadzone), 0.f, 1.f);
                    forwardIntent = Vector2Normalize(delta).x * strength;
                    p.pos = Vector2Add(p.pos, Vector2Scale(Vector2Normalize(delta), speed * strength * dt));
                    const Vector2 ds = v.dirToScreen(Vector2Normalize(delta));
                    stick = Vector2Add(stick, Vector2Scale(ds, strength));
                }
            }
        } else { dragging_ = false; dragId_ = -1; }
        g_flightStick = {Clamp(stick.x, -1.f, 1.f), Clamp(stick.y, -1.f, 1.f)};
        static const bool autoplay = getenv("AUTOPLAY") != nullptr;  // demo/benchmark pilot, no input needed
        if (autoplay) {
            apT_ += dt;
            Vector2 target{v.forward() * (0.22f + 0.08f * std::sin(apT_ * 0.7f)), v.lateral() * (0.5f + 0.38f * std::sin(apT_ * 1.3f))};
            if (g_autopilotHold.x >= 0.f) target = g_autopilotHold;   // t251 rest-zone test
            p.pos = Vector2Lerp(p.pos, target, std::fmin(1.f, 4 * dt));
        }
        // Full forward/reverse joystick deflection drives the shared throttle and camera,
        // regardless of where the ship or the initial touch happens to be.
        p.edgeHold = forwardIntent >= .85f ? 1 : forwardIntent <= -.85f ? -1 : 0;
        p.pos.x = Clamp(p.pos.x, 45 * s, v.forward() * cfg::kPlayerMaxX);
        p.pos.y = Clamp(p.pos.y, 35 * s, v.lateral() - 35 * s);
        Vector2 vel = Vector2Scale(Vector2Subtract(p.pos, prev), 1 / std::fmax(dt, 1e-4f));
        p.vel = Vector2Lerp(p.vel, vel, std::fmin(1.f, 10 * dt));
        p.row = shipRow(v.mode, p.vel, 1, 120 * s);
    }
    const char* name() const override { return "flight"; }
private:
    float apT_ = 0;
    int dragId_ = -1;
    bool dragging_ = false;
    Vector2 anchorF_{};
};
}  // namespace

std::unique_ptr<Controller> makeController(ControllerType type) {
    switch (type) {
        case ControllerType::Flight: break;
        case ControllerType::GroundSide:
        case ControllerType::GroundTopDown:
            // Planned (see docs/LEVEL_FORMAT.md): gravity/jump/tile collision and 8-dir walk with free aim.
            fprintf(stderr, "PLAYER ground controller not implemented yet, using flight\n");
            break;
    }
    return std::make_unique<FlightController>();
}
