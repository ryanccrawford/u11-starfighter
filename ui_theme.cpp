// U11 Starfighter menu / overlay theme (t231). See ui_theme.hpp.
#include "ui_theme.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ui {
namespace {
struct Fonts { bool tried = false; Font display{}, text{}; };
Fonts& fonts() {
    static Fonts f;
    if (!f.tried) {
        f.tried = true;
        int cps[95]; for (int i = 0; i < 95; i++) cps[i] = 32 + i;
        f.display = LoadFontEx("assets/fonts/Oxanium-variable.ttf", 200, cps, 59);    // space .. Z (banners are upper case)
        f.text = LoadFontEx("assets/fonts/Oxanium-variable.ttf", 64, cps, 95);        // full ASCII
        for (Font* p : {&f.display, &f.text})
            if (p->texture.id > 0) { GenTextureMipmaps(&p->texture); SetTextureFilter(p->texture, TEXTURE_FILTER_TRILINEAR); }
    }
    return f;
}
const Texture2D* gNeb = nullptr; const Texture2D* gGlow = nullptr;

// The title's sky, as the t228 title camera sees it (yaw 34, pitch 28, fovy 40): the same baked nebula
// through the same projection (K 0.4, the title tint) and the same 7000 still stars.
const char* kSkyFs = R"(#version 330
in vec2 fragTexCoord; out vec4 o; uniform sampler2D texture0;
uniform vec3 F; uniform vec3 R; uniform vec3 U; uniform vec2 tanH; uniform float K; uniform vec3 tint;
void main(){
  vec2 s = fragTexCoord * 2.0 - 1.0;
  vec3 d = normalize(F + R * (s.x * tanH.x) - U * (s.y * tanH.y));
  vec2 uv = vec2(atan(d.z, d.x) / 6.2831853 + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.1415927);
  o = vec4(texture(texture0, uv).rgb * tint * K, 1.0); })";
struct Sky { RenderTexture2D rt{}; int w = 0, h = 0; bool withNeb = false; Shader sh{}; bool shTried = false; };
Sky gSky;

