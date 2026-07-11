# Anubis RC Controller Daughterboard

**A compact open-hardware support board for a DIY ESP32-S3 RC transmitter.**

Anubis is an open hardware and open firmware RC controller project built around accessible DIY parts, modern radio-control features, and a design that builders can understand, repair, modify, and improve.

This repository contains the custom daughterboard for the Anubis controller. It sits under a Hosyond 2.8 inch ESP32-S3 display board and turns the early wiring-heavy prototype into a cleaner, more repeatable hardware platform.

## Mission

The goal is simple: make a capable, repairable, hackable transmitter that is not locked behind a black box.

The first Anubis prototype used off-the-shelf modules wired together by hand: display board, power switches, regulators, analog input boards, GPIO expanders, gimbal connectors, and radio wiring. That is a great way to prove an idea, but it gets messy fast. This daughterboard pulls those support circuits onto a purpose-built PCB while leaving the Hosyond ESP32-S3 board as the main brain, display, touch interface, and firmware target.

Anubis is meant for experimentation as much as use. Builders should be able to change pin mappings, adapt connectors, swap radio hardware, and tune the firmware for their own transmitter layout.

## At A Glance

| Area | Current Direction |
| --- | --- |
| Main controller | Hosyond 2.8 inch ESP32-S3 display/dev board |
| Battery system | 2S LiPo or two 18650 cells as a 2S pack |
| Charging input | USB-C 5 V input, 500 mA to 1.5 A charge target |
| Switched outputs | `+5V_SW` and `+3V3_SW` accessory rails |
| Analog inputs | Two onboard ADS1115 ADCs, up to 8 analog channels |
| GPIO expansion | MCP23017 16-bit I2C GPIO expander |
| Radio paths | ELRS/CRSF over UART and optional ESP-NOW |
| Assembly goal | As much JLCPCB assembly as practical |
| Project status | Prototype hardware, ready for assembled-board validation |

## Core Design Goals

- Use common, hobby-accessible parts where possible.
- Keep the project open enough for community modification.
- Support modern RC workflows such as ELRS and ESP-NOW.
- Reduce hand wiring compared with the early prototype.
- Make the board practical for JLCPCB assembly.
- Keep external accessory power switchable from firmware.
- Leave room for manual wiring where real-world modules and harnesses vary.

## Hardware Overview

The daughterboard adds the support electronics around the Hosyond ESP32-S3 board:

- USB-C 5 V input for charging and power input.
- Standard 2S LiPo balance connector for the battery pack.
- Support for a 2S LiPo pack or two 18650 cells configured as a 2S pack.
- 2S battery gauge/protection circuitry based around `BQ28Z610DRZR-R1`.
- R14 high-side protection path using two JLC-available `CSD18502Q5B` N-FETs, with the charger and buck regulators fed from protected `BAT_SYS`.
- Switched 5 V rail for UART/ELRS-side accessories.
- Switched 3.3 V rail for I2C, analog, and low-voltage peripherals.
- Two Pololu mini pushbutton power switch modules for rail control.
- Two onboard ADS1115 ADC circuits for up to eight analog inputs.
- One onboard MCP23017 GPIO expander for buttons and digital expansion.
- JST and pin-header breakouts for UART, I2C, GPIO, gimbals, and accessories.
- ESD protection for external signal connections.
- Ground plane and wider routing rules for higher-current power paths.

## Power Rails

| Rail | Intended Use | Approximate Max Current |
| --- | --- | --- |
| USB-C input | 5 V charge/input source | 500 mA to 1.5 A charge target |
| `+5V_SW` | ELRS/UART-side accessory power | about 1 A |
| `+3V3_SW` | I2C, ADCs, MCP23017, gimbals, sensors | about 500 mA |

The switched rails are controlled through the two Pololu #2808 mini pushbutton power switch modules. The ESP32-S3 can shut down attached devices without relying on the attached devices to behave nicely.

## Firmware Map

The main Anubis firmware runs on the Hosyond ESP32-S3 board. Current daughterboard-aware firmware support includes:

- Touchscreen UI for transmitter settings.
- Model storage and configuration.
- Stick calibration.
- Expo, rates, trims, endpoints, failsafe, and mixing.
- ELRS/CRSF support over UART.
- ESP-NOW transmitter support.
- Dual ADS1115 support for onboard analog inputs at `0x48` and `0x49`.
- MCP23017 GPIO expander support at `0x20`.
- Battery monitoring and deep-sleep behavior.

### Pin Assignments

| Function | ESP32-S3 Pin |
| --- | --- |
| I2C SDA | GPIO16 |
| I2C SCL | GPIO15 |
| ELRS/UART TX | GPIO44 |
| ELRS/UART RX | GPIO43 |
| Battery ADC | GPIO9 |
| 5 V rail switch control | GPIO2 |
| 3.3 V rail switch control | GPIO3 |
| Deep-sleep/wake button sense | GPIO14 |
| Deep-sleep/wake button reference | GPIO21 |

