# U11 Starfighter — Data Formats (levels, enemies, players, campaign)

All JSON lives under `assets/`. Press F5/R to hot-reload. Some existing fields have defaults; required fields inside new definitions must be supplied. The target world-space architecture and current migration limits are documented in [WORLD_AND_ENTITY_ARCHITECTURE.md](WORLD_AND_ENTITY_ARCHITECTURE.md).

## Modes and the local coordinate frame
`"mode": "side_scroll" | "top_down"`. Gameplay runs in one local frame: **x = forward** (direction of flight),
**y = lateral**. Side-scroll maps forward → screen right; top-down maps forward → screen up and uses a centred
playfield column (width = min(screen W, H × 1.3)) with HUD side panels. All "y" fractions below are lateral 0..1
(side: top→bottom, top-down: left→right).

## Level — `assets/levels/level_XX.json`

Optional `spawn_points` entries have a required `id` and `position` object containing `x`, `y`, and `z` in design-space units (X forward, Y across, Z height). A point named `player` is projected into the active view at level start. Enemy/object spawn-point integration and authoritative 3D level movement are not complete; existing levels without a player point retain their existing start behavior.
```json
{
  "name": "Outer Rim",
  "mode": "side_scroll",
  "player": "red_fighter",             // assets/players/<id>.json
  "boss": "warden",                    // optional: spawned after all waves are cleared
  "intro": "intro",                    // optional cutscene id played before the level
  "outro": "chapter1_end",             // optional cutscene id played after
  "music": "",                         // reserved
  "terrain": "",                       // reserved for ground controllers (tilemap/heightmap path)
  "clear_color": [6, 6, 20],
  "difficulty": { "hp": 1.0, "fire_rate": 1.0, "speed": 1.0 },   // multipliers on enemy defs
  "background": [ <layer>, ... ],      // drawn back to front
  "waves": [ <wave>, ... ]             // sorted by time automatically
}
```
### Background layer
| key | meaning |
|---|---|
| `type` | `texture`, `stars`, `nebula`, `planet`, `planet3d`, `terrain3d`, `asteroids` |
| `texture` | PNG path (type texture). Missing → falls back to stars |
| `speed` | scroll speed px/s at 600 px reference height (parallax: far = slow) |
| `color` | RGB(A) tint |
| `density` | item count (stars ≈ count, nebula = density/16 blobs, asteroids = density/4) |
| `y` | lateral centre 0..1 (planet, asteroid belt) |
| `size` | size multiplier |
| `model` | GLB under `assets/models/` for `planet3d`/legacy `terrain3d`, or a `shader_terrain` JSON descriptor for procedural terrain |
| `cloud_model` | optional transparent cloud-shell GLB for `planet3d` |
| `spin` | surface rotation radians per second for `planet3d` |
| `cloud_spin` | independent cloud-shell rotation radians per second for `planet3d` |

### Wave
| key | default | meaning |
|---|---|---|
| `time` | 0 | seconds since level start |
| `enemy` | grunt | enemy id |
| `count` | 1 | how many |
| `formation` | line | `line` (side by side), `column` (one after another), `v`, `row`, `random`; invaders-pattern enemies always form a marching grid |
| `y` | 0.5 | lateral centre 0..1 |
| `spacing` | 70 | px between members |
| `amplitude`/`frequency` | from enemy | override movement params |

The level ends when all waves have spawned, no enemies remain, and the boss (if any) is dead.

