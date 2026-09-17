#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FDNReverb;

static constexpr float kWetInternalOffsetDB = 0.0f;

void FDNReverbAudioProcessor::CachedParams::init(juce::AudioProcessorValueTreeState& apvts)
{
    algorithm     = apvts.getRawParameterValue(ParamID::Algorithm);
    preDelay      = apvts.getRawParameterValue(ParamID::PreDelay);
    roomSize      = apvts.getRawParameterValue(ParamID::RoomSize);
    decayTime     = apvts.getRawParameterValue(ParamID::DecayTime);
    hfDamping     = apvts.getRawParameterValue(ParamID::HFDamping);
    lfAbsorption  = apvts.getRawParameterValue(ParamID::LFAbsorption);
    diffusion     = apvts.getRawParameterValue(ParamID::Diffusion);
    modAmount     = apvts.getRawParameterValue(ParamID::ModAmount);
    modRate       = apvts.getRawParameterValue(ParamID::ModRate);
    stereoWidth   = apvts.getRawParameterValue(ParamID::StereoWidth);
    erLevel       = apvts.getRawParameterValue(ParamID::ERLevel);
    saturation    = apvts.getRawParameterValue(ParamID::Saturation);
    satType       = apvts.getRawParameterValue(ParamID::SatType);
    wetLevel      = apvts.getRawParameterValue(ParamID::WetLevel);
    dryLevel      = apvts.getRawParameterValue(ParamID::DryLevel);
    duckAmount    = apvts.getRawParameterValue(ParamID::DuckAmount);
    duckAttack    = apvts.getRawParameterValue(ParamID::DuckAttack);
    duckRelease   = apvts.getRawParameterValue(ParamID::DuckRelease);
    duckThresh    = apvts.getRawParameterValue(ParamID::DuckThresh);
    erSolo        = apvts.getRawParameterValue(ParamID::ERSolo);
    proMode       = apvts.getRawParameterValue(ParamID::ProMode);
    tiltLow       = apvts.getRawParameterValue(ParamID::TiltLow);
    tiltMid       = apvts.getRawParameterValue(ParamID::TiltMid);
    tiltHigh      = apvts.getRawParameterValue(ParamID::TiltHigh);

    const juce::String rtBandIDs[10] = {
        ParamID::RTBand0, ParamID::RTBand1, ParamID::RTBand2, ParamID::RTBand3, ParamID::RTBand4,
        ParamID::RTBand5, ParamID::RTBand6, ParamID::RTBand7, ParamID::RTBand8, ParamID::RTBand9
    };
    for (int b = 0; b < 10; ++b) {
        rtBands[b] = apvts.getRawParameterValue(rtBandIDs[b]);
    }

    loCut         = apvts.getRawParameterValue(ParamID::LoCut);
    hiCut         = apvts.getRawParameterValue(ParamID::HiCut);
    loEQType      = apvts.getRawParameterValue(ParamID::LoEQType);
    hiEQType      = apvts.getRawParameterValue(ParamID::HiEQType);
    loGain        = apvts.getRawParameterValue(ParamID::LoGain);
    hiGain        = apvts.getRawParameterValue(ParamID::HiGain);
    scattering    = apvts.getRawParameterValue(ParamID::Scattering);
    erCrossover   = apvts.getRawParameterValue(ParamID::ERCrossover);
    lateDensity   = apvts.getRawParameterValue(ParamID::LateDensity);
    asymmetry     = apvts.getRawParameterValue(ParamID::Asymmetry);
    clarity       = apvts.getRawParameterValue(ParamID::Clarity);
    airAbsorb     = apvts.getRawParameterValue(ParamID::AirAbsorb);
    rt60Tab       = apvts.getRawParameterValue(ParamID::RT60Tab);
    proTab        = apvts.getRawParameterValue(ParamID::ProTab);
}

FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", ParameterHelper::createLayout())
{
    cachedParams.init(apvts);
    loadPresetDefaults(0);
    lastSavedPresetName = "Init";
    lastPresetModified = false;
}

