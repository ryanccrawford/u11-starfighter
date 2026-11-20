"""Create the reusable Level 3 armored frame and sliding bulkhead leaf GLBs."""
from pathlib import Path
import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/models/ship_interior/hazards/bulkhead_runtime_v11"


def material(name, color, metallic, roughness, emission=0.0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission:
        socket = bsdf.inputs.get("Emission Color") or bsdf.inputs.get("Emission")
        if socket:
            socket.default_value = (*color, 1.0)
        strength = bsdf.inputs.get("Emission Strength")
        if strength:
            strength.default_value = emission
    return mat


def block(name, loc, dims, mat, bevel=0.04, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dims
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    if bevel:
        mod = obj.modifiers.new("Machined radius", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


def cylinder(name, loc, radius, depth, mat, axis="z", vertices=12):
    rotation = (0, 0, 0) if axis == "z" else (0, 1.57079632679, 0) if axis == "x" else (1.57079632679, 0, 0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
                                        location=loc, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def export_joined(objects, filename):
    # Bake transforms so every reusable piece is authored around the model origin.
    for obj in objects:
        obj.data.transform(obj.matrix_world)
        obj.matrix_world = Matrix.Identity(4)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = Path(filename).stem
    bpy.ops.object.select_all(action="DESELECT")
    joined.select_set(True)
    bpy.context.view_layer.objects.active = joined
    bpy.ops.export_scene.gltf(filepath=str(OUT / filename), export_format="GLB", use_selection=True)
    bpy.data.objects.remove(joined, do_unlink=True)


def build_frame(mats):
    dark, steel, bright, hazard, emissive = mats
    parts = []
    # Main forged frame and the deep rails that bolt into the corridor walls.
    # Blender-to-GLB export preserves X, maps Blender Y to GLB Z, and Blender Z to GLB Y.
    # The runtime basis maps GLB X to screen-up, GLB Y toward camera, and GLB Z to screen-width.
    # So author lateral width on Blender Y and camera-facing thickness on Blender Z.
    for x in (-19.4, 19.4, -7.15, 7.15, -6.35, 6.35):
        crossbarWidth = .84 if abs(x) < 7 else .55
        parts.append(block("Pressure cross member", (x, 0, 0), (crossbarWidth, 15.0, .88), dark, .11))
        parts.append(block("Titanium face cap", (x, 0, .50), (crossbarWidth * .62, 14.25, .10), steel, .05))
        if abs(x) < 7:
            parts.append(block("Inner guide channel", (x, 0, .60), (.22, 13.8, .16), bright, .035))
        # Guide rollers and locking pins run along the opening width.
        for y in (-6.2, -4.0, 0, 4.0, 6.2):
            parts.append(cylinder("Guide roller", (x, y, .72), .13, .22, bright))
        for y in (-6.75, 6.75):
            parts.append(cylinder("Frame armored fastener", (x, y, .56), .115, .10, bright))
    # Actuators sit inside the side cheeks and drive the leaves into the top/bottom pockets.
    for lateral in (-6.45, 6.45):
        parts.append(cylinder("Bulkhead hydraulic barrel", (0, lateral, -.46), .18, 5.2, dark, "x"))
        parts.append(cylinder("Hydraulic piston rod", (.25, lateral, -.46), .075, 4.0, bright, "x"))
    # Inlaid warning strips and actuator status lenses at both corners.
    for x in (-6.90, 6.90):
        parts.append(block("Hazard stripe", (x, 0, .57), (.34, .66, .055), hazard, .018))
        parts.append(block("Status light lens", (x, 0, .59), (.24, .38, .06), emissive, .035))
    return parts


def build_near_pillar(mats):
    dark, steel, _, _, _ = mats
    # One reusable pillar asset is instanced on both sides at runtime. Its material
    # and geometry are therefore identical; only the nearest instance gets alpha.
    parts = [block("Reusable bulkhead pillar", (0, 0, 0), (39.0, 0.92, 0.82), dark, .12)]
    for x in (-18.0, -16.0, -13.5, -11.8, -9.4, -7.0, -5.0, -3.0, 3.0, 5.0, 7.0, 9.4, 11.8, 13.5, 16.0, 18.0):
        parts.append(block("Reusable pillar recessed rib", (x, 0, .08), (.20, .98, .92), steel, .035))
    return parts


def build_leaf(mats):
    dark, steel, bright, hazard, emissive = mats
    parts = [block("Titanium leaf core", (0, 0, 0), (6.0, 14.0, .40), dark, .075)]
    # Four replaceable armored sheets with a raised perimeter and recessed central field.
    for y in (-5.25, -3.5, -1.75, 0, 1.75, 3.5, 5.25):
        parts.append(block("Titanium armor sheet", (0, y, .235), (5.18, 1.62, .12), steel, .045))
        parts.append(block("Inset wear plate", (0, y, .305), (4.60, 1.34, .035), dark, .028))
        for x in (-2.30, 2.30):
            parts.append(cylinder("Leaf captive bolt", (x, y, .355), .075, .10, bright))
    # Repeated ribs and visible dovetail lock blocks make the door read as a moving assembly.
    for x in (-2.82, 2.82):
        parts.append(block("Leaf armored edge", (x, 0, .35), (.25, 13.75, .20), dark, .045))
        for y in (-5.6, -3.0, 0, 3.0, 5.6):
            parts.append(block("Lock tooth", (x, y, .48), (.34, .56, .28), bright, .045))
    for y in (-4.2, 0, 4.2):
        parts.append(block("Ribbed reinforcement", (0, y, -.27), (5.0, .11, .18), dark, .025))
    # Chevron-painted inner lip is the visible edge that closes around the safe central gap.
    for x in (-2.70, 2.70):
        parts.append(block("Amber warning lip", (x, 0, .49), (.20, 13.55, .045), hazard, .012))
    for x in (-2.48, 2.48):
        parts.append(block("White warning lip", (x, 0, .49), (.075, 13.55, .05), bright, .012))
    # Narrow inset windows on the front face.
    for x in (-1.75, 1.75):
        parts.append(block("Pressure indicator bezel", (x, 0, .36), (.62, 1.5, .13), dark, .035))
        parts.append(block("Pressure indicator lens", (x, 0, .44), (.40, 1.1, .025), emissive, .01))
    return parts


def main():
    if OUT.exists() and any(OUT.iterdir()):
        raise FileExistsError(f"Refusing to overwrite existing art in {OUT}")
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    dark = material("Forged graphite titanium", (.095, .12, .15), .86, .26)
    steel = material("Brushed armor titanium", (.26, .31, .35), .90, .21)
    bright = material("Machined edge steel", (.48, .53, .55), .92, .16)
    hazard = material("Oxide orange hazard enamel", (.78, .20, .035), .55, .30)
    emissive = material("Amber status emitter", (1.0, .13, .018), .20, .24, 3.0)
    mats = (dark, steel, bright, hazard, emissive)
    frame = build_frame(mats)
    near_pillar = build_near_pillar(mats)
    leaf = build_leaf(mats)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "bulkhead_source.blend"))
    export_joined(frame, "frame.glb")
    export_joined(near_pillar, "pillar.glb")
    export_joined(leaf, "leaf.glb")
    print("BULKHEAD_RUNTIME frame_parts=%d near_pillar_parts=%d leaf_parts=%d output=%s" % (len(frame), len(near_pillar), len(leaf), OUT))


if __name__ == "__main__":
    main()
