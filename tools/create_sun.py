import bpy
import math
import os
import random
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "assets", "sun")
os.makedirs(OUT, exist_ok=True)
random.seed(91827)

def clear():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for coll in (bpy.data.meshes, bpy.data.curves, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
        for item in list(coll):
            if item.users == 0:
                coll.remove(item)

def look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()

def emission_mat(name, color, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()
    out = nodes.new('ShaderNodeOutputMaterial')
    em = nodes.new('ShaderNodeEmission')
    em.inputs['Color'].default_value = (*color, 1)
    em.inputs['Strength'].default_value = strength
    mat.node_tree.links.new(em.outputs['Emission'], out.inputs['Surface'])
    return mat

def volume_mat(name, density, color, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()
    out = nodes.new('ShaderNodeOutputMaterial')
    vol = nodes.new('ShaderNodeVolumePrincipled')
    vol.inputs['Density'].default_value = density
    vol.inputs['Color'].default_value = (*color, 1)
    if 'Emission Color' in vol.inputs:
        vol.inputs['Emission Color'].default_value = (*color, 1)
    if 'Emission Strength' in vol.inputs:
        vol.inputs['Emission Strength'].default_value = strength
    mat.node_tree.links.new(vol.outputs['Volume'], out.inputs['Volume'])
    return mat

def make_sun_material():
    mat = bpy.data.materials.new('Sun_Surface_Animated')
    mat.use_nodes = True
    n = mat.node_tree.nodes
    l = mat.node_tree.links
    n.clear()
    out = n.new('ShaderNodeOutputMaterial')
    em = n.new('ShaderNodeEmission')
    tex = n.new('ShaderNodeTexNoise')
    tex.inputs['Scale'].default_value = 6.5
    tex.inputs['Detail'].default_value = 11.0
    tex.inputs['Roughness'].default_value = 0.82
    tex.inputs['Distortion'].default_value = 0.35
    mapping = n.new('ShaderNodeMapping')
    coord = n.new('ShaderNodeTexCoord')
    mapping.inputs['Scale'].default_value = (1.0, 3.2, 1.0)
    ramp = n.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.18
    ramp.color_ramp.elements[0].color = (0.32, 0.006, 0.0002, 1)
    ramp.color_ramp.elements[1].position = 0.84
    ramp.color_ramp.elements[1].color = (1.0, 0.18, 0.004, 1)
    hot = n.new('ShaderNodeValToRGB')
    hot.color_ramp.elements[0].position = 0.32
    hot.color_ramp.elements[0].color = (0.86, 0.025, 0.001, 1)
    hot.color_ramp.elements[1].position = 0.70
    hot.color_ramp.elements[1].color = (1.0, 0.82, 0.08, 1)
    micro = n.new('ShaderNodeTexNoise')
    micro.inputs['Scale'].default_value = 28.0
    micro.inputs['Detail'].default_value = 4.0
    micro.inputs['Roughness'].default_value = 0.9
    micro_ramp = n.new('ShaderNodeValToRGB')
    micro_ramp.color_ramp.elements[0].position = 0.30
    micro_ramp.color_ramp.elements[0].color = (0.45, 0.02, 0.001, 1)
    micro_ramp.color_ramp.elements[1].position = 0.68
    micro_ramp.color_ramp.elements[1].color = (1.0, 0.32, 0.006, 1)
    detail_mix = n.new('ShaderNodeMixRGB')
    detail_mix.blend_type = 'MULTIPLY'
    detail_mix.inputs[0].default_value = 0.55
    spot_noise = n.new('ShaderNodeTexNoise')
    spot_noise.inputs['Scale'].default_value = 2.2
    spot_noise.inputs['Detail'].default_value = 3.5
    spot_noise.inputs['Roughness'].default_value = 0.72
    spot_ramp = n.new('ShaderNodeValToRGB')
    spot_ramp.color_ramp.elements[0].position = 0.36
    spot_ramp.color_ramp.elements[0].color = (0.0, 0.0, 0.0, 1)
    spot_ramp.color_ramp.elements[1].position = 0.58
    spot_ramp.color_ramp.elements[1].color = (1.0, 1.0, 1.0, 1)
    spot_mix = n.new('ShaderNodeMixRGB')
    spot_mix.blend_type = 'MIX'
    spot_mix.inputs[1].default_value = (0.10, 0.001, 0.00005, 1)
    mix = n.new('ShaderNodeMixRGB')
    mix.blend_type = 'MULTIPLY'
    mix.inputs[0].default_value = 0.72
    l.new(coord.outputs['Generated'], mapping.inputs['Vector'])
    l.new(mapping.outputs['Vector'], tex.inputs['Vector'])
    l.new(mapping.outputs['Vector'], micro.inputs['Vector'])
    l.new(mapping.outputs['Vector'], spot_noise.inputs['Vector'])
    l.new(tex.outputs['Fac'], ramp.inputs['Fac'])
    l.new(tex.outputs['Fac'], hot.inputs['Fac'])
    l.new(micro.outputs['Fac'], micro_ramp.inputs['Fac'])
    l.new(ramp.outputs['Color'], mix.inputs[1])
    l.new(hot.outputs['Color'], mix.inputs[2])
    l.new(mix.outputs['Color'], detail_mix.inputs[1])
    l.new(micro_ramp.outputs['Color'], detail_mix.inputs[2])
    l.new(detail_mix.outputs['Color'], spot_mix.inputs[2])
    l.new(spot_noise.outputs['Fac'], spot_ramp.inputs['Fac'])
    l.new(spot_ramp.outputs['Color'], spot_mix.inputs[0])
    l.new(spot_mix.outputs['Color'], em.inputs['Color'])
    em.inputs['Strength'].default_value = 2.3
    l.new(em.outputs['Emission'], out.inputs['Surface'])
    # Animate convection by slowly moving the mapping coordinates.
    mapping.inputs['Location'].default_value = (0, 0, 0)
    mapping.inputs['Location'].keyframe_insert('default_value', frame=1, index=0)
    mapping.inputs['Location'].default_value[0] = 0.8
    mapping.inputs['Location'].default_value[1] = -0.35
    mapping.inputs['Location'].keyframe_insert('default_value', frame=120, index=0)
    mapping.inputs['Location'].keyframe_insert('default_value', frame=120, index=1)
    return mat

def add_curve(name, points, bevel, material):
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = bevel
    cu.bevel_resolution = 3
    sp = cu.splines.new('BEZIER')
    sp.bezier_points.add(len(points) - 1)
    for bp, co in zip(sp.bezier_points, points):
        bp.co = co
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'
    ob = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(ob)
    ob.data.materials.append(material)
    return ob

clear()

sun_mat = make_sun_material()
spot_mat = emission_mat('Sunspot_Emission', (0.12, 0.002, 0.0001), 0.35)
flare_mat = emission_mat('Solar_Flare_Emission', (1.0, 0.065, 0.0015), 42.0)
hot_mat = emission_mat('Flare_Hot_Core', (1.0, 0.58, 0.025), 70.0)
corona_mat = volume_mat('Solar_Corona_Volume', 0.012, (1.0, 0.035, 0.001), 1.4)

bpy.ops.mesh.primitive_uv_sphere_add(segments=128, ring_count=64, radius=5.0, location=(0, 0, 0))
sun = bpy.context.object
sun.name = 'Animated_Sun'
sun.data.materials.append(sun_mat)
for poly in sun.data.polygons:
    poly.use_smooth = True
sun.rotation_euler[2] = 0
sun.keyframe_insert('rotation_euler', index=2, frame=1)
sun.rotation_euler[2] = math.tau * 0.18
sun.keyframe_insert('rotation_euler', index=2, frame=120)

# A true 3D emitting volume surrounding the photosphere.
bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=5.40, location=(0, 0, 0))
corona = bpy.context.object
corona.name = 'Volumetric_Solar_Corona'
corona.data.materials.append(corona_mat)
for poly in corona.data.polygons:
    poly.use_smooth = True

# Sunspots: dark, slightly raised patches that rotate with the surface.
spot_specs = [(-1.8, 1.0, 0.35), (1.5, 0.6, 0.28), (2.3, -1.0, 0.34), (-0.8, -1.4, 0.24), (0.2, -0.2, 0.20)]
spots = []
for i, (x, z, s) in enumerate(spot_specs):
    y = -math.sqrt(max(0.01, 25.0 - x*x - z*z))
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=s, location=(x, y - 0.06, z))
    ob = bpy.context.object
    ob.name = f'Sunspot_{i+1:02d}'
    ob.scale = (1.55, 0.16, 1.0)
    ob.data.materials.append(spot_mat)
    ob.hide_render = True
    ob.hide_viewport = True
    spots.append(ob)

# Curved 3D prominences distributed around the sphere. Each flare gets a looping scale animation.
flares = []
flare_specs = [(0.4, 1.15, 1.4), (1.7, 2.25, 1.0), (3.9, 0.4, 0.8), (4.8, 2.4, 0.7), (5.4, 4.3, 0.9), (0.7, 4.9, 0.65)]
for i, (theta, phi, height) in enumerate(flare_specs):
    base = Vector((math.sin(phi) * math.cos(theta), math.sin(phi) * math.sin(theta), math.cos(phi)))
    tangent = Vector((-math.sin(theta), math.cos(theta), 0.0)).normalized()
    up = base.cross(tangent).normalized()
    p0 = base * 4.96
    p1 = base * 5.45 + tangent * (0.35 + 0.1 * i)
    p2 = base * (5.7 + height * 0.35) + tangent * (0.3 + 0.15 * i) + up * height
    p3 = base * 5.20 + tangent * (0.7 + 0.1 * i) + up * (height * 0.35)
    arc = add_curve(f'Solar_Prominence_{i+1:02d}', [p0, p1, p2, p3], 0.055, flare_mat)
    arc.scale = (1, 1, 0.25)
    arc.keyframe_insert('scale', frame=1)
    arc.scale.z = 1.0
    arc.keyframe_insert('scale', frame=35 + i * 12)
    arc.scale.z = 0.45
    arc.keyframe_insert('scale', frame=70 + i * 12)
    arc.scale.z = 0.85
    arc.keyframe_insert('scale', frame=120)
    flares.append(arc)
    # Hot core following the same arc.
    core = add_curve(f'Flare_Core_{i+1:02d}', [p0, p1, p2], 0.018, hot_mat)
    core.scale = (1, 1, 0.25)
    core.keyframe_insert('scale', frame=1)
    core.scale.z = 1.0
    core.keyframe_insert('scale', frame=35 + i * 12)
    core.scale.z = 0.45
    core.keyframe_insert('scale', frame=70 + i * 12)
    core.scale.z = 0.85
    core.keyframe_insert('scale', frame=120)
    flares.append(core)

# Subtle star field for the space-facing render.
star_mat = emission_mat('Star_Emission', (0.8, 0.9, 1.0), 3.5)
for i in range(110):
    theta = random.random() * math.tau
    phi = math.acos(random.uniform(-1, 1))
    r = 42.0
    loc = (r * math.sin(phi) * math.cos(theta), r * math.sin(phi) * math.sin(theta), r * math.cos(phi))
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=random.uniform(0.015, 0.045), location=loc)
    star = bpy.context.object
    star.name = 'Star'
    star.data.materials.append(star_mat)

