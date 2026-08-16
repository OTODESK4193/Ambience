/* ===========================================================================
   Offline smoke test for the Ambience LV2 bundle.

   Loads the .so directly (no lilv, no host, no JACK) and answers the two
   questions that actually go wrong in a DSP port: does it make sound, and does
   it make NaN. Run under qemu-aarch64 on the host by smoke_test.sh, so the
   real aarch64 binary is exercised before anything reaches a device.

   It includes lv2/src/ports.h, so the port count, ranges and defaults it tests
   are by construction the ones the plugin and the TTL agree on.

   On the device, LoopPad_Jack's tools/lv2chain -t does the equivalent inside a
   real lilv world; that remains the authoritative check.
   =========================================================================== */

#include <dlfcn.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include "lv2-hmi.h"
#include "ports.h"
#include "preset_table.h"

#include <map>
#include <string>
#include <vector>

using namespace ambience;

// ---------------------------------------------------------------------------
//  A minimal urid:map, needed by save()/restore().
// ---------------------------------------------------------------------------
static std::map<std::string, LV2_URID> g_urids;

static LV2_URID mapUri (LV2_URID_Map_Handle, const char* uri)
{
    auto it = g_urids.find (uri);
    if (it != g_urids.end())
        return it->second;

    const LV2_URID id = (LV2_URID) (g_urids.size() + 1);
    g_urids[uri] = id;
    return id;
}

static const LV2_Feature* const* stateFeatures()
{
    static LV2_URID_Map map = { nullptr, mapUri };
    static LV2_Feature mapFeature = { LV2_URID__map, &map };
    static const LV2_Feature* features[] = { &mapFeature, nullptr };
    return features;
}

// ---------------------------------------------------------------------------
//  A one-property store, which is all this plugin uses.
// ---------------------------------------------------------------------------
struct StateStore
{
    std::vector<uint8_t> blob;
    size_t   size { 0 };
    uint32_t key { 0 }, type { 0 }, flags { 0 };

    static LV2_State_Status put (LV2_State_Handle handle, uint32_t key,
                                 const void* value, size_t size,
                                 uint32_t type, uint32_t flags)
    {
        auto* self = static_cast<StateStore*> (handle);
        self->key = key;
        self->type = type;
        self->flags = flags;
        self->size = size;
        self->blob.assign ((const uint8_t*) value, (const uint8_t*) value + size);
        return LV2_STATE_SUCCESS;
    }

    static const void* get (LV2_State_Handle handle, uint32_t key,
                            size_t* size, uint32_t* type, uint32_t* flags)
    {
        auto* self = static_cast<StateStore*> (handle);
        if (key != self->key || self->blob.empty())
            return nullptr;

        *size = self->size;
        *type = self->type;
        *flags = self->flags;
        return self->blob.data();
    }
};

static constexpr int    BLOCK = 256;
static constexpr double SAMPLE_RATE = 48000.0;

static float in_l[BLOCK], in_r[BLOCK], out_l[BLOCK], out_r[BLOCK];

struct Stats
{
    double peak { 0 }, sum { 0 };
    long   n { 0 }, nans { 0 }, infs { 0 };

    void add (const float* buf, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const float v = buf[i];
            if (std::isnan (v)) { ++nans; continue; }
            if (std::isinf (v)) { ++infs; continue; }
            const double a = std::fabs ((double) v);
            if (a > peak) peak = a;
            sum += (double) v * (double) v;
            ++n;
        }
    }

    double rms() const { return n ? std::sqrt (sum / (double) n) : 0.0; }
    bool   bad() const { return nans != 0 || infs != 0; }
};

// Deterministic noise - no rand(), so a failure reproduces exactly.
static uint32_t rng = 22222u;
static float noise()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) rng * 2.3283064365386963e-10f * 2.0f - 1.0f;
}

static void fillNoise (float amp = 0.5f)
{
    for (int i = 0; i < BLOCK; ++i)
        in_l[i] = in_r[i] = noise() * amp;
}

// ---------------------------------------------------------------------------
struct Instance
{
    const LV2_Descriptor* d;
    LV2_Handle h;
    std::vector<float> controls;
    float activeSlotPort { 0.0f };

