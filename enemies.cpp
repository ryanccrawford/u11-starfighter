#include "enemies.hpp"
#include "ship_physics.hpp"
#include "world_physics.hpp"
#include "camera_system.hpp"
#include "audio.hpp"
#include "config.hpp"
#include "particles.hpp"
#include "formation_layout.hpp"
#include "hit_feedback.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace cfg;

void Enemies::reset() {
    list.clear(); shots.clear();
    warden = Warden{}; safeOn = false;
    for (auto& f : formations_) f.active = false;
}

const Enemy* Enemies::boss() const {
    const Enemy* b = nullptr;
    list.each([&](const Enemy& e) { if (e.def->boss) b = &e; });
    return b;
}

Enemy* Enemies::spawnOne(const EnemyDef& d, const LevelDef& L, const View& v, Vector2 pos) {
    Enemy* e = list.spawn();
    if (!e) return nullptr;
    e->def = &d;
    e->pos = e->origin = pos;
    e->hp = e->maxHp = std::max(1, (int)std::lround(d.hp * L.hpMul));
    e->r = kEnemySize * 0.28f * d.scale * v.s;
    e->hiddenAxis = 0;  // bosses and singles fly in the focal plane; waves assign their own depth
    e->amp = d.amp; e->freq = d.freq;
    e->t = frand(0, 6.28f);
    e->fireT = frand(0.5f, 1.0f) * d.fireInterval;
    return e;
}

namespace {
// Pattern::Path position at time t. Before t = 0 the ship waits off-screen on the extension of the
// first segment, so a staggered wave enters one by one instead of appearing part-way along.
Vector2 pathPoint(const std::vector<Vector2>& P, float t, float F, float Lat, float spd) {
    const Vector2 a{P[0].x * F, P[0].y * Lat}, b{P[1].x * F, P[1].y * Lat};
    if (t < 0) return Vector2Add(a, Vector2Scale(Vector2Normalize(Vector2Subtract(a, b)), -t * spd));
    const float total = (F / spd) * (P.size() - 1) / 2.f;
    const float k = std::fmin(t / total, 0.9999f) * (P.size() - 1);
    const int i = (int)k; const float f = k - i;
    const Vector2 p = P[i], q = P[i + 1];
    Vector2 pos{(p.x + (q.x - p.x) * f) * F, (p.y + (q.y - p.y) * f) * Lat};
    if (t > total) pos.x -= (t - total) * spd;
    return pos;
}
}  // namespace

bool Enemies::spawnClear(Vector2 pos, float hidden, float r, const View& v, int ignoreGroup) const {
    const WorldProjection proj = v.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top;
    const WorldPosition wp = worldFromLegacy(pos, proj, hidden);
    bool clear = true;
    list.each([&](const Enemy& o) {
        if (!clear || !o.alive || (ignoreGroup >= -1 && o.group == ignoreGroup && o.group >= 0)) return;
        if (WorldPhysics::overlaps(wp, r + 6.f * v.s, o.worldPosition(proj), o.r)) clear = false;
    });
    return clear && !(spawnBlocked && spawnBlocked(pos, hidden, r));
}

