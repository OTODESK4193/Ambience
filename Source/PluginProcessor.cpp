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
    satOversampling = apvts.getRawParameterValue(ParamID::SatOversampling);
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

    // サチュレータ段のレイテンシをホストへ申告する。
    // OS ON: ADAA1 + 2x OS の実測 47.5 サンプルの切り上げ = 48。
    // OS OFF: ベースレート ADAA1 のみで、実効遅延は 1 サンプル
    // (phase3/os-toggle でインパルス実測)。amount=0 のバイパス時は
    // どちらの状態でも無遅延だが、申告は状態ごとの最悪値で一定とする。
    // 状態が変わった場合は updateEngineParams() でも連動して再申告する。
    lastSatOversampling = cachedParams.satOversampling
        ? (cachedParams.satOversampling->load(std::memory_order_relaxed) > 0.5f)
        : false;
    setLatencySamples(lastSatOversampling ? SaturatorADAAOS2::kLatencySamples
                                          : SaturatorADAAOS2::kLatencySamplesDirect);

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
    p.satOversampling = getVal(cachedParams.satOversampling, 0.0f) > 0.5f;
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

    // [phase3/os-toggle] OS 状態が変わったらレイテンシ申告も連動させる。
    // ホスト側の PDC 再同期タイミングはホスト依存 (次回再生時など)。
    if (p.satOversampling != lastSatOversampling) {
        lastSatOversampling = p.satOversampling;
        setLatencySamples(p.satOversampling ? SaturatorADAAOS2::kLatencySamples
                                            : SaturatorADAAOS2::kLatencySamplesDirect);
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

    // ─── Panic による Graceful Mute ＆ オーディオスレッド安全リセット ───
    // ★ 無音スリープ中であっても、Panic リクエストがあれば即座にエンジンをリセットして復帰する
    if (panicRequested.load(std::memory_order_acquire)) {
        if (isEngineSuspended) {
            engine.panicReset();
            oversampler->reset();
            inputRMS_L.store(0.0f);
            inputRMS_R.store(0.0f);
            outputRMS_L.store(0.0f);
            outputRMS_R.store(0.0f);
            specFifoIndex.store(0);
            specFifoReady.store(false);
            panicFadeSamplesRemaining = 0;
            panicRequested.store(false, std::memory_order_release);
        } else if (panicFadeSamplesRemaining <= 0) {
            const double sr = getSampleRate();
            panicFadeTotalSamples = std::max(32, static_cast<int>((sr > 0.0 ? sr : 48000.0) * 0.007)); // 7ms
            panicFadeSamplesRemaining = panicFadeTotalSamples;
        }
    }

    // ★★★ 無音時スマートサスペンド（Silence Sleep）★★★
    // 入力が -100dBFS 以下 かつ 直前のウェット出力が -120dBFS 以下
    const bool isInputSilent = (inRMS < 1.0e-5f);
    const float prevOutRMS = std::max(outputRMS_L.load(std::memory_order_relaxed),
                                      outputRMS_R.load(std::memory_order_relaxed));
    const bool isOutputSilent = (prevOutRMS < 1.0e-6f);
    const bool hasPendingEngineWork = engine.hasPendingTransitionOrUpdate() || (panicFadeSamplesRemaining > 0);

    if (isInputSilent && isOutputSilent && !hasPendingEngineWork) {
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

    // ─── Panic フェードアウト実行 ───
    if (panicFadeSamplesRemaining > 0) {
        for (int i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(panicFadeSamplesRemaining) / static_cast<float>(panicFadeTotalSamples);
            float gain = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t));
            for (int ch = 0; ch < numOut; ++ch) {
                buffer.getWritePointer(ch)[i] *= gain;
            }
            --panicFadeSamplesRemaining;
            if (panicFadeSamplesRemaining == 0) {
                // フェードアウト完了：完全無音の状態で音声状態のみを消去する。
                // [panic-fix] engine.reset() は prepare 終端スナップショットへ
                // 動作点ごと巻き戻すため、パラメータ再送が無い限り後部入力段の
                // ゲイン (diffuser/APF、目標値 0) が閉じたままになり、Panic 後
                // の余韻が消えたまま戻らなかった (0a87da49 で混入)。Panic の
                // 役割はテイルの消去のみなので panicReset() (音声状態の消去
                // のみ、動作点は保持) を使う。reset() 本体と seq≡fresh の
                // ためのスナップショット復元は従来通り変更しない。
                engine.panicReset();
                oversampler->reset();
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
    const int algo = cachedParams.algorithm
        ? juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, static_cast<int>(cachedParams.algorithm->load(std::memory_order_relaxed)))
        : 0;
    const auto& preset = *FDNReverb::ALL_PRESETS[algo];

    const float userDecay = cachedParams.decayTime    ? cachedParams.decayTime->load(std::memory_order_relaxed)    : 1.5f;
    const float userHf    = cachedParams.hfDamping    ? cachedParams.hfDamping->load(std::memory_order_relaxed)    : 0.0f;
    const float userLf    = cachedParams.lfAbsorption ? cachedParams.lfAbsorption->load(std::memory_order_relaxed) : 0.0f;
    const float airScale  = cachedParams.airAbsorb    ? cachedParams.airAbsorb->load(std::memory_order_relaxed)    : 1.0f;
    const float tiltLow   = cachedParams.tiltLow      ? cachedParams.tiltLow->load(std::memory_order_relaxed)      : 1.0f;
    const float tiltMid   = cachedParams.tiltMid      ? cachedParams.tiltMid->load(std::memory_order_relaxed)      : 1.0f;
    const float tiltHigh  = cachedParams.tiltHigh     ? cachedParams.tiltHigh->load(std::memory_order_relaxed)     : 1.0f;

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

    for (int b = 0; b < FDNReverb::NUM_BANDS; ++b) {
        const auto* rawBand = cachedParams.rtBands[b];
        const float bandMult = rawBand ? rawBand->load(std::memory_order_relaxed) : 1.0f;
        scaledRT60[b] *= bandMult;
    }

    // 大気減衰 (ISO 9613-1 連続吸収係数モデル: UniversalEngine::updateAbsorptionFilters と完全一致)
    const float safeAirScale = std::clamp(airScale, 0.0f, 5.0f);
    if (std::abs(safeAirScale - 1.0f) > 1e-4f) {
        static constexpr float kAirAlpha[FDNReverb::NUM_BANDS] = {
            0.0001f, 0.0002f, 0.0004f, 0.0010f, 0.0019f,
            0.0037f, 0.0097f, 0.0328f, 0.1050f, 0.2900f }; // 31.25Hz..16kHz
        const float dScale = safeAirScale - 1.0f;
        for (int b = 0; b < FDNReverb::NUM_BANDS; ++b) {
            const float invOld = 1.0f / std::max(0.01f, scaledRT60[b]);
            float inv = invOld + dScale * kAirAlpha[b] * (343.0f / 60.0f);
            inv = std::max(inv, 0.4f * invOld);
            scaledRT60[b] = 1.0f / std::max(inv, 1.0f / 60.0f);
        }
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

float FDNReverbAudioProcessor::getEDT() const noexcept {
    const auto rt60 = calculateInstantRT60();
    float rt60Mid = 0.0f;
    for (int b = 2; b <= 7; ++b)
        rt60Mid += rt60[b];
    rt60Mid = std::max(0.01f, rt60Mid / 6.0f);

    float edtCoeff = 0.70f;
    const int algo = cachedParams.algorithm
        ? juce::jlimit(0, FDNReverb::NUM_ALGORITHMS - 1, static_cast<int>(cachedParams.algorithm->load(std::memory_order_relaxed)))
        : 0;
    switch (algo) {
    case 0: case 1: edtCoeff = 0.70f; break; // Room
    case 2: case 3: edtCoeff = 0.95f; break; // Hall
    case 4:         edtCoeff = 0.60f; break; // Plate
    case 5:         edtCoeff = 0.50f; break; // Spring
    case 6:         edtCoeff = 0.85f; break; // Goldfoil
    case 7:         edtCoeff = 1.00f; break; // Inchindown
    }
    return rt60Mid * edtCoeff;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FDNReverbAudioProcessor();
}
