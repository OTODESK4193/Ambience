#pragma once
#include <JuceHeader.h>
#include "../AlgorithmPresets.h"

class FDNReverbAudioProcessor;

// ─── Ambience Design System (10 Dynamic Color Themes) ────────────────
struct AmbienceTheme {
    const char* name{ "Cyber Neon" };
    juce::Colour primary{ 0xFFFF6B00 };      // 主アクセント色
    juce::Colour secondary{ 0xFF38BDF8 };    // 副アクセント色
    juce::Colour background{ 0xFF1A1A1A };   // ウィンドウ背景
    juce::Colour surface{ 0xFF242424 };      // パネル・コンポーネント背景
    juce::Colour panel{ 0xFF2C2C2C };        // 内部パネル
    juce::Colour border{ 0xFF3C3C3C };       // 枠線
    juce::Colour textPrimary{ 0xFFE8E8E8 };   // 主要テキスト
    juce::Colour textSecondary{ 0xFF888888 }; // 補助テキスト
    juce::Colour arcTrack{ 0xFF3A3A3A };     // ノブトラック
    juce::Colour separator{ 0xFF383838 };    // 境界線
};

namespace AmbienceColors {
    inline const AmbienceTheme THEMES[10] = {
        { "Cyber Neon",   juce::Colour(0xFFFF6B00), juce::Colour(0xFF38BDF8), juce::Colour(0xFF1A1A1A), juce::Colour(0xFF242424), juce::Colour(0xFF2C2C2C), juce::Colour(0xFF3C3C3C), juce::Colour(0xFFE8E8E8), juce::Colour(0xFF888888), juce::Colour(0xFF3A3A3A), juce::Colour(0xFF383838) }, // デフォルト (オレンジ / シアン)
        { "Solar Flare",  juce::Colour(0xFFFFB703), juce::Colour(0xFFE63946), juce::Colour(0xFF1E1A16), juce::Colour(0xFF2B241E), juce::Colour(0xFF352D26), juce::Colour(0xFF453B32), juce::Colour(0xFFFAEDCD), juce::Colour(0xFF9A8C7C), juce::Colour(0xFF3D332A), juce::Colour(0xFF42382F) }, // ゴールド / クリムゾン
        { "Matrix Glow",  juce::Colour(0xFF00FF66), juce::Colour(0xFF00E5FF), juce::Colour(0xFF0D1811), juce::Colour(0xFF14241A), juce::Colour(0xFF1B2E21), juce::Colour(0xFF284432), juce::Colour(0xFFE0FFE8), juce::Colour(0xFF6B9975), juce::Colour(0xFF223A2B), juce::Colour(0xFF253E2E) }, // ネオングリーン / エレクトリックシアン
        { "Vaporwave",    juce::Colour(0xFFFF007F), juce::Colour(0xFF00F0FF), juce::Colour(0xFF181124), juce::Colour(0xFF221832), juce::Colour(0xFF2C1E40), juce::Colour(0xFF422E60), juce::Colour(0xFFFFE5F4), juce::Colour(0xFF987CA6), juce::Colour(0xFF36254E), juce::Colour(0xFF3B2956) }, // ネオンピンク / アクア
        { "Dark Amber",   juce::Colour(0xFFF59E0B), juce::Colour(0xFFFDE047), juce::Colour(0xFF1C1814), juce::Colour(0xFF28221B), juce::Colour(0xFF322A21), juce::Colour(0xFF44392D), juce::Colour(0xFFFDF0D5), juce::Colour(0xFF948574), juce::Colour(0xFF3B3226), juce::Colour(0xFF403629) }, // ウォームアンバー / クリーム
        { "Nordic Frost", juce::Colour(0xFF7DD3FC), juce::Colour(0xFFE0F2FE), juce::Colour(0xFF111827), juce::Colour(0xFF182235), juce::Colour(0xFF1F2B42), juce::Colour(0xFF2C3E5E), juce::Colour(0xFFF0F9FF), juce::Colour(0xFF788EA6), juce::Colour(0xFF24334E), juce::Colour(0xFF283857) }, // アイスブルー / フロストホワイト
        { "Deep Purple",  juce::Colour(0xFFA855F7), juce::Colour(0xFFEC4899), juce::Colour(0xFF161224), juce::Colour(0xFF201A34), juce::Colour(0xFF292242), juce::Colour(0xFF3C3161), juce::Colour(0xFFF8EEFF), juce::Colour(0xFF8B77A6), juce::Colour(0xFF312850), juce::Colour(0xFF352B57) }, // バイオレット / マゼンタ
        { "Midnight",     juce::Colour(0xFF3B82F6), juce::Colour(0xFF14B8A6), juce::Colour(0xFF0F172A), juce::Colour(0xFF16213D), juce::Colour(0xFF1C2A4D), juce::Colour(0xFF2A3D70), juce::Colour(0xFFF0F6FF), juce::Colour(0xFF7388A6), juce::Colour(0xFF213057), juce::Colour(0xFF253662) }, // コバルト / ターコイズ
        { "Blood Moon",   juce::Colour(0xFFEF4444), juce::Colour(0xFFFB923C), juce::Colour(0xFF201214), juce::Colour(0xFF2D191C), juce::Colour(0xFF381F23), juce::Colour(0xFF4E2B31), juce::Colour(0xFFFFECEE), juce::Colour(0xFFA0787D), juce::Colour(0xFF3F2327), juce::Colour(0xFF45272B) }, // ルビーレッド / アンバー
        { "Monochrome",   juce::Colour(0xFFF8FAFC), juce::Colour(0xFF94A3B8), juce::Colour(0xFF18181B), juce::Colour(0xFF222226), juce::Colour(0xFF2A2A2E), juce::Colour(0xFF3A3A40), juce::Colour(0xFFF4F4F5), juce::Colour(0xFF8E8E93), juce::Colour(0xFF333338), juce::Colour(0xFF37373C) }  // ピュアホワイト / シルバー
    };

