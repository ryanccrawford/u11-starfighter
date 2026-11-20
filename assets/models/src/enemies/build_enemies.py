import bpy, sys, math, os
sys.path.insert(0, '/workspace/assetwork')
import importlib, shiplib; importlib.reload(shiplib)
from shiplib import *
from mathutils import Matrix
OUT = '/workspace/assetwork/enemies'
which = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []

def pal(hull, accent, dark=(0.13, 0.14, 0.17), glass=(0.08, 0.28, 0.42), glow=(1.0, 0.72, 0.3), metal=(0.48, 0.5, 0.54)):
    return [('hull', hull, 'hull'), ('dark', dark, 'dark'), ('accent', accent, 'hull'), ('glass', glass, 'glass'), ('glow', glow, 'glow'), ('metal', metal, 'hull')]

def grunt():
    s = Ship()
    s.loft([(-1.0, 0, 0, 0), (-0.82, .08, .07, 0), (-0.5, .16, .14, .02), (-0.1, .2, .17, .03), (0.4, .2, .15, .02), (0.8, .17, .13, 0), (0.88, .15, .12, 0)], HULL, n=12)
    s.sphere((0, -0.36, 0.14), (.1, .27, .1), GLASS)
    s.prism([(0.15, -0.2, .08), (0.8, 0.22, .035), (0.8, 0.45, .035), (0.15, 0.58, .08)], HULL, off=(0, 0, -0.03), mirror_x=True)
    s.prism([(0.42, 0.02, .1), (0.52, 0.08, .1), (0.52, 0.5, .1), (0.42, 0.52, .1)], ACCENT, off=(0, 0, -0.03), mirror_x=True)  # wing stripe
    s.frustum((0.82, -0.12, -0.03), (0.82, 0.5, -0.03), 0.0, 0.045, METAL, n=8, mirror_x=True)  # wingtip cannons
    s.frustum((0.82, -0.12, -0.03), (0.82, -0.3, -0.03), 0.022, 0.022, DARK, n=6, mirror_x=True)
    s.prism([(0.42, 0.1, .045), (0.86, 0.1, .045), (0.96, 0.5, .03), (0.8, 0.5, .03)], ACCENT, plane='YZ')
    s.box((0, 0.1, -0.15), (.18, .5, .06), DARK)  # belly intake
    s.engine(0, 0.82, 1.0, 0.13, n=12)
    s.nozzles = [(-0.04, 1.0, 0), (0.04, 1.0, 0)]
    return s, pal((0.8, 0.76, 0.64), (0.95, 0.55, 0.12)), 1.0, 4000, 35

def diver():
    s = Ship()
    s.loft([(-1.0, 0, 0, 0), (-0.72, .05, .04, 0), (-0.3, .11, .08, .01), (0.2, .15, .1, .01), (0.65, .17, .1, 0), (0.85, .15, .09, 0)], HULL, n=12)
    s.sphere((0, -0.12, 0.08), (.07, .24, .07), GLASS)
    s.prism([(0.1, -0.12, .06), (0.78, 0.72, .02), (0.78, 0.84, .02), (0.12, 0.86, .06)], HULL, off=(0, 0, -0.02), mirror_x=True)
    s.prism([(0.55, 0.42, .03), (0.8, 0.74, .03), (0.8, 0.84, .03), (0.55, 0.62, .03)], ACCENT, off=(0, 0, -0.005), mirror_x=True)
    s.prism([(0.06, -0.66, .025), (0.27, -0.5, .02), (0.27, -0.44, .02), (0.06, -0.47, .025)], ACCENT, mirror_x=True)
    rot = Matrix.Rotation(math.radians(-22), 3, 'Y')
    s.prism([(0.45, 0.0, .03), (0.85, 0.0, .03), (0.95, 0.32, .02), (0.78, 0.32, .02)], ACCENT, plane='YZ', off=(0.16, 0, 0.06), mirror_x=True, rot=rot)
    s.engine(0.12, 0.55, 1.0, 0.075, n=10, mirror_x=True)
    s.box((0, 0.2, -0.1), (.16, .6, .05), DARK)
    return s, pal((0.88, 0.88, 0.9), (0.95, 0.18, 0.2)), 0.97, 4000, 35

