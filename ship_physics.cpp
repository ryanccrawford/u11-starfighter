#include "ship_physics.hpp"
#include "raymath.h"
#include "world_physics.hpp"
#include <algorithm>
#include <cmath>

void ShipPhysics::separateEnemies(Pool<Enemy, 160>& ships, float dt, float scale, WorldProjection projection) {
    // t251: a boss is immovable here (its director owns its motion); smaller ships give way to it fully.
    auto isBoss = [](const Enemy& e) { return e.def && e.def->boss; };
    ships.each([&](Enemy& e) {
        if (isBoss(e)) { e.avoidance = {0, 0}; return; }
        e.avoidance = Vector2Scale(e.avoidance, std::max(0.f, 1.f - dt * .8f));
        e.pos = Vector2Add(e.pos, e.avoidance);
    });
    ships.each([&](Enemy& a) {
        ships.each([&](Enemy& b) {
            if (&a >= &b || !a.alive || !b.alive) return;
            const Vector2 delta = Vector2Subtract(a.pos, b.pos);
            const float distance = Vector2Length(delta), safe = (a.r + b.r) * 1.15f;
            if (!WorldPhysics::overlaps(a.worldPosition(projection), a.r * 1.15f,
                                        b.worldPosition(projection), b.r * 1.15f)) return;
            const float hidden = a.hiddenAxis - b.hiddenAxis;
            const float projectedSafe = std::sqrt(std::max(0.f, safe * safe - hidden * hidden));
            if (distance >= projectedSafe) return;
            const Vector2 direction = distance > .001f ? Vector2Scale(delta, 1.f / distance) : Vector2{0, 1};
            const Vector2 shift = Vector2Scale(direction, std::min((projectedSafe - distance) * .5f, 85.f * scale * dt));
            const bool ab = isBoss(a), bb = isBoss(b);
            if (ab && bb) return;
            const float ka = ab ? 0.f : bb ? 2.f : 1.f, kb = bb ? 0.f : ab ? 2.f : 1.f;
            a.avoidance = Vector2Add(a.avoidance, Vector2Scale(shift, ka)); b.avoidance = Vector2Subtract(b.avoidance, Vector2Scale(shift, kb));
            a.pos = Vector2Add(a.pos, Vector2Scale(shift, ka)); b.pos = Vector2Subtract(b.pos, Vector2Scale(shift, kb));
        });
    });
    // Resolve actual hull penetration in world space, including depth. The softer avoidance
    // above begins earlier; this is the final physical constraint for a pair already touching.
    for (int pass = 0; pass < 8; ++pass) {
        bool resolved = false;
        ships.each([&](Enemy& a) {
            ships.each([&](Enemy& b) {
                if (&a >= &b || !a.alive || !b.alive) return;
                WorldPosition wa = a.worldPosition(projection), wb = b.worldPosition(projection);
                const WorldPosition wa0 = wa, wb0 = wb;
                if (!WorldPhysics::resolveOverlap(wa, a.r, wb, b.r)) return;
                if (isBoss(a) && isBoss(b)) return;
                if (isBoss(a)) { wb = {wb.x + wa0.x - wa.x, wb.y + wa0.y - wa.y, wb.z + wa0.z - wa.z}; wa = wa0; }
                else if (isBoss(b)) { wa = {wa.x + wb0.x - wb.x, wa.y + wb0.y - wb.y, wa.z + wb0.z - wb.z}; wb = wb0; }
                resolved = true;
                const Vector2 pa = projectWorld(wa, projection), pb = projectWorld(wb, projection);
                if (!isBoss(a)) a.avoidance = Vector2Add(a.avoidance, Vector2Subtract(pa, a.pos));
                if (!isBoss(b)) b.avoidance = Vector2Add(b.avoidance, Vector2Subtract(pb, b.pos));
                a.pos = pa; b.pos = pb;
                a.hiddenAxis = projection == WorldProjection::Side ? wa.y : wa.z;
                b.hiddenAxis = projection == WorldProjection::Side ? wb.y : wb.z;
            });
        });
        if (!resolved) break;
    }
}
