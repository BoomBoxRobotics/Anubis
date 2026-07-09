# Anubis RC Controller Daughterboard

Anubis is an open hardware and open firmware RC controller project built around accessible DIY parts, modern radio-control features, and a design that can be modified by the community instead of locked behind a black box.

This repository contains the custom daughterboard for the Anubis controller. The board is designed to sit under a Hosyond 2.8 inch ESP32-S3 display board and turn the wiring-heavy prototype into a cleaner, more repeatable hardware platform.

The goal is simple: make a capable, repairable, hackable transmitter that builders can assemble, understand, modify, and improve.

## Why This Board Exists

The first version of Anubis used off-the-shelf modules wired together by hand: display board, power switches, regulators, analog input boards, GPIO expanders, gimbal connectors, and radio wiring. That approach is great for proving an idea, but it gets messy quickly.

This daughterboard replaces that loose wiring with a purpose-built PCB. It provides battery power management, switched accessory rails, onboard analog and GPIO expansion, and connector breakouts for the controller hardware. The Hosyond ESP32-S3 board remains the main brain and user interface, while this board handles the support electronics around it.

Anubis is intended for experimentation as much as use. Builders should be able to change pin mappings, swap modules, adapt connectors, and tune the firmware for their own transmitter layout.

## Core Design Goals

- Use common, hobby-accessible parts where possible.
- Keep the project open enough for community modification.
- Support modern RC workflows such as ELRS and ESP-NOW.
- Reduce hand wiring compared with the early prototype.
- Make the board practical for JLCPCB assembly.
- Keep power to external accessories switchable so the controller can shut down attached devices cleanly.
- Leave room for manual wiring where real-world modules and harnesses vary.

## Current Hardware Overview

The daughterboard is designed around the Hosyond 2.8 inch ESP32-S3 display/dev board. The Hosyond board provides the ESP32-S3, TFT display, touch input, USB programming interface, audio hardware, battery ADC input, UART, I2C, and expansion GPIO.

The daughterboard adds:

- USB-C 5 V input for charging/power input.
- 2S battery connection through a standard 2S LiPo balance connector.
- Support for a 2S LiPo pack or two 18650 cells configured as a 2S pack.
- 2S battery gauge/protection circuitry based around `BQ28Z610DRZR-R1`.
- Dual-FET protection/power path using `CSD83325L`.
- Switched 5 V rail for UART/ELRS-side accessories.
- Switched 3.3 V rail for I2C, analog, and low-voltage peripherals.
- Two Pololu mini pushbutton power switch modules for rail control.
- Two onboard ADS1115 ADC circuits for up to eight analog inputs.
- One onboard MCP23017 GPIO expander for buttons and digital expansion.
- JST and pin-header breakouts for UART, I2C, GPIO, gimbals, and accessory wiring.
- ESD protection for external signal connections.
- Ground plane and wider routing rules for higher-current power paths.

The current rail targets are:

| Rail | Intended Use | Approximate Max Current |
| --- | --- | --- |
| USB-C input | 5 V charge/input source | 500 mA to 1.5 A charge target |
| `+5V_SW` | ELRS/UART-side accessory power | about 1 A |
| `+3V3_SW` | I2C, ADCs, MCP23017, gimbals, sensors | about 500 mA |

## Firmware Relationship

The daughterboard is part of the larger Anubis controller project. The main firmware lives in the companion Anubis firmware project and runs on the Hosyond ESP32-S3 board.

The firmware currently includes:

- Touchscreen UI for transmitter settings.
- Model storage and configuration.
- Stick calibration.
- Expo, rates, trims, endpoints, failsafe, and mixing.
- ELRS/CRSF support over UART.
- ESP-NOW transmitter support.
- ADS1115 analog stick input support.
- MCP23017/PCF8575-style button and accessory support.
- Battery monitoring and deep-sleep behavior.

Current important firmware pin expectations include:

| Function | ESP32-S3 Pin |
| --- | --- |
| I2C SDA | GPIO16 |
| I2C SCL | GPIO15 |
| ELRS/UART TX | GPIO44 |
| ELRS/UART RX | GPIO43 |
| Battery ADC | GPIO9 |

Some pin assignments are intentionally flexible. For example, GPIO2/GPIO3 and GPIO14/GPIO21 may be reassigned in software depending on how the physical daughterboard and front-panel controls are wired. Likewise, UART-to-ELRS wiring can be crossed manually if a specific module harness expects the opposite order.

## Radio and Control Hardware

The controller is being designed around parts that are easy for hobby builders to source or substitute:

- Hosyond 2.8 inch ESP32-S3 display board as the main controller.
- RadioMaster Pocket/Zorro X5-style gimbals.
- ELRS module support through UART/CRSF.
- Optional ESP-NOW receiver support for WiFi-equipped ESP32 receivers.
- D-pad/button input through the MCP23017 GPIO expander.
- Auxiliary analog inputs through the ADS1115 circuits.

The mechanical layout is designed to fit around the Hosyond board and controller shell constraints. The Hosyond board sits above the daughterboard on standoffs, with the daughterboard occupying the surrounding and lower internal space.

## Repository Contents

Important project files:

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
| `fabrication/` | Current fabrication and assembly outputs |
| `daughterboard2.dxf` | Board outline reference |
| `Daughterboardv2.step` | Mechanical board reference |
| `PROJECT_CONTEXT.md` | Detailed working context for future development |

Current fabrication package:

```text
fabrication/jlcpcb_2026-07-06_r12.zip
```

This package is intended for JLCPCB board fabrication. Assembly files are included under:

```text
fabrication/jlcpcb_2026-07-06_r12/assembly/
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

Known current status from the latest R12 fabrication pass:

- ERC reports no errors or warnings.
- DRC contains known/accepted clearance and silkscreen warnings.
- The known clearance items are around the battery-management IC package and are small differences from the configured rule.
- Silkscreen warnings should not normally prevent fabrication.
- Copper, drill, outline, missing-net, and footprint-orientation issues should be treated as critical.

## Battery and Power Safety

This board works with lithium battery packs and charging/protection circuitry. That means mistakes can damage hardware or create a safety hazard.

Do not assume a PCB revision is safe just because the schematic opens or the Gerbers generate. Review the battery path, charger configuration, protection FETs, current limits, connector polarity, and pack wiring before connecting real cells.

Use protected cells or a known-good 2S pack during testing, current-limit the first power-up, and verify rails with a meter before plugging in the Hosyond board or radio hardware.

## Project Status

This hardware is still a work in progress. The current board has been through several schematic, layout, footprint, BOM, and JLCPCB preparation passes, but it should still be treated as a prototype until assembled boards are tested.

Open work includes:

- Validate the physical PCB after manufacturing.
- Confirm all connector orientations in the assembled board.
- Update firmware for the final GPIO assignments.
- Update firmware battery handling for the 2S power system.
- Add or refine support for the second ADS1115 if all eight analog inputs are used.
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
