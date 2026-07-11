from __future__ import annotations

import csv
from urllib.parse import quote_plus
import uuid
from dataclasses import dataclass
from pathlib import Path


ROOT_UUID = "d4b0641a-9c05-4a64-9f99-e24c4b75c101"
PROJECT = "Daughterboard"
LAYOUT_SCALE_X = 1.3
LAYOUT_SCALE_Y = 1.2


def uid() -> str:
    return str(uuid.uuid4())


@dataclass(frozen=True)
class Pin:
    number: str
    name: str
    side: str
    offset: float
    kind: str = "passive"


@dataclass(frozen=True)
class SymbolDef:
    name: str
    ref_prefix: str
    value: str
    footprint: str
    datasheet: str
    description: str
    width: float
    height: float
    pins: tuple[Pin, ...]
    in_bom: bool = True
    on_board: bool = True


@dataclass(frozen=True)
class Part:
    symbol: str
    ref: str
    value: str
    footprint: str
    x: float
    y: float
    nets: dict[str, str | None]
    datasheet: str = ""
    description: str = ""
    section: str = ""
    notes: str = ""
    in_bom: bool = True
    on_board: bool = True


def q(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def snap(num: float) -> float:
    return round(num / 1.27) * 1.27


def fmt(num: float) -> str:
    num = snap(num)
    return f"{num:.3f}".rstrip("0").rstrip(".")


def prop(name: str, value: str, x: float, y: float, hide: bool = False) -> str:
    hide_s = " (hide yes)" if hide else ""
    return (
        f'\t\t(property "{q(name)}" "{q(value)}"\n'
        f"\t\t\t(at {fmt(x)} {fmt(y)} 0)\n"
        f"\t\t\t(effects (font (size 1.016 1.016)){hide_s} (justify left))\n"
        "\t\t)\n"
    )


def pin_xy(sym: SymbolDef, pin: Pin) -> tuple[float, float, int]:
    hw = sym.width / 2
    hh = sym.height / 2
    if pin.side == "L":
        return snap(-hw - 5.08), snap(pin.offset), 0
    if pin.side == "R":
        return snap(hw + 5.08), snap(pin.offset), 180
    if pin.side == "T":
        return snap(pin.offset), snap(-hh - 5.08), 90
    if pin.side == "B":
        return snap(pin.offset), snap(hh + 5.08), 270
    raise ValueError(pin.side)


def stub_xy(sym: SymbolDef, pin: Pin, x: float, y: float) -> tuple[float, float]:
    px, py, _ = pin_xy(sym, pin)
    sx = x + px
    sy = y - py
    if pin.side == "L":
        return sx - 5.08, sy
    if pin.side == "R":
        return sx + 5.08, sy
    if pin.side == "T":
        return sx, sy - 5.08
    if pin.side == "B":
        return sx, sy + 5.08
    raise ValueError(pin.side)


def draw_pin(pin: Pin, sym: SymbolDef) -> str:
    x, y, angle = pin_xy(sym, pin)
    return (
        f"\t\t\t(pin {pin.kind} line\n"
        f"\t\t\t\t(at {fmt(x)} {fmt(y)} {angle})\n"
        "\t\t\t\t(length 5.08)\n"
        f'\t\t\t\t(name "{q(pin.name)}" (effects (font (size 1.016 1.016))))\n'
        f'\t\t\t\t(number "{q(pin.number)}" (effects (font (size 1.016 1.016))))\n'
        "\t\t\t)\n"
    )


def symbol_def(sym: SymbolDef, embedded: bool = True) -> str:
    name = f"Daughterboard:{sym.name}" if embedded else sym.name
    out = [
        f'\t\t(symbol "{name}"\n',
        "\t\t\t(exclude_from_sim no)\n",
        f"\t\t\t(in_bom {'yes' if sym.in_bom else 'no'})\n",
        f"\t\t\t(on_board {'yes' if sym.on_board else 'no'})\n",
        "\t\t\t(duplicate_pin_numbers_are_jumpers no)\n",
        f'\t\t\t(property "Reference" "{q(sym.ref_prefix)}"\n',
        f"\t\t\t\t(at {fmt(-sym.width / 2)} {fmt(-sym.height / 2 - 7.62)} 0)\n",
        "\t\t\t\t(effects (font (size 1.27 1.27)))\n",
        "\t\t\t)\n",
        f'\t\t\t(property "Value" "{q(sym.value)}"\n',
        f"\t\t\t\t(at {fmt(-sym.width / 2)} {fmt(sym.height / 2 + 7.62)} 0)\n",
        "\t\t\t\t(effects (font (size 1.27 1.27)))\n",
        "\t\t\t)\n",
        f'\t\t\t(property "Footprint" "{q(sym.footprint)}"\n',
        "\t\t\t\t(at 0 0 0)\n",
        "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes))\n",
        "\t\t\t)\n",
        f'\t\t\t(property "Datasheet" "{q(sym.datasheet)}"\n',
        "\t\t\t\t(at 0 0 0)\n",
        "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes))\n",
        "\t\t\t)\n",
        f'\t\t\t(property "Description" "{q(sym.description)}"\n',
        "\t\t\t\t(at 0 0 0)\n",
        "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes))\n",
        "\t\t\t)\n",
        f'\t\t\t(symbol "{sym.name}_0_1"\n',
        f"\t\t\t\t(rectangle (start {fmt(-sym.width / 2)} {fmt(-sym.height / 2)}) "
        f"(end {fmt(sym.width / 2)} {fmt(sym.height / 2)}) "
        "(stroke (width 0.254) (type default)) (fill (type background)))\n",
    ]
    if sym.width >= 20 and sym.height >= 15:
        out.extend(
            [
                f'\t\t\t\t(text "{q(sym.value)}"\n',
                "\t\t\t\t\t(at 0 0 0)\n",
                "\t\t\t\t\t(effects (font (size 1.27 1.27) (bold yes)))\n",
                "\t\t\t\t)\n",
            ]
        )
    out.extend(
        [
            "\t\t\t)\n",
            f'\t\t\t(symbol "{sym.name}_1_1"\n',
        ]
    )
    for pin in sym.pins:
        out.append(draw_pin(pin, sym))
    out.append("\t\t\t)\n")
    out.append("\t\t)\n")
    return "".join(out)


def part_instance(part: Part, sym: SymbolDef) -> str:
    x, y = part.x, part.y
    field_x = x - sym.width / 2
    field_y = y - sym.height / 2 - 8.89
    out = [
        "\t(symbol\n",
        f'\t\t(lib_id "Daughterboard:{part.symbol}")\n',
        f"\t\t(at {fmt(x)} {fmt(y)} 0)\n",
        "\t\t(unit 1)\n",
        "\t\t(exclude_from_sim no)\n",
        f"\t\t(in_bom {'yes' if part.in_bom else 'no'})\n",
        f"\t\t(on_board {'yes' if part.on_board else 'no'})\n",
        "\t\t(dnp no)\n",
        f'\t\t(uuid "{uid()}")\n',
        prop("Reference", part.ref, field_x, field_y),
        prop("Value", part.value, field_x, field_y + 2.54),
        prop("Footprint", part.footprint, x, y, True),
        prop("Datasheet", part.datasheet, x, y, True),
        prop("Purchase Link", purchase_link_for(part), x, y, True),
        "\t\t(instances\n",
        f'\t\t\t(project "{PROJECT}"\n',
        f'\t\t\t\t(path "/{ROOT_UUID}" (reference "{part.ref}") (unit 1))\n',
        "\t\t\t)\n",
        "\t\t)\n",
        "\t)\n",
    ]
    return "".join(out)


def wire_label(x1: float, y1: float, x2: float, y2: float, net: str, inline: bool = False) -> str:
    label_x = x2
    label_y = y2 if inline else y2 - 2.54
    if not inline and abs(y2 - y1) > abs(x2 - x1):
        label_x = x2 - 5.08
        label_y = y2
    wires = (
        f"\t(wire (pts (xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)})) "
        f'(stroke (width 0) (type default)) (uuid "{uid()}"))\n'
    )
    if (label_x, label_y) != (x2, y2):
        wires += (
            f"\t(wire (pts (xy {fmt(x2)} {fmt(y2)}) (xy {fmt(label_x)} {fmt(label_y)})) "
            f'(stroke (width 0) (type default)) (uuid "{uid()}"))\n'
        )
    return wires + (
        f'\t(global_label "{q(net)}"\n'
        "\t\t(shape bidirectional)\n"
        f"\t\t(at {fmt(label_x)} {fmt(label_y)} 0)\n"
        "\t\t(fields_autoplaced yes)\n"
        "\t\t(effects (font (size 0.889 0.889)))\n"
        f'\t\t(uuid "{uid()}")\n'
        f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {fmt(label_x)} {fmt(label_y)} 0) (effects (font (size 0.889 0.889)) (hide yes)))\n'
        "\t)\n"
    )


def no_connect(x: float, y: float) -> str:
    return f'\t(no_connect (at {fmt(x)} {fmt(y)}) (uuid "{uid()}"))\n'


def digikey_search(query: str) -> str:
    return f"https://www.digikey.com/en/products/result?keywords={quote_plus(query)}"


