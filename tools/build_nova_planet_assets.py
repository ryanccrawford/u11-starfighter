"""Build the compact, embedded-texture GLBs used by the runtime planet renderer."""
import bpy
import os

ROOT = r"C:\Users\Administrator\Desktop\U11-unlock\shooter"
MODELS = os.path.join(ROOT, "assets", "models")
SOURCE = r"C:\Users\Administrator\Documents\ChatGPT\3dgame\2026-11-07\cr\outputs"
SURFACE = os.path.join(SOURCE, "nova_surface_art_source.png")
WEATHER = os.path.join(SOURCE, "nova_weather_4k.png")


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def export_selected(path):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = bpy.context.object
    bpy.context.object.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=path,
        export_format="GLB",
        use_selection=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_lights=False,
        export_cameras=False,
        export_animations=False,
    )


def image_texture(path, name):
    image = bpy.data.images.load(path, check_existing=False)
    image.name = name
    image.scale(2048, 1024)
    image.pack()
    return image


clear_scene()
surface = image_texture(SURFACE, "NOVA 2K detailed surface")
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=30, radius=1.0)
planet = bpy.context.object
planet.name = "NOVA Planet Surface (2688 tris)"
bpy.ops.object.shade_smooth()
mat = bpy.data.materials.new("NOVA matte planetary surface")
mat.use_nodes = True
nodes, links = mat.node_tree.nodes, mat.node_tree.links
principled = nodes.get("Principled BSDF")
principled.inputs["Roughness"].default_value = 0.86
if "Specular IOR Level" in principled.inputs:
    principled.inputs["Specular IOR Level"].default_value = 0.10
tex = nodes.new("ShaderNodeTexImage")
tex.image = surface
tex.interpolation = "Linear"
links.new(tex.outputs["Color"], principled.inputs["Base Color"])
planet.data.materials.append(mat)
export_selected(os.path.join(MODELS, "nova_planet_surface.glb"))

clear_scene()
weather = image_texture(WEATHER, "NOVA 2K cloud and cyclone coverage")
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1.012)
clouds = bpy.context.object
clouds.name = "NOVA Cloud Shell (960 tris)"
bpy.ops.object.shade_smooth()
cloud_mat = bpy.data.materials.new("NOVA translucent drifting clouds")
cloud_mat.use_nodes = True
nodes, links = cloud_mat.node_tree.nodes, cloud_mat.node_tree.links
principled = nodes.get("Principled BSDF")
principled.inputs["Roughness"].default_value = 1.0
principled.inputs["Metallic"].default_value = 0.0
cloud_tex = nodes.new("ShaderNodeTexImage")
cloud_tex.image = weather
cloud_tex.interpolation = "Linear"
links.new(cloud_tex.outputs["Color"], principled.inputs["Base Color"])
links.new(cloud_tex.outputs["Alpha"], principled.inputs["Alpha"])
cloud_mat.surface_render_method = "DITHERED"
clouds.data.materials.append(cloud_mat)
export_selected(os.path.join(MODELS, "nova_planet_clouds.glb"))
