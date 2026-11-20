#include "particles.hpp"
#include "config.hpp"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <cstring>

void Particles::init() {
    Image g = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);  // generated once
    ImageDrawRectangle(&g, 30, 30, 4, 4, WHITE);  // opaque white core doubles as raylib's shapes texture
    glow_ = LoadTextureFromImage(g);
    UnloadImage(g);
    SetTextureFilter(glow_, TEXTURE_FILTER_BILINEAR);
    // Shapes (rects, lines, circles) sample the glow's white core, so shapes + glows + bullets share
    // one texture and stay in one batch (each texture switch = a flush that stalls on Mali via kbase).
    SetShapesTexture(glow_, {31, 31, 2, 2});
    {   // Engine flame strip (reference video): rounded base flush with the nozzle, tapered teardrop with a
        // ragged turbulent tail; crisp opaque white core (bulk), narrow lavender rim, faint purple halo.
        const int W = 128, H = 48, F = cfg::kFlameFrames;
        Image im = GenImageColor(W, H * F, BLANK);
        auto hash = [](int a, int b) { unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffff) / 65535.f; };
        for (int f = 0; f < F; f++) {
            const float len = (0.78f + 0.22f * hash(f, 7)) * (W - 4);   // per-frame length jitter
            const float r0 = H * 0.30f;                                     // base radius
            for (int x = 0; x < W; x++) {
                float t = (x - r0) / (len - r0);                            // 0 at base centre .. 1 at tip
                // jagged tail: per-frame value noise on the radius, stronger toward the tip
                float n = hash(f, x / 6) * (1 - std::fmod(x / 6.f, 1.f)) + hash(f, x / 6 + 1) * std::fmod(x / 6.f, 1.f);
                float rad = t < 0 ? std::sqrt(std::fmax(0.f, r0 * r0 - (x - r0) * (x - r0)))   // rounded base
                                  : r0 * std::pow(std::fmax(0.f, 1 - t), 0.75f) * (1 + (n - 0.5f) * 0.9f * t);
                float yoff = (hash(f * 31, x / 9) - 0.5f) * H * 0.10f * std::fmax(0.f, t);     // tail wobble
                for (int y = 0; y < H; y++) {
                    float d = std::fabs(y + 0.5f - H / 2.f - yoff);
                    Color c = BLANK;
                    if (rad > 0.5f && d < rad * 0.62f) c = {255, 255, 255, 255};                  // core
                    else if (rad > 0.5f && d < rad) c = {205, 175, 255, 200};                      // lavender rim
                    else if (d < rad + H * 0.12f && x < len) c = {150, 100, 255, (unsigned char)(70 * (1 - (d - rad) / (H * 0.12f)))};  // halo
                    if (c.a) ImageDrawPixel(&im, x, f * H + y, c);
                }
            }
        }
        flame_ = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(flame_, TEXTURE_FILTER_BILINEAR);
    }
    {   // 32px disc with a 1px soft edge
        Image d = GenImageColor(32, 32, BLANK);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float r = std::sqrt((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f));
            float a = std::fmin(1.f, std::fmax(0.f, 15.5f - r));
            ImageDrawPixel(&d, x, y, {255, 255, 255, (unsigned char)(a * 255)});
        }
        dot_ = LoadTextureFromImage(d); UnloadImage(d);
        SetTextureFilter(dot_, TEXTURE_FILTER_BILINEAR);
    }
}

void Particles::shutdown() { UnloadTexture(glow_); UnloadTexture(dot_); UnloadTexture(flame_); }

void Particles::clear() { soft_.clear(); hard_.clear(); smoke_.clear(); texts_.clear(); }

Particle* Particles::add(Pool<Particle, 3000>& pool, Vector2 p, Vector2 v, float life, float size, Color c) {
    Particle* q = pool.spawn();
    if (!q) return nullptr;
    q->pos = p; q->vel = v; q->life = q->maxLife = life; q->size = size; q->col = c;
    return q;
}

