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

    std::array<float, FDNReverb::NUM_BANDS> getRT60ForDisplay() const noexcept {
        return engine.getEffectiveRT60();
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

    void panic() noexcept;

    juce::String getLastSavedPresetName() const noexcept {
        const juce::ScopedLock sl(stateLock);
        return lastSavedPresetName;
    }
    void setLastSavedPresetName(const juce::String& name) noexcept {
        const juce::ScopedLock sl(stateLock);
        lastSavedPresetName = name;
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

private:
    void updateEngineParams();

    FDNReverb::UniversalEngine engine;
    std::atomic<bool> paramsLocked{ false };

    FDNReverb::DSPParams lastSentParams;
    bool paramsNeedUpdate{ true };

    int lastAlgorithmIndex{ -1 };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> smoothWetGain, smoothDryGain;
    std::atomic<float> inputRMS_L{ 0.f }, inputRMS_R{ 0.f };
    std::atomic<float> outputRMS_L{ 0.f }, outputRMS_R{ 0.f };
    double lastSampleRate{ 0.0 };

    mutable juce::CriticalSection stateLock;
    juce::String lastSavedPresetName;
    int savedEditorWidth{ 900 };
    int savedEditorHeight{ 540 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbAudioProcessor)
};