def purchase_link_for(part: Part) -> str:
    ref = part.ref
    value = part.value
    footprint = part.footprint

    exact_by_ref = {
        "J1": digikey_search("GCT USB4125 USB-C receptacle 6 pin"),
        "J7": digikey_search("JST XH 3 pin horizontal S3B-XH-A"),
        "U1": digikey_search("BQ25887RGE"),
        "U2": "https://www.digikey.com/en/products/detail/texas-instruments/BQ28Z610DRZR-R1/11625468",
        "Q1": "https://jlcpcb.com/partdetail/TexasInstruments-CSD18502Q5B/C473915",
        "Q2": "https://jlcpcb.com/partdetail/TexasInstruments-CSD18502Q5B/C473915",
        "U3": digikey_search("AP63200WU"),
        "U4": digikey_search("AP63200WU"),
        "U7": digikey_search("MCP23017-E/SO"),
        "SW1": "https://www.pololu.com/product/2808",
        "SW2": "https://www.pololu.com/product/2808",
        "J4": digikey_search("SM05B-GHS-TB JST GH 5 position"),
    }
    if ref in exact_by_ref:
        return exact_by_ref[ref]

    if ref.startswith("J3") or ref.startswith("TP"):
        return ""
    if "solder pad" in value:
        return ""
    if "test pad" in value:
        return ""

    if ref in {"BT1", "BT2"}:
        return digikey_search("18650 battery holder single cell PCB")
    if ref in {"J2", "J5", "J6", "J8"}:
        positions = {"J2": "3", "J5": "4", "J6": "2", "J8": "10"}[ref]
        return digikey_search(f"2.54mm pin header vertical {positions} position")
    if ref in {"U5", "U6"}:
        return digikey_search("ADS1115IDGS")
    if ref == "U7":
        return digikey_search("MCP23017-E/SO")
    if ref in {"D4", "D5", "D6", "D7"}:
        return digikey_search("4 channel 3.3V ESD TVS array SOT-23-6")
    if ref.startswith("F"):
        return digikey_search(f"{value} resettable fuse PTC SMD")
    if ref.startswith("L"):
        return digikey_search(f"{value} shielded inductor SMD")
    if ref.startswith("D"):
        if "TVS" in value:
            return digikey_search(f"{value} SOD-323 TVS diode")
        return digikey_search(f"{value} 0603 LED")
    if ref.startswith("TH"):
        return digikey_search(f"{value} 0603 NTC thermistor")
    if ref.startswith("R"):
        if "TBD" in value:
            return digikey_search("0603 resistor 1%")
        return digikey_search(f"{value} {footprint or '0603'} resistor")
    if ref.startswith("C"):
        return digikey_search(f"{value} {footprint or 'SMD'} capacitor")

    if footprint:
        return digikey_search(f"{value} {footprint}")
    if value:
        return digikey_search(value)
    return ""


