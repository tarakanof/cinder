# Hardware expansion: speaker and sensors

Status: modules bought 2026-10-30 (MAX98357A amp, 3 W speaker, HLK-LD2410 radar, ENS160+AHT21, AHT20+BMP280, BH1750, PCF8574 expander), plus breadboard, jumper wires and pin headers; nothing wired yet. Firmware: drivers and a hardware test image ([`hardware-test.md`](hardware-test.md)); the app does not use them yet. Prices are rough Temu/AliExpress figures from 2026-10-09 (RSD); sizes are typical listing values, measure before designing the enclosure.

## What the knob exposes

The knob has no audio, sensors or expansion header. Everything goes through the adapter board's 10-pin 2.54 mm header J2 (unpopulated; solder a header). Pin facts: [`board.md`](board.md) (10-pin FPC and J2 tables).

| J2 pin | Signal | Use |
|---|---|---|
| 1 | 5V | Power for every module |
| 3 | GND | Common ground |
| 6, 7 | GPIO44 / GPIO43 (UART0 RX/TX) | Free: flashing and logs use native USB |
| 2, 4, 5 | Probably GPIO38 / 40 / 39 | Unverified, the vendor sources disagree |
| 8, 9, 10 | EN, USB D+, D− | Leave alone |

That is 5 GPIOs, 3 of them to be verified first: the test image ([`hardware-test.md`](hardware-test.md)) holds one candidate GPIO high at a time and you measure each J2 pin against GND with a multimeter (about 3.3 V marks the pin). No LED needed.

## Plan

| Pins | Use |
|---|---|
| 3 | MAX98357A I2S amp (BCLK, LRC, DIN) |
| 2 | I2C bus: ENS160+AHT21, BH1750, PCF8574 |
| via PCF8574 P0 | LD2410 radar OUT (presence high/low) |

I2C addresses don't clash: AHT21 0x38, ENS160 0x53 (ADD high; 0x52 low), BH1750 0x23 (ADDR low), PCF8574 0x20 (A0-A2 low). A board with a PCF8574**A** chip answers at 0x38-0x3F instead and clashes with the AHT21 at the default: read the chip marking, and if it is the A part set A0-A2 to anything but all low (0x39-0x3F).

The radar is polled over I2C (a few reads a second); the PCF8574's INT pin stays unconnected, since it would need a GPIO of its own.

The AHT20+BMP280 board stays off the bus: the AHT20 and the AHT21 both sit at fixed 0x38, and there is no spare pin pair for a second bus. It only adds pressure (BMP280 0x76 or 0x77), which a desk display barely needs. To add it anyway: lift the AHT20 off its board, or put a TCA9548A I2C mux (0x70) in front of one of the two boards.

