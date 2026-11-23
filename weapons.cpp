#include "weapons.hpp"
#include "audio.hpp"
#include "config.hpp"
#include "particles.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace cfg;

// ---- laser bolt texture: the makeLaserBolt fragment shader evaluated once into an RGBA texture ----
namespace {
struct BoltLook { Color core, plasma, halo, tip; };
float smooth(float a, float b, float x) { float t = std::fmin(1.f, std::fmax(0.f, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
Texture2D makeBoltTexture(const BoltLook& L) {
    const int W = 128, H = 144;  // aspect = width/length = 1.91/1.7
    const float tailStart = .18f, tailEnd = .79f, noseStart = .87f, noseEnd = .96f, tipPos = .68f, tipSpread = 7.2f;
    const float coreF = 145, plasmaF = 13, haloF = 7, tipF = 23, coreG = .88f, plasmaG = .7f, haloG = .48f, tipG = .44f;
    Image im = GenImageColor(W, H, BLANK);
    auto c3 = [](Color c) { return Vector3{c.r / 255.f, c.g / 255.f, c.b / 255.f}; };
    const Vector3 core = c3(L.core), plasma = c3(L.plasma), halo = c3(L.halo), tipc = c3(L.tip);
    for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
        float x = (px + .5f) / W, y = std::fabs((py + .5f) / H - .5f);
        float shape = smooth(tailStart, tailEnd, x) * (1 - smooth(noseStart, noseEnd, x));
        float tip = (x - tipPos) * tipSpread, edge = 1 - smooth(.36f, .5f, y);
        float eh = std::exp(-y * haloF) * haloG, ep = std::exp(-y * plasmaF) * plasmaG, ec = std::exp(-y * coreF) * coreG;
        float et = std::exp(-tip * tip - y * tipF) * tipG;
        Vector3 e{(halo.x * eh + plasma.x * ep + core.x * ec) * shape + tipc.x * et,
                  (halo.y * eh + plasma.y * ep + core.y * ec) * shape + tipc.y * et,
                  (halo.z * eh + plasma.z * ep + core.z * ec) * shape + tipc.z * et};
        e = Vector3Scale(e, edge);
        float a = std::fmin(1.f, std::fmax(e.x, std::fmax(e.y, e.z)));
        auto u8 = [](float v) { return (unsigned char)std::lround(std::fmin(1.f, std::fmax(0.f, v)) * 255); };
        ImageDrawPixel(&im, px, py, {u8(e.x), u8(e.y), u8(e.z), u8(a)});
    }
    Texture2D t = LoadTextureFromImage(im); UnloadImage(im);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}
// spark sprite: 1 - smoothstep(softness .05, 1, r), white (tinted per spark)
Texture2D makeSparkTexture() {
    const int N = 32; Image im = GenImageColor(N, N, BLANK);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float r = std::sqrt(std::pow((x + .5f) / N - .5f, 2.f) + std::pow((y + .5f) / N - .5f, 2.f)) * 2;
        ImageDrawPixel(&im, x, y, {255, 255, 255, (unsigned char)std::lround((1 - smooth(.05f, 1, r)) * 255)});
    }
    Texture2D t = LoadTextureFromImage(im); UnloadImage(im);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}
}  // namespace

const Texture2D& boltTexture(bool enemy) {
    static Texture2D player = makeBoltTexture({{0xea, 0xfa, 0xff, 255}, {0x0f, 0xe6, 0xff, 255}, {0x14, 0x52, 0xff, 255}, {0x4d, 0xbf, 0xff, 255}});
    static Texture2D foe = makeBoltTexture({{0xff, 0xf1, 0xea, 255}, {0xff, 0x4a, 0x1a, 255}, {0xff, 0x14, 0x40, 255}, {0xff, 0x8a, 0x4d, 255}});
    return enemy ? foe : player;
}

const char* primaryName(int w) {
    switch (w) { case kPrimaryFlame: return "FLAMER"; case kPrimaryPlasma: return "PLASMA";
                 case kPrimaryRotary: return "ROTARY LASER"; default: return "LASER"; }
}

void Weapons::reset() {
    lasers.clear(); missiles.clear(); sparks.clear(); flames.clear();
    plasmaHeat = 0; plasmaLock = 0; beamOn = false; beamLen = 0; beamAcc = 0; beamHit = false; beamHitPos = {}; beamDepthScale = 1.f;
    flameOn = false; flameTick = 0; flameDebt_ = 0; flameStops.fill(0.f); plasmaCycleT_ = 0.f;
    level = 1; ammo = kMissileStartAmmo; rapid = 0; heat = 0; overheat = 0; flameBeam = false; fireT_ = 0; rotaryT_ = 0; missileT_ = 0.5f;
    laserVolley_ = rotarySide_ = 0; rotarySpin = 0;
}

void Weapons::update(float dt, Vector2 ship, Pool<Enemy, 160>& enemies, const View& v, Particles& fx, Audio& sfx,
                     bool primaryFire, bool secondaryFire, float aim, const ProjectileTarget* extraTarget,
                     const std::array<Vector2, 4>& cannonMuzzles,
                     const std::array<Vector2, 2>& rotaryMuzzles) {
    const float s = v.s;
    rapid = std::fmax(0.f, rapid - dt);
    overheat = std::fmax(0.f, overheat - dt);
    heat = std::fmax(0.f, heat - dt * (rapid > 0 ? 1.8f : 0.72f));
    const bool rotaryRequested = primary == kPrimaryRotary && primaryFire && overheat <= 0 && (rapid > 0 || heat < 1.f);
    rotarySpin += (rotaryRequested ? 1.f - rotarySpin : -rotarySpin) * fminf(1.f, dt * (rotaryRequested ? 8.f : 3.f));
    if (rotarySpin < 0.01f) rotarySpin = 0.f;
    const float ca0 = std::cos(aim), sa0 = std::sin(aim);
    const Vector2 muzzle{ship.x + 40 * s * ca0, ship.y + 40 * s * sa0}, fwd{ca0, sa0};

    // --- plasma lance: fixed firing/cooling pulses while held, plus forced cooldown at 100 % ---
    plasmaLock = std::fmax(0.f, plasmaLock - dt);
    const bool plasmaRequested = primary == kPrimaryPlasma && primaryFire;
    if (plasmaRequested) {
        const float cycle = kPlasmaPulseOn + kPlasmaPulseOff;
        plasmaCycleT_ = std::fmod(plasmaCycleT_ + dt, cycle);
    } else plasmaCycleT_ = 0.f;
    beamOn = plasmaRequested && plasmaCycleT_ < kPlasmaPulseOn && plasmaLock <= 0;
    beamHitT = std::fmax(0.f, beamHitT - dt);
    if (beamOn) {
        plasmaHeat += dt * kPlasmaHeatRate * (rapid > 0 ? 0.5f : 1.f);
        if (plasmaHeat >= 1.f) { plasmaHeat = 1.f; plasmaLock = kPlasmaLockTime; beamOn = false; }
    } else plasmaHeat = std::fmax(0.f, plasmaHeat - dt * (plasmaLock > 0 ? 1.f / kPlasmaLockTime : kPlasmaCoolRate));
    sfx.plasmaLance(beamOn, plasmaHeat, plasmaLock);   // t235: beam hum (pitch / strain with heat), lockout alarm + cooling hiss
    beamFrom = muzzle; beamDir = fwd;
    beamDepthScale = 1.f;
    const float margin = 40.f * s + v.vis;
    auto rayEdge = [](float p, float d, float lo, float hi) {
        if (d > 1e-5f) return (hi - p) / d;
        if (d < -1e-5f) return (lo - p) / d;
        return std::numeric_limits<float>::infinity();
    };
    const float xExit = rayEdge(muzzle.x, fwd.x, -margin, v.forward() + margin);
    const float yExit = rayEdge(muzzle.y, fwd.y, -margin, v.lateral() + margin);
    beamMax = std::max(0.f, std::min(xExit, yExit));
    if (!beamOn) { beamLen = 0; beamHit = false; }

    // --- flamethrower: a short cone of fire, damage over time (Game applies the ticks) ---
    flameOn = primary == kPrimaryFlame && primaryFire;
    flameFrom = muzzle; flameDir = fwd;
    flameRange = kFlameRangeK * v.forward();
    flameStops.fill(flameRange);
    flameBeam = flameOn;
    if (flameOn) {
        flameDebt_ += dt * 150.f;
        const float life = 0.42f, speed = flameRange / life * 1.2f;   // drag: reaches ~the cone range
        while (flameDebt_ >= 1.f) {
            flameDebt_ -= 1.f;
            if (FlameBit* f = flames.spawn()) {
                const int ray = GetRandomValue(0, kFlameRayCount - 1);
                const float a = aim - kFlameHalfAngle + (2.f * kFlameHalfAngle) * ray / (kFlameRayCount - 1.f);
                f->pos = Vector2Add(muzzle, {frand(-4, 4) * s, frand(-4, 4) * s});
                const float particleSpeed = speed * frand(0.8f, 1.1f);
                f->vel = {std::cos(a) * particleSpeed, std::sin(a) * particleSpeed};
                f->life = life * frand(0.8f, 1.1f); f->size = frand(14, 22) * s; f->spin = frand(0, 6.28f);
                f->rayIndex = ray;
            }
        }
    } else flameDebt_ = 0;
    flames.each([&](FlameBit& f) {
        f.age += dt;
        if (f.age >= f.life) { f.alive = false; return; }
        f.pos = Vector2Add(f.pos, Vector2Scale(f.vel, dt));
        f.vel = Vector2Scale(f.vel, std::exp(-1.2f * dt));
    });

    // --- laser volleys: 1 = single, 2 = double, 3 = spread ---
    fireT_ -= dt;
    if (primary == kPrimaryLaser && primaryFire && fireT_ <= 0 && overheat <= 0 && (rapid > 0 || heat < 1.f)) {
        fireT_ = cfg::dev.weaponCooldown * (rapid > 0 ? kRapidMultiplier : 1.f);
        struct Shot { float angle; };
        static const Shot single[] = {{0}};
        static const Shot dbl[] = {{0}, {0}};
        static const Shot spread[] = {{0}, {0}, {-0.18f}, {0.18f}, {0}};
        const Shot* set = level >= 3 ? spread : level == 2 ? dbl : single;
        int n = level >= 3 ? 5 : level == 2 ? 2 : 1;
        const unsigned volley = laserVolley_++;
        if (rapid <= 0) {
            heat = std::fmin(1.f, heat + 0.052f + 0.009f * (float)n);
            if (heat >= 1.f) overheat = 1.15f;
        }
        for (int i = 0; i < n; i++)
            if (Laser* l = lasers.spawn()) {
                // Single shots cycle all four barrels. Double volleys alternate the nose
                // and wing pairs. Spread fires all four plus one cycling centre shot.
                const unsigned barrel = n == 1 ? volley % 4 : n == 2 ? (volley % 2) * 2 + i
                                                : i < 4 ? i : volley % 4;
                l->pos = cannonMuzzles[barrel];
                l->previousPos = l->pos; l->faction = Faction::Player;
                l->vel = {std::cos(set[i].angle + aim) * kLaserSpeed * s, std::sin(set[i].angle + aim) * kLaserSpeed * s};
                l->damage = cfg::dev.laserDamage;
                l->col = rapid > 0 ? Color{255, 120, 220, 255} : Color{120, 230, 255, 255};
                l->flightSound = sfx.laserFire();
            }
    }
    // Six-barrel rotary clusters: small bolts alternate sides as the rotor reaches speed.
    rotaryT_ -= dt;
    if (rotaryRequested && rotarySpin >= 0.4f && rotaryT_ <= 0.f) {
        rotaryT_ = cfg::dev.weaponCooldown * 0.65f * (rapid > 0 ? kRapidMultiplier : 1.f);
        if (rapid <= 0) {
            heat = std::fmin(1.f, heat + 0.07f);
            if (heat >= 1.f) overheat = 1.15f;
        }
        if (Laser* l = lasers.spawn()) {
            l->pos = rotaryMuzzles[rotarySide_++ % rotaryMuzzles.size()];
            l->previousPos = l->pos; l->faction = Faction::Player;
            l->vel = {std::cos(aim) * kLaserSpeed * s, std::sin(aim) * kLaserSpeed * s};
            l->damage = cfg::dev.laserDamage;
            l->visualScale = 0.38f;
            l->col = rapid > 0 ? Color{255, 120, 220, 255} : Color{120, 230, 255, 255};
            l->flightSound = sfx.laserFire();
        }
    }
    // bolt trail sparks (screen space): unit = bolt length / 1.7, emitted along the distance travelled this frame
    const float baseBoltLen = kLaserLength * kBoltQuadScale * s;
    sparkDebt_ += kSparkRate * dt;
    const int perBolt = (int)sparkDebt_; sparkDebt_ -= perBolt;
    lasers.each([&](Laser& l) {
        const float boltLen = baseBoltLen * l.visualScale, unit = boltLen / 1.7f;
        Vector2 prev = l.pos;
        l.previousPos = l.pos;
        l.previousHiddenAxis = l.hiddenAxis;
        l.pos = Vector2Add(l.pos, Vector2Scale(l.vel, dt));
        if (l.pos.x > v.forward() + 40 * s + v.vis || l.pos.y < -40 * s - v.vis || l.pos.y > v.lateral() + 40 * s + v.vis) { l.alive = false; return; }
        Vector2 dirS = Vector2Normalize(v.dirToScreen(l.vel)), nrm{-dirS.y, dirS.x};
        Vector2 centre = Vector2Subtract(v.toScreen(l.pos), Vector2Scale(dirS, boltLen * (0.96f - 0.5f)));
        float travelled = Vector2Distance(v.toScreen(prev), v.toScreen(l.pos));
        for (int k = 0; k < perBolt; k++)
            if (BoltSpark* p = sparks.spawn()) {
                p->pos = Vector2Add(Vector2Subtract(centre, Vector2Scale(dirS, travelled * frand(0, 1) + boltLen * 0.01f)),
                                    Vector2Scale(nrm, frand(-.5f, .5f) * kSparkSpawnSpread * unit));
                p->vel = Vector2Add(Vector2Scale(dirS, -frand(kSparkDriftMin, kSparkDriftMax) * unit),
                                    Vector2Scale(nrm, frand(-.5f, .5f) * kSparkSpread * unit));
                p->dur = frand(kSparkLifeMin, kSparkLifeMax); p->size = frand(kSparkSizeMin, kSparkSizeMax) * s * l.visualScale; p->life = 1;
            }
    });
    sparks.each([&](BoltSpark& p) {
        p.life -= dt / p.dur;
        if (p.life <= 0) { p.alive = false; return; }
        p.pos = Vector2Add(p.pos, Vector2Scale(p.vel, dt));
    });

    // --- homing missiles: a tap on the secondary control launches the available pair ---
    missileT_ -= dt;
    if (secondaryFire && missileT_ <= 0 && ammo > 0) {
        missileT_ = cfg::dev.weaponCooldown * 6.f;
        for (int side = -1; side <= 1 && ammo > 0; side += 2)
            if (Missile* m = missiles.spawn()) {
                m->faction = Faction::Player;
                ammo--;
                m->pos = {ship.x, ship.y + side * 22 * s};
                m->previousPos = m->pos;
                m->vel = {120 * s, side * 220 * s};  // kick out sideways, then home in
                m->damage = cfg::dev.missileDamage;
            }
        sfx.missileLaunch();
    }
    missiles.each([&](Missile& m) {
        m.previousPos = m.pos;
        m.previousHiddenAxis = m.hiddenAxis;
        m.age += dt;
        // Select among active hostile ships and the optional active hostile emplacement.
        Vector2 best{}; bool hasTarget = false; float bd = 1e9f;
        enemies.each([&](Enemy& e) {
            const ProjectileTarget candidate{e.pos, Faction::Enemy, e.alive};
            if (!ProjectileTargeting::canTarget(m.faction, candidate)) return;
            float d = Vector2DistanceSqr(e.pos, m.pos);
            if (e.pos.x > m.pos.x - 60 * s && d < bd) { bd = d; best = e.pos; hasTarget = true; }
        });
        if (extraTarget && ProjectileTargeting::canTarget(m.faction, *extraTarget)) {
            const float distance = Vector2DistanceSqr(extraTarget->pos, m.pos);
            if (extraTarget->pos.x > m.pos.x - 60 * s && distance < bd) {
                best = extraTarget->pos; hasTarget = true;
            }
        }
        Vector2 desired = hasTarget ? Vector2Normalize(Vector2Subtract(best, m.pos)) : Vector2{1, 0};
        float speed = kMissileSpeed * s * std::fmin(1.f, 0.4f + m.age * 1.5f);
        float turn = kMissileTurn * std::fmin(1.f, m.age * 2.f);  // ease-in turning
        Vector2 dir = Vector2Normalize(Vector2Lerp(Vector2Normalize(m.vel), desired, std::fmin(1.f, turn * dt)));
        m.vel = Vector2Scale(dir, speed);
        m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
        // smoke + spark trail (particles live in screen space)
        m.trail -= dt;
        if (m.trail <= 0) {
            m.trail = 0.02f;
            Vector2 p = v.toScreen(m.pos);
            fx.smoke(p, 7 * s);
            fx.sparks(p, v.dirToScreen(Vector2Negate(dir)), 1, {255, 190, 90, 255}, 120 * s);
        }
        if (m.age > 4 || m.pos.x > v.forward() + 80 * s + v.vis || m.pos.x < -80 * s - v.vis || m.pos.y < -80 * s - v.vis || m.pos.y > v.lateral() + 80 * s + v.vis)
            m.alive = false;
    });
}

void Weapons::drawFlameSmoke(const View& v, const Texture2D& glow) const {
    if (!flameOn && flames.count() == 0) return;
    const Rectangle src{0, 0, (float)glow.width, (float)glow.height};
    flames.each([&](const FlameBit& f) {
        const float u = f.age / f.life, sz = f.size * (1.f + 4.2f * u);
        const float alpha = (u < 0.08f ? u / 0.08f : 1.f) * (1.f - u) * 0.52f;
        const Vector2 p = v.toScreen(f.pos);
        DrawTexturePro(glow, src, {p.x, p.y, sz * 1.12f, sz * 0.82f},
                       {sz * 0.56f, sz * 0.41f}, f.spin * RAD2DEG, Fade({70, 24, 18, 255}, alpha));
    });
}

void Weapons::draw(const View& v, const Texture2D& glow) const {
    const float s = v.s;
    const Rectangle gsrc{0, 0, (float)glow.width, (float)glow.height};
    // glows first (one texture), then solid beams/bodies (shapes texture)
    missiles.each([&](const Missile& m) {
        Vector2 p = v.toScreen(m.pos); float g = 34 * s;
        DrawTexturePro(glow, gsrc, {p.x, p.y, g, g}, {g / 2, g / 2}, 0, Fade(ORANGE, 0.7f));
    });
    // laser bolts: one rotated quad each from the precomputed bolt texture (one texture -> one batch)
    const Texture2D& bt = boltTexture(false);
    const float L = kLaserLength * kBoltQuadScale * s, Wd = L * kBoltAspect;
    lasers.each([&](const Laser& l) {
        Vector2 p = v.toScreen(l.pos), d = v.dirToScreen(l.vel);
        float rot = std::atan2(d.y, d.x) * RAD2DEG;
        Color tint = l.col.g < 200 ? Color{255, 175, 235, 255} : WHITE;   // rapid-fire bolts keep their pink cue
        const float len = L * l.visualScale, width = Wd * l.visualScale;
        DrawTexturePro(bt, {0, 0, (float)bt.width, (float)bt.height}, {p.x, p.y, len, width}, {len * 0.96f, width / 2}, rot, tint);  // nose at the bolt position
    });
    // trail sparks: colour = mix(cool #1c82ff, hot #bfffff, life), alpha = life * gain, size shrinks to 50%
    static Texture2D st = makeSparkTexture();
    sparks.each([&](const BoltSpark& p) {
        float f = p.life, sz = p.size * (kSparkShrink + (1 - kSparkShrink) * f);
        Color c{(unsigned char)(0x1c + (0xbf - 0x1c) * f), (unsigned char)(0x82 + (0xff - 0x82) * f), 255, (unsigned char)(255 * std::fmin(1.f, f * kSparkGain))};
        DrawTexturePro(st, {0, 0, (float)st.width, (float)st.height}, {p.pos.x, p.pos.y, sz, sz}, {sz / 2, sz / 2}, 0, c);
    });
    missiles.each([&](const Missile& m) {
        Vector2 dir = Vector2Normalize(m.vel);
        Vector2 a = v.toScreen(m.pos), b = v.toScreen(Vector2Subtract(m.pos, Vector2Scale(dir, 12 * s)));
        DrawLineEx(a, b, 4 * s, {255, 230, 200, 255});
    });
    // Flamethrower hot core; the dark soot body is drawn in the preceding alpha pass.
    flames.each([&](const FlameBit& f) {
        const float u = f.age / f.life, sz = f.size * (1.f + 4.2f * u);
        const float a = (u < 0.08f ? u / 0.08f : 1.f) * (1.f - u);
        const Vector2 p = v.toScreen(f.pos);
        const float core = sz * (0.34f - 0.12f * u);
        DrawTexturePro(glow, gsrc, {p.x, p.y, core, core * 0.78f}, {core / 2, core * 0.39f},
                       f.spin * RAD2DEG, Fade({255, 132, 36, 255}, a * 0.62f));
        if (u < 0.52f) {
            const float hot = core * 0.55f;
            DrawTexturePro(glow, gsrc, {p.x, p.y, hot, hot}, {hot / 2, hot / 2},
                           0, Fade({255, 244, 198, 255}, (1.f - u / 0.52f) * 0.78f));
        }
    });
    // plasma lance: layered additive beam from the muzzle to the impact (or the screen edge)
    if (beamOn && beamLen > 1.f) {
        const float tm = (float)GetTime();
        const Vector2 a = v.toScreen(beamFrom), b = v.toScreen(Vector2Add(beamFrom, Vector2Scale(beamDir, beamLen)));
        const float fl = 0.85f + 0.15f * std::sin(tm * 61.f) * std::sin(tm * 37.f);
        const float widthScale = beamDepthScale;
        DrawLineEx(a, b, 26 * s * fl * widthScale, Fade({170, 60, 255, 255}, 0.22f));
        DrawLineEx(a, b, 13 * s * fl * widthScale, Fade({215, 110, 255, 255}, 0.45f));
        DrawLineEx(a, b, 6 * s * widthScale, Fade({255, 200, 255, 255}, 0.85f));
        DrawLineEx(a, b, 2.5f * s * widthScale, WHITE);
        const float g = 46 * s * fl;
        DrawTexturePro(glow, gsrc, {a.x, a.y, g, g}, {g / 2, g / 2}, 0, Fade({220, 120, 255, 255}, 0.75f));
    }
}

void Weapons::drawBeamImpact(const View& v, const Texture2D& glow) const {
    if (!beamOn || !beamHit || beamLen <= 1.f || glow.id <= 0) return;
    const float tm = (float)GetTime(), s = v.s;
    const Vector2 p = v.toScreen(beamHitPos);
    const float flare = (60.f + 20.f * std::sin(tm * 47.f)) * s * beamDepthScale;
    const Rectangle src{0, 0, (float)glow.width, (float)glow.height};
    DrawTexturePro(glow, src, {p.x, p.y, flare * 1.45f, flare * 1.45f},
                   {flare * 0.725f, flare * 0.725f}, 0, Fade({255, 110, 35, 255}, 0.78f));
    const float core = flare * 0.46f;
    DrawTexturePro(glow, src, {p.x, p.y, core, core}, {core / 2.f, core / 2.f},
                   0, Fade({255, 236, 190, 255}, 0.92f));
}
