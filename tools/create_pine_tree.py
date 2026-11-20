import bpy
import math
import os
import random
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "assets", "pine_tree")
os.makedirs(OUT, exist_ok=True)
random.seed(18473)

def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
        for block in list(datablocks):
            if block.users == 0:
                datablocks.remove(block)

def make_texture(name, path, base, variation, roughness=False, size=1024):
    img = bpy.data.images.new(name, width=size, height=size, alpha=False)
    px = [0.0] * (size * size * 4)
    for y in range(size):
        for x in range(size):
            i = (y * size + x) * 4
            n = random.uniform(-variation, variation)
            if roughness:
                v = max(0.0, min(1.0, base + n))
                px[i:i+4] = [v, v, v, 1.0]
            else:
                px[i:i+4] = [max(0.0, min(1.0, c + n)) for c in base] + [1.0]
    img.pixels = px
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.pack()
    img.save()
    return img

def image_material(name, image, roughness_image, metallic=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = image
    rough = nt.nodes.new('ShaderNodeTexImage')
    rough.image = roughness_image
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(rough.outputs['Color'], bsdf.inputs['Roughness'])
    bsdf.inputs['Metallic'].default_value = metallic
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    return mat

def add_cone(name, radius1, radius2, depth, loc, material, verts=24, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius1, radius2=radius2, depth=depth, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(material)
    return obj

def add_ellipsoid(name, loc, scale, material):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1.0, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(material)
    for poly in obj.data.polygons:
        poly.use_smooth = True
    return obj

def add_branch(a, b, r1, r2, material):
    a, b = Vector(a), Vector(b)
    delta = b - a
    mid = (a + b) * 0.5
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=r1, radius2=r2, depth=delta.length, location=mid)
    obj = bpy.context.object
    obj.name = 'Pine_Branch'
    obj.data.materials.append(material)
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = delta.to_track_quat('Z', 'Y')
    return obj

def join_objects(objects, name):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    objects[0].name = name
    return objects[0]

def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()

clear_scene()

bark = make_texture('pine_bark_albedo', os.path.join(OUT, 'pine_bark_albedo.png'), (0.18, 0.055, 0.018), 0.07)
bark_r = make_texture('pine_bark_roughness', os.path.join(OUT, 'pine_bark_roughness.png'), 0.82, 0.08, roughness=True)
needles = make_texture('pine_needles_albedo', os.path.join(OUT, 'pine_needles_albedo.png'), (0.025, 0.16, 0.035), 0.055)
needles_r = make_texture('pine_needles_roughness', os.path.join(OUT, 'pine_needles_roughness.png'), 0.76, 0.08, roughness=True)
bark_mat = image_material('Pine_Bark', bark, bark_r)
needle_mat = image_material('Pine_Needles', needles, needles_r)

trunk = add_cone('Pine_Trunk', 0.78, 0.24, 18.0, (0, 0, 9.0), bark_mat, verts=32)
trunk.rotation_euler[2] = math.radians(8)

foliage = []
branches = []
levels = 9
for level in range(levels):
    z = 5.8 + level * 1.32
    t = level / (levels - 1)
    radius = 4.8 * (1.0 - t) ** 0.55 + 0.55
    count = 8 if level < 5 else 7
    for i in range(count):
        ang = 2 * math.pi * i / count + random.uniform(-0.18, 0.18)
        length = radius * random.uniform(0.72, 1.02)
        start = (0, 0, z)
        end = (math.cos(ang) * length, math.sin(ang) * length, z + random.uniform(-0.12, 0.18))
        branches.append(add_branch(start, end, 0.13 * (1.0 - 0.45*t), 0.045, bark_mat))
        # Overlapping soft needle masses create a natural, irregular silhouette.
        pad_center = (end[0] * 0.94, end[1] * 0.94, end[2] + random.uniform(-0.15, 0.28))
        pad = add_ellipsoid('Pine_Foliage_Pad', pad_center,
                            (radius * 0.10, radius * 0.07, radius * 0.06), needle_mat)
        pad.rotation_euler[2] = ang
        foliage.append(pad)
        for spray_i in range(30):
            spray_ang = math.tau * spray_i / 30.0 + random.uniform(-0.12, 0.12)
            spray_start = pad_center
            spray_end = (pad_center[0] + math.cos(spray_ang) * radius * random.uniform(0.32, 0.74),
                         pad_center[1] + math.sin(spray_ang) * radius * random.uniform(0.32, 0.74),
                         pad_center[2] + random.uniform(-radius * 0.26, radius * 0.30))
            foliage.append(add_branch(spray_start, spray_end, 0.036, 0.003, needle_mat))
        # Short needle sprays break up the smooth masses and create a believable pine silhouette.
        for needle_i in range(7):
            needle_ang = math.tau * needle_i / 7.0 + random.uniform(-0.22, 0.22)
            needle_start = (end[0] * 0.74, end[1] * 0.74, z + random.uniform(-0.12, 0.32))
            needle_end = (end[0] * 0.98 + math.cos(needle_ang) * radius * 0.12,
                          end[1] * 0.98 + math.sin(needle_ang) * radius * 0.12,
                          z + random.uniform(-0.10, 0.45))
            foliage.append(add_branch(needle_start, needle_end, 0.075, 0.012, needle_mat))
        tip = (end[0] * 0.78, end[1] * 0.78, end[2] + 0.08)
        twig_end = (end[0] * 1.04, end[1] * 1.04, end[2] + 0.24)
        branches.append(add_branch(end, twig_end, 0.055, 0.018, bark_mat))
    if level >= levels - 2:
        pad = add_ellipsoid('Pine_Foliage_Crown', (random.uniform(-0.25, 0.25), random.uniform(-0.25, 0.25), z + 0.45),
                            (radius * 0.38, radius * 0.30, radius * 0.22), needle_mat)
        pad.rotation_euler[2] = random.uniform(0, math.tau)
        foliage.append(pad)

join_objects(foliage, 'Pine_Needle_Canopy')
join_objects(branches, 'Pine_Branches')

# Small root flare for a grounded, realistic silhouette.
roots = []
for i in range(8):
    ang = math.tau * i / 8
    roots.append(add_branch((0, 0, 0.35), (math.cos(ang)*0.95, math.sin(ang)*0.95, 0.03), 0.18, 0.035, bark_mat))
join_objects(roots, 'Pine_Root_Flare')

# Ground plane for the preview render only; it is excluded from the GLB.
bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.03))
ground = bpy.context.object
ground.name = 'Preview_Ground'
gmat = bpy.data.materials.new('Preview_Ground_Material')
gmat.diffuse_color = (0.035, 0.045, 0.028, 1)
gmat.use_nodes = True
gmat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (0.035, 0.045, 0.028, 1)
gmat.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value = 0.95
ground.data.materials.append(gmat)

