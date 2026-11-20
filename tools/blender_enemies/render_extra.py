"""Lineup next to red_fighter + Idle/Fire animation frames for an enemy .blend.
blender -b <enemy>.blend --python render_extra.py -- <enemy_id> <red_fighter.glb> <out_dir>"""
import bpy, sys, os, math
from mathutils import Vector
EID, RED, OUT = sys.argv[sys.argv.index('--') + 1:][:3]
sc = bpy.context.scene
hi = bpy.data.objects.get(EID + '_high_procedural') or bpy.data.objects[EID + '_high']
hi.hide_viewport = False; lo = bpy.data.objects[EID + '_lod']; rig = bpy.data.objects[EID + '_rig']
for o in sc.objects:
    if o.type == 'MESH': o.hide_render = o is not hi
cam = bpy.data.objects['cam']
sc.render.engine = 'CYCLES'; sc.cycles.samples = 64; sc.cycles.use_denoising = True
def look(loc, target=(0, 0, 0), ortho=None):
    cam.location = loc; cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    if ortho: cam.data.type = 'ORTHO'; cam.data.ortho_scale = ortho
    else: cam.data.type = 'PERSP'; cam.data.lens = 50
# ---- lineup (side-view game scale: player S=1.53, enemy S=0.99)
before = set(sc.objects)
bpy.ops.import_scene.gltf(filepath=RED)
red = [o for o in sc.objects if o not in before and o.type == 'MESH'][0]
mw = red.matrix_world.copy(); red.parent = None
from mathutils import Matrix
s = 1.53 / 0.9945
red.matrix_world = Matrix.Translation((-1.9, 0, 0)) @ Matrix.Rotation(math.pi / 2, 4, 'Z') @ Matrix.Scale(s, 4) @ mw  # nose -X -> -Y like the enemy
rig.location = (1.5, 0, 0)
sc.render.resolution_x, sc.render.resolution_y = 1800, 900
look((3.2, -6.4, 3.4), (-0.2, 0, 0)); cam.data.lens = 42
sc.render.filepath = os.path.join(OUT, f'{EID}_lineup_red_fighter.png'); bpy.ops.render.render(write_still=True)
look((0, 0, 9), (-0.2, 0, 0), ortho=6.5); cam.rotation_euler = (0, 0, 0)
sc.render.filepath = os.path.join(OUT, f'{EID}_lineup_top.png'); bpy.ops.render.render(write_still=True)
bpy.data.objects.remove(red); rig.location = (0, 0, 0)
# ---- animation frames: Idle (2 s) then Fire (0.35 s, x3), 30 fps, the game LOD with its baked texture
for o in sc.objects:
    if o.type == 'MESH': o.hide_render = o is not lo
sc.cycles.samples = 20; sc.render.resolution_x, sc.render.resolution_y = 640, 420
look((2.3, -2.9, 1.8))
os.makedirs(os.path.join(OUT, 'anim'), exist_ok=True)
i = 0
for name, dur, reps in (('Idle', 2.0, 1), ('Fire', 0.35, 3)):
    rig.animation_data.action = bpy.data.actions[name]
    for r in range(reps):
        for f in range(0, int(dur * 60) + (1 if name == 'Fire' else 0), 2):
            sc.frame_set(f); i += 1
            sc.render.filepath = os.path.join(OUT, 'anim', f'f{i:04d}.png'); bpy.ops.render.render(write_still=True)
