#!/usr/bin/env python3
"""
Cross-check the generated bundle the way lilv will read it.

    python3 lv2/tools/validate_bundle.py [bundle-dir]

Needs rdflib. If it is not installed this exits 0 with a notice - the
device-side lilv load is the authoritative check, this just catches the
mistakes that are cheap to make and expensive to find:

  * a modgui:port whose lv2:index or symbol disagrees with the real port,
    which silently mislabels or misroutes a knob;
  * a preset that omits ports - LV2 leaves an unlisted port at whatever the
    last preset set, so a partial preset makes the plugin's sound depend on
    load order;
  * a preset declared in manifest.ttl whose subject URI does not match the one
    in the data file. This one is easy to get wrong and completely silent:
    write <> instead of <default-preset> in default-preset.ttl and the two
    resolve against different bases, so lilv lists a preset that sets nothing.

Everything is parsed with one shared base URI, because that is what makes
relative subjects across files in a bundle resolve to the same thing.
"""

import os
import sys

BUNDLE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "bundle")

try:
    import rdflib
except ImportError:
    print("rdflib not installed - skipping bundle validation.")
    print("Install it (pip install rdflib) or rely on the device-side lilv load.")
    sys.exit(0)

LV2 = rdflib.Namespace("http://lv2plug.in/ns/lv2core#")
MODGUI = rdflib.Namespace("http://moddevices.com/ns/modgui#")
PSET = rdflib.Namespace("http://lv2plug.in/ns/ext/presets#")

# NOTE: LV2.index would return str.index - rdflib.Namespace subclasses str, so
# any term that collides with a string method has to be subscripted.
INDEX = LV2["index"]
SYMBOL = LV2["symbol"]
PORT = LV2["port"]
APPLIES_TO = LV2["appliesTo"]

URI = rdflib.URIRef("https://github.com/OTODESK4193/Ambience1.0.1")

# Any absolute base works; it only has to be the SAME one for every file, so
# that relative subjects across the bundle unify the way lilv unifies them.
BASE = "file:///lv2/AmbienceReverb.lv2/"

FILES = ["manifest.ttl", "AmbienceReverb.ttl", "modgui.ttl",
         "presets.ttl", "default-preset.ttl"]

errors = []


def fail(msg):
    errors.append(msg)


def main():
    graph = rdflib.Graph()
    for name in FILES:
        path = os.path.join(BUNDLE, name)
        if not os.path.exists(path):
            fail("missing file: %s" % name)
            continue
        try:
            graph.parse(path, format="turtle", publicID=BASE + name)
        except Exception as exc:
            fail("%s does not parse: %s" % (name, exc))

    if errors:
        report()
        return

    # --- ports -------------------------------------------------------------
    ports = {}
    for node in graph.objects(URI, PORT):
        idx = graph.value(node, INDEX)
        sym = graph.value(node, SYMBOL)
        if idx is None or sym is None:
            fail("a port is missing lv2:index or lv2:symbol")
            continue
        ports[int(idx)] = str(sym)

    if not ports:
        fail("no lv2:port found on <%s> - is the URI right?" % URI)
        report()
        return

    if sorted(ports) != list(range(len(ports))):
        fail("port indices are not contiguous from 0: %s" % sorted(ports))

    # A preset is made of the SOUND ports only. The preset-slot ports are
    # machinery - a preset that reassigned the buttons would be circular - so
    # they are excluded from the completeness check below rather than making
    # every preset fail it.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ports as P

    sound = {p["sym"] for p in P.SOUND_PORTS}
    outputs = {p["sym"] for p in P.OUTPUT_PORTS}
    controls = {sym for idx, sym in ports.items() if idx >= 4} - outputs

    missing_from_ttl = sound - controls
    if missing_from_ttl:
        fail("ports.py lists sound ports the TTL does not have: %s"
             % sorted(missing_from_ttl))

    print("  ports              %d (%d audio, %d control, %d output)"
          % (len(ports), len(ports) - len(controls) - len(outputs),
             len(controls), len(outputs)))
    print("  sound ports        %d (what a preset is made of)" % len(sound))

    # --- modgui ------------------------------------------------------------
    gui = graph.value(URI, MODGUI.gui)
    if gui is None:
        fail("no modgui:gui - mod-ui would render generic sliders")
    else:
        n = 0
        for node in graph.objects(gui, MODGUI["port"]):
            idx = graph.value(node, INDEX)
            sym = graph.value(node, SYMBOL)
            if idx is None or sym is None:
                fail("a modgui:port is missing lv2:index or lv2:symbol")
                continue
            n += 1
            if ports.get(int(idx)) != str(sym):
                fail("modgui:port index %s says %r, the plugin says %r"
                     % (idx, str(sym), ports.get(int(idx))))
        print("  modgui:port        %d, all matching the plugin's ports" % n)

        for key in ("iconTemplate", "stylesheet", "javascript",
                    "screenshot", "thumbnail"):
            ref = graph.value(gui, MODGUI[key])
            if ref is None:
                fail("modgui:%s is not declared" % key)
                continue
            rel = str(ref)[len(BASE):] if str(ref).startswith(BASE) else str(ref)
            if not os.path.exists(os.path.join(BUNDLE, rel)):
                fail("modgui:%s points at %s, which does not exist" % (key, rel))

    # --- presets -----------------------------------------------------------
    presets = set(graph.subjects(rdflib.RDF.type, PSET.Preset))
    if not presets:
        fail("no pset:Preset found")

    incomplete = 0
    for subject in presets:
        label = graph.value(subject, rdflib.RDFS.label)
        if label is None:
            fail("preset <%s> has no rdfs:label - mod-ui would show it blank"
                 % subject)

        if graph.value(subject, APPLIES_TO) != URI:
            fail("preset %s does not lv2:appliesTo the plugin" % (label or subject))

        got = {str(graph.value(p, SYMBOL)) for p in graph.objects(subject, PORT)}

        # A preset declared in the manifest but whose data file uses a
        # different subject URI shows up here as a preset with zero ports.
        if not got:
            fail("preset %s sets no ports - its subject URI probably differs "
                 "between manifest.ttl and its data file" % (label or subject))
            incomplete += 1
            continue

        unknown = got - sound
        if unknown:
            extra = unknown & controls
            if extra:
                fail("preset %s sets preset-slot ports, which would make "
                     "loading it reassign the buttons: %s"
                     % (label, sorted(extra)))
            if unknown - controls:
                fail("preset %s sets ports that do not exist: %s"
                     % (label, sorted(unknown - controls)))
        missing = sound - got
        if missing:
            fail("preset %s omits %d port(s), so its sound depends on what was "
                 "loaded before it: %s" % (label, len(missing), sorted(missing)))
            incomplete += 1

    print("  presets            %d, %d complete"
          % (len(presets), len(presets) - incomplete))

    report()


def report():
    print()
    if errors:
        for e in errors:
            print("  ERROR: %s" % e)
        print("\n%d problem(s)" % len(errors))
        sys.exit(1)
    print("  bundle OK")


if __name__ == "__main__":
    print("Validating %s:" % os.path.relpath(BUNDLE))
    main()
