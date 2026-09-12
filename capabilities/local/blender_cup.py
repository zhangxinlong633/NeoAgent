#!/usr/bin/env python3
# blender_cup.py — 后台生成一只简单杯子并渲染静帧。
# blender --background --factory-startup --python this.py -- --out DIR

import math
import os
import sys


def _parse_out_dir(argv):
    out = ".neo/blender_cup"
    i = 0
    while i < len(argv):
        if argv[i] == "--out" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
            continue
        i += 1
    return out


def _principled(name, **kwargs):
    import bpy

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    for k, v in kwargs.items():
        if k in bsdf.inputs:
            bsdf.inputs[k].default_value = v
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def main():
    import bpy
    from mathutils import Euler, Vector

    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    out_dir = _parse_out_dir(argv)
    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, "cup.png")
    out_blend = os.path.join(out_dir, "cup.blend")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1024
    scene.render.resolution_y = 1024
    scene.render.filepath = out_png
    scene.render.image_settings.file_format = "PNG"

    world = bpy.data.worlds.new("CupWorld")
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.85, 0.88, 0.92, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    wo = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs["Background"], wo.inputs["Surface"])

    # 桌面
    bpy.ops.mesh.primitive_plane_add(size=4, location=(0, 0, 0))
    table = bpy.context.active_object
    table.name = "Table"
    table.data.materials.append(
        _principled(
            "TableMat",
            **{
                "Base Color": (0.72, 0.68, 0.62, 1),
                "Roughness": 0.45,
            },
        )
    )

    # 杯身：外筒
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.45, depth=0.95, vertices=64, location=(0, 0, 0.48)
    )
    outer = bpy.context.active_object
    outer.name = "CupOuter"

    # 内筒（布尔挖空）
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.38, depth=1.0, vertices=64, location=(0, 0, 0.58)
    )
    inner = bpy.context.active_object
    inner.name = "CupInner"

    bool_mod = outer.modifiers.new("Hollow", "BOOLEAN")
    bool_mod.operation = "DIFFERENCE"
    bool_mod.solver = "EXACT"
    bool_mod.object = inner
    bpy.context.view_layer.objects.active = outer
    bpy.ops.object.modifier_apply(modifier="Hollow")
    bpy.data.objects.remove(inner, do_unlink=True)

    # 杯底加厚感：底部小圆盘
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.40, depth=0.06, vertices=64, location=(0, 0, 0.03)
    )
    base = bpy.context.active_object
    base.name = "CupBase"

    # 合并杯身与底
    bpy.ops.object.select_all(action="DESELECT")
    outer.select_set(True)
    base.select_set(True)
    bpy.context.view_layer.objects.active = outer
    bpy.ops.object.join()
    cup = bpy.context.active_object
    cup.name = "Cup"
    bpy.ops.object.shade_smooth()

    ceramic = _principled(
        "Ceramic",
        **{
            "Base Color": (0.92, 0.93, 0.95, 1),
            "Roughness": 0.28,
            "Metallic": 0.0,
        },
    )
    cup.data.materials.clear()
    cup.data.materials.append(ceramic)

    # 把手：半截圆环
    bpy.ops.mesh.primitive_torus_add(
        major_radius=0.28,
        minor_radius=0.055,
        major_segments=48,
        minor_segments=16,
        location=(0.55, 0, 0.52),
    )
    handle = bpy.context.active_object
    handle.name = "Handle"
    handle.rotation_euler = Euler((math.radians(90), 0, 0), "XYZ")
    # 裁掉靠杯内侧约一半：用立方布尔
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.15, 0, 0.52))
    cutter = bpy.context.active_object
    cutter.scale = (0.55, 0.8, 0.8)
    hbool = handle.modifiers.new("Trim", "BOOLEAN")
    hbool.operation = "DIFFERENCE"
    hbool.solver = "EXACT"
    hbool.object = cutter
    bpy.context.view_layer.objects.active = handle
    bpy.ops.object.modifier_apply(modifier="Trim")
    bpy.data.objects.remove(cutter, do_unlink=True)
    bpy.ops.object.shade_smooth()
    handle.data.materials.append(ceramic)

    # 灯光
    def add_sun(name, energy, rot_deg, color=(1, 1, 1)):
        data = bpy.data.lights.new(name + "Data", type="SUN")
        data.energy = energy
        data.color = color
        obj = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(obj)
        obj.rotation_euler = Euler(tuple(math.radians(a) for a in rot_deg), "XYZ")
        return obj

    def add_area(name, loc, rot_deg, color, energy, size=1.5):
        data = bpy.data.lights.new(name + "Data", type="AREA")
        data.energy = energy
        data.color = color
        data.size = size
        obj = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(obj)
        obj.location = Vector(loc)
        obj.rotation_euler = Euler(tuple(math.radians(a) for a in rot_deg), "XYZ")
        return obj

    add_sun("Sun", 2.5, (45, 0, 35))
    add_area("Fill", (-1.8, -1.2, 1.6), (60, 0, -40), (0.85, 0.9, 1.0), 40, 2.0)

    # 相机
    cam_data = bpy.data.cameras.new("CupCam")
    cam_data.lens = 70
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (1.9, -2.3, 1.35)
    direction = Vector((0.1, 0, 0.45)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    eevee = getattr(scene, "eevee", None)
    if eevee is not None and hasattr(eevee, "taa_render_samples"):
        eevee.taa_render_samples = 64

    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out_blend))
    bpy.ops.render.render(write_still=True)
    print(f"neo blender_cup: wrote {out_png}")
    print(f"neo blender_cup: blend {out_blend}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"neo blender_cup: ERROR {e}", file=sys.stderr)
        raise
