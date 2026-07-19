"""Automated veteran-layout checks (see .claude/skills/pcb-layout-review).
Run with KiCad bundled python. Exit code 1 if any check fails."""
import argparse
import os, sys, math
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_sch as g
from fc_geometry import (
    ANTENNA_BOTTOM,
    ANTENNA_LEFT,
    ANTENNA_RIGHT,
    ANTENNA_TOP,
    BODY_LEFT,
    BODY_RIGHT,
    BOTTOM_Y,
    FINGER_TIP_TRACK_KEEPOUT_Y,
    OUTLINE,
    SHOULDER_Y,
    TONGUE_KEEPOUT_BOTTOM,
    TONGUE_KEEPOUT_LEFT,
    TONGUE_KEEPOUT_RIGHT,
    TOP_Y,
    ZONE_BOTTOM,
    ZONE_LEFT,
    ZONE_RIGHT,
    ZONE_TOP,
)

DRC_HELPERS = os.path.normpath(os.path.join(
    HERE, "..", "..", "tools", "kicad-mcp", "helpers"))
sys.path.insert(0, DRC_HELPERS)
import drc_classified
from layout_check_rules import physical_pads_by_number

parser = argparse.ArgumentParser()
parser.add_argument(
    "board", nargs="?",
    default=os.path.normpath(os.path.join(HERE, "..", "nescart-fc.kicad_pcb")))
parser.add_argument(
    "--power-waiver-file",
    help="JSON with exact DRC item UUIDs plus reason and connectivity evidence")
args = parser.parse_args()
BOARD = args.board


def to_mm(v):
    return pcbnew.ToMM(v)


board = pcbnew.LoadBoard(BOARD)
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}


def cyd_bbox(fp):
    for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
        poly = fp.GetCourtyard(layer)
        if poly.OutlineCount():
            return poly.BBox()
    bb = None
    for pad in fp.Pads():
        pb = pad.GetBoundingBox()
        if bb is None:
            bb = pb
        else:
            bb.Merge(pb)
    return bb
fails, warns = [], []


def bbox_values(bb):
    return (
        to_mm(bb.GetLeft()), to_mm(bb.GetTop()),
        to_mm(bb.GetRight()), to_mm(bb.GetBottom()),
    )


def bbox_matches(actual, expected, tolerance=0.01):
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected))


# ---- geometry source-of-truth gate: a checker must reject a stale generated
# board even when that stale artifact happens to be internally DRC-clean.
mechanical_outline = pcbnew.SHAPE_POLY_SET()
if not board.GetBoardPolygonOutlines(mechanical_outline, False):
    fails.append("geometry: could not construct closed board outline")
else:
    actual = bbox_values(mechanical_outline.BBox())
    expected = (BODY_LEFT, TOP_Y, BODY_RIGHT, BOTTOM_Y)
    if not bbox_matches(actual, expected):
        fails.append(
            "geometry: saved Edge.Cuts bbox does not match fc_geometry.py "
            f"(actual={actual}, expected={expected})")


def canonical_segment(x1, y1, x2, y2):
    a = (round(x1, 3), round(y1, 3))
    b = (round(x2, 3), round(y2, 3))
    return tuple(sorted((a, b)))


expected_edges = {
    canonical_segment(x1, y1, x2, y2) for x1, y1, x2, y2 in OUTLINE}
actual_edges = set()
unexpected_edge_shapes = 0
for drawing in board.GetDrawings():
    if drawing.GetLayer() != pcbnew.Edge_Cuts:
        continue
    if drawing.GetShape() != pcbnew.SHAPE_T_SEGMENT:
        unexpected_edge_shapes += 1
        continue
    start, end = drawing.GetStart(), drawing.GetEnd()
    actual_edges.add(canonical_segment(
        to_mm(start.x), to_mm(start.y), to_mm(end.x), to_mm(end.y)))
if unexpected_edge_shapes or actual_edges != expected_edges:
    fails.append(
        "geometry: saved tongue/notch/slot segments do not match "
        f"fc_geometry.py (unexpected_shapes={unexpected_edge_shapes}, "
        f"missing={len(expected_edges - actual_edges)}, "
        f"extra={len(actual_edges - expected_edges)})")

