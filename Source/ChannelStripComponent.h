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

    if (targetGr < lastGr) {
      lastGr = targetGr;
    } else {
      lastGr += (targetGr - lastGr) * 0.15f;
      if (std::abs(lastGr - targetGr) < 0.05f)
        lastGr = targetGr;
    }

    if (std::abs(lastGr - previousGr) > 0.01f)
      repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(bounds, 4.0f);

    float height = bounds.getHeight();
    float normalizedGr = juce::jlimit(0.0f, 1.0f, -lastGr / 20.0f);
    float barHeight = normalizedGr * height;

    g.setColour(juce::Colours::red.withAlpha(0.8f));
    g.fillRect(bounds.removeFromTop(barHeight));
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

    // --- EQ ---
    addAndMakeVisible(eqGroup);
    eqGroup.setText("EQ SECTION");
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

    addAndMakeVisible(hpfKnob);
    hpfKnob.setRange(20.0, 400.0, 1.0);
    hpfKnob.setValue(slot.getStrip().hpfFreq, juce::dontSendNotification);
    hpfKnob.onValueChange = [this] {
      slot.getStrip().hpfFreq = (float)hpfKnob.getValue();
    };

    addAndMakeVisible(eqLowLabel);
    eqLowLabel.setText("LOW", juce::dontSendNotification);
    eqLowLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lowKnob);
    lowKnob.setRange(-12.0, 12.0, 0.1);
    lowKnob.setValue(slot.getStrip().lowShelfGain, juce::dontSendNotification);
    lowKnob.onValueChange = [this] {
      slot.getStrip().lowShelfGain = (float)lowKnob.getValue();
    };

    addAndMakeVisible(eqMedLabel);
    eqMedLabel.setText("HPF", juce::dontSendNotification);
    eqMedLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(eqHiLabel);
    eqHiLabel.setText("HI", juce::dontSendNotification);
    eqHiLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(highKnob);
    highKnob.setRange(-12.0, 12.0, 0.1);
    highKnob.setValue(slot.getStrip().highShelfGain, juce::dontSendNotification);
    highKnob.onValueChange = [this] {
      slot.getStrip().highShelfGain = (float)highKnob.getValue();
    };

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

    addAndMakeVisible(compAmtKnob);
    compAmtKnob.setRange(0.0, 1.0, 0.01);
    compAmtKnob.setValue(slot.getStrip().compAmount, juce::dontSendNotification);
    compAmtKnob.onValueChange = [this] {
      slot.getStrip().compAmount = (float)compAmtKnob.getValue();
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

    setSize(800, 480);
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
    int colW = getWidth() / 4;

    gateGroup.setBounds(10, sectionY, colW - 20, sectionH);
    gateToggle.setBounds(20, sectionY + 20, colW - 40, 30);
    gateThreshKnob.setBounds(20, sectionY + 60, colW - 40, 100);

    eqGroup.setBounds(colW + 10, sectionY, colW - 20, sectionH);
    eqToggle.setBounds(colW + 20, sectionY + 20, colW - 40, 30);
    int eqKnobW = (colW - 40) / 3;
    eqMedLabel.setBounds(colW + 20, sectionY + 50, eqKnobW, 20);
    hpfKnob.setBounds(colW + 20, sectionY + 60, eqKnobW, 80);
    eqLowLabel.setBounds(colW + 20 + eqKnobW, sectionY + 50, eqKnobW, 20);
    lowKnob.setBounds(colW + 20 + eqKnobW, sectionY + 60, eqKnobW, 80);
    eqHiLabel.setBounds(colW + 20 + (eqKnobW * 2), sectionY + 50, eqKnobW, 20);
    highKnob.setBounds(colW + 20 + (eqKnobW * 2), sectionY + 60, eqKnobW, 80);

    compGroup.setBounds((colW * 2) + 10, sectionY, colW - 20, sectionH);
    compToggle.setBounds((colW * 2) + 20, sectionY + 20, colW - 40, 30);
    compAmtKnob.setBounds((colW * 2) + 20, sectionY + 60, colW - 80, 100);
    grMeter.setBounds((colW * 3) - 50, sectionY + 60, 20, 100);

    // Reverb / Chorus column
    int col4X = colW * 3;
    revChoGroup.setBounds(col4X + 10, sectionY, colW - 20, sectionH);
    reverbToggle.setBounds(col4X + 20, sectionY + 20, colW - 40, 24);
    reverbSizeKnob.setBounds(col4X + 20, sectionY + 46, (colW - 50) / 2, 55);
    reverbMixKnob.setBounds(col4X + 20 + (colW - 50) / 2, sectionY + 46, (colW - 50) / 2, 55);

    chorusToggle.setBounds(col4X + 20, sectionY + 106, colW - 40, 24);
    chorusRateKnob.setBounds(col4X + 20, sectionY + 132, (colW - 50) / 2, 55);
    chorusMixKnob.setBounds(col4X + 20 + (colW - 50) / 2, sectionY + 132, (colW - 50) / 2, 55);

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
  juce::Label eqLowLabel, eqMedLabel, eqHiLabel;
  BigKnob hpfKnob{" Hz"};
  BigKnob lowKnob{" dB"};
  BigKnob highKnob{" dB"};
  juce::GroupComponent compGroup;
  juce::TextButton compToggle;
  BigKnob compAmtKnob{" %"};
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
