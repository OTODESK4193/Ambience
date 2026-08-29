#include "UniversalEngine_v121.h"
#include "../Source/AlgorithmPresets.h"

namespace FDNReverb::V121 {

    static int findNearestUniquePrime(int target, const std::array<int, 16>& used, int count) {
        auto isPrime = [](int n) {
            if (n < 2) return false;
            if (n == 2 || n == 3) return true;
            if (n % 2 == 0 || n % 3 == 0) return false;
            for (int i = 5; i * i <= n; i += 6)
                if (n % i == 0 || n % (i + 2) == 0) return false;
            return true;
        };
        for (int offset = 0; offset < 2000; ++offset) {
            for (int sign : { 1, -1 }) {
                int cand = target + sign * offset;
                if (cand < 11) continue;
                if (!isPrime(cand)) continue;
                bool duplicate = false;
                for (int i = 0; i < count; ++i) {
                    if (used[i] == cand) { duplicate = true; break; }
                }
                if (!duplicate) return cand;
            }
        }
        return target;
    }

    UniversalEngineUpdate::UniversalEngineUpdate() {
        ChorusLFO::initTable();
        constexpr float chorusRates[16] = {
            0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.43f, 0.47f,
            0.53f, 0.59f, 0.61f, 0.67f, 0.71f, 0.73f, 0.79f, 0.83f
        };
        for (int i = 0; i < 16; ++i) {
            chorusLFOs[i].rateScale = chorusRates[i];
            chorusLFOs[i].phase = static_cast<float>(i) / 16.0f;
        }
        dcX1.fill(0.0f); dcY1.fill(0.0f);
        fdnRmsEnv.fill(0.0f); fbVec.fill(0.0f);
    }