zones_by_name = {z.GetZoneName(): z for z in board.Zones()}
antenna_zone = zones_by_name.get("antenna")
expected_antenna = (
    ANTENNA_LEFT, ANTENNA_TOP, ANTENNA_RIGHT, ANTENNA_BOTTOM)
if antenna_zone is None:
    fails.append("geometry: missing all-layer antenna keepout zone")
elif not bbox_matches(bbox_values(antenna_zone.GetBoundingBox()), expected_antenna):
    fails.append(
        "geometry: saved antenna keepout does not match fc_geometry.py")

expected_plane = (ZONE_LEFT, ZONE_TOP, ZONE_RIGHT, ZONE_BOTTOM)
for zone in board.Zones():
    if zone.GetZoneName().startswith("plane_"):
        actual = bbox_values(zone.GetBoundingBox())
        if not bbox_matches(actual, expected_plane):
            fails.append(
                f"geometry: {zone.GetZoneName()} bbox {actual} does not "
                f"match shared plane extent {expected_plane}")

# ---- connector wiping-area gate: only J1 pad copper may extend to the
# finger tips.  A router once used the area below the signal fingers as a
# sideways PPU_A7 channel; that trace would be exposed to connector wear and
# could bridge contacts.  Require the generated outer-layer fence and reject
# every routed item that enters its protected tip band.
tip_zone = zones_by_name.get("tongue_tip_notracks")
expected_tip = (
    TONGUE_KEEPOUT_LEFT,
    FINGER_TIP_TRACK_KEEPOUT_Y,
    TONGUE_KEEPOUT_RIGHT,
    TONGUE_KEEPOUT_BOTTOM,
)
if tip_zone is None:
    fails.append("geometry: missing outer-layer tongue-tip no-track fence")
else:
    if not bbox_matches(bbox_values(tip_zone.GetBoundingBox()), expected_tip):
        fails.append(
            "geometry: tongue-tip no-track fence does not match "
            f"fc_geometry.py (actual={bbox_values(tip_zone.GetBoundingBox())}, "
            f"expected={expected_tip})")
    if not (tip_zone.GetIsRuleArea()
            and tip_zone.GetDoNotAllowTracks()
            and tip_zone.GetDoNotAllowVias()
            and tip_zone.GetDoNotAllowZoneFills()
            and not tip_zone.GetDoNotAllowPads()):
        fails.append(
            "geometry: tongue-tip fence must reject tracks/vias/fills "
            "while allowing J1 pads")

tongue_no_via_top = SHOULDER_Y - 0.25
for item in board.GetTracks():
    if item.Type() == pcbnew.PCB_VIA_T:
        pos = item.GetPosition()
        x, y = to_mm(pos.x), to_mm(pos.y)
        if (TONGUE_KEEPOUT_LEFT <= x <= TONGUE_KEEPOUT_RIGHT
                and tongue_no_via_top <= y <= TONGUE_KEEPOUT_BOTTOM):
            fails.append(
                "connector-tongue: via is inside the insertion tongue "
                f"({item.GetNetname()} at {x:.3f},{y:.3f})")
        continue
    start, end = item.GetStart(), item.GetEnd()
    x1, y1 = to_mm(start.x), to_mm(start.y)
    x2, y2 = to_mm(end.x), to_mm(end.y)
    enters_x = (max(x1, x2) >= TONGUE_KEEPOUT_LEFT
                and min(x1, x2) <= TONGUE_KEEPOUT_RIGHT)
    enters_tip = max(y1, y2) >= FINGER_TIP_TRACK_KEEPOUT_Y
    if enters_x and enters_tip:
        fails.append(
            "connector-tongue: routed copper enters the finger wiping/tip "
            f"area ({item.GetNetname()} {board.GetLayerName(item.GetLayer())} "
            f"{x1:.3f},{y1:.3f}->{x2:.3f},{y2:.3f})")

