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

    // ─── プリセット UI ヘルパー ───
    void refreshPresetCombo();
    void savePresetWithDialog();
    void deleteCurrentPreset();

    FDNReverbAudioProcessor& audioProcessor;
    AmbienceLookAndFeel laf;

    // ─── 共通 ───
    AlgorithmSelector algoSelector;
    RT60Visualizer    rt60Viz;
    SpectrumAnalyzer  spectrumViz;
    DecayCurveViz     decayCurveViz;
    VUMeter           vuIn, vuOut;
    juce::Label       titleLabel;

    juce::Label labelMetricsTitle;
    juce::Label labelD50Caption, labelD50Value;
    juce::Label labelC50Caption, labelC50Value;
    juce::Label labelC80Caption, labelC80Value;
    juce::Label labelEDTCaption, labelEDTValue;

    juce::TextButton proModeButton;
    juce::TextButton erSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> proModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erSoloAttachment;

    bool isProMode{ false };

    // ─── Normal Mode ノブ ───
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;
    ArcKnob kLoCutNorm, kHiCutNorm;

    // ─── ProMode パネル ───
    std::array<ArcKnob, 10> kRTBands;
    juce::Label    satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;
    ArcKnob kLoCutPro, kHiCutPro;

    // ─── プリセット UI ───
    std::unique_ptr<PresetManager> presetManager;
    juce::TextButton presetPrevButton;
    juce::ComboBox   presetCombo;
    juce::TextButton presetNextButton;
    // 既存の presetSaveButton / presetDeleteButton の隣に追加
    juce::TextButton presetSaveButton;
    juce::TextButton presetLoadButton;    // ★ 追加
    juce::TextButton presetDeleteButton;

    // ─── Layout 定数 ───
    static constexpr int W = 900;
    static constexpr int H = 540;
    static constexpr int PAD = 8;
    static constexpr int KNOB_W = 64;
    static constexpr int KNOB_H = 72;
    static constexpr int KNOB_LBL_H = 14;
    static constexpr int UNIT_H = 88;
    static constexpr int ROW1_GAP = 18;

    // プリセットパネルの固定 X 座標
    // DUCKING 終端 (616) + gap(16) = 632
    static constexpr int PRESET_PANEL_X = 632;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FDNReverbEditor)
};