def make_symbols() -> dict[str, SymbolDef]:
    two = (
        Pin("1", "1", "L", 0),
        Pin("2", "2", "R", 0),
    )
    three = (
        Pin("1", "1", "R", -5.08),
        Pin("2", "2", "R", 0),
        Pin("3", "3", "R", 5.08),
    )
    return {
        "USB_C_SINK_GCT_6PAD": SymbolDef(
            "USB_C_SINK_GCT_6PAD",
            "J",
            "USB-C 5V sink",
            "",
            "https://www.usb.org/document-library/usb-type-cr-cable-and-connector-specification-release-24",
            "Power-only USB-C receptacle using the GCT USB4125 six-pad footprint.",
            20.32,
            30,
            (
                Pin("A9", "VBUS_A", "R", -10.16),
                Pin("B9", "VBUS_B", "R", -5.08),
                Pin("A12", "GND_A", "R", 5.08),
                Pin("B12", "GND_B", "R", 10.16),
                Pin("A5", "CC1", "L", -2.54),
                Pin("B5", "CC2", "L", 2.54),
                Pin("SH", "SHIELD", "L", 10.16),
            ),
        ),
        "TWO_PIN": SymbolDef("TWO_PIN", "X", "2-pin part", "", "", "Generic two-pin part.", 10.16, 7.62, two),
        "TESTPAD": SymbolDef("TESTPAD", "TP", "Test pad", "", "", "Single test pad.", 7.62, 5.08, (Pin("1", "PAD", "R", 0),)),
        "PWR_FLAG": SymbolDef("PWR_FLAG", "#FLG", "PWR_FLAG", "", "", "ERC power flag.", 7.62, 5.08, (Pin("1", "PWR", "R", 0, "power_out"),), False, False),
        "CONN_2": SymbolDef(
            "CONN_2",
            "J",
            "2-pin connector",
            "",
            "",
            "Generic 2-pin connector.",
            12.7,
            12.7,
            (
                Pin("1", "1", "R", -2.54),
                Pin("2", "2", "R", 2.54),
            ),
        ),
        "CONN_3": SymbolDef("CONN_3", "J", "3-pin connector", "", "", "Generic 3-pin connector.", 12.7, 17.78, three),
        "CONN_4": SymbolDef(
            "CONN_4",
            "J",
            "4-pin connector",
            "",
            "",
            "Generic 4-pin connector.",
            12.7,
            22.86,
            (
                Pin("1", "1", "R", -7.62),
                Pin("2", "2", "R", -2.54),
                Pin("3", "3", "R", 2.54),
                Pin("4", "4", "R", 7.62),
            ),
        ),
        "CONN_5": SymbolDef(
            "CONN_5",
            "J",
            "5-pin connector",
            "",
            "",
            "Generic 5-pin connector.",
            12.7,
            27.94,
            (
                Pin("1", "1", "R", -10.16),
                Pin("2", "2", "R", -5.08),
                Pin("3", "3", "R", 0),
                Pin("4", "4", "R", 5.08),
                Pin("5", "5", "R", 10.16),
            ),
        ),
        "CONN_9": SymbolDef(
            "CONN_9",
            "J",
            "9-pin connector",
            "",
            "",
            "Generic 9-pin connector.",
            15.24,
            50.8,
            (
                Pin("1", "1", "R", -20.32),
                Pin("2", "2", "R", -15.24),
                Pin("3", "3", "R", -10.16),
                Pin("4", "4", "R", -5.08),
                Pin("5", "5", "R", 0),
                Pin("6", "6", "R", 5.08),
                Pin("7", "7", "R", 10.16),
                Pin("8", "8", "R", 15.24),
                Pin("9", "9", "R", 20.32),
            ),
        ),
        "CONN_10": SymbolDef(
            "CONN_10",
            "J",
            "10-pin connector",
            "",
            "",
            "Generic 10-pin connector.",
            15.24,
            55.88,
            (
                Pin("1", "1", "R", -22.86),
                Pin("2", "2", "R", -17.78),
                Pin("3", "3", "R", -12.7),
                Pin("4", "4", "R", -7.62),
                Pin("5", "5", "R", -2.54),
                Pin("6", "6", "R", 2.54),
                Pin("7", "7", "R", 7.62),
                Pin("8", "8", "R", 12.7),
                Pin("9", "9", "R", 17.78),
                Pin("10", "10", "R", 22.86),
            ),
        ),
        "ESD_ARRAY_4CH": SymbolDef(
            "ESD_ARRAY_4CH",
            "D",
            "4ch GPIO ESD array",
            "Daughterboard:SOT-23-6_NoPad6",
            "",
            "Generic 4-channel low-capacitance TVS/ESD array for exposed 3.3V GPIO pads.",
            20.32,
            27.94,
            (
                Pin("1", "IO1", "L", -7.62),
                Pin("2", "IO2", "L", -2.54),
                Pin("3", "GND", "B", 0),
                Pin("4", "IO3", "R", -2.54),
                Pin("5", "IO4", "R", -7.62),
                Pin("6", "NC", "R", 7.62, "no_connect"),
            ),
        ),
        "POLOLU_2808": SymbolDef(
            "POLOLU_2808",
            "SW",
            "Pololu #2808",
            "",
            "https://www.pololu.com/product/2808",
            "Mini Pushbutton Power Switch LV module.",
            25.4,
            22.86,
            (
                Pin("1", "VIN", "L", -5.08),
                Pin("2", "VOUT", "R", -5.08),
                Pin("3", "GND", "L", 5.08),
                Pin("4", "CTRL", "R", 5.08),
            ),
        ),
        "BQ24074RGT": SymbolDef(
            "BQ24074RGT",
            "U",
            "BQ24074RGT",
            "Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm",
            "https://www.ti.com/lit/ds/symlink/bq24074.pdf",
            "USB-friendly 1-cell Li-ion charger with power-path management.",
            30,
            50,
            (
                Pin("13", "IN", "L", -20.32),
                Pin("4", "~{CE}", "L", -15.24),
                Pin("5", "EN2", "L", -10.16),
                Pin("6", "EN1", "L", -5.08),
                Pin("12", "ILIM", "L", 0),
                Pin("16", "ISET", "L", 5.08),
                Pin("14", "TMR", "L", 10.16),
                Pin("15", "ITERM", "L", 15.24),
                Pin("1", "TS", "R", -20.32),
                Pin("2", "BAT", "R", -15.24),
                Pin("3", "BAT", "R", -10.16),
                Pin("10", "OUT", "R", -5.08),
                Pin("11", "OUT", "R", 0),
                Pin("7", "~{PGOOD}", "R", 5.08),
                Pin("9", "~{CHG}", "R", 10.16),
                Pin("8", "VSS", "R", 15.24),
                Pin("17", "EP/VSS", "R", 20.32),
            ),
        ),
        "BQ25887RGE": SymbolDef(
            "BQ25887RGE",
            "U",
            "BQ25887RGE",
            "Package_DFN_QFN:Texas_RGE0024H_VQFN-24-1EP_4x4mm_P0.5mm_EP2.7x2.7mm",
            "https://www.ti.com/lit/ds/symlink/bq25887.pdf",
            "I2C-controlled 2-cell Li-ion/LiPo boost-mode USB charger with cell balancing.",
            40,
            70,
            (
                Pin("23", "VBUS", "L", -30.48, "power_in"),
                Pin("24", "PSEL", "L", -25.4),
                Pin("3", "CD", "L", -20.32),
                Pin("4", "SDA", "L", -15.24),
                Pin("5", "SCL", "L", -10.16),
                Pin("6", "~{INT}", "L", -5.08, "open_collector"),
                Pin("1", "~{PG}", "L", 0, "open_collector"),
                Pin("2", "STAT", "L", 5.08, "open_collector"),
                Pin("8", "ILIM", "L", 10.16),
                Pin("15", "SNS1", "L", 15.24),
                Pin("16", "SNS2", "L", 20.32),
                Pin("7", "TS", "R", -30.48),
                Pin("10", "CBSET", "R", -25.4),
                Pin("11", "REGN", "R", -20.32, "power_out"),
                Pin("12", "BTST", "R", -15.24),
                Pin("21", "PMID1", "R", -10.16),
                Pin("22", "PMID2", "R", -5.08),
                Pin("17", "SW1", "R", 0),
                Pin("18", "SW2", "R", 5.08),
                Pin("13", "BAT1", "R", 10.16),
                Pin("14", "BAT2", "R", 15.24),
                Pin("9", "MID", "R", 20.32),
                Pin("19", "GND1", "B", -5.08, "power_in"),
                Pin("20", "GND2", "B", 0, "power_in"),
                Pin("25", "EP/GND", "B", 5.08, "power_in"),
            ),
        ),
        "BQ297xy": SymbolDef(
            "BQ297xy",
            "U",
            "BQ297xy",
            "Package_SON:WSON-6_1.5x1.5mm_P0.5mm",
            "https://www.ti.com/lit/ds/symlink/bq2970.pdf",
            "Single-cell Li-ion protection controller.",
            25.4,
            25,
            (
                Pin("1", "NC", "R", 0, "no_connect"),
                Pin("2", "Cout", "R", -7.62, "output"),
                Pin("3", "Dout", "R", -2.54, "output"),
                Pin("4", "VSS", "B", 0, "power_in"),
                Pin("5", "BAT", "L", -2.54),
                Pin("6", "V-", "L", 7.62),
            ),
        ),
        "S8252A_M6": SymbolDef(
            "S8252A_M6",
            "U",
            "S-8252A",
            "Package_TO_SOT_SMD:SOT-23-6",
            "https://www.ablic.com/en/doc/datasheet/battery_protection/S8252_E.pdf",
            "ABLIC 2-serial-cell Li-ion/Li-polymer battery protection IC, SOT-23-6.",
            30.48,
            30.48,
            (
                Pin("1", "DO", "R", -10.16, "output"),
                Pin("2", "CO", "R", -5.08, "output"),
                Pin("3", "VM", "R", 5.08),
                Pin("4", "VC", "L", 0),
                Pin("5", "VDD", "L", -7.62, "power_in"),
                Pin("6", "VSS", "L", 7.62, "power_in"),
            ),
        ),
        "BQ28Z610_DRZ": SymbolDef(
            "BQ28Z610_DRZ",
            "U",
            "BQ28Z610DRZR-R1",
            "Daughterboard:BQ28Z610_DRZ0012A_12SON_2.5x4mm_P0.5mm",
            "https://www.ti.com/lit/ds/symlink/bq28z610.pdf",
            "TI 1S/2S Li-ion Impedance Track gas gauge and primary protection controller, DRZ0012A VSON-12.",
            35.56,
            50.8,
            (
                Pin("1", "VSS", "L", -20.32, "power_in"),
                Pin("2", "SRN", "L", -15.24),
                Pin("3", "SRP", "L", -10.16),
                Pin("4", "TS1", "L", -5.08),
                Pin("5", "SCL", "L", 5.08, "input"),
                Pin("6", "SDA", "L", 10.16),
                Pin("7", "DSG", "R", -17.78, "output"),
                Pin("8", "PACK", "R", -10.16),
                Pin("9", "CHG", "R", -2.54, "output"),
                Pin("10", "PBI", "R", 5.08),
                Pin("11", "VC2", "R", 12.7, "power_in"),
                Pin("12", "VC1", "R", 17.78),
                Pin("13", "PWPD", "B", 0, "power_in"),
            ),
        ),
        "PROT_FET_PAIR": SymbolDef(
            "PROT_FET_PAIR",
            "Q",
            "CSD83325L",
            "Daughterboard:CSD83325L_YJE0006A",
            "https://www.ti.com/lit/ds/symlink/csd83325l.pdf",
            "12 V dual common-drain N-channel NexFET in TI YJE0006A PicoStar/LGA package.",
            30.48,
            30.48,
            (
                Pin("A1", "S1A", "L", -10.16),
                Pin("C1", "S1B", "L", -5.08),
                Pin("B1", "G1", "L", 5.08),
                Pin("A2", "S2A", "R", -10.16),
                Pin("C2", "S2B", "R", -5.08),
                Pin("B2", "G2", "R", 5.08),
            ),
        ),
        "CSD18502Q5B": SymbolDef(
            "CSD18502Q5B",
            "Q",
            "CSD18502Q5B",
            "Daughterboard:CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm",
            "https://www.ti.com/lit/ds/symlink/csd18502q5b.pdf",
            "40 V single N-channel NexFET in TI DNK/VSON-CLIP-8 package; JLCPCB C473915.",
            25.4,
            35.56,
            (
                Pin("1", "S", "L", -10.16),
                Pin("2", "S", "L", -5.08),
                Pin("3", "S", "L", 0),
                Pin("4", "G", "L", 7.62, "input"),
                Pin("5", "D", "R", -10.16),
                Pin("6", "D", "R", -5.08),
                Pin("7", "D", "R", 0),
                Pin("8", "D", "R", 5.08),
            ),
        ),
        "TPS61023": SymbolDef(
            "TPS61023",
            "U",
            "TPS61023",
            "Package_TO_SOT_SMD:SOT-563",
            "https://www.ti.com/lit/ds/symlink/tps61023.pdf",
            "Synchronous boost converter for 5 V output.",
            25.4,
            25,
            (
                Pin("1", "FB", "L", -7.62),
                Pin("2", "EN", "L", -2.54),
                Pin("3", "VIN", "L", 2.54),
                Pin("4", "GND", "L", 7.62),
                Pin("5", "SW", "R", -2.54),
                Pin("6", "VOUT", "R", 2.54),
            ),
        ),
        "TPS63031DSK": SymbolDef(
            "TPS63031DSK",
            "U",
            "TPS63031DSK",
            "",
            "https://www.ti.com/lit/ds/symlink/tps63031.pdf",
            "Fixed 3.3 V buck-boost converter.",
            27.94,
            38,
            (
                Pin("1", "VOUT", "R", -15.24),
                Pin("2", "L2", "R", -10.16),
                Pin("3", "PGND", "R", -5.08),
                Pin("4", "L1", "R", 0),
                Pin("5", "VIN", "L", -10.16),
                Pin("6", "EN", "L", -5.08),
                Pin("7", "PS/SYNC", "L", 0),
                Pin("8", "VINA", "L", 5.08),
                Pin("9", "GND", "L", 10.16),
                Pin("10", "FB", "R", 5.08),
                Pin("11", "EP", "R", 10.16),
            ),
        ),
        "AP63200WU": SymbolDef(
            "AP63200WU",
            "U",
            "AP63200WU",
            "Package_TO_SOT_SMD:TSOT-23-6",
            "https://www.diodes.com/assets/Datasheets/AP63200-AP63201-AP63203-AP63205.pdf",
            "2A adjustable synchronous buck converter.",
            27.94,
            27.94,
            (
                Pin("3", "IN", "L", -7.62, "power_in"),
                Pin("2", "EN", "L", -2.54),
                Pin("4", "GND", "L", 7.62, "power_in"),
                Pin("1", "FB", "R", -7.62),
                Pin("6", "BST", "R", -2.54),
                Pin("5", "SW", "R", 2.54),
            ),
        ),
        "ADS1115IDGS": SymbolDef(
            "ADS1115IDGS",
            "U",
            "ADS1115IDGS",
            "Package_SO:TSSOP-10_3x3mm_P0.5mm",
            "https://www.ti.com/lit/ds/symlink/ads1115.pdf",
            "16-bit, 4-channel, I2C ADC with internal reference.",
            27.94,
            35.56,
            (
                Pin("4", "AIN0", "L", -12.7),
                Pin("5", "AIN1", "L", -7.62),
                Pin("6", "AIN2", "L", -2.54),
                Pin("7", "AIN3", "L", 2.54),
                Pin("1", "ADDR", "L", 10.16),
                Pin("10", "SCL", "R", -10.16),
                Pin("9", "SDA", "R", -5.08),
                Pin("2", "ALERT/RDY", "R", 0),
                Pin("8", "VDD", "R", 7.62),
                Pin("3", "GND", "R", 12.7),
            ),
        ),
        "MCP23017_SO": SymbolDef(
            "MCP23017_SO",
            "U",
            "MCP23017-E/SO",
            "Daughterboard:SOIC-28W_7.5x17.9mm_P1.27mm_NoPads11_14",
            "https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf",
            "16-bit I2C GPIO expander with interrupt outputs and internal pullups, SOIC-28W.",
            45.72,
            96.52,
            (
                Pin("1", "GPB0", "R", -5.08),
                Pin("2", "GPB1", "R", -10.16),
                Pin("3", "GPB2", "R", -15.24),
                Pin("4", "GPB3", "R", -20.32),
                Pin("5", "GPB4", "R", -25.4),
                Pin("6", "GPB5", "R", -30.48),
                Pin("7", "GPB6", "R", -35.56),
                Pin("8", "GPB7", "R", -40.64, "output"),
                Pin("9", "VDD", "T", 0, "power_in"),
                Pin("10", "VSS", "B", 0, "power_in"),
                Pin("11", "NC1", "L", 35.56, "no_connect"),
                Pin("12", "SCL", "L", 25.4, "input"),
                Pin("13", "SDA", "L", 17.78),
                Pin("14", "NC2", "L", 40.64, "no_connect"),
                Pin("15", "A0", "L", -20.32, "input"),
                Pin("16", "A1", "L", -27.94, "input"),
                Pin("17", "A2", "L", -35.56, "input"),
                Pin("18", "~{RESET}", "L", -10.16, "input"),
                Pin("19", "INTB", "L", 0, "tri_state"),
                Pin("20", "INTA", "L", 7.62, "tri_state"),
                Pin("21", "GPA0", "R", 35.56),
                Pin("22", "GPA1", "R", 30.48),
                Pin("23", "GPA2", "R", 25.4),
                Pin("24", "GPA3", "R", 20.32),
                Pin("25", "GPA4", "R", 15.24),
                Pin("26", "GPA5", "R", 10.16),
                Pin("27", "GPA6", "R", 5.08),
                Pin("28", "GPA7", "R", 0, "output"),
            ),
        ),
    }


