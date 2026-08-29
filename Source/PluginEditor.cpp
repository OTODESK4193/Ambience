#include "PluginProcessor.h"
#include "PluginEditor.h"

FDNReverbEditor::FDNReverbEditor(FDNReverbAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    algoSelector(p.apvts),
    spectrumViz(p),
    vuIn("IN", VUMeter::Side::Input),
    vuOut("OUT", VUMeter::Side::Output)
{
    setLookAndFeel(&laf);

    // ── Content Component の設定 (LIFT-X 式アスペクト比固定スケーリング) ──
    addAndMakeVisible(content);
    content.onPaint = [this](juce::Graphics& g) { paintContent(g); };
    content.onLayout = [this] { layoutContent(); };

    constrainer.setFixedAspectRatio((double)kBaseW / (double)kBaseH);
    constrainer.setSizeLimits(
        static_cast<int>(kBaseW * 0.7), static_cast<int>(kBaseH * 0.7),
        static_cast<int>(kBaseW * 2.0), static_cast<int>(kBaseH * 2.0));
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(kBaseW, kBaseH);

    // ── Title ──
    titleLabel.setText("AMBIENCE 1.2.1 B013", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(
        "Helvetica Neue", 14.f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, AmbienceColors::TextPrimary);
    content.addAndMakeVisible(titleLabel);

    // ── ProMode Button ──
    proModeButton.setButtonText("PRO");
    proModeButton.setClickingTogglesState(true);
    proModeButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    proModeButton.setColour(juce::TextButton::buttonOnColourId, AmbienceColors::Accent);
    proModeButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    proModeButton.setColour(juce::TextButton::textColourOnId, AmbienceColors::Background);
    content.addAndMakeVisible(proModeButton);
    proModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "promode", proModeButton);

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

    // ── Algorithm Selector ──
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

    // ★ 正しい APVTS ID に修正 (wetlevel / drylevel)
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

    presetLoadButton.setButtonText("LOAD");
    presetLoadButton.setColour(juce::TextButton::buttonColourId, AmbienceColors::Surface);
    presetLoadButton.setColour(juce::TextButton::textColourOffId, AmbienceColors::TextSecondary);
    presetLoadButton.onClick = [this] {
        if (presetManager && presetCombo.getSelectedId() > 0)
            presetManager->loadPreset(presetCombo.getText());
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
        refreshPresetCombo();
    };

    presetCombo.onChange = [this] {
        if (presetManager && presetCombo.getSelectedId() > 0)
            presetManager->loadPreset(presetCombo.getText());
    };
    presetPrevButton.onClick = [this] { if (presetManager) presetManager->loadPrevPreset(); };
    presetNextButton.onClick = [this] { if (presetManager) presetManager->loadNextPreset(); };

    // ── Visualizers ──
    rt60Viz.setProcessor(&p);
    decayCurveViz.setProcessor(&p);
    content.addAndMakeVisible(rt60Viz);
    content.addAndMakeVisible(spectrumViz);
    content.addAndMakeVisible(decayCurveViz);

    content.addAndMakeVisible(vuIn);
    content.addAndMakeVisible(vuOut);

    // ── DECAY TIME 超特大表示 (右下端) ──
    labelMetricsTitle.setText("DECAY TIME", juce::dontSendNotification);
    labelMetricsTitle.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.5f, juce::Font::bold)));
    labelMetricsTitle.setColour(juce::Label::textColourId, AmbienceColors::Accent.withAlpha(0.9f));
    labelMetricsTitle.setJustificationType(juce::Justification::centredRight);
    content.addAndMakeVisible(labelMetricsTitle);

    labelDecayLargeValue.setText("--", juce::dontSendNotification);
    labelDecayLargeValue.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 30.0f, juce::Font::bold)));
    labelDecayLargeValue.setColour(juce::Label::textColourId, AmbienceColors::Accent);
    labelDecayLargeValue.setJustificationType(juce::Justification::centredRight);
    content.addAndMakeVisible(labelDecayLargeValue);

    refreshPresetCombo();
    updatePanelVisibility();
    startTimerHz(60);
}

FDNReverbEditor::~FDNReverbEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
    satTypeCombo.setLookAndFeel(nullptr);
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
            labelDecayLargeValue.setText(juce::String(edt, 2) + " s", juce::dontSendNotification);
        } else {
            labelDecayLargeValue.setText(juce::String(edt, 1) + " s", juce::dontSendNotification);
        }
    }

    bool newProMode = (*audioProcessor.apvts.getRawParameterValue("promode") > 0.5f);
    if (newProMode != isProMode) {
        isProMode = newProMode;
        updatePanelVisibility();
        layoutContent();
        content.repaint();
    }
}

