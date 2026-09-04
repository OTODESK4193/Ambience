#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── V1.2.0 オリジナル定数レイアウト ──
static constexpr int Y_HEADER = 8;
static constexpr int Y_ALGO = 48;
static constexpr int Y_SLABEL1 = 86;
static constexpr int Y_ROW1 = 104;
static constexpr int Y_SLABEL2 = 204;
static constexpr int Y_ROW2 = 222;
static constexpr int Y_VIZ = 326;

static constexpr int SEC_TIME = 8;
static constexpr int SEC_FREQUENCY = 254;
static constexpr int SEC_DIFFUSION = 418;
static constexpr int SEC_STEREO = 664;
static constexpr int SEC_CHARACTER = 746;
static constexpr int SEP_TF = 245;
static constexpr int SEP_FD = 409;
static constexpr int SEP_DS = 655;
static constexpr int SEP_SC = 737;

FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    spectrumViz(p),
    algoSelector(p.apvts),
    vuIn("IN", VUMeter::Side::Input),
    vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);

    // ── Content Component の設定 (80%縮小〜150%拡大) ──
    addAndMakeVisible(content);
    content.onPaint = [this](juce::Graphics& g) { paintContent(g); };
    content.onLayout = [this] { layoutContent(); };

    constrainer.setFixedAspectRatio((double)kBaseW / (double)kBaseH);
    constrainer.setSizeLimits(
        static_cast<int>(kBaseW * 0.80), static_cast<int>(kBaseH * 0.80),
        static_cast<int>(kBaseW * 1.50), static_cast<int>(kBaseH * 1.50));
    setConstrainer(&constrainer);
    setResizable(true, true);

    const int savedW = audioProcessor.getSavedEditorWidth();
    const int savedH = audioProcessor.getSavedEditorHeight();
    setSize(juce::jlimit(static_cast<int>(kBaseW * 0.80), static_cast<int>(kBaseW * 1.50), savedW),
            juce::jlimit(static_cast<int>(kBaseH * 0.80), static_cast<int>(kBaseH * 1.50), savedH));

    // ── Title ──
    titleLabel.setText("AMBIENCE 2.0.0 B019", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 15.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    content.addAndMakeVisible(titleLabel);

    // ── RT60 Tab Button ──
    rt60TabButton.setButtonText("RT60");
    rt60TabButton.setClickingTogglesState(true);
    rt60TabButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    rt60TabButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    rt60TabButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    rt60TabButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    content.addAndMakeVisible(rt60TabButton);
    rt60TabAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "rt60tab", rt60TabButton);

    // ── PRO Tab Button ──
    proTabButton.setButtonText("PRO");
    proTabButton.setClickingTogglesState(true);
    proTabButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    proTabButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    proTabButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    proTabButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    content.addAndMakeVisible(proTabButton);
    proTabAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "protab", proTabButton);

    // ── ER Solo Button ──
    erSoloButton.setButtonText("ER SOLO");
    erSoloButton.setClickingTogglesState(true);
    erSoloButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    erSoloButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    erSoloButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    erSoloButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    content.addAndMakeVisible(erSoloButton);
    erSoloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "ersolo", erSoloButton);

    // ── Lock Button (ノブロック切替) ──
    lockButton.setButtonText("LOCK");
    lockButton.setClickingTogglesState(true);
    lockButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    lockButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    lockButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    lockButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    lockButton.onClick = [this] {
        audioProcessor.setParamsLocked(lockButton.getToggleState());
    };
    content.addAndMakeVisible(lockButton);

    // ── Send Mode Button (Dry -60dB / Wet 0dB) ──
    sendModeButton.setButtonText("SEND");
    sendModeButton.setClickingTogglesState(true);
    sendModeButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    sendModeButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    sendModeButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    sendModeButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    sendModeButton.onClick = [this] {
        const bool isSend = sendModeButton.getToggleState();
        if (auto* wetParam = audioProcessor.apvts.getParameter("wetlevel"))
            wetParam->setValueNotifyingHost(wetParam->convertTo0to1(isSend ? 0.0f : -12.0f));
        if (auto* dryParam = audioProcessor.apvts.getParameter("drylevel"))
            dryParam->setValueNotifyingHost(dryParam->convertTo0to1(isSend ? -60.0f : 0.0f));
    };
    content.addAndMakeVisible(sendModeButton);

    // ── Panic Button (発音即時停止) ──
    panicButton.setButtonText("PANIC");
    panicButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFEF4444));
    panicButton.onClick = [this] {
        audioProcessor.panic();
    };
    content.addAndMakeVisible(panicButton);

    // ── Algorithm Selector (Lock状態連携 & 変更検知) ──
    algoSelector.isLockedCallback = [this] {
        return lockButton.getToggleState();
    };
    algoSelector.onUserAlgorithmSelected = [this](int newAlgo, bool shouldResetKnobs) {
        if (shouldResetKnobs) {
            audioProcessor.loadPresetDefaults(newAlgo);
        }
        if (loadingPresetCounter > 0) return;
        if (currentBasePresetName.isNotEmpty()) {
            setPresetModified(true);
        }
    };
    algoSelector.onAlgorithmChangedCallback = [this](int) {
        if (loadingPresetCounter > 0) return;
        if (currentBasePresetName.isNotEmpty()) {
            setPresetModified(true);
        }
    };
    content.addAndMakeVisible(algoSelector);

    // ── Normal Mode Knobs ──
    auto& a = audioProcessor.apvts;
    kPreDelay.build(a, "predelay", "PRE-DELAY", &content, laf);
    kRoomSize.build(a, "roomsize", "ROOM SIZE", &content, laf);
    kDecay.build(a, "decaytime", "DECAY", &content, laf);
    kHFDamp.build(a, "hfdamping", "HF DAMP", &content, laf);
    kLFAbsorb.build(a, "lfabsorption", "LF ABSORB", &content, laf);
    kDiffusion.build(a, "diffusion", "DIFFUSION", &content, laf);
    kModAmt.build(a, "modamount", "MOD AMT", &content, laf);
    kModRate.build(a, "modrate", "MOD RATE", &content, laf);
    kStereoW.build(a, "stereowidth", "WIDTH", &content, laf);
    kERLevel.build(a, "erlevel", "ER LEVEL", &content, laf);
    kSaturation.build(a, "saturation", "SATURATE", &content, laf);

    kWet.build(a, "wetlevel", "WET", &content, laf);
    kDry.build(a, "drylevel", "DRY", &content, laf);

    kLoCutNorm.build(a, "locut", "LO CUT", &content, laf);
    kHiCutNorm.build(a, "hicut", "HI CUT", &content, laf);
    kDuckAmt.build(a, "duckamount", "AMOUNT", &content, laf);
    kDuckThr.build(a, "duckthresh", "THRESH", &content, laf);
    kDuckAtt.build(a, "duckattack", "ATTACK", &content, laf);
    kDuckRel.build(a, "duckrelease", "RELEASE", &content, laf);

    // ── ProMode Knobs ──
    static const char* bandLabels[10] = {
        "31","63","125","250","500","1k","2k","4k","8k","16k"
    };
    for (int i = 0; i < 10; ++i)
        kRTBands[i].build(a, "rtband" + juce::String(i), bandLabels[i], &content, laf);

    satTypeLabel.setText("SAT TYPE", juce::dontSendNotification);
    satTypeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    satTypeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    satTypeLabel.setJustificationType(juce::Justification::centred);
    content.addAndMakeVisible(satTypeLabel);

    satTypeCombo.addItemList(juce::StringArray{ "Warm","Tape","Tube","Hard" }, 1);
    satTypeCombo.setLookAndFeel(&laf);
    content.addAndMakeVisible(satTypeCombo);
    satTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        a, "sattype", satTypeCombo);

    kTiltLow.build(a, "tiltlow", "TILT LO", &content, laf);
    kTiltMid.build(a, "tiltmid", "TILT MID", &content, laf);
    kTiltHigh.build(a, "tilthigh", "TILT HI", &content, laf);
    kLoCutPro.build(a, "locut", "LO CUT", &content, laf);
    kHiCutPro.build(a, "hicut", "HI CUT", &content, laf);

    // ── PRO Tab Knobs ──
    kScattering.build(a, "scattering", "SCATTERING", &content, laf);
    kERCrossover.build(a, "ercrossover", "ER CROSSOVER", &content, laf);
    kLateDensity.build(a, "latedensity", "LATE DENSITY", &content, laf);
    kAsymmetry.build(a, "asymmetry", "ASYMMETRY", &content, laf);
    kClarity.build(a, "clarity", "CLARITY", &content, laf);
    kAirAbsorb.build(a, "airabsorb", "AIR ABSORB", &content, laf);

    // ── Theme UI ──
    themeLabel.setText("THEME", juce::dontSendNotification);
    themeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    themeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    themeLabel.setJustificationType(juce::Justification::centred);
    content.addAndMakeVisible(themeLabel);

    themeCombo.addItemList(juce::StringArray{
        "Cyber Neon", "Solar Flare", "Matrix Glow", "Vaporwave", "Dark Amber",
        "Nordic Frost", "Deep Purple", "Midnight", "Blood Moon", "Monochrome"
    }, 1);
    themeCombo.setLookAndFeel(&laf);
    content.addAndMakeVisible(themeCombo);
    themeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        a, "theme", themeCombo);
    themeCombo.onChange = [this] {
        updateTheme(themeCombo.getSelectedItemIndex());
    };

    // ── Preset UI ──
    presetPrevButton.setButtonText("<");
    presetPrevButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetPrevButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    content.addAndMakeVisible(presetPrevButton);

    presetCombo.setLookAndFeel(&laf);
    presetCombo.setTextWhenNothingSelected("Select Preset...");
    content.addAndMakeVisible(presetCombo);

    presetNextButton.setButtonText(">");
    presetNextButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetNextButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    content.addAndMakeVisible(presetNextButton);

    presetSaveButton.setButtonText("SAVE");
    presetSaveButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Accent);
    presetSaveButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::Background);
    presetSaveButton.onClick = [this] { savePresetWithDialog(); };
    content.addAndMakeVisible(presetSaveButton);

    presetRevertButton.setButtonText("REVERT");
    presetRevertButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetRevertButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    presetRevertButton.setTooltip("Revert to saved preset settings");
    presetRevertButton.setEnabled(false);
    presetRevertButton.onClick = [this] {
        if (currentBasePresetName.isNotEmpty() && presetManager) {
            ++loadingPresetCounter;
            presetManager->loadPreset(currentBasePresetName);
            setPresetModified(false);
            refreshPresetCombo();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                }
            });
        }
    };
    content.addAndMakeVisible(presetRevertButton);

    presetLoadButton.setButtonText("LOAD");
    presetLoadButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetLoadButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    presetLoadButton.onClick = [this] {
        if (presetManager && presetCombo.getSelectedId() > 0) {
            auto name = presetCombo.getText();
            if (name.endsWith(" *")) name = name.dropLastCharacters(2);
            ++loadingPresetCounter;
            presetManager->loadPreset(name);
            currentBasePresetName = name;
            setPresetModified(false);
            refreshPresetCombo();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                }
            });
        }
    };
    content.addAndMakeVisible(presetLoadButton);

    presetDeleteButton.setButtonText("DELETE");
    presetDeleteButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetDeleteButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    presetDeleteButton.onClick = [this] { deleteCurrentPreset(); };
    content.addAndMakeVisible(presetDeleteButton);

    presetManager = std::make_unique<PresetManager>(audioProcessor);
    presetManager->onPresetLoaded = [this](const juce::String& name) {
        audioProcessor.setLastSavedPresetName(name);
        currentBasePresetName = name;
        setPresetModified(false);
        refreshPresetCombo();
    };

    presetCombo.onChange = [this] {
        if (loadingPresetCounter > 0) return;
        if (presetManager && presetCombo.getSelectedId() > 0) {
            auto name = presetCombo.getText();
            if (name.endsWith(" *")) name = name.dropLastCharacters(2);
            ++loadingPresetCounter;
            presetManager->loadPreset(name);
            currentBasePresetName = name;
            setPresetModified(false);
            refreshPresetCombo();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                }
            });
        }
    };
    presetPrevButton.onClick = [this] {
        if (presetManager) {
            ++loadingPresetCounter;
            presetManager->loadPrevPreset();
            currentBasePresetName = presetManager->getCurrentPresetName();
            setPresetModified(false);
            refreshPresetCombo();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                }
            });
        }
    };
    presetNextButton.onClick = [this] {
        if (presetManager) {
            ++loadingPresetCounter;
            presetManager->loadNextPreset();
            currentBasePresetName = presetManager->getCurrentPresetName();
            setPresetModified(false);
            refreshPresetCombo();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                }
            });
        }
    };

    // ── Register APVTS Parameter Listeners ──
    static const juce::String kMonitoredParams[] = {
        "algorithm", "predelay", "roomsize", "decaytime", "hfdamping", "lfabsorption",
        "diffusion", "modamount", "modrate", "stereowidth", "erlevel", "saturation",
        "sattype", "wetlevel", "drylevel", "duckamount", "duckattack", "duckrelease",
        "duckthresh", "ersolo", "tiltlow", "tiltmid", "tilthigh",
        "rtband0", "rtband1", "rtband2", "rtband3", "rtband4",
        "rtband5", "rtband6", "rtband7", "rtband8", "rtband9",
        "locut", "hicut",
        "rt60tab", "protab", "scattering", "ercrossover", "latedensity", "asymmetry", "clarity", "airabsorb"
    };
    for (const auto& pid : kMonitoredParams)
        audioProcessor.apvts.addParameterListener(pid, this);

    // ── Visualizers ──
    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    content.addAndMakeVisible(rt60Viz);
    content.addAndMakeVisible(spectrumViz);
    content.addAndMakeVisible(decayCurveViz);

    content.addAndMakeVisible(vuIn);
    content.addAndMakeVisible(vuOut);

    // ── DECAY TIME 1行表示 (DECAY TIME: 29.1 s) ──
    labelDecayLine.setText("DECAY TIME: --", juce::dontSendNotification);
    labelDecayLine.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 13.0f, juce::Font::bold)));
    labelDecayLine.setColour(juce::Label::textColourId, AmbienceColors::Accent);
    labelDecayLine.setJustificationType(juce::Justification::centredRight);
    content.addAndMakeVisible(labelDecayLine);

    refreshPresetCombo();
    updatePanelVisibility();

    int curTheme = 0;
    if (auto* rawTheme = audioProcessor.apvts.getRawParameterValue("theme"))
        curTheme = juce::jlimit(0, 9, juce::roundToInt(rawTheme->load()));
    themeCombo.setSelectedItemIndex(curTheme, juce::dontSendNotification);
    updateTheme(curTheme);

    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();

    static const juce::String kMonitoredParams[] = {
        "algorithm", "predelay", "roomsize", "decaytime", "hfdamping", "lfabsorption",
        "diffusion", "modamount", "modrate", "stereowidth", "erlevel", "saturation",
        "sattype", "wetlevel", "drylevel", "duckamount", "duckattack", "duckrelease",
        "duckthresh", "ersolo", "tiltlow", "tiltmid", "tilthigh",
        "rtband0", "rtband1", "rtband2", "rtband3", "rtband4",
        "rtband5", "rtband6", "rtband7", "rtband8", "rtband9",
        "locut", "hicut",
        "rt60tab", "protab", "scattering", "ercrossover", "latedensity", "asymmetry", "clarity", "airabsorb"
    };
    for (const auto& pid : kMonitoredParams)
        audioProcessor.apvts.removeParameterListener(pid, this);

    setLookAndFeel(nullptr);
    satTypeCombo.setLookAndFeel(nullptr);
    themeCombo.setLookAndFeel(nullptr);
    presetCombo.setLookAndFeel(nullptr);
}

