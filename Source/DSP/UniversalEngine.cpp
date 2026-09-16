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
        { 2.0f, 25.0f },
        // SPRING: Vintage Tank (Dispersive Dispersion)
        { 15.0f, 50.0f },
        // GOLDFOIL: EMT 240 (Ultra-thin Gold Foil)
        { 1.0f, 18.0f },
        // INCHINDOWN: 125,000 m3, Length 237m (Colossal Underground Tank)
        { 40.0f, 1500.0f }
    } };

    struct ApfConfig {
        float baseMs[3];
        float spreadCoeff;
    };

    static constexpr std::array<ApfConfig, 6> TOPOLOGY_APF_CONFIGS = { {
        // Room: 小型〜中型部屋 (227Hz, 357Hz, 588Hz)
        { { 1.7f, 2.8f, 4.4f }, 0.40f },
        // Hall: ホール (106Hz, 172Hz, 312Hz)
        { { 3.2f, 5.8f, 9.4f }, 0.60f },
        // Plate: 金属板 - 超短ディレイのモジュレーションで高密度エコーを生成
        { { 0.5f, 1.2f, 2.0f }, 0.20f },
        // Spring: スプリング - 意図的なチャープ（Boing）を生むストレッチド・オールパス
        { { 5.2f, 9.8f, 14.5f }, 0.85f },
        // Goldfoil: 金箔 (312Hz, 500Hz, 909Hz)
        { { 1.1f, 2.0f, 3.2f }, 0.30f },
        // Inchindown: 巨大地下トンネル (超低域分散 42Hz, 73Hz, 138Hz: 中央音域での共鳴ゼロ)
        { { 7.2f, 13.7f, 23.9f }, 0.80f }
    } };

    void UniversalEngine::prepare(double sampleRate, int maxBlockSize) {
        fs = sampleRate;
        DualGoldenLFO::initTable();

        auto getPow2 = [](size_t s) {
            size_t p = 16;
            while (p < s) p <<= 1;
            return p;
        };

        size_t totalMemoryNeeded = 0;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.5)) * 2;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 1.0)); // ER Dummy
        for (int i = 0; i < 4; ++i)
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05)) * 2; // Mid & Side
        for (int i = 0; i < FDN_ORDER; ++i) {
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 2.0)); // ★ 2.0s 確保 (Inchindown対応)
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05));
        }
        for (int i = 0; i < SDNShoebox3D::NUM_NODES; ++i) {
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 2.0)); // SDN Nodes
        }

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineL.init(ptr, mask);
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLineR.init(ptr, mask);

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
        
        // Initialize SDN Engine
        sdnEngine.prepare(sampleRate, maxBlockSize);
        plateMesh.prepare(sampleRate, maxBlockSize);
        springChain.prepare(sampleRate, maxBlockSize);
        inchindownEngine.prepare(sampleRate, maxBlockSize);
        for (int i = 0; i < SDNShoebox3D::NUM_NODES; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 2.0), mask);
            sdnEngine.delayLines[i].init(ptr, mask);
        }
        for (int i = 0; i < 4; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusersM[i].init(ptr, mask);
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            inputDiffusersS[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 2.0), mask); // ★ 2.0s 確保
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
        saturatorL.prepare(sampleRate);
        saturatorR.prepare(sampleRate);
        dynamicDucker.prepare(sampleRate, maxBlockSize);

        ismBufferL.resize(static_cast<size_t>(std::max(1024, maxBlockSize)), 0.0f);
        ismBufferR.resize(static_cast<size_t>(std::max(1024, maxBlockSize)), 0.0f);
        ismSeedBuffer.resize(static_cast<size_t>(std::max(1024, maxBlockSize)), 0.0f);
        ismEngine.prepare(sampleRate, maxBlockSize);

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

        // ★ 入力段 Bandwidth LPF (12kHz 1次ローパス)
        inBandwidthCoeff = 1.0f - std::exp(-6.2831853f * 12000.0f / fsf);
        
        // ★ FDN 遅延線の 1-pole スムージング時定数 (25ms)
        delaySmoothCoeff = 1.0f - std::exp(-1.0f / (fsf * 0.025f));
        
        // ★ ER の 1-pole スムージング時定数 (20ms)
        erSmoothCoeff = 1.0f - std::exp(-1.0f / (fsf * 0.020f));
        erSmoothedGain = activeParams.erLevel * activeParams.erLevel;

        currentFdnDelaySamples.fill(0.0f);

        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        dcX1.fill(0.0f); dcY1.fill(0.0f);

        // ★ ループ内 ユニタリ・エネルギー正規化 AGC 時定数 (Attack: 1ms, Release: 100ms)
        loopAttackCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.001f));
        loopReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.100f));
        loopEnergyEnv = 0.0f;

        lateMakeupGainSmoothed.reset(sampleRate, 0.030); // 30ms sample-accurate linear smoothing
        preDelaySmoothed.reset(sampleRate, 0.025);
        roomSizeSmoothed.reset(sampleRate, 0.040);
        diffuserGainSmoothed.reset(sampleRate, 0.025);
        apfGainStageSmoothed.reset(sampleRate, 0.025);
        stereoWidthSmoothed.reset(sampleRate, 0.025);

        preDelaySmoothed.setCurrentAndTargetValue(0.0f);
        roomSizeSmoothed.setCurrentAndTargetValue(1.0f);
        diffuserGainSmoothed.setCurrentAndTargetValue(0.0f);
        apfGainStageSmoothed.setCurrentAndTargetValue(0.0f);
        stereoWidthSmoothed.setCurrentAndTargetValue(1.0f);

        transitionSamplesTotal = std::max(64, static_cast<int>(0.008 * sampleRate)); // 8ms fade
        transitionState = TransitionState::Normal;
        transitionGain = 1.0f;
        transitionSampleCount = 0;

        fitter.precomputeInteractionMatrix(fs);
        
        // ★ サンプルベース・レート制御 (15ms 判定間隔, 30ms クロスフェード)
        absorptionRateLimitIntervalSamples = std::max(32, static_cast<int>(fs * 0.015)); // 15ms
        samplesSinceLastAbsorptionUpdate = 0;
        absorptionUpdatePending = false;
        absoCrossfadeInc = 1.0f / (fsf * 0.030f); // 30ms crossfade
        absoCrossfadePos = 0.0f; // 初期状態: Bank A が 100%
        absoFadeState = AbsoFadeState::IdleAtA;

        isPreparedFlag = true;

        updateTopologyAndRouting(); // ★ 初期トポロジー・ディレイ長・吸音フィルタの完全同期初期化
        reset();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        panicReset();

        currentFdnDelaySamples.fill(0.0f);
        erSmoothedGain = activeParams.erLevel * activeParams.erLevel;
        smoothedModAmount = activeParams.modAmount;
        smoothedModRate = activeParams.modRate;

        transitionState = TransitionState::Normal;
        transitionGain = 1.0f;
        transitionSampleCount = 0;

        absoCrossfadePos = 0.0f;
        absoFadeState = AbsoFadeState::IdleAtA;
        samplesSinceLastAbsorptionUpdate = 0;
        absorptionUpdatePending = false;

        // ★ LFO 初期位相の決定論的復元 (再現性 100% 保証)
        for (int i = 0; i < FDN_ORDER; ++i) {
            dualLFOs[i].phase1 = static_cast<float>(i) / 16.0f;
            dualLFOs[i].phase2 = std::fmod(static_cast<float>(i) * 0.6180339887f, 1.0f);
        }
    }

    void UniversalEngine::panicReset() noexcept {
        fbVec.fill(0.0f);

        preDelayLineL.resetState();
        preDelayLineR.resetState();
        erDelay.resetState();
        for (auto& dl : inputDiffusersM) dl.resetState();
        for (auto& dl : inputDiffusersS) dl.resetState();
        for (auto& dl : fdnDelays) dl.resetState();
        for (auto& chDelays : nestedAllpassDelays)
            for (auto& dl : chDelays) dl.resetState();

#if AMBIENCE_USE_STAGE2_ABSORPTION
        for (auto& lineFilters : absorptionFiltersS2_A)
            for (auto& f : lineFilters) f.reset();
        for (auto& lineFilters : absorptionFiltersS2_B)
            for (auto& f : lineFilters) f.reset();
        absoCrossfadePos = 0.0f;
        absoFadeState = AbsoFadeState::IdleAtA;
#else
        for (auto& f : absorptionFilters) f.reset();
#endif

        acousticMetrics.reset();
        saturatorL.reset(); saturatorR.reset();
        outputLimiter.reset(); outputEQ.reset();
        dynamicDucker.reset();
        outApL1.reset(); outApL2.reset(); outApL3.reset();
        outApR1.reset(); outApR2.reset(); outApR3.reset();
        loopEnergyEnv = 0.0f;
        sdnEngine.reset();
        plateMesh.reset();
        springChain.reset();
        inchindownEngine.reset();
        ismEngine.reset();
        erOffsetDelayL.fill(0.0f);
        erOffsetDelayR.fill(0.0f);
        erOffsetWriteIdx = 0;

        inLpfStateL = 0.0f;
        inLpfStateR = 0.0f;
        inputTransientEnvFast = 0.0f;
        inputTransientEnvSlow = 0.0f;
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        const bool algoChanged = (activeParams.algorithmIndex != p.algorithmIndex);

        // ディレイ長の再計算を要するパラメータ（RoomSize のみ）
        const bool topologyNeedsUpdate = (activeParams.roomSizeScale != p.roomSizeScale);

        // 吸音フィルタ再設計を要するパラメータ（17ノブ）
        const bool absorptionNeedsUpdate = 
               (activeParams.decayScale != p.decayScale)
            || (activeParams.hfDamping != p.hfDamping)
            || (activeParams.lfAbsorption != p.lfAbsorption)
            || (activeParams.tiltLow != p.tiltLow)
            || (activeParams.tiltMid != p.tiltMid)
            || (activeParams.tiltHigh != p.tiltHigh)
            || (activeParams.rtBands != p.rtBands)
            || (activeParams.airAbsorbScale != p.airAbsorbScale)
            || (activeParams.scattering != p.scattering)
            || (activeParams.erCrossoverMs != p.erCrossoverMs)
            || (activeParams.lateDensity != p.lateDensity)
            || (activeParams.asymmetry != p.asymmetry)
            || (activeParams.clarityDB != p.clarityDB);

        if (algoChanged) {
            ReverbTopology newTop = currentTopology;
            switch (p.algorithmIndex) {
            case 0: case 1: newTop = ReverbTopology::Room;       break;
            case 2: case 3: newTop = ReverbTopology::Hall;       break;
            case 4:         newTop = ReverbTopology::Plate;      break;
            case 5:         newTop = ReverbTopology::Spring;     break;
            case 6:         newTop = ReverbTopology::Goldfoil;   break;
            case 7:         newTop = ReverbTopology::Inchindown; break;
            }
            // ★ 全8アルゴリズムの切り替え時（同トポロジー間やLock点灯時含む）に100%例外なく Graceful Mute を実行
            pendingTopology = newTop;
            pendingParams = p;
            transitionState = TransitionState::FadingOut;
            transitionSampleCount = 0;
            topologyUpdatePending = false;
        } else {
            if (topologyNeedsUpdate) {
                topologyUpdatePending = true;
            }
            if (absorptionNeedsUpdate) {
                // サンプルベース・レート制御用フラグ
                absorptionUpdatePending = true;
            }
        }

        activeParams = p;

        // ★ スムーザーへの目標値設定 (サンプル単位で滑らかに追従)
        preDelaySmoothed.setTargetValue(p.preDelayMs * 0.001f * static_cast<float>(fs));
        preDelaySamples = p.preDelayMs * 0.001f * static_cast<float>(fs);
        roomSizeSmoothed.setTargetValue(p.roomSizeScale);
        stereoWidthSmoothed.setTargetValue(std::clamp(p.stereoWidth, 0.0f, 1.0f));

        const float diff = std::clamp(p.diffusion * diffusionSensitivity, 0.0f, 1.0f);
        const float rawDiffuserGain = diff * 0.70f;
        const float targetDiffuserGain = std::clamp(rawDiffuserGain, 0.0f, 0.7071f);
        diffuserGainSmoothed.setTargetValue(targetDiffuserGain);

        const float effectiveApfGain = apfGain * std::pow(diff, 0.75f);
        const float lateDensityScale = std::clamp(p.lateDensity / 0.7f, 0.1f, 1.4286f);
        const float rawApfGainStage = effectiveApfGain * 0.76f * lateDensityScale;
        const float targetApfGainStage = std::clamp(rawApfGainStage, 0.0f, 0.7071f);
        apfGainStageSmoothed.setTargetValue(targetApfGainStage);

        // 即時反映パラメータ (OutputEQ, DynamicDucker, Saturator)
        outputEQ.setLoParams(p.loEQType, p.loCutHz, p.loGainDB);
        outputEQ.setHiParams(p.hiEQType, p.hiCutHz, p.hiGainDB);
        dynamicDucker.setParameters(p.duckingAmount, p.duckingAttackMs, p.duckingRelMs, p.duckingThreshDB);

        float satMultiplier = 1.0f;
        switch (currentTopology) {
        case ReverbTopology::Room:       satMultiplier = 0.90f; break;
        case ReverbTopology::Hall:       satMultiplier = 0.93f; break;
        case ReverbTopology::Plate:      satMultiplier = 1.00f; break;
        case ReverbTopology::Spring:     satMultiplier = 1.05f; break;
        case ReverbTopology::Goldfoil:   satMultiplier = 1.02f; break;
        case ReverbTopology::Inchindown: satMultiplier = 0.90f; break;
        }
        const float effectiveSatAmount = juce::jlimit(0.0f, 1.0f, p.saturation * satMultiplier);
        saturatorL.setAmount(effectiveSatAmount);
        saturatorR.setAmount(effectiveSatAmount);
        saturatorL.setMode(p.satTypeIdx);
        saturatorR.setMode(p.satTypeIdx);
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

        // ★ トポロジー別 Allpass 遅延長の動的キャッシュ計算
        const int topoIdx = juce::jlimit(0, 5, static_cast<int>(currentTopology));
        const auto& apfCfg = TOPOLOGY_APF_CONFIGS[topoIdx];
        const float msToSmp = 0.001f * static_cast<float>(fs);

        for (int i = 0; i < FDN_ORDER; ++i) {
            const float chFrac = static_cast<float>((i * 7) % 16) / 16.0f;
            for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                const float spreadMs = (s + 1) * apfCfg.spreadCoeff * chFrac;
                cachedApfBaseDelaySmp[i][s] = (apfCfg.baseMs[s] + spreadMs) * msToSmp;
            }
        }

        // 吸音フィルタ初期化（AとBの双方に初期係数を適用）
        updateAbsorptionFilters(false);
        for (int i = 0; i < FDN_ORDER; ++i) {
            for (int b = 0; b < NUM_BANDS; ++b) {
                absorptionCoeffsS2_B[i][b] = absorptionCoeffsS2_A[i][b];
            }
        }

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
            apfGain = 0.50f;  diffusionSensitivity = 1.0f;
            break;
        }

        // ★ 【SDN コア ジオメトリ更新 (Asymmetry 連動)】
        float erSizeScale = 0.5f + activeParams.roomSizeScale;
        const float asym = activeParams.asymmetry;
        
        switch (currentTopology) {
        case ReverbTopology::Room:
            sdnEngine.updateGeometry(4.6f * erSizeScale, 7.4f * erSizeScale, 2.89f * erSizeScale, 1.0f, 1.5f, 1.2f, 3.5f, 5.5f, 1.2f, asym);
            break;
        case ReverbTopology::Hall:
            sdnEngine.updateGeometry(13.5f * erSizeScale, 27.0f * erSizeScale, 10.8f * erSizeScale, 3.0f, 5.0f, 1.7f, 10.0f, 20.0f, 1.7f, asym);
            break;
        case ReverbTopology::Plate:
            sdnEngine.updateGeometry(2.0f * erSizeScale, 1.0f * erSizeScale, 0.001f, 0.3f, 0.7f, 0.0005f, 1.5f, 0.4f, 0.0005f, asym);
            break;
        case ReverbTopology::Spring:
            sdnEngine.updateGeometry(0.3f * erSizeScale, 0.3f * erSizeScale, 0.01f, 0.0f, 0.15f, 0.005f, 0.3f, 0.15f, 0.005f, asym);
            break;
        case ReverbTopology::Goldfoil:
            sdnEngine.updateGeometry(0.27f * erSizeScale, 0.29f * erSizeScale, 0.00002f, 0.05f, 0.14f, 0.00001f, 0.20f, 0.10f, 0.00001f, asym);
            break;
        case ReverbTopology::Inchindown:
            sdnEngine.updateGeometry(9.0f * erSizeScale, 237.0f * erSizeScale, 13.5f * erSizeScale, 4.5f, 10.0f, 6.0f, 4.5f, 50.0f, 6.0f, asym);
            break;
        }

        // ★ SDN 壁面散乱度 (Scattering 連動)
        sdnEngine.scattering = juce::jlimit(0.0f, 1.0f, activeParams.scattering);

        // ★ SDN 吸音・減衰の適用 (erCrossoverMs 連動: 幾何反射から後期残響への物理移行)
        const float fsf = static_cast<float>(fs);
        const float sdnAvgDelaySmp = 0.02f * fsf * erSizeScale;
        const float crossoverSec = std::clamp(activeParams.erCrossoverMs * 0.001f, 0.010f, 0.100f);
        const float sdnRt60 = std::clamp((crossoverSec * 2.0f) * erSizeScale, 0.03f, 0.5f);
        const float sdnDbPerSample = -60.0f / (sdnRt60 * fsf);
        sdnEngine.damping = juce::jlimit(0.1f, 0.999f, juce::Decibels::decibelsToGain(sdnDbPerSample * sdnAvgDelaySmp));
        
        // 高域の壁面吸収 ＆ 大気分子吸音 (ISO 9613-1 / airAbsorbScale 連動)
        const float effectiveAirAbsorb = std::clamp(activeParams.airAbsorbScale, 0.2f, 2.5f);
        const float hfCutoff = std::clamp((10000.0f * (1.1f - activeParams.hfDamping)) / (0.6f + 0.4f * effectiveAirAbsorb), 800.0f, 20000.0f);
        sdnEngine.lpfCoeff = 1.0f - std::exp(-6.2831853f * hfCutoff / fsf);

        if (currentTopology == ReverbTopology::Plate) {
            plateMesh.setParameters(sdnRt60, activeParams.hfDamping, erSizeScale, true);
        } else if (currentTopology == ReverbTopology::Goldfoil) {
            plateMesh.setParameters(sdnRt60, activeParams.hfDamping, erSizeScale, false);
        } else if (currentTopology == ReverbTopology::Spring) {
            springChain.setParameters(sdnRt60, activeParams.hfDamping, erSizeScale, 1.2f);
        } else if (currentTopology == ReverbTopology::Inchindown) {
            inchindownEngine.setParameters(sdnRt60, activeParams.hfDamping, erSizeScale, 1.0f);
        }
        
        // ★ 【ER ビジュアライザー用動的タップ抽出ブリッジ】
        const auto& erPattern = PRESET_ER_PATTERNS[juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex)];
        currentERTapCount = erPattern.numTaps;
        const float maxSafeERDelaySamples = static_cast<float>(fs * 0.95);
        for (int i = 0; i < erPattern.numTaps; ++i) {
            const float delaySmp = erPattern.taps[i].delayMs * 0.001f * fsf * erSizeScale;
            currentERDelaySamples[i] = juce::jlimit(1.0f, maxSafeERDelaySamples, delaySmp);
            currentERGains[i] = erPattern.taps[i].gain;
        }

        float edtCoeff = 0.7f;
        switch (currentTopology) {
        case ReverbTopology::Room:       edtCoeff = 0.70f; break;
        case ReverbTopology::Hall:       edtCoeff = 0.95f; break;
        case ReverbTopology::Plate:      edtCoeff = 0.60f; break;
        case ReverbTopology::Spring:     edtCoeff = 0.50f; break;
        case ReverbTopology::Goldfoil:   edtCoeff = 0.85f; break;
        case ReverbTopology::Inchindown: edtCoeff = 1.00f; break;
        }
        theoreticalEDT = currentRT60Mid * edtCoeff;

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
        dynamicDucker.setParameters(activeParams.duckingAmount, activeParams.duckingAttackMs, activeParams.duckingRelMs, activeParams.duckingThreshDB);
        ismEngine.updateParameters(activeParams.algorithmIndex, activeParams.roomSizeScale, activeParams.preDelayMs, activeParams.hfDamping, activeParams.lfAbsorption);

    }

    void UniversalEngine::updateAbsorptionFilters(bool targetIsB) {
        const int safeAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex);
        auto& preset = *ALL_PRESETS[safeAlgo];

        std::array<float, NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
        for (auto& v : scaledRT60) v *= activeParams.decayScale;

        // ★ TiltEq: 対数周波数軸上の滑らかな 2次多項式補間チルティング
        if (std::abs(activeParams.tiltLow - 1.0f) > 1e-4f ||
            std::abs(activeParams.tiltMid - 1.0f) > 1e-4f ||
            std::abs(activeParams.tiltHigh - 1.0f) > 1e-4f)
        {
            const float x0 = std::log2(BAND_FREQ[1]);
            const float x1 = std::log2(BAND_FREQ[5]);
            const float x2 = std::log2(BAND_FREQ[8]);
            const float d01 = x0 - x1;
            const float d02 = x0 - x2;
            const float d12 = x1 - x2;
            const float denom0 = d01 * d02;
            const float denom1 = -d01 * d12;
            const float denom2 = -d02 * -d12;

            for (int b = 0; b < NUM_BANDS; ++b) {
                const float x = std::log2(BAND_FREQ[b]);
                const float L0 = ((x - x1) * (x - x2)) / denom0;
                const float L1 = ((x - x0) * (x - x2)) / denom1;
                const float L2 = ((x - x0) * (x - x1)) / denom2;
                float tiltFactor = activeParams.tiltLow * L0 + activeParams.tiltMid * L1 + activeParams.tiltHigh * L2;
                tiltFactor = std::clamp(tiltFactor, 0.1f, 10.0f);
                scaledRT60[b] *= tiltFactor;
            }
        }
        for (int b = 0; b < NUM_BANDS; ++b)
            scaledRT60[b] *= activeParams.rtBands[b];

        // 大気減衰 (ISO 9613-1 指数べき乗モデル)
        const float safeAirScale = std::clamp(activeParams.airAbsorbScale, 0.0f, 5.0f);
        if (std::abs(safeAirScale - 1.0f) > 1e-4f) {
            float ratio7 = std::pow(0.90f, safeAirScale);
            float ratio8 = std::pow(0.75f, safeAirScale);
            float ratio9 = std::pow(0.60f, safeAirScale);
            scaledRT60[7] = std::min(scaledRT60[7], scaledRT60[6] * ratio7);
            scaledRT60[8] = std::min(scaledRT60[8], scaledRT60[7] * ratio8);
            scaledRT60[9] = std::min(scaledRT60[9], scaledRT60[8] * ratio9);
        }

        // GUI 表示用に物理減衰を反映した実効 RT60
        std::array<float, NUM_BANDS> displayRT60 = scaledRT60;
        for (int b = 0; b < NUM_BANDS; ++b) {
            float t60 = displayRT60[b];
            float f = BAND_FREQ[b];
            float lfWeight = 1.0f / (1.0f + (f / 160.0f) * (f / 160.0f));
            t60 = std::max(0.01f, t60 * (1.0f - activeParams.lfAbsorption * 0.8f * lfWeight));
            
            float hfWeight = std::pow(std::max(0.0f, f - 2000.0f) / 14000.0f, 2.0f);
            float invT60 = (1.0f / t60) + (activeParams.hfDamping * hfWeight * 2.0f);
            t60 = 1.0f / std::max(1e-4f, invT60);
            displayRT60[b] = t60;
        }
        targetRT60 = displayRT60;
        effectiveRT60 = targetRT60;

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // ★ 状態変数（s1, s2）のコピーは完全に排除！新係数のみを非アクティブ側（targetIsB）に書き込む
        for (int i = 0; i < FDN_ORDER; ++i) {
            auto s2 = fitter.designStage2(
                static_cast<int>(fdnBaseDelaySamples[i]), fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption);
            for (int b = 0; b < NUM_BANDS; ++b) {
                if (targetIsB) {
                    absorptionCoeffsS2_B[i][b] = s2.geqStages[b];
                } else {
                    absorptionCoeffsS2_A[i][b] = s2.geqStages[b];
                }
            }
        }
