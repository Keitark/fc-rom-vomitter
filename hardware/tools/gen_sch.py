"""Generate nescart-fc Rev A-FC KiCad schematic (functional flat hierarchy).

Single source of truth for the netlist. Emits the root schematic and four
functional child sheets under hardware-fc/.
Run: python gen_sch.py   then validate: kicad-cli sch erc nescart-fc.kicad_sch
"""
import os, sys, uuid, json

sys.path.insert(0, os.path.dirname(__file__))
from sexp import parse, dump, find_all, find, Quoted

KICAD_SYMS = r"C:\Program Files\KiCad\10.0\share\kicad\symbols"
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT = "nescart-fc"

# ---------------------------------------------------------------- helpers
def U():
    return str(uuid.uuid4())

ROOT_UUID = "b7ea11aa-0001-4000-8000-000000000001"  # stable root sheet uuid


def q(s):
    return '"%s"' % str(s).replace("\\", "\\\\").replace('"', '\\"')


# ------------------------------------------------------- symbol library
SYM_SOURCES = {
    # alias -> (libfile, symbol name)
    "LVC245": ("Logic_LevelTranslator", "SN74LVC245APW"),
    # Start from a full standalone SOIC-32 symbol, then annotate the two pins
    # whose function differs on CY62128. This avoids an unresolved inherited
    # symbol in the self-contained generated schematic.
    "SRAM": ("Memory_RAM", "AS6C4008-55PCN"),
    "HC595": ("74xx", "74HC595"),
    "MUX157": ("74xx", "74LS157"),
    "G04": ("74xGxx", "74LVC1G04"),
    "G10": ("74xGxx", "74LVC1G10"),
    "ESP32": ("RF_Module", "ESP32-S3-WROOM-1"),
    "TINY13": ("MCU_Microchip_ATtiny", "ATtiny13V-10SS"),
    "REG": ("Regulator_Linear", "AP1117-15"),
    "NMOS": ("Transistor_FET", "Q_NMOS_GSD"),
    "USBC": ("Connector", "USB_C_Receptacle_USB2.0_16P"),
    "R": ("Device", "R"),
    "C": ("Device", "C"),
    "LED": ("Device", "LED"),
    "SCHOTTKY": ("Device", "D_Schottky"),
    "SW": ("Switch", "SW_Push"),
    "HDR4": ("Connector_Generic", "Conn_01x04"),
    "TESTPOINT": ("Connector", "TestPoint"),
    "PWR_FLAG": ("power", "PWR_FLAG"),
}

SRAM_MPN = "CY62128EV30LL-45SXIT"
SRAM_LCSC = "C2840304"
SRAM_DATASHEET = (
    "https://www.infineon.com/assets/row/public/documents/10/49/"
    "infineon-cy62128ev30-mobl-1-mbit-128k-x-8-static-ram-datasheet-en.pdf"
)
SRAM_SYMBOL_NAME = "CY62128EV30LL-45SXIT_Compat"
SRAM_SYMBOL_FULLNAME = f"nescart-fc:{SRAM_SYMBOL_NAME}"

_libcache = {}


def load_lib(libname):
    if libname not in _libcache:
        with open(f"{KICAD_SYMS}\\{libname}.kicad_sym", encoding="utf-8") as f:
            lib = parse(f.read())
        _libcache[libname] = {str(s[1]): s for s in find_all(lib, "symbol")}
    return _libcache[libname]


def get_symbol(alias):
    libname, symname = SYM_SOURCES[alias]
    sym = load_lib(libname)[symname]
    if alias != "SRAM":
        return sym, f"{libname}:{symname}"

    # CY62128 pin 1 is NC, but the common SOIC-32 footprint uses this pad as
    # an upper address input on legacy 4-Mbit SRAMs. Keep the full input pin
    # electrically visible so its intentional static compatibility tie is
    # documented and ERC-checkable.
    # Pin 30 is CE2 on CY62128 and an unused upper address input on legacy
    # parts. Pin 31 (A15) is also unused by Rev A; both are fixed HIGH so the
    # three adjacent pads 30/31/32 can share a short, unambiguous 3V3 tie.
    compat = parse(dump(sym))
    for unit in find_all(compat, "symbol"):
        unit_name = str(unit[1])
        if unit_name.startswith(symname):
            unit[1] = Quoted(SRAM_SYMBOL_NAME + unit_name[len(symname):])
        for pin in find_all(unit, "pin"):
            number = str(find(pin, "number")[1])
            if number == "1":
                find(pin, "name")[1] = Quoted("NC / legacy addr")
            elif number == "30":
                find(pin, "name")[1] = Quoted(
                    "CE2 / legacy addr")

    props = {str(p[1]): p for p in find_all(compat, "property")}
    props["Value"][2] = Quoted(SRAM_MPN)
    props["Footprint"][2] = Quoted(
        "Package_SO:SOP-32_11.305x20.495mm_P1.27mm")
    props["Datasheet"][2] = Quoted(SRAM_DATASHEET)
    props["Description"][2] = Quoted(
        "3V 1-Mbit (128K x 8) asynchronous SRAM, 45ns, SOIC-32; "
        "Rev A compatibility ties documented on pins 1, 30, and 31")
    return compat, SRAM_SYMBOL_FULLNAME


def symbol_pins(sym, libname_syms):
    """All pins of symbol (resolving extends is caller's job; we use bases)."""
    pins = []
    for unit in find_all(sym, "symbol"):
        for p in find_all(unit, "pin"):
            at = find(p, "at")
            pins.append({
                "num": str(find(p, "number")[1]),
                "name": str(find(p, "name")[1]),
                "x": float(at[1]),
                "y": float(at[2]),
                "ang": float(at[3]) if len(at) > 3 else 0.0,
            })
    return pins


# ------------------------------------------------- custom edge connector
# Board convention: B.Cu is the label side; F.Cu is the component side.
# Pin numbering follows the NESdev top-down connector view.  The footprint
# mirrors that view physically, but the schematic symbol stays conventional.
EDGE_PINNAMES = {
    1: "GND", 2: "CPU_A11", 3: "CPU_A10", 4: "CPU_A9", 5: "CPU_A8",
    6: "CPU_A7", 7: "CPU_A6", 8: "CPU_A5", 9: "CPU_A4", 10: "CPU_A3",
    11: "CPU_A2", 12: "CPU_A1", 13: "CPU_A0", 14: "CPU_R/W", 15: "/IRQ",
    16: "GND", 17: "PPU_/RD", 18: "CIRAM_A10", 19: "PPU_A6",
    20: "PPU_A5", 21: "PPU_A4", 22: "PPU_A3", 23: "PPU_A2",
    24: "PPU_A1", 25: "PPU_A0", 26: "PPU_D0", 27: "PPU_D1",
    28: "PPU_D2", 29: "PPU_D3", 30: "+5V",
    31: "CART_PRESENT/+5V", 32: "M2", 33: "CPU_A12", 34: "CPU_A13",
    35: "CPU_A14", 36: "CPU_D7", 37: "CPU_D6", 38: "CPU_D5",
    39: "CPU_D4", 40: "CPU_D3", 41: "CPU_D2", 42: "CPU_D1",
    43: "CPU_D0", 44: "/ROMSEL", 45: "AUDIO_FROM_CONSOLE",
    46: "AUDIO_TO_CONSOLE", 47: "PPU_/WR", 48: "CIRAM_/CE",
    49: "PPU_/A13", 50: "PPU_A7", 51: "PPU_A8", 52: "PPU_A9",
    53: "PPU_A10", 54: "PPU_A11", 55: "PPU_A12", 56: "PPU_A13",
    57: "PPU_D7", 58: "PPU_D6", 59: "PPU_D5", 60: "PPU_D4",
}