    // Which slot the plugin says is live. Read from the output port rather
    // than from any internal state, so this tests what a host would see.
    int activeSlot() const { return (int) (activeSlotPort + 0.5f); }

    Instance (const LV2_Descriptor* desc)
        : d (desc), controls (kNumControlPorts)
    {
        // No features at all: the plugin must cope with a host that offers
        // neither urid:map nor bufsz options, falling back to its own maximum
        // block length. That fallback is easy to break and silent when broken.
        h = d->instantiate (d, SAMPLE_RATE, "/tmp/", nullptr);
        if (h == nullptr) { std::fprintf (stderr, "instantiate returned NULL\n"); std::exit (1); }

        for (int i = 0; i < kNumControlPorts; ++i)
            controls[i] = kControls[i].def;

        d->connect_port (h, PORT_IN_L,  in_l);
        d->connect_port (h, PORT_IN_R,  in_r);
        d->connect_port (h, PORT_OUT_L, out_l);
        d->connect_port (h, PORT_OUT_R, out_r);
        for (int i = 0; i < kNumControlPorts; ++i)
            d->connect_port (h, (uint32_t) (kFirstControlPort + i), &controls[i]);

        d->connect_port (h, PORT_ACTIVE_SLOT, &activeSlotPort);

        d->activate (h);
    }

    ~Instance() { d->deactivate (h); d->cleanup (h); }

    void run (int n = BLOCK) { d->run (h, (uint32_t) n); }
};

static int failures = 0;

