from __future__ import annotations

from pathlib import Path

import pcbnew


BOARD_PATH = Path("Daughterboard.kicad_pcb")


EXPECTED_PAD_NETS = {
    ("J1", "A12"): "GND",
    ("J1", "B12"): "GND",
    ("J1", "SH"): "GND",
    ("U1", "13"): "BAT_SYS",
    ("U1", "14"): "BAT_SYS",
    ("U1", "15"): "BAT_SYS",
    ("U1", "16"): "BAT_SYS",
    ("U1", "9"): "CELL_MID",
    ("U1", "19"): "GND",
    ("U1", "20"): "GND",
    ("U1", "23"): "CHG_IN",
    ("U2", "1"): "BATT_RAW_N",
    ("U2", "2"): "BQ_SRN",
    ("U2", "3"): "BQ_SRP",
    ("U2", "7"): "BQ_DSG",
    ("U2", "8"): "BQ_PACK",
    ("U2", "9"): "BQ_CHG",
    ("U2", "11"): "PROT_VDD",
    ("U2", "12"): "PROT_VC",
    ("U2", "13"): "BATT_RAW_N",
    ("R46", "1"): "BATT_RAW_N",
    ("R46", "2"): "GND",
    ("R47", "1"): "GND",
    ("R47", "2"): "BQ_SRN",
    ("R48", "1"): "BATT_RAW_N",
    ("R48", "2"): "BQ_SRP",
    ("R16", "1"): "BAT_SYS",
    ("R16", "2"): "BQ_PACK",
    ("R21", "1"): "BQ_DSG",
    ("R21", "2"): "DO_GATE",
    ("R22", "1"): "BQ_CHG",
    ("R22", "2"): "CO_GATE",
    ("F3", "2"): "BATT_RAW_P",
    ("F4", "1"): "FET_BATT_P",
    ("F4", "2"): "BATT_RAW_P",
    ("J7", "1"): "BATT_RAW_N",
    ("J7", "2"): "CELL_MID",
    ("J7", "3"): "BATT_RAW_P",
}

EXPECTED_DUPLICATE_PAD_NETS = {
    ("Q1", "1"): "FET_SRC_COMMON",
    ("Q1", "2"): "CO_GATE",
    ("Q1", "3"): "BAT_SYS",
    ("Q2", "1"): "FET_SRC_COMMON",
    ("Q2", "2"): "DO_GATE",
    ("Q2", "3"): "FET_BATT_P",
}

FORBIDDEN_NET_NAMES = {
    "Q1_DRAIN_COMMON",
    "PACK_P",
}


def pad_net_name(pad: pcbnew.PAD) -> str:
    net = pad.GetNet()
    return net.GetNetname() if net else ""


def footprint(board: pcbnew.BOARD, ref: str) -> pcbnew.FOOTPRINT | None:
    return board.FindFootprintByReference(ref)


def check_single_pad(board: pcbnew.BOARD, ref: str, pad_num: str, expected: str) -> str | None:
    fp = footprint(board, ref)
    if fp is None:
        return f"{ref}: missing footprint"
    pad = fp.FindPadByNumber(pad_num)
    if pad is None:
        return f"{ref}.{pad_num}: missing pad"
    actual = pad_net_name(pad)
    if actual != expected:
        return f"{ref}.{pad_num}: expected {expected}, got {actual or '<none>'}"
    return None


def check_duplicate_pads(board: pcbnew.BOARD, ref: str, pad_num: str, expected: str) -> list[str]:
    fp = footprint(board, ref)
    if fp is None:
        return [f"{ref}: missing footprint"]
    matched = 0
    errors: list[str] = []
    for pad in fp.Pads():
        if pad.GetNumber() != pad_num:
            continue
        matched += 1
        actual = pad_net_name(pad)
        if actual != expected:
            errors.append(f"{ref}.{pad_num}: expected {expected}, got {actual or '<none>'}")
    if matched == 0:
        errors.append(f"{ref}.{pad_num}: no duplicate pads found")
    return errors


def main() -> int:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    errors: list[str] = []

    for key, expected in EXPECTED_PAD_NETS.items():
        error = check_single_pad(board, key[0], key[1], expected)
        if error:
            errors.append(error)

    for key, expected in EXPECTED_DUPLICATE_PAD_NETS.items():
        errors.extend(check_duplicate_pads(board, key[0], key[1], expected))

    net_names = {net.GetNetname() for net in board.GetNetInfo().NetsByName().values()}
    for forbidden in sorted(FORBIDDEN_NET_NAMES):
        if forbidden in net_names:
            errors.append(f"forbidden legacy net still present: {forbidden}")

    print("R13 topology audit")
    print(f"Board: {BOARD_PATH}")
    if errors:
        print("FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print("PASS")
    print("- USB-C A12/B12/shield are GND.")
    print("- Charger BAT/SNS pins are protected BAT_SYS, not raw BATT_RAW_P.")
    print("- High-side Q1/Q2 FET pads match the R13 common-source topology.")
    print("- R46 is in series between BATT_RAW_N and GND.")
    print("- SRP/SRN filter polarity matches charge-positive/discharge-negative current convention.")
    print("- Legacy PACK_P/Q1_DRAIN_COMMON nets are absent.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
