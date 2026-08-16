# `lv2/` — Ambience as a MOD LV2 plugin

A native LV2 build of Ambience for the MOD environment in
[`LoopPad_Jack`](https://github.com/) — aarch64 / Cortex-A72 (Raspberry Pi 4),
Buildroot 2025.02.6, gcc 13.4, LV2 1.18.10 — with a MOD pedal UI, all 21
factory presets plus 7 per-algorithm defaults in the preset menu, and four
assignable preset buttons with hardware LEDs.

```sh
lv2/build.sh                        # build + package the tar
lv2/build.sh --deploy root@HOST     # ...and install it
lv2/tools/smoke_test.sh             # run the aarch64 binary under qemu
node lv2/tools/test_modgui_js.js    # test the modgui's re-seed and slot logic
lv2/tools/benchmark.sh              # what the DSP costs
```

The output is `lv2/build/ambience-reverb-1.1.0-aarch64.tar.gz`, whose members
are bundle-relative so it drops straight into the device's writable plugin
directory:

```sh
scp lv2/build/ambience-reverb-1.1.0-aarch64.tar.gz root@HOST:/tmp/
ssh root@HOST 'mkdir -p /data/mod/lv2 \
    && tar xzf /tmp/ambience-reverb-1.1.0-aarch64.tar.gz -C /data/mod/lv2 \
    && /etc/init.d/S65modhost restart && /etc/init.d/S66modui restart'
```

Restart **mod-host before mod-ui**: `S66modui` refuses to start while mod-host
is down, and each builds its lilv world once at process start, so neither sees
a new bundle without a restart.

**Nothing in the LoopPad_Jack tree is modified.** It is read for the
cross-compiler, the sysroot and the LV2 headers only. Point `LOOPPAD_ROOT` at
the checkout if it is not at `~/Sources/LoopPad_Jack`.

## How the DSP gets here without JUCE

`Source/DSP/*.cpp` are compiled **unmodified**. The whole JUCE surface those
files use is six things — `jlimit`, `jmax`, `jmin`, `MathConstants::pi`,
`Decibels::decibelsToGain`, `roundToInt` — plus `juce::String` as the type of
the `ParamID` constants. [`src/JuceCompat.h`](src/JuceCompat.h) supplies
exactly those, and [`src/JuceHeader.h`](src/JuceHeader.h) is a stub that the
`#include <JuceHeader.h>` lines resolve to via `-Ilv2/src`.

`juce::dsp::DelayLine` and `ProcessSpec` are *not* provided, and do not need to
be: their only users are `Source/DSP/EarlyReflections.*` and
`Source/DSP/SAPFStage.*`, which are dead code — absent from the VST3's
`CMakeLists.txt` and included by nothing. Early reflections are implemented
inside `UniversalEngine`.

`ScopedNoDenormals` is reimplemented for aarch64 (bit 24, `FZ`, of `FPCR`).
This is not cosmetic: the 16 FDN feedback lines decay into denormal range on
every tail, and without flush-to-zero the CPU cost spikes exactly when the
reverb goes quiet.

The resulting `.so` is ~84 KB and needs only libc, libm, libstdc++, libgcc and
libpthread (the LED thread).

## Differences from the VST3

| | VST3 | here |
|---|---|---|
| Oversampling | `juce::dsp::Oversampling` at factor index 0 | dropped — index 0 is 1×, an identity path |
| Algorithm re-seeds other params | in `processBlock` | in the modgui's `javascript.js` (see below) |
| Pro Mode | opens a GUI panel | a port (presets carry it) with no widget |
| Visualisers | RT60 graph, decay curve, D50/C50/C80/EDT | not ported |
| Preset save/load | `PresetManager` → `~/Documents` | mod-ui's own, into `/data/mod/lv2` |

Three mappings in `gatherParams()` are **not** identity and must stay that way
— they are the likeliest cause of an audible mismatch if anyone "tidies" them:

- `roomSizeScale = roomsize − 0.5`
- `decayScale = decaytime ÷ (selected algorithm's 500 Hz RT60)`
- `wetDB` carries a fixed −1 dB internal offset (`PluginProcessor.cpp:16`; the
  comment there still says −3 dB and is stale)

## The algorithm re-seed

In the VST3, picking an algorithm rewrites Room Size, Decay, Diffusion, Mod
Amount, Mod Rate, ER Level and Saturation from that algorithm's defaults, and
resets HF Damping, LF Absorption, the ten RT band trims and the three tilts to
neutral (`loadPresetDefaults`, `PluginProcessor.cpp:186`).

An LV2 plugin cannot write its own control ports — but a MOD *modgui* can.
mod-ui calls the `modgui:javascript` hook as `jsCallback(event, funcs)` and
`funcs.set_port_value(symbol, value)` exists for precisely this ("added in v1:
allow plugin js code to change plugin controls", `html/js/modgui.js:1682`; this
build is `api_version 3`).

The trap, and the reason `modgui/javascript.js` looks the way it does: **mod-ui
fires a `change` event for every port when a preset or pedalboard loads**
(`modgui.js:507`, `:523`), in no defined order. A handler that simply reacted
to `algorithm` would overwrite each of the 29 presets' own values with generic
defaults at load time, silently. So changes are batched into a microtask and
only a *lone* algorithm change counts as the user turning the knob; several
ports moving in one tick is a bulk load and is left alone.

State lives on `event.data`, never in closure variables — mod-ui compiles one
callback and shares it across every instance of the plugin (`modgui.js:156`),
so a closure would cross-talk between two Ambiences on one pedalboard.

`lv2/tools/test_modgui_js.js` reproduces mod-ui's dispatch and asserts all of
this, including the preset-clobber case and the two-instance case.

## Four preset buttons

`slot1_select`…`slot4_select` recall the preset named by `slot1_preset`…
`slot4_preset`. Address a select port to a pad in mod-ui and that pad becomes a
preset button; only the active one's LED is at full brightness, the other
assigned ones sit dim, unassigned ones are dark.

The constraint that shapes all of this: **an LV2 plugin cannot load its own
presets and cannot write its own control ports.** The algorithm re-seed above
dodges that in the browser, which is useless for a footswitch. So a slot press
applies the preset values *inside the DSP*, held in an override array that
takes precedence over the ports until you move a knob — recall, then tweak, and
moving one control frees only that one.

When the web UI is open, `javascript.js` additionally pushes all 36 values via
`set_port_value` so the knobs follow. Both paths write the same numbers, so
they agree; the override is what covers the browser-closed case, which is the
whole point.

`state:interface` persists the override array and the active slot. Without it a
pedalboard saved after a headless recall would store the *stale knob values* and
reload the wrong sound.

Presets deliberately do **not** set the nine slot ports — a preset that
reassigned the buttons would be circular. `validate_bundle.py` enforces the
split, and will complain if a preset ever grows one.

### LEDs

Via MOD's HMI extension: `lv2:optionalFeature hmi:WidgetControl` plus
`lv2:extensionData hmi:PluginNotification`. **The second is load bearing** —
mod-host only asks for the notification interface if the TTL advertises it, and
without it every addressing silently does nothing.

`lv2/src/lv2-hmi.h` is vendored from the mod-host build this device runs, i.e.
MOD's header plus LoopPad_Jack's `set_led_rgb` patch. There is no copy in the
sysroot, so the two must agree about `LV2_HMI_WidgetControl`'s layout or calls
go through the wrong vtable slot. Re-diff it after a mod-host bump.

Painting runs on a 20 Hz thread, never `run()`: `set_led_rgb` takes a mutex
inside mod-host and writes a shared-memory ring. A last-sent cache means an
idle plugin puts no traffic on that ring at all. On stock MOD firmware, which
has no `set_led_rgb`, the size guard falls back to `set_led_with_brightness` —
chosen over `set_led_with_blink` because brightness is what carries our signal.

**If no LED ever lights but addressing appears to succeed**, suspect mod-host
before the plugin. Standalone mod-host without
`0006-effects-set-up-the-HMI-channel-when-running-standalone.patch` leaves
`g_hmi_wc.handle` NULL and *every* widget call returns at its first line with no
error and no log — `addressed()` still fires with the right index and caps. It
looks exactly like a plugin bug. This device's build has the patch; verify with
`grep MOD_HMI_BUILD` in mod-host's `effects.c`.

## Everything derived is generated

`lv2/tools/gen_bundle.py` is run by `build.sh` on every build. From
[`tools/ports.py`](tools/ports.py) — the port table, transcribed from
`Source/PluginParameters.cpp` — it writes:

```
src/ports.h            port indices, ranges and defaults for the C++ wrapper
src/preset_table.h     the preset VALUES, so a slot button can recall one
bundle/AmbienceReverb.ttl   the port descriptions lilv and mod-ui read
bundle/modgui.ttl           modgui:port entries for all 49 ports
bundle/modgui/javascript.js the algorithm and slot-preset tables
bundle/presets.ttl          29 presets
bundle/default-preset.ttl
bundle/manifest.ttl
```

`presets.ttl`, `preset_table.h` and the modgui's `SLOT_PRESETS` all come out of
one pass over `Presets/*.ambpreset`, so what a button recalls, what the preset
menu loads and what the knobs jump to cannot disagree.

So the C++ port indices and the metadata cannot disagree — they have one
source. Edit `tools/ports.py` (and `modgui/javascript.js.in`), never the
generated files.

The artwork is generated too, by `tools/make_gui_assets.py` (pycairo), and
committed — `build.sh` does not need pycairo. It is drawn rather than reused
because the VST3 editor is entirely vector-drawn JUCE code with no image assets
to port, and MOD's stock knob and pedal PNGs belong to MOD's own bundles under
their own licences. The palette is `AmbienceColors` from
`Source/GUI/AmbienceUI.h`. The knob is a 65-frame horizontal filmstrip, the
format mod-ui derives frame count from at `modgui.js:2151`.

## Presets

29 in the menu: the 21 `Presets/*.ambpreset` files, 7 `<ALGO> Default`
presets built from `PRESET_DEFAULTS` in `Source/AlgorithmPresets.h`, and
`Default`.

`.ambpreset` is `juce::copyXmlToBinary` output — a short binary header then
literal XML with real (not normalised) values. Three quirks in all 21 files are
handled by the converter: the dead parameters `crossfeed` and `oversampling`
are dropped, `<PARAM id="hicut"/>` has no value attribute and falls back to its
default, and `currentPresetName` is not a parameter.

Every preset sets **all 36 ports**. LV2 leaves an unlisted port at whatever the
previous preset set, so a partial preset would make the plugin's sound depend
on load order. `tools/validate_bundle.py` enforces this.

Note the `Presets/` files and the top-level README disagree about *Surf Guitar*
and *Vintage Studio* — the files say algorithm 4 (PLATE), the README says
SPRING. The files win here.

## Verifying

`build.sh` gates on the two failures that are silent at build time and painful
on the device: a host-arch `.so` ("plugin does not load") and a missing modgui
("generic sliders"). It then runs `tools/validate_bundle.py`, which needs
rdflib and skips itself without it — worth installing, since it is what caught
a `<>` vs `<default-preset>` subject mismatch that would have shipped a Default
preset setting nothing.

`tools/smoke_test.sh` runs the real aarch64 binary under qemu-aarch64: impulse
and sustained-noise response for all seven algorithms, every control at both
declared extremes, out-of-range and NaN control values, odd block sizes,
unconnected control ports, the preset slots (including that a recall really
reaches the DSP - it compares tail energy against the same run without one) and
a state save/restore round-trip. qemu is faithful for arithmetic (including `FPCR`
flush-to-zero) but says nothing about timing.

On the device, LoopPad_Jack's `tools/lv2chain` is the authoritative check:

```sh
ssh root@HOST 'LV2_PATH=/usr/lib/lv2:/data/mod/lv2 /tmp/lv2chain -l | grep -i ambience'
ssh root@HOST '/tmp/lv2chain -d https://github.com/OTODESK4193/Ambience1.0.1'
```

Then in mod-ui on `:8888`: the pedal renders as pedal art, the algorithm
dropdown lists all seven, and the preset menu lists 29. Load *Gothic Cathedral*
and confirm decay 8.0 s, GOLDFOIL, and its RT band curve
(1.8/1.6/1.4/1.2/1.0/0.92/0.85/0.75/0.65/0.55) all survive — that is the
preset-clobber case, live. Then turn the algorithm dropdown by hand and confirm
the other knobs *do* jump.

## CPU

`tools/benchmark.sh` on the host reports `run()` at ~7% of one core for a
128-frame block at 48 kHz, i.e. roughly **28–56% of one Cortex-A72 core**. It
is a 16-channel FDN with three serial allpasses and a 10-band biquad cascade
per channel; that is the cost.

`setParams()` — the 16× 10-band weighted-least-squares GEQ refit — measures
~15 µs, about 1/13 of a single block's `run()` cost. An earlier version of
`ambience_lv2.cpp` coalesced parameter updates to one per 20 ms on the
assumption that this refit was expensive. The measurement says it is not, so
the throttle is gone: it bought under 1% of a core and added up to 20 ms of
latency to every parameter change. The engine's own dirty-flag guard (skip
`setParams()` when nothing moved) is kept — that is the guard that matters, and
the VST3 has the same one.

If the device does show xruns, the fix is `run()`, not parameters: raise the
JACK period, or move `setParams()` to an `LV2_Worker` only after confirming
with `top` that it is actually implicated.

## Licence

AGPLv3, same as the rest of the repo.