void Enemies::spawnWave(const SpawnWave& w, const EnemyDef& d, const LevelDef& L, const View& v) {
    const float s = v.s, F = v.forward(), Lat = v.lateral();
    // Keep formation centers separated by at least two collision radii; a smaller configured
    // spacing can otherwise put ships inside each other as soon as they spawn.
    const float sp = std::max(w.spacing, kEnemySize * 0.56f * d.scale + 8.f) * s;
    const float enter = F + 60 * s + (v.rot != 0.f ? v.vis : 0.f);  // just beyond the far edge (L6 side view: beyond the screen edge)
    const float r = kEnemySize * 0.28f * d.scale * s;
    const float dMax = CameraSystem::maxDepthLocal(v);
    const int kMaxShift = 16;  // the formation moves out by whole spacings until every slot is legal
    static const bool log = getenv("WAVE_LOG") != nullptr;

    if (d.pattern == Pattern::Invaders) {  // classic marching grid
        int slot = -1;
        for (int i = 0; i < kMaxFormations; i++) if (!formations_[i].active) { slot = i; break; }
        if (slot < 0) return;
        Formation& f = formations_[slot];
        int cols = std::min(w.count, 8), rows = (w.count + cols - 1) / cols;
        f = Formation{};
        f.active = true; f.initial = w.count;
        // Each rank of the grid flies at its own depth (front rank nearest the camera); the whole
        // stack is centred so no rank is clamped onto its neighbour's depth.
        const float rankStep = rows > 1 ? std::min(dMax * 0.6f, 2.f * dMax / (rows - 1)) : 0.f;
        const float halfStack = (rows - 1) * 0.5f * rankStep;
        const float grid0 = Clamp(frand(-0.3f, 0.3f) * dMax, -dMax + halfStack, dMax - halfStack);
        auto cellDepth = [&](int i) { return Clamp(grid0 + ((rows - 1) * 0.5f - i / cols) * rankStep, -dMax, dMax); };
        // Spacing from the real projectile contact circle (radius at the nearest rank's depth + the
        // laser pad), so neighbours never share a hit volume; the grid settles fully on-screen.
        float maxDS = 0.f;
        for (int k = 0; k < rows; k++) maxDS = std::max(maxDS, CameraSystem::depthScale(v, cellDepth(k * cols)));
        const float hitR = r * maxDS + 8.f * s;
        const FormationLayout lay = layoutFormation(w.count, w.spacing * s, hitR, F, Lat, w.y, s);
        cols = lay.cols; rows = lay.rows;
        const float gsp = lay.spacing;
        f.spacing = gsp; f.settleX = lay.settleX;
        f.extent = {lay.extentX, lay.extentY};
        f.speed = d.speed * s * L.speedMul; f.step = 26 * s;
        f.entrySpeed = std::max(170.f * s, 3.f * f.speed);
        f.entering = true;
        f.origin = {F + 30 * s + hitR + (v.rot != 0.f ? v.vis : 0.f), lay.originY};  // front rank starts just beyond the far edge
        // Off-screen start must not overlap other traffic; a grid still marching from an earlier
        // wave is handled during the entry (this one holds behind it, off-screen, until clear).
        int shift = 0;
        for (; shift < kMaxShift; ++shift) {
            bool ok = true;
            for (int i = 0; i < w.count && ok; i++)
                ok = spawnClear(Vector2Add({f.origin.x + shift * gsp, f.origin.y}, {(i / cols) * gsp, (i % cols) * gsp}), cellDepth(i), r, v);
            if (ok) break;
        }
        f.origin.x += shift * gsp;
        for (int i = 0; i < w.count; i++)
            if (Enemy* e = spawnOne(d, L, v, {})) {
                e->group = slot;
                e->gridOff = {(i / cols) * gsp, (i % cols) * gsp};
                e->pos = Vector2Add(f.origin, e->gridOff);
                e->hiddenAxis = cellDepth(i);
            }
        if (log) fprintf(stderr, "WAVE %s x%d grid %dx%d sp=%.0f hitR=%.0f settleX=%.0f F=%.0f Lat=%.0f\n", d.id.c_str(), w.count, rows, cols, gsp, hitR, lay.settleX, F, Lat);
        ShipPhysics::separateEnemies(list, 0.f, s,
            v.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top);
        return;
    }
    // Lateral sweep of the movement pattern, so a lane is legal along the whole flight, not only at
    // the spawn point.
    const float amp = (w.amp >= 0 ? w.amp : d.amp) * s;
    const float sweep = (d.pattern == Pattern::Sine || d.pattern == Pattern::Circle) ? amp : 0.f;
    const float halfSpan = (w.count - 1) * sp * 0.5f;
    const float margin = 30.f * s;
    const float center = halfSpan <= Lat * .5f - margin
        ? Clamp(Lat * w.y, margin + halfSpan, Lat - margin - halfSpan) : Lat * .5f;
    std::vector<Vector2> P(w.count);
    std::vector<float> H(w.count);
    for (int i = 0; i < w.count; i++) {
        float off = (i - (w.count - 1) / 2.f) * sp;
        Vector2 p{enter, center + off};
        if (w.formation == "column") p = {enter + i * sp, Lat * w.y};
        else if (w.formation == "v") p = {enter + std::fabs(off), center + off};
        else if (w.formation == "row") p = {enter + i * sp * 1.4f, center + off};
        else if (w.formation == "random") {
            // Scattered, but never inside a wave-mate: re-draw a slot that lands on an earlier one.
            for (int tries = 0; tries < 40; ++tries) {
                p = {enter + frand(0, 3) * sp, frand(0.1f, 0.9f) * Lat};
                bool ok = true;
                for (int j = 0; j < i && ok; ++j) ok = Vector2Distance(p, P[j]) >= 2.f * r + 8.f * s;
                if (ok) break;
            }
        }
        // Oversized formations spill forward in additional ranks rather than stacking at an edge.
        if (w.formation != "random" && w.formation != "column" && (p.y < margin || p.y > Lat - margin)) {
            p.x += (1 + i) * sp;
        }
        p.y = Clamp(p.y, 30 * s, Lat - 30 * s);
        P[i] = p;
        if (i == 0) waveDepth_ = frand(-0.6f, 0.6f) * dMax;
        H[i] = Clamp(waveDepth_ + frand(-0.4f, 0.4f) * dMax, -dMax, dMax);
    }
    // Hazard lane (L3 turret): the whole formation drops below it, keeping its shape.
    if (laneMin > 0.f && d.pattern != Pattern::Path) {
        float top = 1e9f;
        for (const Vector2& p : P) top = std::min(top, p.y - sweep - r);
        if (top < laneMin) for (int i = 0; i < w.count; i++) {
            const float bottom = Lat - 30 * s - sweep;
            P[i].y += laneMin - top;
            if (P[i].y > bottom) { P[i].y = bottom; P[i].x += (1 + i) * sp; }  // squeezed: spill forward, never stack
        }
    }
    // A wave-authored path is referenced by the ships, so it must live in the LevelDef (Game passes
    // the authored wave itself); the type's own path lives in the EnemyDef.
    const std::vector<Vector2>* path = d.pattern == Pattern::Path ? (!w.path.empty() ? &w.path : &d.path) : nullptr;
    if (path && path->size() < 2) path = nullptr;
    const float spd = d.speed * s * L.speedMul;
    const float pathGap = 0.6f;  // seconds between ships entering the same path
    auto slot = [&](int i, int shift) -> Vector2 {
        if (path) return pathPoint(*path, -(i * pathGap + shift * sp / spd), F, Lat, spd);
        return {P[i].x + shift * sp, P[i].y};
    };
    int shift = 0;
    for (; shift < kMaxShift; ++shift) {
        bool ok = true;
        for (int i = 0; i < w.count && ok; i++) ok = spawnClear(slot(i, shift), H[i], r, v);
        if (ok) break;
    }
    if (shift >= kMaxShift) shift = kMaxShift - 1;
    for (int i = 0; i < w.count; i++) {
        if (Enemy* e = spawnOne(d, L, v, slot(i, shift))) {
            e->hiddenAxis = H[i];
            if (w.amp >= 0) e->amp = w.amp;
            if (w.freq >= 0) e->freq = w.freq;
            if (path) { e->path = path; e->t = -(i * pathGap + shift * sp / spd); }
            else e->t = i * 0.6f;
        }
    }
    if (log) {
        float minD = 1e9f;
        for (int i = 0; i < w.count; i++) for (int j = i + 1; j < w.count; j++) minD = std::min(minD, Vector2Distance(slot(i, shift), slot(j, shift)));
        fprintf(stderr, "WAVE %s x%d %s y=%.2f shift=%d minGap=%.0f r=%.0f laneMin=%.0f%s\n", d.id.c_str(), w.count, path ? "path" : w.formation.c_str(), w.y, shift,
                w.count > 1 ? minD : 0.f, r, laneMin, path && path != &d.path ? " (wave path)" : "");
    }
    ShipPhysics::separateEnemies(list, 0.f, s,
        v.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top);
}