def build_edge_symbol():
    """60-pin cart edge as one long, human-readable right-side column."""
    name = "nescart-fc:Famicom_Cart_Edge"
    top = (60 - 1) * 2.54 / 2
    body_half_height = top + 2.54
    body_half_width = 25.4
    pin_x = body_half_width + 2.54
    body = ["symbol", Quoted("Famicom_Cart_Edge_1_1")]
    # A single 60-row column is intentionally tall.  It makes pin order,
    # direction-bearing names, and external net labels readable at a glance.
    body.append(parse(f"(rectangle (start {-body_half_width} {body_half_height})"
                      f" (end {body_half_width} {-body_half_height})"
                      " (stroke (width 0.254) (type default))"
                      " (fill (type background)))"))
    pins = []
    for i in range(1, 61):
        row = i - 1
        y = top - row * 2.54
        x = pin_x
        ang = 180
        p = parse(
            f'(pin passive line (at {x} {y} {ang}) (length 2.54)'
            f' (name {q(EDGE_PINNAMES[i])} (effects (font (size 1.27 1.27))))'
            f' (number "{i}" (effects (font (size 1.27 1.27)))))')
        body.append(p)
        pins.append({"num": str(i), "name": EDGE_PINNAMES[i],
                     "x": x, "y": y, "ang": float(ang)})
    sym = ["symbol", Quoted(name),
           parse("(pin_names (offset 1.016))"),
           parse("(exclude_from_sim no)"), parse("(in_bom yes)"),
           parse("(on_board yes)"),
           parse(f'(property "Reference" "J" (at 0 {body_half_height + 1.27} 0)'
                  f' (effects (font (size 1.27 1.27))))'),
           parse(f'(property "Value" "Famicom_Cart_Edge"'
                  f' (at 0 {-body_half_height - 2.54} 0)'
                  f' (effects (font (size 1.27 1.27))))'),
           body]
    return sym, name, pins


# ------------------------------------------------------------- net maps
def lvc245(a_nets, b_nets, dir_net, oe_net):
    """a_nets/b_nets: list of 8 (net or 'NC' or 'GND')."""
    m = {"1": dir_net, "10": "GND", "19": oe_net, "20": "+3V3"}
    for i in range(8):
        m[str(2 + i)] = a_nets[i]          # A1..A8
        m[str(18 - i)] = b_nets[i]         # B1..B8
    return m


def sram(prefix, abits, oe, we):
    """CY62128: used addresses plus footprint-compatible static upper pins."""
    apins = {0: "12", 1: "11", 2: "10", 3: "9", 4: "8", 5: "7", 6: "6",
             7: "5", 8: "27", 9: "26", 10: "23", 11: "25", 12: "4",
             13: "28", 14: "3"}
    dpins = {0: "13", 1: "14", 2: "15", 3: "17", 4: "18", 5: "19",
             6: "20", 7: "21"}
    m = {
        "1": "GND",       # NC on CY62128; legacy upper address held static
        "2": "GND",       # A16 unused by both Rev A memories
        "16": "GND", "22": "GND", "24": oe, "29": we,
        "30": "+3V3",    # CE2 HIGH; legacy upper address held static
        "31": "+3V3",    # A15 unused; enables a clean 30/31/32 copper tie
        "32": "+3V3",
    }
    for bit, pin in apins.items():
        m[pin] = f"{prefix}_A{bit}" if bit < abits else "GND"
    for bit, pin in dpins.items():
        m[pin] = f"{prefix}_D{bit}"
    return m


ESP32_MAP = {
    "1": "GND", "40": "GND", "41": "GND", "2": "+3V3", "3": "ESP_EN",
    "4": "MCU_D0", "5": "MCU_D1", "6": "MCU_D2", "7": "MCU_D3",
    "8": "MCU_D4", "9": "MCU_D5", "10": "MCU_D6", "11": "MCU_D7",
    "12": "SR_SER", "17": "SR_SRCLK", "18": "SR_RCLK",
    "19": "PRG_WE_n", "20": "CHR_WE_n", "21": "PRG_OE_n", "22": "CHR_OE_n",
    "23": "LOAD_MODE", "24": "PRG_MCU_EN_n", "25": "CHR_MCU_EN_n",
    "28": "MCU_DATA_DIR", "29": "ESP_IO36_SPARE", "30": "MIRROR_SEL",
    "39": "NES5V_SENSE", "38": "LED_STATUS", "27": "ESP_IO0",
    "13": "USB_DN", "14": "USB_DP", "36": "DBG_RX", "37": "DBG_TX",
    "15": "NC", "16": "NC", "26": "NC", "31": "NC", "32": "NC",
    "33": "NC", "34": "NC", "35": "NC",
}
# sanity: pin12=IO8 SER, pin17=IO9 SRCLK, pin18=IO10 RCLK, pin19=IO11,
# pin20=IO12, pin21=IO13, pin22=IO14, pin23=IO21, pin24=IO47, pin25=IO48,
# pin28=IO35, pin29=IO36, pin30=IO37, pin39=IO1, pin38=IO2, pin27=IO0,
# NC: IO3(15), IO46(16), IO45(26), IO38..IO42(31-35)

EDGE_MAP = {}
for _pin, _sig in {
    1: "GND", 2: "CPU_A11", 3: "CPU_A10", 4: "CPU_A9", 5: "CPU_A8",
    6: "CPU_A7", 7: "CPU_A6", 8: "CPU_A5", 9: "CPU_A4", 10: "CPU_A3",
    11: "CPU_A2", 12: "CPU_A1", 13: "CPU_A0", 14: "CPU_RW", 15: "NC",
    16: "GND", 17: "PPU_RD_n", 18: "CIRAM_A10", 19: "PPU_A6",
    20: "PPU_A5", 21: "PPU_A4", 22: "PPU_A3", 23: "PPU_A2",
    24: "PPU_A1", 25: "PPU_A0", 26: "PPU_D0", 27: "PPU_D1",
    28: "PPU_D2", 29: "PPU_D3", 30: "NES_5V",
    # Pin 31 is either +5 V or cartridge-present depending on console revision.
    # Tying it to pin 30's NES_5V asserts cartridge-present on affected units.
    31: "NES_5V", 32: "NC", 33: "CPU_A12", 34: "CPU_A13",
    35: "CPU_A14", 36: "CPU_D7", 37: "CPU_D6", 38: "CPU_D5",
    39: "CPU_D4", 40: "CPU_D3", 41: "CPU_D2", 42: "CPU_D1",
    43: "CPU_D0", 44: "ROMSEL_n", 45: "FC_AUDIO_FROM_CONSOLE",
    46: "FC_AUDIO_TO_CONSOLE",
    47: "NC", 48: "PPU_A13_n", 49: "PPU_A13_n", 50: "PPU_A7",
    51: "PPU_A8", 52: "PPU_A9", 53: "PPU_A10", 54: "PPU_A11",
    55: "PPU_A12", 56: "NC", 57: "PPU_D7", 58: "PPU_D6",
    59: "PPU_D5", 60: "PPU_D4",
}.items():
    EDGE_MAP[str(_pin)] = _sig

CPU_A = [f"CPU_A{i}" for i in range(15)]
PPU_A = [f"PPU_A{i}" for i in range(13)]
PRG_A = [f"PRG_A{i}" for i in range(15)]
CHR_A = [f"CHR_A{i}" for i in range(13)]
MCU_A = [f"MCU_A{i}" for i in range(15)]
CPU_D = [f"CPU_D{i}" for i in range(8)]
PPU_D = [f"PPU_D{i}" for i in range(8)]
PRG_D = [f"PRG_D{i}" for i in range(8)]
CHR_D = [f"CHR_D{i}" for i in range(8)]
MCU_D = [f"MCU_D{i}" for i in range(8)]

