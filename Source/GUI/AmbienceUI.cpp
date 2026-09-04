#include "AmbienceUI.h"
#include "../PluginProcessor.h"

// ─── AmbienceLookAndFeel ─────────────────────────────────────────────
AmbienceLookAndFeel::AmbienceLookAndFeel()
{
    setColour(juce::Slider::backgroundColourId, AmbienceColors::ArcTrack);
    setColour(juce::Slider::thumbColourId, AmbienceColors::Accent);
    setColour(juce::Slider::trackColourId, AmbienceColors::ArcFill);
    setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    setColour(juce::ComboBox::backgroundColourId, AmbienceColors::Surface);
    setColour(juce::ComboBox::textColourId, AmbienceColors::TextPrimary);
    setColour(juce::ComboBox::outlineColourId, AmbienceColors::Border);
}

void AmbienceLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4.f);
    float cx = b.getCentreX(), cy = b.getCentreY();
    float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.45f;
    float th = r * 0.22f;

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour(AmbienceColors::ArcTrack);
    g.strokePath(track, juce::PathStrokeType(th,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path fill;
    fill.addCentredArc(cx, cy, r, r, 0.f, startAngle, angle, true);
    juce::ColourGradient grad(AmbienceColors::AccentBlue, cx - r, cy,
        AmbienceColors::Accent, cx + r, cy, false);
    g.setGradientFill(grad);
    g.strokePath(fill, juce::PathStrokeType(th,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(AmbienceColors::Panel);
    g.fillEllipse(cx - r * 0.28f, cy - r * 0.28f, r * 0.56f, r * 0.56f);

    float ix = cx + r * 0.6f * std::sin(angle);
    float iy = cy - r * 0.6f * std::cos(angle);
    g.setColour(AmbienceColors::TextPrimary);
    g.drawLine(cx, cy, ix, iy, 2.f);
}

void AmbienceLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    auto b = juce::Rectangle<int>(x, y, w, h).toFloat();
    float ty = b.getCentreY() - 2.f;
    g.setColour(AmbienceColors::ArcTrack);
    g.fillRoundedRectangle(b.getX(), ty, b.getWidth(), 4.f, 2.f);
    g.setColour(AmbienceColors::Accent);
    g.fillRoundedRectangle(b.getX(), ty, sliderPos - b.getX(), 4.f, 2.f);
    float r = 7.f;
    g.setColour(AmbienceColors::TextPrimary);
    g.fillEllipse(sliderPos - r, b.getCentreY() - r, r * 2.f, r * 2.f);
}

void AmbienceLookAndFeel::drawComboBox(juce::Graphics& g,
    int w, int h, bool isDown, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<int>(0, 0, w, h).toFloat();
    g.setColour(isDown ? AmbienceColors::Panel : AmbienceColors::Surface);
    g.fillRoundedRectangle(b, 3.f);
    g.setColour(AmbienceColors::Border);
    g.drawRoundedRectangle(b.reduced(0.5f), 3.f, 1.f);
    juce::Path arrow;
    arrow.addTriangle(w - 16.f, h * 0.5f - 3.f,
        w - 8.f, h * 0.5f - 3.f,
        w - 12.f, h * 0.5f + 3.f);
    g.setColour(AmbienceColors::TextSecondary);
    g.fillPath(arrow);
}

void AmbienceLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font AmbienceLookAndFeel::getLabelFont(juce::Label&) { return mainFont.withHeight(10.f); }
juce::Font AmbienceLookAndFeel::getComboBoxFont(juce::ComboBox&) { return mainFont.withHeight(11.f); }

void AmbienceLookAndFeel::drawGroupComponentOutline(juce::Graphics& g,
    int w, int h, const juce::String& text,
    const juce::Justification&, juce::GroupComponent&)
{
    float textH = 12.f, indent = 8.f, yOff = textH * 0.5f;
    juce::Path p;
    p.startNewSubPath(indent + 4.f, yOff); p.lineTo(indent, yOff);
    p.lineTo(indent, (float)h - 1.f);
    p.lineTo((float)w - indent, (float)h - 1.f);
    p.lineTo((float)w - indent, yOff);

    // ✅ 変更後（警告なし）
    juce::GlyphArrangement ga;
    ga.addLineOfText(mainFont.withHeight(textH), text, 0.f, 0.f);
    float tw = ga.getBoundingBox(0, -1, true).getWidth() + 6.f;

    p.lineTo(indent + 14.f + tw, yOff);
    g.setColour(AmbienceColors::Border);
    g.strokePath(p, juce::PathStrokeType(1.f));
    g.setColour(AmbienceColors::TextSecondary);
    g.setFont(mainFont.withHeight(textH).boldened());
    g.drawText(text, (int)(indent + 14.f), 0, (int)tw, (int)textH,
        juce::Justification::centredLeft);
}

// ─── RT60Visualizer ──────────────────────────────────────────────────
RT60Visualizer::RT60Visualizer() {
    displayRT60.fill(1.0f);
    startTimerHz(30);
}
RT60Visualizer::~RT60Visualizer() { stopTimer(); }

void RT60Visualizer::timerCallback() {
    if (!processor) return;

    auto live = processor->getRT60ForDisplay();
    for (int i = 0; i < FDNReverb::NUM_BANDS; ++i)
        displayRT60[i] += 0.25f * (live[i] - displayRT60[i]);

    // ★ 動的 Y 軸上限: 現在の実効値とデフォルト基準値の最大値 × 1.3 に滑らかに追従
    float maxVal = *std::max_element(displayRT60.begin(), displayRT60.end());
    int algo = (int)*processor->apvts.getRawParameterValue("algorithm");
    auto& preset = *FDNReverb::ALL_PRESETS[juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, algo)];
    float presetMax = *std::max_element(preset.acoustics.rt60.begin(), preset.acoustics.rt60.end());
    maxVal = std::max(maxVal, presetMax);

    float targetMax = std::max(MAX_RT60_DISPLAY_FLOOR, maxVal * 1.3f);
    // 指数平滑化（上昇は素早く、下降は緩やか→スケールが頻繁に変わらない）
    float smoothFactor = (targetMax > dynamicMaxRT60) ? 0.15f : 0.03f;
    dynamicMaxRT60 += smoothFactor * (targetMax - dynamicMaxRT60);

    repaint();
}

void RT60Visualizer::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(2.f);
    float W = b.getWidth(), H = b.getHeight();
    float x0 = b.getX(), y0 = b.getY();

    g.setColour(AmbienceColors::Surface);
    g.fillRoundedRectangle(b, 4.f);
    g.setColour(AmbienceColors::Border);
    g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.f);

    // ★ 動的 Y 軸スケール
    float logMin = std::log10(MIN_RT60_DISPLAY);
    float logMax = std::log10(dynamicMaxRT60);

    // グリッド値を動的 Y 軸に合わせて生成
    // 固定候補値から dynamicMaxRT60 以下のものだけを描画する
    static constexpr float kAllGridVals[] = {
        0.1f, 0.3f, 0.5f, 1.0f, 2.0f, 4.0f,
        8.0f, 12.0f, 16.0f, 20.0f
    };

    // グリッド線
    g.setColour(AmbienceColors::Separator);
    for (float v : kAllGridVals) {
        if (v > dynamicMaxRT60 * 1.05f) break;
        float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
        g.drawHorizontalLine((int)(y0 + ny * H), x0 + 36.f, x0 + W - 4.f);
    }

    // 周波数ラベル (X軸)
    g.setFont(8.5f);
    g.setColour(AmbienceColors::TextSecondary);
    static const char* fLbls[] = {
        "31","63","125","250","500","1k","2k","4k","8k","16k"
    };
    for (int i = 0; i < FDNReverb::NUM_BANDS; ++i) {
        float px = x0 + 36.f + (float)i / (FDNReverb::NUM_BANDS - 1) * (W - 40.f);
        g.drawText(fLbls[i], (int)(px - 12.f), (int)(y0 + H - 14.f),
            24, 13, juce::Justification::centred);
    }

    // 秒数ラベル (Y軸) - 動的
    for (float v : kAllGridVals) {
        if (v > dynamicMaxRT60 * 1.05f) break;
        float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
        float py = y0 + ny * H;
        juce::String lbl = (v < 1.f)
            ? juce::String(v, 1) + "s"
            : (v < 10.f ? juce::String(v, 1) : juce::String((int)v)) + "s";
        g.drawText(lbl, (int)(x0 + 2.f), (int)(py - 7.f), 32, 14,
            juce::Justification::centredLeft);
    }

    auto plotCurve = [&](const std::array<float, FDNReverb::NUM_BANDS>& rt60,
        juce::Colour col, float thick)
        {
            juce::Path path;
            bool first = true;
            for (int i = 0; i < FDNReverb::NUM_BANDS; ++i) {
                float v = std::clamp(rt60[i], MIN_RT60_DISPLAY, dynamicMaxRT60);
                float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
                float px = x0 + 36.f + (float)i / (FDNReverb::NUM_BANDS - 1) * (W - 40.f);
                float py = y0 + ny * H;
                if (first) { path.startNewSubPath(px, py); first = false; }
                else        path.lineTo(px, py);
            }
            g.setColour(col);
            g.strokePath(path, juce::PathStrokeType(thick,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            for (int i = 0; i < FDNReverb::NUM_BANDS; ++i) {
                float v = std::clamp(rt60[i], MIN_RT60_DISPLAY, dynamicMaxRT60);
                float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
                float px = x0 + 36.f + (float)i / (FDNReverb::NUM_BANDS - 1) * (W - 40.f);
                float py = y0 + ny * H;
                g.fillEllipse(px - 3.f, py - 3.f, 6.f, 6.f);
            }
        };

    // プリセット・デフォルト基準カーブ (半透明グレー: RoomType 固有の固定リファレンス)
    if (processor) {
        int algo = (int)*processor->apvts.getRawParameterValue("algorithm");
        auto& preset = *FDNReverb::ALL_PRESETS[
            juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, algo)];
        plotCurve(preset.acoustics.rt60,
            AmbienceColors::TextSecondary.withAlpha(0.5f), 1.2f);
    }

    // ユーザー操作による現在の実効カーブ (オレンジ: デフォルト灰色線からの変化を表示)
    plotCurve(displayRT60, AmbienceColors::Accent, 2.f);

    // 右上タイトル
    g.setColour(AmbienceColors::TextSecondary);
    g.setFont(9.f);
    g.drawText("RT60 (s) per band",
        (int)x0 + 36, (int)y0 + 3, (int)W - 40, 12,
        juce::Justification::right);
}