void Enemies::spawnBoss(const EnemyDef& d, const LevelDef& L, const View& v) {
    Enemy* e = spawnOne(d, L, v, {v.forward() + 120 * v.s, v.lateral() / 2});
    if (e && d.id == "warden") {   // t251: scripted first boss
        warden = Warden{};
        warden.st = WardenState::Engage; warden.p = e->pos; warden.atkT = 3.0f;   // first attack once it has glided in
        fprintf(stderr, "WARDEN spawn hp=%d r=%.0f phase=0\n", e->hp, e->r);
    }
}

// ============================================================ t251: The Warden director
namespace {
constexpr int kAimed = 1, kFan = 2, kSweep = 4, kRockets = 8, kRing = 16;
// Fixed, repeating attack cycles (one step per telegraph). Each return adds weapons and patterns.
const int kWardenSeq[4][4] = {
    {kAimed, kAimed, kAimed, kAimed},
    {kAimed, kFan, kAimed, kFan},
    {kFan, kSweep, kAimed | kRockets, kSweep},
    {kRing | kAimed, kSweep | kRockets, kFan | kAimed, kRing | kFan}};
const float kWardenCool[4] = {1.9f, 1.6f, 1.4f, 1.2f};     // rest between attacks (s)
const float kWardenTele = 0.9f;                             // charge-up before every attack (s)
// Fixed station cycle per phase (fractions of forward / lateral); the boss glides to the next one after each attack.
const Vector2 kWardenStation[4][4] = {
    {{.80f, .40f}, {.80f, .54f}, {.80f, .32f}, {.80f, .48f}},
    {{.78f, .35f}, {.82f, .56f}, {.78f, .45f}, {.82f, .30f}},
    {{.76f, .30f}, {.80f, .58f}, {.76f, .44f}, {.82f, .36f}},
    {{.77f, .32f}, {.81f, .57f}, {.75f, .43f}, {.82f, .38f}}};
const float kWardenThr[3] = {0.75f, 0.50f, 0.25f};   // helper waves at these hp fractions
struct Helper { const char* id; int n; float y; };
const Helper kWardenHelpers[3][2] = {
    {{"scarab", 5, .45f}, {nullptr, 0, 0}},
    {{"hornet", 2, .40f}, {"weaver", 4, .58f}},
    {{"gunship", 2, .42f}, {"scarab", 5, .60f}}};
// Exact critically damped spring step (frame-rate independent, C1-continuous: no snaps, no overshoot).
void springStep(float& x, float& v, float tgt, float w, float dt) {
    const float d = x - tgt, e = std::exp(-w * dt), k = (v + w * d) * dt;
    x = tgt + (d + k) * e; v = (v - w * k) * e;
}
Color wardenAtkColor(int m) {
    if (m & kRing) return {190, 120, 255, 255};
    if (m & kSweep) return {255, 70, 50, 255};
    if (m & kRockets) return {255, 210, 80, 255};
    if (m & kFan) return {255, 150, 50, 255};
    return {255, 90, 170, 255};
}
float wardenFireLen(int m) {
    float t = 0.05f;
    if (m & kAimed) t = std::max(t, 0.30f);
    if (m & kRockets) t = std::max(t, 0.40f);
    if (m & kSweep) t = std::max(t, 1.30f);
    return t;
}
const char* wardenStateName(Enemies::WardenState s) {
    switch (s) { case Enemies::WardenState::Engage: return "engage"; case Enemies::WardenState::Retreat: return "retreat";
                 case Enemies::WardenState::Helpers: return "helpers"; case Enemies::WardenState::Return: return "return"; default: return "off"; }
}
}  // namespace