void renderSky(int SW, int SH) {
    Sky& k = gSky;
    if (k.rt.id) UnloadRenderTexture(k.rt);
    k.rt = LoadRenderTexture(SW, SH); k.w = SW; k.h = SH;
    SetTextureFilter(k.rt.texture, TEXTURE_FILTER_BILINEAR);
    const float yaw = 34.f * DEG2RAD, pitch = 28.f * DEG2RAD, fovy = 40.f;
    const Vector3 f = Vector3Normalize({-sinf(yaw), sinf(pitch), -cosf(yaw)});
    const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
    const float th = tanf(fovy * 0.5f * DEG2RAD), fp = SH * 0.5f / th;
    BeginTextureMode(k.rt);
    ClearBackground({6, 6, 18, 255});
    if (!k.shTried) { k.shTried = true; k.sh = LoadShaderFromMemory(nullptr, kSkyFs); }
    k.withNeb = gNeb && gNeb->id > 0 && k.sh.id > 0 && k.sh.id != rlGetShaderIdDefault();
    if (k.withNeb) {
        const Vector2 tanH{th * SW / (float)SH, th};
        const float K = 0.40f; const Vector3 tint{165 / 255.f, 160 / 255.f, 235 / 255.f};
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "F"), &f, SHADER_UNIFORM_VEC3);
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "R"), &r, SHADER_UNIFORM_VEC3);
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "U"), &u, SHADER_UNIFORM_VEC3);
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "tanH"), &tanH, SHADER_UNIFORM_VEC2);
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "K"), &K, SHADER_UNIFORM_FLOAT);
        SetShaderValue(k.sh, GetShaderLocation(k.sh, "tint"), &tint, SHADER_UNIFORM_VEC3);
        BeginBlendMode(BLEND_ADDITIVE); BeginShaderMode(k.sh);
        DrawTexturePro(*gNeb, {0, 0, (float)gNeb->width, (float)gNeb->height}, {0, 0, (float)SW, (float)SH}, {0, 0}, 0.f, WHITE);
        EndShaderMode(); EndBlendMode();
    }
    {   // the title's star list (same generator, seed and colour temperatures as Render3D's sky)
        uint32_t h = 2463534242u;
        auto rnd = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xFFFFFF) / 16777216.f; };
        static const Color temp[] = {{170, 195, 255, 255}, {205, 220, 255, 255}, {255, 255, 255, 255}, {255, 244, 220, 255}, {255, 214, 160, 255}, {255, 180, 140, 255}};
        const float cosLim = cosf(fminf(1.5f, fovy * DEG2RAD));
        for (int i = 0; i < 7000; i++) {
            const float z = rnd() * 2.f - 1.f, ph = rnd() * 2.f * PI, rr = sqrtf(1.f - z * z);
            const Vector3 d{rr * cosf(ph), z, rr * sinf(ph)}; const float w = powf(rnd(), 4.f);
            const float tr = rnd(); const Color c = temp[tr < 0.12f ? 0 : tr < 0.30f ? 1 : tr < 0.62f ? 2 : tr < 0.82f ? 3 : tr < 0.94f ? 4 : 5];
            const float zz = Vector3DotProduct(d, f);
            if (zz < cosLim) continue;
            const float px = floorf(SW * 0.5f + Vector3DotProduct(d, r) / zz * fp), py = floorf(SH * 0.5f - Vector3DotProduct(d, u) / zz * fp);
            if (px < 0 || py < 0 || px >= SW || py >= SH) continue;
            const float b = 0.18f + 0.82f * w, sz = w > 0.55f ? 2.f : 1.f;
            DrawRectangleV({px, py}, {sz, sz}, Fade(c, b));
        }
    }
    if (gGlow && gGlow->id > 0) {   // the title's sunrise, far off in the upper right: a faint warm glow only
        const Rectangle gs{0, 0, (float)gGlow->width, (float)gGlow->height};
        const float X = SW * 0.80f, Y = SH * 0.20f, b = SH * 1.1f;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(*gGlow, gs, {X, Y, b, b}, {b / 2, b / 2}, 0, Fade({255, 150, 80, 255}, 0.10f));
        DrawTexturePro(*gGlow, gs, {X, Y, b * 0.35f, b * 0.35f}, {b * 0.175f, b * 0.175f}, 0, Fade({255, 220, 180, 255}, 0.10f));
        EndBlendMode();
    }
    DrawRectangleGradientV(0, 0, SW, (int)(SH * 0.18f), Fade(BLACK, 0.35f), Fade(BLACK, 0.f));          // gentle vignette
    DrawRectangleGradientV(0, (int)(SH * 0.80f), SW, SH - (int)(SH * 0.80f), Fade(BLACK, 0.f), Fade(BLACK, 0.40f));
    EndTextureMode();
}
}  // namespace

static const Font& fallbackFont() { static Font d{}; if (d.texture.id == 0) d = GetFontDefault(); return d; }
const Font& display() { Fonts& f = fonts(); return f.display.texture.id > 0 ? f.display : f.text.texture.id > 0 ? f.text : fallbackFont(); }
const Font& text() { Fonts& f = fonts(); return f.text.texture.id > 0 ? f.text : fallbackFont(); }

void setSky(const Texture2D* nebula, const Texture2D* glow) {
    if (nebula && nebula->id > 0) gNeb = nebula;
    if (glow && glow->id > 0) gGlow = glow;
}

void backdrop(float alpha) {
    if (alpha <= 0.f) return;
    const int SW = GetScreenWidth(), SH = GetScreenHeight();
    const bool nebNow = gNeb && gNeb->id > 0;
    if (!gSky.rt.id || gSky.w != SW || gSky.h != SH || (nebNow && !gSky.withNeb)) {
        // (re)render off screen: keep the caller's current target / scaling intact
        rlDrawRenderBatchActive();
        renderSky(SW, SH);
    }
    if (!gSky.rt.id) { DrawRectangle(0, 0, SW, SH, Fade({6, 6, 18, 255}, alpha)); return; }
    DrawTexturePro(gSky.rt.texture, {0, 0, (float)gSky.w, -(float)gSky.h}, {0, 0, (float)SW, (float)SH}, {0, 0}, 0, Fade(WHITE, alpha));
}

