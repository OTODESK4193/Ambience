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

#include "ports.h"

using namespace ambience;

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

    dlclose (lib);

    std::printf ("\n%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
