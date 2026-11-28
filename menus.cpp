// U11 Starfighter menus and overlays (t231): level select, options, development, the in-level pause
// menu, LEVEL CLEAR / GAME OVER and the campaign-complete screen, in the t228 title's look (ui_theme).
// Moved here from game.cpp with the same layout, buttons and flow; what is new is the theme (Oxanium
// type, the title's still stars + light nebula, title-style buttons) and keyboard focus on every button.
#include "player.hpp"
#include "rlgl.h"
#include "game.hpp"
#include "config.hpp"
#include "ui_theme.hpp"
#if defined(_WIN32)
#include "pc_display.hpp"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>

void Game::drawPauseMenu() {
    if (!paused) {  // small touch pause button (top centre); ESC / P / gamepad START also pause
        const float bw = 46 * s, bh = 34 * s;
        if (button({W / 2 - bw / 2, 8 * s, bw, bh}, "II", true, &titleFont)) paused = true;
        return;
    }
    static ui::Nav nav;
    nav.begin(true, false);   // Space stays the fire key: Enter presses the focused button
    ui::dim(W, H, 0.62f);
    ui::banner("PAUSED", W / 2, H * 0.20f, 64 * s, s, 1.f, time, false);
    const float bw = 360 * s, bh = 58 * s, bx = W / 2 - bw / 2, y = H * 0.38f, gap = bh + 12 * s;
    auto b = [&](Rectangle r, const char* label) { const bool hit = button(r, label, true, &titleFont); return nav.item(r, true, s) || hit; };
    if (b({bx, y, bw, bh}, "RESUME")) paused = false;
    if (b({bx, y + gap, bw, bh}, "EXIT TO MAIN MENU")) exitToMenu();
    if (b({bx, y + 2 * gap, bw, bh * 0.8f}, "QUIT GAME")) quit = true;
    ui::caption("ESC to resume", W / 2, y + 3 * gap, 18 * s, s, ui::kHint);
    nav.end();
}

void Game::drawLevelSelect() {
    ui::setSky(&r3d.nebulaTexture(), &fx.glowTexture());
    ui::backdrop();
    static ui::Nav nav;
    nav.begin();
    ui::heading("SELECT LEVEL", W / 2, 22 * s, 42 * s, s);
    const auto& lv = db.levels();
    int cols = 5;
    float bw = (W - 80 * s) / cols - 12 * s, bh = 110 * s;
    const std::vector<int> order = levelOrder();
    for (int k = 0; k < (int)order.size(); k++) {
        const int i = order[k];
        float x = 40 * s + (k % cols) * (bw + 12 * s), y = 90 * s + (k / cols) * (bh + 12 * s);
        bool open = k < unlocked || getenv("UNLOCK_ALL");
        const char* label = TextFormat("%d %s", k + 1, lv[i].mode == Mode::TopDown ? "^" : ">");
        const Rectangle r{x, y, bw, bh};
        const bool hit = button(r, label, open, &titleFont);
        const bool key = nav.item(r, open, s);
        ui::captionL(lv[i].name.c_str(), x + 10 * s, y + bh - 24 * s, 14 * s, s, open ? ui::kCaption : Color{95, 100, 115, 255});
        if (hit || key) { nav.end(); startLevel(i, true); return; }
    }
    const Rectangle back{W / 2 - 100 * s, H - 80 * s, 200 * s, 56 * s};
    const bool hit = button(back, "BACK", true, &titleFont);
    if (nav.item(back, true, s) || hit || ui::backPressed()) state = State::Title;
    ui::captionL("> side-scroll   ^ top-down     R = reload data", 12 * s, H - 24 * s, 14 * s, s, ui::kHint);
    nav.end();
}

void Game::drawOptions() {
    ui::setSky(&r3d.nebulaTexture(), &fx.glowTexture());
    ui::backdrop();
    static ui::Nav nav;
    nav.begin();
    ui::heading("OPTIONS", W / 2, 26 * s, 42 * s, s);
    float bw = 420 * s, bh = 52 * s, bx = W / 2 - bw / 2;
#if defined(_WIN32)
    float y = H * 0.07f;
    const float gap = 50 * s;
#else
    float y = H * 0.12f;
    const float gap = 64 * s;
#endif
    auto b = [&](Rectangle r, const char* label) { const bool hit = button(r, label, true, &titleFont); return nav.item(r, true, s) || hit; };
    int row = 0;
    auto yAt = [&]() { return y + (float)row * gap; };
    auto nextY = [&]() { const float yy = yAt(); row++; return yy; };
    if (b({bx, nextY(), bw, bh}, showFps ? "FPS DISPLAY: ON" : "FPS DISPLAY: OFF")) { showFps = !showFps; writeSave(); }
    const float yVol = nextY();
    if (b({bx, yVol, bh, bh}, "-")) { volume = std::max(0.f, volume - 0.1f); SetMasterVolume(volume); writeSave(); audio.play(Sfx::Pickup); }
    ui::caption(TextFormat("VOLUME %d%%", (int)std::lround(volume * 100)), W / 2, yVol + 12 * s, 26 * s, s, RAYWHITE);
    if (b({bx + bw - bh, yVol, bh, bh}, "+")) { volume = std::min(1.f, volume + 0.1f); SetMasterVolume(volume); writeSave(); audio.play(Sfx::Pickup); }
    if (b({bx, nextY(), bw, bh}, autoFire ? "PRIMARY FIRE: AUTO" : "PRIMARY FIRE: MANUAL")) { autoFire = !autoFire; writeSave(); }
    if (b({bx, nextY(), bw, bh}, TextFormat("GRAPHICS: %s", gfxName()))) {
        gfxMode = (gfxMode + 1) % 3; autoStep = 0; applyQuality(); writeSave();
    }
    // t260: invert up/down (trench run pitch; X flips it in-game). OPTIONS_TOGGLE / OPTIONS_SHOT: hidden test hooks
    static int optFrames = 0; optFrames++;
    const bool testToggle = getenv("OPTIONS_TOGGLE") && optFrames == 20;
    if (b({bx, nextY(), bw, bh}, g_invertPitch ? "UP/DOWN: INVERTED" : "UP/DOWN: NORMAL") || testToggle) {
        g_invertPitch = !g_invertPitch; writeSave();
        fprintf(stderr, "INVERT toggled in Options -> %d (saved)\n", g_invertPitch ? 1 : 0);
    }
#if defined(_WIN32)
    {
        const int req = pcDisplayRequestedAA(), eff = pcDisplayEffectiveAA();
        const char* aaLabel = (req <= 0) ? "ANTI-ALIASING: OFF" :
            (req > eff) ? TextFormat("ANTI-ALIASING: %dX (MAX)", eff) :
            TextFormat("ANTI-ALIASING: %dX", req);
        if (b({bx, nextY(), bw, bh}, aaLabel)) {
            static const int kAA[] = {0, 2, 4, 8, 16};
            int i = 0; for (int n = 0; n < 5; n++) if (kAA[n] == req) { i = n; break; }
            pcDisplaySetAA(kAA[(i + 1) % 5]);
            writeSave();
        }
        if (b({bx, nextY(), bw, bh}, pcDisplayVsync() ? "VSYNC: ON" : "VSYNC: OFF")) {
            pcDisplaySetVsync(!pcDisplayVsync());
            pcDisplayApplyTiming();
            writeSave();
        }
        const int cap = pcDisplayFpsLimit(), hz = pcDisplayMonitorRefresh();
        const bool vs = pcDisplayVsync();
        const char* capTxt = cap <= 0 ? "UNLIMITED" : TextFormat("%d", cap);
        const char* fpsLabel = (vs && hz > 0 && (cap <= 0 || cap > hz))
            ? TextFormat("FPS LIMIT: %s (%d VSYNC)", capTxt, hz)
            : TextFormat("FPS LIMIT: %s", capTxt);
        if (b({bx, nextY(), bw, bh}, fpsLabel)) {
            static const int kFps[] = {30, 60, 75, 90, 120, 144, 0};
            int i = 0; for (int n = 0; n < 7; n++) if (kFps[n] == cap) { i = n; break; }
            pcDisplaySetFpsLimit(kFps[(i + 1) % 7]);
            pcDisplayApplyTiming();
            writeSave();
        }
    }
#endif
    if (b({bx, nextY(), bw, bh}, "DEVELOPMENT")) { developmentPage = 0; state = State::Development; }
    if (b({bx, nextY(), bw, bh}, "BACK") || ui::backPressed()) state = State::Title;
    nav.end();
    if (const char* dir = getenv("OPTIONS_SHOT")) {
        if (optFrames == 10) fprintf(stderr, "OPTIONS invert at open = %d\n", g_invertPitch ? 1 : 0);
        if (optFrames == 40) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen(); ExportImage(img, TextFormat("%s/options_%s.png", dir, g_invertPitch ? "inverted" : "normal")); UnloadImage(img);
            fprintf(stderr, "OPTIONS shot invert=%d\n", g_invertPitch ? 1 : 0);
        }
        if (optFrames == 45) { CloseWindow(); exit(0); }
    }
}

