# Project Context - Hosyond ESP32-S3 Daughterboard

Last updated: 2026-07-10

## Project Goal

This project is a custom daughterboard/carrier PCB for a Hosyond 2.8 inch ESP32-S3 display/dev board. The daughterboard replaces loose wiring with a compact board that provides battery charging/protection, switched 5 V and 3.3 V rails, UART/I2C/GPIO breakouts, onboard analog/digital expanders, and connectors for radio-control hardware such as ELRS, gimbals, D-pad/buttons, and auxiliary inputs.

The daughterboard should be manufactured and assembled offsite by JLCPCB as completely as possible. The main exception is the Pololu mini pushbutton power switch modules, which are installed separately on raised headers. Most passive parts, ICs, and connectors should be SMD/JLC-available where practical. Through-hole/user-installed headers are acceptable where the design intentionally needs them.

## Workspaces

Current daughterboard hardware workspace:

```text
C:\Users\Avala\OneDrive\Desktop\Daughterboard
```

Previous/main controller firmware workspace:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a
```

The previous project is about the main Hosyond ESP32-S3 board, its firmware, UI, protocol behavior, and controller functionality. A current copy of the daughterboard folder has also been placed inside that project at:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\Daughterboard
```

Older carrier-board planning docs from the previous project are under:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\project_anubis\Daughterboard
```

Those older docs are useful historical context, but they describe an earlier module-carrier version using a protected 1S pack and PCF8575/ADS1115 modules. The current design has moved to a 2S battery architecture and more onboard components.

## Current Daughterboard State

Primary KiCad files:

```text
Daughterboard.kicad_pro
Daughterboard.kicad_sch
Daughterboard.kicad_pcb
Daughterboard.kicad_sym
Daughterboard.net
Daughterboard.pretty\
```

Important custom footprints:

```text
Daughterboard.pretty\BQ28Z610_DRZ0012A_12SON_2.5x4mm_P0.5mm.kicad_mod
Daughterboard.pretty\Pololu_2808_Raised_Header.kicad_mod
Daughterboard.pretty\SOIC-28W_7.5x17.9mm_P1.27mm_NoPads11_14.kicad_mod
Daughterboard.pretty\SOT-23-6_NoPad6.kicad_mod
```

Current fabrication package:

```text
fabrication\jlcpcb_2026-07-10_r13.zip
fabrication\jlcpcb_2026-07-10_r13\
```

R13 assembly files:

```text
fabrication\jlcpcb_2026-07-10_r13\assembly\Daughterboard_jlcpcb_bom_template_filled.csv
fabrication\jlcpcb_2026-07-10_r13\assembly\Daughterboard_jlcpcb_bom_template_filled.xlsx
fabrication\jlcpcb_2026-07-10_r13\assembly\Daughterboard_jlcpcb_cpl.csv
fabrication\jlcpcb_2026-07-10_r13\Daughterboard_jlcpcb_gerber_drill.zip
```

J5 and J6 are intentionally through-hole/user-installed headers and are excluded from the JLC SMT BOM/CPL unless JLC through-hole assembly is explicitly used.

R13 topology update:

- `Daughterboard.kicad_sch`, `Daughterboard.net`, and `Daughterboard_pcb_bom.csv` now describe the corrected high-side BQ28Z610 protection topology.
- `Daughterboard.kicad_pcb` has been updated for R13 with the old single Q1 low-side footprint replaced by Q1/Q2 `CSD16412Q5A`, and R49/R50/C32/C33/TP13 added.
- The PCB has been rerouted after the R13 topology change.
- Backup before the R13 PCB staging is `Daughterboard.pre_r13_topology_backup.kicad_pcb`.

## Power Architecture

The daughterboard is intended to run from installed batteries. The current design supports a 2S battery connection through a standard 2S LiPo balance lead, so users can use either a 2S LiPo pack or two 18650 cells configured as a 2S pack.

Original charging target:

- USB-C input: 5 V
- Charge current: 500 mA minimum target, 1.5 A maximum
- 5 V switched rail: about 1 A max
- 3.3 V switched rail: about 500 mA max

Current direction:

- 2S battery management/protection moved away from the unavailable `S-8252AAH-M6T1U`.
- `BQ28Z610DRZR-R1` was added as the major 2S battery gauge/protection/management IC.
- R13 rewired the BQ28Z610 protection topology from the old incompatible low-side FET path to a high-side N-FET path using two `CSD16412Q5A` FETs: `Q1` charge FET and `Q2` discharge FET.
- Raw cell-stack positive is now `BATT_RAW_P`; protected/system positive is `BAT_SYS`.
- `R46` is now documented as a true series current-sense resistor between raw cell negative `BATT_RAW_N` and protected/system `GND`.
- USB-C ground pins `A12`, `B12`, and shield are on `GND`; the old accidental `Q1_DRAIN_COMMON` net was removed from the generated schematic/netlist.
- USB-C VBUS and CC resistors are on the board. CC resistors were placed close to the USB-C port.
- B.Cu has a GND copper zone/ground plane.
- Power/high-current nets such as battery, charge, 5 V, switched 5 V, 3.3 V, and switched 3.3 V were assigned wider routing rules during layout.

Known firmware integration note:

- The firmware still has 1S battery voltage assumptions in places: empty/full around 3.30 V to 4.20 V and divider ratio 2.0.
- The hardware is now 2S. Firmware battery measurement, warnings, fuel-gauge integration, and any ADC divider expectations must be reviewed before relying on battery display/protection behavior.

## Switched Rails and Pololu Modules

The design incorporates two Pololu mini pushbutton power switch modules, Pololu product 2808:

- One switches the 5 V output rail.
- One switches the 3.3 V output rail.

The intent from the daughterboard design thread:

- `GPIO2` controls one Pololu switch.
- `GPIO3` controls the other Pololu switch.
- `GPIO14` and `GPIO21` route to the deep-sleep/wake pushbutton header `J6`.

Current firmware assignment:

- `GPIO2` drives the 5 V Pololu switch control path.
- `GPIO3` drives the 3.3 V Pololu switch control path.
- `GPIO14` is the deep-sleep/wake button sense pin.
- `GPIO21` is the deep-sleep/wake button reference pin.
- The old GPIO14/GPIO21 power LED behavior is disabled so it does not conflict with `J6`.

The UART-to-ELRS pinout remains similar to earlier notes: if the board pin order does not match a given ELRS module harness, it can be manually wired/crossed later.

## Signal and Connector Map

Hosyond ESP32-S3 board interface:

- I2C SDA: firmware currently uses GPIO16.
- I2C SCL: firmware currently uses GPIO15.
- ELRS/UART default pins: firmware uses TX GPIO44 and RX GPIO43, with auto/swap handling.
- Battery ADC: firmware currently uses GPIO9.

Primary daughterboard connector intent:

- UART from Hosyond TX/RX routes to a 5-pin 1.25 mm JST-GH top-entry connector with `+5V_SW` and GND.
- I2C SDA/SCL routes to a 4-pin 2.54 mm through-hole header `J5` with `+3V3_SW` and GND.
- `J6` is a 2-pin 2.54 mm through-hole header for a pushbutton/deep-sleep signal path.
- Gimbal connectors were split so each gimbal has two 3-wire JST connections: GND, analog input, and `+3V3_SW`.
- Remaining analog/GPIO breakout signals were moved to labeled test pads/solder pads where full connectors were not needed.

Connector notes:

- `J4` uses top-entry `BM05B-GHS-TBT(LF)(SN)(N)` style connector for the 5-pin JST-GH output.
- `J7` is the 2S balance connector, using a JST-XH style part such as `S3B-XH-A(LF)(SN)`.
- `J5` and `J6` were corrected to through-hole 2.54 mm headers and remain that way in the R13 fabrication outputs.

## Onboard I2C and Analog Devices

The board includes onboard components instead of relying on plug-in ADS1115/PCF modules:

- Two `ADS1115` ADC circuits for about 8 analog inputs.
- One `MCP23017` GPIO expander circuit.
- ADS1115s, MCP23017, attached analog devices, and other 3.3 V peripherals are powered from `+3V3_SW`, so powering down the switched 3.3 V rail shuts them off.
- ESD arrays D4-D7 were updated to use the JLC-selected SRV05-style SOT-23-6 part. The verified pinout is:
  - IO pins: 1, 3, 4, 6
  - GND: pin 2
  - `+3V3_SW_LOGIC`: pin 5

Firmware currently expects:

- Primary ADS1115 at I2C address `0x48` for stick axes.
- Auxiliary ADS1115 at I2C address `0x49` for spare analog inputs.
- MCP23017 fixed at I2C address `0x20`.
- MCP23017 active-low D-pad mapping on low GPIO bits:
  - select: bit 0
  - left: bit 1
  - up: bit 2
  - down: bit 3
  - right: bit 4

Firmware caution:

- If switched `+3V3_SW` removes power from the I2C devices while SDA/SCL remain connected, firmware and hardware should account for possible back-powering through I2C lines. ESD/protection and bus behavior should be checked.

## Main Firmware Context

Main sketch:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\Hosyond_apr24b\Hosyond_apr24b.ino
```

The firmware is a large Hosyond ESP32-S3 transmitter/controller application. It includes:

- TFT/touch UI using `TFT_eSPI` and FT6336U touch.
- Model storage and settings.
- Stick calibration, expo/rates, trims, endpoints, failsafe, drive type, and mixing.
- Protocol selection between ELRS/CRSF and ESP-NOW.
- ELRS module support on `Serial1`, with default pins TX GPIO44 and RX GPIO43, auto/swap logic, and 5,250,000 baud CRSF handling.
- ESP-NOW transmitter support and receiver/bind handling.
- Dual ADS1115 support for the daughterboard ADCs at `0x48` and `0x49`.
- MCP23017 GPIO expander support at `0x20`.
- Battery display, warnings, and auto deep-sleep behavior.
- Display backlight and audio support.

Important current firmware defines:

```text
POWER_BUTTON_SENSE_PIN 14
POWER_BUTTON_REF_PIN   21
POWER_LED_ENABLED      false
UART_5V_ENABLE_PIN     2
I2C_3V3_ENABLE_PIN     3
BATTERY_ADC_PIN        9
I2C_SDA_PIN            16
I2C_SCL_PIN            15
ADS1115 primary/aux    0x48 / 0x49
MCP23017 address       0x20
ELRS default TX/RX     44/43
```

