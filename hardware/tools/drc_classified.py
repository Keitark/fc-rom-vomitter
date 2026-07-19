"""Run KiCad DRC and conservatively classify release-significant findings.

Gold-finger mask bridges and ESP-module thermal-via drill warnings have narrow,
design-specific waivers.  Unconnected power copper is *not* waived by net name:
a GND/+3V3 track, via, pad, local zone, or plane can still be an isolated island.

An exceptional power-disconnection waiver must be supplied explicitly with
``--power-waiver-file``.  The JSON file contains a ``waivers`` list; every entry
must identify the exact two DRC item UUIDs and include both ``reason`` and
``evidence`` strings.  Example::

    {"waivers": [{"item_uuids": ["uuid-a", "uuid-b"],
                   "reason": "KiCad representative-only split",
                   "evidence": "IPC connectivity report path/hash"}]}

Prints JSON only to stdout.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile

KICAD_CLI = (
    os.environ.get("KICAD_CLI")
    or shutil.which("kicad-cli")
    or r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
)
POWER_NETS = {"GND", "+3V3", "+5V", "NES_5V", "VBUS"}


def run_kicad_drc(board_path):
    """Return KiCad's JSON DRC report for *board_path*."""
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "drc.json")
        cmd = [KICAD_CLI, "pcb", "drc", "--format", "json", "--units", "mm",
               "--severity-all", "-o", out, board_path]
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        if not os.path.exists(out):
            raise RuntimeError(f"kicad-cli drc failed: exit {proc.returncode}: "
                               f"{proc.stderr.strip()}")
        with open(out, encoding="utf-8") as f:
            return json.load(f)


def _unconnected_signature(item):
    """Stable signature used by an explicit evidence-bearing waiver."""
    uuids = [str(part.get("uuid", "")).strip().lower()
             for part in item.get("items", [])]
    if len(uuids) != 2 or not all(uuids):
        return ""
    return "|".join(sorted(uuids))


def load_power_waivers(path):
    """Load and validate exact-item power waivers from JSON."""
    if not path:
        return {}
    with open(path, encoding="utf-8") as f:
        raw = json.load(f)
    records = raw.get("waivers", []) if isinstance(raw, dict) else raw
    if not isinstance(records, list):
        raise ValueError("power waiver file must contain a 'waivers' list")

    waivers = {}
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError(f"power waiver #{index + 1} must be an object")
        uuids = record.get("item_uuids", [])
        reason = str(record.get("reason", "")).strip()
        evidence = str(record.get("evidence", "")).strip()
        if len(uuids) != 2 or not all(str(value).strip() for value in uuids):
            raise ValueError(f"power waiver #{index + 1} needs two item_uuids")
        if not reason or not evidence:
            raise ValueError(f"power waiver #{index + 1} needs reason and evidence")
        signature = "|".join(sorted(str(value).strip().lower()
                                    for value in uuids))
        if signature in waivers:
            raise ValueError(f"duplicate power waiver signature: {signature}")
        waivers[signature] = {"reason": reason, "evidence": evidence}
    return waivers


def _item_kind(description):
    text = description.lower()
    if "pad" in text or "パッド" in description:
        return "pad"
    if "via" in text or "ビア" in description:
        return "via"
    if "track" in text or "配線" in description:
        return "track"
    if "plane" in text or "プレーン" in description or "'plane_" in text:
        return "plane"
    if "zone" in text or "ゾーン" in description:
        return "zone"
    return "other"


def _connection_kind(kinds):
    """Human-readable category for a two-item unconnected finding."""
    if len(kinds) != 2:
        return "unknown"
    kind_set = set(kinds)
    if kind_set == {"zone", "plane"}:
        return "local-zone-to-plane"
    if kind_set <= {"zone", "plane"}:
        return "zone-region"
    if "pad" in kind_set:
        other = kinds[1] if kinds[0] == "pad" else kinds[0]
        return f"pad-to-{other}"
    if kind_set == {"track", "via"}:
        return "track-to-via"
    if kinds[0] == kinds[1]:
        return f"{kinds[0]}-to-{kinds[1]}"
    return "-to-".join(kinds)


def _unconnected_detail(item):
    parts = item.get("items", [])
    descriptions = [part.get("description", "") for part in parts]
    kinds = [_item_kind(description) for description in descriptions]
    nets = sorted({match for text in descriptions
                   for match in re.findall(r"\[([^\]]+)\]", text)})
    return {
        "signature": _unconnected_signature(item),
        "kind": _connection_kind(kinds),
        "nets": nets,
        "item_kinds": kinds,
        "item_uuids": [part.get("uuid", "") for part in parts],
        "descriptions": descriptions,
    }


