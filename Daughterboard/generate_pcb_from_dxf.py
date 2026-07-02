from __future__ import annotations

import csv
import math
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

import pcbnew


PROJECT = "Daughterboard"
DXF_PATH = Path("daughterboard2.dxf")
XML_NETLIST = Path("Daughterboard_pcb_source.xml")
PCB_PATH = Path("Daughterboard.kicad_pcb")
REPORT_PATH = Path("Daughterboard_pcb_population_report.txt")

KICAD_FP_ROOT = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
PROJECT_FP_ROOT = Path.cwd() / "Daughterboard.pretty"

BOARD_MARGIN_MM = 10.0

# Fixed interface placement around the Hosyond ESP32-S3 display board cutout.
# Coordinates are in normalized board millimeters after importing daughterboard2.dxf.
MANUAL_PLACEMENTS = {
    # USB-C input and charger cluster on the upper-left square.
    "J1": (73.0, 52.0, 0),
    "R1": (73.2, 45.0, 0),
    "R2": (77.0, 45.0, 0),
    "F1": (26.0, 18.0, 0),
    "D1": (31.0, 18.0, 0),
    "C1": (37.0, 18.0, 0),
    "C2": (42.0, 18.0, 0),
    "U1": (30.0, 34.0, 0),
    "L3": (18.0, 34.0, 0),
    "C3": (37.0, 30.0, 0),
    "C4": (43.0, 30.0, 0),
    "C5": (37.0, 35.0, 0),
    "C6": (43.0, 35.0, 0),
    "C7": (24.0, 42.0, 0),
    "C8": (30.0, 42.0, 0),
    "C9": (37.0, 42.0, 0),
    "C10": (43.0, 42.0, 0),
    "R3": (18.0, 25.0, 0),
    "R4": (24.0, 25.0, 0),
    "R5": (30.0, 25.0, 0),
    "R6": (48.0, 31.0, 0),
    "D2": (54.0, 31.0, 0),
    "R7": (48.0, 37.0, 0),
    "D3": (54.0, 37.0, 0),
    "TH1": (54.0, 43.0, 0),
    # 5 V buck regulator cluster on the lower-left side.
    "U3": (25.0, 58.0, 0),
    "L1": (16.0, 58.0, 0),
    "C11": (16.0, 64.0, 0),
    "C12": (22.0, 64.0, 0),
    "C13": (28.0, 64.0, 0),
    "C14": (34.0, 64.0, 0),
    "C15": (36.0, 62.0, 0),
    "R8": (16.0, 70.0, 0),
    "R9": (22.0, 70.0, 0),
    "R10": (28.0, 70.0, 0),
    # 3.3 V buck regulator cluster moved into the expanded left/center power area.
    "U4": (86.0, 22.0, 0),
    "L2": (101.0, 22.0, 0),
    "C16": (80.0, 17.0, 0),
    "C21": (85.0, 17.0, 0),
    "C22": (90.0, 17.0, 0),
    "C23": (95.0, 17.0, 0),
    "C24": (100.0, 17.0, 0),
    "R11": (80.0, 29.0, 0),
    "R12": (85.0, 29.0, 0),
    "R13": (90.0, 29.0, 0),
    # Battery/balance protection entry parts.
    "F2": (15.0, 45.0, 0),
    "F3": (22.0, 45.0, 0),
    "F4": (29.0, 45.0, 0),
    "J7": (39.0, 51.0, 0),
    # 2S protection cluster.
    "U2": (50.0, 28.0, 0),
    "Q1": (50.0, 35.0, 0),
    "C25": (45.0, 30.0, 0),
    "C26": (55.0, 30.0, 0),
    "R14": (45.0, 35.0, 0),
    "R15": (55.0, 35.0, 0),
    "R16": (45.0, 40.0, 0),
    "R21": (50.0, 40.0, 0),
    "R22": (55.0, 40.0, 0),
    # Output-switch GPIO control resistors.
    "SW1": (46.0, 20.0, 0),
    "SW2": (88.0, 40.0, 0),
    "R17": (50.0, 52.0, 0),
    "R18": (55.0, 52.0, 0),
    # Hosyond UART pads on the left inner wall of the upper gap.
    "J30": (58.5, 15.0, 0),  # +5V_SW for UART-side device power
    "J31": (58.5, 20.0, 0),  # UART_TX
    "J32": (58.5, 25.0, 0),  # UART_RX
    "J39": (58.5, 30.0, 0),  # GND near UART pads
    # Hosyond I2C/GPIO pads on the right inner wall of the upper gap.
    "J33": (117.0, 18.0, 0),  # I2C_SDA
    "J34": (117.0, 23.0, 0),  # I2C_SCL
    "J35": (117.0, 28.0, 0),  # GPIO2
    "J36": (117.0, 33.0, 0),  # GPIO3
    "J37": (117.0, 38.0, 0),  # GPIO14
    "J38": (117.0, 43.0, 0),  # GPIO21
    # External/user-facing headers near the corresponding Hosyond side.
    "J4": (99.0, 52.0, 0),  # UART out on the narrow bridge beside USB-C
    "J5": (129.0, 23.0, 0),  # I2C header on right half
    "J6": (129.0, 40.0, 0),  # Deep-sleep pushbutton header on right half
    "J8": (137.8, 15.4, 90),  # Analog input header rotated into upper-right blank space
    # Keep the right-side I2C/ADC cluster close to the daughterboard input area.
    "U5": (124.5, 49.0, 0),
    "U6": (132.0, 49.0, 0),
    "C17": (139.5, 49.0, 0),
    "C18": (144.5, 49.0, 0),
    "C19": (149.5, 49.0, 0),
    "C20": (154.5, 49.0, 0),
    "R19": (150.0, 54.0, 0),
    "R20": (155.0, 54.0, 0),
    # Keep the MCP23017 and its local support parts on the right-side signal wedge.
    "U7": (156.0, 66.0, 0),
    "C27": (138.0, 54.0, 0),
    "C28": (143.0, 54.0, 0),
    "C29": (138.0, 59.0, 0),
    "R39": (143.0, 59.0, 0),
    "R40": (138.0, 64.0, 0),
    "R41": (143.0, 64.0, 0),
    "R42": (143.0, 69.0, 0),
    "R43": (150.0, 75.0, 0),
    "R44": (156.0, 75.0, 0),
    # MCP23017 GPIO series resistors, kept in explicit non-overlapping rows.
    "R23": (135.0, 30.0, 0),
    "R24": (140.0, 30.0, 0),
    "R25": (145.0, 30.0, 0),
    "R26": (150.0, 30.0, 0),
    "R27": (155.0, 30.0, 0),
    "R28": (135.0, 35.0, 0),
    "R29": (140.0, 35.0, 0),
    "R30": (145.0, 35.0, 0),
    "R31": (150.0, 35.0, 0),
    "R32": (155.0, 35.0, 0),
    "R33": (135.0, 40.0, 0),
    "R34": (140.0, 40.0, 0),
    "R35": (145.0, 40.0, 0),
    "R36": (150.0, 40.0, 0),
    "R37": (155.0, 40.0, 0),
    "R38": (160.0, 40.0, 0),
    # MCP23017 exposed solder pads and ESD arrays, explicitly spread near the right-side signal area.
    "J91": (120.0, 45.0, 0),
    "J92": (125.0, 45.0, 0),
    "J93": (122.5, 42.5, 0),
    "J94": (135.0, 45.0, 0),
    "J95": (140.0, 45.0, 0),
    "J96": (145.0, 45.0, 0),
    "J97": (150.0, 45.0, 0),
    "J98": (155.0, 45.0, 0),
    "J99": (120.0, 50.0, 0),
    "J910": (125.0, 50.0, 0),
    "J911": (130.0, 50.0, 0),
    "J912": (135.0, 50.0, 0),
    "J913": (140.0, 50.0, 0),
    "J914": (145.0, 50.0, 0),
    "J915": (150.0, 50.0, 0),
    "J916": (155.0, 50.0, 0),
    "J925": (130.0, 55.0, 0),
    "J926": (135.0, 55.0, 0),
    "J927": (140.0, 55.0, 0),
    "J928": (145.0, 55.0, 0),
    "J929": (150.0, 55.0, 0),
    "D4": (120.0, 35.0, 0),
    "D5": (125.0, 35.0, 0),
    "D6": (130.0, 35.0, 0),
    "D7": (120.0, 40.0, 0),
    # Debug test pads, spread out so they do not short each other before routing.
    "TP1": (15.0, 75.0, 0),
    "TP2": (20.0, 75.0, 0),
    "TP3": (25.0, 75.0, 0),
    "TP4": (15.0, 80.0, 0),
    "TP5": (20.0, 80.0, 0),
    "TP6": (15.0, 85.0, 0),
    "TP7": (75.0, 17.0, 0),
    "TP8": (35.0, 66.0, 0),
    "TP9": (75.0, 29.0, 0),
    "TP10": (60.0, 34.0, 0),
    "TP11": (60.0, 39.0, 0),
    "TP12": (60.0, 44.0, 0),
    "TP13": (60.0, 49.0, 0),
    "TP14": (35.0, 70.0, 0),
}

