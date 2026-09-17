#pragma once

#include <JuceHeader.h>
#include "DSP/UniversalEngine.h"
#include "PluginParameters.h"
class SpectrumAnalyzer;

class FDNReverbAudioProcessor : public juce::AudioProcessor
{
public:
    SpectrumAnalyzer* specAnalyzer{ nullptr };
    FDNReverbAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { engine.reset(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ambience1.2.1"; }
    double getTailLengthSeconds() const override { return 120.0; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    int getNumPrograms()    override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    std::array<float, FDNReverb::NUM_BANDS> calculateInstantRT60() const noexcept;

    std::array<float, FDNReverb::NUM_BANDS> getRT60ForDisplay() const noexcept {
        return calculateInstantRT60();
    }
    std::array<float, FDNReverb::NUM_BANDS> getTargetRT60ForDisplay() const noexcept {
        return calculateInstantRT60();
    }

    float getInputRMSL()  const noexcept { return inputRMS_L.load(); }
    float getInputRMSR()  const noexcept { return inputRMS_R.load(); }
    float getOutputRMSL() const noexcept { return outputRMS_L.load(); }
    float getOutputRMSR() const noexcept { return outputRMS_R.load(); }

    float getD50() const noexcept { return engine.getD50(); }
    float getC50() const noexcept { return engine.getC50(); }
    float getC80() const noexcept { return engine.getC80(); }
    float getEDT() const noexcept { return engine.getEDT(); }

    const FDNReverb::UniversalEngine& getEngine() const noexcept { return engine; }

    void loadPresetDefaults(int algorithmIndex);

    bool isParamsLocked() const noexcept { return paramsLocked.load(); }
    void setParamsLocked(bool locked) noexcept { paramsLocked.store(locked); }

    bool isBypassed() const noexcept { return bypassEnabled.load(std::memory_order_relaxed); }
    void setBypass(bool b) noexcept { bypassEnabled.store(b, std::memory_order_release); }

    float getDuckingReductionDB() const noexcept { return duckingReductionDB.load(std::memory_order_relaxed); }

    void panic() noexcept { panicRequested.store(true, std::memory_order_release); }

    juce::String getLastSavedPresetName() const noexcept {
        const juce::ScopedLock sl(stateLock);
        return lastSavedPresetName;
    }
    void setLastSavedPresetName(const juce::String& name) noexcept {
        const juce::ScopedLock sl(stateLock);
        lastSavedPresetName = name;
    }

    bool isLastPresetModified() const noexcept {
        const juce::ScopedLock sl(stateLock);
        return lastPresetModified;
    }
    void setLastPresetModified(bool modified) noexcept {
        const juce::ScopedLock sl(stateLock);
        lastPresetModified = modified;
    }

    int getSavedEditorWidth() const noexcept {
        const juce::ScopedLock sl(stateLock);
        return savedEditorWidth;
    }
    int getSavedEditorHeight() const noexcept {
        const juce::ScopedLock sl(stateLock);
        return savedEditorHeight;
    }
    void setSavedEditorSize(int w, int h) noexcept {
        const juce::ScopedLock sl(stateLock);
        savedEditorWidth = w;
        savedEditorHeight = h;
    }

    std::array<float, 2048> specFifoDry;
    std::array<float, 2048> specFifoWet;
    std::atomic<int> specFifoIndex{ 0 };
    std::atomic<bool> specFifoReady{ false };
    std::atomic<bool> isEditorOpen{ false };

    // ★ 高速パラメータ生ポインタキャッシュ (毎ブロックのハッシュ探索を完全根絶)
    struct CachedParams {
        std::atomic<float>* algorithm{ nullptr };
        std::atomic<float>* preDelay{ nullptr };
        std::atomic<float>* roomSize{ nullptr };
        std::atomic<float>* decayTime{ nullptr };
        std::atomic<float>* hfDamping{ nullptr };
        std::atomic<float>* lfAbsorption{ nullptr };
        std::atomic<float>* diffusion{ nullptr };
        std::atomic<float>* modAmount{ nullptr };
        std::atomic<float>* modRate{ nullptr };
        std::atomic<float>* stereoWidth{ nullptr };
        std::atomic<float>* erLevel{ nullptr };
        std::atomic<float>* saturation{ nullptr };
        std::atomic<float>* satType{ nullptr };
        std::atomic<float>* wetLevel{ nullptr };
        std::atomic<float>* dryLevel{ nullptr };
        std::atomic<float>* duckAmount{ nullptr };
        std::atomic<float>* duckAttack{ nullptr };
        std::atomic<float>* duckRelease{ nullptr };
        std::atomic<float>* duckThresh{ nullptr };
        std::atomic<float>* erSolo{ nullptr };
        std::atomic<float>* proMode{ nullptr };
        std::atomic<float>* tiltLow{ nullptr };
        std::atomic<float>* tiltMid{ nullptr };
        std::atomic<float>* tiltHigh{ nullptr };
        std::array<std::atomic<float>*, 10> rtBands{};
        std::atomic<float>* loCut{ nullptr };
        std::atomic<float>* hiCut{ nullptr };
        std::atomic<float>* loEQType{ nullptr };
        std::atomic<float>* hiEQType{ nullptr };
        std::atomic<float>* loGain{ nullptr };
        std::atomic<float>* hiGain{ nullptr };
        std::atomic<float>* scattering{ nullptr };
        std::atomic<float>* erCrossover{ nullptr };
        std::atomic<float>* lateDensity{ nullptr };
        std::atomic<float>* asymmetry{ nullptr };
        std::atomic<float>* clarity{ nullptr };
        std::atomic<float>* airAbsorb{ nullptr };
        std::atomic<float>* rt60Tab{ nullptr };
        std::atomic<float>* proTab{ nullptr };

        void init(juce::AudioProcessorValueTreeState& apvts);
    };

private:
    void updateEngineParams();

    CachedParams cachedParams;

    // ★ 無音時スマートサスペンド（Silence Sleep: アイドル時 15% -> 0.0% 達成）
    int silenceDurationSamples{ 0 };
    int silenceThresholdSamples{ 48000 }; // 1.0秒相当
    bool isEngineSuspended{ false };

    FDNReverb::UniversalEngine engine;
    std::atomic<bool> paramsLocked{ false };
    std::atomic<bool> bypassEnabled{ false };
    std::atomic<float> duckingReductionDB{ 0.0f };
    std::atomic<bool> panicRequested{ false };
    int panicFadeSamplesRemaining{ 0 };
    int panicFadeTotalSamples{ 0 };

    FDNReverb::DSPParams lastSentParams;
    bool paramsNeedUpdate{ true };

    int lastAlgorithmIndex{ -1 };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> stereoBlockBuffer;
    juce::SmoothedValue<float> smoothWetGain, smoothDryGain;
    std::atomic<float> inputRMS_L{ 0.f }, inputRMS_R{ 0.f };
    std::atomic<float> outputRMS_L{ 0.f }, outputRMS_R{ 0.f };
    double lastSampleRate{ 0.0 };

    mutable juce::CriticalSection stateLock;
    juce::String lastSavedPresetName{ "Init" };
    bool lastPresetModified{ false };
    int savedEditorWidth{ 900 };
    int savedEditorHeight{ 540 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbAudioProcessor)
};
