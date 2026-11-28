#pragma once
#include "data.hpp"
#include "view.hpp"
#include "ship.hpp"
#include <memory>

// Controller-agnostic player state: weapons, damage and power-ups only touch
// these fields, so ground characters can reuse everything.
// t251 capture hook: while set (x >= 0), the AUTOPLAY pilot flies to this local point instead of its sweep.
extern Vector2 g_autopilotHold;
// t260: the flight stick in SCREEN terms (x right, y down; -1..1), keys + touch joystick, set every frame by the
// flight controller; the trench run flies from it. g_invertPitch: Options > UP/DOWN (saved), X toggles in-game.
extern Vector2 g_flightStick;
extern bool g_invertPitch;
extern int g_reflections;              // player glass/chrome: 0 off, 1 low, 2 high (PC Options, saved)
extern int g_groundShadows;            // 0 off, 1 low, 2 high (Options, saved)

struct Player : Ship {
    Player() : Ship(100) {}
    const PlayerDef* def = nullptr;
    int shield = 0;
    float invuln = 0;
    int edgeHold = 0;      // +1 held into the forward stop edge, -1 held into the back edge, 0 otherwise
    float edgeScroll = 1;  // eased world-scroll multiplier from edgeHold (cfg::kEdgeBoostMul / kEdgeSlowMul), set by Game
    bool grounded = false; // used by ground controllers
};

// Action touches are claimed by Game before the flight controller sees them.
// Movement can use a raylib touch index or a platform-provided screen position.
struct PlayerInput {
    int movementTouch = -1;
    int movementId = -1;
    bool mouseMovement = true;
    bool movementPositionValid = false;
    Vector2 movementPosition{};
};

// Movement + animation strategy, selected by PlayerDef::controller.
class Controller {
public:
    virtual ~Controller() = default;
    virtual void update(Player& p, float dt, const View& v, const PlayerInput& input) = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<Controller> makeController(ControllerType type);

// Shared by ships (player + enemies): pick a sheet row from local velocity.
// facing = +1 for forward-facing ships, -1 for ships flying toward the player.
int shipRow(Mode mode, Vector2 localVel, float facing, float threshold);