# Camera and studio lighting.
bpy.ops.object.camera_add(location=(27.0, -31.0, 15.5))
cam = bpy.context.object
cam.data.lens = 58
look_at(cam, (0, 0, 8.5))
bpy.context.scene.camera = cam
bpy.ops.object.light_add(type='AREA', location=(5.5, -7.5, 15))
key = bpy.context.object
key.data.energy = 1500
key.data.shape = 'DISK'
key.data.size = 7
look_at(key, (0, 0, 8))
bpy.ops.object.light_add(type='AREA', location=(-8, -1, 8))
fill = bpy.context.object
fill.data.energy = 650
fill.data.size = 8
look_at(fill, (0, 0, 8))
bpy.ops.object.light_add(type='SUN', location=(0, 0, 12))
sun = bpy.context.object
sun.data.energy = 1.6
sun.rotation_euler = (math.radians(28), math.radians(-22), math.radians(28))

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 768
scene.render.resolution_y = 768
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = os.path.join(OUT, 'pine_tree_render.png')
scene.render.film_transparent = False
scene.world.color = (0.012, 0.018, 0.012)
scene.render.image_settings.color_mode = 'RGBA'

# Save the production scene with packed maps, and export a clean GLB without preview-only objects.
blend_path = os.path.join(OUT, 'pine_tree.blend')
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
bpy.ops.render.render(write_still=True)

ground.hide_render = True
cam.hide_render = True
key.hide_render = True
fill.hide_render = True
sun.hide_render = True
for obj in list(bpy.context.scene.objects):
    obj.select_set(False)
for obj in (trunk, bpy.data.objects.get('Pine_Branches'), bpy.data.objects.get('Pine_Needle_Canopy'), bpy.data.objects.get('Pine_Root_Flare')):
    if obj:
        obj.select_set(True)
bpy.context.view_layer.objects.active = trunk
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'pine_tree.glb'), export_format='GLB', use_selection=True, export_apply=True, export_materials='EXPORT', export_image_format='AUTO')

print('PINE_TREE_OUTPUT', OUT)
print('PINE_TREE_GLB', os.path.join(OUT, 'pine_tree.glb'))
print('PINE_TREE_RENDER', os.path.join(OUT, 'pine_tree_render.png'))
