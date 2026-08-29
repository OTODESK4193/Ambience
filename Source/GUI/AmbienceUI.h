#pragma once
#include <JuceHeader.h>
#include "../AlgorithmPresets.h"

class FDNReverbAudioProcessor;

// ─── Ambience Design System ──────────────────────────────────────────
namespace AmbienceColors {
    const juce::Colour Background{ 0xFF1A1A1A };
    const juce::Colour Surface{ 0xFF242424 };
    const juce::Colour Panel{ 0xFF2C2C2C };
    const juce::Colour Border{ 0xFF3C3C3C };
    const juce::Colour Accent{ 0xFFFF6B00 };
    const juce::Colour AccentBlue{ 0xFF4090FF };
    const juce::Colour TextPrimary{ 0xFFE8E8E8 };
    const juce::Colour TextSecondary{ 0xFF888888 };
    const juce::Colour ArcTrack{ 0xFF3A3A3A };
    const juce::Colour ArcFill{ 0xFFFF6B00 };
    const juce::Colour Separator{ 0xFF383838 };
}

// ─── Ambience LookAndFeel ────────────────────────────────────────────
class AmbienceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbienceLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPos, float startAngle, float endAngle,
        juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
        float sliderPos, float, float,
        juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool isDown,
        int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawGroupComponentOutline(juce::Graphics&, int w, int h,
        const juce::String&, const juce::Justification&,
        juce::GroupComponent&) override;
private:
    juce::Font mainFont{ juce::FontOptions("Helvetica Neue", 11.f, juce::Font::plain) };
};

// ─── RT60 Visualizer ─────────────────────────────────────────────────
class RT60Visualizer : public juce::Component, private juce::Timer
{
public:
    RT60Visualizer();
    ~RT60Visualizer() override;
    void setProcessor(FDNReverbAudioProcessor* p) { processor = p; }
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    FDNReverbAudioProcessor* processor{ nullptr };
    std::array<float, FDNReverb::NUM_BANDS> displayRT60;

    static constexpr float MIN_RT60_DISPLAY = 0.05f;
    static constexpr float MAX_RT60_DISPLAY_FLOOR = 4.0f;  // Y軸の最低上限値

    // ★ 動的 Y 軸上限: effectiveRT60 の最大値に滑らかに追従
    float dynamicMaxRT60{ MAX_RT60_DISPLAY_FLOOR };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RT60Visualizer)
};

// ─── VU Meter ────────────────────────────────────────────────────────
class VUMeter : public juce::Component
{
public:
    enum class Side { Input, Output };
    VUMeter(const juce::String& label, Side side);
    void paint(juce::Graphics&) override;
    void setLevels(float l, float r) noexcept { levelL = l; levelR = r; }
private:
    juce::String label;
    Side side;
    float levelL{ 0.f }, levelR{ 0.f };
};

// ─── Labelled Arc Knob ───────────────────────────────────────────────
struct ArcKnob {
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void build(juce::AudioProcessorValueTreeState& apvts,
        const juce::String& paramID,
        const juce::String& labelText,
        juce::Component* parent,
        AmbienceLookAndFeel& laf);
};

// ─── Algorithm Selector ──────────────────────────────────────────────
class AlgorithmSelector : public juce::Component,
    private juce::AudioProcessorValueTreeState::Listener
{
public:
    AlgorithmSelector(juce::AudioProcessorValueTreeState& apvts);
    ~AlgorithmSelector() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<bool()> isLockedCallback;
private:
    void parameterChanged(const juce::String&, float) override;
    void updateButtonColors();
    std::array<juce::TextButton, FDNReverb::NUM_ALGORITHMS> buttons;
    juce::AudioProcessorValueTreeState& apvts;
    int currentAlgo{ 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlgorithmSelector)
};