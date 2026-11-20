#include "profile.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstdlib>
extern "C" void glFinish(void);

namespace prof {
static const char* kNames[Count] = {"update", "clear", "bg", "smoke", "additive", "ships", "shots", "hud", "present"};
static double acc[Count], last, windowStart;
static int cur = Update, frames;
bool on() { static bool v = getenv("PROFILE") != nullptr; return v; }
static bool sync() { static bool v = !getenv("PROFILE") || atoi(getenv("PROFILE")) != 2; return v; }  // PROFILE=2: CPU-only
static void close() {
    if (sync()) { rlDrawRenderBatchActive(); glFinish(); }
    double t = GetTime(); acc[cur] += t - last; last = t;
}
void begin() { if (!on()) return; last = GetTime(); if (windowStart == 0) windowStart = last; cur = Update; }
void mark(Section next) { if (!on()) return; close(); cur = next; }
void end() {
    if (!on()) return;
    double t = GetTime(); acc[cur] += t - last; last = t;
    if (++frames, t - windowStart >= 1.0) {
        fprintf(stderr, "PROF");
        double total = 0;
        for (int i = 0; i < Count; i++) { fprintf(stderr, " %s=%.2f", kNames[i], acc[i] * 1000 / frames); total += acc[i]; acc[i] = 0; }
        fprintf(stderr, " total=%.2fms\n", total * 1000 / frames);
        frames = 0; windowStart = t;
    }
}
}  // namespace prof