int Enemies::bossHpFloor(const Enemy& e) const {
    if (!e.def || !e.def->boss || !wardenOn()) return 0;
    if (warden.st == WardenState::Engage && warden.phase < 3) return (int)std::ceil(kWardenThr[warden.phase] * e.maxHp);
    if (warden.st != WardenState::Engage) return e.hp;
    return 0;
}

bool Enemies::segCrossesSafe(Vector2 a, Vector2 b, float r) const {
    if (!safeOn) return false;
    const float pad = r + playerHitR + 6.f;
    const float x0 = safeRect.x - pad, x1 = safeRect.x + safeRect.width + pad, y0 = safeRect.y - pad, y1 = safeRect.y + safeRect.height + pad;
    float t0 = 0.f, t1 = 1.f;   // Liang-Barsky
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
    for (int i = 0; i < 4; i++) {
        if (std::fabs(p[i]) < 1e-6f) { if (q[i] < 0.f) return false; continue; }
        const float t = q[i] / p[i];
        if (p[i] < 0.f) { if (t > t1) return false; if (t > t0) t0 = t; }
        else { if (t < t0) return false; if (t < t1) t1 = t; }
    }
    return true;
}

bool Enemies::pathCrossesSafe(Vector2 from, Vector2 vel, float r) const {
    if (!safeOn) return false;
    const float len = Vector2Length(vel);
    if (len < 1e-4f) return segCrossesSafe(from, from, r);
    return segCrossesSafe(from, Vector2Add(from, Vector2Scale(vel, 20000.f / len)), r);   // the whole straight flight
}

void Enemies::wardenFire(Enemy& e, int kind, int round, const View& v, Audio& sfx) {
    const float s = v.s;
    const Vector2 muzzle = muzzleOf ? muzzleOf(e) : e.pos;
    auto shot = [&](Vector2 at, float ang, float speed, Color c, float r, bool bolt) {
        const Vector2 vel{std::cos(ang) * speed * s, std::sin(ang) * speed * s};
        if (pathCrossesSafe(at, vel, r * s)) { warden.dropped++; return; }   // the rest zone is never in a bullet's path
        if (EnemyShot* b = shots.spawn()) {
            b->faction = Faction::Enemy; b->pos = b->previousPos = at; b->dmg = e.def->shotDamage; b->vel = vel;
            b->col = c; b->r = r * s; b->homing = false; b->bolt = bolt;
        }
    };
    switch (kind) {
        case kAimed: {
            const Vector2 d = Vector2Subtract(warden.aimAt, muzzle);
            shot(muzzle, std::atan2(d.y, d.x), kEnemyBulletSpeed * 1.15f, {255, 90, 170, 255}, 6, false);
            break;
        }
        case kFan:
            for (int i = -4; i <= 4; i++) shot(muzzle, PI + i * 0.17f, kEnemyBulletSpeed * 0.9f, {255, 150, 50, 255}, 6, false);
            break;
        case kSweep: {
            const float u = Clamp(round / 28.f, 0.f, 1.f);
            shot(muzzle, PI - 0.75f + 1.10f * u, kEnemyBulletSpeed * 1.8f, {255, 70, 50, 255}, 4, true);
            break;
        }
        case kRockets: {
            static const float off[4] = {-0.32f, -0.11f, 0.11f, 0.32f};
            shot({e.pos.x - e.r * 0.55f, e.pos.y + off[round & 3] * e.r * 1.3f}, PI, kEnemyBulletSpeed * 0.75f, {255, 210, 80, 255}, 7, false);
            break;
        }
        case kRing:
            for (int i = 0; i < 14; i++) shot(e.pos, warden.spin + i * 2.f * PI / 14.f, kEnemyBulletSpeed * 0.6f, {190, 120, 255, 255}, 6, false);
            warden.spin += 0.11f;
            break;
    }
    e.fireAnimT = 0;
    if (kind != kSweep || (round % 4) == 0) sfx.play(Sfx::Hit, 0.25f, 0.2f);
}

