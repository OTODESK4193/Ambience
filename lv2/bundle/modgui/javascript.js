/* ===========================================================================
   Ambience - modgui behaviour

   GENERATED: lv2/tools/gen_bundle.py substitutes the algorithm table below
   from PRESET_DEFAULTS in Source/AlgorithmPresets.h. Edit javascript.js.in,
   never javascript.js.

   ---------------------------------------------------------------------------
   What this reproduces and why it lives here

   In the VST3, selecting an algorithm re-seeds seven other parameters from
   that algorithm's own defaults (FDNReverbAudioProcessor::loadPresetDefaults,
   Source/PluginProcessor.cpp:186) so you land on something that sounds like
   the room you just picked instead of the previous room's settings.

   An LV2 plugin cannot write its own control ports, so the DSP cannot do this.
   mod-ui can: the modgui javascript hook is called as jsCallback(event, funcs)
   and funcs.set_port_value(symbol, value) exists for exactly this purpose
   ("added in v1: allow plugin js code to change plugin controls",
   html/js/modgui.js:1682). This build is api_version 3.

   ---------------------------------------------------------------------------
   The trap this code is shaped around

   mod-ui fires a 'change' event for EVERY port when a preset or a pedalboard
   loads (modgui.js:507 and :523), in no defined order. A handler that simply
   reacted to 'algorithm' would therefore overwrite each of the 28 presets'
   Room Size / Decay / Diffusion / Mod / ER / Saturation with the algorithm's
   generic defaults, at load, every time - silently ruining every preset.

   So changes are batched into a microtask and only a LONE algorithm change is
   treated as the user turning the knob. Several ports moving in one tick is a
   bulk load and is left alone. This mirrors the VST3's own guard, which skips
   the re-seed on the first call (`if (lastAlgorithmIndex >= 0)`).

   State lives on event.data, not in closure variables: mod-ui caches one
   compiled callback per plugin and shares it across instances
   (modgui.js:156), so closure state would leak between two Ambiences on the
   same pedalboard. event.data is per instance.
   =========================================================================== */

function (event, funcs) {

    /* From PRESET_DEFAULTS in Source/AlgorithmPresets.h, via
       loadPresetDefaults(). hfDamp/lfAbsorb from that table are
       deliberately ignored and forced to 0, exactly as the VST3
       does: picking an algorithm should reproduce that
       algorithm's own RT60 curve with no correction on top. */
    var ALGORITHM_DEFAULTS = [
        /* ROOM1    */ { roomsize: 0.85, decaytime: 0.21, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.55, modamount: 0.2, modrate: 0.4, erlevel: 0.7, saturation: 0.0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* ROOM2    */ { roomsize: 1.0, decaytime: 1.38, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.6, modamount: 0.25, modrate: 0.45, erlevel: 0.65, saturation: 0.05, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* HALL1    */ { roomsize: 1.3, decaytime: 1.89, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.7, modamount: 0.3, modrate: 0.3, erlevel: 0.6, saturation: 0.1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* HALL2    */ { roomsize: 1.5, decaytime: 2.08, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.75, modamount: 0.3, modrate: 0.3, erlevel: 0.55, saturation: 0.1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* PLATE    */ { roomsize: 0.7, decaytime: 1.14, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.85, modamount: 0.15, modrate: 0.5, erlevel: 0.2, saturation: 0.15, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* SPRING   */ { roomsize: 0.5, decaytime: 2.93, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.65, modamount: 0.26, modrate: 0.33, erlevel: 0.1, saturation: 0.2, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 },
        /* GOLDFOIL */ { roomsize: 0.95, decaytime: 2.06, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.8, modamount: 0.35, modrate: 0.45, erlevel: 0.3, saturation: 0.18, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0 }
    ];

    function applyDefaults(data, index) {
        var def = ALGORITHM_DEFAULTS[index];
        if (!def) {
            return;
        }

        /* set_port_value re-enters this callback with a 'change' event per
           port. That is harmless - 16 symbols in one tick is not a lone
           algorithm change, so the guard below rejects it - but flagging it
           keeps the intent obvious and the batch clean. */
        data.applying = true;
        try {
            for (var symbol in def) {
                if (Object.prototype.hasOwnProperty.call(def, symbol)) {
                    funcs.set_port_value(symbol, def[symbol]);
                }
            }
        } finally {
            data.applying = false;
        }
    }

    function flush(data) {
        var symbols = Object.keys(data.pending);
        var value = data.pending.algorithm;

        data.pending = {};
        data.timer = null;

        /* The whole guard: exactly one port changed, and it was algorithm. */
        if (data.started && symbols.length === 1 && symbols[0] === 'algorithm') {
            applyDefaults(data, Math.round(value));
        }
    }

    var data = event.data;

    if (!data.pending) {
        data.pending = {};
        data.timer = null;
        data.applying = false;
        data.started = false;
    }

    if (event.type === 'start') {
        /* The opening sweep reports every port's current value. Seeding the
           algorithm here without applying anything is what stops a freshly
           loaded pedalboard from being re-seeded out from under itself. */
        data.started = true;
        return;
    }

    if (event.type !== 'change' || data.applying) {
        return;
    }

    /* Patch-parameter changes arrive with a uri instead of a symbol. */
    if (typeof event.symbol !== 'string') {
        return;
    }

    data.pending[event.symbol] = event.value;

    if (data.timer) {
        clearTimeout(data.timer);
    }
    data.timer = setTimeout(function () {
        flush(data);
    }, 0);
}
