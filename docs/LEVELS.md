# Campaign Levels & Testing Guide

This document describes the campaign levels in U11 Starfighter according to their campaign execution order (`START_STEP`), their in-game mechanics from the source code, and the environment variables used for testing and validation.

---

## 1. Campaign Progression Order

The campaign sequence is defined in `assets/campaign.json`. Testing or starting directly at a step uses the zero-based index `START_STEP=0..7`.

| Step (`START_STEP`) | Level ID | Level Name | Mode | Duration | Description |
|---|---|---|---|---|---|
| **0** | `level_01` | Outer Rim | Side-Scroll | 180 s | Space orbit over Nova planet; introductory combat and shield mechanics. |
| **1** | `level_02` | Invasion Grid | Top-Down | 180 s | Overhead space sector featuring marching invader formations and agile escorts. |
| **2** | `level_03` | Enemy Ship Infiltration | Side-Scroll | 180 s | Interior flight corridor with moving security bulkheads and articulated turrets. |
| **3** | `level_06` | Planetfall | Top-Down (Transition) | 180 s | High-altitude atmospheric entry; camera orbit and descent through volumetric clouds. |
| **4** | `level_04` | Fortress Approach | Top-Down | 180 s | Low-altitude terrain skimming; ground guns; Flamethrower weapon unlock. |
| **5** | `level_07` | Liftoff | Top-Down | 180 s | Launchpad takeoff, vertical climb to orbit, ground drop; Plasma Lance unlock. |
| **6** | `level_05` | The Warden | Side-Scroll | Boss Fight | High-orbit showdown against the flagship against the sun; all weapons unlocked. |
| **7** | `level_08` | Moon Breakup | Side-Scroll / 3D Trench | 400 s | Orbital approach followed by full 3D Moon base trench run, bombing run, and escape. |

---

## 2. Level Details from Source Code

### Step 0: `level_01` — Outer Rim
- **Mode:** Side-scroll (`Mode::SideScroll`).
- **Music:** `music/level_01.mp3`.
- **Setting & Visuals:** Orbit over planet Nova. Features a 3D rotating planetary sphere (`nova_planet_surface.glb`) with an independent cloud shell (`nova_planet_clouds.glb`), purple nebulae, and multi-layer parallax stars.
- **Gameplay:**
  - Introduces flight, banking pitch, and shield defense mechanics. The player begins with a full 60 HP shield.
  - Spawns waves of grunts, weavers, scarabs, hornets, gunships, and diving enemies.
  - Plays the opening story cutscene (`intro.json`).

### Step 1: `level_02` — Invasion Grid
- **Mode:** Top-down (`Mode::TopDown`).
- **Music:** `music/level_02.ogg`.
- **Setting & Visuals:** Centered vertical playfield column with HUD telemetry bars on screen flanks. Deep space backdrop with cyan nebula.
- **Gameplay:**
  - Wave design features classic marching enemy grids (`enemy: "invader"`). Invaders step sideways, drop downward at screen boundaries, and accelerate as members are destroyed.
  - Escort waves of agile Scarab drones, Weavers, and diving interceptors break up grid patterns.

