#include "../bulkhead.hpp"
#include <cassert>

int main() {
    // Side-view local screen Y is negative world Z; a nonzero depth does not evade a door.
    assert(!Bulkhead::contacts({50, 0, -50}, 5, 50, 2, 100, 15));
    assert(Bulkhead::contacts({50, 30, -20}, 5, 50, 2, 100, 15));
    assert(Bulkhead::contacts({50, -30, -80}, 5, 50, 2, 100, 15));
    assert(!Bulkhead::contacts({20, 0, -20}, 5, 50, 2, 100, 15));
    assert(!Bulkhead::contacts({50, 0, -20}, 5, 50, 2, 100, 50));
    assert(Bulkhead::contacts({50, 0, -50}, 5, 50, 2, 100, 0));
    assert(Bulkhead::firstProjectileContact({0, 50}, {100, 50}, 1, 50, 2, 100, 15) < 0);
    const float hit = Bulkhead::firstProjectileContact({0, 10}, {100, 10}, 1, 50, 2, 100, 15);
    assert(hit > .46f && hit < .48f);
}