bpy.ops.object.camera_add(location=(0, -24.5, 0.2))
cam = bpy.context.object
cam.data.lens = 52
look_at(cam, (0, 0, 0))
bpy.context.scene.camera = cam
cam.keyframe_insert('location', frame=1)
cam.keyframe_insert('rotation_euler', frame=1)
cam.location = (21.0, -15.0, 9.0)
look_at(cam, (0, 0, 0))
cam.keyframe_insert('location', frame=40)
cam.keyframe_insert('rotation_euler', frame=40)
cam.location = (0.0, 23.0, -6.0)
look_at(cam, (0, 0, 0))
cam.keyframe_insert('location', frame=80)
cam.keyframe_insert('rotation_euler', frame=80)
cam.location = (-22.0, -12.0, 8.0)
look_at(cam, (0, 0, 0))
cam.keyframe_insert('location', frame=120)
cam.keyframe_insert('rotation_euler', frame=120)
bpy.ops.object.light_add(type='AREA', location=(0, -12, 8))
bpy.context.object.data.energy = 80

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 800
scene.render.resolution_y = 800
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = os.path.join(OUT, 'sun_render.png')
scene.world.color = (0.001, 0.0002, 0.0001)
scene.frame_start = 1
scene.frame_end = 120
scene.render.fps = 24
scene.render.film_transparent = False
scene.view_settings.view_transform = 'Standard'

# Glow is carried by high-intensity emission so the scene remains portable across Blender 5.x.

blend_path = os.path.join(OUT, 'sun.blend')
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
bpy.ops.render.render(write_still=True)

# Export the animated sun, spots, and flare curves as a GLB.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'sun.glb'), export_format='GLB', use_selection=True, export_apply=True, export_materials='EXPORT', export_image_format='AUTO', export_animations=True)

# Render a compact 48-frame preview sequence of the flare/rotation animation.
scene.render.image_settings.file_format = 'PNG'
scene.frame_end = 48
frames_dir = os.path.join(OUT, 'sun_frames')
os.makedirs(frames_dir, exist_ok=True)
scene.render.filepath = os.path.join(frames_dir, 'frame_')
scene.render.resolution_percentage = 50
bpy.ops.render.render(animation=True)

print('SUN_OUTPUT', OUT)
print('SUN_BLEND', blend_path)
print('SUN_GLB', os.path.join(OUT, 'sun.glb'))
print('SUN_RENDER', os.path.join(OUT, 'sun_render.png'))
print('SUN_ANIMATION_FRAMES', frames_dir)
