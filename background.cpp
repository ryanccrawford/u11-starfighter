#include "background.hpp"
#include "config.hpp"
#include "entities.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "rlgl.h"

void Background::build(const LevelDef& level, Assets& assets, const View& v, const Texture2D& glow) {
    v_ = v; glow_ = glow;
    const float F = v.forward(), Lat = v.lateral(), s = v.s;
    layers_.clear();
    const float m = margin, areaK = ((F + 2 * m) * (Lat + 2 * m)) / std::max(1.f, F * Lat);
    for (const BgLayer& d : level.layers) {
        Layer L; L.def = d;
        if (d.type == LayerType::Texture) L.tex = assets.texture(d.texture);
        int n = 0;
        switch (d.type) {
            case LayerType::Texture: n = L.tex ? 0 : d.density; break;  // missing PNG -> star placeholder
            case LayerType::Stars: n = (int)std::lround(d.density * areaK * 6.f); break;   // point stars: 6x denser
            case LayerType::Nebula: n = std::max(1, d.density / 16); break;
            case LayerType::Planet: case LayerType::Planet3D: n = 1; break;
            case LayerType::Terrain3D: n = 0; break;
            case LayerType::Asteroids: n = d.density / 4; break;
        }
        for (int i = 0; i < n; i++) {
            Item it{{frand(0, F), frand(0, Lat)}, 1, frand(0, 360), frand(-60, 60), 0, 0, d.color};
            switch (d.type) {
                case LayerType::Texture: it.size = frand(1, 2.5f) * s; it.col.a = (unsigned char)frand(90, 255);
                    if (m > 0) it.pos = {frand(-m, F + m), frand(-m, Lat + m)};
                    break;
                case LayerType::Stars: {   // size = brightness class (1 or 2 screen px), real colour temperatures
                    static const Color temp[] = {{170, 195, 255, 255}, {205, 220, 255, 255}, {255, 255, 255, 255}, {255, 244, 220, 255}, {255, 214, 160, 255}, {255, 180, 140, 255}};
                    const float tr = frand(0, 1), w = powf(frand(0, 1), 3.f);
                    const Color tc = temp[tr < 0.12f ? 0 : tr < 0.30f ? 1 : tr < 0.62f ? 2 : tr < 0.82f ? 3 : tr < 0.94f ? 4 : 5];
                    it.col = {(unsigned char)(tc.r * d.color.r / 255), (unsigned char)(tc.g * d.color.g / 255), (unsigned char)(tc.b * d.color.b / 255),
                              (unsigned char)(40 + 215 * w)};
                    it.size = w > 0.6f ? 2.f : 1.f;
                    if (m > 0) it.pos = {frand(-m, F + m), frand(-m, Lat + m)};
                    break; }
                case LayerType::Nebula: it.size = frand(200, 340) * s * d.size; it.col.a = (unsigned char)frand(35, 70); break;
                case LayerType::Planet: it.pos = {F * 0.7f, Lat * d.y}; it.size = 150 * s * d.size; break;
                case LayerType::Planet3D:
                    it.pos = {F * d.x, Lat * d.y}; it.size = 150 * s * d.size;
                    it.rot = frand(0, 6.28f); it.spin = d.spin;
                    it.cloudRot = frand(0, 6.28f); it.cloudSpin = d.cloudSpin;
                    break;
                case LayerType::Terrain3D: break;
                case LayerType::Asteroids: it.size = frand(8, 26) * s * d.size; it.pos.y = Lat * d.y + frand(-0.12f, 0.12f) * Lat; break;
            }
            L.items.push_back(it);
        }
        layers_.push_back(L);
    }
}

void Background::update(float dt, float scrollMul) {
    const float F = v_.forward(), Lat = v_.lateral(), m = margin;
    float nebulaSpeed = 0.f;
    for (const auto& L : layers_) if (L.def.type == LayerType::Nebula) { nebulaSpeed = L.def.speed; break; }
    for (auto& L : layers_) {
        float layerSpeed = L.def.speed;
        if (L.def.type == LayerType::Stars && layerSpeed < 100.f) layerSpeed = 0.f; // far layer sits at infinity
        else if (L.def.type == LayerType::Stars && layerSpeed < 200.f && nebulaSpeed > 0.f) layerSpeed = nebulaSpeed; // near layer follows nebula
        float dx = layerSpeed * v_.s * dt * scrollMul;
        L.offset += dx;
        for (auto& it : L.items) {
            it.pos.x -= dx;
            it.rot += it.spin * dt;
            it.cloudRot += it.cloudSpin * dt;
            const bool starLike = L.def.type == LayerType::Stars || L.def.type == LayerType::Texture;
            const float mm = starLike ? m : 0.f;
            if (it.pos.x < -it.size - mm) {
                it.pos.x += F + 2 * it.size + 2 * mm;
                if (starLike) it.pos.y = frand(-mm, Lat + mm);
            }
        }
    }
}

