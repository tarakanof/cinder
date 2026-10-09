# Hardware expansion: speaker and sensors

Status: parts list only. Nothing is bought, wired or supported in firmware yet. Prices are rough Temu/AliExpress figures from 2026-10-09 (RSD); sizes are typical listing values, measure before designing the enclosure.

## What the knob exposes

The knob has no audio, sensors or expansion header. Everything goes through the adapter board's 10-pin 2.54 mm header J2 (unpopulated; solder a header). Pin facts: [`board.md`](board.md) (10-pin FPC and J2 tables).

| J2 pin | Signal | Use |
|---|---|---|
| 1 | 5V | Power for every module |
| 3 | GND | Common ground |
| 6, 7 | GPIO44 / GPIO43 (UART0 RX/TX) | Free: flashing and logs use native USB |
| 2, 4, 5 | Probably GPIO38 / 40 / 39 | Unverified, the vendor sources disagree |
| 8, 9, 10 | EN, USB D+, D− | Leave alone |

That is 5 GPIOs, 3 of them to be verified first: flash a test build that holds each GPIO high for 5 s in turn and measure each J2 pin against GND with a multimeter (about 3.3 V marks the pin). No LED needed.

## Plan

| Pins | Use |
|---|---|
| 3 | MAX98357A I2S amp (BCLK, LRC, DIN) |
| 2 | I2C bus: ENS160+AHT21, PCF8574, optional BH1750 |
| via PCF8574 P0 | LD2410C radar OUT (presence high/low) |

I2C addresses don't clash: AHT21 0x38, ENS160 0x53, BH1750 0x23, PCF8574 0x20.

Uses: sounds and TTS clips from Ember (amp), "air is stale" alerts and room temperature/humidity (ENS160+AHT21), panel off when you leave and on when you sit down (radar), auto-dim (BH1750).

## Voltage rules

- J2 has 5V only, no 3.3 V. Buy modules marked 3.3-5V or with an on-board regulator, and power them from 5V.
- ESP32-S3 pins are not 5V-tolerant. Before wiring a module's SDA/SCL/OUT to a GPIO, power it and measure those pins to GND: about 3.3 V is fine, about 5 V means don't connect.
- The PCF8574 board pulls SDA/SCL up to its VCC: power it from the ENS160+AHT21 board's 3V3 pin (measure 3.3 V there first), not 5V.

## Parts: prototype

| Item | Search term | Check | ~RSD |
|---|---|---|---|
| Speaker amp | MAX98357A I2S amplifier | D-FLIFE (cheapest, most sold) or the one with pre-soldered yellow header | 265-322 |
| Speaker | 8 ohm 3W cavity speaker | Boxed (fuller sound), bare wires to the amp's screw terminal; about 1.4 W into 8 Ω at 5V | ~200 |
| Air quality + temp + humidity | ENS160 AHT21 | Blue board, two sensor chips (silver ENS160 + AHT21 in a white ring), pins VIN/3V3/GND/SCL/SDA/ADD/CS/INT. The purple single-chip board is ENS160 only: avoid | 500-1,021 (AliExpress) |
| Presence radar | HLK-LD2410C | HLK silkscreen, pins VCC/GND/OUT/RX/TX; VCC on 5V | 300-500 |
| I/O expander | PCF8574 IO expansion module | Blue board, J3 in / J5 chain out, P0-P7 side header, address jumpers A0-A2 at default | 150-550 (AliExpress) |
| Breadboard | 400 point breadboard | Half size; power rails share 5V/GND | 100-200 |
| Jumper wires | dupont jumper wire 3 types 20cm | F-F, F-M, M-M | 150-250 |
| Pin headers | 2.54mm male pin header 40pin | Pack of 10, snap to length (J2 needs 10) | <100 |
| Optional: light sensor | BH1750 module | | ~100 |

ENS160 notes: "CO2" in listings is eCO2, an estimate from VOCs, not measured CO2 (that needs an SCD40/SCD41). About 1 h burn-in on first power-up, a few minutes of warm-up per boot. Keep it away from the amp and the adapter, or the temperature reads high.

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
| LD2410C | ~22×16 |
| PCF8574 | ~42×16 |
| Boxed speaker | ~30×30×15 plus tabs |

Plan: stack under the adapter (perfboard layer with amp and PCF8574 on M2 standoffs, about 8 mm per layer), so the puck grows about 15-20 mm taller, not wider. Wall-mount the rest: radar on the front wall facing the user (plastic only, no metal in front), ENS160 at vent slots away from heat, speaker on the bottom or a side wall behind a grille. Use plug-in headers between layers so the stack comes apart. If space is short, drop the PCF8574 (it only serves the radar).
