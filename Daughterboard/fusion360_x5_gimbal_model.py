"""Fusion 360 script: RadioMaster Pocket/Zorro X5 gimbal fit model.

Run from Fusion 360:
Utilities > Scripts and Add-Ins > Add-Ins/Scripts > + > select this file.

The script builds a measured/photographic reference model and exports a STEP
next to this script as radiomaster_x5_gimbal_fit_model.step.

All source dimensions are millimeters. Fusion API geometry uses centimeters,
so helper functions convert mm to cm at the API boundary.
"""

from __future__ import annotations

import math
import os
import traceback

import adsk.core
import adsk.fusion


# Measured from user photos / calipers.
PLATE_W = 39.0
PLATE_W_AT_BUMP = 41.9
PLATE_H = 35.0
PLATE_T = 2.2
RIGHT_BUMP_W = PLATE_W_AT_BUMP - PLATE_W
RIGHT_BUMP_H = 12.0
LEFT_LATCH_W = 1.2
LEFT_LATCH_H = 8.0

MOUNT_SPACING_X = 32.9
MOUNT_SPACING_Y = 30.0
MOUNTING_HOLE_D = 2.2
SCREW_BOSS_D = 6.1
SCREW_BOSS_H = 1.1

SHROUD_D = 37.0
SHROUD_H = 4.0
SHROUD_LIP_D = 34.2
SHROUD_LIP_H = 1.0
TRAVEL_WINDOW_W = 24.0
TRAVEL_WINDOW_H = 10.5

BODY_W = 34.0
BODY_H = 30.0
BODY_DEPTH = 23.0
OVERALL_HEIGHT = 43.3

STICK_SHAFT_D = 4.2
STICK_CAP_D = 8.6
STICK_CAP_H = 5.5
STICK_BASE_Z = PLATE_T + SHROUD_H + SHROUD_LIP_H
STICK_TOP_Z = OVERALL_HEIGHT - BODY_DEPTH
STICK_SHAFT_H = STICK_TOP_Z - STICK_BASE_Z - STICK_CAP_H


def cm(mm: float) -> float:
    return mm / 10.0


def pt(x_mm: float, y_mm: float, z_mm: float) -> adsk.core.Point3D:
    return adsk.core.Point3D.create(cm(x_mm), cm(y_mm), cm(z_mm))


def add_box(
    root: adsk.fusion.Component,
    tmp_mgr: adsk.fusion.TemporaryBRepManager,
    name: str,
    cx: float,
    cy: float,
    cz: float,
    sx: float,
    sy: float,
    sz: float,
) -> adsk.fusion.BRepBody:
    bbox = adsk.core.OrientedBoundingBox3D.create(
        pt(cx, cy, cz),
        adsk.core.Vector3D.create(1, 0, 0),
        adsk.core.Vector3D.create(0, 1, 0),
        cm(sx),
        cm(sy),
        cm(sz),
    )
    body = root.bRepBodies.add(tmp_mgr.createBox(bbox))
    body.name = name
    return body


def add_cylinder(
    root: adsk.fusion.Component,
    tmp_mgr: adsk.fusion.TemporaryBRepManager,
    name: str,
    cx: float,
    cy: float,
    z0: float,
    z1: float,
    diameter: float,
) -> adsk.fusion.BRepBody:
    body = root.bRepBodies.add(
        tmp_mgr.createCylinderOrCone(
            pt(cx, cy, z0),
            cm(diameter / 2),
            pt(cx, cy, z1),
            cm(diameter / 2),
        )
    )
    body.name = name
    return body


def add_cylinder_between(
    root: adsk.fusion.Component,
    tmp_mgr: adsk.fusion.TemporaryBRepManager,
    name: str,
    x0: float,
    y0: float,
    z0: float,
    x1: float,
    y1: float,
    z1: float,
    diameter: float,
) -> adsk.fusion.BRepBody:
    body = root.bRepBodies.add(
        tmp_mgr.createCylinderOrCone(
            pt(x0, y0, z0),
            cm(diameter / 2),
            pt(x1, y1, z1),
            cm(diameter / 2),
        )
    )
    body.name = name
    return body


def try_cut(
    root: adsk.fusion.Component,
    target: adsk.fusion.BRepBody,
    tools: list[adsk.fusion.BRepBody],
) -> None:
    """Best-effort boolean cut. Keeps tool bodies if Fusion rejects a cut."""
    if not tools:
        return
    tool_collection = adsk.core.ObjectCollection.create()
    for tool in tools:
        tool_collection.add(tool)
    combine_features = root.features.combineFeatures
    combine_input = combine_features.createInput(target, tool_collection)
    combine_input.operation = adsk.fusion.FeatureOperations.CutFeatureOperation
    combine_input.isKeepToolBodies = False
    combine_features.add(combine_input)