void FDNReverbEditor::timerCallback() {
    vuIn.setLevels(audioProcessor.getInputRMSL(), audioProcessor.getInputRMSR());
    vuOut.setLevels(audioProcessor.getOutputRMSL(), audioProcessor.getOutputRMSR());
    vuIn.repaint();
    vuOut.repaint();

    static int metricsCounter = 0;
    if (++metricsCounter >= 2) {
        metricsCounter = 0;
        const float edt = audioProcessor.getEDT();

        if (edt < 10.0f) {
            labelDecayLine.setText("DECAY TIME: " + juce::String(edt, 2) + " s", juce::dontSendNotification);
        } else {
            labelDecayLine.setText("DECAY TIME: " + juce::String(edt, 1) + " s", juce::dontSendNotification);
        }
    }

    bool newRT60Tab = (*audioProcessor.apvts.getRawParameterValue("rt60tab") > 0.5f);
    bool newProTab  = (*audioProcessor.apvts.getRawParameterValue("protab") > 0.5f);
    if (newRT60Tab != isRT60Tab || newProTab != isProTab) {
        isRT60Tab = newRT60Tab;
        isProTab  = newProTab;
        updatePanelVisibility();
        layoutContent();
        content.repaint();
    }
}

void FDNReverbEditor::updatePanelVisibility() {
    bool isNormal = (!isRT60Tab && !isProTab);
    kPreDelay.slider.setVisible(isNormal);   kPreDelay.label.setVisible(isNormal);
    kRoomSize.slider.setVisible(isNormal);   kRoomSize.label.setVisible(isNormal);
    kDecay.slider.setVisible(isNormal);      kDecay.label.setVisible(isNormal);
    kHFDamp.slider.setVisible(isNormal);     kHFDamp.label.setVisible(isNormal);
    kLFAbsorb.slider.setVisible(isNormal);   kLFAbsorb.label.setVisible(isNormal);
    kDiffusion.slider.setVisible(isNormal);  kDiffusion.label.setVisible(isNormal);
    kModAmt.slider.setVisible(isNormal);     kModAmt.label.setVisible(isNormal);
    kModRate.slider.setVisible(isNormal);    kModRate.label.setVisible(isNormal);
    kStereoW.slider.setVisible(isNormal);    kStereoW.label.setVisible(isNormal);
    kERLevel.slider.setVisible(isNormal);    kERLevel.label.setVisible(isNormal);
    kSaturation.slider.setVisible(isNormal); kSaturation.label.setVisible(isNormal);
    kWet.slider.setVisible(isNormal);        kWet.label.setVisible(isNormal);
    kDry.slider.setVisible(isNormal);        kDry.label.setVisible(isNormal);
    kLoCutNorm.slider.setVisible(isNormal);  kLoCutNorm.label.setVisible(isNormal);
    kHiCutNorm.slider.setVisible(isNormal);  kHiCutNorm.label.setVisible(isNormal);
    kDuckAmt.slider.setVisible(isNormal);    kDuckAmt.label.setVisible(isNormal);
    kDuckThr.slider.setVisible(isNormal);    kDuckThr.label.setVisible(isNormal);
    kDuckAtt.slider.setVisible(isNormal);    kDuckAtt.label.setVisible(isNormal);
    kDuckRel.slider.setVisible(isNormal);    kDuckRel.label.setVisible(isNormal);

    // RT60 Tab (10-Band + Tilt + Satu + Theme)
    for (auto& k : kRTBands) {
        k.slider.setVisible(isRT60Tab);
        k.label.setVisible(isRT60Tab);
    }
    satTypeLabel.setVisible(isRT60Tab);
    satTypeCombo.setVisible(isRT60Tab);
    kTiltLow.slider.setVisible(isRT60Tab);  kTiltLow.label.setVisible(isRT60Tab);
    kTiltMid.slider.setVisible(isRT60Tab);  kTiltMid.label.setVisible(isRT60Tab);
    kTiltHigh.slider.setVisible(isRT60Tab); kTiltHigh.label.setVisible(isRT60Tab);
    kLoCutPro.slider.setVisible(isRT60Tab); kLoCutPro.label.setVisible(isRT60Tab);
    kHiCutPro.slider.setVisible(isRT60Tab); kHiCutPro.label.setVisible(isRT60Tab);
    themeLabel.setVisible(isRT60Tab);
    themeCombo.setVisible(isRT60Tab);

    // PRO Tab (6 Knobs)
    kScattering.slider.setVisible(isProTab);  kScattering.label.setVisible(isProTab);
    kERCrossover.slider.setVisible(isProTab); kERCrossover.label.setVisible(isProTab);
    kLateDensity.slider.setVisible(isProTab); kLateDensity.label.setVisible(isProTab);
    kAsymmetry.slider.setVisible(isProTab);   kAsymmetry.label.setVisible(isProTab);
    kClarity.slider.setVisible(isProTab);     kClarity.label.setVisible(isProTab);
    kAirAbsorb.slider.setVisible(isProTab);   kAirAbsorb.label.setVisible(isProTab);
}