FP = {
    "LVC245": "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm",
    "SRAM": "Package_SO:SOP-32_11.305x20.495mm_P1.27mm",
    "HC595": "Package_SO:TSSOP-16_4.4x5mm_P0.65mm",
    "MUX157": "Package_SO:TSSOP-16_4.4x5mm_P0.65mm",
    "G04": "Package_TO_SOT_SMD:SOT-353_SC-70-5",
    "G10": "Package_TO_SOT_SMD:SOT-363_SC-70-6",
    "ESP32": "RF_Module:ESP32-S3-WROOM-1",
    "TINY13": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "REG": "Package_TO_SOT_SMD:SOT-223-3_TabPin2",
    "NMOS": "Package_TO_SOT_SMD:SOT-23",
    "USBC": "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
    "R": "Resistor_SMD:R_0603_1608Metric",
    "C": "Capacitor_SMD:C_0603_1608Metric",
    "C10u": "Capacitor_SMD:C_0805_2012Metric",
    "LED": "LED_SMD:LED_0603_1608Metric",
    "SCHOTTKY": "Diode_SMD:D_SMA",
    "SW": "Button_Switch_SMD:SW_SPST_B3U-1000P",
    "HDR4": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "TESTPOINT": "TestPoint:TestPoint_Pad_D1.0mm",
    "EDGE": "nescart-fc:cartridge_edge_60",
}

# --------------------------------------------------------- placements
COMPONENTS = []  # (ref, alias, value, mpn, fp, x, y, pinmap)


def add(ref, alias, value, mpn, fp, x, y, pinmap, lcsc="", datasheet=""):
    COMPONENTS.append(dict(ref=ref, alias=alias, value=value, mpn=mpn,
                           fp=fp, x=x, y=y, pinmap=pinmap,
                           lcsc=lcsc, datasheet=datasheet))


def snap(v):
    return round(v / 1.27) * 1.27


# J1 cart edge
add("J1", "EDGE", "Famicom_Cart_Edge", "", FP["EDGE"], 50.8, 152.4,
    EDGE_MAP)

# console-side buffers
buf_x = 132.08
console_bufs = [
    ("U1", CPU_A[0:8], PRG_A[0:8], "+3V3", "LOAD_MODE"),
    ("U2", CPU_A[8:15] + ["GND"], PRG_A[8:15] + ["NC"], "+3V3", "LOAD_MODE"),
    ("U3", PPU_A[0:8], CHR_A[0:8], "+3V3", "LOAD_MODE"),
    ("U4", PPU_A[8:13] + ["GND"] * 3, CHR_A[8:13] + ["NC"] * 3, "+3V3",
     "LOAD_MODE"),
    ("U5", PRG_D, CPU_D, "+3V3", "PRG_EN_n"),
    ("U6", CHR_D, PPU_D, "+3V3", "CHR_EN_n"),
]
for i, (ref, a, b, d, oe) in enumerate(console_bufs):
    add(ref, "LVC245", "SN74LVC245APW", "SN74LVC245APWR", FP["LVC245"],
        buf_x, snap(50 + i * 45), lvc245(a, b, d, oe))

# SRAMs
add("U13", "SRAM", "CY62128EV30", SRAM_MPN, FP["SRAM"],
    213.36, 88.9, sram("PRG", 15, "PRG_OE_n", "PRG_WE_n"),
    lcsc=SRAM_LCSC, datasheet=SRAM_DATASHEET)
add("U14", "SRAM", "CY62128EV30", SRAM_MPN, FP["SRAM"],
    213.36, 180.34, sram("CHR", 13, "CHR_OE_n", "CHR_WE_n"),
    lcsc=SRAM_LCSC, datasheet=SRAM_DATASHEET)

# MCU-side buffers
mcu_bufs = [
    ("U7", MCU_A[0:8], PRG_A[0:8], "+3V3", "RUN"),
    ("U8", MCU_A[8:15] + ["GND"], PRG_A[8:15] + ["NC"], "+3V3", "RUN"),
    ("U9", MCU_A[0:8], CHR_A[0:8], "+3V3", "RUN"),
    ("U10", MCU_A[8:13] + ["GND"] * 3, CHR_A[8:13] + ["NC"] * 3, "+3V3",
     "RUN"),
    ("U11", MCU_D, PRG_D, "MCU_DATA_DIR", "PRG_MCU_EN_n"),
    ("U12", MCU_D, CHR_D, "MCU_DATA_DIR", "CHR_MCU_EN_n"),
]
for i, (ref, a, b, d, oe) in enumerate(mcu_bufs):
    add(ref, "LVC245", "SN74LVC245APW", "SN74LVC245APWR", FP["LVC245"],
        292.1, snap(50 + i * 45), lvc245(a, b, d, oe))

# shift registers
add("U15", "HC595", "74HC595PW", "SN74HC595PWR", FP["HC595"], 373.38, 60.96, {
    "14": "SR_SER", "11": "SR_SRCLK", "12": "SR_RCLK", "10": "+3V3",
    "13": "GND", "9": "SR_DAISY", "8": "GND", "16": "+3V3",
    "15": "MCU_A0", "1": "MCU_A1", "2": "MCU_A2", "3": "MCU_A3",
    "4": "MCU_A4", "5": "MCU_A5", "6": "MCU_A6", "7": "MCU_A7"})
add("U16", "HC595", "74HC595PW", "SN74HC595PWR", FP["HC595"], 373.38, 111.76, {
    "14": "SR_DAISY", "11": "SR_SRCLK", "12": "SR_RCLK", "10": "+3V3",
    "13": "GND", "9": "NC", "8": "GND", "16": "+3V3",
    "15": "MCU_A8", "1": "MCU_A9", "2": "MCU_A10", "3": "MCU_A11",
    "4": "MCU_A12", "5": "MCU_A13", "6": "MCU_A14", "7": "NC"})

# mirroring mux (fit SN74LVC157APW)
add("U17", "MUX157", "SN74LVC157APW", "SN74LVC157APWR", FP["MUX157"],
    373.38, 170.18, {
        "1": "MIRROR_SEL", "2": "PPU_A10", "3": "PPU_A11", "4": "CIRAM_A10",
        "5": "GND", "6": "GND", "7": "NC", "8": "GND", "9": "NC",
        "10": "GND", "11": "GND", "12": "NC", "13": "GND", "14": "GND",
        "15": "GND", "16": "+3V3"})

# glue logic
add("U18", "G04", "74LVC1G04", "SN74LVC1G04DCKR", FP["G04"], 213.36, 236.22,
    {"1": "NC", "2": "LOAD_MODE", "3": "GND", "4": "RUN", "5": "+3V3"})
add("U19", "G04", "74LVC1G04", "SN74LVC1G04DCKR", FP["G04"], 213.36, 261.62,
    {"1": "NC", "2": "ROMSEL_n", "3": "GND", "4": "ROMSEL_inv", "5": "+3V3"})
add("U20", "G04", "74LVC1G04", "SN74LVC1G04DCKR", FP["G04"], 213.36, 287.02,
    {"1": "NC", "2": "PPU_RD_n", "3": "GND", "4": "RD_inv", "5": "+3V3"})
add("U21", "G10", "74LVC1G10", "SN74LVC1G10DCKR", FP["G10"], 213.36, 316.23,
    {"1": "ROMSEL_inv", "3": "CPU_RW", "6": "RUN", "4": "PRG_EN_n",
     "2": "GND", "5": "+3V3"})
add("U22", "G10", "74LVC1G10", "SN74LVC1G10DCKR", FP["G10"], 213.36, 345.44,
    {"1": "RD_inv", "3": "PPU_A13_n", "6": "RUN", "4": "CHR_EN_n",
     "2": "GND", "5": "+3V3"})

# ESP32
add("U23", "ESP32", "ESP32-S3-WROOM-1-N8", "ESP32-S3-WROOM-1-N8",
    FP["ESP32"], 471.17, 91.44, ESP32_MAP)