void Enemies::updateWarden(Enemy& e, float dt, Vector2 player, const LevelDef& L, const View& v, Audio& sfx) {
    (void)L;
    Warden& W = warden;
    const float F = v.forward(), Lat = v.lateral();
    const float dMax = CameraSystem::maxDepthLocal(v);
    const WardenState was = W.st;
    W.stT += dt;
    auto station = [&](int ph, int i) { const Vector2 k = kWardenStation[ph & 3][i & 3]; return Vector2{k.x * F, k.y * Lat}; };
    Vector2 tgt = station(W.phase, W.station);
    float dTgt = 0.f, w = 2.0f, shieldTgt = 0.f;
    bool attacking = false;
    switch (W.st) {
        case WardenState::Engage:
            if (W.phase < 3 && e.hp <= (int)std::ceil(kWardenThr[W.phase] * e.maxHp)) {   // threshold: call the helpers in
                W.st = WardenState::Retreat; W.stT = 0; W.atkState = 0; W.atkMask = 0;
                if (onBossEvent) onBossEvent(1);
                break;
            }
            attacking = true;
            break;
        case WardenState::Retreat:
        case WardenState::Helpers: {
            tgt = {F * 0.88f, Lat * 0.24f}; dTgt = -0.22f * dMax; w = 1.7f; shieldTgt = 1.f;   // backs off but stays big and on screen (t255: deep z read small)
            if (W.st == WardenState::Retreat && W.stT > 1.6f) {
                for (const Helper& h : kWardenHelpers[std::min(W.phase, 2)]) if (h.id) pendingMinions_.push_back({h.id, h.n, h.y, "column"});
                W.st = WardenState::Helpers; W.stT = 0;
            } else if (W.st == WardenState::Helpers) {
                int helpers = 0;
                list.each([&](const Enemy& o) { if (o.alive && !o.def->boss) helpers++; });
                if ((helpers == 0 && W.stT > 2.5f) || W.stT > 45.f) {
                    W.st = WardenState::Return; W.stT = 0; W.phase = std::min(3, W.phase + 1);
                    W.station = 0; W.seqI = 0; W.atkState = 0; W.atkT = 2.6f;
                    if (onBossEvent) onBossEvent(2);
                }
            }
            break;
        }
        case WardenState::Return:
            tgt = station(W.phase, 0); w = 1.9f; shieldTgt = W.stT < 1.4f ? 1.f : 0.f;
            if (W.stT > 2.0f) { W.st = WardenState::Engage; W.stT = 0; }
            break;
        default: break;
    }
    if (W.st != was) fprintf(stderr, "WARDEN state=%s phase=%d hp=%d/%d\n", wardenStateName(W.st), W.phase, e.hp, e.maxHp);
    // gentle continuous drift on top of the station target (the target itself never jumps the hull: the spring follows it)
    tgt.y += std::sin(e.t * 0.9f) * 0.012f * Lat;
    tgt.x += std::sin(e.t * 0.55f) * 0.008f * F;
    if (!W.tgtInit) { W.tgtS = tgt; W.tgtInit = true; }
    W.tgtS = Vector2Lerp(W.tgtS, tgt, 1.f - std::exp(-3.f * dt));   // first-order ease of the target: C2-smooth hull motion
    springStep(W.p.x, W.v.x, W.tgtS.x, w, dt);
    springStep(W.p.y, W.v.y, W.tgtS.y, w, dt);
    springStep(W.depth, W.depthV, dTgt, w, dt);
    W.shield += Clamp(shieldTgt - W.shield, -2.5f * dt, 2.5f * dt);
    e.pos = e.origin = W.p; e.vel = W.v; e.hiddenAxis = W.depth; e.phase = W.phase;
    e.avoidance = {0, 0};

    // ---- telegraphed attack cycle
    W.charge = muzzleOf ? muzzleOf(e) : e.pos;
    bool tele = false;
    if (attacking) {
        if (W.atkState == 0) {
            W.atkT -= dt;
            if (W.atkT <= 0.f) {
                W.atkState = 1; W.atkT = kWardenTele; W.atkMask = kWardenSeq[W.phase][W.seqI]; W.seqI = (W.seqI + 1) % 4;
                W.glowCol = wardenAtkColor(W.atkMask);
                sfx.play(Sfx::BossCharge, 0.6f, 0.f);
                if (getenv("BOSS_LOG")) fprintf(stderr, "WARDEN telegraph mask=%d phase=%d\n", W.atkMask, W.phase);
            }
        }
        if (W.atkState == 1) {
            tele = true;
            W.atkT -= dt;
            W.glow = Clamp(1.f - W.atkT / kWardenTele, 0.f, 1.f); W.chargeK = W.glow;
            if (W.atkT <= 0.f) {
                W.atkState = 2; W.atkT = 0.f; W.firedA = W.firedS = W.firedR = 0; W.aimAt = player;
                if (W.atkMask & kFan) wardenFire(e, kFan, 0, v, sfx);
                if (W.atkMask & kRing) wardenFire(e, kRing, 0, v, sfx);
            }
        } else if (W.atkState == 2) {
            W.atkT += dt;
            if (W.atkMask & kAimed) while (W.firedA < 3 && W.atkT >= 0.14f * W.firedA) wardenFire(e, kAimed, W.firedA++, v, sfx);
            if (W.atkMask & kRockets) while (W.firedR < 4 && W.atkT >= 0.12f * W.firedR) wardenFire(e, kRockets, W.firedR++, v, sfx);
            if (W.atkMask & kSweep) while (W.firedS < 29 && W.atkT >= 0.045f * W.firedS) wardenFire(e, kSweep, W.firedS++, v, sfx);
            if (W.atkT >= wardenFireLen(W.atkMask)) {
                W.atkState = 0; W.atkT = kWardenCool[W.phase]; W.station = (W.station + 1) % 4;
            }
        }
    } else { W.atkState = 0; }
    if (!tele) { W.glow = std::max(0.f, W.glow - 3.f * dt); W.chargeK = std::max(0.f, W.chargeK - 4.f * dt); }

    static const bool blog = getenv("BOSS_LOG") != nullptr;
    if (blog) fprintf(stderr, "BOSSPOS dt=%.5f x=%.3f y=%.3f h=%.3f vx=%.2f vy=%.2f st=%s ph=%d hp=%d sh=%.2f\n", dt, W.p.x, W.p.y, W.depth, W.v.x, W.v.y,
                      wardenStateName(W.st), W.phase, e.hp, W.shield);
}

