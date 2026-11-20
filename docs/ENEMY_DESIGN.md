# Alien-robot enemy fleet: design language + pipeline

The enemy faction is **robot warships flown by robot pilots and controlled by aliens**. Every ship must read as
"machine hull + robot pilot + alien brain in charge" in both the side and the overhead camera.

## Shared design language (keep all five consistent)
| Element | Rule | Scarab example |
|---|---|---|
| Hull metal | Gunmetal `(0.25, 0.26, 0.29)`, metallic, large plates with thin dark panel seams, worn bright edges, grime | body, engine can |
| Faction paint | Deep alien violet `(0.17, 0.06, 0.25)` on armour plates, shells and fins | beetle wing cases, fins, brow, mandibles |
| Mechanics | Near-black `(0.05)` for joints, vents, mounts, abdomen rings; bright steel `(0.50)` for barrels, hinges and antennas | gun mount, hinges, rings |
| Robot pilot | Red robot glow: a single visor slit or red eye(s) `glow_red` at the front | the big red sensor eye |
| Alien control | A **green glowing bio-brain dome** (veined, `brain` material) on the dorsal centre, steel ring, cables into the hull. Every ship has one, always visible from above | dorsal dome |
| Alien tech | Small acid-green `glow_green` lights: fin tips, side lights, engine cores, weapon coils | fin tips, gun coil |
| Engine flame | `engineTint` in the enemy JSON = the ship's secondary (faction-paint) colour at full brightness. The shader recolours the flame and keeps a thin white-hot core | violet [214, 128, 255] (wing-case violet (115, 69, 137) sRGB, brightened) |
| Weapon glow | Matches the shot colour (Scarab: green needles). Later tiers: magenta plasma, cyan/green beam, orange fire | coil + telegraph |
| Hazard | Yellow/black stripes (`hazard`) only on big weapons of tier 3+ | – |
| Silhouette | Nose at Blender −Y (glTF +Z), length exactly 2.0, width ≤ 2.0, height ~0.8. Origin at bbox centre | 1.94 × 2.0 × 0.79 |
| Size class | `scale` in JSON sets the hit radius (on-screen size is fixed by the engine) | 0.85 (small) |
| Budgets | High: 25–40k tris (render/source). Game LOD: 4–6k tris, one mesh, one 2048² texture | 28,438 / 5,042 |
| Animation | **No flapping wings or fins, ever** (it's space). Fins and wings are rigid plates; Idle animates sensors, mandibles, antennas, lights and machinery. ≤ 10 rigid bones. `Idle` 2.0 s loop (secondary motion); `Fire` short one-shot on `W_` bones only | 8 bones |

Planned tiers (same language, rising threat):
1. **Scarab Scout Drone**: single aimed green needle (`needle`). *Done.*
2. Hornet Gunship: rotary-cannon bursts. Twin engine pods with spinning intake fans, robot head cockpit.
3. Wraith Interceptor: homing magenta plasma orbs. Forward-swept blades, spinning plasma core ring.
4. Lancer Beam Frigate: telegraphed charge beam. Long emitter with rotating capacitor rings, opening prongs.
5. Inferno Brute: short-range flamethrower with a telegraph and continuous damage. Fuel tanks pump, iris nozzle petals open.

## Current placement (temporary)
Until the other four are built, **every enemy type (and the Warden boss) uses the Scarab model** with the violet flame. Ryan will decide where each new design fits in the story as it's finished. Then set that enemy's `model`, `engineTint` and `tint` (restore from `legacyTint`).

## Pipeline (box: Blender 4.2 LTS headless; the scripts also target Blender 5.0)
```
blender -b --python tools/blender_enemies/pipeline.py -- <id> <out_dir> [--fast]
blender -b <out_dir>/<id>.blend --python tools/blender_enemies/render_extra.py -- <id> assets/models/red_fighter.glb <render_dir>
```
- `enemy_lib.py`: the hard-surface kit (loft hulls, bevelled boxes/cylinders, plates, tori, tubes, surface-snapped greebles), the procedural Cycles materials (triplanar panel seams, grime, edge wear, glow) and the shared faction parts (`robot_pilot`, `brain_dome`, `engine`).
- `enemy_defs.py`: one function per enemy: parts → bones, pivots, nozzles, muzzle, and the `Idle`/`Fire` motion as functions of time. Add the next enemy here.
- `pipeline.py` builds the **high** and **LOD** versions from the same function, normalises to length 2, rigs (one bone per part group), samples the actions at 60 fps, unwraps, renders previews, then bakes from high to LOD (albedo, glow, AO). The glow goes into the texture alpha (glow = 1 − alpha). It exports `<id>_lod.glb`, `<id>_high.glb`, the `<id>_lod.json` sidecar and `<id>.blend`.
- Copy the output folder to `assets/models/enemies/<id>/`, add `assets/enemies/<id>.json` with `"model"`, and test with `ENEMY_TEST=<id> START_LEVEL=1` (side) and `START_LEVEL=2` (overhead).

## Engine hooks (C++)
- `Render3D::enemyModel(file)` loads/caches a model (index 0 = default blue_racer). Each model gets its own breakup chunks, bbox (dents and hit contact), nozzles and muzzle from the sidecar.
- `EnemyAnim {model, idleT, fireT, held, glow}` is passed to `drawShip`. The game fills it from `Enemy::t`, `Enemy::fireAnimT` (reset to 0 on every shot) and the `telegraph` window.
- `Enemies::muzzleOf` makes shots leave the model's muzzle (only for enemies with a `model`).
- New weapon types go in `EnemyWeapon` (data.hpp), `weaponType()` (data.cpp) and `Enemies::fire()` (enemies.cpp). `EnemyShot::dmg` carries per-weapon damage.