bool FDNReverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // 出力は Mono または Stereo のみ許可
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // 入力が無効（サイドチェーン等）でない場合、Mono または Stereo を許可
    if (!mainIn.isDisabled())
    {
        if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void FDNReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // FL Studio や Reaper の巨大ブロック（レンダリング時等）に備え、十分な最大容量を確保
    const int maxBlock = std::max(samplesPerBlock, 8192);

    int osIdx = 0;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, osIdx,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing(static_cast<size_t>(maxBlock));

    engine.prepare(sampleRate, maxBlock);

    // 最大ブロックサイズで事前確保（オーディオスレッドでの再確保を根絶）
    wetBuffer.setSize(2, maxBlock);
    stereoBlockBuffer.setSize(2, maxBlock);
    smoothWetGain.reset(sampleRate, 0.05);
    smoothDryGain.reset(sampleRate, 0.05);

    // ★ 無音時スマートサスペンド初期化（1.2秒無音で休止）
    silenceThresholdSamples = static_cast<int>(1.2 * sampleRate);
    silenceDurationSamples = 0;
    isEngineSuspended = false;

    lastSampleRate = sampleRate;
    paramsNeedUpdate = true;
}

void FDNReverbAudioProcessor::updateEngineParams()
{
    auto getVal = [](std::atomic<float>* p, float defVal = 0.0f) noexcept {
        return p ? p->load(std::memory_order_relaxed) : defVal;
    };

    int currentAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1,
        juce::roundToInt(getVal(cachedParams.algorithm, 0.0f)));
    if (currentAlgo != lastAlgorithmIndex) {
        lastAlgorithmIndex = currentAlgo;
        paramsNeedUpdate = true;
    }

    DSPParams p;
    p.algorithmIndex = currentAlgo;
    float effectivePreDelay = getVal(cachedParams.preDelay, 10.0f);
    // ★ Send Mode 時の位相保護: Dry がミュート (-59dB以下) の場合、
    // 原音トラックとのコムフィルタリング・位相干渉を音響工学的に自動防止 (+5.0ms 下限オフセットガード)
    if (getVal(cachedParams.dryLevel, 0.0f) <= -59.0f) {
        effectivePreDelay = std::max(5.0f, effectivePreDelay);
    }
    p.preDelayMs = effectivePreDelay;
    // ★ RoomSize ノブ値 (0.3 ~ 2.0) をそのままスケール係数として伝達
    p.roomSizeScale = getVal(cachedParams.roomSize, 1.0f);

    p.decayScale = getVal(cachedParams.decayTime, 1.5f)
        / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[5];

    p.hfDamping = getVal(cachedParams.hfDamping, 0.0f);
    p.lfAbsorption = getVal(cachedParams.lfAbsorption, 0.0f);
    p.diffusion = getVal(cachedParams.diffusion, 0.7f);
    p.modAmount = getVal(cachedParams.modAmount, 0.25f);
    p.modRate = getVal(cachedParams.modRate, 0.5f);
    p.stereoWidth = getVal(cachedParams.stereoWidth, 0.95f);
    p.erLevel = getVal(cachedParams.erLevel, 0.6f);
    p.saturation = getVal(cachedParams.saturation, 0.0f);
    p.satTypeIdx = static_cast<int>(getVal(cachedParams.satType, 0.0f));
    p.wetDB = getVal(cachedParams.wetLevel, -4.0f);
    p.dryDB = getVal(cachedParams.dryLevel, 0.0f);

    p.duckingAmount = getVal(cachedParams.duckAmount, 0.0f);
    p.duckingAttackMs = getVal(cachedParams.duckAttack, 10.0f);
    p.duckingRelMs = getVal(cachedParams.duckRelease, 200.0f);
    p.duckingThreshDB = getVal(cachedParams.duckThresh, -20.0f);

    p.erSolo = getVal(cachedParams.erSolo, 0.0f) > 0.5f;
    p.proMode = getVal(cachedParams.proMode, 0.0f) > 0.5f;

    p.tiltLow = getVal(cachedParams.tiltLow, 1.0f);
    p.tiltMid = getVal(cachedParams.tiltMid, 1.0f);
    p.tiltHigh = getVal(cachedParams.tiltHigh, 1.0f);

    for (int b = 0; b < 10; ++b) {
        p.rtBands[b] = getVal(cachedParams.rtBands[b], 1.0f);
    }

    p.loCutHz = getVal(cachedParams.loCut, 20.0f);
    p.hiCutHz = getVal(cachedParams.hiCut, 20000.0f);
    p.loEQType = static_cast<int>(getVal(cachedParams.loEQType, 0.0f));
    p.hiEQType = static_cast<int>(getVal(cachedParams.hiEQType, 0.0f));
    p.loGainDB = getVal(cachedParams.loGain, 0.0f);
    p.hiGainDB = getVal(cachedParams.hiGain, 0.0f);

    p.scattering = getVal(cachedParams.scattering, 0.5f);
    p.erCrossoverMs = getVal(cachedParams.erCrossover, 40.0f);
    p.lateDensity = getVal(cachedParams.lateDensity, 0.7f);
    p.asymmetry = getVal(cachedParams.asymmetry, 0.3f);
    p.clarityDB = getVal(cachedParams.clarity, 0.0f);
    p.airAbsorbScale = getVal(cachedParams.airAbsorb, 1.0f);
    p.rt60Tab = getVal(cachedParams.rt60Tab, 0.0f) > 0.5f;
    p.proTab = getVal(cachedParams.proTab, 0.0f) > 0.5f;

    const bool isBypass = bypassEnabled.load(std::memory_order_relaxed);
    if (isBypass) {
        smoothWetGain.setTargetValue(0.0f);
        smoothDryGain.setTargetValue(1.0f);
    } else {
        smoothWetGain.setTargetValue(
            juce::Decibels::decibelsToGain(p.wetDB + kWetInternalOffsetDB));

        if (p.erSolo) {
            smoothDryGain.setTargetValue(0.0f);
        } else {
            smoothDryGain.setTargetValue(juce::Decibels::decibelsToGain(p.dryDB));
        }
    }

    if (paramsNeedUpdate || p != lastSentParams) {
        engine.setParams(p);
        lastSentParams = p;
        paramsNeedUpdate = false;
    }
}

void FDNReverbAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    _mm_setcsr(_mm_getcsr() | 0x8040); // Hardware FTZ (bit 15) & DAZ (bit 6)

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (numSamples == 0 || numOut == 0 || oversampler == nullptr || !engine.isPrepared()) {
        buffer.clear();
        return;
    }

    updateEngineParams();

    // 入力 RMS 計測 (Mono 入力時も安全に取得)
    const float inRMSL = (numIn > 0) ? buffer.getRMSLevel(0, 0, numSamples) : 0.0f;
    const float inRMSR = (numIn > 1) ? buffer.getRMSLevel(1, 0, numSamples) : inRMSL;
    const float inRMS = std::max(inRMSL, inRMSR);
    inputRMS_L.store(inRMSL, std::memory_order_relaxed);
    inputRMS_R.store(inRMSR, std::memory_order_relaxed);

    // ★★★ 無音時スマートサスペンド（Silence Sleep）★★★
    // 入力が -100dBFS 以下 かつ 直前のウェット出力が -120dBFS 以下
    const bool isInputSilent = (inRMS < 1.0e-5f);
    const float prevOutRMS = std::max(outputRMS_L.load(std::memory_order_relaxed),
                                      outputRMS_R.load(std::memory_order_relaxed));
    const bool isOutputSilent = (prevOutRMS < 1.0e-6f);

    if (isInputSilent && isOutputSilent) {
        silenceDurationSamples += numSamples;
    } else {
        silenceDurationSamples = 0;
        isEngineSuspended = false;
    }

    // 設定時間（1.2秒）以上完全無音が続いた場合、サスペンド発動
    if (silenceDurationSamples >= silenceThresholdSamples) {
        isEngineSuspended = true;
    }

    if (isEngineSuspended) {
        buffer.clear();
        outputRMS_L.store(0.0f, std::memory_order_relaxed);
        outputRMS_R.store(0.0f, std::memory_order_relaxed);
        return; // ★ CPU 0.0% 完全スリープ（DSPエンジン計算を完全スキップ）
    }

    // 内部ステレオ処理用バッファの確保（安全マージン）
    if (wetBuffer.getNumSamples() < numSamples) {
        wetBuffer.setSize(2, numSamples, false, false, true);
    }

    // 入力信号を内部ステレオバッファへ安全に展開 (Mono -> Dual Mono 展開)
    if (stereoBlockBuffer.getNumSamples() < numSamples) {
        stereoBlockBuffer.setSize(2, numSamples, false, false, true);
    }

    if (numIn >= 2) {
        stereoBlockBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
        stereoBlockBuffer.copyFrom(1, 0, buffer.getReadPointer(1), numSamples);
    } else if (numIn == 1) {
        stereoBlockBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
        stereoBlockBuffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);
    } else {
        stereoBlockBuffer.clear(0, numSamples);
    }

    auto block = juce::dsp::AudioBlock<float>(stereoBlockBuffer).getSubBlock(0, static_cast<size_t>(numSamples));
    auto osBlock = oversampler->processSamplesUp(block);
    const int osNumSamples = static_cast<int>(osBlock.getNumSamples());

    if (wetBuffer.getNumSamples() < osNumSamples) {
        wetBuffer.setSize(2, osNumSamples, false, false, true);
    }

    engine.processBlock(osBlock.getChannelPointer(0), osBlock.getChannelPointer(1),
        wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1),
        osNumSamples);

    const bool editorOpen = isEditorOpen.load(std::memory_order_relaxed);

    for (int i = 0; i < osNumSamples; ++i) {
        float dryL = osBlock.getSample(0, i);
        float wetL = wetBuffer.getSample(0, i);
        if (editorOpen && (i % 2 == 0)) {
            int idx = specFifoIndex.load(std::memory_order_relaxed);
            if (idx < 2048) {
                specFifoDry[idx] = dryL;
                specFifoWet[idx] = wetL;
                specFifoIndex.store(idx + 1, std::memory_order_release);
                if (idx + 1 == 2048) {
                    specFifoReady.store(true, std::memory_order_release);
                }
            }
        }
        float w = smoothWetGain.getNextValue();
        float d = smoothDryGain.getNextValue();
        osBlock.setSample(0, i, dryL * d + wetL * w);
        osBlock.setSample(1, i, osBlock.getSample(1, i) * d + wetBuffer.getSample(1, i) * w);
    }

    oversampler->processSamplesDown(block);
    duckingReductionDB.store(engine.getDuckingReductionDB(), std::memory_order_relaxed);

    // 出力チャンネルへの書き戻しとルーティング
    if (numOut >= 2) {
        buffer.copyFrom(0, 0, stereoBlockBuffer.getReadPointer(0), numSamples);
        buffer.copyFrom(1, 0, stereoBlockBuffer.getReadPointer(1), numSamples);
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    } else if (numOut == 1) {
        // Stereo -> Mono Downmix
        auto* dest = buffer.getWritePointer(0);
        const auto* srcL = stereoBlockBuffer.getReadPointer(0);
        const auto* srcR = stereoBlockBuffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            dest[i] = (srcL[i] + srcR[i]) * 0.5f;
        }
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    // ─── Panic による Graceful Mute ＆ オーディオスレッド安全リセット ───
    if (panicRequested.load(std::memory_order_acquire)) {
        if (panicFadeSamplesRemaining <= 0) {
            const double sr = getSampleRate();
            panicFadeTotalSamples = std::max(32, static_cast<int>((sr > 0.0 ? sr : 48000.0) * 0.007)); // 7ms
            panicFadeSamplesRemaining = panicFadeTotalSamples;
        }
    }

    if (panicFadeSamplesRemaining > 0) {
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(panicFadeSamplesRemaining) / static_cast<float>(panicFadeTotalSamples);
            float gain = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t));
            for (int ch = 0; ch < numOut; ++ch) {
                buffer.getWritePointer(ch)[i] *= gain;
            }
            --panicFadeSamplesRemaining;
            if (panicFadeSamplesRemaining == 0) {
                // フェードアウト完了：完全無音の状態でエンジンリセット
                engine.reset();
                inputRMS_L.store(0.0f);
                inputRMS_R.store(0.0f);
                outputRMS_L.store(0.0f);
                outputRMS_R.store(0.0f);
                specFifoIndex.store(0);
                specFifoReady.store(false);
                panicRequested.store(false, std::memory_order_release);
                for (int c = 0; c < numOut; ++c) {
                    juce::FloatVectorOperations::clear(buffer.getWritePointer(c) + i + 1, numSamples - (i + 1));
                }
                break;
            }
        }
    }

    // 出力 RMS 計測 (Mono 出力時も安全に取得)
    const float outRMSL = (numOut > 0) ? buffer.getRMSLevel(0, 0, numSamples) : 0.0f;
    const float outRMSR = (numOut > 1) ? buffer.getRMSLevel(1, 0, numSamples) : outRMSL;
    outputRMS_L.store(outRMSL);
    outputRMS_R.store(outRMSR);
}

