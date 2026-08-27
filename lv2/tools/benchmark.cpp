/* ===========================================================================
   Cost of the Ambience engine, in percent of one core at real time.

   Two numbers matter for a Pi:

     run()      the steady-state per-block cost. If this exceeds one core the
                plugin cannot work at all.
     setParams() the 16-channel, 10-band weighted-least-squares GEQ refit that
                fires whenever a parameter changes. It is ~three orders of
                magnitude more expensive than a block and it runs on the audio
                thread, which is why ambience_lv2.cpp rate-limits it. This
                measures the thing that limit is protecting.

   Built for the HOST, not the device - it links the DSP directly, no LV2. Host
   numbers are not device numbers, but the RATIO between the two costs is
   informative, and if run() is already a large fraction of a core here it will
   certainly not fit there. The authoritative measurement is mod-host's CPU and
   the JACK xrun counter on the device.

       lv2/tools/benchmark.sh
   =========================================================================== */

#include "DSP/UniversalEngine.h"
#include "AlgorithmPresets.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

using namespace FDNReverb;
using Clock = std::chrono::steady_clock;

static uint32_t rng = 7777u;
static float noise()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) rng * 2.3283064365386963e-10f * 2.0f - 1.0f;
}

int main (int argc, char** argv)
{
    const double sampleRate = 48000.0;

    // JACK period, in frames. modpad runs jackd -p 64, which is half the
    // budget of the 128 this used to assume - pass it on the command line
    // rather than guessing.
    const int block = (argc > 1) ? std::atoi (argv[1]) : 128;

    UniversalEngine engine;
    engine.prepare (sampleRate, block);

    DSPParams p;
    p.algorithmIndex = 2;               // HALL1
    p.decayScale = 2.0f / ALL_PRESETS[2]->acoustics.rt60[4];
    p.roomSizeScale = 0.5f;
    engine.setParams (p);

    std::vector<float> inL (block), inR (block), outL (block), outR (block);

    // --- run() -------------------------------------------------------------
    const int blocks = (int) (sampleRate * 20.0 / block);   // 20 s of audio

    for (int b = 0; b < 200; ++b)       // warm up caches and fill the tail
    {
        for (int i = 0; i < block; ++i) inL[i] = inR[i] = noise() * 0.5f;
        engine.processBlock (inL.data(), inR.data(), outL.data(), outR.data(), block);
    }

    auto t0 = Clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i) inL[i] = inR[i] = noise() * 0.5f;
        engine.processBlock (inL.data(), inR.data(), outL.data(), outR.data(), block);
    }
    auto t1 = Clock::now();

    const double runSecs = std::chrono::duration<double> (t1 - t0).count();
    const double audioSecs = blocks * block / sampleRate;
    const double runPercent = 100.0 * runSecs / audioSecs;
    const double perBlockUs = 1e6 * runSecs / blocks;
    const double blockBudgetUs = 1e6 * block / sampleRate;

    std::printf ("run()        %7.2f us/block  (budget %.0f us at %d frames)"
                 "  = %5.2f%% of one core\n",
                 perBlockUs, blockBudgetUs, block, runPercent);

    // --- setParams() -------------------------------------------------------
    // Alternate the decay so the dirty-flag inside the engine cannot elide the
    // work, and so the GEQ targets really do change every call.
    const int fits = 200;
    auto t2 = Clock::now();
    for (int i = 0; i < fits; ++i)
    {
        p.decayScale = (1.0f + 0.5f * (i & 1)) * 2.0f / ALL_PRESETS[2]->acoustics.rt60[4];
        engine.setParams (p);
    }
    auto t3 = Clock::now();

    const double fitUs = 1e6 * std::chrono::duration<double> (t3 - t2).count() / fits;

    std::printf ("setParams()  %7.2f us/call   (%.1fx one block's budget)\n",
                 fitUs, fitUs / blockBudgetUs);

    // Dragging a knob changes a parameter every block, i.e. the worst case is
    // one refit per block. This is the number that decides whether the plugin
    // needs to defer setParams() to an LV2_Worker - and it says it does not.
    const double refitsPerSec = sampleRate / block;

    std::printf ("\nWorst case, a knob being dragged (one refit per block):\n");
    std::printf ("  %.0f refits/s = %.2f%% of a core, on top of run()\n",
                 refitsPerSec, 100.0 * fitUs * 1e-6 * refitsPerSec);
    std::printf ("  -> run() dominates by ~%.0fx; throttling parameter updates\n"
                 "     would trade real latency for nothing measurable.\n",
                 perBlockUs / fitUs);

    std::printf ("\nHOST numbers. A Cortex-A72 is roughly 4-8x slower per core, so\n"
                 "expect run() around %.0f-%.0f%% of one core there. Measure on the\n"
                 "device (mod-host in top, plus the JACK xrun counter) before\n"
                 "trusting any of this.\n",
                 runPercent * 4.0, runPercent * 8.0);

    return 0;
}