void FDNReverbEditor::refreshPresetCombo() {
    if (!presetManager) return;
    presetCombo.clear(juce::dontSendNotification);
    auto names = presetManager->getPresetNames();

    if (presetManager->getCurrentPresetName().isEmpty()) {
        auto saved = audioProcessor.getLastSavedPresetName();
        if (saved.isNotEmpty()) {
            presetManager->setCurrentPresetName(saved);
            currentBasePresetName = saved;
        }
    } else {
        currentBasePresetName = presetManager->getCurrentPresetName();
    }

    if (names.isEmpty()) {
        presetCombo.addItem("-- No Presets --", 1);
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        presetDeleteButton.setEnabled(false);
        presetLoadButton.setEnabled(false);
        presetPrevButton.setEnabled(false);
        presetNextButton.setEnabled(false);
        presetRevertButton.setEnabled(false);
        return;
    }

    for (int i = 0; i < names.size(); ++i) {
        juce::String itemText = names[i];
        if (itemText == currentBasePresetName && isPresetModified)
            itemText += " *";
        presetCombo.addItem(itemText, i + 1);
    }

    int idx = presetManager->getCurrentPresetIndex();
    if (idx >= 0)
        presetCombo.setSelectedItemIndex(idx, juce::dontSendNotification);
    else
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);

    if (currentBasePresetName.isNotEmpty()) {
        juce::String displayText = currentBasePresetName + (isPresetModified ? " *" : "");
        presetCombo.setText(displayText, juce::dontSendNotification);
    }

    presetDeleteButton.setEnabled(true);
    presetLoadButton.setEnabled(true);
    presetPrevButton.setEnabled(names.size() > 1);
    presetNextButton.setEnabled(names.size() > 1);
    presetRevertButton.setEnabled(isPresetModified);
}