void FDNReverbAudioProcessor::getStateInformation(juce::MemoryBlock& d) {
    const juce::ScopedLock sl(stateLock);
    auto state = apvts.copyState();
    if (lastSavedPresetName.isNotEmpty()) {
        state.setProperty("currentPresetName", lastSavedPresetName, nullptr);
        state.setProperty("isPresetModified", lastPresetModified, nullptr);
    }
    state.setProperty("editorWidth", savedEditorWidth, nullptr);
    state.setProperty("editorHeight", savedEditorHeight, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr) {
        copyXmlToBinary(*xml, d);
    }
}

void FDNReverbAudioProcessor::setStateInformation(const void* d, int s) {
    if (d == nullptr || s <= 0) return;

    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(d, s));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid()) {
            {
                const juce::ScopedLock sl(stateLock);
                lastSavedPresetName = tree.getProperty("currentPresetName", "").toString();
                lastPresetModified = tree.getProperty("isPresetModified", false);
                savedEditorWidth = tree.getProperty("editorWidth", 900);
                savedEditorHeight = tree.getProperty("editorHeight", 540);
            }
            apvts.replaceState(tree);
            paramsNeedUpdate = true;
        }
    }
}

juce::AudioProcessorEditor* FDNReverbAudioProcessor::createEditor() {
    return new FDNReverbEditor(*this);
}

