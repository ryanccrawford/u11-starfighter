#include "../turret.hpp"
#include <cassert>
#include <cmath>

int main() {
    Turret turret; turret.mountStartX = 1000.f; turret.corridorScrollAtMount = 12.f;
    assert(turret.mountedX(12.f, 50.f) == 1000.f);
    assert(std::fabs(turret.mountedX(14.f, 50.f) - 780.f) < .001f);
    // The same integrated scroll produces the same position regardless of frame pacing.
    assert(turret.mountedX(14.f, 50.f) == turret.mountedX(12.f + 1.f + 1.f, 50.f));
}
