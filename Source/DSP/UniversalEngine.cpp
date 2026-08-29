#include "UniversalEngine.h"
#include "../AlgorithmPresets.h"

namespace FDNReverb {

    static bool isPrime(int n) {
        if (n <= 1) return false;
        if (n <= 3) return true;
        if (n % 2 == 0 || n % 3 == 0) return false;
        for (int i = 5; i * i <= n; i += 6) {
            if (n % i == 0 || n % (i + 2) == 0) return false;
        }
        return true;
    }

    static int findNearestUniquePrime(int target, const std::array<int, 16>& used, int usedCount) {
        int offset = 0;
        while (true) {
            int candUp = target + offset;
            if (candUp > 1 && isPrime(candUp)) {
                bool clash = false;
                for (int u = 0; u < usedCount; ++u) {
                    if (used[u] == candUp) { clash = true; break; }
                }
                if (!clash) return candUp;
            }
            if (offset > 0) {
                int candDown = target - offset;
                if (candDown > 1 && isPrime(candDown)) {
                    bool clash = false;
                    for (int u = 0; u < usedCount; ++u) {
                        if (used[u] == candDown) { clash = true; break; }
                    }
                    if (!clash) return candDown;
                }
            }
            ++offset;
        }
    }

    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        ChorusLFO::initTable();

        constexpr float chorusRates[16] = {
            0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.43f, 0.47f,
            0.53f, 0.59f, 0.61f, 0.67f, 0.71f, 0.73f, 0.79f, 0.83f
        };
        for (int i = 0; i < FDN_ORDER; ++i) {
            chorusLFOs[i].rateScale = chorusRates[i];
            chorusLFOs[i].phase = static_cast<float>(i) / 16.0f;
            lfos[i].state = 12345u + static_cast<uint32_t>(i * 7919);
            lfos[i].rateMultiplier = chorusRates[i];
        }
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;
        ChorusLFO::initTable();

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

        acousticMetrics.prepare(sampleRate, 2000.0f);
        currentERTapCount = 0;
        currentERDelaySamples.fill(0.0f);
        currentERGains.fill(0.0f);
        outputLimiter.prepare(sampleRate);
        outputEQ.prepare(sampleRate);

        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.010f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.200f));
        duckingEnvelope = 0.0f;

        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        dcX1.fill(0.0f); dcY1.fill(0.0f);
        fdnRmsEnv.fill(0.0f);
        rmsCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.003f));

        reset();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        fbVec.fill(0.0f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (auto& lineFilters : absorptionFiltersS2)
            for (auto& f : lineFilters) f.reset();
#else
        for (auto& f : absorptionFilters) f.reset();
#endif

        acousticMetrics.reset();
        saturatorL.reset(); saturatorR.reset();
        outputLimiter.reset(); outputEQ.reset();
        duckingEnvelope = 0.0f;
        for (auto& chDelays : nestedAllpassDelays)
            for (auto& dl : chDelays) dl.resetState();
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        const bool algoChanged = (activeParams.algorithmIndex != p.algorithmIndex);
        activeParams = p;

        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room;     break;
        case 2: case 3: currentTopology = ReverbTopology::Hall;     break;
        case 4:         currentTopology = ReverbTopology::Plate;    break;
        case 5:         currentTopology = ReverbTopology::Spring;   break;
        case 6:         currentTopology = ReverbTopology::Goldfoil; break;
        }

        const float attMs = juce::jmax(0.1f, p.duckingAttackMs);
        const float relMs = juce::jmax(0.1f, p.duckingRelMs);
        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * attMs * 0.001f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * relMs * 0.001f));

        preDelaySamples = p.preDelayMs * 0.001f * static_cast<float>(fs);
        outputEQ.setLoCutHz(p.loCutHz);
        outputEQ.setHiCutHz(p.hiCutHz);

        if (algoChanged) {
            updateTopologyAndRouting();
            topologyUpdateCounter = 0;
        } else {
            topologyUpdatePending = true;
        }
    }

    void UniversalEngine::calculatePrimePowerDelays() {
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

    void UniversalEngine::updateTopologyAndRouting() {
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

        // ★ 物理音響学に基づく実空間大気減衰制約 (Physical Air Absorption Monotonicity)
        scaledRT60[7] = std::min(scaledRT60[7], scaledRT60[6] * 0.90f);
        scaledRT60[8] = std::min(scaledRT60[8], scaledRT60[7] * 0.75f);
        scaledRT60[9] = std::min(scaledRT60[9], scaledRT60[8] * 0.60f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        std::array<float, NUM_BANDS> targetDbAccum;
        targetDbAccum.fill(0.0f);

        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = MagnitudeResponseFitter::designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                currentAbsorptionCoeffsS2[i][b] = s2.geqStages[b];
                targetDbAccum[b] += s2.targetDb[b];
            }
        }

        const float representativeDelay = fdnBaseDelaySamples[FDN_ORDER / 2];
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float avgTargetDb = targetDbAccum[b] / static_cast<float>(FDN_ORDER);
            if (avgTargetDb < -0.001f) {
                effectiveRT60[b] = -60.0f * representativeDelay
                    / (static_cast<float>(fs) * avgTargetDb);
            }
            else {
                effectiveRT60[b] = scaledRT60[b];
            }
            effectiveRT60[b] = juce::jlimit(0.05f, 30.0f, effectiveRT60[b]);
        }