def add_plate(root, tmp_mgr):
    bodies = []
    bodies.append(add_box(root, tmp_mgr, "front_plate_main_39x35", 0, 0, PLATE_T / 2, PLATE_W, PLATE_H, PLATE_T))
    bodies.append(
        add_box(
            root,
            tmp_mgr,
            "right_side_front_bump",
            PLATE_W / 2 + RIGHT_BUMP_W / 2,
            0,
            PLATE_T / 2,
            RIGHT_BUMP_W,
            RIGHT_BUMP_H,
            PLATE_T,
        )
    )
    bodies.append(
        add_box(
            root,
            tmp_mgr,
            "left_side_cable_latch",
            -PLATE_W / 2 - LEFT_LATCH_W / 2,
            -1.0,
            PLATE_T / 2,
            LEFT_LATCH_W,
            LEFT_LATCH_H,
            PLATE_T,
        )
    )

    cutters = []
    for x in (-MOUNT_SPACING_X / 2, MOUNT_SPACING_X / 2):
        for y in (-MOUNT_SPACING_Y / 2, MOUNT_SPACING_Y / 2):
            cutters.append(add_cylinder(root, tmp_mgr, "mounting_hole_cutter_2p2mm", x, y, -0.8, PLATE_T + 3.0, MOUNTING_HOLE_D))
    for body in bodies:
        try:
            try_cut(root, body, cutters)
            break
        except Exception:
            # If one composite body cannot use every cutter, leave the cutters
            # visible as clearance markers rather than failing the whole model.
            for cutter in cutters:
                cutter.name = "mounting_hole_clearance_2p2mm"
            break
    return bodies


def add_mount_bosses(root, tmp_mgr):
    for x in (-MOUNT_SPACING_X / 2, MOUNT_SPACING_X / 2):
        for y in (-MOUNT_SPACING_Y / 2, MOUNT_SPACING_Y / 2):
            boss = add_cylinder(root, tmp_mgr, "raised_screw_boss", x, y, PLATE_T, PLATE_T + SCREW_BOSS_H, SCREW_BOSS_D)
            cutter = add_cylinder(
                root,
                tmp_mgr,
                "boss_hole_cutter_2p2mm",
                x,
                y,
                PLATE_T - 0.5,
                PLATE_T + SCREW_BOSS_H + 0.5,
                MOUNTING_HOLE_D,
            )
            try:
                try_cut(root, boss, [cutter])
            except Exception:
                cutter.name = "boss_hole_clearance_2p2mm"


def add_front_shroud(root, tmp_mgr):
    cup = add_cylinder(root, tmp_mgr, "raised_37mm_circular_shroud", 0, 0, PLATE_T, PLATE_T + SHROUD_H, SHROUD_D)
    lip = add_cylinder(
        root,
        tmp_mgr,
        "inner_shroud_lip_34p2mm",
        0,
        0,
        PLATE_T + SHROUD_H,
        PLATE_T + SHROUD_H + SHROUD_LIP_H,
        SHROUD_LIP_D,
    )
    for target in (cup, lip):
        slot = add_box(
            root,
            tmp_mgr,
            "rectangular_travel_window_cutter",
            0,
            0,
            PLATE_T + SHROUD_H / 2,
            TRAVEL_WINDOW_W,
            TRAVEL_WINDOW_H,
            SHROUD_H + SHROUD_LIP_H + 2.0,
        )
        try:
            try_cut(root, target, [slot])
        except Exception:
            slot.name = "rectangular_travel_window_clearance"


