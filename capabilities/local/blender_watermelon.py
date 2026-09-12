#!/usr/bin/env python3
# blender_watermelon.py — 后台生成一只西瓜并渲染静帧。
# blender --background --factory-startup --python this.py -- --out DIR

import math
import os
import sys


def _parse_out_dir(argv):
    out = ".neo/blender_watermelon"
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


def _watermelon_material(name):
    """深绿底 + 浅绿条纹：用 Wave 纹理沿球面做条带。"""
    import bpy

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nodes = nt.nodes
    links = nt.links
    nodes.clear()

    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.35
    bsdf.inputs["Specular IOR Level"].default_value = 0.4

    tex_coord = nodes.new("ShaderNodeTexCoord")
    # 用物体坐标的 Y 分量做条纹方向，配合 Wave 的 bands
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Rotation"].default_value = (0.0, 0.0, math.radians(90))
    wave = nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "X"
    wave.inputs["Scale"].default_value = 6.0
    wave.inputs["Distortion"].default_value = 1.5
    wave.inputs["Detail"].default_value = 2.0

    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.interpolation = "CONSTANT"
    e0 = ramp.color_ramp.elements[0]
    e1 = ramp.color_ramp.elements[1]
    e0.position = 0.0
    e0.color = (0.05, 0.22, 0.08, 1.0)   # 深绿
    e1.position = 0.5
    e1.color = (0.45, 0.68, 0.22, 1.0)   # 浅绿条纹

    links.new(tex_coord.outputs["Object"], mapping.inputs["Vector"])
    links.new(mapping.outputs["Vector"], wave.inputs["Vector"])
    links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
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
    out_png = os.path.join(out_dir, "watermelon.png")
    out_blend = os.path.join(out_dir, "watermelon.blend")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1024
    scene.render.resolution_y = 1024
    scene.render.filepath = out_png
    scene.render.image_settings.file_format = "PNG"

    world = bpy.data.worlds.new("MelonWorld")
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.86, 0.90, 0.94, 1.0)
    bg.inputs["Strength"].default_value = 0.9
    wo = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs["Background"], wo.inputs["Surface"])

    # 桌面
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    table = bpy.context.active_object
    table.name = "Table"
    table.data.materials.append(
        _principled(
            "TableMat",
            **{"Base Color": (0.74, 0.70, 0.64, 1), "Roughness": 0.5},
        )
    )

    # 瓜身：略扁的球（西瓜是椭球）
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=0.6, segments=64, ring_count=32, location=(0, 0, 0.52)
    )
    melon = bpy.context.active_object
    melon.name = "Watermelon"
    melon.scale = (1.0, 1.0, 0.85)
    bpy.ops.object.shade_smooth()
    melon.data.materials.append(_watermelon_material("MelonSkin"))

    # 藤蔓：弯曲的小圆柱
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.02, depth=0.22, vertices=16, location=(0.0, 0.0, 1.02)
    )
    stem = bpy.context.active_object
    stem.name = "Stem"
    stem.rotation_euler = Euler((math.radians(18), 0, 0), "XYZ")
    bpy.ops.object.shade_smooth()
    stem.data.materials.append(
        _principled(
            "StemMat",
            **{"Base Color": (0.30, 0.42, 0.14, 1), "Roughness": 0.6},
        )
    )

    # 叶子：压扁的球
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=0.16, segments=24, ring_count=12, location=(0.16, 0.02, 1.06)
    )
    leaf = bpy.context.active_object
    leaf.name = "Leaf"
    leaf.scale = (1.0, 0.55, 0.12)
    leaf.rotation_euler = Euler((0, math.radians(20), math.radians(25)), "XYZ")
    bpy.ops.object.shade_smooth()
    leaf.data.materials.append(
        _principled(
            "LeafMat",
            **{"Base Color": (0.34, 0.50, 0.16, 1), "Roughness": 0.55},
        )
    )

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

    add_sun("Sun", 3.0, (48, 0, 40))
    add_area("Fill", (-2.0, -1.4, 1.8), (60, 0, -40), (0.85, 0.9, 1.0), 45, 2.2)

    # 相机
    cam_data = bpy.data.cameras.new("MelonCam")
    cam_data.lens = 65
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (1.9, -2.4, 1.5)
    direction = Vector((0.0, 0.0, 0.5)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    eevee = getattr(scene, "eevee", None)
    if eevee is not None and hasattr(eevee, "taa_render_samples"):
        eevee.taa_render_samples = 64

    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out_blend))
    bpy.ops.render.render(write_still=True)
    print(f"neo blender_watermelon: wrote {out_png}")
    print(f"neo blender_watermelon: blend {out_blend}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"neo blender_watermelon: ERROR {e}", file=sys.stderr)
        raise
