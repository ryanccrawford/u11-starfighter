"""Build the editable Blender title scene and its runtime 3D models.

Run with the repository's Blender installation:
  E:\\blender\\blender.exe --background --python tools\\build_title_scene.py

The existing title artwork is retained only as a non-rendering composition guide. The supplied
displaced exoplanet contributes its surface, atmosphere shell, albedo, and normal maps. Its 8K maps
are resampled to 4K for runtime. The 3D wordmark is exported as individual Blender-made glyphs.
"""
from pathlib import Path
import math
import os
import random

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parent.parent
TITLE = ROOT / "assets" / "title"
MODELS = ROOT / "assets" / "models"
REFERENCE = TITLE / "title_space.png"
FONT = ROOT / "assets" / "fonts" / "Oxanium-variable.ttf"
SOURCE_PLANET = Path(os.environ.get(
    "U11_TITLE_PLANET_SOURCE",
    r"C:\Users\Administrator\Downloads\exoplanet_8k_displaced_pbr.glb",
))
SURFACE = MODELS / "title_exoplanet_surface_4k.glb"
ATMOSPHERE = MODELS / "title_exoplanet_atmosphere_4k.glb"
GLYPH_DIR = MODELS / "title_glyphs"
OUT_BLEND = TITLE / "title_scene.blend"
OUT_PREVIEW = TITLE / "title_scene_preview.png"


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        if collection.name != "Collection" and collection.users == 0:
            bpy.data.collections.remove(collection)


def move_to_collection(obj, collection):
    for old in list(obj.users_collection):
        old.objects.unlink(obj)
    collection.objects.link(obj)


def principled_material(name, base, metallic=0.0, roughness=0.5, emission=None, strength=0.0, alpha=1.0):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*base, alpha)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*base, alpha)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        if "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        elif "Emission" in bsdf.inputs:
            bsdf.inputs["Emission"].default_value = (*emission, 1.0)
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs["Emission Strength"].default_value = strength
    if alpha < 1.0:
        mat.surface_render_method = "DITHERED"
    return mat


def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def build_wordmark(scene_collection):
    font = bpy.data.fonts.load(str(FONT), check_existing=True)
    silver = principled_material("Wordmark | cool titanium", (0.48, 0.74, 0.88), 0.72, 0.24)
    gold = principled_material("Wordmark | sunset gold", (1.0, 0.55, 0.10), 0.54, 0.28)
    glyphs = []

    def measure(text, size):
        curve = bpy.data.curves.new("Temporary title measure", "FONT")
        curve.body = text
        curve.font = font
        curve.size = size
        curve.align_x = "LEFT"
        curve.align_y = "CENTER"
        curve.space_character = 1.12
        obj = bpy.data.objects.new("Temporary title measure", curve)
        scene_collection.objects.link(obj)
        bpy.context.view_layer.update()
        width = obj.dimensions.x
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.curves.remove(curve)
        return width

    for line, label, text, size, z, mat in (
        (0, "U11", "U11", 2.05, 0.22, silver),
        (1, "STARFIGHTER", "STARFIGHTER", 0.90, -0.78, gold),
    ):
        line_width = measure(text, size)
        left = -line_width * 0.5
        for index, character in enumerate(text):
            before = measure(text[:index], size) if index else 0.0
            after = measure(text[:index + 1], size)
            advance = after - before
            curve = bpy.data.curves.new(f"Title glyph {line}-{index}", "FONT")
            curve.body = character
            curve.font = font
            curve.size = size
            curve.align_x = "CENTER"
            curve.align_y = "CENTER"
            curve.space_character = 1.12
            curve.extrude = 0.075
            curve.bevel_depth = 0.022
            curve.bevel_resolution = 3
            obj = bpy.data.objects.new(f"TITLE_{label}_{index:02d}_{character}", curve)
            scene_collection.objects.link(obj)
            obj.location = (left + before + advance * 0.5, 0, z)
            obj.rotation_euler[0] = math.radians(90.0)
            obj.data.materials.append(mat)
            bpy.context.view_layer.objects.active = obj
            obj.select_set(True)
            bpy.ops.object.convert(target="MESH")
            obj = bpy.context.object
            obj.name = f"TITLE_{label}_{index:02d}_{character}"
            for poly in obj.data.polygons:
                poly.use_smooth = True
            obj.select_set(False)
            glyphs.append((line, index, obj))
    return glyphs