void bold(const Font& f, const char* t, Vector2 p, float size, float spacing, Color c, float weight) {
    for (int i = 0; i <= 3; i++) DrawTextEx(f, t, {p.x + weight * i / 3.f, p.y}, size, spacing, c);
}
Vector2 measure(const Font& f, const char* t, float size, float spacing) { return MeasureTextEx(f, t, size, spacing); }

void banner(const char* t, float cx, float y, float size, float s, float a, float time, bool fx, Color tint) {
    const Font& F = display(); const float sp = size * 0.08f, w = size * 0.031f;
    const Vector2 m = MeasureTextEx(F, t, size, sp); const float x = cx - m.x * 0.5f;
    bold(F, t, {x + 4 * s, y + 5 * s}, size, sp, Fade(BLACK, 0.4f * a), w);
    if (!fx) {   // inside the scaled gameplay target: a light top bevel instead of the scissored sky-lit half
        bold(F, t, {x, y - 1.5f * s}, size, sp, Fade({226, 236, 248, 255}, a), w);
        bold(F, t, {x, y + 1.0f * s}, size, sp, Fade(tint, a), w);
        return;
    }
    bold(F, t, {x, y}, size, sp, Fade(tint, a), w);
    const Color hi{(unsigned char)std::min(255, tint.r + 76), (unsigned char)std::min(255, tint.g + 72), (unsigned char)std::min(255, tint.b + 64), 255};
    BeginScissorMode((int)x, (int)y, (int)(m.x + 8 * s), (int)(m.y * 0.47f));
    bold(F, t, {x, y}, size, sp, Fade(hi, a), w);
    EndScissorMode();
    const float gk = fmodf(time * 0.11f, 1.f), gx = x - m.x * 0.3f + gk * m.x * 1.6f;   // the sun glint
    BeginScissorMode((int)gx, (int)y, (int)(m.x * 0.12f), (int)m.y);
    BeginBlendMode(BLEND_ADDITIVE);
    bold(F, t, {x, y}, size, sp, Fade({255, 200, 140, 255}, 0.3f * a), w);
    EndBlendMode();
    EndScissorMode();
}

void heading(const char* t, float cx, float y, float size, float s, float a) {
    const Font& F = display(); const float sp = size * 0.36f;
    const Vector2 m = MeasureTextEx(F, t, size, sp); const float x = cx - m.x * 0.5f;
    bold(F, t, {x + 3 * s, y + 3 * s}, size, sp, Fade(BLACK, 0.5f * a), size * 0.037f);
    bold(F, t, {x, y}, size, sp, Fade(kAmber, a), size * 0.037f);
    const float ry = y + m.y + 6 * s;   // amber rule, brightest at the centre
    DrawRectangleGradientH((int)(cx - m.x * 0.55f), (int)ry, (int)(m.x * 0.55f), (int)(2 * s), Fade(kAmber, 0.f), Fade(kAmber, 0.85f * a));
    DrawRectangleGradientH((int)cx, (int)ry, (int)(m.x * 0.55f), (int)(2 * s), Fade(kAmber, 0.85f * a), Fade(kAmber, 0.f));
}

void caption(const char* t, float cx, float y, float size, float s, Color c, float a) {
    const Font& F = text(); const float sp = size * 0.12f;
    const Vector2 m = MeasureTextEx(F, t, size, sp);
    DrawTextEx(F, t, {cx - m.x * 0.5f + 1.5f * s, y + 1.5f * s}, size, sp, Fade(BLACK, 0.5f * a));
    DrawTextEx(F, t, {cx - m.x * 0.5f, y}, size, sp, Fade(c, a));
}
void captionL(const char* t, float x, float y, float size, float s, Color c, float a) {
    const Font& F = text(); const float sp = size * 0.12f;
    DrawTextEx(F, t, {x + 1.5f * s, y + 1.5f * s}, size, sp, Fade(BLACK, 0.5f * a));
    DrawTextEx(F, t, {x, y}, size, sp, Fade(c, a));
    DrawTextEx(F, t, {x + 0.6f * s, y}, size, sp, Fade(c, a));   // a touch of weight for body text
}

