#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace FDNReverb;

// 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
//  笘・Step A: Wet 縺ｮ蜀・Κ繧ｪ繝輔そ繝・ヨ
// 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
//   繝ｦ繝ｼ繧ｶ繝ｼ陦ｨ遉ｺ縺ｯ -60縲・dB 縺縺後仝et 譛螟ｧ縺ｯ螳溷柑逧・↓ -3dB 縺ｫ縺励◆縺・・
//   逅・罰: Wet=0dB 縺縺ｨ FDN 縺ｮ makeup 繧ｲ繧､繝ｳ縺ｨ蜷医ｏ縺輔ｊ OutputLimiter 縺・
//         騾｣邯壻ｽ懷虚縺励※髻ｳ縺悟牡繧後ｋ縲・3dB 縺ｮ繝倥ャ繝峨Ν繝ｼ繝縺悟ｿ・ｦ√・
//   螳溯｣・ APVTS 縺九ｉ蜿悶▲縺溷､縺ｫ -3dB 繧貞刈邂・(= 0.708 蛟・ 縺吶ｋ縺ｮ縺ｧ縺ｯ縺ｪ縺上・
//         Decibels::decibelsToGain 蠕後↓荵礼ｮ励☆繧句ｽ｢縺梧焚蛟､逧・↓螳牙・縲・
// 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
static constexpr float kWetInternalOffsetDB = -1.0f;

FDNReverbAudioProcessor::FDNReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "FDNReverbState", ParameterHelper::createLayout())
{
}

void FDNReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    int osIdx = 0;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, osIdx,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampler->initProcessing(static_cast<size_t>(samplesPerBlock));

    engine.prepare(sampleRate, samplesPerBlock);

    wetBuffer.setSize(2, samplesPerBlock);
    smoothWetGain.reset(sampleRate, 0.05);
    smoothDryGain.reset(sampleRate, 0.05);

    lastSampleRate = sampleRate;
    paramsNeedUpdate = true;
}

void FDNReverbAudioProcessor::updateEngineParams()
{
    int currentAlgo = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    if (currentAlgo != lastAlgorithmIndex) {
        if (lastAlgorithmIndex >= 0)
            loadPresetDefaults(currentAlgo);
        lastAlgorithmIndex = currentAlgo;
        paramsNeedUpdate = true;
    }

    DSPParams p;
    p.algorithmIndex = (int)*apvts.getRawParameterValue(ParamID::Algorithm);
    p.preDelayMs = *apvts.getRawParameterValue(ParamID::PreDelay);
    p.roomSizeScale = *apvts.getRawParameterValue(ParamID::RoomSize) - 0.5f;

    p.decayScale = *apvts.getRawParameterValue(ParamID::DecayTime)
        / ALL_PRESETS[p.algorithmIndex]->acoustics.rt60[4];

    p.hfDamping = *apvts.getRawParameterValue(ParamID::HFDamping);
    p.lfAbsorption = *apvts.getRawParameterValue(ParamID::LFAbsorption);
    p.diffusion = *apvts.getRawParameterValue(ParamID::Diffusion);
    p.modAmount = *apvts.getRawParameterValue(ParamID::ModAmount);
    p.modRate = *apvts.getRawParameterValue(ParamID::ModRate);
    p.stereoWidth = *apvts.getRawParameterValue(ParamID::StereoWidth);
    p.erLevel = *apvts.getRawParameterValue(ParamID::ERLevel);
    p.saturation = *apvts.getRawParameterValue(ParamID::Saturation);
    p.wetDB = *apvts.getRawParameterValue(ParamID::WetLevel);
    p.dryDB = *apvts.getRawParameterValue(ParamID::DryLevel);
    p.duckingAmount = *apvts.getRawParameterValue(ParamID::DuckAmount);
    p.duckingAttackMs = *apvts.getRawParameterValue(ParamID::DuckAttack);
    p.duckingRelMs = *apvts.getRawParameterValue(ParamID::DuckRelease);
    p.duckingThreshDB = *apvts.getRawParameterValue(ParamID::DuckThresh);
    p.satTypeIdx = (int)*apvts.getRawParameterValue(ParamID::SatType);
    p.erSolo = (*apvts.getRawParameterValue(ParamID::ERSolo)) > 0.5f;
    p.proMode = (*apvts.getRawParameterValue(ParamID::ProMode)) > 0.5f;
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

    // 笘・Step A: Wet 縺ｫ蜀・Κ -3dB 繧ｪ繝輔そ繝・ヨ繧帝←逕ｨ
    // 繝ｦ繝ｼ繧ｶ繝ｼ謫堺ｽ懊・ -60縲・dB縲∝ｮ溷柑蛟､縺ｯ -63縲・3dB 縺ｨ縺ｪ繧九・
    smoothWetGain.setTargetValue(
        juce::Decibels::decibelsToGain(p.wetDB + kWetInternalOffsetDB));

    // 笘・ER Solo譎ゅ・Dry髻ｳ繧貞ｮ悟・縺ｫ繝溘Η繝ｼ繝医☆繧・
    if (p.erSolo) {
        smoothDryGain.setTargetValue(0.0f);
    } else {
        smoothDryGain.setTargetValue(juce::Decibels::decibelsToGain(p.dryDB));
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

    updateEngineParams();

    inputRMS_L.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    inputRMS_R.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));

    juce::dsp::AudioBlock<float> block(buffer);
    auto osBlock = oversampler->processSamplesUp(block);
    int numSamples = static_cast<int>(osBlock.getNumSamples());

    wetBuffer.setSize(2, numSamples, false, false, true);

    engine.processBlock(osBlock.getChannelPointer(0), osBlock.getChannelPointer(1),
        wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1),
        numSamples);

    for (int i = 0; i < numSamples; ++i) {
        float dryL = osBlock.getSample(0, i);
        float wetL = wetBuffer.getSample(0, i);
        if (i % 2 == 0) {
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

    outputRMS_L.store(buffer.getRMSLevel(0, 0, buffer.getNumSamples()));
    outputRMS_R.store(buffer.getRMSLevel(1, 0, buffer.getNumSamples()));
}

void FDNReverbAudioProcessor::getStateInformation(juce::MemoryBlock& d) {
    auto state = apvts.copyState();

    // 笘・菫ｮ豁｣: 迴ｾ蝨ｨ縺ｮ繝励Μ繧ｻ繝・ヨ蜷阪ｒ ValueTree 縺ｫ菫晏ｭ・
    // 繧ｨ繝・ぅ繧ｿ繝ｼ縺悟ｭ伜惠縺吶ｋ蝣ｴ蜷医￣resetManager 縺九ｉ蜷榊燕繧貞叙蠕励☆繧九・
    // 繧ｨ繝・ぅ繧ｿ繝ｼ縺ｯ AudioProcessor 縺檎峩謗･菫晄戟縺励↑縺・◆繧√・
    // 繝励Μ繧ｻ繝・ヨ蜷阪ｒ Processor 蛛ｴ縺ｧ邂｡逅・☆繧九ヵ繧｣繝ｼ繝ｫ繝峨ｒ霑ｽ蜉縺吶ｋ縲・
    if (lastSavedPresetName.isNotEmpty())
        state.setProperty("currentPresetName", lastSavedPresetName, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, d);
}

void FDNReverbAudioProcessor::setStateInformation(const void* d, int s) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(d, s));
    if (xml && xml->hasTagName(apvts.state.getType())) {
        auto tree = juce::ValueTree::fromXml(*xml);

        // 笘・菫ｮ豁｣: 繝励Μ繧ｻ繝・ヨ蜷阪ｒ蠕ｩ蜈・
        lastSavedPresetName = tree.getProperty("currentPresetName", "").toString();

        apvts.replaceState(tree);
        paramsNeedUpdate = true;
    }
}

