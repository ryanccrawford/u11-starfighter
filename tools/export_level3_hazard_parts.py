"""Export separate reusable hazard models from the editable Blender scene."""
from pathlib import Path
import bpy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "models" / "ship_interior" / "hazards" / "parts_v1"
SOURCE = OUT.parent / "level3_hazards_detail.blend"

def export_group(name, prefixes, offset):
    target = OUT / f"{name}.glb"
    if target.exists():
        raise FileExistsError(f"Refusing to overwrite {target}")
    members = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and obj.name.startswith(prefixes)]
    if not members:
        raise RuntimeError(f"No objects found for {name}")
    bpy.ops.object.select_all(action="DESELECT")
    root = bpy.data.objects.new(name + "_root", None)
    bpy.context.collection.objects.link(root)
    root.select_set(True)
    for obj in members:
        obj.parent = root
        obj.select_set(True)
    root.location.x = offset
    bpy.context.view_layer.objects.active = root
    bpy.ops.export_scene.gltf(filepath=str(target), export_format="GLB", use_selection=True)
    for obj in members:
        obj.parent = None
    bpy.data.objects.remove(root, do_unlink=True)
    print(f"EXPORTED {name}: {len(members)} objects -> {target}")

def main():
    if not SOURCE.is_file():
        raise FileNotFoundError(SOURCE)
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    export_group("turret", ("Turret ",), 4.8)
    export_group("turret_wreck", ("Wreck ",), 8.2)
    export_group("bulkhead", ("Bulkhead ", "Titanium ", "Plate ", "Door ",
                              "Electronics ", "Cable ", "Animated ", "Rotating ",
                              "Beacon ", "Damage "), 0)

if __name__ == "__main__":
    main()
