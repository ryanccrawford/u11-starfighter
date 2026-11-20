"""The five alien-robot enemy designs (Blender design space: nose -Y, up +Z, length ~2)."""
import math
from mathutils import Matrix, Euler, Vector
from enemy_lib import robot_pilot, brain_dome, engine

TAU = 2 * math.pi
def S(x): return math.sin(TAU * x)
def recoil(t, hit=0.04, back=0.28):
    if t < hit: return t / hit
    return max(0.0, 1 - (t - hit) / back) ** 2
def env(t, dur, a=0.12, r=0.15):  # 0->1 open, hold, ->0 close
    if t < a: u = t / a; return u * u * (3 - 2 * u)
    if t > dur - r: u = max(0.0, (dur - t) / r); return u * u * (3 - 2 * u)
    return 1.0


# =============================================================== 1. SCARAB scout drone
def scarab(k):
    k.loft('body', [(-0.78, 0.05, 0.05, 0.0, 2), (-0.72, 0.15, 0.12, 0.0, 2.2), (-0.55, 0.25, 0.18, 0.01, 2.5),
                    (-0.25, 0.32, 0.22, 0.02, 2.8), (0.10, 0.34, 0.22, 0.02, 2.8), (0.40, 0.27, 0.18, 0.02, 2.6),
                    (0.62, 0.17, 0.12, 0.02, 2.3), (0.72, 0.10, 0.08, 0.02, 2.0)], 'hull')
    el = k.loft('elytraL', [(-0.54, 0.03, 0.02, 0.17, 2, 0.06), (-0.45, 0.13, 0.05, 0.20, 2.2, 0.15), (-0.1, 0.17, 0.075, 0.215, 2.4, 0.165),
                       (0.25, 0.165, 0.075, 0.21, 2.4, 0.16), (0.50, 0.10, 0.05, 0.17, 2.2, 0.12), (0.58, 0.03, 0.02, 0.14, 2, 0.08)], 'armor', 'Hull')
    k.mirror(el, 'elytraR', 'Hull')
    for i, (y, hw, hh) in enumerate([(0.36, 0.285, 0.19), (0.47, 0.235, 0.165), (0.58, 0.18, 0.13)]):
        k.torus(f'abdomen_ring{i}', 1.0, 0.05, (0, y, 0.02), 'dark', rot=(math.pi / 2, 0, 0), scale=(hw, hh, 0.5))
    k.box('spine', (0.05, 0.95, 0.05), (0, -0.02, 0.28), 'dark')
    # the eye: a robotic sensor in a heavy socket
    k.torus('eye_socket', 0.105, 0.03, (0, -0.74, 0.0), 'hull', rot=(math.pi / 2, 0, 0))
    k.sphere('eye_ball', 0.088, (0, -0.75, 0.0), 'glow_red', 'Eye')
    k.torus('eye_iris', 0.055, 0.014, (0, -0.835, 0.0), 'dark', 'Eye', rot=(math.pi / 2, 0, 0))
    k.box('eye_brow', (0.26, 0.10, 0.04), (0, -0.70, 0.12), 'armor', rot=(-0.35, 0, 0))
    # mandible claws
    m = k.plate('mandL', [(0.12, -0.60), (0.30, -0.64), (0.27, -0.90), (0.20, -0.98), (0.19, -0.82), (0.12, -0.70)], 0.05, 'armor', 'MandL', z=0.0)
    k.mirror(m, 'mandR', 'MandR')
    # fins (upper + lower pair on each side, one bone per side)
    fins = [k.plate('finL_up', [(0.26, -0.18), (0.80, 0.14), (0.84, 0.25), (0.76, 0.30), (0.26, 0.22)], 0.03, 'armor', 'FinL', z=0.10, rot=(0, -0.18, 0), pivot=(0.28, 0, 0.08)),
            k.plate('finL_lo', [(0.26, 0.05), (0.66, 0.40), (0.63, 0.50), (0.26, 0.34)], 0.026, 'hull', 'FinL', z=-0.05, rot=(0, 0.28, 0), pivot=(0.28, 0, -0.06)),
            k.box('finL_rib', (0.56, 0.028, 0.03), (0.55, 0.035, 0.155), 'dark', 'FinL', rot=(0, -0.18, 0.52)),
            k.box('finL_rib2', (0.44, 0.022, 0.026), (0.50, 0.20, 0.14), 'steel', 'FinL', rot=(0, -0.18, 0.20)),
            k.box('finL_glow', (0.40, 0.012, 0.012), (0.56, -0.02, 0.165), 'glow_green', 'FinL', rot=(0, -0.18, 0.52), bevel=False),
            k.sphere('finL_tip', 0.026, (0.82, 0.25, 0.21), 'glow_green', 'FinL'),
            k.cyl('finL_hinge', 0.045, 0.30, (0.28, 0.02, 0.05), 'dark', 'FinL', rot=(math.pi / 2, 0, 0)),
            k.cyl('finL_hcap', 0.05, 0.04, (0.28, -0.14, 0.05), 'steel', 'FinL', rot=(math.pi / 2, 0, 0))]
    for o in fins: k.mirror(o, o.name.replace('L_', 'R_'), 'FinR')
    # alien controller + pilot sensor cluster
    brain_dome(k, 0.15, 0.25, 0.13)
    for sx in (-1, 1):
        k.box(f'side_pod{sx}', (0.08, 0.36, 0.10), (sx * 0.33, -0.12, -0.02), 'dark', taper=(0.6, 0.7))
        k.cyl(f'side_light{sx}', 0.02, 0.02, (sx * 0.375, -0.30, -0.02), 'glow_green', rot=(0, math.pi / 2, 0), bevel=False)
    eng = engine(k, 'eng', 0, 0.60, 0.02, 0.11, 0.24)
    # under-nose needle gun
    k.box('gun_mount', (0.10, 0.22, 0.07), (0, -0.48, -0.19), 'dark', taper=(0.8, 0.8))
    k.cyl('gun_barrel', 0.022, 0.34, (0, -0.68, -0.20), 'steel', 'W_Gun', rot=(math.pi / 2, 0, 0))
    k.cyl('gun_brake', 0.034, 0.06, (0, -0.84, -0.20), 'dark', 'W_Gun', rot=(math.pi / 2, 0, 0))
    k.cyl('gun_coil', 0.03, 0.05, (0, -0.62, -0.20), 'glow_green', 'W_Gun', rot=(math.pi / 2, 0, 0), bevel=False)
    # greebles
    for i, (x, y) in enumerate([(0.12, -0.35), (-0.12, -0.35), (0.18, 0.35), (-0.18, 0.35), (0.0, 0.48), (0.22, 0.0), (-0.22, 0.0)]):
        k.greeble(f'g{i}', x, y, (0.07, 0.05, 0.03), 'dark' if i % 2 else 'steel')
    k.vents('vent', 0.08, 0.36, 0.56, 4, 0.05)
    k.vents('ventb', -0.08, 0.36, 0.56, 4, 0.05)
    for i, (x, y) in enumerate([(0.15, -0.2), (-0.15, -0.2), (0.0, 0.2), (0.2, 0.3), (-0.2, 0.3)]):
        k.greeble(f'b{i}', x, y, (0.09, 0.07, 0.03), 'dark', from_below=True)
    k.cyl('antenna', 0.006, 0.22, (0.10, 0.40, 0.33), 'steel', 'Antenna', segs=6)
    k.sphere('antenna_tip', 0.014, (0.10, 0.40, 0.44), 'glow_red', 'Antenna')

    def idle(t, P):
        # Fins are rigid stabiliser plates: NO flapping (Ryan, Nov 11: wings don't beat in a vacuum).
        # They stay at rest; only sensors, mandibles and the antenna move in Idle.
        return {'Eye': P((0, -0.74, 0), rot=(0.12 * S(t / 2.0 * 2), 0, 0.4 * S(t / 2.0))),
                'MandL': P((0.15, -0.62, 0), rot=(0, 0, 0.14 * max(0, S(t / 2.0 * 3)))),
                'MandR': P((-0.15, -0.62, 0), rot=(0, 0, -0.14 * max(0, S(t / 2.0 * 3)))),
                'Antenna': P((0.10, 0.40, 0.22), rot=(0.18 * S(t / 2.0), 0.12 * S(t / 2.0 * 2 + 0.25), 0))}
    def fire(t, P):
        return {'W_Gun': P((0, -0.62, -0.2), loc=(0, 0.075 * recoil(t), 0))}
    return dict(
        bones={'Hull': ((0, 0, 0), None), 'Eye': ((0, -0.74, 0), 'Hull'), 'MandL': ((0.15, -0.62, 0), 'Hull'), 'MandR': ((-0.15, -0.62, 0), 'Hull'),
               'FinL': ((0.28, 0, 0.08), 'Hull'), 'FinR': ((-0.28, 0, 0.08), 'Hull'), 'Antenna': ((0.10, 0.40, 0.22), 'Hull'),
               'W_Gun': ((0, -0.62, -0.2), 'Hull')},
        nozzles=[(0.04, eng[1], eng[2]), (-0.04, eng[1], eng[2])], muzzle=(0, -0.88, -0.20),
        anims={'Idle': (2.0, idle), 'Fire': (0.35, fire)})


ENEMIES = {
    'scarab': dict(name='Scarab Scout Drone', build=scarab),
}