## Enemy — `assets/enemies/<id>.json`
```json
{
  "id": "weaver", "name": "Weaver",
  "sprite": "sprites/blue_racer_pi3_sprites.json",
  "scale": 0.8, "tint": [200, 170, 255],
  "hp": 2, "speed": 130, "score": 150, "boss": false,
  "movement": { "pattern": "sine", "amplitude": 80, "frequency": 3,
                "points": [[1.05, 0.15], [0.6, 0.5], [-0.1, 0.5]] },
  "weapon": { "type": "aimed", "interval": 2.4, "shots": 1 },
  "drops": { "chance": 0.15, "table": { "weapon": 3, "missiles": 2, "rapid": 1, "shield": 1, "repair": 1 } },
  "phases": [ ... ]   // bosses only
}
```
- **pattern**: `straight`, `sine`, `dive` (homes toward the player's lateral position), `circle`, `path`
  (`points` = [forward 0..1, lateral 0..1] waypoints in playfield fractions), `invaders` (marching grid: steps
  sideways, advances at the edges, speeds up as members die), `boss` (holds near the far edge and strafes).
- **weapon.type**: `none`, `aimed`, `spread` (`shots` fan), `laser` (fast straight bolt), `missile` (slow homing),
  `spiral` (`shots` arms, rotating), `sweep` (rotating stream).
- **drops.table**: weights per pickup — `weapon` (laser level 1→3: single, double, spread), `missiles` (+ammo),
  `shield`, `repair`, `rapid` (fire-rate ×2 for 8 s).

### Boss phases
`"boss": true` + `"phases"` list. A phase applies while hp fraction ≤ its `hp` (the lowest matching wins):
```json
{ "hp": 0.66, "pattern": "boss",
  "weapon": { "type": "spiral", "interval": 0.12, "shots": 3 },
  "minions": { "enemy": "diver", "every": 6, "count": 3 } }
```
Bosses get the top HP bar with `name`, a WARNING banner on entry, white hit flashes, and a multi-stage death
(chain explosions for ~1.8 s, then a big blast).

## Player — `assets/players/<id>.json`
```json
{ "id": "red_fighter", "sprite": "sprites/red_fighter_pi3_sprites.json",
  "controller": "flight",            // flight | ground_side (planned) | ground_topdown (planned)
  "size": 110, "speed": 650, "hp": 100,
  "gravity": 1800, "jump": 700, "walk": 220, "run": 380 }   // used by ground controllers
```

## Campaign — `assets/campaign.json`
```json
{ "title": "U11 Starfighter",
  "chapters": [ { "title": "Chapter 1", "steps": [ {"level": "level_01"}, {"cutscene": "interlude"}, {"level": "level_02"} ] } ] }
```
A step is a `level` (its own intro/outro play around it) or a standalone `cutscene`. Missing campaign → every
level in filename order. Progress (unlocked levels, campaign step, high score, volume, FPS display) is saved to
`~/.u11shooter_save` (chroot `/root/.u11shooter_save`).

## Cutscene — `assets/cutscenes/<id>.json`
`{"steps": [ ... ]}` executed in order. Tap advances dialog; the **SKIP »** corner ends the scene.
| type | keys |
|---|---|
| `background` | `color` [r,g,b], `image` (PNG) |
| `actor` | `actor` (id), `sprite` (ship json), `from` / `to` [x,y] screen fractions, `duration`, `amount` (scale) |
| `dialog` | `speaker`, `portrait` (PNG), `text` (`\n` for line breaks; typewriter 40 chars/s) |
| `title` | `text`, `duration` |
| `wait` | `duration` |
| `fade` | `from_alpha`, `to_alpha`, `duration` (1 = black) |
| `shake` | `amount` 0..1, `duration` |
| `sfx` | `sfx`: `laser`, `pickup`, `explosion` |

## Debug env vars (set via `GLENV="..."` for run-shooter.sh)
`START_LEVEL=n` jump into level n · `UNLOCK_ALL=1` · `GODMODE=1` · `NOAUDIO=1` · `MSAA=1` (slow on DRI3) ·
`DBG=mask` skip draw passes (1 bg, 2 particles, 4 weapons, 8 HUD, 16 scissor) · `BGSKIP=mask` skip bg layer types.

`AUTOPLAY=1` demo pilot (implies god mode) · `PROFILE=1|2` per-section timings (1 = GPU-synced, 2 = CPU-only) ·
`RENDER_SCALE=0.4..1` (+`LOWFX=1`) forces graphics quality · `BATCHBUFS=n` raylib vertex buffers (default 8) ·
`PIN="..."` CPU pinning prefix (default `taskset -c 6,7`). `bench.sh` runs the autoplay benchmark.