# power
add("U25", "REG", "AMS1117-3.3", "AMS1117-3.3", FP["REG"], 373.38, 316.23,
    {"3": "+5V", "2": "+3V3", "1": "GND"})
add("D1", "SCHOTTKY", "SS34", "SS34", FP["SCHOTTKY"], 373.38, 337.82,
    {"2": "VBUS", "1": "+5V"})
add("D2", "SCHOTTKY", "SS34", "SS34", FP["SCHOTTKY"], 373.38, 350.52,
    {"2": "NES_5V", "1": "+5V"})

# USB-C
add("J2", "USBC", "USB-C_16P", "HRO TYPE-C-31-M-12", FP["USBC"],
    471.17, 198.12, {
        "A1": "GND", "B1": "GND", "A12": "GND", "B12": "GND", "SH": "GND",
        "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
        "A5": "CC1", "B5": "CC2", "A6": "USB_DP", "B6": "USB_DP",
        "A7": "USB_DN", "B7": "USB_DN", "A8": "NC", "B8": "NC"})

# debug header, buttons
add("J3", "HDR4", "DBG_UART", "PinHeader 1x4", FP["HDR4"], 471.17, 251.46,
    {"1": "+3V3", "2": "DBG_TX", "3": "DBG_RX", "4": "GND"})
add("SW1", "SW", "ESP RST", "B3U-1000P", FP["SW"], 471.17, 281.94,
    {"1": "ESP_EN", "2": "GND"})
add("SW2", "SW", "BOOT", "B3U-1000P", FP["SW"], 471.17, 297.18,
    {"1": "ESP_IO0", "2": "GND"})

# LEDs
add("LED1", "LED", "STATUS_BLUE", "0603 LED", FP["LED"], 471.17, 316.23,
    {"1": "GND", "2": "LED_ST_A"})
add("LED2", "LED", "PWR_GREEN", "0603 LED", FP["LED"], 471.17, 331.47,
    {"1": "GND", "2": "LED_PWR_A"})

# resistors: (ref, value, net1, net2)
RESISTORS = [
    ("R1", "10k", "+3V3", "ESP_EN"),
    ("R2", "10k", "+3V3", "ESP_IO0"),
    ("R3", "5.1k", "CC1", "GND"),
    ("R4", "5.1k", "CC2", "GND"),
    ("R5", "100k", "NES_5V", "NES5V_SENSE"),
    ("R6", "100k", "NES5V_SENSE", "GND"),
    ("R9", "10k", "MIRROR_SEL", "GND"),
    ("R10", "10k", "LOAD_MODE", "GND"),
    ("R11", "10k", "PRG_OE_n", "+3V3"),
    ("R12", "10k", "CHR_OE_n", "+3V3"),
    ("R13", "10k", "PRG_WE_n", "+3V3"),
    ("R14", "10k", "CHR_WE_n", "+3V3"),
    ("R15", "10k", "PRG_MCU_EN_n", "+3V3"),
    ("R16", "10k", "CHR_MCU_EN_n", "+3V3"),
    ("R17", "10k", "MCU_DATA_DIR", "GND"),
    ("R18", "1k", "LED_STATUS", "LED_ST_A"),
    ("R19", "1k", "+3V3", "LED_PWR_A"),
    ("R20", "0R", "FC_AUDIO_FROM_CONSOLE", "FC_AUDIO_TO_CONSOLE"),
]
for i, (ref, val, n1, n2) in enumerate(RESISTORS):
    # Two compact columns keep the pull network inside the A2 page instead
    # of producing one nearly full-height strip at the right border.
    col, row = divmod(i, 9)
    add(ref, "R", val, f"0603 {val} 1%", FP["R"],
        snap(510 + col * 35), snap(38 + row * 18),
        {"1": n1, "2": n2})

# Former CIC-gate GPIO is retained as an explicit, labeled service test point.
add("TP1", "TESTPOINT", "IO36", "", FP["TESTPOINT"], 471.17, 350.52,
    {"1": "ESP_IO36_SPARE"})

# capacitors
CAPS = [("C1", "1u", "ESP_EN", "GND", "C")]
decouple_3v3 = ["U%d" % n for n in range(1, 23) if n not in (13, 14)] + \
               ["U23x"]
ci = 2
for tgt in decouple_3v3:
    CAPS.append((f"C{ci}", "100n", "+3V3", "GND", "C")); ci += 1
for tgt in ("U13", "U14"):
    CAPS.append((f"C{ci}", "100n", "+3V3", "GND", "C")); ci += 1
# C25 belonged to the removed CIC clone and intentionally stays unused.
ci = 26
# Bulk-cap reference map (keep synchronized with place_parts.py):
#   C26 = U25 +5V input, C27 = NES_5V entry, C28 = USB VBUS,
#   C29 = U25 +3V3 output, C30 = ESP32-local +3V3, C31 = +3V3 22u bulk.
for net in ("+5V", "NES_5V", "VBUS", "+3V3", "+3V3"):
    CAPS.append((f"C{ci}", "10u", net, "GND", "C10u")); ci += 1
CAPS.append((f"C{ci}", "22u", "+3V3", "GND", "C10u")); ci += 1
for i, (ref, val, n1, n2, fpk) in enumerate(CAPS):
    # Three columns make the one-cap-per-IC intent readable and leave room
    # for the connector/control blocks below them.
    col, row = divmod(i, 11)
    add(ref, "C", val, f"{val} X7R", FP[fpk],
        snap(500 + col * 32), snap(218 + row * 14),
        {"1": n1, "2": n2})

# power flags
# no flag on +3V3: it is driven by U25 VO (power output) already
for i, net in enumerate(["GND", "+5V", "NES_5V", "VBUS"]):
    add(f"#FLG0{i+1}", "PWR_FLAG", "PWR_FLAG", "", "",
        snap(500 + i * 15), 388.62, {"1": net})

# ------------------------------------------------------ human page layout
# The canonical schematic is a flat hierarchy: global net labels preserve the
# electrical model across pages, while each page has one reviewable purpose.
# This follows the normal professional convention of keeping the bus core,
# loader/interfaces, reset/power defaults, and repetitive decoupling separate.
BUS_REFS = {"J1", *(f"U{i}" for i in range(1, 15)),
            *(f"U{i}" for i in range(18, 23)),
            *(f"R{i}" for i in range(10, 18)), "R20"}
LOADER_REFS = {"U15", "U16", "U17", "U23", "J2", "J3",
               "SW1", "SW2", "LED1", "LED2", "C1",
               "R1", "R2", "R3", "R4", "R9", "R18", "R19", "TP1"}
POWER_REFS = {"U25", "D1", "D2", "R5", "R6",
              "#FLG01", "#FLG02", "#FLG03", "#FLG04"}

BUS_POS = {
    "J1": (60.96, 190.5),
    "U1": (125.73, 58.42), "U2": (125.73, 101.6),
    "U3": (125.73, 165.1), "U4": (125.73, 208.28),
    "U5": (125.73, 274.32), "U6": (125.73, 325.12),
    "U13": (231.14, 91.44), "U14": (231.14, 213.36),
    "U7": (337.82, 58.42), "U8": (337.82, 101.6),
    "U9": (337.82, 165.1), "U10": (337.82, 208.28),
    "U11": (337.82, 274.32), "U12": (337.82, 325.12),
    "U18": (474.98, 63.5),
    "U19": (431.8, 228.6), "U21": (492.76, 228.6),
    "U20": (431.8, 292.1), "U22": (492.76, 292.1),
    "R10": (439.42, 83.82),
    "R11": (271.78, 73.66), "R13": (292.1, 73.66),
    "R12": (271.78, 195.58), "R14": (292.1, 195.58),
    "R15": (391.16, 266.7), "R17": (414.02, 292.1),
    "R16": (391.16, 325.12),
    "R20": (111.76, 237.49),
}

