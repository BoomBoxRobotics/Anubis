from __future__ import annotations

import math
from pathlib import Path


OUT = Path("radiomaster_x5_gimbal_fit_model.stl")

PLATE_W = 39.0
PLATE_W_AT_BUMP = 41.9
PLATE_H = 35.0
PLATE_T = 2.2
BUMP_H = 12.0
RIGHT_BUMP_W = PLATE_W_AT_BUMP - PLATE_W

STICK_OPENING_D = 37.0
CIRCULAR_SHROUD_H = 4.0
SHROUD_LIP_D = 34.2
SHROUD_LIP_H = 1.0
WINDOW_W = 24.0
WINDOW_H = 10.5
MOUNT_SPACING_X = 32.9
MOUNT_SPACING_Y = 30.0
MOUNTING_HOLE_D = 2.2
SCREW_BOSS_D = 6.1
SCREW_BOSS_H = 1.1

BODY_W = 34.0
BODY_H = 30.0
BODY_DEPTH = 23.0

STICK_SHAFT_D = 4.2
STICK_CAP_D = 8.6
STICK_CAP_H = 5.5
OVERALL_HEIGHT = 43.3
STICK_TOP_Z = OVERALL_HEIGHT - BODY_DEPTH
STICK_BASE_Z = PLATE_T + CIRCULAR_SHROUD_H + SHROUD_LIP_H
STICK_SHAFT_H = STICK_TOP_Z - STICK_BASE_Z - STICK_CAP_H

SEGMENTS = 96


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def norm(a):
    length = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2)
    if length == 0:
        return (0.0, 0.0, 0.0)
    return (a[0] / length, a[1] / length, a[2] / length)


def tri_normal(tri):
    return norm(cross(vsub(tri[1], tri[0]), vsub(tri[2], tri[0])))


def add_box(tris, cx, cy, cz, sx, sy, sz):
    x0, x1 = cx - sx / 2, cx + sx / 2
    y0, y1 = cy - sy / 2, cy + sy / 2
    z0, z1 = cz - sz / 2, cz + sz / 2
    p = {
        "000": (x0, y0, z0),
        "100": (x1, y0, z0),
        "110": (x1, y1, z0),
        "010": (x0, y1, z0),
        "001": (x0, y0, z1),
        "101": (x1, y0, z1),
        "111": (x1, y1, z1),
        "011": (x0, y1, z1),
    }
    faces = [
        ("bottom", ["000", "010", "110", "100"]),
        ("top", ["001", "101", "111", "011"]),
        ("front", ["000", "100", "101", "001"]),
        ("right", ["100", "110", "111", "101"]),
        ("back", ["110", "010", "011", "111"]),
        ("left", ["010", "000", "001", "011"]),
    ]
    for _, keys in faces:
        a, b, c, d = [p[k] for k in keys]
        tris.append((a, b, c))
        tris.append((a, c, d))


def add_cylinder(tris, cx, cy, z0, z1, radius, segments=SEGMENTS):
    top_center = (cx, cy, z1)
    bottom_center = (cx, cy, z0)
    for i in range(segments):
        a0 = 2 * math.pi * i / segments
        a1 = 2 * math.pi * (i + 1) / segments
        p0 = (cx + radius * math.cos(a0), cy + radius * math.sin(a0), z0)
        p1 = (cx + radius * math.cos(a1), cy + radius * math.sin(a1), z0)
        p2 = (cx + radius * math.cos(a1), cy + radius * math.sin(a1), z1)
        p3 = (cx + radius * math.cos(a0), cy + radius * math.sin(a0), z1)
        tris.append((p0, p1, p2))
        tris.append((p0, p2, p3))
        tris.append((top_center, p3, p2))
        tris.append((bottom_center, p1, p0))


def add_slotted_disk_grid(tris, z0, z1, radius, slot_w, slot_h, step=0.5):
    cells = set()
    count = int(math.ceil((radius * 2) / step))
    min_xy = -count * step / 2
    for ix in range(count):
        for iy in range(count):
            cx = min_xy + (ix + 0.5) * step
            cy = min_xy + (iy + 0.5) * step
            in_circle = cx * cx + cy * cy <= radius * radius
            in_slot = abs(cx) <= slot_w / 2 and abs(cy) <= slot_h / 2
            if in_circle and not in_slot:
                cells.add((ix, iy))

    def pt(ix, iy, z):
        return (min_xy + ix * step, min_xy + iy * step, z)

    for ix, iy in cells:
        p00 = pt(ix, iy, z0)
        p10 = pt(ix + 1, iy, z0)
        p11 = pt(ix + 1, iy + 1, z0)
        p01 = pt(ix, iy + 1, z0)
        q00 = pt(ix, iy, z1)
        q10 = pt(ix + 1, iy, z1)
        q11 = pt(ix + 1, iy + 1, z1)
        q01 = pt(ix, iy + 1, z1)

        tris.append((q00, q10, q11))
        tris.append((q00, q11, q01))
        tris.append((p00, p11, p10))
        tris.append((p00, p01, p11))

        neighbors = [
            ((ix, iy - 1), (p00, p10, q10, q00)),
            ((ix + 1, iy), (p10, p11, q11, q10)),
            ((ix, iy + 1), (p11, p01, q01, q11)),
            ((ix - 1, iy), (p01, p00, q00, q01)),
        ]
        for neighbor, quad in neighbors:
            if neighbor not in cells:
                a, b, c, d = quad
                tris.append((a, b, c))
                tris.append((a, c, d))


