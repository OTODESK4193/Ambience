#pragma once
#include <immintrin.h>
#include <cstdint>
#include <atomic>
#include <array>
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

struct ISMParameters {
    std::array<ISMTapChunk, NUM_ISM_CHUNKS> chunks;
    int activeTaps{ 0 };
    int numActiveChunks{ 0 };
    bool useVelvetNoise{ false };
    float velvetGain{ 0.0f };
};

// ベルベットノイズ FIR パルス構造
struct alignas(32) VelvetPulse {
    int32_t delaySample;
    float   gainL;
    float   gainR;
};

constexpr int MAX_VELVET_PULSES = 64;

class EarlyReflectionsISM {
public:
    EarlyReflectionsISM();
    ~EarlyReflectionsISM() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // 幾何座標・タップ更新 (非オーディオスレッド / パラメータ変更時)
    void updateParameters(int algorithmIndex, float roomSizeScale, float preDelayMs,
                          float hfDamping, float lfAbsorption);

    // リアルタイムオーディオ処理 (AVX2 SIMD)
    void processBlock(const float* inL, const float* inR,
                      float* earlyWetL, float* earlyWetR,
                      float* clusterSeed, int numSamples, float erLevel);

    bool isPrepared() const noexcept { return sampleRate > 0.0; }

private:
    void computeGeometry(int algorithmIndex, float roomSizeScale, float preDelayMs,
                         float hfDamping, float lfAbsorption, ISMParameters& outParams);

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

    // リングバッファ (サイズ 16384: 192kHzでも 85ms をカバー)
    static constexpr int RING_BUFFER_SIZE = 16384;
    static constexpr int RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;
    alignas(32) std::array<float, RING_BUFFER_SIZE> ringBufferL{};
    alignas(32) std::array<float, RING_BUFFER_SIZE> ringBufferR{};
    uint32_t writeIndex{ 0 };

    // パラメータダブルバッファリング
    std::shared_ptr<ISMParameters> currentParams;
    std::atomic<std::shared_ptr<ISMParameters>> pendingParams{ nullptr };

    // ベルベットノイズ生成用
    std::array<VelvetPulse, MAX_VELVET_PULSES> velvetPulses{};
    int numVelvetPulses{ 0 };

    // スムージング用
    float smoothedErLevel{ 0.0f };
    int lastAlgorithmIndex{ -1 };
    float lastRoomSizeScale{ -1.0f };
    float lastPreDelayMs{ -1.0f };
    float lastHfDamping{ -1.0f };
    float lastLfAbsorption{ -1.0f };
};

} // namespace FDNReverb\n