LOADER_POS = {
    "U15": (88.9, 73.66), "U16": (88.9, 132.08),
    "U17": (88.9, 210.82), "U23": (215.9, 137.16),
    "J2": (337.82, 132.08), "J3": (279.4, 106.68),
    "SW1": (185.42, 208.28), "SW2": (185.42, 238.76),
    "R1": (226.06, 198.12), "C1": (248.92, 198.12),
    "R2": (226.06, 238.76),
    "R3": (317.5, 175.26), "R4": (355.6, 175.26),
    "R18": (276.86, 190.5), "LED1": (276.86, 215.9),
    "R19": (342.9, 190.5), "LED2": (342.9, 215.9),
    "R9": (149.86, 223.52), "TP1": (279.4, 157.48),
}

POWER_POS = {
    "U25": (160.02, 91.44), "D1": (116.84, 71.12),
    "D2": (116.84, 111.76),
    "R5": (210.82, 91.44), "R6": (210.82, 132.08),
    "#FLG01": (139.7, 226.06), "#FLG02": (165.1, 226.06),
    "#FLG03": (190.5, 226.06), "#FLG04": (215.9, 226.06),
}

DECOUPLE_REFS = {c["ref"] for c in COMPONENTS
                  if c["ref"].startswith("C") and c["ref"] != "C1"}
DECOUPLE_POS = {}
for i, ref in enumerate(sorted(DECOUPLE_REFS,
                               key=lambda r: int(r[1:]))):
    col, row = divmod(i, 4)
    DECOUPLE_POS[ref] = (snap(48.26 + col * 48.26),
                         snap(55.88 + row * 58.42))

PAGE_POSITIONS = {
    "bus_core": BUS_POS,
    "loader": LOADER_POS,
    "power_reset": POWER_POS,
    "decoupling": DECOUPLE_POS,
}
for page, positions in PAGE_POSITIONS.items():
    for c in COMPONENTS:
        if c["ref"] in positions:
            c["sheet"] = page
            c["x"], c["y"] = positions[c["ref"]]

# Face the USB connector pins toward the ESP32, as a human-drawn schematic
# would, rather than routing all signals around the back of the symbol.
for c in COMPONENTS:
    c["rot"] = 180 if c["ref"] in {"J2", "U15", "U16"} else 0

unassigned = [c["ref"] for c in COMPONENTS if "sheet" not in c]
if unassigned:
    raise RuntimeError(f"components missing page placement: {unassigned}")

# ------------------------------------------------------------- emission
def label_angle(pin_ang):
    return {0.0: 180, 180.0: 0, 90.0: 270, 270.0: 90}[pin_ang]


def justify(lang):
    return "left" if lang in (0, 90) else "right"


def sch_line(kind, pts, width=0):
    """Emit a root-level KiCad wire or bus polyline."""
    xy = " ".join(f"(xy {round(x, 3)} {round(y, 3)})" for x, y in pts)
    return (f'  ({kind} (pts {xy})'
            f' (stroke (width {width}) (type solid)) (uuid "{U()}"))')


def sch_bus_entry(x, y, dx, dy):
    return (f'  (bus_entry (at {round(x, 3)} {round(y, 3)})'
            f' (size {round(dx, 3)} {round(dy, 3)})'
            f' (stroke (width 0.1524) (type solid)) (uuid "{U()}"))')


def sch_local_label(name, x, y, angle=0, align="left"):
    return (f'  (label {q(name)} (at {round(x, 3)} {round(y, 3)} {angle})'
            f' (effects (font (size 1.27 1.27))'
            f' (justify {align} bottom))'
            f' (uuid "{U()}"))')


def sch_global_label(name, x, y, angle=0, align="left"):
    return (f'  (global_label {q(name)} (shape passive)'
            f' (at {round(x, 3)} {round(y, 3)} {angle})'
            f' (fields_autoplaced yes)'
            f' (effects (font (size 1.27 1.27)) (justify {align}))'
            f' (uuid "{U()}"))')


def is_core_bus_net(net):
    return any(net.startswith(prefix) and net[len(prefix):].isdigit()
               for prefix in ("CPU_A", "PPU_A", "PRG_A", "CHR_A", "MCU_A",
                              "CPU_D", "PPU_D", "PRG_D", "CHR_D", "MCU_D"))


def build_bus_wiring(pin_points, page):
    """Draw short, page-local KiCad bus corridors for the main data paths."""
    cpu_a = {f"CPU_A{i}" for i in range(15)}
    ppu_a = {f"PPU_A{i}" for i in range(13)}
    prg_a = {f"PRG_A{i}" for i in range(15)}
    chr_a = {f"CHR_A{i}" for i in range(13)}
    mcu_a = {f"MCU_A{i}" for i in range(15)}
    cpu_d = {f"CPU_D{i}" for i in range(8)}
    ppu_d = {f"PPU_D{i}" for i in range(8)}
    prg_d = {f"PRG_D{i}" for i in range(8)}
    chr_d = {f"CHR_D{i}" for i in range(8)}
    mcu_d = {f"MCU_D{i}" for i in range(8)}

    # Each definition is (name, members, backbone-y, taps, named endpoints).
    # A tap is (refs, bus-x, pin-side).  Endpoints make the page-to-page flow
    # explicit without drawing page-wide lines through unrelated circuitry.
    page_defs = {
        "bus_core": [
            ("CPU_A[0..14]", cpu_a, 31.75,
             [(("U1", "U2"), 101.6, "left")], [76.2]),
            ("PRG_A[0..14]", prg_a, 41.91,
             [(("U1", "U2"), 149.86, "right"),
              ("U13", 205.74, "left"),
              (("U7", "U8"), 363.22, "right")], []),
            ("MCU_A[0..14]", mcu_a, 24.13,
             [(("U7", "U8", "U9", "U10"), 312.42, "left")],
             [397.51]),
            ("PPU_A[0..12]", ppu_a, 137.16,
             [(("U3", "U4"), 101.6, "left")], []),
            ("CHR_A[0..12]", chr_a, 147.32,
             [(("U3", "U4"), 149.86, "right"),
              ("U14", 205.74, "left"),
              (("U9", "U10"), 363.22, "right")], []),
            ("CPU_D[0..7]", cpu_d, 248.92,
             [("U5", 149.86, "right")], []),
            ("PRG_D[0..7]", prg_d, 259.08,
             [("U5", 101.6, "left"), ("U13", 256.54, "right"),
              ("U11", 363.22, "right")], []),
            ("PPU_D[0..7]", ppu_d, 373.38,
             [("U6", 149.86, "right")], [76.2]),
            ("CHR_D[0..7]", chr_d, 363.22,
             [("U6", 101.6, "left"), ("U14", 269.24, "right"),
              ("U12", 363.22, "right")], []),
            ("MCU_D[0..7]", mcu_d, 386.08,
             [(("U11", "U12"), 312.42, "left")], [397.51]),
        ],
        "loader": [
            ("MCU_A[0..14]", mcu_a, 33.02,
             [(("U15", "U16"), 63.5, "left")], [50.8]),
            ("PPU_A[0..12]", ppu_a, 190.5,
             [("U17", 60.96, "left")], [50.8]),
            ("MCU_D[0..7]", mcu_d, 172.72,
             [("U23", 187.96, "left")], [50.8]),
        ],
    }
    defs = page_defs.get(page, [])

    result = []
    for bus_name, nets, backbone_y, taps, anchors in defs:
        backbone_y = snap(backbone_y)
        tap_xs = []
        for refs, bus_x, side in taps:
            bus_x = snap(bus_x)
            refs = {refs} if isinstance(refs, str) else set(refs)
            pts = []
            for p in pin_points:
                if p["ref"] not in refs or p["net"] not in nets:
                    continue
                if side == "left" and p["x"] >= p["cx"]:
                    continue
                if side == "right" and p["x"] <= p["cx"]:
                    continue
                pts.append(p)
            if not pts:
                continue

            entry_bus_ys = []
            for p in pts:
                direction = 1 if bus_x > p["x"] else -1
                wire_end_x = bus_x - direction * 2.54
                result.append(sch_line(
                    "wire", [(p["x"], p["y"]), (wire_end_x, p["y"])], 0))
                result.append(sch_bus_entry(
                    wire_end_x, p["y"], direction * 2.54, -2.54))
                entry_bus_ys.append(p["y"] - 2.54)

            y0 = min(entry_bus_ys + [backbone_y])
            y1 = max(entry_bus_ys + [backbone_y])
            result.append(sch_line("bus", [(bus_x, y0), (bus_x, y1)], 0))
            tap_xs.append(bus_x)

        tap_xs.extend(snap(x) for x in anchors)
        if tap_xs:
            # Split the backbone at every vertical tap.  KiCad does not treat
            # an intermediate T-intersection as a connected bus junction
            # unless that point is also a segment endpoint.
            xs = sorted(set(tap_xs))
            for x0, x1 in zip(xs, xs[1:]):
                result.append(sch_line("bus", [(x0, backbone_y),
                                                 (x1, backbone_y)], 0))
            x0 = xs[0]
            # Keep bus members globally named (matching the PCB/netlist) while
            # using compact local labels at each component pin.
            result.append(sch_global_label(bus_name, x0, backbone_y))

    return result