GPIO2 and GPIO3 control the two Pololu rail-switch modules. GPIO14 and GPIO21 route to the two-pin deep-sleep/wake pushbutton header. UART-to-ELRS wiring can still be crossed manually if a specific module harness expects the opposite order.

### I2C Devices

| Device | Address | Role |
| --- | --- | --- |
| ADS1115 primary | `0x48` | Four gimbal/stick analog axes |
| ADS1115 auxiliary | `0x49` | Spare analog inputs |
| MCP23017 | `0x20` | GPIO expansion and active-low button/switch inputs |
| BQ25887 charger | `0x6B` | 2S charger status and pack voltage reporting |

## Board Connection Reference

The following tables describe the designators and nets on the R14 PCB. Pin numbers are the KiCad footprint pin numbers and should be checked against connector orientation and pin-1 markings before making a cable.

### Electrical Test Pads

| Pad | Net | Signal source or intended measurement |
| --- | --- | --- |
| `TP1` | `USB_VBUS` | USB-C `J1` VBUS, before input fuse `F1` |
| `TP2` | `CHG_IN` | Fused USB input after `F1`; charger `U1` pin 23 |
| `TP3` | `BAT_SYS` | Protected battery/system bus feeding charger `U1` BAT pins and the 5 V/3.3 V regulators |
| `TP4` | `CELL_MID` | 2S pack midpoint from `J7` pin 2; charger `U1` pin 9 sense connection |
| `TP5` | `BAT_SYS` | Second protected battery/system-bus test point; electrically the same as `TP3` |
| `TP6` | `+5V` | Unswitched 5 V regulator output after `L1`, before Pololu switch `SW1` |
| `TP7` | `+3V3` | Unswitched 3.3 V regulator output after `L2`, before Pololu switch `SW2` |
| `TP9` | `+3V3_SW` | Switched 3.3 V output from `SW2` |
| `TP10` | `CHG_STAT` | Charger `U1` pin 2 charge-status output; also drives status LED `D2` |
| `TP11` | `PG_STAT` | Charger `U1` pin 1 power-good output; also drives status LED `D3` |
| `TP12` | `BATT_RAW_N` | Raw pack negative from `J7` pin 1, on the battery side of current-sense resistor `R46` |
| `TP13` | `BATT_RAW_P` | Raw pack positive from `J7` pin 3 |
| `TP14` | `GND` | System ground, on the system side of current-sense resistor `R46` |
| `TP15` | `AIN4` | Auxiliary ADC `U6` (`0x49`) AIN0, pin 4 |
| `TP16` | `AIN5` | Auxiliary ADC `U6` (`0x49`) AIN1, pin 5 |
| `TP17` | `AIN6` | Auxiliary ADC `U6` (`0x49`) AIN2, pin 6 |
| `TP18` | `AIN7` | Auxiliary ADC `U6` (`0x49`) AIN3, pin 7 |

`TP8` is not fitted on the R14 PCB. Use `J30` or `J4` pin 1 to access `+5V_SW`. Also note that `BATT_RAW_N` and `GND` are separated by the 2 mOhm current-sense resistor; do not treat `TP12` and `TP14` as interchangeable measurement points.

### Hosyond Manual Solder Pads

These pads replace the original `J3` connector so individual wires from the Hosyond board can be soldered directly to the daughterboard.

| Pad | Net | Hosyond/source mapping |
| --- | --- | --- |
| `J30` | `+5V_SW` | Switched 5 V output from `SW1`; also available at `J4` pin 1 |
| `J31` | `UART_TX` | Hosyond ESP32-S3 GPIO44; routed to `J4` pin 3 |
| `J32` | `UART_RX` | Hosyond ESP32-S3 GPIO43; routed to `J4` pin 4 |
| `J33` | `I2C_SDA` | Hosyond ESP32-S3 GPIO16; shared daughterboard I2C data bus |
| `J34` | `I2C_SCL` | Hosyond ESP32-S3 GPIO15; shared daughterboard I2C clock bus |
| `J35` | `GPIO2` | Hosyond ESP32-S3 GPIO2; drives `SW1` 5 V control through `R17` |
| `J36` | `GPIO3` | Hosyond ESP32-S3 GPIO3; drives `SW2` 3.3 V control through `R18` |
| `J37` | `GPIO14` | Hosyond ESP32-S3 GPIO14; routed to wake-button header `J6` pin 1 |
| `J38` | `GPIO21` | Hosyond ESP32-S3 GPIO21; routed to wake-button header `J6` pin 2 |
| `J39` | `GND` | Daughterboard system ground |

### MCP23017 Manual Solder Pads

