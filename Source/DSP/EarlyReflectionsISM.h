#pragma once
#include <immintrin.h>
#include <cstdint>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace FDNReverb {

// 8タップ単位の SoA チャンク (32バイトアライメント)
struct alignas(32) ISMTapChunk {
    int32_t delayInt[8];     // 整数遅延サンプル
    float   delayFrac[8];    // 小数遅延部 [0, 1)
    float   gainL[8];        // L ゲイン (距離減衰 + ITD/ILD パンニング)
    float   gainR[8];        // R ゲイン (距離減衰 + ITD/ILD パンニング)
    float   filterCoef[8];   // 壁面吸収LPF係数
    float   filterState[8];  // フィルタ状態変数
};

constexpr int MAX_ISM_TAPS = 64;
constexpr int NUM_ISM_CHUNKS = MAX_ISM_TAPS / 8; // 8 チャンク (64タップ)

// ベルベットノイズ FIR パルス構造
struct alignas(32) VelvetPulse {
    int32_t delaySample;
    float   gainL;
    float   gainR;
};

constexpr int MAX_VELVET_PULSES = 64;

struct ISMParameters {
    std::array<ISMTapChunk, NUM_ISM_CHUNKS> chunks;
    int activeTaps{ 0 };
    int numActiveChunks{ 0 };
    bool useVelvetNoise{ false };
    float velvetGain{ 0.0f };
    std::array<VelvetPulse, MAX_VELVET_PULSES> velvetPulses{};
    int numVelvetPulses{ 0 };
};

class EarlyReflectionsISM {
public:
    EarlyReflectionsISM();
    ~EarlyReflectionsISM() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // [phase3/small-fixes] reset() 完全初期化用スナップショット。
    // reset() は currentParams / pendingParams と updateParameters の
    // 変更検出キャッシュ (last*) を戻さないため、呼び出し側が prepare
    // 終端の状態を捕捉し reset() の後に復元できるようにする。
    struct StateSnapshot {
        std::shared_ptr<ISMParameters> current;
        std::shared_ptr<ISMParameters> pending;
        int lastAlgorithmIndex{ -1 };
        float lastRoomSizeScale{ -1.0f };
        float lastPreDelayMs{ -1.0f };
        float lastHfDamping{ -1.0f };
        float lastLfAbsorption{ -1.0f };
        bool valid{ false };
    };
    StateSnapshot captureState();
    void restoreState(const StateSnapshot& s);

    // 幾何座標・タップ更新 (非オーディオスレッド / パラメータ変更時)
    // rt60Scaled1k / rt60Base1k: 1kHz 帯の実効 RT60 (decayScale 適用後 / プリセット素値)。
    // タップゲインへ decayScale 連動の相対包絡を適用するために渡す (ism-link)。
    void updateParameters(int algorithmIndex, float roomSizeScale, float preDelayMs,
                          float hfDamping, float lfAbsorption,
                          float rt60Scaled1k, float rt60Base1k);

    // リアルタイムオーディオ処理 (AVX2 SIMD)
    void processBlock(const float* inL, const float* inR,
                      float* earlyWetL, float* earlyWetR,
                      float* clusterSeed, int numSamples, float erLevel);

    bool isPrepared() const noexcept { return sampleRate > 0.0; }

private:
    void computeGeometry(int algorithmIndex, float roomSizeScale, float preDelayMs,
                         float hfDamping, float lfAbsorption, ISMParameters& outParams,
                         float rt60Scaled1k, float rt60Base1k);

    // 256bit レジスタ水平加算ヘルパー
    static inline float hsum_m256(__m256 v) noexcept {
        __m128 vlow  = _mm256_castps256_ps128(v);
        __m128 vhigh = _mm256_extractf128_ps(v, 1);
        vlow  = _mm_add_ps(vlow, vhigh);
        __m128 shuf  = _mm_movehl_ps(vlow, vlow);
        vlow  = _mm_add_ps(vlow, shuf);
        shuf  = _mm_shuffle_ps(vlow, vlow, _MM_SHUFFLE(2, 3, 0, 1));
        vlow  = _mm_add_ps(vlow, shuf);
        return _mm_cvtss_f32(vlow);
    }

    double sampleRate{ 48000.0 };

    // リングバッファ: サイズは prepare() で秒ベースに決める (プリディレイ
    // 上限 500 ms + ER 窓 45 ms を当該サンプルレートで収める 2 の冪)。
    // 旧実装は固定 16384 サンプル (192 kHz で 85 ms) で、Inchindown 既定の
    // タップ (65+45 ms = 110 ms) すら収まらず、マスク読みがラップして
    // 過去のサンプルを誤って読んでいた (phase3/inchindown/INCHINDOWN.md)。
    std::vector<float> ringBufferL;
    int ringBufferMask{ 0 };
    uint32_t writeIndex{ 0 };

    // パラメータダブルバッファリング & 30ms スムーズクロスフェード (ジッパー・クリック完全ゼロ)
    std::shared_ptr<ISMParameters> currentParams;
    std::shared_ptr<ISMParameters> fadingFromParams;
    std::atomic<std::shared_ptr<ISMParameters>> pendingParams{ nullptr };
    float paramCrossfadePos{ 1.0f };
    float paramCrossfadeInc{ 0.001f };

    // スムージング用
    float smoothedErLevel{ 0.0f };
    int lastAlgorithmIndex{ -1 };
    float lastRoomSizeScale{ -1.0f };
    float lastPreDelayMs{ -1.0f };
    float lastHfDamping{ -1.0f };
    float lastLfAbsorption{ -1.0f };
    float lastRt60Scaled1k{ -1.0f };
    float lastRt60Base1k{ -1.0f };
};

} // namespace FDNReverb\n