static void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::fprintf (stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// ---------------------------------------------------------------------------
int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s <AmbienceReverb.so>\n", argv[0]);
        return 2;
    }

    void* lib = dlopen (argv[1], RTLD_NOW);
    if (lib == nullptr) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 1; }

    auto getDesc = (const LV2_Descriptor* (*) (uint32_t)) dlsym (lib, "lv2_descriptor");
    if (getDesc == nullptr) { std::fprintf (stderr, "no lv2_descriptor export\n"); return 1; }

    const LV2_Descriptor* d = getDesc (0);
    if (d == nullptr) { std::fprintf (stderr, "lv2_descriptor(0) is NULL\n"); return 1; }

    std::printf ("URI      %s\n", d->URI);
    check (std::strcmp (d->URI, AMBIENCE_URI) == 0, "descriptor URI != AMBIENCE_URI");
    check (getDesc (1) == nullptr, "lv2_descriptor(1) should be NULL");

    // -- 1. impulse response, per algorithm ---------------------------------
    // A fresh instance each time. Sharing one would measure the tail of the
    // previous algorithm as much as this one's onset.
    std::printf ("\nImpulse response, wet only, decay 2.0 s:\n");
    for (int algo = 0; algo < 7; ++algo)
    {
        Instance inst (d);
        inst.controls[CTL_ALGORITHM] = (float) algo;
        inst.controls[CTL_DECAYTIME] = 2.0f;
        inst.controls[CTL_WETLEVEL]  = 0.0f;
        inst.controls[CTL_DRYLEVEL]  = -60.0f;   // wet only: silence == broken

        std::memset (in_l, 0, sizeof in_l);
        std::memset (in_r, 0, sizeof in_r);
        in_l[0] = in_r[0] = 1.0f;

        Stats s;
        const int blocks = (int) (SAMPLE_RATE * 2.0 / BLOCK);
        for (int b = 0; b < blocks; ++b)
        {
            inst.run();
            s.add (out_l, BLOCK);
            s.add (out_r, BLOCK);
            if (b == 0) { std::memset (in_l, 0, sizeof in_l); std::memset (in_r, 0, sizeof in_r); }
        }

        std::printf ("  algo %d  peak %-10.6f rms %-10.6f%s\n",
                     algo, s.peak, s.rms(), s.bad() ? "  <-- NaN/Inf" : "");

        check (! s.bad(), "NaN/Inf in an impulse response");
        check (s.peak > 1e-5, "algorithm produced (near) silence from an impulse");
    }

    // -- 2. sustained noise, per algorithm ----------------------------------
    std::printf ("\nSustained noise, wet only:\n");
    for (int algo = 0; algo < 7; ++algo)
    {
        Instance inst (d);
        inst.controls[CTL_ALGORITHM] = (float) algo;
        inst.controls[CTL_DECAYTIME] = 2.0f;
        inst.controls[CTL_WETLEVEL]  = 0.0f;
        inst.controls[CTL_DRYLEVEL]  = -60.0f;

        // Let the tail build before measuring - the first second of a 2 s
        // reverb is still filling up.
        for (int b = 0; b < (int) (SAMPLE_RATE / BLOCK); ++b) { fillNoise(); inst.run(); }

        Stats s;
        for (int b = 0; b < (int) (SAMPLE_RATE / BLOCK); ++b)
        {
            fillNoise();
            inst.run();
            s.add (out_l, BLOCK);
            s.add (out_r, BLOCK);
        }

        std::printf ("  algo %d  peak %-10.6f rms %-10.6f%s\n",
                     algo, s.peak, s.rms(), s.bad() ? "  <-- NaN/Inf" : "");

        check (! s.bad(), "NaN/Inf under sustained noise");
        check (s.rms() > 1e-4, "algorithm is (near) silent under sustained noise");
    }

    // -- 3. every control at both declared extremes -------------------------
    // Ranges come from ports.h, so this covers exactly what the TTL promises a
    // host may send.
    std::printf ("\nControl sweep (%d ports, declared min and max):\n", kNumControlPorts);
    {
        Instance inst (d);
        int worst = -1;

        for (int c = 0; c < kNumControlPorts; ++c)
        {
            const float saved = inst.controls[c];

            for (int end = 0; end < 2; ++end)
            {
                inst.controls[c] = end ? kControls[c].hi : kControls[c].lo;

                Stats s;
                for (int b = 0; b < 16; ++b)
                {
                    fillNoise();
                    inst.run();
                    s.add (out_l, BLOCK);
                    s.add (out_r, BLOCK);
                }

                if (s.bad())
                {
                    std::fprintf (stderr, "  %s at %s: %ld NaN / %ld Inf\n",
                                  kControls[c].sym, end ? "max" : "min", s.nans, s.infs);
                    worst = c;
                    ++failures;
                }
            }

            inst.controls[c] = saved;
        }

        if (worst < 0)
            std::printf ("  no NaN/Inf\n");
    }

    // -- 4. out-of-range and NaN input on control ports ----------------------
    // Hosts are not required to clamp, so the plugin has to. Without the clamp
    // in Ambience::ctl(), a Diffusion of 20000 poisons the FDN state with NaN
    // within a few blocks and it never recovers.
    std::printf ("\nOut-of-range and NaN control values:\n");
    {
        Instance inst (d);
        const float nan = std::nanf ("");

        for (int c = 0; c < kNumControlPorts; ++c)
        {
            const float saved = inst.controls[c];
            const float wild[] = { -1.0e6f, 1.0e6f, nan };

            for (float v : wild)
            {
                inst.controls[c] = v;

                Stats s;
                for (int b = 0; b < 8; ++b)
                {
                    fillNoise();
                    inst.run();
                    s.add (out_l, BLOCK);
                    s.add (out_r, BLOCK);
                }

                if (s.bad())
                {
                    std::fprintf (stderr, "  %s = %g: %ld NaN / %ld Inf\n",
                                  kControls[c].sym, v, s.nans, s.infs);
                    ++failures;
                }
            }

            inst.controls[c] = saved;
        }
        std::printf ("  clamped cleanly\n");
    }

    // -- 5. block sizes ------------------------------------------------------
    // Odd, tiny and larger-than-usual blocks, to exercise the chunking loop
    // and the gain ramp's countdown.
    std::printf ("\nBlock sizes:\n");
    {
        Instance inst (d);
        Stats s;
        for (int n : { 1, 2, 7, 37, 64, 129, BLOCK })
        {
            fillNoise();
            inst.run (n);
            s.add (out_l, n);
            s.add (out_r, n);
        }
        std::printf ("  1..%d frames: %s\n", BLOCK, s.bad() ? "NaN/Inf" : "clean");
        check (! s.bad(), "NaN/Inf with unusual block sizes");
    }

    // -- 6. unconnected control ports ---------------------------------------
    // connect_port(NULL) is legal; the plugin must fall back to defaults
    // rather than dereference nothing.
    std::printf ("\nUnconnected control ports:\n");
    {
        Instance inst (d);
        for (int i = 0; i < kNumControlPorts; ++i)
            d->connect_port (inst.h, (uint32_t) (kFirstControlPort + i), nullptr);

        Stats s;
        for (int b = 0; b < 16; ++b) { fillNoise(); inst.run(); s.add (out_l, BLOCK); }
        std::printf ("  %s\n", s.bad() ? "NaN/Inf" : "clean (defaults used)");
        check (! s.bad(), "NaN/Inf with unconnected control ports");

        // Reconnect before the destructor runs.
        for (int i = 0; i < kNumControlPorts; ++i)
            d->connect_port (inst.h, (uint32_t) (kFirstControlPort + i), &inst.controls[i]);
    }

    // -- 7. the preset button ------------------------------------------------
    // The whole point of the slots is that they work with no browser and no
    // host preset machinery: the DSP applies the values itself, and it picks
    // the next slot itself too. So these run against the raw plugin, exactly
    // as a footswitch press would arrive.
    std::printf ("\nPreset button:\n");
    {
        Instance inst (d);

        // Slot 1 -> a preset with an unmistakable decay, so a recall is
        // audible in the numbers rather than having to be inferred.
        int gothic = -1;
        for (int i = 0; i < kNumPresets; ++i)
            if (std::strcmp (kPresetNames[i], "Gothic Cathedral") == 0)
                gothic = i;
        check (gothic >= 0, "the Gothic Cathedral preset exists");

        inst.controls[CTL_SLOT1_PRESET] = (float) (gothic + 1);
        inst.controls[CTL_DECAYTIME] = 0.5f;      // nothing like the preset

        auto settle = [&] (int blocks) {
            for (int b = 0; b < blocks; ++b) { fillNoise(); inst.run(); }
        };

        // One press of the one button: 1 -> 0 -> 1 so there is a rising edge.
        auto press = [&] () {
            inst.controls[CTL_SLOT_NEXT] = 0.0f;
            inst.run();
            inst.controls[CTL_SLOT_NEXT] = 1.0f;
            inst.run();
        };

        // The first block must only baseline. A host restoring a pedalboard
        // can present any value on a trigger port, and that is not a press.
        inst.controls[CTL_SLOT_NEXT] = 1.0f;
        inst.run();
        check (inst.activeSlot() == 0,
               "a button already high on the first block does not recall");

        // Nothing recalled yet, so the first real press lands on slot 1.
        press();
        check (inst.activeSlot() == 1, "the first press recalls slot 1");

        // The recall must reach the ENGINE, not just the bookkeeping. Measure
        // the tail one second after the input stops, and compare against the
        // identical run with no recall: the port says decay 0.5 s, the preset
        // says 8 s, so a working recall rings far longer.
        auto measureTail = [&] () {
            settle ((int) (SAMPLE_RATE / BLOCK));
            std::memset (in_l, 0, sizeof in_l);
            std::memset (in_r, 0, sizeof in_r);
            Stats tail;
            for (int b = 0; b < (int) (SAMPLE_RATE / BLOCK); ++b)
            {
                inst.run();
                tail.add (out_l, BLOCK);
            }
            return tail;
        };

        const Stats recalled = measureTail();

        Instance plain (d);                        // same ports, no press
        plain.controls[CTL_DECAYTIME] = 0.5f;
        Stats bare;
        {
            for (int b = 0; b < (int) (SAMPLE_RATE / BLOCK); ++b)
            {
                fillNoise();
                plain.run();
            }
            std::memset (in_l, 0, sizeof in_l);
            std::memset (in_r, 0, sizeof in_r);
            for (int b = 0; b < (int) (SAMPLE_RATE / BLOCK); ++b)
            {
                plain.run();
                bare.add (out_l, BLOCK);
            }
        }

        std::printf ("  tail rms: recalled %.6f vs not-recalled %.6f (%.0fx)\n",
                     recalled.rms(), bare.rms(),
                     bare.rms() > 0 ? recalled.rms() / bare.rms() : 0.0);

        check (! recalled.bad(), "no NaN/Inf after a recall");
        check (recalled.rms() > bare.rms() * 4.0,
               "the recalled 8 s decay rings far longer than the port's 0.5 s, "
               "i.e. the recall reached the DSP");

        // Tweak one knob: it must escape the preset, and only it.
        inst.controls[CTL_STEREOWIDTH] = 0.0f;
        inst.run();
        check (inst.activeSlot() == 1,
               "tweaking a knob does not clear the active slot");

        // A slot set to "(None)" is stepped over, not stopped on: a button
        // that sometimes does nothing reads as a broken button.
        inst.controls[CTL_SLOT2_PRESET] = 0.0f;
        press();
        check (inst.activeSlot() == 3,
               "the cycle skips a slot set to (None)");

        press();
        check (inst.activeSlot() == 4, "and carries on to slot 4");

        press();
        check (inst.activeSlot() == 1, "then wraps from the last slot to the first");

        // With every slot unassigned there is nowhere to go, and the button
        // must leave the sound alone rather than clearing what is playing.
        for (int s = 0; s < kNumSlots; ++s)
            inst.controls[kFirstSlotPresetCtl + s] = 0.0f;
        press();
        check (inst.activeSlot() == 1,
               "with no slot assigned a press changes nothing");

        // Two assigned slots: the button is a toggle between them.
        inst.controls[kFirstSlotPresetCtl + 1] = 1.0f;
        inst.controls[kFirstSlotPresetCtl + 3] = 2.0f;
        press();
        check (inst.activeSlot() == 2, "two assigned slots: forward to the first");
        press();
        check (inst.activeSlot() == 4, "then to the second");
        press();
        check (inst.activeSlot() == 2, "and back again");
    }

    // -- 8. state round-trip -------------------------------------------------
    // Without this a pedalboard saved after a headless recall would store the
    // stale knob positions and reload the wrong sound.
    std::printf ("\nState:\n");
    {
        const LV2_State_Interface* iface =
            (const LV2_State_Interface*) d->extension_data (LV2_STATE__interface);
        check (iface != nullptr, "state:interface is exported");

        const LV2_HMI_PluginNotification* notif =
            (const LV2_HMI_PluginNotification*) d->extension_data (LV2_HMI__PluginNotification);
        check (notif != nullptr && notif->addressed != nullptr,
               "hmi:PluginNotification is exported");

        if (iface != nullptr)
        {
            Instance a (d);
            a.controls[CTL_SLOT1_PRESET] = 3.0f;
            a.controls[CTL_SLOT_NEXT] = 0.0f;
            a.run();
            a.controls[CTL_SLOT_NEXT] = 1.0f;
            a.run();
            check (a.activeSlot() == 1, "recalled before saving");

            StateStore store;
            iface->save (a.h, StateStore::put, &store, 0, stateFeatures());
            check (store.size > 0, "save() stored a blob");

            Instance b (d);
            fillNoise();
            b.run();
            check (b.activeSlot() == 0, "a fresh instance starts with no slot");

            iface->restore (b.h, StateStore::get, &store, 0, stateFeatures());

            // The output port only carries a value once run() has published
            // it, so this checks the restore AND that the restored override
            // survives a block rather than being cleared by ports that never
            // moved.
            fillNoise();
            b.run();
            check (b.activeSlot() == 1,
                   "restore() brings the active slot back, and it survives a block");

            for (int i = 0; i < 8; ++i) { fillNoise(); b.run(); }
            check (b.activeSlot() == 1, "and it is still there eight blocks on");

            // The cycle resumes from the restored slot rather than restarting
            // at 1 - otherwise reloading a pedalboard would silently repeat a
            // sound the player had already stepped past.
            b.controls[CTL_SLOT_NEXT] = 0.0f;
            b.run();
            b.controls[CTL_SLOT_NEXT] = 1.0f;
            b.run();
            check (b.activeSlot() == 2, "the cycle resumes from the restored slot");
        }
    }

    dlclose (lib);

    std::printf ("\n%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
