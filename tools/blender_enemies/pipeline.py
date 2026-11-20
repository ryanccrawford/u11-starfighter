"""Build one enemy: high + LOD meshes, rig, Idle/Fire actions, texture bake, GLB export, previews.
Run: blender -b --python pipeline.py -- <enemy_id> <out_dir> [--skip-bake] [--fast]
"""
import bpy, bmesh, sys, os, math, json, importlib
import numpy as np
from mathutils import Vector, Matrix, Quaternion, Euler
sys.path.insert(0, os.path.dirname(__file__))
import enemy_lib as EL
import enemy_defs as ED
importlib.reload(EL); importlib.reload(ED)

argv = sys.argv[sys.argv.index('--') + 1:]
EID, OUT = argv[0], argv[1]
SKIP_BAKE = '--skip-bake' in argv
FAST = '--fast' in argv
TEX = 2048 if not FAST else 512
os.makedirs(OUT, exist_ok=True); os.makedirs(os.path.join(OUT, 'textures'), exist_ok=True); os.makedirs(os.path.join(OUT, 'renders'), exist_ok=True)
FPS = 60

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.fps = FPS
D = ED.ENEMIES[EID]

def log(*a): print('ENEMY', EID, *a, flush=True)

# ------------------------------------------------------------------ build both qualities
def build(quality):
    EL._MATS.clear() if quality == 'high' else None
    k = EL.Kit(quality)
    meta = D['build'](k)
    return k, meta

kh, meta = build('high')
# normalisation from the high model: centre the bbox, length (Y) -> D['length'] (default 2.0)
def bbox(objs):
    mn = Vector((1e9,) * 3); mx = Vector((-1e9,) * 3)
    for o in objs:
        for v in o.data.vertices:
            for i in range(3): mn[i] = min(mn[i], v.co[i]); mx[i] = max(mx[i], v.co[i])
    return mn, mx
mn, mx = bbox([o for o, _ in kh.parts])
centre = (mn + mx) / 2; scale = D.get('length', 2.0) / (mx.y - mn.y)
NM = Matrix.Diagonal((scale, scale, scale, 1)) @ Matrix.Translation(-centre)
def norm_pt(p): return NM @ Vector(p)
for o, _ in kh.parts: o.data.transform(NM)
kl, meta_l = build('low')
for o, _ in kl.parts: o.data.transform(NM)

def join(k, name):
    objs = [o for o, _ in k.parts]
    for o, b in k.parts:
        vg = o.vertex_groups.new(name=b); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active; ob.name = name; ob.data.name = name
    me = ob.data
    for p in me.polygons: p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(40))
    me.calc_loop_triangles()
    return ob

hi = join(kh, EID + '_high')
lo = join(kl, EID + '_lod')
tri_hi = len(hi.data.loop_triangles); tri_lo = len(lo.data.loop_triangles)
log('tris high', tri_hi, 'lod', tri_lo)
mn, mx = bbox([hi]); log('bbox', tuple(round(v, 3) for v in (mx - mn)))

# ------------------------------------------------------------------ armature
bones = meta['bones']  # name -> (pivot(design), parent or None)
arm = bpy.data.armatures.new(EID + '_rig'); rig = bpy.data.objects.new(EID + '_rig', arm)
sc.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='EDIT')
for name, (piv, parent) in bones.items():
    eb = arm.edit_bones.new(name)
    h = norm_pt(piv) if name != 'Hull' else Vector((0, 0, 0))
    eb.head = h; eb.tail = h + Vector((0, 0, 0.12)); eb.roll = 0
for name, (piv, parent) in bones.items():
    if parent: arm.edit_bones[name].parent = arm.edit_bones[parent]; arm.edit_bones[name].use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')
for ob in (hi, lo):
    ob.parent = rig
    mod = ob.modifiers.new('Armature', 'ARMATURE'); mod.object = rig
    missing = [g.name for g in ob.vertex_groups if g.name not in bones]
    assert not missing, missing