### Step 2: `level_03` — Enemy Ship Infiltration
- **Mode:** Side-scroll (`Mode::SideScroll`).
- **Music:** `music/level_03.ogg`.
- **Setting & Visuals:** The interior of a massive alien warship. Framed top and bottom by 3D metal corridor structures (`drawShipCorridor`).
- **Gameplay & Hazards:**
  - **Bulkhead Doors:** Animated security bulkheads (`level3Door`, [bulkhead.cpp](file:///C:/Users/Administrator/Desktop/u11-docs/bulkhead.cpp)) with sliding vertical leaves. Doors push the player back with the corridor scroll and crush the ship if pinned against screen boundaries. Front pillars fade dynamically as the ship approaches.
  - **Articulated Turrets:** Ceiling and floor-mounted rotating turrets ([turret.hpp](file:///C:/Users/Administrator/Desktop/u11-docs/turret.hpp)) track the player, aim, and fire needle bolts. Destroyed turrets erupt into fire and leave smoking mechanical wrecks.
  - Waves feature pre-scripted waypoint-following loopers, heavy gunships, and hornets.

### Step 3: `level_06` — Planetfall
- **Mode:** Top-down (`Mode::TopDown`).
- **Setting & Visuals:** Re-entry descent into the enemy planet's atmosphere (`approach: true`, `approach_end: 122`).
- **Gameplay & Transitions:**
  - Over the first 122 seconds, a dynamic chase camera orbits from a side view down into a top-down perspective.
  - Atmospheric entry visuals feature plasma re-entry glow, heat shimmers, aerodynamic turbulence, and thick volumetric cloud layers (`VolFog`).
  - Audio dynamically mixes atmospheric wind rush and re-entry roar while ducking the music stream ([flight_audio.cpp](file:///C:/Users/Administrator/Desktop/u11-docs/flight_audio.cpp)).
  - At 122 seconds, the ship punches through the cloud deck onto the procedural chunked terrain ([planet_terrain.cpp](file:///C:/Users/Administrator/Desktop/u11-docs/planet_terrain.cpp)).
  - Level completion sets `planetCarry = true`, seamlessly preserving the terrain position into Step 4.

### Step 4: `level_04` — Fortress Approach
- **Mode:** Top-down (`Mode::TopDown`).
- **Music:** `music/level_04.ogg`.
- **Setting & Visuals:** Low-altitude flight skimming the planetary terrain (`approach: true`, `approach_start: 122`, `ground_guns: true`).
- **Gameplay:**
  - Seamlessly carries over the terrain scroll offset from Planetfall without popping or resetting.
  - **Ground Guns:** Level 3 turrets mounted on flattened pads of the terrain chunk grid scroll with the planet surface and fire upward at the player.
  - **Weapon Unlock:** Guaranteed Flamethrower unlock appears at 16 seconds (or is granted upon level completion).
  - Heavy enemy waves consisting of invader grids, weavers, and scarabs.

### Step 5: `level_07` — Liftoff
- **Mode:** Top-down (`Mode::TopDown`).
- **Music:** `music/level_07.ogg`.
- **Setting & Visuals:** Planetary surface launchpad takeoff and vertical escape burn (`approach: true`, `liftoff: true`, `ground_guns: true`).
- **Gameplay:**
  - Begins grounded on a surface launch pad. Engines spool up, billowing screen-space exhaust smoke puffs.
  - The ship pitches nose-up and climbs vertically. The terrain surface drops away into the distance (`setGroundDrop`), shrinking beneath the ship.
  - High-energy escape burn puts the shield offline until orbit is achieved.
  - **Weapon Unlock:** Guaranteed Plasma Lance unlock appears at 22 seconds (or is granted upon level completion).

### Step 6: `level_05` — The Warden
- **Mode:** Side-scroll (`Mode::SideScroll`).
- **Setting & Visuals:** High orbit facing directly into the blazing sun with procedural solar flares, corona shader passes, and nebula haze (`WardenSky`).
- **Gameplay & Boss Fight:**
  - All primary weapons (Laser, Flamethrower, Plasma Lance) come online for this encounter.
  - **The Warden Boss (`boss: "warden"`):**
    - Multi-phase boss battle with telegraphed charge-ups, rotating spiral laser volleys, sweeping beam attacks, minion spawns, and rest-zone defensive windows.
    - Cinematic multi-stage destruction: hit-stop slow motion, chain explosions, inside-out hull breakup into tumbling 3D debris, fireball expansions, and shockwave rings.
  - Upon victory, sky and solar coordinates are passed smoothly to Step 7.

### Step 7: `level_08` — Moon Breakup (Moon Base Trench Run)
- **Mode:** Side-scroll into 3D Trench Run.
- **Music:** `music/level_08.ogg`.
- **Duration:** 400 s. Outro: `chapter1_end`.
- **Phased Sequence ([moon_level.cpp](file:///C:/Users/Administrator/Desktop/u11-docs/moon_level.cpp) & [trench_run.cpp](file:///C:/Users/Administrator/Desktop/u11-docs/trench_run.cpp)):**
  1. **Orbit Approach (0–15.5 s):** The camera turns from the Warden's solar angle toward the cratered moon. The ship descends toward the lunar base plating.
  2. **Skim & Trench Dive (7–15.5 s):** The ship skims the greebled hull and dives into a narrow, twisting 3D trench.
  3. **Trench Run:** Flight down an enclosed spline path with low/high gantries, wall-mounted turrets, red/blue alert lamps, and pursuing TIE-style chaser craft trailing and overtaking the player.
  4. **The Bombing Run:** At 11,200 distance units along the path, the player delivers the Super Drop Bomb into the station's thermal exhaust port.
  5. **Escape & Finale:** The player pulls up out of the trench with a wall of fire racing behind. The moon shatters in a massive explosion. (Failing to exit before the blast arrives results in blast death).

---

## 3. Testing Environment Variables

The engine exposes environment variables for automated testing, benchmark captures, and component isolation.

### Campaign & Boot Control
- `START_STEP=n`: Boot directly into campaign step `n` (0..7).
- `START_LEVEL=n`: Boot directly into level index `n` (1-based, e.g. `1` for Outer Rim) in free-play mode.
- `UNLOCK_ALL=1`: Unlocks all stages on the Level Select screen.
- `NOINTRO=1`: Bypasses intro cutscenes.
- `AUTOPLAY=1`: Activates the automated pilot AI; flies, aims, and bypasses damage (godmode).
- `GODMODE=1`: Player ignores all damage.
- `FIXED_DT=f`: Sets a fixed simulation delta time (e.g. `0.016666`) for deterministic benchmarking and captures.
- `TITLE_AUTOSTART=s`: Automatically starts the game after `s` seconds on the title screen.
- `TITLE_CONTINUE=1`: In autostart mode, resumes saved campaign if `savedStep > 0`.
- `PAUSE_TEST=t`: Triggers an automatic pause at level time `t`.
- `PAUSE_TEST_EXIT=1`: Exits to the main menu after pausing during pause tests.

### Window & Rendering Diagnostics
- `HIDDEN_WINDOW=1`: Initializes the window with `FLAG_WINDOW_HIDDEN`. Tests and screen captures run without displaying a visible desktop window.
- `MSAA=1`: Requests 4x MSAA hint (`FLAG_MSAA_4X_HINT`; disabled by default on Mali).
- `BATCHBUFS=n`: Overrides the number of rotating rlgl render batch vertex buffers (default 8).
- `PROFILE=1|2`: Enables performance profiling (1 = CPU+GPU sync, 2 = CPU only).
- `BGSKIP=mask`: Bitmask to skip background layers for performance bisecting.
- `VFXDBG=mask`: Bitmask to suppress visual effects (1: points, 2: smoke, 4: shader quads, 8: impacts).
- `NOHEAT=1`: Disables engine heat-shimmer refraction.
- `NOFLAME=1`: Disables engine exhaust flame billboards.
- `NOSHIPLIGHTS=1`: Disables ship-mounted navigation/strobe lights.
- `SHIPLIGHT_STROBE=1`: Holds ship navigation strobes permanently on.
- `R3D_SCALE=f`: Overrides 3D ship rendering scale factor.
- `HIDEPLAYER3D=1`: Hides the player's 3D mesh.

### Audio Testing
- `AUDIO=0` / `NOAUDIO=1`: Disables sound device initialization and audio playback.
- `PULSE_SERVER=tcp:...`: Specifies target PulseAudio server (e.g. `tcp:127.0.0.1:4713`).
- `AUDIO_TAP=<path>`: Dumps raw audio PCM streams to a binary file.
- `DUMPSFX=<dir>`: Exports procedurally generated sound effects to directory as WAV files.
- `SFX_LOG=1`: Logs sound playback triggers and parameters to stderr.

### Weapons & HUD Testing
- `OWN_WEAPONS=mask`: Overrides owned primary weapons bitmask (1: Laser, 2: Flamethrower, 4: Plasma Lance; `7` = all).
- `WEAPON=id`: Force-selects active primary weapon (0: Laser, 1: Flamethrower, 2: Plasma Lance).
- `AUTOWEAPON=interval`: Automatically cycles primary weapons every `interval` seconds during captures.
- `HOLD_FIRE=1`: Forces the player to hold fire.
- `HUD_HP=pct`: Sets player HP to `pct` percent (1..100) for UI verification.
- `HUD_SHIELD=val`: Sets player shield directly (0..60).

### Hazards & Boss Testing (Levels 3, 5, 6, 7)
- `BULKHEAD_REVIEW=1`: Fast-forwards Level 3 directly to bulkhead door encounter.
- `BULKHEAD_REVIEW_OPEN=1`: Tests open bulkhead door state.
- `BULKHEAD_REVIEW_NEAR=1`: Positions player near bulkhead door.
- `BULKHEAD_REVIEW_GAP=1`: Aligns player with open bulkhead door gap.
- `BULKHEAD_COLLISION_DEBUG=1`: Logs bulkhead pillar and leaf collision boundaries.
- `TURRET_REVIEW=1`: Fast-forwards Level 3 to ceiling turret encounter.
- `TURRET_REVIEW_SHOOT=1`: Positions player in turret firing line.
- `TURRET_REVIEW_MISSILE=1`: Launches missiles at turret during review.
- `BOSS_LOG=1`: Logs Warden attack telegraph masks, phases, and frame timing.
- `HAZARD_LOG=1`: Logs hazard spawn resolution.
- `GUN_LOG=1`: Logs ground gun aiming and firing events.
- `L6_TIME=s`: Fast-forwards Planetfall approach timeline to `s` seconds.
- `TERRAIN_LOG=1`: Logs procedural terrain chunk generation and elevation queries.

### Moon Base Trench Run Testing (Level 8)
- `MOON_DBG=val`: Debugs moon surface shader parameters.
- `MOON_SUN_INFRAME=1`: Forces sun disc into the moon camera frame.
- `TRENCH_CAM=id`: Forces specific trench run camera angle.
- `TRENCH_CHASER=<name>`: Overrides trench chaser enemy model.
- `TRENCH_TEST_LOG=1`: Logs 4 Hz trench flight data, weapon hits, and damage events.
- `TRENCH_TEST_SCRIPT=1`: Executes automated trench test maneuvers.
- `TRENCH_TEST_NOGANTRY=1`: Suppresses gantry generation to test clean banking.
- `TRENCH_TEST_MISSILES=1`: Forces missile testing inside trench.
- `TRENCH_TEST_EARLYDIVE=1`: Forces early dive into trench.
- `TRENCH_TEST_NOPULLUP=1`: Suppresses pull-up at trench finale.
- `TRENCH_TEST_HIGH="t0,t1"`: Forces flight above trench rim during time window.
- `TRENCH_SHOTS=<dir>`: Saves trench run screen captures to directory.
- `TRENCH_FRAMES=<dir>`: Saves continuous frame sequences.
- `TRENCH_SNAP_AT=t1,t2,...`: Captures full snapshots at exact trench time stamps.
- `INVERT_TEST_AT="t0,t1,..."`: Simulates pressing `X` to invert pitch at specific level times.
