#pragma once
#include <JuceHeader.h>
#include "../AlgorithmPresets.h"

class FDNReverbAudioProcessor;

// ─── Ambience Design System (10 Dynamic Color Themes) ────────────────
namespace AmbienceColors {
    struct ThemeInfo {
        const char* name;
        juce::Colour primary;    // 主アクセント色
        juce::Colour secondary;  // 副アクセント色
    };

    inline const ThemeInfo THEMES[10] = {
        { "Cyber Neon",   juce::Colour(0xFFFF6B00), juce::Colour(0xFF38BDF8) }, // デフォルト (オレンジ / シアン)
        { "Solar Flare",  juce::Colour(0xFFFFB703), juce::Colour(0xFFE63946) }, // ゴールド / クリムゾン
        { "Matrix Glow",  juce::Colour(0xFF10B981), juce::Colour(0xFF06D6A0) }, // ライム / エメラルド
        { "Vaporwave",    juce::Colour(0xFFFF007F), juce::Colour(0xFF00F0FF) }, // ネオンピンク / アクア
        { "Dark Amber",   juce::Colour(0xFFF59E0B), juce::Colour(0xFFFDE047) }, // ウォームアンバー / クリーム
        { "Nordic Frost", juce::Colour(0xFF7DD3FC), juce::Colour(0xFFE0F2FE) }, // アイスブルー / フロストホワイト
        { "Deep Purple",  juce::Colour(0xFFA855F7), juce::Colour(0xFFEC4899) }, // バイオレット / マゼンタ
        { "Midnight",     juce::Colour(0xFF3B82F6), juce::Colour(0xFF14B8A6) }, // コバルト / ターコイズ
        { "Blood Moon",   juce::Colour(0xFFEF4444), juce::Colour(0xFFFB923C) }, // ルビーレッド / アンバー
        { "Monochrome",   juce::Colour(0xFFF8FAFC), juce::Colour(0xFF94A3B8) }  // ピュアホワイト / シルバー
    };

    inline int activeThemeIndex = 0;
    inline juce::Colour Background{ 0xFF1A1A1A };
    inline juce::Colour Surface{ 0xFF242424 };
    inline juce::Colour Panel{ 0xFF2C2C2C };
    inline juce::Colour Border{ 0xFF3C3C3C };
    inline juce::Colour Accent{ 0xFFFF6B00 };
    inline juce::Colour AccentBlue{ 0xFF38BDF8 };
    inline juce::Colour ArcFill{ 0xFFFF6B00 };
    inline juce::Colour TextPrimary{ 0xFFE8E8E8 };
    inline juce::Colour TextSecondary{ 0xFF888888 };
    inline juce::Colour ArcTrack{ 0xFF3A3A3A };
    inline juce::Colour Separator{ 0xFF383838 };

    inline void setTheme(int idx) noexcept {
        activeThemeIndex = juce::jlimit(0, 9, idx);
        Accent = THEMES[activeThemeIndex].primary;
        AccentBlue = THEMES[activeThemeIndex].secondary;
        ArcFill = THEMES[activeThemeIndex].primary;
    }
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
    std::function<void(int)> onAlgorithmChangedCallback;
    void updateButtonColors();
private:
    void parameterChanged(const juce::String&, float) override;
    std::array<juce::TextButton, FDNReverb::NUM_ALGORITHMS> buttons;
    juce::AudioProcessorValueTreeState& apvts;
    int currentAlgo{ 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlgorithmSelector)
};