# ------------------------------------------------------------------ actions: world-space deltas per bone -> pose basis
def pivot_delta(piv, rot=(0, 0, 0), loc=(0, 0, 0), scl=(1, 1, 1)):
    """World delta: scale+rotate about the (design-space) pivot, then translate (design units scaled)."""
    p = norm_pt(piv)
    return Matrix.Translation(p + Vector(loc) * scale) @ Euler(rot).to_matrix().to_4x4() @ Matrix.Diagonal((*scl, 1)) @ Matrix.Translation(-p)

def bake_action(aname, dur, fn):
    rig.animation_data_create()
    act = bpy.data.actions.new(aname); act.use_fake_user = True
    rig.animation_data.action = act
    nfr = int(round(dur * FPS))
    order = list(bones.keys())
    for f in range(nfr + 1):
        t = f / FPS
        own = fn(t, pivot_delta)
        total = {}
        for b in order:  # parents are declared before children
            par = bones[b][1]
            P = total.get(par, Matrix.Identity(4)) if par else Matrix.Identity(4)
            total[b] = P @ own.get(b, Matrix.Identity(4))
        for b in order:
            if b == 'Hull': continue
            pb = rig.pose.bones[b]; pb.rotation_mode = 'QUATERNION'
            rest = arm.bones[b].matrix_local
            basis = rest.inverted() @ total[b] @ rest
            l, r, s = basis.decompose()
            pb.location = l; pb.rotation_quaternion = r; pb.scale = s
            pb.keyframe_insert('location', frame=f); pb.keyframe_insert('rotation_quaternion', frame=f); pb.keyframe_insert('scale', frame=f)
    for fc in act.fcurves:
        for kp in fc.keyframe_points: kp.interpolation = 'LINEAR'
    tr = rig.animation_data.nla_tracks.new(); tr.name = aname
    tr.strips.new(aname, 0, act); tr.mute = True
    rig.animation_data.action = None
    return act

for aname, (dur, fn) in meta['anims'].items():
    bake_action(aname, dur, fn)
log('actions', list(meta['anims'].keys()))

# ------------------------------------------------------------------ UVs
def uv_unwrap(ob, margin):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=margin, area_weight=0.6, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
uv_unwrap(lo, 0.004); uv_unwrap(hi, 0.002)

# ------------------------------------------------------------------ previews (procedural materials, Cycles)
def setup_render(w, h, samples):
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = samples; sc.cycles.use_denoising = True
    sc.render.resolution_x = w; sc.render.resolution_y = h; sc.render.film_transparent = False
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
    if not sc.world:
        wd = bpy.data.worlds.new('w'); sc.world = wd; wd.use_nodes = True
        bg = wd.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.012, 0.014, 0.03, 1); bg.inputs[1].default_value = 1.0
    if 'key' not in bpy.data.objects:
        for nm, loc, e, col, sz in [('key', (3.0, -2.5, 4.5), 900, (1, 0.96, 0.9), 2.0), ('fill', (-4, 1.5, 2.0), 260, (0.55, 0.65, 1.0), 3.0), ('rim', (0.5, 4.0, -1.5), 500, (0.7, 0.5, 1.0), 2.0)]:
            ld = bpy.data.lights.new(nm, 'AREA'); ld.energy = e; ld.color = col; ld.size = sz
            lo_ = bpy.data.objects.new(nm, ld); lo_.location = loc; sc.collection.objects.link(lo_)
            lo_.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    if 'cam' not in bpy.data.objects:
        cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam

def shoot(path, loc, ortho=None, up_y=False, lens=55):
    cam = bpy.data.objects['cam']; cam.location = loc
    cam.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y' if not up_y else 'Y').to_euler()
    if up_y:  # top-down: screen-up = +Y (nose -Y points down, like the game)
        cam.rotation_euler = (0, 0, 0)
    if ortho: cam.data.type = 'ORTHO'; cam.data.ortho_scale = ortho
    else: cam.data.type = 'PERSP'; cam.data.lens = lens
    sc.render.filepath = path; bpy.ops.render.render(write_still=True)