def add_body_details(root, tmp_mgr):
    # The rear is intentionally built from smaller visible forms instead of a
    # single bounding block so the side view resembles the real gimbal housing.
    add_box(root, tmp_mgr, "rear_recessed_upper_housing", 0, 4.8, -7.0, 27.0, 13.0, 12.0)
    add_box(root, tmp_mgr, "rear_lower_rocker_tray", 0, -6.8, -6.5, 26.0, 8.4, 9.5)
    add_box(root, tmp_mgr, "rear_back_spine_plate", 0, 0.8, -19.8, 27.0, 24.5, 2.4)
    add_box(root, tmp_mgr, "left_rear_side_tower", -17.7, -0.5, -11.4, 4.4, 28.0, 17.0)
    add_box(root, tmp_mgr, "right_rear_side_tower", 17.7, -0.5, -11.4, 4.4, 28.0, 17.0)

    # Side connector stack and small exposed PCB/wire details seen from the side.
    add_box(root, tmp_mgr, "side_connector_body_upper", -22.0, 4.2, -12.5, 4.4, 6.2, 5.6)
    add_box(root, tmp_mgr, "side_connector_socket_void_upper", -24.4, 4.2, -12.5, 0.9, 3.7, 2.5)
    add_box(root, tmp_mgr, "side_connector_body_lower", -22.0, -5.2, -13.5, 4.0, 5.2, 4.0)
    add_box(root, tmp_mgr, "side_sensor_pcb_plate", -20.2, 0.2, -17.2, 1.3, 22.0, 8.0)
    add_box(root, tmp_mgr, "bottom_sensor_pcb", 0, 4.0, -16.0, 14.0, 7.0, 1.4)
    add_box(root, tmp_mgr, "right_sensor_pcb", 18.8, 1.5, -13.5, 1.4, 13.0, 8.0)

    # Round side features: bearing/screw detail and the two stacked lower posts.
    bearing_outer = add_cylinder_between(root, tmp_mgr, "left_side_bearing_ring_outer", -24.0, -6.0, -11.5, -20.0, -6.0, -11.5, 5.4)
    bearing_inner = add_cylinder_between(root, tmp_mgr, "left_side_bearing_ring_cutter", -24.3, -6.0, -11.5, -19.7, -6.0, -11.5, 3.2)
    try:
        try_cut(root, bearing_outer, [bearing_inner])
    except Exception:
        bearing_inner.name = "left_side_bearing_ring_inner_marker"
    for y, z in ((-12.2, -13.2), (-15.8, -14.8)):
        add_cylinder_between(root, tmp_mgr, "stacked_lower_side_post", -23.2, y, z, -18.0, y, z, 3.2)

    add_box(root, tmp_mgr, "right_metal_retainer", 14.0, 0, -10.2, 2.8, 28.0, 1.1)
    for y in (-1.8, 0.0, 1.8):
        add_box(root, tmp_mgr, "wire_bundle_reference", -7.0, y + 7.0, -18.2, 26.0, 0.8, 0.8)


def add_stick(root, tmp_mgr):
    add_box(root, tmp_mgr, "front_yoke_block", 0, 0, PLATE_T + 3.8, 23.5, 7.4, 4.4)
    add_box(root, tmp_mgr, "front_lower_travel_bar", 0, -4.5, PLATE_T + 3.2, 26.0, 2.8, 2.0)
    add_cylinder(root, tmp_mgr, "stick_shaft", 0, 0, STICK_BASE_Z, STICK_BASE_Z + STICK_SHAFT_H, STICK_SHAFT_D)
    add_cylinder(root, tmp_mgr, "knurled_stick_top_core", 0, 0, STICK_BASE_Z + STICK_SHAFT_H, STICK_TOP_Z, STICK_CAP_D)
    for i in range(16):
        angle = 2 * math.pi * i / 16
        x = math.cos(angle) * (STICK_CAP_D / 2 + 0.35)
        y = math.sin(angle) * (STICK_CAP_D / 2 + 0.35)
        add_box(root, tmp_mgr, "stick_top_knurl_ridge", x, y, STICK_BASE_Z + STICK_SHAFT_H + STICK_CAP_H / 2, 0.8, 1.2, STICK_CAP_H * 0.82)


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.DirectDesignType
        root = design.rootComponent
        tmp_mgr = adsk.fusion.TemporaryBRepManager.get()

        add_plate(root, tmp_mgr)
        add_mount_bosses(root, tmp_mgr)
        add_front_shroud(root, tmp_mgr)
        add_body_details(root, tmp_mgr)
        add_stick(root, tmp_mgr)

        script_dir = os.path.dirname(os.path.realpath(__file__))
        step_path = os.path.join(script_dir, "radiomaster_x5_gimbal_fit_model.step")
        export_mgr = design.exportManager
        step_options = export_mgr.createSTEPExportOptions(step_path)
        export_mgr.execute(step_options)

        if ui:
            ui.messageBox(f"Created X5 gimbal fit model and exported STEP:\n{step_path}")
    except Exception:
        if ui:
            ui.messageBox("Fusion 360 X5 gimbal script failed:\n{}".format(traceback.format_exc()))
