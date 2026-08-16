// ============================================================================
//  Ambience - LV2 wrapper
// ============================================================================
//  A thin shell around FDNReverb::UniversalEngine, which already exposes
//  exactly the API an LV2 run() wants:
//
//      prepare(sampleRate, maxBlock) / reset() / setParams(DSPParams) /
//      processBlock(inL, inR, outL, outR, n)
//
//  Everything here mirrors FDNReverbAudioProcessor in Source/PluginProcessor.cpp.
//  Where it deliberately differs from the VST3, there is a comment saying so.
// ============================================================================

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/options/options.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/urid/urid.h>

#include "ports.h"

#include "DSP/UniversalEngine.h"
#include "AlgorithmPresets.h"
#include "PluginParameters.h"

#include <cstring>
#include <cmath>
#include <new>
#include <vector>

using namespace FDNReverb;

// ----------------------------------------------------------------------------
//  Wet is attenuated by a fixed 1 dB before the gain conversion.
//  Straight from PluginProcessor.cpp:16. Without the headroom the FDN makeup
//  gain keeps OutputLimiter engaged and the tail breaks up. (The comment block
//  around the constant in the VST3 still says -3 dB; the value is -1.)
// ----------------------------------------------------------------------------
static constexpr float kWetInternalOffsetDB = -1.0f;

// Gain ramp length. juce::SmoothedValue::reset(sampleRate, 0.05) in
// prepareToPlay - a 50 ms linear ramp to target.
static constexpr double kGainRampSeconds = 0.05;

// Fallback when the host does not supply bufsz:maxBlockLength. run() never
// allocates, so this only bounds how many chunks a large block is split into.
static constexpr uint32_t kFallbackMaxBlock = 8192;

// ----------------------------------------------------------------------------
//  A linear ramp to a target value, matching juce::SmoothedValue<float>.
// ----------------------------------------------------------------------------
class LinearRamp
{
public:
    void reset (double sampleRate, double rampSeconds) noexcept
    {
        stepsTotal = static_cast<int> (sampleRate * rampSeconds);
        if (stepsTotal < 1) stepsTotal = 1;
        countdown = 0;
        current = target;
    }

    void setTargetValue (float newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;
        countdown = stepsTotal;
        step = (target - current) / static_cast<float> (stepsTotal);
    }

    void snapTo (float value) noexcept
    {
        target = current = value;
        countdown = 0;
    }

    inline float getNextValue() noexcept
    {
        if (countdown <= 0)
            return target;

        current += step;
        if (--countdown == 0)
            current = target;

        return current;
    }

private:
    float current { 0.0f }, target { 0.0f }, step { 0.0f };
    int   stepsTotal { 1 }, countdown { 0 };
};

// ----------------------------------------------------------------------------
//  Plugin instance
// ----------------------------------------------------------------------------
struct Ambience
{
    UniversalEngine engine;

    const float* audioIn[2]  { nullptr, nullptr };
    float*       audioOut[2] { nullptr, nullptr };
    const float* control[ambience::kNumControlPorts] {};

    std::vector<float> wetL, wetR;

    LinearRamp wetGain, dryGain;

    DSPParams lastSentParams {};
    bool      paramsDirty { true };

    double   sampleRate { 48000.0 };
    uint32_t maxBlock { kFallbackMaxBlock };

    // Read a control port.
    //
    // Clamped to the range declared in the TTL, and defaulted when the host
    // left the port unconnected. Neither is paranoia: LV2 does not require
    // hosts to clamp, and the DSP is not defensive about it - feeding this
    // engine a Diffusion of 20000 instead of 0..1 produces NaN out of the
    // allpass network within a few blocks, which then poisons the FDN state
    // permanently. Clamping here is one comparison per port per block.
    inline float ctl (int i) const noexcept
    {
        const ambience::ControlDesc& d = ambience::kControls[i];
        const float* p = control[i];

        if (p == nullptr)
            return d.def;

        const float v = *p;
        if (! (v == v))          // NaN in, default out
            return d.def;

        return v < d.lo ? d.lo : (v > d.hi ? d.hi : v);
    }

    inline int ctlInt (int i) const noexcept
    {
        return static_cast<int> (ctl (i) + 0.5f);
    }
};

