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
        inLpfStateL = 0.0f;
        inLpfStateR = 0.0f;
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
        // Phase3 PL-1: widened 2-25 ms -> 3-40 ms to lift FDN modal density
        // (Schroeder sum 66% -> ~100%) and dilute the 234 Hz outlier mode.
        // Phase3 PL-2: -> 4-55 ms (density ~137%) to dilute the residual
        // slowest eigenmode further (PL-1 left W2 prominence +1.2 dB @205 Hz).
        { 4.0f, 55.0f },
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
        // Room: 小型〜中型部屋 (196Hz, 303Hz, 476Hz)
        { { 2.1f, 3.3f, 5.1f }, 0.55f },
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

        // Goldfoil mesh-feed lowpass coefficient (1-pole, fc = 1 kHz).
        gfMeshFeedLpCoeff = 1.0f - std::exp(-6.28318530718f * 1000.0f / static_cast<float>(fs));

        // Second feed-shaping stage: 6th-order Butterworth LP at 2.2 kHz
        // (3 RBJ sections; see the header note and FEED8K.md).
        {
            static constexpr double kQ[3] = { 0.5176, 0.7071, 1.9319 };
            for (int s = 0; s < 3; ++s) {
                const double w = 6.283185307179586 * 2200.0 / fs;
                const double alpha = std::sin(w) / (2.0 * kQ[s]);
                const double cs = std::cos(w);
                const double a0 = 1.0 + alpha;
                gfMeshFeedBqCoeffs[s] = { static_cast<float>((1.0 - cs) * 0.5 / a0),
                                          static_cast<float>((1.0 - cs) / a0),
                                          static_cast<float>((1.0 - cs) * 0.5 / a0),
                                          static_cast<float>(-2.0 * cs / a0),
                                          static_cast<float>((1.0 - alpha) / a0) };
            }
        }

        auto getPow2 = [](size_t s) {
            size_t p = 16;
            while (p < s) p <<= 1;
            return p;
        };

        // FDN delay-line capacity is derived from the longest designable
        // delay: the largest ALGORITHM_DELAY_BOUNDS maximum (Inchindown
        // 1500 ms) times the largest roomSizeScale the delay calculator
        // accepts (2.5) = 3.75 s. The previous flat 2.0 s allocation could
        // not hold that line: integer reads wrapped modulo the mask
        // (143,999 -> 12,927 samples at 48 kHz) and fractional reads
        // clamped, so the designed prime never sounded. Sizing follows
        // seconds x sample rate, so it stays correct at every SR.
        // (phase3/inchindown/INCHINDOWN.md)
        float fdnAllocSec = 0.0f;
        for (const auto& b : ALGORITHM_DELAY_BOUNDS)
            fdnAllocSec = std::max(fdnAllocSec, b.maxDelayMs * 2.5f * 0.001f);

        size_t totalMemoryNeeded = 0;
        totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.5)) * 2;
        for (int i = 0; i < INPUT_DIFFUSER_STAGES; ++i)
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * 0.05)) * 2; // Mid & Side
        for (int i = 0; i < FDN_ORDER; ++i) {
            totalMemoryNeeded += getPow2(static_cast<size_t>(fs * fdnAllocSec)); // FDN lines (sized above)
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
        
        // Initialize SDN Engine
        sdnEngine.prepare(sampleRate, maxBlockSize);
        plateMesh.prepare(sampleRate, maxBlockSize);
        springChain.prepare(sampleRate, maxBlockSize);
        inchindownEngine.prepare(sampleRate, maxBlockSize);
        for (int i = 0; i < SDNShoebox3D::NUM_NODES; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 2.0), mask);
            sdnEngine.delayLines[i].init(ptr, mask);
        }
        inputDiffusersValid = true;
        for (int i = 0; i < INPUT_DIFFUSER_STAGES; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            if (ptr == nullptr) inputDiffusersValid = false;
            inputDiffusersM[i].init(ptr, mask);
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.05), mask);
            if (ptr == nullptr) inputDiffusersValid = false;
            inputDiffusersS[i].init(ptr, mask);
        }

        for (int i = 0; i < FDN_ORDER; ++i) {
            ptr = memoryPool.requestMemory(static_cast<size_t>(fs * fdnAllocSec), mask);
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
        saturatorL.prepare(sampleRate);
        saturatorR.prepare(sampleRate);
        dynamicDucker.prepare(sampleRate, maxBlockSize);

        ismBufferL.resize(static_cast<size_t>(std::max(1024, maxBlockSize)), 0.0f);
        ismBufferR.resize(static_cast<size_t>(std::max(1024, maxBlockSize)), 0.0f);
        ismEngine.prepare(sampleRate, maxBlockSize);

        const float fsf = static_cast<float>(fs);
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i) {
            cachedFreqModScales[i] = 0.6f + (1.0f - static_cast<float>(i) * invFdnM1) * 0.9f;
            dualLfoIncScale1[i] = dualLFOs[i].rateScale / fsf;
            dualLfoIncScale2[i] = (dualLFOs[i].rateScale * 0.6180339887f) / fsf;
        }

        // [phase3/diffusion 候補(a)] 入力ディフューザ遅延セット再設計:
        // 旧セットは等差数列 (M: 3/5/7/9ms = 全て 48 smp の倍数, GCD=48) で
        // 出力が 1ms 格子に量子化されバースト NED 平均 0.058 しかなかった。
        // 互いに素な素数遅延 (GCD=1) に置換し 6 段化。48k 基準サンプル数で
        // 保持し SR 比でスケールする。詳細: phase3/diffusion/DIFFUSION.md
        static constexpr float DIFF_SMP_M[INPUT_DIFFUSER_STAGES] = { 97.0f, 191.0f, 307.0f, 139.0f, 421.0f, 263.0f };
        static constexpr float DIFF_SMP_S[INPUT_DIFFUSER_STAGES] = { 113.0f, 229.0f, 167.0f, 389.0f, 271.0f, 239.0f };
        for (int i = 0; i < INPUT_DIFFUSER_STAGES; ++i) {
            cachedDiffuserDelaySmpM[i] = DIFF_SMP_M[i] * (fsf / 48000.0f);
            cachedDiffuserDelaySmpS[i] = DIFF_SMP_S[i] * (fsf / 48000.0f);
        }

        // ★ 入力段 Bandwidth LPF (12kHz 1次ローパス)
        inBandwidthCoeff = 1.0f - std::exp(-6.2831853f * 12000.0f / fsf);
        
        // ★ FDN 遅延線の 1-pole スムージング時定数 (25ms)
        delaySmoothCoeff = 1.0f - std::exp(-1.0f / (fsf * 0.025f));
        
        // ★ ER の 1-pole スムージング時定数 (20ms)
        erSmoothCoeff = 1.0f - std::exp(-1.0f / (fsf * 0.020f));
        erSmoothedGain = activeParams.erLevel * activeParams.erLevel;

        currentFdnDelaySamples.fill(0.0f);
        fdnReadCacheTarget.fill(-1.0f); // 遅延ライン再初期化に合わせて読み出しキャッシュを無効化

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
        // [phase3/small-fixes] この時点の状態が「新規インスタンスの初期状態」。
        // 以後の reset() はここへ完全復元する (captureResetSnapshot 参照)。
        captureResetSnapshot();
    }

    void UniversalEngine::reset() {
        memoryPool.clear();
        panicReset();

        // [phase3/small-fixes] prepare 終端スナップショットの復元。
        // activeParams を含むため、この後の erSmoothedGain /
        // smoothedModAmount / smoothedModRate の導出も prepare 終端の
        // 値 (既定パラメータ由来) に揃う。prepare() 内の reset() 呼び出し
        // ではスナップショット未捕捉のため無効 (自然な初期状態のまま)。
        if (resetSnapshotValid) restoreResetSnapshot();
        topologyUpdatePending = false;
        sdnParamsUpdatePending = false;
        fdnReadCacheTarget.fill(-1.0f); // prepare と同じ読み出しキャッシュ無効化
        fdnReadCacheId.fill(0);
        fdnReadCacheFrac.fill(0.0f);
        fdnReadCacheIsInt.fill(0);

        gfMeshFeedLpState.fill(0.0f);
        for (int s = 0; s < 3; ++s) {
            gfMeshFeedBqX1[s].fill(0.0f); gfMeshFeedBqX2[s].fill(0.0f);
            gfMeshFeedBqY1[s].fill(0.0f); gfMeshFeedBqY2[s].fill(0.0f);
        }
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

    // [phase3/small-fixes] prepare() 終端の状態を捕捉する。reset() が従来
    // 復元し損ねていた状態 (下記) を全て含む。信号履歴そのもの (遅延線・
    // フィルタ状態変数・履歴バッファ) は memoryPool.clear() と
    // panicReset() が消去するため対象外。
    void UniversalEngine::captureResetSnapshot() {
        snapActiveParams = activeParams;
        snapPendingParams = pendingParams;
        snapCurrentTopology = currentTopology;
        snapPendingTopology = pendingTopology;
        snapCoeffsA = absorptionCoeffsS2_A;
        snapCoeffsB = absorptionCoeffsS2_B;
        snapEffectiveRT60 = effectiveRT60;
        snapTargetRT60 = targetRT60;
        snapTheoreticalEDT = theoreticalEDT;
        snapCurrentRT60Mid = currentRT60Mid;
        snapCurrentDesignRT60_1k = currentDesignRT60_1k;
        snapModDepthScale = modDepthScale;
        snapLateMakeupGainLinear = lateMakeupGainLinear;
        snapApfGain = apfGain;
        snapDiffusionSensitivity = diffusionSensitivity;
        snapBypassER = bypassER;
        snapBypassInputDiffusers = bypassInputDiffusers;
        snapPreDelaySamples = preDelaySamples;
        snapFdnBaseDelaySamples = fdnBaseDelaySamples;
        snapCachedApfBaseDelaySmp = cachedApfBaseDelaySmp;
        snapCachedApfBaseDelayInt = cachedApfBaseDelayInt;
        snapCurrentERTapCount = currentERTapCount;
        snapCurrentERDelaySamples = currentERDelaySamples;
        snapCurrentERGains = currentERGains;
        snapSmPreDelayCur = preDelaySmoothed.getCurrentValue();
        snapSmPreDelayTgt = preDelaySmoothed.getTargetValue();
        snapSmRoomSizeCur = roomSizeSmoothed.getCurrentValue();
        snapSmRoomSizeTgt = roomSizeSmoothed.getTargetValue();
        snapSmDiffuserCur = diffuserGainSmoothed.getCurrentValue();
        snapSmDiffuserTgt = diffuserGainSmoothed.getTargetValue();
        snapSmApfCur = apfGainStageSmoothed.getCurrentValue();
        snapSmApfTgt = apfGainStageSmoothed.getTargetValue();
        snapSmWidthCur = stereoWidthSmoothed.getCurrentValue();
        snapSmWidthTgt = stereoWidthSmoothed.getTargetValue();
        snapSmMakeupCur = lateMakeupGainSmoothed.getCurrentValue();
        snapSmMakeupTgt = lateMakeupGainSmoothed.getTargetValue();
        snapSdnBaseDelaySamples = sdnEngine.captureBaseDelaySamples();
        snapSdnRngStates = sdnEngine.captureModulatorRngStates();
        snapIsmState = ismEngine.captureState();
        snapSatL = saturatorL.captureResetState();
        snapSatR = saturatorR.captureResetState();
        resetSnapshotValid = true;
    }

    void UniversalEngine::restoreResetSnapshot() {
        activeParams = snapActiveParams;
        pendingParams = snapPendingParams;
        currentTopology = snapCurrentTopology;
        pendingTopology = snapPendingTopology;
        absorptionCoeffsS2_A = snapCoeffsA;
        absorptionCoeffsS2_B = snapCoeffsB;
        effectiveRT60 = snapEffectiveRT60;
        targetRT60 = snapTargetRT60;
        theoreticalEDT = snapTheoreticalEDT;
        currentRT60Mid = snapCurrentRT60Mid;
        currentDesignRT60_1k = snapCurrentDesignRT60_1k;
        modDepthScale = snapModDepthScale;
        lateMakeupGainLinear = snapLateMakeupGainLinear;
        apfGain = snapApfGain;
        diffusionSensitivity = snapDiffusionSensitivity;
        bypassER = snapBypassER;
        bypassInputDiffusers = snapBypassInputDiffusers;
        preDelaySamples = snapPreDelaySamples;
        fdnBaseDelaySamples = snapFdnBaseDelaySamples;
        cachedApfBaseDelaySmp = snapCachedApfBaseDelaySmp;
        cachedApfBaseDelayInt = snapCachedApfBaseDelayInt;
        currentERTapCount = snapCurrentERTapCount;
        currentERDelaySamples = snapCurrentERDelaySamples;
        currentERGains = snapCurrentERGains;
        // 平滑値: 現在値を即値で戻し、目標値が異なる場合のみランプを張り直す。
        // prepare 終端では lateMakeup のみ current != target で、ランプ残量は
        // stepsToTarget 全量 (= setTargetValue の再設定と一致) である。
        auto restoreSm = [](auto& sm, float cur, float tgt) {
            sm.setCurrentAndTargetValue(cur);
            if (tgt != cur) sm.setTargetValue(tgt);
        };
        restoreSm(preDelaySmoothed, snapSmPreDelayCur, snapSmPreDelayTgt);
        restoreSm(roomSizeSmoothed, snapSmRoomSizeCur, snapSmRoomSizeTgt);
        restoreSm(diffuserGainSmoothed, snapSmDiffuserCur, snapSmDiffuserTgt);
        restoreSm(apfGainStageSmoothed, snapSmApfCur, snapSmApfTgt);
        restoreSm(stereoWidthSmoothed, snapSmWidthCur, snapSmWidthTgt);
        restoreSm(lateMakeupGainSmoothed, snapSmMakeupCur, snapSmMakeupTgt);
        sdnEngine.restoreBaseDelaySamples(snapSdnBaseDelaySamples);
        sdnEngine.restoreModulatorRngStates(snapSdnRngStates);
        ismEngine.restoreState(snapIsmState);
        saturatorL.restoreResetState(snapSatL);
        saturatorR.restoreResetState(snapSatR);
    }

    void UniversalEngine::panicReset() noexcept {
        fbVec.fill(0.0f);

        preDelayLineL.resetState();
        preDelayLineR.resetState();
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
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);
    }

    void UniversalEngine::setParams(const DSPParams& p) {
        const bool algoChanged = (activeParams.algorithmIndex != p.algorithmIndex);

        // ディレイ長の再計算を要するパラメータ（RoomSize のみ）
        const bool topologyNeedsUpdate = (activeParams.roomSizeScale != p.roomSizeScale);

        // 吸音フィルタ再設計を要するパラメータ
        // [P6] トリガーは designStage2 の実入力のみに限定する。旧実装では
        // scattering / erCrossoverMs / lateDensity / asymmetry / clarityDB の
        // 変更でも同一係数の再設計 (約 656 回の pow 他) と 30ms の無意味な
        // クロスフェードが発生し、フェード中は同一信号の cos+sin 合成で
        // 最大 +3dB の膨らみまで生じていた。これら 5 つは設計入力ではない。
        const bool absorptionNeedsUpdate =
               (activeParams.decayScale != p.decayScale)
            || (activeParams.hfDamping != p.hfDamping)
            || (activeParams.lfAbsorption != p.lfAbsorption)
            || (activeParams.tiltLow != p.tiltLow)
            || (activeParams.tiltMid != p.tiltMid)
            || (activeParams.tiltHigh != p.tiltHigh)
            || (activeParams.rtBands != p.rtBands)
            || (activeParams.airAbsorbScale != p.airAbsorbScale);

        // [P6] GEQ 再設計を伴わず SDN 派生値だけを更新すべきパラメータ
        const bool sdnDerivedNeedsUpdate =
               (activeParams.scattering != p.scattering)
            || (activeParams.erCrossoverMs != p.erCrossoverMs);

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
            if (sdnDerivedNeedsUpdate) {
                sdnParamsUpdatePending = true;
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

    // Phase3 Goldfoil redesign (adopted candidate D24): explicit delay set.
    // The ms-bounds -> log-spaced -> prime-snap generation is a lottery for the
    // FDN's transmission-zero pattern: both widened sets tried in the metallic
    // programme (2-30 ms, 2-24 ms) placed a deep steady-state zero on 1 kHz
    // (fundamental -26.4 / -17.1 dB). This set is designed in samples at
    // 48 kHz / roomSize 0.90 (the Goldfoil preset default): 16 distinct
    // primes, sum ~7440 samples = Schroeder modal density ~50% (dev: 28%),
    // screened with a simplified FDN steady-state model and verified with
    // the steady-state sweep: on the 1/6-oct grid every point is within 6 dB
    // of the dev response except one narrow zero at 158.7 Hz (dev has the
    // same zero at ~160 Hz; see phase3/goldfoil/REDESIGN.md).
    // Candidate D: fractional delays. Integer loop lengths share a common
    // lattice with the (integer) serial-APF delays and the sample grid;
    // golden-ratio fractional offsets make every total loop length
    // incommensurate, smearing the transmission-zero pattern. The engine's
    // delay read already interpolates non-integer delays.
    // Robust-screening revision (phase3/goldfoil/ROBUST_SCREENING.md): the
    // fractional parts were re-screened over the full condition grid
    // (SR 44.1/48/96/192k x roomSize 0.30-2.00 + spot values) under a hard
    // min-distance constraint. Every channel's delay now keeps a distance
    // of >= 0.05 samples from the nearest integer at all 40 checked
    // conditions (the previous set landed exactly on an integer at
    // roomSize 2.00, ch0 = 173.00 samples, defeating the fractional-delay
    // premise for that channel, and within 0.003 at roomSize 0.30).
    // Design point (48k / 0.90) vs the previous set: ring 21.7 -> 20.9 dB,
    // THDN 2.27 -> 2.01 %, T30@1k error +1.5 -> +3.1 % (gate +-3pt), the
    // sweep keeps only the shared ~159 Hz zero. The grid exceedance
    // profile vs dev is unchanged in character (worst margin +8, at
    // 48k / roomSize 1.15, as before): within the constants-only scope no
    // screened set beat it without breaking a design-point gate; see
    // ROBUST_SCREENING.md for the full before/after and the rejected
    // margin-optimal alternative.
    static constexpr std::array<float, 16> GOLDFOIL_DELAY_SET_48K = { {
        67.204f, 71.39f, 89.4705f, 113.558f, 137.5775f, 191.842f, 223.2695f, 257.156f,
        337.6045f, 421.4115f, 499.8085f, 631.3845f, 757.8545f, 977.5775f, 1181.096f, 1481.4285f
    } };

    void UniversalEngine::calculatePrimePowerDelays() {
        const float fsf = static_cast<float>(fs);
        const int safeAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex);

        if (safeAlgo == 6) { // Goldfoil: explicit set, scaled from the 48k/0.90 reference
            const float sizeScaleGF = juce::jlimit(0.3f, 2.5f, activeParams.roomSizeScale);
            const float rel = (fsf / 48000.0f) * (sizeScaleGF / 0.90f);
            for (int i = 0; i < FDN_ORDER; ++i) {
                fdnBaseDelaySamples[i] = std::max(11.0f, GOLDFOIL_DELAY_SET_48K[i] * rel + 24.0f * (fsf / 48000.0f));
            }
            return;
        }

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
                cachedApfBaseDelayInt[i][s] = static_cast<int>(std::round(cachedApfBaseDelaySmp[i][s]));
            }
        }

        // 吸音フィルタ初期化（AとBの双方に初期係数を適用）
        updateAbsorptionFilters(false);
        for (int i = 0; i < FDN_ORDER; ++i) {
            for (int b = 0; b < ABSO_STAGES_S2; ++b) {
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
            // The tank must be driven by the Decay Time the FDN implements,
            // not by the ER-crossover pseudo-RT60 (sdnRt60 <= 0.5 s): at
            // 0.5 s the 237 m long-axis per-trip broadband gain is ~2e-11,
            // so the namesake long-axis nodes are silent and the core dies
            // within ~2 s. (phase3/inchindown/INCHINDOWN.md)
            inchindownEngine.setParameters(currentDesignRT60_1k, activeParams.hfDamping, erSizeScale, 1.0f);
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

    // phase3/stereo SPLIT8 implementation support (STEREO_IMPL.md):
    // per-algorithm band-T30 recalibration factors (multipliers on the
    // design RT60 targets) and late-makeup trims (dB) for the split-FDN
    // path. Splitting the shared 16ch network into two 8ch per-ear
    // networks changes each band's modal statistics, so the design
    // targets are trimmed per band to reproduce the current head's
    // measured band T30, and the late makeup is trimmed to restore the
    // total IR energy lost with the crossLeak contribution.
    // Goldfoil (index 6) never takes the split path: its row is the
    // identity and is never applied. Bands follow BAND_FREQ
    // (31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 Hz).
    // Rebase note (phase3/stereo/STEREO_REBASE.md): the Room1 / Room2 /
    // Spring / Inchindown rows and all makeup trims were re-identified
    // on the f309b2fb base (B3 + Inchindown fixes + OS2 + diffusion (a)
    // + F3 landed after the original identification); the Hall1 /
    // Hall2 / Plate band rows are unchanged from the original
    // identification and still meet gates there.
    static constexpr std::array<std::array<float, NUM_BANDS>, NUM_ALGORITHMS> SPLIT8_BAND_CORR = { {
        { 0.8021f, 0.8021f, 0.8021f, 1.2297f, 1.0485f, 1.1074f, 0.9245f, 1.0418f, 0.9882f, 0.9882f }, // Room1
        { 0.9483f, 0.9483f, 0.9483f, 1.0001f, 1.0600f, 0.9927f, 0.9906f, 1.0027f, 1.0028f, 1.0028f }, // Room2
        { 0.9753f, 0.9753f, 0.9753f, 1.0001f, 1.0146f, 0.9669f, 1.0128f, 0.9963f, 0.9949f, 0.9949f }, // Hall1
        { 1.0177f, 1.0177f, 1.0177f, 0.9941f, 0.9957f, 1.0249f, 1.0248f, 0.9810f, 0.9984f, 0.9984f }, // Hall2
        { 0.9997f, 0.9997f, 0.9997f, 1.0260f, 1.0859f, 1.0274f, 0.9545f, 0.9992f, 0.9231f, 0.9231f }, // Plate
        { 1.0129f, 1.0129f, 1.0129f, 1.0518f, 0.9851f, 1.0114f, 0.9400f, 1.0063f, 0.9871f, 0.9871f }, // Spring
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, // Goldfoil (unused)
        { 0.9626f, 0.9626f, 0.9626f, 1.0178f, 1.0804f, 1.0003f, 1.0140f, 0.9948f, 1.0163f, 1.0163f }  // Inchindown
    } };
    // Makeup trims are applied in the audio path (processBlock, keyed on
    // activeParams.algorithmIndex), NOT in this design function: a
    // design-time trim leaks into the prepare()-time design (which runs
    // with the default algorithm index) and shifts the lateMakeupGain
    // smoother's starting target, which audibly perturbs even Goldfoil's
    // first ~400 samples; and Room1's design-time total already sits at
    // the +12 dB makeup clamp, nullifying any in-limit trim there.
    // Rebase round 3: values identified with the split-mode width
    // remap (below) active; the remap retains side energy that the
    // legacy stage scaled down, raising the dual-mono IR energy by
    // +0.24...+0.59 dB, and these trims absorb that so the patch as a
    // whole stays level-neutral at preset defaults.
    static constexpr std::array<float, NUM_ALGORITHMS> SPLIT8_MAKEUP_DB = { {
        // Room1     Room2     Hall1     Hall2     Plate     Spring    Goldfoil  Inchindown
        -0.48f,     -0.26f,     -0.18f,     -0.10f,     -0.08f,     -0.31f,     0.0f,       1.30f
    } };

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

        // phase3/stereo SPLIT8: per-algorithm band recalibration for the
        // split (per-ear) networks (see SPLIT8_BAND_CORR above). Applied
        // to the design targets only; the audio path is untouched.
        // Goldfoil keeps the legacy shared design (identity).
        if (safeAlgo != 6) {
            const auto& splitCorr = SPLIT8_BAND_CORR[safeAlgo];
            for (int b = 0; b < NUM_BANDS; ++b)
                scaledRT60[b] *= splitCorr[b];
        }

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
        currentDesignRT60_1k = effectiveRT60[5];

#if AMBIENCE_USE_STAGE2_ABSORPTION
        // ★ 状態変数（s1, s2）のコピーは完全に排除！新係数のみを非アクティブ側（targetIsB）に書き込む
        // Phase3 PL-3: APF の群遅延は周波数依存 (Schroeder APF:
        // tau(w) = m (1-g^2) / (1+g^2+2g cos(w m))) で、公称遅延和と最大で
        // (1+g)/(1-g) 倍ずれる。バンド中心ごとに実効ループ長を求めて設計する。
        // g は setParams と同一式で再計算する (設計時点の目標値)。
        const float diffForDesign = std::clamp(activeParams.diffusion * diffusionSensitivity, 0.0f, 1.0f);
        const float lateDensityScaleForDesign = std::clamp(activeParams.lateDensity / 0.7f, 0.1f, 1.4286f);
        const float gApfDesign = std::clamp(
            apfGain * std::pow(diffForDesign, 0.75f) * 0.76f * lateDensityScaleForDesign,
            0.0f, 0.7071f);
        // Phase3 PL-3d: per-band design is applied ONLY to Spring.
        // Measured: Spring cepstrum 21.5 -> 17.2 dB (adopt). On Plate it
        // improved band T30 further (250 Hz +19 -> +13%) but worsened
        // cepstrum (+1.2 dB), NED (0 -> 1.4%) and the 205 Hz tone
        // prominence, so Plate keeps PL-2 + the nominal P2 design.
        // Room/Hall/Inchindown were neutral, Goldfoil negative
        // (ring 35.6 -> 39.9 dB): they also keep the P2 design.
        const bool usePerBandDesign =
            (currentTopology == ReverbTopology::Spring);
        for (int i = 0; i < FDN_ORDER; ++i) {
            // Phase2 P2: 実際のループ周期で設計する。帰還路には FDN ディレイ本体に加えて
            // 直列3段 APF の遅延合計と fbVec の 1 サンプルが含まれる。従来は本体長だけで
            // 設計していたため、実効 T60 が (本体+APF)/本体 倍に伸びていた（実測で確認）。
            if (!usePerBandDesign) {
                float apfDelaySum = 0.0f;
                for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                    apfDelaySum += cachedApfBaseDelaySmp[i][s];
                const int effectiveLoopSamples = static_cast<int>(std::round(
                    fdnBaseDelaySamples[i] + apfDelaySum + 1.0f));
                auto s2n = fitter.designStage2(
                    effectiveLoopSamples, fs, scaledRT60,
                    activeParams.hfDamping, activeParams.lfAbsorption);
                for (int b = 0; b < NUM_BANDS; ++b) {
                    if (targetIsB) absorptionCoeffsS2_B[i][b] = s2n.geqStages[b];
                    else           absorptionCoeffsS2_A[i][b] = s2n.geqStages[b];
                }
                continue;
            }
            std::array<float, NUM_BANDS> loopPerBand;
            for (int b = 0; b < NUM_BANDS; ++b) {
                float loopB = fdnBaseDelaySamples[i] + 1.0f;
                for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                    const float mApf = cachedApfBaseDelaySmp[i][s];
                    // tau(f) oscillates with period fs/m Hz; sampling it at the
                    // band centre alone is meaningless at HF. Average tau over
                    // the octave band (8 log-spaced points): tends to m at HF,
                    // captures the smooth LF peak (f ~ fs/(2m)) faithfully.
                    double tauSum = 0.0;
                    for (int k = 0; k < 8; ++k) {
                        const double frac = (static_cast<double>(k) + 0.5) / 8.0;
                        const double fEval = static_cast<double>(BAND_FREQ[b])
                                           * std::pow(2.0, frac - 0.5);
                        const double w = 2.0 * 3.141592653589793 * fEval
                                       * static_cast<double>(mApf) / fs;
                        const double denom = 1.0 + static_cast<double>(gApfDesign) * gApfDesign
                                           + 2.0 * gApfDesign * std::cos(w);
                        tauSum += static_cast<double>(mApf) * (1.0 - static_cast<double>(gApfDesign) * gApfDesign)
                                / std::max(0.01, denom);
                    }
                    loopB += static_cast<float>(tauSum / 8.0);
                }
                loopPerBand[b] = loopB;
            }
            auto s2 = fitter.designStage2PerBand(
                loopPerBand, fs, scaledRT60,
                activeParams.hfDamping, activeParams.lfAbsorption,
                true); // Spring: オクターブ較正 (phase3/spring8k/SPRING8K.md)
            for (int b = 0; b < NUM_BANDS; ++b) {
                if (targetIsB) {
                    absorptionCoeffsS2_B[i][b] = s2.geqStages[b];
                } else {
                    absorptionCoeffsS2_A[i][b] = s2.geqStages[b];
                }
            }
        }
        // ─────────────────────────────────────────────────────────────
        // Phase3 plate234 candidate B3: APF group-delay compensation stage
        // (stage index NUM_BANDS). The serial Schroeder APFs inflate the
        // true loop period tau(f) above the nominal period used by the P2
        // design, peaking where the stage-3 group delay peaks
        // (f = fs / (2*m3) = 195-250 Hz at 48 kHz, staggered per channel).
        // Modal decay rate = per-trip loss * fs / tau(f) therefore dips
        // ~24% around 205-225 Hz, leaving a slow modal basin that rings.
        // This stage adds a shallow peaking cut at the tau-peak frequency
        // whose depth is derived from the same tau model, restoring a
        // flat modal decay rate. Plate only; every other topology gets
        // an exact identity stage (bit-transparent).
        {
            if (currentTopology == ReverbTopology::Plate) {
                const double q = 2.5;
                // Per-channel design: each channel's tau peak sits at its
                // own stage-3 frequency fs / (2*m3_i) (195-250 Hz), and the
                // excess (tau_i - nominal_i) is largest for the shortest
                // channels, whose modes dominate the late tail. A single
                // shared depth under-doses exactly those channels, so the
                // cut is designed per channel from its own tau model.
                for (int i = 0; i < FDN_ORDER; ++i) {
                    const double m3 = static_cast<double>(cachedApfBaseDelaySmp[i][2]);
                    const double fc = std::clamp(fs / (2.0 * m3), 150.0, 300.0);
                    const double w = 2.0 * 3.141592653589793 * fc / fs;
                    double nom = static_cast<double>(fdnBaseDelaySamples[i]) + 1.0;
                    double tau = static_cast<double>(fdnBaseDelaySamples[i]) + 1.0;
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const double m = static_cast<double>(cachedApfBaseDelaySmp[i][s]);
                        nom += m;
                        const double den = 1.0 + gApfDesign * gApfDesign
                            + 2.0 * gApfDesign * std::cos(w * m);
                        tau += m * (1.0 - gApfDesign * gApfDesign) / std::max(0.01, den);
                    }
                    // design decay rate at fc from the (scaled) RT60
                    // targets, log-interpolated between the 125/250 Hz bands
                    const double frac = std::clamp(std::log2(fc / 125.0), 0.0, 1.0);
                    const double rt60fc = std::exp(
                        (1.0 - frac) * std::log(static_cast<double>(scaledRT60[2]))
                        + frac * std::log(static_cast<double>(scaledRT60[3])));
                    const double designRate = 60.0 / std::max(0.05, rt60fc); // dB/s
                    double depthDb = designRate * (tau - nom) / fs;
                    depthDb = std::clamp(depthDb, 0.0, 0.45);
                    // RBJ peaking cut, gain = -depthDb
                    const double A = std::pow(10.0, -depthDb / 40.0);
                    const double alpha = std::sin(w) / (2.0 * q);
                    const double cw = std::cos(w);
                    const double a0 = 1.0 + alpha / A;
                    BiquadCoeffs comp{};
                    comp.b0 = static_cast<float>((1.0 + alpha * A) / a0);
                    comp.b1 = static_cast<float>((-2.0 * cw) / a0);
                    comp.b2 = static_cast<float>((1.0 - alpha * A) / a0);
                    comp.a1 = static_cast<float>((-2.0 * cw) / a0);
                    comp.a2 = static_cast<float>((1.0 - alpha / A) / a0);
                    if (targetIsB) absorptionCoeffsS2_B[i][NUM_BANDS] = comp;
                    else           absorptionCoeffsS2_A[i][NUM_BANDS] = comp;
                }
            } else {
                const BiquadCoeffs identity{};
                for (int i = 0; i < FDN_ORDER; ++i) {
                    if (targetIsB) absorptionCoeffsS2_B[i][NUM_BANDS] = identity;
                    else           absorptionCoeffsS2_A[i][NUM_BANDS] = identity;
                }
            }
        }
