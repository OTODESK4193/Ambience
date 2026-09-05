#include "AmbienceUI.h"
#include "../PluginProcessor.h"

// ─── AmbienceLookAndFeel ─────────────────────────────────────────────
AmbienceLookAndFeel::AmbienceLookAndFeel()
{
    setTheme(0);
}

void AmbienceLookAndFeel::setTheme(int idx) noexcept
{
    currentThemeIndex = juce::jlimit(0, 9, idx);
    currentTheme = AmbienceColors::THEMES[currentThemeIndex];
    setColour(juce::Slider::backgroundColourId, currentTheme.arcTrack);
    setColour(juce::Slider::thumbColourId, currentTheme.primary);
    setColour(juce::Slider::trackColourId, currentTheme.primary);
    setColour(juce::Label::textColourId, currentTheme.textSecondary);
    setColour(juce::ComboBox::backgroundColourId, currentTheme.surface);
    setColour(juce::ComboBox::textColourId, currentTheme.textPrimary);
    setColour(juce::ComboBox::outlineColourId, currentTheme.border);
}

void AmbienceLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.f);
    float cx = b.getCentreX(), cy = b.getCentreY();
    float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.44f;
    float th = r * 0.20f;

    // ── 目盛りドット (Subtle Min/Mid/Max Dots) ──
    {
        const float dotR = r + th * 0.9f;
        g.setColour(currentTheme.textSecondary.withAlpha(0.28f));
        for (float a : { startAngle, (startAngle + endAngle) * 0.5f, endAngle }) {
            float dx = cx + dotR * std::sin(a);
            float dy = cy - dotR * std::cos(a);
            g.fillEllipse(dx - 1.2f, dy - 1.2f, 2.4f, 2.4f);
        }
    }

    // ── トラック円弧背景 ──
    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour(currentTheme.arcTrack);
    g.strokePath(track, juce::PathStrokeType(th,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ── 値の円弧 (グラデーション ＆ ネオングロー) ──
    float angle = startAngle + sliderPos * (endAngle - startAngle);
    if (sliderPos > 0.001f) {
        juce::Path fill;
        fill.addCentredArc(cx, cy, r, r, 0.f, startAngle, angle, true);

        // 外側ネオングロー
        g.setColour(currentTheme.primary.withAlpha(0.18f));
        g.strokePath(fill, juce::PathStrokeType(th * 1.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // メイン発光ストローク
        juce::ColourGradient grad(currentTheme.secondary, cx - r, cy + r,
            currentTheme.primary, cx + r, cy - r, false);
        g.setGradientFill(grad);
        g.strokePath(fill, juce::PathStrokeType(th,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── 立体感のあるインナーダイヤル (Metallic / Matte Glass Dial) ──
    const float innerR = r - th * 0.8f;
    if (innerR > 4.f) {
        // ダイヤル外周ドロップシャドウ
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(cx - innerR, cy - innerR + 1.2f, innerR * 2.f, innerR * 2.f);

        // ダイヤル面 (立体グラデーション)
        juce::ColourGradient dialGrad(
            currentTheme.surface.interpolatedWith(juce::Colours::white, 0.05f), cx, cy - innerR,
            currentTheme.panel.interpolatedWith(juce::Colours::black, 0.20f), cx, cy + innerR, false);
        g.setGradientFill(dialGrad);
        g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.f, innerR * 2.f);

        // 微細ハイライト枠線
        g.setColour(currentTheme.border.withAlpha(0.6f));
        g.drawEllipse(cx - innerR, cy - innerR, innerR * 2.f, innerR * 2.f, 0.8f);

        // ── インジケーターポインター (ニードル ＆ 先端ネオンドット) ──
        float pointerLen = innerR * 0.80f;
        float px = cx + pointerLen * std::sin(angle);
        float py = cy - pointerLen * std::cos(angle);

        // ニードル線
        g.setColour(currentTheme.textPrimary.withAlpha(0.85f));
        g.drawLine(cx, cy, px, py, 1.6f);

        // 先端発光ドット
        g.setColour(currentTheme.primary);
        g.fillEllipse(px - 2.0f, py - 2.0f, 4.0f, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(px - 0.9f, py - 0.9f, 1.8f, 1.8f);

        // ダイヤル中央ピボット
        g.setColour(currentTheme.surface.withAlpha(0.8f));
        g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
    }
}

void AmbienceLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    auto b = juce::Rectangle<int>(x, y, w, h).toFloat();
    float ty = b.getCentreY() - 2.f;
    g.setColour(currentTheme.arcTrack);
    g.fillRoundedRectangle(b.getX(), ty, b.getWidth(), 4.f, 2.f);
    g.setColour(currentTheme.primary);
    g.fillRoundedRectangle(b.getX(), ty, sliderPos - b.getX(), 4.f, 2.f);
    float r = 7.f;
    g.setColour(currentTheme.textPrimary);
    g.fillEllipse(sliderPos - r, b.getCentreY() - r, r * 2.f, r * 2.f);
}

void AmbienceLookAndFeel::drawComboBox(juce::Graphics& g,
    int w, int h, bool isDown, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<int>(0, 0, w, h).toFloat();
    g.setColour(isDown ? currentTheme.panel : currentTheme.surface);
    g.fillRoundedRectangle(b, 3.f);
    g.setColour(currentTheme.border);
    g.drawRoundedRectangle(b.reduced(0.5f), 3.f, 1.f);
    juce::Path arrow;
    arrow.addTriangle(w - 16.f, h * 0.5f - 3.f,
        w - 8.f, h * 0.5f - 3.f,
        w - 12.f, h * 0.5f + 3.f);
    g.setColour(currentTheme.textSecondary);
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

    juce::GlyphArrangement ga;
    ga.addLineOfText(mainFont.withHeight(textH), text, 0.f, 0.f);
    float tw = ga.getBoundingBox(0, -1, true).getWidth() + 6.f;

    p.lineTo(indent + 14.f + tw, yOff);
    g.setColour(currentTheme.border);
    g.strokePath(p, juce::PathStrokeType(1.f));
    g.setColour(currentTheme.textSecondary);
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
    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[0];

    auto b = getLocalBounds().toFloat().reduced(2.f);
    float W = b.getWidth(), H = b.getHeight();
    float x0 = b.getX(), y0 = b.getY();

    g.setColour(theme.surface);
    g.fillRoundedRectangle(b, 4.f);
    g.setColour(theme.border);
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
    g.setColour(theme.separator);
    for (float v : kAllGridVals) {
        if (v > dynamicMaxRT60 * 1.05f) break;
        float ny = 1.f - (std::log10(v) - logMin) / (logMax - logMin);
        g.drawHorizontalLine((int)(y0 + ny * H), x0 + 36.f, x0 + W - 4.f);
    }

    // 周波数ラベル (X軸)
    g.setFont(8.5f);
    g.setColour(theme.textSecondary);
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
            theme.textSecondary.withAlpha(0.5f), 1.2f);
    }

    // ユーザー操作による現在の実効カーブ (オレンジ: デフォルト灰色線からの変化を表示)
    plotCurve(displayRT60, theme.primary, 2.f);

    // 右上タイトル
    g.setColour(theme.textSecondary);
    g.setFont(9.f);
    g.drawText("RT60 (s) per band",
        (int)x0 + 36, (int)y0 + 3, (int)W - 40, 12,
        juce::Justification::right);
}

// ─── OutEQVisualizer ─────────────────────────────────────────────────
OutEQVisualizer::OutEQVisualizer() {}

void OutEQVisualizer::setParams(int loType, float loFreq, float loGain,
                               int hiType, float hiFreq, float hiGain)
{
    if (loEQType != loType || loFreqHz != loFreq || loGainDB != loGain ||
        hiEQType != hiType || hiFreqHz != hiFreq || hiGainDB != hiGain) {
        loEQType = loType;
        loFreqHz = loFreq;
        loGainDB = loGain;
        hiEQType = hiType;
        hiFreqHz = hiFreq;
        hiGainDB = hiGain;
        repaint();
    }
}

void OutEQVisualizer::paint(juce::Graphics& g)
{
    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[0];

    auto b = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(theme.surface);
    g.fillRoundedRectangle(b, 4.f);
    g.setColour(theme.border);
    g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 1.f);

    float W = b.getWidth(), H = b.getHeight();
    float x0 = b.getX(), y0 = b.getY();

    // 0dB センターライン
    float midY = y0 + H * 0.5f;
    g.setColour(theme.separator.withAlpha(0.6f));
    g.drawHorizontalLine((int)midY, x0 + 4.f, x0 + W - 4.f);
    // +6dB / -6dB ガイド線
    g.setColour(theme.separator.withAlpha(0.3f));
    g.drawHorizontalLine((int)(midY - H * 0.35f), x0 + 4.f, x0 + W - 4.f);
    g.drawHorizontalLine((int)(midY + H * 0.35f), x0 + 4.f, x0 + W - 4.f);

    // 100Hz, 1kHz, 10kHz グリッド線
    auto freqToX = [&](float f) {
        float norm = (std::log10(std::clamp(f, 20.f, 20000.f)) - std::log10(20.f)) / (std::log10(20000.f) - std::log10(20.f));
        return x0 + norm * W;
    };
    g.drawVerticalLine((int)freqToX(100.f), y0 + 4.f, y0 + H - 4.f);
    g.drawVerticalLine((int)freqToX(1000.f), y0 + 4.f, y0 + H - 4.f);
    g.drawVerticalLine((int)freqToX(10000.f), y0 + 4.f, y0 + H - 4.f);

    // 周波数応答曲線の計算 (20Hz ~ 20kHz, 120ポイント)
    constexpr int numPoints = 120;
    juce::Path curvePath;
    bool first = true;

    for (int i = 0; i < numPoints; ++i) {
        float norm = (float)i / (float)(numPoints - 1);
        float f = 20.f * std::pow(1000.f, norm); // 20Hz ~ 20kHz
        float px = x0 + norm * W;

        float respDB = 0.f;

        // Lo 応答
        if (loEQType == 1) { // Cut (12dB/oct HPF)
            float ratio = loFreqHz / f;
            respDB += -10.f * std::log10(1.f + ratio * ratio * ratio * ratio);
        } else if (loEQType == 2) { // Shelf
            float ratio = f / loFreqHz;
            respDB += loGainDB / (1.f + ratio * ratio);
        }

        // Hi 応答
        if (hiEQType == 1) { // Cut (12dB/oct LPF)
            float ratio = f / hiFreqHz;
            respDB += -10.f * std::log10(1.f + ratio * ratio * ratio * ratio);
        } else if (hiEQType == 2) { // Shelf
            float ratio = hiFreqHz / f;
            respDB += hiGainDB / (1.f + ratio * ratio);
        }

        // dB を Y座標にマップ (±18dB レンジ)
        float clampedDB = std::clamp(respDB, -18.f, 18.f);
        float py = midY - (clampedDB / 18.f) * (H * 0.45f);

        if (first) {
            curvePath.startNewSubPath(px, py);
            first = false;
        } else {
            curvePath.lineTo(px, py);
        }
    }

    g.setColour(theme.primary);
    g.strokePath(curvePath, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 右上ラベル
    g.setColour(theme.textSecondary);
    g.setFont(8.f);
    g.drawText("OutEQ Response", (int)(x0 + 4.f), (int)(y0 + 2.f), (int)W - 8, 12, juce::Justification::topRight);
}

// ─── VUMeter ─────────────────────────────────────────────────────────
VUMeter::VUMeter(const juce::String& lbl, Side s) : label(lbl), side(s) {}

void VUMeter::setLevels(float l, float r) noexcept
{
    levelL = l; levelR = r;

    // バリスティクス: アタックは瞬時、リリースは滑らかな減衰 (~300ms)
    smoothL = (levelL > smoothL) ? levelL : smoothL * 0.82f + levelL * 0.18f;
    smoothR = (levelR > smoothR) ? levelR : smoothR * 0.82f + levelR * 0.18f;

    // ピークホールド (約 1.2 秒ホールド後にゆっくりフォールオフ)
    if (levelL >= peakL) {
        peakL = levelL;
        peakHoldL = 36;
    } else if (peakHoldL > 0) {
        --peakHoldL;
    } else {
        peakL *= 0.94f;
    }

    if (levelR >= peakR) {
        peakR = levelR;
        peakHoldR = 36;
    } else if (peakHoldR > 0) {
        --peakHoldR;
    } else {
        peakR *= 0.94f;
    }

    smoothGR = smoothGR * 0.85f + reductionDB * 0.15f;
}

void VUMeter::paint(juce::Graphics& g)
{
    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[0];

    auto b = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(theme.surface);
    g.fillRoundedRectangle(b, 3.f);

    float bx = b.getX() + 22.f, bw = b.getWidth() - 24.f;

    // K-14 スケール: -60dB ~ +4dBFS (-14dBFS が 0VU / 72% の位置)
    constexpr float minDB = -60.f;
    constexpr float maxDB = 0.f;
    constexpr float k14DB = -14.f;
    const float k14Norm = (k14DB - minDB) / (maxDB - minDB);

    auto drawSingleBar = [&](float y, float level, float peak) {
        float db = juce::Decibels::gainToDecibels(level + 1e-9f);
        float n = juce::jlimit(0.f, 1.f, juce::jmap(db, minDB, maxDB, 0.f, 1.f));

        float peakDB = juce::Decibels::gainToDecibels(peak + 1e-9f);
        float peakN = juce::jlimit(0.f, 1.f, juce::jmap(peakDB, minDB, maxDB, 0.f, 1.f));

        // トラック
        g.setColour(theme.arcTrack.withAlpha(0.6f));
        g.fillRoundedRectangle(bx, y, bw, 6.f, 1.5f);

        // K-System ゾーン別カラーグラデーション
        juce::Colour zoneCol = (db > -2.f) ? juce::Colour(0xFFEF4444) // クリップ警告
                             : (db > k14DB) ? theme.primary          // K-14 ヘッドルーム
                             : theme.secondary;                     // 安全ゾーン

        juce::ColourGradient gr(theme.secondary, bx, y,
                                zoneCol, bx + bw * n, y, false);
        g.setGradientFill(gr);
        g.fillRoundedRectangle(bx, y, bw * n, 6.f, 1.5f);

        // ピークホールドインジケーター線
        if (peakN > 0.02f) {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            float px = bx + bw * peakN;
            g.drawVerticalLine((int)px, y, y + 6.f);
        }

        // K-14 (0VU) リファレンスマーカー線
        float refX = bx + bw * k14Norm;
        g.setColour(theme.separator.withAlpha(0.7f));
        g.drawVerticalLine((int)refX, y - 1.f, y + 7.f);
    };

    drawSingleBar(b.getY() + 2.f, smoothL, peakL);
    drawSingleBar(b.getY() + 10.f, smoothR, peakR);

    // ラベル
    g.setColour(theme.textSecondary);
    g.setFont(8.f);
    g.drawText(label, (int)b.getX(), (int)b.getY(), 20, (int)b.getHeight(),
        juce::Justification::centredLeft);

    // Output 側の Gain Reduction (GR) 表示 (ダッキング動作時)
    if (side == Side::Output && smoothGR > 0.2f) {
        juce::String grText = "GR -" + juce::String(smoothGR, 1) + "dB";
        g.setFont(7.5f);
        g.setColour(juce::Colour(0xFFFF9500)); // オレンジ
        g.drawText(grText, (int)(bx + bw - 60.f), (int)b.getY(), 60, (int)b.getHeight(),
            juce::Justification::centredRight);
    }
}

// ─── ArcKnob ─────────────────────────────────────────────────────────
void ArcKnob::build(juce::AudioProcessorValueTreeState& apvts,
    const juce::String& paramID,
    const juce::String& labelText,
    juce::Component* parent,
    AmbienceLookAndFeel& laf)
{
    slider.setLookAndFeel(&laf);
    slider.setColour(juce::Slider::textBoxTextColourId,
        AmbienceColors::TextSecondary);
    slider.setColour(juce::Slider::textBoxOutlineColourId,
        juce::Colours::transparentBlack);

    if (auto* p = apvts.getParameter(paramID)) {
        float normDef = p->getDefaultValue();
        float realDef = apvts.getParameterRange(paramID).convertFrom0to1(normDef);
        slider.setDefaultValue(realDef);

        const juce::String unit = p->label;
        if (unit == "%") {
            slider.textFromValue = [](double val) {
                return juce::String(juce::roundToInt(val * 100.0)) + " %";
            };
            slider.valueFromText = [](const juce::String& text) {
                juce::String t = text.upToFirstOccurrenceOf("%", false, false).trim();
                return t.getDoubleValue() / 100.0;
            };
        } else if (unit == "Hz") {
            slider.textFromValue = [](double val) {
                if (val >= 1000.0)
                    return juce::String(val / 1000.0, 1) + " kHz";
                return juce::String(juce::roundToInt(val)) + " Hz";
            };
            slider.valueFromText = [](const juce::String& text) {
                juce::String t = text.trim().toLowerCase();
                if (t.endsWith("khz") || t.endsWith("k")) {
                    t = t.upToFirstOccurrenceOf("k", false, false).trim();
                    return t.getDoubleValue() * 1000.0;
                }
                t = t.upToFirstOccurrenceOf("hz", false, false).trim();
                return t.getDoubleValue();
            };
        } else if (unit == "ms") {
            slider.textFromValue = [](double val) {
                return juce::String(val, (val < 10.0f ? 1 : 0)) + " ms";
            };
            slider.valueFromText = [](const juce::String& text) {
                return text.upToFirstOccurrenceOf("ms", false, false).trim().getDoubleValue();
            };
        } else if (unit == "s") {
            slider.textFromValue = [](double val) {
                return juce::String(val, 2) + " s";
            };
            slider.valueFromText = [](const juce::String& text) {
                return text.upToFirstOccurrenceOf("s", false, false).trim().getDoubleValue();
            };
        } else if (unit == "dB") {
            slider.textFromValue = [](double val) {
                return juce::String(val, 1) + " dB";
            };
            slider.valueFromText = [](const juce::String& text) {
                return text.upToFirstOccurrenceOf("db", false, false).trim().getDoubleValue();
            };
        } else if (unit == "x") {
            slider.textFromValue = [](double val) {
                return juce::String(val, 2) + " x";
            };
            slider.valueFromText = [](const juce::String& text) {
                return text.upToFirstOccurrenceOf("x", false, false).trim().getDoubleValue();
            };
        } else if (unit.isNotEmpty()) {
            slider.setTextValueSuffix(" " + unit);
        }
    }

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
    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[0];

    for (int i = 0; i < FDNReverb::NUM_ALGORITHMS; ++i) {
        bool on = (i == currentAlgo);
        buttons[i].setColour(juce::TextButton::buttonColourId,
            on ? theme.primary : theme.surface);
        buttons[i].setColour(juce::TextButton::textColourOffId,
            on ? theme.background : theme.textSecondary);
        buttons[i].repaint();
    }
}

void AlgorithmSelector::paint(juce::Graphics& g) {
    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[0];

    g.setColour(theme.surface);
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