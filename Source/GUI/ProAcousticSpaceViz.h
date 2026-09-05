#pragma once

#include <JuceHeader.h>
#include "AmbienceUI.h"

class FDNReverbAudioProcessor;

// ==============================================================================
/**
    Pro Acoustic Space Visualizer (音響空間・物理モーフィング・ビジュアライザー)
    
    PROタブ 1行目右側 (Bounds: x=590, y=86, w=302, h=110) に配置される
    物理音響空間の幾何学・波動・エネルギー粒子モーフィング描画コンポーネント。
    
    【連動する 6 つの物理パラメータ】
    1. ASYMMETRY   (alpha ∈ [0, 1])  : シューボックスから非平行壁スタジオへの2.5Dシアー変形
    2. SCATTERING  (s ∈ [0, 1])      : 壁面境界パスの QRD/三角波 音響拡散凹凸変調
    3. ER CROSSOVER(Tx ∈ [10, 100]ms): パースペクティブ楕円波面リングと破線交差境界
    4. LATE DENSITY(D ∈ [0.1, 1.0])  : Halton低不一致乱数による音響エネルギー微粒子の空間充填
    5. CLARITY     (C ∈ [-6, +6]dB)  : 音源ノードのラジアル・グロー＆白熱集束コア
    6. AIR ABSORB  (m ∈ [0.2, 2.5]x) : 奥方向への大気減衰フォグとワイヤーフレーム彩度減衰
    
    【パフォーマンス保証】
    - 描画時ヒープアロケーション（malloc/free）完全ゼロ（Path 事前リザーブ & メンバ再利用）
    - 低不一致乱数 Halton 列の静的テーブル化
    - パラメータ Dirty-Check によるアイドル時描画スキップ（CPU 負荷 0.05% 未満）
    - Direct2D / CoreGraphics / Software Renderer 完全互換
*/
class ProAcousticSpaceViz : public juce::Component, private juce::Timer
{
public:
    ProAcousticSpaceViz();
    ~ProAcousticSpaceViz() override;

    void setProcessor(FDNReverbAudioProcessor* p) noexcept { processor = p; }

    /** 外部からの明示的なパラメータ更新（Sliderリスナー等からの即時反映用） */
    void setParams(float scattering, float erCrossover, float lateDensity,
                   float asymmetry, float clarity, float airAbsorb) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // ── 内部幾何・描画ステージ ───────────────────────────────────────
    void computeRoomGeometry(float width, float height);
    void drawRoomWireframeAndSurfaces(juce::Graphics& g, const AmbienceTheme& theme);
    void drawScatteringPanels(juce::Graphics& g, const AmbienceTheme& theme);
    void drawWavefronts(juce::Graphics& g, const AmbienceTheme& theme);
    void drawLateDensityParticles(juce::Graphics& g, const AmbienceTheme& theme);
    void drawSoundSourceNode(juce::Graphics& g, const AmbienceTheme& theme);
    void drawAirAbsorptionFog(juce::Graphics& g, const AmbienceTheme& theme);
    void drawHUDOverlay(juce::Graphics& g, const AmbienceTheme& theme);

    // ── プロセッサ参照 ───────────────────────────────────────────────
    FDNReverbAudioProcessor* processor{ nullptr };

    // ── キャッシュされたパラメータ値 ─────────────────────────────────
    float cachedScattering  { 0.5f };   // [0, 1]
    float cachedERCrossover { 40.0f };  // [10, 100] ms
    float cachedLateDensity { 0.7f };   // [0, 1]
    float cachedAsymmetry   { 0.3f };   // [0, 1]
    float cachedClarity     { 0.0f };   // [-6, +6] dB
    float cachedAirAbsorb   { 1.0f };   // [0.2, 2.5] x

    // ── 前フレーム比較用（Dirty Check）──────────────────────────────
    float lastScattering  { -1.0f };
    float lastERCrossover { -1.0f };
    float lastLateDensity { -1.0f };
    float lastAsymmetry   { -1.0f };
    float lastClarity     { -999.0f };
    float lastAirAbsorb   { -1.0f };
    int   lastThemeIndex  { -1 };

    // ── 幾何学頂点バッファ（2.5D投影座標）─────────────────────────────
    // 0..3: 手前壁（左上, 右上, 右下, 左下）
    // 4..7: 奥壁  （左上, 右上, 右下, 左下）
    std::array<juce::Point<float>, 8> roomVertices;
    juce::Point<float> soundSourcePos; // 床面上の音源位置

    // ── ゼロアロケーション用 Path メンバオブジェクト ────────────────
    juce::Path pathFloor;
    juce::Path pathCeiling;
    juce::Path pathLeftWall;
    juce::Path pathRightWall;
    juce::Path pathBackWall;
    juce::Path pathWireframe;
    juce::Path pathScattering;
    juce::Path pathWaveSolid;
    juce::Path pathWaveDashed;
    juce::Path pathParticlesNear;
    juce::Path pathParticlesFar;

    // ── Halton 列粒子シードテーブル（64粒子）────────────────────────
    struct ParticleSeed {
        float x, y, z; // 正規化空間座標 [0, 1]
    };
    static constexpr int NUM_PARTICLES = 64;
    std::array<ParticleSeed, NUM_PARTICLES> particleSeeds;

    void initializeHaltonSeeds() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProAcousticSpaceViz)
};
