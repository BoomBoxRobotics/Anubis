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
fabrication/jlcpcb_2026-07-10_r13.zip
```

Assembly files are included under:

```text
fabrication/jlcpcb_2026-07-10_r13/assembly/
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