void dim(float W, float H, float a) {
    DrawRectangle(0, 0, (int)W, (int)H, Fade({4, 8, 22, 255}, a));
    DrawRectangleGradientV(0, 0, (int)W, (int)(H * 0.3f), Fade({2, 4, 12, 255}, a * 0.5f), Fade({2, 4, 12, 255}, 0.f));
    DrawRectangleGradientV(0, (int)(H * 0.7f), (int)W, (int)(H * 0.3f) + 1, Fade({2, 4, 12, 255}, 0.f), Fade({2, 4, 12, 255}, a * 0.5f));
}
void panel(Rectangle r, float s, float a) {
    DrawRectangleRounded(r, 0.10f, 8, Fade({5, 14, 30, 255}, 0.82f * a));
    DrawRectangleRoundedLinesEx(r, 0.10f, 8, 1.4f * s, Fade(SKYBLUE, 0.55f * a));
}

void focusRing(Rectangle r, float s) {
    DrawRectangleRoundedLinesEx({r.x - 3 * s, r.y - 3 * s, r.width + 6 * s, r.height + 6 * s}, 0.18f, 8, 1.6f * s,
                                Fade(GOLD, 0.55f + 0.25f * sinf((float)GetTime() * 4.f)));
}
bool backPressed() { return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE); }
bool confirmPressed() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER); }

void Nav::begin(bool keys, bool space) {
    activate = -1;
    const int n = (int)prev.size();
    auto ok = [&](int i) { return i >= 0 && i < n && prevEn[i]; };
    if (!ok(focus)) { focus = -1; if (ok(preferred)) focus = preferred; else for (int i = 0; i < n; i++) if (ok(i)) { focus = i; break; } }
    const Vector2 md = GetMouseDelta();
    if (md.x != 0.f || md.y != 0.f)   // mouse / touch: the focus follows the button under the pointer
        for (int i = 0; i < n; i++) if (ok(i) && CheckCollisionPointRec(GetMousePosition(), prev[i])) focus = i;
    if (!keys || focus < 0) return;
    Vector2 dir{0, 0};
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) dir = {0, 1};
    else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) dir = {0, -1};
    else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) dir = {1, 0};
    else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) dir = {-1, 0};
    if (dir.x != 0.f || dir.y != 0.f) {
        auto ctr = [](Rectangle r) { return Vector2{r.x + r.width * 0.5f, r.y + r.height * 0.5f}; };
        const Vector2 c = ctr(prev[focus]);
        int best = -1, wrap = -1; float bs = 1e9f, ws = 1e9f;
        for (int i = 0; i < n; i++) {
            if (i == focus || !ok(i)) continue;
            const Vector2 d = Vector2Subtract(ctr(prev[i]), c);
            const float along = d.x * dir.x + d.y * dir.y, perp = fabsf(d.x * dir.y - d.y * dir.x);
            if (along > 1.f) { const float sc = along + perp * 2.5f; if (sc < bs) { bs = sc; best = i; } }
            else if (dir.y != 0.f) { const float sc = along + perp * 2.5f; if (sc < ws) { ws = sc; wrap = i; } }   // up / down wrap around
        }
        if (best >= 0) focus = best; else if (wrap >= 0) focus = wrap;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || (space && IsKeyPressed(KEY_SPACE))) activate = focus;
}
bool Nav::item(Rectangle r, bool enabled, float s) {
    const int i = (int)cur.size();
    cur.push_back(r); curEn.push_back(enabled);
    if (i == focus && enabled) focusRing(r, s);
    return enabled && i == activate;
}
void Nav::end() { prev.swap(cur); prevEn.swap(curEn); cur.clear(); curEn.clear(); }
}  // namespace ui
