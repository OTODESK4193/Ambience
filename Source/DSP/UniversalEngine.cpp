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
        DualGoldenLFO::initTable();

        constexpr float chorusRates[16] = {
            0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.43f, 0.47f,
            0.53f, 0.59f, 0.61f, 0.67f, 0.71f, 0.73f, 0.79f, 0.83f
        };
        for (int i = 0; i < FDN_ORDER; ++i) {
            dualLFOs[i].rateScale = chorusRates[i];
            dualLFOs[i].phase1 = static_cast<float>(i) / 16.0f;
            dualLFOs[i].phase2 = std::fmod(static_cast<float>(i) * 0.6180339887f, 1.0f);
        }
        erLpfState.fill(0.0f);
        inLpfStateL = 0.0f;
        inLpfStateR = 0.0f;
        inputTransientEnvFast = 0.0f;
        inputTransientEnvSlow = 0.0f;
    }

    struct DelayBounds {
        float minDelayMs;
        float maxDelayMs;
    };

    static constexpr std::array<DelayBounds, NUM_ALGORITHMS> ALGORITHM_DELAY_BOUNDS = { {
        // ROOM1: 40 m3, V^(1/3) ~ 3.4m, mean path ~ 2.3m
        { 7.5f, 34.0f },
        // ROOM2: 100 m3, V^(1/3) ~ 4.6m, mean path ~ 3.1m
        { 11.0f, 52.0f },
        // HALL1: 2,000 m3, V^(1/3) ~ 12.6m, mean path ~ 8.4m
        { 18.0f, 88.0f },
        // HALL2: 12,000 m3, V^(1/3) ~ 22.9m, mean path ~ 15.5m
        { 26.0f, 135.0f },
        // PLATE: EMT 140 (2D Steel Plate, High Density)
        { 5.5f, 38.0f },
        // SPRING: Vintage Tank (Dispersive Dispersion)
        { 8.0f, 55.0f },
        // GOLDFOIL: EMT 240 (Ultra-thin Gold Foil)
        { 4.8f, 30.0f },
        // INCHINDOWN: 125,000 m3, Length 237m (Colossal Underground Tank)
        { 28.0f, 330.0f }
    } };

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;
        DualGoldenLFO::initTable();

        auto getPow2 = [](size_t s) {
            size_t p = 16;
            while (p < s) p <<= 1;
            return p;
        };

        size_t totalMemoryNeeded = 0;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.5)) * 2;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 1.0)); // ★ ER 遅延線を 1.0s 確保 (Inchindown 831ms対応)
        for (int i = 0; i < 4; ++i)
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05)) * 2; // Mid & Side
        for (int i = 0; i < FDN_ORDER; ++i) {
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 1.0)); // ★ 1.0s 確保 (Inchindown最大660ms対応)
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05));
        }

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineL.init(ptr, mask);
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineR.init(ptr, mask);

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask); // ★ 1.0s 確保
        erDelay.init(ptr, mask);

        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusersM[i].init(ptr, mask);
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusersS[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask); // ★ 1.0s 確保
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
        erLpfState.fill(0.0f);

        outputLimiter.prepare(sampleRate);
        outputEQ.prepare(sampleRate);

        const float fsf = static_cast<float>(fs);
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i) {
            cachedFreqModScales[i] = 0.6f + (1.0f - static_cast<float>(i) * invFdnM1) * 0.9f;
            dualLfoIncScale1[i] = dualLFOs[i].rateScale / fsf;
            dualLfoIncScale2[i] = (dualLFOs[i].rateScale * 0.6180339887f) / fsf;
        }

        for (int i = 0; i < 4; ++i) {
            cachedDiffuserDelaySmpM[i] = (3.0f + i * 2.0f) * 0.001f * fsf;
            cachedDiffuserDelaySmpS[i] = (3.5f + i * 2.3f) * 0.001f * fsf;
        }

        constexpr float apfBaseMs[SERIAL_APF_STAGES]   = { 1.7f, 2.8f, 4.4f };
        const float msToSmp = 0.001f * fsf;
        for (int i = 0; i < FDN_ORDER; ++i) {
            const float chFrac = static_cast<float>((i * 7) % 16) / 16.0f;
            for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                const float spreadMs = (s + 1) * 0.40f * chFrac;
                cachedApfBaseDelaySmp[i][s] = (apfBaseMs[s] + spreadMs) * msToSmp;
            }
        }

        // ★ 入力段 Bandwidth LPF (12kHz 1次ローパス)
        inBandwidthCoeff = 1.0f - std::exp(-6.2831853f * 12000.0f / fsf);

        duckingAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.010f));
        duckingReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.200f));
        duckingEnvelope = 0.0f;

        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        dcX1.fill(0.0f); dcY1.fill(0.0f);

        // ★ ループ内 ユニタリ・エネルギー正規化 AGC 時定数 (Attack: 1ms, Release: 100ms)
        loopAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.001f));
        loopReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.100f));
        loopEnergyEnv = 0.0f;

        fitter.precomputeInteractionMatrix(fs);
        isPreparedFlag = true;

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
        loopEnergyEnv = 0.0f;
        for (auto& chDelays : nestedAllpassDelays)
            for (auto& dl : chDelays) dl.resetState();
        erLpfState.fill(0.0f);

        inLpfStateL = 0.0f;
        inLpfStateR = 0.0f;
        inputTransientEnvFast = 0.0f;
        inputTransientEnvSlow = 0.0f;
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        const bool algoChanged = (activeParams.algorithmIndex != p.algorithmIndex);
        activeParams = p;

        switch (p.algorithmIndex) {
        case 0: case 1: currentTopology = ReverbTopology::Room;       break;
        case 2: case 3: currentTopology = ReverbTopology::Hall;       break;
        case 4:         currentTopology = ReverbTopology::Plate;      break;
        case 5:         currentTopology = ReverbTopology::Spring;     break;
        case 6:         currentTopology = ReverbTopology::Goldfoil;   break;
        case 7:         currentTopology = ReverbTopology::Inchindown; break;
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
        const int safeAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex);
        const auto& bounds = ALGORITHM_DELAY_BOUNDS[safeAlgo];

        // RoomSize ノブ (0.3 ~ 2.0) による線形スケーリング
        const float sizeScale = juce::jlimit(0.3f, 2.5f, activeParams.roomSizeScale);

        const float minDelayMs = bounds.minDelayMs * sizeScale;
        const float maxDelayMs = bounds.maxDelayMs * sizeScale;

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

        const int safeAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex);
        auto& preset = *ALL_PRESETS[safeAlgo];

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

        // 大気減衰
        scaledRT60[7] = std::min(scaledRT60[7], scaledRT60[6] * 0.90f);
        scaledRT60[8] = std::min(scaledRT60[8], scaledRT60[7] * 0.75f);
        scaledRT60[9] = std::min(scaledRT60[9], scaledRT60[8] * 0.60f);

