# Procedural low-poly enemy ship builder for U11 Starfighter 3D (Blender 4.2, headless).
# Blender coords: X = wings, -Y = nose (glTF +Z), Z = up (glTF +Y).  glTF = (x, z, -y).
import bpy, bmesh, math, json, os
import numpy as np
from mathutils import Vector, Matrix

HULL, DARK, ACCENT, GLASS, GLOW, METAL = range(6)

class Ship:
    def __init__(self):
        self.bm = bmesh.new()
        self.nozzles = []  # blender coords
    def _faces(self, verts):
        return {f for v in verts for f in v.link_faces}
    def _setmat(self, faces, m):
        for f in faces: f.material_index = m
    # ---- primitives
    def loft(self, secs, mat, n=12, p=2.0, off=0.0, cap_mat=None, mirror_x=False):
        """secs: list of (y, rx, rz, cz[, cx]); rx==0 -> point."""
        for sgn in ([1, -1] if mirror_x else [1]):
            rings = []
            for s in secs:
                y, rx, rz, cz = s[:4]; cx = (s[4] if len(s) > 4 else 0.0) * sgn
                if rx == 0:
                    rings.append([self.bm.verts.new((cx, y, cz))]); continue
                r = []
                for k in range(n):
                    a = 2 * math.pi * k / n + off
                    c, si = math.cos(a), math.sin(a)
                    x = cx + rx * math.copysign(abs(c) ** (2 / p), c)
                    z = cz + rz * math.copysign(abs(si) ** (2 / p), si)
                    r.append(self.bm.verts.new((x, y, z)))
                rings.append(r)
            faces = []
            for A, B in zip(rings, rings[1:]):
                if len(A) == 1 and len(B) == 1: continue
                if len(A) == 1:
                    for k in range(n): faces.append(self.bm.faces.new((A[0], B[k], B[(k + 1) % n])))
                elif len(B) == 1:
                    for k in range(n): faces.append(self.bm.faces.new((A[k], A[(k + 1) % n], B[0])))
                else:
                    for k in range(n): faces.append(self.bm.faces.new((A[k], A[(k + 1) % n], B[(k + 1) % n], B[k])))
            self._setmat(faces, mat)
            for R in (rings[0], rings[-1]):
                if len(R) > 2:
                    f = self.bm.faces.new(R); f.material_index = cap_mat if cap_mat is not None else mat
    def prism(self, pts, mat, plane='XY', off=(0, 0, 0), mirror_x=False, rot=None):
        """pts: [(u, v, thickness)] polygon in plane; extruded symmetric along the plane normal."""
        for sgn in ([1, -1] if mirror_x else [1]):
            top, bot = [], []
            for (u, v, t) in pts:
                if plane == 'XY': a, b = (u, v, t / 2), (u, v, -t / 2)
                elif plane == 'YZ': a, b = (t / 2, u, v), (-t / 2, u, v)
                else: a, b = (u, t / 2, v), (u, -t / 2, v)
                a, b = Vector(a), Vector(b)
                if rot is not None: a, b = rot @ a, rot @ b
                a += Vector(off); b += Vector(off)
                if sgn < 0: a.x, b.x = -a.x, -b.x
                top.append(self.bm.verts.new(a)); bot.append(self.bm.verts.new(b))
            n = len(pts); faces = [self.bm.faces.new(top), self.bm.faces.new(bot[::-1])]
            for k in range(n):
                faces.append(self.bm.faces.new((top[k], bot[k], bot[(k + 1) % n], top[(k + 1) % n])))
            self._setmat(faces, mat)
    def frustum(self, p0, p1, r0, r1, mat, n=10, cap0_mat=None, cap1_mat=None, mirror_x=False, squash=1.0):
        for sgn in ([1, -1] if mirror_x else [1]):
            P0 = Vector(p0); P1 = Vector(p1)
            if sgn < 0: P0.x, P1.x = -P0.x, -P1.x
            ax = (P1 - P0).normalized()
            t = Vector((0, 0, 1)) if abs(ax.z) < 0.9 else Vector((1, 0, 0))
            u = ax.cross(t).normalized(); w = ax.cross(u).normalized()
            def ring(P, r):
                if r == 0: return [self.bm.verts.new(P)]
                return [self.bm.verts.new(P + (u * math.cos(2 * math.pi * k / n) + w * squash * math.sin(2 * math.pi * k / n)) * r) for k in range(n)]
            A, B = ring(P0, r0), ring(P1, r1); faces = []
            if len(A) == 1:
                for k in range(n): faces.append(self.bm.faces.new((A[0], B[(k + 1) % n], B[k])))
            elif len(B) == 1:
                for k in range(n): faces.append(self.bm.faces.new((A[k], A[(k + 1) % n], B[0])))
            else:
                for k in range(n): faces.append(self.bm.faces.new((A[k], A[(k + 1) % n], B[(k + 1) % n], B[k])))
            self._setmat(faces, mat)
            if len(A) > 2: f = self.bm.faces.new(A[::-1]); f.material_index = mat if cap0_mat is None else cap0_mat
            if len(B) > 2: f = self.bm.faces.new(B); f.material_index = mat if cap1_mat is None else cap1_mat
    def sphere(self, c, r, mat, us=12, vs=8, mirror_x=False):
        for sgn in ([1, -1] if mirror_x else [1]):
            res = bmesh.ops.create_uvsphere(self.bm, u_segments=us, v_segments=vs, radius=1.0)
            vv = res['verts']
            for v in vv:
                v.co = Vector((v.co.x * r[0] + c[0] * sgn, v.co.y * r[1] + c[1], v.co.z * r[2] + c[2]))
            self._setmat(self._faces(vv), mat)
    def box(self, c, s, mat, mirror_x=False):
        for sgn in ([1, -1] if mirror_x else [1]):
            res = bmesh.ops.create_cube(self.bm, size=1.0)
            vv = res['verts']
            for v in vv:
                v.co = Vector((v.co.x * s[0] + c[0] * sgn, v.co.y * s[1] + c[1], v.co.z * s[2] + c[2]))
            self._setmat(self._faces(vv), mat)
    def engine(self, x, y0, y1, r, n=10, mirror_x=False, z=0.0, mat=METAL):
        """nozzle pointing +Y (tail): metal can + recessed glow disc; records nozzle point(s)."""
        self.frustum((x, y0, z), (x, y1, z), r * 0.85, r, mat, n=n, cap0_mat=mat, cap1_mat=DARK, mirror_x=mirror_x)
        self.frustum((x, y1 - 0.02, z), (x, y1 + 0.005, z), r * 0.72, r * 0.72, GLOW, n=n, mirror_x=mirror_x)
        self.nozzles.append((x, y1, z))
        if mirror_x: self.nozzles.append((-x, y1, z))

