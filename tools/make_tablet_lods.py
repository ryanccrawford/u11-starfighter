#!/usr/bin/env python3
"""
Build tablet LODs for U11 Starfighter (Phase 2). Headless:

  blender -b --factory-startup -P tools/make_tablet_lods.py -- --root <shooter-root>

Writes player_starfighter_tablet.glb and moon_tablet.glb. Never overwrites the source GLBs.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def log(msg):
    print(msg, flush=True)


def _after_dashes(argv):
    if "--" in argv:
        return argv[argv.index("--") + 1 :]
    return argv[1:]


def parse_args(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--root", required=True)
    p.add_argument("--ship", action="store_true")
    p.add_argument("--moon", action="store_true")
    p.add_argument("--ship-target", type=int, default=8000)
    p.add_argument("--moon-target", type=int, default=10000)
    args = p.parse_args(argv)
    if not args.ship and not args.moon:
        args.ship = True
        args.moon = True
    return args


def mesh_objects():
    import bpy
    return [o for o in bpy.data.objects if o.type == "MESH"]


def count_tris(objs=None):
    objs = objs if objs is not None else mesh_objects()
    total = 0
    for obj in objs:
        obj.data.calc_loop_triangles()
        total += len(obj.data.loop_triangles)
    return total


def import_glb(path: Path):
    import bpy
    log(f"IMPORT {path}")
    bpy.ops.import_scene.gltf(filepath=str(path))
    log(f"IMPORTED meshes={len(mesh_objects())} tris={count_tris()}")


def strip_shape_keys_and_modifiers(obj):
    import bpy
    # Decimate cannot apply on a mesh that still has shape keys or earlier modifiers.
    if obj.data.shape_keys:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        try:
            bpy.ops.object.shape_key_remove(all=True)
        except Exception:
            try:
                obj.shape_key_clear()
            except Exception as e:
                log(f"WARN shape keys on {obj.name}: {e}")
        obj.select_set(False)
    for mod in list(obj.modifiers):
        obj.modifiers.remove(mod)


def prepare_meshes():
    """Clear parents (keep world transform) and apply TRS so join/export keep the game axis."""
    import bpy

    meshes = mesh_objects()
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    if meshes:
        bpy.context.view_layer.objects.active = meshes[0]
        try:
            bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
        except Exception as e:
            log(f"WARN parent_clear: {e}")
        try:
            bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        except Exception as e:
            log(f"WARN transform_apply: {e}")
    # Drop armatures / extras so they cannot hitch a ride into the export.
    for obj in list(bpy.data.objects):
        if obj.type != "MESH":
            bpy.data.objects.remove(obj, do_unlink=True)
        else:
            strip_shape_keys_and_modifiers(obj)
    # Drop empty / material-less crumbs (importer leftovers), so we stay at <=14 material meshes.
    kept = []
    for obj in list(mesh_objects()):
        obj.data.calc_loop_triangles()
        n = len(obj.data.loop_triangles)
        if n == 0 or (not obj.data.materials or not obj.data.materials[0]) and n < 16:
            log(f"DROP leftover {obj.name} tris={n}")
            bpy.data.objects.remove(obj, do_unlink=True)
        else:
            kept.append(obj)
    return kept


def material_key(obj):
    if obj.data.materials and obj.data.materials[0]:
        return obj.data.materials[0].name
    return ""


def join_by_material(objs):
    import bpy

    groups = {}
    for obj in objs:
        groups.setdefault(material_key(obj), []).append(obj)
    joined = []
    for key, members in groups.items():
        log(f"JOIN material={key!r} parts={len(members)}")
        if key == "":
            # Importer leftover with no material: drop (keeps us at <=14 material draws).
            for m in members:
                m.data.calc_loop_triangles()
                log(f"DROP unmaterialed {m.name} tris={len(m.data.loop_triangles)}")
                bpy.data.objects.remove(m, do_unlink=True)
            continue
        bpy.ops.object.select_all(action="DESELECT")
        for m in members:
            m.select_set(True)
        bpy.context.view_layer.objects.active = members[0]
        if len(members) > 1:
            bpy.ops.object.join()
        joined.append(bpy.context.view_layer.objects.active)
    return joined


def decimate_to_target(objs, target_tris, min_keep=80):
    import bpy

    total = count_tris(objs)
    log(f"DECIMATE start tris={total} target={target_tris}")
    if total <= target_tris:
        return total
    sizes = []
    for obj in objs:
        obj.data.calc_loop_triangles()
        sizes.append((obj, len(obj.data.loop_triangles)))
    reserved = sum(n for _, n in sizes if n <= min_keep)
    reducible = [(obj, n) for obj, n in sizes if n > min_keep]
    reducible_tris = sum(n for _, n in reducible)
    budget = max(target_tris - reserved, max(16, len(reducible)))
    for obj, n in reducible:
        want = max(min_keep, int(round(n * (budget / max(1, reducible_tris)))))
        ratio = min(1.0, max(0.02, want / max(1, n)))
        if ratio >= 0.995:
            continue
        log(f"  decimate {obj.name} {n} -> ~{want} ratio={ratio:.3f}")
        mod = obj.modifiers.new(name="TabletLOD", type="DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = ratio
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    total = count_tris(objs)
    log(f"DECIMATE done tris={total}")
    return total


def resize_images(max_width=1024):
    import bpy
    for img in bpy.data.images:
        try:
            w, h = img.size
        except Exception:
            continue
        if w <= max_width or w <= 0:
            continue
        new_w = max_width
        new_h = max(1, int(round(h * (max_width / float(w)))))
        log(f"RESIZE image {img.name} {w}x{h} -> {new_w}x{new_h}")
        img.scale(new_w, new_h)


def export_glb(path: Path):
    import bpy
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in mesh_objects():
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
    kwargs = dict(
        filepath=str(path),
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_animations=False,
        export_skins=False,
        export_cameras=False,
        export_lights=False,
    )
    for _ in range(8):
        try:
            bpy.ops.export_scene.gltf(**kwargs)
            log(f"EXPORTED {path} bytes={path.stat().st_size}")
            return
        except TypeError as e:
            dropped = False
            for key in list(kwargs):
                if key != "filepath" and key in str(e):
                    kwargs.pop(key)
                    dropped = True
                    log(f"WARN drop export kw {key}")
            if not dropped:
                raise


def bbox_of_meshes(objs):
    mins = [math.inf, math.inf, math.inf]
    maxs = [-math.inf, -math.inf, -math.inf]
    have = False
    for obj in objs:
        for v in obj.data.vertices:
            co = obj.matrix_world @ v.co
            for i in range(3):
                mins[i] = min(mins[i], float(co[i]))
                maxs[i] = max(maxs[i], float(co[i]))
            have = True
    if not have:
        return None
    return {
        "min": mins,
        "max": maxs,
        "size": [maxs[i] - mins[i] for i in range(3)],
        "center": [(maxs[i] + mins[i]) * 0.5 for i in range(3)],
    }


def report(label, extra=None):
    objs = mesh_objects()
    mats = []
    for o in objs:
        mats.append(o.data.materials[0].name if o.data.materials and o.data.materials[0] else "")
    data = {
        "label": label,
        "meshes": len(objs),
        "materials": mats,
        "tris": count_tris(objs),
        "bbox": bbox_of_meshes(objs),
    }
    if extra:
        data.update(extra)
    log("LOD_REPORT " + json.dumps(data))


def new_empty_scene():
    import bpy
    # Do not call read_factory_settings (it has been crashy under -b). Clear the current file instead.
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.objects, bpy.data.armatures, bpy.data.actions, bpy.data.materials, bpy.data.images, bpy.data.collections):
        for item in list(block):
            try:
                block.remove(item)
            except Exception:
                pass
    if not bpy.data.collections:
        col = bpy.data.collections.new("Collection")
        bpy.context.scene.collection.children.link(col)
    log("SCENE cleared")


def build_ship(root: Path, target: int):
    src = root / "assets" / "models" / "player_starfighter.glb"
    dst = root / "assets" / "models" / "player_starfighter_tablet.glb"
    new_empty_scene()
    import_glb(src)
    objs = prepare_meshes()
    log(f"SHIP prepared meshes={len(objs)} tris={count_tris(objs)}")
    joined = join_by_material(objs)
    log(f"SHIP joined meshes={len(joined)} tris={count_tris(joined)}")
    decimate_to_target(joined, target)
    export_glb(dst)
    report("player_starfighter_tablet", {"dst": str(dst), "target": target})


def build_moon(root: Path, target: int):
    src = root / "assets" / "moon" / "moon.glb"
    dst = root / "assets" / "moon" / "moon_tablet.glb"
    new_empty_scene()
    import_glb(src)
    objs = prepare_meshes()
    log(f"MOON prepared meshes={len(objs)} tris={count_tris(objs)}")
    joined = join_by_material(objs)
    decimate_to_target(joined, target, min_keep=200)
    resize_images(max_width=1024)
    export_glb(dst)
    report("moon_tablet", {"dst": str(dst), "target": target})


def main():
    args = parse_args(_after_dashes(sys.argv))
    root = Path(args.root).resolve()
    log(f"ROOT={root}")
    if args.ship:
        build_ship(root, args.ship_target)
    if args.moon:
        build_moon(root, args.moon_target)
    log("ALL_DONE")


if __name__ == "__main__":
    main()
