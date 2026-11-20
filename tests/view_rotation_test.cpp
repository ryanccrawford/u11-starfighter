// Level 6 turned view: toScreen/toLocal and dirToScreen/dirToLocal stay exact inverses at any turn
// and shake, rot = 0 is identical to the plain top-down mapping, and rot = pi/2 reads like the
// side-scroller (forward = screen right, lateral = screen down).
#include "view.hpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>
static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { ++fails; std::printf("FAIL %s\n", what); } }
static bool near(Vector2 a, Vector2 b, float e = 1e-3f) { return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e; }
int main() {
    View v; v.mode = Mode::TopDown; v.pf = {280, 0, 720, 720}; v.s = 1;
    View plain = v;
    const Vector2 pts[] = {{0, 0}, {100, 200}, {719, 3}, {360, 360}, {-80, 500}, {900, 720}};
    for (float rot : {0.f, 0.3f, 0.785f, 1.5707963f}) for (Vector2 sh : {Vector2{0, 0}, Vector2{9, -14}}) {
        v.rot = rot; v.shake = sh;
        for (Vector2 l : pts) {
            check(near(v.toLocal(v.toScreen(l)), l), "toLocal(toScreen(l)) == l");
            const Vector2 d = {l.x * 0.01f + 0.3f, l.y * -0.02f + 0.1f};
            check(near(v.dirToLocal(v.dirToScreen(d)), d), "dirToLocal(dirToScreen(d)) == d");
            // distances are preserved (camera turn only: no rescale of world sizes)
            const Vector2 a = v.toScreen(l), b = v.toScreen({l.x + 50, l.y + 30});
            check(std::fabs(std::hypot(b.x - a.x, b.y - a.y) - std::hypot(50.f, 30.f)) < 1e-2f, "screen distance preserved");
        }
    }
    v.rot = 0; v.shake = {0, 0};
    for (Vector2 l : pts) check(near(v.toScreen(l), plain.toScreen(l)), "rot 0 == plain top-down");
    v.rot = 1.5707963f;
    const Vector2 f = v.dirToScreen({1, 0}), lat = v.dirToScreen({0, 1});
    check(near(f, {1, 0}), "side phase: forward = screen right");
    check(near(lat, {0, 1}), "side phase: lateral = screen down");
    check(std::fabs(v.forwardAngle() - 0.f) < 1e-3f, "side phase: forwardAngle 0");
    std::printf(fails ? "view_rotation_test: %d FAIL\n" : "view_rotation_test: OK\n", fails);
    return fails ? 1 : 0;
}
