# SPDX-License-Identifier: GPL-2.0-or-later

import bpy
from bpy.types import Operator


def get_rig_and_cam(obj):
    if (obj.type == 'ARMATURE'
            and "rig_id" in obj
            and obj["rig_id"].lower() in {"dolly_rig",
                                          "crane_rig", "2d_rig"}):
        cam = None
        for child in obj.children:
            if child.type == 'CAMERA':
                cam = child
                break
        if cam is not None:
            return obj, cam
    elif (obj.type == 'CAMERA'
          and obj.parent is not None
          and "rig_id" in obj.parent
          and obj.parent["rig_id"].lower() in {"dolly_rig",
                                               "crane_rig", "2d_rig"}):
        return obj.parent, obj
    return None, None


class CameraRigMixin():
    @classmethod
    def poll(cls, context):
        if context.active_object is not None:
            return get_rig_and_cam(context.active_object) != (None, None)

        return False


class ADD_CAMERA_RIGS_OT_set_scene_camera(Operator):
    bl_idname = "add_camera_rigs.set_scene_camera"
    bl_label = "Make Camera Active"
    bl_description = "Makes the camera parented to this rig the active scene camera"

    @classmethod
    def poll(cls, context):
        if context.active_object is not None:
            rig, cam = get_rig_and_cam(context.active_object)
            if cam is not None:
                return cam is not context.scene.camera

        return False

    def execute(self, context):
        rig, cam = get_rig_and_cam(context.active_object)
        scene_cam = context.scene.camera

        context.scene.camera = cam
        return {'FINISHED'}


class ADD_CAMERA_RIGS_OT_add_marker_bind(Operator, CameraRigMixin):
    bl_idname = "add_camera_rigs.add_marker_bind"
    bl_label = "Add Marker and Bind Camera"
    bl_description = "Add marker to current frame then bind rig camera to it (for camera switching)"

    def execute(self, context):
        rig, cam = get_rig_and_cam(context.active_object)

        marker = context.scene.timeline_markers.new(
            "cam_" + str(context.scene.frame_current),
            frame=context.scene.frame_current
        )
        marker.camera = cam

        return {'FINISHED'}


class ADD_CAMERA_RIGS_OT_add_dof_object(Operator, CameraRigMixin):
    bl_idname = "add_camera_rigs.add_dof_object"
    bl_label = "Add DOF Object"
    bl_description = "Create Empty and add as DOF Object"

    def execute(self, context):
        rig, cam = get_rig_and_cam(context.active_object)
        bone = rig.data.bones['Aim_shape_rotation-MCH']

        # Add Empty
        empty_obj = bpy.data.objects.new("EmptyDOF", None)
        context.scene.collection.objects.link(empty_obj)

        # Parent to Aim Child bone
        empty_obj.parent = rig
        empty_obj.parent_type = "BONE"
        empty_obj.parent_bone = "Aim_shape_rotation-MCH"

        # Move to bone head
        empty_obj.location = bone.head

        # Make this new empty the dof_object
        cam.data.dof.use_dof = True
        cam.data.dof.focus_object = empty_obj

        return {'FINISHED'}


classes = (
    ADD_CAMERA_RIGS_OT_set_scene_camera,
    ADD_CAMERA_RIGS_OT_add_marker_bind,
    ADD_CAMERA_RIGS_OT_add_dof_object,
)


def register():
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)


def unregister():
    from bpy.utils import unregister_class
    for cls in classes:
        unregister_class(cls)
