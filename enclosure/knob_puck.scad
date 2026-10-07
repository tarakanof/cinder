// knob_puck.scad: desk puck for the VIEWE UEDX46460015-MD50ET round knob
// (ESP32-S3, Ø51.7 body, rear hub with 3x M2.5 holes) and its
// USB-test-MD50-V5.1 adapter board.
//
// The rear hub (3x M2.5 holes, 2 positioning pillars, FPC exit) is the
// fixed mount. Most likely only the CNC metal ring turns (the FPC runs
// through the base to the PCB, so the PCB and display probably stay put);
// check by hand. Either way this puck touches ONLY the hub: the body floats
// 1 mm above the deck with 0.6 mm radial clearance inside a shallow shroud,
// so you grip and turn the exposed 12 mm of the ring.
//
// Parts (set `part`):
//   "shell"    common to every variant: knob socket, adapter cavity, USB-C port
//   "washer"   clamps the hub under the ledge with 3x M2.5x6 + flat washers
//   "lid"      bottom plate with the adapter standoffs, per variant
//              (a feet / b pogo + magnets / c Qi panel recess / d wire hole)
//   "dock"     variant b only: magnetic pogo dock with USB-C breakout pocket
//   "riser"    variant d only: Ø84 cup for the LiPo + PowerBoost under the lid
//   "assembly" / "exploded"  preview with dummy knob and adapter
//
// Units mm. Shell z = 0 is the shell's bottom face. Print every part on its
// flat bottom face as modelled (shell: pocket up; washer: groove up;
// lid/dock/riser: as modelled). No supports by design, but two bridges are
// long: the shell's cavity roof (about 43 mm) and lid_c's Qi recess roof
// (32 x 48 mm). Sag is not verified. The shell roof prints as a closed
// 0.2 mm membrane over the Ø18 ledge hole (`membrane`), so no hole edge
// prints over air; cut the membrane out with a knife after printing. If the
// roof sags, use "supports on build plate only" for the cavity.
// The USB-C port roof is a 13 mm bridge.
//
// Values marked EST were scaled from the vendor drawing or a photo.
// Measure them (see README.md) before you print the final part.
//
// The block between [params-begin] and [params-end] is also read by
// make_stl.py. Keep it to `name = expression;` with numbers, lists,
// + - * / and indexing only.

// [params-begin]
variant = 0;        // 0 = (a) USB-C cable, 1 = (b) magnetic pogo + dock, 2 = (c) Qi receiver, 3 = (d) battery

// --- knob: vendor drawing SFG-UEDX4646015-MD50E rev A (spec V2.0 p.9) ---
body_d = 51.7;      // body OD, drawing 51.7 +-0.5. MEASURE; enter your max
body_h = 15.23;     // front face to rear face of the round body, +-0.3
disp_d = 38.21;     // display area
hub1_d = 24.0;      // rear hub step 1 (meets the body)
hub1_h = 5.07;      // = 25.15 - 15.23 - 4.85
hub2_d = 20.9;      // rear hub step 2
hub2_h = 2.77;      // EST: 4.85 - 2.08 (drawing chain is ambiguous)
hub3_d = 17.4;      // rear hub step 3 (FPC exits its centre)
hub3_h = 2.08;
hub_tab_d = 26.0;   // EST: latch tabs on the hub ring, scaled from back view
pin_pcd = 15.03;    // EST circle: 2x dia 2.0 positioning pillars
pin_d = 2.0;
pin_h = 1.8;        // EST pillar protrusion
scr_pcd = 12.10;    // EST circle: 3x M2.5 screw holes at 120 deg
fpc_w = 5.5;        // FPC width, 70 +-5 long from the hub face
fpc_len = 70;       // ~30 mm direct route; slack goes into the +y service loop (README "FPC route")

// --- adapter USB-test-MD50-V5.1: EST from photo (2.54 header pitch as scale) ---
ad_w = 42.0;        // along x; the USB-C edge faces +x
ad_d = 37.0;        // along y; the J1 FPC edge faces -y
ad_t = 1.6;
ad_y = 3.0;         // board centre offset towards +y
ad_holes = [[3, 3], [3, 34], [39, 34], [39, 14]];  // EST, [x, y] from the J1-edge/J2-side corner
usb_y = 5.5;        // EST USB-C centre, measured from the J1 edge
usb_zc = 1.6;       // plug axis above board top (top-mount receptacle)

