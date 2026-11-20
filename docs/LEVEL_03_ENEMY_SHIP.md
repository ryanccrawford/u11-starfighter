# Level 3: Enemy Ship Infiltration

A side-scrolling flight through a corridor inside an immense alien warship. The stage lasts 180 seconds and represents Step 2 of Chapter 1 in the campaign.

## Visual direction

The playfield is a horizontal flight corridor framed above and below by textured three-dimensional metal structures. The entrance reveals open space behind torn hull plating; deeper sections transition to enclosed armor, pipes, cable runs, moving machinery, hazard stripes, illuminated panels, and reactor conduits. Layered depth and lighting should make the ship feel immense while maintaining a clear silhouette for the player, enemies, projectiles, turrets, and door openings.

## Route

| Time | Section | Setting and encounter |
| --- | --- | --- |
| 0:00–0:30 | Hull breach | Broken exterior armor, visible stars, entrance defenses and introductory turret fire. |
| 0:30–1:05 | Maintenance tunnels | Pipes, vents, pistons and service bays; flying enemies enter between machinery. |
| 1:05–1:40 | Weapons deck | Armored gun platforms and ceiling/floor turret combinations with readable firing cues. |
| 1:40–2:20 | Security bulkheads | Repeated mechanical doors with warning lights and timed openings; always leave a feasible escape route. |
| 2:20–3:00 | Reactor approach | Energy conduits, rotating machinery, heavier defenses and a final approach into the core. |

## Gameplay requirements

- Corridor surfaces and moving door hitboxes must follow their visible geometry in the player's coordinate system. Test at all supported aspect ratios and render scales.
- Doors must announce closing early enough to react and offer a reachable opening. Their visible panels and collision derive from the same state. Contact deals damage and pushes the ship backward with the scrolling door. If the door would push the ship off-screen, destroy it once, decrement a life, and respawn beyond the door with temporary protection; preserve normal game-over rules.
- Turrets are attached to the scrolling ship, show their aim and firing cues, accept player damage, and stop firing once destroyed. Enemy projectiles must remain visible against the environment.
- Use time-based movement and animation so PC frame rate and the tablet's frame cap do not alter gameplay. Reset all stage state on retry and level transition.
- Preserve pickup, weapon, touch, and controller behavior, including accessibility of the playfield and action controls.
- Keep the art modular and bounded in draw calls, triangle count, texture memory and active objects. Profile on the target tablet as well as PC.
- Level 3 is fully integrated into the 8-step campaign (`assets/campaign.json`) and Level Select screen with persistent save progress.

## Art and animation requirements

Use the supplied turret and bulkhead references as the quality bar: detailed 3D armored shapes, geared mount, multi-barrel cannon, realistic reflective metal, panel seams, varied pipe materials, practical lights, layered depth and rugged mechanical structure. Reflection must represent the environment, not merely a flat metallic color. Avoid obvious tile repetition through varied bays and asymmetric arrangements. Reuse individual parts and materials where sensible.

Animate components that would move: turret aiming, barrel rotation, recoil and feed; door leaves, motors, pistons and locks; wiring, flow indicators, warning lamps, rotating emergency beacons and background machinery. Impacts should produce damage, an explosion and a ragged turret base with exposed wires, sparks and a small black smoke plume. Lights must use emissive materials. Optimize and profile without silently removing visual detail.

The turret is integrated in the current source using separately animated, material-batched base/head/barrel assets under `assets/models/ship_interior/hazards/turret_runtime_v2/` and a detailed-base wreck at `turret_wreck_runtime_v2.glb`. It is mounted upside-down on a flat ceiling hardpoint; mount, plate and wreck use the corridor scroll clock. A controlled PC run verified firing, laser destruction and missile contact. This is still a view-local mount rather than an authoritative world-space attachment, and has not been tablet-tested or deployed. Texture wear, physical environment capture/reflection, fully reusable entity/weapon integration and target-device profiling remain open.

The current bulkhead uses `assets/models/ship_interior/hazards/bulkhead_runtime_v11/{frame,pillar,leaf}.glb`. Both vertical pillars are instances of the same `pillar.glb`; only the screen-facing instance receives a per-instance opacity value. `BulkheadPillarVisual::setOpacity` and `updateDistance` provide the visual state API, with transition events for approaching fade, crossing the door center, and passing beyond the fade distance. The frame remains perpendicular to flight and mostly edge-on from the cockpit. Crossmembers, locks, and both door leaves remain solid. The two fixed-size leaves slide vertically apart from the center into extended upper/lower frame pockets; their meshes are translated, never stretched. Rendering and collision use the same `level3Door.opening` value. PC captures cover the shared-pillar near x-ray state; Windows x64 and ARM64 builds both succeed. The door still needs Ryan's visual review and a gameplay pass for full collision, crushing, and timing on target hardware. Tablet deployment and tablet testing have not been done.

## Verification and review

First implement a vertical slice containing a textured corridor, one turret and one opening/closing bulkhead. Verify the player's collision and safe route at different frame rates and aspect ratios, then review the appearance and behavior with Ryan before extending the full route. Validate all 180 seconds, level completion and restart, and build both PC and ARM64. Review the final playable build before deployment.