The `EXP_*` GPIO pads are on the protected external side of a 100 ohm series resistor and an ESD array. `U7` is the MCP23017 at I2C address `0x20`.

| Pad | Net | Source |
| --- | --- | --- |
| `J91` | `EXP_GPA0` | `U7` GPA0 pin 21 through `R23`; ESD `D4` |
| `J92` | `EXP_GPA1` | `U7` GPA1 pin 22 through `R24`; ESD `D4` |
| `J93` | `EXP_GPA2` | `U7` GPA2 pin 23 through `R25`; ESD `D4` |
| `J94` | `EXP_GPA3` | `U7` GPA3 pin 24 through `R26`; ESD `D4` |
| `J95` | `EXP_GPA4` | `U7` GPA4 pin 25 through `R27`; ESD `D5` |
| `J96` | `EXP_GPA5` | `U7` GPA5 pin 26 through `R28`; ESD `D5` |
| `J97` | `EXP_GPA6` | `U7` GPA6 pin 27 through `R29`; ESD `D5` |
| `J98` | `EXP_GPA7` | `U7` GPA7 pin 28 through `R30`; ESD `D5` |
| `J99` | `EXP_GPB0` | `U7` GPB0 pin 1 through `R31`; ESD `D6` |
| `J910` | `EXP_GPB1` | `U7` GPB1 pin 2 through `R32`; ESD `D6` |
| `J911` | `EXP_GPB2` | `U7` GPB2 pin 3 through `R33`; ESD `D6` |
| `J912` | `EXP_GPB3` | `U7` GPB3 pin 4 through `R34`; ESD `D6` |
| `J913` | `EXP_GPB4` | `U7` GPB4 pin 5 through `R35`; ESD `D7` |
| `J914` | `EXP_GPB5` | `U7` GPB5 pin 6 through `R36`; ESD `D7` |
| `J915` | `EXP_GPB6` | `U7` GPB6 pin 7 through `R37`; ESD `D7` |
| `J916` | `EXP_GPB7` | `U7` GPB7 pin 8 through `R38`; ESD `D7` |
| `J925` | `MCP_INTA` | `U7` INTA pin 20 with pull-up `R43` |
| `J926` | `MCP_INTB` | `U7` INTB pin 19 with pull-up `R44` |
| `J927` | `MCP_RESET` | `U7` RESET pin 18 with pull-up `R42` and capacitor `C29` |
| `J928` | `+3V3_SW_LOGIC` | Switched 3.3 V logic rail after isolation link `R45` |
| `J929` | `GND` | Daughterboard system ground |

### Connector Pinouts

| Connector | Purpose | Pinout |
| --- | --- | --- |
| `J1` | USB-C 5 V input | A5 = `CC1`; B5 = `CC2`; A9/B9 = `USB_VBUS`; A12/B12 = `GND`; shield tabs = `GND` |
| `J4` | 5-pin UART JST-GH, top-entry, 1.25 mm | 1 = `+5V_SW`; 2 = `GND`; 3 = `UART_TX`; 4 = `UART_RX`; 5 = `GND` |
| `J5` | 4-pin I2C through-hole header, 2.54 mm | 1 = `+3V3_SW`; 2 = `GND`; 3 = `I2C_SDA`; 4 = `I2C_SCL` |
| `J6` | 2-pin deep-sleep/wake button header, 2.54 mm | 1 = `GPIO14`; 2 = `GPIO21`; a momentary button connects the two pins |
| `J7` | 2S balance input, side-entry JST-XH, 2.50 mm | 1 = `BATT_RAW_N`; 2 = `CELL_MID`; 3 = `BATT_RAW_P` |
| `J8` | Left gimbal A, 3-pin JST-GH, 1.25 mm | 1 = `GND`; 2 = `AIN0`; 3 = `+3V3_SW` |
| `J40` | Left gimbal B, 3-pin JST-GH, 1.25 mm | 1 = `GND`; 2 = `AIN1`; 3 = `+3V3_SW` |
| `J41` | Right gimbal A, 3-pin JST-GH, 1.25 mm | 1 = `GND`; 2 = `AIN2`; 3 = `+3V3_SW` |
| `J42` | Right gimbal B, 3-pin JST-GH, 1.25 mm | 1 = `GND`; 2 = `AIN3`; 3 = `+3V3_SW` |

`J2` is a schematic-only switched-power-output symbol and has no footprint on the R14 PCB. `J3` is not a physical connector; its signals are implemented as solder pads `J30` through `J39`. JST-GH mounting pads marked `MP` are mechanical and have no electrical connection.

### Pololu Power-Switch Module Headers

| Header | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
| --- | --- | --- | --- | --- |
| `SW1` Pololu #2808 5 V switch | `+5V` input | `+5V_SW` output | `GND` | `CTRL_5V_SW` |
| `SW2` Pololu #2808 3.3 V switch | `+3V3` input | `+3V3_SW` output | `GND` | `CTRL_3V3_SW` |