void Enemies::fire(Enemy& e, EnemyWeapon weapon, int nShots, Vector2 player, const View& v, Audio& sfx) {
    const float s = v.s;
    Vector2 aim = Vector2Normalize(Vector2Subtract(player, e.pos));
    float base = std::atan2(aim.y, aim.x);
    const Vector2 from = muzzleOf ? muzzleOf(e) : e.pos;
    if (muzzleOf) { aim = Vector2Normalize(Vector2Subtract(player, from)); base = std::atan2(aim.y, aim.x); }
    auto shot = [&](float ang, float speed, Color c, float r, bool homing) -> EnemyShot* {
        if (pathCrossesSafe(from, {std::cos(ang) * speed * s, std::sin(ang) * speed * s}, r * s)) { warden.dropped++; return nullptr; }  // t251 rest zone
        EnemyShot* b = shots.spawn();
        if (b) {
            b->faction = Faction::Enemy;
            b->pos = from; b->dmg = e.def->shotDamage; b->vel = {std::cos(ang) * speed * s, std::sin(ang) * speed * s};
            b->col = c; b->r = r * s; b->homing = homing;
        }
        return b;
    };
    switch (weapon) {
        case EnemyWeapon::None: return;
        case EnemyWeapon::Needle: {  // Scarab: one fast alien-green needle from the gun muzzle, aimed when it fires
            const float sp = kEnemyBulletSpeed * 1.3f * e.def->shotSpeed, sz = 4.f * e.def->shotSize;
            for (int i = 0; i < std::max(1, nShots); i++) shot(base + (i - (nShots - 1) / 2.f) * 0.08f, sp, {120, 255, 110, 255}, sz, false);
            break;
        }
        case EnemyWeapon::Rotary:  // Hornet: spin the barrels up, then a burst of rounds (Enemies::update fires them)
            e.burstLeft = std::max(1, nShots); e.burstT = e.def->spinUp; e.fireAnimT = 0;
            return;
        case EnemyWeapon::Aimed: shot(base, kEnemyBulletSpeed, PINK, 5, false); break;
        case EnemyWeapon::Spread:
            for (int i = 0; i < nShots; i++) shot(base + (i - (nShots - 1) / 2.f) * 0.22f, kEnemyBulletSpeed, ORANGE, 5, false);
            break;
        case EnemyWeapon::Laser: if (EnemyShot* b = shot(PI, kEnemyBulletSpeed * 2.2f, RED, 4, false)) b->bolt = true; break;  // drawn as a warm laser bolt  // straight down the forward axis
        case EnemyWeapon::Missile:
            for (int i = 0; i < std::max(1, nShots); i++) shot(base + (i - (nShots - 1) / 2.f) * 0.5f, kEnemyBulletSpeed * 0.7f, {255, 200, 60, 255}, 6, true);
            break;
        case EnemyWeapon::Spiral:  // rotating ring arms
            for (int i = 0; i < nShots; i++) shot(e.spin + i * 2 * PI / nShots, kEnemyBulletSpeed * 0.8f, {180, 120, 255, 255}, 5, false);
            e.spin += 0.33f;
            break;
        case EnemyWeapon::Sweep:   // fast bolts sweeping across the player's side
            for (int i = 0; i < nShots; i++) shot(PI + std::sin(e.t * 1.6f) * 0.9f + i * 0.05f, kEnemyBulletSpeed * 2.f, RED, 4, false);
            break;
    }
    e.fireAnimT = 0;
    sfx.play(Sfx::Hit, 0.25f, 0.2f);
}

void Enemies::rotaryRound(Enemy& e, Vector2 player, const View& v, Audio& sfx) {
    const float s = v.s;
    // muzzleOf already projects the model muzzle at the ship's depth (hiddenAxis), like every other weapon.
    const Vector2 from = muzzleOf ? muzzleOf(e) : e.pos;
    const Vector2 aim = Vector2Normalize(Vector2Subtract(player, from));  // re-aimed every round: the burst tracks
    const float ang = std::atan2(aim.y, aim.x) + frand(-0.05f, 0.05f);   // small rotary-cannon scatter
    const float speed = kEnemyBulletSpeed * 1.15f * e.def->shotSpeed;
    if (pathCrossesSafe(from, {std::cos(ang) * speed * s, std::sin(ang) * speed * s}, 4.f * e.def->shotSize * s)) { warden.dropped++; sfx.play(Sfx::Hit, 0.18f, 0.3f); return; }  // t251
    if (EnemyShot* b = shots.spawn()) {  // same ownership/fields as Enemies::fire's shot()
        b->faction = Faction::Enemy;
        b->pos = from; b->dmg = e.def->shotDamage; b->vel = {std::cos(ang) * speed * s, std::sin(ang) * speed * s};
        b->col = {255, 180, 60, 255}; b->r = 4.f * e.def->shotSize * s; b->homing = false;
    }
    sfx.play(Sfx::Hit, 0.18f, 0.3f);
}

