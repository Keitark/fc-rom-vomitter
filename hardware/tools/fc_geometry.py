"""Single source of truth for nescart-fc mechanical geometry."""

BODY_LEFT = 24.85
BODY_RIGHT = 114.85
BODY_WIDTH = BODY_RIGHT - BODY_LEFT

# User-approved routing-area extension away from the gold fingers.  Width,
# connector tongue, notches, slot, and finger datum remain unchanged.  The
# owner approved a custom 3D-printed shell for the extra 10 mm on 2026-07-15;
# compatibility with an original Famicom shell is not claimed.
STANDARD_TOP_Y = 20.00
TOP_EXTENSION = 10.00
TOP_Y = STANDARD_TOP_Y - TOP_EXTENSION

NOTCH_TOP_Y = 40.90
NOTCH_BOTTOM_Y = 42.90
SHOULDER_Y = 66.10
BOTTOM_Y = 76.80

TONGUE_LEFT = 30.65
TONGUE_RIGHT = 109.05
TONGUE_WIDTH = TONGUE_RIGHT - TONGUE_LEFT
EDGE_AT = (69.85, SHOULDER_Y)

# Outer-layer routing may approach a finger from the board side, but it must
# never use the exposed/wiping tip area as a routing channel.  The ordinary
# signal fingers end at y=74.80 mm; keep tracks and vias out from there to the
# fabrication keepout boundary.  Pads remain allowed so the gold fingers
# themselves are not removed.
TONGUE_KEEPOUT_LEFT = TONGUE_LEFT - 0.25
TONGUE_KEEPOUT_RIGHT = TONGUE_RIGHT + 0.25
TONGUE_KEEPOUT_BOTTOM = BOTTOM_Y + 0.20
FINGER_TIP_TRACK_KEEPOUT_Y = 74.80

SLOT_LEFT = 72.35
SLOT_RIGHT = 77.35
LEFT_NOTCH_INNER = 29.35
RIGHT_NOTCH_INNER = 110.35

# ESP32-S3-WROOM antenna region. U23 remains referenced to the extended top
# edge, and this area is a copper/via/track keepout on every layer.
ANTENNA_LEFT = 29.70
ANTENNA_RIGHT = 48.70
ANTENNA_TOP = TOP_Y - 0.50
ANTENNA_BOTTOM = TOP_Y + 7.00

# Zone polygons extend slightly beyond Edge.Cuts; the board outline clips the
# filled copper at the legal mechanical boundary.
ZONE_LEFT = BODY_LEFT - 0.85
ZONE_RIGHT = BODY_RIGHT + 1.15
ZONE_TOP = TOP_Y - 1.00
ZONE_BOTTOM = BOTTOM_Y + 1.20

OUTLINE = [
    (BODY_LEFT, TOP_Y, BODY_RIGHT, TOP_Y),
    (BODY_LEFT, TOP_Y, BODY_LEFT, NOTCH_TOP_Y),
    (BODY_LEFT, NOTCH_TOP_Y, LEFT_NOTCH_INNER, NOTCH_TOP_Y),
    (LEFT_NOTCH_INNER, NOTCH_TOP_Y, LEFT_NOTCH_INNER, NOTCH_BOTTOM_Y),
    (LEFT_NOTCH_INNER, NOTCH_BOTTOM_Y, BODY_LEFT, NOTCH_BOTTOM_Y),
    (BODY_LEFT, NOTCH_BOTTOM_Y, BODY_LEFT, SHOULDER_Y),
    (BODY_LEFT, SHOULDER_Y, TONGUE_LEFT, SHOULDER_Y),
    (TONGUE_LEFT, SHOULDER_Y, TONGUE_LEFT, BOTTOM_Y),
    (TONGUE_LEFT, BOTTOM_Y, TONGUE_RIGHT, BOTTOM_Y),
    (TONGUE_RIGHT, BOTTOM_Y, TONGUE_RIGHT, SHOULDER_Y),
    (TONGUE_RIGHT, SHOULDER_Y, BODY_RIGHT, SHOULDER_Y),
    (BODY_RIGHT, SHOULDER_Y, BODY_RIGHT, NOTCH_BOTTOM_Y),
    (BODY_RIGHT, NOTCH_BOTTOM_Y, RIGHT_NOTCH_INNER, NOTCH_BOTTOM_Y),
    (RIGHT_NOTCH_INNER, NOTCH_BOTTOM_Y, RIGHT_NOTCH_INNER, NOTCH_TOP_Y),
    (RIGHT_NOTCH_INNER, NOTCH_TOP_Y, BODY_RIGHT, NOTCH_TOP_Y),
    (BODY_RIGHT, NOTCH_TOP_Y, BODY_RIGHT, TOP_Y),
    # Internal shell-boss cutout.
    (SLOT_LEFT, NOTCH_TOP_Y, SLOT_RIGHT, NOTCH_TOP_Y),
    (SLOT_RIGHT, NOTCH_TOP_Y, SLOT_RIGHT, NOTCH_BOTTOM_Y),
    (SLOT_RIGHT, NOTCH_BOTTOM_Y, SLOT_LEFT, NOTCH_BOTTOM_Y),
    (SLOT_LEFT, NOTCH_BOTTOM_Y, SLOT_LEFT, NOTCH_TOP_Y),
]

assert abs(BODY_WIDTH - 90.0) < 1e-9
assert abs(TONGUE_WIDTH - 78.4) < 1e-9
assert EDGE_AT == (69.85, 66.10)
assert TOP_Y == 10.0
