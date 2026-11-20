"""Create an animated review copy of the detailed turret without overwriting the source."""
from pathlib import Path
import math
import bpy

ROOT = Path(__file__).resolve().parents[1] / 'assets/models/ship_interior/hazards'
SOURCE = ROOT / 'turret_detailed_v1/turret.blend'
OUT = ROOT / 'turret_animated_v2'

def empty(name, pos, parent=None):
    obj=bpy.data.objects.new(name, None); bpy.context.collection.objects.link(obj)
    obj.location=pos; obj.parent=parent
    return obj

def attach(obj, parent):
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_world = world

def key(obj, channel, frame):
    obj.keyframe_insert(data_path=channel, frame=frame)

def build():
    if OUT.exists() and any(OUT.iterdir()): raise FileExistsError(f'Output exists: {OUT}')
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    meshes=[o for o in bpy.context.scene.objects if o.type == 'MESH']
    yaw=empty('RIG_yaw',(0,0,.52))
    recoil=empty('RIG_recoil',(0,0,0),yaw)
    spin=empty('RIG_barrel_spin',(0,0,.97),recoil)
    bpy.context.view_layer.update()  # evaluate rig pivots before preserving mesh world transforms
    base_prefix=('Base ', 'Bearing ', 'Mount bolt', 'Armored radial foot')
    barrel_prefix=('Rotating cannon barrel','Barrel muzzle sleeve','Dark muzzle bore','Barrel drive spindle','Barrel cage collar','Collar trim')
    for obj in meshes:
        if obj.name.startswith(base_prefix): continue
        attach(obj, spin if obj.name.startswith(barrel_prefix) else recoil)
    for frame, angle in ((1,-.18),(49,.18),(97,-.18)):
        yaw.rotation_euler.z=angle; key(yaw,'rotation_euler',frame)
    for frame, angle in ((1,0),(49,math.tau*2),(97,math.tau*4)):
        spin.rotation_euler.x=angle; key(spin,'rotation_euler',frame)
    for frame, x in ((1,0),(8,-.10),(12,0),(20,-.10),(24,0),(32,-.10),(36,0),(44,-.10),(48,0),(97,0)):
        recoil.location.x=x; key(recoil,'location',frame)
    beacon=bpy.data.objects.get('Emergency beacon lens')
    if beacon:
        for frame, strength in ((1,1),(7,.35),(13,1),(19,.35),(25,1),(31,.35),(37,1),(43,.35),(49,1)):
            beacon.scale=(strength,strength,1); key(beacon,'scale',frame)
    scene=bpy.context.scene; scene.frame_start=1; scene.frame_end=97; scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'turret_animated.blend'))
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes+[yaw,recoil,spin]: obj.select_set(True)
    bpy.context.view_layer.objects.active=yaw
    bpy.ops.export_scene.gltf(filepath=str(OUT/'turret_animated.glb'),export_format='GLB',use_selection=True)
    scene.frame_set(24); scene.render.filepath=str(OUT/'turret_pose.png'); bpy.ops.render.render(write_still=True)

if __name__ == '__main__': build()
