# Knob puck enclosure

Desk puck for the VIEWE UEDX46460015-MD50ET knob and its `USB-test-MD50-V5.1` adapter. Drawings, power comparison and research: [`../docs/enclosure.html`](../docs/enclosure.html) (generated from a separate source, not in this repository).

**Status:** modelled and meshed, not printed. Values marked `EST` in `knob_puck.scad` were scaled from the vendor drawing or a photo. Measure them first (list below).

## Design in one paragraph

The rear hub (3× M2.5, 2 positioning pillars, FPC exit) is the fixed mount. Most likely only the CNC ring turns; check by hand. Either way the puck holds **only the hub**. The hub's Ø20.9→Ø17.4 shoulder sits on a 2.2 mm ledge, and a washer under the ledge takes the hub's 3× M2.5 screws. The washer has a ring groove for the pillars and radial slots for the screws, so the knob can sit at any rotation and either reading of the screw circle (Ø12.10 or Ø15.03) fits. The body floats 1.0 mm above the deck, inside a Ø52.9 shroud (0.6 mm radial gap) that covers its lower 3 mm. That leaves 12.2 mm of ring for your fingers. The adapter is screwed to standoffs on the lid. **FPC route:** the direct path from the hub face to J1 is about 30 mm and the FPC is 70 ±5 mm. So the FPC drops through the ledge hole and runs +y under the washer heads (3.75 mm space, `top_clear` = 9). It folds back at a service loop near the +y wall, returns −y over J1, and U-turns into J1 in the 5.5 mm bay. Moving the loop tip along y absorbs 36–75 mm. The USB-C plug enters through a 13 × 13.1 mm port at +x, and the port walls hold the plug overmold, which relieves strain on the receptacle.

## Files

| File | What |
|---|---|
| `knob_puck.scad` | Parametric OpenSCAD source. Set `variant` (0–3) and `part`. |
| `make_stl.py` | Builds every STL with Python 3 stdlib only (about 5 s). It reads the parameter block of the `.scad` file and checks that each mesh is closed. |
| `stl/` | Output of `make_stl.py` (git-ignored). |

```sh
python3 enclosure/make_stl.py            # all parts -> enclosure/stl/
python3 enclosure/make_stl.py shell lid_a --res 0.2
```

## Variants and parts

| Variant | Print | Buy |
|---|---|---|
| (a) USB-C cable, **start here** | `shell`, `washer`, `lid_a` | USB-C **data** cable, 3× M3×8 self-tapping, 4× M2×4 self-tapping (not ×5), 3× M2.5×6 + flat washers, 4 bumpers ≤ Ø10 |
| (b) magnetic pogo + dock | `shell`, `washer`, `lid_b`, `dock_b` | [Adafruit 5413](https://www.adafruit.com/product/5413) 5-pin magnetic pair (or [Mill-Max Maxnetic](https://www.mill-max.com/products/new/introducing-100-pitch-maxnetic-connectors)), [Adafruit 4090](https://www.adafruit.com/product/4090) USB-C breakout, 8× [8×2 N45](https://www.supermagnete.de/eng/S-08-02-N), steel ballast |
| (c) Qi | `shell`, `washer`, `lid_c` (PETG, 4 mm) | [Adafruit 1901](https://www.adafruit.com/product/1901), any Qi pad, 1N5819, 220–470 µF |
| (d) battery | `shell`, `washer`, `lid_d`, `riser_d` | 803040 LiPo (about 1000 mAh), [PowerBoost 1000C](https://www.adafruit.com/product/2465), SS12D00 switch, 1N5819, optional [MAX17048](https://www.adafruit.com/product/5580); 3× M3×22 |

Wiring for (b), (c) and (d) goes to adapter header J2: pin 1 5V, pin 3 GND, pin 9 DP, pin 10 DN. These pins agree in every vendor source. Feed (c) and (d) through the diode. In (b), never plug the adapter's own USB-C at the same time as the dock, and the receptacle must fit within 6.5 mm under the board.

Power: the spec gives 50–150 mA averages at 5 V, but ESP32-S3 Wi-Fi TX peaks reach about 300+ mA. The 500 mA Qi receiver has about 1.5× headroom at the peak, so add a 220–470 µF capacitor at J2. The PowerBoost 1000C and USB have plenty.

## Print settings (Prusa, 0.4 nozzle, no supports)

| Part | Orientation | Settings |
|---|---|---|
| shell | pocket up | 0.2 mm, 3 perimeters, 20 % gyroid. The cavity roof is a 43 mm bridge; sag is not verified. A 0.2 mm membrane closes the Ø18 ledge hole so no hole edge prints over air: cut it out afterwards. If the roof sags, use "supports on build plate only". |
| washer | groove up | 0.15 mm, 100 % infill. 1.3 mm under the heads, so use flat washers. |
| lid_* | standoffs up | 0.2 mm, 4 perimeters, elephant-foot compensation 0.2. lid_c's Qi recess roof is a 32 mm bridge. |
| dock_b | seat up | 0.2 mm, 15 % infill |
| riser_d | floor down | 0.2 mm, 3 perimeters |

Fit test first: in PrusaSlicer, cut `shell.stl` at z = 17 and print only the top ring. Check that the hub seats and that the body turns without touching.

FDM fits used: 0.3 mm general clearance, 0.6 mm radial at the body, 1.0 mm axial gap, magnets +0.15 (press fit) or +0.2 (glue), M3 into Ø2.5 pilots, M2 into Ø1.7.

## Assembly (a)

1. Drop the knob into the shell. The hub shoulder must sit on the ledge. The body must turn freely and click when pressed.
2. Cut the membrane out of the ledge hole. From below, put the washer (groove up) over the FPC and fit 3× M2.5×6 with flat washers into the hub.
3. Screw the adapter (components up) to the lid standoffs with 4× M2×4. The USB-C goes on the side of the lid without a screw hole (+x).
4. Hold the lid next to the shell and connect the FPC to J1. Lift the lid into the shell, folding the slack into the service loop toward +y. If the contacts face the wrong way, turn the knob and washer 180° in the socket.
5. Fit 3× M3×8 through the lid and stick on the bumpers.

## Measure before the final print

| Parameter | What |
|---|---|
| `body_d` | Body OD, largest of 3 readings (drawing 51.7 ± 0.5) |
| `hub1_h`, `hub2_h` (EST), `hub3_h` | Hub step lengths; these set the 1.0 mm gap under the body |
| `hub_tab_d` (EST) | OD across the hub latch tabs |
| `pin_h`, `pin_pcd`, `scr_pcd` (EST) | Pillar height and circle, screw circle and hole depth |
| `ad_w`, `ad_d`, `ad_holes`, `usb_y`, `usb_zc` (EST) | Adapter size, hole centres, USB-C position, top- or mid-mount receptacle |
| FPC | Real length; contact face relative to J1 |
| `plug_w` | Your USB-C plug overmold must be under 13 × 11.4 mm |
| `pogo_l`, `pogo_w` (EST) | (b) connector body |
| also | Press travel (must be under `ax_gap` = 1.0) |
