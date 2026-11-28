import bpy
import math
from mathutils import Vector
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'assets/models/enemy_destroyer.glb'
preview = root / 'assets/develop/destroyer/v1'
preview.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

def material(name, color, metallic=0.0, emission=0.0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    bs = m.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*color, 1)
    bs.inputs['Metallic'].default_value = metallic
    bs.inputs['Roughness'].default_value = 0.48
    if emission:
        bs.inputs['Emission Color'].default_value = (*color, 1)
        bs.inputs['Emission Strength'].default_value = emission
    return m

hull = material('Titanium Hull', (0.16, 0.19, 0.23), 1)
armor = material('Dark Armor', (0.055, 0.07, 0.09), 0.6)
detail = material('Bronze Weaponry', (0.24, 0.15, 0.09), 1)
engine = material('Engine Light', (0.16, 0.52, 1.0), 0, 3)

def box(name, location, scale, mat, bevel=0.08):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    o = bpy.context.object
    o.name = name
    o.dimensions = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = o.modifiers.new('Armor bevel', 'BEVEL')
        mod.width = bevel
        mod.segments = 1
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier=mod.name)
    o.data.materials.append(mat)
    return o

# A broad armored wedge, nose at -X and engines at +X.
verts = [(-7,0,-.35),(-5,-1.15,-.6),(-5,1.15,-.6),(5,-2.5,-.7),(5,2.5,-.7),(-7,0,.35),(-5,-1.15,.6),(-5,1.15,.6),(5,-2.5,.7),(5,2.5,.7)]
faces = [(0,2,1),(0,1,3,4,2),(5,6,7),(5,7,9,8,6),(0,5,6,1),(0,2,7,5),(1,6,8,3),(2,4,9,7),(3,8,9,4)]
mesh=bpy.data.meshes.new('Destroyer hull mesh'); mesh.from_pydata(verts, [], faces); mesh.update()
o=bpy.data.objects.new('Main armored hull', mesh); bpy.context.collection.objects.link(o); o.data.materials.append(hull)
box('Upper citadel', (1,0,.88), (5,2.1,.55), armor)
box('Command bridge', (2.1,0,1.3), (1.8,1.35,.45), hull)
box('Lower keel', (1,0,-.82), (6,1.15,.35), armor)
for side in (-1,1):
    box('Side armor', (1,side*2.0,.65), (6,.38,.26), armor)
    for x in (-3,0,3):
        box('Turret base', (x,side*1.15,.9), (.9,.75,.35), detail)
        for y in (-.18,.18):
            box('Cannon barrel', (x-.75,side*1.15+y,1.03), (1.3,.12,.12), detail, .02)
    for y in (.65,1.55):
        box('Engine housing', (5.12,side*y,0), (.75,.7,.7), armor)
        box('Engine glow', (5.52,side*y,0), (.08,.48,.48), engine, .01)

for mat in (hull, armor, detail, engine):
    parts = [o for o in bpy.context.scene.objects if o.type == 'MESH' and mat in o.data.materials[:]]
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    bpy.context.object.name = 'Destroyer ' + mat.name

for o in list(bpy.context.scene.objects):
    if o.type == 'MESH':
        bpy.context.view_layer.objects.active=o
        o.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        o.select_set(False)
objs=[o for o in bpy.context.scene.objects if o.type=='MESH']
print('TRIANGLES',sum(len(p.vertices)-2 for o in objs for p in o.data.polygons))
bpy.ops.wm.save_as_mainfile(filepath=str(preview/'destroyer.blend'))
bpy.ops.export_scene.gltf(filepath=str(out), export_format='GLB', use_selection=False, export_apply=True, export_yup=True, export_texcoords=True, export_normals=True, export_materials='EXPORT', export_animations=True)

bpy.ops.object.light_add(type='SUN', location=(0,0,10))
bpy.context.object.rotation_euler=(math.radians(28),math.radians(-25),math.radians(-35))
bpy.context.object.data.energy=2
bpy.context.scene.world.color=(0,0,0)
bpy.ops.object.camera_add()
camera=bpy.context.object
bpy.context.scene.camera=camera
scene=bpy.context.scene
scene.render.engine='CYCLES'
scene.cycles.samples=16
scene.render.resolution_x=1200; scene.render.resolution_y=800; scene.render.resolution_percentage=100
def render(name, position, target, lens):
    camera.location=position
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type='ORTHO'; camera.data.ortho_scale=lens
    scene.render.filepath=str(preview/name)
    bpy.ops.render.render(write_still=True)
render('hero.png',(-13,-16,12),(0,0,0),16)
render('side.png',(0,-20,3),(0,0,0),16)
render('detail.png',(-3,-9,7),(0,0,.7),8)
