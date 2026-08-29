#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PresetManager.h"
#include "GUI/AmbienceUI.h"
#include "GUI/SpectrumAnalyzer.h"
#include "GUI/DecayCurveViz.h"

class FDNReverbEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updatePanelVisibility();

    void refreshPresetCombo();
    void savePresetWithDialog();
    void deleteCurrentPreset();

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // ---- リサイズ対応 (LIFT-X 準拠: アスペクト比固定スケーリング) ----
    static constexpr int kBaseW = 900;
    static constexpr int kBaseH = 540;
    static constexpr int W = kBaseW;
    static constexpr int H = kBaseH;

    struct ContentComponent : public juce::Component
    {
        std::function<void(juce::Graphics&)> onPaint;
        std::function<void()> onLayout;
        void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
        void resized() override { if (onLayout) onLayout(); }
    };

    ContentComponent content;
    juce::ComponentBoundsConstrainer constrainer;

    void paintContent(juce::Graphics& g);
    void layoutContent();

    // ── Components ──
    AlgorithmSelector algoSelector;
    RT60Visualizer    rt60Viz;
    SpectrumAnalyzer  spectrumViz;
    DecayCurveViz     decayCurveViz;
    VUMeter           vuIn, vuOut;
    juce::Label       titleLabel;

    // ── Total Decay Time & Acoustics ──
    juce::Label labelMetricsTitle;
    juce::Label labelDecayLargeValue;
    juce::Label labelBassRatioCaption, labelBassRatioValue;
    juce::Label labelTrebleRatioCaption, labelTrebleRatioValue;

    juce::TextButton proModeButton;
    juce::TextButton erSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> proModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erSoloAttachment;

    bool isProMode{ false };

    // ── Normal Mode Knobs ──
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;
    ArcKnob kLoCutNorm, kHiCutNorm;

    // ── Pro Mode Panel ──
    std::array<ArcKnob, 10> kRTBands;
    juce::Label    satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;
    ArcKnob kLoCutPro, kHiCutPro;

    // ── Preset UI ──
    std::unique_ptr<PresetManager> presetManager;
    juce::TextButton presetPrevButton;
    juce::ComboBox   presetCombo;
    juce::TextButton presetNextButton;
    juce::TextButton presetSaveButton;
    juce::TextButton presetLoadButton;
    juce::TextButton presetDeleteButton;

    // ── Layout Constants ──
    static constexpr int PAD = 8;
    static constexpr int KNOB_W = 64;
    static constexpr int KNOB_H = 72;
    static constexpr int KNOB_LBL_H = 14;
    static constexpr int UNIT_H = 88;
    static constexpr int ROW1_GAP = 18;
    static constexpr int PRESET_PANEL_X = 632;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};