# ---- rule 1: decoupler within 2.5mm of its IC VCC pin
DECOUPLE = {}
_seq = [f"U{n}" for n in list(range(1, 13)) + list(range(15, 24))]
for i, tgt in enumerate(_seq):
    DECOUPLE[f"C{i+2}"] = tgt
DECOUPLE.update({"C23": "U13", "C24": "U14"})
VCC_PIN = {"U13": "32", "U14": "32", "U15": "16", "U16": "16", "U17": "16",
           "U18": "5", "U19": "5", "U20": "5", "U21": "5", "U22": "5",
           "U23": "2"}
for i in range(1, 13):
    VCC_PIN[f"U{i}"] = "20"

for cref, tgt in DECOUPLE.items():
    cap, ic = fps[cref], fps[tgt]
    # Select the explicit supply pin number, not every pin tied to +3V3.  For
    # example, pin 1 on a 74LVC245 is DIR even when this design straps it high.
    # Iterate Pads() so duplicated physical instances of the requested
    # electrical pad number remain separate candidates.
    supply_pads = physical_pads_by_number(ic, VCC_PIN[tgt])
    if not supply_pads:
        fails.append(
            f"rule1: {tgt} has no physical VCC pad {VCC_PIN[tgt]}")
        continue
    d = min(
        math.hypot(to_mm(cap.GetPosition().x - pad.GetPosition().x),
                   to_mm(cap.GetPosition().y - pad.GetPosition().y))
        for pad in supply_pads
    )
    if d > 3.5:
        fails.append(f"rule1: {cref} is {d:.1f}mm from {tgt} VCC pin (>3.5)")
    elif d > 2.5:
        warns.append(f"rule1: {cref} is {d:.1f}mm from {tgt} VCC (2.5-3.5)")

# ---- rule 2: bulk caps near their power nodes
BULK_PIN = {
    # capacitor ref: (target ref, target power net, maximum pad-to-pad mm)
    "C26": ("U25", "+5V", 3.5),
    "C27": ("J1", "NES_5V", 18.0),
    "C28": ("J2", "VBUS", 5.0),
    "C29": ("U25", "+3V3", 8.0),
    "C30": ("U23", "+3V3", 3.5),
    "C31": ("U25", "+3V3", 4.0),
}
for cref, (tgt, net, lim) in BULK_PIN.items():
    cpad = fps[cref].FindPadByNumber("1")
    tpads = [pad for pad in fps[tgt].Pads() if pad.GetNetname() == net]
    if not cpad or not tpads:
        fails.append(f"rule2: cannot find {cref} pad 1 or {tgt} {net} pad")
        continue
    d = min(math.hypot(to_mm(cpad.GetPosition().x - pad.GetPosition().x),
                       to_mm(cpad.GetPosition().y - pad.GetPosition().y))
            for pad in tpads)
    if d > lim:
        fails.append(f"rule2: bulk {cref} pad is {d:.1f}mm from {tgt} {net} "
                     f"(>{lim})")

# ---- rule 11: no PADS within 1mm of fingers (FC finger copper starts y=69.1;
# JLC rule is about pads/holes, courtyards may approach)
def pads_bottom(fp):
    pads = list(fp.Pads())
    if not pads:
        return None
    return max(to_mm(pad.GetBoundingBox().GetBottom()) for pad in pads)


for ref, fp in fps.items():
    if ref == "J1":
        continue
    bottom = pads_bottom(fp)
    # Graphics-only logo/identity footprints intentionally have no pads and
    # cannot intrude into the electrical finger clearance.
    if bottom is None:
        continue
    if bottom > 68.1:
        fails.append(f"rule11: {ref} pads intrude finger clearance"
                     f" (bottom y={bottom:.1f} > 68.1)")

# ---- rule 12: antenna keepout free of parts. Check the whole courtyard (or
# physical-pad fallback), not just the footprint origin.
for ref, fp in fps.items():
    if ref in ("J1", "U23"):
        continue
    bbox = cyd_bbox(fp)
    if bbox is None:
        continue
    left, top, right, bottom = bbox_values(bbox)
    if (left < ANTENNA_RIGHT and right > ANTENNA_LEFT
            and top < ANTENNA_BOTTOM and bottom > ANTENNA_TOP):
        fails.append(
            f"rule12: {ref} body/courtyard intersects antenna keepout "
            f"({left:.1f},{top:.1f})-({right:.1f},{bottom:.1f})")