void FDNReverbEditor::updatePanelVisibility() {
    bool normal = !isProMode;
    kPreDelay.slider.setVisible(normal);   kPreDelay.label.setVisible(normal);
    kRoomSize.slider.setVisible(normal);   kRoomSize.label.setVisible(normal);
    kDecay.slider.setVisible(normal);      kDecay.label.setVisible(normal);
    kHFDamp.slider.setVisible(normal);     kHFDamp.label.setVisible(normal);
    kLFAbsorb.slider.setVisible(normal);   kLFAbsorb.label.setVisible(normal);
    kDiffusion.slider.setVisible(normal);  kDiffusion.label.setVisible(normal);
    kModAmt.slider.setVisible(normal);     kModAmt.label.setVisible(normal);
    kModRate.slider.setVisible(normal);    kModRate.label.setVisible(normal);
    kStereoW.slider.setVisible(normal);    kStereoW.label.setVisible(normal);
    kERLevel.slider.setVisible(normal);    kERLevel.label.setVisible(normal);
    kSaturation.slider.setVisible(normal); kSaturation.label.setVisible(normal);
    kWet.slider.setVisible(normal);        kWet.label.setVisible(normal);
    kDry.slider.setVisible(normal);        kDry.label.setVisible(normal);
    kLoCutNorm.slider.setVisible(normal);  kLoCutNorm.label.setVisible(normal);
    kHiCutNorm.slider.setVisible(normal);  kHiCutNorm.label.setVisible(normal);
    kDuckAmt.slider.setVisible(normal);    kDuckAmt.label.setVisible(normal);
    kDuckThr.slider.setVisible(normal);    kDuckThr.label.setVisible(normal);
    kDuckAtt.slider.setVisible(normal);    kDuckAtt.label.setVisible(normal);
    kDuckRel.slider.setVisible(normal);    kDuckRel.label.setVisible(normal);

    for (auto& k : kRTBands) {
        k.slider.setVisible(isProMode);
        k.label.setVisible(isProMode);
    }
    satTypeLabel.setVisible(isProMode);
    satTypeCombo.setVisible(isProMode);
    kTiltLow.slider.setVisible(isProMode);  kTiltLow.label.setVisible(isProMode);
    kTiltMid.slider.setVisible(isProMode);  kTiltMid.label.setVisible(isProMode);
    kTiltHigh.slider.setVisible(isProMode); kTiltHigh.label.setVisible(isProMode);
    kLoCutPro.slider.setVisible(isProMode); kLoCutPro.label.setVisible(isProMode);
    kHiCutPro.slider.setVisible(isProMode); kHiCutPro.label.setVisible(isProMode);
}

void FDNReverbEditor::refreshPresetCombo() {
    if (!presetManager) return;
    presetCombo.clear(juce::dontSendNotification);
    auto names = presetManager->getPresetNames();

    if (presetManager->getCurrentPresetName().isEmpty()) {
        auto saved = audioProcessor.getLastSavedPresetName();
        if (saved.isNotEmpty())
            presetManager->setCurrentPresetName(saved);
    }

    if (names.isEmpty()) {
        presetCombo.addItem("-- No Presets --", 1);
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        presetDeleteButton.setEnabled(false);
        presetLoadButton.setEnabled(false);
        presetPrevButton.setEnabled(false);
        presetNextButton.setEnabled(false);
        return;
    }

    for (int i = 0; i < names.size(); ++i)
        presetCombo.addItem(names[i], i + 1);

    int idx = presetManager->getCurrentPresetIndex();
    if (idx >= 0)
        presetCombo.setSelectedItemIndex(idx, juce::dontSendNotification);
    else
        presetCombo.setSelectedItemIndex(0, juce::dontSendNotification);

    presetDeleteButton.setEnabled(true);
    presetLoadButton.setEnabled(true);
    presetPrevButton.setEnabled(names.size() > 1);
    presetNextButton.setEnabled(names.size() > 1);
}

void FDNReverbEditor::savePresetWithDialog() {
    if (!presetManager) return;
    auto* dialog = new juce::AlertWindow(
        "Save Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);

    dialog->addTextEditor("name", presetManager->getCurrentPresetName());
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, dialog](int result) {
            if (safeThis != nullptr && result == 1) {
                auto name = dialog->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                    safeThis->presetManager->savePreset(name);
            }
        }),
        true
    );
}