FIXED_PLACEMENT_REFS = {
    "J1", "R1", "R2", "J4", "J5", "J6", "J8", "SW1", "SW2",
    "J30", "J31", "J32", "J33", "J34", "J35", "J36", "J37", "J38", "J39",
    "U5", "U6", "U7",
    "TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7", "TP8", "TP9", "TP10", "TP11", "TP12", "TP13", "TP14",
}


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def vec(x_mm: float, y_mm: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x_mm), mm(y_mm))


def split_footprint(fp: str) -> tuple[str, str] | None:
    if not fp or ":" not in fp:
        return None
    lib, name = fp.split(":", 1)
    return lib, name


def fp_lib_path(lib: str) -> Path:
    if lib == "Daughterboard":
        return PROJECT_FP_ROOT
    return KICAD_FP_ROOT / f"{lib}.pretty"


def read_dxf_pairs(path: Path) -> list[tuple[str, str]]:
    lines = path.read_text().splitlines()
    pairs: list[tuple[str, str]] = []
    for idx in range(0, len(lines) - 1, 2):
        pairs.append((lines[idx].strip(), lines[idx + 1].strip()))
    return pairs


def read_dxf_lwpolyline(path: Path) -> list[tuple[float, float]]:
    pairs = read_dxf_pairs(path)
    for idx, (code, value) in enumerate(pairs):
        if code == "0" and value == "LWPOLYLINE":
            vertices: list[tuple[float, float]] = []
            pending_x: float | None = None
            j = idx + 1
            while j < len(pairs):
                c, v = pairs[j]
                if c == "0":
                    break
                if c == "10":
                    pending_x = float(v)
                elif c == "20" and pending_x is not None:
                    vertices.append((pending_x, float(v)))
                    pending_x = None
                j += 1
            if len(vertices) < 3:
                raise RuntimeError("DXF LWPOLYLINE did not contain enough vertices.")
            return vertices
    raise RuntimeError("No LWPOLYLINE outline found in DXF.")