# ---- rule 13: courtyard overlaps (bbox approximation, pairwise)
items = [(r, bbox) for r, f in fps.items() if r != "J1"
         if (bbox := cyd_bbox(f)) is not None]
for i in range(len(items)):
    for j in range(i + 1, len(items)):
        r1, b1 = items[i]
        r2, b2 = items[j]
        ix = min(b1.GetRight(), b2.GetRight()) - max(b1.GetLeft(), b2.GetLeft())
        iy = min(b1.GetBottom(), b2.GetBottom()) - max(b1.GetTop(), b2.GetTop())
        if ix > 0 and iy > 0:
            fails.append(
                f"rule13: {r1} ({to_mm(b1.GetLeft()):.1f},{to_mm(b1.GetTop()):.1f})-"
                f"({to_mm(b1.GetRight()):.1f},{to_mm(b1.GetBottom()):.1f}) overlaps "
                f"{r2} ({to_mm(b2.GetLeft()):.1f},{to_mm(b2.GetTop()):.1f})-"
                f"({to_mm(b2.GetRight()):.1f},{to_mm(b2.GetBottom()):.1f})")

# ---- rule 15: support passives near their function
NEAR = {"R1": ("U23", 20), "C1": ("U23", 20), "R2": ("U23", 20),
        "R3": ("J2", 10), "R4": ("J2", 10), "R9": ("U17", 8),
        "R18": ("LED1", 7), "R19": ("LED2", 7), "R20": ("J1", 12)}
for ref, (tgt, lim) in NEAR.items():
    d = math.hypot(to_mm(fps[ref].GetPosition().x - fps[tgt].GetPosition().x),
                   to_mm(fps[ref].GetPosition().y - fps[tgt].GetPosition().y))
    if d > lim:
        fails.append(f"rule15: {ref} is {d:.1f}mm from {tgt} (>{lim})")

# ---- coarse body-envelope warning; the irregular polygon is enforced below.
for ref, fp in fps.items():
    if ref == "J1":
        continue
    bb = cyd_bbox(fp)
    if bb is None:
        continue
    if (to_mm(bb.GetLeft()) < BODY_LEFT - 0.5
            or to_mm(bb.GetRight()) > BODY_RIGHT + 0.5
            or to_mm(bb.GetTop()) < TOP_Y - 2.0):
        warns.append(f"outline: {ref} at/over board edge"
                     f" ({to_mm(bb.GetLeft()):.1f},{to_mm(bb.GetTop()):.1f})"
                     f"-({to_mm(bb.GetRight()):.1f},{to_mm(bb.GetBottom()):.1f})")

# ---- rule 16: every real pad must have substrate underneath it
# Courtyards and connector bodies may intentionally overhang an edge, but an
# SMT contact or plated/mechanical pad may not.  Check the actual irregular
# board polygon, not only the rectangular body limits above.  This catches a
# right-angle connector placed with its contact row facing away from the PCB.
outline = pcbnew.SHAPE_POLY_SET()
if not board.GetBoardPolygonOutlines(outline, False):
    fails.append("rule16: could not construct closed board outline")
else:
    for ref, fp in fps.items():
        if ref == "J1":
            continue
        for pad in fp.Pads():
            bb = pad.GetBoundingBox()
            corners = (
                pcbnew.VECTOR2I(bb.GetLeft(), bb.GetTop()),
                pcbnew.VECTOR2I(bb.GetRight(), bb.GetTop()),
                pcbnew.VECTOR2I(bb.GetLeft(), bb.GetBottom()),
                pcbnew.VECTOR2I(bb.GetRight(), bb.GetBottom()),
            )
            if not all(outline.Contains(point) for point in corners):
                fails.append(
                    f"rule16: {ref} pad {pad.GetNumber() or '<mechanical>'} "
                    "extends outside the board outline")

# ---- graduated lessons (auto-gates from lessons.md) ----------------------