def make_parts() -> list[Part]:
    p: list[Part] = []
    add = p.append
    two = "TWO_PIN"
    tp = "TESTPAD"
    ref_map = {
        "RCC1": "R1",
        "RCC2": "R2",
        "CIN_USB1": "C1",
        "CIN_USB2": "C2",
        "RILIM": "R3",
        "RCBSET": "R4",
        "RTSBIAS": "R5",
        "RSTAT_LED": "R6",
        "RPGOOD_LED": "R7",
        "CPMID1": "C3",
        "CPMID2": "C4",
        "CBAT1": "C5",
        "CREGN": "C6",
        "CBTST": "C7",
        "CCBSET": "C8",
        "CBAL1": "C9",
        "CBAL2": "C10",
        "NTC1": "TH1",
        "DCHG": "D2",
        "DPGOOD": "D3",
        "R5FB1": "R8",
        "R5FB2": "R9",
        "R5EN": "R10",
        "R3FB1": "R11",
        "R3FB2": "R12",
        "R3EN": "R13",
        "RPROT_TOP": "R14",
        "RPROT_MID": "R15",
        "RPROT_VM": "R16",
        "C5IN1": "C11",
        "C5IN2": "C12",
        "C5OUT1": "C13",
        "C5OUT2": "C14",
        "C5BST": "C15",
        "C3IN1": "C16",
        "C3IN2": "C21",
        "C3OUT1": "C22",
        "C3OUT2": "C23",
        "C3BST": "C24",
        "CPROT_TOP": "C25",
        "CPROT_BOT": "C26",
        "RCTRL1": "R17",
        "RCTRL2": "R18",
        "RDO_GATE": "R21",
        "RCO_GATE": "R22",
        "CPBI": "C30",
        "CSRN_SRP": "C31",
        "RSENSE": "R46",
        "RSRN": "R47",
        "RSRP": "R48",
        "RDSG_GS": "R49",
        "RCHG_GS": "R50",
        "CCHG_FET": "C32",
        "CDSG_FET": "C33",
        "LCHG": "L3",
        "QCHG": "Q1",
        "QDSG": "Q2",
    }

    def part(symbol, ref, value, footprint, x, y, nets, section="", notes="", datasheet="", description="", in_bom=True, on_board=True):
        add(Part(symbol, ref_map.get(ref, ref), value, footprint, snap(x * LAYOUT_SCALE_X), snap(y * LAYOUT_SCALE_Y), nets, datasheet, description, section, notes, in_bom, on_board))

    part("USB_C_SINK_GCT_6PAD", "J1", "USB-C 5V Sink", "Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal", 28, 42,
         {"VBUS_A": "USB_VBUS", "VBUS_B": "USB_VBUS", "GND_A": "GND", "GND_B": "GND", "CC1": "CC1", "CC2": "CC2", "SHIELD": "GND"}, "USB-C input",
         "Power-only sink; CC1/CC2 each have 5.1k Rd to GND.")
    part(two, "RCC1", "5.1k 1%", "Resistor_SMD:R_0603_1608Metric", 28, 70, {"1": "CC1", "2": "GND"}, "USB-C input", "USB-C Rd.")
    part(two, "RCC2", "5.1k 1%", "Resistor_SMD:R_0603_1608Metric", 28, 82, {"1": "CC2", "2": "GND"}, "USB-C input", "USB-C Rd.")
    part(two, "F1", "2A hold polyfuse", "Fuse:Fuse_1206_3216Metric", 65, 42, {"1": "USB_VBUS", "2": "CHG_IN"}, "USB-C input", "Input fuse/polyfuse; choose exact SMD PTC/fuse current rating before layout.")
    part(two, "D1", "5V VBUS TVS", "Diode_SMD:D_SOD-323", 65, 56, {"1": "CHG_IN", "2": "GND"}, "USB-C input", "Place close to USB-C connector.")
    part(two, "CIN_USB1", "10uF 10V X5R", "Capacitor_SMD:C_0805_2012Metric", 65, 70, {"1": "CHG_IN", "2": "GND"}, "USB-C input", "Charger input bulk.")
    part(two, "CIN_USB2", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 65, 82, {"1": "CHG_IN", "2": "GND"}, "USB-C input", "Charger input bypass.")

    part("BQ25887RGE", "U1", "BQ25887RGE 2S charger", "Package_DFN_QFN:Texas_RGE0024H_VQFN-24-1EP_4x4mm_P0.5mm_EP2.7x2.7mm", 122, 60,
         {
             "VBUS": "CHG_IN", "PSEL": "GND", "CD": "GND", "SDA": "I2C_SDA", "SCL": "I2C_SCL",
             "~{INT}": None, "~{PG}": "PG_STAT", "STAT": "CHG_STAT", "ILIM": "ILIM",
             "SNS1": "BAT_SYS", "SNS2": "BAT_SYS", "TS": "TS", "CBSET": "CBSET", "REGN": "REGN",
             "BTST": "BTST", "PMID1": "PMID", "PMID2": "PMID", "SW1": "CHG_SW", "SW2": "CHG_SW",
             "BAT1": "BAT_SYS", "BAT2": "BAT_SYS", "MID": "CELL_MID", "GND1": "GND", "GND2": "GND",
             "EP/GND": "GND",
         },
         "2S charger", "USB 5V boost-mode charger connected on the protected/system side of the BQ28Z610 high-side FETs.")
    part(two, "LCHG", "1uH >=4A sat", "Inductor_SMD:L_APV_ANR5040", 174, 22, {"1": "PMID", "2": "CHG_SW"}, "2S charger", "BQ25887 boost-charge SMD inductor; finalize DCR/current from layout and charge limit.")
    part(two, "RILIM", "806R 1% TBD", "Resistor_SMD:R_0603_1608Metric", 174, 34, {"1": "ILIM", "2": "GND"}, "2S charger", "Input-current hardware limit target no more than 1.5A; set charge current in BQ25887 firmware/registers between 500mA and 1.5A.")
    part(two, "RCBSET", "TBD 1%", "Resistor_SMD:R_0603_1608Metric", 174, 46, {"1": "CBSET", "2": "GND"}, "2S charger", "Cell-balance setting resistor; choose from BQ25887 datasheet.")
    part(two, "CPMID1", "22uF 16V X5R", "Capacitor_SMD:C_0805_2012Metric", 174, 58, {"1": "PMID", "2": "GND"}, "2S charger", "PMID bulk capacitor.")
    part(two, "CPMID2", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 174, 70, {"1": "PMID", "2": "GND"}, "2S charger", "PMID HF bypass.")
    part(two, "CBAT1", "22uF 16V X5R", "Capacitor_SMD:C_0805_2012Metric", 174, 82, {"1": "BAT_SYS", "2": "GND"}, "2S charger", "2S charger BAT bulk capacitor on protected/system-side pack positive.")
    part(two, "CREGN", "4.7uF 6.3V X5R", "Capacitor_SMD:C_0603_1608Metric", 214, 22, {"1": "REGN", "2": "GND"}, "2S charger", "REGN regulator capacitor.")
    part(two, "CBTST", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 214, 34, {"1": "BTST", "2": "CHG_SW"}, "2S charger", "Bootstrap capacitor.")
    part(two, "CCBSET", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 214, 46, {"1": "CBSET", "2": "GND"}, "2S charger", "CBSET filter placeholder; verify final requirement.")
    part(two, "CBAL1", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 214, 58, {"1": "BATT_RAW_P", "2": "CELL_MID"}, "2S charger", "Upper-cell raw-pack sense bypass; place close to charger/protection sense pins.")
    part(two, "CBAL2", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 214, 70, {"1": "CELL_MID", "2": "BATT_RAW_N"}, "2S charger", "Lower-cell raw-pack sense bypass; place close to charger/protection sense pins.")
    part(two, "NTC1", "10k NTC", "Resistor_SMD:R_0603_1608Metric", 214, 82, {"1": "TS", "2": "GND"}, "2S charger", "0603 NTC; place against or near the selected pack/cells.")
    part(two, "RTSBIAS", "10k 1%", "Resistor_SMD:R_0603_1608Metric", 214, 94, {"1": "REGN", "2": "TS"}, "2S charger", "TS bias; verify with selected NTC curve and BQ25887 TS thresholds.")
    part(two, "RSTAT_LED", "2.2k", "Resistor_SMD:R_0603_1608Metric", 254, 34, {"1": "REGN", "2": "LED_CHG_A"}, "2S charger", "Charge-status LED resistor.")
    part(two, "DCHG", "LED green", "LED_SMD:LED_0603_1608Metric", 254, 46, {"1": "LED_CHG_A", "2": "CHG_STAT"}, "2S charger", "STAT open-drain LED.")
    part(two, "RPGOOD_LED", "2.2k", "Resistor_SMD:R_0603_1608Metric", 254, 58, {"1": "REGN", "2": "LED_PGOOD_A"}, "2S charger", "Power-good LED resistor.")
    part(two, "DPGOOD", "LED blue", "LED_SMD:LED_0603_1608Metric", 254, 70, {"1": "LED_PGOOD_A", "2": "PG_STAT"}, "2S charger", "PG open-drain LED.")

    part("CONN_3", "BT1", "18650 Cell 1 lower", "", 35, 125, {"1": "CELL1_P", "2": "BATT_RAW_N", "3": None}, "Battery", "Lower series cell holder: negative to raw pack negative, positive through F2 to CELL_MID.")
    part("CONN_3", "BT2", "18650 Cell 2 upper", "", 35, 150, {"1": "CELL2_P", "2": "CELL_MID", "3": None}, "Battery", "Upper series cell holder: negative to CELL_MID, positive through F3 to BATT_RAW_P.")
    part(two, "F2", "PTC/fuse cell1", "Fuse:Fuse_1206_3216Metric", 75, 125, {"1": "CELL1_P", "2": "CELL_MID"}, "Battery", "SMD cell fuse/PTC; place close to lower cell positive.")
    part(two, "F3", "PTC/fuse cell2", "Fuse:Fuse_1206_3216Metric", 75, 150, {"1": "CELL2_P", "2": "BATT_RAW_P"}, "Battery", "SMD cell fuse/PTC; place close to upper cell positive.")
    part("CONN_3", "J7", "2S balance input JST-XH side-entry", "Connector_JST:JST_XH_S3B-XH-A_1x03_P2.50mm_Horizontal", 115, 137,
         {"1": "BATT_RAW_N", "2": "CELL_MID", "3": "BATT_RAW_P"},
         "Battery", "Top-edge side-entry 2S balance connector pinout: B-, cell midpoint, B+. Pin 3 is raw cell-stack positive before high-side protection FETs.")

    part("BQ28Z610_DRZ", "U2", "BQ28Z610DRZR-R1", "Daughterboard:BQ28Z610_DRZ0012A_12SON_2.5x4mm_P0.5mm", 155, 137,
         {
             "VSS": "BATT_RAW_N", "SRN": "BQ_SRN", "SRP": "BQ_SRP", "TS1": "TS",
             "SCL": "I2C_SCL", "SDA": "I2C_SDA", "DSG": "BQ_DSG", "PACK": "BQ_PACK",
             "CHG": "BQ_CHG", "PBI": "BQ_PBI", "VC2": "PROT_VDD", "VC1": "PROT_VC",
             "PWPD": "BATT_RAW_N",
         },
         "2S protection", "TI 1S/2S gas gauge and primary protection controller wired for the high-side N-FET topology from the BQ28Z610 reference design.",
         "https://www.ti.com/lit/ds/symlink/bq28z610.pdf")
    part(two, "RPROT_TOP", "470R 1%", "Resistor_SMD:R_0603_1608Metric", 195, 113, {"1": "BATT_RAW_P", "2": "PROT_VDD"}, "2S protection", "BQ28Z610 VC2 input resistor; place near U2 and verify with cell-balance routing.")
    part(two, "RPROT_MID", "470R 1%", "Resistor_SMD:R_0603_1608Metric", 195, 125, {"1": "CELL_MID", "2": "PROT_VC"}, "2S protection", "BQ28Z610 VC1 input resistor; place near U2 and verify with cell-balance routing.")
    part(two, "CPROT_TOP", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 195, 137, {"1": "PROT_VDD", "2": "PROT_VC"}, "2S protection", "BQ28Z610 upper-cell sense filter capacitor.")
    part(two, "CPROT_BOT", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 195, 149, {"1": "PROT_VC", "2": "BATT_RAW_N"}, "2S protection", "BQ28Z610 lower-cell sense filter capacitor.")
    part(two, "RPROT_VM", "100R", "Resistor_SMD:R_0603_1608Metric", 195, 161, {"1": "BAT_SYS", "2": "BQ_PACK"}, "2S protection", "BQ28Z610 PACK input series resistor from protected/system-side pack positive.")
    part(two, "CPBI", "2.2uF 10V X5R", "Capacitor_SMD:C_0603_1608Metric", 195, 173, {"1": "BQ_PBI", "2": "BATT_RAW_N"}, "2S protection", "BQ28Z610 PBI hold-up capacitor; place close to U2.")
    part(two, "RSENSE", "2mR 1% 50ppm", "Resistor_SMD:R_1206_3216Metric", 195, 185, {"1": "BATT_RAW_N", "2": "GND"}, "2S protection", "BQ28Z610 series current-sense resistor in the pack-negative return path; Kelvin route both ends.")
    part(two, "RSRN", "100R", "Resistor_SMD:R_0603_1608Metric", 235, 101, {"1": "GND", "2": "BQ_SRN"}, "2S protection", "BQ28Z610 SRN input filter resistor; Kelvin route from system/pack-negative side of R46.")
    part(two, "RSRP", "100R", "Resistor_SMD:R_0603_1608Metric", 235, 113, {"1": "BATT_RAW_N", "2": "BQ_SRP"}, "2S protection", "BQ28Z610 SRP input filter resistor; Kelvin route from raw-cell-negative side of R46.")
    part(two, "CSRN_SRP", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 235, 149, {"1": "BQ_SRN", "2": "BQ_SRP"}, "2S protection", "BQ28Z610 differential SRN/SRP filter capacitor; place close to U2.")
    part(two, "RDO_GATE", "100R", "Resistor_SMD:R_0603_1608Metric", 235, 125, {"1": "BQ_DSG", "2": "DO_GATE"}, "2S protection", "Small series gate resistor for BQ28Z610 discharge FET control.")
    part(two, "RCO_GATE", "100R", "Resistor_SMD:R_0603_1608Metric", 235, 137, {"1": "BQ_CHG", "2": "CO_GATE"}, "2S protection", "Small series gate resistor for BQ28Z610 charge FET control.")
    part("CSD18502Q5B", "QCHG", "CSD18502Q5B charge FET", "Daughterboard:CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm", 235, 158,
         {"D": "BAT_SYS", "S": "FET_SRC_COMMON", "G": "CO_GATE"},
         "2S protection", "High-side charge-blocking N-FET; drain on protected/system side, source at the common high-side FET node.")
    part("CSD18502Q5B", "QDSG", "CSD18502Q5B discharge FET", "Daughterboard:CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm", 275, 158,
         {"D": "FET_BATT_P", "S": "FET_SRC_COMMON", "G": "DO_GATE"},
         "2S protection", "High-side discharge-blocking N-FET; drain feeds the raw cell-stack positive through F4.")
    part(two, "RCHG_GS", "10M", "Resistor_SMD:R_0603_1608Metric", 235, 173, {"1": "CO_GATE", "2": "FET_SRC_COMMON"}, "2S protection", "Gate-source bleed for the high-side charge FET.")
    part(two, "RDSG_GS", "10M", "Resistor_SMD:R_0603_1608Metric", 275, 173, {"1": "DO_GATE", "2": "FET_SRC_COMMON"}, "2S protection", "Gate-source bleed for the high-side discharge FET.")
    part(two, "CCHG_FET", "100nF 25V X7R", "Capacitor_SMD:C_0603_1608Metric", 235, 185, {"1": "BAT_SYS", "2": "FET_SRC_COMMON"}, "2S protection", "ESD/transient capacitor across charge FET per BQ28Z610 layout guidance.")
    part(two, "CDSG_FET", "100nF 25V X7R", "Capacitor_SMD:C_0603_1608Metric", 275, 185, {"1": "FET_BATT_P", "2": "FET_SRC_COMMON"}, "2S protection", "ESD/transient capacitor across discharge FET per BQ28Z610 layout guidance.")

    part(two, "F4", "2A battery path fuse", "Fuse:Fuse_1206_3216Metric", 315, 137, {"1": "FET_BATT_P", "2": "BATT_RAW_P"}, "Battery", "SMD high-side pack fuse between protection FETs and raw cell-stack positive.")
    part("PWR_FLAG", "#FLG1", "PWR_FLAG", "", 315, 125, {"PWR": "BAT_SYS"}, "ERC", "Marks protected 2S system bus as powered for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG2", "PWR_FLAG", "", 315, 137, {"PWR": "CHG_IN"}, "ERC", "Marks USB charger input as externally powered for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG3", "PWR_FLAG", "", 315, 149, {"PWR": "GND"}, "ERC", "Marks protected ground as driven for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG4", "PWR_FLAG", "", 315, 161, {"PWR": "BATT_RAW_N"}, "ERC", "Marks raw pack negative as powered for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG5", "PWR_FLAG", "", 315, 173, {"PWR": "PROT_VDD"}, "ERC", "Marks filtered protector VDD as powered for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG6", "PWR_FLAG", "", 315, 185, {"PWR": "+3V3_SW"}, "ERC", "Marks switched 3.3V rail as powered for ERC.", in_bom=False, on_board=False)
    part("PWR_FLAG", "#FLG7", "PWR_FLAG", "", 315, 197, {"PWR": "+3V3_SW_LOGIC"}, "ERC", "Marks local switched 3.3V logic branch as powered for ERC.", in_bom=False, on_board=False)

    part("AP63200WU", "U3", "AP63200WU 5V Buck", "Package_TO_SOT_SMD:TSOT-23-6", 70, 220,
         {"IN": "BAT_SYS", "EN": "EN_5V", "GND": "GND", "FB": "FB_5V", "BST": "BST_5V", "SW": "SW_5V"},
         "5V buck", "2S-to-5V buck regulator; 5V / 1A target.")
    part(two, "L1", "4.7uH >=3A sat", "Inductor_SMD:L_APV_ANR4020", 115, 205, {"1": "SW_5V", "2": "+5V"}, "5V buck", "Low-DCR shielded SMD inductor.")
    part(two, "C5IN1", "10uF 16V X5R", "Capacitor_SMD:C_0805_2012Metric", 115, 217, {"1": "BAT_SYS", "2": "GND"}, "5V buck", "Input bulk.")
    part(two, "C5IN2", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 115, 229, {"1": "BAT_SYS", "2": "GND"}, "5V buck", "Input bypass.")
    part(two, "C5OUT1", "22uF 10V X5R", "Capacitor_SMD:C_0805_2012Metric", 155, 205, {"1": "+5V", "2": "GND"}, "5V buck", "Output bulk.")
    part(two, "C5OUT2", "22uF 10V X5R", "Capacitor_SMD:C_0805_2012Metric", 155, 217, {"1": "+5V", "2": "GND"}, "5V buck", "Output bulk.")
    part(two, "C5BST", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 155, 229, {"1": "BST_5V", "2": "SW_5V"}, "5V buck", "Bootstrap capacitor.")
    part(two, "R5FB1", "523k 1%", "Resistor_SMD:R_0603_1608Metric", 155, 241, {"1": "+5V", "2": "FB_5V"}, "5V buck", "Feedback top for 5V from 0.8V reference.")
    part(two, "R5FB2", "100k 1%", "Resistor_SMD:R_0603_1608Metric", 155, 253, {"1": "FB_5V", "2": "GND"}, "5V buck", "Feedback bottom.")
    part(two, "R5EN", "100k", "Resistor_SMD:R_0603_1608Metric", 115, 241, {"1": "EN_5V", "2": "BAT_SYS"}, "5V buck", "Default 5V buck enabled when pack is present.")

    part("AP63200WU", "U4", "AP63200WU 3V3 Buck", "Package_TO_SOT_SMD:TSOT-23-6", 70, 285,
         {"IN": "BAT_SYS", "EN": "EN_3V3", "GND": "GND", "FB": "FB_3V3", "BST": "BST_3V3", "SW": "SW_3V3"},
         "3V3 buck", "2S-to-3.3V buck regulator; 3.3V / 500mA target.")
    part(two, "L2", "4.7uH >=2A sat", "Inductor_SMD:L_APV_ANR4020", 115, 270, {"1": "SW_3V3", "2": "+3V3"}, "3V3 buck", "Low-DCR shielded SMD inductor.")
    part(two, "C3IN1", "10uF 16V X5R", "Capacitor_SMD:C_0805_2012Metric", 115, 282, {"1": "BAT_SYS", "2": "GND"}, "3V3 buck", "Input bulk.")
    part(two, "C3IN2", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 115, 294, {"1": "BAT_SYS", "2": "GND"}, "3V3 buck", "Input bypass.")
    part(two, "C3OUT1", "22uF 10V X5R", "Capacitor_SMD:C_0805_2012Metric", 155, 270, {"1": "+3V3", "2": "GND"}, "3V3 buck", "Output cap.")
    part(two, "C3OUT2", "22uF 10V X5R", "Capacitor_SMD:C_0805_2012Metric", 155, 282, {"1": "+3V3", "2": "GND"}, "3V3 buck", "Output cap.")
    part(two, "C3BST", "100nF 16V X7R", "Capacitor_SMD:C_0603_1608Metric", 155, 294, {"1": "BST_3V3", "2": "SW_3V3"}, "3V3 buck", "Bootstrap capacitor.")
    part(two, "R3FB1", "316k 1%", "Resistor_SMD:R_0603_1608Metric", 155, 306, {"1": "+3V3", "2": "FB_3V3"}, "3V3 buck", "Feedback top for 3.3V from 0.8V reference.")
    part(two, "R3FB2", "100k 1%", "Resistor_SMD:R_0603_1608Metric", 155, 318, {"1": "FB_3V3", "2": "GND"}, "3V3 buck", "Feedback bottom.")
    part(two, "R3EN", "100k", "Resistor_SMD:R_0603_1608Metric", 115, 306, {"1": "EN_3V3", "2": "BAT_SYS"}, "3V3 buck", "Default 3.3V buck enabled when pack is present.")

    pololu_2808_fp = "Daughterboard:Pololu_2808_Raised_Header"
    part("POLOLU_2808", "SW1", "Pololu #2808 5V switch", pololu_2808_fp, 230, 215,
         {"VIN": "+5V", "VOUT": "+5V_SW", "GND": "GND", "CTRL": "CTRL_5V_SW"}, "Output switching", "VIN=+5V, VOUT=+5V_SW.")
    part("POLOLU_2808", "SW2", "Pololu #2808 3V3 switch", pololu_2808_fp, 230, 270,
         {"VIN": "+3V3", "VOUT": "+3V3_SW", "GND": "GND", "CTRL": "CTRL_3V3_SW"}, "Output switching", "VIN=+3V3, VOUT=+3V3_SW.")
    part(two, "RCTRL1", "330R", "Resistor_SMD:R_0603_1608Metric", 230, 242, {"1": "GPIO2", "2": "CTRL_5V_SW"}, "Output switching", "Series resistor from ESP32 GPIO2.")
    part(two, "RCTRL2", "330R", "Resistor_SMD:R_0603_1608Metric", 230, 297, {"1": "GPIO3", "2": "CTRL_3V3_SW"}, "Output switching", "Series resistor from ESP32 GPIO3.")
    hosyond_pad_fp = "TestPoint:TestPoint_Pad_1.5x1.5mm"
    hosyond_pads = [
        ("J30", "J3P", "+5V_SW", 295, 188),
        ("J31", "J3A", "UART_TX", 295, 198),
        ("J32", "J3B", "UART_RX", 295, 208),
        ("J33", "J3C", "I2C_SDA", 295, 218),
        ("J34", "J3D", "I2C_SCL", 295, 228),
        ("J35", "J3E", "GPIO2", 295, 238),
        ("J36", "J3F", "GPIO3", 295, 248),
        ("J37", "J3G", "GPIO14", 295, 258),
        ("J38", "J3H", "GPIO21", 295, 268),
        ("J39", "J3I", "GND", 295, 278),
    ]
    for ref, pad_name, net, x, y in hosyond_pads:
        part(tp, ref, f"{pad_name} Hosyond {net} solder pad", hosyond_pad_fp, x, y, {"PAD": net},
             "Control", "Individual solder-wire pad for wire from Hosyond ESP32-S3 board.")
    part("CONN_3", "J2", "Switched power outputs", "", 275, 220, {"1": "+5V_SW", "2": "+3V3_SW", "3": "GND"}, "Outputs", "External switched-load connector.")

    part("CONN_5", "J4", "UART JST-GH 1.25mm", "Connector_JST:JST_GH_SM05B-GHS-TB_1x05-1MP_P1.25mm_Horizontal", 390, 190,
         {"1": "+5V_SW", "2": "GND", "3": "UART_TX", "4": "UART_RX", "5": "GND"},
         "Signals", "5-pin 1.25mm JST UART header: +5V_SW,GND,TX,RX,GND.")
    part("CONN_4", "J5", "I2C male header 2.54mm", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical", 390, 230,
         {"1": "+3V3_SW", "2": "GND", "3": "I2C_SDA", "4": "I2C_SCL"},
         "Signals", "4-pin 2.54mm I2C header: +3V3_SW,GND,SDA,SCL.")
    part("CONN_2", "J6", "Deep sleep pushbutton header", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", 390, 265,
         {"1": "GPIO14", "2": "GPIO21"},
         "Signals", "2-pin 2.54mm header for pushbutton between GPIO14 and GPIO21.")

    part("ADS1115IDGS", "U5", "ADS1115IDGS 0x48", "Package_SO:TSSOP-10_3x3mm_P0.5mm", 360, 52,
         {"AIN0": "AIN0", "AIN1": "AIN1", "AIN2": "AIN2", "AIN3": "AIN3", "ADDR": "GND",
          "SCL": "I2C_SCL", "SDA": "I2C_SDA", "ALERT/RDY": None, "VDD": "+3V3_SW_LOGIC", "GND": "GND"},
         "Analog inputs", "First 4-channel ADS1115; ADDR=GND for I2C address 0x48.")
    part("ADS1115IDGS", "U6", "ADS1115IDGS 0x49", "Package_SO:TSSOP-10_3x3mm_P0.5mm", 360, 112,
         {"AIN0": "AIN4", "AIN1": "AIN5", "AIN2": "AIN6", "AIN3": "AIN7", "ADDR": "+3V3_SW_LOGIC",
          "SCL": "I2C_SCL", "SDA": "I2C_SDA", "ALERT/RDY": None, "VDD": "+3V3_SW_LOGIC", "GND": "GND"},
         "Analog inputs", "Second 4-channel ADS1115; ADDR=VDD for I2C address 0x49.")
    part(two, "R19", "4.7k", "Resistor_SMD:R_0603_1608Metric", 405, 34,
         {"1": "I2C_SDA", "2": "+3V3_SW_LOGIC"}, "Analog inputs", "Local I2C SDA pullup for switched onboard ADCs.")
    part(two, "R20", "4.7k", "Resistor_SMD:R_0603_1608Metric", 405, 46,
         {"1": "I2C_SCL", "2": "+3V3_SW_LOGIC"}, "Analog inputs", "Local I2C SCL pullup for switched onboard ADCs.")
    part(two, "C17", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 405, 62,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "Analog inputs", "U5 local VDD bypass.")
    part(two, "C18", "1uF 10V X5R", "Capacitor_SMD:C_0603_1608Metric", 405, 74,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "Analog inputs", "U5 local supply bulk.")
    part(two, "C19", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 405, 122,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "Analog inputs", "U6 local VDD bypass.")
    part(two, "C20", "1uF 10V X5R", "Capacitor_SMD:C_0603_1608Metric", 405, 134,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "Analog inputs", "U6 local supply bulk.")
    gimbal_jst_fp = "Connector_JST:JST_GH_BM03B-GHS-TBT_1x03-1MP_P1.25mm_Vertical"
    gimbal_headers = [
        ("J8", "Left gimbal A", "AIN0", 420, 150),
        ("J40", "Left gimbal B", "AIN1", 420, 178),
        ("J41", "Right gimbal A", "AIN2", 470, 150),
        ("J42", "Right gimbal B", "AIN3", 470, 178),
    ]
    for ref, label, ain_net, x, y in gimbal_headers:
        part("CONN_3", ref, f"{label} JST-GH 1.25mm", gimbal_jst_fp, x, y,
             {"1": "GND", "2": ain_net, "3": "+3V3_SW"},
             "Analog inputs", f"3-pin gimbal potentiometer input: GND,{ain_net},+3V3_SW.")
    part(two, "R45", "0R", "Resistor_SMD:R_0603_1608Metric", 405, 22,
         {"1": "+3V3_SW", "2": "+3V3_SW_LOGIC"}, "Analog inputs",
         "Net-tie jumper: heavy switched 3.3V rail to light local ADC/MCP logic branch.")

    mcp_nets = {
        "VDD": "+3V3_SW_LOGIC", "VSS": "GND", "SCL": "I2C_SCL", "SDA": "I2C_SDA",
        "~{RESET}": "MCP_RESET", "INTA": "MCP_INTA", "INTB": "MCP_INTB",
        "A0": "MCP_A0", "A1": "MCP_A1", "A2": "MCP_A2", "NC1": None, "NC2": None,
    }
    for bank in ("A", "B"):
        for idx in range(8):
            mcp_nets[f"GP{bank}{idx}"] = f"MCP_GP{bank}{idx}"
    part("MCP23017_SO", "U7", "MCP23017-E/SO", "Daughterboard:SOIC-28W_7.5x17.9mm_P1.27mm_NoPads11_14", 325, 280,
         mcp_nets, "GPIO expander",
         "16-bit I2C GPIO expander at address 0x20; powered from the light +3V3_SW_LOGIC branch. Treat GPA7/GPB7 as output-only unless the selected MCP23017 variant is verified.")
    part(two, "C27", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 262, 245,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "GPIO expander", "MCP23017 local VDD bypass, place close to U7.")
    part(two, "C28", "1uF 10V X5R", "Capacitor_SMD:C_0603_1608Metric", 262, 257,
         {"1": "+3V3_SW_LOGIC", "2": "GND"}, "GPIO expander", "MCP23017 local supply bulk.")
    part(two, "R39", "0R", "Resistor_SMD:R_0603_1608Metric", 262, 269,
         {"1": "MCP_A0", "2": "GND"}, "GPIO expander", "Default address strap low; A2:A0=000 gives I2C address 0x20.")
    part(two, "R40", "0R", "Resistor_SMD:R_0603_1608Metric", 262, 281,
         {"1": "MCP_A1", "2": "GND"}, "GPIO expander", "Default address strap low.")
    part(two, "R41", "0R", "Resistor_SMD:R_0603_1608Metric", 262, 293,
         {"1": "MCP_A2", "2": "GND"}, "GPIO expander", "Default address strap low.")
    part(two, "R42", "10k", "Resistor_SMD:R_0603_1608Metric", 262, 305,
         {"1": "+3V3_SW_LOGIC", "2": "MCP_RESET"}, "GPIO expander", "Reset pullup to switched 3.3V logic branch.")
    part(two, "C29", "100nF 10V X7R", "Capacitor_SMD:C_0603_1608Metric", 262, 317,
         {"1": "MCP_RESET", "2": "GND"}, "GPIO expander", "Optional reset delay/noise filter; omit if firmware needs immediate reset release.")
    part(two, "R43", "10k", "Resistor_SMD:R_0603_1608Metric", 332, 245,
         {"1": "+3V3_SW_LOGIC", "2": "MCP_INTA"}, "GPIO expander", "INTA pullup; also available on a solder pad.")
    part(two, "R44", "10k", "Resistor_SMD:R_0603_1608Metric", 332, 257,
         {"1": "+3V3_SW_LOGIC", "2": "MCP_INTB"}, "GPIO expander", "INTB pullup; also available on a solder pad.")

    gpio_series = [
        ("R23", "MCP_GPA0", "EXP_GPA0", 390, 285), ("R24", "MCP_GPA1", "EXP_GPA1", 390, 297),
        ("R25", "MCP_GPA2", "EXP_GPA2", 390, 309), ("R26", "MCP_GPA3", "EXP_GPA3", 390, 321),
        ("R27", "MCP_GPA4", "EXP_GPA4", 390, 333), ("R28", "MCP_GPA5", "EXP_GPA5", 390, 345),
        ("R29", "MCP_GPA6", "EXP_GPA6", 390, 357), ("R30", "MCP_GPA7", "EXP_GPA7", 390, 369),
        ("R31", "MCP_GPB0", "EXP_GPB0", 455, 285), ("R32", "MCP_GPB1", "EXP_GPB1", 455, 297),
        ("R33", "MCP_GPB2", "EXP_GPB2", 455, 309), ("R34", "MCP_GPB3", "EXP_GPB3", 455, 321),
        ("R35", "MCP_GPB4", "EXP_GPB4", 455, 333), ("R36", "MCP_GPB5", "EXP_GPB5", 455, 345),
        ("R37", "MCP_GPB6", "EXP_GPB6", 455, 357), ("R38", "MCP_GPB7", "EXP_GPB7", 455, 369),
    ]
    for ref, chip_net, pad_net, x, y in gpio_series:
        part(two, ref, "100R", "Resistor_SMD:R_0603_1608Metric", x, y,
             {"1": chip_net, "2": pad_net}, "GPIO expander", "Series resistor between MCP23017 GPIO and external solder pad.")

    esd_groups = [
        ("D4", ("EXP_GPA0", "EXP_GPA1", "EXP_GPA2", "EXP_GPA3"), 365, 430),
        ("D5", ("EXP_GPA4", "EXP_GPA5", "EXP_GPA6", "EXP_GPA7"), 425, 430),
        ("D6", ("EXP_GPB0", "EXP_GPB1", "EXP_GPB2", "EXP_GPB3"), 485, 430),
        ("D7", ("EXP_GPB4", "EXP_GPB5", "EXP_GPB6", "EXP_GPB7"), 545, 430),
    ]
    for ref, nets, x, y in esd_groups:
        part("ESD_ARRAY_4CH", ref, "4ch 3.3V GPIO TVS array TBD", "Daughterboard:SOT-23-6_NoPad6", x, y,
             {"IO1": nets[0], "IO2": nets[1], "IO3": nets[2], "IO4": nets[3], "GND": "GND", "NC": None},
             "GPIO expander", "ESD protection placeholder for four exposed MCP23017 solder-pad lines; choose exact low-cap 3.3V array before layout.")

    gpio_pad_fp = "TestPoint:TestPoint_Pad_1.5x1.5mm"
    gpio_pads = []
    for idx in range(8):
        gpio_pads.append((f"J9{idx + 1}", f"MCP23017 GPA{idx}", f"EXP_GPA{idx}", 520, 285 + idx * 12))
    for idx in range(8):
        gpio_pads.append((f"J9{idx + 9}", f"MCP23017 GPB{idx}", f"EXP_GPB{idx}", 585, 285 + idx * 12))
    gpio_pads.extend([
        ("J925", "MCP23017 INTA", "MCP_INTA", 520, 390),
        ("J926", "MCP23017 INTB", "MCP_INTB", 585, 390),
        ("J927", "MCP23017 RESET", "MCP_RESET", 520, 402),
        ("J928", "MCP23017 +3V3_SW_LOGIC", "+3V3_SW_LOGIC", 585, 402),
        ("J929", "MCP23017 GND", "GND", 520, 414),
    ])
    for ref, pad_name, net, x, y in gpio_pads:
        part(tp, ref, f"{pad_name} solder pad", gpio_pad_fp, x, y, {"PAD": net},
             "GPIO expander", "Individual solder-wire pad for MCP23017 GPIO/utility breakout.")

    testpads = [
        ("TP1", "USB_VBUS", 620, 32), ("TP2", "CHG_IN", 620, 42), ("TP3", "BAT_SYS", 620, 52),
        ("TP4", "CELL_MID", 620, 62), ("TP5", "BAT_SYS", 620, 72), ("TP6", "+5V", 620, 82),
        ("TP7", "+3V3", 620, 92), ("TP8", "+5V_SW", 620, 102), ("TP9", "+3V3_SW", 620, 112),
        ("TP10", "CHG_STAT", 620, 122), ("TP11", "PG_STAT", 620, 132), ("TP12", "BATT_RAW_N", 620, 142),
        ("TP13", "BATT_RAW_P", 620, 152),
        ("TP14", "GND", 620, 162), ("TP15", "AIN4", 620, 172), ("TP16", "AIN5", 620, 182),
        ("TP17", "AIN6", 620, 192), ("TP18", "AIN7", 620, 202),
    ]
    for ref, net, x, y in testpads:
        part(tp, ref, f"{net} test pad", "TestPoint:TestPoint_Pad_D1.0mm", x, y, {"PAD": net}, "Test pads", "Debug/access pad.")

    return p


