"""Verify independently reusable corridor exports with Blender's GLB importer."""
from pathlib import Path
import os
import bpy

ROOT = Path(__file__).resolve().parents[1]
KIT = Path(os.environ.get("CORRIDOR_KIT_DIR", str(ROOT / "assets" / "models" / "ship_interior" / "modular_v2")))
EXPECTED = ("hull", "rail", "pipe", "panel", "rib", "recess", "light", "warning_stripe",
            "tower_base", "tower_spire", "tower_halo", "rugged_node", "pipe_collar")

for name in EXPECTED:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    path = KIT / f"part_{name}.glb"
    bpy.ops.import_scene.gltf(filepath=str(path))
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if len(meshes) != 1 or not meshes[0].data.vertices:
        raise RuntimeError(f"Invalid reusable part: {path}")
    print(f"VALID_PART {name} vertices={len(meshes[0].data.vertices)}")

bpy.ops.wm.open_mainfile(filepath=str(KIT / "corridor_modular.blend"))
count = sum(obj.type == "MESH" for obj in bpy.context.scene.objects)
if count < 110:
    raise RuntimeError(f"Modular assembly unexpectedly contains only {count} meshes")
print(f"VALID_ASSEMBLY meshes={count}")
