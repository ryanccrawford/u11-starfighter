# U11 Starfighter 3D: ChatGPT Handoff

> **Current architecture direction and work status:** Read [docs/WORLD_AND_ENTITY_ARCHITECTURE.md](docs/WORLD_AND_ENTITY_ARCHITECTURE.md) and [docs/LEVEL_03_ENEMY_SHIP.md](docs/LEVEL_03_ENEMY_SHIP.md) first. This older handoff contains historical assumptions (including no device access and the old Level 3 name) and is not a statement that the planned 3D-world migration or Level 3 is complete. The current assistant has tablet ADB access; use the established isolated review and deployment workflow.

## 1. Paste-ready prompt

```text
You are joining development of U11 Starfighter 3D, a C++17 / raylib 5.5 arcade shooter that runs on
Ryan's Doogee U11 tablet (Unisoc UMS9230, Mali-G57). It runs inside a rooted Ubuntu arm64 chroot, with
Panfork Mesa for the GPU, Termux:X11 for the display and Termux PulseAudio for sound.

The game is 3D-only. Ships and planets are GLB models; terrain can be a GLB or a data-driven shader grid, all drawn with an orthographic camera.
Explosions and UI are 2D drawn over the top. There are two view modes:
- side_scroll: the player flies right.
- top_down: the player flies up the screen.
Levels, enemies, effects (VFX) and sound effects (SFX) are data-driven JSON under assets/.

Ryan will mostly create the Blender art and animation for the remaining levels and enemies. Your job:
1. Help him model, texture and export assets that drop into this engine. Follow the Blender pipeline
   section of CHATGPT_HANDOFF.md exactly: axes, scale, triangle budgets, no skeletal animation.
2. Write or patch the C++/JSON when a new asset needs engine support.
3. Give him exact copy-paste commands to push, build (make -j8 in the chroot), launch and test.

You have no device access. Ryan runs every command and pastes the output back.

Rules:
- Use Ryan's own art and parameter values. Don't "improve" his numbers unless he asks.
- Make small, reversible changes and state exactly which files change.
- Respect the FPS budgets.
- Never suggest flashing, partition writes, kernel modules, efuse, Play Protect or lock-screen changes.

Read the rest of CHATGPT_HANDOFF.md before answering.
```

---

## 2. Project overview
- **Engine:** raylib 5.5, C++17, single-threaded main loop. Windows is uncapped (`SetTargetFPS(0)`); Linux/ARM64 tablet remains capped at 90 FPS.
- **Rendering:**
  - The 2D background (stars, nebula) is drawn first.
  - Then a 3D pass draws Ryan's planets and terrain (`planet3d` / `terrain3d` layers).
  - Then 2D additive weapons and particles.
  - Then animated 3D pickup GLBs and the 3D ships (orthographic camera), drawn directly to the backbuffer.
  - The player's exhaust heat-shimmer copies only its own screen box into one scene texture.
  - Then nozzle flames, 2D VFX (explosions/impacts/shards) and the HUD.
- **Campaign** (`assets/campaign.json`) is currently `level_01`, then `level_02`. All 5 levels are reachable from LEVEL SELECT.