    using ThemeInfo = AmbienceTheme;

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
        const auto& t = THEMES[activeThemeIndex];
        Accent = t.primary;
        AccentBlue = t.secondary;
        ArcFill = t.primary;
        Background = t.background;
        Surface = t.surface;
        Panel = t.panel;
        Border = t.border;
        TextPrimary = t.textPrimary;
        TextSecondary = t.textSecondary;
        ArcTrack = t.arcTrack;
        Separator = t.separator;
    }
}

// ─── Ambience LookAndFeel ────────────────────────────────────────────
class AmbienceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbienceLookAndFeel();
    void setTheme(int idx) noexcept;
    const AmbienceTheme& getTheme() const noexcept { return currentTheme; }
    int getThemeIndex() const noexcept { return currentThemeIndex; }

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
    int currentThemeIndex{ 0 };
    AmbienceTheme currentTheme{ AmbienceColors::THEMES[0] };
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

// ─── OutEQ Visualizer ────────────────────────────────────────────────
class OutEQVisualizer : public juce::Component
{
public:
    OutEQVisualizer();
    void setParams(int loType, float loFreq, float loGain,
                   int hiType, float hiFreq, float hiGain);
    void paint(juce::Graphics&) override;
private:
    int loEQType{ 0 };
    float loFreqHz{ 20.0f };
    float loGainDB{ 0.0f };
    int hiEQType{ 0 };
    float hiFreqHz{ 20000.0f };
    float hiGainDB{ 0.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutEQVisualizer)
};

// ─── VU Meter with K-System & GR Ballistics ─────────────────────────
class VUMeter : public juce::Component
{
public:
    enum class Side { Input, Output };
    VUMeter(const juce::String& label, Side side);
    void paint(juce::Graphics&) override;
    void setLevels(float l, float r) noexcept;
    void setReduction(float grDB) noexcept { reductionDB = grDB; }
private:
    juce::String label;
    Side side;
    float levelL{ 0.f }, levelR{ 0.f };
    float smoothL{ 0.f }, smoothR{ 0.f };
    float peakL{ 0.f }, peakR{ 0.f };
    int peakHoldL{ 0 }, peakHoldR{ 0 };
    float reductionDB{ 0.f };
    float smoothGR{ 0.f };
};

// ─── ArcKnobSlider with Double-Click & Shift-Drag ────────────────────
class ArcKnobSlider : public juce::Slider
{
public:
    ArcKnobSlider()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 14);
        setMouseDragSensitivity(250);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        showTextBox();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isShiftDown())
            setMouseDragSensitivity(1250); // Fine adjust (5x precision)
        else
            setMouseDragSensitivity(250);
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isShiftDown())
            setMouseDragSensitivity(1250);
        else
            setMouseDragSensitivity(250);
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity(250);
        juce::Slider::mouseUp(e);
    }

    std::function<juce::String(double)> textFromValue;
    std::function<double(const juce::String&)> valueFromText;

    juce::String getTextFromValue(double val) override
    {
        if (textFromValue)
            return textFromValue(val);
        return juce::Slider::getTextFromValue(val);
    }

    double getValueFromText(const juce::String& text) override
    {
        if (valueFromText)
            return valueFromText(text);
        return juce::Slider::getValueFromText(text);
    }
};

// ─── Labelled Arc Knob ───────────────────────────────────────────────
struct ArcKnob {
    ArcKnobSlider slider;
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
    std::function<void(int newAlgo, bool shouldResetKnobs)> onUserAlgorithmSelected;
    std::function<void(int)> onAlgorithmChangedCallback;
    void updateButtonColors();
private:
    void parameterChanged(const juce::String&, float) override;
    std::array<juce::TextButton, FDNReverb::NUM_ALGORITHMS> buttons;
    juce::AudioProcessorValueTreeState& apvts;
    int currentAlgo{ 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlgorithmSelector)
};