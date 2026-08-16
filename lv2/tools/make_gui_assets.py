#!/usr/bin/env python3
"""
Draw the modgui artwork: knob filmstrip, pedal chassis, screenshot and
thumbnail. The algorithm dropdown is styled entirely in CSS, so it needs no
image of its own.

Why generated rather than reused: the VST3 editor is 100% vector-drawn JUCE
code (Source/GUI/AmbienceUI.cpp) - there is not a single image or font file in
the repo to port. And MOD's stock knob/pedal PNGs belong to their own bundles
under their own licences, which is not something to quietly vendor into an
AGPL plugin. So the art is drawn here, from the plugin's own palette.

Palette is AmbienceColors from Source/GUI/AmbienceUI.h, so the pedal looks
like the desktop plugin.

Filmstrip format follows mod-ui exactly (html/js/modgui.js:2151): N square
frames laid out horizontally in one PNG, and mod-ui derives
    filmSteps = round(boxHeight * spriteW / (spriteH * boxH)) - 1
which for a square box is just (spriteW / spriteH) - 1. It then shifts
background-position by -frameSize px per step. 65 frames of 128px is what
MOD's own knobs use, so that is what this draws.

Output is committed to the repo, so build.sh never needs pycairo.

    python3 lv2/tools/make_gui_assets.py
"""

import math
import os
import sys

import cairo

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "lv2", "bundle", "modgui")
ASSETS = os.path.join(OUT, "assets")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ports as P
import gen_bundle as G

# The slot ports carry no preset list until the presets have been read, so do
# that here too rather than hardcoding names the screenshot would then get
# wrong the moment a preset is added.
_PRESETS = G.collect_presets()
G.resolve_slot_presets(_PRESETS)
PRESET_LABELS = ["(None)"] + [label for label, _ in _PRESETS]

# --- AmbienceColors, Source/GUI/AmbienceUI.h --------------------------------
BACKGROUND = (0x1A, 0x1A, 0x1A)
SURFACE = (0x24, 0x24, 0x24)
PANEL = (0x2C, 0x2C, 0x2C)
BORDER = (0x3C, 0x3C, 0x3C)
ACCENT = (0xFF, 0x6B, 0x00)
ACCENT_BLUE = (0x40, 0x90, 0xFF)
TEXT_PRIMARY = (0xE8, 0xE8, 0xE8)
TEXT_SECONDARY = (0x88, 0x88, 0x88)
ARC_TRACK = (0x3A, 0x3A, 0x3A)

FRAMES = 65
FRAME = 128

# Knob sweep, matching the JUCE rotary: 270 degrees centred on 12 o'clock.
START_ANGLE = math.radians(135.0)
SWEEP = math.radians(270.0)


def rgb(c, a=1.0):
    return (c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, a)


def set_rgb(cr, c, a=1.0):
    cr.set_source_rgba(*rgb(c, a))


# ---------------------------------------------------------------------------
def draw_knob(cr, cx, cy, radius, t):
    """One knob at normalised position t in [0, 1].

    Every arc opens with new_path(): cairo's arc() appends a line from the
    current point to the start of the arc, so without it each knob is joined
    to the previous one by a stray diagonal.
    """
    # Arc track: a full 270-degree groove behind the fill.
    cr.set_line_width(radius * 0.16)
    cr.set_line_cap(cairo.LINE_CAP_ROUND)

    arc_r = radius * 0.92
    set_rgb(cr, ARC_TRACK)
    cr.new_path()
    cr.arc(cx, cy, arc_r, START_ANGLE, START_ANGLE + SWEEP)
    cr.stroke()

    # Arc fill, blue -> orange across the sweep, as the desktop knobs do.
    if t > 0.001:
        grad = cairo.LinearGradient(cx - arc_r, cy, cx + arc_r, cy)
        grad.add_color_stop_rgba(0.0, *rgb(ACCENT_BLUE))
        grad.add_color_stop_rgba(1.0, *rgb(ACCENT))
        cr.set_source(grad)
        cr.new_path()
        cr.arc(cx, cy, arc_r, START_ANGLE, START_ANGLE + SWEEP * t)
        cr.stroke()

    # Body.
    body_r = radius * 0.66
    grad = cairo.LinearGradient(cx, cy - body_r, cx, cy + body_r)
    grad.add_color_stop_rgba(0.0, *rgb((0x3A, 0x3A, 0x3A)))
    grad.add_color_stop_rgba(1.0, *rgb((0x1E, 0x1E, 0x1E)))
    cr.set_source(grad)
    cr.new_path()
    cr.arc(cx, cy, body_r, 0, 2 * math.pi)
    cr.fill()

    set_rgb(cr, BORDER)
    cr.set_line_width(radius * 0.035)
    cr.new_path()
    cr.arc(cx, cy, body_r, 0, 2 * math.pi)
    cr.stroke()

    # Pointer.
    ang = START_ANGLE + SWEEP * t
    set_rgb(cr, TEXT_PRIMARY)
    cr.set_line_width(radius * 0.09)
    cr.new_path()
    cr.move_to(cx + math.cos(ang) * body_r * 0.30,
               cy + math.sin(ang) * body_r * 0.30)
    cr.line_to(cx + math.cos(ang) * body_r * 0.82,
               cy + math.sin(ang) * body_r * 0.82)
    cr.stroke()


