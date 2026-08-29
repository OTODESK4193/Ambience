#include "DecayCurveViz.h"

DecayCurveViz::DecayCurveViz() {
    cachedERDelayMs.fill(0.0f);
    cachedERGains.fill(0.0f);
    startTimerHz(30);
}

DecayCurveViz::~DecayCurveViz() {
    stopTimer();
}

void DecayCurveViz::timerCallback() {
    if (processor != nullptr) {
        const auto& engine = processor->getEngine();
        auto rt60 = engine.getEffectiveRT60();
        cachedRT60Mid = std::max(0.1f, rt60[4]);

        cachedERBypassed = engine.isERBypassed();
        cachedERTapCount = engine.getERTapCount();
        if (cachedERTapCount > MAX_DISPLAY_TAPS)
            cachedERTapCount = MAX_DISPLAY_TAPS;

        double sr = engine.getSampleRate();
        if (sr < 1.0) sr = 48000.0;

        for (int i = 0; i < cachedERTapCount; ++i) {
            float delaySamples = engine.getERTapDelaySamples(i);
            cachedERDelayMs[i] = delaySamples / static_cast<float>(sr) * 1000.0f;
            cachedERGains[i] = engine.getERTapGain(i);
        }
    }
    repaint();
}

void DecayCurveViz::resized() {}

