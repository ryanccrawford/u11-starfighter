# U11 Starfighter — Asset Spec

All paths are relative to `assets/`. The game runs from `/root/shooter` (chroot), so `assets/` must sit next to the binary.
Hot reload: press **F5** or **R** (keyboard) to reload every JSON and texture without restarting.
Missing files never crash: a missing sprite PNG becomes a procedural arrow sheet, a missing texture becomes a
placeholder (or a star layer for backgrounds), a missing portrait becomes a "?" box, and a bad JSON is logged
(`DATA bad ...` / `ASSET ...` in `/data/local/tmp/shooter.log`) and skipped.

## Directory layout
```
assets/
  sprites/      ship sheets + their *_sprites.json metadata, flame sheets
  players/      <id>.json   player definitions
  enemies/      <id>.json   enemy + boss definitions
  levels/       level_XX.json  (sorted by filename = default order)
  cutscenes/    <id>.json
  backgrounds/  PNGs used by "texture" background layers
  portraits/    dialog portraits (square PNG, any size; drawn at ~96 px)
  campaign.json chapters -> steps
```

## Ship sprite sheets (flight)
One metadata JSON per ship, e.g. `sprites/red_fighter_pi3_sprites.json`:
```json
{
  "frame_size_px": [128, 128],
  "frames_per_loop": 8,
  "side_scroller": {
    "file": "red_fighter_side_scroller_pi3.png",   // relative to the json's folder
    "columns": 8, "rows": 5, "sheet_size_px": [1024, 640],
    "states_by_row": ["cruise", "accelerate", "brake", "turn_up", "turn_down"],
    "exhaust_direction": "left",
    "nozzles_by_frame": [ [ [{"x":0.27,"y":0.57},{"x":0.27,"y":0.45}], ...8 frames ], ...rows ]
  },
  "top_down": {
    "file": "red_fighter_top_down_pi3.png",
    "columns": 8, "rows": 9, "sheet_size_px": [1024, 1152],
    "movement_directions_by_row": ["N","NE","E","SE","S","SW","W","NW","idle"],
    "exhaust_direction": "down",
    "nozzles_by_frame": [...]
  }
}
```
Rules:
- Frames are **128×128**, laid out row-major; columns = animation frames (looped at `cfg::kAnimFps` = 20 fps).
- Side view: ship faces **right** (+x). Top-down: ship faces **north** (up); rows are *banking poses* for the
  movement direction — the ship never rotates.
- `nozzles_by_frame[row][col]` = up to 2 nozzle points, **normalised 0..1** within the frame. Flames are anchored there.
- Optional side_scroller pitch (lasers/flames follow the visible nose): `pitch_deg_by_frame[row][col]` = measured nose pitch vs the cruise row, degrees, + = nose up (generate with `tools/measure_pitch.py <sheet.json>`). Applied pitch = clamp(measured × `pitch_gain` (default 0.5), ±`pitch_max_deg` (12)); flame/spark direction uses it every frame, player lasers only when |applied| ≥ `aim_pitch_min_deg` (8). Top-down sheets are banking poses: no pitch. After re-rendering frames in Blender, re-run the tool or edit these numbers; no code change needed.
  Without nozzle data the flame anchor falls back to `cfg::kFlameAnchorSide` (0.62, 0.69) / `kFlameAnchorTop` (0.5, 0.15).
- Enemies reuse the same sheets mirrored (side: horizontal flip, top-down: vertical flip + nozzle y → 1−y).
- `rows`/`columns` may be omitted; they're derived from `sheet_size_px / frame_size_px`.
- PNG: RGBA, straight alpha, transparent background.

## Engine flames
`sprites/engine_flame_pi3_sprites.json` → 1024×128 strip, 8 frames of 128×128, drawn **additively** at
`cfg::kFlameScale` (0.75) with a pulsing radial glow and pooled glitter. Tunables live in `config.hpp`.

## Ground characters (planned — `controller: ground_side | ground_topdown`)
Not implemented yet (the controller logs "not implemented" and falls back to flight). Art spec so it can be made now:
- Frame 128×128 (or 96×96 for small chars; set `frame_size_px`), feet at bottom-centre (pivot x=0.5, y=0.94).
- **Side view** sheet (`"side"` section), one row per state, facing right, 8 columns:
  `idle, walk, run, jump (rise/apex/fall across frames), shoot, hurt, death` → 7 rows.
- **Top-down** sheet (`"top_down"` section): 8 directions (N, NE, E, SE, S, SW, W, NW) × states.
  Layout: row = state × 8 + direction, states `idle, walk, run, shoot, hurt, death` → 48 rows × 8 cols
  (split into one PNG per state if that exceeds 4096 px: `"files_by_state": {"idle": "...", ...}`).
- Optional `"muzzle_by_frame"` (same shape as `nozzles_by_frame`) for gun-flash/bullet origin.
- Optional `"hitbox": [x, y, w, h]` normalised.

## Backgrounds
- `texture` layers: PNG, any size. Side mode scales to playfield height and tiles **horizontally**; top-down scales to
  playfield width and tiles **vertically**. Make the edges seamless. Suggested 2048×604 (side) / 785×2048 (top-down).
- Procedural layer types (`stars`, `nebula`, `planet`, `asteroids`) need no art.

## Portraits & cutscene images
- Portraits: square PNG (256×256 recommended) in `portraits/`. Cutscene `background.image`: any PNG, stretched to screen.

## Audio
SFX are loaded or synthesised at startup. Designer sounds live in `sfx/*.json`; `pickup_collect.json` is timed to the 0.45 s pickup-dispersion animation. On the tablet, audio goes through Termux PulseAudio (AAudio sink, tcp:127.0.0.1:4713), started by start-pulse.sh from run-shooter.sh; AUDIO=0 skips it.