def schematic(symbols: dict[str, SymbolDef], parts: list[Part]) -> str:
    out = [
        "(kicad_sch\n",
        "\t(version 20250114)\n",
        '\t(generator "codex")\n',
        '\t(generator_version "1.1")\n',
        f'\t(uuid "{ROOT_UUID}")\n',
        '\t(paper "A1")\n',
        "\t(title_block\n",
        '\t\t(title "USB-C 2S LiPo / 2x18650 Power Board - Pin-Level")\n',
        '\t\t(date "2026-06-30")\n',
        '\t\t(rev "A-pinlevel")\n',
        '\t\t(company "Daughterboard")\n',
        '\t\t(comment 1 "2S source: J7 pack OR installed series 18650s; do not use both at once.")\n',
        '\t\t(comment 2 "2S protection included; verify FET margin, thresholds, and charger settings before fabrication.")\n',
        "\t)\n",
        "\t(lib_symbols\n",
    ]
    for sym in symbols.values():
        out.append(symbol_def(sym))
    out.append("\t)\n")

    for part in parts:
        sym = symbols[part.symbol]
        out.append(part_instance(part, sym))
        for pin in sym.pins:
            net = part.nets.get(pin.name, part.nets.get(pin.number))
            px, py, _ = pin_xy(sym, pin)
            sx, sy = stub_xy(sym, pin, part.x, part.y)
            x1, y1 = part.x + px, part.y + py
            y1 = part.y - py
            if net is None:
                out.append(no_connect(x1, y1))
            elif net:
                out.append(wire_label(x1, y1, sx, sy, net, inline=(part.section == "GPIO expander")))

    out.append("\t(sheet_instances\n")
    out.append(f'\t\t(path "/" (page "1"))\n')
    out.append("\t)\n")
    out.append("\t(embedded_fonts no)\n")
    out.append(")\n")
    return "".join(out)