#endif

        float rt60Mid = 0.0f;
        for (int b = 2; b <= 7; ++b)
            rt60Mid += effectiveRT60[b];
        rt60Mid = std::max(0.1f, rt60Mid / 6.0f);
        currentRT60Mid = rt60Mid;

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

        // [phase3/modulation F3] RT60 連動の深さスケール (最大 ×3) は撤廃し 1.0 固定。
        // 変調の利益 (密度・平坦度) は深さの全条件で JND 未満と実測されたため、
        // 副作用 (持続音 AM・サイドバンド) だけを適正化する。深さ係数も
        // 0.0022 → 0.00055 (×0.25) に再校正済み (下記 depthSamples 参照)。
        modDepthScale = 1.0f;

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

        // SDN 吸音の更新 (安価な派生値のみ; [P6] で独立関数化)
        applySdnDerivedParams();
    }

    // [P6] SDN 側の派生パラメータ (damping / lpfCoeff / scattering) のみを更新する。
    // designStage2 の入力ではない erCrossoverMs / scattering の変更は、
    // GEQ 再設計を経ずにここだけで反映できる (出力はビット同一)。
    void UniversalEngine::applySdnDerivedParams() {
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
        // Stage 1: h = 1 (隣接ペアのバタフライ)
        const float t0  = a[0] + a[1];   const float t1  = a[0] - a[1];
        const float t2  = a[2] + a[3];   const float t3  = a[2] - a[3];
        const float t4  = a[4] + a[5];   const float t5  = a[4] - a[5];
        const float t6  = a[6] + a[7];   const float t7  = a[6] - a[7];
        const float t8  = a[8] + a[9];   const float t9  = a[8] - a[9];
        const float t10 = a[10] + a[11]; const float t11 = a[10] - a[11];
        const float t12 = a[12] + a[13]; const float t13 = a[12] - a[13];
        const float t14 = a[14] + a[15]; const float t15 = a[14] - a[15];

        // Stage 2: h = 2 (間隔2ペアのバタフライ)
        const float u0  = t0 + t2;   const float u2  = t0 - t2;
        const float u1  = t1 + t3;   const float u3  = t1 - t3;
        const float u4  = t4 + t6;   const float u6  = t4 - t6;
        const float u5  = t5 + t7;   const float u7  = t5 - t7;
        const float u8  = t8 + t10;  const float u10 = t8 - t10;
        const float u9  = t9 + t11;  const float u11 = t9 - t11;
        const float u12 = t12 + t14; const float u14 = t12 - t14;
        const float u13 = t13 + t15; const float u15 = t13 - t15;

        // Stage 3: h = 4 (間隔4ペアのバタフライ)
        const float v0  = u0 + u4;   const float v4  = u0 - u4;
        const float v1  = u1 + u5;   const float v5  = u1 - u5;
        const float v2  = u2 + u6;   const float v6  = u2 - u6;
        const float v3  = u3 + u7;   const float v7  = u3 - u7;
        const float v8  = u8 + u12;  const float v12 = u8 - u12;
        const float v9  = u9 + u13;  const float v13 = u9 - u13;
        const float v10 = u10 + u14; const float v14 = u10 - u14;
        const float v11 = u11 + u15; const float v15 = u11 - u15;

        // Stage 4: h = 8 (間隔8ペアのバタフライ & 1/sqrt(16) = 0.25f ユニタリ正規化)
        constexpr float s = 0.25f;
        a[0]  = (v0 + v8) * s;   a[8]  = (v0 - v8) * s;
        a[1]  = (v1 + v9) * s;   a[9]  = (v1 - v9) * s;
        a[2]  = (v2 + v10) * s;  a[10] = (v2 - v10) * s;
        a[3]  = (v3 + v11) * s;  a[11] = (v3 - v11) * s;
        a[4]  = (v4 + v12) * s;  a[12] = (v4 - v12) * s;
        a[5]  = (v5 + v13) * s;  a[13] = (v5 - v13) * s;
        a[6]  = (v6 + v14) * s;  a[14] = (v6 - v14) * s;
        a[7]  = (v7 + v15) * s;  a[15] = (v7 - v15) * s;
    }

    inline void UniversalEngine::applySignFlipping(std::array<float, 16>& v) noexcept {
        static constexpr float s[16] = {
            1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f
        };
        for (int i = 0; i < 16; ++i) v[i] *= s[i];
    }

    // phase3/stereo SPLIT8 (STEREO_IMPL.md): 8-point unitary FWHT
    // (1/sqrt(8) scaling) + sign flipping for the per-ear partition
    // networks. Sign patterns reuse the two halves of the 16ch
    // applySignFlipping table.
    static inline void stereoFwht8(std::array<float, 8>& a) noexcept {
        const float t0 = a[0] + a[1], t1 = a[0] - a[1];
        const float t2 = a[2] + a[3], t3 = a[2] - a[3];
        const float t4 = a[4] + a[5], t5 = a[4] - a[5];
        const float t6 = a[6] + a[7], t7 = a[6] - a[7];
        const float u0 = t0 + t2, u2 = t0 - t2;
        const float u1 = t1 + t3, u3 = t1 - t3;
        const float u4 = t4 + t6, u6 = t4 - t6;
        const float u5 = t5 + t7, u7 = t5 - t7;
        constexpr float s = 0.35355339f; // 1/sqrt(8)
        a[0] = (u0 + u4) * s; a[4] = (u0 - u4) * s;
        a[1] = (u1 + u5) * s; a[5] = (u1 - u5) * s;
        a[2] = (u2 + u6) * s; a[6] = (u2 - u6) * s;
        a[3] = (u3 + u7) * s; a[7] = (u3 - u7) * s;
    }
    static inline void stereoSignFlip8(std::array<float, 8>& v, bool secondHalf) noexcept {
        static constexpr float s0[8] = { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f };
        static constexpr float s1[8] = { 1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f };
        const float* s = secondHalf ? s1 : s0;
        for (int i = 0; i < 8; ++i) v[i] *= s[i];
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

        // phase3/stereo SPLIT8: per-algorithm late-energy makeup trim
        // (see SPLIT8_MAKEUP_DB). Exactly 1.0f for Goldfoil, so its
        // renders stay bit-identical to the current head.
        const int curAlgoIdx = juce::jlimit(0, NUM_ALGORITHMS - 1, activeParams.algorithmIndex);
        const float splitMakeupLin = (curAlgoIdx != 6)
            ? juce::Decibels::decibelsToGain(SPLIT8_MAKEUP_DB[curAlgoIdx]) : 1.0f;

        // [P6] GEQ 再設計を伴わない SDN 派生値の即時反映
        if (sdnParamsUpdatePending) {
            applySdnDerivedParams();
            sdnParamsUpdatePending = false;
        }

        // ★ サンプルベース・レート制御 ＆ ビジー保護トリガー (15ms間隔 / アイドル時のみ遷移)
        samplesSinceLastAbsorptionUpdate += numSamples;
        const bool isCrossfadeIdle = (absoFadeState == AbsoFadeState::IdleAtA || absoFadeState == AbsoFadeState::IdleAtB);
        if (absorptionUpdatePending && isCrossfadeIdle && (samplesSinceLastAbsorptionUpdate >= absorptionRateLimitIntervalSamples)) {
            const bool targetIsB = (absoFadeState == AbsoFadeState::IdleAtA);
            updateAbsorptionFilters(targetIsB);
            // [inchindown-decay fix] The tank's RT60 feed lives only in
            // updateTopologyAndRouting, which a Decay-knob turn (absorption-
            // only update) never reaches: without this re-feed the tank
            // keeps its load-time RT60 (35 s at the preset) at every knob
            // position while the FDN tracks. Re-feed with the fresh design
            // value, same arguments as the topology path.
            if (currentTopology == ReverbTopology::Inchindown) {
                const float erSizeScaleTank = 0.5f + activeParams.roomSizeScale;
                inchindownEngine.setParameters(currentDesignRT60_1k, activeParams.hfDamping, erSizeScaleTank, 1.0f);
            }
            // ★ 状態変数ディープコピー（クリックノイズ完全ゼロの状態転送）
            if (targetIsB) {
                for (int i = 0; i < FDN_ORDER; ++i) {
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        absorptionFiltersS2_B[i][s] = absorptionFiltersS2_A[i][s];
                    }
                }
            } else {
                for (int i = 0; i < FDN_ORDER; ++i) {
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        absorptionFiltersS2_A[i][s] = absorptionFiltersS2_B[i][s];
                    }
                }
            }
            absoFadeState = targetIsB ? AbsoFadeState::FadingToB : AbsoFadeState::FadingToA;
            samplesSinceLastAbsorptionUpdate = 0;
            absorptionUpdatePending = false;
        }

        const float fsf = static_cast<float>(fs);
        
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

        // ★ Clarity ゲインのブロック単位事前計算 (毎サンプルの std::pow / decibelsToGain を完全排除)
        const float cDb = std::clamp(activeParams.clarityDB, -6.0f, 6.0f);
        const float clarityShift = cDb * 0.25f; // 最大 ±1.5dB
        const float gClarityER   = juce::Decibels::decibelsToGain(+clarityShift);
        const float gClarityLate = juce::Decibels::decibelsToGain(-clarityShift);

        // ★ Schroeder APF (s=2) safeMod の定常時事前計算 (毎サンプルの std::tanh を完全排除)
        const bool modAmountStationary = (std::abs(activeParams.modAmount - smoothedModAmount) < 1e-6f);
        if (modAmountStationary) {
            smoothedModAmount = activeParams.modAmount;
        }
        std::array<float, FDN_ORDER> cachedApfSafeMod{};
        bool usePrecomputedSafeMod = false;
        if (modAmountStationary) {
            const float modAmtCurvedStat = smoothedModAmount * smoothedModAmount;
            const float depthSamplesStat = modAmtCurvedStat * 0.00055f * fsf * modDepthScale;
            if (depthSamplesStat > 0.01f) {
                for (int i = 0; i < FDN_ORDER; ++i) {
                    const float baseDelay = cachedApfBaseDelaySmp[i][2];
                    const float maxSafeMod = baseDelay * 0.15f;
                    const float targetMod = depthSamplesStat * apfModFrac[2] * cachedFreqModScales[i];
                    const float knee = maxSafeMod * 0.75f;
                    float safeMod = targetMod;
                    if (targetMod > knee) {
                        const float excess = targetMod - knee;
                        const float room = maxSafeMod - knee;
                        safeMod = knee + room * std::tanh(excess / room);
                    }
                    cachedApfSafeMod[i] = safeMod;
                }
            }
            usePrecomputedSafeMod = true;
        }

        // ★ SDN / 2D Mesh 注入ゲインのブロック単位事前計算 (毎サンプルの std::sqrt を完全排除)
        const float lateDensityScale = std::clamp(activeParams.lateDensity, 0.1f, 1.0f);
        const float lateInjectGain = 0.7071f * std::sqrt(lateDensityScale / 0.7f);

        // ★ ISM (鏡像法) 初期反射エンジンによる並列 AVX2 処理
        if (!bypassER) {
            if (ismBufferL.size() < static_cast<size_t>(numSamples)) {
                ismBufferL.resize(static_cast<size_t>(numSamples), 0.0f);
                ismBufferR.resize(static_cast<size_t>(numSamples), 0.0f);
            }
            std::fill(ismBufferL.begin(), ismBufferL.begin() + numSamples, 0.0f);
            std::fill(ismBufferR.begin(), ismBufferR.begin() + numSamples, 0.0f);

            // 未使用の ismSeedBuffer を排除し nullptr を渡すことで不要なメモリ書き込みをスキップ
            ismEngine.processBlock(inL, inR, ismBufferL.data(), ismBufferR.data(), nullptr, numSamples, activeParams.erLevel);
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

            // ★ 線形（凸結合）ゲイン計算（アイドル時は分岐のみ、フェード時も乗算のみ）
            // [DecayKnob fix] 旧実装の等パワー (cos/sin) 合成は「無相関な2信号」
            // 前提だが、A/B 両バンクは同一入力・同一初期状態から分岐する強相関
            // 信号であり、実測では全帯域・全フェード位置で合成ゲインが端点に
            // 対し最大 +3.01 dB (cos+sin の理論最大) 上乗せされていた。FDN
            // ループ内ではこの上乗せが周回ごとに蓄積し、Decay ドラッグ中に
            // テイルが再上昇する (最大 +50 dB 級の包絡膨らみ、Plate 低域)。
            // 線形フェードは凸結合であり、各周波数で合成応答が複素平面上の
            // 線分上に収まるため |H| が端点の最大を超えないことが保証される。
            // 詳細: phase3/decay-knob/DECAY_KNOB.md
            float absoGainA = 1.0f;
            float absoGainB = 0.0f;
            if (absoFadeState == AbsoFadeState::IdleAtA) [[likely]] {
                absoGainA = 1.0f;
                absoGainB = 0.0f;
            } else if (absoFadeState == AbsoFadeState::IdleAtB) {
                absoGainA = 0.0f;
                absoGainB = 1.0f;
            } else {
                absoGainA = 1.0f - absoCrossfadePos;
                absoGainB = absoCrossfadePos;
            }

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
            const float depthSamples = modAmtCurved * 0.00055f * fsf * modDepthScale;

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

            // phase3/stereo SPLIT8 (STEREO_IMPL.md): Goldfoil keeps the
            // legacy shared-FDN path below bit-exactly (its structural
            // mesh feed is a shared cross-path that needs a per-ear
            // redesign first); all other topologies run the per-ear
            // split FDN. In split mode the two variables below carry the
            // per-ear streams (fdnInputMid = L stream, fdnInputSide = R
            // stream) instead of Mid/Side.
            const bool splitStereo = (currentTopology != ReverbTopology::Goldfoil);

            // ★ Mid / Side 双方を 6段ディフューザーで完全拡散 (ハードバイパス撤廃・サンプル線形平滑化)
            // SPLIT8: the two diffuser chains carry the raw per-ear
            // signals (M chain = L, S chain = R) instead of Mid/Side.
            // Position information lives in the correlation between Mid
            // and Side; diffusing them through separate chains destroys
            // that correlation, after which L = M+S and R = M-S have
            // equal energy and no split FDN can recover the position
            // (phase3/stereo/STEREO.md §2a, §3). Each chain keeps its own
            // delay set: in split mode the M chain's prime set
            // (DIFF_SMP_M) serves L and the S chain's (DIFF_SMP_S) serves
            // R, with the same per-stage taper — the 6-stage extension of
            // the assignment the SPLIT8 prep used on the 4-stage sets.
            float fdnInputMid = splitStereo ? inLpfStateL : midIn;
            float fdnInputSide = splitStereo ? inLpfStateR : sideIn;
            if (!bypassInputDiffusers && inputDiffusersValid) {
                // [phase3/diffusion 候補(a)] 段別ゲインテーパ (Dattorro 型:
                // 前段を強く、後段を弱く)。curDiffuserGain は段1のゲイン。
                static constexpr float DIFF_TAPER[INPUT_DIFFUSER_STAGES] = { 1.0f, 1.0f, 0.9286f, 0.8333f, 0.8333f, 0.7743f };
                for (int i = 0; i < INPUT_DIFFUSER_STAGES; ++i) {
                    const float stageGain = curDiffuserGain * DIFF_TAPER[i];
                    float dm = inputDiffusersM[i].read(cachedDiffuserDelaySmpM[i]);
                    float wm = fdnInputMid + stageGain * dm;
                    if (!std::isfinite(wm)) [[unlikely]] wm = 0.0f;
                    inputDiffusersM[i].write(wm);
                    fdnInputMid = dm - stageGain * wm;

                    float ds = inputDiffusersS[i].read(cachedDiffuserDelaySmpS[i]);
                    float ws = fdnInputSide + stageGain * ds;
                    if (!std::isfinite(ws)) [[unlikely]] ws = 0.0f;
                    inputDiffusersS[i].write(ws);
                    fdnInputSide = ds - stageGain * ws;
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
                // SDN / 2D Mesh 散乱出力をFDNへ注入 (LateDensity 連動ハイブリッド結合: 事前計算ゲイン適用)
                if (splitStereo) {
                    // Per-ear streams: inject each ear's core output into
                    // its own stream. The factor 2 matches the legacy
                    // design's excitation for a mono source (Mid receives
                    // (erL+erR)*g, i.e. 2*e*g when erL == erR == e).
                    fdnInputMid += erOutL * lateInjectGain * 2.0f;
                    fdnInputSide += erOutR * lateInjectGain * 2.0f;
                } else {
                    fdnInputMid += (erOutL + erOutR) * lateInjectGain;
                    fdnInputSide += (erOutL - erOutR) * lateInjectGain;
                }
            }

            // Goldfoil structural feed (phase3/goldfoil/STRUCTURAL.md):
            // lowpass the per-channel mesh pickup vector (fdnMeshInject,
            // computed above by the 2D mesh but otherwise unused) at 1 kHz,
            // then through the 6th-order Butterworth stage at 2.2 kHz
            // (FEED8K.md: keeps the <= 3 kHz zero-filling band, removes the
            // bright 3-6 kHz content that inflates the HF tail).
            // The conditioned vector is added to the FDN delay-line inputs
            // in the channel loop below. The mesh's decorrelated per-channel
            // excitation breaks the fixed phasor geometry of the single
            // broadcast FDN input, flattening the deep transmission zeros
            // the long-delay FDN otherwise produces at non-design room
            // sizes; the lowpass keeps the mesh's HF modal content out of
            // the sustained tail. Feedforward only: loop poles unchanged.
            if (currentTopology == ReverbTopology::Goldfoil) {
                for (int i = 0; i < FDN_ORDER; ++i) {
                    gfMeshFeedLpState[i] += gfMeshFeedLpCoeff * (fdnMeshInject[i] - gfMeshFeedLpState[i]);
                    float v = gfMeshFeedLpState[i];
                    for (int s = 0; s < 3; ++s) {
                        const auto& c = gfMeshFeedBqCoeffs[s];
                        const float y = c[0] * v + c[1] * gfMeshFeedBqX1[s][i] + c[2] * gfMeshFeedBqX2[s][i]
                                      - c[3] * gfMeshFeedBqY1[s][i] - c[4] * gfMeshFeedBqY2[s][i];
                        gfMeshFeedBqX2[s][i] = gfMeshFeedBqX1[s][i]; gfMeshFeedBqX1[s][i] = v;
                        gfMeshFeedBqY2[s][i] = gfMeshFeedBqY1[s][i]; gfMeshFeedBqY1[s][i] = y;
                        v = y;
                    }
                    fdnMeshInject[i] = v;
                }
            }

            // ★ 空間相関の対称性破壊 (Asymmetric Injection / Extraction)
            static constexpr float injectSign[16]  = { 1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f };
            static constexpr float extractSign[16] = { 1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f, -1.0f };

            std::array<float, 16> currentFb = fbVec;
            if (splitStereo) {
                // Partition mixing: even channels form the L network, odd
                // channels the R network; each gets its own 8ch FWHT, so
                // no energy crosses between the ears inside the loop.
                std::array<float, 8> blkA, blkB;
                for (int k = 0; k < 8; ++k) { blkA[k] = currentFb[2 * k]; blkB[k] = currentFb[2 * k + 1]; }
                stereoFwht8(blkA); stereoFwht8(blkB);
                stereoSignFlip8(blkA, false); stereoSignFlip8(blkB, true);
                for (int k = 0; k < 8; ++k) { currentFb[2 * k] = blkA[k]; currentFb[2 * k + 1] = blkB[k]; }
            } else {
                fastWalshHadamardTransform(currentFb);
                applySignFlipping(currentFb);
            }

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
                if (currentFdnDelaySamples[i] == targetSmp && fdnReadCacheTarget[i] == targetSmp) [[likely]] {
                    // ★ 定常時高速経路: 遅延は目標値に一致したまま静止しているため、
                    //   引数は事前計算済みのものを使う（値は従来経路の計算と完全同一）
                    d = fdnReadCacheIsInt[i] ? fdnDelays[i].readInt(fdnReadCacheId[i])
                                             : fdnDelays[i].readPrepared(fdnReadCacheId[i], fdnReadCacheFrac[i]);
                } else {
                    const int intDelay = static_cast<int>(std::round(currentFdnDelaySamples[i]));
                    if (std::abs(currentFdnDelaySamples[i] - static_cast<float>(intDelay)) < 1e-5f) {
                        d = fdnDelays[i].readInt(intDelay);
                        if (currentFdnDelaySamples[i] == targetSmp) {
                            fdnReadCacheTarget[i] = targetSmp;
                            fdnReadCacheIsInt[i] = 1;
                            fdnReadCacheId[i] = intDelay;
                        }
                    } else {
                        d = fdnDelays[i].read(currentFdnDelaySamples[i] - 1.0f);
                        if (currentFdnDelaySamples[i] == targetSmp && fdnDelays[i].hasBuffer()) {
                            int pid; float pfrac;
                            fdnDelays[i].prepareReadArgs(currentFdnDelaySamples[i] - 1.0f, pid, pfrac);
                            fdnReadCacheTarget[i] = targetSmp;
                            fdnReadCacheIsInt[i] = 0;
                            fdnReadCacheId[i] = pid;
                            fdnReadCacheFrac[i] = pfrac;
                        }
                    }
                }

#if AMBIENCE_USE_STAGE2_ABSORPTION
                // ★ 状態転送付きシングルバンク駆動（定常時は160基のみ計算、フェード時のみ両バンク計算）
                if (absoFadeState == AbsoFadeState::IdleAtA) [[likely]] {
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        d = absorptionFiltersS2_A[i][s].tick(d, absorptionCoeffsS2_A[i][s]);
                    }
                } else if (absoFadeState == AbsoFadeState::IdleAtB) {
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        d = absorptionFiltersS2_B[i][s].tick(d, absorptionCoeffsS2_B[i][s]);
                    }
                } else {
                    // クロスフェード中（30ms間）のみ両バンクを計算し、等パワー合成
                    float outA = d;
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        outA = absorptionFiltersS2_A[i][s].tick(outA, absorptionCoeffsS2_A[i][s]);
                    }
                    float outB = d;
                    for (int s = 0; s < ABSO_STAGES_S2; ++s) {
                        outB = absorptionFiltersS2_B[i][s].tick(outB, absorptionCoeffsS2_B[i][s]);
                    }
                    d = outA * absoGainA + outB * absoGainB;
                }
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
                        float safeMod;
                        if (usePrecomputedSafeMod) [[likely]] {
                            safeMod = cachedApfSafeMod[i];
                        } else {
                            const float maxSafeMod = baseDelay * 0.15f;
                            const float targetMod = depthSamples * apfModFrac[s] * cachedFreqModScales[i];
                            const float knee = maxSafeMod * 0.75f;
                            safeMod = targetMod;
                            if (targetMod > knee) {
                                const float excess = targetMod - knee;
                                const float room = maxSafeMod - knee;
                                safeMod = knee + room * std::tanh(excess / room);
                            }
                        }
                        const float apfDelaySmp = baseDelay + chorusVal * safeMod;
                        delayed = nestedAllpassDelays[i][s].read(apfDelaySmp);
                    } else {
                        delayed = nestedAllpassDelays[i][s].readInt(cachedApfBaseDelayInt[i][s]);
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

                float fdnInputForThisCh;
                if (splitStereo) {
                    // Each channel is fed only by its own ear's stream.
                    fdnInputForThisCh = (((i & 1) == 0) ? fdnInputMid : fdnInputSide) * injectSign[i] * 0.25f;
                } else {
                    const float sideForCh = (i % 2 == 0 ? +fdnInputSide : -fdnInputSide) * sideBoost;
                    fdnInputForThisCh = (fdnInputMid * injectSign[i] + sideForCh) * 0.25f;
                }
                float fdnWriteVal = fdnInputForThisCh + currentFb[i];
                // Goldfoil structural feed: add the lowpassed per-channel
                // mesh pickup (see the conditioning block above). The gain
                // is relative to the raw pickup values (which already carry
                // the mesh's MESH_SIGN extraction signs).
                if (currentTopology == ReverbTopology::Goldfoil)
                    fdnWriteVal += fdnMeshInject[i] * 4.0f;
                if (!std::isfinite(fdnWriteVal)) [[unlikely]] fdnWriteVal = 0.0f;
                fdnDelays[i].write(fdnWriteVal);

                const float extractedOut = limitedApfOut * extractSign[i];
                if ((i & 1) == 0) evenSum += extractedOut;
                else              oddSum  += extractedOut;
            }

            float fdnOutL, fdnOutR;
            if (splitStereo) {
                // Per-ear extraction: no crossLeak. Stereo width is still
                // applied by the final Mid/Side stage below, unchanged.
                fdnOutL = evenSum * 0.353553f;
                fdnOutR = oddSum * 0.353553f;
            } else {
                const float curCrossLeak = 1.0f - curWidth;
                // 16ch FDNのL/R出力 (各8chの非相関サミング: 1/sqrt(8) = 0.3535f)
                fdnOutL = (evenSum + oddSum * curCrossLeak) * 0.353553f;
                fdnOutR = (oddSum + evenSum * curCrossLeak) * 0.353553f;
            }
            fbVec = nextFb;

            const float erMakeupGain = 2.5f; // 音響テスト・残響バランス完全維持

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
            const float lateMixL = fdnOutL * curLateMakeupGain * lateLevel * lateServo * modLossComp * gClarityLate * splitMakeupLin;
            const float lateMixR = fdnOutR * curLateMakeupGain * lateLevel * lateServo * modLossComp * gClarityLate * splitMakeupLin;

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
            // phase3/stereo width-cap revision (STEREO_REBASE.md): in split
            // mode the legacy M/S stage caps the late-field ILD at
            // 20*log10((1+w)/(1-w)) (Spring default w=0.70 -> 15.1 dB,
            // Room1 w=0.75 -> 16.9 dB), discarding most of the separation
            // the split FDN delivers (wet ILD at w=1: Spring 24.4 dB,
            // Room1 25.8 dB). Remap the width curve in split mode only:
            // wEff = 1-(1-w)^1.5 (endpoints and monotonicity preserved,
            // w=0 stays exact mono). Goldfoil never takes the split path,
            // so its renders stay bit-identical.
            const float effWidth = splitStereo
                ? 1.0f - (1.0f - curWidth) * std::sqrt(1.0f - curWidth)
                : curWidth;
            const float mid  = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * effWidth;
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