#pragma once

#include <JuceHeader.h>
#include "Version.h"
#include "PluginProcessor.h"
#include "PresetManager.h"
#include "GUI/AmbienceUI.h"
#include "GUI/SpectrumAnalyzer.h"
#include "GUI/DecayCurveViz.h"
#include "GUI/PresetBrowser.h"

class FDNReverbEditor : public juce::AudioProcessorEditor,
    private juce::Timer,
    private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit FDNReverbEditor(FDNReverbAudioProcessor&);
    ~FDNReverbEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updatePanelVisibility();

    void refreshPresetCombo();
    void savePresetWithDialog();
    void deleteCurrentPreset();
    void setPresetModified(bool modified);

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

    juce::Label labelDecayLine;

    juce::TextButton mainTabButton;
    juce::TextButton rt60TabButton;
    juce::TextButton proTabButton;
    juce::TextButton erSoloButton;
    juce::TextButton lockButton;
    juce::TextButton sendModeButton;
    juce::TextButton panicButton;
    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rt60TabAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> proTabAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erSoloAttachment;

    bool isRT60Tab{ false };
    bool isProTab{ false };
    float tabTransitionAlpha{ 1.0f };
    float tabPillCurrentX{ 175.0f };
    float tabPillTargetX{ 175.0f };
    float contentSlideOffset{ 0.0f };

    // ── Normal Mode Knobs ──
    ArcKnob kPreDelay, kRoomSize, kDecay;
    ArcKnob kHFDamp, kLFAbsorb;
    ArcKnob kDiffusion, kModAmt, kModRate;
    ArcKnob kStereoW;
    ArcKnob kERLevel, kSaturation;
    ArcKnob kWet, kDry;
    ArcKnob kDuckAmt, kDuckThr, kDuckAtt, kDuckRel;
    ArcKnob kLoCutNorm, kHiCutNorm;

    // ── RT60 Tab Panel ──
    std::array<ArcKnob, 10> kRTBands;
    juce::Label    satTypeLabel;
    juce::ComboBox satTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> satTypeAttachment;
    ArcKnob kTiltLow, kTiltMid, kTiltHigh;
    ArcKnob kLoCutPro, kHiCutPro;

    // ── PRO Tab Knobs & OutEQ Section ──
    ArcKnob kScattering, kERCrossover, kLateDensity;
    ArcKnob kAsymmetry, kClarity, kAirAbsorb;

    juce::Label    loEQTypeLabel;
    juce::ComboBox loEQTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loEQTypeAttachment;
    ArcKnob        kLoGainPro;

    juce::Label    hiEQTypeLabel;
    juce::ComboBox hiEQTypeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> hiEQTypeAttachment;
    ArcKnob        kHiGainPro;

    OutEQVisualizer outEQViz;

    juce::Label    themeLabel;
    juce::ComboBox themeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> themeAttachment;

    void updateTheme(int idx);
    void updateBypassButtonColor();
    juce::File getThemeSettingsFile() const;
    void applySavedTheme();

    // ── Preset UI ──
    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<PresetBrowser> presetBrowser;
    juce::TextButton presetPrevButton;
    juce::ComboBox   presetCombo;
    juce::TextButton presetOverlayButton;
    juce::TextButton presetRevertButton;
    juce::TextButton presetNextButton;
    juce::TextButton presetSaveButton;
    juce::TextButton presetLoadButton;
    juce::TextButton presetDeleteButton;

    juce::String currentBasePresetName;
    bool isPresetModified{ false };
    int loadingPresetCounter{ 0 };

    // ── Send Mode Parameter Cache ──
    float cachedWetDB{ -12.0f };
    float cachedDryDB{ 0.0f };
    bool hasCachedSendMode{ false };

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
