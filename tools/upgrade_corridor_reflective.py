"""Create a new reflective-material corridor build without modifying modular_v4."""
from pathlib import Path
import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/models/ship_interior/modular_v4/corridor_modular.blend"
OUT = ROOT / "assets/models/ship_interior/modular_v5"


def set_pbr(name, metallic, roughness, color=None):
    mat = bpy.data.materials.get(name)
    if not mat or not mat.use_nodes:
        raise RuntimeError(f"Missing node material: {name}")
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not bsdf:
        raise RuntimeError(f"Missing Principled BSDF: {name}")
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if color is not None:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        mat.diffuse_color = (*color, 1.0)


def export_objects(objects, path):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True)


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise FileExistsError(f"Refusing to overwrite existing art in {OUT}")
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    # The repeated conduit materials use sharper metal response; the hull and controls
    # remain slightly rougher so the entire environment does not become mirror-like.
    set_pbr("Recessed machinery", .94, .19)
    set_pbr("Warning brass", .88, .21)
    set_pbr("Armored titanium", .92, .23)
    set_pbr("Embedded control panel", .78, .29)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "corridor_modular.blend"))

    pipes = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("Armoured conduit")]
    if not pipes:
        raise RuntimeError("No separate Armoured conduit meshes in source scene")
    export_objects(pipes, OUT / "part_pipe.glb")
    collars = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("Pipe collar")]
    if not collars:
        raise RuntimeError("No separate Pipe collar meshes in source scene")
    export_objects(collars, OUT / "part_pipe_collar.glb")

    mats = list(bpy.data.materials)
    for mat in mats:
        group = [o for o in bpy.data.objects if o.type == "MESH" and o.data.materials and o.data.materials[0] == mat]
        if len(group) < 2:
            continue
        bpy.ops.object.select_all(action="DESELECT")
        for obj in group:
            obj.select_set(True)
            obj.data.transform(obj.matrix_world)
            obj.matrix_world = Matrix.Identity(4)
        bpy.context.view_layer.objects.active = group[0]
        bpy.ops.object.join()
    scene_meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    export_objects(scene_meshes, OUT / "corridor_section.glb")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "corridor_section.blend"))
    print(f"REFLECTIVE_CORRIDOR mats=4 pipes={len(pipes)} collars={len(collars)} out={OUT}")


if __name__ == "__main__":
    main()