void Enemies::update(float dt, Vector2 player, const LevelDef& L, const View& v, const Database& db, Audio& sfx) {
    const float s = v.s, F = v.forward(), Lat = v.lateral();
    // t251: the Warden's rest zone (bottom-left pocket) is live for the whole fight
    safeOn = wardenOn();
    safeRect = {0.f, Lat * 0.79f, F * 0.30f, Lat * 0.21f};

    // Formations march as a block.
    for (int g = 0; g < kMaxFormations; g++) {
        Formation& f = formations_[g];
        if (!f.active) continue;
        int alive = 0; float minLat = 1e9f, maxLat = -1e9f, minFwd = 1e9f;
        list.each([&](const Enemy& e) {
            if (e.group != g) return;
            alive++;
            minLat = std::min(minLat, e.pos.y); maxLat = std::max(maxLat, e.pos.y); minFwd = std::min(minFwd, e.pos.x);
        });
        if (!alive) { f.active = false; continue; }
        float speedUp = 1 + 2.5f * (1 - (float)alive / f.initial);
        f.origin.y += f.dir * f.speed * speedUp * dt;
        if (f.entering) {
            // Glide in from beyond the edge; hold behind (still off-screen) any grid that is
            // still occupying the settle area, then ease onto the settle line.
            float goal = f.settleX;
            for (int h = 0; h < kMaxFormations; h++)
                if (h != g && formations_[h].active)
                    goal = std::max(goal, formations_[h].origin.x + formations_[h].extent.x + f.spacing);
            f.origin.x = formationEntryStep(f.origin.x, goal, f.entrySpeed, dt);
            if (f.origin.x <= f.settleX + 0.5f * s) { f.origin.x = f.settleX; f.entering = false; }
        }
        if ((f.dir > 0 && maxLat > Lat - 30 * s) || (f.dir < 0 && minLat < 30 * s)) {
            f.dir = -f.dir;
            if (!f.entering && minFwd > 140 * s) f.origin.x -= f.step;  // step toward the player
        }
    }

    list.each([&](Enemy& e) {
        const EnemyDef& d = *e.def;
        const float spd = d.speed * s * L.speedMul;
        // Boss phases: pick the deepest phase whose hp threshold we've crossed.
        Pattern pattern = d.pattern; EnemyWeapon weapon = d.weapon; float interval = d.fireInterval; int nShots = d.shots;
        const BossPhase* ph = nullptr;
        for (std::size_t i = 0; i < d.phases.size(); i++)
            if ((float)e.hp / e.maxHp <= d.phases[i].hp) { ph = &d.phases[i]; e.phase = (int)i; }
        if (ph) { pattern = ph->pattern; weapon = ph->weapon; interval = ph->interval; nShots = ph->shots; }
        Vector2 prev = e.pos;
        e.t += dt; e.spawnAge += dt; e.flash -= dt; e.shieldContact = std::max(0.f, e.shieldContact - dt);
        stepHitBar(e.barSince, e.barAlpha, dt);
        if (e.fireAnimT >= 0) e.fireAnimT += dt;
        if (d.boss && wardenOn()) { updateWarden(e, dt, player, L, v, sfx); return; }   // t251: scripted boss
        switch (pattern) {
            case Pattern::Straight: e.origin.x -= spd * dt * scrollMul; e.pos = e.origin; break;
            case Pattern::Sine:
                e.origin.x -= spd * dt * scrollMul;
                e.pos = {e.origin.x, e.origin.y + std::sin(e.t * e.freq) * e.amp * s};
                break;
            case Pattern::Dive:  // cruise in, then swoop at the player's lane
                e.origin.x -= spd * dt * scrollMul * (e.origin.x < F * 0.65f ? 1.8f : 1.f);
                if (e.origin.x < F * 0.65f) e.origin.y += Clamp(player.y - e.origin.y, -1, 1) * spd * 0.9f * dt;
                e.pos = e.origin;
                break;
            case Pattern::Circle:
                e.origin.x -= spd * 0.5f * dt * scrollMul;
                e.pos = {e.origin.x + std::cos(e.t * e.freq) * e.amp * s, e.origin.y + std::sin(e.t * e.freq) * e.amp * s};
                break;
            case Pattern::Path: {
                // points: (forward 0..1, lateral 0..1); traverse over F/speed seconds. A wave may
                // author its own path; t < 0 waits off-screen on the entry line.
                const std::vector<Vector2>& P = e.path ? *e.path : d.path;
                if (P.size() >= 2) e.pos = pathPoint(P, e.t, F, Lat, spd);
                else { e.origin.x -= spd * dt; e.pos = e.origin; }
                break;
            }
            case Pattern::Invaders:
                if (e.group >= 0) e.pos = Vector2Add(formations_[e.group].origin, e.gridOff);
                break;
            case Pattern::Boss: {
                float hold = F * 0.78f;
                if (e.origin.x > hold) e.origin.x -= spd * dt;
                e.pos = {e.origin.x + std::sin(e.t * 0.7f) * 20 * s, Lat / 2 + std::sin(e.t * e.freq * 0.5f) * (Lat * 0.32f)};
                break;
            }
        }
        if ((retreat || e.leaveT >= 0.f) && !d.boss) {  // once ordered out, a ship keeps leaving
            // Disengage: carry on the way the ship faces (toward the player's edge), speeding up,
            // and swerve wide of the player's lane. Removed only once well off-screen.
            if (e.leaveT < 0.f) e.leaveT = 0.f;
            e.leaveT += dt;
            const float v = Clamp(e.leaveT / 1.2f, 0.f, 1.f) * std::max(spd * 1.6f, 300.f * s);
            e.leaveOff += v * dt;
            const float dy = e.pos.y - player.y;
            if (e.pos.x - e.leaveOff > player.x - 60.f * s && std::fabs(dy) < 170.f * s)
                e.leaveLat += (dy >= 0.f ? 1.f : -1.f) * 320.f * s * dt;
            e.pos.x -= e.leaveOff; e.pos.y += e.leaveLat;
            e.burstLeft = 0; e.fireT = std::max(e.fireT, 1.f);  // no shots while leaving
        }
        e.vel = Vector2Scale(Vector2Subtract(e.pos, prev), 1 / std::fmax(dt, 1e-4f));
        if (e.burstLeft > 0) {  // rotary burst in progress
            e.burstT -= dt;
            while (e.burstT <= 0 && e.burstLeft > 0) { rotaryRound(e, player, v, sfx); e.burstLeft--; e.burstT += e.def->burstGap; }
        }
        e.fireT -= dt;
        if (e.fireT <= 0 && e.pos.x < F - 10 * s) {
            e.fireT = interval * frand(0.8f, 1.2f) / L.fireMul;
            fire(e, weapon, nShots, player, v, sfx);
            if (d.boss && GetRandomValue(0, 2) == 0) {  // bosses mix in an aimed burst
                for (int k = 0; k < 3; k++) {
                    Vector2 aim = Vector2Normalize(Vector2Subtract(player, e.pos));
                    if (EnemyShot* b = shots.spawn()) {
                        b->pos = e.pos; b->vel = Vector2Scale(aim, (kEnemyBulletSpeed + k * 60) * s); b->col = PINK; b->r = 5 * s;
                    }
                }
            }
        }
        if (ph && !ph->minion.empty() && ph->minionEvery > 0 && (e.minionT -= dt) <= 0) {
            e.minionT = ph->minionEvery;
            pendingMinions_.push_back({ph->minion, ph->minionCount, e.pos.y / Lat});
        }
        if (e.leaveT >= 0.f ? (e.pos.x < -0.8f * F - v.vis) : (e.pos.x < -100 * s - v.vis)) {  // escaped past the player
            e.alive = false;
            if (e.leaveT >= 0.f) fprintf(stderr, "LEAVE %s removed off-screen at local (%.0f, %.0f) after %.1fs (F=%.0f)\n", d.id.c_str(), e.pos.x, e.pos.y, e.leaveT, F);
        }
    });
    ShipPhysics::separateEnemies(list, dt, s,
        v.mode == Mode::SideScroll ? WorldProjection::Side : WorldProjection::Top);
    {   // Depth stays inside the camera's playable volume; physical radius remains unchanged.
        const float dMax = CameraSystem::maxDepthLocal(v);
        list.each([&](Enemy& e) {
            e.hiddenAxis = Clamp(e.hiddenAxis, -dMax, dMax);
        });
    }
    for (auto& m : pendingMinions_) {  // spawned after iteration (pool safety)
        SpawnWave w; w.enemy = m.enemy; w.count = m.count; w.formation = m.formation; w.y = m.y;
        spawnWave(w, db.enemy(m.enemy), L, v);
    }
    pendingMinions_.clear();

    shots.each([&](EnemyShot& b) {
        b.previousPos = b.pos;
        b.previousHiddenAxis = b.hiddenAxis;
        b.age += dt;
        const ProjectileTarget target{player, Faction::Player, true};
        if (b.homing && b.age < 2.5f && !insideSafe(player) && ProjectileTargeting::canTarget(b.faction, target)) {
            Vector2 want = Vector2Scale(Vector2Normalize(Vector2Subtract(player, b.pos)), Vector2Length(b.vel));
            b.vel = Vector2Lerp(b.vel, want, std::fmin(1.f, 1.6f * dt));
        }
        b.pos = Vector2Add(b.pos, Vector2Scale(b.vel, dt));
        if (safeOn && segCrossesSafe(b.previousPos, b.pos, b.r)) { b.alive = false; warden.blocked++; return; }   // t251: never into the rest zone
        if (b.pos.x < -30 * s - v.vis || b.pos.x > F + 80 * s + v.vis || b.pos.y < -30 * s - v.vis || b.pos.y > Lat + 30 * s + v.vis) b.alive = false;
    });
}
