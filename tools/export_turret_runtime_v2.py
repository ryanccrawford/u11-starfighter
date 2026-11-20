"""Export a subdued, platform-matched turret base as runtime v2 components."""
from pathlib import Path
import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1] / "assets/models/ship_interior/hazards"
SOURCE = ROOT / "turret_detailed_v1/turret.blend"
OUT = ROOT / "turret_runtime_v2"
BASE = ("Base ", "Bearing ", "Mount bolt", "Armored radial foot")
BARRELS = ("Rotating cannon barrel", "Barrel muzzle sleeve", "Dark muzzle bore",
           "Barrel drive spindle", "Barrel cage collar", "Collar trim")


def group(name):
    if name.startswith(BASE): return "base"
    if name.startswith(BARRELS): return "barrels"
    return "head"


def export_part(part, objects, pivot_z):
    target = OUT / (part + ".glb")
    copies = []
    for source in objects:
        copy = source.copy()
        copy.data = source.data.copy()
        bpy.context.collection.objects.link(copy)
        copy.matrix_world = source.matrix_world.copy()
        copies.append(copy)
    batches = {}
    for obj in copies:
        if not obj.data.materials:
            raise RuntimeError("Missing turret material: " + obj.name)
        batches.setdefault(obj.data.materials[0], []).append(obj)
    joined = []
    for batch in batches.values():
        bpy.ops.object.select_all(action="DESELECT")
        for obj in batch:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = batch[0]
        bpy.ops.object.join()
        obj = bpy.context.object
        obj.data.transform(obj.matrix_world)
        obj.matrix_world = Matrix.Identity(4)
        obj.data.transform(Matrix.Translation((0, 0, -pivot_z)))
        joined.append(obj)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in joined:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = joined[0]
    bpy.ops.export_scene.gltf(filepath=str(target), export_format="GLB", use_selection=True)
    for obj in joined:
        bpy.data.objects.remove(obj, do_unlink=True)


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise FileExistsError(f"Refusing to overwrite existing art in {OUT}")
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    # Cool graphite-titanium better matches the ceiling hardpoint. The machined edge
    # stays brighter; the dark recesses remain dark to preserve the layered silhouette.
    updates = {
        "Brushed titanium": ((.16, .20, .24), .94, .23),
        "Machined bright edges": ((.42, .48, .52), .97, .17),
        "Recessed graphite steel": ((.035, .047, .060), .90, .27),
    }
    for name, (color, metallic, roughness) in updates.items():
        mat = bpy.data.materials.get(name)
        if not mat or not mat.use_nodes:
            raise RuntimeError("Missing turret material: " + name)
        bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if not bsdf:
            raise RuntimeError("Missing Principled BSDF: " + name)
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        bsdf.inputs["Metallic"].default_value = metallic
        bsdf.inputs["Roughness"].default_value = roughness
        mat.diffuse_color = (*color, 1.0)
    models = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    for part, pivot in (("base", 0), ("head", .52), ("barrels", 1.49)):
        selected = [obj for obj in models if group(obj.name) == part]
        if not selected:
            raise RuntimeError("Missing turret group: " + part)
        export_part(part, selected, pivot)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "turret_material_source.blend"))
    print(f"TURRET_RUNTIME_V2 output={OUT}")


if __name__ == "__main__": main()