# lesson no-signals-on-gnd-plane: DEDICATED plane layers must carry only
# their net. Zones named "plane_*" declare a dedicated plane; "pour_*"
# zones are post-route fills that legitimately share a layer with signals
# (4L-free stackup) and are exempt.
plane_layers = {}   # layer -> net, from dedicated plane zones only
for z in board.Zones():
    if z.GetIsRuleArea():
        continue
    if (z.GetNetname() in ("GND", "+3V3")
            and z.GetZoneName().startswith("plane_")):
        lname = board.GetLayerName(z.GetLayer())
        if lname.startswith("In"):
            plane_layers[lname] = z.GetNetname()
n_tracks = n_vias = 0
plane_viol = {}
via_keys = {}
for t in board.GetTracks():
    if t.Type() == pcbnew.PCB_VIA_T:
        n_vias += 1
        p = t.GetPosition()
        key = (t.GetNetname(), p.x, p.y, t.TopLayer(), t.BottomLayer(),
               t.GetWidth(t.TopLayer()), t.GetDrillValue())
        via_keys[key] = via_keys.get(key, 0) + 1
        continue
    n_tracks += 1
    lname = board.GetLayerName(t.GetLayer())
    if lname in plane_layers and t.GetNetname() != plane_layers[lname]:
        plane_viol[lname] = plane_viol.get(lname, 0) + 1
for lname, n in plane_viol.items():
    fails.append(f"lesson[no-signals-on-gnd-plane]: {n} foreign track "
                 f"segments on plane layer {lname} ({plane_layers[lname]})")

# lesson deduplicate-same-net-vias-before-fab: KiCad DRC permits identical
# same-net vias, but the duplicate survives into Excellon as a redundant drill
# hit.  Exact duplicates are never intentional stacked-via structures.
for key, count in via_keys.items():
    if count > 1:
        net, x, y, top, bottom, width, drill = key
        fails.append(
            "lesson[deduplicate-same-net-vias-before-fab]: "
            f"{count} identical {net} vias at "
            f"({to_mm(x):.3f},{to_mm(y):.3f})mm, "
            f"{board.GetLayerName(top)}-{board.GetLayerName(bottom)}, "
            f"{to_mm(width):.3f}/{to_mm(drill):.3f}mm")

# lesson no-untreated-via-in-pad-on-assembled-ics: same-net DRC intentionally
# permits a through-via to overlap its own SMD pad, so ordinary clearance
# checks cannot catch the solder-wicking risk.  For the low-cost assembly flow
# used by this project, every via must leave an assembled IC pad on a short
# dog-bone escape.  (A future filled/capped via-in-pad process would require an
# explicit fabrication contract and a separate waiver mechanism.)
for via in (item for item in board.GetTracks()
            if item.Type() == pcbnew.PCB_VIA_T):
    position = via.GetPosition()
    radius = via.GetWidth(via.TopLayer()) // 2
    for ref, fp in fps.items():
        if not ref.startswith("U"):
            continue
        for pad in fp.Pads():
            if (pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD
                    and pad.GetNetname() == via.GetNetname()
                    and pad.HitTest(position, radius)):
                fails.append(
                    "lesson[no-untreated-via-in-pad-on-assembled-ics]: "
                    f"{via.GetNetname()} via at "
                    f"({to_mm(position.x):.3f},{to_mm(position.y):.3f})mm "
                    f"overlaps {ref} SMD pad {pad.GetNumber()}"
                )

# lesson routing-via-efficiency: vias/net > 2x corpus median (1.34) = thrash
n_nets = board.GetNetCount()
if n_tracks > 100 and n_nets > 10:
    vpn = n_vias / n_nets
    if vpn > 2 * 1.34:
        warns.append(f"lesson[routing-via-efficiency]: {vpn:.2f} vias/net "
                     f"(corpus median 1.34) -- router thrashing layers")