# Direct wires are deliberately page-local.  Global labels remain only once
# per wired net so the canonical net names and cross-page connectivity stay
# stable, but nearby components no longer appear to float beside each other.
DIRECT_WIRE_SPECS = {
    "loader": [
        dict(net="SR_SER", refs={"U15", "U23"}, axis="v", coord=149.86),
        dict(net="SR_SRCLK", refs={"U15", "U16", "U23"}, axis="v", coord=139.7),
        dict(net="SR_RCLK", refs={"U15", "U16", "U23"}, axis="v", coord=129.54),
        dict(net="USB_DP", refs={"U23", "J2"}, axis="h", coord=127.0),
        dict(net="USB_DN", refs={"U23", "J2"}, path=[
            (231.14, 124.46), (250.19, 124.46), (250.19, 132.08),
            (322.58, 132.08), (322.58, 134.62)]),
        dict(net="CC1", refs={"J2", "R3"}, axis="v", coord=309.88),
        dict(net="CC2", refs={"J2", "R4"}, axis="v", coord=342.9),
        dict(net="DBG_TX", refs={"U23", "J3"}, path=[
            (231.14, 114.3), (256.54, 114.3),
            (256.54, 106.68), (274.32, 106.68)]),
        dict(net="DBG_RX", refs={"U23", "J3"}, path=[
            (231.14, 116.84), (261.62, 116.84),
            (261.62, 109.22), (274.32, 109.22)]),
        dict(net="ESP_EN", refs={"U23", "SW1", "R1", "C1"}, axis="v", coord=172.72),
        dict(net="ESP_IO0", refs={"U23", "SW2", "R2"}, axis="v", coord=162.56),
        dict(net="MIRROR_SEL", refs={"U17", "R9"}, path=[
            (76.2, 226.06), (76.2, 219.71), (149.86, 219.71)]),
        dict(net="ESP_IO36_SPARE", refs={"U23", "TP1"}, path=[
            (231.14, 134.62), (254.0, 134.62),
            (254.0, 157.48), (279.4, 157.48)]),
        dict(net="LED_STATUS", refs={"U23", "R18"}, axis="v", coord=177.8),
        dict(net="LED_ST_A", refs={"R18", "LED1"}, axis="v", coord=287.02),
        dict(net="LED_PWR_A", refs={"R19", "LED2"}, axis="v", coord=353.06),
    ],
    "power_reset": [
        dict(net="+5V", refs={"U25", "D1", "D2"}, axis="v", coord=139.7),
        dict(net="NES5V_SENSE", refs={"R5", "R6"}, axis="v", coord=264.16),
    ],
    "bus_core": [
        dict(net="FC_AUDIO_FROM_CONSOLE", refs={"J1", "R20"}, path=[
            (88.9, 227.33), (106.68, 227.33),
            (106.68, 233.68), (111.76, 233.68)],
            label_stub=[(111.76, 233.68), (114.3, 233.68)],
            label_at=(114.3, 233.68, 0, "left")),
        dict(net="FC_AUDIO_TO_CONSOLE", refs={"J1", "R20"}, path=[
            (88.9, 229.87), (101.6, 229.87),
            (101.6, 241.3), (111.76, 241.3)],
            label_stub=[(111.76, 241.3), (114.3, 241.3)],
            label_at=(114.3, 241.3, 0, "left")),
        dict(net="ROMSEL_inv", refs={"U19", "U21"}, axis="h", coord=228.6),
        dict(net="RD_inv", refs={"U20", "U22"}, axis="h", coord=292.1),
        dict(net="LOAD_MODE", refs={"U18", "R10"}, axis="v", coord=447.04),
        dict(net="PRG_OE_n", refs={"U13", "R11"}, axis="v", coord=259.08),
        dict(net="PRG_WE_n", refs={"U13", "R13"}, axis="v", coord=284.48),
        dict(net="CHR_OE_n", refs={"U14", "R12"}, axis="v", coord=259.08),
        dict(net="CHR_WE_n", refs={"U14", "R14"}, axis="v", coord=284.48),
        dict(net="PRG_MCU_EN_n", refs={"U11", "R15"}, axis="h", coord=264.16),
        dict(net="CHR_MCU_EN_n", refs={"U12", "R16"}, axis="h", coord=314.96),
        dict(net="MCU_DATA_DIR", refs={"U11", "U12", "R17"}, axis="v", coord=304.8),
    ],
}


def direct_spec(page, ref, net):
    for spec in DIRECT_WIRE_SPECS.get(page, []):
        if spec["net"] == net and ref in spec["refs"]:
            return spec
    return None


def build_direct_wiring(pin_points, page):
    result = []
    for spec in DIRECT_WIRE_SPECS.get(page, []):
        pts = []
        for p in pin_points:
            if p["net"] == spec["net"] and p["ref"] in spec["refs"]:
                xy = (p["x"], p["y"])
                if xy not in pts:
                    pts.append(xy)
        if len(pts) < 2:
            raise RuntimeError(f'direct wire {page}:{spec["net"]} has {len(pts)} pins')
        if "path" in spec:
            # KiCad schematic wires are two-point segments.  A routed path is
            # emitted as consecutive segments so every bend is a valid node.
            for p0, p1 in zip(spec["path"], spec["path"][1:]):
                result.append(sch_line("wire", [p0, p1], 0))
            if "label_at" in spec:
                if "label_stub" in spec:
                    result.append(sch_line("wire", spec["label_stub"], 0))
                x, y, angle, align = spec["label_at"]
                result.append(sch_global_label(
                    spec["net"], x, y, angle, align))
            continue
        coord = snap(spec["coord"])
        if spec["axis"] == "v":
            ys = sorted({y for _, y in pts})
            for x, y in pts:
                if x != coord:
                    result.append(sch_line("wire", [(x, y), (coord, y)], 0))
            for y0, y1 in zip(ys, ys[1:]):
                result.append(sch_line("wire", [(coord, y0), (coord, y1)], 0))
        else:
            xs = sorted({x for x, _ in pts})
            for x, y in pts:
                if y != coord:
                    result.append(sch_line("wire", [(x, y), (x, coord)], 0))
            for x0, x1 in zip(xs, xs[1:]):
                result.append(sch_line("wire", [(x0, coord), (x1, coord)], 0))
    return result


def sch_text(x, y, value, size=2.0, bold=False):
    weight = " (bold yes)" if bold else ""
    return (f'  (text {q(value)} (at {snap(x)} {snap(y)} 0)'
            f' (effects (font (size {size} {size}){weight}))'
            f' (uuid "{U()}"))')