// ─── VUMeter ─────────────────────────────────────────────────────────
VUMeter::VUMeter(const juce::String& lbl, Side s) : label(lbl), side(s) {}

void VUMeter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(AmbienceColors::Surface);
    g.fillRoundedRectangle(b, 3.f);

    float bx = b.getX() + 22.f, bw = b.getWidth() - 22.f;
    auto bar = [&](float y, float level) {
        float n = juce::jlimit(0.f, 1.f, juce::jmap(
            juce::Decibels::gainToDecibels(level + 1e-9f), -60.f, 0.f, 0.f, 1.f));
        g.setColour(AmbienceColors::ArcTrack);
        g.fillRoundedRectangle(bx, y, bw, 7.f, 2.f);
        juce::ColourGradient gr(AmbienceColors::AccentBlue, bx, y,
            AmbienceColors::Accent, bx + bw, y, false);
        g.setGradientFill(gr);
        g.fillRoundedRectangle(bx, y, bw * n, 7.f, 2.f);
        };
    bar(b.getY() + 2.f, levelL);
    bar(b.getY() + 11.f, levelR);

    g.setColour(AmbienceColors::TextSecondary);
    g.setFont(8.f);
    g.drawText(label, (int)b.getX(), (int)b.getY(), 20, (int)b.getHeight(),
        juce::Justification::centredLeft);
}

