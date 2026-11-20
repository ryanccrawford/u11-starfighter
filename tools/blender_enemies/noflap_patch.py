"""Patch an existing <id>.blend: zero the FinL/FinR channels of the Idle action (rest pose),
then re-export <id>_lod.glb / <id>_high.glb with the pipeline's exact glTF settings. Keeps meshes, UVs, bake."""
import bpy, sys, os
argv = sys.argv[sys.argv.index('--') + 1:]
EID, OUT = argv[0], argv[1]
bones = argv[2].split(',') if len(argv) > 2 else ['FinL', 'FinR']
act = bpy.data.actions['Idle']
n = 0
for fc in act.fcurves:
    if not any(f'pose.bones["{b}"]' in fc.data_path for b in bones): continue
    rest = 1.0 if (fc.data_path.endswith('scale') or (fc.data_path.endswith('rotation_quaternion') and fc.array_index == 0)) else 0.0
    for kp in fc.keyframe_points:
        kp.co[1] = rest; kp.handle_left[1] = rest; kp.handle_right[1] = rest
    fc.update(); n += 1
print('PATCHED fcurves', n)
rig = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
lo = bpy.data.objects[EID + '_lod']; hi = bpy.data.objects[EID + '_high']
for o in (lo, hi): o.hide_viewport = False; o.hide_set(False)
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
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, EID + '.blend'), compress=True)
print('NOFLAP DONE')