// --- FDM fit ---
tol = 0.3;          // general clearance per side
rot_gap = 0.6;      // radial gap, body to shroud
ax_gap = 1.0;       // axial gap, body rear face to deck (press travel + slop)

// --- puck ---
puck_d = 66;
shroud = 3.0;       // how much of the body sits inside the shroud
ledge_t = 2.2;      // > hub3_h so the washer clamps the ledge
under = 2.5;        // lid top to board bottom = standoff height
top_clear = 9.0;    // board top to ceiling: washer 3.5 + M2.5 head 1.75 + 3.75 for the 2-layer FPC service loop
cav_clr = 0.5;
cav_bay = 5.5;      // extra room past the J1 edge for the FPC U-turn
cav_r = 2.0;
plug_w = 13.0;      // port opening for the USB-C plug overmold
post_d = 5.0;       // adapter standoffs on the lid (M2 self-tapping)
post_pilot = 1.7;
lid_scr_r = 28.5;   // 3x M3 self-tapping, lid into shell
lid_pilot = 2.5;
lid_pilot_len = 8;
lid_hole = 3.4;
lid_cb_d = 6.0;
foot_d = 10.5;      // recess for self-adhesive bumpers
foot_h = 0.6;
foot_r = 24;
washer_d = 24.0;
washer_t = 3.5;
fpc_hole = 7.0;
m25_hole = 2.9;
membrane = 0.2;     // printed skin over the ledge hole, cut out after printing

// --- variants ---
lid_t_v = [3.0, 4.0, 4.0, 3.0];   // c: 4 so the M2 pilots stay 1.2 above the Qi recess
mag_d = 8.0;        // N45 8x2 mm discs (b)
mag_h = 2.0;
mag_r = 24;
mag_fit = 0.15;     // press fit; use 0.2 for glue
pogo_l = 17.0;      // EST receptacle cut-out (b): MEASURE your connector
pogo_w = 6.0;
pogo_x = 0;
pogo_y = -6;
key_w = 6.0;        // orientation key (b)
key_d = 3.0;
qi_l = 48.0;        // Adafruit 1901 Qi receiver panel (c)
qi_w = 32.0;
qi_h = 1.8;
qi_wire = 4.0;      // wire hole from the Qi panel up to J2
dock_d = 84;
dock_h = 12;
dock_recess = 3.0;
brk_l = 22;         // pocket for Adafruit 4090 USB-C breakout (20.4 x 14.2 x 5)
brk_w = 20;
riser_d = 84;       // (d) battery cup under the lid
riser_h = 13.5;
riser_wall = 3;
riser_floor = 2.5;
bat_l = 40;         // 803040 LiPo, about 1000 mAh (d)
bat_w = 30;
pb_l = 45;          // Adafruit PowerBoost 1000C (d)
pb_w = 23;

// --- derived (do not edit) ---
lid_t = lid_t_v[variant];
lid_cb_h = lid_t - 1.2;
pocket_d = body_d + 2 * rot_gap;
bore_d = hub_tab_d + 2 * tol;
ledge_hd = hub3_d + 2 * tol;
z_bt = under + ad_t;
z_ceil = z_bt + top_clear;
z_lt = z_ceil + ledge_t;
z_deck = z_lt + hub2_h + hub1_h - ax_gap;
z_body = z_deck + ax_gap;
shell_h = z_deck + ax_gap + shroud;
cav_x = ad_w / 2 + cav_clr;
cav_y0 = ad_y - ad_d / 2 - cav_bay;
cav_y1 = ad_y + ad_d / 2 + cav_clr;
usb_yc = ad_y - ad_d / 2 + usb_y;
z_usb = z_bt + usb_zc;
// [params-end]

part = "assembly";
$fn = 128;
e = 0.01;

// ---------- helpers ----------
module rrect(x0, y0, x1, y1, r) {
    translate([x0 + r, y0 + r]) offset(r = r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]);
}
function ad_pt(p) = [p[0] - ad_w / 2, p[1] - ad_d / 2 + ad_y];

module cavity2d() {
    rrect(-cav_x, cav_y0, cav_x, cav_y1, cav_r);
    // USB-C plug port, open to the outside (+x) and to the bottom (lid closes it)
    translate([cav_x - 1, usb_yc - plug_w / 2]) square([puck_d, plug_w]);
}


