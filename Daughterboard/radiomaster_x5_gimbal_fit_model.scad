// RadioMaster Pocket X5 gimbal fit/detail model
// Units: mm
//
// Measured dimensions:
// - Mounting hole spacing: 32.9 mm x 30.0 mm center-to-center
// - Front mounting plate: 39 mm x 35 mm
// - Right-side front bump width: 41.9 mm overall
// - Raised circular front shroud: 37 mm diameter, overhangs the 35 mm plate height
// - Mounting holes: 4x 2.2 mm
// - Overall height: 43.3 mm from bottom of gimbal to top of stick
// - Side-view body depth: about 23.0 mm below the front mounting face
//
// Photo-derived details are approximate and intended for enclosure/clearance design.

$fn = 96;

plate_w = 39.0;
plate_w_at_bump = 41.9;
plate_h = 35.0;
plate_t = 2.2;
corner_r = 3.1;
right_bump_w = plate_w_at_bump - plate_w;
right_bump_h = 12.0;

mount_spacing_x = 32.9;
mount_spacing_y = 30.0;
mounting_hole_d = 2.2;
screw_boss_d = 6.1;
screw_boss_h = 1.1;

shroud_d = 37.0;
shroud_h = 4.0;
shroud_lip_d = 34.2;
shroud_lip_h = 1.0;
window_w = 24.0;
window_h = 10.5;

body_w = 34.0;
body_h = 30.0;
body_depth = 23.0;
overall_height = 43.3;

stick_shaft_d = 4.2;
stick_cap_d = 8.6;
stick_cap_h = 5.5;
stick_top_z = overall_height - body_depth;
stick_base_z = plate_t + shroud_h;
stick_shaft_h = stick_top_z - stick_base_z - stick_cap_h;

pcb_t = 1.4;

module rounded_rect_2d(w, h, r) {
    hull() {
        for (x=[-w/2+r, w/2-r])
            for (y=[-h/2+r, h/2-r])
                translate([x, y])
                    circle(r=r);
    }
}

module front_plate_2d() {
    union() {
        rounded_rect_2d(plate_w, plate_h, corner_r);
        translate([plate_w/2 + right_bump_w/2 - 0.02, 0])
            square([right_bump_w + 0.04, right_bump_h], center=true);
        // Small left-side cable latch seen in the side photos.
        translate([-plate_w/2 - 0.6, -1.0])
            square([1.2, 8.0], center=true);
    }
}

module front_plate() {
    difference() {
        linear_extrude(plate_t)
            front_plate_2d();

        for (x=[-mount_spacing_x/2, mount_spacing_x/2])
            for (y=[-mount_spacing_y/2, mount_spacing_y/2])
                translate([x, y, -0.1])
                    cylinder(h=plate_t + screw_boss_h + 0.4, d=mounting_hole_d);
    }
}

module screw_bosses() {
    difference() {
        union() {
            for (x=[-mount_spacing_x/2, mount_spacing_x/2])
                for (y=[-mount_spacing_y/2, mount_spacing_y/2])
                    translate([x, y, plate_t])
                        cylinder(h=screw_boss_h, d=screw_boss_d);
        }
        for (x=[-mount_spacing_x/2, mount_spacing_x/2])
            for (y=[-mount_spacing_y/2, mount_spacing_y/2])
                translate([x, y, plate_t - 0.1])
                    cylinder(h=screw_boss_h + 0.3, d=mounting_hole_d);
    }
}

module front_shroud() {
    difference() {
        union() {
            translate([0, 0, plate_t])
                cylinder(h=shroud_h, d=shroud_d);
            translate([0, 0, plate_t + shroud_h])
                cylinder(h=shroud_lip_h, d=shroud_lip_d);
        }

        // The front opening is visually a wide rectangular travel window inside the cup.
        translate([0, 0, plate_t + 0.45])
            cube([window_w, window_h, shroud_h + shroud_lip_h + 0.8], center=true);
    }
}

module front_yoke() {
    translate([0, 0, plate_t + 1.6])
        cube([23.5, 7.4, 4.4], center=true);
    translate([0, -4.5, plate_t + 1.0])
        cube([26.0, 2.8, 2.0], center=true);
}

module stick() {
    color("silver") {
        translate([0, 0, stick_base_z])
            cylinder(h=stick_shaft_h, d=stick_shaft_d);

        translate([0, 0, stick_base_z + stick_shaft_h])
            cylinder(h=stick_cap_h, d=stick_cap_d);

        // Coarse knurl ridges around the cap.
        for (i=[0:15]) {
            rotate([0, 0, i * 360 / 16])
                translate([stick_cap_d/2 + 0.25, 0, stick_base_z + stick_shaft_h + stick_cap_h/2])
                    cube([0.7, 1.2, stick_cap_h * 0.82], center=true);
        }
    }
}

module rear_body() {
    color("black") {
        translate([0, 0, -body_depth/2])
            cube([body_w, body_h, body_depth], center=true);

        translate([0, -8.8, -6.8])
            cube([26.0, 5.0, 8.0], center=true);

        // Bottom cylindrical posts visible from the side.
        for (x=[-14.0, 14.0])
            translate([x, -plate_h/2 - 1.0, -12.5])
                cylinder(h=8.0, d=3.2);
    }
}

module underside_details() {
    // Left connector pair and small side PCB.
    color("white") {
        translate([-22.0, 3.4, -7.5])
            cube([4.2, 6.0, 3.2], center=true);
        translate([-22.0, -4.5, -9.2])
            cube([4.0, 5.0, 3.0], center=true);
    }

    color("darkslategray") {
        translate([0, 4.0, -12.0])
            cube([14.0, 7.0, pcb_t], center=true);
        translate([18.5, 1.5, -10.5])
            cube([8.0, 13.0, pcb_t], center=true);
    }

    // Metal strap on bottom/right side.
    color("silver")
        translate([14.0, 0, -7.0])
            cube([3.0, 28.0, 1.1], center=true);

    // Approximate colored wire bundle paths.
    for (dy=[-1.8, 0, 1.8]) {
        color(dy < -1 ? "red" : dy > 1 ? "yellow" : "black")
            translate([2.0, dy, -10.8])
                rotate([0, 90, 0])
                    cylinder(h=31.0, d=0.9, center=true);
    }
}

module clearance_envelope() {
    // Transparent-ish max envelope that includes the side bump and bottom protrusions.
    // Enable manually if you want to sanity-check enclosure clearance.
    // color([0, 0.4, 1, 0.18]) cube([45, 39, 43], center=true);
}

module gimbal_fit_model() {
    color("black") front_plate();
    color("black") screw_bosses();
    color("black") front_shroud();
    color("black") front_yoke();
    rear_body();
    underside_details();
    stick();
    clearance_envelope();
}

gimbal_fit_model();
