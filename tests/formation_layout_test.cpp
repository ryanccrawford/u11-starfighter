#include "../formation_layout.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

// Every invader cell settles fully inside the playfield, and neighbouring hit circles never touch.
int main() {
    struct Case { const char* name; float F, Lat, s; };
    const Case views[] = {{"side 1280x720", 1280.f, 720.f, 1.2f}, {"top 1280x720", 720.f, 936.f, 1.2f},
                          {"side 2000x1200", 2000.f, 1200.f, 2.f}, {"top tablet", 1200.f, 1560.f, 2.f}};
    const int counts[] = {1, 5, 8, 16, 24, 28, 32};
    int checked = 0;
    for (const Case& v : views) for (int n : counts) for (float lane : {0.2f, 0.5f, 0.8f}) {
        const float r = 96.f * 0.28f * 0.6f * v.s, hitR = r * 1.35f + 8.f * v.s;   // nearest rank, laser pad
        const FormationLayout L = layoutFormation(n, 58.f * v.s, hitR, v.F, v.Lat, lane, v.s);
        assert(L.spacing + 1e-3f >= 2.f * hitR + 6.f * v.s);                      // hit volumes apart
        for (int i = 0; i < n; i++) {
            const float x = L.settleX + (i / L.cols) * L.spacing, y = L.originY + (i % L.cols) * L.spacing;
            assert(x - hitR >= 0.f && x + hitR <= v.F - 40.f * v.s + 1e-3f);       // fully on-screen, forward
            assert(y - hitR >= L.edge - 1e-3f && y + hitR <= v.Lat - L.edge + 1e-3f); // inside the march margins
            ++checked;
        }
    }
    // Entry glide: monotonic, never stops short of the settle line, arrives.
    float x = 1400.f, prev = x; int frames = 0;
    while (x > 500.f && frames < 2000) { x = formationEntryStep(x, 500.f, 200.f, 1 / 60.f); assert(x < prev || x == 500.f); prev = x; ++frames; }
    assert(x == 500.f && frames < 600);
    std::printf("formation_layout_test OK (%d cells, entry %d frames)\n", checked, frames);
}
