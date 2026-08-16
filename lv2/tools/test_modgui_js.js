/* ===========================================================================
   Behaviour test for modgui/javascript.js.

       node lv2/tools/test_modgui_js.js

   This is the riskiest single piece of the LV2 port, because when it goes
   wrong it goes wrong SILENTLY and destructively: a handler that reacts to
   every algorithm change overwrites each preset's Room Size / Decay /
   Diffusion / Mod / ER / Saturation at load time, and the user just sees
   presets that do not sound like they used to.

   The harness reproduces the parts of mod-ui the callback actually depends
   on (html/js/modgui.js):

     - the file is evaluated as `method = <code>` (:165), so one shared
       callback serves every instance of the plugin (:156). Two instances are
       run here to prove no state leaks between them.
     - triggerJS attaches per-instance `data` and calls jsCallback(event, funcs)
       (:1730-1742).
     - set_port_value re-enters as a further 'change' event (:1682 -> :507).
     - loading a preset fires one 'change' per port, in arbitrary order (:507).
   =========================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', 'bundle', 'modgui', 'javascript.js');

let method;
eval('method = ' + fs.readFileSync(SRC, 'utf8'));

if (typeof method !== 'function') {
    console.error('FAIL: javascript.js did not evaluate to a function');
    process.exit(1);
}

let failures = 0;
function check(ok, what) {
    console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${what}`);
    if (!ok) failures++;
}

/* --- a fake mod-ui plugin instance -------------------------------------- */
class Instance {
    constructor(name) {
        this.name = name;
        this.data = {};           // mod-ui's per-instance jsData
        this.ports = {};          // current port values
        this.writes = [];         // what the callback pushed back
        this.funcs = {
            set_port_value: (symbol, value) => {
                this.writes.push([symbol, value]);
                this.ports[symbol] = value;
                // mod-ui really does re-enter the callback here.
                this.change(symbol, value);
            },
        };
    }

    start(ports) {
        Object.assign(this.ports, ports);
        method({ type: 'start', data: this.data, ports: ports, api_version: 3 },
               this.funcs);
    }

    change(symbol, value) {
        this.ports[symbol] = value;
        method({ type: 'change', symbol: symbol, value: value, data: this.data,
                 api_version: 3 },
               this.funcs);
    }
}

/* The callback batches with setTimeout(..., 0); drain the queue. */
const tick = () => new Promise((r) => setTimeout(r, 5));

const INITIAL = {
    algorithm: 6, roomsize: 2.0, decaytime: 8.0, diffusion: 0.9,
    modamount: 0.4, modrate: 0.25, erlevel: 0.35, saturation: 0.05,
    hfdamping: 0.0, lfabsorption: 0.0,
    rtband0: 1.8, rtband1: 1.6, rtband2: 1.4, rtband3: 1.2, rtband4: 1.0,
    rtband5: 0.92, rtband6: 0.85, rtband7: 0.75, rtband8: 0.65, rtband9: 0.55,
    tiltlow: 1.0, tiltmid: 1.0, tilthigh: 1.0,
};

async function main() {
    console.log('modgui javascript.js behaviour\n');

    /* -- 1. the opening sweep must not write anything -------------------- */
    {
        const a = new Instance('a');
        a.start(Object.assign({}, INITIAL));
        await tick();
        check(a.writes.length === 0,
              "'start' sweep writes nothing (a fresh pedalboard is not re-seeded)");
    }

    /* -- 2. a lone algorithm change re-seeds ----------------------------- */
    {
        const a = new Instance('a');
        a.start(Object.assign({}, INITIAL));
        await tick();

        a.change('algorithm', 0);          // user picks ROOM1
        await tick();

        const written = new Map(a.writes);
        check(written.size > 0, 'a lone algorithm change re-seeds the other ports');
        check(Math.abs(written.get('roomsize') - 0.85) < 1e-9,
              'ROOM1 roomsize = 0.85 (PRESET_DEFAULTS[0])');
        check(Math.abs(written.get('decaytime') - 0.21) < 1e-9,
              'ROOM1 decaytime = 0.21');
        check(Math.abs(written.get('erlevel') - 0.70) < 1e-9,
              'ROOM1 erlevel = 0.70');
        check(written.get('hfdamping') === 0 && written.get('lfabsorption') === 0,
              'hfdamping/lfabsorption forced to 0, as loadPresetDefaults() does');
        check(written.get('rtband0') === 1 && written.get('rtband9') === 1
              && written.get('tiltlow') === 1,
              'all ten rtband trims and the three tilts reset to neutral');
        check(!written.has('algorithm'),
              'the algorithm port itself is not written back');
    }

    /* -- 3. THE IMPORTANT ONE: a preset load must not be clobbered -------- */
    {
        const a = new Instance('a');
        a.start({ algorithm: 0, roomsize: 0.85, decaytime: 0.21 });
        await tick();

        // "Gothic Cathedral": mod-ui fires one change per port, same tick,
        // arbitrary order - algorithm deliberately NOT first.
        const preset = Object.entries(INITIAL);
        for (const [sym, val] of preset) a.change(sym, val);
        await tick();

        check(a.writes.length === 0,
              'a preset load (many ports in one tick) triggers NO re-seed');
        check(a.ports.decaytime === 8.0 && a.ports.roomsize === 2.0,
              "the preset's own decay 8.0 s and room size 2.0 survive");
        check(a.ports.rtband0 === 1.8 && a.ports.rtband9 === 0.55,
              "the preset's RT band curve survives intact");
    }

    /* -- 4. no feedback loop --------------------------------------------- */
    {
        const a = new Instance('a');
        a.start(Object.assign({}, INITIAL));
        await tick();

        a.change('algorithm', 2);
        await tick();
        const first = a.writes.length;

        await tick();
        await tick();
        check(a.writes.length === first,
              're-seed does not re-trigger itself (set_port_value re-entry is contained)');
    }

    /* -- 5. state does not leak between instances ------------------------ */
    /* The callback object is shared across every instance of the plugin
       (modgui.js:156), so anything kept in a closure instead of event.data
       would cross-talk. */
    {
        const a = new Instance('a');
        const b = new Instance('b');
        a.start(Object.assign({}, INITIAL));
        b.start(Object.assign({}, INITIAL));
        await tick();

        // A gets a lone algorithm change; B gets a bulk load, same tick.
        a.change('algorithm', 4);
        for (const [sym, val] of Object.entries(INITIAL)) b.change(sym, val);
        await tick();

        check(a.writes.length > 0, 'instance A re-seeds on its lone change');
        check(b.writes.length === 0, 'instance B is untouched by A (no shared state)');
    }

    /* -- 6. non-port events are ignored ---------------------------------- */
    {
        const a = new Instance('a');
        a.start(Object.assign({}, INITIAL));
        await tick();

        // Patch-parameter changes carry a uri, not a symbol.
        method({ type: 'change', uri: 'urn:x:thing', value: 1, data: a.data,
                 api_version: 3 }, a.funcs);
        await tick();
        check(a.writes.length === 0, 'patch-parameter changes (uri, no symbol) are ignored');
    }

    console.log(`\n${failures === 0 ? 'PASS' : 'FAILED'}`);
    process.exit(failures === 0 ? 0 : 1);
}

main();
