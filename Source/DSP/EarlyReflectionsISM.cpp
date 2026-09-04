#include "EarlyReflectionsISM.h"
#include "DSPConstants.h"

namespace FDNReverb {

EarlyReflectionsISM::EarlyReflectionsISM() {
    currentParams = std::make_shared<ISMParameters>();
}

void EarlyReflectionsISM::prepare(double sr, int /*maxBlockSize*/) {
    sampleRate = sr;
    reset();
    ISMParameters initialParams;
    computeGeometry(0, 1.0f, 10.0f, 0.5f, 0.5f, initialParams);
    currentParams = std::make_shared<ISMParameters>(initialParams);
    pendingParams.store(nullptr);
}

void EarlyReflectionsISM::reset() {
    ringBufferL.fill(0.0f);
    ringBufferR.fill(0.0f);
    writeIndex = 0;
    smoothedErLevel = 0.0f;
    lastAlgorithmIndex = -1;
}

void EarlyReflectionsISM::updateParameters(int algorithmIndex, float roomSizeScale,
                                          float preDelayMs, float hfDamping, float lfAbsorption)
{
    if (algorithmIndex == lastAlgorithmIndex &&
        std::abs(roomSizeScale - lastRoomSizeScale) < 0.001f &&
        std::abs(preDelayMs - lastPreDelayMs) < 0.05f &&
        std::abs(hfDamping - lastHfDamping) < 0.005f &&
        std::abs(lfAbsorption - lastLfAbsorption) < 0.005f)
    {
        return;
    }

    lastAlgorithmIndex = algorithmIndex;
    lastRoomSizeScale = roomSizeScale;
    lastPreDelayMs = preDelayMs;
    lastHfDamping = hfDamping;
    lastLfAbsorption = lfAbsorption;

    auto newParams = std::make_shared<ISMParameters>();
    computeGeometry(algorithmIndex, roomSizeScale, preDelayMs, hfDamping, lfAbsorption, *newParams);
    pendingParams.store(newParams, std::memory_order_release);
}

void EarlyReflectionsISM::computeGeometry(int algorithmIndex, float roomSizeScale,
                                         float preDelayMs, float hfDamping, float /*lfAbsorption*/,
                                         ISMParameters& outParams)
{
    outParams.activeTaps = 0;
    outParams.numActiveChunks = 0;
    outParams.useVelvetNoise = false;
    outParams.velvetGain = 0.0f;

    constexpr float c_sound = 343.0f;
    const float fs = static_cast<float>(sampleRate);
    const float preDelaySamples = (preDelayMs * 0.001f) * fs;
    const float sizeScale = std::clamp(roomSizeScale, 0.2f, 5.0f);

    // 空間容積に応じた物理的初期反射窓 (Room: 35ms, Hall/Plate/Spring/Inch: 45ms)
    const float roomErWindowSec = (algorithmIndex == 0 || algorithmIndex == 1) ? 0.035f : 0.045f;
    const float maxErDelaySamples = preDelaySamples + (roomErWindowSec * fs);

    struct RawTap {
        float delaySamples;
        float gainL;
        float gainR;
        float filterCoef;
    };
    std::vector<RawTap> rawTaps;
    rawTaps.reserve(MAX_ISM_TAPS);

    // ─────────────────────────────────────────────────────────────────────────────
    //  RoomType 別のバイノーラル幾何反射計算 (左右受音点の完全時間的分離)
    // ─────────────────────────────────────────────────────────────────────────────
    if (algorithmIndex == 0 || algorithmIndex == 1) {
        // ─── Case 0, 1: 3D Shoebox (ROOM1, ROOM2) ───
        float Lx = (algorithmIndex == 0 ? 4.0f : 5.5f) * std::cbrt(sizeScale);
        float Ly = (algorithmIndex == 0 ? 5.0f : 6.5f) * std::cbrt(sizeScale);
        float Lz = (algorithmIndex == 0 ? 2.0f : 2.8f) * std::cbrt(sizeScale);

        float sx = 0.35f * Lx, sy = 0.38f * Ly, sz = 0.45f * Lz;
        float rx = 0.65f * Lx, ry = 0.62f * Ly, rz = 0.45f * Lz;

        // 人間頭部音響モデル (耳間幅 19cm ＋ 耳介前後角オフセット 2.5cm)
        constexpr float earDistY = 0.095f;
        constexpr float earDistX = 0.025f;
        float rxL = rx - earDistX, ryL = ry - earDistY, rzL = rz;
        float rxR = rx + earDistX, ryR = ry + earDistY, rzR = rz;

        float d0 = std::max(0.5f, std::sqrt((sx - rx)*(sx - rx) + (sy - ry)*(sy - ry) + (sz - rz)*(sz - rz)));

        int tapIdx = 0;
        for (int nx = -1; nx <= 1 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++nx) {
            for (int ny = -1; ny <= 1 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++ny) {
                for (int nz = -1; nz <= 1 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++nz) {
                    if (nx == 0 && ny == 0 && nz == 0) continue;
                    int order = std::abs(nx) + std::abs(ny) + std::abs(nz);
                    if (order > 2) continue;

                    float px = (nx % 2 == 0) ? (nx * Lx + sx) : (nx * Lx + (Lx - sx));
                    float py = (ny % 2 == 0) ? (ny * Ly + sy) : (ny * Ly + (Ly - sy));
                    float pz = (nz % 2 == 0) ? (nz * Lz + sz) : (nz * Lz + (Lz - sz));

                    float distL = std::sqrt((px-rxL)*(px-rxL) + (py-ryL)*(py-ryL) + (pz-rzL)*(pz-rzL));
                    float distR = std::sqrt((px-rxR)*(px-rxR) + (py-ryR)*(py-ryR) + (pz-rzR)*(pz-rzR));

                    float delaySmpL = preDelaySamples + (distL / c_sound) * fs;
                    float delaySmpR = preDelaySamples + (distR / c_sound) * fs;

                    if (delaySmpL < maxErDelaySamples) {
                        float ampL = (d0 / std::max(d0, distL)) * std::pow(0.85f, static_cast<float>(order)) * 0.7071f;
                        float filterCoefL = std::clamp(0.95f - (order * 0.12f * (1.0f + hfDamping)), 0.15f, 0.95f);
                        rawTaps.push_back({ delaySmpL, ampL, 0.0f, filterCoefL });
                    }
                    if (delaySmpR < maxErDelaySamples) {
                        float ampR = (d0 / std::max(d0, distR)) * std::pow(0.85f, static_cast<float>(order)) * 0.7071f;
                        float filterCoefR = std::clamp(0.95f - (order * 0.12f * (1.0f + hfDamping)), 0.15f, 0.95f);
                        rawTaps.push_back({ delaySmpR, 0.0f, ampR, filterCoefR });
                    }
                    ++tapIdx;
                }
            }
        }
    }
    else if (algorithmIndex == 2 || algorithmIndex == 3) {
        // ─── Case 2, 3: 大容積 3D Shoebox ＋ 独立ベルベットノイズ (HALL1, HALL2) ───
        float Lx = (algorithmIndex == 2 ? 15.0f : 25.0f) * std::cbrt(sizeScale);
        float Ly = (algorithmIndex == 2 ? 22.0f : 36.0f) * std::cbrt(sizeScale);
        float Lz = (algorithmIndex == 2 ? 6.0f : 13.5f) * std::cbrt(sizeScale);

        float sx = 0.40f * Lx, sy = 0.30f * Ly, sz = 0.35f * Lz;
        float rx = 0.60f * Lx, ry = 0.70f * Ly, rz = 0.35f * Lz;

        constexpr float earDist = 0.10f;
        float rxL = rx, ryL = ry - earDist, rzL = rz;
        float rxR = rx, ryR = ry + earDist, rzR = rz;

        float d0 = std::max(1.0f, std::sqrt((sx - rx)*(sx - rx) + (sy - ry)*(sy - ry) + (sz - rz)*(sz - rz)));

        int tapIdx = 0;
        for (int nx = -1; nx <= 1 && rawTaps.size() + 2 <= 32; ++nx) {
            for (int ny = -1; ny <= 1 && rawTaps.size() + 2 <= 32; ++ny) {
                for (int nz = -1; nz <= 1 && rawTaps.size() + 2 <= 32; ++nz) {
                    if (nx == 0 && ny == 0 && nz == 0) continue;
                    int order = std::abs(nx) + std::abs(ny) + std::abs(nz);
                    if (order > 2) continue;

                    float px = (nx % 2 == 0) ? (nx * Lx + sx) : (nx * Lx + (Lx - sx));
                    float py = (ny % 2 == 0) ? (ny * Ly + sy) : (ny * Ly + (Ly - sy));
                    float pz = (nz % 2 == 0) ? (nz * Lz + sz) : (nz * Lz + (Lz - sz));

                    float distL = std::sqrt((px-rxL)*(px-rxL) + (py-ryL)*(py-ryL) + (pz-rzL)*(pz-rzL));
                    float distR = std::sqrt((px-rxR)*(px-rxR) + (py-ryR)*(py-ryR) + (pz-rzR)*(pz-rzR));

                    float delaySmpL = preDelaySamples + (distL / c_sound) * fs;
                    float delaySmpR = preDelaySamples + (distR / c_sound) * fs;

                    if (delaySmpL < maxErDelaySamples) {
                        float ampL = (d0 / std::max(d0, distL)) * std::pow(0.80f, static_cast<float>(order)) * 0.7071f;
                        float filterCoefL = std::clamp(0.90f - (order * 0.15f * (1.0f + hfDamping)), 0.10f, 0.90f);
                        rawTaps.push_back({ delaySmpL, ampL, 0.0f, filterCoefL });
                    }
                    if (delaySmpR < maxErDelaySamples) {
                        float ampR = (d0 / std::max(d0, distR)) * std::pow(0.80f, static_cast<float>(order)) * 0.7071f;
                        float filterCoefR = std::clamp(0.90f - (order * 0.15f * (1.0f + hfDamping)), 0.10f, 0.90f);
                        rawTaps.push_back({ delaySmpR, 0.0f, ampR, filterCoefR });
                    }
                    ++tapIdx;
                }
            }
        }

        // Velvet Noise: 左右交互パルス (ER時間窓 45ms 内に収束)
        outParams.useVelvetNoise = true;
        outParams.velvetGain = 0.10f;
        numVelvetPulses = 0;
        constexpr int vCount = 16;
        float vIntervalSmp = (0.0020f) * fs;
        for (int v = 0; v < vCount && numVelvetPulses + 2 <= MAX_VELVET_PULSES; ++v) {
            float vOffsetL = (v + 1) * vIntervalSmp + (std::fmod(v * 2.71828f, 1.0f) - 0.5f) * vIntervalSmp * 0.5f;
            float vOffsetR = (v + 1) * vIntervalSmp + (std::fmod(v * 3.14159f, 1.0f) - 0.5f) * vIntervalSmp * 0.5f + (vIntervalSmp * 0.5f);

            float delayL = preDelaySamples + vOffsetL;
            float delayR = preDelaySamples + vOffsetR;
            if (delayL >= maxErDelaySamples && delayR >= maxErDelaySamples) break;

            float vSign = (v % 2 == 0) ? 1.0f : -1.0f;
            float vDecay = std::exp(-static_cast<float>(v) / 6.0f);

            if (delayL < maxErDelaySamples) {
                velvetPulses[numVelvetPulses].delaySample = static_cast<int32_t>(delayL);
                velvetPulses[numVelvetPulses].gainL = vSign * vDecay * 0.08f;
                velvetPulses[numVelvetPulses].gainR = 0.0f;
                ++numVelvetPulses;
            }
            if (delayR < maxErDelaySamples) {
                velvetPulses[numVelvetPulses].delaySample = static_cast<int32_t>(delayR);
                velvetPulses[numVelvetPulses].gainL = 0.0f;
                velvetPulses[numVelvetPulses].gainR = -vSign * vDecay * 0.08f;
                ++numVelvetPulses;
            }
        }
    }
    else if (algorithmIndex == 4 || algorithmIndex == 6) {
        // ─── Case 4, 6: 2D 鏡像法 (PLATE, GOLDFOIL) ───
        float Lx = (algorithmIndex == 4 ? 2.0f : 0.4f) * std::sqrt(sizeScale);
        float Ly = (algorithmIndex == 4 ? 1.0f : 0.3f) * std::sqrt(sizeScale);
        float c_plate = (algorithmIndex == 4) ? 550.0f : 420.0f;

        float sx = 0.38f * Lx, sy = 0.42f * Ly;
        float rx = 0.62f * Lx, ry = 0.58f * Ly;

        constexpr float earDist = 0.06f;
        float rxL = rx, ryL = ry - earDist;
        float rxR = rx, ryR = ry + earDist;

        float d0 = std::max(0.2f, std::sqrt((sx - rx)*(sx - rx) + (sy - ry)*(sy - ry)));

        int tapIdx = 0;
        for (int nx = -2; nx <= 2 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++nx) {
            for (int ny = -2; ny <= 2 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++ny) {
                if (nx == 0 && ny == 0) continue;
                int order = std::abs(nx) + std::abs(ny);
                if (order > 3) continue;

                float px = (nx % 2 == 0) ? (nx * Lx + sx) : (nx * Lx + (Lx - sx));
                float py = (ny % 2 == 0) ? (ny * Ly + sy) : (ny * Ly + (Ly - sy));

                float distL = std::sqrt((px-rxL)*(px-rxL) + (py-ryL)*(py-ryL));
                float distR = std::sqrt((px-rxR)*(px-rxR) + (py-ryR)*(py-ryR));

                float delaySmpL = preDelaySamples + (distL / c_plate) * fs;
                float delaySmpR = preDelaySamples + (distR / c_plate) * fs;

                if (delaySmpL < maxErDelaySamples) {
                    float ampL = std::sqrt(d0 / std::max(d0, distL)) * std::pow(0.88f, static_cast<float>(order)) * 0.7071f;
                    float filterCoefL = std::clamp(0.92f - (order * 0.08f * (1.0f + hfDamping)), 0.20f, 0.95f);
                    rawTaps.push_back({ delaySmpL, ampL, 0.0f, filterCoefL });
                }
                if (delaySmpR < maxErDelaySamples) {
                    float ampR = std::sqrt(d0 / std::max(d0, distR)) * std::pow(0.88f, static_cast<float>(order)) * 0.7071f;
                    float filterCoefR = std::clamp(0.92f - (order * 0.08f * (1.0f + hfDamping)), 0.20f, 0.95f);
                    rawTaps.push_back({ delaySmpR, 0.0f, ampR, filterCoefR });
                }
                ++tapIdx;
            }
        }
    }
    else if (algorithmIndex == 5) {
        // ─── Case 5: 1D デュアル・スプリング往復反射 (SPRING) ───
        float LL = 0.33f * sizeScale;
        float LR = 0.38f * sizeScale;
        float c_spring = 80.0f;
        float roundTripSecL = (2.0f * LL) / c_spring;
        float roundTripSecR = (2.0f * LR) / c_spring;
        float roundTripSmpL = roundTripSecL * fs;
        float roundTripSmpR = roundTripSecR * fs;

        for (int n = 1; n <= 5 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++n) {
            float delayL = preDelaySamples + n * roundTripSmpL;
            float delayR = preDelaySamples + n * roundTripSmpR;

            if (delayL < maxErDelaySamples) {
                float ampL = std::pow(0.80f, static_cast<float>(n)) * 0.7071f;
                float filterCoefL = std::clamp(0.88f - (n * 0.05f * (1.0f + hfDamping)), 0.10f, 0.90f);
                rawTaps.push_back({ delayL, ampL, 0.0f, filterCoefL });
            }
            if (delayR < maxErDelaySamples) {
                float ampR = std::pow(0.80f, static_cast<float>(n)) * 0.7071f;
                float filterCoefR = std::clamp(0.88f - (n * 0.05f * (1.0f + hfDamping)), 0.10f, 0.90f);
                rawTaps.push_back({ delayR, 0.0f, ampR, filterCoefR });
            }
        }
    }
    else if (algorithmIndex == 7) {
        // ─── Case 7: 短軸集中型 2D スライス ISM (INCHINDOWN - XZ 平面) ───
        float Lx = 9.0f * sizeScale;
        float Lz = 13.5f * sizeScale;
        float sx = 0.45f * Lx, sz = 0.40f * Lz;
        float rx = 0.55f * Lx, rz = 0.60f * Lz;

        constexpr float earDist = 0.10f;
        float rxL = rx - earDist, rzL = rz;
        float rxR = rx + earDist, rzR = rz;

        float d0 = std::max(0.5f, std::sqrt((sx - rx)*(sx - rx) + (sz - rz)*(sz - rz)));

        int tapIdx = 0;
        for (int nx = -3; nx <= 3 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++nx) {
            for (int nz = -2; nz <= 2 && rawTaps.size() + 2 <= MAX_ISM_TAPS; ++nz) {
                if (nx == 0 && nz == 0) continue;
                int order = std::abs(nx) + std::abs(nz);
                if (order > 3) continue;

                float px = (nx % 2 == 0) ? (nx * Lx + sx) : (nx * Lx + (Lx - sx));
                float pz = (nz % 2 == 0) ? (nz * Lz + sz) : (nz * Lz + (Lz - sz));

                float distL = std::sqrt((px-rxL)*(px-rxL) + (pz-rzL)*(pz-rzL));
                float distR = std::sqrt((px-rxR)*(px-rxR) + (pz-rzR)*(pz-rzR));

                float delaySmpL = preDelaySamples + (distL / c_sound) * fs;
                float delaySmpR = preDelaySamples + (distR / c_sound) * fs;

                if (delaySmpL < maxErDelaySamples) {
                    float ampL = (d0 / std::max(d0, distL)) * std::pow(0.96f, static_cast<float>(order)) * 0.7071f;
                    float filterCoefL = std::clamp(0.94f - (order * 0.04f * (1.0f + hfDamping)), 0.20f, 0.95f);
                    rawTaps.push_back({ delaySmpL, ampL, 0.0f, filterCoefL });
                }
                if (delaySmpR < maxErDelaySamples) {
                    float ampR = (d0 / std::max(d0, distR)) * std::pow(0.96f, static_cast<float>(order)) * 0.7071f;
                    float filterCoefR = std::clamp(0.94f - (order * 0.04f * (1.0f + hfDamping)), 0.20f, 0.95f);
                    rawTaps.push_back({ delaySmpR, 0.0f, ampR, filterCoefR });
                }
                ++tapIdx;
            }
        }
    }

    std::sort(rawTaps.begin(), rawTaps.end(), [](const RawTap& a, const RawTap& b) {
        return a.delaySamples < b.delaySamples;
    });

    int numTaps = std::min(static_cast<int>(rawTaps.size()), MAX_ISM_TAPS);
    outParams.activeTaps = numTaps;
    outParams.numActiveChunks = (numTaps + 7) / 8;

    for (int c = 0; c < NUM_ISM_CHUNKS; ++c) {
        auto& chunk = outParams.chunks[c];
        for (int i = 0; i < 8; ++i) {
            int tapIndex = c * 8 + i;
            if (tapIndex < numTaps) {
                float s = rawTaps[tapIndex].delaySamples;
                int32_t iPart = static_cast<int32_t>(std::floor(s));
                float fPart = s - static_cast<float>(iPart);

                chunk.delayInt[i]    = iPart;
                chunk.delayFrac[i]   = fPart;
                chunk.gainL[i]        = rawTaps[tapIndex].gainL;
                chunk.gainR[i]        = rawTaps[tapIndex].gainR;
                chunk.filterCoef[i]  = rawTaps[tapIndex].filterCoef;
                chunk.filterState[i] = 0.0f;
            } else {
                chunk.delayInt[i]    = 100;
                chunk.delayFrac[i]   = 0.0f;
                chunk.gainL[i]        = 0.0f;
                chunk.gainR[i]        = 0.0f;
                chunk.filterCoef[i]  = 0.5f;
                chunk.filterState[i] = 0.0f;
            }
        }
    }
}

void EarlyReflectionsISM::processBlock(const float* inL, const float* inR,
                                      float* earlyWetL, float* earlyWetR,
                                      float* clusterSeed, int numSamples, float erLevel)
{
    if (erLevel <= 0.001f && smoothedErLevel <= 0.001f) {
        return;
    }

    auto newParams = pendingParams.exchange(nullptr, std::memory_order_acq_rel);
    if (newParams) {
        currentParams = newParams;
    }

    if (!currentParams || currentParams->numActiveChunks == 0) return;

    const auto& chunks = currentParams->chunks;
    const int numChunks = currentParams->numActiveChunks;
    const __m256i maskReg = _mm256_set1_epi32(RING_BUFFER_MASK);

    for (int i = 0; i < numSamples; ++i) {
        smoothedErLevel += 0.01f * (erLevel - smoothedErLevel);

        float inSample = (inL[i] + inR[i]) * 0.5f;
        ringBufferL[writeIndex] = inSample;

        __m256 sumL = _mm256_setzero_ps();
        __m256 sumR = _mm256_setzero_ps();
        __m256i vWriteIdx = _mm256_set1_epi32(static_cast<int32_t>(writeIndex));

        for (int c = 0; c < numChunks; ++c) {
            auto& chunk = const_cast<ISMTapChunk&>(chunks[c]);

            __m256i vDelayInt = _mm256_load_si256(reinterpret_cast<const __m256i*>(chunk.delayInt));
            __m256i vReadIdx0 = _mm256_and_si256(_mm256_sub_epi32(vWriteIdx, vDelayInt), maskReg);

            __m256i vReadIdxM1 = _mm256_and_si256(_mm256_add_epi32(vReadIdx0, _mm256_set1_epi32(1)), maskReg);
            __m256i vReadIdx1  = _mm256_and_si256(_mm256_sub_epi32(vReadIdx0, _mm256_set1_epi32(1)), maskReg);
            __m256i vReadIdx2  = _mm256_and_si256(_mm256_sub_epi32(vReadIdx0, _mm256_set1_epi32(2)), maskReg);

            __m256 yM1 = _mm256_i32gather_ps(ringBufferL.data(), vReadIdxM1, 4);
            __m256 y0  = _mm256_i32gather_ps(ringBufferL.data(), vReadIdx0,  4);
            __m256 y1  = _mm256_i32gather_ps(ringBufferL.data(), vReadIdx1,  4);
            __m256 y2  = _mm256_i32gather_ps(ringBufferL.data(), vReadIdx2,  4);

            __m256 frac = _mm256_load_ps(chunk.delayFrac);
            __m256 c0 = y0;
            __m256 c1 = _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_sub_ps(y1, yM1));
            __m256 c2 = _mm256_sub_ps(_mm256_add_ps(yM1, y1), _mm256_mul_ps(_mm256_set1_ps(2.0f), y0));
            __m256 c3 = _mm256_mul_ps(_mm256_set1_ps(0.5f),
                _mm256_sub_ps(_mm256_sub_ps(y2, yM1), _mm256_mul_ps(_mm256_set1_ps(3.0f), _mm256_sub_ps(y1, y0))));

            __m256 interp = _mm256_fmadd_ps(c3, frac, c2);
            interp = _mm256_fmadd_ps(interp, frac, c1);
            interp = _mm256_fmadd_ps(interp, frac, c0);

            __m256 coef  = _mm256_load_ps(chunk.filterCoef);
            __m256 state = _mm256_load_ps(chunk.filterState);
            state = _mm256_fmadd_ps(coef, _mm256_sub_ps(interp, state), state);
            _mm256_store_ps(chunk.filterState, state);

            __m256 gL = _mm256_load_ps(chunk.gainL);
            __m256 gR = _mm256_load_ps(chunk.gainR);
            sumL = _mm256_fmadd_ps(state, gL, sumL);
            sumR = _mm256_fmadd_ps(state, gR, sumR);
        }

        // 自然で音楽的な初期反射スケーリング (ER-FDN 完全分離)
        float outL_sample = hsum_m256(sumL) * smoothedErLevel * 0.19f;
        float outR_sample = hsum_m256(sumR) * smoothedErLevel * 0.19f;

        if (currentParams->useVelvetNoise && numVelvetPulses > 0) {
            float vSumL = 0.0f, vSumR = 0.0f;
            for (int p = 0; p < numVelvetPulses; ++p) {
                int rIdx = (static_cast<int32_t>(writeIndex) - velvetPulses[p].delaySample) & RING_BUFFER_MASK;
                float vSample = ringBufferL[rIdx];
                vSumL += vSample * velvetPulses[p].gainL;
                vSumR += vSample * velvetPulses[p].gainR;
            }
            outL_sample += vSumL * currentParams->velvetGain * smoothedErLevel;
            outR_sample += vSumR * currentParams->velvetGain * smoothedErLevel;
        }

        earlyWetL[i] += outL_sample;
        earlyWetR[i] += outR_sample;

        if (clusterSeed != nullptr) {
            clusterSeed[i] += (outL_sample + outR_sample) * 0.7071f;
        }

        writeIndex = (writeIndex + 1) & RING_BUFFER_MASK;
    }
}

} // namespace FDNReverb
