"""Export material-batched turret parts with pivots for runtime animation."""
from pathlib import Path
import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1] / 'assets/models/ship_interior/hazards'
SOURCE = ROOT / 'turret_detailed_v1/turret.blend'
OUT = ROOT / 'turret_runtime_v1'

BASE = ('Base ', 'Bearing ', 'Mount bolt', 'Armored radial foot')
BARRELS = ('Rotating cannon barrel', 'Barrel muzzle sleeve', 'Dark muzzle bore',
           'Barrel drive spindle', 'Barrel cage collar', 'Collar trim')

def group(name):
    if name.startswith(BASE): return 'base'
    if name.startswith(BARRELS): return 'barrels'
    return 'head'

def export_part(part, originals, pivot_z):
    target = OUT / (part + '.glb')
    if target.exists(): raise FileExistsError(f'Refusing to overwrite {target}')
    copies=[]
    for source in originals:
        copy=source.copy(); copy.data=source.data.copy(); bpy.context.collection.objects.link(copy)
        copy.matrix_world=source.matrix_world.copy(); copies.append(copy)
    batches={}
    for obj in copies:
        if not obj.data.materials: raise RuntimeError('Missing material on ' + obj.name)
        batches.setdefault(obj.data.materials[0], []).append(obj)
    joined=[]
    for batch in batches.values():
        bpy.ops.object.select_all(action='DESELECT')
        for obj in batch: obj.select_set(True)
        bpy.context.view_layer.objects.active=batch[0]
        bpy.ops.object.join(); obj=bpy.context.object
        # Bake the object's full transform, then shift the geometry to the required pivot.
        obj.data.transform(obj.matrix_world); obj.matrix_world=Matrix.Identity(4)
        obj.data.transform(Matrix.Translation((0,0,-pivot_z)))
        joined.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in joined: obj.select_set(True)
    bpy.context.view_layer.objects.active=joined[0]
    bpy.ops.export_scene.gltf(filepath=str(target), export_format='GLB', use_selection=True)
    print(f'RUNTIME_PART {part} source_objects={len(originals)} material_batches={len(joined)} pivot_z={pivot_z}')
    for obj in joined: bpy.data.objects.remove(obj, do_unlink=True)

def main():
    if OUT.exists() and any(OUT.iterdir()): raise FileExistsError(f'Output exists: {OUT}')
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    models=[obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    for part, pivot in (('base', 0), ('head', .52), ('barrels', 1.49)):
        selected=[obj for obj in models if group(obj.name) == part]
        if not selected: raise RuntimeError('Missing turret group: ' + part)
        export_part(part, selected, pivot)

if __name__ == '__main__': main()