def looper():
    s = Ship()
    s.loft([(-0.78, 0, 0, 0), (-0.64, .12, .11, 0), (-0.32, .23, .19, .01), (0.1, .23, .18, .01), (0.36, .16, .13, 0), (0.46, .09, .08, 0)], HULL, n=14)
    s.sphere((0, -0.36, 0.15), (.13, .21, .11), GLASS)
    s.loft([(-0.62, 0, 0, 0, 0.56), (-0.48, .08, .08, 0, 0.56), (0.0, .1, .1, 0, 0.56), (0.62, .085, .085, 0, 0.56), (0.7, .08, .08, 0, 0.56)], HULL, n=10, mirror_x=True)
    s.prism([(0.0, -0.25, .07), (0.6, -0.18, .05), (0.6, 0.16, .05), (0.0, 0.26, .07)], HULL, mirror_x=True)
    s.prism([(-0.56, 0.5, .035), (0.56, 0.5, .035), (0.56, 0.68, .035), (-0.56, 0.68, .035)], ACCENT, off=(0, 0, 0.13))
    s.prism([(0.38, 0.05, .035), (0.74, 0.05, .035), (0.8, 0.34, .03), (0.64, 0.34, .03)], ACCENT, plane='YZ', off=(0.56, 0, 0), mirror_x=True)
    s.box((0.56, -0.2, 0.0), (0.21, 0.08, 0.21), ACCENT, mirror_x=True)  # boom bands
    s.engine(0.56, 0.68, 0.86, 0.08, n=10, mirror_x=True)
    s.engine(0, 0.44, 0.56, 0.06, n=8)
    s.nozzles = s.nozzles[:2]
    return s, pal((0.84, 0.86, 0.88), (0.15, 0.72, 0.95)), 0.93, 4000, 35

def weaver():
    s = Ship()
    s.loft([(-1.0, 0, 0, 0), (-0.6, .1, .08, 0), (-0.1, .19, .14, .01), (0.5, .17, .12, 0), (0.86, .12, .09, 0)], HULL, n=4, p=2.0, off=0.0)
    s.sphere((0, -0.3, 0.09), (.08, .22, .08), GLASS, us=6, vs=4)
    s.prism([(0.12, 0.38, .06), (0.84, -0.18, .02), (0.9, -0.04, .02), (0.16, 0.66, .06)], HULL, off=(0, 0, -0.01), mirror_x=True)
    s.prism([(0.5, 0.1, .03), (0.84, -0.17, .03), (0.88, -0.07, .03), (0.52, 0.22, .03)], ACCENT, off=(0, 0, 0.012), mirror_x=True)
    s.prism([(-0.28, 0.0, .03), (0.12, 0.0, .03), (0.06, 0.26, .02)], ACCENT, plane='YZ', off=(0.88, 0, 0), mirror_x=True)
    s.prism([(-0.12, 0.0, .03), (0.12, 0.0, .03), (0.1, -0.16, .02)], DARK, plane='YZ', off=(0.88, 0, 0), mirror_x=True)
    s.prism([(0.08, 0.58, .03), (0.44, 0.94, .02), (0.44, 1.02, .02), (0.08, 0.9, .03)], DARK, mirror_x=True)
    s.prism([(0.45, 0.08, .03), (0.85, 0.08, .03), (0.9, 0.32, .02), (0.75, 0.32, .02)], ACCENT, plane='YZ')
    s.engine(0.075, 0.74, 1.0, 0.065, n=6, mirror_x=True)
    return s, pal((0.62, 0.6, 0.7), (0.62, 0.3, 0.95)), 1.0, 4000, 20

def gunship():
    s = Ship()
    s.loft([(-1.0, .22, .1, -0.02), (-0.82, .36, .18, 0), (-0.3, .46, .24, 0), (0.4, .46, .24, 0), (0.85, .38, .2, 0), (0.95, .34, .17, 0)], HULL, n=16, p=4.0)
    s.box((0, -0.42, 0.26), (.32, .36, .12), HULL)
    s.box((0, -0.6, 0.27), (.24, .04, .07), GLASS)
    s.prism([(0.4, -0.2, .11), (1.0, -0.06, .08), (1.0, 0.45, .08), (0.4, 0.56, .11)], DARK, off=(0, 0, -0.04), mirror_x=True)
    s.frustum((1.0, -0.38, -0.04), (1.0, 0.72, -0.04), 0.13, 0.15, METAL, n=12, cap0_mat=DARK, mirror_x=True)
    s.box((1.0, -0.1, -0.04), (0.33, 0.1, 0.33), ACCENT, mirror_x=True)
    s.engine(1.0, 0.7, 0.92, 0.15, n=12, mirror_x=True, z=-0.04)
    for x in (0.55, 0.76):
        s.frustum((x, -0.2, -0.13), (x, -0.86, -0.13), 0.04, 0.032, DARK, n=8, mirror_x=True)
        s.box((x, -0.25, -0.13), (0.1, 0.14, 0.09), METAL, mirror_x=True)
    s.sphere((0, 0.22, 0.25), (.17, .17, .09), METAL, us=14, vs=6)
    s.frustum((0.05, 0.12, 0.29), (0.05, -0.3, 0.29), 0.025, 0.025, DARK, n=6, mirror_x=True)
    s.box((0.465, 0.1, 0.06), (0.02, 0.7, 0.07), ACCENT, mirror_x=True)
    s.engine(0, 0.9, 1.0, 0.14, n=12)
    s.nozzles = s.nozzles[:2]
    return s, pal((0.74, 0.68, 0.57), (0.95, 0.58, 0.1)), 1.27, 5000, 35

