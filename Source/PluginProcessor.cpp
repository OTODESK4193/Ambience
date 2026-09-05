#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FDNReverb;

static constexpr float kWetInternalOffsetDB = 0.0f;

FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", ParameterHelper::createLayout())
{
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
    smoothWetGain.reset(sampleRate, 0.05);
    smoothDryGain.reset(sampleRate, 0.05);

    lastSampleRate = sampleRate;
    paramsNeedUpdate = true;
}

void FDNReverbAudioProcessor::updateEngineParams()
{
    int currentAlgo = juce::jlimit(0, NUM_ALGORITHMS - 1,
        (int)*apvts.getRawParameterValue(ParamID::Algorithm));
    if (currentAlgo != lastAlgorithmIndex) {
        lastAlgorithmIndex = currentAlgo;
        paramsNeedUpdate = true;
    }

    DSPParams p;
    p.algorithmIndex = currentAlgo;
    float effectivePreDelay = *apvts.getRawParameterValue(ParamID::PreDelay);
    // ★ Send Mode 時の位相保護: Dry がミュート (-59dB以下) の場合、
    // 原音トラックとのコムフィルタリング・位相干渉を音響工学的に自動防止 (+5.0ms 下限オフセットガード)
    if (*apvts.getRawParameterValue(ParamID::DryLevel) <= -59.0f) {
        effectivePreDelay = std::max(5.0f, effectivePreDelay);
    }
    p.preDelayMs = effectivePreDelay;
    // ★ RoomSize ノブ値 (0.3 ~ 2.0) をそのままスケール係数として伝達
    p.roomSizeScale = *apvts.getRawParameterValue(ParamID::RoomSize);

    p.decayScale = *apvts.getRawParameterValue(ParamID::DecayTime)
        / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[5];

    p.hfDamping = *apvts.getRawParameterValue(ParamID::HFDamping);
    p.lfAbsorption = *apvts.getRawParameterValue(ParamID::LFAbsorption);
    p.diffusion = *apvts.getRawParameterValue(ParamID::Diffusion);
    p.modAmount = *apvts.getRawParameterValue(ParamID::ModAmount);
    p.modRate = *apvts.getRawParameterValue(ParamID::ModRate);
    p.stereoWidth = *apvts.getRawParameterValue(ParamID::StereoWidth);
    p.erLevel = *apvts.getRawParameterValue(ParamID::ERLevel);
    p.saturation = *apvts.getRawParameterValue(ParamID::Saturation);
    p.satTypeIdx = (int)*apvts.getRawParameterValue(ParamID::SatType);
    p.wetDB = *apvts.getRawParameterValue(ParamID::WetLevel);
    p.dryDB = *apvts.getRawParameterValue(ParamID::DryLevel);

    p.duckingAmount = *apvts.getRawParameterValue(ParamID::DuckAmount);
    p.duckingAttackMs = *apvts.getRawParameterValue(ParamID::DuckAttack);
    p.duckingRelMs = *apvts.getRawParameterValue(ParamID::DuckRelease);
    p.duckingThreshDB = *apvts.getRawParameterValue(ParamID::DuckThresh);

    p.erSolo = *apvts.getRawParameterValue(ParamID::ERSolo) > 0.5f;
    p.proMode = *apvts.getRawParameterValue(ParamID::ProMode) > 0.5f;

    p.tiltLow = *apvts.getRawParameterValue(ParamID::TiltLow);
    p.tiltMid = *apvts.getRawParameterValue(ParamID::TiltMid);
    p.tiltHigh = *apvts.getRawParameterValue(ParamID::TiltHigh);

    p.rtBands[0] = *apvts.getRawParameterValue(ParamID::RTBand0);
    p.rtBands[1] = *apvts.getRawParameterValue(ParamID::RTBand1);
    p.rtBands[2] = *apvts.getRawParameterValue(ParamID::RTBand2);
    p.rtBands[3] = *apvts.getRawParameterValue(ParamID::RTBand3);
    p.rtBands[4] = *apvts.getRawParameterValue(ParamID::RTBand4);
    p.rtBands[5] = *apvts.getRawParameterValue(ParamID::RTBand5);
    p.rtBands[6] = *apvts.getRawParameterValue(ParamID::RTBand6);
    p.rtBands[7] = *apvts.getRawParameterValue(ParamID::RTBand7);
    p.rtBands[8] = *apvts.getRawParameterValue(ParamID::RTBand8);
    p.rtBands[9] = *apvts.getRawParameterValue(ParamID::RTBand9);

    p.loCutHz = *apvts.getRawParameterValue(ParamID::LoCut);
    p.hiCutHz = *apvts.getRawParameterValue(ParamID::HiCut);
    p.loEQType = static_cast<int>(*apvts.getRawParameterValue(ParamID::LoEQType));
    p.hiEQType = static_cast<int>(*apvts.getRawParameterValue(ParamID::HiEQType));
    p.loGainDB = *apvts.getRawParameterValue(ParamID::LoGain);
    p.hiGainDB = *apvts.getRawParameterValue(ParamID::HiGain);

    p.scattering = *apvts.getRawParameterValue(ParamID::Scattering);
    p.erCrossoverMs = *apvts.getRawParameterValue(ParamID::ERCrossover);
    p.lateDensity = *apvts.getRawParameterValue(ParamID::LateDensity);
    p.asymmetry = *apvts.getRawParameterValue(ParamID::Asymmetry);
    p.clarityDB = *apvts.getRawParameterValue(ParamID::Clarity);
    p.airAbsorbScale = *apvts.getRawParameterValue(ParamID::AirAbsorb);
    p.rt60Tab = *apvts.getRawParameterValue(ParamID::RT60Tab) > 0.5f;
    p.proTab = *apvts.getRawParameterValue(ParamID::ProTab) > 0.5f;

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
    inputRMS_L.store(inRMSL);
    inputRMS_R.store(inRMSR);

    // 内部ステレオ処理用バッファの確保（安全マージン）
    if (wetBuffer.getNumSamples() < numSamples) {
        wetBuffer.setSize(2, numSamples, false, false, true);
    }

    // 入力信号を内部ステレオバッファへ安全に展開 (Mono -> Dual Mono 展開)
    juce::AudioBuffer<float> stereoBlockBuffer;
    stereoBlockBuffer.setSize(2, numSamples, false, false, true);

    if (numIn >= 2) {
        stereoBlockBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
        stereoBlockBuffer.copyFrom(1, 0, buffer.getReadPointer(1), numSamples);
    } else if (numIn == 1) {
        stereoBlockBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
        stereoBlockBuffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);
    } else {
        stereoBlockBuffer.clear();
    }

    juce::dsp::AudioBlock<float> block(stereoBlockBuffer);
    auto osBlock = oversampler->processSamplesUp(block);
    const int osNumSamples = static_cast<int>(osBlock.getNumSamples());

    if (wetBuffer.getNumSamples() < osNumSamples) {
        wetBuffer.setSize(2, osNumSamples, false, false, true);
    }

    engine.processBlock(osBlock.getChannelPointer(0), osBlock.getChannelPointer(1),
        wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1),
        osNumSamples);

    const bool editorOpen = (getActiveEditor() != nullptr);

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
    if (lastSavedPresetName.isNotEmpty())
        state.setProperty("currentPresetName", lastSavedPresetName, nullptr);
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

    paramsNeedUpdate = true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FDNReverbAudioProcessor();
}