# ---------------------------------------------------------------- materials / bake / export
def make_material(name, color, kind):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; N = nt.nodes; L = nt.links
    bsdf = N['Principled BSDF']
    col = (*color, 1)
    if kind in ('glow', 'glass'):
        if kind == 'glass':
            # vertical gradient highlight on canopies
            tc = N.new('ShaderNodeTexCoord'); sep = N.new('ShaderNodeSeparateXYZ'); ramp = N.new('ShaderNodeValToRGB')
            L.new(tc.outputs['Normal'], sep.inputs[0]); L.new(sep.outputs['Z'], ramp.inputs[0])
            ramp.color_ramp.elements[0].color = (color[0] * 0.5, color[1] * 0.5, color[2] * 0.5, 1)
            ramp.color_ramp.elements[1].color = (min(1, color[0] * 2.4 + .15), min(1, color[1] * 2.4 + .15), min(1, color[2] * 2.4 + .15), 1)
            L.new(ramp.outputs[0], bsdf.inputs['Base Color'])
        else:
            bsdf.inputs['Base Color'].default_value = col
            bsdf.inputs['Emission Color'].default_value = (1, 1, 1, 1); bsdf.inputs['Emission Strength'].default_value = 1.0
    else:
        # object-space hull panels (brick texture = rectangular plating seams) + light grime noise
        tc = N.new('ShaderNodeTexCoord')
        sep = N.new('ShaderNodeSeparateXYZ'); L.new(tc.outputs['Object'], sep.inputs[0])
        comb = N.new('ShaderNodeCombineXYZ')
        add = N.new('ShaderNodeMath'); add.operation = 'MULTIPLY_ADD'; add.inputs[1].default_value = 0.8
        L.new(sep.outputs['Z'], add.inputs[0]); L.new(sep.outputs['X'], add.inputs[2])
        L.new(add.outputs[0], comb.inputs['X']); L.new(sep.outputs['Y'], comb.inputs['Y'])
        br = N.new('ShaderNodeTexBrick'); L.new(comb.outputs[0], br.inputs['Vector'])
        br.inputs['Color1'].default_value = (1, 1, 1, 1); br.inputs['Color2'].default_value = (0.86, 0.86, 0.86, 1)
        br.inputs['Mortar'].default_value = (0.5, 0.5, 0.5, 1)
        br.inputs['Scale'].default_value = 2.2 if kind != 'dark' else 3.5
        br.inputs['Mortar Size'].default_value = 0.012; br.inputs['Bias'].default_value = 0.0
        br.inputs['Brick Width'].default_value = 0.55; br.inputs['Row Height'].default_value = 0.3
        noi = N.new('ShaderNodeTexNoise'); noi.inputs['Scale'].default_value = 9; noi.inputs['Detail'].default_value = 4
        L.new(tc.outputs['Object'], noi.inputs['Vector'])
        nr = N.new('ShaderNodeMapRange'); nr.inputs['From Min'].default_value = 0.3; nr.inputs['From Max'].default_value = 0.7
        nr.inputs['To Min'].default_value = 0.85; nr.inputs['To Max'].default_value = 1.0
        L.new(noi.outputs['Fac'], nr.inputs['Value'])
        rgb = N.new('ShaderNodeRGB'); rgb.outputs[0].default_value = col
        vm = N.new('ShaderNodeVectorMath'); vm.operation = 'MULTIPLY'
        L.new(rgb.outputs[0], vm.inputs[0]); L.new(br.outputs['Color'], vm.inputs[1])
        mix = N.new('ShaderNodeVectorMath'); mix.operation = 'SCALE'
        L.new(vm.outputs[0], mix.inputs[0]); L.new(nr.outputs[0], mix.inputs['Scale'])
        L.new(mix.outputs[0], bsdf.inputs['Base Color'])
    return m

