# AI Agent Guidelines

Rules and operational constraints for any AI working on the U11 Starfighter repository.

---

## 1. Git & Workflow Hygiene

- **One issue and one branch per change:** Every modification must map to a single focused issue and be developed on a dedicated feature branch.
- **Strictly scoped edits:** Change nothing outside the issue's immediate scope. Do not reformat unrelated files, reorder imports, or refactor working code unless explicitly requested.
- **Never push to `main`:** All work must be submitted via pull requests against `main`. Direct pushes to `main` are strictly forbidden.
- **Owner approval before merge:** Do not merge, enable auto-merge, or ask another agent to merge a pull request until Ryan has personally submitted an explicit approval on that PR. Treat only Ryan's own review or clear written authorization in the PR conversation as approval. A bot, agent, automated review, or inferred consent is never approval. GitHub's branch-protection status does not replace this rule.
- **Assets never go into git:** Game asset binaries (GLB models, Blender `.blend` files, audio WAV/MP3/OGG, textures, PNGs, and TTF fonts) must never be committed to git. Asset synchronization is managed outside git via [assets.manifest](assets.manifest) and `tools/assets-sync.ps1`. See [ASSETS.md](ASSETS.md).

---

## 2. Gameplay & Art Guardrails

- **Keep the 85% edge throttle unchanged:**
  Holding the ship into the forward edge (`forwardIntent >= 0.85f`) accelerates world scroll speed by +15% (`cfg::kEdgeBoostMul = 1.15f`) and expands engine flames. Holding into the back edge (`forwardIntent <= -0.85f`) reduces scroll speed by -15% (`cfg::kEdgeSlowMul = 0.85f`). These thresholds and multipliers are intentional design choices—do not adjust them.
- **Keep the Scarab art unchanged:**
  The Scarab scout drone model (`assets/models/enemies/scarab/`) and its visual parameters (gunmetal hull, deep violet faction armor plates, green glowing bio-brain dome, single red robot visor slit, acid-green weapon coils, and violet engine flame) represent the established art style bar. Do not modify or replace Scarab geometry or textures.
- **No ship wiggle:**
  Do not introduce artificial floating, hovering, side-to-side oscillation, or idle swaying animations to ships. In space, hulls stay rigid on their vectors; motion comes solely from physics, throttle, banking, and player steering.
- **The sun is the only light:**
  Directional illumination comes exclusively from the single solar light source across space, planets, ships, and terrain. Ship-mounted navigation lights and strobes are allowed. **No ambient fill lights, rim lights, or secondary omni lights are permitted.**

---

## 3. Performance & Platform Budgets

- **Target frame rate on tablet:**
  The game must maintain at least **30 FPS** on the Doogee U11 tablet (Unisoc T606 / Mali-G57 GPU) under heavy combat. Normal gameplay targets 60–90 FPS. Never introduce unbatched draw calls, redundant shader passes, heavy blending, or unoptimized meshes that degrade tablet frame rates.
- **No visible test windows on PC:**
  Automated tests, verification scripts, and screen capture runs executed on Windows must run with `HIDDEN_WINDOW=1` so no visible window pops over the desktop (`FLAG_WINDOW_HIDDEN`).

---

## 4. Hardware Deployment

- **Never deploy to the tablet unless explicitly requested:**
  Never run `tools/sync-tablet.ps1` or `tools/build-and-deploy.ps1` against the physical Doogee U11 device unless the repository owner (Ryan) explicitly instructs you to deploy. Keep PC testing and ARM64 cross-compilation isolated from device installation.
