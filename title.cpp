// U11 Starfighter title screen (t228). Kept in its own file: the only hooks elsewhere are
// Game::drawTitle() (declared in game.hpp) and the title-only branch of Render3D::drawApproachScenery.
//
// The scene (Ryan's idea): his exoplanet as a dark horizon arc lower right, the sun rising over its
// limb (the ONE light: planet, atmosphere and his fighter are lit by it), a light nebula and still
// far stars, his red fighter gliding toward the sunrise, the U11 / STARFIGHTER wordmark and the menu
// on the left. Slow cinematic camera drift. Never black: if the 3D scene cannot draw, his 2D title
// art (assets/title/title_space.png) is the backdrop.
#include "game.hpp"
#include "ui_theme.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {
float envf(const char* n, float d) { const char* e = getenv(n); return e ? (float)atof(e) : d; }   // capture / tuning hooks
// Oxanium-variable loads at its light default weight: a few sub-pixel copies give the wordmark body
void boldText(const Font& f, const char* t, Vector2 p, float sz, float sp, Color c, float w) {
    for (int i = 0; i <= 3; i++) DrawTextEx(f, t, {p.x + w * i / 3.f, p.y}, sz, sp, c);
}
float sstep(float a, float b, float x) { x = Clamp((x - a) / (b - a), 0.f, 1.f); return x * x * (3.f - 2.f * x); }

struct TitleRes {
    bool tried = false;
    Font big{};          // Oxanium at display size (the 96 px menu font blurs when scaled to the wordmark)
    Texture2D art{};     // Ryan's title art: fallback backdrop only
};
TitleRes& res() {
    static TitleRes r;
    if (!r.tried) {
        r.tried = true;
        r.big = ui::display();   // t231: one Oxanium display font for the title and every other screen
        r.art = LoadTexture("assets/title/title_space.png");
        if (r.art.id > 0) SetTextureFilter(r.art, TEXTURE_FILTER_BILINEAR);
    }
    return r;
}

int gArtFrames = 0;       // first title frames show the 2D art while the 4k planet loads (no black window)
float gScene3dT = 0.f;    // seconds since the 3D scene first drew: the art cross-fades out over it
int gFocus = -1;          // keyboard focus (-1: not yet chosen)
float gLaunch = -1.f;     // > = 0: START / CONTINUE pressed, the push-in to the sunrise is playing
int gLaunchStep = 0;
}  // namespace