def make_knob_filmstrip(path, frames=FRAMES, size=FRAME):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size * frames, size)
    cr = cairo.Context(surf)
    for i in range(frames):
        draw_knob(cr, i * size + size / 2.0, size / 2.0, size * 0.44,
                  i / float(frames - 1))
    surf.write_to_png(path)
    return size * frames, size


# ---------------------------------------------------------------------------
def rounded_rect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
    cr.close_path()


PEDAL_W, PEDAL_H = 560, 548

# (top, height) of the recessed panels: two knob wells and the preset-slot
# strip at the bottom. Shared with make_screenshot so the render and the
# chassis cannot drift apart.
PANEL_RECTS = ((112, 172), (296, 104), (410, 66))

# Knob row centres, and the row -> panel mapping they imply.
ROW_CY = (160, 232, 336)


def draw_chassis(cr, w=PEDAL_W, h=PEDAL_H):
    """The pedal body. Widgets are laid out over this with flexbox, so this is
    texture and framing only - nothing here has to line up with a knob."""
    grad = cairo.LinearGradient(0, 0, 0, h)
    grad.add_color_stop_rgba(0.0, *rgb((0x22, 0x22, 0x22)))
    grad.add_color_stop_rgba(0.5, *rgb(BACKGROUND))
    grad.add_color_stop_rgba(1.0, *rgb((0x14, 0x14, 0x14)))
    cr.set_source(grad)
    rounded_rect(cr, 0, 0, w, h, 14)
    cr.fill()

    set_rgb(cr, BORDER)
    cr.set_line_width(2)
    rounded_rect(cr, 1, 1, w - 2, h - 2, 13)
    cr.stroke()

    # Accent strip under the header.
    set_rgb(cr, ACCENT)
    cr.rectangle(0, 52, w, 2)
    cr.fill()

    # Two recessed knob panels. Geometry is shared with make_screenshot via
    # PANEL_RECTS so the render and the chassis cannot drift apart.
    for (py, ph) in PANEL_RECTS:
        set_rgb(cr, PANEL, 0.55)
        rounded_rect(cr, 14, py, w - 28, ph, 8)
        cr.fill()
        set_rgb(cr, BORDER, 0.8)
        cr.set_line_width(1)
        rounded_rect(cr, 14.5, py + 0.5, w - 29, ph - 1, 8)
        cr.stroke()

    # No footswitch is drawn here. mod-ui renders the bypass control itself as
    # a DOM element over the chassis (mod-role="bypass"), so baking one into
    # the background would just put a dead second switch on the pedal.


def make_background(path):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, PEDAL_W, PEDAL_H)
    draw_chassis(cairo.Context(surf))
    surf.write_to_png(path)


# ---------------------------------------------------------------------------
def text(cr, s, x, y, size, colour, bold=False, align="center"):
    cr.select_font_face("DejaVu Sans",
                        cairo.FONT_SLANT_NORMAL,
                        cairo.FONT_WEIGHT_BOLD if bold else cairo.FONT_WEIGHT_NORMAL)
    cr.set_font_size(size)
    ext = cr.text_extents(s)
    if align == "center":
        x -= ext.width / 2.0 + ext.x_bearing
    elif align == "right":
        x -= ext.width + ext.x_bearing
    set_rgb(cr, colour)
    cr.move_to(x, y)
    cr.show_text(s)


# Rows of the pedal face, matching icon-ambience.html.
ROWS = [
    ["predelay", "roomsize", "decaytime", "hfdamping"],
    ["lfabsorption", "diffusion", "modamount", "modrate"],
    ["stereowidth", "erlevel", "wetlevel", "drylevel"],
]