void DecayCurveViz::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 10.0f || bounds.getHeight() < 10.0f) return;

    g.fillAll(AmbienceColors::Background);

    const float topMargin = 4.0f;
    const float bottomMargin = 14.0f;
    const float leftMargin = 30.0f;
    const float rightMargin = 8.0f;
    const float plotX = bounds.getX() + leftMargin;
    const float plotY = bounds.getY() + topMargin;
    const float plotW = bounds.getWidth() - leftMargin - rightMargin;
    const float plotH = bounds.getHeight() - topMargin - bottomMargin;

    const float maxTimeSec = juce::jlimit(0.5f, 120.0f, cachedRT60Mid * 1.25f);
    const float minDB = -60.0f;
    const float maxDB = 0.0f;

    auto timeToX = [&](float timeSec) -> float {
        if (timeSec <= splitSec) {
            const float ratio = timeSec / splitSec;
            return plotX + ratio * plotW * splitRatio;
        } else {
            const float lateRange = maxTimeSec - splitSec;
            if (lateRange <= 0.0f) return plotX + plotW;
            const float ratio = (timeSec - splitSec) / lateRange;
            return plotX + plotW * splitRatio + ratio * plotW * (1.0f - splitRatio);
        }
    };

    auto dbToY = [&](float db) -> float {
        const float normalized = (db - minDB) / (maxDB - minDB);
        return plotY + (1.0f - normalized) * plotH;
    };

    // ─── ER ゾーン背景 ───
    const float erZoneW = plotW * splitRatio;
    juce::ColourGradient erZoneGrad(
        juce::Colour(0xFF0F1B2A), plotX, plotY,
        juce::Colour(0xFF09101A), plotX + erZoneW, plotY, false);
    g.setGradientFill(erZoneGrad);
    g.fillRect(plotX, plotY, erZoneW, plotH);

    // ─── グリッド線 ───
    g.setColour(AmbienceColors::Separator.withAlpha(0.25f));
    for (float db = 0.0f; db >= -60.0f; db -= 20.0f)
        g.drawHorizontalLine((int)dbToY(db), plotX, plotX + plotW);

    // ER 垂直グリッド (ms)
    {
        static const float erGridMs[] = { 20.0f, 50.0f, 100.0f, 150.0f, 200.0f };
        g.setColour(AmbienceColors::Separator.withAlpha(0.35f));
        for (float ms : erGridMs) {
            const float t = ms * 0.001f;
            if (t >= maxTimeSec) break;
            g.drawVerticalLine((int)timeToX(t), plotY, plotY + plotH);
        }
    }

    // Late 垂直グリッド (s)
    float timeStep;
    if (maxTimeSec <= 2.0f)       timeStep = 0.5f;
    else if (maxTimeSec <= 5.0f)  timeStep = 1.0f;
    else if (maxTimeSec <= 15.0f) timeStep = 3.0f;
    else if (maxTimeSec <= 45.0f) timeStep = 10.0f;
    else                          timeStep = 25.0f;

    g.setColour(AmbienceColors::Separator.withAlpha(0.20f));
    for (float t = splitSec + timeStep; t <= maxTimeSec; t += timeStep)
        g.drawVerticalLine((int)timeToX(t), plotY, plotY + plotH);

    // スプリット境界線
    const float splitX = timeToX(splitSec);
    g.setColour(juce::Colour(0xFF00E5FF).withAlpha(0.20f));
    g.drawVerticalLine((int)splitX, plotY, plotY + plotH);

    // ─── 軸ラベル ───
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::plain)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.55f));

    for (float db = 0.0f; db >= -60.0f; db -= 20.0f) {
        const float y = dbToY(db);
        g.drawText(juce::String((int)db) + "dB",
            (int)(plotX - leftMargin + 2), (int)(y - 6),
            (int)(leftMargin - 4), 12, juce::Justification::centredRight);
    }

    for (float ms : { 20.0f, 50.0f, 100.0f, 150.0f, 200.0f }) {
        const float t = ms * 0.001f;
        if (t >= maxTimeSec) break;
        g.drawText(juce::String((int)ms) + "ms",
            (int)(timeToX(t) - 18), (int)(plotY + plotH + 2),
            36, 11, juce::Justification::centred);
    }
    for (float t = splitSec + timeStep; t <= maxTimeSec; t += timeStep) {
        g.drawText(juce::String(t, 1) + "s",
            (int)(timeToX(t) - 18), (int)(plotY + plotH + 2),
            36, 11, juce::Justification::centred);
    }

    // ─── LATE REVERB 流麗なネオン指数減衰カーブ ───
    {
        juce::Path lateCurve;
        juce::Path lateFill;
        const int numPts = 100;
        const float yBottom = plotY + plotH;

        lateCurve.startNewSubPath(plotX, dbToY(-2.0f));
        lateFill.startNewSubPath(plotX, yBottom);
        lateFill.lineTo(plotX, dbToY(-2.0f));

        for (int i = 1; i <= numPts; ++i) {
            float ratio = (float)i / (float)numPts;
            float t = ratio * maxTimeSec;
            float normTime = t / cachedRT60Mid;
            float db = -60.0f * normTime;
            db = juce::jlimit(minDB, maxDB, db);

            float x = timeToX(t);
            float y = dbToY(db);
            lateCurve.lineTo(x, y);
            lateFill.lineTo(x, y);
        }

        lateFill.lineTo(plotX + plotW, yBottom);
        lateFill.closeSubPath();

        juce::ColourGradient fillGrad(
            AmbienceColors::Accent.withAlpha(0.20f), plotX, plotY,
            juce::Colour(0xFF100B1A).withAlpha(0.04f), plotX, yBottom, false);
        g.setGradientFill(fillGrad);
        g.fillPath(lateFill);

        g.setColour(AmbienceColors::Accent.withAlpha(0.25f));
        g.strokePath(lateCurve, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved));

        g.setColour(AmbienceColors::Accent.withAlpha(0.90f));
        g.strokePath(lateCurve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }

    // ─── ER レーザーピン ＆ 発光ネオンオーブ ───
    if (!cachedERBypassed && cachedERTapCount > 0) {
        const juce::Colour neonCyan(0xFF00E5FF);

        for (int t = 0; t < cachedERTapCount; ++t) {
            const float timeSec = cachedERDelayMs[t] * 0.001f;
            if (timeSec > maxTimeSec) continue;

            float gainDB = (cachedERGains[t] > 1e-6f)
                ? juce::Decibels::gainToDecibels(cachedERGains[t]) : minDB;
            gainDB = juce::jlimit(minDB, maxDB, gainDB);

            const float x = timeToX(timeSec);
            const float yTop = dbToY(gainDB);
            const float yBottom = plotY + plotH;

            // レーザーピン光線
            juce::ColourGradient pinGrad(
                neonCyan.withAlpha(0.85f), x, yTop,
                neonCyan.withAlpha(0.08f), x, yBottom, false);
            g.setGradientFill(pinGrad);
            g.drawLine(x, yTop, x, yBottom, 1.2f);

            // 先端の発光ネオンオーブ (Bloom)
            g.setColour(neonCyan.withAlpha(0.35f));
            g.fillEllipse(x - 3.5f, yTop - 3.5f, 7.0f, 7.0f);

            g.setColour(juce::Colours::white);
            g.fillEllipse(x - 1.2f, yTop - 1.2f, 2.4f, 2.4f);
        }
    }

    // ─── ゾーンバッジ ───
    g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::bold)));

    g.setColour(juce::Colour(0xFF00E5FF).withAlpha(0.9f));
    g.drawText("ER", (int)(plotX + 6), (int)(plotY + 2), 30, 11, juce::Justification::centredLeft);

    g.setColour(AmbienceColors::Accent.withAlpha(0.9f));
    g.drawText("LATE", (int)(splitX + 6), (int)(plotY + 2), 40, 11, juce::Justification::centredLeft);
}
