"""Procedural hard-surface kit for the U11 Starfighter alien-robot enemy fleet.

Blender 4.2, Z-up. Enemy convention: nose at Blender -Y (glTF +Z), wings along X, up +Z.
Every part is a separate mesh object that is rigidly bound to one bone. Two quality levels:
'high' (bevels, dense rings, all greebles) and 'low' (game LOD, ~4-6k tris).
"""
import bpy, bmesh, math, random
from mathutils import Vector, Matrix, Euler, Quaternion
from mathutils.bvhtree import BVHTree

Q = {
    'high': dict(ring=40, cyl=32, sph=(32, 16), bev_seg=3, bev_w=0.010, interp=4, greeble=True, tor=(40, 12)),
    'low':  dict(ring=20, cyl=14, sph=(16, 9),  bev_seg=0, bev_w=0.0, interp=2, greeble=False, tor=(20, 6)),
}

# ------------------------------------------------------------------ materials (Cycles procedural)
PALETTE = {
    'hull':   dict(base=(0.25, 0.26, 0.29), metal=0.75, rough=0.38, panel=2.6, wear=1.0),
    'armor':  dict(base=(0.17, 0.06, 0.25), metal=0.45, rough=0.40, panel=2.0, wear=1.0),   # alien violet
    'dark':   dict(base=(0.05, 0.05, 0.06), metal=0.6, rough=0.5, panel=4.0, wear=0.6),
    'steel':  dict(base=(0.50, 0.52, 0.55), metal=0.9, rough=0.25, panel=5.0, wear=0.3),
    'bone':   dict(base=(0.62, 0.58, 0.50), metal=0.1, rough=0.6, panel=2.5, wear=0.4),   # robot skull plating
    'hazard': dict(base=(0.85, 0.62, 0.05), metal=0.2, rough=0.5, panel=6.0, wear=1.0, stripes=True),
    'glow_green': dict(glow=(0.35, 1.0, 0.25), strength=4.0),
    'glow_red':   dict(glow=(1.0, 0.05, 0.02), strength=5.0),
    'glow_mag':   dict(glow=(1.0, 0.15, 0.85), strength=7.0),
    'glow_cyan':  dict(glow=(0.2, 0.9, 1.0), strength=6.0),
    'glow_fire':  dict(glow=(1.0, 0.45, 0.05), strength=7.0),
    'brain':      dict(glow=(0.30, 1.0, 0.30), strength=2.2, veins=True),
}
_MATS = {}


def _n(nt, t, loc=(0, 0), **kw):
    n = nt.nodes.new(t); n.location = loc
    for k, v in kw.items():
        if k in n.inputs: n.inputs[k].default_value = v
        else: setattr(n, k, v)
    return n


def _brick(nt, L, vec, scale, loc):
    b = _n(nt, 'ShaderNodeTexBrick', loc, offset=0.37, offset_frequency=3, squash=0.55, squash_frequency=3)
    b.inputs['Scale'].default_value = scale
    b.inputs['Mortar Size'].default_value = 0.007
    b.inputs['Mortar Smooth'].default_value = 0.2
    b.inputs['Bias'].default_value = 0.0
    b.inputs['Brick Width'].default_value = 1.35
    b.inputs['Row Height'].default_value = 0.42
    b.inputs['Color1'].default_value = (1, 1, 1, 1)
    b.inputs['Color2'].default_value = (0.80, 0.80, 0.80, 1)
    b.inputs['Mortar'].default_value = (0, 0, 0, 1)
    L.new(vec, b.inputs['Vector'])
    return b.outputs['Color']