#if AMBIENCE_USE_STAGE2_ABSORPTION
        std::array<float, NUM_BANDS> targetDbAccum;
        targetDbAccum.fill(0.0f);

        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = fitter.designStage2(
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
            effectiveRT60[b] = juce::jlimit(0.05f, 150.0f, effectiveRT60[b]);
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

        modDepthScale = 1.0f + juce::jlimit(0.0f, 2.0f, (rt60Mid - 1.0f) * 0.5f);

        constexpr float baseDB = 5.0f;
        // 短いRT60(<1s)のみ軽微に補正。長いRT60(>2s)はエネルギーが自然蓄積するため追加ブースト不要
        float decayCompDB = (rt60Mid < 1.0f) ? (2.5f * std::log10(rt60Mid)) : 0.0f;
        decayCompDB = juce::jlimit(-4.0f, 0.0f, decayCompDB);

        static constexpr std::array<float, 8> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f, +5.5f
        };
        float algoOffset = algorithmOffsetDB[juce::jlimit(0, 7, activeParams.algorithmIndex)];

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
        case ReverbTopology::Inchindown:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.58f;  diffusionSensitivity = 1.0f;
            break;
        }

        // ★ 【ER 音響壁面減衰 & 立体パンニング設計】
        const auto& erPattern = PRESET_ER_PATTERNS[
            juce::jlimit(0, 7, activeParams.algorithmIndex)];
        currentERTapCount = erPattern.numTaps;
        float erSizeScale = 0.5f + activeParams.roomSizeScale;

        static constexpr float kPans[12] = {
            -0.75f, +0.70f, -0.45f, +0.85f, -0.80f, +0.35f,
            -0.60f, +0.65f, -0.30f, +0.40f, -0.50f, +0.55f
        };

        const float maxSafeERDelaySamples = static_cast<float>(fs * 0.95);
        for (int i = 0; i < erPattern.numTaps; ++i) {
            const float delaySmp = erPattern.taps[i].delayMs * 0.001f
                * static_cast<float>(fs) * erSizeScale;
            currentERDelaySamples[i] = juce::jlimit(1.0f, maxSafeERDelaySamples, delaySmp);
            currentERGains[i] = erPattern.taps[i].gain;

            // 方位角パンニング (幾何学的反射)
            const float pan = kPans[i % 12];
            currentERPanL[i] = std::cos(0.785398f * (pan + 1.0f));
            currentERPanR[i] = std::sin(0.785398f * (pan + 1.0f));

            // 壁面吸収 1次 LPF 係数 (反射回数 i に応じて高域ダンピング)
            const float cutoffHz = 8000.0f * std::pow(0.91f, static_cast<float>(i));
            currentERLpfCoeff[i] = 1.0f - std::exp(-6.2831853f * cutoffHz / static_cast<float>(fs));
        }
        if (erPattern.numTaps == 0) bypassER = true;

        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:       edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:       edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:      edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:     edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil:   edtCoeff = 0.85f; break;
        case ReverbTopology::Inchindown: edtCoeff = 1.00f; break;
        }
        theoreticalEDT = rt60Mid * edtCoeff;

        float satMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:       satMultiplier = 0.90f; break;
        case ReverbTopology::Hall:       satMultiplier = 0.93f; break;
        case ReverbTopology::Plate:      satMultiplier = 1.00f; break;
        case ReverbTopology::Spring:     satMultiplier = 1.05f; break;
        case ReverbTopology::Goldfoil:   satMultiplier = 1.02f; break;
        case ReverbTopology::Inchindown: satMultiplier = 0.90f; break;
        }

        float effectiveSatAmount = juce::jlimit(0.0f, 1.0f,
            activeParams.saturation * satMultiplier);
        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);
        saturatorL.setMode(activeParams.satTypeIdx);
        saturatorR.setMode(activeParams.satTypeIdx);

        const float totalMakeupDB = juce::jlimit(-6.0f, 12.0f, baseDB + decayCompDB + algoOffset);
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
        if (!isPreparedFlag || inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) {
            if (outL && numSamples > 0) std::fill(outL, outL + numSamples, 0.0f);
            if (outR && numSamples > 0) std::fill(outR, outR + numSamples, 0.0f);
            return;
        }

        if (topologyUpdatePending) {
            if (++topologyUpdateCounter >= TOPOLOGY_UPDATE_INTERVAL) {
                updateTopologyAndRouting();
                topologyUpdatePending = false;
                topologyUpdateCounter = 0;
            }
        }
        const float fsf = static_cast<float>(fs);

        const float stereoWidth = activeParams.stereoWidth;
        
        // ★ ERLevel の二乗カーブ (0.0 で完全無音、0.5 で自然、1.0 で明瞭な部屋鳴り)
        const float erGainCurved = activeParams.erLevel * activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;

        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;
        const float maxDuckReductionGain = (duckAmountDB > 0.001f)
            ? juce::Decibels::decibelsToGain(-duckAmountDB) : 1.0f;

        const float diff = activeParams.diffusion * diffusionSensitivity;
        const float diffuserGain = diff * 0.70f;
        const float effectiveApfGain = apfGain * std::pow(diff, 0.75f);
        const float apfGainStage = effectiveApfGain * 0.76f;
        const bool  skipInputDiffusers = (diff < 0.05f);

        const float sideBoost = stereoWidth * 1.5f;
        constexpr float apfModFrac[SERIAL_APF_STAGES] = { 0.25f, 0.20f, 0.15f };

        for (int n = 0; n < numSamples; ++n) {
            smoothedModAmount += (activeParams.modAmount - smoothedModAmount) * 0.005f;
            smoothedModRate   += (activeParams.modRate - smoothedModRate)     * 0.005f;

            // ★ デュアル黄金比 LFO レート設定 (除算完全排除・事前計算スケール乗算)
            for (int i = 0; i < FDN_ORDER; ++i) {
                dualLFOs[i].phaseInc1 = smoothedModRate * dualLfoIncScale1[i];
                dualLFOs[i].phaseInc2 = smoothedModRate * dualLfoIncScale2[i];
            }

            const float modAmtCurved = smoothedModAmount * smoothedModAmount;
            const float depthSamples = modAmtCurved * 0.0035f * fsf * modDepthScale;

            preDelayLineL.write(inL[n]);
            preDelayLineR.write(inR[n]);
            const float delayedL = (preDelaySamples > 0.5f) ? preDelayLineL.read(preDelaySamples) : inL[n];
            const float delayedR = (preDelaySamples > 0.5f) ? preDelayLineR.read(preDelaySamples) : inR[n];

            // ★ 入力段 Bandwidth 1次 LPF (12kHz)
            inLpfStateL += inBandwidthCoeff * (delayedL - inLpfStateL);
            inLpfStateR += inBandwidthCoeff * (delayedR - inLpfStateR);
            float midIn = (inLpfStateL + inLpfStateR) * 0.5f;
            float sideIn = (inLpfStateL - inLpfStateR) * 0.5f;

            // ★ 入力過大ピークのソフトリミッティング (ステップ段差のない C1 連続処理)
            const float inputLevel = std::max(std::abs(midIn), std::abs(sideIn));
            if (inputLevel > 1.2f) {
                const float scale = 1.2f / inputLevel;
                midIn *= scale;
                sideIn *= scale;
            }

            float erOutL = 0.0f, erOutR = 0.0f;

            const float inputPeak = juce::jmax(std::abs(inL[n]), std::abs(inR[n]));
            const float envCoeff = (inputPeak > duckingEnvelope) ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            // ★ ダッキング超越関数 (std::log10 & pow) 完全排除・代数的高速化
            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                const float ratioGain = duckThreshLin / duckingEnvelope;
                duckGainLinear = std::max(ratioGain, maxDuckReductionGain);
            }

            // ★ Mid / Side 双方を 4段ディフューザーで完全拡散 (左右のアタックの角を溶かす)
            float fdnInputMid = midIn;
            float fdnInputSide = sideIn;
            if (!skipInputDiffusers && !bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float dm = inputDiffusersM[i].read(cachedDiffuserDelaySmpM[i]);
                    float wm = fdnInputMid + diffuserGain * dm;
                    inputDiffusersM[i].write(wm);
                    fdnInputMid = dm - diffuserGain * wm;

                    float ds = inputDiffusersS[i].read(cachedDiffuserDelaySmpS[i]);
                    float ws = fdnInputSide + diffuserGain * ds;
                    inputDiffusersS[i].write(ws);
                    fdnInputSide = ds - diffuserGain * ws;
                }
            }

            // ★ 【ER 音響壁面吸収 ＆ 立体パンニング処理】
            if (!bypassER) {
                erDelay.write(midIn);
                for (int t = 0; t < currentERTapCount; ++t) {
                    const float tapValue = erDelay.read(currentERDelaySamples[t]);
                    // 壁面吸収 1次 LPF
                    erLpfState[t] += currentERLpfCoeff[t] * (tapValue - erLpfState[t]);
                    const float filteredTap = erLpfState[t] * currentERGains[t];

                    erOutL += filteredTap * currentERPanL[t];
                    erOutR += filteredTap * currentERPanR[t];
                }
            }

            if (!bypassER) {
                fdnInputMid += (erOutL + erOutR) * 0.5f * 0.15f;
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float evenSum = 0.0f, oddSum = 0.0f;
            std::array<float, 16> nextFb;
            std::array<float, 16> apfOutVec;
            float maxChPeak = 0.0f;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float chorusVal = dualLFOs[i].tick();
                
                // ★ FDNベースディレイの高速整数リード (Hermite多項式補間バイパス)
                const int delaySmpInt = static_cast<int>(fdnBaseDelaySamples[i]);
                float d = fdnDelays[i].readInt(delaySmpInt);

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

                // Schroeder Modulated Allpass (B023 標準安全変調)
                float apfOut = d;
                if (apfGainStage > 0.001f) {
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const float baseDelay = cachedApfBaseDelaySmp[i][s];
                        const float maxSafeMod = baseDelay * 0.40f;
                        const float targetMod = depthSamples * apfModFrac[s] * cachedFreqModScales[i];
                        const float safeMod = std::min(targetMod, maxSafeMod);
                        const float apfDelaySmp = baseDelay + chorusVal * safeMod;

                        const float delayed = nestedAllpassDelays[i][s].read(apfDelaySmp);
                        const float v = apfOut - apfGainStage * delayed;
                        nestedAllpassDelays[i][s].write(v);
                        apfOut = delayed + apfGainStage * v;
                    }
                }

                apfOutVec[i] = apfOut;
                maxChPeak = std::max(maxChPeak, std::abs(apfOut));
            }

            // ★【ユニタリ・エネルギー正規化 AGC】
            // 16ch 全体のピークをエンベロープ追従し、全チャンネルに共通スカラー gLoop を適用
            // （直交性・空間広がりを100%保持したまま、非線形歪みゼロでエネルギーのみを平滑制御）
            const float agcEnvCoeff = (maxChPeak > loopEnergyEnv) ? loopAttackCoeff : loopReleaseCoeff;
            loopEnergyEnv += (maxChPeak - loopEnergyEnv) * agcEnvCoeff;

            float gLoop = 1.0f;
            if (loopEnergyEnv > 0.85f) {
                gLoop = 0.85f / loopEnergyEnv;
            }

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float limitedApfOut = apfOutVec[i] * gLoop;
                nextFb[i] = limitedApfOut;

                const float sideForCh = (i % 2 == 0 ? +fdnInputSide : -fdnInputSide) * sideBoost;
                const float fdnInputForThisCh = (fdnInputMid + sideForCh) * 0.25f;
                fdnDelays[i].write(fdnInputForThisCh + currentFb[i]);

                if ((i & 1) == 0) evenSum += limitedApfOut;
                else              oddSum  += limitedApfOut;
            }

            const float crossLeak = 1.0f - stereoWidth;
            const float fdnOutL = (evenSum + oddSum * crossLeak) * 0.125f;
            const float fdnOutR = (oddSum + evenSum * crossLeak) * 0.125f;
            fbVec = nextFb;

            const float erMakeupGain = (currentERTapCount > 6) ? 1.5f : 2.5f;
            const float erMixL = bypassER ? 0.0f : erOutL * erGainCurved * erMakeupGain;
            const float erMixR = bypassER ? 0.0f : erOutR * erGainCurved * erMakeupGain;
            const float lateMixL = fdnOutL * lateMakeupGainLinear * lateLevel;
            const float lateMixR = fdnOutR * lateMakeupGainLinear * lateLevel;

            // ★ Vintage Warmth Saturator
            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            if (erSolo) { satL = 0.0f; satR = 0.0f; }

            float wetL = erMixL + satL;
            float wetR = erMixR + satR;

            outputEQ.process(wetL, wetR);

            const float finalWetGain = duckGainLinear;
            outL[n] = wetL * finalWetGain;
            outR[n] = wetR * finalWetGain;

            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb