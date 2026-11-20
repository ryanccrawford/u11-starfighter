"""Build modular, animated-ready Level 3 turret and bulkhead assets in Blender."""
from pathlib import Path
import bpy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "models" / "ship_interior" / "hazards"

def mat(name, color, metallic=.7, rough=.28, glow=0):
    m=bpy.data.materials.new(name); m.use_nodes=True
    b=m.node_tree.nodes.get("Principled BSDF"); b.inputs["Base Color"].default_value=(*color,1); b.inputs["Metallic"].default_value=metallic; b.inputs["Roughness"].default_value=rough
    if glow: b.inputs["Emission Color"].default_value=(*color,1); b.inputs["Emission Strength"].default_value=glow
    return m
def cube(n,p,s,m,bevel=.04):
    bpy.ops.mesh.primitive_cube_add(size=1,location=p); o=bpy.context.object; o.name=n; o.dimensions=s; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); o.data.materials.append(m)
    if bevel: q=o.modifiers.new("Machined edges","BEVEL"); q.width=bevel; q.segments=2; bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=q.name)
    return o
def cyl(n,p,r,d,m,rot=(0,0,0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=r,depth=d,location=p,rotation=rot); o=bpy.context.object; o.name=n; o.data.materials.append(m); return o
def build():
    OUT.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    steel=mat("Reflective gunmetal",(.08,.10,.14),.9,.18); dark=mat("Rugged black steel",(.025,.03,.04),.85,.34); violet=mat("Alien armor",(.27,.08,.42),.72,.22); red=mat("Hazard red",(.55,.025,.025),.65,.25); white=mat("Worn ceramic",(.55,.55,.52),.45,.38); lamp=mat("Warning emission",(1,.09,.035),.1,.2,4)
    # Turret: separate named mechanisms are animation-ready in the game renderer.
    cube("Turret armored base",(0,0,0),(2.8,.8,1.4),dark); cyl("Turret rotating ring",(0,.45,0),.9,.3,steel,(1.5708,0,0)); cube("Turret cradle",(0,.95,0),(1.45,.75,1.0),steel)
    for i,z in enumerate((-.28,0,.28)):
        cyl("Turret barrel",(1.7,1.18,z),.11,3.1,steel,(0,1.5708,0)); cyl("Turret barrel collar",(.55,1.18,z),.22,.22,dark,(0,1.5708,0)); cyl("Turret muzzle",(3.27,1.18,z),.16,.18,lamp,(0,1.5708,0))
    cube("Turret ammo housing",(-.5,1.22,.0),(1.05,.65,.8),violet); cube("Turret hydraulic",(0,1.72,.0),(1.7,.12,.16),steel)
    # Move the complete turret away from the bulkhead for a legible asset review.
    for obj in bpy.context.scene.objects: obj.location.x -= 4.8
    # Separate damaged pedestal for replacement after the intact turret is destroyed.
    cube("Wreck scorched pedestal",(-8.2,0,0),(2.1,.5,1.25),dark)
    for i in range(9):
        x=-9.12+i*.23
        bpy.ops.mesh.primitive_cone_add(vertices=3, radius1=.16, radius2=.015, depth=.45+(i%3)*.13, location=(x,.43,0), rotation=(1.5708,0,0))
        bpy.context.object.name="Wreck jagged torn metal"; bpy.context.object.data.materials.append(steel)
    for i in range(5):
        wire=cyl("Wreck exposed cable",(-8.65+i*.20,.36,.43),.025,.8,dark,(.45,0,.3+i*.12))
        cyl("Wreck glowing cable tip",(wire.location.x,wire.location.y+.30,.53),.035,.05,lamp)
    # Door: profile-facing modular frame and two independently movable segmented leaves.
    cube("Bulkhead frame top",(0,2.9,0),(3.8,.5,.55),dark); cube("Bulkhead frame bottom",(0,-2.9,0),(3.8,.5,.55),dark)
    for x in (-1.65,1.65): cube("Bulkhead side frame",(x,0,0),(.5,5.6,.55),dark)
    for side in (-1,1):
        leaf=cube("Bulkhead moving leaf",(side*.70,0,.12),(1.4,5.05,.42),steel); cube("Bulkhead red armor",(side*.70,0,.36),(1.3,4.95,.08),red,.02)
        for y in (-1.2,0,1.2): cube("Bulkhead ceramic stripe",(side*.68,y,.42),(1.05,.34,.05),white,.01)
        for y in (-2.3,2.3): cyl("Bulkhead warning lamp",(side*1.42,y,.38),.12,.08,lamp,(1.5708,0,0))
        for y in (-1.9,-1.2,-.5,.2,.9,1.6):
            cube("Titanium segmented plate",(side*.7,y,.415),(1.18,.62,.055),white,.015)
            for x in (-.48,.48): cyl("Plate fastener",(side*.7+x,y,.455),.035,.03,dark)
        cyl("Door hydraulic cylinder",(side*1.43,0,.55),.13,3.3,steel,(1.5708,0,0))
        cyl("Door hydraulic rod",(side*1.43,1.9,.55),.065,1.0,white,(1.5708,0,0))
        for j in range(8):
            cyl("Electronics indicator",(side*1.44,-1.4+j*.17,.69),.028,.04,lamp)
        for j in range(3): cyl("Cable conduit",(side*(1.25+j*.08),0,.58),.025,4.6,dark,(1.5708,0,0))
        # Independent objects and keyframes keep motors, status LEDs and emergency beacons
        # editable. Damage/debris objects are animation assets, not a gameplay trigger.
        piston=cyl("Animated piston",(side*1.4,-1.3,.76),.055,1.2,white,(1.5708,0,0))
        for frame, offset in ((1,0),(25,.55),(49,0)):
            piston.location.y=-1.3+offset; piston.keyframe_insert(data_path='location',frame=frame)
        beacon=cyl("Rotating emergency beacon",(side*1.43,2.42,.74),.13,.14,lamp)
        arm=cube("Beacon rotating reflector",(side*1.43,2.42,.84),(.34,.04,.04),white,.01)
        for frame,angle in ((1,0),(49,6.2831853)):
            arm.rotation_euler.z=angle; arm.keyframe_insert(data_path='rotation_euler',frame=frame)
        for frame,scale in ((1,1),(7,.2),(13,1),(19,.2),(25,1),(31,.2),(37,1),(43,.2),(49,1)):
            beacon.scale=(scale,scale,scale); beacon.keyframe_insert(data_path='scale',frame=frame)
        debris=cube("Damage detachable armor fragment",(side*.7,.1,.50),(.28,.34,.06),steel,.01)
        debris.keyframe_insert(data_path='location',frame=1)
        debris.location.z += .5; debris.location.x += side*.45; debris.keyframe_insert(data_path='location',frame=24)
        debris.keyframe_insert(data_path='rotation_euler',frame=1); debris.rotation_euler.y=side*1.4; debris.keyframe_insert(data_path='rotation_euler',frame=24)
    bpy.ops.object.select_all(action="SELECT"); bpy.ops.export_scene.gltf(filepath=str(OUT/"level3_hazards_detail.glb"),export_format="GLB")
    bpy.ops.object.camera_add(location=(10,-14,12)); cam=bpy.context.object; bpy.context.scene.camera=cam
    def look(o, point): o.rotation_euler=(Vector(point)-o.location).to_track_quat('-Z','Y').to_euler()
    from mathutils import Vector
    look(cam,(-2,0,0)); bpy.ops.object.light_add(type='AREA', location=(2,-4,8)); bpy.context.object.data.energy=1200; bpy.context.object.data.shape='DISK'; bpy.context.object.data.size=6
    bpy.ops.object.light_add(type='AREA', location=(-5,-1,3)); bpy.context.object.data.energy=800; bpy.context.object.data.color=(.3,.45,1); bpy.context.object.data.size=5
    sc=bpy.context.scene; sc.render.engine='BLENDER_EEVEE'; sc.render.resolution_x=1024; sc.render.resolution_y=768; sc.render.resolution_percentage=100; sc.render.image_settings.file_format='PNG'; sc.render.filepath=str(OUT/'level3_hazards_detail_preview.png'); sc.world.color=(.005,.008,.015); bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"level3_hazards_detail.blend"))
if __name__=="__main__": build()