def import_model(path, collection, name, location, scale):
    bpy.ops.import_scene.gltf(filepath=str(path))
    imported = list(bpy.context.selected_objects)
    if not imported:
        raise RuntimeError(f"Blender imported no objects from {path}")
    for obj in imported:
        move_to_collection(obj, collection)
        obj.name = f"{name} | {obj.name}"
        obj.location = location
        obj.scale = (scale, scale, scale)
    return imported


def export_title_planet():
    """Preserve the supplied planet and shell materials, with original maps resampled to 4K."""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.import_scene.gltf(filepath=str(SOURCE_PLANET))
    imported = list(bpy.context.selected_objects)
    surface = [o for o in imported if o.type == "MESH" and o.name == "Displaced_Exoplanet_Surface"]
    atmosphere = [o for o in imported if o.type == "MESH" and o.name == "Atmosphere_Glow_Shell"]
    if len(surface) != 1 or len(atmosphere) != 1:
        raise RuntimeError(f"Expected the displaced surface and atmosphere shell in {SOURCE_PLANET}")

    planet_objects = [surface[0], atmosphere[0]]
    images = {node.image for obj in planet_objects for mat in obj.data.materials if mat and mat.use_nodes
              for node in mat.node_tree.nodes if node.type == "TEX_IMAGE" and node.image}
    if not images:
        raise RuntimeError("The supplied planet's albedo and normal maps were not found")
    for image in images:
        if tuple(image.size) == (8192, 4096):
            image.scale(4096, 2048)
        elif tuple(image.size) != (4096, 2048):
            raise RuntimeError(f"Unexpected source map dimensions for {image.name}: {tuple(image.size)}")
        image.pack()
        print(f"TITLE_MAP {image.name} {image.size[0]}x{image.size[1]}")

    for obj, path in ((surface[0], SURFACE), (atmosphere[0], ATMOSPHERE)):
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = world
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        bpy.ops.export_scene.gltf(
            filepath=str(path), export_format="GLB", use_selection=True,
            export_materials="EXPORT", export_image_format="AUTO",
            export_lights=False, export_cameras=False, export_animations=False,
            export_apply=True, export_tangents=True,
        )
        print(f"TITLE_PLANET_PART {path}")

    for obj in imported:
        if obj.name in bpy.data.objects:
            bpy.data.objects.remove(obj, do_unlink=True)


def export_glyphs(glyphs):
    GLYPH_DIR.mkdir(parents=True, exist_ok=True)
    for line, index, obj in glyphs:
        file_name = ("u11" if line == 0 else "starfighter") + f"_{index:02d}.glb"
        path = GLYPH_DIR / file_name
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(
            filepath=str(path), export_format="GLB", use_selection=True,
            export_materials="EXPORT", export_image_format="AUTO",
            export_lights=False, export_cameras=False, export_animations=False,
            export_apply=True,
        )


def add_reference(collection):
    image = bpy.data.images.load(str(REFERENCE), check_existing=True)
    image.pack()
    bpy.ops.object.empty_add(type="IMAGE", location=(0, 8.5, 0))
    ref = bpy.context.object
    ref.name = "REFERENCE ONLY | previous title composition"
    ref.data = image
    ref.empty_display_size = 15.0
    ref.empty_image_depth = "BACK"
    ref.show_in_front = False
    ref.hide_render = True
    move_to_collection(ref, collection)


