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
/* A stand-in for the jQuery object modgui hands the callback as event.icon.
   Only .find() and .toggleClass() are used, and only on the slot LEDs. */
class FakeIcon {
    constructor() {
        this.lit = new Set();
    }

    find(selector) {
        const m = /^\.ambience-slot-led\[data-slot="(\d+)"\]$/.exec(selector);
        if (!m) return { length: 0 };

        const slot = Number(m[1]);
        const lit = this.lit;
        return {
            length: 1,
            toggleClass(cls, on) {
                if (cls !== 'on') throw new Error('unexpected class ' + cls);
                if (on) lit.add(slot); else lit.delete(slot);
            },
        };
    }
}

class Instance {
    constructor(name) {
        this.name = name;
        this.data = {};           // mod-ui's per-instance jsData
        this.ports = {};          // current port values
        this.writes = [];         // what the callback pushed back
        this.icon = new FakeIcon();
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
        // mod-ui passes event.ports as an object of {symbol, value} records.
        const records = Object.entries(ports).map(([symbol, value]) =>
            ({ symbol, value }));
        method({ type: 'start', data: this.data, ports: records,
                 icon: this.icon, api_version: 3 },
               this.funcs);
    }

    change(symbol, value) {
        this.ports[symbol] = value;
        method({ type: 'change', symbol: symbol, value: value, data: this.data,
                 icon: this.icon, api_version: 3 },
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

    /* -- 7. preset slots: only the active LED is lit -------------------- */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, slot2_preset: 11,
                                slot3_preset: 20, slot4_preset: 26,
                                active_slot: 0 }, INITIAL));
        await tick();
        check(a.icon.lit.size === 0, 'no slot LED lit while active_slot is 0');

        a.change('active_slot', 2);
        await tick();
        check(a.icon.lit.size === 1 && a.icon.lit.has(2),
              'active_slot=2 lights slot 2 and only slot 2');

        a.change('active_slot', 4);
        await tick();
        check(a.icon.lit.size === 1 && a.icon.lit.has(4),
              'moving to slot 4 unlights slot 2 (mutual exclusion)');

        a.change('active_slot', 0);
        await tick();
        check(a.icon.lit.size === 0, 'active_slot=0 unlights everything');
    }

    /* -- 8. a step to a new slot moves the knobs ------------------------ */
    /* The DSP owns the cycle and reports where it landed on active_slot; the
       browser follows that, not the button. */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, active_slot: 0 }, INITIAL));
        await tick();

        a.change('active_slot', 1);
        await tick();

        const written = new Map(a.writes);
        check(written.size >= 30,
              'a step to a new slot writes the whole preset to the knobs');
        check(!written.has('slot_next') && !written.has('slot1_preset') &&
              !written.has('active_slot'),
              'it does not write the slot ports back');
        check(a.ports.algorithm !== undefined,
              'the algorithm port is among them');
    }

    /* -- 9. a recall must NOT trigger the algorithm re-seed ------------- */
    /* This is the same hazard as case 3, from the other direction: syncKnobs
       writes 36 ports including algorithm, and if that were mistaken for a
       user turning the algorithm knob the preset would be immediately
       overwritten with the algorithm's generic defaults. */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, active_slot: 0 }, INITIAL));
        await tick();

        a.change('active_slot', 1);
        await tick();

        const roomsizeWrites = a.writes.filter(([s]) => s === 'roomsize');
        check(roomsizeWrites.length === 1,
              'roomsize is written once by the recall, not again by a re-seed');
        check(a.ports.roomsize !== 0.85,
              'roomsize is the preset value, not ROOM1 Default 0.85');
    }

    /* -- 10. the button itself writes nothing --------------------------- */
    /* slot_next is a trigger the DSP acts on. The browser must not recall
       from it as well, or it would have to reimplement the skip-empty-slots
       rule and could disagree with the DSP about where the cycle landed. */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, active_slot: 0 }, INITIAL));
        await tick();

        a.change('slot_next', 1);
        await tick();
        check(a.writes.length === 0, 'pressing the button writes nothing by itself');

        a.change('slot_next', 0);      // host resetting the trigger
        await tick();
        check(a.writes.length === 0, 'and neither does the host resetting it');

        // Only the DSP's answer moves the knobs.
        a.change('active_slot', 1);
        await tick();
        check(a.writes.length > 0, 'the DSP reporting the new slot does');
    }

    /* -- 11. a repeated active_slot does not recall again --------------- */
    /* The port is monitored, so the same value can be reported more than
       once. Re-running syncKnobs would undo a knob the player had tweaked
       since the recall. */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, active_slot: 0 }, INITIAL));
        await tick();

        a.change('active_slot', 1);
        await tick();
        const afterStep = a.writes.length;
        check(afterStep > 0, 'the step recalls');

        a.change('active_slot', 1);
        await tick();
        check(a.writes.length === afterStep,
              'the same active_slot reported again recalls nothing');
    }

    /* -- 12. a pedalboard load with a slot already active --------------- */
    /* The start sweep reports whatever the host restored. active_slot coming
       back as 1 means the state restore already put those values in the DSP,
       and the knobs are the restored ones - writing the preset over them
       would discard whatever the player had tweaked before saving. */
    {
        const a = new Instance('a');
        a.start(Object.assign({ slot1_preset: 2, slot_next: 1,
                                active_slot: 1 }, INITIAL));
        await tick();
        check(a.writes.length === 0,
              'a restored active_slot does not recall on load');
        check(a.icon.lit.has(1),
              'the restored active_slot still lights its LED');

        // Same again for a host that only reports the output port after the
        // sweep, which is the usual case - active_slot is monitored, not part
        // of the start sweep.
        const b = new Instance('b');
        b.start(Object.assign({ slot1_preset: 2 }, INITIAL));
        await tick();
        b.change('active_slot', 1);
        await tick();
        check(b.writes.length === 0,
              'the first active_slot report after load is a baseline, not a step');
        check(b.icon.lit.has(1), 'and it still lights the LED');
    }

    console.log(`\n${failures === 0 ? 'PASS' : 'FAILED'}`);
    process.exit(failures === 0 ? 0 : 1);
}

main();
