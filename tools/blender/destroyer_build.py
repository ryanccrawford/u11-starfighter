import bpy
import math
import os
from mathutils import Vector
from pathlib import Path

root = Path(__file__).resolve().parents[2]
tablet = os.getenv('DESTROYER_TABLET') == '1'
out = root / ('assets/models/enemy_destroyer_tablet.glb' if tablet else 'assets/models/enemy_destroyer.glb')
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
trim = material('Gunmetal Panel Trim', (0.10, 0.12, 0.15), 0.8)
accent = material('Oxidized Violet Armor', (0.065, 0.035, 0.055), 0.5)
lights = material('Navigation Lights', (1.0, 0.28, 0.05), 0, 2)

def box(name, location, scale, mat, bevel=0.08):
    if tablet and name in {'Armor rib', 'Vent grille', 'Small hull fastener',
                           'Bridge machinery cap', 'Hangar structural frame',
                           'Flank structural detail', 'Sensor aerial tip',
                           'Upper hull inset plate', 'Citadel plating',
                           'Side vent recess', 'Side armor panel',
                           'Lower hull structural frame', 'Flank recessed hatch',
                           'Ventilation grille vane'}:
        return None
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

def cylinder(name, location, radius, depth, mat, vertices=12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location, rotation=(0, math.pi/2, 0))
    o = bpy.context.object
    o.name = name
    o.data.materials.append(mat)
    return o

def tapered_structure(name, x, length, width, base, top, taper, mat):
    corners=[(-1,-1),(-1,1),(1,1),(1,-1)]
    verts=[(x+sx*length*.5,sy*width*.5,base) for sx,sy in corners]
    verts += [(x+sx*length*.5*taper,sy*width*.5*taper,top) for sx,sy in corners]
    faces=[(0,3,2,1),(4,5,6,7)]
    faces += [(i,(i+1)%4,(i+1)%4+4,i+4) for i in range(4)]
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bevel=obj.modifiers.new('Structural edges','BEVEL'); bevel.width=.035; bevel.segments=2
    bpy.context.view_layer.objects.active=obj; obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=bevel.name); obj.select_set(False)
    return obj

def ring(name, location, major, minor, mat):
    bpy.ops.mesh.primitive_torus_add(major_segments=12 if tablet else 24, minor_segments=4 if tablet else 6, location=location, rotation=(0, math.pi/2, 0), major_radius=major, minor_radius=minor)
    o=bpy.context.object
    o.name=name
    o.data.materials.append(mat)
    return o

def armor_plate(name, outline, z, thickness, mat):
    n=len(outline)
    vertices=[(x,y,z) for x,y in outline]+[(x,y,z+thickness) for x,y in outline]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(vertices,[],faces); mesh.update()
    obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bevel=obj.modifiers.new('Machined edges','BEVEL'); bevel.width=.035; bevel.segments=2
    bpy.context.view_layer.objects.active=obj
    obj.select_set(True); bpy.ops.object.modifier_apply(modifier=bevel.name); obj.select_set(False)
    return obj

def conduit(name, points, radius, mat):
    curve=bpy.data.curves.new(name,'CURVE'); curve.dimensions='3D'
    curve.resolution_u=3 if tablet else 8; curve.bevel_resolution=0 if tablet else 2; curve.bevel_depth=radius
    spline=curve.splines.new('BEZIER'); spline.bezier_points.add(len(points)-1)
    for i,(p,xyz) in enumerate(zip(spline.bezier_points,points)):
        p.co=xyz; p.handle_left_type='AUTO'; p.handle_right_type='AUTO'
        p.radius=.35 + .65*math.sin(math.pi*(i+1)/(len(points)+1))
    obj=bpy.data.objects.new(name,curve); bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True)
    bpy.context.view_layer.objects.active=obj; bpy.ops.object.convert(target='MESH')
    return bpy.context.object

# A continuous multi-section armored hull, with chamfered upper and lower
# flanks, gives the ship a capital-ship silhouette rather than a flat wedge.
sections=[(-6.4,.75,.58),(-5.2,1.15,.72),(-3.5,1.65,.86),(1.5,2.15,.92),(4.7,2.35,.82),(5.3,1.95,.7)]
profile=[(-.65,1),(.65,1),(1,.55),(1,-.55),(.65,-1),(-.65,-1),(-1,-.55),(-1,.55)]
verts=[(x,y*w,z*h*(1.65 if z<0 else 1)) for x,w,h in sections for y,z in profile]
faces=[tuple(range(8)),tuple(reversed(range((len(sections)-1)*8,len(sections)*8)))]
for i in range(len(sections)-1):
    for j in range(8):
        k=(j+1)%8
        faces.append((i*8+j,(i+1)*8+j,(i+1)*8+k,i*8+k))