void Background::draw() const {
    const Rectangle gsrc{0, 0, (float)glow_.width, (float)glow_.height};
    const bool side = v_.mode == Mode::SideScroll;
    const Rectangle pf = v_.pf;
    static const int skip = getenv("BGSKIP") ? atoi(getenv("BGSKIP")) : 0;  // perf bisect: bit = 1 << LayerType
    for (const auto& L : layers_) {
        if (skip & (1 << (int)L.def.type)) continue;
        if (L.tex) {
            // Side: scaled to playfield height, tiled horizontally, scrolls left.
            // Top:  scaled to playfield width,  tiled vertically,   scrolls down.
            const float tw = (float)L.tex->width, th = (float)L.tex->height;
            if (side) {
                float w = tw * (pf.height / th), x0 = pf.x - std::fmod(L.offset, w);
                for (float x = x0; x < pf.x + pf.width; x += w) DrawTexturePro(*L.tex, {0, 0, tw, th}, {x, pf.y, w + 1, pf.height}, {}, 0, L.def.color);
            } else {
                float h = th * (pf.width / tw), y0 = pf.y - h + std::fmod(L.offset, h);
                for (float y = y0; y < pf.y + pf.height; y += h) DrawTexturePro(*L.tex, {0, 0, tw, th}, {pf.x, y, pf.width, h + 1}, {}, 0, L.def.color);
            }
            continue;
        }
        switch (L.def.type) {
            case LayerType::Texture:
                if (starAlpha <= 0.01f) break;
                for (auto& it : L.items) {
                    Vector2 p = v_.toScreen(it.pos);
                    Color c = it.col; if (starAlpha < 1.f) c.a = (unsigned char)(c.a * starAlpha);
                    DrawRectangleV(p, side ? Vector2{it.size * 2, it.size} : Vector2{it.size, it.size * 2}, c);
                }
                break;
            case LayerType::Stars: {   // single screen pixels (2 for the brightest), snapped to the pixel grid
                if (starAlpha <= 0.01f) break;
                const Matrix mv = rlGetMatrixModelview();
                const float k = fmaxf(1e-3f, sqrtf(mv.m0 * mv.m0 + mv.m1 * mv.m1)), ip = 1.f / k;   // logical units per screen pixel
                for (auto& it : L.items) {
                    Vector2 p = v_.toScreen(it.pos);
                    p = {(floorf((p.x * k) + mv.m12) - mv.m12) * ip, (floorf((p.y * k) + mv.m13) - mv.m13) * ip};
                    Color c = it.col; if (starAlpha < 1.f) c.a = (unsigned char)(c.a * starAlpha);
                    DrawRectangleV(p, {it.size * ip, it.size * ip}, c);
                }
                break; }
            case LayerType::Nebula:
                if (nebulaTex && nebulaTex->id > 0) {   // a band of the baked gas / dust sky, tiled along the scroll
                    const Texture2D& T = *nebulaTex; const float TW = (float)T.width, TH = (float)T.height;
                    const Rectangle src{0, TH * 0.18f, TW, TH * 0.64f};
                    const float o = L.offset * 0.5f;
                    const float strength = std::clamp(L.def.density / 100.f, 0.f, 1.f) * 0.34f;
                    const Color tint = Fade(L.def.color, strength);
                    BeginBlendMode(BLEND_ADDITIVE);
                    if (side) {
                        const float w = TW * (pf.height / src.height), x0 = pf.x - std::fmod(o, w);
                        for (float x = x0; x < pf.x + pf.width; x += w) DrawTexturePro(T, src, {x, pf.y, w + 1, pf.height}, {}, 0, tint);
                    } else {
                        const float h = TW * (pf.width / src.height), y0 = pf.y - h + std::fmod(o, h);
                        for (float y = y0; y < pf.y + pf.height; y += h) DrawTexturePro(T, src, {pf.x + pf.width, y, h + 1, pf.width}, {}, 90.f, tint);
                    }
                    EndBlendMode();
                    break;
                }
                if (gfx::low) break;  // big additive blobs: pure fill cost
                BeginBlendMode(BLEND_ADDITIVE);
                for (auto& it : L.items) {
                    Vector2 p = v_.toScreen(it.pos);
                    float w = side ? it.size : it.size * 0.6f, h = side ? it.size * 0.6f : it.size;
                    DrawTexturePro(glow_, gsrc, {p.x, p.y, w, h}, {w / 2, h / 2}, 0, it.col);
                }
                EndBlendMode();
                break;
            case LayerType::Planet:
                for (auto& it : L.items) {
                    Vector2 p = v_.toScreen(it.pos);
                    if (!gfx::low) BeginBlendMode(BLEND_ADDITIVE);
                    if (!gfx::low) DrawTexturePro(glow_, gsrc, {p.x, p.y, it.size * 2.1f, it.size * 2.1f}, {it.size * 1.05f, it.size * 1.05f}, 0, Fade(it.col, 0.35f));
                    if (!gfx::low) EndBlendMode();
                    DrawCircleV(p, it.size, it.col);
                    DrawCircleV({p.x + it.size * 0.25f, p.y - it.size * 0.1f}, it.size * 0.95f, Fade(BLACK, 0.45f));
                }
                break;
            case LayerType::Planet3D: case LayerType::Terrain3D: break;  // drawn by Render3D (collect3d)
            case LayerType::Asteroids:
                for (auto& it : L.items) {
                    Vector2 p = v_.toScreen(it.pos);
                    DrawPoly(p, 6, it.size, it.rot, it.col);
                    DrawPoly({p.x - it.size * 0.2f, p.y - it.size * 0.2f}, 5, it.size * 0.5f, -it.rot, Fade(WHITE, 0.15f));
                }
                break;
        }
    }
}

void Background::setTerrainOffset(float px) {
    for (auto& L : layers_) if (L.def.type == LayerType::Terrain3D) L.offset = px;
}

float Background::terrainOffset() const {
    for (const auto& L : layers_) if (L.def.type == LayerType::Terrain3D) return L.offset;
    return 0.f;
}

void Background::collect3d(std::vector<Bg3D>& out) const {
    out.clear();
    for (const auto& L : layers_) {
        if (L.def.type == LayerType::Terrain3D) out.push_back({true, L.def.model, {}, {}, 0, L.offset, 0, 0, L.def.color});
        else if (L.def.type == LayerType::Planet3D)
            for (auto& it : L.items)
                out.push_back({false, L.def.model, L.def.cloudModel, it.pos, it.size, 0, it.rot, it.cloudRot, it.col});
    }
}
