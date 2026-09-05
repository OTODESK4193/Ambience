#include "ProAcousticSpaceViz.h"
#include "../PluginProcessor.h"
#include <cmath>

// ── 低不一致乱数（Halton列）計算関数 ──────────────────────────────────
static inline float computeHalton(int index, int base) noexcept
{
    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    int i = index;
    while (i > 0)
    {
        result += static_cast<float>(i % base) * f;
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

// ==============================================================================
ProAcousticSpaceViz::ProAcousticSpaceViz()
{
    initializeHaltonSeeds();
    startTimerHz(40); // 40Hz で十分滑らかな描画更新と極低CPU負荷を両立
}

ProAcousticSpaceViz::~ProAcousticSpaceViz()
{
    stopTimer();
}

void ProAcousticSpaceViz::initializeHaltonSeeds() noexcept
{
    for (int i = 0; i < NUM_PARTICLES; ++i)
    {
        // 基底 2, 3, 5 による準均一3次元分布
        particleSeeds[i].x = computeHalton(i + 1, 2);
        particleSeeds[i].y = computeHalton(i + 1, 3);
        particleSeeds[i].z = computeHalton(i + 1, 5);
    }
}

void ProAcousticSpaceViz::setParams(float scattering, float erCrossover, float lateDensity,
                                     float asymmetry, float clarity, float airAbsorb) noexcept
{
    cachedScattering  = juce::jlimit(0.0f, 1.0f, scattering);
    cachedERCrossover = juce::jlimit(10.0f, 100.0f, erCrossover);
    cachedLateDensity = juce::jlimit(0.0f, 1.0f, lateDensity);
    cachedAsymmetry   = juce::jlimit(0.0f, 1.0f, asymmetry);
    cachedClarity     = juce::jlimit(-6.0f, 6.0f, clarity);
    cachedAirAbsorb   = juce::jlimit(0.2f, 2.5f, airAbsorb);
    repaint();
}

void ProAcousticSpaceViz::resized()
{
    // ヒープ再アロケーションを防ぐための事前容量リザーブ
    pathFloor.preallocateSpace(16);
    pathCeiling.preallocateSpace(16);
    pathLeftWall.preallocateSpace(16);
    pathRightWall.preallocateSpace(16);
    pathBackWall.preallocateSpace(16);
    pathWireframe.preallocateSpace(64);
    pathScattering.preallocateSpace(128);
    pathWaveSolid.preallocateSpace(32);
    pathWaveDashed.preallocateSpace(128);
    pathParticlesNear.preallocateSpace(256);
    pathParticlesFar.preallocateSpace(256);

    auto b = getLocalBounds().toFloat();
    computeRoomGeometry(b.getWidth(), b.getHeight());
}

void ProAcousticSpaceViz::timerCallback()
{
    if (!isVisible())
        return;

    if (processor != nullptr)
    {
        auto& apvts = processor->apvts;
        auto getVal = [&](const juce::String& id, float def) -> float {
            if (auto* raw = apvts.getRawParameterValue(id))
                return raw->load();
            return def;
        };

        cachedScattering  = getVal("scattering", 0.5f);
        cachedERCrossover = getVal("ercrossover", 40.0f);
        cachedLateDensity = getVal("latedensity", 0.7f);
        cachedAsymmetry   = getVal("asymmetry", 0.3f);
        cachedClarity     = getVal("clarity", 0.0f);
        cachedAirAbsorb   = getVal("airabsorb", 1.0f);
    }

    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    int currentThemeIdx = laf ? laf->getThemeIndex() : AmbienceColors::activeThemeIndex;

    // パラメータが変化していない場合は再描画をスキップ（CPU負荷 0.05% 未満保証）
    const float eps = 1e-4f;
    if (std::abs(cachedScattering  - lastScattering)  < eps &&
        std::abs(cachedERCrossover - lastERCrossover) < eps &&
        std::abs(cachedLateDensity - lastLateDensity) < eps &&
        std::abs(cachedAsymmetry   - lastAsymmetry)   < eps &&
        std::abs(cachedClarity     - lastClarity)     < eps &&
        std::abs(cachedAirAbsorb   - lastAirAbsorb)   < eps &&
        currentThemeIdx == lastThemeIndex)
    {
        return;
    }

    lastScattering  = cachedScattering;
    lastERCrossover = cachedERCrossover;
    lastLateDensity = cachedLateDensity;
    lastAsymmetry   = cachedAsymmetry;
    lastClarity     = cachedClarity;
    lastAirAbsorb   = cachedAirAbsorb;
    lastThemeIndex  = currentThemeIdx;

    auto b = getLocalBounds().toFloat();
    computeRoomGeometry(b.getWidth(), b.getHeight());
    repaint();
}

// ── 幾何学頂点計算ステージ ───────────────────────────────────────────
void ProAcousticSpaceViz::computeRoomGeometry(float width, float height)
{
    if (width < 20.0f || height < 20.0f)
        return;

    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const float vpY = cy + 1.0f; // 視点を安定させ、天井と床のバランスを最適化

    // 直方体スケール（カード内 Bounds: 302 x 110 の枠線・マージン内に完全収束）
    const float frontW = 108.0f;
    const float frontTopH = 26.0f;
    const float frontBotH = 36.0f;

    const float backW = 50.0f;
    const float backTopH = 14.0f;
    const float backBotH = 16.0f;

    const float alpha = cachedAsymmetry;

    // ① ASYMMETRY による非線形幾何モーフィング
    // 左側頂点群（対称基準壁）
    roomVertices[0] = { cx - frontW, vpY - frontTopH }; // 手前・左上
    roomVertices[3] = { cx - frontW, vpY + frontBotH }; // 手前・左下
    roomVertices[4] = { cx - backW,  vpY - backTopH };  // 奥・左上
    roomVertices[7] = { cx - backW,  vpY + backBotH };  // 奥・左下

    // 右側頂点群（非平行スタジオ傾斜シアー）
    const float shiftBackX = 16.0f * std::pow(alpha, 1.2f);
    const float shiftBackY = -4.0f * std::sin(alpha * juce::MathConstants<float>::halfPi);
    const float shiftFrontX = -12.0f * alpha;
    const float shiftFrontY = -5.0f * std::pow(alpha, 1.4f);

    roomVertices[5] = { cx + backW + shiftBackX,  vpY - backTopH + shiftBackY }; // 奥・右上
    roomVertices[6] = { cx + backW + shiftBackX * 0.8f, vpY + backBotH - shiftBackY * 0.5f }; // 奥・右下
    roomVertices[1] = { cx + frontW + shiftFrontX, vpY - frontTopH + 2.5f * alpha }; // 手前・右上
    roomVertices[2] = { cx + frontW + shiftFrontX * 1.1f, vpY + frontBotH + shiftFrontY }; // 手前・右下

    // 床面中央の音源位置（右壁の傾斜変形に有機的に追従）
    soundSourcePos.x = (roomVertices[3].x + roomVertices[2].x + roomVertices[7].x + roomVertices[6].x) * 0.25f;
    soundSourcePos.y = (roomVertices[3].y + roomVertices[2].y + roomVertices[7].y + roomVertices[6].y) * 0.25f + 1.5f;
}

void ProAcousticSpaceViz::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 20.0f || bounds.getHeight() < 20.0f)
        return;

    auto* laf = dynamic_cast<AmbienceLookAndFeel*>(&getLookAndFeel());
    const auto& theme = laf ? laf->getTheme() : AmbienceColors::THEMES[AmbienceColors::activeThemeIndex];

    // カード枠線（角丸6px）の内側に美しく収める微細な暗色バッキング（上端バッジを一切塗りつぶさない）
    auto innerRect = bounds.reduced(1.0f);
    g.setColour(theme.surface.darker(0.35f).withAlpha(0.60f));
    g.fillRoundedRectangle(innerRect, 5.0f);

    // レンダリング・パイプライン
    drawRoomWireframeAndSurfaces(g, theme);
    drawAirAbsorptionFog(g, theme);
    drawScatteringPanels(g, theme);
    drawWavefronts(g, theme);
    drawLateDensityParticles(g, theme);
    drawSoundSourceNode(g, theme);
    drawHUDOverlay(g, theme);
}