def read_dxf_circles(path: Path) -> list[tuple[float, float, float]]:
    pairs = read_dxf_pairs(path)
    circles: list[tuple[float, float, float]] = []
    for idx, (code, value) in enumerate(pairs):
        if code != "0" or value != "CIRCLE":
            continue
        x = y = radius = None
        j = idx + 1
        while j < len(pairs):
            c, v = pairs[j]
            if c == "0":
                break
            if c == "10":
                x = float(v)
            elif c == "20":
                y = float(v)
            elif c == "40":
                radius = float(v)
            j += 1
        if x is not None and y is not None and radius is not None:
            circles.append((x, y, radius))
    return circles


def normalize_outline(vertices: list[tuple[float, float]]) -> list[tuple[float, float]]:
    min_x = min(x for x, _ in vertices)
    min_y = min(y for _, y in vertices)
    return [(x - min_x + BOARD_MARGIN_MM, y - min_y + BOARD_MARGIN_MM) for x, y in vertices]


def normalize_circles(
    circles: list[tuple[float, float, float]],
    outline_vertices: list[tuple[float, float]],
) -> list[tuple[float, float, float]]:
    min_x = min(x for x, _ in outline_vertices)
    min_y = min(y for _, y in outline_vertices)
    return [(x - min_x + BOARD_MARGIN_MM, y - min_y + BOARD_MARGIN_MM, radius) for x, y, radius in circles]


def point_in_polygon(x: float, y: float, poly: list[tuple[float, float]]) -> bool:
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        intersects = (yi > y) != (yj > y)
        if intersects:
            x_at_y = (xj - xi) * (y - yi) / ((yj - yi) or 1e-9) + xi
            if x < x_at_y:
                inside = not inside
        j = i
    return inside


def add_outline(board: pcbnew.BOARD, outline: list[tuple[float, float]]) -> None:
    for start, end in zip(outline, outline[1:] + outline[:1]):
        segment = pcbnew.PCB_SHAPE(board)
        segment.SetShape(pcbnew.SHAPE_T_SEGMENT)
        segment.SetLayer(pcbnew.Edge_Cuts)
        segment.SetStart(vec(*start))
        segment.SetEnd(vec(*end))
        segment.SetWidth(mm(0.15))
        board.Add(segment)


