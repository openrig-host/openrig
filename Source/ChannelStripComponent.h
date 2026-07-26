#pragma once

#include "ChannelStripProcessor.h"
#include "RackSlot.h"
#include <JuceHeader.h>
#include "BoutiqueLookAndFeel.h"
#include "ThemeManager.h"

class BigKnob : public juce::Slider {
public:
  BigKnob(const juce::String &suffix) {
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    setTextValueSuffix(suffix);
  }
};

class GainReductionMeter : public juce::Component, public juce::Timer {
public:
  GainReductionMeter(OpenRigDSP::SimpleComp &c) : comp(c) { startTimer(30); }

  void timerCallback() override {
    float targetGr = comp.getGainReductionDb();
    float previousGr = lastGr;

    if (targetGr > lastGr) {
      lastGr = targetGr;
    } else {
      lastGr += (targetGr - lastGr) * 0.20f;
      if (std::abs(lastGr - targetGr) < 0.05f)
        lastGr = targetGr;
    }

    if (std::abs(lastGr - previousGr) > 0.01f)
      repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    
    // Background meter well
    g.setColour(juce::Colour(0xff121820));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(0xff2a3440));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    float height = bounds.getHeight();
    // Scale up to 24 dB of gain reduction
    float normalizedGr = juce::jlimit(0.0f, 1.0f, lastGr / 24.0f);
    float barHeight = normalizedGr * (height - 4.0f);

    if (barHeight > 1.0f) {
      auto barBounds = bounds.reduced(2.0f);
      auto grRect = barBounds.removeFromTop(barHeight);

      juce::ColourGradient grad(
          ThemeManager::get(Theme::Role::ok), grRect.getX(), grRect.getY(),
          ThemeManager::get(Theme::Role::accent), grRect.getX(), grRect.getBottom(), false);
      g.setGradientFill(grad);
      g.fillRoundedRectangle(grRect, 2.0f);
    }

    // Scale tick marks (-6dB, -12dB, -18dB)
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    float y6 = bounds.getY() + (6.0f / 24.0f) * height;
    float y12 = bounds.getY() + (12.0f / 24.0f) * height;
    float y18 = bounds.getY() + (18.0f / 24.0f) * height;
    g.fillRect(bounds.getX() + 2.0f, y6, 4.0f, 1.0f);
    g.fillRect(bounds.getX() + 2.0f, y12, 4.0f, 1.0f);
    g.fillRect(bounds.getX() + 2.0f, y18, 4.0f, 1.0f);
  }

private:
  OpenRigDSP::SimpleComp &comp;
  float lastGr = 0.0f;
};