// ── 空間構造面・ワイヤーフレーム描画 ──────────────────────────────────
void ProAcousticSpaceViz::drawRoomWireframeAndSurfaces(juce::Graphics& g, const AmbienceTheme& theme)
{
    // 床面ポリゴン (V3, V2, V6, V7)
    pathFloor.clear();
    pathFloor.startNewSubPath(roomVertices[3]);
    pathFloor.lineTo(roomVertices[2]);
    pathFloor.lineTo(roomVertices[6]);
    pathFloor.lineTo(roomVertices[7]);
    pathFloor.closeSubPath();

    juce::ColourGradient floorGrad(theme.panel.withAlpha(0.55f), soundSourcePos.x, roomVertices[3].y,
                                   theme.surface.withAlpha(0.20f), soundSourcePos.x, roomVertices[7].y, false);
    g.setGradientFill(floorGrad);
    g.fillPath(pathFloor);

    // 奥壁ポリゴン (V4, V5, V6, V7)
    pathBackWall.clear();
    pathBackWall.startNewSubPath(roomVertices[4]);
    pathBackWall.lineTo(roomVertices[5]);
    pathBackWall.lineTo(roomVertices[6]);
    pathBackWall.lineTo(roomVertices[7]);
    pathBackWall.closeSubPath();

    g.setColour(theme.background.withAlpha(0.70f));
    g.fillPath(pathBackWall);

    // 左壁 (V0, V4, V7, V3) & 右壁 (V1, V5, V6, V2)
    pathLeftWall.clear();
    pathLeftWall.startNewSubPath(roomVertices[0]);
    pathLeftWall.lineTo(roomVertices[4]);
    pathLeftWall.lineTo(roomVertices[7]);
    pathLeftWall.lineTo(roomVertices[3]);
    pathLeftWall.closeSubPath();
    g.setColour(theme.panel.withAlpha(0.25f));
    g.fillPath(pathLeftWall);

    pathRightWall.clear();
    pathRightWall.startNewSubPath(roomVertices[1]);
    pathRightWall.lineTo(roomVertices[5]);
    pathRightWall.lineTo(roomVertices[6]);
    pathRightWall.lineTo(roomVertices[2]);
    pathRightWall.closeSubPath();
    g.setColour(theme.panel.withAlpha(0.35f));
    g.fillPath(pathRightWall);

    // 構造ワイヤーフレームストローク
    pathWireframe.clear();
    // 奥行きリブエッジ
    pathWireframe.startNewSubPath(roomVertices[0]); pathWireframe.lineTo(roomVertices[4]);
    pathWireframe.startNewSubPath(roomVertices[1]); pathWireframe.lineTo(roomVertices[5]);
    pathWireframe.startNewSubPath(roomVertices[2]); pathWireframe.lineTo(roomVertices[6]);
    pathWireframe.startNewSubPath(roomVertices[3]); pathWireframe.lineTo(roomVertices[7]);

    // 手前フレーム
    pathWireframe.startNewSubPath(roomVertices[0]);
    pathWireframe.lineTo(roomVertices[1]);
    pathWireframe.lineTo(roomVertices[2]);
    pathWireframe.lineTo(roomVertices[3]);
    pathWireframe.closeSubPath();

    // 奥フレーム
    pathWireframe.startNewSubPath(roomVertices[4]);
    pathWireframe.lineTo(roomVertices[5]);
    pathWireframe.lineTo(roomVertices[6]);
    pathWireframe.lineTo(roomVertices[7]);
    pathWireframe.closeSubPath();

    g.setColour(theme.border.interpolatedWith(theme.secondary, 0.45f).withAlpha(0.75f));
    g.strokePath(pathWireframe, juce::PathStrokeType(1.0f));

    // 非対称性強調エッジ（右壁）
    if (cachedAsymmetry > 0.05f)
    {
        juce::Path asymPath;
        asymPath.startNewSubPath(roomVertices[1]);
        asymPath.lineTo(roomVertices[5]);
        asymPath.lineTo(roomVertices[6]);
        asymPath.lineTo(roomVertices[2]);
        g.setColour(theme.secondary.withAlpha(0.40f * cachedAsymmetry));
        g.strokePath(asymPath, juce::PathStrokeType(1.5f));
    }
}