def add_edge_cut_holes(board: pcbnew.BOARD, holes: list[tuple[float, float, float]]) -> None:
    for x, y, radius in holes:
        circle = pcbnew.PCB_SHAPE(board)
        circle.SetShape(pcbnew.SHAPE_T_CIRCLE)
        circle.SetLayer(pcbnew.Edge_Cuts)
        circle.SetCenter(vec(x, y))
        circle.SetRadius(mm(radius))
        circle.SetWidth(mm(0.15))
        board.Add(circle)


def add_ground_zone(board: pcbnew.BOARD, outline: list[tuple[float, float]], net: pcbnew.NETINFO_ITEM) -> None:
    zone = pcbnew.ZONE(board)
    layer_set = pcbnew.LSET()
    layer_set.AddLayer(pcbnew.B_Cu)
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetLayerSetAndRemoveUnusedFills(layer_set)
    zone.SetNet(net)
    zone.SetLocalClearance(mm(0.25))
    zone.SetMinThickness(mm(0.20))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    zone.SetThermalReliefGap(mm(0.25))
    zone.SetThermalReliefSpokeWidth(mm(0.30))
    zone.SetFillMode(pcbnew.ZONE_FILL_MODE_POLYGONS)
    zone.SetIsFilled(True)
    zone.SetAssignedPriority(0)
    for x, y in outline:
        zone.AppendCorner(vec(x, y), -1)
    board.Add(zone)


def parse_netlist(path: Path) -> tuple[list[dict[str, str]], dict[tuple[str, str], str], list[str]]:
    root = ET.parse(path).getroot()
    comps: list[dict[str, str]] = []
    for comp in root.find("components").findall("comp"):
        fields = {field.attrib.get("name", ""): field.text or "" for field in comp.findall("./fields/field")}
        comps.append(
            {
                "ref": comp.attrib["ref"],
                "value": comp.findtext("value") or "",
                "footprint": comp.findtext("footprint") or fields.get("Footprint", ""),
            }
        )

    pin_nets: dict[tuple[str, str], str] = {}
    net_names: list[str] = []
    for net in root.find("nets").findall("net"):
        name = net.attrib["name"]
        net_names.append(name)
        for node in net.findall("node"):
            pin_nets[(node.attrib["ref"], node.attrib["pin"])] = name
    return comps, pin_nets, net_names