def finish(ship, name, palette, outdir, scale=1.0, target_tris=4200, smooth_deg=35.0, tex=1024):
    bm = ship.bm
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    # scale + centre bbox at origin (engine pivots at the origin; blue_racer bbox is centred)
    for v in bm.verts: v.co *= scale
    noz = [Vector(n) * scale for n in ship.nozzles]
    mn = Vector([min(v.co[i] for v in bm.verts) for i in range(3)]); mx = Vector([max(v.co[i] for v in bm.verts) for i in range(3)])
    c = (mn + mx) / 2
    for v in bm.verts: v.co -= c
    noz = [n - c for n in noz]
    # triangulate + densify (split longest edges) so hull dents have vertices to push
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for _ in range(40):
        nt = len(bm.faces)
        if nt >= target_tris * 0.97: break
        need = max(1, int((target_tris - nt) / 2.2))
        edges = sorted(bm.edges, key=lambda e: e.calc_length(), reverse=True)[:need]
        bmesh.ops.subdivide_edges(bm, edges=edges, cuts=1)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    for k, (mname, col, kind) in enumerate(palette):
        me.materials.append(make_material(f'{name}_{mname}', col, kind))
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth_deg))
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004)
    bpy.ops.object.mode_set(mode='OBJECT')
    # ---- bake base colour, AO, glow mask into one atlas
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 48
    def img(n, noncolor=False):
        i = bpy.data.images.new(n, tex, tex, alpha=False)
        if noncolor: i.colorspace_settings.name = 'Non-Color'
        return i
    def bake(i, typ, **kw):
        for m in me.materials:
            N = m.node_tree.nodes
            node = N.get('BAKE') or N.new('ShaderNodeTexImage'); node.name = 'BAKE'; node.image = i
            N.active = node
        bpy.ops.object.bake(type=typ, margin=6, use_clear=True, **kw)
        return np.array(i.pixels[:], dtype=np.float32).reshape(tex, tex, 4)
    dif = bake(img(name + '_dif'), 'DIFFUSE', pass_filter={'COLOR'})
    sc.world = sc.world or bpy.data.worlds.new('w')
    ao = bake(img(name + '_ao', True), 'AO')
    glow = bake(img(name + '_glow', True), 'EMIT')
    g = np.clip(glow[..., :1], 0, 1)
    aof = 0.45 + 0.55 * np.clip(ao[..., :1], 0, 1)
    out = dif[..., :3] * (aof * (1 - g) + g)
    final = bpy.data.images.new(name + '_tex', tex, tex, alpha=False)
    px = np.concatenate([np.clip(out, 0, 1), np.ones((tex, tex, 1), np.float32)], axis=2)
    final.pixels[:] = px.ravel()
    final.filepath_raw = os.path.join(outdir, name + '_basecolor.png'); final.file_format = 'PNG'; final.save()
    # ---- single export material: base colour texture only (what the engine's lit shader reads)
    em = bpy.data.materials.new(name + '_mat'); em.use_nodes = True
    N = em.node_tree.nodes; t = N.new('ShaderNodeTexImage'); t.image = final
    em.node_tree.links.new(t.outputs['Color'], N['Principled BSDF'].inputs['Base Color'])
    N['Principled BSDF'].inputs['Roughness'].default_value = 0.6
    me.materials.clear(); me.materials.append(em)
    for p in me.polygons: p.material_index = 0
    glb = os.path.join(outdir, name + '.glb')
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.ops.export_scene.gltf(filepath=glb, export_format='GLB', use_selection=True, export_yup=True, export_apply=True,
                              export_texcoords=True, export_normals=True, export_materials='EXPORT', export_animations=False,
                              export_image_format='AUTO')
    me.calc_loop_triangles()
    vs = [v.co for v in me.vertices]
    bmn = [min(v[i] for v in vs) for i in range(3)]; bmx = [max(v[i] for v in vs) for i in range(3)]
    g2 = lambda v: [round(v[0], 3), round(v[2], 3), round(-v[1], 3)]
    info = {'name': name, 'file': os.path.basename(glb), 'tris': len(me.loop_triangles), 'blender_verts': len(me.vertices),
            'bbox_gltf_min': [round(bmn[0], 3), round(bmn[2], 3), round(-bmx[1], 3)],
            'bbox_gltf_max': [round(bmx[0], 3), round(bmx[2], 3), round(-bmn[1], 3)],
            'nozzles_gltf': [g2(n) for n in noz[:2]] if len(noz) >= 2 else [g2(noz[0]), g2(noz[0])]}
    json.dump(info, open(os.path.join(outdir, name + '.json'), 'w'), indent=1)
    print('SHIPINFO', json.dumps(info))