def only(objs):
    for o in sc.objects:
        if o.type == 'MESH': o.hide_render = o not in objs

RD = os.path.join(OUT, 'renders')
setup_render(1400, 1000, 24 if FAST else 96)
only([hi])
shoot(os.path.join(RD, EID + '_34.png'), (2.6, -3.3, 2.0))
setup_render(1400, 800, 16 if FAST else 64)
shoot(os.path.join(RD, EID + '_side.png'), (6.0, 0, 0.35), ortho=2.6)        # game side view: from +X, nose left
shoot(os.path.join(RD, EID + '_top.png'), (0, 0, 6.0), ortho=2.6, up_y=True)  # game overhead: nose down
log('previews done')

# ------------------------------------------------------------------ bake
def img(name, alpha=False, float_=True):
    im = bpy.data.images.new(name, TEX, TEX, alpha=alpha, float_buffer=float_)
    return im

def set_target(ob, im):
    """Put an active, unconnected image node in every material of ob."""
    for m in ob.data.materials:
        nt = m.node_tree
        n = nt.nodes.get('BAKE') or nt.nodes.new('ShaderNodeTexImage'); n.name = 'BAKE'
        n.image = im; nt.nodes.active = n

def albedo_mode(on):
    for m in bpy.data.materials:
        if not m.use_nodes or 'ALBEDO' not in m.node_tree.nodes: continue
        nt = m.node_tree; bsdf = nt.nodes['Principled BSDF']; rr = nt.nodes['ALBEDO']
        for l in list(bsdf.inputs['Emission Color'].links): nt.links.remove(l)
        if on:
            nt.links.new(rr.outputs[0], bsdf.inputs['Emission Color']); bsdf.inputs['Emission Strength'].default_value = 1.0
        else:
            if m.get('glow', 0) > 0: nt.links.new(rr.outputs[0], bsdf.inputs['Emission Color'])
            bsdf.inputs['Emission Strength'].default_value = m.get('glow', 0.0)

def bake(kind, target, im, source=None, samples=1):
    for o in sc.objects:
        if o.type == 'MESH': o.hide_render = o not in (target, source)
    bpy.ops.object.select_all(action='DESELECT')
    sc.cycles.samples = samples
    b = sc.render.bake; b.margin = 8; b.margin_type = 'EXTEND'
    if source is not None:
        source.select_set(True); b.use_selected_to_active = True; b.cage_extrusion = 0.02; b.max_ray_distance = 0.06
    else:
        b.use_selected_to_active = False
    target.select_set(True); bpy.context.view_layer.objects.active = target
    set_target(target, im)
    if kind == 'AO': bpy.ops.object.bake(type='AO')
    else: bpy.ops.object.bake(type='EMIT')

def to_np(im):
    a = np.empty(TEX * TEX * 4, np.float32); im.pixels.foreach_get(a); return a.reshape(TEX, TEX, 4)

def compose(prefix, alb, glow, ao):
    A = to_np(alb)[..., :3]; G = to_np(glow)[..., :3]; O = to_np(ao)[..., :1]
    lum = G.max(axis=2, keepdims=True)
    mask = np.clip(lum / 1.5, 0, 1)                     # strength >= 1.5 -> full glow
    rgb = A * (0.32 + 0.68 * O)
    gcol = np.clip(G / np.maximum(lum, 1e-4), 0, 1)
    rgb = rgb * (1 - mask) + np.clip(gcol * 0.8 + 0.2 * gcol ** 0.5, 0, 1) * mask
    srgb = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(np.clip(rgb, 0, None), 1 / 2.4) - 0.055)
    out = np.concatenate([np.clip(srgb, 0, 1), 1.0 - 0.85 * mask], axis=2).astype(np.float32)
    im = bpy.data.images.new(prefix, TEX, TEX, alpha=True, float_buffer=False)
    im.colorspace_settings.name = 'sRGB'
    im.pixels.foreach_set(out.ravel())
    path = os.path.join(OUT, 'textures', prefix + '.png')
    im.filepath_raw = path; im.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    im.save()
    im2 = bpy.data.images.load(path); im2.colorspace_settings.name = 'sRGB'
    im2.alpha_mode = 'STRAIGHT'
    return im2, float(mask.mean())