def emit_child(page, filename, paper, page_title, components, lib_symbols,
               pins_by_alias, alias_fullname, sheet_uuid, page_number, notes):
    """Emit one functional child sheet in the flat hierarchy."""
    labels = []
    noconnects = []
    symbols = []
    pin_points = []
    direct_label_seen = set()
    instance_path = f"/{ROOT_UUID}/{sheet_uuid}"
    for c in components:
        alias = c["alias"]
        pins = pins_by_alias[alias]
        pinmap = dict(c["pinmap"])
        X, Y = c["x"], c["y"]
        rotation = c.get("rot", 0)
        # verify completeness
        pin_nums = {p["num"] for p in pins}
        missing = pin_nums - set(pinmap)
        extra = set(pinmap) - pin_nums
        if missing or extra:
            raise SystemExit(f"{c['ref']}: missing pins {sorted(missing)}"
                             f" / unknown pins {sorted(extra)}")
        seen_pos = set()
        for p in pins:
            if rotation == 0:
                gx = round(X + p["x"], 3)
                gy = round(Y - p["y"], 3)
                pin_angle = p["ang"]
            elif rotation == 180:
                gx = round(X - p["x"], 3)
                gy = round(Y + p["y"], 3)
                pin_angle = (p["ang"] + 180) % 360
            else:
                raise ValueError(f"unsupported symbol rotation: {rotation}")
            net = pinmap[p["num"]]
            if (gx, gy) in seen_pos:
                continue
            seen_pos.add((gx, gy))
            if net == "NC":
                noconnects.append(f'  (no_connect (at {gx} {gy})'
                                  f' (uuid "{U()}"))')
                continue
            pin_points.append({"ref": c["ref"], "net": net,
                               "x": gx, "y": gy, "cx": X, "cy": Y})
            la = label_angle(pin_angle)
            visual_spec = direct_spec(page, c["ref"], net)
            if visual_spec:
                if net not in direct_label_seen and "label_at" not in visual_spec:
                    labels.append(sch_global_label(net, gx, gy, la, justify(la)))
                    direct_label_seen.add(net)
            elif is_core_bus_net(net):
                labels.append(sch_local_label(net, gx, gy, la, justify(la)))
            else:
                labels.append(sch_global_label(net, gx, gy, la, justify(la)))
        props = [
            ("Reference", c["ref"], 0, -3.81, False),
            ("Value", c["value"], 0, 3.81, alias == "SRAM"),
            ("Footprint", c["fp"], 0, 6.35, True),
            ("Datasheet", c.get("datasheet", ""), 0, 0, True),
            ("Description", "", 0, 0, True),
            ("MPN", c["mpn"], 0, 8.89, True),
            ("LCSC", c.get("lcsc", ""), 0, 0, True),
        ]
        prop_s = ""
        for name, val, dx, dy, hide in props:
            hide_s = " (hide yes)" if hide else ""
            prop_s += (f'\n    (property {q(name)} {q(val)}'
                       f' (at {round(X+dx,3)} {round(Y+dy,3)} 0)'
                       f' (effects (font (size 1.27 1.27)){hide_s}))')
        pin_uuid_s = "".join(
            f'\n    (pin "{p["num"]}" (uuid "{U()}"))' for p in pins)
        symbols.append(
            f'  (symbol (lib_id {q(alias_fullname[alias])})'
            f' (at {X} {Y} {rotation}) (unit 1)'
            f' (exclude_from_sim no) (in_bom yes) (on_board yes)'
            f' (dnp no) (fields_autoplaced yes)'
            f' (uuid "{U()}"){prop_s}{pin_uuid_s}'
            f'\n    (instances (project "{PROJECT}"'
            f' (path "{instance_path}" (reference {q(c["ref"])}) (unit 1))))'
            f'\n  )')

    out = ["(kicad_sch", '  (version 20250114)',
           '  (generator "eeschema")', '  (generator_version "10.0")',
           f'  (uuid "b7ea11aa-0001-4000-8000-{page_number:012d}")',
           f'  (paper "{paper}")',
           f'  (title_block (title {q(page_title)}) (rev "A-FC")'
           f' (company "keita") (comment 1 "NROM 32K PRG / 8K CHR"))',
           "  " + dump(lib_symbols, 1)]
    texts = [sch_text(*note) for note in notes]
    out += (texts + noconnects + build_bus_wiring(pin_points, page)
            + build_direct_wiring(pin_points, page) + labels + symbols)
    out.append(")")
    path = os.path.join(OUT_DIR, filename)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {path}: {len(components)} components,"
          f" {len(labels)} labels, {len(noconnects)} NCs")


def root_sheet_block(name, filename, x, y, w, h, sheet_uuid, page_number):
    return f'''  (sheet
    (at {x} {y}) (size {w} {h})
    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)
    (stroke (width 0.1524) (type solid))
    (fill (color 0 0 0 0.0000))
    (uuid "{sheet_uuid}")
    (property "Sheetname" {q(name)} (at {x + 2.54} {y - 0.762} 0)
      (effects (font (size 2.0 2.0) (bold yes)) (justify left bottom)))
    (property "Sheetfile" {q(filename)} (at {x + 2.54} {y + h + 0.762} 0)
      (effects (font (size 1.27 1.27)) (justify left top)))
    (instances (project "{PROJECT}"
      (path "/{ROOT_UUID}" (page "{page_number}"))))
  )'''