| # | id / name | mode | boss | length | music |
|---|---|---|---|---|---|
| 1 | level_01 "Outer Rim" | side_scroll | – | 180 s | `music/level_01.mp3` (Ryan's track, 2:59, loops) |
| 2 | level_02 "Invasion Grid" | top_down | – | 180 s | **none yet** |
| 3 | level_03 "Enemy Ship Infiltration" | side_scroll | – | 180 s | **none yet**; in-progress ship interior |
| 4 | level_04 "Fortress Approach" | top_down | – | 180 s | **none yet**; uses Ryan's terrain |
| 5 | level_05 "The Warden" | side_scroll | warden | until boss dies | none |

- **Timed levels:** non-boss levels default to `"duration": 180`. The wave script repeats; each new cycle starts 2 s after the field clears, or after 10 s with no new spawns. No new cycle starts in the last 6 s. At 180 s the level completes once the screen is clear, or at 188 s at the latest.
- **Boss levels** (`duration: 0`): after the last wave clears, the boss spawns, and killing it completes the level.

## 3. Hardware and environment
- **Tablet:** Doogee U11 (Android, rooted with KernelSU). ADB serial `U11000000000094435`.
- **Linux:** Ubuntu arm64 chroot at `/data/linux`.
  - Enter it with `/data/local/linux-enter.sh -c "<cmd>"` (run as root via `su -c`).
  - The game source lives in the chroot at `/root/shooter`, which is `/data/linux/root/shooter` from Android.
- **GPU:**
  - Panfork Mesa (a Panfrost fork for the Mali kbase driver) in `/opt/panfork/lib/aarch64-linux-gnu`.
  - It exposes desktop GL. The game forces `MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330`, so all shaders are `#version 330`.
  - DRI3 is on (`PAN_KBASE_GL_DRI3=1`).
  - The game is pinned to the big cores with `taskset -c 6,7`.
  - `gpu-max.sh` pins the GPU clock while playing.
- **Display:** Termux:X11 (`DISPLAY=:0`, 1280×700 logical).
  - `run-shooter.sh` starts `/data/local/start-x11.sh` if needed, verifies that X11 `:0` answers an actual `xwd` request, then brings the Termux:X11 activity to the front.
- **Audio:**
  - Termux PulseAudio (AAudio sink) listens on `tcp:127.0.0.1:4713`. It is started by `/data/local/start-pulse.sh`, which runs as the Termux uid 10217.
  - The game uses raylib/miniaudio with `PULSE_SERVER=tcp:127.0.0.1:4713`.
  - Health check (in the launcher): `timeout 2 pactl --server=tcp:127.0.0.1:4713 info` from the chroot. On failure it kills all uid-10217 pulseaudio processes, restarts, and retries 3 times.
  - The result goes to `/data/local/tmp/pulse-status.log`. Good output looks like `LAUNCHER pulse=ok …` and `Backend: miniaudio | PulseAudio`. `| Null` means silent.
  - Never run a bare Termux `pactl`; it autospawns a wrong daemon.
- **Files:**
  - **PC source copy:** `C:\Users\Administrator\Desktop\U11-unlock\shooter\`, edited on Windows and pushed.
  - **Launcher:** `/data/local/run-shooter.sh`. Options: `GPU=panfork|virgl|soft`, `DRI3`, `GPU_MAX`, `AUDIO=0`, `GLENV="VAR=.. VAR=.."`.
  - **Game log:** `/data/local/tmp/shooter.log`, with `FPS n` lines every second plus `LEVEL`, `MUSIC`, `BG3D`, `R3D`, `BREAKUP`, `VFX` lines.
  - **Save file:** `$HOME/.u11shooter3d_save` (chroot `/root/.u11shooter3d_save`). Contents: unlocked, step, high score, volume, fps flag, gfx mode.
  - **Termux:Widget shortcuts:** `~/.shortcuts/U11-Starfighter.sh` is the current name; `~/.shortcuts/U11-3D` is maintained as a compatibility alias. Both run the launcher.

### Commands Ryan runs (PowerShell on the PC)
```powershell
$adb="$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"; $S="U11000000000094435"
cd C:\Users\Administrator\Desktop\U11-unlock
tar czf upd.tgz shooter                      # Windows 10+ has tar; or just the changed files
& $adb -s $S push upd.tgz /data/local/tmp/upd.tgz
# unpack + touch (tablet clock runs ahead -> make would skip files) + build
& $adb -s $S shell 'su -c "cd /data/linux/root && tar xzf /data/local/tmp/upd.tgz && touch shooter/*.cpp shooter/*.hpp && /data/local/linux-enter.sh -c \"cd /root/shooter && make -j8 2>&1 | tail -20\""'
# launch to title (audio on)
& $adb -s $S shell 'su -c "sh /data/local/run-shooter.sh; sleep 6; cat /data/local/tmp/pulse-status.log"'
# launch straight into level 4 with the autopilot
& $adb -s $S shell 'su -c "GLENV=\"START_LEVEL=4 AUTOPLAY=1\" sh /data/local/run-shooter.sh"'
# log / FPS
& $adb -s $S shell 'su -c "tail -40 /data/local/tmp/shooter.log"'
```

Notes:
- Anything pushed to `/data/local/tmp` needs root to move. `/sdcard` is readable by `adb pull`.
- To stop the game: `su -c "pkill -x shooter"`.

### Default development pipeline (mandatory)

The PC and tablet must stay synchronized. For every completed change, use the single PowerShell command below from the project root rather than compiling manually on the tablet:

```powershell
.\tools\build-and-deploy.ps1
```

It performs the Windows x64 Release build and WSL Ubuntu 24.04 ARM64 cross-build concurrently from the same source tree. It deploys only after both builds succeed, archives the current source/assets plus the verified `shooter-arm64` binary to the tablet, and launches the tablet unflagged at the fresh title screen so Ryan can set Options. The native PC executable is `out\windows-ninja\shooter.exe`.

The ARM64 cross sysroot is intentionally stored under `tools/arm64-raylib`, `tools/arm64-json`, and `tools/arm64-gl`; these are tablet-matched build inputs and must not be sent to the tablet. The sync script excludes them automatically.

## 4. Build
- **Makefile:**
  - `make -j8` builds `./shooter`; `make clean` removes `build/` and the binary.
  - Libraries: `-lraylib -lGL -lm -lpthread -ldl -lrt -lX11`, plus nlohmann/json (header).
  - Sources: main game assets data background particles weapons enemies player audio cutscene profile render3d vfx.
- **Clean rebuild:** `make clean && make -j8`, or `touch *.cpp *.hpp`. The tablet clock is ahead of the PC, so extracted files can look older than their objects. Always `touch` after unpacking.
- **Building on a PC:**
  - Possible on Linux/WSL with raylib 5.5 (desktop GL 3.3) and `nlohmann-json3-dev` installed, using the same Makefile, for quick logic and shader-compile checks.
  - Run with `./shooter` from the source dir (assets are loaded from `./assets`).
  - The tablet is the real FPS target.
- `3d-test/` holds the original standalone GLB demo that set the shader and camera conventions.

## 5. Code architecture
| file | role |
|---|---|
| `main.cpp` | window (MSAA optional), render batch size, main loop, prints `FPS n` |
| `game.cpp/.hpp` | state machine: Title, LevelSelect, Options, Cutscene, Playing, LevelClear, GameOver, Victory. Level flow and timed waves, collisions, damage, drawWorld/HUD, test hooks, music per state, save |
| `render3d.cpp/.hpp` | GLB ships and animated atomic pickup GLBs, ortho camera per view, pickup dissolve/flash shader, ship matrices, nozzle flames and heat, hull contact, dents, breakup chunks, 3D planets and GLB/shader terrain |
| `player.cpp/.hpp` | flight controller: relative touch drag, keyboard, AUTOPLAY pilot |
| `weapons.cpp/.hpp` | player-selected auto/manual lasers, player-triggered homing missiles, rapid fire, bolt textures |
| `enemies.cpp/.hpp` | enemy pool, movement patterns, enemy weapons, boss phases and minions, spawn formations; per-enemy dent state |
| `vfx.cpp/.hpp` | Ryan's explosion and impact effects (JSON-driven uber-shader quads + point sprites), shards |
| `particles.cpp/.hpp` | older lightweight particles and glow sprites (smoke, glitter) |
| `audio.cpp/.hpp` | synthesized SFX, designer-JSON SFX renderer, engine/hum loops, per-level music |
| `background.cpp/.hpp` | 2D parallax layers; collects 3D layers for render3d |
| `data.cpp/.hpp` | JSON parsing for levels, enemies, players, campaign |
| `cutscene.cpp/.hpp` | JSON cutscenes (intro, chapter1_end) |
| `assets.cpp/.hpp` | texture cache, hot-reload (F5/R) |
| `view.hpp` | local ↔ screen mapping per mode |
| `config.hpp` | constants: `kEnemySize=96`, `kPlayerMaxX=0.6`, `kTopDownAspect=1.3`, etc. |
| `profile.cpp` | `PROFILE=1` per-section GPU/CPU timings |

### Coordinates and camera
- **Gameplay frame:** local `x = forward` (0..forward()), `y = lateral`.
  - Side view: forward is screen right, lateral is screen down, and the playfield is the full screen.
  - Top-down view: forward is screen up, lateral is screen right, and the playfield is a centred column of width min(W, H×1.3).
- **3D camera:** orthographic, `fovy = 14` world units across the playfield height.
  - Side: camera at (0,5,30) looking at the origin; the play plane is XY (z=0), screen-up = +Y.
  - Top-down: camera at (0,40,0) looking down, `up = -Z`; the play plane is XZ, screen-up = -Z.
  - `Render3D::toWorld()` maps local to world.
- **Ship scale:** `shipScale_ = 1.8`.
  - Side player ×0.85, overhead player ×0.5, side enemies ×0.65.

### Model orientation conventions (must match for new GLBs)
- **Player `red_fighter.glb`:** nose **-X**, wings ±Z, up +Y.
  - Side: rotated Y by π (nose → screen right). Roll is about X, clamped ±0.55 rad; nose pitch is about Z, clamped ±0.35.
  - Overhead: roll about X (±0.55) first, then a small nose yaw (±0.17), then `RotateY(-π/2)` so the nose points screen-up.
  - The TRS order is model-space attitude, then view basis, then scale, then translate (raylib `MatrixMultiply(A,B)` applies A first).
- **Enemy `blue_racer*.glb`:** nose **+Z**, wings ±X, up +Y.
  - Model-space bank is about Z (±0.4) and pitch about X (±0.3).
  - Side: `RotateY(-π/2)` so the nose faces left, toward the player.
  - Overhead: no basis turn (nose +Z = screen-down).
- **Nozzle anchors are hard-coded in model space** (there are no empties):
  - Player: (0.735, -0.13, ±0.07).
  - Enemy tail: (±0.07, -0.02, -0.95).
  - A new model with different proportions needs these constants updated (`nozzleModel`/`enemyNozzle` in render3d.cpp).
- Bank and turn limits are deliberately small. Ryan asked for subtle attitude changes, so keep them small.

### Touch controls
- Relative virtual joystick: touchdown is neutral wherever the thumb lands; displacement controls
  direction and speed, holding it continues to a ship boundary, and retouching never jumps the ship.
- The player is clamped to 60% of forward and away from the edges.
- Options selects Auto Fire or Manual Fire for primary lasers. Manual Fire uses the lower-right FIRE touch button; missiles use the adjacent SECONDARY button and do not auto-launch.

### Damage: dents and breakup (`assets/fx/enemy_breakup.json`)
- **Dents:**
  - Each enemy holds up to 8 dents (model space xyz + depth). The dent vertex shader pushes vertices in along -normal with a smooth bowl.
  - It also tilts the normals and adds a dark crease, a hot core and a bright lip.
  - Hits are converted to model space and moved to the camera-facing hull. `hullContact()` projects the model's bounding box so 2D impact effects sit on the drawn hull.
  - Dents need vertex density: around 4k tris works.
- **Breakup:** at load the ENEMY model's triangles are k-means split into `chunks` (6) pieces.
  - This assumes one enemy model, a closed hull and the blue_racer orientation.
  - On death each chunk flies outward, spins and shrinks, with a staggered small explosion (scale 0.55, gain 0.4) and 10–20 pooled 2D shards (max 500). At most 4 breakups run at once.

### VFX / SFX JSON
- **`assets/fx/enemy_explosion.json`, `impact_damage.json`:** Ryan's Three.js effect parameters (colours as linear hex, gains, radii, counts, durations). The engine adds `shipBlastMul`, `shipFlashMul`, `pointScale` and `maxSparks`.
- **`assets/sfx/*.json`:** designer synth format: `{name, duration, loop, volume, layers:[{type osc|noise, wave, filter, gain, start, freq, endFreq, attack, decay, sustain, release, voices, detune, fmRatio, fmDepth, pan, cutoff, resonance, lfoRate, lfoDepth, drive, ...}]}`.
  - Player bolts use Ryan's exact `laser_fire.wav` at volume 0.8. Each successfully spawned bolt owns one of 64 overlapping voices, so the 0.52 s sound normally finishes. A collision stops only that bolt's voice before `laser_damage` takes over; generation-checked tokens prevent an old bolt from stopping a reused voice.
  - `laser_damage` plays on player-laser hits, max 12/s, ±5% pitch.
  - `engine_noise` loops during play.
  - `background_hum` loops during the play session and drops to 50% under music.
  - `enemy_explosion` and `missile_launch` (0.5 s cooldown).
  - `pickup_collect` is the required pickup sound: a 0.45 s magnetic capture, rising energy tone and crystalline vapor-dispersion tail synchronized to the full pickup dissolve. Four simultaneous pickup voices retain mix headroom; an invalid asset is logged and not replaced by a fallback effect.
  - `DUMPSFX=<dir>` writes each rendered SFX to WAV.
- **Music:** the level field `"music": "music/<file>.mp3"`, path relative to `assets/`.
  - It streams and loops, fades in over 1 s and out over 1.5 s on level clear or exit, at mix 0.55. An empty value means silence.
  - MP3/OGG/WAV all work (raylib).

## 6. Level / enemy JSON reference
- **Level** (`assets/levels/level_XX.json`): `name, mode (side_scroll|top_down), player, boss, intro, outro, music, duration, clear_color, difficulty{hp,fire_rate,speed}, background[], waves[]`.
- **Background layer types:**
  - `stars`, `nebula`, `asteroids`, `texture` (PNG), `planet` (old 2D circle, still used on the title screen).
  - `planet3d`: `{model, color, x, y, size, speed, spin}`. `size` 1.0 ≈ 150 px radius; `x`/`y` are 0..1 of the playfield.
  - `terrain3d`: `{model, color (tint), speed}`. `model` can be a legacy GLB or a `shader_terrain` JSON descriptor.
- **Wave:** `{time, enemy, count, formation line|column|v|row|random, y, spacing, amplitude, frequency}`.
- **Enemy** (`assets/enemies/<id>.json`):
  - `id, name, scale, tint, hp, speed, score, boss`.
  - `movement{pattern straight|sine|dive|circle|path|invaders|boss, amplitude, frequency, points}`.
  - `weapon{type none|aimed|spread|laser|missile|spiral|sweep|needle, interval, shots, speed, size, damage, telegraph}` (`speed`/`size` multiply the bullet, `damage` 0 = default 12, `telegraph` = seconds the model's glow charges before each shot).
  - `model`: optional GLB under `assets/models/` (e.g. `enemies/scarab/scarab_lod.glb`). Omit it to keep the default `blue_racer_lod4k.glb`.
  - `engineTint`: optional `[r, g, b]` colour for this enemy's engine flames, usually the ship's secondary colour brightened to full value. Omit it for the original blue-white flame. Enemies only; the player's flame never changes.
  - `legacyTint`: unused by the code. It keeps an enemy's old `tint` while it temporarily uses the Scarab model (copy it back to `tint` to restore).
  - `drops{chance, table}`, and `phases[]` for bosses.
  - Enemies without `model` render with the default model (`ENEMY_MODEL` or `blue_racer_lod4k.glb`), tinted by `tint`. The on-screen model size does not follow `scale` (it only sets the hit radius). The `sprite` key is legacy 2D.
  - See `docs/ENEMY_DESIGN.md` for the alien-robot enemy fleet (design language, Blender pipeline, engine hooks).
- **Player** (`assets/players/red_fighter.json`): `controller flight, size 110, speed 650, hp 100`.
- Full details are in `docs/LEVEL_FORMAT.md`, `docs/ASSET_SPEC.md` and `docs/STORY_TEMPLATE.md`.

## 7. BLENDER ASSET PIPELINE (most important)
### Export settings (glTF 2.0)
- **Format:** **GLB** (glTF Binary), with textures embedded.
- **Transform:** "+Y Up" ON (Blender's default, so glTF Y is up). In Blender, Z-up gets converted to glTF Y-up.
- **Apply transforms:** apply rotation and scale (Ctrl+A) before export, and leave the object at the origin. The engine centres nothing; the origin is the pivot.
- **Units:** 1 Blender unit is 1 glTF unit.
- **Nose axis in glTF space:**
  - Player: about 1.6 units long, nose at **-X**.
  - Enemy: about 2.0 units long (bounding box ~1.6×0.8×2.0), nose at **+Z**.
  - In Blender terms (Z-up): enemy nose = Blender **-Y**, player nose = Blender **-X**. Verify in-game (below).
- **Include:** selected mesh, UVs, normals, materials. Animations are not used (see below). Skip cameras and lights.
- **Geometry:** one mesh object (or a few), triangulated on export, smooth or auto-smooth normals, closed hull for enemies (the breakup and dents assume it).

### Triangle budgets (Mali-G57 at ~60 FPS)
- **Enemy:** about 4k tris (current `blue_racer_lod4k.glb`, 4,000). 6k is acceptable; 40k cost about 9 FPS.
- **Player:** `red_fighter.glb` is **40k = known FPS cost** (hiding it gains about 5 FPS on L1 and 13 on L2). Target 4–6k.
- **Planets:** about 3.7k each.
- **Terrain:** legacy GLB target about 5k per tile. Level 4 intentionally uses Ryan's 128-segment shader terrain: 32,768 ground triangles plus 2 water triangles per visible tile.
- **Bosses:** up to about 8k (only one on screen).

### Materials and textures
- **Used:** the base colour texture (`texture0`) × base colour factor, with one directional light (lambert, 0.38 ambient).
- **Ignored:** vertex colours (the lit shader doesn't read them), normal maps, metallic/roughness, emission node.
- **Enemy glow mask:** enemy textures can store glow in the base-colour **alpha**: alpha 1 = normal lit hull, lower alpha = self-lit glow (glow amount = 1 − alpha, about 0.15 alpha for full glow). The ship is still drawn opaque. Old opaque textures have no glow. `tools/blender_enemies/pipeline.py` bakes this automatically from emissive materials.
- Bake detail into the base colour. PNG/JPG textures of 1024–2048 px.
- Planets and legacy GLB terrain without textures are shaded by relief and height. Level 4's shader terrain generates its vertex height, three-colour surface and translucent water directly from `terrain_fortress.json`.

### Animation
- **Pickups** play one skinned glTF clip (`Pickup_Spin`, CPU skinning, one shared pose per type).
- **Enemies with a `model`** play skinned glTF clips on the GPU (raylib 5.5 `UpdateModelAnimationBones` + the `DENT_SKIN_VS` shader), so every enemy has its own pose for almost no CPU:
  - `Idle` loops on the enemy's own clock.
  - `Fire` plays once after each shot. Only bones whose names start with `W_` take the Fire pose; all other bones stay on Idle, so the clips never fight.
  - Rules: one armature at the origin, rigid parts (each vertex 100% on one bone), max 16 bones (8 is plenty), `W_` bones must not move in `Idle`, and `Fire` must start and end at the rest pose.
  - Optional `fire_sustain: [a, b]` in the sidecar JSON loops Fire between a and b seconds while a held weapon (beam/flamethrower, future) is active.
- Banking, pitch and nose turn stay procedural on top of the clips. The player ship is not animated.

### Adding things
- **New enemy look:**
  - Put the files in `assets/models/enemies/<id>/`: `<id>_lod.glb` (game, 4-6k tris), `<id>_lod.json` (sidecar), `<id>_high.glb`, `<id>.blend`, `textures/`, `renders/`.
  - Sidecar `<id>_lod.json` (glTF model space, nose +Z): `nozzles` (2 points for the engine flames), `muzzle` (where shots leave), `idle`/`fire` clip names, `fire_sustain`.
  - Add `"model": "enemies/<id>/<id>_lod.glb"` to `assets/enemies/<id>.json`. Render3D caches each model at startup and builds its own breakup chunks, dent bounding box and nozzles, so dents and breakup work for every model automatically.
  - Review any enemy on any level with `ENEMY_TEST=<id>` (every wave spawns that type).
  - `ENEMY_MODEL=<file>.glb` still swaps the default model for enemies that have no `model`.
  - Procedural source for the Blender-built fleet: `tools/blender_enemies/` (see `docs/ENEMY_DESIGN.md`).
- **New level:**
  - Copy a `level_XX.json`, then set `mode`, `background`, `waves`, `music` and `duration` (or `boss`).
  - Add it to `assets/campaign.json` steps to put it in the story.
- **New terrain:**
  - Legacy: a GLB heightfield about 20×20 units in XZ (Y up), with the existing fixed terrain palette/water handling.
  - Shader terrain: use `assets/models/terrain_fortress.json` as the schema reference. It reproduces Ryan's preserved `assets/models/src/terrain-mesh.js` algorithm on the GPU: seeded gradient noise, craters, height colours and translucent water.
  - Tiles are mirrored, so edges need not match.
  - Add a `{ "type": "terrain3d", "model": "<file>.glb|<shader descriptor>.json", "speed": 60, "color": [r,g,b] }` layer.
- **New planet:**
  - A sphere (displaced for relief is fine), any radius. It is auto-scaled to `size`.
  - Add `{ "type": "planet3d", "model": "<file>.glb", "color": [r,g,b], "x": 0.7, "y": 0.35, "size": 1.0, "speed": 4, "spin": 0.05 }`.
- **New song:** drop the MP3 in `assets/music/` and set `"music": "music/<file>.mp3"` in the level.
- **New sound:** add a designer JSON in `assets/sfx/`. Hooking it to an event needs a line in `audio.cpp` (see `hitLaser`/`missileLaunch`).
- **Reduce triangles** (on the PC with Node.js):
  - `npx gltfpack -i in.glb -o out.glb -noq -si 0.1` (ratio; `-noq` is required because raylib doesn't read quantized meshes).
  - Add `-slb` for terrain tiles (keeps the borders), and `-km` to keep materials.
  - gltfpack also reads `.obj`.
- **Verify orientation in-game:**
  - `START_LEVEL=1 AUTOPLAY=1` (side) and `START_LEVEL=2 AUTOPLAY=1` (overhead).
  - The log prints `R3D enemy model … tris=` and `BG3D model … bbox=`.
  - `BANK_TEST=L|R|NE|NW` forces player banking to check the roll axis.
  - Enemies must face the player: left in side view, down in overhead.

## 8. Testing
**Environment variables:**
- **Starting and skipping:**
  - `START_LEVEL=n` jumps straight into level n (free play).
  - `AUTOPLAY=1` runs the autopilot and skips intros.
  - `NOINTRO=1` skips intros.
  - `GODMODE=1` makes the player invulnerable.
  - `UNLOCK_ALL=1` unlocks every level.
- **Audio:**
  - `NOAUDIO=1` disables audio. Any value counts, even an empty one: setting `NOAUDIO=` also disables it, so leave it unset.
  - `DUMPSFX=dir` writes every SFX to WAV.
- **Effects and VFX:**
  - `EXPLO_TEST=1` detonates an explosion and fires impacts at an on-screen enemy; `EXPLO_FREEZE=t` freezes the whole game t s after it (for screenshots).
  - `DENT_TEST=1` keeps denting every enemy; `BREAK_TEST=1` breaks up a dented hull at screen centre every 1.8 s.
  - `VFXDBG` bits: 1 no points, 2 no smoke, 4 no quads, 8 no impacts, 16 no shards.
  - `LOWFX=1` low effects (only with `RENDER_SCALE`). `NOHEAT`, `NOFLAME`.
- **Rendering and models:**
  - `ENEMY_MODEL=blue_racer.glb|blue_racer_lod6k.glb` swaps the default enemy model.
  - `ENEMY_TEST=<enemy id>` makes every wave spawn that enemy (e.g. `ENEMY_TEST=scarab`).
  - `SHIPRT=1` uses the old full-scene texture path, for A/B tests.
  - `HIDEPLAYER3D=1` hides the player model (cost probe).
  - `R3D_SCALE` ship scale.
  - `RENDER_SCALE=0.4..1` internal render resolution.
  - `MSAA=1`.
- **Player and HUD:**
  - `BANK_TEST`, `BANK_LOG` player banking test and log.
  - `HUD_HP=%`, `HUD_SHIELD=n` HUD tests.
- **Perf probes:**
  - `DBG` mask (1 bg, 2 smoke/VFX, 4 weapons, 8 HUD, 16 no scissor).
  - `BGSKIP` 2D layer skip mask.
  - `PROFILE=1|2` per-section timings.
  - `BATCHBUFS`, `DRAWTEST`, `FILLTEST`/`FILLMODE`.

**FPS:**
- Run with `GLENV="START_LEVEL=1 AUTOPLAY=1 NOAUDIO=1"` for 25 s, then average the last 10 `FPS` lines in `shooter.log`:
  - ``grep '^FPS' /data/local/tmp/shooter.log | tail -10 | awk '{s+=$2} END{print s/NR}'``
- The in-game FPS overlay is toggled in Options.

**Screenshots:**
- If the tablet is locked, use X capture instead of the touchscreen:
  - `su -c '/data/local/linux-enter.sh -c "DISPLAY=:0 xwd -root -silent > /tmp/s.xwd"'` (lands at `/data/linux/tmp/s.xwd`).
  - Copy it to `/sdcard`, `adb pull`, convert with `ffmpeg -i s.xwd s.png`.

## 9. Current status (Nov 7 2026)
**FPS** (AUTOPLAY average, 25 s):
- L1 (new planets): 52.8 (min 45)
- L2: 74.0 (min 29)
- L4 (terrain): 64.4 (min 46)

**Known issues and to-do:**
- The player model is 40k tris, a known FPS cost. A 4–6k version is wanted.
- FPS items 3–5 from the earlier performance plan are not done (left for Ryan to approve).
- Shards are flat 2D triangles, not 3D.
- **Temporary (Nov 11, Ryan's call): every enemy uses the Scarab model.** grunt, diver, gunship, invader, looper, weaver and the Warden boss all have `"model": "enemies/scarab/scarab_lod.glb"` and the violet `engineTint` [214, 128, 255]. Their stats, movement and weapons are unchanged. Regular enemies' `tint` is set to white (the old value is in `legacyTint`); the Warden keeps its red tint as the boss marker. Each enemy will get its own model as the other 4 designs are finished. To undo one, remove `model`/`engineTint` and copy `legacyTint` back to `tint`.
- Enemy models are per type now (Nov 11): breakup chunks, dents and nozzles come from each model. The first alien-robot enemy, the **Scarab Scout Drone**, is built and wired in (`assets/enemies/scarab.json`) but is **not in any level's waves yet**, and it hasn't been tested on the tablet.
- **Standing behaviours (Nov 11, Ryan):**
  - *Shield, overhead view:* the 3D shield sphere is bigger from above (`shieldTopRadius_`, about 1.97 vs 1.03 model units in the side view). It keeps the side view's on-screen gap above/below the hull as the gap beside the hull. It's visual only; collision (`kShieldRadiusSide/Top`) is unchanged. Tune with `Render3D::kShieldTopGapMatch`.
  - *Edge throttle:* holding the ship INTO the forward stop edge scrolls the world 15% faster (`kEdgeBoostMul`) and lengthens/brightens the player flame 30%. Holding it into the back edge scrolls 15% slower (`kEdgeSlowMul`) with a 30% shorter, dimmer flame. It eases in and out (`kEdgeBlendRate`). It affects background layers and planets, enemy forward travel (straight/sine/dive/circle patterns; path/invaders/boss unaffected) and pickup drift. Enemy flames are unchanged. `Player::edgeHold` and `Game::edgeScroll` carry it; `EDGE_LOG=1` prints it. The Level 3 corridor scroll (`drawShipCorridor(levelTime)`) does not follow it yet.
  - *Planets* (side view) spin so their face turns WITH the scroll; the old sign slid the surface and clouds against it.
  - *Enemy wings never flap.* Fins are rigid; only sensors, mandibles and antennas idle-animate.
- A small enemy-fire "blip" glitch is still open.
- Levels 2–4 need songs.
- Ryan's planet OBJs had no textures, so they are relief-shaded. The strong swirl bands come from the mesh relief.
- Terrain collision (the `terrain` field) is not implemented. Four more alien-robot enemies are planned (gunship, interceptor, beam frigate, flamethrower brute); see `docs/ENEMY_DESIGN.md`.

## 10. Rules and conventions
- Use Ryan's own art and parameters (VFX/SFX JSON values, colours, sizes). Keep his values unless he asks.
- Attitude changes (bank, pitch, nose) stay small.
- Keep the original assets next to any reduced or converted versions (e.g. `blue_racer.glb` + `_lod4k`; OBJ sources in `assets/models/src`).
- Prefer data (JSON) over code changes, and log new features to `shooter.log`.
- **Safety (device):** no flashing or partition writes, no kernel or kbase module changes, no efuse, no KernelSU grant changes, no Play Protect changes, no lock-screen bypass. Don't change Android settings or volumes beyond what the game needs.
- No secrets in the repo or in prompts.