There is also an ESP-NOW receiver sketch in:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\ESPNow_S3_Receiver
```

## Mechanical Context

The current daughterboard outline is based on:

```text
daughterboard2.dxf
Daughterboardv2.step
```

The board is a wide, notched shape that fits around/under the Hosyond board. The Hosyond board sits above the daughterboard on standoffs, roughly 20 mm above the daughterboard. The upper gap/center area receives the Hosyond board. The USB-C connector is intended to sit in the center island/strip between the two halves.

Placement intent from layout work:

- UART near the left side of the Hosyond gap.
- GPIO and I2C near the right side of the Hosyond gap.
- UART output near the lower center strip.
- Power management/protection mostly on the left side.
- I2C circuits and analog/GPIO expansion mostly on the right side.
- Gimbal connectors near the gimbal wire exits.

RadioMaster Pocket/Zorro X5 gimbal reference work:

- Center circle where the stick emerges: about 37 mm diameter.
- Mounting hole spacing: about 32.9 mm by 30 mm center-to-center.
- Mounting plate: about 39 mm wide, 41 mm at the bump, by 35 mm tall.
- Bottom of gimbal to top of stick: about 43 mm.
- Reference images and generated fit models are retained in the workspace.

## Fabrication and Checks

R13 fabrication outputs were generated after:

- Converting the PCB to 4 layers.
- Updating J5/J6 to through-hole 2.54 mm headers.
- Updating D4-D7 to the normal 6-pad SOT-23-6 ESD footprint.
- Reworking the `BQ28Z610` protection circuit into the corrected high-side FET topology.
- Rebuilding Gerbers, drill files, JLC BOM/CPL files, validation reports, and `fabrication\jlcpcb_2026-07-10_r13.zip`.

Latest known checks from R13:

- ERC: 0 errors, 0 warnings.
- DRC: 38 reported violations.
- Unconnected pads/items: 0 reported in the final checked state before packaging.
- R13 topology audit: PASS.
- The remaining DRC items are accepted/known items:
  - U1 clearance differences, roughly 0.20 mm to 0.225 mm versus a 0.25 mm rule.
  - Silkscreen/text-size/overlap warnings that should not normally block fabrication.
- JLC assembly still needs confirmed selections for four R13 support parts:
  - `Q1`, `Q2`: `CSD16412Q5A` high-side protection FETs.
  - `R49`, `R50`: 10M 0603 gate-source bleed resistors.
  - See `fabrication\jlcpcb_2026-07-10_r13\reports\Daughterboard_r13_missing_jlc_parts.csv`.

Manufacturing caution:

- Before ordering, re-run KiCad DRC/ERC after any manual edits, refill zones, and regenerate fabrication files.
- Silkscreen issues are usually non-critical, but copper clearance, drill, board outline, missing nets, and footprint orientation issues should be treated as critical.
- Check JLC part availability again before final ordering, because stock and part lifecycle status can change.

## BOM and Part Selection Notes

Current major JLC/assembly direction:

- Use JLC-available SMD components wherever possible.
- Keep 1.25 mm/2.54 mm JST or pin headers as chosen unless there is a specific layout/manufacturing reason to change.
- Non-component items such as test pads and solder pads should be placed at the bottom of any human-facing BOM or excluded from assembly upload.

Notable parts/substitutions from the design thread:

- `BQ28Z610DRZR-R1` replaced the unavailable/minimum-order-problem `S-8252AAH-M6T1U` direction.
- R13 replaced the old `CSD83325L`/`PT8810` low-side FET position with two `CSD16412Q5A` high-side protection FETs.
- `C2012X5R1C226M125AC` substituted for `GRM21BR61C226ME44L`.
- `BM05B-GHS-TBT(LF)(SN)(N)` selected for the top-entry 5-pin JST-GH connector.
- `S3B-XH-A(LF)(SN)` style part used for the 2S balance connector.
- `BSMD1206-200-16V` used for F1-F4 in the JLC audit.

Useful BOM files:

```text
Daughterboard_pcb_bom.csv
Daughterboard_current_jlc_part_audit.csv
fabrication\jlcpcb_2026-07-10_r13\assembly\Daughterboard_jlcpcb_bom_template_filled.csv
fabrication\jlcpcb_2026-07-10_r13\assembly\Daughterboard_jlcpcb_cpl.csv
```

## Open Integration Items

1. Update firmware for the 2S battery system:
   - ADC divider/scaling.
   - Battery full/empty/warning thresholds.
   - Whether to read `BQ28Z610` over I2C/SMBus or keep separate analog battery sensing.

2. Confirm daughterboard I2C behavior on first hardware:
   - Primary ADS1115 at `0x48` feeds the four stick axes.
   - Auxiliary ADS1115 at `0x49` feeds spare analog inputs, with AIN4/AIN5 mapped into CH5/CH6.
   - MCP23017 at `0x20` replaces the old PCF8575-style GPIO path.

3. Confirm JLC orientation before ordering:
   - Especially U1/BQ25887, U2/BQ28Z610, Q1/Q2/CSD16412Q5A, USB-C, JST connectors, and ESD arrays.
   - KiCad pin 1 marker orientation and JLC preview orientation can look different depending on rotation convention, so use pad numbering and datasheets, not only the visual corner.

4. Re-run DRC/ERC and regenerate fabrication files after every footprint, placement, routing, or BOM-impacting change.

## Useful Reference Files

Daughterboard hardware:

```text
power_board_design.md
S8252_E.pdf
SRV05-4-TCT_C13612_actual.pdf
SRV05-4-TCT_C13612_datasheet.pdf
JLCPCB_BOM_Template.xls
update_jlc_footprint_migration.ps1
```

Mechanical/gimbal:

```text
daughterboard2.dxf
Daughterboardv2.step
radiomaster_x5_gimbal_fit_model.step
radiomaster_x5_gimbal_fit_model.stl
fusion360_x5_gimbal_model.py
x5Front.webp
x5Side1.webp
x5Side2.webp
x5Side3.jpg
x5Bottom.webp
x5dimensions.webp
```

Older previous-project context:

```text
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\project_anubis\Daughterboard\Hosyond_carrier_block_diagram.md
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\project_anubis\Daughterboard\Hosyond_carrier_connector_inventory.csv
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\project_anubis\Daughterboard\Hosyond_carrier_net_table.csv
C:\Users\Avala\OneDrive\Desktop\sketch_may7a\project_anubis\Daughterboard\Hosyond_carrier_mechanical_constraints.md
```
