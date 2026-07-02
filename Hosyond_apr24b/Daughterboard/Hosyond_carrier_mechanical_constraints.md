# Hosyond Carrier Board Mechanical Constraints

## Board Shape

User-provided available space:

- Top left horizontal: `56 mm`
- Center notch/drop: `22 mm` down
- Center horizontal: `51 mm`
- Center rise: `22 mm` up
- Top right horizontal: `56 mm`
- Side height: `42 mm`
- Bottom width: `163 mm`

Interpreted as a wide shallow `U` / top-notched board:

```text
Total width:  163 mm
Total height: 42 mm
Top notch:    51 mm wide x 22 mm deep
```

## STEP Reference

Reference file:

```text
C:\Users\Avala\Downloads\daughteroard.step
```

Quick geometry scan from the STEP file:

```text
X min/max: -81.500 mm to 81.500 mm
Width:     163.000 mm

Y min/max: -51.738 mm to 0.000 mm
Height:    51.738 mm

Z min/max: -13.650 mm to 0.000 mm
Depth:     13.650 mm
```

Important: the STEP-derived width matches the hand measurement exactly, but
the STEP-derived height is about `51.74 mm`, not `42 mm`. Before ordering a PCB,
use the STEP model or a printed 1:1 outline to confirm whether the real usable
board height should be `42 mm` or `51.74 mm`.

## DXF Board Outline

Reference file:

```text
C:\Users\Avala\Downloads\daughterboard.dxf
```

The DXF declares `$INSUNITS = 4`, which means millimeters. Its 2D extents are:

```text
Width:  163.000 mm
Height: 42.000 mm
Layer:  0
```

The DXF contains one closed `LWPOLYLINE`, which is the likely KiCad
`Edge.Cuts` board outline. Original DXF coordinates:

```text
(-81.500, 12.738)
(-78.500,  9.738)
(-28.500,  9.738)
(-25.500, 12.738)
(-25.500, 28.738)
(-22.500, 31.738)
( 22.500, 31.738)
( 25.500, 28.738)
( 25.500, 12.738)
( 28.500,  9.738)
( 78.500,  9.738)
( 81.500, 12.738)
( 81.500, 48.738)
( 78.500, 51.738)
(-78.500, 51.738)
(-81.500, 48.738)
closed
```

Normalized to a top-left-ish `(0,0)` coordinate system by adding `81.5` to X
and subtracting `9.73761672784435` from Y:

```text
(  0.000,  3.000)
(  3.000,  0.000)
( 53.000,  0.000)
( 56.000,  3.000)
( 56.000, 19.000)
( 59.000, 22.000)
(104.000, 22.000)
(107.000, 19.000)
(107.000,  3.000)
(110.000,  0.000)
(160.000,  0.000)
(163.000,  3.000)
(163.000, 39.000)
(160.000, 42.000)
(  3.000, 42.000)
(  0.000, 39.000)
closed
```

This outline includes `3 mm` chamfers/reliefs at the outer corners and notch
corners, so it is more accurate than the earlier simplified rectangle/notch.

## Coordinate System

Origin is the top-left outer corner of the PCB.

```text
X increases left -> right
Y increases top -> bottom
```

## Edge.Cuts Polygon

Use this simplified point list only for rough planning:

```text
P1  (0,   0)
P2  (56,  0)
P3  (56,  22)
P4  (107, 22)
P5  (107, 0)
P6  (163, 0)
P7  (163, 42)
P8  (0,   42)
P9  (0,   0)
```

Visual sketch:

```text
(0,0)          (56,0)         (107,0)          (163,0)
  +--------------+               +---------------+
  |              |               |               |
  |              +---------------+               |
  |              (56,22)   (107,22)              |
  |                                              |
  |                                              |
  +----------------------------------------------+
(0,42)                                      (163,42)
```

## Placement Priorities

Fixed / case-facing items should be placed first:

- USB-C breakout aligned with the case opening.
- Power button connector near the physical power button.
- Gimbal connectors near left and right gimbal wire exits.
- Hosyond UART/I2C/GPIO connectors near the Hosyond board.
- ELRS header and 5V switch near the ELRS module bay.
- D-pad connector near the front controls.
- Battery connector near the battery pocket.

## Routing Priorities

- Route power first: `GND`, `5V`, `3V3`, `5V_SW`, `3V3_SW`.
- Keep all grounds common.
- Use wider traces for `5V`, `3V3`, switched rails, and battery-related nets.
- Keep analog gimbal traces away from ELRS/UART if practical.
- Keep `D+` and `D-` close together if USB data is routed on this carrier.
- Add test pads for `5V`, `3V3`, `GND`, `SDA`, `SCL`, `UART_TX`, and `UART_RX`.

## Open Mechanical Questions

- Exact USB-C port location.
- Mounting hole count and positions.
- Which side/components face up.
- Maximum component height.
- Whether modules mount directly to the carrier or connect by short jumpers.