void FDNReverbEditor::deleteCurrentPreset() {
    if (!presetManager) return;
    auto name = presetManager->getCurrentPresetName();
    if (name.isEmpty()) return;

    auto* dialog = new juce::AlertWindow(
        "Delete Preset", "Delete \"" + name + "\"?", juce::MessageBoxIconType::WarningIcon);

    dialog->addButton("Delete", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FDNReverbEditor> safeThis(this);

    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, name](int result) {
            if (safeThis != nullptr && result == 1)
                safeThis->presetManager->deletePreset(name);
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
}

void FDNReverbEditor::layoutContent() {
    int topY = PAD;

    titleLabel.setBounds(PAD + 6, topY + 4, 180, 20);

    const int vuW = 100, vuH = 10;
    const int vuX = W - PAD - vuW - 24;
    vuIn.setBounds(vuX, topY + 2, vuW, vuH);
    vuOut.setBounds(vuX, topY + 14, vuW, vuH);

    int btnW = 54, btnH = 20;
    proModeButton.setBounds(196, topY + 4, btnW, btnH);
    erSoloButton.setBounds(256, topY + 4, 68, btnH);

    int algoY = topY + 28;
    int algoH = 26;
    algoSelector.setBounds(PAD, algoY, W - PAD * 2, algoH);

    int row1Y = algoY + algoH + 18;
    auto placeKnob = [](ArcKnob& k, int x, int y) {
        k.slider.setBounds(x, y + KNOB_LBL_H, KNOB_W, KNOB_H - KNOB_LBL_H);
        k.label.setBounds(x - 4, y, KNOB_W + 8, KNOB_LBL_H);
    };

    if (!isProMode) {
        int x = PAD + 2;
        placeKnob(kPreDelay, x, row1Y);       x += KNOB_W + 14;
        placeKnob(kRoomSize, x, row1Y);       x += KNOB_W + 14;
        placeKnob(kDecay, x, row1Y);          x += KNOB_W + ROW1_GAP + 14;
        placeKnob(kHFDamp, x, row1Y);         x += KNOB_W + 14;
        placeKnob(kLFAbsorb, x, row1Y);       x += KNOB_W + ROW1_GAP + 14;
        placeKnob(kDiffusion, x, row1Y);      x += KNOB_W + 14;
        placeKnob(kModAmt, x, row1Y);         x += KNOB_W + 14;
        placeKnob(kModRate, x, row1Y);        x += KNOB_W + ROW1_GAP + 14;
        placeKnob(kStereoW, x, row1Y);        x += KNOB_W + ROW1_GAP + 14;
        placeKnob(kERLevel, x, row1Y);        x += KNOB_W + 14;
        placeKnob(kSaturation, x, row1Y);

        int row2Y = row1Y + UNIT_H + 8;
        x = PAD + 2;
        // ── MIX ──
        placeKnob(kWet, x, row2Y);            x += KNOB_W + 10;
        placeKnob(kDry, x, row2Y);            x += KNOB_W + 18;
        // ── OUT EQ ──
        placeKnob(kLoCutNorm, x, row2Y);      x += KNOB_W + 10;
        placeKnob(kHiCutNorm, x, row2Y);      x += KNOB_W + 18;
        // ── DUCKING (ノブ終了位置 x=612 に収め、Preset との干渉を完全排除) ──
        placeKnob(kDuckAmt, x, row2Y);        x += KNOB_W + 10;
        placeKnob(kDuckThr, x, row2Y);        x += KNOB_W + 10;
        placeKnob(kDuckAtt, x, row2Y);        x += KNOB_W + 10;
        placeKnob(kDuckRel, x, row2Y);
    } else {
        int r1X = PAD + 2;
        int bandSpacing = 68;
        for (int i = 0; i < 7; ++i) {
            placeKnob(kRTBands[i], r1X, row1Y);
            r1X += bandSpacing;
        }
        int rightX = r1X + 8;
        satTypeLabel.setBounds(rightX, row1Y, 76, KNOB_LBL_H);
        satTypeCombo.setBounds(rightX, row1Y + KNOB_LBL_H + 4, 76, 22);
        placeKnob(kTiltLow, rightX + 86, row1Y);
        placeKnob(kTiltMid, rightX + 86 + 68, row1Y);
        placeKnob(kTiltHigh, rightX + 86 + 136, row1Y);

        int row2Y = row1Y + UNIT_H + 8;
        int r2X = PAD + 2;
        for (int i = 7; i < 10; ++i) {
            placeKnob(kRTBands[i], r2X, row2Y);
            r2X += bandSpacing;
        }
        r2X += 16;
        placeKnob(kLoCutPro, r2X, row2Y); r2X += bandSpacing;
        placeKnob(kHiCutPro, r2X, row2Y);
    }

    int row2Y = row1Y + UNIT_H + 8;
    int prH = 22;
    static constexpr int PRESET_X = 640;
    presetPrevButton.setBounds(PRESET_X, row2Y + 14, 24, prH);
    presetCombo.setBounds(PRESET_X + 28, row2Y + 14, 164, prH);
    presetNextButton.setBounds(PRESET_X + 196, row2Y + 14, 24, prH);

    int btnRowY = row2Y + 14 + prH + 6;
    int smBtnW = 66, smBtnH = 22;
    presetSaveButton.setBounds(PRESET_X + 2, btnRowY, smBtnW, smBtnH);
    presetLoadButton.setBounds(PRESET_X + 72, btnRowY, smBtnW, smBtnH);
    presetDeleteButton.setBounds(PRESET_X + 142, btnRowY, smBtnW + 10, smBtnH);

    // ── Visualizers (RT60グラフ 112px 大画面, ER/LATE 74px 洗練バー) ──
    int vizTop = row2Y + UNIT_H + 4;
    int vizH = 112;
    rt60Viz.setBounds(PAD, vizTop, W - PAD * 2, vizH);
    spectrumViz.setBounds(PAD, vizTop, W - PAD * 2, vizH);

    int decayY = vizTop + vizH + 6;
    int decayHeight = H - decayY - PAD;
    decayCurveViz.setBounds(PAD, decayY, W - PAD * 2, decayHeight);

    // ── DECAY TIME 超特大表示 (右下端) ──
    const int dtW = 160;
    const int dtX = W - PAD - dtW - 12;
    labelMetricsTitle.setBounds(dtX, decayY + 6, dtW, 12);
    labelDecayLargeValue.setBounds(dtX, decayY + 18, dtW, 36);
}

void FDNReverbEditor::paintContent(juce::Graphics& g) {
    g.fillAll(AmbienceColors::Background);
    juce::ColourGradient grad(
        AmbienceColors::Surface.withAlpha(0.12f), 0.f, 0.f,
        AmbienceColors::Background, 0.f, (float)H, false);
    g.setGradientFill(grad);
    g.fillAll();

    g.setFont(juce::Font(juce::FontOptions(8.f)));
    g.setColour(AmbienceColors::TextSecondary.withAlpha(0.6f));
    g.drawText("16ch FDN | SAPF | ISM-ER | 44.1-192kHz",
        330, PAD + 8, 220, 14, juce::Justification::centredLeft);

    auto drawSectionHeader = [&](const juce::String& title, int x, int y, int w) {
        g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", 8.0f, juce::Font::bold)));
        g.setColour(AmbienceColors::Accent.withAlpha(0.85f));
        g.drawText(title, x, y, w, 10, juce::Justification::left);
    };

    int algoY = PAD + 28;
    int row1Y = algoY + 26 + 18;
    int secHdrY = row1Y - 14;

    if (!isProMode) {
        int x = PAD + 2;
        drawSectionHeader("TIME", x, secHdrY, KNOB_W * 3 + 28);
        x += (KNOB_W + 14) * 3 + ROW1_GAP - 14;
        drawSectionHeader("FREQUENCY", x, secHdrY, KNOB_W * 2 + 14);
        x += (KNOB_W + 14) * 2 + ROW1_GAP - 14;
        drawSectionHeader("DIFFUSION", x, secHdrY, KNOB_W * 3 + 28);
        x += (KNOB_W + 14) * 3 + ROW1_GAP - 14;
        drawSectionHeader("STEREO", x, secHdrY, KNOB_W);
        x += KNOB_W + ROW1_GAP;
        drawSectionHeader("CHARACTER", x, secHdrY, KNOB_W * 2 + 14);

        int row2Y = row1Y + UNIT_H + 8;
        int secHdr2Y = row2Y - 14;
        x = PAD + 2;
        drawSectionHeader("MIX", x, secHdr2Y, KNOB_W * 2 + 10);
        x += (KNOB_W + 10) * 2 + 18 - 10;
        drawSectionHeader("OUT EQ", x, secHdr2Y, KNOB_W * 2 + 10);
        x += (KNOB_W + 10) * 2 + 18 - 10;
        drawSectionHeader("DUCKING", x, secHdr2Y, KNOB_W * 4 + 30);
        drawSectionHeader("PRESET", 640, secHdr2Y, 200);
    } else {
        int x = PAD + 2;
        drawSectionHeader("BAND RT60 MULTIPLIERS (10-BAND GRAPHIC EQ)", x, secHdrY, 400);
        int row2Y = row1Y + UNIT_H + 8;
        drawSectionHeader("PRESET", 640, row2Y - 14, 200);
    }
}