void FDNReverbEditor::parameterChanged(const juce::String& paramID, float newValue) {
    if (loadingPresetCounter > 0) return;

    if (paramID == "rt60tab") {
        if (newValue > 0.5f) {
            if (auto* p = audioProcessor.apvts.getParameter("protab"))
                if (p->getValue() > 0.5f) p->setValueNotifyingHost(0.0f);
        }
        return;
    }
    if (paramID == "protab") {
        if (newValue > 0.5f) {
            if (auto* p = audioProcessor.apvts.getParameter("rt60tab"))
                if (p->getValue() > 0.5f) p->setValueNotifyingHost(0.0f);
        }
        return;
    }

    if (paramID == "promode" || paramID == "theme") return;
    if (currentBasePresetName.isNotEmpty()) {
        juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr) {
                if (safeThis->loadingPresetCounter == 0 && !safeThis->isPresetModified) {
                    safeThis->setPresetModified(true);
                }
            }
        });
    }
}

void FDNReverbEditor::setPresetModified(bool modified) {
    isPresetModified = modified;
    if (currentBasePresetName.isNotEmpty()) {
        juce::String displayText = currentBasePresetName + (modified ? " *" : "");
        int idx = presetManager ? presetManager->getCurrentPresetIndex() : -1;
        if (idx >= 0 && idx < presetCombo.getNumItems()) {
            presetCombo.changeItemText(idx + 1, displayText);
        }
        presetCombo.setText(displayText, juce::dontSendNotification);
    }
    presetRevertButton.setEnabled(modified);
    presetRevertButton.setColour(juce::TextButton::buttonColourId,
        modified ? AmbienceColors::Accent : AmbienceColors::Surface);
    presetRevertButton.setColour(juce::TextButton::textColourOffId,
        modified ? AmbienceColors::Background : AmbienceColors::TextSecondary);
    presetRevertButton.repaint();
}