class ChannelStripComponent : public juce::Component,
                              public juce::ChangeListener {
public:
  ChannelStripComponent(RackSlot &s, std::function<void()> onClose)
      : slot(s), onCloseRequest(onClose),
        grMeter(slot.getStrip().getCompReference()) {

    // Close Button
    addAndMakeVisible(closeBtn);
    closeBtn.setButtonText("X");
    closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
    closeBtn.onClick = [this] {
      if (onCloseRequest)
        onCloseRequest();
    };

    // --- GATE ---
    addAndMakeVisible(gateGroup);
    gateGroup.setText("NOISE GATE");
    gateGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colours::grey);

    addAndMakeVisible(gateToggle);
    gateToggle.setButtonText("ENABLE");
    gateToggle.setClickingTogglesState(true);
    gateToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
    gateToggle.getProperties().set("useToggleSwitch", true);
    gateToggle.getProperties().set("isOrangeToggle", false);
    gateToggle.setToggleState(slot.getStrip().gateEnabled, juce::dontSendNotification);
    gateToggle.onClick = [this] {
      slot.getStrip().gateEnabled = gateToggle.getToggleState();
    };

    addAndMakeVisible(gateThreshKnob);
    gateThreshKnob.setRange(-80.0, 0.0, 1.0);
    gateThreshKnob.setValue(slot.getStrip().gateThreshold, juce::dontSendNotification);
    gateThreshKnob.onValueChange = [this] {
      slot.getStrip().gateThreshold = (float)gateThreshKnob.getValue();
    };

    // --- 10-BAND GRAPHIC EQ ---
    addAndMakeVisible(eqGroup);
    eqGroup.setText("10-BAND GRAPHIC EQ");
    eqGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colours::cyan);

    addAndMakeVisible(eqToggle);
    eqToggle.setButtonText("ENABLE");
    eqToggle.setClickingTogglesState(true);
    eqToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
    eqToggle.getProperties().set("useToggleSwitch", true);
    eqToggle.getProperties().set("isOrangeToggle", false);
    eqToggle.setToggleState(slot.getStrip().eqEnabled, juce::dontSendNotification);
    eqToggle.onClick = [this] {
      slot.getStrip().eqEnabled = eqToggle.getToggleState();
    };

    addAndMakeVisible(flatBtn);
    flatBtn.setButtonText("FLAT");
    flatBtn.setTooltip("Reset all 10 EQ bands to 0 dB");
    flatBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
    flatBtn.onClick = [this] {
      for (int i = 0; i < 10; ++i) {
        slot.getStrip().eqBands[i].store(0.0f);
        if (eqSliders[i]) eqSliders[i]->setValue(0.0, juce::dontSendNotification);
      }
    };

    // Vol Slider (Input Trim)
    addAndMakeVisible(eqVolSlider);
    eqVolSlider.setSliderStyle(juce::Slider::LinearVertical);
    eqVolSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    eqVolSlider.setRange(-12.0, 12.0, 0.1);
    eqVolSlider.setValue(slot.getStrip().eqVolGain.load(), juce::dontSendNotification);
    eqVolSlider.setDoubleClickReturnValue(true, 0.0);
    eqVolSlider.setTooltip("Input Trim Gain (-12 dB to +12 dB)");
    eqVolSlider.onValueChange = [this] {
      slot.getStrip().eqVolGain.store((float)eqVolSlider.getValue());
    };

    addAndMakeVisible(eqVolLabel);
    eqVolLabel.setText("VOL", juce::dontSendNotification);
    eqVolLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    eqVolLabel.setJustificationType(juce::Justification::centred);

    // 10 Frequency Band Sliders
    const char* bandNames[10] = { "31.25", "62.5", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    for (int i = 0; i < 10; ++i) {
      eqSliders[i] = std::make_unique<juce::Slider>();
      addAndMakeVisible(*eqSliders[i]);
      eqSliders[i]->setSliderStyle(juce::Slider::LinearVertical);
      eqSliders[i]->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
      eqSliders[i]->setRange(-12.0, 12.0, 0.1);
      eqSliders[i]->setValue(slot.getStrip().eqBands[i].load(), juce::dontSendNotification);
      eqSliders[i]->setDoubleClickReturnValue(true, 0.0);
      eqSliders[i]->setTooltip(juce::String(bandNames[i]) + " Hz Band (-12 dB to +12 dB)");
      int bandIdx = i;
      eqSliders[i]->onValueChange = [this, bandIdx] {
        slot.getStrip().eqBands[bandIdx].store((float)eqSliders[bandIdx]->getValue());
      };

      eqLabels[i] = std::make_unique<juce::Label>();
      addAndMakeVisible(*eqLabels[i]);
      eqLabels[i]->setText(bandNames[i], juce::dontSendNotification);
      eqLabels[i]->setFont(juce::FontOptions(10.0f, juce::Font::bold));
      eqLabels[i]->setJustificationType(juce::Justification::centred);
      eqLabels[i]->setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::textDim));
    }

    // Gain Slider (Master Trim)
    addAndMakeVisible(eqGainSlider);
    eqGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    eqGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    eqGainSlider.setRange(-12.0, 12.0, 0.1);
    eqGainSlider.setValue(slot.getStrip().eqMasterGain.load(), juce::dontSendNotification);
    eqGainSlider.setDoubleClickReturnValue(true, 0.0);
    eqGainSlider.setTooltip("Master EQ Output Gain (-12 dB to +12 dB)");
    eqGainSlider.onValueChange = [this] {
      slot.getStrip().eqMasterGain.store((float)eqGainSlider.getValue());
    };

    addAndMakeVisible(eqGainLabel);
    eqGainLabel.setText("GAIN", juce::dontSendNotification);
    eqGainLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    eqGainLabel.setJustificationType(juce::Justification::centred);

    // --- COMP ---
    addAndMakeVisible(compGroup);
    compGroup.setText("COMPRESSOR");
    compGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colours::orange);

    addAndMakeVisible(compToggle);
    compToggle.setButtonText("ENABLE");
    compToggle.setClickingTogglesState(true);
    compToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
    compToggle.getProperties().set("useToggleSwitch", true);
    compToggle.getProperties().set("isOrangeToggle", false);
    compToggle.setToggleState(slot.getStrip().compEnabled, juce::dontSendNotification);
    compToggle.onClick = [this] {
      slot.getStrip().compEnabled = compToggle.getToggleState();
    };

    addAndMakeVisible(compPresetCombo);
    compPresetCombo.addItem("Preset: Manual", 1);
    compPresetCombo.addItem("Preset: MP3 / Track Leveler", 2);
    compPresetCombo.addItem("Preset: Vocal / Lead Focus", 3);
    compPresetCombo.addItem("Preset: Punchy Drums", 4);
    compPresetCombo.addItem("Preset: Brickwall Limiter", 5);
    compPresetCombo.setSelectedId(slot.getStrip().compPreset.load() + 1, juce::dontSendNotification);
    compPresetCombo.onChange = [this] {
      int id = compPresetCombo.getSelectedId() - 1;
      slot.getStrip().compPreset.store(id);
    };

    addAndMakeVisible(compAmtKnob);
    compAmtKnob.setRange(0.0, 1.0, 0.01);
    compAmtKnob.setValue(slot.getStrip().compAmount, juce::dontSendNotification);
    compAmtKnob.setTooltip("Amount / Threshold");
    compAmtKnob.onValueChange = [this] {
      slot.getStrip().compAmount = (float)compAmtKnob.getValue();
    };

    addAndMakeVisible(compMakeupKnob);
    compMakeupKnob.setRange(-12.0, 18.0, 0.5);
    compMakeupKnob.setValue(slot.getStrip().compMakeupDb.load(), juce::dontSendNotification);
    compMakeupKnob.setTooltip("Makeup Gain (dB)");
    compMakeupKnob.onValueChange = [this] {
      slot.getStrip().compMakeupDb.store((float)compMakeupKnob.getValue());
    };

    addAndMakeVisible(grMeter);

    // --- REVERB & CHORUS ---
    addAndMakeVisible(revChoGroup);
    revChoGroup.setText("SPACE / MOD");
    revChoGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colours::magenta.darker(0.3f));

    // Reverb
    addAndMakeVisible(reverbToggle);
    reverbToggle.setButtonText("REVERB");
    reverbToggle.setClickingTogglesState(true);
    reverbToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
    reverbToggle.getProperties().set("useToggleSwitch", true);
    reverbToggle.setToggleState(slot.getStrip().reverbEnabled, juce::dontSendNotification);
    reverbToggle.onClick = [this] {
      slot.getStrip().reverbEnabled = reverbToggle.getToggleState();
    };

    addAndMakeVisible(reverbSizeKnob);
    reverbSizeKnob.setRange(0.0, 1.0, 0.01);
    reverbSizeKnob.setValue(slot.getStrip().reverbSize, juce::dontSendNotification);
    reverbSizeKnob.setTooltip("Room Size");
    reverbSizeKnob.onValueChange = [this] {
      slot.getStrip().reverbSize = (float)reverbSizeKnob.getValue();
    };

    addAndMakeVisible(reverbMixKnob);
    reverbMixKnob.setRange(0.0, 1.0, 0.01);
    reverbMixKnob.setValue(slot.getStrip().reverbMix, juce::dontSendNotification);
    reverbMixKnob.setTooltip("Reverb Mix");
    reverbMixKnob.onValueChange = [this] {
      slot.getStrip().reverbMix = (float)reverbMixKnob.getValue();
    };

    // IR / convolution reverb
    addAndMakeVisible(irToggle);
    irToggle.setButtonText("IR");
    irToggle.setClickingTogglesState(true);
    irToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::iem));
    irToggle.getProperties().set("useToggleSwitch", true);
    irToggle.setToggleState(slot.getStrip().irEnabled.load(), juce::dontSendNotification);
    irToggle.onClick = [this] {
      slot.getStrip().irEnabled.store(irToggle.getToggleState());
    };

    addAndMakeVisible(irLoadBtn);
    irLoadBtn.setButtonText("LOAD IR");
    irLoadBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::iem));
    irLoadBtn.setTooltip("Load an impulse response (.wav) for convolution reverb / cab sim");
    irLoadBtn.onClick = [this] {
      irChooser = std::make_unique<juce::FileChooser>(
          "Load Impulse Response (.wav)",
          juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav");
      auto flags = juce::FileBrowserComponent::openMode |
                   juce::FileBrowserComponent::canSelectFiles;
      irChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (slot.getStrip().getIRReverb().loadIR(f)) {
          irNameLabel.setText(f.getFileName(), juce::dontSendNotification);
          irToggle.setToggleState(true, juce::sendNotification);
          slot.getStrip().irEnabled.store(true);
        }
      });
    };

    addAndMakeVisible(irMixKnob);
    irMixKnob.setRange(0.0, 1.0, 0.01);
    irMixKnob.setValue(slot.getStrip().irMix.load(), juce::dontSendNotification);
    irMixKnob.setTooltip("IR Wet Mix");
    irMixKnob.onValueChange = [this] {
      slot.getStrip().irMix.store((float)irMixKnob.getValue());
    };

    addAndMakeVisible(irNameLabel);
    irNameLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::textDim));
    irNameLabel.setFont(juce::FontOptions(12.0f));
    {
      auto p = slot.getStrip().getIRReverb().getLoadedPath();
      irNameLabel.setText(p.isEmpty() ? "(no IR loaded)" : juce::File(p).getFileName(),
                          juce::dontSendNotification);
    }

    // Chorus
    addAndMakeVisible(chorusToggle);
    chorusToggle.setButtonText("CHORUS");
    chorusToggle.setClickingTogglesState(true);
    chorusToggle.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
    chorusToggle.getProperties().set("useToggleSwitch", true);
    chorusToggle.setToggleState(slot.getStrip().chorusEnabled, juce::dontSendNotification);
    chorusToggle.onClick = [this] {
      slot.getStrip().chorusEnabled = chorusToggle.getToggleState();
    };

    addAndMakeVisible(chorusRateKnob);
    chorusRateKnob.setRange(0.1, 5.0, 0.05);
    chorusRateKnob.setValue(slot.getStrip().chorusRate, juce::dontSendNotification);
    chorusRateKnob.setTooltip("Chorus Rate");
    chorusRateKnob.onValueChange = [this] {
      slot.getStrip().chorusRate = (float)chorusRateKnob.getValue();
    };

    addAndMakeVisible(chorusMixKnob);
    chorusMixKnob.setRange(0.0, 1.0, 0.01);
    chorusMixKnob.setValue(slot.getStrip().chorusMix, juce::dontSendNotification);
    chorusMixKnob.setTooltip("Chorus Mix");
    chorusMixKnob.onValueChange = [this] {
      slot.getStrip().chorusMix = (float)chorusMixKnob.getValue();
    };

    // --- CUSTOMIZE ---
    addAndMakeVisible(nameLabel);
    nameLabel.setText("Channel Name:", juce::dontSendNotification);
    addAndMakeVisible(nameEditor);
    nameEditor.setText(slot.getName());
    nameEditor.onTextChange = [this] {
      slot.setName(nameEditor.getText());
      repaint();
    };

    // Color picker
    addAndMakeVisible(colorLabel);
    colorLabel.setText("Channel Color:", juce::dontSendNotification);
    addAndMakeVisible(colorPreview);
    colorPreview.setColour(juce::Label::backgroundColourId, slot.getChannelColor());
    addAndMakeVisible(colorButton);
    colorButton.setButtonText("Change Color");
    colorButton.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
    colorButton.onClick = [this] { showColorPicker(); };

    // --- AUX SENDS ---
    addAndMakeVisible(auxGroup);
    auxGroup.setText("AUX SENDS");
    auxGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colours::orange);

    addAndMakeVisible(aux1Knob);
    aux1Knob.setRange(0.0, 1.0, 0.01);
    aux1Knob.setValue(slot.getAux1Send(), juce::dontSendNotification);
    aux1Knob.onValueChange = [this] {
      slot.setAux1Send((float)aux1Knob.getValue());
    };

    addAndMakeVisible(aux2Knob);
    aux2Knob.setRange(0.0, 1.0, 0.01);
    aux2Knob.setValue(slot.getAux2Send(), juce::dontSendNotification);
    aux2Knob.onValueChange = [this] {
      slot.setAux2Send((float)aux2Knob.getValue());
    };

    setSize(980, 520);
  }

  void showColorPicker() {
    auto *colourSelector = new juce::ColourSelector(juce::ColourSelector::showColourspace);
    colourSelector->setCurrentColour(slot.getChannelColor().withAlpha((juce::uint8)255));
    colourSelector->setSize(300, 400);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Select Channel Color";
    options.dialogBackgroundColour = ThemeManager::get(Theme::Role::panel);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.content.setOwned(colourSelector);

    juce::Component::SafePointer<ChannelStripComponent> safe(this);
    auto *listener = new ColourChangeListener(safe);
    colourSelector->addChildComponent(listener);
    colourSelector->addChangeListener(listener);

    options.launchAsync();
  }

  void changeListenerCallback(juce::ChangeBroadcaster *source) override {
    if (auto *cs = dynamic_cast<juce::ColourSelector *>(source)) {
      slot.setChannelColor(cs->getCurrentColour());
      colorPreview.setColour(juce::Label::backgroundColourId, slot.getChannelColor());
    }
  }

  void paint(juce::Graphics &g) override {
    auto* laf = dynamic_cast<BoutiqueLookAndFeel*>(&getLookAndFeel());
    bool useModern = (laf != nullptr && laf->useModernStyle);

    if (useModern) {
      g.fillAll(ThemeManager::get(Theme::Role::panel));
      g.setColour(ThemeManager::get(Theme::Role::border));
      g.drawRect(getLocalBounds(), 1);
    } else {
      g.fillAll(ThemeManager::get(Theme::Role::background).withAlpha(0.9f));
    }
    g.setColour(juce::Colours::white);
    g.setFont(24.0f);
    g.drawText("CHANNEL STRIP: " + slot.getName(), 20, 20, 400, 30, juce::Justification::left);
  }

  void resized() override {
    closeBtn.setBounds(getWidth() - 40, 10, 30, 30);
    int sectionY = 60;
    int sectionH = 200;

    int gateW = 140;
    int eqW = 440;
    int compW = 170;
    int revW = 170;

    int gateX = 10;
    int eqX = gateX + gateW + 10;
    int compX = eqX + eqW + 10;
    int revX = compX + compW + 10;

    // Noise Gate
    gateGroup.setBounds(gateX, sectionY, gateW, sectionH);
    gateToggle.setBounds(gateX + 10, sectionY + 20, gateW - 20, 26);
    gateThreshKnob.setBounds(gateX + 10, sectionY + 60, gateW - 20, 110);

    // 10-Band Graphic EQ
    eqGroup.setBounds(eqX, sectionY, eqW, sectionH);
    eqToggle.setBounds(eqX + 10, sectionY + 20, 75, 24);
    flatBtn.setBounds(eqX + 90, sectionY + 20, 55, 24);

    int numSliders = 12;
    int sliderW = (eqW - 20) / numSliders;
    int sliderY = sectionY + 70;
    int sliderH = 115;
    int labelY = sectionY + 48;

    eqVolLabel.setBounds(eqX + 10, labelY, sliderW, 20);
    eqVolSlider.setBounds(eqX + 10, sliderY, sliderW, sliderH);

    for (int i = 0; i < 10; ++i) {
      int sx = eqX + 10 + (i + 1) * sliderW;
      if (eqLabels[i]) eqLabels[i]->setBounds(sx, labelY, sliderW, 20);
      if (eqSliders[i]) eqSliders[i]->setBounds(sx, sliderY, sliderW, sliderH);
    }

    int masterX = eqX + 10 + 11 * sliderW;
    eqGainLabel.setBounds(masterX, labelY, sliderW, 20);
    eqGainSlider.setBounds(masterX, sliderY, sliderW, sliderH);

    // Compressor
    compGroup.setBounds(compX, sectionY, compW, sectionH);
    compToggle.setBounds(compX + 10, sectionY + 20, compW - 20, 24);
    compPresetCombo.setBounds(compX + 10, sectionY + 48, compW - 20, 22);

    int singleKnobW = (compW - 55) / 2;
    compAmtKnob.setBounds(compX + 10, sectionY + 75, singleKnobW, 110);
    compMakeupKnob.setBounds(compX + 10 + singleKnobW, sectionY + 75, singleKnobW, 110);
    grMeter.setBounds(compX + compW - 30, sectionY + 48, 20, 137);

    // Reverb / Chorus
    revChoGroup.setBounds(revX, sectionY, revW, sectionH);
    reverbToggle.setBounds(revX + 10, sectionY + 20, revW - 20, 24);
    reverbSizeKnob.setBounds(revX + 10, sectionY + 46, (revW - 30) / 2, 55);
    reverbMixKnob.setBounds(revX + 10 + (revW - 30) / 2, sectionY + 46, (revW - 30) / 2, 55);

    chorusToggle.setBounds(revX + 10, sectionY + 106, revW - 20, 24);
    chorusRateKnob.setBounds(revX + 10, sectionY + 132, (revW - 30) / 2, 55);
    chorusMixKnob.setBounds(revX + 10 + (revW - 30) / 2, sectionY + 132, (revW - 30) / 2, 55);

    // IR / convolution reverb row (bottom of panel)
    int irY = 385;
    irToggle.setBounds(20, irY, 60, 24);
    irLoadBtn.setBounds(90, irY, 90, 24);
    irMixKnob.setBounds(190, irY - 6, 90, 60);
    irNameLabel.setBounds(290, irY, getWidth() - 300, 24);

    int custY = sectionY + sectionH + 20;
    nameLabel.setBounds(20, custY, 100, 24);
    nameEditor.setBounds(130, custY, 200, 24);
    colorLabel.setBounds(20, custY + 40, 100, 24);
    colorPreview.setBounds(130, custY + 40, 40, 24);
    colorButton.setBounds(180, custY + 40, 100, 24);

    auxGroup.setBounds(getWidth() - 350, custY - 10, 330, 90);
    aux1Knob.setBounds(getWidth() - 330, custY + 10, 120, 70);
    aux2Knob.setBounds(getWidth() - 180, custY + 10, 120, 70);
  }