// ─── ArcKnob ─────────────────────────────────────────────────────────
void ArcKnob::build(juce::AudioProcessorValueTreeState& apvts,
    const juce::String& paramID,
    const juce::String& labelText,
    juce::Component* parent,
    AmbienceLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 14);
    slider.setLookAndFeel(&laf);
    slider.setColour(juce::Slider::textBoxTextColourId,
        AmbienceColors::TextSecondary);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
        juce::Colours::transparentBlack);
    parent->addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(9.f)));
    label.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    parent->addAndMakeVisible(label);

    // ✅ 変更後
    attachment.reset(
        new juce::AudioProcessorValueTreeState::SliderAttachment(
            apvts, paramID, slider));
}

// ─── AlgorithmSelector ───────────────────────────────────────────────
AlgorithmSelector::AlgorithmSelector(juce::AudioProcessorValueTreeState& a)
    : apvts(a)
{
    static const char* names[] = {
        "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL","INCHINDOWN"
    };
    for (int i = 0; i < FDNReverb::NUM_ALGORITHMS; ++i) {
        buttons[i].setButtonText(names[i]);
        addAndMakeVisible(buttons[i]);
        int idx = i;
        buttons[i].onClick = [this, idx] {
            if (idx == currentAlgo) return;

            bool isLocked = isLockedCallback ? isLockedCallback() : false;
            if (isLocked) {
                if (onUserAlgorithmSelected)
                    onUserAlgorithmSelected(idx, false);
                if (auto* param = apvts.getParameter("algorithm"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
            } else {
                auto* dialog = new juce::AlertWindow(
                    "Reset Knob Parameters?",
                    "Switching Room Type with LOCK OFF will reset all knobs to preset defaults.\nDo you want to continue?",
                    juce::MessageBoxIconType::QuestionIcon);
                dialog->addButton("Yes (Reset Knobs)", 1, juce::KeyPress(juce::KeyPress::returnKey));
                dialog->addButton("No (Cancel)", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                juce::Component::SafePointer<AlgorithmSelector> safeThis(this);
                dialog->enterModalState(
                    true,
                    juce::ModalCallbackFunction::create([safeThis, idx](int result) {
                        if (safeThis != nullptr && result == 1) {
                            if (safeThis->onUserAlgorithmSelected)
                                safeThis->onUserAlgorithmSelected(idx, true);
                            if (auto* param = safeThis->apvts.getParameter("algorithm"))
                                param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
                        }
                    }),
                    true
                );
            }
        };
    }
    apvts.addParameterListener("algorithm", this);
    if (auto* rawAlgo = apvts.getRawParameterValue("algorithm")) {
        currentAlgo = juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, juce::roundToInt(rawAlgo->load()));
    }
    updateButtonColors();
}

AlgorithmSelector::~AlgorithmSelector() {
    apvts.removeParameterListener("algorithm", this);
}

void AlgorithmSelector::parameterChanged(const juce::String&, float newVal) {
    int newAlgo = juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1,
        juce::roundToInt(newVal));
    juce::Component::SafePointer<AlgorithmSelector> safeThis(this);
    juce::MessageManager::callAsync([safeThis, newAlgo] {
        if (safeThis != nullptr) {
            safeThis->currentAlgo = newAlgo;
            safeThis->updateButtonColors();
            if (safeThis->onAlgorithmChangedCallback)
                safeThis->onAlgorithmChangedCallback(newAlgo);
        }
    });
}

void AlgorithmSelector::updateButtonColors() {
    for (int i = 0; i < FDNReverb::NUM_ALGORITHMS; ++i) {
        bool on = (i == currentAlgo);
        buttons[i].setColour(juce::TextButton::buttonColourId,
            on ? AmbienceColors::Accent : AmbienceColors::Surface);
        buttons[i].setColour(juce::TextButton::textColourOffId,
            on ? AmbienceColors::Background : AmbienceColors::TextSecondary);
        buttons[i].repaint();
    }
}

void AlgorithmSelector::paint(juce::Graphics& g) {
    g.setColour(AmbienceColors::Surface);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.f);
}

void AlgorithmSelector::resized() {
    auto area = getLocalBounds().reduced(2);
    int btnW = area.getWidth() / FDNReverb::NUM_ALGORITHMS;
    for (int i = 0; i < FDNReverb::NUM_ALGORITHMS; ++i)
        buttons[i].setBounds(area.getX() + i * btnW, area.getY(),
            btnW - 1, area.getHeight());
    updateButtonColors();
}