def ring_points(cx, cy, r, z, segments=SEGMENTS):
    return [
        (cx + r * math.cos(2 * math.pi * i / segments), cy + r * math.sin(2 * math.pi * i / segments), z)
        for i in range(segments)
    ]


def add_mount_hole_markers(tris):
    # Through-hole voids are represented by clearance cylinders colored by geometry only in STL.
    # The plate is primarily a fit-check visual mesh; edit SCAD for true CSG if needed.
    for x in (-MOUNT_SPACING_X / 2, MOUNT_SPACING_X / 2):
        for y in (-MOUNT_SPACING_Y / 2, MOUNT_SPACING_Y / 2):
            add_cylinder(tris, x, y, PLATE_T, PLATE_T + 0.35, MOUNTING_HOLE_D / 2)


def add_screw_bosses(tris):
    for x in (-MOUNT_SPACING_X / 2, MOUNT_SPACING_X / 2):
        for y in (-MOUNT_SPACING_Y / 2, MOUNT_SPACING_Y / 2):
            add_cylinder(tris, x, y, PLATE_T, PLATE_T + SCREW_BOSS_H, SCREW_BOSS_D / 2)


def write_stl(path, tris):
    with path.open("w", newline="\n") as fh:
        fh.write("solid radiomaster_x5_gimbal_fit_model\n")
        for tri in tris:
            n = tri_normal(tri)
            fh.write(f"  facet normal {n[0]:.6g} {n[1]:.6g} {n[2]:.6g}\n")
            fh.write("    outer loop\n")
            for p in tri:
                fh.write(f"      vertex {p[0]:.6g} {p[1]:.6g} {p[2]:.6g}\n")
            fh.write("    endloop\n")
            fh.write("  endfacet\n")
        fh.write("endsolid radiomaster_x5_gimbal_fit_model\n")


def main():
    tris = []
    add_box(tris, 0, 0, PLATE_T / 2, PLATE_W, PLATE_H, PLATE_T)
    add_box(tris, PLATE_W / 2 + RIGHT_BUMP_W / 2, 0, PLATE_T / 2, RIGHT_BUMP_W, BUMP_H, PLATE_T)
    add_box(tris, -PLATE_W / 2 - 0.6, -1.0, PLATE_T / 2, 1.2, 8.0, PLATE_T)
    add_screw_bosses(tris)
    add_slotted_disk_grid(tris, PLATE_T, PLATE_T + CIRCULAR_SHROUD_H, STICK_OPENING_D / 2, WINDOW_W, WINDOW_H)
    add_slotted_disk_grid(
        tris,
        PLATE_T + CIRCULAR_SHROUD_H,
        PLATE_T + CIRCULAR_SHROUD_H + SHROUD_LIP_H,
        SHROUD_LIP_D / 2,
        WINDOW_W,
        WINDOW_H,
    )
    add_box(tris, 0, 0, PLATE_T + 3.8, 23.5, 7.4, 4.4)
    add_box(tris, 0, -4.5, PLATE_T + 3.2, 26.0, 2.8, 2.0)
    add_box(tris, 0, 0, -BODY_DEPTH / 2, BODY_W, BODY_H, BODY_DEPTH)
    add_box(tris, 0, -8.8, -6.8, 26.0, 5.0, 8.0)
    for x in (-14.0, 14.0):
        add_cylinder(tris, x, -PLATE_H / 2 - 1.0, -12.5, -4.5, 1.6)
    add_box(tris, -22.0, 3.4, -7.5, 4.2, 6.0, 3.2)
    add_box(tris, -22.0, -4.5, -9.2, 4.0, 5.0, 3.0)
    add_box(tris, 0, 4.0, -12.0, 14.0, 7.0, 1.4)
    add_box(tris, 18.5, 1.5, -10.5, 8.0, 13.0, 1.4)
    add_box(tris, 14.0, 0, -7.0, 3.0, 28.0, 1.1)
    for y in (-1.8, 0.0, 1.8):
        add_box(tris, 2.0, y, -10.8, 31.0, 0.9, 0.9)
    add_cylinder(
        tris,
        0,
        0,
        STICK_BASE_Z,
        STICK_BASE_Z + STICK_SHAFT_H,
        STICK_SHAFT_D / 2,
    )
    add_cylinder(tris, 0, 0, STICK_BASE_Z + STICK_SHAFT_H, STICK_TOP_Z, STICK_CAP_D / 2)
    for i in range(16):
        angle = 2 * math.pi * i / 16
        x = math.cos(angle) * (STICK_CAP_D / 2 + 0.35)
        y = math.sin(angle) * (STICK_CAP_D / 2 + 0.35)
        add_box(tris, x, y, STICK_BASE_Z + STICK_SHAFT_H + STICK_CAP_H / 2, 0.8, 1.2, STICK_CAP_H * 0.82)
    add_mount_hole_markers(tris)
    write_stl(OUT, tris)
    print(f"Wrote {OUT} with {len(tris)} triangles")


if __name__ == "__main__":
    main()