def read_sections(path: Path) -> dict[str, str]:
    sections: dict[str, str] = {}
    if not path.exists():
        return sections
    with path.open(newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            refs = [ref.strip() for ref in row.get("Reference", "").split(",") if ref.strip()]
            for ref in refs:
                sections[ref] = row.get("Section", "")
    return sections


def natural_key(ref: str) -> tuple[str, int, str]:
    match = re.match(r"([A-Z#]+)(\d+)$", ref)
    if not match:
        return ref, 0, ref
    return match.group(1), int(match.group(2)), ref


def section_for(ref: str, sections: dict[str, str]) -> str:
    if ref in sections:
        return sections[ref]
    if ref.startswith("TP"):
        return "Test pads"
    if ref.startswith("J9") or ref in {"U7"} or ref in {f"R{i}" for i in range(23, 45)}:
        return "GPIO expander"
    return "Other"


def make_zone_grids(outline: list[tuple[float, float]]) -> dict[str, list[tuple[float, float]]]:
    min_x = min(x for x, _ in outline)
    max_x = max(x for x, _ in outline)
    min_y = min(y for _, y in outline)
    max_y = max(y for _, y in outline)

    zones = {
        "2S charger": (min_x + 6, min_y + 5, min_x + 68, min_y + 42, 2.5, 2.5),
        "2S protection": (min_x + 70, min_y + 5, min_x + 120, min_y + 42, 2.5, 2.5),
        "5V buck": (min_x + 7, max_y - 34, min_x + 58, max_y - 7, 2.5, 2.5),
        "3V3 buck": (max_x - 58, max_y - 34, max_x - 7, max_y - 7, 2.5, 2.5),
        "Output switching": (min_x + 45, min_y + 42, min_x + 100, min_y + 64, 2.5, 2.5),
        "Control": (min_x + 58, min_y + 36, min_x + 118, min_y + 50, 2.5, 2.5),
        "Signals": (max_x - 52, min_y + 5, max_x - 7, min_y + 41, 2.5, 2.5),
        "Analog inputs": (max_x - 58, min_y + 39, max_x - 7, min_y + 69, 2.5, 2.5),
        "GPIO expander": (max_x - 68, max_y - 36, max_x - 7, max_y - 7, 2.5, 2.5),
        "Test pads": (min_x + 103, min_y + 5, max_x - 59, min_y + 57, 2.5, 2.5),
        "Battery": (min_x + 5, min_y + 42, min_x + 45, max_y - 38, 2.5, 2.5),
        "Outputs": (min_x + 48, min_y + 64, min_x + 82, max_y - 37, 2.5, 2.5),
        "Other": (min_x + 15, min_y + 15, max_x - 15, max_y - 15, 2.5, 2.5),
    }

    grids: dict[str, list[tuple[float, float]]] = {}
    for name, (x1, y1, x2, y2, dx, dy) in zones.items():
        points: list[tuple[float, float]] = []
        y = y1
        row = 0
        while y <= y2:
            xs = []
            x = x1 + (dx / 2 if row % 2 else 0)
            while x <= x2:
                if point_in_polygon(x, y, outline):
                    xs.append((x, y))
                x += dx
            points.extend(xs)
            y += dy
            row += 1
        grids[name] = points
    return grids


def footprint_box_mm(fp: pcbnew.FOOTPRINT, clearance: float = 0.55) -> tuple[float, float]:
    if str(fp.GetFPID().GetLibItemName()) == "Pololu_2808_Raised_Header":
        return 2.4, 2.4
    pad_boxes = [pad.GetBoundingBox() for pad in fp.Pads()]
    if pad_boxes:
        min_x = min(box.GetX() for box in pad_boxes)
        min_y = min(box.GetY() for box in pad_boxes)
        max_x = max(box.GetRight() for box in pad_boxes)
        max_y = max(box.GetBottom() for box in pad_boxes)
        width = max(pcbnew.ToMM(max_x - min_x), 1.2) + clearance
        height = max(pcbnew.ToMM(max_y - min_y), 1.2) + clearance
        return width, height
    bbox = fp.GetBoundingBox()
    width = max(pcbnew.ToMM(bbox.GetWidth()), 1.6) + clearance
    height = max(pcbnew.ToMM(bbox.GetHeight()), 1.6) + clearance
    return width, height


def box_at(x: float, y: float, width: float, height: float) -> tuple[float, float, float, float]:
    return (x - width / 2, y - height / 2, x + width / 2, y + height / 2)


def boxes_overlap(a: tuple[float, float, float, float], b: tuple[float, float, float, float]) -> bool:
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


def box_inside_outline(box: tuple[float, float, float, float], outline: list[tuple[float, float]]) -> bool:
    x1, y1, x2, y2 = box
    corners = ((x1, y1), (x2, y1), (x2, y2), (x1, y2))
    return all(point_in_polygon(x, y, outline) for x, y in corners)


def load_footprint(footprint: str) -> pcbnew.FOOTPRINT | None:
    parsed = split_footprint(footprint)
    if not parsed:
        return None
    lib, name = parsed
    path = fp_lib_path(lib)
    if not path.exists():
        return None
    return pcbnew.FootprintLoad(str(path), name)


def add_text_note(board: pcbnew.BOARD, text: str, x: float, y: float) -> None:
    note = pcbnew.PCB_TEXT(board)
    note.SetLayer(pcbnew.F_SilkS)
    note.SetText(text)
    note.SetPosition(vec(x, y))
    note.SetTextSize(vec(1.2, 1.2))
    note.SetTextThickness(mm(0.15))
    board.Add(note)


def main() -> None:
    raw_outline = read_dxf_lwpolyline(DXF_PATH)
    outline = normalize_outline(raw_outline)
    holes = normalize_circles(read_dxf_circles(DXF_PATH), raw_outline)
    comps, pin_nets, net_names = parse_netlist(XML_NETLIST)
    sections = read_sections(Path("Daughterboard_pinlevel_bom.csv"))

    board = pcbnew.BOARD()
    board.SetFileName(str(PCB_PATH))
    add_outline(board, outline)
    add_edge_cut_holes(board, holes)

    net_items: dict[str, pcbnew.NETINFO_ITEM] = {}
    for name in sorted(set(net_names)):
        item = pcbnew.NETINFO_ITEM(board, name)
        board.Add(item)
        net_items[name] = item

    grids = make_zone_grids(outline)
    missing: list[str] = []
    placed: list[str] = []
    placed_boxes: list[tuple[float, float, float, float]] = [
        box_at(x, y, radius * 2 + 2.0, radius * 2 + 2.0) for x, y, radius in holes
    ]

    loaded: list[tuple[dict[str, str], pcbnew.FOOTPRINT, str, float, float]] = []
    for comp in comps:
        ref = comp["ref"]
        footprint_name = comp["footprint"]
        if ref.startswith("#"):
            continue
        fp = load_footprint(footprint_name)
        if fp is None:
            missing.append(f"{ref}: {comp['value']} -> no loadable footprint ({footprint_name or 'blank'})")
            continue
        fp.SetReference(ref)
        fp.SetValue(comp["value"])
        fp.Value().SetVisible(False)
        fp.Reference().SetVisible(True)
        width, height = footprint_box_mm(fp)
        loaded.append((comp, fp, section_for(ref, sections), width, height))

    ordered = sorted(
        loaded,
        key=lambda item: (
            0 if item[0]["ref"] in FIXED_PLACEMENT_REFS else 1,
            0 if item[0]["ref"] in MANUAL_PLACEMENTS else 1,
            item[2],
            -(item[3] * item[4]),
            natural_key(item[0]["ref"]),
        ),
    )

    def find_position(section: str, width: float, height: float) -> tuple[float, float, tuple[float, float, float, float]]:
        candidates = grids.get(section, []) + grids["Other"]
        for x, y in candidates:
            box = box_at(x, y, width, height)
            if not box_inside_outline(box, outline):
                continue
            if any(boxes_overlap(box, old) for old in placed_boxes):
                continue
            return x, y, box
        # Fallback: keep the footprint anchor inside the outline and still avoid known occupied areas.
        for x, y in candidates:
            box = box_at(x, y, width, height)
            if not point_in_polygon(x, y, outline):
                continue
            if any(boxes_overlap(box, old) for old in placed_boxes):
                continue
            return x, y, box
        # Last resort: keep the footprint anchor inside the outline, even if clearance has to be solved manually.
        for x, y in grids.get(section, []) + grids["Other"]:
            if point_in_polygon(x, y, outline):
                return x, y, box_at(x, y, width, height)
        raise RuntimeError(f"No placement candidate available for section {section}.")

    for comp, fp, section, width, height in ordered:
        ref = comp["ref"]
        footprint_name = comp["footprint"]
        manual = MANUAL_PLACEMENTS.get(ref)
        if manual:
            x, y, angle = manual
            keepout_width, keepout_height = (height, width) if angle % 180 else (width, height)
            keepout = box_at(x, y, keepout_width, keepout_height)
            if ref not in FIXED_PLACEMENT_REFS:
                if any(boxes_overlap(keepout, old) for old in placed_boxes):
                    x, y, keepout = find_position(section, width, height)
                    angle = 0
        else:
            x, y, keepout = find_position(section, width, height)
            angle = 0
        section = section_for(ref, sections)
        fp.SetPosition(vec(x, y))
        fp.SetOrientationDegrees(angle)

        for pad in fp.Pads():
            net_name = pin_nets.get((ref, pad.GetNumber()))
            if net_name and net_name in net_items and not net_name.startswith("unconnected-"):
                pad.SetNet(net_items[net_name])

        board.Add(fp)
        placed_boxes.append(keepout)
        placement_note = ""
        if ref in FIXED_PLACEMENT_REFS:
            placement_note = " [fixed interface placement]"
        elif manual:
            placement_note = " [manual preferred placement]"
        placed.append(
            f"{ref}: {footprint_name} @ {x:.2f},{y:.2f} mm ({section})"
            f"{placement_note}"
        )

    board.BuildListOfNets()
    board.SanitizeNetcodes()
    if "GND" in net_items:
        add_ground_zone(board, outline, net_items["GND"])
    pcbnew.SaveBoard(str(PCB_PATH), board)

    REPORT_PATH.write_text(
        "\n".join(
            [
                "Daughterboard PCB population report",
                "====================================",
                f"DXF outline vertices: {len(outline)}",
                f"DXF edge-cut holes: {len(holes)}",
                f"Placed footprints: {len(placed)}",
                f"Missing/unplaced footprints: {len(missing)}",
                "",
                "Missing/unplaced footprints",
                "---------------------------",
                *(missing or ["None"]),
                "",
                "Placed footprints",
                "-----------------",
                *placed,
                "",
            ]
        ),
        newline="\n",
    )


if __name__ == "__main__":
    main()
