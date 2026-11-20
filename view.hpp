#pragma once
#include "raylib.h"
#include <cmath>

// Orientation transform. All gameplay runs in LOCAL space:
//   x = "forward" distance from the player's edge of the playfield (0..forward())
//   y = "lateral" position across the playfield               (0..lateral())
// The player sits at small x and shoots toward +x; enemies enter at large x.
//   SideScroll: forward = screen right, lateral = screen down (full screen)
//   TopDown:    forward = screen up,    lateral = screen right (centred column)
enum class Mode { SideScroll, TopDown };

struct View {
    Mode mode = Mode::SideScroll;
    Rectangle pf{};  // playfield rectangle on screen
    float s = 1;     // UI scale

    // Level 6 approach: the whole playfield image is turned `rot` radians clockwise about the
    // playfield centre (pi/2 = forward points screen-right, 0 = plain top-down) and offset by a
    // render-only camera shake.  Both are pure screen transforms: local (world) positions, sizes
    // and collision never change, only where the camera shows them.  Zero for every other level.
    float rot = 0;
    Vector2 shake{0, 0};
    // Level 6 full screen: the scene is drawn edge to edge, so this much of the world beyond the
    // playfield (local units, every side) is visible. Culling / spawning use it so nothing pops in
    // or out where the old frame was. Gameplay bounds (player clamp, edge throttle) ignore it. 0 elsewhere.
    float vis = 0;

    float forward() const { return mode == Mode::SideScroll ? pf.width : pf.height; }
    float lateral() const { return mode == Mode::SideScroll ? pf.height : pf.width; }
    bool turned() const { return rot != 0 || shake.x != 0 || shake.y != 0; }

    Vector2 toScreen(Vector2 l) const {
        Vector2 p = mode == Mode::SideScroll ? Vector2{pf.x + l.x, pf.y + l.y} : Vector2{pf.x + l.y, pf.y + pf.height - l.x};
        return turned() ? camOut(p) : p;
    }
    Vector2 toLocal(Vector2 p) const {
        if (turned()) p = camIn(p);
        return mode == Mode::SideScroll ? Vector2{p.x - pf.x, p.y - pf.y} : Vector2{pf.y + pf.height - p.y, p.x - pf.x};
    }
    Vector2 dirToScreen(Vector2 d) const {
        Vector2 r = mode == Mode::SideScroll ? d : Vector2{d.y, -d.x};
        return rot != 0 ? spin(r, rot) : r;
    }
    Vector2 dirToLocal(Vector2 d) const {
        if (rot != 0) d = spin(d, -rot);
        return mode == Mode::SideScroll ? d : Vector2{-d.y, d.x};
    }
    // rotation (degrees) that maps a "forward-pointing" sprite drawn facing screen-right
    float forwardAngle() const { return (mode == Mode::SideScroll ? 0.f : -90.f) + rot * 57.29578f; }

    Vector2 centre() const { return {pf.x + pf.width * 0.5f, pf.y + pf.height * 0.5f}; }
    static Vector2 spin(Vector2 d, float a) {  // clockwise on screen (y down) for a > 0
        const float c = cosf(a), s_ = sinf(a);
        return {d.x * c - d.y * s_, d.x * s_ + d.y * c};
    }
    Vector2 camOut(Vector2 p) const {
        const Vector2 c = centre(), d = spin({p.x - c.x, p.y - c.y}, rot);
        return {c.x + d.x + shake.x, c.y + d.y + shake.y};
    }
    Vector2 camIn(Vector2 p) const {
        const Vector2 c = centre(), d = spin({p.x - shake.x - c.x, p.y - shake.y - c.y}, -rot);
        return {c.x + d.x, c.y + d.y};
    }
};
