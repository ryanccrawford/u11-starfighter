"""Fresh-import validation for the production power-up GLBs.

Run with Blender 5.0:
  blender --background --python tools/validate_powerup_models.py
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets" / "models" / "pickups"
REPORT = ASSET_DIR / "validation_report.md"
VARIANTS = ("weapon", "missiles", "shield", "repair", "rapid")


def clear_import() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for blocks in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.actions):
        for block in list(blocks):
            blocks.remove(block)


def glb_json(path: Path) -> dict:
    raw = path.read_bytes()
    if raw[:4] != b"glTF" or struct.unpack_from("<I", raw, 4)[0] != 2:
        raise AssertionError("not a GLB 2.0 file")
    json_len, json_type = struct.unpack_from("<II", raw, 12)
    if json_type != 0x4E4F534A:
        raise AssertionError("GLB JSON chunk is missing")
    return json.loads(raw[20:20 + json_len].decode("utf-8").rstrip(" \x00"))


def glb_float_accessor(path: Path, data: dict, accessor_index: int) -> np.ndarray:
    raw = path.read_bytes()
    json_len = struct.unpack_from("<I", raw, 12)[0]
    bin_header = 20 + json_len
    bin_len, bin_type = struct.unpack_from("<II", raw, bin_header)
    assert bin_type == 0x004E4942, "GLB binary chunk is missing"
    binary = raw[bin_header + 8:bin_header + 8 + bin_len]
    accessor = data["accessors"][accessor_index]
    assert accessor["componentType"] == 5126, "animation accessor is not float32"
    widths = {"SCALAR": 1, "VEC4": 4}
    width = widths[accessor["type"]]
    view = data["bufferViews"][accessor["bufferView"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", width * 4)
    values = np.empty((accessor["count"], width), dtype=np.float64)
    for i in range(accessor["count"]):
        values[i] = struct.unpack_from("<" + "f" * width, binary, offset + i * stride)
    return values


def quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array((aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz))


def rotation_steps(values: np.ndarray) -> tuple[np.ndarray, float]:
    q = values / np.linalg.norm(values, axis=1)[:, None]
    for i in range(1, len(q)):
        if np.dot(q[i - 1], q[i]) < 0:
            q[i] *= -1
    steps = []
    for previous, current in zip(q[:-1], q[1:]):
        delta = quat_mul(np.array((-previous[0], -previous[1], -previous[2], previous[3])), current)
        if delta[3] < 0:
            delta *= -1
        vector = delta[:3]
        magnitude = np.linalg.norm(vector)
        angle = 2.0 * math.atan2(magnitude, max(1e-12, delta[3]))
        assert magnitude > 1e-6 and angle > 1e-5, "animation contains a stationary frame"
        steps.append(vector / magnitude * angle)
    return np.array(steps), abs(float(np.dot(q[0], q[-1])))


def bounds(objects):
    points = []
    for obj in objects:
        if obj.type != "MESH":
            continue
        points.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    lo = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    hi = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    return hi - lo


def validate(tag: str, reference_xy: tuple[float, float] | None):
    path = ASSET_DIR / f"pickup_{tag}.glb"
    assert path.exists() and path.stat().st_size > 0, "file missing or empty"
    data = glb_json(path)
    animations = data.get("animations", [])
    assert len(animations) == 1 and animations[0].get("name") == "Pickup_Spin", "expected one Pickup_Spin animation"
    anim = animations[0]
    targets = {(data["nodes"][ch["target"]["node"]]["name"], ch["target"]["path"]) for ch in anim["channels"]}
    assert {("OuterAtomShell", "rotation"), ("InnerCore", "rotation")} <= targets, f"rotation targets missing: {targets}"
    assert all(name in {"OuterAtomShell", "InnerCore"} and path in {"translation", "rotation", "scale"} for name, path in targets), f"bad animation targets: {targets}"
    skins = data.get("skins", [])
    assert len(skins) == 1 and len(skins[0]["joints"]) == 2, "expected one two-joint skin"
    joint_names = {data["nodes"][index]["name"] for index in skins[0]["joints"]}
    assert joint_names == {"OuterAtomShell", "InnerCore"}, f"bad skin joints: {joint_names}"
    for mesh in data.get("meshes", []):
        for primitive in mesh["primitives"]:
            assert "JOINTS_0" in primitive["attributes"] and "WEIGHTS_0" in primitive["attributes"], "primitive is not skinned"
    durations = []
    rotation_samplers = {channel["sampler"] for channel in anim["channels"] if channel["target"]["path"] == "rotation"}
    for index, sampler in enumerate(anim["samplers"]):
        if index in rotation_samplers:
            assert sampler.get("interpolation", "LINEAR") == "LINEAR", "rotation animation is not linear"
        else:
            assert sampler.get("interpolation", "STEP") in {"STEP", "LINEAR"}, "constant transform interpolation is invalid"
        accessor = data["accessors"][sampler["input"]]
        durations.append(accessor["max"][0] - accessor["min"][0])
    assert all(abs(d - 2.0) < 0.0001 for d in durations), f"bad animation duration: {durations}"

    rotation_channels = {}
    for channel in anim["channels"]:
        if channel["target"]["path"] != "rotation":
            continue
        name = data["nodes"][channel["target"]["node"]]["name"]
        sampler = anim["samplers"][channel["sampler"]]
        times = glb_float_accessor(path, data, sampler["input"]).ravel()
        values = glb_float_accessor(path, data, sampler["output"])
        assert len(times) >= 61 and np.all(np.diff(times) > 0), f"{name} is not densely sampled"
        rotation_channels[name] = rotation_steps(values)
    outer_steps, outer_seam = rotation_channels["OuterAtomShell"]
    inner_steps, inner_seam = rotation_channels["InnerCore"]
    assert outer_seam > 0.9999 and inner_seam > 0.9999, "spin loop endpoints are not seamless"
    outer_total = float(np.linalg.norm(outer_steps, axis=1).sum())
    inner_total = float(np.linalg.norm(inner_steps, axis=1).sum())
    assert 6.0 < outer_total < 6.6, f"outer shell is not one continuous turn: {outer_total}"
    assert 12.2 < inner_total < 12.9, f"inner core is not two continuous turns: {inner_total}"
    outer_axis = outer_steps.mean(axis=0); inner_axis = inner_steps.mean(axis=0)
    assert np.dot(outer_axis, inner_axis) < -0.99 * np.linalg.norm(outer_axis) * np.linalg.norm(inner_axis), "inner and outer do not rotate in opposite directions"
    assert np.all(outer_steps @ outer_axis > 0) and np.all(inner_steps @ inner_axis > 0), "rotation reverses during the loop"

    clear_import()
    bpy.ops.import_scene.gltf(filepath=str(path))
    objects = list(bpy.context.scene.objects)
    rigs = [obj for obj in objects if obj.type == "ARMATURE"]
    assert len(rigs) == 1, "expected one imported armature"
    rig = rigs[0]
    assert {bone.name for bone in rig.data.bones} == {"OuterAtomShell", "InnerCore"}, "imported armature bones differ"
    assert rig.location.length < 0.0001, "armature pivot is offset"
    assert all(abs(s - 1.0) < 0.0001 for s in rig.scale), "armature scale is not applied"
    assert not any(o.type in {"CAMERA", "LIGHT"} for o in objects), "production GLB contains camera/light"

    meshes = [o for o in objects if o.type == "MESH"]
    triangles = 0
    material_names = set()
    for obj in meshes:
        obj.data.calc_loop_triangles()
        triangles += len(obj.data.loop_triangles)
        material_names.update(slot.material.name for slot in obj.material_slots if slot.material)
        assert obj.data.uv_layers, f"{obj.name} has no UV map"
        uv = obj.data.uv_layers.active.data
        assert uv and all(-0.0001 <= loop.uv.x <= 1.0001 and -0.0001 <= loop.uv.y <= 1.0001 for loop in uv), f"{obj.name} UV outside 0..1"
        local_dims = Vector((obj.dimensions.x, obj.dimensions.y, obj.dimensions.z))
        assert min(local_dims) > 0.015, f"{obj.name} is planar: {tuple(local_dims)}"
    assert 3500 <= triangles <= 6500, f"triangle count {triangles} outside 3.5k..6.5k"
    expected_materials = {"Pickup_Chassis", f"Pickup_Insert_{tag.upper()}"}
    assert material_names == expected_materials, f"materials {material_names} != {expected_materials}"

    images = {img.name: tuple(img.size) for img in bpy.data.images if tuple(img.size) != (0, 0)}
    assert images.get("chassis_albedo_1024") == (1024, 1024), "chassis texture missing/wrong size"
    assert images.get(f"{tag}_albedo_1024") == (1024, 1024), "insert texture missing/wrong size"
    dims = bounds(meshes)
    assert min(dims) > 2.8, f"pickup is not volumetric on all axes: {tuple(dims)}"
    if reference_xy is not None:
        assert all(abs(dims[i] - reference_xy[i]) < 0.01 for i in range(3)), "chassis bounds differ between variants"
    return triangles, dims, len(meshes), path.stat().st_size, reference_xy or (dims.x, dims.y, dims.z)


def main() -> None:
    rows = []
    reference = None
    failures = []
    for tag in VARIANTS:
        try:
            tri, dims, mesh_count, size, reference = validate(tag, reference)
            rows.append((tag, "PASS", tri, mesh_count, size, f"{dims.x:.3f} × {dims.y:.3f} × {dims.z:.3f}"))
        except Exception as exc:
            failures.append(f"{tag}: {exc}")
            rows.append((tag, "FAIL", "—", "—", (ASSET_DIR / f"pickup_{tag}.glb").stat().st_size if (ASSET_DIR / f"pickup_{tag}.glb").exists() else 0, "—"))
    lines = [
        "# Power-up GLB validation", "",
        "Generated by Blender 5.0 fresh-import validation.", "",
        "| Variant | Result | Triangles | Meshes | GLB bytes | Imported bounds |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    lines.extend(f"| {tag} | {result} | {tri} | {meshes} | {size} | {dims} |" for tag, result, tri, meshes, size, dims in rows)
    lines.extend(["", "Checks: GLB 2.0 structure, fully volumetric geometry, one two-joint skin with weights on every primitive, one `Pickup_Spin` clip targeting both bones, seamless 2.0-second linear timing, one continuous outer turn and two opposite inner turns with no reversal, armature pivot/applied scale, 3.5k–6.5k triangles, exact materials, embedded 1024 textures, UV range, matching bounds, and no cameras/lights.", ""])
    if failures:
        lines.extend(["## Failures", ""] + [f"- {failure}" for failure in failures])
    else:
        lines.append("All production GLBs passed.")
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