def build_scene():
    if not REFERENCE.is_file() or not FONT.is_file() or not SOURCE_PLANET.is_file():
        raise FileNotFoundError("Title reference, font, or supplied displaced exoplanet is missing")
    clear_scene()
    export_title_planet()
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.world.color = (0.001, 0.002, 0.006)
    scene.render.filepath = str(OUT_PREVIEW)
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.render.resolution_percentage = 80

    scene_collection = bpy.data.collections.new("TITLE SCENE | runtime design reference")
    scene.collection.children.link(scene_collection)
    reference_collection = bpy.data.collections.new("REFERENCE | hidden from render")
    scene.collection.children.link(reference_collection)
    logo_collection = bpy.data.collections.new("TITLE LOGO | exported to runtime")
    scene.collection.children.link(logo_collection)
    add_reference(reference_collection)

    # Runtime uses both supplied planet meshes and their imported materials/maps.
    import_model(SURFACE, scene_collection, "Supplied displaced planet", (4.3, 0, 0), 3.7)
    import_model(ATMOSPHERE, scene_collection, "Supplied atmosphere shell", (4.3, 0, 0), 3.7)

    sun_mat = principled_material("Setting sun | amber plasma", (1.0, 0.34, 0.08), 0.0, 0.26,
                                  (1.0, 0.25, 0.035), 6.0)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=32, radius=0.42, location=(0.2, -7.2, 1.0))
    sun = bpy.context.object
    sun.name = "Sun at the planetary limb"
    sun.data.materials.append(sun_mat)
    bpy.ops.object.shade_smooth()
    move_to_collection(sun, scene_collection)

    # A star streak field in the authoring scene mirrors the runtime's independently moving far stars.
    rng = random.Random(1101)
    star_verts, star_edges = [], []
    for _ in range(1200):
        x = rng.uniform(-17.0, 17.0)
        y = rng.uniform(12.0, 22.0)
        z = rng.uniform(-7.0, 8.0)
        length = rng.uniform(0.025, 0.13)
        start = len(star_verts)
        star_verts.extend(((x, y, z), (x - length, y, z)))
        star_edges.append((start, start + 1))
    star_mesh = bpy.data.meshes.new("Far star streak guides")
    star_mesh.from_pydata(star_verts, star_edges, [])
    stars = bpy.data.objects.new("Far stars | composition guide", star_mesh)
    scene_collection.objects.link(stars)
    stars.data.materials.append(principled_material("Starlight", (0.70, 0.86, 1.0), 0.0, 0.2,
                                                    (0.35, 0.55, 1.0), 1.8))

    glyphs = build_wordmark(logo_collection)

    key = bpy.data.lights.new("Warm sunset key", "AREA")
    key.energy = 1800
    key.color = (1.0, 0.46, 0.20)
    key.shape = "DISK"
    key.size = 4.0
    key_obj = bpy.data.objects.new("Warm sunset key", key)
    scene_collection.objects.link(key_obj)
    key_obj.location = (-2.5, -4.5, 5.5)
    look_at(key_obj, (4.3, 0, 0))

    camera_data = bpy.data.cameras.new("Title scene camera")
    camera = bpy.data.objects.new("Title scene camera", camera_data)
    scene_collection.objects.link(camera)
    camera.location = (0.0, -17.0, 3.6)
    look_at(camera, (0.0, 0.0, 0.3))
    camera_data.lens = 42
    scene.camera = camera
    scene.render.image_settings.color_mode = "RGBA"

    # Save the full editable scene and export each 3D glyph as a separate runtime model.
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND))
    export_glyphs(glyphs)
    scene.render.filepath = str(OUT_PREVIEW)
    bpy.ops.render.render(write_still=True)
    print(f"TITLE_BLEND {OUT_BLEND}")
    print(f"TITLE_PREVIEW {OUT_PREVIEW}")


build_scene()