mesh=bpy.data.meshes.new('Destroyer hull mesh'); mesh.from_pydata(verts, [], faces); mesh.update()
o=bpy.data.objects.new('Main armored hull', mesh); bpy.context.collection.objects.link(o); o.data.materials.append(hull)
tapered_structure('Upper citadel',1,5,2.1,.61,1.16,.83,armor)
tapered_structure('Command bridge',2.1,1.8,1.35,1.12,1.57,.78,hull)
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

# Layered armored superstructure, running lights, and surface equipment.
tapered_structure('Command tower lower',2.25,1.75,1.15,1.48,1.82,.80,trim)
tapered_structure('Command tower upper',2.45,1.05,.8,1.78,2.18,.74,hull)
box('Bridge visor', (1.89,0,1.98), (.06,.65,.09), lights, .01)
box('Sensor mast', (2.55,.15,2.5), (.12,.12,.8), detail, .01)
box('Sensor array', (2.5,-.12,2.37), (.5,.6,.12), armor, .02)
for side in (-1, 1):
    # Raised plate strips expose dark seams instead of coplanar overlays.
    for i in range(8):
        x = -4.2 + i*1.05
        width = 0.82 + i*.12
        box('Dorsal segmented armor', (x,side*(.9+width*.35),.77), (.88,width,.14), hull, .035)
        box('Armor rib', (x+.35,side*1.96,.79), (.1,.65,.2), trim, .015)
    for i in range(6):
        x = -3.7+i*1.45
        box('Side armor panel', (x,side*(1.57+i*.105),-.03), (1.18,.28,.82), trim, .04)
        box('Side vent recess', (x,side*(1.73+i*.105),.05), (.65,.06,.4), armor, .01)
        for j in range(3):
            box('Vent grille', (x-.21+j*.2,side*(1.77+i*.105),.05), (.045,.035,.34), detail, .005)
    # Large engine pods and concentric rear details.
    for y in (1.15,2.0):
        cylinder('Armored engine pod', (4.7,side*y,-.05), .48, 1.75, armor)
        cylinder('Engine collar', (5.55,side*y,-.05), .52, .22, trim)
        cylinder('Engine nozzle', (5.69,side*y,-.05), .34, .09, engine)
    for i in range(5):
        box('Citadel plating', (-.9+i*.8,side*1.08,1.07), (.62,.15,.28), trim, .025)
        box('Citadel navigation light', (-.9+i*.8,side*1.17,1.10), (.22,.025,.045), lights, .005)
    for x in (-4.4,-2.4,0,2.4,4.3):
        box('Underside fins', (x,side*.85,-.99), (.42,.2,.5), trim, .025)
    # A raised, angular violet armored spine suggests the reference silhouette.
    for i in range(7):
        x=-4.4+i*.63
        box('Violet armor ridge', (x,side*(1.1+i*.13),.93), (.52,.20,.18), accent, .035)

# Capital-ship scale: armored main battery, bow weapon and layered machinery.
cylinder('Bow weapon housing', (-6.15,0,0), .68, 1.35, armor, 24)
ring('Bow cannon outer rim', (-6.84,0,0), .61, .11, trim)
ring('Bow cannon inner rim', (-6.96,0,0), .39, .07, detail)
cylinder('Bow cannon aperture', (-6.98,0,0), .28, .04, lights, 24)
for side in (-1,1):
    for i in range(4):
        x=-3.8+i*2.1
        box('Heavy broadside emplacement', (x,side*2.2,.45), (.9,.48,.32), hull, .04)
        for z in (.38,.53):
            cylinder('Broadside barrel', (x-.7,side*2.2,z), .07, 1.0, detail, 8)
    for i in range(9):
        x=-4+i*.9
        box('Upper hull inset plate', (x,side*.65,1.22), (.68,.38,.10), trim, .025)
        box('Small hull fastener', (x+.18,side*.67,1.29), (.08,.08,.04), detail, .005)
    for i in range(3):
        x=2.8+i*.55
        box('Bridge machinery', (x,side*.77,1.4), (.34,.35,.46), armor, .03)
        box('Bridge machinery cap', (x,side*.77,1.65), (.28,.3,.05), trim, .01)
    for i in range(3):
        box('Hangar structural frame', (1.1+i*.48,side*2.28,-.12), (.12,.15,.6), hull, .015)
    box('Hangar bay', (1.58,side*2.22,-.12), (1.5,.12,.62), armor, .02)
    box('Hangar deck light', (1.58,side*2.31,-.38), (1.2,.04,.04), lights, .005)