void FDNReverbAudioProcessor::loadPresetDefaults(int algorithmIndex)
{
    if (algorithmIndex < 0 || algorithmIndex >= NUM_ALGORITHMS) return;

    const auto& def = PRESET_DEFAULTS[algorithmIndex];

    auto setParam = [this](const juce::String& paramID, float value) {
        if (auto* param = apvts.getParameter(paramID)) {
            param->setValueNotifyingHost(param->convertTo0to1(value));
        }
    };

    // ── メインノブ ──
    setParam(ParamID::RoomSize, def.roomSize);
    setParam(ParamID::DecayTime, def.decayTime);
    setParam(ParamID::PreDelay, def.preDelayMs);
    setParam(ParamID::StereoWidth, def.stereoWidth);
    setParam(ParamID::HFDamping, def.hfDamp);
    setParam(ParamID::LFAbsorption, def.lfAbsorb);
    setParam(ParamID::Diffusion, def.diffusion);
    setParam(ParamID::ModAmount, def.modAmount);
    setParam(ParamID::ModRate, def.modRate);
    setParam(ParamID::ERLevel, def.erLevel);
    setParam(ParamID::Saturation, def.saturation);
    setParam(ParamID::SatType, static_cast<float>(def.satType));

    // ── ルームタイプ共通デフォルト: Dry 0dB / Wet -12dB ──
    setParam(ParamID::DryLevel, 0.0f);
    setParam(ParamID::WetLevel, -12.0f);

    // ── PRO ACOUSTIC 6ノブ (DeepResearch 音響物理設計値) ──
    setParam(ParamID::Scattering, def.scattering);
    setParam(ParamID::ERCrossover, def.erCrossoverMs);
    setParam(ParamID::LateDensity, def.lateDensity);
    setParam(ParamID::Asymmetry, def.asymmetry);
    setParam(ParamID::Clarity, def.clarityDB);
    setParam(ParamID::AirAbsorb, def.airAbsorbScale);

    // ── ルームタイプ共通デフォルト: 10-Band RT60 & Tilt (フラット) ──
    setParam(ParamID::RTBand0, 1.0f);
    setParam(ParamID::RTBand1, 1.0f);
    setParam(ParamID::RTBand2, 1.0f);
    setParam(ParamID::RTBand3, 1.0f);
    setParam(ParamID::RTBand4, 1.0f);
    setParam(ParamID::RTBand5, 1.0f);
    setParam(ParamID::RTBand6, 1.0f);
    setParam(ParamID::RTBand7, 1.0f);
    setParam(ParamID::RTBand8, 1.0f);
    setParam(ParamID::RTBand9, 1.0f);
    setParam(ParamID::TiltLow, 1.0f);
    setParam(ParamID::TiltMid, 1.0f);
    setParam(ParamID::TiltHigh, 1.0f);

    // ── ルームタイプ共通デフォルト: Ducking (バイパス/初期値) ──
    setParam(ParamID::DuckAmount, 0.0f);
    setParam(ParamID::DuckThresh, -20.0f);
    setParam(ParamID::DuckAttack, 20.0f);
    setParam(ParamID::DuckRelease, 100.0f);

    // ── ルームタイプ共通デフォルト: OutEQ (フラット) ──
    setParam(ParamID::LoCut, 20.0f);
    setParam(ParamID::HiCut, 20000.0f);
    setParam(ParamID::LoEQType, 0.0f);
    setParam(ParamID::HiEQType, 0.0f);
    setParam(ParamID::LoGain, 0.0f);
    setParam(ParamID::HiGain, 0.0f);

    paramsNeedUpdate = true;
}