    void UniversalEngineUpdate::prepare(double sampleRate, int maxBlockSize) {
        fs = sampleRate;

        auto getPow2 = [](size_t n) -> size_t {
            size_t p = 1;
            while (p < n) p <<= 1;
            return p;
        };

        size_t totalMemoryNeeded = 0;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.5)) * 2;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.1));
        for (int i = 0; i < 4; ++i)
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05));
        for (int i = 0; i < FDN_ORDER; ++i) {
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.5));
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05));
        }

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineL.init(ptr, mask);
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineR.init(ptr, mask);

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.1), mask);
        erDelay.init(ptr, mask);

        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusers[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
            fdnDelays[i].init(ptr, mask);
            for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
                nestedAllpassDelays[i][s].init(ptr, mask);
            }
        }

        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        rmsCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.003f));

        reset();
    }

    void UniversalEngineUpdate::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f); dcX1.fill(0.0f); dcY1.fill(0.0f); fdnRmsEnv.fill(0.0f);
        for (auto& s : absorptionFiltersS2) for (auto& b : s) b.reset();
        for (auto& ch : nestedAllpassDelays) for (auto& dl : ch) dl.resetState();
    }

    void UniversalEngineUpdate::setParams(const DSPParams& p) {
        activeParams = p;
        preDelaySamples = p.preDelayMs * 0.001f * static_cast<float>(fs);
        updateTopologyAndRouting();
    }

    void UniversalEngineUpdate::calculatePrimePowerDelays() {
        const float fsf = static_cast<float>(fs);
        const float sizeCoeff = juce::jlimit(0.5f, 2.0f, activeParams.roomSizeScale + 1.0f);
        const float minDelayMs = 15.0f + sizeCoeff * 7.5f;
        const float maxDelayMs = 50.0f + sizeCoeff * 75.0f;
        const int minDelaySamples = std::max(11, static_cast<int>(minDelayMs * 0.001f * fsf));
        const int maxDelaySamples = static_cast<int>(maxDelayMs * 0.001f * fsf);

        const float logMin = std::log(static_cast<float>(minDelaySamples));
        const float logMax = std::log(static_cast<float>(maxDelaySamples));

        std::array<int, FDN_ORDER> usedPrimes;
        usedPrimes.fill(0);

        for (int i = 0; i < FDN_ORDER; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(FDN_ORDER - 1);
            const float logTgt = logMin + t * (logMax - logMin);
            const int   target = static_cast<int>(std::round(std::exp(logTgt)));
            const int   prime = findNearestUniquePrime(target, usedPrimes, i);
            usedPrimes[i] = prime;
            fdnBaseDelaySamples[i] = static_cast<float>(prime);
        }
    }

    void UniversalEngineUpdate::updateTopologyAndRouting() {
        calculatePrimePowerDelays();
        auto& preset = *ALL_PRESETS[activeParams.algorithmIndex];

        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        scaledRT60[0] *= activeParams.tiltLow;
        scaledRT60[1] *= activeParams.tiltLow;
        scaledRT60[2] *= activeParams.tiltLow;
        scaledRT60[3] *= activeParams.tiltMid;
        scaledRT60[4] *= activeParams.tiltMid;
        scaledRT60[5] *= activeParams.tiltMid;
        scaledRT60[6] *= activeParams.tiltMid;
        scaledRT60[7] *= activeParams.tiltHigh;
        scaledRT60[8] *= activeParams.tiltHigh;
        scaledRT60[9] *= activeParams.tiltHigh;
        for (int b = 0; b < NUM_BANDS; ++b)
            scaledRT60[b] *= activeParams.rtBands[b];

        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
            }
        }
    }

    inline void UniversalEngineUpdate::fastWalshHadamardTransform(std::array<float, 16>& a) noexcept {
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    const float x = a[j];
                    const float y = a[j + h];
                    a[j]     = x + y;
                    a[j + h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) a[i] *= 0.25f;
    }

    inline void UniversalEngineUpdate::applySignFlipping(std::array<float, 16>& v) noexcept {
        static constexpr float s[16] = {
            1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f
        };
        for (int i = 0; i < 16; ++i) v[i] *= s[i];
    }

    void UniversalEngineUpdate::processBlock(const float* inL, const float* inR,
        float* outL, float* outR, int numSamples) noexcept {
        
        const float fsf = static_cast<float>(fs);
        const float targetModAmount = activeParams.modAmount;
        const float stereoWidth = activeParams.stereoWidth;
        const float sideBoost = stereoWidth * 1.5f;

        const float effectiveDiffusion = activeParams.diffusion;
        const float diffuserGain = effectiveDiffusion * 0.70f;
        const float effectiveApfGain = apfGain * (0.05f + effectiveDiffusion * 0.75f);
        const float apfGainStage = effectiveApfGain * 0.78f;

        std::array<float, FDN_ORDER> freqModScales;
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i)
            freqModScales[i] = 0.5f + (1.0f - static_cast<float>(i) * invFdnM1) * 1.0f;

        std::array<float, 4> diffuserDelaySmp;
        for (int i = 0; i < 4; ++i)
            diffuserDelaySmp[i] = (3.0f + i * 2.0f) * 0.001f * fsf;

        constexpr float apfBaseMs[SERIAL_APF_STAGES]   = { 1.5f, 2.3f, 3.7f };
        constexpr float apfSpreadMs[SERIAL_APF_STAGES] = { 0.73f, 0.91f, 1.13f };
        constexpr float apfModFrac[SERIAL_APF_STAGES]  = { 0.15f, 0.10f, 0.07f };
        const float msToSmp = 0.001f * fsf;
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> apfBaseDelaySmp;
        for (int i = 0; i < FDN_ORDER; ++i)
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                apfBaseDelaySmp[i][s] = (apfBaseMs[s] + i * apfSpreadMs[s]) * msToSmp;

        constexpr float compThresh = 0.35f;
        constexpr float compThreshSq = compThresh * compThresh;

        for (int i = 0; i < FDN_ORDER; ++i) {
            chorusLFOs[i].phaseInc = activeParams.modRate * chorusLFOs[i].rateScale / fsf;
        }

        const bool isSparseMode = (effectiveDiffusion < 0.25f);

        for (int n = 0; n < numSamples; ++n) {
            smoothedModAmount += (targetModAmount - smoothedModAmount) * 0.005f;
            const float modAmtCurved = smoothedModAmount * smoothedModAmount;
            const float depthSamples = modAmtCurved * 0.006f * fsf * modDepthScale;

            preDelayLineL.write(inL[n]);
            preDelayLineR.write(inR[n]);
            const float delayedL = (preDelaySamples > 0.5f) ? preDelayLineL.read(preDelaySamples) : inL[n];
            const float delayedR = (preDelaySamples > 0.5f) ? preDelayLineR.read(preDelaySamples) : inR[n];

            const float midIn = (delayedL + delayedR) * 0.5f;
            const float sideIn = (delayedL - delayedR) * 0.5f;

            float fdnInputMid = midIn;
            if (effectiveDiffusion > 0.05f) {
                for (int i = 0; i < 4; ++i) {
                    float d = inputDiffusers[i].read(diffuserDelaySmp[i]);
                    float w = fdnInputMid + diffuserGain * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - diffuserGain * w;
                }
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float chorusVal = chorusLFOs[i].tick();

                // ★ メインディレイは変調しない（固定）
                const float delaySmp = fdnBaseDelaySamples[i];
                float d = fdnDelays[i].read(delaySmp);

                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);

                d += 1e-25f;

                // DC Blocker
                {
                    const float dcIn = d;
                    const float dcOut = dcIn - dcX1[i] + dcBlockerCoeff * dcY1[i];
                    dcX1[i] = dcIn;
                    dcY1[i] = dcOut;
                    d = dcOut;
                }

                // Compressor
                {
                    fdnRmsEnv[i] += (d * d - fdnRmsEnv[i]) * rmsCoeff;
                    if (fdnRmsEnv[i] > compThreshSq) {
                        const float env = std::sqrt(fdnRmsEnv[i]);
                        const float over = env - compThresh;
                        d *= compThresh / (compThresh + over * 0.65f);
                    }
                }

                if (microSatBlend > 0.001f) {
                    const float sat = processMicroSaturation(d);
                    d = d + (sat - d) * microSatBlend;
                }

                // ★ Allpassのみを変調
                float apfOut = d;
                {
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const float apfModDepth = depthSamples * apfModFrac[s] * 2.0f;
                        const float apfDelaySmp = apfBaseDelaySmp[i][s]
                            + chorusVal * apfModDepth * freqModScales[i];
                        float apfD = nestedAllpassDelays[i][s].read(apfDelaySmp);
                        float apfW = apfOut + apfGainStage * apfD;
                        nestedAllpassDelays[i][s].write(apfW);
                        apfOut = apfD - apfGainStage * apfW;
                    }
                }

                nextFb[i] = apfOut;

                const float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * sideBoost;
                float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;

                // ★ Diffusion連動: Diff<0.25で間引き
                if (isSparseMode) {
                    if (i % 4 == 0) fdnInputForThisCh *= 4.0f;
                    else fdnInputForThisCh = 0.0f;
                }

                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                const float crossLeak = 1.0f - stereoWidth;
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * crossLeak;
                } else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * crossLeak;
                }
            }

            fbVec = nextFb;
            outL[n] = fdnOutL * 0.25f;
            outR[n] = fdnOutR * 0.25f;
        }
    }

} // namespace FDNReverb::V121
