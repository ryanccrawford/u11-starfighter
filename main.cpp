#include "game.hpp"
#include "config.hpp"
#include "profile.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstdlib>

int main() {
#ifdef _WIN32
    // Windows launchers and shortcuts do not guarantee the executable's
    // directory as the working directory. Runtime assets are packaged beside
    // shooter.exe, so resolve all existing relative asset paths from there.
    ChangeDirectory(GetApplicationDirectory());
#endif
    unsigned flags = FLAG_BORDERLESS_WINDOWED_MODE;
    if (getenv("MSAA")) flags |= FLAG_MSAA_4X_HINT;  // costly on Mali via DRI3; off by default
    // HIDDEN_WINDOW=1: test/capture runs never pop a window over the desktop (GL still renders; captures read the back buffer)
    const char* hw = getenv("HIDDEN_WINDOW");
    if (hw && *hw && *hw != '0') flags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(flags);
    InitWindow(0, 0, "U11 Starfighter");
#ifdef _WIN32
    SetTargetFPS(0);   // PC: no software frame limiter; render as fast as the hardware allows
#else
    SetTargetFPS(90);
#endif
    SetExitKey(KEY_NULL);  // ESC must not quit (tablet extra-keys bar)
    // raylib's default batch has ONE vertex buffer: every flush (texture/blend switch, text) rewrites a VBO
    // the GPU is still reading and Mesa stalls until the GPU drains. Rotating 8 buffers removes the stall.
    static rlRenderBatch batch = rlLoadRenderBatch(getenv("BATCHBUFS") ? atoi(getenv("BATCHBUFS")) : 8, 8192);
    rlSetRenderBatchActive(&batch);
    {
        Game game;
        int frames = 0; double last = 0;
        while (!WindowShouldClose() && !game.wantsQuit()) {
            prof::begin();
            static const float fixedDt = getenv("FIXED_DT") ? (float)atof(getenv("FIXED_DT")) : 0.f;   // t252: deterministic capture runs
            game.update(fixedDt > 0.f ? fixedDt : GetFrameTime());
            game.draw();
            frames++;
            if (GetTime() - last >= 1.0) { fprintf(stderr, "FPS %d\n", (int)(frames / (GetTime() - last) + 0.5)); last = GetTime(); frames = 0; }
        }
    }
    rlSetRenderBatchActive(nullptr);
    rlUnloadRenderBatch(batch);
    CloseWindow();
}
