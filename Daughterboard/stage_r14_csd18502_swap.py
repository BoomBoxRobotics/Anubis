from __future__ import annotations

from pathlib import Path

import pcbnew


BOARD_PATH = Path("Daughterboard.kicad_pcb")
LOCAL_PRETTY = Path("Daughterboard.pretty")
FET_FOOTPRINT = "CSD18502Q5B_DNK0008A_VSON-CLIP_5x6mm"


def get_net(board: pcbnew.BOARD, name: str) -> pcbnew.NETINFO_ITEM:
    net = board.FindNet(name)
    if net is None:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
    return net


def find_fp(board: pcbnew.BOARD, ref: str):
    fp = board.FindFootprintByReference(ref)
    return fp if isinstance(fp, pcbnew.FOOTPRINT) else None


def load_local_footprint(name: str):
    fp = pcbnew.FootprintLoad(str(LOCAL_PRETTY), name)
    if fp is None:
        raise RuntimeError(f"could not load Daughterboard:{name}")
    return fp


def assign_nets(fp, board: pcbnew.BOARD, pad_nets: dict[str, str], warnings: list[str]) -> None:
    for pad_name, net_name in pad_nets.items():
        matched = False
        for pad in fp.Pads():
            if pad.GetNumber() == pad_name:
                pad.SetNet(get_net(board, net_name))
                matched = True
        if not matched:
            warnings.append(f"{fp.GetReference()}: missing pad {pad_name}")


def add_fet(
    board: pcbnew.BOARD,
    ref: str,
    value: str,
    position: pcbnew.VECTOR2I,
    rotation_deg: float,
    pad_nets: dict[str, str],
    warnings: list[str],
) -> None:
    fp = load_local_footprint(FET_FOOTPRINT)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(position)
    fp.SetOrientationDegrees(rotation_deg)
    assign_nets(fp, board, pad_nets, warnings)
    board.Add(fp)


def clear_tracks_and_vias(board: pcbnew.BOARD) -> int:
    removed = 0
    for item in list(board.GetTracks()):
        board.Remove(item)
        removed += 1
    return removed


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    warnings: list[str] = []

    for net_name in (
        "BAT_SYS",
        "FET_SRC_COMMON",
        "FET_BATT_P",
        "CO_GATE",
        "DO_GATE",
    ):
        get_net(board, net_name)

    old_q1 = find_fp(board, "Q1")
    old_q2 = find_fp(board, "Q2")
    q1_position = old_q1.GetPosition() if old_q1 is not None else pcbnew.VECTOR2I(0, 0)
    q2_position = old_q2.GetPosition() if old_q2 is not None else pcbnew.VECTOR2I(0, 0)
    if old_q1 is None:
        warnings.append("Q1: old footprint missing; placing at origin")
    else:
        board.Remove(old_q1)
    if old_q2 is None:
        warnings.append("Q2: old footprint missing; placing at origin")
    else:
        board.Remove(old_q2)

    add_fet(
        board,
        "Q1",
        "CSD18502Q5B charge FET",
        q1_position,
        0,
        {
            "1": "FET_SRC_COMMON",
            "2": "FET_SRC_COMMON",
            "3": "FET_SRC_COMMON",
            "4": "CO_GATE",
            "5": "BAT_SYS",
            "6": "BAT_SYS",
            "7": "BAT_SYS",
            "8": "BAT_SYS",
        },
        warnings,
    )
    add_fet(
        board,
        "Q2",
        "CSD18502Q5B discharge FET",
        q2_position,
        180,
        {
            "1": "FET_SRC_COMMON",
            "2": "FET_SRC_COMMON",
            "3": "FET_SRC_COMMON",
            "4": "DO_GATE",
            "5": "FET_BATT_P",
            "6": "FET_BATT_P",
            "7": "FET_BATT_P",
            "8": "FET_BATT_P",
        },
        warnings,
    )

    removed = clear_tracks_and_vias(board)
    pcbnew.SaveBoard(str(BOARD_PATH), board)

    print("Staged R14 CSD18502Q5B swap")
    print(f"Board: {BOARD_PATH}")
    print(f"Removed {removed} tracks/vias")
    if warnings:
        print("Warnings:")
        for warning in warnings:
            print(f"- {warning}")


if __name__ == "__main__":
    main()
