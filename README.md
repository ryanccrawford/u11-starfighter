# U11 Starfighter

U11 Starfighter is a 3D arcade space rail shooter built with C++17 and raylib 5.5.

The game targets two distinct environments:
- **Windows Desktop:** x64 native build using MSVC and CMake/Ninja. Runs with an uncapped target frame rate (`SetTargetFPS(0)`).
- **Doogee U11 Tablet:** Unisoc T606 (UMS9230) SoC with Mali-G57 GPU, running an Ubuntu ARM64 chroot on Android with Panfork Mesa (desktop OpenGL 3.3 / GLSL 330 via DRI3), Termux:X11 for display (`DISPLAY=:0`), and Termux PulseAudio over TCP (`PULSE_SERVER=tcp:127.0.0.1:4713`) capped at 90 FPS.

The game features real-time 3D GLB ship and hazard models, procedural chunked planetary terrain, dynamic vertex-shader hull dents and breakup debris, 3 primary weapons (Laser, Flamethrower, Plasma Lance) plus secondary homing missiles, and an 8-stage campaign alternating between side-scrolling and top-down perspectives, concluding with a full 3D planetary descent and moon base trench run.

---

## Game Assets

Game asset binaries (3D models, textures, audio WAV/MP3/OGG files, and fonts) are stored outside the git repository to keep repository size minimal.

- See [ASSETS.md](ASSETS.md) for complete instructions on obtaining, adding, and updating assets.
- Asset synchronization is managed via `tools/assets-sync.ps1`, which tracks hashes using [assets.manifest](assets.manifest).

---

## Building

### Windows (x64)

Prerequisites:
- Visual Studio 2022 (MSVC Build Tools x64)
- CMake 3.30+
- Ninja

To build using the automated PowerShell script:
```powershell
.\tools\build-windows.ps1 -Configuration Release
```
This configures and builds into `out/windows-ninja/shooter.exe` and copies the `assets/` directory adjacent to the binary.

Manual CMake build:
```cmd
cmake -S . -B out/windows-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/windows-ninja --parallel
```

### Tablet (ARM64 Cross-Build via WSL)

Prerequisites:
- Ubuntu 24.04 WSL instance with `g++-aarch64-linux-gnu` installed (`sudo apt install g++-aarch64-linux-gnu`).
- Target-matched headers and static libraries are bundled in the repository under `tools/arm64-raylib`, `tools/arm64-json`, and `tools/arm64-gl`.

To cross-compile the ARM64 Linux binary from WSL:
```bash
./tools/build-arm64-wsl.sh
```
This runs `make -j shooter-arm64` using [Makefile](Makefile) to produce the ARM64 executable `shooter-arm64`.

> [!NOTE]
> *TODO: confirm* whether `trench_run.cpp` should be added to `SRC` in [Makefile](Makefile). It is present in [CMakeLists.txt](CMakeLists.txt) `SHOOTER_SOURCES` but currently omitted from `Makefile`, which would cause missing symbol errors when linking `shooter-arm64`.

### Simultaneous Build & Deploy

The repository includes `tools/build-and-deploy.ps1`, which compiles both Windows x64 and WSL ARM64 binaries in parallel. When explicitly requested by the owner, it synchronizes the game bundle to the connected Doogee U11 tablet over ADB via `tools/sync-tablet.ps1`.

---

## Controls

### Keyboard & Mouse

| Action | Control | Notes |
|---|---|---|
| **Movement** | `Arrow Keys` / `WASD` | Screen directions (clamped to playfield bounds) |
| **Mouse Flight** | `Left Click + Drag` | Location-independent virtual joystick relative to touchdown |
| **Primary Fire** | `Spacebar` / On-screen `FIRE` | Fires active primary weapon (Laser / Flamer / Plasma) |
| **Secondary Fire** | `Left Ctrl` / `Right Ctrl` / On-screen `SEC` | Fires homing missile salvo / drops bomb |
| **Cycle Primary** | `Q` (previous) / `E` or `Tab` (next) | Cycles between owned primary weapons |
| **Select Primary** | `1`, `2`, `3` | Direct selection: 1=Laser, 2=Flamethrower, 3=Plasma Lance |
| **Invert Pitch** | `X` | Inverts vertical pitch during the Moon trench run |
| **Pause / Menu** | `Esc` / `P` / On-screen `II` | Opens pause overlay; freezes gameplay clock |
| **Toggle FPS** | `F1` | Shows / hides frame rate overlay |
| **Hot Reload** | `F5` / `R` | Reloads all JSON definitions, levels, and textures |

### Touch (Tablet)

- **Flight Joystick (Left Thumb):** Floating, location-independent virtual joystick. Touchdown sets neutral anchor. Displacements beyond the 8px deadzone (`kTouchJoystickDeadzone`) apply velocity smoothly up to full speed at 90px (`kTouchJoystickRadius`).
- **Edge Throttle:** Holding the ship into the forward playfield edge (>= 85% forward intent) engages the afterburner boost (+15% world scroll speed, enlarged engine flame). Holding into the back edge (<= -85% intent) throttles down (-15% world scroll speed).
- **Combat Buttons (Right Thumb):**
  - **FIRE (Bottom-Right):** Primary weapon trigger.
  - **SEC (Above Fire):** Secondary missile trigger.
  - **WEAPON (Above Secondary):** Primary weapon cycle button.
  - **II (Top Center):** Pause button.

### Gamepad

- **Start Button (`GAMEPAD_BUTTON_MIDDLE_RIGHT`):** Pause / Resume gameplay.

---

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): Source file overview, game loop, subsystem integration, and rendering pipeline.
- [docs/LEVELS.md](docs/LEVELS.md): 8-step campaign sequence, level designs, mechanics, and testing environment variables.
- [AGENTS.md](AGENTS.md): Development rules and constraints for AI contributors.
- [ASSETS.md](ASSETS.md): Asset repository pipeline and synchronization instructions.
- [docs/ENEMY_DESIGN.md](docs/ENEMY_DESIGN.md): Enemy design language and 3D modeling pipeline.
- [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md): JSON schema reference for levels, waves, enemies, and players.
