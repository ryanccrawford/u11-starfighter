#pragma once
#include "world_physics.hpp"

// World-space collision geometry for a side-scrolling bulkhead. The opening is the
// half-height of the clear center passage, in the same units as the ship coordinates.
class Bulkhead {
public:
    static bool contacts(WorldPosition center, float radius, float doorX, float halfThickness,
                         float corridorHeight, float halfOpening);
    // Earliest projected segment contact with either panel; -1 means clear passage.
    static float firstProjectileContact(Vector2 start, Vector2 end, float radius, float doorX,
                                        float halfThickness, float corridorHeight, float halfOpening);
};

// Solid volumes of the runtime bulkhead art (assets/models/ship_interior/hazards/
// bulkhead_runtime_v11) in the door's own model frame, so collision is the drawn shape:
//   u = across the corridor (screen-vertical in side view), 0 at the passage centre
//   v = along the flight direction (the door's thickness)
//   w = depth, positive away from the camera (the door spans the corridor depth)
// Render3D::drawBulkhead positions the leaves with the same constants, and
// Render3D::bulkheadLocal converts gameplay positions into this frame.
class BulkheadModel {
public:
    static constexpr float kHalfDoor = 7.f;        // half-height of the frame aperture
    static constexpr float kLeafHeight = 5.64f;    // fixed leaf height; leaves translate, never stretch
    static constexpr float kLeafTravel = 11.5f;    // retraction at opening = 1
    static constexpr float kLeafV0 = -.36f, kLeafV1 = .62f, kLeafHalfDepth = 7.f;   // leaf.glb bounds
    static constexpr float kSillU = 5.93f, kSillV0 = -.44f, kSillV1 = .83f, kSillHalfDepth = 7.39f;  // frame.glb header/sill
    static constexpr float kPillarW0 = 6.27f, kPillarW1 = 7.64f, kPillarV0 = -.64f, kPillarV1 = .54f; // pillar.glb + side plates
    static constexpr float kFaceV0 = kPillarV0, kFaceV1 = kSillV1;  // approach and far faces of the solid set
    // Leaf centre and passage edge (|u|) for an opening in [0,1].
    static float leafCenter(float opening);
    static float leafInnerEdge(float opening) { return leafCenter(opening) - kLeafHeight * .5f; }
    // True when a sphere (model units) touches any solid part at this opening.
    static bool contacts(Vector3 p, float radius, float opening);
    // Earliest contact parameter in [0,1] of a sphere swept from a to b, or -1 when clear.
    static float firstContact(Vector3 a, Vector3 b, float radius, float opening);
    // Solid boxes (min,max) in model units; used by collision and the debug overlay.
    static int solids(float opening, Vector3 (*out)[2], int maxBoxes);
};