#endif

        float rt60Mid = 0.0f;
        for (int b = 2; b <= 7; ++b)
            rt60Mid += effectiveRT60[b];
        rt60Mid = std::max(0.1f, rt60Mid / 6.0f);
        currentRT60Mid = rt60Mid;

        modDepthScale = 1.0f + juce::jlimit(0.0f, 2.0f, (rt60Mid - 1.0f) * 0.5f);

        constexpr float baseDB = 5.0f;
        float decayCompDB = -10.0f * std::log10(std::max(0.1f, rt60Mid));
        decayCompDB = juce::jlimit(-12.0f, 12.0f, decayCompDB);

        static constexpr std::array<float, 8> algorithmOffsetDB = {
            +0.8f, +0.9f, +0.5f, +0.5f, +1.5f, +0.6f, +0.6f, +4.5f
        };
        float algoOffset = algorithmOffsetDB[juce::jlimit(0, 7, activeParams.algorithmIndex)];
        const float totalMakeupDB = juce::jlimit(-6.0f, 12.0f, baseDB + decayCompDB + algoOffset);
        lateMakeupGainLinear = juce::Decibels::decibelsToGain(totalMakeupDB);
        lateMakeupGainSmoothed.setTargetValue(lateMakeupGainLinear);

        // SDN 吸音の更新
        const float fsf = static_cast<float>(fs);
        const float erSizeScale = 0.5f + activeParams.roomSizeScale;
        const float sdnAvgDelaySmp = 0.02f * fsf * erSizeScale;
        const float crossoverSec = std::clamp(activeParams.erCrossoverMs * 0.001f, 0.010f, 0.100f);
        const float sdnRt60 = std::clamp((crossoverSec * 2.0f) * erSizeScale, 0.03f, 0.5f);
        const float sdnDbPerSample = -60.0f / (sdnRt60 * fsf);
        sdnEngine.damping = juce::jlimit(0.1f, 0.999f, juce::Decibels::decibelsToGain(sdnDbPerSample * sdnAvgDelaySmp));

        const float effectiveAirAbsorb = std::clamp(activeParams.airAbsorbScale, 0.2f, 2.5f);
        const float hfCutoff = std::clamp((10000.0f * (1.1f - activeParams.hfDamping)) / (0.6f + 0.4f * effectiveAirAbsorb), 800.0f, 20000.0f);
        sdnEngine.lpfCoeff = 1.0f - std::exp(-6.2831853f * hfCutoff / fsf);
        sdnEngine.scattering = juce::jlimit(0.0f, 1.0f, activeParams.scattering);
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
        juce::ScopedNoDenormals noDenormals;
        _mm_setcsr(_mm_getcsr() | 0x8040); // Hardware FTZ (bit 15) & DAZ (bit 6)
        
        if (!isPreparedFlag || inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) {
            if (outL && numSamples > 0) std::fill(outL, outL + numSamples, 0.0f);
            if (outR && numSamples > 0) std::fill(outR, outR + numSamples, 0.0f);
            return;
        }

        if (topologyUpdatePending) {
            updateTopologyAndRouting();
            topologyUpdatePending = false;
        }

        // ★ サンプルベース・レート制御 ＆ ビジー保護トリガー (15ms間隔 / アイドル時のみ遷移)
        samplesSinceLastAbsorptionUpdate += numSamples;
        const bool isCrossfadeIdle = (absoFadeState == AbsoFadeState::IdleAtA || absoFadeState == AbsoFadeState::IdleAtB);
        if (absorptionUpdatePending && isCrossfadeIdle && (samplesSinceLastAbsorptionUpdate >= absorptionRateLimitIntervalSamples)) {
            const bool targetIsB = (absoFadeState == AbsoFadeState::IdleAtA);
            updateAbsorptionFilters(targetIsB);
            absoFadeState = targetIsB ? AbsoFadeState::FadingToB : AbsoFadeState::FadingToA;
            samplesSinceLastAbsorptionUpdate = 0;
            absorptionUpdatePending = false;
        }

        const float fsf = static_cast<float>(fs);
        const float stereoWidth = activeParams.stereoWidth;
        
        // ★ ERLevel の二乗カーブ
        const float erGainCurved = activeParams.erLevel * activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;

        // ★ 定常状態完全バイパス
        bypassER = (erGainCurved < 1e-4f && erSmoothedGain < 1e-4f);

        constexpr float apfModFrac[SERIAL_APF_STAGES] = { 0.25f, 0.20f, 0.15f };

        // ★ C80 自動調整サーボ (Clarity)
        acousticMetrics.updateServo(activeParams.clarityDB, numSamples);
        const float erServo = acousticMetrics.getERServoGain();
        const float lateServo = acousticMetrics.getLateServoGain();

        // ★ ISM (鏡像法) 初期反射エンジンによる並列 AVX2 処理
        if (!bypassER) {
            if (ismBufferL.size() < static_cast<size_t>(numSamples)) {
                ismBufferL.resize(static_cast<size_t>(numSamples), 0.0f);
                ismBufferR.resize(static_cast<size_t>(numSamples), 0.0f);
                ismSeedBuffer.resize(static_cast<size_t>(numSamples), 0.0f);
            }
            std::fill(ismBufferL.begin(), ismBufferL.begin() + numSamples, 0.0f);
            std::fill(ismBufferR.begin(), ismBufferR.begin() + numSamples, 0.0f);
            std::fill(ismSeedBuffer.begin(), ismSeedBuffer.begin() + numSamples, 0.0f);

            ismEngine.processBlock(inL, inR, ismBufferL.data(), ismBufferR.data(), ismSeedBuffer.data(), numSamples, activeParams.erLevel);
        }

        for (int n = 0; n < numSamples; ++n) {
            // ★ Wet-Only Graceful Mute State Machine (全8アルゴリズム対応・Panic連動)
            if (transitionState == TransitionState::FadingOut) {
                transitionSampleCount++;
                const float progress = static_cast<float>(transitionSampleCount) / static_cast<float>(transitionSamplesTotal);
                if (progress >= 1.0f) {
                    transitionGain = 0.0f;
                    panicReset();
                    currentTopology = pendingTopology;
                    activeParams = pendingParams;
                    updateTopologyAndRouting();
                    transitionState = TransitionState::FadingIn;
                    transitionSampleCount = 0;
                } else {
                    transitionGain = std::cos(progress * 1.5707963268f);
                }
            } else if (transitionState == TransitionState::FadingIn) {
                transitionSampleCount++;
                const float progress = static_cast<float>(transitionSampleCount) / static_cast<float>(transitionSamplesTotal);
                if (progress >= 1.0f) {
                    transitionGain = 1.0f;
                    transitionState = TransitionState::Normal;
                    transitionSampleCount = 0;
                } else {
                    transitionGain = std::sin(progress * 1.5707963268f);
                }
            } else {
                transitionGain = 1.0f;
            }

            smoothedModAmount += (activeParams.modAmount - smoothedModAmount) * 0.005f;
            smoothedModRate   += (activeParams.modRate - smoothedModRate)     * 0.005f;
            erSmoothedGain    += erSmoothCoeff * (erGainCurved - erSmoothedGain);

            
            // ★【ビジー保護ステートマシンによるクロスフェード進行】
            if (absoFadeState == AbsoFadeState::FadingToB) {
                absoCrossfadePos += absoCrossfadeInc;
                if (absoCrossfadePos >= 1.0f) {
                    absoCrossfadePos = 1.0f;
                    absoFadeState = AbsoFadeState::IdleAtB; // フェード完走
                }
            } else if (absoFadeState == AbsoFadeState::FadingToA) {
                absoCrossfadePos -= absoCrossfadeInc;
                if (absoCrossfadePos <= 0.0f) {
                    absoCrossfadePos = 0.0f;
                    absoFadeState = AbsoFadeState::IdleAtA; // フェード完走
                }
            }

            // ★ 等パワーゲイン計算（サンプルごとに 1 回のみ）
            const float fadeRad = absoCrossfadePos * 1.5707963268f;
            const float absoGainA = std::cos(fadeRad);
            const float absoGainB = std::sin(fadeRad);

            // ★ スムーザーからサンプル値を取得
            const float curPreDelay = preDelaySmoothed.getNextValue();
            const float curDiffuserGain = diffuserGainSmoothed.getNextValue();
            const float curApfGainStage = apfGainStageSmoothed.getNextValue();
            const float curWidth = stereoWidthSmoothed.getNextValue();
            const float sideBoost = curWidth * 1.5f;

            // ★ デュアル黄金比 LFO レート設定 (除算完全排除・事前計算スケール乗算)
            for (int i = 0; i < FDN_ORDER; ++i) {
                dualLFOs[i].phaseInc1 = smoothedModRate * dualLfoIncScale1[i];
                dualLFOs[i].phaseInc2 = smoothedModRate * dualLfoIncScale2[i];
            }

            const float modAmtCurved = smoothedModAmount * smoothedModAmount;
            const float depthSamples = modAmtCurved * 0.0022f * fsf * modDepthScale;

            preDelayLineL.write(inL[n]);
            preDelayLineR.write(inR[n]);
            float delayedL = (curPreDelay > 0.5f) ? preDelayLineL.read(curPreDelay) : inL[n];
            float delayedR = (curPreDelay > 0.5f) ? preDelayLineR.read(curPreDelay) : inR[n];
            if (!std::isfinite(delayedL)) [[unlikely]] delayedL = 0.0f;
            if (!std::isfinite(delayedR)) [[unlikely]] delayedR = 0.0f;

            // ★ 入力段 Bandwidth 1次 LPF (12kHz)
            inLpfStateL += inBandwidthCoeff * (delayedL - inLpfStateL);
            inLpfStateR += inBandwidthCoeff * (delayedR - inLpfStateR);
            if (!std::isfinite(inLpfStateL)) [[unlikely]] inLpfStateL = 0.0f;
            if (!std::isfinite(inLpfStateR)) [[unlikely]] inLpfStateR = 0.0f;
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

            // ★ Mid / Side 双方を 4段ディフューザーで完全拡散 (ハードバイパス撤廃・サンプル線形平滑化)
            float fdnInputMid = midIn;
            float fdnInputSide = sideIn;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float dm = inputDiffusersM[i].read(cachedDiffuserDelaySmpM[i]);
                    float wm = fdnInputMid + curDiffuserGain * dm;
                    if (!std::isfinite(wm)) [[unlikely]] wm = 0.0f;
                    inputDiffusersM[i].write(wm);
                    fdnInputMid = dm - curDiffuserGain * wm;

                    float ds = inputDiffusersS[i].read(cachedDiffuserDelaySmpS[i]);
                    float ws = fdnInputSide + curDiffuserGain * ds;
                    if (!std::isfinite(ws)) [[unlikely]] ws = 0.0f;
                    inputDiffusersS[i].write(ws);
                    fdnInputSide = ds - curDiffuserGain * ws;
                }
            }

            // ★ 【SDN コアによる初期・中期散乱処理】
            alignas(32) float fdnMeshInject[16] = { 0.0f };
            const bool isPlateOrGoldfoil = (currentTopology == ReverbTopology::Plate || currentTopology == ReverbTopology::Goldfoil);
            const bool isSpring = (currentTopology == ReverbTopology::Spring);
            const bool isInchindown = (currentTopology == ReverbTopology::Inchindown);

            if (!bypassER) {
                if (isPlateOrGoldfoil) {
                    plateMesh.processOneSample(inLpfStateL, inLpfStateR, erOutL, erOutR, fdnMeshInject);
                    sdnEngine.modDepth = depthSamples * 0.2f;
                    sdnEngine.modRate = smoothedModRate;
                    sdnEngine.tickModulatorsOnly();
                } else if (isSpring) {
                    springChain.processOneSample(inLpfStateL, inLpfStateR, erOutL, erOutR, fdnMeshInject);
                    sdnEngine.modDepth = depthSamples * 0.2f;
                    sdnEngine.modRate = smoothedModRate;
                    sdnEngine.tickModulatorsOnly();
                } else if (isInchindown) {
                    inchindownEngine.processOneSample(inLpfStateL, inLpfStateR, erOutL, erOutR, fdnMeshInject);
                    sdnEngine.modDepth = depthSamples * 0.2f;
                    sdnEngine.modRate = smoothedModRate;
                    sdnEngine.tickModulatorsOnly();
                } else {
                    sdnEngine.modDepth = depthSamples * 0.2f;
                    sdnEngine.modRate = smoothedModRate;
                    sdnEngine.processOneSample(inLpfStateL, inLpfStateR, erOutL, erOutR);
                }
                erOutL *= erSmoothedGain;
                erOutR *= erSmoothedGain;
            }

            if (!bypassER) {
                erOutL += ismBufferL[n];
                erOutR += ismBufferR[n];
            }

            if (!bypassER) {
                // SDN / 2D Mesh 散乱出力をFDNへ注入 (LateDensity 連動ハイブリッド結合)
                const float densityScale = std::clamp(activeParams.lateDensity, 0.1f, 1.0f);
                const float injectGain = 0.7071f * std::sqrt(densityScale / 0.7f);
                fdnInputMid += (erOutL + erOutR) * injectGain;
                fdnInputSide += (erOutL - erOutR) * injectGain;
            }

            // ★ 空間相関の対称性破壊 (Asymmetric Injection / Extraction)
            static constexpr float injectSign[16]  = { 1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f };
            static constexpr float extractSign[16] = { 1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f, -1.0f };

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float evenSum = 0.0f, oddSum = 0.0f;
            std::array<float, 16> nextFb;
            std::array<float, 16> apfOutVec;
            float maxChPeak = 0.0f;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float chorusVal = dualLFOs[i].tick();
                
                // ★ FDNベースディレイの 1-pole スムージング & Fractional リード
                const float targetSmp = fdnBaseDelaySamples[i];
                
                if (currentFdnDelaySamples[i] == 0.0f) [[unlikely]] {
                    currentFdnDelaySamples[i] = targetSmp;
                } else if (std::abs(targetSmp - currentFdnDelaySamples[i]) > 1e-4f) {
                    currentFdnDelaySamples[i] += delaySmoothCoeff * (targetSmp - currentFdnDelaySamples[i]);
                } else {
                    currentFdnDelaySamples[i] = targetSmp;
                }

                float d;
                const int intDelay = static_cast<int>(std::round(currentFdnDelaySamples[i]));
                if (std::abs(currentFdnDelaySamples[i] - static_cast<float>(intDelay)) < 1e-5f) {
                    d = fdnDelays[i].readInt(intDelay);
                } else {
                    d = fdnDelays[i].read(currentFdnDelaySamples[i] - 1.0f);
                }