// ---------- shell (all variants) ----------
module shell() {
    difference() {
        cylinder(d = puck_d, h = shell_h);
        translate([0, 0, -e]) linear_extrude(z_ceil + e) cavity2d();
        // lid screw pilots
        for (a = [90, 180, 270]) rotate(a) translate([lid_scr_r, 0, -e]) cylinder(d = lid_pilot, h = lid_pilot_len);
        // ledge hole (hub step 3 + FPC), hub bore, shroud pocket
        translate([0, 0, z_ceil + membrane]) cylinder(d = ledge_hd, h = ledge_t - membrane + e);
        translate([0, 0, z_lt]) cylinder(d = bore_d, h = z_deck - z_lt + e);
        translate([0, 0, z_deck]) cylinder(d = pocket_d, h = shell_h);
    }
}

// ---------- hub clamp washer ----------
module washer() {
    gd = pin_h + 0.4;
    difference() {
        cylinder(d = washer_d, h = washer_t);
        translate([0, 0, -e]) cylinder(d = fpc_hole, h = washer_t + 2 * e);
        // radial slots cover both screw-circle readings (12.10 / 15.03)
        for (a = [90, 210, 330]) rotate(a) translate([0, 0, -e]) hull() {
            translate([min(scr_pcd, pin_pcd) / 2, 0, 0]) cylinder(d = m25_hole, h = washer_t + 2 * e);
            translate([max(scr_pcd, pin_pcd) / 2, 0, 0]) cylinder(d = m25_hole, h = washer_t + 2 * e);
        }
        // ring groove takes the 2 positioning pillars at any rotation
        translate([0, 0, washer_t - gd]) difference() {
            cylinder(d = max(scr_pcd, pin_pcd) + pin_d + 1.0, h = gd + e);
            translate([0, 0, -e]) cylinder(d = min(scr_pcd, pin_pcd) - pin_d - 1.0, h = gd + 3 * e);
        }
    }
}

// ---------- lid (+ adapter standoffs) ----------
module lid_screws(t, cb) {
    for (a = [90, 180, 270]) rotate(a) translate([lid_scr_r, 0, 0]) {
        translate([0, 0, -e]) cylinder(d = lid_hole, h = t + 2 * e);
        translate([0, 0, -e]) cylinder(d = lid_cb_d, h = cb + e);
    }
}
module feet(r) {
    for (a = [45, 135, 225, 315]) rotate(a) translate([r, 0, -e]) cylinder(d = foot_d, h = foot_h + e);
}
module lid() {
    difference() {
        union() {
            cylinder(d = puck_d, h = lid_t);
            for (p = ad_holes) translate(concat(ad_pt(p), [lid_t - e])) cylinder(d = post_d, h = under + e);
        }
        for (p = ad_holes) translate(concat(ad_pt(p), [lid_t - 1])) cylinder(d = post_pilot, h = under + 2);
        lid_screws(lid_t, lid_cb_h);
        if (variant == 0) feet(foot_r);
        if (variant == 1) {
            for (a = [45, 135, 225, 315]) rotate(a) translate([mag_r, 0, -e]) cylinder(d = mag_d + mag_fit, h = mag_h + 0.1 + e);
            translate([pogo_x - pogo_l / 2 - tol, pogo_y - pogo_w / 2 - tol, -e]) cube([pogo_l + 2 * tol, pogo_w + 2 * tol, lid_t + 2 * e]);
            translate([puck_d / 2 - key_d, -key_w / 2, -e]) cube([key_d + 1, key_w, lid_t + 2 * e]);
        }
        if (variant == 2) {
            // Qi panel sits coil-down, flush with the bottom face
            translate([-(qi_l / 2 + tol), -(qi_w / 2 + tol), -e]) cube([qi_l + 2 * tol, qi_w + 2 * tol, qi_h + e]);
            translate([-(qi_l / 2 - 3), 3, -e]) cylinder(d = qi_wire, h = lid_t + 2 * e);
        }
        if (variant == 3) translate([-12, 0, -e]) cylinder(d = 6, h = lid_t + 2 * e);
    }
}

