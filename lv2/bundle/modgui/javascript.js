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

    /* Every preset's full parameter set, indexed by the slotN_preset
       port value: 0 is "(None)" and has no values, 1..N are the
       presets in the order gen_bundle.py emits them. Generated from
       the same pass that writes presets.ttl and preset_table.h, so
       all three agree by construction. */
    var SLOT_PRESETS = [
        null    ,
        /* ROOM1 Default        */ { algorithm: 0, predelay: 10.0, roomsize: 0.85, decaytime: 0.21, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.55, modamount: 0.2, modrate: 0.4, stereowidth: 0.8, erlevel: 0.7, saturation: 0.0, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Abbey Road           */ { algorithm: 0, predelay: 10.0, roomsize: 0.75, decaytime: 0.45, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.55, modamount: 0.15, modrate: 0.3, stereowidth: 0.75, erlevel: 0.7, saturation: 0.08, sattype: 0, wetlevel: -6.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.4, rtband1: 1.3, rtband2: 1.2, rtband3: 1.1, rtband4: 1.0, rtband5: 0.95, rtband6: 0.9, rtband7: 0.85, rtband8: 0.8, rtband9: 0.7, locut: 20.0, hicut: 20000.0 },
        /* Drums in a Box       */ { algorithm: 0, predelay: 10.0, roomsize: 0.55, decaytime: 0.3, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.4, modamount: 0.1, modrate: 0.2, stereowidth: 0.65, erlevel: 0.8, saturation: 0.05, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.2, rtband1: 1.1, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 0.9, rtband6: 0.85, rtband7: 0.8, rtband8: 0.7, rtband9: 0.6, locut: 20.0, hicut: 20000.0 },
        /* Tracking Room        */ { algorithm: 0, predelay: 10.0, roomsize: 0.9, decaytime: 0.6, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.65, modamount: 0.2, modrate: 0.35, stereowidth: 0.8, erlevel: 0.65, saturation: 0.1, sattype: 1, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.5, rtband1: 1.4, rtband2: 1.2, rtband3: 1.1, rtband4: 1.0, rtband5: 0.95, rtband6: 0.88, rtband7: 0.82, rtband8: 0.75, rtband9: 0.6, locut: 20.0, hicut: 20000.0 },
        /* ROOM2 Default        */ { algorithm: 1, predelay: 10.0, roomsize: 1.0, decaytime: 1.38, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.6, modamount: 0.25, modrate: 0.45, stereowidth: 0.8, erlevel: 0.65, saturation: 0.05, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Abbey Road 2         */ { algorithm: 1, predelay: 10.0, roomsize: 1.6, decaytime: 1.8, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.7, modamount: 0.25, modrate: 0.4, stereowidth: 0.85, erlevel: 0.6, saturation: 0.08, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.5, rtband1: 1.4, rtband2: 1.3, rtband3: 1.15, rtband4: 1.0, rtband5: 0.93, rtband6: 0.87, rtband7: 0.8, rtband8: 0.73, rtband9: 0.62, locut: 20.0, hicut: 20000.0 },
        /* Capitol Studio A     */ { algorithm: 1, predelay: 10.0, roomsize: 1.35, decaytime: 1.4, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.65, modamount: 0.2, modrate: 0.35, stereowidth: 0.8, erlevel: 0.65, saturation: 0.1, sattype: 1, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.4, rtband1: 1.3, rtband2: 1.2, rtband3: 1.1, rtband4: 1.0, rtband5: 0.92, rtband6: 0.85, rtband7: 0.78, rtband8: 0.7, rtband9: 0.58, locut: 20.0, hicut: 20000.0 },
        /* Skywalker Sound      */ { algorithm: 1, predelay: 10.0, roomsize: 1.7, decaytime: 2.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.75, modamount: 0.3, modrate: 0.45, stereowidth: 0.9, erlevel: 0.55, saturation: 0.06, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.6, rtband1: 1.5, rtband2: 1.35, rtband3: 1.2, rtband4: 1.0, rtband5: 0.92, rtband6: 0.85, rtband7: 0.77, rtband8: 0.68, rtband9: 0.58, locut: 20.0, hicut: 20000.0 },
        /* HALL1 Default        */ { algorithm: 2, predelay: 10.0, roomsize: 1.3, decaytime: 1.89, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.7, modamount: 0.3, modrate: 0.3, stereowidth: 0.8, erlevel: 0.6, saturation: 0.1, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Berlin Konzerthaus   */ { algorithm: 2, predelay: 10.0, roomsize: 1.6, decaytime: 2.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.73, modamount: 0.22, modrate: 0.32, stereowidth: 0.87, erlevel: 0.52, saturation: 0.07, sattype: 0, wetlevel: 0.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.45, rtband1: 1.35, rtband2: 1.25, rtband3: 1.12, rtband4: 1.0, rtband5: 0.93, rtband6: 0.87, rtband7: 0.81, rtband8: 0.74, rtband9: 0.63, locut: 20.0, hicut: 20000.0 },
        /* Carnegie Hall        */ { algorithm: 2, predelay: 10.0, roomsize: 1.5, decaytime: 1.7, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.72, modamount: 0.2, modrate: 0.3, stereowidth: 0.88, erlevel: 0.55, saturation: 0.08, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.45, rtband1: 1.35, rtband2: 1.25, rtband3: 1.12, rtband4: 1.0, rtband5: 0.93, rtband6: 0.87, rtband7: 0.8, rtband8: 0.73, rtband9: 0.62, locut: 20.0, hicut: 20000.0 },
        /* Tokyo Opera City     */ { algorithm: 2, predelay: 10.0, roomsize: 1.65, decaytime: 2.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.75, modamount: 0.25, modrate: 0.35, stereowidth: 0.88, erlevel: 0.5, saturation: 0.06, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.5, rtband1: 1.4, rtband2: 1.3, rtband3: 1.15, rtband4: 1.0, rtband5: 0.94, rtband6: 0.88, rtband7: 0.82, rtband8: 0.75, rtband9: 0.63, locut: 20.0, hicut: 20000.0 },
        /* HALL2 Default        */ { algorithm: 3, predelay: 10.0, roomsize: 1.5, decaytime: 2.08, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.75, modamount: 0.3, modrate: 0.3, stereowidth: 0.8, erlevel: 0.55, saturation: 0.1, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Boston Symphony      */ { algorithm: 3, predelay: 10.0, roomsize: 1.7, decaytime: 1.85, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.78, modamount: 0.22, modrate: 0.28, stereowidth: 0.88, erlevel: 0.52, saturation: 0.06, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.5, rtband1: 1.4, rtband2: 1.28, rtband3: 1.15, rtband4: 1.0, rtband5: 0.93, rtband6: 0.87, rtband7: 0.8, rtband8: 0.73, rtband9: 0.62, locut: 20.0, hicut: 20000.0 },
        /* Concertgebouw        */ { algorithm: 3, predelay: 10.0, roomsize: 1.85, decaytime: 2.05, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.82, modamount: 0.28, modrate: 0.32, stereowidth: 0.92, erlevel: 0.48, saturation: 0.06, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.55, rtband1: 1.45, rtband2: 1.32, rtband3: 1.18, rtband4: 1.0, rtband5: 0.93, rtband6: 0.86, rtband7: 0.78, rtband8: 0.7, rtband9: 0.58, locut: 20.0, hicut: 20000.0 },
        /* Vienna Musikverein   */ { algorithm: 3, predelay: 10.0, roomsize: 1.8, decaytime: 2.05, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.8, modamount: 0.25, modrate: 0.3, stereowidth: 0.9, erlevel: 0.5, saturation: 0.07, sattype: 2, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.55, rtband1: 1.45, rtband2: 1.3, rtband3: 1.18, rtband4: 1.0, rtband5: 0.93, rtband6: 0.87, rtband7: 0.8, rtband8: 0.72, rtband9: 0.6, locut: 20.0, hicut: 20000.0 },
        /* PLATE Default        */ { algorithm: 4, predelay: 10.0, roomsize: 0.7, decaytime: 1.14, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.85, modamount: 0.15, modrate: 0.5, stereowidth: 0.8, erlevel: 0.2, saturation: 0.15, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Dark Plate           */ { algorithm: 4, predelay: 10.0, roomsize: 1.2, decaytime: 2.5, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.88, modamount: 0.35, modrate: 0.55, stereowidth: 0.9, erlevel: 0.1, saturation: 0.1, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.1, rtband1: 1.05, rtband2: 1.02, rtband3: 1.01, rtband4: 1.0, rtband5: 0.98, rtband6: 0.94, rtband7: 0.88, rtband8: 0.8, rtband9: 0.7, locut: 20.0, hicut: 20000.0 },
        /* EMT140 Snare         */ { algorithm: 4, predelay: 10.0, roomsize: 0.8, decaytime: 1.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.8, modamount: 0.2, modrate: 0.45, stereowidth: 0.8, erlevel: 0.35, saturation: 0.15, sattype: 1, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 0.75, rtband1: 0.83, rtband2: 0.9, rtband3: 0.95, rtband4: 1.0, rtband5: 1.03, rtband6: 1.05, rtband7: 1.08, rtband8: 1.06, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* EMT140 Vocal         */ { algorithm: 4, predelay: 10.0, roomsize: 1.0, decaytime: 1.8, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.85, modamount: 0.3, modrate: 0.5, stereowidth: 0.85, erlevel: 0.4, saturation: 0.12, sattype: 1, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 0.8, rtband1: 0.88, rtband2: 0.92, rtband3: 0.96, rtband4: 1.0, rtband5: 1.02, rtband6: 1.04, rtband7: 1.06, rtband8: 1.05, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Surf Guitar          */ { algorithm: 4, predelay: 10.0, roomsize: 0.7, decaytime: 1.5, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.45, modamount: 0.5, modrate: 0.7, stereowidth: 0.7, erlevel: 0.45, saturation: 0.2, sattype: 2, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.3, rtband1: 1.2, rtband2: 1.1, rtband3: 1.05, rtband4: 1.0, rtband5: 0.95, rtband6: 0.9, rtband7: 0.88, rtband8: 0.87, rtband9: 0.85, locut: 20.0, hicut: 20000.0 },
        /* Vintage Studio       */ { algorithm: 4, predelay: 10.0, roomsize: 0.85, decaytime: 2.2, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.5, modamount: 0.55, modrate: 0.65, stereowidth: 0.75, erlevel: 0.4, saturation: 0.35, sattype: 2, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.35, rtband1: 1.25, rtband2: 1.12, rtband3: 1.05, rtband4: 1.0, rtband5: 0.95, rtband6: 0.9, rtband7: 0.88, rtband8: 0.87, rtband9: 0.85, locut: 20.0, hicut: 20000.0 },
        /* SPRING Default       */ { algorithm: 5, predelay: 10.0, roomsize: 0.5, decaytime: 2.93, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.65, modamount: 0.26, modrate: 0.33, stereowidth: 0.8, erlevel: 0.1, saturation: 0.2, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Deep Tank            */ { algorithm: 5, predelay: 10.0, roomsize: 1.1, decaytime: 3.5, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.55, modamount: 0.6, modrate: 0.6, stereowidth: 0.78, erlevel: 0.38, saturation: 0.45, sattype: 2, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.4, rtband1: 1.3, rtband2: 1.15, rtband3: 1.08, rtband4: 1.0, rtband5: 0.95, rtband6: 0.9, rtband7: 0.88, rtband8: 0.87, rtband9: 0.85, locut: 20.0, hicut: 20000.0 },
        /* GOLDFOIL Default     */ { algorithm: 6, predelay: 10.0, roomsize: 0.95, decaytime: 2.06, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.8, modamount: 0.35, modrate: 0.45, stereowidth: 0.8, erlevel: 0.3, saturation: 0.18, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.0, duckrelease: 200.0, duckthresh: -20.0, ersolo: 0, promode: 0, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.0, rtband1: 1.0, rtband2: 1.0, rtband3: 1.0, rtband4: 1.0, rtband5: 1.0, rtband6: 1.0, rtband7: 1.0, rtband8: 1.0, rtband9: 1.0, locut: 20.0, hicut: 20000.0 },
        /* Gothic Cathedral     */ { algorithm: 6, predelay: 10.0, roomsize: 2.0, decaytime: 8.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.9, modamount: 0.4, modrate: 0.25, stereowidth: 0.92, erlevel: 0.35, saturation: 0.05, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.8, rtband1: 1.6, rtband2: 1.4, rtband3: 1.2, rtband4: 1.0, rtband5: 0.92, rtband6: 0.85, rtband7: 0.75, rtband8: 0.65, rtband9: 0.55, locut: 20.0, hicut: 20000.0 },
        /* Infinite Space       */ { algorithm: 6, predelay: 10.0, roomsize: 2.0, decaytime: 12.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.95, modamount: 0.65, modrate: 0.28, stereowidth: 0.95, erlevel: 0.25, saturation: 0.1, sattype: 0, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 2.0, rtband1: 1.8, rtband2: 1.5, rtband3: 1.25, rtband4: 1.0, rtband5: 0.9, rtband6: 0.8, rtband7: 0.7, rtband8: 0.6, rtband9: 0.5, locut: 20.0, hicut: 20000.0 },
        /* Stone Chamber        */ { algorithm: 6, predelay: 10.0, roomsize: 1.8, decaytime: 4.0, hfdamping: 0.0, lfabsorption: 0.0, diffusion: 0.85, modamount: 0.45, modrate: 0.3, stereowidth: 0.88, erlevel: 0.4, saturation: 0.2, sattype: 1, wetlevel: -4.0, drylevel: 0.0, duckamount: 0.0, duckattack: 10.000001, duckrelease: 200.000015, duckthresh: -20.0, ersolo: 0, promode: 1, tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0, rtband0: 1.6, rtband1: 1.5, rtband2: 1.3, rtband3: 1.15, rtband4: 1.0, rtband5: 0.9, rtband6: 0.83, rtband7: 0.75, rtband8: 0.67, rtband9: 0.57, locut: 20.0, hicut: 20000.0 }
    ];

    var NUM_SLOTS = 4;

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

    /* ---------------------------------------------------------------------
       Preset slots
       --------------------------------------------------------------------- */

    /* Light exactly one slot button.
       active_slot is 0 for "nothing recalled", 1..NUM_SLOTS otherwise. */
    function showActiveSlot(active) {
        for (var n = 1; n <= NUM_SLOTS; n++) {
            var led = event.icon.find('.ambience-slot-led[data-slot="' + n + '"]');
            if (led.length === 0) {
                continue;
            }
            led.toggleClass('on', n === active);
        }
    }

    /* Move the knobs to match a slot the user just pressed.

       The DSP has already applied these values internally - that is what makes
       a footswitch work with no browser open. This exists only so the
       on-screen controls stop showing the previous sound.

       Writing all 36 lands as 36 'change' events in one tick, which the
       algorithm guard above correctly reads as a bulk load and ignores. */
    function syncKnobs(data, presetIndex) {
        var values = SLOT_PRESETS[presetIndex];
        if (!values) {
            return;
        }

        data.applying = true;
        try {
            for (var symbol in values) {
                if (Object.prototype.hasOwnProperty.call(values, symbol)) {
                    funcs.set_port_value(symbol, values[symbol]);
                }
            }
        } finally {
            data.applying = false;
        }
    }

    /* A slot button is a trigger port: the host returns it to 0 after the
       press, so only the rising edge means anything. */
    function handleSlotPress(data, slot, value) {
        var wasDown = data.slotDown[slot] === true;
        var isDown = value > 0.5;

        data.slotDown[slot] = isDown;

        if (!data.started || !isDown || wasDown) {
            return;
        }

        syncKnobs(data, Math.round(data.slotPreset[slot] || 0));
    }

    /* Returns the slot number for slotN_select / slotN_preset, else 0.
       "slot1_select" is slot + digit + _ + suffix, i.e. 6 + suffix.length. */
    function slotOf(symbol, suffix) {
        if (symbol.length !== 6 + suffix.length ||
            symbol.slice(0, 4) !== 'slot' ||
            symbol.charAt(5) !== '_' ||
            symbol.slice(6) !== suffix) {
            return 0;
        }
        var n = parseInt(symbol.charAt(4), 10);
        return (n >= 1 && n <= NUM_SLOTS) ? n : 0;
    }

    var data = event.data;

    if (!data.pending) {
        data.pending = {};
        data.timer = null;
        data.applying = false;
        data.started = false;
        data.slotDown = {};
        data.slotPreset = {};
    }

    if (event.type === 'start') {
        /* The opening sweep reports every port's current value. Seeding the
           algorithm here without applying anything is what stops a freshly
           loaded pedalboard from being re-seeded out from under itself. The
           same reasoning covers the slots: record which preset each holds and
           whether its button is down, but recall nothing. */
        var ports = event.ports || {};
        for (var i in ports) {
            var sym = ports[i].symbol;
            var slot = slotOf(sym, 'preset');
            if (slot) {
                data.slotPreset[slot] = ports[i].value;
                continue;
            }
            slot = slotOf(sym, 'select');
            if (slot) {
                data.slotDown[slot] = ports[i].value > 0.5;
                continue;
            }
            if (sym === 'active_slot') {
                showActiveSlot(Math.round(ports[i].value));
            }
        }

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

    /* active_slot is an output port, monitored via modgui:monitoredOutputs.
       It is the DSP telling us which slot is live - never a user action, so
       it does not go through the algorithm batch below. */
    if (event.symbol === 'active_slot') {
        showActiveSlot(Math.round(event.value));
        return;
    }

    var presetSlot = slotOf(event.symbol, 'preset');
    if (presetSlot) {
        data.slotPreset[presetSlot] = event.value;
        return;
    }

    var selectSlot = slotOf(event.symbol, 'select');
    if (selectSlot) {
        handleSlotPress(data, selectSlot, event.value);
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
