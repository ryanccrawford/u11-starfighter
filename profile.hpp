#pragma once
// PROFILE=1: per-section timings. Each mark() flushes raylib's batch and glFinish()es so the
// GPU cost of the previous section is attributed to it (adds sync overhead; use for relative costs).
#include "raylib.h"

namespace prof {
enum Section { Update, Clear, Bg, Smoke, Additive, Ships, Shots, Hud, Present, Count };
bool on();
void begin();             // start of frame
void mark(Section next);  // close the current section, open `next`
void end();               // after EndDrawing
}
