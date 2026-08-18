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

    const juce::String getName() const override { return "Ambience1.1"; }
    double getTailLengthSeconds() const override { return 20.0; }
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

private:
    void updateEngineParams();

    FDNReverb::UniversalEngine engine;

    // 笏笏笏 繝繝ｼ繝・ぅ繝輔Λ繧ｰ: 繝代Λ繝｡繝ｼ繧ｿ螟牙喧縺後↑縺・ｴ蜷医↓ setParams 繧偵せ繧ｭ繝・・ 笏笏笏
    // processBlock 縺ｯ豈弱ヰ繝・ヵ繧｡ updateEngineParams 繧貞他縺ｶ縺後・
    // designStage2() ﾃ・16 縺ｮ WLS 貍皮ｮ励・螟牙喧縺後↑縺・ｴ蜷医↓螳溯｡後＆縺帙↑縺・・
    FDNReverb::DSPParams lastSentParams;
    bool paramsNeedUpdate{ true };  // 蛻晏屓縺ｯ蠢・★騾√ｋ

    int lastAlgorithmIndex{ -1 };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> smoothWetGain, smoothDryGain;
    std::atomic<float> inputRMS_L{ 0.f }, inputRMS_R{ 0.f };
    std::atomic<float> outputRMS_L{ 0.f }, outputRMS_R{ 0.f };
    double lastSampleRate{ 0.0 };
    

    // 笘・霑ｽ蜉: 繧ｻ繝・す繝ｧ繝ｳ菫晏ｭ倡畑縺ｮ繝励Μ繧ｻ繝・ヨ蜷・
// PresetManager 縺ｯ繧ｨ繝・ぅ繧ｿ繝ｼ蛛ｴ縺ｫ蟄伜惠縺吶ｋ縺溘ａ縲・
// Processor 蛛ｴ縺ｧ繝励Μ繧ｻ繝・ヨ蜷阪・縺ｿ菫晄戟縺励※繧ｻ繝・す繝ｧ繝ｳ菫晏ｭ倥↓蟇ｾ蠢懊☆繧九・
    juce::String lastSavedPresetName;

public:
    // 繧ｨ繝・ぅ繧ｿ繝ｼ縺九ｉ蜻ｼ縺ｳ蜃ｺ縺励※繝励Μ繧ｻ繝・ヨ蜷阪ｒ Processor 縺ｫ騾夂衍縺吶ｋ
    void setLastSavedPresetName(const juce::String& name) noexcept {
        lastSavedPresetName = name;
    }
    juce::String getLastSavedPresetName() const noexcept {
        return lastSavedPresetName;
    }


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbAudioProcessor)
};