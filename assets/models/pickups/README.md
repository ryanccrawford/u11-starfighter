# Animated power-up models

Five GLB 2.0 power-ups share a fully volumetric atomic containment cage and use a distinct central insert:

- `pickup_weapon.glb` — cyan triple laser cannon
- `pickup_missiles.glb` — orange three-missile rack
- `pickup_shield.glb` — blue layered shield crest
- `pickup_repair.glb` — green medical/nanite module
- `pickup_rapid.glb` — magenta rotary rapid-fire mechanism

## Animation

Each GLB contains one animation named `Pickup_Spin` with two densely sampled rotation channels:

- `OuterAtomShell`: one continuous +360-degree turn around a fixed 3D axis in 2.0 seconds
- `InnerCore`: two continuous -360-degree turns around the same axis in 2.0 seconds

Both channels use linear interpolation and loop seamlessly. The inner shell and item therefore rotate continuously in the opposite direction at exactly twice the outer cage speed. There is no rocking, reversing, alternating, or flat coin-style spin.

## Coordinate and rendering assumptions

- Authored in Blender 5.0, centered at the origin.
- Blender source is Z-up; Blender's glTF exporter performs its standard Y-up conversion.
- All variants have substantial depth on every axis, matching centered outer-cage bounds, and centered pivots.
- Production transforms are applied and GLBs contain no cameras or lights.
- Materials are `Pickup_Chassis` and `Pickup_Insert_<TYPE>`.
- GLBs embed 1024 × 1024 RGBA albedo textures. The albedo carries panel lines, wear, circuitry, shadows, and accent color because the current game shader samples only the diffuse texture.
- UVs intentionally reuse the non-directional detail texture independently on small mechanical components; every UV remains inside 0..1.

## Source and previews

- Editable source: `source/powerup_variants.blend`
- Source textures: `source/textures/`
- Color review sheet: `previews/powerup_contact_sheet_1920x1080.png`
- Silhouette review sheet: `previews/powerup_silhouettes_640x128.png`
- Spin review video: `previews/powerup_spin_preview.mp4`
- Validation results: `validation_report.md`

## Rebuilding and validating

From the shooter project root:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.0\blender.exe' --background --python tools\create_powerup_models.py
& ffmpeg -y -framerate 30 -start_number 1 -i 'assets\models\pickups\previews\spin_frames\frame_%04d.png' -c:v libx264 -pix_fmt yuv420p -movflags +faststart 'assets\models\pickups\previews\powerup_spin_preview.mp4'
& 'C:\Program Files\Blender Foundation\Blender 5.0\blender.exe' --background --python tools\validate_powerup_models.py
```

The validation script performs a clean import of every GLB and fails on planar geometry, incorrect animation structure/timing, a non-seamless loop, stopped or reversing rotation, incorrect opposing direction/rate, node names, skin weights, pivots, scale, triangle budget, materials, embedded texture dimensions, UV range, mismatched cage bounds, or production cameras/lights.

## Integration status

The game integration loads these models through raylib, advances animation only for pickup types actually drawn, retains the existing pickup collision/state behavior, vaporizes collected pickups, and flashes missed pickups immediately before removal. Deployment remains pending Ryan's visual approval.