def final_material(ob, im, name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes['Principled BSDF']
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = im
    nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(t.outputs['Alpha'], bsdf.inputs['Alpha'])   # keeps the glow mask in the exported PNG
    bsdf.inputs['Metallic'].default_value = 0.0; bsdf.inputs['Roughness'].default_value = 0.6
    ob.data.materials.clear(); ob.data.materials.append(m)

tex_paths = {}
if not SKIP_BAKE:
    for target, src, tag in ((lo, hi, 'lod'), (hi, None, 'high')):
        alb = img(f'{EID}_{tag}_albedo'); glow = img(f'{EID}_{tag}_glow'); ao = img(f'{EID}_{tag}_ao')
        albedo_mode(True); bake('EMIT', target, alb, src, samples=1 if FAST else 12)
        albedo_mode(False); bake('EMIT', target, glow, src, samples=1)
        bake('AO', target, ao, src, samples=8 if FAST else 48)
        im, gm = compose(f'{EID}_{tag}_basecolor', alb, glow, ao)
        tex_paths[tag] = im
        log('baked', tag, 'glow coverage %.3f' % gm)
    final_material(lo, tex_paths['lod'], f'{EID}_lod_mat')
    hi_procedural = [m for m in hi.data.materials]
    # keep the procedural high model in the .blend as a hidden copy for future re-bakes
    src_copy = hi.copy(); src_copy.data = hi.data.copy(); src_copy.name = EID + '_high_procedural'; sc.collection.objects.link(src_copy)
    src_copy.hide_render = True; src_copy.hide_viewport = True
    final_material(hi, tex_paths['high'], f'{EID}_high_mat')

    # baked LOD 3/4 preview (what the game samples)
    setup_render(1000, 720, 16 if FAST else 48)
    only([lo]); shoot(os.path.join(RD, EID + '_lod_baked_34.png'), (2.6, -3.3, 2.0))

# ------------------------------------------------------------------ export
def export(ob, path):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True,
        export_normals=True, export_texcoords=True, export_materials='EXPORT', export_cameras=False, export_lights=False,
        export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True, export_frame_range=False,
        export_yup=True, export_image_format='AUTO', export_skins=True, export_all_influences=False,
        export_def_bones=False, export_optimize_animation_size=False, export_anim_single_armature=True,
        export_reset_pose_bones=True, export_bake_animation=False, export_extras=False)
export(lo, os.path.join(OUT, EID + '_lod.glb'))
export(hi, os.path.join(OUT, EID + '_high.glb'))

# sidecar for the engine
def gl(p):  # blender design point -> normalised -> glTF (x, z, -y)
    v = norm_pt(p); return [round(v.x, 4), round(v.z, 4), round(-v.y, 4)]
side = {
    'id': EID, 'name': D['name'], 'tris_high': tri_hi, 'tris_lod': tri_lo,
    'nozzles': [gl(p) for p in meta['nozzles']],
    'muzzle': gl(meta['muzzle']),
    'idle': 'Idle', 'fire': 'Fire', 'fire_sustain': meta.get('fire_sustain', [0.0, 0.0]),
    'weapon_bone_prefix': 'W_',
    'bones': list(bones.keys()),
}
json.dump(side, open(os.path.join(OUT, EID + '_lod.json'), 'w'), indent=2)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, EID + '.blend'), compress=True)
log('DONE', json.dumps(side))
