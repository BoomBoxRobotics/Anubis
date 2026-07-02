# Hosyond Carrier Board Block Diagram

This carrier board is intended to replace loose wiring with a clean plug-in harness.
It should mostly route signals between prebuilt modules instead of recreating tiny
IC circuits on the PCB.

## System Diagram

```mermaid
flowchart TD
  H["Hosyond ESP32-S3 Display"]

  B["Protected 1S LiPo Battery"]
  USB["External USB-C Breakout"]
  SW5["5V Power Switch"]
  SW3["3.3V Power Switch"]

  ELRS["ELRS Module"]
  ADSA["ADS1115 A\nPrimary sticks"]
  ADSB["ADS1115 B\nFuture analog aux"]
  AUXA["J13 Aux Analog\nA0/A1"]
  AUXB["J23 Aux Analog\nA2/A3"]
  PCF["PCF8575\nDigital expansion"]
  PCFI2C["J22 PCF8575\nI2C + power"]
  PCFA["U3 PCF8575\nSide A"]
  PCFB["U4 PCF8575\nSide B"]

  LG["Left Gimbal"]
  RG["Right Gimbal"]
  DPAD["5-way D-pad"]
  LED["Power LED Ring"]
  EXPA["J19 PCF Expansion A\nP06-P09"]
  EXPB["J20 PCF Expansion B\nP10-P13"]
  EXPC["J21 PCF Expansion C\nP14-P17"]
  PWR["Power Button"]

  B -->|"BAT+ / BAT-"| H
  USB -->|"VBUS, GND, D+, D-"| H

  H -->|"5V rail"| SW5
  H -->|"GPIO14 / ELRS_POWER_CTRL"| SW5
  SW5 -->|"switched 5V"| ELRS
  H <-->|"UART TX/RX"| ELRS

  H -->|"3.3V rail"| SW3
  H -->|"GPIO21 / I2C_POWER_CTRL"| SW3
  SW3 -->|"switched 3.3V"| ADSA
  SW3 -->|"switched 3.3V"| ADSB
  SW3 -->|"switched 3.3V"| PCFI2C

  H <-->|"I2C SDA/SCL"| ADSA
  H <-->|"I2C SDA/SCL"| ADSB
  H <-->|"I2C SDA/SCL"| PCFI2C
  PCFI2C -->|"3.3V, GND"| PCFA
  PCFI2C -->|"3.3V, GND"| PCFB
  PCFA -.->|"same PCF8575 module"| PCF
  PCFB -.->|"same PCF8575 module"| PCF

  LG -->|"X/Y analog"| ADSA
  RG -->|"X/Y analog"| ADSA
  AUXA -->|"A0/A1 analog"| ADSB
  AUXB -->|"A2/A3 analog"| ADSB

  DPAD -->|"P00-P04"| PCFA
  PCFA -->|"P05 / POWER_LED_CTRL"| LED
  PCFA -->|"P06-P08"| EXPA
  PCFB -->|"P09"| EXPA
  PCFB -->|"P10-P13"| EXPB
  PCFB -->|"P14-P17"| EXPC

  PWR -->|"GPIO2 / GPIO3"| H
```

## Main Design Rules

- Keep the power button on direct Hosyond GPIO pins `GPIO2` and `GPIO3` so it can wake the ESP32 from deep sleep.
- Use `GPIO14` to control the switched `5V` rail for the ELRS module.
- Use `GPIO21` to control the switched `3.3V` rail for I2C modules and the D-pad.
- Keep all grounds common.
- Switch the positive power rails, not ground.
- Do not connect USB `VBUS` directly to the LiPo battery.
- Do not connect the LiPo battery directly to the `5V` rail.
- Add test pads for `5V`, `3.3V`, `GND`, `SDA`, `SCL`, `UART_TX`, and `UART_RX`.

## I2C Power Caution

If the `3.3V` rail to the I2C modules is switched off while `SDA` and `SCL` remain
connected, some modules can be partially powered through the I2C lines. The best
fix is to use an I2C power switch/bus-isolation board or add proper bus isolation
in the final PCB design.

## Files

- Detailed net table: `Hosyond_carrier_net_table.csv`
