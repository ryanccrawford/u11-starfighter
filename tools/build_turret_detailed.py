"""Create a detailed, editable Level 3 turret review model in Blender."""
from pathlib import Path
import math
import bpy
from mathutils import Vector

OUT = Path(__file__).resolve().parents[1] / 'assets/models/ship_interior/hazards/turret_detailed_v1'

def material(name, color, metallic=.0, rough=.4, emission=0):
    m = bpy.data.materials.new(name); m.diffuse_color = (*color, 1); m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value = (*color,1)
    p.inputs['Metallic'].default_value = metallic; p.inputs['Roughness'].default_value = rough
    if emission:
        p.inputs['Emission Color'].default_value = (*color,1); p.inputs['Emission Strength'].default_value = emission
    return m

def cube(name, loc, size, material, bevel=.025, rotation=(0,0,0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rotation); o=bpy.context.object; o.name=name
    o.dimensions=size; bpy.ops.object.transform_apply(location=False, rotation=False, scale=True); o.data.materials.append(material)
    if bevel:
        mod=o.modifiers.new('Edge machining','BEVEL'); mod.width=bevel; mod.segments=2
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=mod.name)
    return o

def cylinder(name, loc, radius, depth, material, rotation=(0,0,0), vertices=32):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc, rotation=rotation)
    o=bpy.context.object; o.name=name; o.data.materials.append(material); return o