std::array<float, FDNReverb::NUM_BANDS> FDNReverbAudioProcessor::calculateInstantRT60() const noexcept {
    const auto* algoParam = apvts.getRawParameterValue(ParamID::Algorithm);
    if (algoParam == nullptr) {
        std::array<float, FDNReverb::NUM_BANDS> defArr;
        defArr.fill(1.5f);
        return defArr;
    }
    const int algo = juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, static_cast<int>(algoParam->load()));
    const auto& preset = *FDNReverb::ALL_PRESETS[algo];

    const auto* pDecay = apvts.getRawParameterValue(ParamID::DecayTime);
    const auto* pHf    = apvts.getRawParameterValue(ParamID::HFDamping);
    const auto* pLf    = apvts.getRawParameterValue(ParamID::LFAbsorption);
    const auto* pAir   = apvts.getRawParameterValue(ParamID::AirAbsorb);
    const auto* pTL    = apvts.getRawParameterValue(ParamID::TiltLow);
    const auto* pTM    = apvts.getRawParameterValue(ParamID::TiltMid);
    const auto* pTH    = apvts.getRawParameterValue(ParamID::TiltHigh);

    const float userDecay = pDecay ? pDecay->load() : 1.5f;
    const float userHf    = pHf    ? pHf->load()    : 0.0f;
    const float userLf    = pLf    ? pLf->load()    : 0.0f;
    const float airScale  = pAir   ? pAir->load()   : 1.0f;
    const float tiltLow   = pTL    ? pTL->load()    : 1.0f;
    const float tiltMid   = pTM    ? pTM->load()    : 1.0f;
    const float tiltHigh  = pTH    ? pTH->load()    : 1.0f;

    const float decayScale = userDecay / std::max(0.01f, FDNReverb::PRESET_DEFAULTS[algo].decayTime);

    std::array<float, FDNReverb::NUM_BANDS> scaledRT60 = preset.acoustics.rt60;
    for (auto& v : scaledRT60) v *= decayScale;

    // Tilt EQ 多項式チルティング
    if (std::abs(tiltLow - 1.0f) > 1e-4f ||
        std::abs(tiltMid - 1.0f) > 1e-4f ||
        std::abs(tiltHigh - 1.0f) > 1e-4f)
    {
        const float x0 = std::log2(FDNReverb::BAND_FREQ[1]);
        const float x1 = std::log2(FDNReverb::BAND_FREQ[5]);
        const float x2 = std::log2(FDNReverb::BAND_FREQ[8]);
        const float d01 = x0 - x1;
        const float d02 = x0 - x2;
        const float d12 = x1 - x2;
        const float denom0 = d01 * d02;
        const float denom1 = -d01 * d12;
        const float denom2 = -d02 * -d12;

        for (int b = 0; b < FDNReverb::NUM_BANDS; ++b) {
            const float x = std::log2(FDNReverb::BAND_FREQ[b]);
            const float L0 = ((x - x1) * (x - x2)) / denom0;
            const float L1 = ((x - x0) * (x - x2)) / denom1;
            const float L2 = ((x - x0) * (x - x1)) / denom2;
            float tiltFactor = tiltLow * L0 + tiltMid * L1 + tiltHigh * L2;
            tiltFactor = std::clamp(tiltFactor, 0.1f, 10.0f);
            scaledRT60[b] *= tiltFactor;
        }
    }

    static const juce::String rtBandIDs[FDNReverb::NUM_BANDS] = {
        ParamID::RTBand0, ParamID::RTBand1, ParamID::RTBand2, ParamID::RTBand3, ParamID::RTBand4,
        ParamID::RTBand5, ParamID::RTBand6, ParamID::RTBand7, ParamID::RTBand8, ParamID::RTBand9
    };

    for (int b = 0; b < FDNReverb::NUM_BANDS; ++b) {
        const auto* rawBand = apvts.getRawParameterValue(rtBandIDs[b]);
        const float bandMult = rawBand ? rawBand->load() : 1.0f;
        scaledRT60[b] *= bandMult;
    }

    // 大気減衰 (ISO 9613-1 指数べき乗モデル)
    const float safeAirScale = std::clamp(airScale, 0.0f, 5.0f);
    if (std::abs(safeAirScale - 1.0f) > 1e-4f) {
        const float ratio7 = std::pow(0.90f, safeAirScale);
        const float ratio8 = std::pow(0.75f, safeAirScale);
        const float ratio9 = std::pow(0.60f, safeAirScale);
        scaledRT60[7] = std::min(scaledRT60[7], scaledRT60[6] * ratio7);
        scaledRT60[8] = std::min(scaledRT60[8], scaledRT60[7] * ratio8);
        scaledRT60[9] = std::min(scaledRT60[9], scaledRT60[8] * ratio9);
    }

    std::array<float, FDNReverb::NUM_BANDS> outRT60{};
    for (int b = 0; b < FDNReverb::NUM_BANDS; ++b) {
        float t60 = scaledRT60[b];
        const float f = FDNReverb::BAND_FREQ[b];

        // 低域吸音
        const float fDiv160 = f / 160.0f;
        const float lfWeight = 1.0f / (1.0f + fDiv160 * fDiv160);
        t60 = std::max(0.01f, t60 * (1.0f - userLf * 0.8f * lfWeight));

        // 高域大気分子吸音
        const float fExcess = std::max(0.0f, f - 2000.0f) / 14000.0f;
        const float hfWeight = fExcess * fExcess;
        const float invT60 = (1.0f / t60) + (userHf * hfWeight * 2.0f);
        t60 = 1.0f / std::max(1e-4f, invT60);

        outRT60[b] = std::clamp(t60, 0.01f, 200.0f);
    }
    return outRT60;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FDNReverbAudioProcessor();
}