// ── 大気減衰フォグ描画 ────────────────────────────────────────────────
void ProAcousticSpaceViz::drawAirAbsorptionFog(juce::Graphics& g, const AmbienceTheme& theme)
{
    // ⑥ AIR ABSORB による深度フォグ
    const float mu = (cachedAirAbsorb - 0.2f) / 2.3f; // [0, 1]
    const float fogAlpha = 0.15f + 0.60f * mu;

    const float topY = roomVertices[4].y;
    const float botY = roomVertices[7].y + 12.0f;

    juce::ColourGradient fogGrad(theme.background.withAlpha(fogAlpha), soundSourcePos.x, topY,
                                 theme.background.withAlpha(0.0f), soundSourcePos.x, botY, false);
    g.setGradientFill(fogGrad);
    g.fillPath(pathBackWall);
}

// ── 散乱音響パネル（QRD凹凸）描画 ────────────────────────────────────
void ProAcousticSpaceViz::drawScatteringPanels(juce::Graphics& g, const AmbienceTheme& theme)
{
    // ② SCATTERING による奥壁下部エッジの凹凸変調
    const float s = cachedScattering;
    if (s < 0.02f) return;

    pathScattering.clear();

    const auto pA = roomVertices[7];
    const auto pB = roomVertices[6];
    const auto diff = pB - pA;
    const float len = std::hypot(diff.x, diff.y);
    if (len < 1.0f) return;

    const juce::Point<float> u = diff / len;
    const juce::Point<float> n(u.y, -u.x); // 上向き法線

    const int numDivs = 14;
    static const int qrdPattern[15] = { 0, 1, 4, 2, 2, 4, 1, 0, 1, 4, 2, 2, 4, 1, 0 };

    pathScattering.startNewSubPath(pA);
    for (int k = 1; k <= numDivs; ++k)
    {
        float t = static_cast<float>(k) / static_cast<float>(numDivs);
        auto basePt = pA + diff * t;
        float h = (static_cast<float>(qrdPattern[k]) / 6.0f - 0.2f) * 5.5f * s;
        auto ctrlPt = basePt + n * h;

        float tMid = (static_cast<float>(k) - 0.5f) / static_cast<float>(numDivs);
        auto midPt = pA + diff * tMid + n * h;
        pathScattering.quadraticTo(midPt.x, midPt.y, ctrlPt.x, ctrlPt.y);
    }

    g.setColour(theme.secondary.withAlpha(0.40f + 0.50f * s));
    g.strokePath(pathScattering, juce::PathStrokeType(1.2f));
}