def make_screenshot(path, thumb_path):
    """A render of the pedal as it will look in mod-ui. This is what the
    plugin browser shows, so it should match the real face reasonably."""
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, PEDAL_W, PEDAL_H)
    cr = cairo.Context(surf)
    draw_chassis(cr)

    text(cr, "OTODESK", 24, 34, 15, TEXT_SECONDARY, bold=True, align="left")
    text(cr, "AMBIENCE", PEDAL_W - 24, 36, 22, ACCENT, bold=True, align="right")

    # Algorithm selector.
    set_rgb(cr, SURFACE)
    rounded_rect(cr, (PEDAL_W - 220) / 2.0, 70, 220, 30, 5)
    cr.fill()
    set_rgb(cr, ACCENT, 0.75)
    cr.set_line_width(1)
    rounded_rect(cr, (PEDAL_W - 220) / 2.0 + 0.5, 70.5, 219, 29, 5)
    cr.stroke()
    text(cr, P.ALGORITHMS[0], PEDAL_W / 2.0, 90, 14, TEXT_PRIMARY, bold=True)

    defaults = {p["sym"]: p for p in P.CONTROL_PORTS}
    for r, row in enumerate(ROWS):
        # Rows 0 and 1 sit in the first panel, row 2 in the second.
        cy = ROW_CY[r]
        for c, sym in enumerate(row):
            cx = 84 + c * 131
            p = defaults[sym]
            span = float(p["hi"]) - float(p["lo"])
            t = (float(p["default"]) - float(p["lo"])) / span if span else 0.0
            draw_knob(cr, cx, cy, 27, t)
            text(cr, p["name"].upper(), cx, cy + 42, 9, TEXT_SECONDARY, bold=True)

    # Preset slots. Four buttons, each with its LED and the preset it recalls;
    # only the active one is lit, which here is slot 1.
    slot_defaults = [p["default"] for p in P.SLOT_PRESET_PORTS]
    for i in range(P.NUM_SLOTS):
        x = 20 + i * 134
        active = (i == 0)

        # Button.
        set_rgb(cr, (0x2E, 0x2E, 0x2E))
        rounded_rect(cr, x, 420, 122, 22, 4)
        cr.fill()
        set_rgb(cr, BORDER)
        cr.set_line_width(1)
        rounded_rect(cr, x + 0.5, 420.5, 121, 21, 4)
        cr.stroke()

        # LED.
        set_rgb(cr, ACCENT if active else PANEL)
        cr.new_path()
        cr.arc(x + 12, 431, 4, 0, 2 * math.pi)
        cr.fill()

        # Preset name.
        set_rgb(cr, SURFACE)
        rounded_rect(cr, x, 442, 122, 20, 4)
        cr.fill()
        set_rgb(cr, BORDER)
        cr.set_line_width(1)
        rounded_rect(cr, x + 0.5, 442.5, 121, 19, 4)
        cr.stroke()

        label = PRESET_LABELS[int(slot_defaults[i])]
        text(cr, label[:18], x + 61, 456, 9, TEXT_SECONDARY)

    text(cr, "16-CHANNEL FDN REVERB", PEDAL_W / 2.0, PEDAL_H - 26, 9,
         TEXT_SECONDARY)

    surf.write_to_png(path)

    # Thumbnail: mod-ui shows this small in the browser, so scale the whole
    # pedal rather than cropping it.
    from PIL import Image
    im = Image.open(path)
    im.thumbnail((82, 82), Image.LANCZOS)
    im.save(thumb_path)


# ---------------------------------------------------------------------------
def main():
    os.makedirs(ASSETS, exist_ok=True)

    w, h = make_knob_filmstrip(os.path.join(ASSETS, "knob.png"))
    print("  modgui/assets/knob.png            %dx%d (%d frames)" % (w, h, FRAMES))

    make_background(os.path.join(ASSETS, "background.png"))
    print("  modgui/assets/background.png      %dx%d" % (PEDAL_W, PEDAL_H))

    make_screenshot(os.path.join(OUT, "screenshot-ambience.png"),
                    os.path.join(OUT, "thumbnail-ambience.png"))
    print("  modgui/screenshot-ambience.png    %dx%d" % (PEDAL_W, PEDAL_H))
    print("  modgui/thumbnail-ambience.png     82x69")


if __name__ == "__main__":
    main()