private:
  struct ColourChangeListener : public juce::Component,
                                public juce::ChangeListener {
    juce::Component::SafePointer<ChannelStripComponent> owner;
    explicit ColourChangeListener(juce::Component::SafePointer<ChannelStripComponent> o)
        : owner(o) {}
    void changeListenerCallback(juce::ChangeBroadcaster *source) override {
      if (owner != nullptr)
        owner->changeListenerCallback(source);
    }
  };

  RackSlot &slot;
  std::function<void()> onCloseRequest;
  juce::TextButton closeBtn;
  juce::GroupComponent gateGroup;
  juce::TextButton gateToggle;
  BigKnob gateThreshKnob{" dB"};
  juce::GroupComponent eqGroup;
  juce::TextButton eqToggle;
  juce::TextButton flatBtn;
  juce::Slider eqVolSlider;
  juce::Label eqVolLabel;
  std::unique_ptr<juce::Slider> eqSliders[10];
  std::unique_ptr<juce::Label> eqLabels[10];
  juce::Slider eqGainSlider;
  juce::Label eqGainLabel;

  juce::GroupComponent compGroup;
  juce::TextButton compToggle;
  juce::ComboBox compPresetCombo;
  BigKnob compAmtKnob{" %"};
  BigKnob compMakeupKnob{" dB"};
  GainReductionMeter grMeter;

  // Space / Mod
  juce::GroupComponent revChoGroup;
  juce::TextButton reverbToggle;
  BigKnob reverbSizeKnob{" Size"};
  BigKnob reverbMixKnob{" Mix"};
  juce::TextButton chorusToggle;
  BigKnob chorusRateKnob{" Hz"};
  BigKnob chorusMixKnob{" Mix"};

  // IR / convolution reverb
  juce::TextButton irToggle;
  juce::TextButton irLoadBtn{"LOAD IR"};
  BigKnob irMixKnob{" Mix"};
  juce::Label irNameLabel{"irNameLabel", "(no IR loaded)"};
  std::unique_ptr<juce::FileChooser> irChooser;

  juce::Label nameLabel;
  juce::TextEditor nameEditor;
  juce::Label colorLabel;
  juce::Label colorPreview;
  juce::TextButton colorButton;

  juce::GroupComponent auxGroup;
  BigKnob aux1Knob{" Aux1"};
  BigKnob aux2Knob{" Aux2"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};