// ----------------------------------------------------------------------------
//  Control ports -> DSPParams
// ----------------------------------------------------------------------------
//  A direct transcription of FDNReverbAudioProcessor::updateEngineParams()
//  (Source/PluginProcessor.cpp:44). Three of these are not identity mappings
//  and are the likeliest source of an audible difference from the VST3 if they
//  are ever "cleaned up":
//
//    * roomSizeScale is roomsize MINUS 0.5, not the port value.
//    * decayScale is decaytime divided by the SELECTED ALGORITHM's 500 Hz
//      RT60, so the same Decay Time means different things per algorithm.
//    * wetDB carries the -1 dB internal offset above.
//
//  What is NOT reproduced here: the VST3 calls loadPresetDefaults() when the
//  algorithm changes, force-writing seven other parameters. An LV2 plugin
//  cannot write its own control ports, so that behaviour lives in the modgui's
//  javascript.js instead (mod-ui exposes set_port_value for exactly this), and
//  in the seven "<ALGO> Default" presets for hosts with no modgui.
// ----------------------------------------------------------------------------
static DSPParams gatherParams (const Ambience* self)
{
    using namespace ambience;
    DSPParams p;

    p.algorithmIndex = self->ctlInt (CTL_ALGORITHM);
    if (p.algorithmIndex < 0) p.algorithmIndex = 0;
    if (p.algorithmIndex >= NUM_ALGORITHMS) p.algorithmIndex = NUM_ALGORITHMS - 1;

    p.preDelayMs   = self->ctl (CTL_PREDELAY);
    p.roomSizeScale = self->ctl (CTL_ROOMSIZE) - 0.5f;

    p.decayScale = self->ctl (CTL_DECAYTIME)
                 / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[4];

    p.hfDamping    = self->ctl (CTL_HFDAMPING);
    p.lfAbsorption = self->ctl (CTL_LFABSORPTION);
    p.diffusion    = self->ctl (CTL_DIFFUSION);
    p.modAmount    = self->ctl (CTL_MODAMOUNT);
    p.modRate      = self->ctl (CTL_MODRATE);
    p.stereoWidth  = self->ctl (CTL_STEREOWIDTH);
    p.erLevel      = self->ctl (CTL_ERLEVEL);
    p.saturation   = self->ctl (CTL_SATURATION);
    p.satTypeIdx   = self->ctlInt (CTL_SATTYPE);

    p.wetDB = self->ctl (CTL_WETLEVEL);
    p.dryDB = self->ctl (CTL_DRYLEVEL);

    p.duckingAmount   = self->ctl (CTL_DUCKAMOUNT);
    p.duckingAttackMs = self->ctl (CTL_DUCKATTACK);
    p.duckingRelMs    = self->ctl (CTL_DUCKRELEASE);
    p.duckingThreshDB = self->ctl (CTL_DUCKTHRESH);

    p.erSolo  = self->ctl (CTL_ERSOLO)  > 0.5f;
    p.proMode = self->ctl (CTL_PROMODE) > 0.5f;

    p.tiltLow  = self->ctl (CTL_TILTLOW);
    p.tiltMid  = self->ctl (CTL_TILTMID);
    p.tiltHigh = self->ctl (CTL_TILTHIGH);

    for (int b = 0; b < 10; ++b)
        p.rtBands[b] = self->ctl (CTL_RTBAND0 + b);

    p.loCutHz = self->ctl (CTL_LOCUT);
    p.hiCutHz = self->ctl (CTL_HICUT);

    return p;
}

// ----------------------------------------------------------------------------
//  LV2 entry points
// ----------------------------------------------------------------------------
static LV2_Handle instantiate (const LV2_Descriptor*,
                               double rate,
                               const char*,
                               const LV2_Feature* const* features)
{
    auto* self = new (std::nothrow) Ambience();
    if (self == nullptr)
        return nullptr;

    self->sampleRate = rate;

    // Ask the host for its maximum block length. mod-host runs a fixed JACK
    // period so this is exact there; the fallback only matters for hosts that
    // do not implement the option.
    uint32_t maxBlock = kFallbackMaxBlock;

    const LV2_URID_Map* map = nullptr;
    const LV2_Options_Option* options = nullptr;

    for (int i = 0; features != nullptr && features[i] != nullptr; ++i)
    {
        if (std::strcmp (features[i]->URI, LV2_URID__map) == 0)
            map = static_cast<const LV2_URID_Map*> (features[i]->data);
        else if (std::strcmp (features[i]->URI, LV2_OPTIONS__options) == 0)
            options = static_cast<const LV2_Options_Option*> (features[i]->data);
    }

    if (map != nullptr && options != nullptr)
    {
        const LV2_URID maxBlockUrid = map->map (map->handle, LV2_BUF_SIZE__maxBlockLength);
        const LV2_URID intUrid      = map->map (map->handle, LV2_ATOM__Int);

        for (int i = 0; options[i].key != 0; ++i)
        {
            if (options[i].key == maxBlockUrid && options[i].type == intUrid
                && options[i].value != nullptr)
            {
                const int32_t v = *static_cast<const int32_t*> (options[i].value);
                if (v > 0)
                    maxBlock = static_cast<uint32_t> (v);
                break;
            }
        }
    }

    self->maxBlock = maxBlock;

    // Everything that allocates happens here. run() must not.
    self->engine.prepare (rate, static_cast<int> (maxBlock));
    self->wetL.assign (maxBlock, 0.0f);
    self->wetR.assign (maxBlock, 0.0f);

    self->wetGain.reset (rate, kGainRampSeconds);
    self->dryGain.reset (rate, kGainRampSeconds);

    return static_cast<LV2_Handle> (self);
}