Uses: sounds and TTS clips from Ember (amp), "air is stale" alerts and room temperature/humidity (ENS160+AHT21; the AHT21 readings also feed the ENS160's compensation), panel off when you leave and on when you sit down (radar), auto-dim (BH1750).

## Pin budget

Six signals (I2S 3, I2C 2, radar 1) against at most five J2 GPIOs, so the radar goes through the PCF8574. Reading the radar over UART instead still needs one pin. Without an expander, one module has to go.

GPIO38 is labelled TE on the schematic, but TE is not routed to the ESP: with TE on, no free GPIO (38 included) toggles ([`features.md`](features.md), "No tear sync possible"). So GPIO38 is a plain GPIO.

| Usable J2 GPIOs after the test | Fit |
|---|---|
| 5 | Plan as above |
| 4 | I2C (sensors, radar via PCF8574) plus one pin of PWM audio into an analog amp (PAM8302); the MAX98357A needs 3 pins and drops out |
| 2 (43/44 only) | I2C only: sensors and radar, no speaker |

Put I2C on GPIO43/44, SCL on GPIO43: the ROM bootloader prints on GPIO43 (U0TXD) at every reset, and silencing it burns an eFuse. Toggling SCL while SDA stays high makes no START condition, so the sensors ignore it; on the amp it could pop.

## Voltage rules

- J2 has 5V only, no 3.3 V. Buy modules marked 3.3-5V or with an on-board regulator, and power them from 5V.
- ESP32-S3 pins are not 5V-tolerant. Before wiring a module's SDA/SCL/OUT to a GPIO, power it and measure those pins to GND: about 3.3 V is fine, about 5 V means don't connect.
- The PCF8574 board pulls SDA/SCL up to its VCC, and at 5V its input threshold (0.7×VCC = 3.5 V) is above what a 3.3 V GPIO drives: run it from 3.3 V. Power it from the ENS160+AHT21 board's 3V3 pin (measure 3.3 V there first).
- Each module adds its own SDA/SCL pull-ups (typically 4.7-10 kΩ). Three or four in parallel is still fine for 100-400 kHz.

## Parts: prototype

Bought 2026-10-30:

| Item | Bought | Check on arrival |
|---|---|---|
| Speaker amp | D-FLIFE MAX98357A I2S | Output is a screw terminal or pads, not a JST socket |
| Speaker | 3 W, JST PH2.0 plug | Impedance: the listing said "0 Ohm", so read the label or measure DC resistance (~3.2 Ω means 4 Ω, ~6.5 Ω means 8 Ω). Cut the PH2.0 plug and strip into the amp's terminal, or fit a PH2.0 pigtail socket |
| Presence radar | HLK-LD2410 | Variant: the plain LD2410 has a 1.27 mm header (often shipped with a cable; the LD2410B probably too), the LD2410C 2.54 mm. Pin order differs: LD2410C TX/RX/OUT/GND/VCC, LD2410 OUT/TX/RX/GND/VCC; wire by the silkscreen. VCC 5V (≥200 mA supply, 79 mA average), OUT is 3.3 V logic |
| Air quality + temp + humidity | ENS160+AHT21 | Blue board with two chips, pins VIN/3V3/GND/SCL/SDA/ADD/CS/INT (the purple single-chip board is ENS160 only). Measure SDA/SCL and 3V3 to GND on 5V (expect 3.3 V) |
| Temp + humidity + pressure | AHT20+BMP280 | Spare: clashes with the AHT21 at 0x38 (see Plan) |
| I/O expander | PCF8574 8-channel module | Chip marking PCF8574 or PCF8574A (address, see Plan); A0-A2 jumpers; power from the ENS160 board's 3V3 |
| Light sensor | BH1750 | Same SDA/SCL check; ADDR low = 0x23 |

Still to buy:

| Item | Search term | Check | ~RSD |
|---|---|---|---|
| Bulk capacitor | 470uF 10V electrolytic | Across 5V/GND at the amp | <50 |

Power: the knob draws ≤150 mA and the radar about 80 mA; the amp into 4 Ω at 5V peaks near 3 W, about 0.8 A, which a 500 mA USB port can't supply (expect resets on loud sounds). Leave the amp's GAIN pin at its 9 dB default, cap the volume in firmware and fit the bulk capacitor; measure the draw before raising the cap. 8 Ω halves the peak.

Radar notes: a moving speaker cone reads as motion, so don't put the radar behind the speaker grille or aim it at the speaker. Default UART is 256000 baud if the distance/energy data is ever wanted; presence alone only needs OUT.

ENS160 notes: "CO2" in listings is eCO2, an estimate from VOCs, not measured CO2 (that needs an SCD40/SCD41). About 1 h burn-in on first power-up, a few minutes of warm-up per boot. Keep it away from the amp and the adapter, or the temperature reads high.

BH1750 notes: the AMOLED lights the sensor itself. Put it in a side or rear wall window facing the room, not next to the display, and compare readings with the panel on and off.

## Parts: tools

| Item | Search term | Note |
|---|---|---|
| Soldering iron | soldering iron kit 60W adjustable temperature | Kits include stand and tips |
| Solder | solder wire 0.8mm rosin core 63/37 | Kit solder is often poor |
| Flux | flux pen | |
| Flush cutters | flush cutter 170 | Trims leads; also strips 28 AWG silicone (nick and pull) |
| Multimeter | | Already owned |

Strippers: rotary/coax strippers and electrician pliers with mm notches (smallest 0.8 mm ≈ 20 AWG) can't strip 28 AWG (0.32 mm copper). Fine-wire strippers (20-30 AWG, e.g. Klein 11057, Engineer PA-14) exist but are rarely on Temu; flush cutters are enough for silicone wire.

## Parts: final build

Buy after the breadboard prototype works.

| Item | Search term | Note |
|---|---|---|
| Perfboard | double sided prototype PCB 5x7cm | Cut to the adapter's 42×37 footprint |
| Wire | silicone wire 28AWG | Small kits are enough; 10×10 m kits include heat shrink |
| Standoffs | M2 nylon standoff kit | Stack layers through corner holes |
| Female headers | 2.54mm female header 40pin | Layers and modules plug in and out |
| Heat shrink | heat shrink tube kit | Skip if the wire kit has it |

## Enclosure

The current puck ([`enclosure.html`](enclosure.html), Ø66 × 29 mm, 43×43 mm cavity) holds the adapter only.

| Part | Size, mm (typical) |
|---|---|
| Adapter | 42×37 |
| MAX98357A | ~18×18 |
| ENS160+AHT21 | ~24×19 |
| BH1750 | ~19×14 (measure) |
| LD2410 | ~35×7 (LD2410/B) or ~22×16 (LD2410C) |
| PCF8574 | ~42×16 |
| Speaker | measure |

Plan: stack under the adapter (perfboard layer with amp and PCF8574 on M2 standoffs, about 8 mm per layer), so the puck grows about 15-20 mm taller, not wider. Wall-mount the rest: radar on the front wall facing the user (plastic only, no metal in front), ENS160+AHT21 at vent slots away from heat (amp, adapter, regulator), BH1750 behind a window away from the display, speaker on the bottom or a side wall behind a grille. Use plug-in headers between layers so the stack comes apart. If space is short, drop the PCF8574 (it only serves the radar).
