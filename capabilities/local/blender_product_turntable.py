#!/usr/bin/env python3
# blender_product_turntable.py — 转盘上的简易产品静帧（非动画成片）。
# blender --background --factory-startup --python this.py -- --out DIR

import math
import os
import sys


def _parse_out_dir(argv):
    out = ".neo/blender_product_turntable"
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
    out_png = os.path.join(out_dir, "turntable.png")
    out_blend = os.path.join(out_dir, "turntable.blend")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1024
    scene.render.resolution_y = 1024
    scene.render.filepath = out_png
    scene.render.image_settings.file_format = "PNG"

    world = bpy.data.worlds.new("TurnWorld")
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.78, 0.80, 0.84, 1.0)
    bg.inputs["Strength"].default_value = 0.85
    wo = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs["Background"], wo.inputs["Surface"])

    # 转盘底座
    bpy.ops.mesh.primitive_cylinder_add(
        radius=1.15, depth=0.08, location=(0, 0, 0.04), vertices=64
    )
    platter = bpy.context.active_object
    platter.name = "Turntable"
    platter.data.materials.append(
        _principled(
            "PlatterMat",
            **{
                "Base Color": (0.18, 0.18, 0.20, 1),
                "Metallic": 0.85,
                "Roughness": 0.22,
            },
        )
    )

    # 简易「产品」：矮瓶身 + 盖
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.28, depth=0.85, location=(0, 0, 0.55), vertices=48
    )
    bottle = bpy.context.active_object
    bottle.name = "ProductBody"
    bpy.ops.object.shade_smooth()
    bottle.data.materials.append(
        _principled(
            "ProductMat",
            **{
                "Base Color": (0.92, 0.55, 0.22, 1),
                "Roughness": 0.35,
            },
        )
    )
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.22, depth=0.12, location=(0, 0, 1.02), vertices=32
    )
    cap = bpy.context.active_object
    cap.name = "ProductCap"
    cap.data.materials.append(
        _principled(
            "CapMat",
            **{
                "Base Color": (0.12, 0.12, 0.13, 1),
                "Metallic": 0.4,
                "Roughness": 0.3,
            },
        )
    )

    def add_area(name, loc, rot_deg, color, energy, size=2.0):
        light_data = bpy.data.lights.new(name=name + "Data", type="AREA")
        light_data.energy = energy
        light_data.color = color
        light_data.shape = "DISK"
        light_data.size = size
        obj = bpy.data.objects.new(name, light_data)
        bpy.context.collection.objects.link(obj)
        obj.location = Vector(loc)
        obj.rotation_euler = Euler(tuple(math.radians(a) for a in rot_deg), "XYZ")
        return obj

    add_area("Key", (2.5, -2.4, 2.8), (48, 0, 35), (1.0, 0.95, 0.88), 280, 2.0)
    add_area("Fill", (-2.2, -1.2, 1.8), (60, 0, -45), (0.6, 0.75, 1.0), 90, 2.5)
    add_area("Rim", (0.0, 2.8, 2.0), (65, 0, 180), (1.0, 0.5, 0.7), 140, 1.8)

    cam_data = bpy.data.cameras.new("TurnCam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (2.8, -3.2, 1.85)
    cam.rotation_euler = Euler((math.radians(62), 0, math.radians(38)), "XYZ")
    scene.camera = cam

    bpy.ops.wm.save_as_mainfile(filepath=out_blend)
    bpy.ops.render.render(write_still=True)
    print(f"neo blender_product_turntable: wrote {out_png}")
    print(f"neo blender_product_turntable: blend {out_blend}")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"neo blender_product_turntable: ERROR {e}", file=sys.stderr)
        sys.exit(1)