void FDNReverbEditor::savePresetWithDialog() {
    if (!presetManager) return;
    auto* dialog = new juce::AlertWindow(
        "Save Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);

    dialog->addTextEditor("name", currentBasePresetName);
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, dialog](int result) {
            if (safeThis != nullptr && result == 1) {
                auto name = dialog->getTextEditorContents("name").trim();
                if (name.isNotEmpty()) {
                    safeThis->presetManager->savePreset(name);
                    safeThis->currentBasePresetName = name;
                    safeThis->setPresetModified(false);
                    safeThis->refreshPresetCombo();
                }
            }
        }),
        true
    );
}

void FDNReverbEditor::deleteCurrentPreset() {
    if (!presetManager) return;
    auto name = currentBasePresetName.isNotEmpty() ? currentBasePresetName : presetManager->getCurrentPresetName();
    if (name.isEmpty()) return;

    auto* dialog = new juce::AlertWindow(
        "Delete Preset", "Delete \"" + name + "\"?", juce::MessageBoxIconType::WarningIcon);

    dialog->addButton("Delete", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, name](int result) {
            if (safeThis != nullptr && result == 1) {
                safeThis->presetManager->deletePreset(name);
                safeThis->currentBasePresetName.clear();
                safeThis->setPresetModified(false);
                safeThis->refreshPresetCombo();
            }
        }),
        true
    );
}