# Irregular interlocking deck plates and sweeping armored cabling soften the
# blocky silhouette and echo the battle-damaged, biomechanical reference.
for side in (-1,1):
    for i in range(8):
        x=-4.9+i*.91
        outer=min(1.9,1.02+i*.12)
        inner=.34 + .08*(i%2)
        outline=[(x,side*inner),(x+.68,side*(inner+.04)),(x+.83,side*(outer-.18)),(x+.57,side*outer),(x-.12,side*(outer-.15))]
        if side<0: outline.reverse()
        armor_plate('Interlocking deck plating',outline,.99,.09,hull if i%3 else trim)
        for j in range(2):
            cylinder('Deck access fastener', (x+.19+j*.34,side*(inner+.17),1.10), .035, .05, detail, 8).rotation_euler=(0,0,0)
    for k in range(4):
        points=[]
        for i in range(9):
            x=-5.25+i*1.12
            y=side*(1.0 + .13*i + .28*math.sin(i*.95+k*.8))
            z=.91 + .09*math.sin(i*1.05+k*.75) + k*.025
            points.append((x,y,z))
        conduit('Armored organic conduit',points,.045+k*.006,accent)
        if k in (1,3):
            conduit('Conduit metallic core',[(x,y,z+.06) for x,y,z in points],.022,detail)
    for x in (3.7,4.5,5.1):
        for y in (1.15,2.0):
            ring('Engine reinforcing band',(x,side*y,-.05),.50,.045,trim)

# The lower hull carries recessed launch bays and structural ribs. These
# details follow the hull width instead of floating in a rectangular grid.
def beam_width(x):
    for a,b in zip(sections,sections[1:]):
        if a[0] <= x <= b[0]:
            t=(x-a[0])/(b[0]-a[0])
            return a[1]*(1-t)+b[1]*t
    return sections[-1][1]

for side in (-1,1):
    for i in range(9):
        x=-4.5+i*1.06
        y=side*(beam_width(x)+.035)
        panel=box('Lower hull structural frame',(x,y,-.55),(.12,.14,.80),trim,.025)
        plate=box('Flank service panel',(x+.39,side*(beam_width(x+.39)+.025),-.55),(.64,.09,.52),hull,.025)
        box('Flank recessed hatch',(x+.39,side*(beam_width(x+.39)+.085),-.55),(.38,.04,.29),armor,.015)
    for x in (-2.8,.2,3.0):
        y=side*(beam_width(x)+.14)
        box('Broadside battery foundation',(x,y,-.43),(.86,.30,.46),trim,.06)
        for z in (-.32,-.5):
            cylinder('Broadside gun barrel',(x-.65,y,z),.075,1.05,detail,12)
    for i in range(4):
        x=-3.65+i*.4
        y=side*(beam_width(x)+.11)
        box('Ventilation grille vane',(x,y,-.97),(.13,.1,.34),trim,.01)
    for x in (1.7,2.0,2.3,2.6):
        box('Bridge vision slit',(x,side*.565,1.68),(.16,.03,.06),lights,.005)
    for i in range(3):
        x=2.1+i*.28
        mast=box('Sensor aerial',(x,side*.35,2.32+i*.08),(.04,.04,.40+i*.16),detail,.006)
        box('Sensor aerial tip',(x,side*.35,2.55+i*.16),(.07,.07,.045),trim,.006)

for mat in (hull, armor, detail, engine, trim, accent, lights):
    parts = [o for o in bpy.context.scene.objects if o.type == 'MESH' and mat in o.data.materials[:]]
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    bpy.context.object.name = 'Destroyer ' + mat.name
    if tablet:
        # Start from the reduced-detail design, then make a modest reduction
        # rather than collapsing the fully detailed PC model to one fifth.
        mod=bpy.context.object.modifiers.new('Tablet mesh reduction','DECIMATE')
        mod.ratio=.50
        bpy.ops.object.modifier_apply(modifier=mod.name)

