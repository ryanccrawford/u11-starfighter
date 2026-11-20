#include "bulkhead.hpp"
#include <algorithm>
#include <cmath>

bool Bulkhead::contacts(WorldPosition center, float radius, float doorX, float halfThickness,
                        float corridorHeight, float halfOpening) {
    const float height = std::max(0.f, corridorHeight);
    const float gap = std::max(0.f, halfOpening);
    const float thickness = std::max(0.f, halfThickness);
    const float mid = height * .5f;
    const float upperEnd = std::max(0.f, mid - gap);
    const float lowerStart = std::min(height, mid + gap);
    const float x0 = doorX - thickness, x1 = doorX + thickness;
    const WorldBox upper{{x0, -100000.f, -upperEnd}, {x1, 100000.f, 0.f}};
    const WorldBox lower{{x0, -100000.f, -height}, {x1, 100000.f, -lowerStart}};
    return (upperEnd > 0.f && WorldPhysics::overlapsBox(center, radius, upper)) ||
           (lowerStart < height && WorldPhysics::overlapsBox(center, radius, lower));
}

float Bulkhead::firstProjectileContact(Vector2 start, Vector2 end, float radius, float doorX,
                                       float halfThickness, float corridorHeight, float halfOpening) {
    const float h = std::max(0.f, corridorHeight), gap = std::max(0.f, halfOpening);
    const float thickness = std::max(0.f, halfThickness), r = std::max(0.f, radius);
    const float upper = std::max(0.f, h * .5f - gap), lower = std::min(h, h * .5f + gap);
    auto segmentBox = [&](float y0, float y1) {
        float lo = 0.f, hi = 1.f;
        auto axis = [&](float p, float d, float low, float high) {
            if (std::fabs(d) < 1e-7f) return p >= low && p <= high;
            float a = (low - p) / d, b = (high - p) / d;
            if (a > b) std::swap(a, b);
            lo = std::max(lo, a); hi = std::min(hi, b);
            return lo <= hi;
        };
        if (!axis(start.x, end.x-start.x, doorX-thickness-r, doorX+thickness+r) ||
            !axis(start.y, end.y-start.y, y0-r, y1+r)) return -1.f;
        return lo;
    };
    const float a = upper > 0.f ? segmentBox(0.f, upper) : -1.f;
    const float b = lower < h ? segmentBox(lower, h) : -1.f;
    return a < 0.f ? b : b < 0.f ? a : std::min(a, b);
}

float BulkheadModel::leafCenter(float opening) {
    return kHalfDoor - kLeafHeight * .5f + std::clamp(opening, 0.f, 1.f) * kLeafTravel;
}

int BulkheadModel::solids(float opening, Vector3 (*out)[2], int maxBoxes) {
    constexpr float far = 60.f;  // pockets and frame continue beyond the visible corridor
    const float inner = leafInnerEdge(opening);
    int n = 0;
    auto add = [&](Vector3 a, Vector3 b) { if (n < maxBoxes) { out[n][0] = a; out[n][1] = b; ++n; } };
    for (float side : {-1.f, 1.f}) {
        // Leaf plus its frame pocket: everything from the leaf's passage edge outward.
        const float u0 = side > 0 ? inner : -far, u1 = side > 0 ? far : -inner;
        add({u0, kLeafV0, -kLeafHalfDepth}, {u1, kLeafV1, kLeafHalfDepth});
        const float s0 = side > 0 ? kSillU : -far, s1 = side > 0 ? far : -kSillU;
        add({s0, kSillV0, -kSillHalfDepth}, {s1, kSillV1, kSillHalfDepth});
        const float w0 = side > 0 ? kPillarW0 : -kPillarW1, w1 = side > 0 ? kPillarW1 : -kPillarW0;
        add({-far, kPillarV0, w0}, {far, kPillarV1, w1});
    }
    return n;
}

bool BulkheadModel::contacts(Vector3 p, float radius, float opening) {
    Vector3 boxes[6][2];
    const int n = solids(opening, boxes, 6);
    const WorldPosition c{p.x, p.y, p.z};
    for (int i = 0; i < n; ++i) {
        const WorldBox box{{boxes[i][0].x, boxes[i][0].y, boxes[i][0].z}, {boxes[i][1].x, boxes[i][1].y, boxes[i][1].z}};
        if (WorldPhysics::overlapsBox(c, radius, box)) return true;
    }
    return false;
}

float BulkheadModel::firstContact(Vector3 a, Vector3 b, float radius, float opening) {
    Vector3 boxes[6][2];
    const int n = solids(opening, boxes, 6);
    const float r = std::max(0.f, radius);
    float best = 2.f;
    for (int i = 0; i < n; ++i) {
        // Slab test against the box grown by the radius (conservative at edges and corners).
        float lo = 0.f, hi = 1.f; bool hit = true;
        const float p0[3] = {a.x, a.y, a.z}, d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
        const float mn[3] = {boxes[i][0].x - r, boxes[i][0].y - r, boxes[i][0].z - r};
        const float mx[3] = {boxes[i][1].x + r, boxes[i][1].y + r, boxes[i][1].z + r};
        for (int k = 0; k < 3 && hit; ++k) {
            if (std::fabs(d[k]) < 1e-7f) { hit = p0[k] >= mn[k] && p0[k] <= mx[k]; continue; }
            float t0 = (mn[k] - p0[k]) / d[k], t1 = (mx[k] - p0[k]) / d[k];
            if (t0 > t1) std::swap(t0, t1);
            lo = std::max(lo, t0); hi = std::min(hi, t1);
            hit = lo <= hi;
        }
        if (hit && lo < best) best = lo;
    }
    return best <= 1.f ? best : -1.f;
}