def main():
    edge_sym, edge_name, edge_pins = build_edge_symbol()
    lib_symbols = ["lib_symbols"]
    used_aliases = sorted({c["alias"] for c in COMPONENTS
                           if c["alias"] != "EDGE"})
    pins_by_alias = {"EDGE": edge_pins}
    for alias in used_aliases:
        sym, fullname = get_symbol(alias)
        sym_copy = parse(dump(sym))
        sym_copy[1] = Quoted(fullname)
        lib_symbols.append(sym_copy)
        pins_by_alias[alias] = symbol_pins(sym, None)
    lib_symbols.append(edge_sym)
    alias_fullname = {a: get_symbol(a)[1] for a in used_aliases}
    alias_fullname["EDGE"] = edge_name

    sheets = [
        ("bus_core", "FAMICOM BUS, SRAM, AUDIO, AND OWNERSHIP", "nescart-fc_bus_core.kicad_sch",
         "A2", "b7ea11aa-1000-4000-8000-000000000002", 2,
         [(297, 8, "FAMICOM BUS CORE  |  edge -> console buffers -> SRAM <- MCU buffers", 2.54, True),
          (125, 16, "CONSOLE-SIDE BUFFERS", 1.8, True),
          (231, 16, "PRG / CHR SRAM", 1.8, True),
          (231, 50, "U13 PRG SRAM - CY62128 (128K x 8)", 1.27, True),
          (231, 172, "U14 CHR SRAM - CY62128 (128K x 8)", 1.27, True),
          (338, 16, "MCU-SIDE BUFFERS", 1.8, True),
          (475, 16, "OWNERSHIP / ENABLE LOGIC", 1.8, True),
          (410, 24, "TO LOADER", 1.5, True),
          (410, 386, "TO LOADER", 1.5, True),
          (485, 105, "LAYOUT HANDOFF - ROUTING TARGETS, NOT TIMING PROOF", 1.5, True),
          (485, 113, "CPU/PRG ~1.79 MHz; PPU/CHR ~5.37 MHz", 1.27, False),
          (485, 121, "Skew target: CPU/PRG <=25 mm; PPU/CHR <=15 mm; no trombone tuning", 1.27, False),
          (485, 129, "Fast LVC edges dominate SI: short stubs + continuous GND return", 1.27, False),
          (485, 137, "Keep /OE, /WE, /CE direct; preserve edge -> buffer -> SRAM order", 1.27, False),
           (231, 365, "SRAM FIT: CY62128 pin 30 CE2 + pin 31 unused A15 -> 3V3", 1.27, True),
           (231, 373, "Pin 1 NC -> GND for legacy footprint; fixed upper bits select one bank", 1.27, False),
           (231, 381, "U13 uses A0..A14 (32 KiB); U14 uses A0..A12 (8 KiB)", 1.27, False),
           (55, 280, "FAMICOM AUDIO PASS-THROUGH", 1.5, True),
           (55, 288, "Pins 45 -> R20 0R -> 46; DNP only when an audio mixer replaces it", 1.27, False)]),
        ("loader", "ESP32 LOADER AND EXTERNAL INTERFACES", "nescart-fc_loader.kicad_sch",
         "A3", "b7ea11aa-1000-4000-8000-000000000003", 3,
         [(210, 10, "ESP32 LOADER / ADDRESS GENERATION / USB", 2.54, True),
          (102, 20, "ADDRESS SHIFT CHAIN", 1.8, True),
          (229, 20, "ESP32-S3", 1.8, True),
          (338, 20, "USB / DEBUG / USER I/O", 1.8, True),
          (43, 25, "FROM BUS CORE", 1.5, True),
          (43, 180, "TO BUS CORE", 1.5, True),
          (218, 38, "MCU LOAD BUS - FIRMWARE TARGET <= 10 MHz", 1.5, True),
          (218, 46, "Group skew <= 25 mm; short SRCLK/RCLK/WE/OE; continuous GND", 1.27, False),
          (335, 38, "USB FULL-SPEED 12 Mbps - 90 ohm +/-10% differential", 1.5, True),
          (335, 46, "D+/D- skew <= 1 mm; no stubs; minimize vias; solid GND below", 1.27, False),
           (335, 54, "ESP antenna keepout + placement: see docs/layout-electrical-guidance.md", 1.27, False),
           (135, 255, "FAMICOM: no CIC reset hold. Upload completes, then user presses console RESET.", 1.27, True),
           (135, 263, "IO36 is spare at TP1; MIRROR_SEL defaults LOW through R9 10k.", 1.27, False)]),
        ("power_reset", "POWER AND SAFE DEFAULTS", "nescart-fc_power_safe.kicad_sch",
         "A3", "b7ea11aa-1000-4000-8000-000000000004", 4,
         [(210, 10, "POWER / STARTUP DEFAULTS", 2.54, True),
           (145, 25, "POWER OR + REGULATOR", 1.8, True),
           (330, 25, "DEFINED STARTUP STATES", 1.8, True),
          (122, 135, "PROVISIONAL POWER-ROUTING ALLOCATION", 1.5, True),
          (122, 143, "+3V3 and 5V trunks: design for >= 0.8 A peak", 1.27, False),
          (122, 151, "ESP branch supply capacity >= 0.5 A; each SRAM <= 60 mA max", 1.27, False),
          (122, 159, "Logic/LED allowance: 0.1 A; measure all rails before fabrication", 1.27, False),
          (122, 167, "AMS1117 loss at 0.8 A ~= 1.36 W: thermal review REQUIRED", 1.27, True),
          (315, 135, "LAYER / RETURN-PATH TARGET", 1.5, True),
          (315, 143, "In1: uninterrupted GND plane; In4: +3V3 plane; no signals", 1.27, False),
          (315, 151, "Route buses primarily on F.Cu/In2 with In1 GND reference", 1.27, False),
           (315, 159, "Keep copper/vias/traces out of ESP antenna zone and Famicom tongue", 1.27, False),
           (315, 167, "Source capability + regulator thermals remain signoff items", 1.27, True)]),
        ("decoupling", "DECOUPLING AND BULK CAPACITORS", "nescart-fc_decoupling.kicad_sch",
         "A3", "b7ea11aa-1000-4000-8000-000000000005", 5,
         [(210, 10, "DECOUPLING / BULK CAPACITORS", 2.54, True),
          (210, 22, "One local capacitor per IC plus rail bulk capacitance", 1.5, False),
          (210, 270, "LAYOUT: each 100 nF in the VCC-pin escape path with its own short GND via", 1.27, True),
          (210, 278, "Place >=10 uF at each source entry and directly at the ESP32 branch", 1.27, False)]),
    ]

    for page, title, filename, paper, sheet_uuid, page_number, notes in sheets:
        page_components = [c for c in COMPONENTS if c["sheet"] == page]
        emit_child(page, filename, paper, title, page_components, lib_symbols,
                   pins_by_alias, alias_fullname, sheet_uuid, page_number, notes)

    root = ["(kicad_sch", '  (version 20250114)',
            '  (generator "eeschema")', '  (generator_version "10.0")',
            f'  (uuid "{ROOT_UUID}")', '  (paper "A4")',
            '  (title_block (title "nescart-fc Rev A-FC - schematic index")'
            ' (rev "A-FC") (company "keita")'
            ' (comment 1 "Functional flat hierarchy; global nets connect pages"))',
            "  " + dump(lib_symbols, 1),
            sch_text(148, 17, "NESCART-FC REV A-FC - FUNCTIONAL SCHEMATIC", 2.54, True),
            sch_text(80, 39, "FAMICOM EDGE / MEMORY DATA PATH", 1.5, True),
            sch_text(217, 39, "UPLOAD / ADDRESS GENERATION", 1.5, True),
            sch_text(80, 109, "POWER / SAFE STATES", 1.5, True),
            sch_text(217, 109, "LOCAL SUPPLY BYPASSING", 1.5, True),
            root_sheet_block("BUS CORE", sheets[0][2], 25, 45, 110, 45,
                             sheets[0][4], 2),
            root_sheet_block("LOADER + INTERFACES", sheets[1][2], 162, 45, 110, 45,
                             sheets[1][4], 3),
            root_sheet_block("POWER + SAFE STATES", sheets[2][2], 25, 115, 110, 45,
                             sheets[2][4], 4),
            root_sheet_block("DECOUPLING", sheets[3][2], 162, 115, 110, 45,
                             sheets[3][4], 5),
            sch_text(148, 98, "BUS CORE <-> LOADER   |   all pages share named global nets", 1.5, True),
            '  (sheet_instances (path "/" (page "1")))', ")"]
    sch_path = os.path.join(OUT_DIR, f"{PROJECT}.kicad_sch")
    with open(sch_path, "w", encoding="utf-8") as f:
        f.write("\n".join(root) + "\n")
    print(f"wrote {sch_path}: functional index + {len(sheets)} child sheets")

    # standalone symbol lib so the 'nescart' lib nickname resolves
    lib_sym = parse(dump(edge_sym))
    lib_sym[1] = Quoted("Famicom_Cart_Edge")
    sram_lib_sym, _ = get_symbol("SRAM")
    sram_lib_sym = parse(dump(sram_lib_sym))
    sram_lib_sym[1] = Quoted(SRAM_SYMBOL_NAME)
    lib_path = os.path.join(OUT_DIR, f"{PROJECT}.kicad_sym")
    with open(lib_path, "w", encoding="utf-8") as f:
        f.write(dump(["kicad_symbol_lib", parse("(version 20241209)"),
                      parse('(generator "gen_sch")'), lib_sym,
                      sram_lib_sym]) + "\n")
    print(f"wrote {lib_path}")

    pro_path = os.path.join(OUT_DIR, f"{PROJECT}.kicad_pro")
    if not os.path.exists(pro_path):
        with open(pro_path, "w", encoding="utf-8") as f:
            json.dump({"meta": {"filename": f"{PROJECT}.kicad_pro",
                                "version": 3},
                       "schematic": {"legacy_lib_list": []},
                       "boards": [], "libraries":
                       {"pinned_footprint_libs": [],
                        "pinned_symbol_libs": []}}, f, indent=2)
        print(f"wrote {pro_path}")


if __name__ == "__main__":
    main()
