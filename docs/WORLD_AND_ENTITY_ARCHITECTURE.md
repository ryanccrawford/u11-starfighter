# World, entities and assets

This document records the agreed direction and distinguishes it from the current implementation. It does not claim that the 3D world migration or Level 3 is finished.

## Design principle

Design for the intended 3D world rather than preserving obsolete 2D assumptions for their own sake. Update or replace legacy abstractions when they obstruct the agreed direction; preserve the player's intended gameplay and data deliberately, and validate changes instead of adding indefinite compatibility layers. Existing implementation details are evidence of current behavior, not a constraint on the future design.

## World and views

- **Target:** Every object has one authoritative 3D position in a fixed world frame: X forward, Y across, Z height. Switching side/top views changes the camera, not the object's position. Level-owned spawn points use this frame. A future `changeView` transitions the camera smoothly at runtime.
- Ship and scenery physics use actual 3D volumes in every view. Objects cannot occupy the same physical space; a visual overlap is valid when objects are separated in depth. Avoid collisions when possible and apply appropriate damage/destruction when avoidance fails.
- **Projectile exception:** Side-view hits project onto X/Z and ignore Y; top-view hits project onto X/Y and ignore Z, because the player cannot aim along the hidden axis. A shot hits the first valid object along its path; a target's threat priority does not let it pass through an earlier blocker. During a view transition, the agreed starting rule is to retain the source collision plane until the transition completes.
- Shared flight controls, throttle, camera behavior, flame response and collision rules apply to every level of each view type. Holding the joystick at forward/reverse extremes should produce a smooth frontal/rear camera change and throttle/flame response, interpreted appropriately for the view.
- **Current:** The active `makeCamera` uses perspective, and `Ship::hiddenAxis` produces visible near/far scaling for ships and related effects. `WorldPosition`, `WorldPhysics`, projected-contact helpers and `ViewTransition` exist with tests. Gameplay still stores much of its position in 2D: `Ship::worldPosition` reconstructs a position from the current view and a hidden coordinate, while render placement slides along a camera ray. This is useful perspective rendering, but it is not an authoritative, fixed 3D world position. Enemy `r` no longer changes with perspective; player projectiles calculate a projected radius separately. However, `r` and the reconstructed coordinates are still in legacy gameplay units rather than verified world-space physical bounds. The new world camera and runtime view change are not wired into a live level.
- **Migration requirement:** Make fixed world transforms and physical dimensions authoritative; project them for rendering and projectile hits. Do not let a camera change or visual size multiplier change an object's world position or physical collision volume. Retain top-down forward as screen-up unless a deliberate gameplay design changes that orientation. Validate changes against the current perspective/depth implementation rather than restoring historical camera files.
- **Known visual gaps:** Flame overlays are not depth-sorted against hulls; enemy bullets, lasers and sparks remain flat overlays. Resolve these deliberately as the world/render migration progresses rather than describing the current perspective pass as a completed 3D world.

## Entities, ownership and behavior

- **Target:** Reusable entity/combat behavior and a shared ship base support player-controlled or enemy-controlled ships. Health, damage, movement, physical collision, weapons and death are not inherently player-only or enemy-only. Controls and AI are replaceable behavior components.
- Weapons and projectiles are reusable regardless of who fires them. Ownership and allegiance determine valid targets and friendly-fire rules. A missile should acquire any eligible opposing player, ship, active turret, or destructible threat rather than relying on a player-only/enemy-only projectile class. Threat, distance, reachability and line of fire can affect selection, while first contact determines what is actually hit.
- Ships, turrets, doors and scenery can be allied, hostile, neutral, or unowned. Doors and other objects are targetable only when their state and destructibility permit it. Disabled/destroyed threats should not be selected as active threats.
- **Current:** `Ship` is a base for `Player` and `Enemy`; `Projectile` is a base for the current laser, missile and enemy-shot types. `combat_types.hpp` defines faction, ownership and target-profile vocabulary. Player and enemy firing/homing paths are still separate, only partly use faction filtering, and there is one player. A general entity registry, object IDs, threat ranking, shared weapon behavior and scenery targeting are not implemented.

## Presentation assets

Entity behavior is separate from presentation. An entity can reference a model or sprite, child parts, named animation clips, material/emissive settings and sound cues; changing an asset changes appearance and presentation, not the entity's physical or combat identity. Model anchors and compatible scale/collision definitions must be validated when swapping assets. Raylib supplies model, animation, rendering, input, collision and audio primitives; game classes define gameplay behavior.

**Current:** `PresentationAsset` groups model/sprite references, and enemy definitions may select idle/fire clip names. Models can have raylib-loaded animations and sidecar muzzle/nozzle metadata. Generic child-part ownership, player model swapping, configurable sound cues, and complete asset-driven animations are not integrated.

## Level 3 direction and state

See [LEVEL_03_ENEMY_SHIP.md](LEVEL_03_ENEMY_SHIP.md) for the three-minute route, art direction and door/turret behavior. The detailed turret source is `assets/models/ship_interior/hazards/turret_detailed_v1/turret.blend`; material-batched parts and a detailed-base wreck are now used by the current PC source. The mount follows the corridor scroll clock but still uses view-local placement, not the planned authoritative world transform. The turret has not been profiled or play-tested on the tablet; the bulkhead art is not final.

**Current validation:** The isolated tablet review has logged a projectile striking a placeholder door panel, projectiles passing its central opening, and a crush followed by a life decrement and respawn beyond the door. That does not establish full timing, all collision cases, finished art, animation, performance, or full-level completion. Review-only flags and the normal installation must not be confused.

## Verification and release

Build both Windows and ARM64 targets. Test real simultaneous touch, both views and camera extremes, first-hit order, physical separation, shield contacts, spawn/reset, door pushback/crush/respawn and the entire 180-second route on the U11. Profile the final art and effects on-device. The user requested human review; the earlier local-model review was explicitly waived for this work. Keep the normal tablet installation separate from isolated review builds until the user approves deployment. The established full build/deploy script is `tools/build-and-deploy.ps1`.