// ── 初期反射波面リング描画 ────────────────────────────────────────────
void ProAcousticSpaceViz::drawWavefronts(juce::Graphics& g, const AmbienceTheme& theme)
{
    // ③ ER CROSSOVER 波面楕円
    const float tau = (cachedERCrossover - 10.0f) / 90.0f; // [0, 1]
    const float rx = 14.0f + 56.0f * tau;
    const float ry = rx * 0.35f; // パースペクティブ床面扁平率

    // 内側波面（直接音ゾーン）
    pathWaveSolid.clear();
    const float inRx = rx * 0.55f;
    const float inRy = ry * 0.55f;
    pathWaveSolid.addEllipse(soundSourcePos.x - inRx, soundSourcePos.y - inRy, inRx * 2.0f, inRy * 2.0f);
    g.setColour(theme.secondary.withAlpha(0.40f));
    g.strokePath(pathWaveSolid, juce::PathStrokeType(1.0f));

    // 外側波面（Crossover境界: 破線ストローク）
    juce::Path ringEllipse;
    ringEllipse.addEllipse(soundSourcePos.x - rx, soundSourcePos.y - ry, rx * 2.0f, ry * 2.0f);

    float dashes[] = { 4.0f, 3.0f };
    pathWaveDashed.clear();
    juce::PathStrokeType(1.2f).createDashedStroke(pathWaveDashed, ringEllipse, dashes, 2);

    g.setColour(theme.primary.withAlpha(0.85f));
    g.fillPath(pathWaveDashed);
}

// ── 後期残響エネルギー微粒子（Halton列）描画 ─────────────────────────
void ProAcousticSpaceViz::drawLateDensityParticles(juce::Graphics& g, const AmbienceTheme& theme)
{
    // ④ LATE DENSITY パーティクル
    const int count = static_cast<int>(12.0f + 52.0f * cachedLateDensity);

    pathParticlesNear.clear();
    pathParticlesFar.clear();

    for (int i = 0; i < count; ++i)
    {
        const auto& s = particleSeeds[i];
        float z = s.z; // 0: 手前, 1: 奥

        // 錐台補間による3次元位置決定
        float wLeft  = juce::jmap(z, roomVertices[3].x, roomVertices[7].x);
        float wRight = juce::jmap(z, roomVertices[2].x, roomVertices[6].x);
        float hTop   = juce::jmap(z, roomVertices[0].y, roomVertices[4].y);
        float hBot   = juce::jmap(z, roomVertices[3].y, roomVertices[7].y);

        float px = juce::jmap(s.x, wLeft + 4.0f, wRight - 4.0f);
        float py = juce::jmap(s.y, hTop + 4.0f, hBot - 4.0f);

        float radius = (z < 0.5f) ? (1.4f - z * 0.8f) : (0.9f - z * 0.3f);

        if (z < 0.5f)
            pathParticlesNear.addEllipse(px - radius, py - radius, radius * 2.0f, radius * 2.0f);
        else
            pathParticlesFar.addEllipse(px - radius, py - radius, radius * 2.0f, radius * 2.0f);
    }

    // 奥の粒子（ソフト減衰）
    g.setColour(theme.textSecondary.withAlpha(0.35f));
    g.fillPath(pathParticlesFar);

    // 手前の粒子（高輝度アクセント）
    g.setColour(theme.secondary.withAlpha(0.80f));
    g.fillPath(pathParticlesNear);
}