## Control Hardware

The controller is designed around parts that are easy for hobby builders to source or substitute:

- Hosyond 2.8 inch ESP32-S3 display board as the main controller.
- RadioMaster Pocket/Zorro X5-style gimbals.
- ELRS module support through UART/CRSF.
- Optional ESP-NOW receiver support for WiFi-equipped ESP32 receivers.
- D-pad/button input through the MCP23017 GPIO expander.
- Auxiliary analog inputs through the ADS1115 circuits.

The mechanical layout fits around the Hosyond board and controller shell constraints. The Hosyond board sits above the daughterboard on standoffs, with the daughterboard occupying the surrounding and lower internal space.

## Repository Contents

| Path | Purpose |
| --- | --- |
| `Daughterboard.kicad_pro` | KiCad project file |
| `Daughterboard.kicad_sch` | Main schematic |
| `Daughterboard.kicad_pcb` | PCB layout |
| `Daughterboard.kicad_sym` | Project symbols |
| `Daughterboard.pretty/` | Custom KiCad footprints |
| `Daughterboard.net` | Exported netlist |
| `Daughterboard_pcb_bom.csv` | Human-readable PCB BOM |
| `Daughterboard_current_jlc_part_audit.csv` | JLC part audit/reference |
| `fabrication/` | Fabrication and assembly outputs |
| `daughterboard2.dxf` | Board outline reference |
| `Daughterboardv2.step` | Mechanical board reference |
| `PROJECT_CONTEXT.md` | Detailed working context for future development |

Current fabrication package:

```text
fabrication/jlcpcb_2026-07-10_r14.zip
```

Assembly files are included under:

```text
fabrication/jlcpcb_2026-07-10_r14/assembly/
```

## Manufacturing Notes

The board is being prepared for JLCPCB assembly with as many populated parts as practical. The Pololu mini pushbutton power switch modules are treated as separately installed modules. Some through-hole headers may also be hand-installed unless through-hole assembly is explicitly ordered.

Before ordering boards, always check:

- KiCad ERC and DRC reports.
- Copper clearance and board-edge clearance.
- Unrouted nets.
- Connector orientation.
- IC pin 1 orientation.
- JLCPCB part availability.
- BOM and CPL alignment in the JLCPCB preview.

Known current status from the active R14 working files:

- ERC reports no errors or warnings.
- Q1/Q2 are now JLC-available `CSD18502Q5B` FETs using project-local TI DNK/VSON-CLIP footprints.
- The R14 topology audit passes: USB-C ground, charger/system bus, high-side protection FETs, current-sense resistor, and SRP/SRN polarity all match the corrected battery-protection intent.
- The PCB has been rerouted after the Q1/Q2 footprint swap and the GND zone has been refilled.
- DRC currently reports 40 known non-connectivity items and 0 unconnected pads/items.
- JLC assembly still needs confirmed selections for `R49` and `R50`.
- Silkscreen warnings should not normally prevent fabrication.
- Copper, drill, outline, missing-net, and footprint-orientation issues should be treated as critical.

## Battery And Power Safety

This board works with lithium battery packs and charging/protection circuitry. Mistakes can damage hardware or create a safety hazard.

Do not assume a PCB revision is safe just because the schematic opens or the Gerbers generate. Review the battery path, charger configuration, protection FETs, current limits, connector polarity, and pack wiring before connecting real cells.

Use protected cells or a known-good 2S pack during testing. Current-limit the first power-up and verify rails with a meter before plugging in the Hosyond board or radio hardware.

## Project Status

This hardware is still a prototype until assembled boards are tested.

Open work includes:

- Validate the physical PCB after manufacturing.
- Confirm all connector orientations in the assembled board.
- Test the updated firmware GPIO assignments against the first assembled daughterboard.
- Verify 2S battery reporting and charger/fuel-gauge behavior on hardware.
- Confirm the auxiliary ADS1115 inputs and MCP23017 pads behave as expected with real devices attached.
- Test ELRS and ESP-NOW behavior in the finished controller.
- Document assembly steps after the first successful build.

## Contributing

Anubis is meant to be community driven. Contributions are welcome in the form of firmware changes, PCB review, mechanical improvements, documentation, testing notes, alternate part suggestions, and build reports.

If you change the hardware, include enough context for another builder to understand why the change was made. If you change the firmware, document the pin assumptions and hardware revision you tested against.

## Companion Project

The firmware and receiver-side code for the broader Anubis controller project are hosted with the main project:

```text
https://github.com/BoomBoxRobotics/Anubis
```

This daughterboard is one hardware path toward that larger goal: a capable open source RC controller that builders can actually understand, repair, and make their own.