void Game::drawDevelopment() {
    ui::setSky(&r3d.nebulaTexture(), &fx.glowTexture());
    ui::backdrop();
    static ui::Nav nav;
    nav.begin();
    ui::heading("DEVELOPMENT", W / 2, 14 * s, 34 * s, s);
    ui::caption("Runtime only - resets when the game closes", W / 2, 58 * s, 14 * s, s, ui::kHint);
    const char* pageLabel = developmentPage == 0 ? "COMBAT" : "PLAYER / AUDIO";
    ui::caption(pageLabel, W / 2, 74 * s, 15 * s, s, SKYBLUE);
    const float bw = 430 * s, bh = 44 * s, bx = W / 2 - bw / 2, y = 88 * s;
    auto b = [&](Rectangle r, const char* label) { const bool hit = button(r, label, true, &titleFont); return nav.item(r, true, s) || hit; };
    auto row = [&](int n, const char* label, const char* value, auto dec, auto inc) {
        const float yy = y + n * 78 * s;
        ui::captionL(label, bx, yy + 12 * s, 17 * s, s, RAYWHITE);
        if (b({bx + bw - 142 * s, yy, 42 * s, bh}, "-")) dec();
        ui::captionL(value, bx + bw - 92 * s, yy + 13 * s, 16 * s, s, SKYBLUE);
        if (b({bx + bw - 42 * s, yy, 42 * s, bh}, "+")) inc();
    };
    const int first = developmentPage == 0 ? 0 : 3;
    if (first == 0) row(0, "LASER COOLDOWN", TextFormat("%.2fs", cfg::dev.weaponCooldown),
        [] { cfg::dev.weaponCooldown = std::max(0.02f, cfg::dev.weaponCooldown - 0.02f); },
        [] { cfg::dev.weaponCooldown = std::min(2.f, cfg::dev.weaponCooldown + 0.02f); });
    if (first == 0) row(1, "LASER DAMAGE", TextFormat("%d", cfg::dev.laserDamage),
        [] { cfg::dev.laserDamage = std::max(1, cfg::dev.laserDamage - 1); },
        [] { cfg::dev.laserDamage = std::min(999, cfg::dev.laserDamage + 1); });
    if (first == 0) row(2, "MISSILE DAMAGE", TextFormat("%d", cfg::dev.missileDamage),
        [] { cfg::dev.missileDamage = std::max(1, cfg::dev.missileDamage - 1); },
        [] { cfg::dev.missileDamage = std::min(999, cfg::dev.missileDamage + 1); });
    if (first == 3) row(0, "PLAYER SPEED", TextFormat("%.0f", cfg::dev.playerSpeed),
        [] { cfg::dev.playerSpeed = std::max(50.f, cfg::dev.playerSpeed - 25.f); },
        [] { cfg::dev.playerSpeed = std::min(2000.f, cfg::dev.playerSpeed + 25.f); });
    if (first == 3) row(1, "STARTING LIVES", TextFormat("%d", cfg::dev.startingLives),
        [] { cfg::dev.startingLives = std::max(1, cfg::dev.startingLives - 1); },
        [] { cfg::dev.startingLives = std::min(99, cfg::dev.startingLives + 1); });
    if (first == 3) row(2, "MUSIC LEVEL", TextFormat("%d%%", (int)std::lround(cfg::dev.musicLevel * 100)),
        [] { cfg::dev.musicLevel = std::max(0.f, cfg::dev.musicLevel - 0.1f); },
        [] { cfg::dev.musicLevel = std::min(1.f, cfg::dev.musicLevel + 0.1f); });
    const float controlsY = y + 3 * 78 * s;
    if (b({bx, controlsY, bw, bh}, developmentPage == 0 ? "NEXT PAGE" : "PREVIOUS PAGE")) developmentPage = developmentPage == 0 ? 1 : 0;
    if (b({bx, controlsY + 55 * s, bw, bh}, "RESET DEVELOPMENT SETTINGS")) cfg::dev = {};
    if (b({bx, controlsY + 110 * s, bw, bh}, "BACK") || ui::backPressed()) state = State::Options;
    nav.end();
}

