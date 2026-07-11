from __future__ import annotations

from pathlib import Path

import pcbnew


BOARD_PATH = Path("Daughterboard.kicad_pcb")
KICAD_FP = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")


def mm(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def get_net(board: pcbnew.BOARD, name: str) -> pcbnew.NETINFO_ITEM:
    net = board.FindNet(name)
    if net is None:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
    return net


def find_fp(board: pcbnew.BOARD, ref: str):
    return board.FindFootprintByReference(ref)


def set_pad_net(board: pcbnew.BOARD, ref: str, pad_name: str, net_name: str, warnings: list[str]) -> None:
    fp = find_fp(board, ref)
    if fp is None:
        warnings.append(f"missing footprint {ref}")
        return
    matched = False
    for pad in fp.Pads():
        if pad.GetNumber() == pad_name:
            pad.SetNet(get_net(board, net_name))
            matched = True
    if not matched:
        warnings.append(f"missing pad {ref}.{pad_name}")


def load_footprint(lib: str, name: str):
    fp = pcbnew.FootprintLoad(str(KICAD_FP / f"{lib}.pretty"), name)
    if fp is None:
        raise RuntimeError(f"could not load footprint {lib}:{name}")
    return fp


def place_fp(board: pcbnew.BOARD, fp, ref: str, value: str, x: float, y: float, rot: float = 0.0):
    old = find_fp(board, ref)
    if old is not None:
        board.Remove(old)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(mm(x, y))
    fp.SetOrientationDegrees(rot)
    return fp


def reuse_or_place_fp(board: pcbnew.BOARD, fp, ref: str, value: str, x: float, y: float, rot: float = 0.0):
    old = find_fp(board, ref)
    if old is not None:
        old.SetValue(value)
        old.SetPosition(mm(x, y))
        old.SetOrientationDegrees(rot)
        return old, False
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(mm(x, y))
    fp.SetOrientationDegrees(rot)
    return fp, True


def assign_nets(fp, pad_nets: dict[str, str], board: pcbnew.BOARD, warnings: list[str]) -> None:
    for pad_name, net_name in pad_nets.items():
        matched = False
        try:
            pads = list(fp.Pads())
        except TypeError:
            pad = fp.FindPadByNumber(pad_name)
            pads = [pad] if pad is not None else []
        for pad in pads:
            if pad is not None and pad.GetNumber() == pad_name:
                pad.SetNet(get_net(board, net_name))
                matched = True
        if not matched:
            warnings.append(f"missing pad {fp.GetReference()}.{pad_name}")


def clear_tracks_and_vias(board: pcbnew.BOARD) -> int:
    removed = 0
    for item in list(board.GetTracks()):
        board.Remove(item)
        removed += 1
    return removed


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    warnings: list[str] = []

    old_q1 = find_fp(board, "Q1")
    q1_x, q1_y = 72.0, 66.0
    if old_q1 is not None:
        pos = old_q1.GetPosition()
        if "PT8810" in old_q1.GetValue():
            q1_x = pcbnew.ToMM(pos.x) + 12.0
            q1_y = pcbnew.ToMM(pos.y)
        board.Remove(old_q1)

    # Create all R13-only nets up front so pad assignment is explicit.
    for net_name in (
        "BATT_RAW_P",
        "BAT_SYS",
        "FET_SRC_COMMON",
        "FET_BATT_P",
        "CO_GATE",
        "DO_GATE",
        "BQ_CHG",
        "BQ_DSG",
        "BQ_SRN",
        "BQ_SRP",
        "BATT_RAW_N",
        "GND",
    ):
        get_net(board, net_name)

    # Correct existing pad nets that changed with the high-side topology.
    for ref, pad, net in (
        ("J1", "A12", "GND"),
        ("J1", "B12", "GND"),
        ("J1", "SH", "GND"),
        ("U1", "13", "BAT_SYS"),
        ("U1", "14", "BAT_SYS"),
        ("U1", "15", "BAT_SYS"),
        ("U1", "16", "BAT_SYS"),
        ("C5", "1", "BAT_SYS"),
        ("C9", "1", "BATT_RAW_P"),
        ("F3", "2", "BATT_RAW_P"),
        ("J7", "3", "BATT_RAW_P"),
        ("R14", "1", "BATT_RAW_P"),
        ("F4", "1", "FET_BATT_P"),
        ("F4", "2", "BATT_RAW_P"),
        ("R47", "1", "GND"),
        ("R47", "2", "BQ_SRN"),
        ("R48", "1", "BATT_RAW_N"),
        ("R48", "2", "BQ_SRP"),
        ("TP3", "1", "BAT_SYS"),
    ):
        set_pad_net(board, ref, pad, net, warnings)

    # Add the two high-side FETs and their local support parts near the old Q1 area.
    q1 = place_fp(
        board,
        load_footprint("Package_SON", "VSONP-8-1EP_5x6_P1.27mm"),
        "Q1",
        "CSD16412Q5A charge FET",
        q1_x - 4.5,
        q1_y,
        90,
    )
    assign_nets(q1, {"1": "FET_SRC_COMMON", "2": "CO_GATE", "3": "BAT_SYS"}, board, warnings)
    board.Add(q1)

    q2 = place_fp(
        board,
        load_footprint("Package_SON", "VSONP-8-1EP_5x6_P1.27mm"),
        "Q2",
        "CSD16412Q5A discharge FET",
        q1_x + 4.5,
        q1_y,
        270,
    )
    assign_nets(q2, {"1": "FET_SRC_COMMON", "2": "DO_GATE", "3": "FET_BATT_P"}, board, warnings)
    board.Add(q2)

    for ref, value, x, y, nets, rot in (
        ("R50", "10M", q1_x - 4.5, q1_y + 5.0, {"1": "CO_GATE", "2": "FET_SRC_COMMON"}, 0),
        ("R49", "10M", q1_x + 4.5, q1_y + 5.0, {"1": "DO_GATE", "2": "FET_SRC_COMMON"}, 0),
        ("C32", "100nF 25V X7R", q1_x - 4.5, q1_y - 5.0, {"1": "BAT_SYS", "2": "FET_SRC_COMMON"}, 0),
        ("C33", "100nF 25V X7R", q1_x + 4.5, q1_y - 5.0, {"1": "FET_BATT_P", "2": "FET_SRC_COMMON"}, 0),
    ):
        fp, needs_add = reuse_or_place_fp(board, load_footprint("Resistor_SMD" if ref.startswith("R") else "Capacitor_SMD", "R_0603_1608Metric" if ref.startswith("R") else "C_0603_1608Metric"), ref, value, x, y, rot)
        assign_nets(fp, nets, board, warnings)
        if needs_add:
            board.Add(fp)

    # Add the new raw-pack-positive debug/test pad from the R13 schematic.
    tp13, needs_add = reuse_or_place_fp(
        board,
        load_footprint("TestPoint", "TestPoint_Pad_D1.0mm"),
        "TP13",
        "BATT_RAW_P test pad",
        q1_x + 12.0,
        q1_y - 8.0,
        0,
    )
    assign_nets(tp13, {"1": "BATT_RAW_P"}, board, warnings)
    if needs_add:
        board.Add(tp13)

    removed = clear_tracks_and_vias(board)
    pcbnew.SaveBoard(str(BOARD_PATH), board)

    print(f"Staged R13 topology in {BOARD_PATH}")
    print(f"Removed {removed} tracks/vias")
    if warnings:
        print("Warnings:")
        for warning in warnings:
            print(f"- {warning}")


if __name__ == "__main__":
    main()