def classify_drc_data(data, gold_finger_ref="J1", module_ref="U23",
                      power_waivers=None):
    """Classify a KiCad JSON report; power opens are real by default."""
    violations = data.get("violations", [])
    unconnected_items = data.get("unconnected_items", [])
    power_waivers = power_waivers or {}

    real_errors = []
    real_warnings = []
    waivable_errors = []
    waivable_warnings = []

    for violation in violations:
        severity = violation.get("severity", "")
        violation_type = violation.get("type", "")
        item_text = " ".join(part.get("description", "")
                             for part in violation.get("items", []))

        is_waivable = False
        # The edge connector intentionally has one mask opening over all
        # fingers.  This waiver remains restricted to its reference.
        if violation_type == "solder_mask_bridge" and gold_finger_ref in item_text:
            is_waivable = True
        # ESP module exposed-pad thermal vias are smaller by footprint design.
        if violation_type == "drill_out_of_range" and module_ref in item_text:
            is_waivable = True

        target = (waivable_errors if severity == "error" else waivable_warnings)
        if not is_waivable:
            target = real_errors if severity == "error" else real_warnings
        target.append(violation)

    real_pad_opens = 0
    real_signal_opens = 0
    real_power_opens = []
    waived_power_opens = []
    zone_region_flags = 0
    total_unconnected_pads = 0

    for item in unconnected_items:
        detail = _unconnected_detail(item)
        nets = set(detail["nets"])
        kinds = detail["item_kinds"]
        power_only = bool(nets) and nets <= POWER_NETS
        has_pad = "pad" in kinds
        zone_only = bool(kinds) and set(kinds) <= {"zone", "plane"}

        if has_pad:
            real_pad_opens += 1
            total_unconnected_pads += 1

        if power_only:
            waiver = power_waivers.get(detail["signature"])
            if waiver:
                detail["waiver"] = waiver
                waived_power_opens.append(detail)
            else:
                # A power net name is not connectivity evidence.  This catches
                # local-zone/plane islands, pad/track gaps, track/via gaps, and
                # same-zone split regions until independently proven benign.
                real_power_opens.append(detail)
                if zone_only:
                    zone_region_flags += 1
        elif not has_pad:
            # Non-power split copper is electrically real, regardless of
            # whether KiCad selected a pad as either representative.
            real_signal_opens += 1
            if zone_only:
                zone_region_flags += 1

    real_error_count = (len(real_errors) + real_signal_opens
                        + len(real_power_opens))
    result = {
        "real": {
            "errors": real_error_count,
            "warnings": len(real_warnings),
            "unconnected_pads": real_pad_opens,
            "unconnected_signals": real_signal_opens,
            "unconnected_power": len(real_power_opens),
        },
        "waivable": {
            "errors": len(waivable_errors),
            "warnings": len(waivable_warnings),
            "unconnected_pads": 0,
            "unconnected_power": len(waived_power_opens),
        },
        "unconnected_pads": total_unconnected_pads,
        "zone_region_flags": zone_region_flags,
        # Retained for compatibility; now counts only evidence-bearing waivers.
        "waivable_power_regions": len(waived_power_opens),
        "power_unconnected_real": real_power_opens,
        "power_unconnected_waived": waived_power_opens,
        "raw_unconnected_items": len(unconnected_items),
        "summary": (f"real: {real_error_count} errors, {len(real_warnings)} warnings, "
                    f"{real_pad_opens} pad opens, {real_signal_opens} signal splits, "
                    f"{len(real_power_opens)} power disconnects; waivable: "
                    f"{len(waivable_errors)} errors, {len(waivable_warnings)} warnings, "
                    f"{len(waived_power_opens)} evidenced power disconnects"),
    }
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("board_path")
    parser.add_argument("--gold-finger-ref", default="J1",
                        help="connector ref for gold-finger solder-mask waiver")
    parser.add_argument("--module-ref", default="U23",
                        help="module ref for thermal-via drill waiver")
    parser.add_argument("--power-waiver-file",
                        help="JSON with exact item UUIDs plus reason and evidence")
    args = parser.parse_args()

    try:
        data = run_kicad_drc(args.board_path)
        waivers = load_power_waivers(args.power_waiver_file)
        result = classify_drc_data(data, args.gold_finger_ref, args.module_ref,
                                   waivers)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        result = {"error": str(exc)}
    print(json.dumps(result))


if __name__ == "__main__":
    main()
