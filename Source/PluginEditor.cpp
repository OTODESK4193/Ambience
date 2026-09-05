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
    titleLabel.setText(AMBIENCE_FULL_TITLE, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    content.addAndMakeVisible(titleLabel);

    // ── Segmented Pill Tabs (MAIN / RT60 / PRO) ──
    auto setupPillBtn = [&](juce::TextButton& b, const juce::String& text) {
        b.setButtonText(text);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
        b.setColour(juce::TextButton::textColourOnId, AmbienceColors::TextPrimary);
        content.addAndMakeVisible(b);
    };

    setupPillBtn(mainTabButton, "MAIN");
    setupPillBtn(rt60TabButton, "RT60");
    setupPillBtn(proTabButton, "PRO");

    mainTabButton.onClick = [this] {
        audioProcessor.apvts.getParameter("rt60tab")->setValueNotifyingHost(0.0f);
        audioProcessor.apvts.getParameter("protab")->setValueNotifyingHost(0.0f);
    };

    rt60TabButton.setClickingTogglesState(false);
    rt60TabButton.onClick = [this] {
        audioProcessor.apvts.getParameter("protab")->setValueNotifyingHost(0.0f);
        audioProcessor.apvts.getParameter("rt60tab")->setValueNotifyingHost(1.0f);
    };

    proTabButton.setClickingTogglesState(false);
    proTabButton.onClick = [this] {
        audioProcessor.apvts.getParameter("rt60tab")->setValueNotifyingHost(0.0f);
        audioProcessor.apvts.getParameter("protab")->setValueNotifyingHost(1.0f);
    };

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
        auto* wetParam = audioProcessor.apvts.getParameter("wetlevel");
        auto* dryParam = audioProcessor.apvts.getParameter("drylevel");

        if (isSend) {
            // ON: 現在の値をキャッシュして 100% Wet / Mute Dry に設定
            if (wetParam && dryParam) {
                cachedWetDB = *audioProcessor.apvts.getRawParameterValue("wetlevel");
                cachedDryDB = *audioProcessor.apvts.getRawParameterValue("drylevel");
                hasCachedSendMode = true;
                wetParam->setValueNotifyingHost(wetParam->convertTo0to1(0.0f));
                dryParam->setValueNotifyingHost(dryParam->convertTo0to1(-60.0f));
            }
        } else {
            // OFF: キャッシュされていた元のユーザー値を完全復元
            if (wetParam && dryParam) {
                float restoreWet = hasCachedSendMode ? cachedWetDB : -12.0f;
                float restoreDry = hasCachedSendMode ? cachedDryDB : 0.0f;
                wetParam->setValueNotifyingHost(wetParam->convertTo0to1(restoreWet));
                dryParam->setValueNotifyingHost(dryParam->convertTo0to1(restoreDry));
                hasCachedSendMode = false;
            }
        }
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

    // ── Bypass Button (テーマ連動・点灯式バイパス) ──
    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    updateBypassButtonColor();
    bypassButton.onClick = [this] {
        audioProcessor.setBypass(bypassButton.getToggleState());
        updateBypassButtonColor();
    };
    content.addAndMakeVisible(bypassButton);

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
    kLoCutPro.build(a, "locut", "LO FREQ", &content, laf);
    kLoGainPro.build(a, "logain", "LO GAIN", &content, laf);
    kHiCutPro.build(a, "hicut", "HI FREQ", &content, laf);
    kHiGainPro.build(a, "higain", "HI GAIN", &content, laf);

    loEQTypeLabel.setText("LO CURVE", juce::dontSendNotification);
    loEQTypeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    loEQTypeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    loEQTypeLabel.setJustificationType(juce::Justification::centred);
    content.addAndMakeVisible(loEQTypeLabel);

    loEQTypeCombo.addItemList(juce::StringArray{ "Off", "Cut", "Shelf" }, 1);
    loEQTypeCombo.setLookAndFeel(&laf);
    content.addAndMakeVisible(loEQTypeCombo);
    loEQTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        a, "loeqtype", loEQTypeCombo);

    hiEQTypeLabel.setText("HI CURVE", juce::dontSendNotification);
    hiEQTypeLabel.setFont(juce::Font(juce::FontOptions(9.f)));
    hiEQTypeLabel.setColour(juce::Label::textColourId, AmbienceColors::TextSecondary);
    hiEQTypeLabel.setJustificationType(juce::Justification::centred);
    content.addAndMakeVisible(hiEQTypeLabel);

    hiEQTypeCombo.addItemList(juce::StringArray{ "Off", "Cut", "Shelf" }, 1);
    hiEQTypeCombo.setLookAndFeel(&laf);
    content.addAndMakeVisible(hiEQTypeCombo);
    hiEQTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        a, "hieqtype", hiEQTypeCombo);

    content.addAndMakeVisible(outEQViz);

    proSpaceViz.setProcessor(&audioProcessor);
    content.addAndMakeVisible(proSpaceViz);

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

    presetOverlayButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    presetOverlayButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    presetOverlayButton.setButtonText("");
    presetOverlayButton.setAlpha(0.0f);
    presetOverlayButton.setTooltip("Click to open Preset Browser");
    presetOverlayButton.onClick = [this] {
        if (presetBrowser) {
            bool willShow = !presetBrowser->isVisible();
            presetBrowser->setVisible(willShow);
            if (willShow) {
                presetBrowser->setCurrentPreset(currentBasePresetName);
                presetBrowser->toFront(true);
            }
        }
    };
    content.addAndMakeVisible(presetOverlayButton);

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
            applySavedTheme();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                    safeThis->applySavedTheme();
                }
            });
        }
    };
    content.addAndMakeVisible(presetRevertButton);

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
        applySavedTheme();
        if (presetBrowser) presetBrowser->setCurrentPreset(name);
    };

    presetBrowser = std::make_unique<PresetBrowser>(*presetManager, laf);
    presetBrowser->onLoadFactory = [this](const PresetBrowser::FactoryPresetDef& def) {
        ++loadingPresetCounter;
        auto setParamVal = [this](const juce::String& paramId, float value) {
            if (auto* p = audioProcessor.apvts.getParameter(paramId))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        setParamVal("algorithm", (float)def.algorithmIndex);
        setParamVal("roomsize", def.roomSize);
        setParamVal("decaytime", def.decayTime);
        setParamVal("diffusion", def.diffusion);
        setParamVal("modamount", def.modAmount);
        setParamVal("modrate", def.modRate);
        setParamVal("stereowidth", def.stereoWidth);
        setParamVal("predelay", def.preDelayMs);
        setParamVal("erlevel", def.erLevel);
        setParamVal("hfdamping", def.hfDamp);
        setParamVal("lfabsorption", def.lfAbsorb);
        setParamVal("saturation", def.saturation);
        setParamVal("sattype", (float)def.satType);

        // Dry / Wet
        setParamVal("drylevel", def.dryDB);
        setParamVal("wetlevel", def.wetDB);

        // Ducking
        setParamVal("duckamount", def.duckAmount);
        setParamVal("duckthresh", def.duckThresh);
        setParamVal("duckattack", def.duckAttack);
        setParamVal("duckrelease", def.duckRelease);

        // OutEQ
        setParamVal("loeqtype", (float)def.loEQType);
        setParamVal("locut", def.loCut);
        setParamVal("logain", def.loGain);
        setParamVal("hieqtype", (float)def.hiEQType);
        setParamVal("hicut", def.hiCut);
        setParamVal("higain", def.hiGain);

        // PRO ACOUSTIC 6ノブ
        setParamVal("scattering", def.scattering);
        setParamVal("ercrossover", def.erCrossoverMs);
        setParamVal("latedensity", def.lateDensity);
        setParamVal("asymmetry", def.asymmetry);
        setParamVal("clarity", def.clarityDB);
        setParamVal("airabsorb", def.airAbsorbScale);

        // ★ 10バンド RT60 はユーザーの調整幅を残すためデフォルト(1.00x)に設定
        for (int b = 0; b < 10; ++b) {
            setParamVal("rtband" + juce::String(b), 1.0f);
        }
        setParamVal("tiltlow", 1.0f);
        setParamVal("tiltmid", 1.0f);
        setParamVal("tilthigh", 1.0f);

        --loadingPresetCounter;
        currentBasePresetName = def.name;
        setPresetModified(false);
        refreshPresetCombo();
        applySavedTheme();
        if (presetBrowser) presetBrowser->setCurrentPreset(def.name);
    };

    presetBrowser->onLoadUser = [this](const juce::String& name) {
        if (presetManager) {
            ++loadingPresetCounter;
            presetManager->loadPreset(name);
            currentBasePresetName = name;
            setPresetModified(false);
            refreshPresetCombo();
            applySavedTheme();
            if (presetBrowser) presetBrowser->setCurrentPreset(name);
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                    safeThis->applySavedTheme();
                }
            });
        }
    };

    presetBrowser->onClose = [this] {
        if (presetBrowser)
            presetBrowser->setVisible(false);
    };

    content.addChildComponent(*presetBrowser);

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
            applySavedTheme();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                    safeThis->applySavedTheme();
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
            applySavedTheme();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                    safeThis->applySavedTheme();
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
            applySavedTheme();
            juce::Component::SafePointer<FDNReverbEditor> safeThis(this);
            juce::Timer::callAfterDelay(50, [safeThis] {
                if (safeThis != nullptr) {
                    if (safeThis->loadingPresetCounter > 0) --safeThis->loadingPresetCounter;
                    safeThis->setPresetModified(false);
                    safeThis->applySavedTheme();
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
        "locut", "hicut", "loeqtype", "hieqtype", "logain", "higain",
        "rt60tab", "protab", "scattering", "ercrossover", "latedensity", "asymmetry", "clarity", "airabsorb"
    };
    for (const auto& pid : kMonitoredParams)
        audioProcessor.apvts.addParameterListener(pid, this);

    // ── Visualizers ──
    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    rt60Viz.setOpaque(true);
    decayCurveViz.setOpaque(true);
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

    applySavedTheme();

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
        "locut", "hicut", "loeqtype", "hieqtype", "logain", "higain",
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
    vuOut.setReduction(audioProcessor.getDuckingReductionDB());
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

    if (isProTab) {
        int loType = loEQTypeCombo.getSelectedId() - 1;
        float loFreq = static_cast<float>(kLoCutPro.slider.getValue());
        float loGain = static_cast<float>(kLoGainPro.slider.getValue());
        int hiType = hiEQTypeCombo.getSelectedId() - 1;
        float hiFreq = static_cast<float>(kHiCutPro.slider.getValue());
        float hiGain = static_cast<float>(kHiGainPro.slider.getValue());
        outEQViz.setParams(loType, loFreq, loGain, hiType, hiFreq, hiGain);
    }

    bool newRT60Tab = (*audioProcessor.apvts.getRawParameterValue("rt60tab") > 0.5f);
    bool newProTab  = (*audioProcessor.apvts.getRawParameterValue("protab") > 0.5f);
    if (newRT60Tab != isRT60Tab || newProTab != isProTab) {
        isRT60Tab = newRT60Tab;
        isProTab  = newProTab;
        contentSlideOffset = 0.0f;
        tabTransitionAlpha = 1.0f;
        content.setAlpha(1.0f);
        updatePanelVisibility();
        layoutContent();
        content.repaint();
    }

    // スライディングピル位置 (アニメーションなしで即座に同期)
    if (isRT60Tab)       tabPillTargetX = 211.0f;
    else if (isProTab)   tabPillTargetX = 262.0f;
    else                 tabPillTargetX = 160.0f;
    if (tabPillCurrentX != tabPillTargetX) {
        tabPillCurrentX = tabPillTargetX;
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

    // RT60 Tab Panel
    for (auto& k : kRTBands) {
        k.slider.setVisible(isRT60Tab);
        k.label.setVisible(isRT60Tab);
    }
    satTypeLabel.setVisible(isRT60Tab);
    satTypeCombo.setVisible(isRT60Tab);
    kTiltLow.slider.setVisible(isRT60Tab);  kTiltLow.label.setVisible(isRT60Tab);
    kTiltMid.slider.setVisible(isRT60Tab);  kTiltMid.label.setVisible(isRT60Tab);
    kTiltHigh.slider.setVisible(isRT60Tab); kTiltHigh.label.setVisible(isRT60Tab);
    themeLabel.setVisible(isRT60Tab);
    themeCombo.setVisible(isRT60Tab);

    // PRO Tab (6 Knobs + OutEQ Section)
    kScattering.slider.setVisible(isProTab);  kScattering.label.setVisible(isProTab);
    kERCrossover.slider.setVisible(isProTab); kERCrossover.label.setVisible(isProTab);
    kLateDensity.slider.setVisible(isProTab); kLateDensity.label.setVisible(isProTab);
    kAsymmetry.slider.setVisible(isProTab);   kAsymmetry.label.setVisible(isProTab);
    kClarity.slider.setVisible(isProTab);     kClarity.label.setVisible(isProTab);
    kAirAbsorb.slider.setVisible(isProTab);   kAirAbsorb.label.setVisible(isProTab);

    loEQTypeLabel.setVisible(isProTab);
    loEQTypeCombo.setVisible(isProTab);
    kLoCutPro.slider.setVisible(isProTab);  kLoCutPro.label.setVisible(isProTab);
    kLoGainPro.slider.setVisible(isProTab); kLoGainPro.label.setVisible(isProTab);

    hiEQTypeLabel.setVisible(isProTab);
    hiEQTypeCombo.setVisible(isProTab);
    kHiCutPro.slider.setVisible(isProTab);  kHiCutPro.label.setVisible(isProTab);
    kHiGainPro.slider.setVisible(isProTab); kHiGainPro.label.setVisible(isProTab);

    outEQViz.setVisible(isProTab);
    proSpaceViz.setVisible(isProTab);
}

void FDNReverbEditor::refreshPresetCombo() {
    if (!presetManager) return;
    presetCombo.clear(juce::dontSendNotification);
    auto names = presetManager->getPresetNames();

    if (currentBasePresetName.isEmpty()) {
        if (presetManager->getCurrentPresetName().isNotEmpty())
            currentBasePresetName = presetManager->getCurrentPresetName();
        else {
            auto saved = audioProcessor.getLastSavedPresetName();
            if (saved.isNotEmpty())
                currentBasePresetName = saved;
        }
    }

    // ── コンボボックスにアイテムを追加 ──
    bool foundInUserList = false;
    for (int i = 0; i < names.size(); ++i) {
        juce::String itemText = names[i];
        if (itemText == currentBasePresetName) {
            foundInUserList = true;
            if (isPresetModified) itemText += " *";
        }
        presetCombo.addItem(itemText, i + 1);
    }

    // Factory プリセット等の場合、コンボにその項目を追加して選択
    if (!foundInUserList && currentBasePresetName.isNotEmpty()) {
        juce::String itemText = currentBasePresetName + (isPresetModified ? " *" : "");
        presetCombo.addItem(itemText, names.size() + 1);
        presetCombo.setSelectedId(names.size() + 1, juce::dontSendNotification);
    } else {
        int idx = names.indexOf(currentBasePresetName);
        if (idx >= 0)
            presetCombo.setSelectedItemIndex(idx, juce::dontSendNotification);
        else if (presetCombo.getNumItems() > 0)
            presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    // 表示テキストを確実に反映！
    if (currentBasePresetName.isNotEmpty()) {
        juce::String displayText = currentBasePresetName + (isPresetModified ? " *" : "");
        presetCombo.setText(displayText, juce::dontSendNotification);
    }

    presetDeleteButton.setEnabled(foundInUserList);
    presetPrevButton.setEnabled(true);
    presetNextButton.setEnabled(true);
    presetRevertButton.setEnabled(isPresetModified);

    if (presetBrowser)
        presetBrowser->setCurrentPreset(currentBasePresetName);
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
    const auto& theme = laf.getTheme();
    presetRevertButton.setColour(juce::TextButton::buttonColourId,
        modified ? theme.primary : theme.surface);
    presetRevertButton.setColour(juce::TextButton::textColourOffId,
        modified ? theme.background : theme.textSecondary);
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
    g.fillAll(laf.getTheme().background);
}

void FDNReverbEditor::resized() {
    const float scale = juce::jmax(0.25f, (float)getWidth() / (float)kBaseW);
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kBaseW, kBaseH);
    audioProcessor.setSavedEditorSize(getWidth(), getHeight());
}

void FDNReverbEditor::layoutContent() {
    titleLabel.setBounds(PAD, Y_HEADER, 145, 32);

    // ── Segmented Pill Tab Bar (カプセルバー: x=158, w=156, h=24) ──
    mainTabButton.setBounds(160, Y_HEADER + 5, 50, 22);
    rt60TabButton.setBounds(211, Y_HEADER + 5, 50, 22);
    proTabButton.setBounds(262, Y_HEADER + 5, 50, 22);

    erSoloButton.setBounds(320, Y_HEADER + 5, 58, 22);
    lockButton.setBounds(381, Y_HEADER + 5, 44, 22);
    sendModeButton.setBounds(428, Y_HEADER + 5, 44, 22);
    panicButton.setBounds(475, Y_HEADER + 5, 44, 22);
    bypassButton.setBounds(522, Y_HEADER + 5, 54, 22);

    // Bypass の右側 (x=584) から右端 (x=892) までの幅 308px を均等活用！
    // 2つのメーター幅: (308 - 12) / 2 = 148px
    vuIn.setBounds(584, Y_HEADER + 2, 148, 28);
    vuOut.setBounds(744, Y_HEADER + 2, 148, 28);

    algoSelector.setBounds(PAD, Y_ALGO, W - PAD * 2, 30);

    const int slide = static_cast<int>(contentSlideOffset);

    auto placeKnob = [&](ArcKnob& k, int x, int y) {
        k.label.setBounds(x + slide, y, KNOB_W, KNOB_LBL_H);
        k.slider.setBounds(x + slide, y + KNOB_LBL_H, KNOB_W, KNOB_H);
    };

    const int yRow1 = 98;  // カード Y=86 内のノブ上端 (label: 98, slider: 112..184, card: 86..196)
    const int yRow2 = 214; // カード Y=204 内のノブ上端 (label: 214, slider: 228..300, card: 204..316)

    if (!isRT60Tab && !isProTab) {
        // ── MAIN Tab: Row 1 ──
        // TIME (Card: x=8, w=210)
        placeKnob(kPreDelay, 17, yRow1);
        placeKnob(kRoomSize, 85, yRow1);
        placeKnob(kDecay,    153, yRow1);

        // FREQUENCY (Card: x=224, w=144)
        placeKnob(kHFDamp,   230, yRow1);
        placeKnob(kLFAbsorb, 298, yRow1);

        // DIFFUSION & MOD (Card: x=374, w=210)
        placeKnob(kDiffusion, 381, yRow1);
        placeKnob(kModAmt,    449, yRow1);
        placeKnob(kModRate,   517, yRow1);

        // SPATIAL & DYNAMICS (Card: x=590, w=302)
        placeKnob(kStereoW,    615, yRow1);
        placeKnob(kERLevel,    709, yRow1);
        placeKnob(kSaturation, 803, yRow1);

        // ── MAIN Tab: Row 2 ──
        // MIX (Card: x=8, w=144)
        placeKnob(kWet, 14, yRow2);
        placeKnob(kDry, 82, yRow2);

        // OUT EQ (Card: x=158, w=144)
        placeKnob(kLoCutNorm, 164, yRow2);
        placeKnob(kHiCutNorm, 232, yRow2);

        // DUCKING (Card: x=308, w=276)
        placeKnob(kDuckAmt, 312, yRow2);
        placeKnob(kDuckThr, 380, yRow2);
        placeKnob(kDuckAtt, 448, yRow2);
        placeKnob(kDuckRel, 516, yRow2);
    } else if (isRT60Tab) {
        // ── RT60 Tab: Row 1 (10-Band Evenly Centered across 884px) ──
        // Card: x=8, w=884
        for (int i = 0; i < 10; ++i) {
            const int kx = 27 + i * 87;
            placeKnob(kRTBands[i], kx, yRow1);
        }

        // ── RT60 Tab: Row 2 ──
        // SATURATION (Card: x=8, w=144)
        satTypeLabel.setText("TYPE", juce::dontSendNotification);
        satTypeLabel.setBounds(14 + slide, 218, 132, 14);
        satTypeCombo.setBounds(14 + slide, 238, 132, 26);

        // TILT EQ (Card: x=158, w=220)
        placeKnob(kTiltLow,  165, yRow2);
        placeKnob(kTiltMid,  236, yRow2);
        placeKnob(kTiltHigh, 307, yRow2);

        // THEME (Card: x=384, w=200)
        themeLabel.setText("PALETTE", juce::dontSendNotification);
        themeLabel.setBounds(392 + slide, 218, 184, 14);
        themeCombo.setBounds(392 + slide, 238, 184, 26);
    } else if (isProTab) {
        // ── PRO Tab: Row 1 (6 Parameters on Left Card x=8, w=576, Space Viz on Right Card x=590, w=302) ──
        placeKnob(kScattering,   24, yRow1);
        placeKnob(kERCrossover, 120, yRow1);
        placeKnob(kLateDensity, 216, yRow1);
        placeKnob(kAsymmetry,   312, yRow1);
        placeKnob(kClarity,     408, yRow1);
        placeKnob(kAirAbsorb,   504, yRow1);

        // Right Card: PRO ACOUSTIC SPACE VIZ (x=590, y=86, w=302, h=110)
        // 下段の PRESET カード (x=590, w=302) と完全垂直整列
        proSpaceViz.setBounds(590 + slide, 86, 302, 110);

        // ── PRO Tab: Row 2 ──
        // PARAMETRIC OUT EQ (Card: x=8, w=576)
        // LOW BAND
        loEQTypeLabel.setBounds(16 + slide, 218, 64, 14);
        loEQTypeCombo.setBounds(16 + slide, 238, 64, 24);
        placeKnob(kLoCutPro,  86,  yRow2);
        placeKnob(kLoGainPro, 154, yRow2);

        // HIGH BAND
        hiEQTypeLabel.setBounds(226 + slide, 218, 64, 14);
        hiEQTypeCombo.setBounds(226 + slide, 238, 64, 24);
        placeKnob(kHiCutPro,  296, yRow2);
        placeKnob(kHiGainPro, 364, yRow2);

        // OUT EQ RESPONSE GRAPH
        outEQViz.setBounds(436 + slide, 212, 140, 96);
    }

    // ── PRESET Section (Card: x=590, w=302, y=204..316) ──
    {
        const int px = 590 + slide;
        const int btnH = 26;

        // Top line: Navigation & Preset Combo
        presetPrevButton.setBounds(px + 8, 222, 26, btnH);
        presetCombo.setBounds(px + 38, 222, 226, btnH);
        presetOverlayButton.setBounds(px + 38, 222, 226, btnH);
        presetNextButton.setBounds(px + 268, 222, 26, btnH);

        // Bottom line: 3 Action Buttons (各幅 92px, gap 8px)
        const int btnW = 92;
        const int gap = 8;
        presetSaveButton.setBounds(px + 7, 258, btnW, btnH);
        presetRevertButton.setBounds(px + 7 + (btnW + gap) * 1, 258, btnW, btnH);
        presetDeleteButton.setBounds(px + 7 + (btnW + gap) * 2, 258, btnW, btnH);
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

    // ── Preset Browser Overlay (RT60/ERグラフエリア全体に重ねて表示: 884 x 206) ──
    if (presetBrowser) {
        presetBrowser->setBounds(PAD, Y_VIZ, W - PAD * 2, H - Y_VIZ - PAD);
        if (presetBrowser->isVisible())
            presetBrowser->toFront(true);
    }
}

void FDNReverbEditor::paintContent(juce::Graphics& g) {
    const auto& theme = laf.getTheme();
    g.fillAll(theme.background);

    // 全体背景のアンビエントグラデーション
    juce::ColourGradient grad(
        theme.surface.withAlpha(0.18f), 0.f, 0.f,
        theme.background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    // ── ヘッダー: スライディングピル カプセルバー ──
    {
        juce::Rectangle<float> barRect(158.0f, (float)(Y_HEADER + 4), 156.0f, 24.0f);
        // カプセル背景
        g.setColour(theme.surface.withAlpha(0.70f));
        g.fillRoundedRectangle(barRect, 12.0f);
        g.setColour(theme.border.withAlpha(0.60f));
        g.drawRoundedRectangle(barRect.reduced(0.5f), 12.0f, 1.0f);

        // スライディングピルインジケーター (現在選択中のタブ背後をスムーズ移動)
        juce::Rectangle<float> pillRect(tabPillCurrentX, (float)(Y_HEADER + 5), 50.0f, 22.0f);
        g.setColour(theme.primary.withAlpha(0.25f));
        g.fillRoundedRectangle(pillRect, 11.0f);
        g.setColour(theme.primary.withAlpha(0.85f));
        g.drawRoundedRectangle(pillRect.reduced(0.5f), 11.0f, 1.2f);
    }

    // ── Glassmorphic Cards 描画ヘルパー ──
    auto drawCard = [&](float cx, float cy, float cw, float ch, const juce::String& title) {
        juce::Rectangle<float> cardRect(cx, cy, cw, ch);

        // カード半透明サーフェス
        g.setColour(theme.surface.withAlpha(0.40f));
        g.fillRoundedRectangle(cardRect, 6.0f);

        // 微細ハイライト枠線
        g.setColour(theme.border.withAlpha(0.50f));
        g.drawRoundedRectangle(cardRect.reduced(0.5f), 6.0f, 1.0f);

        // アクセントタイトルバッジ (カード上端にすっきりと浮かび上がる)
        if (title.isNotEmpty()) {
            juce::Font badgeFont(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold));
            g.setFont(badgeFont);
            const float textW = badgeFont.getStringWidthFloat(title) + 16.0f;
            juce::Rectangle<float> badgeRect(cx + 8.0f, cy - 7.0f, textW, 14.0f);

            // バッジ背景
            g.setColour(theme.panel);
            g.fillRoundedRectangle(badgeRect, 3.0f);
            g.setColour(theme.border.withAlpha(0.60f));
            g.drawRoundedRectangle(badgeRect.reduced(0.5f), 3.0f, 0.8f);

            // バッジテキスト
            g.setColour(theme.primary.withAlpha(0.90f));
            g.drawText(title, badgeRect, juce::Justification::centred);
        }
    };

    const float cardY1 = 86.0f;
    const float cardH1 = 110.0f;
    const float cardY2 = 204.0f;
    const float cardH2 = 112.0f;

    if (!isRT60Tab && !isProTab) {
        // Row 1 Cards
        drawCard(8.0f,   cardY1, 210.0f, cardH1, "TIME");
        drawCard(224.0f, cardY1, 144.0f, cardH1, "FREQUENCY");
        drawCard(374.0f, cardY1, 210.0f, cardH1, "DIFFUSION & MOD");
        drawCard(590.0f, cardY1, 302.0f, cardH1, "SPATIAL & DYNAMICS");

        // Row 2 Cards
        drawCard(8.0f,   cardY2, 144.0f, cardH2, "MIX");
        drawCard(158.0f, cardY2, 144.0f, cardH2, "OUT EQ");
        drawCard(308.0f, cardY2, 276.0f, cardH2, "DUCKING");
    } else if (isRT60Tab) {
        drawCard(8.0f,   cardY1, 884.0f, cardH1, "10-BAND GRAPHIC RT60 MULTIPLIERS");

        drawCard(8.0f,   cardY2, 144.0f, cardH2, "SATURATION");
        drawCard(158.0f, cardY2, 220.0f, cardH2, "TILT EQ");
        drawCard(384.0f, cardY2, 200.0f, cardH2, "THEME");
    } else if (isProTab) {
        drawCard(8.0f,   cardY1, 576.0f, cardH1, "PRO ACOUSTIC MATRIX (6 PARAMETERS)");
        drawCard(590.0f, cardY1, 302.0f, cardH1, "PRO ACOUSTIC SPACE");
        drawCard(8.0f,   cardY2, 576.0f, cardH2, "PARAMETRIC OUT EQ");
    }

    // PRESET Card (共通: x=590, w=302 で 1行目カードと完全整列！)
    drawCard(590.0f, cardY2, 302.0f, cardH2, "PRESET");
}

void FDNReverbEditor::updateTheme(int idx) {
    laf.setTheme(idx);
    const auto& theme = laf.getTheme();

    rt60TabButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    proTabButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    erSoloButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    lockButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    sendModeButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    presetSaveButton.setColour(juce::TextButton::buttonColourId, theme.primary);
    labelDecayLine.setColour(juce::Label::textColourId, theme.primary);

    presetRevertButton.setColour(juce::TextButton::buttonColourId,
        isPresetModified ? theme.primary : theme.surface);
    presetRevertButton.setColour(juce::TextButton::textColourOffId,
        isPresetModified ? theme.background : theme.textSecondary);

    algoSelector.updateButtonColors();
    updateBypassButtonColor();

    auto f = getThemeSettingsFile();
    if (f != juce::File()) {
        f.replaceWithText(juce::String(idx));
    }

    repaint();
    content.repaint();
    rt60Viz.repaint();
    decayCurveViz.repaint();
    outEQViz.repaint();
    proSpaceViz.repaint();
    if (presetBrowser)
        presetBrowser->repaint();
}

juce::File FDNReverbEditor::getThemeSettingsFile() const {
    return presetManager ? presetManager->getPresetsFolder().getChildFile("_selected_theme.txt") : juce::File();
}

void FDNReverbEditor::applySavedTheme() {
    int idx = 0;
    auto f = getThemeSettingsFile();
    if (f.existsAsFile()) {
        idx = juce::jlimit(0, 9, f.loadFileAsString().trim().getIntValue());
    } else if (auto* rawTheme = audioProcessor.apvts.getRawParameterValue("theme")) {
        idx = juce::jlimit(0, 9, juce::roundToInt(rawTheme->load()));
    }
    themeCombo.setSelectedItemIndex(idx, juce::dontSendNotification);
    laf.setTheme(idx);
    updateTheme(idx);
}

void FDNReverbEditor::updateBypassButtonColor() {
    const auto& theme = laf.getTheme();
    const bool on = bypassButton.getToggleState();
    bypassButton.setColour(juce::TextButton::buttonColourId, on ? theme.primary : theme.surface);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, theme.primary);
    bypassButton.setColour(juce::TextButton::textColourOffId, on ? theme.background : theme.textSecondary);
    bypassButton.setColour(juce::TextButton::textColourOnId, theme.background);
    bypassButton.repaint();
}