for o in list(bpy.context.scene.objects):
    if o.type == 'MESH':
        bpy.context.view_layer.objects.active=o
        o.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        o.select_set(False)
objs=[o for o in bpy.context.scene.objects if o.type=='MESH']

# Bake subtle mottled metal into portable image textures; GLB cannot carry
# Blender's procedural nodes. Each material group receives its own UV atlas.
scene=bpy.context.scene
scene.render.engine='CYCLES'
scene.cycles.samples=8
for mat in (hull, armor, detail, trim, accent):
    obj=next(o for o in objs if mat in o.data.materials[:])
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')
    nodes=mat.node_tree.nodes
    links=mat.node_tree.links
    bs=nodes.get('Principled BSDF')
    output=nodes.get('Material Output')
    base=tuple(bs.inputs['Base Color'].default_value)
    noise=nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value=55
    noise.inputs['Detail'].default_value=4
    texcoord=nodes.new('ShaderNodeTexCoord')
    links.new(texcoord.outputs['UV'],noise.inputs['Vector'])
    mix=nodes.new('ShaderNodeMixRGB'); mix.blend_type='MIX'
    mix.inputs[1].default_value=(*[c*.48 for c in base[:3]],1)
    mix.inputs[2].default_value=(*[min(c*1.65,1) for c in base[:3]],1)
    links.new(noise.outputs['Fac'],mix.inputs[0])
    emission=nodes.new('ShaderNodeEmission')
    links.new(mix.outputs[0],emission.inputs['Color'])
    links.new(emission.outputs[0],output.inputs['Surface'])
    img=bpy.data.images.new(mat.name+' baked',width=1024,height=1024)
    tex=nodes.new('ShaderNodeTexImage'); tex.image=img
    nodes.active=tex
    bpy.ops.object.bake(type='EMIT', margin=8)
    img.pack()
    links.new(bs.outputs[0],output.inputs['Surface'])
    links.new(tex.outputs['Color'],bs.inputs['Base Color'])
    for node in (noise,texcoord,mix,emission): nodes.remove(node)

# Use the authored armor atlas on the outer hull and trim. Keep a packed copy
# so the GLB is self-contained; scale it to the tablet texture limit as well.
armor_image=bpy.data.images.load(str(preview/'hull_texture.png'),check_existing=True)
armor_image.scale(1024,1024)
armor_image.pack()
for mat in (hull,trim):
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.name='Authored armor texture'; tex.image=armor_image
    bs=mat.node_tree.nodes.get('Principled BSDF')
    mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])

print('TRIANGLES',sum(len(p.vertices)-2 for o in objs for p in o.data.polygons))
bpy.ops.wm.save_as_mainfile(filepath=str(preview/('destroyer_tablet.blend' if tablet else 'destroyer.blend')))
bpy.ops.export_scene.gltf(filepath=str(out), export_format='GLB', use_selection=False, export_apply=True, export_yup=True, export_texcoords=True, export_normals=True, export_materials='EXPORT', export_animations=True)

bpy.ops.object.light_add(type='SUN', location=(0,0,10))
bpy.context.object.rotation_euler=Vector((13,16,-12)).to_track_quat('-Z','Y').to_euler()
bpy.context.object.data.energy=4
bpy.context.scene.world.color=(0,0,0)
bpy.ops.object.camera_add()
camera=bpy.context.object
bpy.context.scene.camera=camera
scene=bpy.context.scene
scene.world.use_nodes=True
scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(0.002,0.003,0.008,1)
scene.render.engine='CYCLES'
scene.cycles.samples=16
scene.view_settings.look='AgX - Medium High Contrast'
scene.view_settings.exposure=1.8
scene.render.resolution_x=1200; scene.render.resolution_y=800; scene.render.resolution_percentage=100
def render(name, position, target, lens):
    camera.location=position
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type='ORTHO'; camera.data.ortho_scale=lens
    scene.render.filepath=str(preview/name)
    bpy.ops.render.render(write_still=True)
prefix='tablet-' if tablet else ''
render(prefix+'hero.png',(-13,-16,12),(0,0,0),16)
render(prefix+'side.png',(0,-20,3),(0,0,0),16)
render(prefix+'detail.png',(-3,-9,7),(0,0,.7),8)
