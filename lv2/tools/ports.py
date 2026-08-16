"""
The Ambience LV2 port table - the single source of truth.

Transcribed from FDNReverb::ParameterHelper::createLayout() in
Source/PluginParameters.cpp, in declaration order. gen_bundle.py turns this
into lv2/src/ports.h, AmbienceReverb.ttl, modgui.ttl and presets.ttl, so the
DSP's view of the ports and the metadata mod-ui reads can never drift apart.

Fields
    sym    LV2 port symbol. Deliberately identical to the JUCE APVTS parameter
           ID, so a .ambpreset converts by name with no mapping table.
    name   Human label (the JUCE parameter name).
    lo/hi/default
           Real-world values, matching NormalisableRange(min, max, 0.01, skew).
    unit   None | 'ms' | 's' | 'hz' | 'db'
    kind   'float' | 'enum' | 'bool'
    skew   The JUCE skew factor. Anything < 1.0 becomes
           pprops:logarithmic, which is LV2's only nonlinear taper. Recorded
           even where it cannot be used so the reason is visible.
    points For 'enum': the ordered choice labels.
"""

# Audio ports come first so the control indices are stable if a param is ever
# appended. mod-ui does not care about the order, only that modgui.ttl's
# lv2:index values match these.
AUDIO_PORTS = [
    ("in_l",  "In L",  "input"),
    ("in_r",  "In R",  "input"),
    ("out_l", "Out L", "output"),
    ("out_r", "Out R", "output"),
]

FIRST_CONTROL_INDEX = len(AUDIO_PORTS)

ALGORITHMS = ["ROOM1", "ROOM2", "HALL1", "HALL2", "PLATE", "SPRING", "GOLDFOIL"]
SAT_TYPES = ["Warm", "Tape", "Tube", "Hard"]


def f(sym, name, lo, hi, default, skew=1.0, unit=None):
    return dict(sym=sym, name=name, lo=lo, hi=hi, default=default,
                skew=skew, unit=unit, kind="float", points=None)


def enum(sym, name, points, default=0):
    return dict(sym=sym, name=name, lo=0, hi=len(points) - 1, default=default,
                skew=1.0, unit=None, kind="enum", points=points)


def boolean(sym, name, default=0):
    return dict(sym=sym, name=name, lo=0, hi=1, default=default,
                skew=1.0, unit=None, kind="bool", points=None)


CONTROL_PORTS = [
    enum("algorithm", "Algorithm", ALGORITHMS),

    # skew 1.0 and a 0.0 minimum: must stay linear, pprops:logarithmic is
    # undefined for a range that includes zero.
    f("predelay",     "Pre-Delay",     0.0,   500.0,  10.0,  1.00, "ms"),
    f("roomsize",     "Room Size",     0.3,   2.0,    1.0),
    f("decaytime",    "Decay Time",    0.1,   20.0,   1.5,   0.35, "s"),
    f("hfdamping",    "HF Damping",    0.0,   1.0,    0.0),
    f("lfabsorption", "LF Absorption", 0.0,   1.0,    0.0),
    f("diffusion",    "Diffusion",     0.0,   1.0,    0.7),
    f("modamount",    "Mod Amount",    0.0,   1.0,    0.25),
    f("modrate",      "Mod Rate",      0.05,  2.0,    0.5,   1.00, "hz"),
    f("stereowidth",  "Stereo Width",  0.0,   1.0,    0.8),
    f("erlevel",      "ER Level",      0.0,   1.0,    0.6),
    f("saturation",   "Saturation",    0.0,   1.0,    0.0),

    enum("sattype", "Sat Type", SAT_TYPES),

    f("wetlevel",     "Wet",          -60.0,  0.0,   -4.0,   1.00, "db"),
    f("drylevel",     "Dry",          -60.0,  0.0,    0.0,   1.00, "db"),

    f("duckamount",   "Ducking",       0.0,   20.0,   0.0,   1.00, "db"),
    f("duckattack",   "Duck Attack",   0.5,   100.0,  10.0,  0.40, "ms"),
    f("duckrelease",  "Duck Release",  10.0,  2000.0, 200.0, 0.40, "ms"),
    f("duckthresh",   "Duck Thresh",  -60.0,  0.0,   -20.0,  1.00, "db"),

    boolean("ersolo",  "ER Solo"),
    # promode drives no DSP - it only opened the Pro panel in the VST3 editor.
    # It is a port anyway because every .ambpreset carries promode=1.0, and a
    # preset that cannot round-trip its own contents is a preset that silently
    # changes the sound of the ones that follow it.
    boolean("promode", "Pro Mode"),

    f("tiltlow",  "Tilt Low",  0.5, 2.0, 1.0),
    f("tiltmid",  "Tilt Mid",  0.5, 2.0, 1.0),
    f("tilthigh", "Tilt High", 0.5, 2.0, 1.0),

    f("rtband0", "RT 31Hz",  0.5, 2.0, 1.0),
    f("rtband1", "RT 62Hz",  0.5, 2.0, 1.0),
    f("rtband2", "RT 125Hz", 0.5, 2.0, 1.0),
    f("rtband3", "RT 250Hz", 0.5, 2.0, 1.0),
    f("rtband4", "RT 500Hz", 0.5, 2.0, 1.0),
    f("rtband5", "RT 1kHz",  0.5, 2.0, 1.0),
    f("rtband6", "RT 2kHz",  0.5, 2.0, 1.0),
    f("rtband7", "RT 4kHz",  0.5, 2.0, 1.0),
    f("rtband8", "RT 8kHz",  0.5, 2.0, 1.0),
    f("rtband9", "RT 16kHz", 0.5, 2.0, 1.0),

    f("locut", "Lo Cut",  20.0,   500.0,   20.0,    0.30, "hz"),
    f("hicut", "Hi Cut",  1000.0, 20000.0, 20000.0, 0.30, "hz"),
]

BY_SYM = {p["sym"]: p for p in CONTROL_PORTS}

# Ports the pedal face shows. Everything else still exists and still works via
# presets, MIDI/CV assignment and the host's generic control list - it just is
# not painted on the pedal. 36 knobs on a pedal-sized icon is not a UI.
GUI_PORTS = [
    "algorithm",
    "predelay", "roomsize", "decaytime",
    "hfdamping", "lfabsorption", "diffusion",
    "modamount", "modrate", "stereowidth",
    "erlevel", "wetlevel", "drylevel",
]


def index_of(sym):
    """LV2 port index for a control symbol."""
    for i, p in enumerate(CONTROL_PORTS):
        if p["sym"] == sym:
            return FIRST_CONTROL_INDEX + i
    raise KeyError(sym)
