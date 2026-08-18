#include "UniversalEngine.h"

namespace FDNReverb {

    namespace {
        static bool isMathPrime(int n) noexcept {
            if (n < 2)  return false;
            if (n == 2) return true;
            if (n % 2 == 0) return false;
            for (int i = 3; i * i <= n; i += 2)
                if (n % i == 0) return false;
            return true;
        }

        static int findNearestUniquePrime(int target,
            const std::array<int, 16>& usedPrimes,
            int usedCount) noexcept {
            target = std::max(target, 2);
            for (int offset = 0; offset < 100000; ++offset) {
                int hi = target + offset;
                if (isMathPrime(hi)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == hi) { used = true; break; }
                    if (!used) return hi;
                }
                int lo = target - offset;
                if (offset > 0 && lo >= 2 && isMathPrime(lo)) {
                    bool used = false;
                    for (int k = 0; k < usedCount; ++k)
                        if (usedPrimes[k] == lo) { used = true; break; }
                    if (!used) return lo;
                }
            }
            return target;
        }
    } // anonymous namespace

    UniversalEngine::UniversalEngine() {
        fbVec.fill(0.0f);
        constexpr float phi = 1.6180339887f;
        for (int i = 0; i < FDN_ORDER; ++i) {
            lfos[i].state = 12345u + static_cast<uint32_t>(i) * 9876u;
            lfos[i].smoothed = 0.0f;
            const float angle = static_cast<float>(i) * phi;
            const float frac = angle - std::floor(angle);
            lfos[i].rateMultiplier = 0.80f + frac * 0.40f;

            // 笘・繧ｳ繝ｼ繝ｩ繧ｹLFO: 繝弱う繧ｺLFO縺ｨ縺ｯ逡ｰ縺ｪ繧九が繝輔そ繝・ヨ縺ｧ鮟・≡豈泌・蟶・
            const float cAngle = static_cast<float>(i + 5) * phi;
            chorusLFOs[i].phase = cAngle - std::floor(cAngle);
            const float cRateAngle = static_cast<float>(i + 11) * phi;
            chorusLFOs[i].rateScale = 0.30f + (cRateAngle - std::floor(cRateAngle)) * 0.50f;
        }
    }

    void UniversalEngine::prepare(double sampleRate, int /*maxBlockSize*/) {
        fs = sampleRate;
        ChorusLFO::initTable();  // 笘・繧ｦ繧ｧ繝ｼ繝悶ユ繝ｼ繝悶Ν蛻晄悄蛹厄ｼ亥・蝗槭・縺ｿ螳溯｡鯉ｼ・

#if AMBIENCE_USE_STAGE2_ABSORPTION
        MagnitudeResponseFitter::precomputeInteractionMatrix(sampleRate);
#endif

        auto getPow2 = [](size_t s) -> size_t {
            size_t p = 1;
            while (p < s) p *= 2;
            return p;
            };

        size_t totalMemoryNeeded =
            getPow2(static_cast<size_t>(fs * 0.5))              // 笘・preDelay (max 500ms)
            + getPow2(static_cast<size_t>(fs * 1.0))
            + getPow2(static_cast<size_t>(fs * 0.05)) * 4
            + getPow2(static_cast<size_t>(fs * 0.5)) * FDN_ORDER
            + getPow2(static_cast<size_t>(fs * 0.05)) * FDN_ORDER * SERIAL_APF_STAGES;

        memoryPool.allocate(totalMemoryNeeded);

        int mask = 0;
        float* ptr = nullptr;

        // 笘・PreDelay (max 500ms)
        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 0.5), mask);
        preDelayLine.init(ptr, mask);

        ptr = memoryPool.requestMemory(static_cast<size_t>(fs * 1.0), mask);
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

        // 笘・DC繝悶Ο繝・き繝ｼ菫よ焚: fc 竕・5Hz 縺ｮ1谺｡HPF
        dcBlockerCoeff = 1.0f - (6.28318530718f * 5.0f / static_cast<float>(fs));
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);

        // 笘・Soft-knee繧ｳ繝ｳ繝・ RMS繧ｨ繝ｳ繝吶Ο繝ｼ繝嶺ｿよ焚 (~3ms遯・
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
        saturatorL.reset();
        saturatorR.reset();
        outputLimiter.reset();
        outputEQ.reset();
        duckingEnvelope = 0.0f;
        dcX1.fill(0.0f);
        dcY1.fill(0.0f);
        fdnRmsEnv.fill(0.0f);
        for (auto& dl : fdnDelays) dl.resetState();  // 笘・Thiran allpass迥ｶ諷九Μ繧ｻ繝・ヨ
        for (auto& chDelays : nestedAllpassDelays)    // 笘・繝阪せ繝・llpass Thiran迥ｶ諷九Μ繧ｻ繝・ヨ
            for (auto& dl : chDelays) dl.resetState();
        for (auto& lfo : lfos) lfo.smoothed = 0.0f;
    }

    void UniversalEngine::setParams(const DSPParams& p) {
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

        // 笘・PreDelay: ms 竊・繧ｵ繝ｳ繝励Ν謨ｰ縺ｫ螟画鋤
        preDelaySamples = p.preDelayMs * 0.001f * static_cast<float>(fs);

        outputEQ.setLoCutHz(p.loCutHz);
        outputEQ.setHiCutHz(p.hiCutHz);

        updateTopologyAndRouting();
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

        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  笘・竭｡菫ｮ豁｣: proMode 繝輔Λ繧ｰ縺ｫ髢｢菫ゅ↑縺丞ｸｸ縺ｫ Tilt / 蟶ｯ蝓溘ヮ繝悶ｒ驕ｩ逕ｨ縺吶ｋ
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //   譌ｧ螳溯｣・ if (activeParams.proMode) { ... }
        //   ProMode 繧・OFF 縺ｫ縺吶ｋ縺ｨ繝ｦ繝ｼ繧ｶ繝ｼ縺瑚ｨｭ螳壹＠縺・Tilt/蟶ｯ蝓溘′辟｡隕悶＆繧後・
        //   RT60 繧ｰ繝ｩ繝輔′蜈・・繧ｫ繝ｼ繝悶↓謌ｻ縺｣縺ｦ縺・◆縲・
        //
        //   譁ｰ螳溯｣・ 蟶ｸ縺ｫ驕ｩ逕ｨ縲ゅョ繝輔か繝ｫ繝亥､縺悟・縺ｦ 1.0f 縺ｪ縺ｮ縺ｧ縲・
        //   繝ｦ繝ｼ繧ｶ繝ｼ縺悟､画峩縺励※縺・↑縺代ｌ縺ｰ scaledRT60 縺ｫ螟牙喧縺ｯ縺ｪ縺上・
        //   繧｢繝ｫ繧ｴ繝ｪ繧ｺ繝蛻・崛譎ゅ↓ loadPresetDefaults() 縺・1.0f 縺ｫ繝ｪ繧ｻ繝・ヨ縺吶ｋ
        //   縺溘ａ縲∝憶菴懃畑縺ｯ荳蛻・↑縺・・
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
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

        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  笘・EDT 菫ｮ豁｣: 蜈ｨ蟶ｯ蝓溷ｹｳ蝮・喧縺ｧ LF/HF 陬懈ｭ｣繧貞渚譏
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //   譌ｧ螳溯｣・ effectiveRT60[4] (500Hz) 縺ｮ蜊倅ｸ繝舌Φ繝峨・縺ｿ菴ｿ逕ｨ
        //   竊・HF Damping 縺碁ｫ伜沺繧剃ｸ九￡縺ｦ繧・EDT 縺ｫ蜿肴丐縺輔ｌ縺ｪ縺・
        //   竊・LF Absorption 縺御ｽ主沺繧剃ｸ九￡縺ｦ繧・EDT 縺ｫ蜿肴丐縺輔ｌ縺ｪ縺・
        //
        //   譁ｰ螳溯｣・ 荳ｭ蝓溘ヰ繝ｳ繝会ｼ・25Hz縲・kHz = band 2縲・・峨・蟷ｳ蝮・､繧剃ｽｿ逕ｨ
        //   竊・閠ｳ縺ｫ閨ｴ縺薙∴繧・☆縺・ｸｯ蝓溘ｒ驥崎ｦ悶＠縺､縺､ LF/HF 陬懈ｭ｣縺ｮ蠖ｱ髻ｿ繧貞叙繧願ｾｼ繧
        //   竊・荳｡遶ｯ・・1Hz, 63Hz, 8kHz, 16kHz・峨・髯､螟厄ｼ亥ｿ・炊髻ｳ髻ｿ逧・↓ EDT 縺ｸ縺ｮ
        //     蟇・ｸ弱′蟆代↑縺・◆繧√∝､悶ｌ蛟､縺ｫ繧医ｋ荳榊ｮ牙ｮ壼喧繧帝亟縺撰ｼ・
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        float rt60Mid = 0.0f;
        for (int b = 2; b <= 7; ++b)
            rt60Mid += effectiveRT60[b];
        rt60Mid = std::max(0.1f, rt60Mid / 6.0f);

        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  笘・驥大ｱ樣浹蟇ｾ遲・(1): Decay萓晏ｭ倥・繧､繧ｯ繝ｭ繧ｵ繝√Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  FDN 繝ｫ繝ｼ繝怜・縺ｮ processMicroSaturation() 縺ｯ縲∫洒縺・ｮ矩涸縺ｧ縺ｯ貂ｩ縺九∩繧貞刈縺医ｋ縺後・
        //  髟ｷ縺・ｮ矩涸縺ｧ縺ｯ髱樒ｷ壼ｽ｢豁ｪ縺ｿ縺悟､壽焚蝗櫁塘遨阪＠縲√さ繝繝輔ぅ繝ｫ繧ｿ讒矩縺ｮ蜈ｱ魑ｴ蜻ｨ豕｢謨ｰ繧・
        //  蠑ｷ隱ｿ縺励※驥大ｱ樒噪縺ｪ繧ｭ繝ｼ繝ｳ髻ｳ繧貞ｼ輔″襍ｷ縺薙☆縲・
        //
        //  蟇ｾ遲・ RT60 荳ｭ蝓溷ｹｳ蝮・′ 2.0s 莉･荳九↑繧牙ｾ捺擂騾壹ｊ驕ｩ逕ｨ縲・.0s縲・.0s 縺ｧ貍ｸ貂帙・
        //        6.0s 莉･荳翫〒螳悟・繝舌う繝代せ縲・
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        microSatBlend = juce::jlimit(0.0f, 1.0f, 1.0f - (rt60Mid - 2.0f) / 4.0f);

        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  笘・驥大ｱ樣浹蟇ｾ遲・(2): Decay萓晏ｭ倥Δ繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ豺ｱ縺輔せ繧ｱ繝ｼ繝ｪ繝ｳ繧ｰ
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
        //  髟ｷ縺・ｮ矩涸縺ｻ縺ｩ縲√さ繝繝輔ぅ繝ｫ繧ｿ縺ｮ繝斐・繧ｯ繧偵⊂縺九☆縺溘ａ縺ｫ豺ｱ縺・Δ繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ縺悟ｿ・ｦ√・
        //  Lexicon / Strymon 遲峨・鬮伜刀菴阪Μ繝舌・繝悶・讓呎ｺ匁焔豕輔・
        //
        //  笘・繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ豺ｱ縺輔せ繧ｱ繝ｼ繝ｪ繝ｳ繧ｰ・域椛蛻ｶ迚茨ｼ・
        //  RT60 竕､ 1.0s 竊・1.0x (螟牙喧縺ｪ縺・
        //  RT60 = 3.0s 竊・2.0x
        //  RT60 竕･ 5.0s 竊・3.0x (荳企剞)
        // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
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
            apfGain = 0.3f;   diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Hall:
            bypassER = false; bypassInputDiffusers = false;
            apfGain = 0.618f; diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Plate:
            bypassER = false;  bypassInputDiffusers = false;
            apfGain = 0.5f;   diffusionSensitivity = 1.0f;
            break;
        case ReverbTopology::Spring:
            bypassER = false;  bypassInputDiffusers = false;
            apfGain = 0.6f;   diffusionSensitivity = 0.7f;
            break;
        case ReverbTopology::Goldfoil:
            bypassER = false;  bypassInputDiffusers = false;
            apfGain = 0.5f;   diffusionSensitivity = 0.8f;
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

        lateMakeupGainLinear = juce::Decibels::decibelsToGain(baseDB + decayCompDB + algoOffset);
    }

    inline void UniversalEngine::fastWalshHadamardTransform(
        std::array<float, 16>& v) noexcept
    {
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    float x = v[j], y = v[j + h];
                    v[j] = x + y;
                    v[j + h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) v[i] *= 0.25f;
    }

    inline void UniversalEngine::applySignFlipping(
        std::array<float, 16>& v) noexcept
    {
        static constexpr std::array<float, 16> flip = {
             1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f,  1.f,
             1.f,  1.f, -1.f, -1.f, -1.f, -1.f,  1.f,  1.f
        };
        for (int i = 0; i < 16; ++i) v[i] *= flip[i];
    }

    void UniversalEngine::processBlock(const float* inL, const float* inR,
        float* outL, float* outR,
        int numSamples) noexcept
    {
        // 笘・CPU譛驕ｩ蛹・ fs 繧・float 縺ｫ繧ｭ繝｣繝・す繝･・・rocessBlock 蜈ｨ蝓溘〒菴ｿ逕ｨ・・
        const float fsf = static_cast<float>(fs);

        // 笘・繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ豺ｱ縺・ 莠御ｹ励き繝ｼ繝・+ 繝吶・繧ｹ菫よ焚謚大宛
        //   modAmountﾂｲ 縺ｧ繝弱ヶ菴主沺繧堤ｷｩ繧・°縺ｫ縲・.001f 縺ｧ蜈ｨ菴捺ｷｱ縺輔ｒ蜊頑ｸ・
        //   譌ｧ: modAmt=0.5 竊・48smp(1ms) / 譁ｰ: modAmt=0.5 竊・12smp(0.25ms)
        const float targetModAmount = activeParams.modAmount;
        const float wetGain = juce::Decibels::decibelsToGain(activeParams.wetDB);
        const float stereoWidth = activeParams.stereoWidth;
        const float erLevel = activeParams.erLevel;
        const float lateLevel = activeParams.lateLevel;
        const bool  erSolo = activeParams.erSolo;
        const float duckThreshLin = juce::Decibels::decibelsToGain(activeParams.duckingThreshDB);
        const float duckAmountDB = activeParams.duckingAmount;

        const float effectiveDiffusion = activeParams.diffusion * diffusionSensitivity;
        const float diffuserGain = 0.25f + effectiveDiffusion * 0.55f;
        const float effectiveApfGain = apfGain * (0.60f + effectiveDiffusion * 0.40f);

        const float sideBoost = stereoWidth * 1.5f;
        const float erLeakage = (1.0f - stereoWidth) * 0.7f;

        // 笘・CPU譛驕ｩ蛹・ apfGainStage 縺ｯ繝ｫ繝ｼ繝嶺ｸ榊､・竊・莠句燕險育ｮ・
        const float apfGainStage = effectiveApfGain * 0.78f;

        // 笘・CPU譛驕ｩ蛹・ freqModScale 繧剃ｺ句燕險育ｮ暦ｼ・6ch蛻・ｼ・
        std::array<float, FDN_ORDER> freqModScales;
        constexpr float invFdnM1 = 1.0f / static_cast<float>(FDN_ORDER - 1);
        for (int i = 0; i < FDN_ORDER; ++i)
            freqModScales[i] = 0.5f + (1.0f - static_cast<float>(i) * invFdnM1) * 1.0f;

        // 笘・CPU譛驕ｩ蛹・ 蜈･蜉帙ョ繧｣繝輔Η繝ｼ繧ｶ縺ｮ繝・ぅ繝ｬ繧､譎る俣繧剃ｺ句燕險育ｮ・
        std::array<float, 4> diffuserDelaySmp;
        for (int i = 0; i < 4; ++i)
            diffuserDelaySmp[i] = (3.0f + i * 2.0f) * 0.001f * fsf;

        // 笘・CPU譛驕ｩ蛹・ Allpass繝吶・繧ｹ繝・ぅ繝ｬ繧､繧剃ｺ句燕險育ｮ暦ｼ・6ch ﾃ・3谿ｵ・・
        constexpr float apfBaseMs[SERIAL_APF_STAGES]   = { 1.5f, 2.3f, 3.7f };
        constexpr float apfSpreadMs[SERIAL_APF_STAGES] = { 0.73f, 0.91f, 1.13f };
        constexpr float apfModFrac[SERIAL_APF_STAGES]  = { 0.15f, 0.10f, 0.07f };
        const float msToSmp = 0.001f * fsf;
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> apfBaseDelaySmp;
        for (int i = 0; i < FDN_ORDER; ++i)
            for (int s = 0; s < SERIAL_APF_STAGES; ++s)
                apfBaseDelaySmp[i][s] = (apfBaseMs[s] + i * apfSpreadMs[s]) * msToSmp;

        // 笘・CPU譛驕ｩ蛹・ ER tapGain 縺ｮ * 0.5f 繧剃ｺ句燕險育ｮ・
        std::array<float, MAX_ER_TAPS> erTapGainsHalf;
        for (int t = 0; t < currentERTapCount; ++t)
            erTapGainsHalf[t] = currentERGains[t] * 0.5f;

        // 笘・CPU譛驕ｩ蛹・ soft-knee 髢ｾ蛟､縺ｮ莠御ｹ励ｒ莠句燕險育ｮ暦ｼ・qrt 蝗樣∩・・
        constexpr float compThresh = 0.35f;
        constexpr float compThreshSq = compThresh * compThresh;

        std::array<float, FDN_ORDER> lfoCoeffs;
        {
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i < FDN_ORDER; ++i) {
                const float fc = activeParams.modRate * lfos[i].rateMultiplier;
                lfoCoeffs[i] = juce::jlimit(0.0001f, 0.9999f,
                    1.0f - std::exp(-twoPi * fc / fsf));
                // 笘・繧ｳ繝ｼ繝ｩ繧ｹLFO繝ｬ繝ｼ繝域峩譁ｰ
                chorusLFOs[i].phaseInc = activeParams.modRate * chorusLFOs[i].rateScale / fsf;
            }
        }

                for (int n = 0; n < numSamples; ++n) {
            smoothedModAmount += (targetModAmount - smoothedModAmount) * 0.005f;
            const float modAmtCurved = smoothedModAmount * smoothedModAmount;
            const float depthSamples = modAmtCurved * 0.003f * fsf * modDepthScale;
            const float leftIn = inL[n];
            const float rightIn = inR[n];
            const float midIn = (leftIn + rightIn) * 0.5f;
            const float sideIn = (leftIn - rightIn) * 0.5f;
            float erOutL = 0.0f, erOutR = 0.0f;

            // 笘・PreDelay: 蜴滄浹縺ｨ繝ｪ繝舌・繝悶・譎る俣逧・・髮｢
            //   ER繝ｻFDN 荳｡譁ｹ縺ｮ蜈･蜉帙ｒ繝励Μ繝・ぅ繝ｬ繧､縺ｧ驕・ｻｶ縺輔○繧九・
            //   縺薙ｌ縺ｫ繧医ｊ蜴滄浹縺ｮ繧｢繧ｿ繝・け逶ｴ蠕後↓繝ｪ繝舌・繝悶′蟋九∪繧峨★縲・
            //   繝溘ャ繧ｯ繧ｹ縺ｮ譏守椚蠎ｦ (D50/C50) 縺悟､ｧ蟷・↓蜷台ｸ翫☆繧九・
            preDelayLine.write(midIn);
            const float delayedMid = (preDelaySamples > 0.5f)
                ? preDelayLine.read(preDelaySamples)
                : midIn;

            const float inputPeak = juce::jmax(std::abs(leftIn), std::abs(rightIn));
            const float envCoeff = (inputPeak > duckingEnvelope)
                ? duckingAttackCoeff : duckingReleaseCoeff;
            duckingEnvelope += (inputPeak - duckingEnvelope) * envCoeff;

            float duckGainLinear = 1.0f;
            if (duckAmountDB > 0.001f && duckingEnvelope > duckThreshLin) {
                const float envDB = 20.0f * std::log10(juce::jmax(duckingEnvelope, 1e-6f));
                const float overDB = envDB - activeParams.duckingThreshDB;
                const float gainRedDB = -juce::jmin(overDB, duckAmountDB);
                duckGainLinear = juce::Decibels::decibelsToGain(gainRedDB);
            }

            float fdnInputMid = delayedMid;
            if (!bypassInputDiffusers) {
                for (int i = 0; i < 4; ++i) {
                    float d = inputDiffusers[i].read(diffuserDelaySmp[i]);
                    float w = fdnInputMid + diffuserGain * d;
                    inputDiffusers[i].write(w);
                    fdnInputMid = d - diffuserGain * w;
                }
            }

            if (!bypassER) {
                erDelay.write(delayedMid);
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

            // 笘・ER竊鱈ate驕ｷ遘ｻ繧ｹ繝繝ｼ繧ｸ繝ｳ繧ｰ: ER蜃ｺ蜉帙ｒFDN蜈･蜉帙↓繝輔ぅ繝ｼ繝・
            //   螳溽ｩｺ髢薙〒縺ｯ蛻晄悄蜿榊ｰ・′螢・擇縺ｧ蜿榊ｰ・ｒ郢ｰ繧願ｿ斐＠Late Reverb繧堤函謌舌☆繧九・
            //   縺薙・閾ｪ辟ｶ縺ｪ驕ｷ遘ｻ繧呈ｨ｡謫ｬ縺励・R縺ｨLate縺ｮ蠅・阜繧呈ｻ代ｉ縺九↓縺吶ｋ縲・
            if (!bypassER) {
                fdnInputMid += (erOutL + erOutR) * 0.5f * 0.15f;
            }

            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);
            applySignFlipping(currentFb);

            float fdnOutL = 0.0f, fdnOutR = 0.0f;
            std::array<float, 16> nextFb;

            for (int i = 0; i < FDN_ORDER; ++i) {
                const float lfoVal = lfos[i].tick(lfoCoeffs[i]);
                // 笘・繧ｳ繝ｼ繝ｩ繧ｹ蝙九ヴ繝・メ繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ: 豁｣蠑ｦ豕｢LFO繧偵ヮ繧､繧ｺLFO縺ｫ蜉邂・
                //   繝弱う繧ｺ = 繝ｩ繝ｳ繝繝縺ｪ謠ｺ繧峨℃・磯≡螻樣浹謚大宛・・
                //   繧ｳ繝ｼ繝ｩ繧ｹ = 貊代ｉ縺九↑繝斐ャ繝√す繝輔ヨ闢・ｩ搾ｼ医Μ繝・メ縺ｪ繝・・繝ｫ・・
                const float chorusVal = chorusLFOs[i].tick();
                const float combinedLfo = lfoVal * 0.02f + chorusVal * 1.0f;

                // 笘・蜻ｨ豕｢謨ｰ萓晏ｭ倥Δ繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ: 鬮伜沺繝√Ε繝ｳ繝阪Ν繧呈ｷｱ縺上∽ｽ主沺繧呈ｵ・￥
                const float freqModScale = freqModScales[i];
                const float delaySmp = fdnBaseDelaySamples[i]
                    + combinedLfo * depthSamples * freqModScale;
                float d = fdnDelays[i].read(delaySmp);

#if AMBIENCE_USE_STAGE2_ABSORPTION
                for (int s = 0; s < ABSO_STAGES_S2; ++s)
                    d = absorptionFiltersS2[i][s].tick(d, currentAbsorptionCoeffsS2[i][s]);
#else
                d = absorptionFilters[i].tick(d, currentAbsorptionCoeffs[i]);
#endif

                // 笘・Anti-denormal: 髱樊ｭ｣隕丞喧謨ｰ縺ｫ繧医ｋCPU繧ｹ繝代う繧ｯ繧帝亟豁｢
                //   -500dBFS・亥ｮ悟・縺ｫ荳榊庄閨ｴ・峨・讌ｵ蟆丞ｮ壽焚繧貞刈邂励・
                //   Lexicon/Strymon/Valhalla 蜈ｨ縺ｦ縺梧治逕ｨ縺吶ｋ讌ｭ逡梧ｨ呎ｺ匁焔豕輔・
                d += 1e-25f;

                // 笘・驥大ｱ樣浹蟇ｾ遲・(3): DC繝悶Ο繝・き繝ｼ (1谺｡HPF, fc竕・Hz)
                //   FDN繝ｫ繝ｼ繝怜・縺ｧ蜷ｸ蜿弱ヵ繧｣繝ｫ繧ｿ繧・・繧､繧ｯ繝ｭ繧ｵ繝√Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ縺檎函謌舌☆繧・
                //   DC謌仙・縺ｮ闢・ｩ阪ｒ髦ｲ豁｢縲り塘遨好C縺ｯ菴主沺縺ｮ縺・↑繧翫ｄ髱槫ｯｾ遘ｰ豁ｪ縺ｿ縺ｮ蜴溷屏縺ｫ縺ｪ繧九・
                {
                    const float dcIn = d;
                    const float dcOut = dcIn - dcX1[i] + dcBlockerCoeff * dcY1[i];
                    dcX1[i] = dcIn;
                    dcY1[i] = dcOut;
                    d = dcOut;
                }

                // 笘・Soft-knee繧ｳ繝ｳ繝励Ξ繝・す繝ｧ繝ｳ (FDN繝輔ぅ繝ｼ繝峨ヰ繝・け繝ｫ繝ｼ繝・
                //   RMS繧ｨ繝ｳ繝吶Ο繝ｼ繝励〒繝ｬ繝吶Ν繧定ｿｽ蠕薙＠縲・明蛟､雜・℃蛻・ｒ繧ｽ繝輔ヨ縺ｫ蝨ｧ邵ｮ縲・
                //   笘・CPU譛驕ｩ蛹・ sqrt 繧帝明蛟､雜・℃譎ゅ・縺ｿ螳溯｡鯉ｼ井ｺ御ｹ玲ｯ碑ｼ・〒繧ｲ繝ｼ繝茨ｼ・
                {
                    fdnRmsEnv[i] += (d * d - fdnRmsEnv[i]) * rmsCoeff;
                    if (fdnRmsEnv[i] > compThreshSq) {
                        const float env = std::sqrt(fdnRmsEnv[i]);
                        const float over = env - compThresh;
                        d *= compThresh / (compThresh + over * 0.65f);
                    }
                }

                // 笘・驥大ｱ樣浹蟇ｾ遲・(1): Decay萓晏ｭ倥・繧､繧ｯ繝ｭ繧ｵ繝√Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ
                //   microSatBlend=1.0 竊・蠕捺擂騾壹ｊ驕ｩ逕ｨ (遏ｭ谿矩涸)
                //   microSatBlend=0.0 竊・螳悟・繝舌う繝代せ (髟ｷ谿矩涸)
                if (microSatBlend > 0.001f) {
                    const float sat = processMicroSaturation(d);
                    d = d + (sat - d) * microSatBlend;
                }

                // 笘・3谿ｵ繧ｷ繝ｪ繧｢繝ｫAllpass繝√ぉ繝ｼ繝ｳ (繝ｬ繧､繝医ヵ繧｣繝ｼ繝ｫ繝牙ｯ・ｺｦ謾ｹ蝟・
                //   笘・CPU譛驕ｩ蛹・ 繝吶・繧ｹ繝・ぅ繝ｬ繧､繝ｻapfGainStage 繧剃ｺ句燕險育ｮ玲ｸ医∩
                float apfOut = d;
                {
                    for (int s = 0; s < SERIAL_APF_STAGES; ++s) {
                        const float apfModDepth = depthSamples * apfModFrac[s];
                        const float apfDelaySmp = apfBaseDelaySmp[i][s]
                            + combinedLfo * apfModDepth * freqModScale;
                        float apfD = nestedAllpassDelays[i][s].read(apfDelaySmp);
                        float apfW = apfOut + apfGainStage * apfD;
                        nestedAllpassDelays[i][s].write(apfW);
                        apfOut = apfD - apfGainStage * apfW;
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

            // 笘・ER Boost: Late Reverb縺ｮ繝｡繧､繧ｯ繧｢繝・・繧ｲ繧､繝ｳ縺ｫ蟇ｾ縺励※ER縺悟ｰ上＆縺吶℃繧九◆繧・+12dB(邏・.0蛟・ 繝悶・繧ｹ繝・
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