void FDNReverbEditor::paint(juce::Graphics& g) {
    g.fillAll(AmbienceColors::Background);
}

void FDNReverbEditor::resized() {
    const float scale = juce::jmax(0.25f, (float)getWidth() / (float)kBaseW);
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kBaseW, kBaseH);
    audioProcessor.setSavedEditorSize(getWidth(), getHeight());
}

// ── V1.2.0 オリジナル完全一致レイアウト ──
void FDNReverbEditor::layoutContent() {
    titleLabel.setBounds(PAD, Y_HEADER, 140, 32);
    rt60TabButton.setBounds(145, Y_HEADER + 5, 48, 22);
    proTabButton.setBounds(197, Y_HEADER + 5, 48, 22);
    erSoloButton.setBounds(249, Y_HEADER + 5, 62, 22);
    lockButton.setBounds(315, Y_HEADER + 5, 48, 22);
    sendModeButton.setBounds(367, Y_HEADER + 5, 48, 22);
    panicButton.setBounds(419, Y_HEADER + 5, 48, 22);

    vuIn.setBounds(W - 220, Y_HEADER + 2, 96, 28);
    vuOut.setBounds(W - 120, Y_HEADER + 2, 96, 28);

    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    auto place1 = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + ROW1_GAP;
    };
    auto place2 = [&](ArcKnob& k, int& x, int y) {
        k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
        x += KNOB_W + PAD;
    };

    if (!isRT60Tab && !isProTab) {
        // Row 1
        int kx = PAD;
        place1(kPreDelay, kx, Y_ROW1);
        place1(kRoomSize, kx, Y_ROW1);
        place1(kDecay, kx, Y_ROW1);
        place1(kHFDamp, kx, Y_ROW1);
        place1(kLFAbsorb, kx, Y_ROW1);
        place1(kDiffusion, kx, Y_ROW1);
        place1(kModAmt, kx, Y_ROW1);
        place1(kModRate, kx, Y_ROW1);
        place1(kStereoW, kx, Y_ROW1);
        place1(kERLevel, kx, Y_ROW1);
        place1(kSaturation, kx, Y_ROW1);

        // Row 2: MIX | OUT EQ | DUCKING
        kx = PAD;
        place2(kWet, kx, Y_ROW2);
        place2(kDry, kx, Y_ROW2);
        kx += 16;
        place2(kLoCutNorm, kx, Y_ROW2);
        place2(kHiCutNorm, kx, Y_ROW2);
        kx += 16;
        place2(kDuckAmt, kx, Y_ROW2);
        place2(kDuckThr, kx, Y_ROW2);
        place2(kDuckAtt, kx, Y_ROW2);
        place2(kDuckRel, kx, Y_ROW2);
    } else if (isRT60Tab) {
        int kx = PAD;
        for (int i = 0; i < 10; ++i)
            place1(kRTBands[i], kx, Y_ROW1);

        int kx2 = PAD;
        satTypeLabel.setBounds(kx2, Y_SLABEL2, KNOB_W, KNOB_LBL_H);
        satTypeCombo.setBounds(kx2, Y_SLABEL2 + KNOB_LBL_H + 2, KNOB_W + PAD, 24);
        kx2 += KNOB_W + PAD + PAD + 8;
        place2(kTiltLow, kx2, Y_ROW2);
        place2(kTiltMid, kx2, Y_ROW2);
        place2(kTiltHigh, kx2, Y_ROW2);
        kx2 += 16;
        place2(kLoCutPro, kx2, Y_ROW2);
        place2(kHiCutPro, kx2, Y_ROW2);

        const int theme_x = kx2 + 16;
        themeLabel.setBounds(theme_x, Y_SLABEL2, 100, KNOB_LBL_H);
        themeCombo.setBounds(theme_x, Y_SLABEL2 + KNOB_LBL_H + 2, 100, 24);
    } else if (isProTab) {
        // PRO Tab: 6ノブ（上段3つ、下段3つ）を PRESET_PANEL_X (632px) 手前の領域に美しくセンタリング配置
        const int availableW = PRESET_PANEL_X - PAD;
        const int totalKnobW = 3 * KNOB_W;
        const int gap = (availableW - totalKnobW) / 4;
        const int startX = PAD + gap;

        auto placePro = [&](ArcKnob& k, int& x, int y) {
            k.label.setBounds(x, y, KNOB_W, KNOB_LBL_H);
            k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H);
            x += KNOB_W + gap;
        };

        int kx = startX;
        placePro(kScattering, kx, Y_ROW1);
        placePro(kERCrossover, kx, Y_ROW1);
        placePro(kLateDensity, kx, Y_ROW1);

        kx = startX;
        placePro(kAsymmetry, kx, Y_ROW2);
        placePro(kClarity, kx, Y_ROW2);
        placePro(kAirAbsorb, kx, Y_ROW2);
    }

    // Preset Section (Idea B: Wide Combo Top, 4 Buttons Bottom with zero cut-off)
    {
        const int px = PRESET_PANEL_X;
        const int btnH = 26;

        presetPrevButton.setBounds(px, Y_ROW2, 26, btnH);
        presetCombo.setBounds(px + 30, Y_ROW2, 200, btnH);
        presetNextButton.setBounds(px + 234, Y_ROW2, 26, btnH);

        const int btnW = 62;
        const int gap = 4;
        presetSaveButton.setBounds(px, Y_ROW2 + 34, btnW, btnH);
        presetRevertButton.setBounds(px + (btnW + gap) * 1, Y_ROW2 + 34, btnW, btnH);
        presetLoadButton.setBounds(px + (btnW + gap) * 2, Y_ROW2 + 34, btnW, btnH);
        presetDeleteButton.setBounds(px + (btnW + gap) * 3, Y_ROW2 + 34, btnW, btnH);
    }

    // ── Visualizers: RT60 グラフ 135px、ER/LATE 64px ──
    const int rt60Height = 135;
    rt60Viz.setBounds(PAD, Y_VIZ, W - PAD * 2, rt60Height);
    spectrumViz.setBounds(PAD, Y_VIZ, W - PAD * 2, rt60Height);

    const int decayY = Y_VIZ + rt60Height + 4;
    const int decayHeight = H - decayY - PAD;
    decayCurveViz.setBounds(PAD, decayY, W - PAD * 2, decayHeight);

    // ── DECAY TIME 1行表示 (右側の元のDecayTime位置) ──
    const int dtW = 220;
    const int dtX = W - PAD - dtW - 8;
    labelDecayLine.setBounds(dtX, decayY + 4, dtW, 16);
}