# USB full-speed service/programming pair.  <=1 mm and equal vias remain the
# preferred routing target; up to 3.81 mm (TI's USB 2.0 150 mil guidance) and
# one-via asymmetry are soft warnings
# on this board because ROM transfer uses WiFi, not USB.  Larger imbalance is
# still a release failure.
usb = {}
for net in ("USB_DP", "USB_DN"):
    segs = [t for t in board.GetTracks() if t.GetNetname() == net]
    copper = [t for t in segs if t.Type() != pcbnew.PCB_VIA_T]
    usb[net] = {
        "length": sum(to_mm(t.GetLength()) for t in copper),
        "vias": sum(t.Type() == pcbnew.PCB_VIA_T for t in segs),
        "widths": {round(to_mm(t.GetWidth()), 4) for t in copper},
    }
if all(usb[n]["length"] > 0 for n in usb):
    skew = abs(usb["USB_DP"]["length"] - usb["USB_DN"]["length"])
    if skew > 3.81:
        fails.append(f"USB pair skew is {skew:.2f}mm (>3.81mm): "
                     f"DP={usb['USB_DP']['length']:.2f}mm, "
                     f"DN={usb['USB_DN']['length']:.2f}mm")
    elif skew > 1.0:
        warns.append(f"USB pair skew is {skew:.2f}mm (preferred <=1.00mm; "
                     "USB 2.0 guidance <=3.81mm)")
    via_delta = abs(usb["USB_DP"]["vias"] - usb["USB_DN"]["vias"])
    if via_delta > 1:
        fails.append("USB pair via counts differ: "
                     f"DP={usb['USB_DP']['vias']}, DN={usb['USB_DN']['vias']}")
    elif via_delta == 1:
        warns.append("USB pair differs by one via: "
                     f"DP={usb['USB_DP']['vias']}, DN={usb['USB_DN']['vias']}")
    if usb["USB_DP"]["widths"] != usb["USB_DN"]["widths"]:
        fails.append("USB pair trace-width sets differ: "
                     f"DP={sorted(usb['USB_DP']['widths'])}, "
                     f"DN={sorted(usb['USB_DN']['widths'])}")
else:
    fails.append("USB pair routing is missing on USB_DP or USB_DN")

# lesson real-boards-use-gnd-plane: multi-IC board must have a GND zone
ic_count = sum(1 for r in fps if r.startswith("U"))
if ic_count >= 3:
    has_gnd_zone = any(z.GetNetname() == "GND" and not z.GetIsRuleArea()
                       for z in board.Zones())
    if not has_gnd_zone:
        fails.append("lesson[real-boards-use-gnd-plane]: multi-IC board "
                     "with no GND plane/pour (12/14 corpus boards have one)")

# lesson no-hidden-power-islands: KiCad may report zero signal/pad opens while
# still identifying disconnected GND or rail representatives.  A power net
# name is not proof that a local zone, plane, track, via, or pad reaches the
# source.  Reuse the release DRC classifier and fail every such finding unless
# an exact item-UUID pair carries an explicit reason and connectivity evidence.
try:
    drc_data = drc_classified.run_kicad_drc(BOARD)
    power_waivers = drc_classified.load_power_waivers(args.power_waiver_file)
    drc_result = drc_classified.classify_drc_data(
        drc_data, power_waivers=power_waivers)
except (OSError, ValueError, RuntimeError) as exc:
    fails.append(f"lesson[no-hidden-power-islands]: DRC classification failed: {exc}")
else:
    for detail in drc_result["power_unconnected_real"]:
        refs = ",".join(str(value)[:8] for value in detail["item_uuids"])
        fails.append(
            "lesson[no-hidden-power-islands]: "
            f"{detail['kind']} on {','.join(detail['nets'])} "
            f"is unconnected (item UUIDs {refs})")
    for detail in drc_result["power_unconnected_waived"]:
        waiver = detail["waiver"]
        warns.append(
            "lesson[no-hidden-power-islands]: explicitly waived "
            f"{detail['kind']} on {','.join(detail['nets'])}: "
            f"{waiver['reason']} (evidence: {waiver['evidence']})")

print(f"== layout_check: {len(fails)} FAIL, {len(warns)} WARN")
for f in fails:
    print("FAIL", f)
for w in warns:
    print("WARN", w)
sys.exit(1 if fails else 0)