static Vector2 radial(float speed) {
    float a = frand(0, 2 * PI), v = frand(0.2f, 1.f) * speed;
    return {std::cos(a) * v, std::sin(a) * v};
}

void Particles::explosion(Vector2 p, float k, Color tint) {
    // 1) white flash
    if (auto* q = add(soft_, p, {}, 0.12f, 140 * k, WHITE)) q->grow = 400 * k;
    // 2) fireball blobs
    for (int i = 0; i < (int)(14 * k + 6); i++)
        if (auto* q = add(soft_, Vector2Add(p, radial(12 * k)), radial(120 * k), frand(0.3f, 0.6f), frand(30, 60) * k,
                          i % 3 ? Color{255, 140, 40, 255} : tint)) { q->grow = -40 * k; q->drag = 0.92f; }
    // 3) debris sparks
    for (int i = 0; i < (int)(40 * k + 10); i++)
        if (auto* q = add(hard_, p, radial(520 * k), frand(0.4f, 1.0f), frand(2, 4.5f) * std::sqrt(k),
                          i % 2 ? Color{255, 220, 120, 255} : tint)) q->drag = 0.95f;
    // 4) smoke ring
    for (int i = 0; i < (int)(12 * k + 4); i++) {
        float a = 2 * PI * i / (12 * k + 4);
        Vector2 v{std::cos(a) * 140 * k, std::sin(a) * 140 * k};
        if (auto* q = add(smoke_, p, v, frand(0.7f, 1.2f), 30 * k, {40, 40, 50, 160})) { q->grow = 50 * k; q->drag = 0.93f; }
    }
}

void Particles::pickupVaporize(Vector2 p, float k, Color tint) {
    // A deliberately bounded pickup effect: collection must remain cheap even when the
    // fixed particle pools are already busy with combat explosions.
    if (auto* q = add(soft_, p, {}, 0.22f, 54 * k, WHITE)) q->grow = -180 * k;
    for (int i = 0; i < 18; i++) {
        Vector2 v = radial(170 * k);
        if (auto* q = add(soft_, p, v, frand(0.28f, 0.48f), frand(8, 18) * k, tint)) {
            q->grow = frand(12, 34) * k; q->drag = 0.93f;
        }
    }
    for (int i = 0; i < 24; i++) {
        if (auto* q = add(hard_, p, radial(300 * k), frand(0.22f, 0.48f), frand(1.5f, 3.5f) * k,
                          i % 4 ? tint : WHITE)) q->twinkle = true;
    }
    for (int i = 0; i < 7; i++) {
        if (auto* q = add(smoke_, Vector2Add(p, radial(8 * k)), radial(55 * k), frand(0.32f, 0.55f),
                          frand(10, 20) * k, Fade(tint, 0.38f))) { q->grow = 34 * k; q->drag = 0.94f; }
    }
}

void Particles::sparks(Vector2 p, Vector2 dir, int n, Color c, float speed, float sizeScale) {
    float base = std::atan2(dir.y, dir.x);
    for (int i = 0; i < n; i++) {
        float a = base + frand(-0.8f, 0.8f), v = frand(0.3f, 1.f) * speed;
        add(hard_, p, {std::cos(a) * v, std::sin(a) * v}, frand(0.15f, 0.35f), frand(1.5f, 3.f) * sizeScale, c);
    }
}

void Particles::embers(Vector2 p, int n, float size, float speed) {
    for (int i = 0; i < n; i++) {
        const float vx = frand(-0.28f, 0.28f) * speed;
        const float vy = frand(0.52f, 1.15f) * speed;
        const Color c = i % 3 == 0 ? Color{255, 216, 105, 255} : Color{255, 92, 24, 255};
        if (Particle* q = add(hard_, p, {vx, vy}, frand(0.28f, 0.62f), frand(0.65f, 1.25f) * size, c))
            q->twinkle = true;
    }
}

void Particles::smoke(Vector2 p, float size) {
    if (auto* q = add(smoke_, p, {frand(-60, -20), frand(-20, 20)}, frand(0.5f, 0.9f), size, {70, 70, 80, 120}))
        q->grow = size * 1.5f;
}

