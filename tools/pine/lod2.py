# blender -b -P lod2.py -- <in.glb> <outdir>
# Structural LOD: canopy -> k-means clusters -> convex hull of farthest-point samples;
# branches -> PCA prisms (LOD0 only); trunk -> procedural tapered cylinder with flare.
import bpy, bmesh, sys, math, json, mathutils
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
src, outd = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
objs = [o for o in bpy.context.scene.objects if o.type == 'MESH']
H = 18.0
rng = np.random.default_rng(7)

def albedo(o):
    for n in o.data.materials[0].node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image and 'albedo' in n.image.name.lower(): return n.image
def tri_data(o):
    """triangle centroids, vertex positions (T,3,3), per-tri albedo colour (sRGB)"""
    me = o.data; me.calc_loop_triangles()
    nv = len(me.vertices); co = np.empty(nv * 3, np.float32); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    nt = len(me.loop_triangles)
    tv = np.empty(nt * 3, np.int64); me.loop_triangles.foreach_get("vertices", tv); tv = tv.reshape(-1, 3)
    tl = np.empty(nt * 3, np.int64); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(-1, 3)
    img = albedo(o); w, h = img.size
    px = np.empty(w * h * 4, np.float32); img.pixels.foreach_get(px); px = px.reshape(h, w, 4)
    uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    tuv = uv[tl].mean(1)
    xi = np.clip((tuv[:, 0] % 1) * w, 0, w - 1).astype(int); yi = np.clip((tuv[:, 1] % 1) * h, 0, h - 1).astype(int)
    P = co[tv]
    return P.mean(1), P, px[yi, xi, :3]

def kmeans(X, k, it=25):
    C = X[rng.choice(len(X), k, replace=False)]
    for _ in range(it):
        d = ((X[:, None, :] - C[None]) ** 2).sum(-1) if len(X) * k < 4e7 else None
        if d is None:
            lab = np.concatenate([((X[i:i + 20000, None, :] - C[None]) ** 2).sum(-1).argmin(1) for i in range(0, len(X), 20000)])
        else: lab = d.argmin(1)
        for j in range(k):
            m = lab == j
            if m.any(): C[j] = X[m].mean(0)
            else: C[j] = X[rng.integers(len(X))]
    return lab

def fps(pts, m):
    c = pts.mean(0); idx = [int(((pts - c) ** 2).sum(1).argmax())]
    d = ((pts - pts[idx[0]]) ** 2).sum(1)
    for _ in range(m - 1):
        i = int(d.argmax()); idx.append(i); d = np.minimum(d, ((pts - pts[i]) ** 2).sum(1))
    return pts[idx]

for o in objs:
    bpy.context.view_layer.objects.active = o
    for x in objs: x.select_set(x == o)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
byname = {o.name: o for o in objs}
can = [o for o in objs if 'Icosphere' in o.data.name or 'Needle' in o.name][0]
brn = [o for o in objs if 'Branch' in o.name][0]
cC, cP, cCol = tri_data(can)
bC, bP, bCol = tri_data(brn)
needle = cCol.mean(0); bark = bCol.mean(0)
print("MEANS needle", needle.round(3), "branch", bark.round(3), "canopy tris", len(cC), "branch tris", len(bC))

def build(kc, SIDES, kb, trunk_sides, flare, ZW=2.5, RS=1.0, WS=1.0):
    V, F, VC = [], [], []
    def add_tris(verts, faces, cols):
        b = len(V); V.extend(verts); VC.extend(cols); F.extend([tuple(b + i for i in f) for f in faces])
    # canopy -> radial spikes (one tapered bipyramid per foliage cluster, root near trunk, tip at outer extent)
    X = cC * np.array([1.0, 1.0, ZW])
    lab = kmeans(X, kc)
    for j in range(kc):
        m = lab == j
        if m.sum() < 6: continue
        pts = cP[m].reshape(-1, 3); cen = pts.mean(0)
        rad = np.hypot(pts[:, 0], pts[:, 1])
        d = np.array([cen[0], cen[1], 0.0]); dl = np.linalg.norm(d)
        d = d / dl if dl > 1e-4 else np.array([1.0, 0, 0])
        tg = np.array([-d[1], d[0], 0.0])
        rmin, rmax = np.percentile(rad, 4), np.percentile(rad, 99)
        r0 = max(0.25, rmin * 0.8)
        far = pts[rad >= np.percentile(rad, 90)]
        ztip = far[:, 2].mean(); z0 = cen[2] + 0.15
        root = d * r0 + np.array([0, 0, z0]); tip = d * rmax * RS + np.array([0, 0, ztip])
        mid = root + (tip - root) * 0.42
        w = max(0.25, 1.7 * np.std(pts @ tg)) * WS; h = max(0.2, 1.6 * np.std(pts[:, 2])) * WS
        up = np.array([0, 0, 1.0])
        col = cCol[m].mean(0); hz = cen[2] / H
        if SIDES == 4:
            ring = [mid + tg * w, mid + up * h, mid - tg * w, mid - up * h * 0.8]
            lit = [1.0, 1.22, 1.0, 0.72]
        else:
            ring = [mid + up * h, mid + tg * w - up * h * 0.5, mid - tg * w - up * h * 0.5]
            lit = [1.22, 0.9, 0.9]
        vs = [tuple(root), tuple(tip)] + [tuple(p) for p in ring]
        k0 = 0.85 + 0.25 * hz
        cs = [tuple(np.clip(col * k0 * 0.75, 0, 1)), tuple(np.clip(col * k0 * 1.05, 0, 1))] + [tuple(np.clip(col * k0 * l, 0, 1)) for l in lit]
        n = len(ring); fs = []
        for i in range(n):
            a, b = 2 + i, 2 + (i + 1) % n
            fs += [(0, b, a), (1, a, b)]
        add_tris(vs, fs, cs)
    # branches -> prisms along PCA axis
    if kb:
        labb = kmeans(bC, kb)
        for j in range(kb):
            m = labb == j
            if m.sum() < 3: continue
            pts = bP[m].reshape(-1, 3); cen = pts.mean(0)
            u, sv, vt = np.linalg.svd(pts - cen, full_matrices=False); ax = vt[0]
            t = (pts - cen) @ ax; a = cen + ax * t.min(); b = cen + ax * t.max()
            r0, r1 = 0.16, 0.06
            ref = np.array([0, 0, 1.0]) if abs(ax[2]) < 0.9 else np.array([1.0, 0, 0])
            e1 = np.cross(ax, ref); e1 /= np.linalg.norm(e1); e2 = np.cross(ax, e1)
            ring = [math.tau * i / 3 for i in range(3)]
            vs = [tuple(a + r0 * (math.cos(q) * e1 + math.sin(q) * e2)) for q in ring] + [tuple(b + r1 * (math.cos(q) * e1 + math.sin(q) * e2)) for q in ring]
            fs = []
            for i in range(3):
                i2 = (i + 1) % 3; fs += [(i, i2, 3 + i2), (i, 3 + i2, 3 + i)]
            add_tris(vs, fs, [tuple(bCol[m].mean(0))] * 6)
    # trunk: rings (z, r)
    rings = ([(0.0, 1.05), (0.7, 0.70)] if flare else [(0.0, 0.80)]) + [(18.0, 0.18)]
    vs, fs, cs = [], [], []
    for (z, r) in rings:
        for i in range(trunk_sides):
            q = math.tau * i / trunk_sides; vs.append((r * math.cos(q), r * math.sin(q), z - (0.3 if z == 0 else 0)))
            cs.append(tuple(bark * (0.75 if z < 1 else 1.0)))
    n = trunk_sides
    for ri in range(len(rings) - 1):
        for i in range(n):
            i2 = (i + 1) % n; a, b, c, d = ri * n + i, ri * n + i2, (ri + 1) * n + i2, (ri + 1) * n + i
            fs += [(a, b, c), (a, c, d)]
    add_tris(vs, fs, cs)
    return V, F, VC