void FDNReverbEditor::paintContent(juce::Graphics& g) {
    g.fillAll(AmbienceColors::Background);

    juce::ColourGradient grad(
        AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    auto sl = [&](int x, int y, const char* text, int w = 120) {
        g.setFont(juce::Font(juce::FontOptions(
            "Helvetica Neue", 8.5f, juce::Font::bold)));
        g.drawText(text, x, y, w, KNOB_LBL_H, juce::Justification::centredLeft);
    };

    if (!isRT60Tab && !isProTab) {
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(SEC_TIME, Y_SLABEL1, "TIME");
        sl(SEC_FREQUENCY, Y_SLABEL1, "FREQUENCY");
        sl(SEC_DIFFUSION, Y_SLABEL1, "DIFFUSION");
        sl(SEC_STEREO, Y_SLABEL1, "STEREO");
        sl(SEC_CHARACTER, Y_SLABEL1, "CHARACTER");

        g.setColour(AmbienceColors::Separator);
        auto drawSep = [&](int x) {
            g.drawVerticalLine(x, (float)Y_SLABEL1, (float)(Y_ROW1 + UNIT_H));
        };
        drawSep(SEP_TF);
        drawSep(SEP_FD);
        drawSep(SEP_DS);
        drawSep(SEP_SC);

        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        g.drawHorizontalLine(Y_SLABEL2 - 4, (float)PAD, (float)(W - PAD));

        const int outeq_x = PAD + 2 * (KNOB_W + PAD) + 16;
        const int duck_x = outeq_x + 2 * (KNOB_W + PAD) + 16;

        g.setColour(AmbienceColors::Separator);
        g.drawVerticalLine(outeq_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));
        drawSep(duck_x - 9);

        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(PAD, Y_SLABEL2, "MIX");
        sl(outeq_x, Y_SLABEL2, "OUT EQ");
        sl(duck_x, Y_SLABEL2, "DUCKING");
    } else if (isRT60Tab) {
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(PAD, Y_SLABEL1, "BAND RT60 MULTIPLIERS (10-BAND GRAPHIC EQ)", 500);

        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        g.drawHorizontalLine(Y_SLABEL2 - 4, (float)PAD, (float)(W - PAD));

        const int tilt_x = PAD + KNOB_W + PAD + PAD + 8;
        const int outeq_x = tilt_x + 3 * (KNOB_W + PAD) + 16;
        const int theme_x = outeq_x + 2 * (KNOB_W + PAD) + 16;

        g.setColour(AmbienceColors::Separator);
        g.drawVerticalLine(outeq_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));
        g.drawVerticalLine(theme_x - 9, (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));

        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(tilt_x, Y_SLABEL2, "TILT EQ");
        sl(outeq_x, Y_SLABEL2, "OUT EQ");
        sl(theme_x, Y_SLABEL2, "THEME");
    } else if (isProTab) {
        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(PAD, Y_SLABEL1, "PRO ACOUSTICS DESIGN (SCATTERING / ER CROSSOVER / LATE DENSITY)", 500);

        g.setColour(AmbienceColors::Separator.withAlpha(0.5f));
        g.drawHorizontalLine(Y_SLABEL2 - 4, (float)PAD, (float)(PRESET_PANEL_X - 16));

        g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
        sl(PAD, Y_SLABEL2, "PRO SPATIAL DESIGN (ASYMMETRY / C80 CLARITY / AIR ABSORB)", 500);
    }

    g.setColour(AmbienceColors::Separator);
    g.drawVerticalLine(PRESET_PANEL_X - 9,
        (float)Y_SLABEL2, (float)(Y_ROW2 + UNIT_H));
    g.setColour(AmbienceColors::Accent.withAlpha(0.75f));
    sl(PRESET_PANEL_X, Y_SLABEL2, "PRESET");
}

void FDNReverbEditor::updateTheme(int idx) {
    AmbienceColors::setTheme(idx);

    rt60TabButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    proTabButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    erSoloButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    lockButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    sendModeButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    presetSaveButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Accent);
    labelDecayLine.setColour(juce::Label::textColourId, AmbienceColors::Accent);

    presetRevertButton.setColour(juce::TextButton::buttonColourId,
        isPresetModified ? AmbienceColors::Accent : AmbienceColors::Surface);
    presetRevertButton.setColour(juce::TextButton::textColourOffId,
        isPresetModified ? AmbienceColors::Background : AmbienceColors::TextSecondary);

    algoSelector.updateButtonColors();

    repaint();
    content.repaint();
    rt60Viz.repaint();
    decayCurveViz.repaint();
}
