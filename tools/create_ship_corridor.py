"""Build a modular 3D enemy-ship corridor for level 3 using Blender."""
from pathlib import Path
import os
import bpy

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("CORRIDOR_OUTPUT_DIR", str(ROOT / "assets" / "models" / "ship_interior")))


def material(name, color, metallic, roughness, emission=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    node = mat.node_tree.nodes.get("Principled BSDF")
    node.inputs["Base Color"].default_value = (*color, 1.0)
    node.inputs["Metallic"].default_value = metallic
    node.inputs["Roughness"].default_value = roughness
    if emission:
        node.inputs["Emission Color"].default_value = (*color, 1.0)
        node.inputs["Emission Strength"].default_value = emission
    return mat


def metal_texture(mat):
    image_path = ROOT / "assets" / "models" / "ship_interior" / "worn_titanium.png"
    image = bpy.data.images.load(str(image_path), check_existing=True)
    image.pack()
    nodes = mat.node_tree.nodes
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    mat.node_tree.links.new(tex.outputs["Color"], nodes.get("Principled BSDF").inputs["Base Color"])


def panel_texture(mat):
    image_path = ROOT / "assets" / "models" / "ship_interior" / "enemy_ship_control_panels.png"
    image = bpy.data.images.load(str(image_path), check_existing=True)
    image.pack()
    nodes = mat.node_tree.nodes
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    mat.node_tree.links.new(tex.outputs["Color"], nodes.get("Principled BSDF").inputs["Base Color"])


def box(name, location, scale, mat, bevel=0.03):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    if bevel:
        mod = obj.modifiers.new("Machined edges", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


def cylinder(name, location, radius, depth, mat, rotation=(0, 0, 0), vertices=12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
        location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def cone(name, location, radius1, radius2, depth, mat, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=radius1, radius2=radius2, depth=depth,
        location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def ring(name, location, major, minor, mat, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=16,
        minor_segments=6, location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    parts = {"hull": "Main pressure hull", "rail": "Inner corridor rail", "pipe": "Armoured conduit",
             "panel": "Armour panel", "rib": "Rib", "recess": "Recess", "light": "Indicator",
             "warning_stripe": "Warning stripe", "tower_base": "Alien tower base",
             "tower_spire": "Alien tower spire", "tower_halo": "Tower halo ring",
             "rugged_node": "Rugged growth node", "pipe_collar": "Pipe collar"}
    targets = [OUT / "corridor_section.glb", OUT / "corridor_section.blend", OUT / "corridor_modular.blend"]
    targets.extend(OUT / f"part_{name}.glb" for name in parts)
    if any(path.exists() for path in targets):
        raise FileExistsError("Output already exists; choose a new CORRIDOR_OUTPUT_DIR to preserve existing work")
    if bpy.data.filepath or any(obj.type != "CAMERA" and obj.type != "LIGHT" and obj.name != "Cube" for obj in bpy.context.scene.objects):
        raise RuntimeError("Run in a fresh Blender scene without user content")
    for obj in list(bpy.context.scene.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    steel = material("Armored titanium", (0.15, 0.19, 0.23), 0.8, 0.4)
    metal_texture(steel)
    panels = material("Embedded control panel", (0.28, 0.32, 0.34), 0.65, 0.34)
    panel_texture(panels)
    dark = material("Recessed machinery", (0.055, 0.075, 0.09), 0.75, 0.5)
    trim = material("Warning brass", (0.58, 0.30, 0.045), 0.7, 0.38)
    light = material("Coolant light", (0.04, 0.55, 0.8), 0.2, 0.35, 2.0)
    alien = material("Alien ceramic alloy", (0.12, 0.07, 0.20), 0.72, 0.28)
    hot = material("Alien reactor glow", (0.95, 0.06, 0.42), 0.12, 0.25, 3.0)
    for side in (-1, 1):
        y = side * 3.55
        box("Main pressure hull", (0, y, -0.4), (8, 1.25, 1.3), steel)
        box("Inner corridor rail", (0, side * 2.85, 0.3), (8, 0.20, 0.35), dark)
        for pipe in range(3):
            bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.055 + 0.014 * pipe, depth=8,
                location=(0, y + side * (0.08 + pipe * 0.13), 0.68), rotation=(0, 1.57079632679, 0))
            bpy.context.object.name = "Armoured conduit"
            bpy.context.object.data.materials.append(dark if pipe != 1 else trim)
        for i in range(8):
            x = -3.5 + i
            box("Armour panel", (x, y, 0.35), (0.93, 0.9, 0.22), panels)
            box("Rib", (x + 0.48, y, 0.53), (0.12, 1.15, 0.25), dark)
            box("Recess", (x, y + side * 0.17, 0.49), (0.56, 0.25, 0.09), dark)
            box("Indicator", (x, y - side * 0.28, 0.5), (0.38, 0.045, 0.08), light, 0.01)
            if i % 2 == 0:
                box("Warning stripe", (x + 0.36, y, 0.51), (0.07, 0.62, 0.08), trim, 0.01)
        # Tall asymmetric alien-industrial towers repeat at different intervals above and below the flight lane.
        for number, x in enumerate((-2.8, 0.15, 2.65)):
            tall = 0.85 + 0.22 * ((number + (side > 0)) % 2)
            inward = side * 2.50
            tag = "" if side == -1 and number == 0 else f".{side}.{number}"
            cylinder("Alien tower base" + tag, (x, inward, 0.52), 0.34, 0.55, alien,
                rotation=(1.57079632679, 0, 0), vertices=12)
            cone("Alien tower spire" + tag, (x + 0.08, inward - side * tall * 0.42, 0.53),
                0.24, 0.055, tall, dark, rotation=(1.57079632679, 0, 0))
            ring("Tower halo ring" + tag, (x + 0.08, inward - side * tall * 0.54, 0.55),
                0.26, 0.045, hot, rotation=(1.57079632679, 0, 0))
            for claw in (-1, 1):
                node_tag = "" if side == -1 and number == 0 and claw == -1 else tag + str(claw)
                box("Rugged growth node" + node_tag,
                    (x + claw * 0.26, inward - side * tall * 0.26, 0.61),
                    (0.20, 0.25, 0.24), alien, 0.055)
        # Banded collars break long conduits into tangible, reusable mechanical parts.
        for x in (-3.0, -1.0, 1.0, 3.0):
            cylinder("Pipe collar" + ("" if side == -1 and x == -3.0 else f".{side}.{x}"),
                (x, y + side * 0.20, 0.68), 0.17, 0.16, trim,
                rotation=(0, 1.57079632679, 0), vertices=12)
    # Keep the editable assembly intact before preparing the draw-call-optimized version.
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "corridor_modular.blend"), copy=True)
    for filename, object_name in parts.items():
        source = bpy.data.objects.get(object_name)
        if source is None:
            raise RuntimeError(f"Missing corridor component: {object_name}")
        duplicate = source.copy()
        duplicate.data = source.data.copy()
        bpy.context.collection.objects.link(duplicate)
        duplicate.location = (0, 0, 0)
        bpy.ops.object.select_all(action="DESELECT")
        duplicate.select_set(True)
        bpy.context.view_layer.objects.active = duplicate
        bpy.ops.export_scene.gltf(filepath=str(OUT / f"part_{filename}.glb"), export_format="GLB", use_selection=True)
        bpy.data.objects.remove(duplicate, do_unlink=True)
    # Join geometry by material to limit draw calls on the tablet.
    # Every material used by the finished section must be joined.  Leaving any group out
    # turns each decorative object into a separate submission on the tablet GPU.
    for mat in (steel, panels, dark, trim, light, alien, hot):
        objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and obj.data.materials and obj.data.materials[0] == mat]
        bpy.ops.object.select_all(action="DESELECT")
        for obj in objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        bpy.ops.object.join()
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=str(OUT / "corridor_section.glb"), export_format="GLB")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "corridor_section.blend"))


if __name__ == "__main__":
    build()
