#pragma once
#include "raylib.h"
#include "combat_types.hpp"
#include "world_position.hpp"
#include <cstdint>
#include <cmath>
#include <initializer_list>

// Snapshot rather than a ship pointer: pooled enemies move in memory during compaction.
struct ProjectileTarget { Vector2 pos{}; Faction faction = Faction::Neutral; bool active = false; };

// Shared motion and lifetime state for shots from either side. The pool resets each object
// to its default state before reuse; these types intentionally remain non-polymorphic.
struct Projectile {
    bool alive = false;
    Vector2 pos{}, vel{};
    Vector2 previousPos{}; // start of this simulation step, for swept collision
    float hiddenAxis = 0.f, previousHiddenAxis = 0.f;
    WorldPosition worldPosition(WorldProjection projection) const { return worldFromLegacy(pos, projection, hiddenAxis); }
    WorldPosition previousWorldPosition(WorldProjection projection) const { return worldFromLegacy(previousPos, projection, previousHiddenAxis); }
    Faction faction = Faction::Neutral;
};

class ProjectileTargeting {
public:
    static bool canTarget(Faction owner, const ProjectileTarget& target) {
        return target.active && owner != Faction::Neutral && target.faction != Faction::Neutral && owner != target.faction;
    }
    // First intersection of a 2D swept projectile and a circular projected target.
    // Returns a fraction along the segment, or -1 when the segment misses.
    static float firstContact(Vector2 start, Vector2 end, Vector2 center, float radius) {
        const float dx=end.x-start.x, dy=end.y-start.y, x=start.x-center.x, y=start.y-center.y;
        const float c=x*x+y*y-radius*radius;
        if (c <= 0.f) return 0.f;
        const float a=dx*dx+dy*dy;
        if (a <= 1e-8f) return -1.f;
        const float b=x*dx+y*dy, disc=b*b-a*c;
        if (disc < 0.f) return -1.f;
        const float t=(-b-std::sqrt(disc))/a;
        return t >= 0.f && t <= 1.f ? t : -1.f;
    }
    static float firstProjectedContact(WorldPosition start, WorldPosition end,
                                       WorldPosition center, float radius, WorldProjection projection) {
        return firstContact(projectWorld(start, projection), projectWorld(end, projection),
                            projectWorld(center, projection), radius);
    }
    // Swept point against a capsule around an articulated object's projected body.
    static float firstCapsuleContact(Vector2 start, Vector2 end, Vector2 a, Vector2 b, float radius) {
        const float ax=b.x-a.x, ay=b.y-a.y, len2=ax*ax+ay*ay;
        if (len2 < 1e-8f) return firstContact(start, end, a, radius);
        const float dx=end.x-start.x, dy=end.y-start.y, px=start.x-a.x, py=start.y-a.y;
        const float along=(px*ax+py*ay)/len2;
        if (along >= 0.f && along <= 1.f) {
            const float cross=px*ay-py*ax;
            if (cross*cross <= radius*radius*len2) return 0.f;
        }
        float best=2.f;
        const float capA=firstContact(start,end,a,radius), capB=firstContact(start,end,b,radius);
        if (capA >= 0.f) best=capA;
        if (capB >= 0.f && capB < best) best=capB;
        const float cross0=px*ay-py*ax, crossD=dx*ay-dy*ax;
        if (std::fabs(crossD) > 1e-8f) {
            const float edge=radius*std::sqrt(len2);
            for (int sign : {-1,1}) {
                const float t=(sign*edge-cross0)/crossD;
                const float u=((px+dx*t)*ax+(py+dy*t)*ay)/len2;
                if (t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f && t < best) best=t;
            }
        }
        return best <= 1.f ? best : -1.f;
    }
};
struct Laser : Projectile {
    int damage = 1;
    Color col = WHITE;
    float visualScale = 1.f;
    std::uint64_t flightSound = 0;
};
struct Missile : Projectile {
    float age = 0, trail = 0;
    int damage = 6;
};
struct EnemyShot : Projectile {
    float r = 5, age = 0;
    bool homing = false, bolt = false;
    Color col = PINK;
    int dmg = 0; // zero uses the default bullet damage
};