// ---------- (b) magnetic pogo dock ----------
module dock() {
    zf = dock_h - dock_recess;
    difference() {
        cylinder(d = dock_d, h = dock_h);
        difference() {
            translate([0, 0, zf]) cylinder(d = puck_d + 2 * tol, h = dock_recess + e);
            // key tab, fits the lid notch
            translate([puck_d / 2 - key_d + tol, -key_w / 2 + tol, zf - e]) cube([key_d + 2, key_w - 2 * tol, dock_recess + 2 * e]);
        }
        for (a = [45, 135, 225, 315]) rotate(a) translate([mag_r, 0, zf - mag_h - 0.1]) cylinder(d = mag_d + mag_fit, h = mag_h + 0.1 + e);
        // pogo plug: through the floor into the wiring cavity
        translate([pogo_x - pogo_l / 2 - tol, pogo_y - pogo_w / 2 - tol, -e]) cube([pogo_l + 2 * tol, pogo_w + 2 * tol, zf + 2 * e]);
        // wiring channel, breakout pocket, USB-C port (+x), ballast; all open to the bottom
        translate([0, 0, -e]) linear_extrude(6 + e) {
            rrect(pogo_x - 6, pogo_y - 5, dock_d / 2 - 3, pogo_y + 5, 1);
            rrect(dock_d / 2 - 3 - brk_l, -brk_w / 2, dock_d / 2 - 3, brk_w / 2, 1);
            translate([dock_d / 2 - 4, -plug_w / 2]) square([10, plug_w]);
            rrect(-dock_d / 2 + 8, -14, -10, 14, 2);
        }
    }
}

// ---------- (d) battery cup under the lid ----------
module riser() {
    difference() {
        cylinder(d = riser_d, h = riser_h);
        difference() {
            translate([0, 0, riser_floor]) cylinder(d = riser_d - 2 * riser_wall, h = riser_h);
            for (a = [90, 180, 270]) rotate(a) translate([lid_scr_r, 0, 0]) cylinder(d = 7, h = riser_h);
        }
        // the same 3 M3 screws pass cup + lid into the shell (M3x22)
        for (a = [90, 180, 270]) rotate(a) translate([lid_scr_r, 0, 0]) {
            translate([0, 0, -e]) cylinder(d = lid_hole, h = riser_h + 2 * e);
            translate([0, 0, -e]) cylinder(d = lid_cb_d, h = 1.3 + e);
        }
        feet(30);
        // PowerBoost micro-USB notch (-y) and slide-switch notch (+x), open at the top
        translate([12.5, -riser_d / 2 - 1, riser_floor + 1]) cube([12, riser_d / 2 - 23, riser_h]);
        translate([riser_d / 2 - riser_wall - 2, -4.75, 4]) cube([riser_wall + 3, 9.5, riser_h]);
    }
}

// ---------- dummies for preview ----------
module knob_dummy() {
    color("dimgray") translate([0, 0, z_body]) cylinder(d = body_d, h = body_h);
    color("black") translate([0, 0, z_body + body_h - e]) cylinder(d = disp_d, h = 0.2);
    color("silver") {
        translate([0, 0, z_body - hub1_h]) cylinder(d = hub1_d, h = hub1_h);
        translate([0, 0, z_body - hub1_h - hub2_h]) cylinder(d = hub2_d, h = hub2_h);
        translate([0, 0, z_body - hub1_h - hub2_h - hub3_h]) cylinder(d = hub3_d, h = hub3_h);
    }
}
module adapter_dummy() {
    color("royalblue") translate([-ad_w / 2, ad_y - ad_d / 2, under]) cube([ad_w, ad_d, ad_t]);
    color("silver") translate([ad_w / 2 - 7.3, usb_yc - 4.5, z_bt]) cube([7.5, 9, 3.2]);
}

module stack(gap) {
    base = (variant == 3) ? riser_h : 0;
    if (variant == 3) translate([0, 0, -gap]) color("teal") riser();
    if (variant == 1) translate([0, 0, dock_recess - dock_h - gap]) color("teal") dock();
    translate([0, 0, base]) {
        color("khaki") lid();
        translate([0, 0, lid_t]) {
            translate([0, 0, gap / 2]) adapter_dummy();
            translate([0, 0, gap]) color("orange", 0.5) shell();
            translate([0, 0, z_ceil - washer_t + gap / 2]) color("tomato") washer();
            translate([0, 0, 2 * gap]) knob_dummy();
        }
    }
}

if (part == "shell") shell();
else if (part == "washer") washer();
else if (part == "lid") lid();
else if (part == "dock") dock();
else if (part == "riser") riser();
else if (part == "exploded") stack(25);
else stack(0);