void Game::drawTitle() {
    TitleRes& R = res();
    ui::setSky(&r3d.nebulaTexture(), &fx.glowTexture());
    const float t = time, rv = titleRevealTime, dt = GetFrameTime();
    const int SW = GetScreenWidth(), SH = GetScreenHeight();
    const Font& big = R.big.texture.id > 0 ? R.big : (titleFont.texture.id > 0 ? titleFont : GetFontDefault());
    const Font* menuFont = titleFont.texture.id > 0 ? &titleFont : nullptr;
    if (rv < 0.05f && gLaunch >= 0.f) gLaunch = -1.f;   // back on the title after a game: no stale launch

    // ---- 3D scene: one camera on the planet, slow drift; the sun just over the limb ----
    const float launchK = gLaunch >= 0.f ? sstep(0.f, 0.7f, gLaunch) : 0.f;
    const float yaw = envf("TITLE_YAW", 34.f) + 1.1f * sinf(t * 0.045f) - 4.f * launchK;
    const float pitch = envf("TITLE_PITCH", 28.f) + 0.7f * sinf(t * 0.033f + 1.f);
    const float dist = envf("TITLE_DIST", 1.75f) + 0.025f * sinf(t * 0.027f) - 0.10f * launchK;
    Render3D::ApproachScene sc;
    sc.f = Vector3Normalize({-sinf(yaw * DEG2RAD), sinf(pitch * DEG2RAD), -cosf(yaw * DEG2RAD)});
    sc.u = {0, 1, 0};
    sc.pdir = {0, 0, -1};          // camera at (0, 0, dist): the planet centre is straight ahead of it
    {   // the sun: just beyond the limb, on the upper side of the arc (base framing, so it holds still in the sky)
        const Vector3 pc{0, 0, -1};
        const float y0 = 34.f * DEG2RAD, p0 = 28.f * DEG2RAD;
        const Vector3 f0 = Vector3Normalize({-sinf(y0), sinf(p0), -cosf(y0)});
        const Vector3 r0 = Vector3Normalize(Vector3CrossProduct(f0, {0, 1, 0})), u0 = Vector3CrossProduct(r0, f0);
        const float ang = envf("TITLE_SUNANG", 118.f) * DEG2RAD, sep = envf("TITLE_SUNSEP", 35.9f) * DEG2RAD;
        Vector3 perp = Vector3Add(Vector3Scale(r0, cosf(ang)), Vector3Scale(u0, sinf(ang)));
        perp = Vector3Normalize(Vector3Subtract(perp, Vector3Scale(pc, Vector3DotProduct(perp, pc))));
        sc.sun = Vector3Normalize(Vector3Add(Vector3Scale(pc, cosf(sep)), Vector3Scale(perp, sinf(sep))));
    }
    sc.fovy = 40.f;
    sc.dist = dist;
    sc.spin = t * 0.0035f;
    sc.stars = 1.f; sc.starSpeed = 0.f; sc.separateStarFrame = false;   // far stars: still, at infinity
    sc.nebula = envf("TITLE_NEB", 0.40f);                                  // light, not dense
    sc.nebulaTint = {165, 160, 235, 255};
    sc.sunR = envf("TITLE_SUNR", 0.018f);
    sc.time = t;
    sc.titleRevealTime = rv;
    sc.cloudA = 0.f;
    sc.atmoK = 1.f;
    sc.titleScene = true;
    sc.titleAtmo = true; sc.titleShell = false; sc.titleGlyphs = false;
    {   // Ryan's fighter: gliding out toward the sunrise, slow bank and bob (camera space)
        const float bob = sinf(t * 0.42f), sway = sinf(t * 0.23f);
        sc.titleShip = {envf("TITLE_SX", 0.63f) + 0.012f * sway + 0.10f * launchK, envf("TITLE_SY", 0.62f) + 0.010f * bob - 0.10f * launchK,
                        envf("TITLE_SD", 0.30f) + 0.25f * launchK};
        sc.titleShipHead = {envf("TITLE_HX", 0.8f), envf("TITLE_HY", 0.3f), envf("TITLE_HZ", 0.5f)};
        sc.titleShipRoll = envf("TITLE_ROLL", 48.f) * DEG2RAD + 0.05f * sway;
        sc.titleShipW = envf("TITLE_SW", 0.36f);
        sc.titleShipAmb = envf("TITLE_AMB", 0.30f);
    }
    if (rv < 0.05f) { gArtFrames = 0; gScene3dT = 0.f; }   // (re)entering the title: the title assets reload
    Render3D::ApproachOut so;
    const bool artFirst = gArtFrames++ < 2;   // present the art before the (long) first 3D load
    if (r3d.ok() && !artFirst) r3d.drawApproachScenery(sc, so);
    const bool scene3d = so.radiusPx > 0.f;   // drawApproachScenery bails out (no planet model) before it sets this
    gScene3dT = scene3d ? gScene3dT + fminf(dt, 0.05f) : 0.f;
    const float artA = scene3d ? 1.f - sstep(0.f, 0.9f, gScene3dT) : 1.f;
    if (artA > 0.f) {   // never black: Ryan's 2D title art (fallback, and the cross-fade into the 3D scene)
        if (R.art.id > 0) {
            const float k = fmaxf(SW / (float)R.art.width, SH / (float)R.art.height);
            DrawTexturePro(R.art, {0, 0, (float)R.art.width, (float)R.art.height},
                           {SW * 0.5f, SH * 0.5f, R.art.width * k, R.art.height * k}, {R.art.width * k * 0.5f, R.art.height * k * 0.5f}, 0, Fade(WHITE, artA));
        } else if (!scene3d) titleBg.draw();
    }

    // ---- 2D light on top of the 3D: engine flames + heat mirage, sun bloom, grade ----
    const Texture2D& glow = fx.glowTexture(); const Rectangle gs{0, 0, (float)glow.width, (float)glow.height};
    if (scene3d && so.titleShipPx > 1.f) r3d.drawHeatAndFlames();
    BeginBlendMode(BLEND_ADDITIVE);
    if (scene3d && so.sunOn) {   // sunrise bloom on the limb + a soft anamorphic streak (all from the one sun)
        const Vector2 P = so.sunDiscPx; const float R0 = fmaxf(3.f, so.sunRadPx), v = 0.35f + 0.65f * so.sunVis;
        const float b1 = R0 * 10.f, b2 = R0 * 26.f;
        DrawTexturePro(glow, gs, {P.x, P.y, b2, b2}, {b2 / 2, b2 / 2}, 0, Fade({255, 150, 70, 255}, 0.16f * v));
        DrawTexturePro(glow, gs, {P.x, P.y, b1, b1}, {b1 / 2, b1 / 2}, 0, Fade({255, 240, 215, 255}, 0.45f * v));
        const float sl = SW * 0.75f, sh = R0 * 1.4f;
        DrawTexturePro(glow, gs, {P.x, P.y, sl, sh}, {sl / 2, sh / 2}, 0, Fade({255, 200, 150, 255}, 0.22f * v));
        DrawTexturePro(glow, gs, {P.x, P.y, sl * 0.45f, sh * 0.6f}, {sl * 0.225f, sh * 0.3f}, 0, Fade(WHITE, 0.25f * v));
        for (int k = 0; k < 6; k++) {   // thin lens rays, turning very slowly
            const float ang = 18.f + k * 60.f + t * 1.5f, rl = R0 * (k % 2 ? 9.f : 15.f);
            DrawTexturePro(glow, gs, {P.x, P.y, rl, R0 * 0.5f}, {rl / 2, R0 * 0.25f}, ang, Fade({255, 236, 205, 255}, 0.35f * v));
        }
        DrawTexturePro(glow, gs, {P.x, P.y, R0 * 4.f, R0 * 4.f}, {R0 * 2.f, R0 * 2.f}, 0, Fade(WHITE, 0.8f * v));   // white-hot core
    }
    EndBlendMode();
    {   // cinematic grade: soft dark left column for the menu, gentle vignette top/bottom
        DrawRectangleGradientH(0, 0, (int)(SW * 0.55f), SH, Fade({2, 4, 12, 255}, 0.62f), Fade({2, 4, 12, 255}, 0.f));
        DrawRectangleGradientV(0, 0, SW, (int)(SH * 0.16f), Fade(BLACK, 0.45f), Fade(BLACK, 0.f));
        DrawRectangleGradientV(0, (int)(SH * 0.82f), SW, SH - (int)(SH * 0.82f), Fade(BLACK, 0.f), Fade(BLACK, 0.5f));
    }

    // ---- wordmark: Oxanium, lit metal, warm sun glint sweeping across ----
    const float logoA = sstep(0.15f, 1.4f, rv);
    const float lx = W * 0.075f, ly = H * 0.085f;
    {
        const float f1 = 128.f * s, sp1 = 10.f * s, f2 = 40.f * s, sp2 = 15.f * s;
        const char* l1 = "U11"; const char* l2 = "STARFIGHTER";
        const Vector2 m1 = MeasureTextEx(big, l1, f1, sp1), m2 = MeasureTextEx(big, l2, f2, sp2);
        const float y2 = ly + m1.y * 0.86f;
        const float rise = (1.f - logoA) * 14.f * s;
        boldText(big, l1, {lx + 4 * s, ly + 5 * s + rise}, f1, sp1, Fade({0, 0, 0, 255}, 0.4f * logoA), 4 * s);
        boldText(big, l1, {lx, ly + rise}, f1, sp1, Fade({150, 164, 184, 255}, logoA), 4 * s);                 // brushed steel
        BeginScissorMode((int)lx, (int)(ly + rise), (int)(m1.x + 8 * s), (int)(m1.y * 0.47f));             // sky-lit upper half
        boldText(big, l1, {lx, ly + rise}, f1, sp1, Fade({226, 236, 248, 255}, logoA), 4 * s);
        EndScissorMode();
        {   // the sun glint: a narrow bright band that travels across the letters every ~9 s
            const float gk = fmodf(t * 0.11f, 1.f), gx = lx - m1.x * 0.3f + gk * m1.x * 1.6f;
            BeginScissorMode((int)gx, (int)(ly + rise), (int)(m1.x * 0.12f), (int)m1.y);
            BeginBlendMode(BLEND_ADDITIVE);
            boldText(big, l1, {lx, ly + rise}, f1, sp1, Fade({255, 200, 140, 255}, 0.3f * logoA), 4 * s);
            EndBlendMode();
            EndScissorMode();
        }
        boldText(big, l2, {lx + 3 * s, y2 + 3 * s + rise}, f2, sp2, Fade(BLACK, 0.5f * logoA), 1.5f * s);
        boldText(big, l2, {lx, y2 + rise}, f2, sp2, Fade({255, 172, 72, 255}, logoA), 1.5f * s);                   // sunrise amber
        const float ry = y2 + m2.y + 10 * s + rise;
        DrawRectangleGradientH((int)lx, (int)ry, (int)m2.x, (int)(2 * s), Fade({255, 172, 72, 255}, 0.85f * logoA), Fade({255, 172, 72, 255}, 0.f));
    }

    // ---- menu (touch / mouse via button(), keyboard: up/down + enter) ----
    const float menuA = sstep(0.6f, 1.6f, rv);
    const float bw = std::min(340.f * s, W * 0.34f), bh = 50.f * s;
    const float bx = lx - (1.f - menuA) * 30.f * s, y = H * 0.47f, gap = bh + 9.f * s;
    const bool canContinue = savedStep > 0;
    const Rectangle rects[5] = {{bx, y, bw, bh}, {bx, y + gap, bw, bh}, {bx, y + 2 * gap, bw * 0.485f, bh},
                                {bx + bw * 0.515f, y + 2 * gap, bw * 0.485f, bh}, {bx, y + 3 * gap, bw, bh * 0.84f}};
    const char* labels[5] = {"START MISSION", "CONTINUE", "LEVELS", "OPTIONS", "QUIT"};
    const bool enabled[5] = {true, canContinue, true, true, true};
    if (gFocus < 0 || !enabled[gFocus]) gFocus = canContinue ? 1 : 0;
    int act = -1;
    const bool live = gLaunch < 0.f && menuA > 0.5f;
    if (live) {
        auto move = [&](int d) { int f = gFocus; for (int i = 0; i < 5; i++) { f = (f + d + 5) % 5; if (enabled[f]) break; } gFocus = f; };
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) move(gFocus == 2 ? 2 : 1);
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) move(gFocus == 4 ? -2 : (gFocus == 3 ? -2 : -1));
        if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) && gFocus == 2) gFocus = 3;
        if ((IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) && gFocus == 3) gFocus = 2;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) act = gFocus;
    }
    for (int i = 0; i < 5 && menuA > 0.02f; i++) {   // (t231: not drawn before the fade-in starts)
        if (button(rects[i], labels[i], enabled[i], menuFont) && live) { act = i; gFocus = i; }
        if (i == gFocus && live) {   // keyboard focus ring (same gold as the hover state)
            const Rectangle r = rects[i];
            ui::focusRing(r, s);
        }
    }
    if (menuA > 0.02f && menuA < 0.999f) DrawRectangle((int)(bx - 6 * s), (int)(y - 6 * s), (int)(bw + 12 * s), (int)(4 * gap + 12 * s), Fade({2, 4, 12, 255}, 1.f - menuA));   // menu fades in over the scene
    if (const char* e = getenv("TITLE_AUTOSTART"); e && gLaunch < 0.f && rv > (float)atof(e)) act = savedStep > 0 && getenv("TITLE_CONTINUE") ? 1 : 0;   // smoke-test hook
    if (act >= 0) audio.play(Sfx::Pickup, 0.32f);
    switch (act) {
        case 0: gLaunch = 0.f; gLaunchStep = 0; break;
        case 1: gLaunch = 0.f; gLaunchStep = savedStep; break;
        case 2: state = State::LevelSelect; break;
        case 3: state = State::Options; break;
        case 4: quit = true; break;
        default: break;
    }
    {
        const char* high = TextFormat("HIGH SCORE  %d", highScore);
        DrawTextEx(menuFont ? *menuFont : big, high, {lx, H - 30.f * s}, 15.f * s, 2.f * s, Fade(GOLD, 0.85f * menuA));
    }

    // ---- launch: push in toward the sunrise, the screen fills with warm light (never black), then the game ----
    if (gLaunch >= 0.f) {
        gLaunch += fminf(dt, 0.05f);
        const float w = sstep(0.1f, 0.45f, gLaunch) * (1.f - sstep(0.5f, 0.8f, gLaunch));
        DrawRectangle(0, 0, SW, SH, Fade({255, 236, 210, 255}, 0.75f * w));
        ui::backdrop(sstep(0.45f, 0.8f, gLaunch));   // t231: settle on the shared star field (the intro opens on it)
        if (gLaunch >= 0.8f) { const int st = gLaunchStep; gLaunch = -1.f; newCampaign(st); }
    }
}

// t231: the title theme starts once the scene's first (long) load is behind it, so the stream never stalls
bool ui::titleMusicReady(float titleRevealTime) { return titleRevealTime > 0.05f && gArtFrames >= 3; }
