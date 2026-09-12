#!/usr/bin/env python3
# blender_showcase.py — 在 Blender 后台生成一帧「微距金属环 + 玻璃球」展示图。
# 用法（由 wrapper 调用）：
#   blender --background --factory-startup --python this.py -- --out DIR
#
# 不依赖 MCP；供 Neo capability_matrix 白名单 exec。

import math
import os
import sys


def _parse_out_dir(argv):
    out = ".neo/blender_showcase"
    i = 0
    while i < len(argv):
        if argv[i] == "--out" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
            continue
        i += 1
    return out


def main():
    import bpy
    from mathutils import Euler, Vector

    # blender 把 "--" 之后的参数留给脚本
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    out_dir = _parse_out_dir(argv)
    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, "showcase.png")
    out_blend = os.path.join(out_dir, "showcase.blend")

    # --- 干净场景 ---
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.filepath = out_png
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False

    # 世界：深空渐变感
    world = bpy.data.worlds.new("NeoWorld")
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.02, 0.025, 0.04, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    out_w = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs["Background"], out_w.inputs["Surface"])

    # --- 地面（暗哑光）---
    bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, 0))
    ground = bpy.context.active_object
    ground.name = "Ground"
    mat_g = bpy.data.materials.new("GroundMat")
    mat_g.use_nodes = True
    nodes = mat_g.node_tree.nodes
    links = mat_g.node_tree.links
    nodes.clear()
    out_g = nodes.new("ShaderNodeOutputMaterial")
    bsdf_g = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf_g.inputs["Base Color"].default_value = (0.04, 0.045, 0.055, 1)
    bsdf_g.inputs["Roughness"].default_value = 0.55
    links.new(bsdf_g.outputs["BSDF"], out_g.inputs["Surface"])
    ground.data.materials.append(mat_g)

    # --- 金属环（Torus）---
    bpy.ops.mesh.primitive_torus_add(
        major_radius=1.05,
        minor_radius=0.28,
        major_segments=64,
        minor_segments=32,
        location=(0, 0, 0.55),
    )
    ring = bpy.context.active_object
    ring.name = "MetalRing"
    ring.rotation_euler = Euler((math.radians(18), math.radians(12), math.radians(35)), "XYZ")
    bpy.ops.object.shade_smooth()
    if hasattr(ring.data, "use_auto_smooth"):
        ring.data.use_auto_smooth = True
    sub = ring.modifiers.new("Subsurf", "SUBSURF")
    sub.levels = 2
    sub.render_levels = 2
    mat_m = bpy.data.materials.new("MetalMat")
    mat_m.use_nodes = True
    nodes = mat_m.node_tree.nodes
    links = mat_m.node_tree.links
    nodes.clear()
    out_m = nodes.new("ShaderNodeOutputMaterial")
    bsdf_m = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf_m.inputs["Base Color"].default_value = (0.75, 0.78, 0.82, 1)
    bsdf_m.inputs["Metallic"].default_value = 1.0
    bsdf_m.inputs["Roughness"].default_value = 0.12
    links.new(bsdf_m.outputs["BSDF"], out_m.inputs["Surface"])
    ring.data.materials.append(mat_m)

    # --- 玻璃球 ---
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.42, segments=64, ring_count=32, location=(0, 0, 0.55))
    ball = bpy.context.active_object
    ball.name = "GlassOrb"
    bpy.ops.object.shade_smooth()
    sub_b = ball.modifiers.new("Subsurf", "SUBSURF")
    sub_b.levels = 2
    sub_b.render_levels = 2
    mat_gl = bpy.data.materials.new("GlassMat")
    mat_gl.use_nodes = True
    nodes = mat_gl.node_tree.nodes
    links = mat_gl.node_tree.links
    nodes.clear()
    out_gl = nodes.new("ShaderNodeOutputMaterial")
    bsdf_gl = nodes.new("ShaderNodeBsdfPrincipled")
    # Blender 4+/5 Principled 用 Transmission Weight
    if "Transmission Weight" in bsdf_gl.inputs:
        bsdf_gl.inputs["Transmission Weight"].default_value = 1.0
    elif "Transmission" in bsdf_gl.inputs:
        bsdf_gl.inputs["Transmission"].default_value = 1.0
    bsdf_gl.inputs["Base Color"].default_value = (0.85, 0.92, 1.0, 1)
    bsdf_gl.inputs["Roughness"].default_value = 0.02
    bsdf_gl.inputs["IOR"].default_value = 1.45
    if "Alpha" in bsdf_gl.inputs:
        bsdf_gl.inputs["Alpha"].default_value = 0.15
    links.new(bsdf_gl.outputs["BSDF"], out_gl.inputs["Surface"])
    mat_gl.blend_method = "BLEND"
    ball.data.materials.append(mat_gl)

    # --- 三点光（主光暖、辅光青、轮廓品红）---
    def add_area(name, loc, rot_deg, color, energy, size=2.5):
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

    add_area("KeyLight", (2.8, -2.2, 3.2), (50, 0, 40), (1.0, 0.92, 0.82), 350, 2.2)
    add_area("FillLight", (-2.6, -1.0, 2.0), (65, 0, -50), (0.45, 0.75, 1.0), 120, 3.0)
    add_area("RimLight", (0.2, 3.0, 2.4), (70, 0, 180), (1.0, 0.35, 0.75), 200, 2.0)

    # --- 相机 ---
    cam_data = bpy.data.cameras.new("ShowcaseCam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = (3.4, -3.6, 2.1)
    # 看向环心
    direction = Vector((0, 0, 0.55)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    # EEVEE 提一点观感
    eevee = getattr(scene, "eevee", None)
    if eevee is not None:
        if hasattr(eevee, "use_raytracing"):
            eevee.use_raytracing = True
        if hasattr(eevee, "taa_render_samples"):
            eevee.taa_render_samples = 64

    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out_blend))
    bpy.ops.render.render(write_still=True)

    print(f"neo blender_showcase: wrote {out_png}")
    print(f"neo blender_showcase: blend {out_blend}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"neo blender_showcase: ERROR {e}", file=sys.stderr)
        raise