static void connect_port (LV2_Handle instance, uint32_t port, void* data)
{
    auto* self = static_cast<Ambience*> (instance);

    switch (port)
    {
        case ambience::PORT_IN_L:  self->audioIn[0]  = static_cast<const float*> (data); return;
        case ambience::PORT_IN_R:  self->audioIn[1]  = static_cast<const float*> (data); return;
        case ambience::PORT_OUT_L: self->audioOut[0] = static_cast<float*> (data);       return;
        case ambience::PORT_OUT_R: self->audioOut[1] = static_cast<float*> (data);       return;
        default: break;
    }

    const uint32_t c = port - ambience::kFirstControlPort;
    if (c < static_cast<uint32_t> (ambience::kNumControlPorts))
        self->control[c] = static_cast<const float*> (data);
}

static void activate (LV2_Handle instance)
{
    auto* self = static_cast<Ambience*> (instance);

    self->engine.reset();
    self->paramsDirty = true;   // force a setParams() on the first run()

    // Start at the current port values rather than ramping up from silence.
    const float wet = juce::Decibels::decibelsToGain (self->ctl (ambience::CTL_WETLEVEL)
                                                      + kWetInternalOffsetDB);
    const float dry = juce::Decibels::decibelsToGain (self->ctl (ambience::CTL_DRYLEVEL));
    self->wetGain.snapTo (wet);
    self->dryGain.snapTo (dry);
}

static void run (LV2_Handle instance, uint32_t nSamples)
{
    auto* self = static_cast<Ambience*> (instance);

    if (self->audioIn[0] == nullptr || self->audioIn[1] == nullptr
        || self->audioOut[0] == nullptr || self->audioOut[1] == nullptr)
        return;

    // Flush-to-zero for the whole callback. The 16 FDN feedback lines decay
    // into denormal range on every tail; without this the CPU cost spikes
    // exactly when the reverb goes quiet. Mirrors juce::ScopedNoDenormals at
    // the top of processBlock().
    ScopedNoDenormals noDenormals;

    // --- parameter dispatch ------------------------------------------------
    // Same dirty-flag guard as the VST3 (lastSentParams / paramsNeedUpdate in
    // PluginProcessor.h): setParams() re-runs a 10-band weighted-least-squares
    // GEQ fit for each of the 16 FDN channels, so it is worth skipping when
    // nothing moved - but it is NOT worth throttling beyond that.
    //
    // Measured with lv2/tools/benchmark.sh: setParams() costs ~16 us against a
    // ~2700 us budget for a 128-frame block, i.e. even refitting on every
    // single block while a knob is dragged adds well under 1% of a core. An
    // earlier version of this file coalesced updates to one per 20 ms; that
    // bought nothing measurable and added up to 20 ms of latency to every
    // parameter change, so it is gone. processBlock() itself is the cost that
    // matters here - see lv2/README.md.
    const DSPParams p = gatherParams (self);

    if (self->paramsDirty || p != self->lastSentParams)
    {
        self->engine.setParams (p);
        self->lastSentParams = p;
        self->paramsDirty = false;
    }

    // Wet/dry are smoothed sample-by-sample rather than being part of the
    // engine's parameter block, so they always follow the ports immediately.
    self->wetGain.setTargetValue (
        juce::Decibels::decibelsToGain (p.wetDB + kWetInternalOffsetDB));
    self->dryGain.setTargetValue (juce::Decibels::decibelsToGain (p.dryDB));

    // --- audio -------------------------------------------------------------
    // Chunked so an oversized block cannot overrun the scratch buffers that
    // were sized in instantiate(). With a well-behaved host this loops once.
    uint32_t offset = 0;

    while (offset < nSamples)
    {
        const uint32_t n = (nSamples - offset) > self->maxBlock
                         ? self->maxBlock
                         : (nSamples - offset);

        const float* inL = self->audioIn[0] + offset;
        const float* inR = self->audioIn[1] + offset;
        float* outL = self->audioOut[0] + offset;
        float* outR = self->audioOut[1] + offset;

        float* wL = self->wetL.data();
        float* wR = self->wetR.data();

        self->engine.processBlock (inL, inR, wL, wR, static_cast<int> (n));

        // Read the input before writing the output: LV2 permits in-place
        // buffers, so outL may alias inL.
        for (uint32_t i = 0; i < n; ++i)
        {
            const float w = self->wetGain.getNextValue();
            const float d = self->dryGain.getNextValue();
            const float dryL = inL[i];
            const float dryR = inR[i];

            outL[i] = dryL * d + wL[i] * w;
            outR[i] = dryR * d + wR[i] * w;
        }

        offset += n;
    }
}

static void deactivate (LV2_Handle) {}

static void cleanup (LV2_Handle instance)
{
    delete static_cast<Ambience*> (instance);
}

static const void* extension_data (const char*) { return nullptr; }

static const LV2_Descriptor descriptor = {
    AMBIENCE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

extern "C" LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor (uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