// LEVEL CLEAR / GAME OVER over the frozen level (drawn by drawHud, inside the scaled game target)
void Game::drawEndOverlay() const {
    const float fs = 26 * s;
    if (state == State::LevelClear) {
        const float a = std::min(1.f, stateTime / 0.35f);
        ui::dim(W, H, 0.50f * a);
        ui::banner("LEVEL CLEAR", W / 2, H * 0.30f, 72 * s, s, a, time, false);
        ui::heading(TextFormat("SCORE %d", score), W / 2, H * 0.50f, 24 * s, s, a);
        if (stateTime > 1.2f) ui::caption("Tap or press Enter to continue", W / 2, H * 0.65f, fs * 0.8f, s, ui::kCaption, 0.6f + 0.4f * sinf(time * 3.f));
    }
    if (state == State::GameOver) {
        const float a = std::min(1.f, stateTime / 0.35f);
        ui::dim(W, H, 0.60f * a);
        ui::banner("GAME OVER", W / 2, H * 0.28f, 80 * s, s, a, time, false, {196, 84, 78, 255});
        ui::heading(TextFormat("SCORE %d   HIGH %d", score, highScore), W / 2, H * 0.50f, 22 * s, s, a);
        if (stateTime > 1.2f) ui::caption("Tap or press Enter for title", W / 2, H * 0.65f, fs * 0.8f, s, ui::kCaption, 0.6f + 0.4f * sinf(time * 3.f));
    }
}

void Game::drawVictory() {
    ui::setSky(&r3d.nebulaTexture(), &fx.glowTexture());
    ui::backdrop();
    ui::banner("CAMPAIGN COMPLETE", W / 2, H * 0.32f, 64 * s, s, std::min(1.f, stateTime / 0.6f), time);
    ui::heading(TextFormat("FINAL SCORE %d", score), W / 2, H * 0.55f, 24 * s, s, std::min(1.f, stateTime / 0.9f));
    if (stateTime > 1.2f) ui::caption("Tap or press Enter for title", W / 2, H * 0.68f, 20 * s, s, ui::kCaption, 0.6f + 0.4f * sinf(time * 3.f));
}