// ── 音源コア & ラジアルブルーム描画 ──────────────────────────────────
void ProAcousticSpaceViz::drawSoundSourceNode(juce::Graphics& g, const AmbienceTheme& theme)
{
    // ⑤ CLARITY 音源ノード
    const float cNorm = (cachedClarity + 6.0f) / 12.0f; // [0, 1]
    const float bloomRadius = 28.0f - 16.0f * cNorm;   // 28px (拡散) ~ 12px (集束)
    const float coreRadius  = 2.2f + 2.3f * cNorm;     // 2.2px ~ 4.5px

    // 周囲への放射ブルーム（ラジアルグラデーション）
    juce::Colour innerBloom = theme.primary.withAlpha(0.35f + 0.45f * cNorm);
    juce::Colour outerBloom = theme.primary.withAlpha(0.0f);

    juce::ColourGradient bloomGrad(innerBloom, soundSourcePos.x, soundSourcePos.y,
                                   outerBloom, soundSourcePos.x + bloomRadius, soundSourcePos.y, true);
    g.setGradientFill(bloomGrad);
    g.fillEllipse(soundSourcePos.x - bloomRadius, soundSourcePos.y - bloomRadius,
                  bloomRadius * 2.0f, bloomRadius * 2.0f);

    // シャープな白熱コア
    juce::Colour coreCol = theme.primary.interpolatedWith(juce::Colours::white, 0.30f + 0.65f * cNorm);
    g.setColour(coreCol);
    g.fillEllipse(soundSourcePos.x - coreRadius, soundSourcePos.y - coreRadius,
                  coreRadius * 2.0f, coreRadius * 2.0f);
}

// ── HUD オーバーレイ描画 ──────────────────────────────────────────────
void ProAcousticSpaceViz::drawHUDOverlay(juce::Graphics& g, const AmbienceTheme& theme)
{
    auto b = getLocalBounds().toFloat();

    // 四隅のマイクロ・レティクル（コーナーブラケット L字 5px, カード枠角丸の内側に配置）
    g.setColour(theme.textSecondary.withAlpha(0.35f));
    const float m = 8.0f;
    const float len = 5.0f;
    // 左上
    g.drawLine(m, m, m + len, m, 1.0f);
    g.drawLine(m, m, m, m + len, 1.0f);
    // 右上
    g.drawLine(b.getRight() - m, m, b.getRight() - m - len, m, 1.0f);
    g.drawLine(b.getRight() - m, m, b.getRight() - m, m + len, 1.0f);
    // 左下
    g.drawLine(m, b.getBottom() - m, m + len, b.getBottom() - m, 1.0f);
    g.drawLine(m, b.getBottom() - m, m, b.getBottom() - m - len, 1.0f);
    // 右下
    g.drawLine(b.getRight() - m, b.getBottom() - m, b.getRight() - m - len, b.getBottom() - m, 1.0f);
    g.drawLine(b.getRight() - m, b.getBottom() - m, b.getRight() - m, b.getBottom() - m - len, 1.0f);

    // 左上ミニタグ（カードバッジと重複しない控えめなステータス）
    g.setFont(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::plain));
    g.setColour(theme.textSecondary.withAlpha(0.55f));
    g.drawText("LIVE 3D SIM", juce::Rectangle<float>(14.0f, 8.0f, 80.0f, 12.0f), juce::Justification::left);

    // 空間モードバッジ（右上にすっきりと表示）
    juce::String modeStr = (cachedAsymmetry < 0.15f) ? "MODE: SHOEBOX" : "MODE: ASYMMETRIC";
    g.setFont(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::bold));
    g.setColour(theme.secondary.withAlpha(0.85f));
    g.drawText(modeStr, juce::Rectangle<float>(b.getWidth() - 130.0f, 8.0f, 118.0f, 12.0f), juce::Justification::right);
}