#else
        effectiveRT60 = scaledRT60;
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto absoStages = FilterDesign::designAbsorption(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            currentAbsorptionCoeffs[i] = absoStages[0];
        }
#endif

        float rt60Mid = 0.0f;
        for (int b = 2; b <= 7; ++b)
            rt60Mid += effectiveRT60[b];
        rt60Mid = std::max(0.1f, rt60Mid / 6.0f);

        microSatBlend = juce::jlimit(0.0f, 1.0f, 1.0f - (rt60Mid - 2.0f) / 4.0f);
        modDepthScale = 1.0f + juce::jlimit(0.0f, 2.0f, (rt60Mid - 1.0f) * 0.5f);

        constexpr float baseDB = 16.0f;
        float decayCompDB = 7.0f * std::log10(rt60Mid);

        static constexpr std::array<float, 7> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f
        };
        float algoOffset = algorithmOffsetDB[juce::jlimit(0, 6, activeParams.algorithmIndex)];

        switch (currentTopology) {
        case ReverbTopology::Room:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.40f;  diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.65f;  diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Plate:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.55f;  diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Spring:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.62f;  diffusionSensitivity = 0.7f;
            break;
        case ReverbTopology::Goldfoil:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.52f;  diffusionSensitivity = 0.8f;
            break;
        }

        const auto& erPattern = PRESET_ER_PATTERNS[
            juce::jlimit(0, 6, activeParams.algorithmIndex)];
        currentERTapCount = erPattern.numTaps;
        float erSizeScale = 0.5f + activeParams.roomSizeScale;
        for (int i = 0; i < erPattern.numTaps; ++i) {
            currentERDelaySamples[i] = erPattern.taps[i].delayMs * 0.001f
                * static_cast<float>(fs) * erSizeScale;
            currentERGains[i] = erPattern.taps[i].gain;
        }
        if (erPattern.numTaps == 0) bypassER = true;

        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:     edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:     edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:    edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:   edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil: edtCoeff = 0.85f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        float satMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:     satMultiplier = 0.90f; break;
        case ReverbTopology::Hall:     satMultiplier = 0.93f; break;
        case ReverbTopology::Plate:    satMultiplier = 1.00f; break;
        case ReverbTopology::Spring:   satMultiplier = 1.05f; break;
        case ReverbTopology::Goldfoil: satMultiplier = 1.02f; break;
        }

        float effectiveSatAmount = juce::jlimit(0.0f, 1.0f,
            activeParams.saturation * satMultiplier);
        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        const float totalMakeupDB = baseDB + decayCompDB + algoOffset;
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalMakeupDB);
    }

    inline void UniversalEngine::fastWalshHadamardTransform(std::array<float, 16>& a) noexcept {
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

    inline void UniversalEngine::applySignFlipping(std::array<float, 16>& v) noexcept {
        static constexpr float s[16] = {
            1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f
        };
        for (int i = 0; i < 16; ++i) v[i] *= s[i];
    }

    void UniversalEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR, int numSamples) noexcept {
        if (topologyUpdatePending) {
            if (++topologyUpdateCounter >= TOPOLOGY_UPDATE_INTERVAL) {
                updateTopologyAndRouting();
                topologyUpdatePending = false;
                topologyUpdateCounter = 0;
            }
        }
        const float fsf = static_cast<float>(fs);

        const float stereoWidth = activeParams.stereoWidth;
        const float erLevel = activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;

        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;

        // ★ 【Diffusion 劇的コントロール】0.0(完全粒状・スパーズ) 〜 1.0(超濃密シルキー)
        const float diff = activeParams.diffusion * diffusionSensitivity;
        const float diffuserGain = diff * 0.70f;
        const float effectiveApfGain = apfGain * std::pow(diff, 0.75f);
        const float apfGainStage = effectiveApfGain * 0.76f;
        const bool  skipInputDiffusers = (diff < 0.05f);

        const float sideBoost = stereoWidth * 1.5f;
        const float erLeakage = (1.0f - stereoWidth) * 0.7f;

        std::array<float, FDN_ORDER> freqModScales;
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i)
            freqModScales[i] = 0.6f + (1.0f - static_cast<float>(i) * invFdnM1) * 0.9f;

        std::array<float, 4> diffuserDelaySmp;
        for (int i = 0; i < 4; ++i)
            diffuserDelaySmp[i] = (3.0f + i * 2.0f) * 0.001f * fsf;

        // ★ 1k〜2kHz モードスミアリング最適化 Allpass (1.7ms, 2.8ms, 4.4ms)
        constexpr float apfBaseMs[SERIAL_APF_STAGES]   = { 1.7f, 2.8f, 4.4f };
        constexpr float apfModFrac[SERIAL_APF_STAGES]  = { 0.25f, 0.20f, 0.15f };
        const float msToSmp = 0.001f * fsf;
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> apfBaseDelaySmp;
        for (int i = 0; i < FDN_ORDER; ++i) {
            const float chFrac = static_cast<float>((i * 7) % 16) / 16.0f;
            for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                const float spreadMs = (s + 1) * 0.40f * chFrac;
                apfBaseDelaySmp[i][s] = (apfBaseMs[s] + spreadMs) * msToSmp;
            }
        }

        std::array<float, MAX_ER_TAPS> erTapGainsHalf;
        for (int t = 0; t < currentERTapCount; ++t)
            erTapGainsHalf[t] = currentERGains[t] * 0.5f;

        constexpr float compThresh = 0.35f;
        constexpr float compThreshSq = compThresh * compThresh;

        const float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);

        for (int n = 0; n < numSamples; ++n) {
            // ★ サンプル単位でのパラメータスムージング (ノブ回転時のクリック・ジッパー完全根絶)
            smoothedModAmount += (activeParams.modAmount - smoothedModAmount) * 0.005f;
            smoothedModRate   += (activeParams.modRate - smoothedModRate)     * 0.005f;

            for (int i = 0; i < FDN_ORDER; ++i) {
                chorusLFOs[i].phaseInc = smoothedModRate * chorusLFOs[i].rateScale / fsf;
            }

            const float modAmtCurved = smoothedModAmount * smoothedModAmount;
            const float depthSamples = modAmtCurved * 0.0035f * fsf * modDepthScale;

            preDelayLineL.write(inL[n]);
            preDelayLineR.write(inR[n]);
            const float delayedL = (preDelaySamples > 0.5f) ? preDelayLineL.read(preDelaySamples) : inL[n];
            const float delayedR = (preDelaySamples > 0.5f) ? preDelayLineR.read(preDelaySamples) : inR[n];

            const float midIn = (delayedL + delayedR) * 0.5f;
            const float sideIn = (delayedL - delayedR) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            const float inputPeak = juce::jmax(std::abs(inL[n]), std::abs(inR[n]));
            const float envCoeff = (inputPeak > duckingEnvelope) ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                const float envDB = 20.0f * std::log10(juce::jmax(duckingEnvelope, 1e-6f));
                const float overDB = envDB - activeParams.duckingThreshDB;
                const float gainRedDB = -juce::jmin(overDB, duckAmountDB);
                duckGainLinear = juce::Decibels::decibelsToGain(gainRedDB);
            }

            float fdnInputMid = midIn;
            if (!skipInputDiffusers && !bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float d = inputDiffusers[i].read(diffuserDelaySmp[i]);
                    float w = fdnInputMid + diffuserGain * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - diffuserGain * w;
                }
            }

            if (!bypassER) {
                erDelay.write(midIn);
                float erTotalL = 0.0f, erTotalR = 0.0f;
                for (int t = 0; t < currentERTapCount; ++t) {
                    const float tapValue = erDelay.read(currentERDelaySamples[t]);
                    const float tapGain = erTapGainsHalf[t];
                    const float tg = tapValue * tapGain;
                    const float tgLeak = tg * erLeakage;
                    if (t % 2 == 0) {
                        erTotalL += tg;
                        erTotalR += tgLeak;
                    }
                    else {
                        erTotalR += tg;
                        erTotalL += tgLeak;
                    }
                }
                erOutL = erTotalL;
                erOutR = erTotalR;
            }

            if (!bypassER) {
                fdnInputMid += (erOutL + erOutR) * 0.5f * 0.15f;
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float chorusVal = chorusLFOs[i].tick();
                
                // メインディレイは完全固定 (変調ゼロ: ノイズ・ピッチ揺れ完全ゼロ)
                const float delaySmp = fdnBaseDelaySamples[i];
                float d = fdnDelays[i].read(delaySmp);

#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                d += 1e-25f;

                {
                    const float dcIn = d;
                    const float dcOut = dcIn - dcX1[i] + dcBlockerCoeff * dcY1[i];
                    dcX1[i] = dcIn;
                    dcY1[i] = dcOut;
                    d = dcOut;
                }

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

                // ★ 【完全ノイズフリー】Schroeder 標準型 Modulated Allpass
                float apfOut = d;
                if (apfGainStage > 0.001f) {
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const float baseDelay = apfBaseDelaySmp[i][s];
                        const float maxSafeMod = baseDelay * 0.40f;
                        const float targetMod = depthSamples * apfModFrac[s] * freqModScales[i];
                        const float safeMod = std::min(targetMod, maxSafeMod);
                        const float apfDelaySmp = baseDelay + chorusVal * safeMod;

                        const float delayed = nestedAllpassDelays[i][s].read(apfDelaySmp);
                        const float v = apfOut - apfGainStage * delayed;
                        nestedAllpassDelays[i][s].write(v);
                        apfOut = delayed + apfGainStage * v;
                    }
                }

                nextFb[i] = apfOut;

                const float sideForCh = (i % 2 == 0 ? +sideIn : -sideIn) * sideBoost;
                const float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                const float crossLeak = 1.0f - stereoWidth;
                if (i % 2 == 0) {
                    fdnOutL += apfOut;
                    fdnOutR += apfOut * crossLeak;
                }
                else {
                    fdnOutR += apfOut;
                    fdnOutL += apfOut * crossLeak;
                }
            }

            fdnOutL *= 0.125f;
            fdnOutR *= 0.125f;
            fbVec = nextFb;

            const float erMakeupGain = 4.0f;
            const float erMixL = bypassER ? 0.0f : erOutL * erLevel * erMakeupGain;
            const float erMixR = bypassER ? 0.0f : erOutR * erLevel * erMakeupGain;
            const float lateMixL = fdnOutL * lateMakeupGainLinear * lateLevel;
            const float lateMixR = fdnOutR * lateMakeupGainLinear * lateLevel;

            acousticMetrics.processSample((lateMixL + lateMixR) * 0.5f);

            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            if (erSolo) { satL = 0.0f; satR = 0.0f; }

            float wetL = erMixL + satL;
            float wetR = erMixR + satR;

            outputEQ.process(wetL, wetR);

            const float finalWetGain = wetGain * duckGainLinear;
            outL[n] = wetL * finalWetGain;
            outR[n] = wetR * finalWetGain;

            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb