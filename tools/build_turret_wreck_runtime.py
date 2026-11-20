"""Create a batched wreck from the detailed turret's actual mounting base."""
from pathlib import Path
import math
import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[1] / 'assets/models/ship_interior/hazards'
SOURCE = ROOT / 'turret_detailed_v1/turret.blend'
OUT = ROOT / 'turret_wreck_runtime_v2.glb'
BASE = ('Base ', 'Bearing ', 'Mount bolt', 'Armored radial foot', 'Turntable gear tooth')

def material(name, color, metallic, roughness):
    m=bpy.data.materials.new(name); m.use_nodes=True
    node=m.node_tree.nodes.get('Principled BSDF'); node.inputs['Base Color'].default_value=(*color,1)
    node.inputs['Metallic'].default_value=metallic; node.inputs['Roughness'].default_value=roughness
    return m

def main():
    if OUT.exists(): raise FileExistsError(f'Refusing to overwrite {OUT}')
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    originals=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.name.startswith(BASE)]
    if not originals: raise RuntimeError('Detailed turret base not found')
    parts=[]
    for source in originals:
        obj=source.copy(); obj.data=source.data.copy(); bpy.context.collection.objects.link(obj)
        obj.matrix_world=source.matrix_world.copy(); parts.append(obj)
    char=material('Wreck scorched interior',(.025,.022,.025),.7,.62)
    torn=material('Wreck exposed torn steel',(.23,.21,.19),.85,.35)
    cables=[material('Wreck insulated cable '+str(i), color,.15,.55) for i,color in enumerate(((.025,.025,.03),(.35,.10,.025),(.025,.23,.30)))]
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=.81, depth=.10, location=(0,0,.42))
    interior=bpy.context.object; interior.name='Scorched bearing interior'; interior.data.materials.append(char); parts.append(interior)
    for i in range(15):
        a=i*math.tau/15; w=.12+(i%3)*.025; r=.69+(i%2)*.07; high=.54+(i%4)*.11
        tangent=(-math.sin(a),math.cos(a)); x,y=r*math.cos(a),r*math.sin(a)
        verts=[(x+tangent[0]*w,y+tangent[1]*w,.44),(x-tangent[0]*w,y-tangent[1]*w,.44),
               (x*.90+tangent[0]*w*.28,y*.90+tangent[1]*w*.28,high)]
        mesh=bpy.data.meshes.new('Torn steel mesh'); mesh.from_pydata(verts,[],[(0,1,2)]); mesh.update()
        obj=bpy.data.objects.new('Torn jagged plating',mesh); bpy.context.collection.objects.link(obj); obj.data.materials.append(torn)
        mod=obj.modifiers.new('Metal thickness','SOLIDIFY'); mod.thickness=.025
        bpy.context.view_layer.objects.active=obj; bpy.ops.object.modifier_apply(modifier=mod.name); parts.append(obj)
    for i in range(7):
        a=i*math.tau/7; curve=bpy.data.curves.new('Exposed cable','CURVE'); curve.dimensions='3D'; curve.bevel_depth=.025; curve.bevel_resolution=2
        spline=curve.splines.new('POLY'); spline.points.add(3)
        for point,(r,z) in zip(spline.points,((.18,.52),(.46,.72),(.83,.63),(1.0,.35+i*.025))):
            point.co=(r*math.cos(a),r*math.sin(a),z,1)
        obj=bpy.data.objects.new('Exposed cable',curve); bpy.context.collection.objects.link(obj); obj.data.materials.append(cables[i%3])
        bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
        bpy.ops.object.convert(target='MESH'); parts.append(bpy.context.object)
    batches={}
    for obj in parts: batches.setdefault(obj.data.materials[0],[]).append(obj)
    joined=[]
    for batch in batches.values():
        bpy.ops.object.select_all(action='DESELECT')
        for obj in batch: obj.select_set(True)
        bpy.context.view_layer.objects.active=batch[0]
        if len(batch)>1: bpy.ops.object.join()
        obj=bpy.context.object; obj.data.transform(obj.matrix_world); obj.matrix_world=Matrix.Identity(4); joined.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in joined: obj.select_set(True)
    bpy.context.view_layer.objects.active=joined[0]
    bpy.ops.export_scene.gltf(filepath=str(OUT),export_format='GLB',use_selection=True)
    print(f'WRECK exported {len(parts)} parts in {len(joined)} material batches')

if __name__=='__main__': main()