def invader():
    s = Ship()
    s.sphere((0, 0.05, 0), (.5, .45, .2), HULL, us=20, vs=10)
    s.sphere((0, 0.08, 0.1), (.27, .27, .16), GLASS, us=16, vs=8)
    s.sphere((0.16, -0.36, 0.06), (.06, .06, .05), GLOW, us=8, vs=6, mirror_x=True)
    s.frustum((0.28, -0.22, 0), (0.55, -0.6, 0), 0.085, 0.065, DARK, n=8, mirror_x=True)
    s.frustum((0.55, -0.6, 0), (0.4, -1.0, 0), 0.07, 0.0, ACCENT, n=8, mirror_x=True, squash=0.6)
    s.frustum((0.55, -0.6, 0), (0.7, -0.92, 0), 0.055, 0.0, ACCENT, n=8, mirror_x=True, squash=0.6)
    s.prism([(0.42, 0.0, .05), (0.86, 0.14, .03), (0.88, 0.25, .03), (0.44, 0.2, .05)], ACCENT, mirror_x=True)
    s.prism([(0.36, 0.3, .05), (0.76, 0.6, .03), (0.72, 0.7, .03), (0.3, 0.46, .05)], DARK, mirror_x=True)
    s.box((0, 0.42, -0.02), (0.36, 0.18, 0.12), DARK)
    s.engine(0.15, 0.45, 0.62, 0.07, n=8, mirror_x=True)
    return s, pal((0.66, 0.74, 0.62), (0.3, 0.95, 0.38), glass=(0.12, 0.42, 0.2)), 0.64, 4200, 35

def warden():
    s = Ship()
    s.loft([(-1.0, .05, .03, 0), (-0.8, .18, .08, 0), (-0.4, .38, .14, 0), (0.2, .5, .18, 0), (0.7, .5, .18, 0), (0.95, .42, .15, 0)], HULL, n=16, p=3.0)
    s.loft([(-1.16, 0, 0, 0, 0.32), (-0.96, .05, .04, 0, 0.32), (-0.4, .09, .07, 0, 0.32), (0.1, .09, .07, 0, 0.32)], DARK, n=8, mirror_x=True)
    s.frustum((0.32, -1.0, 0.0), (0.32, -1.18, 0.0), 0.02, 0.0, ACCENT, n=6, mirror_x=True)
    s.box((0, 0.25, 0.2), (.42, .62, .12), HULL)
    s.box((0, 0.42, 0.3), (.26, .34, .1), METAL)
    s.box((0, 0.28, 0.33), (.2, .04, .05), GLASS)
    s.prism([(0.38, 0.0, .06), (0.0, 0.0, .06)], HULL) if False else None
    s.box((0.62, 0.3, 0.0), (.22, .82, .18), METAL, mirror_x=True)
    for y in (0.0, 0.42):
        s.sphere((0.62, y, 0.1), (.09, .09, .07), DARK, us=10, vs=6, mirror_x=True)
        s.frustum((0.62, y, 0.13), (0.62, y - 0.32, 0.13), 0.022, 0.02, DARK, n=6, mirror_x=True)
    s.prism([(0.45, -0.2, .08), (1.08, 0.36, .04), (1.08, 0.78, .04), (0.45, 0.72, .08)], DARK, off=(0, 0, -0.04), mirror_x=True)
    s.prism([(-0.05, 0.0, .05), (0.38, 0.0, .05), (0.34, 0.28, .04), (0.12, 0.28, .04)], ACCENT, plane='YZ', off=(1.08, 0.4, -0.04), mirror_x=True)
    s.box((0.25, -0.1, 0.13), (.05, 1.0, .03), ACCENT, mirror_x=True)
    s.sphere((0, -0.6, 0.08), (.07, .07, .05), GLOW, us=10, vs=6)
    s.engine(0.18, 0.85, 1.08, 0.1, n=10, mirror_x=True)
    s.engine(0.42, 0.85, 1.05, 0.09, n=10, mirror_x=True)
    s.nozzles = s.nozzles[:2]
    return s, pal((0.44, 0.44, 0.5), (0.9, 0.12, 0.12)), 2.0, 7500, 35

D = dict(grunt=grunt, diver=diver, looper=looper, weaver=weaver, gunship=gunship, invader=invader, warden=warden)
for n in (which or list(D)):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s, palette, sc, tgt, sm = D[n]()
    finish(s, 'enemy_' + n, palette, OUT, scale=sc, target_tris=tgt, smooth_deg=sm)