def write_bom(parts: list[Part]) -> None:
    bom_path = Path("Daughterboard_pinlevel_bom.csv")
    with bom_path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["Section", "Reference", "Part / Value", "Footprint", "Nets", "Notes", "Purchase Link"])
        for part in parts:
            if not part.in_bom:
                continue
            nets = ",".join(sorted({net for net in part.nets.values() if net}))
            writer.writerow([part.section, part.ref, part.value, part.footprint, nets, part.notes, purchase_link_for(part)])
    bom_text = bom_path.read_text()
    for duplicate in (
        "Daughterboard_pcb_bom.csv",
        "pcb_component_schedule.csv",
        "starter_bom_and_nets.csv",
        "starter_bom_and_nets_with_links.csv",
    ):
        Path(duplicate).write_text(bom_text, newline="\n")


def write_project_symbol_library(symbols: dict[str, SymbolDef]) -> None:
    out = [
        "(kicad_symbol_lib\n",
        "\t(version 20250114)\n",
        '\t(generator "codex")\n',
        '\t(generator_version "1.1")\n',
    ]
    for sym in symbols.values():
        out.append(symbol_def(sym, embedded=False))
    out.append(")\n")
    Path("Daughterboard.kicad_sym").write_text("".join(out), newline="\n")
    Path("sym-lib-table").write_text(
        "(sym_lib_table\n"
        "\t(version 7)\n"
        '\t(lib (name "Daughterboard") (type "KiCad") (uri "${KIPRJMOD}/Daughterboard.kicad_sym") (options "") (descr "Daughterboard project-local symbols"))\n'
        ")\n",
        newline="\n",
    )