void Particles::turretSmoke(Vector2 p, float size) {
    if (auto* q = add(smoke_, p, {frand(-30, -8), frand(-34, -14)}, frand(.6f, 1.f), size, {22, 22, 26, 150}))
        q->grow = size * 1.1f;
}

void Particles::dust(Vector2 p, Vector2 vel, float size, float life, Color c) {
    if (auto* q = add(smoke_, p, vel, life, size, c)) { q->grow = size * 1.4f; q->drag = 0.93f; }
}

void Particles::glitter(Vector2 p, Vector2 vel, float size, float life) {
    if (auto* q = add(hard_, p, vel, life, size, {(unsigned char)frand(180, 255), (unsigned char)frand(200, 255), 255, 255}))
        q->twinkle = true;
}

void Particles::flameSpark(Vector2 p, Vector2 vel, float size, float life) {
    Color c = GetRandomValue(0, 2) ? Color{255, 255, 255, 255} : Color{210, 185, 255, 255};
    add(hard_, p, vel, life, size, c);
}

void Particles::text(Vector2 p, const char* t, Color c, float sizeScale) {
    if (auto* f = texts_.spawn()) { f->pos = p; f->life = 1.0f; f->col = c; f->scale = Clamp(sizeScale, 0.8f, 1.3f); std::snprintf(f->text, sizeof f->text, "%s", t); }
}

void Particles::update(float dt) {
    auto step = [dt](Particle& q) {
        q.pos = Vector2Add(q.pos, Vector2Scale(q.vel, dt));
        q.vel = Vector2Scale(q.vel, q.drag);
        q.size = std::fmax(0.5f, q.size + q.grow * dt);
        if ((q.life -= dt) <= 0) q.alive = false;
    };
    soft_.each(step); hard_.each(step); smoke_.each(step);
    texts_.each([dt](FloatText& f) { f.pos.y -= 50 * dt; if ((f.life -= dt) <= 0) f.alive = false; });
}

void Particles::drawAdditive() const {
    const Rectangle src{0, 0, (float)glow_.width, (float)glow_.height};
    const float cap = gfx::low ? 110.f : 1e9f;  // low: clamp huge flash/fireball quads (fill rate)
    soft_.each([&](const Particle& q) {
        float a = q.life / q.maxLife;
        if (gfx::low && a < 0.1f) return;
        float sz = std::fmin(q.size, cap);
        DrawTexturePro(glow_, src, {q.pos.x, q.pos.y, sz, sz}, {sz / 2, sz / 2}, 0, Fade(q.col, a));
    });
    hard_.each([](const Particle& q) {
        float a = q.life / q.maxLife;
        if (q.twinkle) a *= frand(0.3f, 1.f);
        DrawRectangleV({q.pos.x - q.size / 2, q.pos.y - q.size / 2}, {q.size, q.size}, Fade(q.col, a));
    });
}

void Particles::drawSmoke() const {
    const Rectangle src{0, 0, (float)glow_.width, (float)glow_.height};
    smoke_.each([&](const Particle& q) {
        if (gfx::low && q.life / q.maxLife < 0.15f) return;
        DrawTexturePro(glow_, src, {q.pos.x, q.pos.y, q.size, q.size}, {q.size / 2, q.size / 2}, 0, Fade(q.col, q.life / q.maxLife));
    });
}

void Particles::drawText(float s) const {
    texts_.each([s](const FloatText& f) {
        int fs = (int)(22 * s * f.scale);
        const int x = (int)(f.pos.x - MeasureText(f.text, fs) / 2), y = (int)f.pos.y;
        const Color edge = Fade(BLACK, std::fmin(0.9f, f.life * 1.8f));
        DrawText(f.text, x - 1, y, fs, edge); DrawText(f.text, x + 1, y, fs, edge);
        DrawText(f.text, x, y - 1, fs, edge); DrawText(f.text, x, y + 1, fs, edge);
        DrawText(f.text, x, y, fs, Fade(f.col, std::fmin(1.f, f.life * 2)));
    });
}
