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
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include "lv2-hmi.h"
#include "ports.h"
#include "preset_table.h"

#include "DSP/UniversalEngine.h"
#include "AlgorithmPresets.h"
#include "PluginParameters.h"

#include <atomic>
#include <cstring>
#include <cmath>
#include <new>
#include <pthread.h>
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
//  Preset-slot LEDs
// ----------------------------------------------------------------------------
//  The plugin's accent orange, #FF6B00. Full brightness marks the slot whose
//  preset is playing; the other assigned slots sit dim so you can find them in
//  the dark; unassigned slots are off.
// ----------------------------------------------------------------------------
static constexpr uint8_t kLedActive[3]   = { 0xFF, 0x6B, 0x00 };
static constexpr uint8_t kLedAssigned[3] = { 0x28, 0x11, 0x00 };

// How often the LED thread wakes. It sends nothing when nothing changed, so
// this is an upper bound on latency, not on traffic.
static constexpr long kLedIntervalNs = 50 * 1000 * 1000;   // 20 Hz

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

    float* activeSlotPort { nullptr };   // output, may be unconnected

    std::vector<float> wetL, wetR;

    LinearRamp wetGain, dryGain;

    DSPParams lastSentParams {};
    bool      paramsDirty { true };

    double   sampleRate { 48000.0 };
    uint32_t maxBlock { kFallbackMaxBlock };

    // ------------------------------------------------------------------
    //  Preset slots
    // ------------------------------------------------------------------
    //  A slot press cannot move the control ports - an LV2 plugin may not
    //  write its own - so the recalled values are held here and take
    //  precedence over the ports until the user moves a knob.
    //
    //  overridden[i] is cleared as soon as port i changes, so a preset holds
    //  until you touch a control and then only that one control escapes it.
    //  When the web UI is open its javascript.js writes all 36 ports to the
    //  same values, which clears every override to identical numbers - the
    //  sound does not move, and the knobs stop lying.
    float overrideValue[ambience::kNumSoundPorts] {};
    bool  overridden[ambience::kNumSoundPorts] {};
    float lastPortValue[ambience::kNumSoundPorts] {};

    // An LV2 control port carries a level, not an event, so a press is only
    // visible as a change. The first block records a baseline and fires
    // nothing: a host restoring a pedalboard may present any value, and that
    // is not someone pressing a button.
    float slotLast[ambience::kNumSlots] {};
    bool  primed { false };

    std::atomic<int> activeSlot { 0 };   // 0 = none, 1..kNumSlots

    // ------------------------------------------------------------------
    //  MOD HMI
    // ------------------------------------------------------------------
    //  Present only under mod-host built with the widget-control feature.
    //  NULL under jalv, lv2chain and anything else, where every LED path
    //  short-circuits and the plugin is otherwise unaffected.
    const LV2_HMI_WidgetControl* hmiWc { nullptr };

    // Addressing handle per slot, or 0 for "not addressed".
    //
    // Atomic because addressed()/unaddressed() run on mod-host's command
    // thread while the LED thread reads them, and because mod-host REUSES
    // these slots: it clears the actuator on unmap and hands the same slot to
    // the next mapping. Painting through a stale handle would colour someone
    // else's actuator, which is exactly what the extension forbids after
    // unaddressed().
    std::atomic<uintptr_t> hmiAddr[ambience::kNumSlots] {};
    unsigned int           hmiCaps[ambience::kNumSlots] {};

    // Last colour actually sent per slot, so an unchanged LED sends nothing.
    // This is the entire rate limit: an idle plugin puts no traffic on the
    // shared-memory ring at all.
    uint8_t hmiLast[ambience::kNumSlots][3] {};
    bool    hmiLastValid[ambience::kNumSlots] {};

    pthread_t       ledThread {};
    bool            ledThreadRunning { false };
    std::atomic<bool> ledThreadStop { false };
    pthread_mutex_t   ledMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t    ledWake  = PTHREAD_COND_INITIALIZER;

    // Read a control port as the host presents it.
    //
    // Clamped to the range declared in the TTL, and defaulted when the host
    // left the port unconnected. Neither is paranoia: LV2 does not require
    // hosts to clamp, and the DSP is not defensive about it - feeding this
    // engine a Diffusion of 20000 instead of 0..1 produces NaN out of the
    // allpass network within a few blocks, which then poisons the FDN state
    // permanently. Clamping here is one comparison per port per block.
    inline float rawCtl (int i) const noexcept
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

    // The value the DSP should actually use: a preset recalled by a slot
    // button wins over the port until the user moves that control.
    inline float ctl (int i) const noexcept
    {
        if (i < ambience::kNumSoundPorts && overridden[i])
            return overrideValue[i];

        return rawCtl (i);
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
//  Preset slots
// ----------------------------------------------------------------------------

// Recall the preset held by slot `slot` (1-based). A slot set to 0, "(None)",
// does nothing.
//
// This is the whole reason the override array exists. mod-ui's preset menu
// works by writing all 36 control ports, which a plugin cannot do to itself,
// so a slot button applies the values here instead - and that is what makes a
// footswitch work with no browser open.
//
// RT-safe: a 36-float copy plus the setParams() the next block would run
// anyway (~46 us against a 1333 us budget at 64 frames on the device), so
// unlike the LED painting this can happen on the audio thread.
static void recallSlot (Ambience* self, int slot)
{
    const int presetIndex = self->ctlInt (ambience::kFirstSlotPresetCtl + slot - 1);

    if (presetIndex <= 0 || presetIndex > ambience::kNumPresets)
        return;

    const float* values = ambience::kPresetValues[presetIndex - 1];

    for (int i = 0; i < ambience::kNumSoundPorts; ++i)
    {
        self->overrideValue[i] = values[i];
        self->overridden[i] = true;

        // Re-baseline, so the ports as they stand now do not read as a knob
        // move on the very next block and immediately undo the recall.
        self->lastPortValue[i] = self->rawCtl (i);
    }

    self->activeSlot.store (slot, std::memory_order_release);
}

// Clear the override on any control the user has moved since the last block.
static void releaseTouchedControls (Ambience* self)
{
    for (int i = 0; i < ambience::kNumSoundPorts; ++i)
    {
        const float v = self->rawCtl (i);

        if (v != self->lastPortValue[i])
        {
            self->lastPortValue[i] = v;
            self->overridden[i] = false;
        }
    }
}

// Rising edge on any slot button.
//
// These are lv2:trigger ports, so the host returns them to 0 after the press;
// the edge test is what protects a host that does not. The plugin never writes
// them - they are const float* here.
static void pollSlotButtons (Ambience* self)
{
    for (int s = 0; s < ambience::kNumSlots; ++s)
    {
        const float v = self->rawCtl (ambience::kFirstSlotSelectCtl + s);

        if (self->primed && v > 0.5f && self->slotLast[s] <= 0.5f)
            recallSlot (self, s + 1);

        self->slotLast[s] = v;
    }

    self->primed = true;
}

// ----------------------------------------------------------------------------
//  HMI LEDs
// ----------------------------------------------------------------------------
static void hmiPaint (Ambience* self, int slot, LV2_HMI_Addressing a,
                      const uint8_t rgb[3])
{
    const LV2_HMI_WidgetControl* wc = self->hmiWc;

    // Both tests are needed. The size alone would trust a host that reports a
    // big struct with an empty slot; the NULL check alone would read past the
    // end of a smaller struct to find out.
    if (wc->size >= LV2_HMI_WIDGETCONTROL_SIZE_SET_LED_RGB && wc->set_led_rgb != nullptr)
    {
        wc->set_led_rgb (wc->handle, a, rgb[0], rgb[1], rgb[2],
                         LV2_HMI_LED_Blink_None, 0);
        return;
    }

    // Stock MOD firmware: eight fixed colours, no RGB. Fall back to
    // set_led_with_brightness rather than set_led_with_blink, because
    // brightness is what carries our signal - which slot is live. The exact
    // hue is the part we can afford to lose.
    const bool active = (slot == self->activeSlot.load (std::memory_order_acquire));
    const bool assigned = (rgb[0] | rgb[1] | rgb[2]) != 0;

    wc->set_led_with_brightness (wc->handle, a,
                                 assigned ? LV2_HMI_LED_Colour_Yellow
                                          : LV2_HMI_LED_Colour_Off,
                                 assigned ? (active ? 100 : 20) : 0);
}

static void hmiUpdateLeds (Ambience* self)
{
    if (self->hmiWc == nullptr)
        return;

    const int active = self->activeSlot.load (std::memory_order_acquire);

    for (int s = 0; s < ambience::kNumSlots; ++s)
    {
        LV2_HMI_Addressing a = reinterpret_cast<LV2_HMI_Addressing> (
            self->hmiAddr[s].load (std::memory_order_acquire));

        if (a == nullptr)
            continue;

        if ((self->hmiCaps[s] & LV2_HMI_AddressingCapability_LED) == 0)
            continue;

        // An unassigned slot has nothing to recall, so its button stays dark
        // rather than advertising itself as useful.
        const bool assigned =
            self->ctlInt (ambience::kFirstSlotPresetCtl + s) > 0;

        uint8_t rgb[3] = { 0, 0, 0 };
        if (assigned)
        {
            const uint8_t* src = (active == s + 1) ? kLedActive : kLedAssigned;
            rgb[0] = src[0]; rgb[1] = src[1]; rgb[2] = src[2];
        }

        if (self->hmiLastValid[s]
            && self->hmiLast[s][0] == rgb[0]
            && self->hmiLast[s][1] == rgb[1]
            && self->hmiLast[s][2] == rgb[2])
            continue;

        self->hmiLast[s][0] = rgb[0];
        self->hmiLast[s][1] = rgb[1];
        self->hmiLast[s][2] = rgb[2];
        self->hmiLastValid[s] = true;

        hmiPaint (self, s + 1, a, rgb);
    }
}

// Painting runs here and never in run(): set_led_rgb takes a mutex inside
// mod-host and writes to a shared-memory ring, neither of which belongs on the
// audio thread. 20 Hz is far faster than a human pressing buttons, and an
// unchanged LED sends nothing at all.
static void* ledThreadMain (void* arg)
{
    Ambience* self = static_cast<Ambience*> (arg);

    pthread_mutex_lock (&self->ledMutex);

    while (! self->ledThreadStop.load (std::memory_order_acquire))
    {
        hmiUpdateLeds (self);

        struct timespec deadline;
        clock_gettime (CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += kLedIntervalNs;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        pthread_cond_timedwait (&self->ledWake, &self->ledMutex, &deadline);
    }

    pthread_mutex_unlock (&self->ledMutex);
    return nullptr;
}

static void hmiAddressed (LV2_Handle handle, uint32_t index,
                          LV2_HMI_Addressing addressing,
                          const LV2_HMI_AddressingInfo* info)
{
    Ambience* self = static_cast<Ambience*> (handle);

    if (index < static_cast<uint32_t> (ambience::kFirstSlotSelectPort)
        || index >= static_cast<uint32_t> (ambience::kFirstSlotSelectPort
                                           + ambience::kNumSlots))
        return;

    const int s = static_cast<int> (index) - ambience::kFirstSlotSelectPort;

    self->hmiCaps[s] = (info != nullptr) ? info->caps : 0u;

    // Invalidate the cache so the next pass repaints unconditionally: the
    // actuator has just been bound and is showing whatever the surface
    // decided, not what we last sent.
    self->hmiLastValid[s] = false;

    // Released last, so a reader that sees the handle also sees the caps.
    self->hmiAddr[s].store (reinterpret_cast<uintptr_t> (addressing),
                            std::memory_order_release);
}

static void hmiUnaddressed (LV2_Handle handle, uint32_t index)
{
    Ambience* self = static_cast<Ambience*> (handle);

    if (index < static_cast<uint32_t> (ambience::kFirstSlotSelectPort)
        || index >= static_cast<uint32_t> (ambience::kFirstSlotSelectPort
                                           + ambience::kNumSlots))
        return;

    // Store first thing: once this returns, the host is free to hand the same
    // addressing to another plugin.
    self->hmiAddr[static_cast<int> (index) - ambience::kFirstSlotSelectPort]
        .store (0, std::memory_order_release);
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
        else if (std::strcmp (features[i]->URI, LV2_HMI__WidgetControl) == 0)
            self->hmiWc = static_cast<const LV2_HMI_WidgetControl*> (features[i]->data);
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

    // Only worth a thread if the host can actually show an LED. cleanup()
    // joins it before freeing, which is what makes the bare `self` the thread
    // captures safe.
    if (self->hmiWc != nullptr)
    {
        self->ledThreadStop.store (false, std::memory_order_release);

        if (pthread_create (&self->ledThread, nullptr, ledThreadMain, self) == 0)
            self->ledThreadRunning = true;
    }

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
        case ambience::PORT_ACTIVE_SLOT: self->activeSlotPort = static_cast<float*> (data); return;
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

    // --- preset slots ------------------------------------------------------
    // Order matters. Release first, so a control the user moved since the last
    // block escapes its override; then poll the buttons, so a press in this
    // same block re-baselines every control and wins. Doing it the other way
    // round would have a fresh recall immediately undone by its own values
    // reading as "changed".
    releaseTouchedControls (self);
    pollSlotButtons (self);

    if (self->activeSlotPort != nullptr)
        *self->activeSlotPort =
            static_cast<float> (self->activeSlot.load (std::memory_order_acquire));

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
    auto* self = static_cast<Ambience*> (instance);

    // Join before the delete below: the thread holds a bare pointer to self.
    if (self->ledThreadRunning)
    {
        pthread_mutex_lock (&self->ledMutex);
        self->ledThreadStop.store (true, std::memory_order_release);
        pthread_cond_signal (&self->ledWake);
        pthread_mutex_unlock (&self->ledMutex);

        pthread_join (self->ledThread, nullptr);
        self->ledThreadRunning = false;
    }

    delete self;
}

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------
//  A slot recall changes the sound without changing the control ports, because
//  a plugin cannot write its own. With no browser open to sync the knobs, a
//  pedalboard saved after a recall would otherwise store the stale port values
//  and reload the wrong sound. Persisting the override array fixes that, and
//  restores the lit LED with it.
//
//  Stored as one opaque blob rather than 73 typed properties: nothing outside
//  this plugin has any use for the individual fields.
// ----------------------------------------------------------------------------
struct SavedState
{
    uint32_t magic;
    uint32_t version;
    int32_t  activeSlot;
    float    overrideValue[ambience::kNumSoundPorts];
    uint8_t  overridden[ambience::kNumSoundPorts];
};

static constexpr uint32_t kStateMagic = 0x414D4253;   // 'AMBS'
static constexpr uint32_t kStateVersion = 1;

#define AMBIENCE_STATE_URI AMBIENCE_URI "#state"

static LV2_State_Status save_state (LV2_Handle instance,
                                    LV2_State_Store_Function store,
                                    LV2_State_Handle handle,
                                    uint32_t,
                                    const LV2_Feature* const* features)
{
    auto* self = static_cast<Ambience*> (instance);

    const LV2_URID_Map* map = nullptr;
    for (int i = 0; features != nullptr && features[i] != nullptr; ++i)
        if (std::strcmp (features[i]->URI, LV2_URID__map) == 0)
            map = static_cast<const LV2_URID_Map*> (features[i]->data);

    if (map == nullptr)
        return LV2_STATE_ERR_NO_FEATURE;

    SavedState s {};
    s.magic = kStateMagic;
    s.version = kStateVersion;
    s.activeSlot = self->activeSlot.load (std::memory_order_acquire);

    for (int i = 0; i < ambience::kNumSoundPorts; ++i)
    {
        s.overrideValue[i] = self->overrideValue[i];
        s.overridden[i] = self->overridden[i] ? 1u : 0u;
    }

    return store (handle,
                  map->map (map->handle, AMBIENCE_STATE_URI),
                  &s, sizeof (s),
                  map->map (map->handle, LV2_ATOM__Chunk),
                  LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status restore_state (LV2_Handle instance,
                                       LV2_State_Retrieve_Function retrieve,
                                       LV2_State_Handle handle,
                                       uint32_t,
                                       const LV2_Feature* const* features)
{
    auto* self = static_cast<Ambience*> (instance);

    const LV2_URID_Map* map = nullptr;
    for (int i = 0; features != nullptr && features[i] != nullptr; ++i)
        if (std::strcmp (features[i]->URI, LV2_URID__map) == 0)
            map = static_cast<const LV2_URID_Map*> (features[i]->data);

    if (map == nullptr)
        return LV2_STATE_ERR_NO_FEATURE;

    size_t   size = 0;
    uint32_t type = 0;
    uint32_t flags = 0;

    const void* data = retrieve (handle,
                                 map->map (map->handle, AMBIENCE_STATE_URI),
                                 &size, &type, &flags);

    // No state is normal - a pedalboard saved before this feature existed, or
    // one that never recalled a slot.
    if (data == nullptr)
        return LV2_STATE_SUCCESS;

    if (size != sizeof (SavedState))
        return LV2_STATE_ERR_BAD_TYPE;

    SavedState s;
    std::memcpy (&s, data, sizeof (s));

    if (s.magic != kStateMagic || s.version != kStateVersion)
        return LV2_STATE_ERR_BAD_TYPE;

    for (int i = 0; i < ambience::kNumSoundPorts; ++i)
    {
        self->overrideValue[i] = s.overrideValue[i];
        self->overridden[i] = (s.overridden[i] != 0);
    }

    if (s.activeSlot >= 0 && s.activeSlot <= ambience::kNumSlots)
        self->activeSlot.store (s.activeSlot, std::memory_order_release);

    // The ports are about to be presented as they were saved, which is not a
    // knob move. Re-arm the baseline so the restored override is not cleared
    // by the first block after this.
    self->primed = false;
    for (int i = 0; i < ambience::kNumSoundPorts; ++i)
        self->lastPortValue[i] = self->rawCtl (i);

    self->paramsDirty = true;

    return LV2_STATE_SUCCESS;
}

static const void* extension_data (const char* uri)
{
    // mod-host only asks for the HMI interface if the TTL advertises
    // hmi:PluginNotification, and without it every addressing to a hardware
    // actuator silently does nothing.
    if (std::strcmp (uri, LV2_HMI__PluginNotification) == 0)
    {
        static const LV2_HMI_PluginNotification notification = {
            hmiAddressed, hmiUnaddressed
        };
        return &notification;
    }

    if (std::strcmp (uri, LV2_STATE__interface) == 0)
    {
        static const LV2_State_Interface state = { save_state, restore_state };
        return &state;
    }

    return nullptr;
}

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
