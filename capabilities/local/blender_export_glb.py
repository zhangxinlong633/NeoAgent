#!/usr/bin/env python3
# blender_export_glb.py — 生成简易网格并导出 GLB（含 .blend）。
# blender --background --factory-startup --python this.py -- --out DIR

import math
import os
import sys


def _parse_out_dir(argv):
    out = ".neo/blender_export_glb"
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

    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    out_dir = _parse_out_dir(argv)
    os.makedirs(out_dir, exist_ok=True)
    out_glb = os.path.join(out_dir, "product.glb")
    out_blend = os.path.join(out_dir, "product.blend")
    out_png = os.path.join(out_dir, "preview.png")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.filepath = out_png
    scene.render.image_settings.file_format = "PNG"

    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=0.9, location=(0, 0, 0.9))
    mesh = bpy.context.active_object
    mesh.name = "ExportMesh"
    bpy.ops.object.shade_smooth()
    mat = bpy.data.materials.new("ExportMat")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (0.35, 0.65, 0.95, 1)
        bsdf.inputs["Roughness"].default_value = 0.28
        if "Metallic" in bsdf.inputs:
            bsdf.inputs["Metallic"].default_value = 0.15
    mesh.data.materials.append(mat)

    bpy.ops.mesh.primitive_plane_add(size=4, location=(0, 0, 0))
    ground = bpy.context.active_object
    ground.name = "Ground"

    light_data = bpy.data.lights.new(name="SunData", type="SUN")
    light_data.energy = 3.0
    light = bpy.data.objects.new("Sun", light_data)
    bpy.context.collection.objects.link(light)
    light.rotation_euler = Euler((math.radians(45), math.radians(15), 0), "XYZ")

    cam_data = bpy.data.cameras.new("ExportCam")
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = Vector((3.2, -3.2, 2.4))
    cam.rotation_euler = Euler((math.radians(58), 0, math.radians(45)), "XYZ")
    scene.camera = cam

    # 只导出网格物体到 GLB
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.export_scene.gltf(
        filepath=out_glb,
        export_format="GLB",
        use_selection=True,
    )
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.wm.save_as_mainfile(filepath=out_blend)
    bpy.ops.render.render(write_still=True)
    print(f"neo blender_export_glb: wrote {out_glb}")
    print(f"neo blender_export_glb: preview {out_png}")
    print(f"neo blender_export_glb: blend {out_blend}")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"neo blender_export_glb: ERROR {e}", file=sys.stderr)
        sys.exit(1)