def write_project_footprint_library() -> None:
    pretty = Path("Daughterboard.pretty")
    pretty.mkdir(exist_ok=True)
    footprint = """(footprint "CSD83325L_YJE0006A"
\t(version 20240108)
\t(generator "codex")
\t(layer "F.Cu")
\t(descr "TI CSD83325L, YJE0006A PicoStar/LGA-6, 2.2x1.15mm body, 0.65mm pitch, 0.30mm NSMD pads")
\t(tags "CSD83325L YJE0006A PicoStar LGA-6 dual common-drain MOSFET")
\t(property "Reference" "Q" (at 0 -1.8 0) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Value" "CSD83325L" (at 0 1.8 0) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Footprint" "" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(property "Datasheet" "https://www.ti.com/lit/ds/symlink/csd83325l.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(attr smd)
\t(fp_line (start -0.575 -1.1) (end 0.575 -1.1) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 0.575 -1.1) (end 0.575 1.1) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 0.575 1.1) (end -0.575 1.1) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start -0.575 1.1) (end -0.575 -1.1) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start -0.575 -1.1) (end -0.25 -1.1) (stroke (width 0.1) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -0.575 -1.1) (end -0.575 -0.75) (stroke (width 0.1) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -0.575 -1.1) (end 0.575 -1.1) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 0.575 -1.1) (end 0.575 1.1) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 0.575 1.1) (end -0.575 1.1) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -0.575 1.1) (end -0.575 -1.1) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (effects (font (size 0.35 0.35) (thickness 0.05))))
\t(pad "A1" smd circle (at -0.325 -0.65) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
\t(pad "A2" smd circle (at 0.325 -0.65) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
\t(pad "B1" smd circle (at -0.325 0) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
\t(pad "B2" smd circle (at 0.325 0) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
\t(pad "C1" smd circle (at -0.325 0.65) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
\t(pad "C2" smd circle (at 0.325 0.65) (size 0.3 0.3) (layers "F.Cu" "F.Paste" "F.Mask"))
)
"""
    (pretty / "CSD83325L_YJE0006A.kicad_mod").write_text(footprint, newline="\n")
    csd18502q5b = """(footprint "CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm"
\t(version 20240108)
\t(generator "codex")
\t(layer "F.Cu")
\t(descr "TI CSD18502Q5B, DNK0008A / VSON-CLIP-8, 5x6mm NexFET, JLCPCB C473915")
\t(tags "CSD18502Q5B DNK0008A VSON-CLIP Q5B NexFET C473915")
\t(property "Reference" "Q" (at 0 -3.45 0) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Value" "CSD18502Q5B" (at 0 3.45 0) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Footprint" "" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(property "Datasheet" "https://www.ti.com/lit/ds/symlink/csd18502q5b.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(attr smd)
\t(duplicate_pad_numbers_are_jumpers no)
\t(fp_line (start -3.35 -2.6) (end 3.35 -2.6) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 3.35 -2.6) (end 3.35 2.6) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 3.35 2.6) (end -3.35 2.6) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start -3.35 2.6) (end -3.35 -2.6) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start -2.5 -3.0) (end -1.8 -3.0) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -2.5 -3.0) (end -2.5 -2.35) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -2.5 3.0) (end 2.5 3.0) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -2.5 -3.0) (end 2.5 -3.0) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 2.5 -3.0) (end 2.5 3.0) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 2.5 3.0) (end -2.5 3.0) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -2.5 3.0) (end -2.5 -3.0) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_circle (center -2.95 -2.75) (end -2.85 -2.75) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))
\t(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (effects (font (size 0.6 0.6) (thickness 0.08))))
\t(pad "" smd rect (at -1.15 -1.27) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "" smd rect (at -1.15 0) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "" smd rect (at -1.15 1.27) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "" smd rect (at 0.25 -1.27) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "" smd rect (at 0.25 0) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "" smd rect (at 0.25 1.27) (size 1.20 1.05) (layers "F.Paste"))
\t(pad "5" smd roundrect (at -0.65 0) (size 4.44 4.52) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.03))
\t(pad "5" smd roundrect (at -2.7 -1.905) (size 0.72 0.72) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "6" smd roundrect (at -2.7 -0.635) (size 0.72 0.72) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "7" smd roundrect (at -2.7 0.635) (size 0.72 0.72) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "8" smd roundrect (at -2.7 1.905) (size 0.72 0.72) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "4" smd roundrect (at 2.55 -1.905) (size 1.10 0.71) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "3" smd roundrect (at 2.55 -0.635) (size 1.10 0.71) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "2" smd roundrect (at 2.55 0.635) (size 1.10 0.71) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
\t(pad "1" smd roundrect (at 2.55 1.905) (size 1.10 0.71) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
)
"""
    (pretty / "CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm.kicad_mod").write_text(csd18502q5b, newline="\n")
    bq28z610 = """(footprint "BQ28Z610_DRZ0012A_12SON_2.5x4mm_P0.5mm"
\t(version 20240108)
\t(generator "codex")
\t(layer "F.Cu")
\t(descr "TI BQ28Z610, DRZ0012A VSON-12, 2.5x4.0mm body, 0.5mm pitch, exposed pad")
\t(tags "BQ28Z610 DRZ0012A VSON SON-12 0.5mm")
\t(property "Reference" "U" (at 0 -2.9 0) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Value" "BQ28Z610DRZR-R1" (at 0 2.9 0) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(property "Footprint" "" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(property "Datasheet" "https://www.ti.com/lit/ds/symlink/bq28z610.pdf" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(attr smd)
\t(fp_line (start -1.25 -2) (end 1.25 -2) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 1.25 -2) (end 1.25 2) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 1.25 2) (end -1.25 2) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -1.25 2) (end -1.25 -2) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -1.25 -2) (end -0.85 -2.4) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -1.55 -2.15) (end -1.55 -1.65) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -1.55 -2.15) (end -1.05 -2.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start 1.55 -2.15) (end 1.55 -1.65) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start 1.55 -2.15) (end 1.05 -2.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -1.55 2.15) (end -1.55 1.65) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start -1.55 2.15) (end -1.05 2.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start 1.55 2.15) (end 1.55 1.65) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_line (start 1.55 2.15) (end 1.05 2.15) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
\t(fp_circle (center -1.75 -2.25) (end -1.65 -2.25) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))
\t(fp_line (start -2.05 -2.35) (end 2.05 -2.35) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 2.05 -2.35) (end 2.05 2.35) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start 2.05 2.35) (end -2.05 2.35) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_line (start -2.05 2.35) (end -2.05 -2.35) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
\t(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (effects (font (size 0.45 0.45) (thickness 0.06))))
\t(pad "1" smd roundrect (at -1.225 -1.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "2" smd roundrect (at -1.225 -0.75) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "3" smd roundrect (at -1.225 -0.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "4" smd roundrect (at -1.225 0.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "5" smd roundrect (at -1.225 0.75) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "6" smd roundrect (at -1.225 1.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "7" smd roundrect (at 1.225 1.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "8" smd roundrect (at 1.225 0.75) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "9" smd roundrect (at 1.225 0.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "10" smd roundrect (at 1.225 -0.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "11" smd roundrect (at 1.225 -0.75) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "12" smd roundrect (at 1.225 -1.25) (size 0.6 0.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
\t(pad "13" smd roundrect (at 0 0) (size 1.3 2.1) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.08))
)
"""
    (pretty / "BQ28Z610_DRZ0012A_12SON_2.5x4mm_P0.5mm.kicad_mod").write_text(bq28z610, newline="\n")
    pololu_2808 = """(footprint "Pololu_2808_Raised_Header"
\t(version 20240108)
\t(generator "codex")
\t(layer "F.Cu")
\t(descr "Pololu 2808 mini pushbutton power switch carrier, raised on 2.54mm headers; body outline only, no full-body courtyard")
\t(tags "Pololu 2808 pushbutton power switch raised header")
\t(property "Reference" "SW" (at 0 -10.6 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))
\t(property "Value" "Pololu_2808" (at 0 10.6 0) (layer "F.Fab") (effects (font (size 0.8 0.8) (thickness 0.12))))
\t(property "Footprint" "" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(property "Datasheet" "https://www.pololu.com/product/2808" (at 0 0 0) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
\t(attr through_hole)
\t(fp_line (start -7.6 -8.9) (end 7.6 -8.9) (stroke (width 0.12) (type dash)) (layer "F.SilkS"))
\t(fp_line (start 7.6 -8.9) (end 7.6 8.9) (stroke (width 0.12) (type dash)) (layer "F.SilkS"))
\t(fp_line (start 7.6 8.9) (end -7.6 8.9) (stroke (width 0.12) (type dash)) (layer "F.SilkS"))
\t(fp_line (start -7.6 8.9) (end -7.6 -8.9) (stroke (width 0.12) (type dash)) (layer "F.SilkS"))
\t(fp_line (start -7.6 -8.9) (end 7.6 -8.9) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 7.6 -8.9) (end 7.6 8.9) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start 7.6 8.9) (end -7.6 8.9) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_line (start -7.6 8.9) (end -7.6 -8.9) (stroke (width 0.1) (type solid)) (layer "F.Fab"))
\t(fp_text user "VIN" (at -4.7 -6.35 0) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(fp_text user "GND" (at -4.4 -1.27 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.1))))
\t(fp_text user "CTRL" (at -4.0 7.62 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.1))))
\t(fp_text user "VOUT" (at 3.8 -6.35 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.1))))
\t(fp_text user "GND" (at 4.4 -1.27 0) (layer "F.SilkS") (effects (font (size 0.65 0.65) (thickness 0.1))))
\t(fp_text user "${REFERENCE}" (at 0 0 0) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
\t(pad "1" thru_hole rect (at -6.35 -7.62) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "1" thru_hole circle (at -6.35 -5.08) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "3" thru_hole circle (at -6.35 -2.54) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "3" thru_hole circle (at -6.35 0) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "4" thru_hole circle (at -6.35 7.62) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "2" thru_hole circle (at 6.35 -7.62) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "2" thru_hole circle (at 6.35 -5.08) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "3" thru_hole circle (at 6.35 -2.54) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
\t(pad "3" thru_hole circle (at 6.35 0) (size 1.8 1.8) (drill 1.02) (layers "*.Cu" "*.Mask"))
)
"""
    (pretty / "Pololu_2808_Raised_Header.kicad_mod").write_text(pololu_2808, newline="\n")
    Path("fp-lib-table").write_text(
        "(fp_lib_table\n"
        "\t(version 7)\n"
        '\t(lib (name "Daughterboard") (type "KiCad") (uri "${KIPRJMOD}/Daughterboard.pretty") (options "") (descr "Daughterboard project-local footprints"))\n'
        ")\n",
        newline="\n",
    )


def main() -> None:
    all_symbols = make_symbols()
    parts = make_parts()
    symbols = {name: all_symbols[name] for name in dict.fromkeys(part.symbol for part in parts)}
    Path("Daughterboard.kicad_sch").write_text(schematic(symbols, parts), newline="\n")
    write_project_symbol_library(symbols)
    write_project_footprint_library()
    write_bom(parts)


if __name__ == "__main__":
    main()