juce::AudioProcessorEditor* FDNReverbAudioProcessor::createEditor() {
    return new FDNReverbEditor(*this);
}

void FDNReverbAudioProcessor::loadPresetDefaults(int algorithmIndex)
{
    if (algorithmIndex < 0 || algorithmIndex >= 7) return;

    const auto& def = PRESET_DEFAULTS[algorithmIndex];

    auto setParam = [this](const juce::String& paramID, float value) {
        if (auto* param = apvts.getParameter(paramID)) {
            param->setValueNotifyingHost(param->convertTo0to1(value));
        }
        };

    setParam(ParamID::RoomSize, def.roomSize);
    setParam(ParamID::DecayTime, def.decayTime);

    // 笘・Step A: HF Damping / LF Absorption 縺ｯ蟶ｸ縺ｫ 0 縺ｫ繝ｪ繧ｻ繝・ヨ
    //   AlgorithmPresets.h 縺ｮ def.hfDamp / def.lfAbsorb 縺ｯ菴ｿ繧上↑縺・・
    //   逅・罰: 繧｢繝ｫ繧ｴ繝ｪ繧ｺ繝驕ｸ謚樒峩蠕後・縲後・繝ｪ繧ｻ繝・ヨ縺昴・繧ゅ・縺ｮ RT60 繧ｫ繝ｼ繝悶阪ｒ
    //         縺昴・縺ｾ縺ｾ蜀咲樟縺吶ｋ縺ｮ縺梧ｭ｣縺励＞謖吝虚縲ゅΘ繝ｼ繧ｶ繝ｼ縺梧э蝗ｳ逧・↓陬懈ｭ｣繧・
    //         蜉縺医ｋ蜑阪↓繝・ヵ繧ｩ繝ｫ繝医〒陬懈ｭ｣縺悟・繧九・縺ｯ荳崎・辟ｶ縲・
    setParam(ParamID::HFDamping, 0.0f);
    setParam(ParamID::LFAbsorption, 0.0f);

    setParam(ParamID::Diffusion, def.diffusion);
    setParam(ParamID::ModAmount, def.modAmount);
    setParam(ParamID::ModRate, def.modRate);
    setParam(ParamID::ERLevel, def.erLevel);
    setParam(ParamID::Saturation, def.saturation);

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