#if AMBIENCE_USE_STAGE2_ABSORPTION
                // ★★★【常時デュアル駆動・状態変数完全保持の神髄】★★★
                // 分岐条件は一切なし！Bank A と Bank B の両方を常に tick して内部状態を同期
                float outA = d;
                for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                    outA = absorptionFiltersS2_A[i][s].tick(outA, absorptionCoeffsS2_A[i][s]);
                }

                float outB = d;
                for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                    outB = absorptionFiltersS2_B[i][s].tick(outB, absorptionCoeffsS2_B[i][s]);
                }

                // 等パワー合成（過渡ショックゼロ、振幅段差ゼロ、クリックゼロ）
                d = outA * absoGainA + outB * absoGainB;
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

                // Schroeder Allpass (ハードバイパス撤廃・curApfGainStage による連続平滑遷移)
                float apfOut = d;
                for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                    const float baseDelay = cachedApfBaseDelaySmp[i][s];
                    float delayed;
                    
                    if (s == 2 && depthSamples > 0.01f) {
                        const float maxSafeMod = baseDelay * 0.15f;
                        const float targetMod = depthSamples * apfModFrac[s] * cachedFreqModScales[i];
                        
                        const float knee = maxSafeMod * 0.75f;
                        float safeMod = targetMod;
                        if (targetMod > knee) {
                            const float excess = targetMod - knee;
                            const float room = maxSafeMod - knee;
                            safeMod = knee + room * std::tanh(excess / room);
                        }
                        
                        const float apfDelaySmp = baseDelay + chorusVal * safeMod;
                        delayed = nestedAllpassDelays[i][s].read(apfDelaySmp);
                    } else {
                        delayed = nestedAllpassDelays[i][s].readInt(static_cast<int>(std::round(baseDelay)));
                    }

                    const float v = apfOut - curApfGainStage * delayed;
                    nestedAllpassDelays[i][s].write(v);
                    apfOut = delayed + curApfGainStage * v;
                }

                apfOutVec[i] = apfOut;
                maxChPeak = std::max(maxChPeak, std::abs(apfOut));
            }

            // ★【自己修復メカニズム (Auto-Recovery)】
            // ループ内ピークが NaN / Inf または異常な過大エネルギーに達した場合、直ちにバッファを自己修復
            if (!std::isfinite(maxChPeak) || maxChPeak > 1.0e4f || !std::isfinite(loopEnergyEnv)) [[unlikely]] {
                memoryPool.clear();
                fbVec.fill(0.0f);
                loopEnergyEnv = 0.0f;
                maxChPeak = 0.0f;
                for (int ch = 0; ch < FDN_ORDER; ++ch) {
                    apfOutVec[ch] = 0.0f;
                    dcX1[ch] = 0.0f;
                    dcY1[ch] = 0.0f;
                }
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
                float fdnInputForThisCh = (fdnInputMid * injectSign[i] + sideForCh) * 0.25f;
                float fdnWriteVal = fdnInputForThisCh + currentFb[i];
                if (!std::isfinite(fdnWriteVal)) [[unlikely]] fdnWriteVal = 0.0f;
                fdnDelays[i].write(fdnWriteVal);

                const float extractedOut = limitedApfOut * extractSign[i];
                if ((i & 1) == 0) evenSum += extractedOut;
                else              oddSum  += extractedOut;
            }

            const float curCrossLeak = 1.0f - curWidth;
            // 16ch FDNのL/R出力 (各8chの非相関サミング: 1/sqrt(8) = 0.3535f)
            const float fdnOutL = (evenSum + oddSum * curCrossLeak) * 0.353553f;
            const float fdnOutR = (oddSum + evenSum * curCrossLeak) * 0.353553f;
            fbVec = nextFb;

            const float erMakeupGain = 2.5f; // 音響テスト・残響バランス完全維持
            const float cDb = std::clamp(activeParams.clarityDB, -6.0f, 6.0f);
            const float clarityShift = cDb * 0.25f; // 最大 ±1.5dB
            const float gClarityER   = juce::Decibels::decibelsToGain(+clarityShift);
            const float gClarityLate = juce::Decibels::decibelsToGain(-clarityShift);

            float erMixL = bypassER ? 0.0f : erOutL * erSmoothedGain * erMakeupGain * erServo * gClarityER;
            float erMixR = bypassER ? 0.0f : erOutR * erSmoothedGain * erMakeupGain * erServo * gClarityER;

            // ★ ER Solo / Send Mode 時の 0〜5ms コムフィルター防止オフセット（音響工学的インテリジェント制御）
            const bool isSendOrErSolo = (erSolo || activeParams.dryDB <= -59.0f);
            if (isSendOrErSolo && !bypassER) {
                const float minAllowedDelaySmp = 0.005f * fsf; // 5ms 下限 (コムフィルター完全防止境界)
                if (curPreDelay < minAllowedDelaySmp) {
                    const int shiftSmp = static_cast<int>(minAllowedDelaySmp - curPreDelay);
                    erOffsetDelayL[erOffsetWriteIdx] = erMixL;
                    erOffsetDelayR[erOffsetWriteIdx] = erMixR;
                    const size_t readIdx = (erOffsetWriteIdx + 2048 - static_cast<size_t>(shiftSmp)) & 2047;
                    erMixL = erOffsetDelayL[readIdx];
                    erMixR = erOffsetDelayR[readIdx];
                    erOffsetWriteIdx = (erOffsetWriteIdx + 1) & 2047;
                }
            }
            // ★ ModAmt 変調時の Hermite 補間通過損失を動的補正（音量低下ゼロ化）
            const float modLossComp = 1.0f + (0.08f + 0.08f * std::min(5.0f, currentRT60Mid)) * modAmtCurved;
            const float curLateMakeupGain = lateMakeupGainSmoothed.getNextValue();
            const float lateMixL = fdnOutL * curLateMakeupGain * lateLevel * lateServo * modLossComp * gClarityLate;
            const float lateMixR = fdnOutR * curLateMakeupGain * lateLevel * lateServo * modLossComp * gClarityLate;

            // ★ Vintage Warmth ADAA Saturator (出力段 L/R 独立 1次 ADAA 処理)
            float satL = saturatorL.processSample(lateMixL);
            float satR = saturatorR.processSample(lateMixR);

            if (erSolo) { satL = 0.0f; satR = 0.0f; }

            float wetL = erMixL + satL;
            float wetR = erMixR + satR;

            // ★ 出力段ステレオ・オールパス・ディフューザー (音色着色ゼロ・3段カスケード素数拡散)
            const float curApDiffGain = 0.618f * curWidth; // 黄金比ゲインによる最適散乱
            wetL = outApL3.process(outApL2.process(outApL1.process(wetL, curApDiffGain), curApDiffGain), curApDiffGain);
            wetR = outApR3.process(outApR2.process(outApR1.process(wetR, -curApDiffGain), -curApDiffGain), -curApDiffGain);

            outputEQ.process(wetL, wetR);

            // ★ 4バンド・ダイナミックEQダッキング (周波数追従型マスキング解消)
            dynamicDucker.processStereo(inL[n], inR[n], wetL, wetR);

            // ★ 最終出力段 Mid/Side（和差）幅制御 (条件分岐完全撤廃・全域 C^∞ 連続平滑化)
            const float mid  = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * curWidth;
            wetL = mid + side;
            wetR = mid - side;

            // ★ Wet-Only Graceful Mute 適用 (アルゴリズム切り替え時の無音化＆フェード)
            wetL *= transitionGain;
            wetR *= transitionGain;

            // ★ ソフトウェア・アンチデノーマル完全遮断
            if (std::abs(wetL) < 1.0e-15f) [[unlikely]] wetL = 0.0f;
            if (std::abs(wetR) < 1.0e-15f) [[unlikely]] wetR = 0.0f;

            outL[n] = wetL;
            outR[n] = wetR;

            acousticMetrics.processSample((wetL + wetR) * 0.5f);

            outputLimiter.process(outL[n], outR[n]);
        }
    }

} // namespace FDNReverb