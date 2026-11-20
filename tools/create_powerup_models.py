"""Create the five production power-up GLBs and their review renders.

Run with Blender 5.0:
  blender --background --python tools/create_powerup_models.py
"""

from __future__ import annotations

import math
import os
import random
import zlib
from pathlib import Path

import bpy
import numpy as np
from mathutils import Quaternion, Vector


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets" / "models" / "pickups"
SOURCE_DIR = ASSET_DIR / "source"
TEXTURE_DIR = SOURCE_DIR / "textures"
PREVIEW_DIR = ASSET_DIR / "previews"

VARIANTS = {
    "weapon": (0.18, 0.82, 1.00, 1.0),
    "missiles": (1.00, 0.40, 0.06, 1.0),
    "shield": (0.20, 0.42, 1.00, 1.0),
    "repair": (0.12, 1.00, 0.30, 1.0),
    "rapid": (1.00, 0.12, 0.72, 1.0),
}


def ensure_dirs() -> None:
    for path in (ASSET_DIR, SOURCE_DIR, TEXTURE_DIR, PREVIEW_DIR):
        path.mkdir(parents=True, exist_ok=True)


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.materials, bpy.data.cameras,
                       bpy.data.lights, bpy.data.images, bpy.data.actions):
        for block in list(datablocks):
            datablocks.remove(block)


def make_texture(name: str, accent: tuple[float, float, float, float] | None = None) -> bpy.types.Image:
    """Generate a deterministic, high-frequency sci-fi albedo texture."""
    size = 1024
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    u, v = x / size, y / size
    rng = np.random.default_rng(zlib.crc32(name.encode("utf-8")))
    noise = rng.random((size, size), dtype=np.float32)
    fine = (np.sin(u * 650.0) * 0.018 + np.sin(v * 510.0 + u * 17.0) * 0.012)
    scratches = np.zeros((size, size), dtype=np.float32)
    for _ in range(90):
        yy = rng.integers(0, size)
        xx = rng.integers(0, size - 80)
        length = int(rng.integers(25, 180))
        scratches[max(0, yy - 1):min(size, yy + 1), xx:min(size, xx + length)] += rng.uniform(0.05, 0.18)
    panel = ((np.mod(x, 128) < 4) | (np.mod(y, 128) < 4)).astype(np.float32)
    micro = (noise - 0.5) * 0.055 + fine + scratches
    if accent is None:
        base = np.zeros((size, size, 4), dtype=np.float32)
        metal = 0.20 + 0.13 * np.sin(u * math.pi * 10.0) * np.sin(v * math.pi * 8.0)
        base[..., 0] = metal * 0.78 + micro - panel * 0.11
        base[..., 1] = metal * 0.88 + micro - panel * 0.10
        base[..., 2] = metal * 1.02 + micro - panel * 0.08
        # Repeating copper circuitry and cold-blue status marks.
        circuit = ((np.mod(x + 2 * y, 173) < 5) & (np.mod(y, 43) < 22))
        status = ((np.mod(x, 257) < 7) & (np.mod(y, 97) < 38))
        base[circuit, :3] = (0.55, 0.24, 0.07)
        base[status, :3] = (0.08, 0.55, 0.72)
    else:
        color = np.array(accent[:3], dtype=np.float32)
        radial = 0.52 + 0.34 * np.sin((u * 7.0 + v * 5.0) * math.pi)
        base = np.empty((size, size, 4), dtype=np.float32)
        base[..., :3] = color * radial[..., None] + micro[..., None]
        ribs = ((np.mod(x, 96) < 6) | (np.mod(y + x // 2, 128) < 5))
        base[ribs, :3] = np.clip(color * 1.25 + 0.18, 0, 1)
        dark = ((np.mod(x + y, 211) < 10) & (np.mod(y, 83) < 40))
        base[dark, :3] *= 0.28
    base[..., :3] = np.clip(base[..., :3], 0.015, 1.0)
    base[..., 3] = 1.0
    image = bpy.data.images.new(name, width=size, height=size, alpha=True)
    image.pixels.foreach_set(base.ravel())
    image.file_format = "PNG"
    image.filepath_raw = str(TEXTURE_DIR / f"{name}.png")
    image.save()
    image.pack()
    return image


def material(name: str, image: bpy.types.Image, metallic: float, roughness: float,
             emission_strength: float = 0.0) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = tuple(image.pixels[:3]) + (1.0,)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    bsdf = nodes.get("Principled BSDF")
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Linear"
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission_strength:
        links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = emission_strength
    return mat


def apply_bevel(obj: bpy.types.Object, width: float = 0.04, segments: int = 2) -> None:
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    mod = obj.modifiers.new("ProductionBevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = "ANGLE"
    bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.select_set(False)


def assign_and_unwrap(obj: bpy.types.Object, mat: bpy.types.Material) -> None:
    if obj.type != "MESH":
        return
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.012)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.select_set(False)


def cube(name: str, loc, scale, mat, rotation=(0, 0, 0), bevel=0.04) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(location=loc, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    if bevel > 0:
        apply_bevel(obj, bevel, 2)
    else:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        obj.select_set(False)
    assign_and_unwrap(obj, mat)
    return obj


def cylinder(name: str, loc, radius, depth, mat, rotation=(0, 0, 0), vertices=20,
             bevel=0.025) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
                                       end_fill_type="NGON", location=loc, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    if bevel > 0:
        apply_bevel(obj, bevel, 1)
    assign_and_unwrap(obj, mat)
    return obj


def torus(name: str, major, minor, mat, loc=(0, 0, 0), rotation=(0, 0, 0),
          major_segments=64, minor_segments=12) -> bpy.types.Object:
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor,
                                    major_segments=major_segments, minor_segments=minor_segments,
                                    location=loc, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    assign_and_unwrap(obj, mat)
    return obj


def ico_sphere(name: str, loc, radius, mat, subdivisions=1) -> bpy.types.Object:
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=radius, location=loc)
    obj = bpy.context.object
    obj.name = name
    assign_and_unwrap(obj, mat)
    return obj


def scale_parts(objects: list[bpy.types.Object], factor: float) -> None:
    """Uniformly shrink an authored insert around the world origin and apply its scale."""
    for obj in objects:
        obj.location = Vector(obj.location) * factor
        obj.scale = Vector(obj.scale) * factor
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        obj.select_set(False)


def parent_all(objects: list[bpy.types.Object], parent: bpy.types.Object) -> None:
    for obj in objects:
        obj.parent = parent


def chassis(mat: bpy.types.Material, tag: str) -> tuple[bpy.types.Object, list[bpy.types.Object]]:
    """Build a fully volumetric atomic containment cage, never a flat disc."""
    root = bpy.data.objects.new(f"OuterChassis_{tag.upper()}", None)
    bpy.context.collection.objects.link(root)
    parts: list[bpy.types.Object] = []
    orbit_rotations = (
        (math.radians(58), 0, math.radians(28)),
        (math.radians(-58), 0, math.radians(-28)),
        (0, math.radians(90), 0),
    )
    for i, rotation in enumerate(orbit_rotations):
        parts.append(torus(f"{tag}_AtomicOrbit_{i}", 1.46, 0.115, mat, rotation=rotation,
                           major_segments=32, minor_segments=6))

    # Eight armored reactor nodes occupy all octants, making the silhouette and depth
    # unmistakably three-dimensional from either gameplay camera.
    node_radius = 1.49 / math.sqrt(3.0)
    for i, (sx, sy, sz) in enumerate((
        (-1, -1, -1), (-1, -1, 1), (-1, 1, -1), (-1, 1, 1),
        (1, -1, -1), (1, -1, 1), (1, 1, -1), (1, 1, 1),
    )):
        loc = (sx * node_radius, sy * node_radius, sz * node_radius)
        parts.append(ico_sphere(f"{tag}_ArmorNode_{i:02d}", loc, 0.19, mat, subdivisions=1))
        parts.append(cube(f"{tag}_NodeBand_{i:02d}", loc, (0.12, 0.12, 0.24), mat,
                          rotation=(sx * 0.35, sy * 0.35, sz * 0.35), bevel=0.035))
    parent_all(parts, root)
    return root, parts


def inner_shell(mat: bpy.types.Material, tag: str) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    rotations = ((0, 0, 0), (math.pi / 2, 0, 0), (0, math.pi / 2, 0))
    for i, rotation in enumerate(rotations):
        parts.append(torus(f"{tag}_InnerOrbit_{i}", 0.78, 0.045, mat, rotation=rotation,
                           major_segments=24, minor_segments=5))
    for i, loc in enumerate(((0.82, 0, 0), (-0.82, 0, 0), (0, 0.82, 0),
                             (0, -0.82, 0), (0, 0, 0.82), (0, 0, -0.82))):
        parts.append(ico_sphere(f"{tag}_InnerNode_{i:02d}", loc, 0.085, mat, subdivisions=1))
    return parts


def weapon_insert(mat, tag):
    parts = [cube(f"{tag}_Receiver", (-0.12, 0, 0.05), (0.62, 0.35, 0.20), mat, bevel=0.08)]
    for i, yy in enumerate((-0.30, 0.0, 0.30)):
        parts.append(cylinder(f"{tag}_Barrel_{i}", (0.58, yy, 0.06), 0.105, 1.35, mat,
                              rotation=(0, math.pi / 2, 0), vertices=20, bevel=0.02))
        parts.append(torus(f"{tag}_Muzzle_{i}", 0.125, 0.035, mat, loc=(1.25, yy, 0.06),
                           rotation=(0, math.pi / 2, 0), major_segments=20, minor_segments=6))
    parts.append(cube(f"{tag}_RearBrace", (-0.72, 0, 0.05), (0.18, 0.52, 0.12), mat, bevel=0.04))
    return parts


def missile_insert(mat, tag):
    parts = [cube(f"{tag}_MissileRack", (-0.08, 0, 0.0), (0.62, 0.58, 0.14), mat, bevel=0.07)]
    for i, yy in enumerate((-0.42, 0, 0.42)):
        parts.append(cylinder(f"{tag}_MissileBody_{i}", (0.08, yy, 0.12), 0.145, 1.45, mat,
                              rotation=(0, math.pi / 2, 0), vertices=20, bevel=0.02))
        bpy.ops.mesh.primitive_cone_add(vertices=20, radius1=0.15, radius2=0.0, depth=0.42,
                                       location=(1.01, yy, 0.12), rotation=(0, math.pi / 2, 0))
        nose = bpy.context.object; nose.name = f"{tag}_Warhead_{i}"; assign_and_unwrap(nose, mat); parts.append(nose)
        for sy in (-1, 1):
            parts.append(cube(f"{tag}_Fin_{i}_{sy}", (-0.62, yy + sy * 0.15, 0.12),
                              (0.18, 0.08, 0.035), mat, rotation=(0, 0, sy * 0.25), bevel=0.018))
    return parts


def extruded_polygon(name, points, depth, mat):
    n = len(points)
    verts = [(x, y, -depth / 2) for x, y in points] + [(x, y, depth / 2) for x, y in points]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, n * 2))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
    mesh = bpy.data.meshes.new(name + "Mesh")
    mesh.from_pydata(verts, [], faces); mesh.update()
    obj = bpy.data.objects.new(name, mesh); bpy.context.collection.objects.link(obj)
    apply_bevel(obj, 0.055, 3); assign_and_unwrap(obj, mat)
    return obj


def shield_insert(mat, tag):
    outline = [(-0.85, 0.85), (0.85, 0.85), (0.78, 0.05), (0.48, -0.62),
               (0, -1.03), (-0.48, -0.62), (-0.78, 0.05)]
    inner = [(x * 0.68, y * 0.68) for x, y in outline]
    a = extruded_polygon(f"{tag}_ShieldOuter", outline, 0.30, mat)
    b = extruded_polygon(f"{tag}_ShieldInner", inner, 0.38, mat); b.location.z = 0.12
    spine = cube(f"{tag}_ShieldSpine", (0, 0.05, 0.32), (0.07, 0.72, 0.055), mat, bevel=0.025)
    rim = torus(f"{tag}_ShieldEnergyRim", 0.96, 0.035, mat, loc=(0, 0, 0.08),
                major_segments=40, minor_segments=6)
    return [a, b, spine, rim]


def repair_insert(mat, tag):
    parts = [cylinder(f"{tag}_NanoCore", (0, 0, 0), 0.72, 0.30, mat, vertices=32, bevel=0.035),
             cube(f"{tag}_CrossH", (0, 0, 0.24), (0.82, 0.24, 0.11), mat, bevel=0.07),
             cube(f"{tag}_CrossV", (0, 0, 0.25), (0.24, 0.82, 0.11), mat, bevel=0.07),
             torus(f"{tag}_NanoRace", 0.79, 0.055, mat, loc=(0, 0, 0.04), major_segments=40, minor_segments=8)]
    for i in range(8):
        a = i * math.tau / 8
        parts.append(cylinder(f"{tag}_Injector_{i}", (0.98 * math.cos(a), 0.98 * math.sin(a), 0),
                              0.065, 0.34, mat, rotation=(0, math.pi / 2, a), vertices=12, bevel=0.01))
    return parts


def rapid_insert(mat, tag):
    parts = [cylinder(f"{tag}_Rotor", (-0.20, 0, 0.04), 0.46, 0.40, mat, vertices=32, bevel=0.04),
             cube(f"{tag}_RotorHousing", (-0.58, 0, 0.02), (0.32, 0.62, 0.18), mat, bevel=0.08)]
    for i in range(6):
        a = i * math.tau / 6
        yy = 0.34 * math.sin(a); zz = 0.10 + 0.20 * math.cos(a)
        parts.append(cylinder(f"{tag}_RapidBarrel_{i}", (0.52, yy, zz), 0.075, 1.50, mat,
                              rotation=(0, math.pi / 2, 0), vertices=16, bevel=0.015))
    for i, yy in enumerate((-0.42, 0, 0.42)):
        parts.append(cube(f"{tag}_EnergyProng_{i}", (1.02, yy, 0.08), (0.46, 0.045, 0.045), mat,
                          bevel=0.025))
    return parts


INSERT_BUILDERS = {
    "weapon": weapon_insert,
    "missiles": missile_insert,
    "shield": shield_insert,
    "repair": repair_insert,
    "rapid": rapid_insert,
}


def rig_variant(tag: str, outer_root: bpy.types.Object, inner_root: bpy.types.Object,
                outer_parts: list[bpy.types.Object], insert_parts: list[bpy.types.Object]) -> bpy.types.Object:
    arm_data = bpy.data.armatures.new(f"PickupRig_{tag.upper()}_Data")
    rig = bpy.data.objects.new(f"PickupRig_{tag.upper()}", arm_data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    outer_bone = arm_data.edit_bones.new("OuterAtomShell")
    outer_bone.head = (0, 0, 0); outer_bone.tail = (0, 0, 0.50)
    inner_bone = arm_data.edit_bones.new("InnerCore")
    inner_bone.head = (0, 0, 0); inner_bone.tail = (0, 0, 0.35)
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.select_set(False)
    for obj, bone_name in [(part, "OuterAtomShell") for part in outer_parts] + [(part, "InnerCore") for part in insert_parts]:
        obj.parent = rig
        group = obj.vertex_groups.new(name=bone_name)
        group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
        modifier = obj.modifiers.new("PickupArmature", "ARMATURE")
        modifier.object = rig
    bpy.data.objects.remove(outer_root, do_unlink=True)
    bpy.data.objects.remove(inner_root, do_unlink=True)
    return rig


def add_spin(rig: bpy.types.Object) -> None:
    bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"
    spin_axis = Vector((1.0, 0.78, 0.56)).normalized()
    for bone_name, turns in (("OuterAtomShell", 1.0), ("InnerCore", -2.0)):
        bone = rig.pose.bones[bone_name]
        bone.rotation_mode = "QUATERNION"
        # Sample the compound rotation throughout the cycle. Keying only the identical
        # start/end orientations lets quaternion export collapse a full turn to no motion.
        for frame in range(1, 62):
            t = (frame - 1) / 60.0
            angle = turns * math.tau * t
            bone.rotation_quaternion = Quaternion(spin_axis, angle)
            bone.keyframe_insert("rotation_quaternion", frame=frame)
    action = rig.animation_data.action
    action.name = "Pickup_Spin"
    track = rig.animation_data.nla_tracks.new()
    track.name = "Pickup_Spin"
    track.strips.new("Pickup_Spin", 1, action)
    rig.animation_data.action = None


def descendants(root: bpy.types.Object) -> list[bpy.types.Object]:
    out = [root]
    for child in root.children:
        out.extend(descendants(child))
    return out


def mesh_triangles(objects: list[bpy.types.Object]) -> int:
    total = 0
    for obj in objects:
        if obj.type == "MESH":
            obj.data.calc_loop_triangles()
            total += len(obj.data.loop_triangles)
    return total


def export_variant(tag: str, rig: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    selected = descendants(rig)
    for obj in selected:
        obj.select_set(True)
        obj.hide_render = False
    bpy.context.view_layer.objects.active = rig
    output = ASSET_DIR / f"pickup_{tag}.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(output), export_format="GLB", use_selection=True,
        export_apply=True, export_normals=True, export_texcoords=True,
        export_materials="EXPORT", export_cameras=False, export_lights=False,
        export_animations=True, export_frame_range=True, export_force_sampling=True,
        export_animation_mode="NLA_TRACKS", export_merge_animation="NLA_TRACK",
        export_nla_strips_merged_animation_name="Pickup_Spin",
        export_image_format="AUTO", export_yup=True,
    )
    for obj in selected:
        obj.select_set(False)


def point_camera(camera: bpy.types.Object, target=(0, 0, 0)) -> None:
    camera.rotation_euler = (Vector(target) - camera.location).to_track_quat("-Z", "Y").to_euler()


def render_previews(roots: dict[str, bpy.types.Object]) -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.world.color = (0.004, 0.008, 0.025)
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    positions = [-7.2, -3.6, 0, 3.6, 7.2]
    for (tag, rig), x in zip(roots.items(), positions):
        rig.location.x = x
        rig.scale = (0.92, 0.92, 0.92)
    bpy.ops.object.camera_add(location=(0, -0.8, 16))
    camera = bpy.context.object; camera.name = "PreviewCamera"; camera.data.type = "ORTHO"; camera.data.ortho_scale = 19.5
    point_camera(camera, (0, 0, 0)); scene.camera = camera
    for loc, energy, size, color in [((-6, -4, 10), 1700, 6, (0.18, 0.38, 1.0)),
                                      ((6, 3, 8), 2200, 5, (1.0, 0.32, 0.08)),
                                      ((0, -1, 12), 1300, 4, (0.5, 0.7, 1.0))]:
        bpy.ops.object.light_add(type="AREA", location=loc)
        light = bpy.context.object; light.data.energy = energy; light.data.shape = "DISK"; light.data.size = size; light.data.color = color
        point_camera(light, (0, 0, 0))
    for tag, x in zip(roots, positions):
        bpy.ops.object.text_add(location=(x, -2.30, 0.1))
        text = bpy.context.object; text.name = f"Label_{tag}"; text.data.body = tag.upper(); text.data.align_x = "CENTER"; text.data.size = 0.42; text.data.extrude = 0.012
        text.data.materials.append(bpy.data.materials["Preview_Label"])
    scene.frame_set(18)
    scene.render.filepath = str(PREVIEW_DIR / "powerup_contact_sheet_1920x1080.png")
    bpy.ops.render.render(write_still=True)

    # Silhouette proof: identical framing, flat black override, white world.
    black = bpy.data.materials.new("Silhouette_Black"); black.diffuse_color = (0, 0, 0, 1); black.use_nodes = True
    bsdf = black.node_tree.nodes.get("Principled BSDF"); bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1); bsdf.inputs["Roughness"].default_value = 1
    scene.view_layers[0].material_override = black
    scene.world.color = (1, 1, 1)
    for obj in bpy.data.objects:
        if obj.name.startswith("Label_") or obj.type == "LIGHT": obj.hide_render = True
    scene.render.resolution_x, scene.render.resolution_y = 640, 128
    scene.render.filepath = str(PREVIEW_DIR / "powerup_silhouettes_640x128.png")
    bpy.ops.render.render(write_still=True)
    scene.view_layers[0].material_override = None
    scene.world.color = (0.004, 0.008, 0.025)
    for obj in bpy.data.objects:
        obj.hide_render = False

    # Lightweight PNG sequence of the exact exported timing. The host encodes it afterward
    # because this Blender distribution is built without its internal FFmpeg format.
    frames_dir = PREVIEW_DIR / "spin_frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 960, 540
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(frames_dir / "frame_")
    scene.frame_start, scene.frame_end, scene.render.fps = 1, 60, 30
    bpy.ops.render.render(animation=True)


def main() -> None:
    ensure_dirs(); clear_scene()
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end, scene.render.fps = 1, 61, 30
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.look = "AgX - Medium High Contrast"
    chassis_image = make_texture("chassis_albedo_1024")
    chassis_mat = material("Pickup_Chassis", chassis_image, 0.82, 0.28)
    label_mat = bpy.data.materials.new("Preview_Label"); label_mat.diffuse_color = (0.65, 0.85, 1.0, 1)
    roots: dict[str, bpy.types.Object] = {}
    report_lines = []
    for tag, color in VARIANTS.items():
        insert_image = make_texture(f"{tag}_albedo_1024", color)
        insert_mat = material(f"Pickup_Insert_{tag.upper()}", insert_image, 0.42, 0.22, 0.9)
        outer, outer_parts = chassis(chassis_mat, tag)
        inner = bpy.data.objects.new(f"InnerInsert_{tag.upper()}", None); bpy.context.collection.objects.link(inner)
        symbol_parts = INSERT_BUILDERS[tag](insert_mat, tag)
        scale_parts(symbol_parts, 0.52)
        insert_parts = inner_shell(insert_mat, tag) + symbol_parts
        parent_all(insert_parts, inner)
        rig = rig_variant(tag, outer, inner, outer_parts, insert_parts)
        add_spin(rig)
        roots[tag] = rig
        tri = mesh_triangles(outer_parts + insert_parts)
        report_lines.append(f"{tag}: {tri} triangles")
        export_variant(tag, rig)
    render_previews(roots)
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE_DIR / "powerup_variants.blend"))
    (SOURCE_DIR / "creation_report.txt").write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print("\n".join(report_lines))


if __name__ == "__main__":
    main()