def material(key):
    if key in _MATS: return _MATS[key]
    p = PALETTE[key]
    m = bpy.data.materials.new('M_' + key); m.use_nodes = True
    nt = m.node_tree; L = nt.links
    bsdf = nt.nodes['Principled BSDF']; out = nt.nodes['Material Output']
    albedo = _n(nt, 'ShaderNodeRGB', (-200, 400)); albedo.name = 'ALBEDO_SRC'
    if 'glow' in p:
        g = p['glow']
        col_out = None
        if p.get('veins'):
            tc = _n(nt, 'ShaderNodeTexCoord', (-1400, 0))
            vor = _n(nt, 'ShaderNodeTexVoronoi', (-1200, 0), feature='DISTANCE_TO_EDGE')
            vor.inputs['Scale'].default_value = 26.0
            L.new(tc.outputs['Object'], vor.inputs['Vector'])
            mr = _n(nt, 'ShaderNodeMapRange', (-1000, 0)); mr.inputs['From Min'].default_value = 0.0; mr.inputs['From Max'].default_value = 0.06
            mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.25
            L.new(vor.outputs['Distance'], mr.inputs['Value'])
            mix = _n(nt, 'ShaderNodeMix', (-800, 0), data_type='RGBA')
            mix.inputs[6].default_value = (g[0] * 0.25, g[1] * 0.25, g[2] * 0.25, 1)
            mix.inputs[7].default_value = (min(1, g[0] * 1.6 + 0.2), min(1, g[1] * 1.0), min(1, g[2] * 1.6 + 0.2), 1)
            L.new(mr.outputs['Result'], mix.inputs['Factor'])
            col_out = mix.outputs[2]
        else:
            rgb = _n(nt, 'ShaderNodeRGB', (-800, 0)); rgb.outputs[0].default_value = (*g, 1)
            col_out = rgb.outputs[0]
        rr = _n(nt, 'NodeReroute', (-400, 0)); rr.name = 'ALBEDO'
        L.new(col_out, rr.inputs[0])
        L.new(rr.outputs[0], bsdf.inputs['Base Color'])
        L.new(rr.outputs[0], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = p['strength']
        bsdf.inputs['Roughness'].default_value = 0.2
        m['glow'] = p['strength']
        _MATS[key] = m
        return m
    base = p['base']
    tc = _n(nt, 'ShaderNodeTexCoord', (-2200, 0))
    sep = _n(nt, 'ShaderNodeSeparateXYZ', (-2000, 0)); L.new(tc.outputs['Object'], sep.inputs[0])
    def comb(a, b, loc):
        c = _n(nt, 'ShaderNodeCombineXYZ', loc); L.new(sep.outputs[a], c.inputs[0]); L.new(sep.outputs[b], c.inputs[1]); return c.outputs[0]
    bxy = _brick(nt, L, comb(0, 1, (-1800, 300)), p['panel'], (-1600, 300))
    byz = _brick(nt, L, comb(1, 2, (-1800, 0)), p['panel'], (-1600, 0))
    bxz = _brick(nt, L, comb(0, 2, (-1800, -300)), p['panel'], (-1600, -300))
    geo = _n(nt, 'ShaderNodeNewGeometry', (-2200, -600))
    nsep = _n(nt, 'ShaderNodeSeparateXYZ', (-2000, -600)); L.new(geo.outputs['Normal'], nsep.inputs[0])
    absn = []
    for i in range(3):
        a = _n(nt, 'ShaderNodeMath', (-1800, -600 - i * 120), operation='ABSOLUTE'); L.new(nsep.outputs[i], a.inputs[0]); absn.append(a.outputs[0])
    def sub(a, b, loc):
        s = _n(nt, 'ShaderNodeMath', loc, operation='SUBTRACT'); L.new(a, s.inputs[0]); L.new(b, s.inputs[1]); return s.outputs[0]
    def mrange(v, a, b, loc):
        r = _n(nt, 'ShaderNodeMapRange', loc); r.inputs['From Min'].default_value = a; r.inputs['From Max'].default_value = b; L.new(v, r.inputs['Value']); return r.outputs['Result']
    def mixc(f, a, b, loc):
        mx = _n(nt, 'ShaderNodeMix', loc, data_type='RGBA'); L.new(f, mx.inputs['Factor']); L.new(a, mx.inputs[6]); L.new(b, mx.inputs[7]); return mx.outputs[2]
    f1 = mrange(sub(absn[1], absn[0], (-1600, -700)), -0.15, 0.15, (-1400, -700))       # |ny| vs |nx|: yz-plane (x facing) vs xz-plane (y facing)
    mx_ = _n(nt, 'ShaderNodeMath', (-1600, -850), operation='MAXIMUM'); L.new(absn[0], mx_.inputs[0]); L.new(absn[1], mx_.inputs[1])
    f2 = mrange(sub(absn[2], mx_.outputs[0], (-1400, -850)), -0.15, 0.15, (-1200, -850))
    side = mixc(f1, byz, bxz, (-1200, -200))
    panels = mixc(f2, side, bxy, (-1000, 0))
    # panel lines + per-plate tone
    pbw = _n(nt, 'ShaderNodeRGBToBW', (-850, 0)); L.new(panels, pbw.inputs[0])
    lines = mrange(pbw.outputs[0], 0.02, 0.6, (-700, 0))  # 0 in seams -> 1 on plates
    tone = _n(nt, 'ShaderNodeMapRange', (-700, 150)); L.new(pbw.outputs[0], tone.inputs['Value'])
    tone.inputs['From Min'].default_value = 0.79; tone.inputs['From Max'].default_value = 1.0
    tone.inputs['To Min'].default_value = 0.86; tone.inputs['To Max'].default_value = 1.06
    # grime noise
    noise = _n(nt, 'ShaderNodeTexNoise', (-1000, 500)); noise.inputs['Scale'].default_value = 9.0; noise.inputs['Detail'].default_value = 8.0
    L.new(tc.outputs['Object'], noise.inputs['Vector'])
    grime = mrange(noise.outputs['Fac'], 0.35, 0.75, (-800, 500))
    gr2 = _n(nt, 'ShaderNodeMapRange', (-650, 500)); L.new(grime, gr2.inputs['Value']); gr2.inputs['To Min'].default_value = 0.72; gr2.inputs['To Max'].default_value = 1.0
    # base colour (with optional hazard stripes)
    basec = _n(nt, 'ShaderNodeRGB', (-700, 700)); basec.outputs[0].default_value = (*base, 1)
    bcol = basec.outputs[0]
    if p.get('stripes'):
        wave = _n(nt, 'ShaderNodeTexWave', (-1000, 800), wave_type='BANDS', bands_direction='DIAGONAL')
        wave.inputs['Scale'].default_value = 7.0; wave.inputs['Distortion'].default_value = 0.0
        L.new(tc.outputs['Object'], wave.inputs['Vector'])
        st = _n(nt, 'ShaderNodeMath', (-850, 800), operation='GREATER_THAN'); st.inputs[1].default_value = 0.5; L.new(wave.outputs['Fac'], st.inputs[0])
        blk = _n(nt, 'ShaderNodeRGB', (-850, 950)); blk.outputs[0].default_value = (0.03, 0.03, 0.03, 1)
        bcol = mixc(st.outputs[0], bcol, blk.outputs[0], (-600, 800))
    def mulc(a, f, loc):
        mm = _n(nt, 'ShaderNodeMix', loc, data_type='RGBA', blend_type='MULTIPLY'); mm.inputs['Factor'].default_value = 1.0
        L.new(a, mm.inputs[6]); 
        if isinstance(f, float): mm.inputs[7].default_value = (f, f, f, 1)
        else: L.new(f, mm.inputs[7])
        return mm.outputs[2]
    def f2c(v, loc):
        c = _n(nt, 'ShaderNodeCombineColor', loc); L.new(v, c.inputs[0]); L.new(v, c.inputs[1]); L.new(v, c.inputs[2]); return c.outputs[0]
    col = mulc(bcol, f2c(tone.outputs['Result'], (-500, 150)), (-350, 600))
    col = mulc(col, f2c(gr2.outputs['Result'], (-500, 500)), (-200, 600))
    seam = _n(nt, 'ShaderNodeMapRange', (-500, 0)); L.new(lines, seam.inputs['Value']); seam.inputs['To Min'].default_value = 0.18; seam.inputs['To Max'].default_value = 1.0
    col = mulc(col, f2c(seam.outputs['Result'], (-350, 0)), (-50, 400))
    # edge wear from the bevel node: bright worn metal on convex edges
    bev = _n(nt, 'ShaderNodeBevel', (-1000, -1100)); bev.samples = 6; bev.inputs['Radius'].default_value = 0.012
    dot = _n(nt, 'ShaderNodeVectorMath', (-800, -1100), operation='DOT_PRODUCT'); L.new(bev.outputs[0], dot.inputs[0]); L.new(geo.outputs['Normal'], dot.inputs[1])
    edge = _n(nt, 'ShaderNodeMapRange', (-600, -1100)); L.new(dot.outputs['Value'], edge.inputs['Value'])
    edge.inputs['From Min'].default_value = 0.985; edge.inputs['From Max'].default_value = 0.90
    n2 = _n(nt, 'ShaderNodeTexNoise', (-800, -1300)); n2.inputs['Scale'].default_value = 40.0; n2.inputs['Detail'].default_value = 4.0
    L.new(tc.outputs['Object'], n2.inputs['Vector'])
    wm = _n(nt, 'ShaderNodeMath', (-450, -1200), operation='MULTIPLY'); L.new(edge.outputs['Result'], wm.inputs[0])
    nm = mrange(n2.outputs['Fac'], 0.45, 0.6, (-600, -1300)); L.new(nm, wm.inputs[1])
    wf = _n(nt, 'ShaderNodeMath', (-300, -1200), operation='MULTIPLY'); wf.inputs[1].default_value = p['wear']; L.new(wm.outputs[0], wf.inputs[0])
    worn = _n(nt, 'ShaderNodeRGB', (-300, -1400)); worn.outputs[0].default_value = (0.72, 0.73, 0.76, 1)
    col = mixc(wf.outputs[0], col, worn.outputs[0], (100, 200))
    rr = _n(nt, 'NodeReroute', (300, 200)); rr.name = 'ALBEDO'
    L.new(col, rr.inputs[0]); L.new(rr.outputs[0], bsdf.inputs['Base Color'])
    bsdf.inputs['Metallic'].default_value = p['metal']
    rough = _n(nt, 'ShaderNodeMapRange', (100, -300)); L.new(noise.outputs['Fac'], rough.inputs['Value'])
    rough.inputs['To Min'].default_value = p['rough'] - 0.1; rough.inputs['To Max'].default_value = p['rough'] + 0.15
    L.new(rough.outputs['Result'], bsdf.inputs['Roughness'])
    bsdf.inputs['Emission Strength'].default_value = 0.0
    m['glow'] = 0.0
    _MATS[key] = m
    return m


# ------------------------------------------------------------------ geometry kit
class Kit:
    def __init__(self, quality):
        self.q = Q[quality]; self.quality = quality
        self.parts = []          # (obj, bone)
        self.hull_objs = []      # raycast targets for greeble placement
        self._bvh = None

    # -- object plumbing
    def _obj(self, name, bm, mat, bone, smooth=True, hull=False):
        me = bpy.data.meshes.new(name)
        if smooth:
            for f in bm.faces: f.smooth = True
        bm.normal_update()
        bm.to_mesh(me); bm.free()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        me.materials.append(material(mat))
        self.parts.append((ob, bone))
        if hull: self.hull_objs.append(ob); self._bvh = None
        return ob

    def _bevel(self, bm, w=None, seg=None, angle=30):
        w = self.q['bev_w'] if w is None else w
        seg = self.q['bev_seg'] if seg is None else seg
        if seg <= 0 or w <= 0: return
        edges = [e for e in bm.edges if e.is_manifold and e.calc_face_angle(0) > math.radians(angle)]
        if edges:
            bmesh.ops.bevel(bm, geom=edges, offset=w, segments=seg, profile=0.5, affect='EDGES', clamp_overlap=True)

    @staticmethod
    def _xf(bm, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
        M = Matrix.Translation(loc) @ Euler(rot).to_matrix().to_4x4() @ Matrix.Diagonal((*scale, 1))
        bmesh.ops.transform(bm, matrix=M, verts=bm.verts)

    # -- primitives
    def box(self, name, size, loc, mat, bone='Hull', rot=(0, 0, 0), bevel=True, taper=None, w=None, hull=False):
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
        if taper:  # taper=(tx, tz): scale of the -Y (front) end relative to the back
            for v in bm.verts:
                if v.co.y < 0: v.co.x *= taper[0]; v.co.z *= taper[1]
        self._xf(bm, scale=size)
        if bevel: self._bevel(bm, w=w if w is not None else min(self.q['bev_w'], min(size) * 0.25))
        self._xf(bm, loc, rot)
        return self._obj(name, bm, mat, bone, hull=hull)

    def cyl(self, name, r, depth, loc, mat, bone='Hull', rot=(0, 0, 0), r2=None, segs=None, caps=True, bevel=True, hull=False):
        segs = segs or self.q['cyl']
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=segs, radius1=r, radius2=r if r2 is None else r2, depth=depth)
        if bevel: self._bevel(bm, w=min(self.q['bev_w'], r * 0.2), angle=50)
        self._xf(bm, loc, rot)
        return self._obj(name, bm, mat, bone, hull=hull)

    def sphere(self, name, r, loc, mat, bone='Hull', scale=(1, 1, 1), rot=(0, 0, 0), hemi=False, hull=False):
        u, v = self.q['sph']
        bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=u, v_segments=v, radius=r)
        if hemi:
            bmesh.ops.delete(bm, geom=[vv for vv in bm.verts if vv.co.z < -1e-4], context='VERTS')
            edges = [e for e in bm.edges if e.is_boundary]
            if edges: bmesh.ops.edgeloop_fill(bm, edges=edges)
        self._xf(bm, loc, rot, scale)
        return self._obj(name, bm, mat, bone, hull=hull)

    def torus(self, name, R, r, loc, mat, bone='Hull', rot=(0, 0, 0), scale=(1, 1, 1)):
        U, V = self.q['tor']
        bm = bmesh.new(); rings = []
        for i in range(U):
            a = 2 * math.pi * i / U; ring = []
            for j in range(V):
                b = 2 * math.pi * j / V
                ring.append(bm.verts.new(((R + r * math.cos(b)) * math.cos(a), (R + r * math.cos(b)) * math.sin(a), r * math.sin(b))))
            rings.append(ring)
        for i in range(U):
            for j in range(V):
                bm.faces.new((rings[i][j], rings[(i + 1) % U][j], rings[(i + 1) % U][(j + 1) % V], rings[i][(j + 1) % V]))
        self._xf(bm, loc, rot, scale)
        return self._obj(name, bm, mat, bone)

    def loft(self, name, sections, mat, bone='Hull', ring=None, hull=True, cap=True, loc=(0, 0, 0)):
        """sections: (y, half_w, half_h, z_centre, exponent[, x_centre]) ordered nose->tail."""
        n = ring or self.q['ring']
        secs = [tuple(s) + ((0.0,) if len(s) == 5 else ()) for s in sections]
        k = self.q['interp']
        if k > 1:  # Catmull-Rom densify along the length
            dense = []
            for i in range(len(secs) - 1):
                p0 = secs[max(0, i - 1)]; p1 = secs[i]; p2 = secs[i + 1]; p3 = secs[min(len(secs) - 1, i + 2)]
                for s in range(k):
                    t = s / k
                    dense.append(tuple(0.5 * ((2 * p1[c]) + (-p0[c] + p2[c]) * t + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t * t + (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t ** 3) for c in range(6)))
            dense.append(secs[-1]); secs = dense
        bm = bmesh.new(); rings = []
        for (y, hw, hh, zc, ex, xc) in secs:
            hw = max(hw, 1e-3); hh = max(hh, 1e-3); r = []
            for i in range(n):
                a = 2 * math.pi * i / n; c, s = math.cos(a), math.sin(a)
                x = hw * math.copysign(abs(c) ** (2 / ex), c); z = hh * math.copysign(abs(s) ** (2 / ex), s)
                r.append(bm.verts.new((xc + x, y, zc + z)))
            rings.append(r)
        for a, b in zip(rings[:-1], rings[1:]):
            for i in range(n):
                bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
        if cap:
            bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        self._xf(bm, loc)
        return self._obj(name, bm, mat, bone, hull=hull)

    def plate(self, name, outline, thick, mat, bone='Hull', z=0.0, tip_thin=0.5, rot=(0, 0, 0), pivot=(0, 0, 0), hull=False, bevel=True):
        """Extruded wing/fin from a 2D (x, y) outline (counter-clockwise seen from +Z). Thickness thins toward |x| max."""
        bm = bmesh.new()
        xs = [p[0] for p in outline]; x0, x1 = min(xs), max(xs)
        top = [bm.verts.new((x, y, z)) for x, y in outline]
        f = bm.faces.new(top)
        res = bmesh.ops.extrude_face_region(bm, geom=[f])
        newv = [g for g in res['geom'] if isinstance(g, bmesh.types.BMVert)]
        for v in newv: v.co.z -= thick
        for v in bm.verts:  # thin toward the tip, symmetric about the mid-plane
            t = (abs(v.co.x) - min(abs(x0), abs(x1))) / max(1e-4, abs(x1 - x0))
            t = min(1, max(0, t)); mid = z - thick / 2
            v.co.z = mid + (v.co.z - mid) * (1 - (1 - tip_thin) * t)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        if bevel: self._bevel(bm, w=min(self.q['bev_w'], thick * 0.3), angle=40)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
        M = Matrix.Translation(pivot) @ Euler(rot).to_matrix().to_4x4() @ Matrix.Translation(Vector(pivot) * -1)
        bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
        return self._obj(name, bm, mat, bone, hull=hull)

    def tube(self, name, pts, r, mat, bone='Hull'):
        cu = bpy.data.curves.new(name + '_c', 'CURVE'); cu.dimensions = '3D'
        cu.bevel_depth = r; cu.bevel_resolution = 3 if self.quality == 'high' else 0
        cu.resolution_u = 8 if self.quality == 'high' else 2; cu.use_fill_caps = True
        sp = cu.splines.new('BEZIER'); sp.bezier_points.add(len(pts) - 1)
        for bp, p in zip(sp.bezier_points, pts):
            bp.co = p; bp.handle_left_type = bp.handle_right_type = 'AUTO'
        tmp = bpy.data.objects.new(name + '_tmp', cu); bpy.context.scene.collection.objects.link(tmp)
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
        bpy.data.objects.remove(tmp); bpy.data.curves.remove(cu)
        bm = bmesh.new(); bm.from_mesh(me); bpy.data.meshes.remove(me)
        return self._obj(name, bm, mat, bone)

    def mirror(self, ob, name=None, bone=None):
        """X-mirrored copy (new object). bone defaults to the source bone with L<->R swapped."""
        src_bone = dict((o.name, b) for o, b in self.parts)[ob.name]
        if bone is None: bone = src_bone.replace('L', '#').replace('R', 'L').replace('#', 'R') if src_bone[-1] in 'LR' else src_bone
        bm = bmesh.new(); bm.from_mesh(ob.data)
        bmesh.ops.scale(bm, vec=(-1, 1, 1), verts=bm.verts)
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
        mat = ob.data.materials[0].name[2:]
        return self._obj(name or (ob.name + '_m'), bm, mat, bone, hull=ob in self.hull_objs)

    # -- surface placement for greebles
    def surface(self, x, y, z_from=3.0, direction=(0, 0, -1)):
        if self._bvh is None:
            bm = bmesh.new()
            for o in self.hull_objs: bm.from_mesh(o.data)
            self._bvh = BVHTree.FromBMesh(bm); bm.free()
        hit = self._bvh.ray_cast(Vector((x, y, z_from)) if direction == (0, 0, -1) else Vector((x, y, z_from)), Vector(direction))
        return hit[0], hit[1]

    def greeble(self, name, x, y, size, mat='dark', bone='Hull', kind='box', from_below=False, rot_z=0.0, sink=0.25):
        """Small detail object glued to the hull surface found by a vertical ray. High LOD only unless big."""
        if not self.q['greeble'] and max(size) < 0.09: return None
        p, nrm = self.surface(x, y, -3.0 if from_below else 3.0, (0, 0, 1) if from_below else (0, 0, -1))
        if p is None: return None
        up = Vector((0, 0, 1)); q = up.rotation_difference(nrm)
        rot = (q @ Quaternion((0, 0, 1), rot_z)).to_euler()
        c = p + nrm * (size[2] * (0.5 - sink))
        if kind == 'box': return self.box(name, size, tuple(c), mat, bone, rot=tuple(rot))
        if kind == 'cyl': return self.cyl(name, size[0] / 2, size[2], tuple(c), mat, bone, rot=tuple(rot), segs=max(8, self.q['cyl'] // 2))
        if kind == 'dome': return self.sphere(name, size[0] / 2, tuple(p), mat, bone, scale=(1, size[1] / size[0], size[2] / (size[0] / 2)), rot=tuple(rot), hemi=True)

    def vents(self, prefix, x, y0, y1, n, w, mat='dark', bone='Hull', from_below=False):
        for i in range(n):
            y = y0 + (y1 - y0) * (i + 0.5) / n
            self.greeble(f'{prefix}{i}', x, y, (w, (y1 - y0) / n * 0.55, 0.018), mat, bone, from_below=from_below)


# ------------------------------------------------------------------ shared faction details
def robot_pilot(k, y, z, s=1.0, eyes=1, bone='Hull'):
    """Exposed robot pilot head in an open cockpit frame + glowing visor."""
    k.box('pilot_neck', (0.10 * s, 0.10 * s, 0.08 * s), (0, y + 0.02 * s, z + 0.02 * s), 'dark', bone)
    k.box('pilot_head', (0.15 * s, 0.15 * s, 0.11 * s), (0, y, z + 0.09 * s), 'bone', bone, taper=(0.8, 0.75))
    k.box('pilot_brow', (0.16 * s, 0.05 * s, 0.03 * s), (0, y - 0.065 * s, z + 0.13 * s), 'hull', bone)
    if eyes == 1:
        k.box('pilot_visor', (0.12 * s, 0.02 * s, 0.025 * s), (0, y - 0.077 * s, z + 0.095 * s), 'glow_red', bone, bevel=False)
    else:
        for i in range(eyes):
            xx = (i - (eyes - 1) / 2) * 0.042 * s
            k.sphere(f'pilot_eye{i}', 0.016 * s, (xx, y - 0.07 * s, z + 0.095 * s), 'glow_red', bone)
    k.box('pilot_jaw', (0.10 * s, 0.05 * s, 0.035 * s), (0, y - 0.05 * s, z + 0.04 * s), 'dark', bone)
    for sx in (-1, 1):
        k.cyl(f'pilot_ear{sx}', 0.03 * s, 0.03 * s, (sx * 0.085 * s, y, z + 0.09 * s), 'hull', bone, rot=(0, math.pi / 2, 0), segs=12)
    k.cyl('pilot_antenna', 0.006 * s, 0.12 * s, (0.05 * s, y + 0.04 * s, z + 0.19 * s), 'steel', bone, segs=6)
    k.sphere('pilot_antenna_tip', 0.012 * s, (0.05 * s, y + 0.04 * s, z + 0.25 * s), 'glow_red', bone)
    # cockpit frame: a horseshoe of armour around the pilot
    k.torus('cockpit_rim', 0.13 * s, 0.022 * s, (0, y, z + 0.015 * s), 'armor', bone, scale=(1.0, 1.25, 1.0))


def brain_dome(k, y, z, r, bone='Hull', cables=True):
    """The alien controller: a glowing bio-brain under a reinforced dome, wired into the hull."""
    k.torus('brain_ring', r * 1.02, r * 0.14, (0, y, z), 'steel', bone)
    k.sphere('brain_dome', r, (0, y, z), 'brain', bone, hemi=True, scale=(1, 1.1, 0.85))
    if cables and k.q['greeble']:
        for sx in (-1, 1):
            k.tube(f'brain_cable{sx}', [(sx * r * 0.9, y + r * 0.3, z + 0.01), (sx * r * 1.5, y + r * 1.2, z - 0.02), (sx * r * 1.6, y + r * 2.4, z - 0.05)], 0.012, 'dark', bone)


def engine(k, name, x, y, z, r, length, bone='Hull', glow='glow_green'):
    """Rear engine can: housing, banding, dark bell and glowing core. Returns the nozzle point."""
    k.cyl(name + '_can', r, length, (x, y, z), 'hull', bone, rot=(math.pi / 2, 0, 0), hull=True)
    k.cyl(name + '_band', r * 1.08, length * 0.12, (x, y + length * 0.25, z), 'dark', bone, rot=(math.pi / 2, 0, 0))
    k.cyl(name + '_bell', r * 1.1, length * 0.25, (x, y + length * 0.6, z), 'dark', bone, rot=(math.pi / 2, 0, 0), r2=r * 0.95)
    k.cyl(name + '_core', r * 0.62, 0.02, (x, y + length * 0.66, z), glow, bone, rot=(math.pi / 2, 0, 0), bevel=False)
    return (x, y + length * 0.72, z)