def s2l(c): c = np.asarray(c); return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
vmat = bpy.data.materials.new("vcol"); vmat.use_nodes = True
nt = vmat.node_tree; bs = nt.nodes["Principled BSDF"]; at = nt.nodes.new("ShaderNodeVertexColor"); at.layer_name = "Col"
nt.links.new(at.outputs["Color"], bs.inputs["Base Color"]); bs.inputs["Roughness"].default_value = 0.85

def make(tag, offset, **kw):
    V, F, VC = build(**kw)
    me = bpy.data.meshes.new(tag); me.from_pydata(V, [], F); me.update()
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:]); bm.to_mesh(me); bm.free(); me.update()
    ca = me.color_attributes.new("Col", 'FLOAT_COLOR', 'POINT')
    d = np.ones((len(V), 4), np.float32); d[:, :3] = s2l(np.array(VC)); ca.data.foreach_set("color", d.ravel())
    me.materials.append(vmat)
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new(tag, me); bpy.context.scene.collection.objects.link(ob)
    me.calc_loop_triangles()
    P, N, C = [], [], []
    for t in me.loop_triangles:
        for vi in t.vertices:
            v = me.vertices[vi]; p = v.co; n = v.normal
            P += [p.x / H, p.z / H, -p.y / H]; N += [n.x, n.z, -n.y]; C += list(VC[vi])
    json.dump({"tris": len(me.loop_triangles), "pos": [round(x, 5) for x in P], "nrm": [round(x, 4) for x in N], "col": [round(float(x), 4) for x in C]},
              open(outd + "/pine_" + tag + ".json", "w"))
    print("LOD", tag, len(me.loop_triangles))
    s = ob.copy(); s.data = me.copy(); bpy.context.scene.collection.objects.link(s); s.scale = (1 / H,) * 3
    bpy.ops.object.select_all(action='DESELECT'); s.select_set(True); bpy.context.view_layer.objects.active = s
    bpy.ops.export_scene.gltf(filepath=outd + "/pine_" + tag + ".glb", use_selection=True, export_format='GLB', export_apply=True)
    bpy.data.objects.remove(s)
    ob.location.x = offset
    return ob

make('lod0', 20.0, kc=int(argv[2]) if len(argv) > 2 else 150, SIDES=4, kb=0, trunk_sides=8, flare=True, WS=0.8)
make('lod1', 40.0, kc=int(argv[3]) if len(argv) > 3 else 24, SIDES=3, kb=0, trunk_sides=3, flare=False, WS=0.95)

sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 24; sc.cycles.use_denoising = True
sc.render.resolution_x = 1500; sc.render.resolution_y = 600
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.18, 0.26, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 0.6
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN')); sun.data.energy = 3.5
sun.rotation_euler = (math.radians(50), 0, math.radians(35)); sc.collection.objects.link(sun)
bpy.ops.mesh.primitive_plane_add(size=400, location=(20, 0, 0))
g = bpy.context.object; gm = bpy.data.materials.new("g"); gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.25, 0.22, 0.28, 1); g.data.materials.append(gm)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 62
def shot(name, loc, tgt):
    cam.location = loc; cam.rotation_euler = (mathutils.Vector(tgt) - mathutils.Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = outd + "/cmp2_" + name + ".png"; bpy.ops.render.render(write_still=True)
shot("side", (20, -80, 9), (20, 0, 9))
sc.render.resolution_y = 520
shot("top", (20, 0, 90), (20, 0.01, 0))
print("DONE")