def build():
    if OUT.exists() and any(OUT.iterdir()): raise FileExistsError(f'Output exists: {OUT}')
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    steel=material('Brushed titanium',(.23,.27,.31),.9,.27); dark=material('Recessed graphite steel',(.035,.045,.055),.8,.32)
    edge=material('Machined bright edges',(.46,.50,.52),.95,.2); armor=material('Armored violet ceramic',(.15,.075,.23),.68,.3)
    orange=material('Emergency amber emission',(1,.12,.012),.1,.25,5); cyan=material('Electronics cyan emission',(.02,.65,1),.1,.22,4)
    rubber=material('Cable insulation',(.015,.017,.02),.1,.65)
    # Base, fasteners, structural feet and concentric turntable.
    cylinder('Base armored flange',(0,0,.12),1.38,.24,dark,vertices=48)
    cylinder('Base bevel ring',(0,0,.26),1.20,.16,steel,vertices=48)
    cylinder('Bearing inner race',(0,0,.37),.98,.12,edge,vertices=48)
    cylinder('Rotating turret pedestal',(0,0,.52),.88,.24,dark,vertices=40)
    for i in range(12):
        a=i*math.tau/12; x,y=1.06*math.cos(a),1.06*math.sin(a)
        cylinder('Mount bolt', (x,y,.355),.055,.06,edge,vertices=8)
        cube('Armored radial foot',(1.22*math.cos(a),1.22*math.sin(a),.14),(.40,.18,.20),steel,.035,(0,0,a))
    for i in range(32):
        a=i*math.tau/32
        cube('Turntable gear tooth',(.92*math.cos(a),.92*math.sin(a),.51),(.15,.10,.14),edge,.008,(0,0,a))
    # Housing has layered cheeks, vents, panel seams and an exposed pivot.
    cube('Pedestal central block',(0,0,.91),(1.35,1.02,.72),steel,.09)
    for side in (-1,1):
        cube('Armored side cheek',(-.10,side*.56,1.02),(1.22,.16,.66),armor,.055)
        cylinder('Elevation pivot',(0,side*.67,1.16),.29,.16,dark,(math.pi/2,0,0))
        cylinder('Pivot cap',(0,side*.77,1.16),.17,.06,edge,(math.pi/2,0,0))
        for j in range(5): cube('Heat sink rib',(-.52+j*.18,side*.665,.90),(.07,.07,.29),dark,.009)
        for j in range(4): cylinder('Housing fastener',(-.48+j*.31,side*.66,1.28),.034,.035,edge,(math.pi/2,0,0),12)
    cube('Gun receiver',(.32,0,1.48),(1.72,.66,.54),dark,.06)
    cube('Upper receiver armor',(.16,0,1.78),(1.30,.72,.14),steel,.04)
    cube('Rear equipment block',(-.75,0,1.48),(.60,.78,.64),armor,.055)
    for i in range(6): cube('Rear vent fin',(-1.075,-.29+i*.115,1.50),(.07,.055,.43),edge,.006)
    # Six real barrels around the central axle, with end collars and dark open muzzles.
    cylinder('Barrel drive spindle',(1.05,0,1.49),.19,2.7,steel,(0,math.pi/2,0))
    for i in range(6):
        a=i*math.tau/6; y=.26*math.cos(a); z=1.49+.26*math.sin(a)
        cylinder('Rotating cannon barrel',(1.42,y,z),.075,2.45,steel,(0,math.pi/2,0),24)
        cylinder('Barrel muzzle sleeve',(2.67,y,z),.092,.12,edge,(0,math.pi/2,0),24)
        cylinder('Dark muzzle bore',(2.74,y,z),.047,.012,dark,(0,math.pi/2,0),20)
    for x in (.55,1.45,2.40):
        cylinder('Barrel cage collar',(x,0,1.49),.40,.11,dark,(0,math.pi/2,0),32)
        cylinder('Collar trim',(x+.06,0,1.49),.36,.025,edge,(0,math.pi/2,0),32)
    # Feed belt, pistons, cable runs, and tiny signal lamps are separate objects.
    for i in range(11):
        a=-.95+i*.18; x=-.1+.54*math.cos(a); z=1.05+.54*math.sin(a)
        cube('Ammunition feed link',(x,-.55,z),(.16,.18,.10),edge,.008,(0,a,0))
    for side in (-1,1):
        cylinder('Hydraulic cylinder',(-.33,side*.48,.77),.085,.75,dark,(0,math.pi/2,0))
        cylinder('Hydraulic polished rod',(.18,side*.48,.77),.045,.55,edge,(0,math.pi/2,0))
        for i in range(3):
            cylinder('Insulated cable',(-.65,side*(.48+i*.06),1.12),.023,.52,rubber,(0,math.pi/2,0),12)
        for i in range(5): cylinder('Electronics indicator',(-.92+i*.10,side*.407,1.70),.025,.02,cyan,(math.pi/2,0,0),12)
    cylinder('Warning beacon base',(-.8,.25,1.91),.105,.10,dark)
    cylinder('Emergency beacon lens',(-.8,.25,2.02),.075,.14,orange)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.gltf(filepath=str(OUT/'turret.glb'),export_format='GLB',use_selection=True)
    bpy.ops.object.camera_add(location=(6,-8,5)); cam=bpy.context.object; bpy.context.scene.camera=cam
    cam.rotation_euler=(Vector((.25,0,1.0))-cam.location).to_track_quat('-Z','Y').to_euler(); cam.data.type='ORTHO'; cam.data.ortho_scale=6.8
    for loc,energy,size,col in [((1,-5,7),1300,5,(.78,.88,1)),((-4,-1,4),1000,4,(.45,.55,1)),((3,4,5),1600,3,(1,.45,.22))]:
        bpy.ops.object.light_add(type='AREA',location=loc); o=bpy.context.object; o.data.energy=energy; o.data.size=size; o.data.color=col
        o.rotation_euler=(Vector((0,0,1))-o.location).to_track_quat('-Z','Y').to_euler()
    sc=bpy.context.scene; sc.render.engine='BLENDER_EEVEE'; sc.render.resolution_x=1200; sc.render.resolution_y=900; sc.render.resolution_percentage=100
    sc.world.color=(.015,.015,.02); sc.render.image_settings.file_format='PNG'; sc.render.filepath=str(OUT/'turret_review.png')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'turret.blend')); bpy.ops.render.render(write_still=True)

if __name__=='__main__': build()
