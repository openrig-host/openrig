#include "RackSlotComponent.h"
#include "Logger.h"

namespace {
juce::String midiNoteName(double v) {
  static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  int n = juce::jlimit(0, 127, (int)v);
  return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

// Per-instrument layer config: enable, level, and key range. Bound live to the
// slot's ChainSlotSettings.
class PluginStackConfigComp : public juce::Component {
public:
  PluginStackConfigComp(RackSlot &s, int chainIdx) : slot(s), idx(chainIdx) {
    auto &cs = slot.getChainSlotSettings(idx);

    enableBtn.setButtonText("Enabled");
    enableBtn.setClickingTogglesState(true);
    enableBtn.setToggleState(cs.enabled.load(), juce::dontSendNotification);
    enableBtn.onClick = [this] {
      slot.getChainSlotSettings(idx).enabled.store(enableBtn.getToggleState());
    };
    addAndMakeVisible(enableBtn);

    levelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    levelSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    levelSlider.setRange(0.0, 1.5, 0.01);
    levelSlider.setValue(cs.level.load(), juce::dontSendNotification);
    levelSlider.textFromValueFunction = [](double v) { return juce::String(v, 2); };
    levelSlider.onValueChange = [this] {
      slot.getChainSlotSettings(idx).level.store((float)levelSlider.getValue());
    };
    addAndMakeVisible(levelSlider);

    levelLabel.setText("Level", juce::dontSendNotification);
    levelLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(levelLabel);

    rangeSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
    rangeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rangeSlider.setRange(0, 127, 1);
    rangeSlider.setMinAndMaxValues(cs.lowNote.load(), cs.highNote.load(),
                                   juce::dontSendNotification);
    rangeSlider.onValueChange = [this] {
      auto &cs2 = slot.getChainSlotSettings(idx);
      cs2.lowNote.store((int)rangeSlider.getMinValue());
      cs2.highNote.store((int)rangeSlider.getMaxValue());
      refreshRangeLabel();
    };
    addAndMakeVisible(rangeSlider);

    rangeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(rangeLabel);
    refreshRangeLabel();

    setSize(240, 150);
  }

  void resized() override {
    auto b = getLocalBounds().reduced(8);
    enableBtn.setBounds(b.removeFromTop(24));
    b.removeFromTop(4);
    levelLabel.setBounds(b.removeFromTop(16));
    levelSlider.setBounds(b.removeFromTop(56));
    b.removeFromTop(4);
    rangeLabel.setBounds(b.removeFromTop(16));
    rangeSlider.setBounds(b);
  }

private:
  void refreshRangeLabel() {
    rangeLabel.setText("Range: " + midiNoteName(rangeSlider.getMinValue()) + " - " +
                           midiNoteName(rangeSlider.getMaxValue()),
                       juce::dontSendNotification);
  }

  RackSlot &slot;
  int idx;
  juce::ToggleButton enableBtn;
  juce::Slider levelSlider;
  juce::Slider rangeSlider;
  juce::Label levelLabel, rangeLabel;
};
} // namespace

RackSlotComponent::RackSlotComponent(RackSlot &s, int index, juce::LookAndFeel &laf)
    : slot(s), slotIndex(index) {
  slotBtnMouseListener.owner = this;
  addAndMakeVisible(channelSlider);
  channelSlider.setLookAndFeel(&laf);
  channelSlider.setComponentID("foh");
  channelSlider.setSliderStyle(juce::Slider::LinearVertical);
  channelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  channelSlider.setRange(0.0, 1.0);
  channelSlider.onValueChange = [this]() {
    float val = (float)channelSlider.getValue();
    slot.setFohLevel(val);
    if (slot.areFadersLinked()) {
      slot.setIemLevel(val);
      iemSlider.setValue(val, juce::dontSendNotification);
    }
  };

  addAndMakeVisible(iemSlider);
  iemSlider.setLookAndFeel(&laf);
  iemSlider.setComponentID("iem");
  iemSlider.setSliderStyle(juce::Slider::LinearVertical);
  iemSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  iemSlider.setRange(0.0, 1.0);
  iemSlider.onValueChange = [this]() {
    float val = (float)iemSlider.getValue();
    slot.setIemLevel(val);
    if (slot.areFadersLinked()) {
      slot.setFohLevel(val);
      channelSlider.setValue(val, juce::dontSendNotification);
    }
  };

  addAndMakeVisible(linkButton);
  linkButton.setVisible(false);
  linkButton.setButtonText(juce::CharPointer_UTF8("\xf0\x9f\x94\x97"));
  linkButton.setClickingTogglesState(true);
  linkButton.setToggleState(true, juce::dontSendNotification);
  linkButton.setTooltip("Link FOH/IEM faders");
  linkButton.onClick = [this]() {
    bool linked = linkButton.getToggleState();
    slot.setFadersLinked(linked);
    linkButton.setButtonText(
        linked ? juce::CharPointer_UTF8("\xf0\x9f\x94\x97")
               : juce::CharPointer_UTF8("\xf0\x9f\x94\x93"));
  };

  addAndMakeVisible(iemOffsetKnob);
  iemOffsetKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  iemOffsetKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  iemOffsetKnob.setRange(0.0, 2.0);
  iemOffsetKnob.setValue(1.0);
  iemOffsetKnob.setTooltip("IEM Offset (relative to FOH)");
  iemOffsetKnob.onValueChange = [this] {
    slot.setIemOffset((float)iemOffsetKnob.getValue());
  };

  addAndMakeVisible(inputSelector);
  inputSelector.setTooltip("Select hardware input channel");
  inputSelector.addItem("---", 1);
  inputSelector.setSelectedId(1, juce::dontSendNotification);
  inputSelector.onChange = [this]() {
    slot.setInputChannelIndex(inputSelector.getSelectedId() - 2);
  };

  addAndMakeVisible(fohRoutingBtn);
  fohRoutingBtn.setClickingTogglesState(true);
  fohRoutingBtn.setButtonText("FOH");
  fohRoutingBtn.setTooltip("FOH Send: Routes this channel's signal to the Front of House mix.");
  fohRoutingBtn.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::limegreen);
  fohRoutingBtn.getProperties().set("useToggleSwitch", true);
  fohRoutingBtn.getProperties().set("isOrangeToggle", true);
  fohRoutingBtn.setToggleState(slot.isFohEnabled(),
                               juce::dontSendNotification);
  fohRoutingBtn.onClick = [this]() {
    slot.setFohEnabled(fohRoutingBtn.getToggleState());
  };

  if (slot.getName() == "Monitor In") {
    fohRoutingBtn.setVisible(false);
  }

  addAndMakeVisible(iemRoutingBtn);
  iemRoutingBtn.setClickingTogglesState(true);
  iemRoutingBtn.setButtonText("IEM");
  iemRoutingBtn.setTooltip("IEM Send: Routes this channel's signal to your In-Ear Monitors mix.");
  iemRoutingBtn.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::mediumblue);
  iemRoutingBtn.getProperties().set("useToggleSwitch", true);
  iemRoutingBtn.getProperties().set("isOrangeToggle", true);
  iemRoutingBtn.setToggleState(slot.isIemEnabled(),
                               juce::dontSendNotification);
  iemRoutingBtn.onClick = [this]() {
    slot.setIemEnabled(iemRoutingBtn.getToggleState());
  };

  if (slot.getName() == "Monitor In") {
    iemRoutingBtn.setEnabled(false);
    iemRoutingBtn.setToggleState(true, juce::dontSendNotification);
  }

  addAndMakeVisible(bypassButton);
  bypassButton.setButtonText("MUTE");
  bypassButton.setTooltip("Mute Channel: Instantly cuts all audio output for this track.");
  bypassButton.setClickingTogglesState(true);
  bypassButton.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colours::red);
  bypassButton.getProperties().set("useToggleSwitch", true);
  bypassButton.getProperties().set("isOrangeToggle", false);
  bypassButton.onClick = [this] {
    slot.setBypass(bypassButton.getToggleState());
  };

  for (int i = 0; i < 3; ++i) {
    addAndMakeVisible(slotBtns[i]);
    slotBtns[i].addMouseListener(&slotBtnMouseListener, false);
    slotBtns[i].setButtonText(
        slot.getPluginName(i).isEmpty() ? "[EMPTY]" : slot.getPluginName(i));
    slotBtns[i].onClick = [this, i] {
      if (onShowPluginMenu)
        onShowPluginMenu(i);
    };

    addAndMakeVisible(editGuiBtns[i]);
    editGuiBtns[i].setButtonText("E");
    editGuiBtns[i].setColour(juce::TextButton::buttonColourId,
                             juce::Colours::red.darker(0.3f));
    editGuiBtns[i].setColour(juce::TextButton::textColourOffId,
                             juce::Colours::white);
    editGuiBtns[i].onClick = [this, i] {
      if (onOpenEditor)
        onOpenEditor(i);
    };
  }

  addAndMakeVisible(ccButton);
  ccButton.setButtonText("CC");
  ccButton.setTooltip("CC Mapping: Opens the assignment manager for mapping MIDI continuous controllers.");
  ccButton.setColour(juce::TextButton::buttonColourId,
                     juce::Colours::darkorange);
  ccButton.onClick = [this] {
    if (onShowCCDialog)
      onShowCCDialog();
  };

  addAndMakeVisible(fohLearnBtn);
  fohLearnBtn.setButtonText("L");
  fohLearnBtn.setTooltip("Learn FOH fader CC");
  fohLearnBtn.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::darkgrey);
  fohLearnBtn.onClick = [this] { armFaderLearn(true); };

  addAndMakeVisible(iemLearnBtn);
  iemLearnBtn.setButtonText("L");
  iemLearnBtn.setTooltip("Learn IEM fader CC");
  iemLearnBtn.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::darkgrey);
  iemLearnBtn.onClick = [this] { armFaderLearn(false); };

  addAndMakeVisible(noteRangeButton);
  noteRangeButton.setButtonText("NR");
  noteRangeButton.setTooltip("Note Range: Restricts this channel strip to a specific key range on your controller.");
  noteRangeButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::darkviolet);
  noteRangeButton.onClick = [this] {
    if (onShowNoteRangeDialog)
      onShowNoteRangeDialog();
  };

  addAndMakeVisible(customizeButton);
  customizeButton.setButtonText("DYN");
  customizeButton.setTooltip("Dynamics (DYN): Opens the channel strip processor (Gate, EQ, Compressor).");
  customizeButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::darkslategrey);
  customizeButton.onClick = [this] {
    if (onShowChannelStrip)
      onShowChannelStrip();
  };

  addAndMakeVisible(arpButton);
  arpButton.setButtonText("MIDI");
  arpButton.setColour(juce::TextButton::buttonColourId, juce::Colours::purple.brighter(0.2f));
  arpButton.setTooltip("MIDI FX: Opens the built-in arpeggiator controls.");
  arpButton.onClick = [this] {
    if (onShowArpeggiator)
      onShowArpeggiator();
  };

  addAndMakeVisible(samplerButton);
  samplerButton.setButtonText("SMP");
  samplerButton.setColour(juce::TextButton::buttonColourId, juce::Colours::cyan.darker(0.3f));
  samplerButton.setTooltip("Open Sampler");
  samplerButton.onClick = [this] {
    if (onShowSampler)
      onShowSampler();
  };

  addAndMakeVisible(saveStripBtn);
  saveStripBtn.setButtonText("SAVE");
  saveStripBtn.setTooltip("Save this strip to file");
  saveStripBtn.setColour(juce::TextButton::buttonColourId,
                         juce::Colours::darkgreen);
  saveStripBtn.onClick = [this] {
    if (onSaveStrip)
      onSaveStrip();
  };

  addAndMakeVisible(loadStripBtn);
  loadStripBtn.setButtonText("LOAD");
  loadStripBtn.setTooltip("Load a strip from file");
  loadStripBtn.setColour(juce::TextButton::buttonColourId,
                         juce::Colours::darkblue);
  loadStripBtn.onClick = [this] {
    if (onLoadStripFile || onLoadStrip)
      showLoadStripMenu();
  };

  addAndMakeVisible(midiLed);

  addAndMakeVisible(noteRangeLabel);
  noteRangeLabel.setJustificationType(juce::Justification::centred);
  noteRangeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
  noteRangeLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
  noteRangeLabelListener.owner = this;
  noteRangeLabel.addMouseListener(&noteRangeLabelListener, false);

  // Register right-click help listeners on control buttons
  for (auto *b : {&ccButton, &noteRangeButton, &customizeButton, &arpButton,
                  &samplerButton, &bypassButton, &fohRoutingBtn, &iemRoutingBtn,
                  &saveStripBtn, &loadStripBtn, &fohLearnBtn, &iemLearnBtn})
    b->addMouseListener(this, false);

  startTimer(50);
}

RackSlotComponent::~RackSlotComponent() {
  if (armedLearnBtn != nullptr)
    LearnBus::getInstance().disarm();
  channelSlider.setLookAndFeel(nullptr);
  iemSlider.setLookAndFeel(nullptr);
  stopTimer();
}

void RackSlotComponent::resetLearnButtonVisuals() {
  fohLearnBtn.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::darkgrey);
  iemLearnBtn.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::darkgrey);
  armedLearnBtn = nullptr;
  fohLearnBtn.repaint();
  iemLearnBtn.repaint();
}

void RackSlotComponent::armFaderLearn(bool isFoh) {
  juce::TextButton *clicked = isFoh ? &fohLearnBtn : &iemLearnBtn;

  // Clicking the already-armed button disarms (toggle off).
  if (armedLearnBtn == clicked) {
    LearnBus::getInstance().disarm();
    resetLearnButtonVisuals();
    return;
  }

  RackSlot *s = &slot;
  bool foh = isFoh;
  OpenRig::LearnTarget t;
  t.isFader = true;
  t.isFohFader = isFoh;
  t.slotIndex = slotIndex;
  t.label = isFoh ? "FOH fader" : "IEM fader";

  juce::Component::SafePointer<RackSlotComponent> safe(this);
  std::function<void(int, int)> cb = [s, foh, safe](int cc, int) {
    if (foh) {
      s->setFohCC(cc);
      s->allowCC(cc);
    } else {
      s->setIemCC(cc);
      s->allowCC(cc);
    }
    juce::MessageManager::callAsync([safe] {
      if (safe)
        safe->resetLearnButtonVisuals();
    });
  };

  LearnBus::getInstance().arm(t, cb);

  armedLearnBtn = clicked;
  fohLearnBtn.setColour(juce::TextButton::buttonColourId,
                        isFoh ? juce::Colours::red : juce::Colours::darkgrey);
  iemLearnBtn.setColour(juce::TextButton::buttonColourId,
                        isFoh ? juce::Colours::darkgrey : juce::Colours::red);
  fohLearnBtn.repaint();
  iemLearnBtn.repaint();
}

void RackSlotComponent::timerCallback() {
  float decay = 0.85f;

  float peakL = OpenRigLog::amplitudeToLogScale(slot.getLeftPeak() *
                                                slot.getChannelLevel());
  float peakR = OpenRigLog::amplitudeToLogScale(slot.getRightPeak() *
                                                slot.getChannelLevel());

  curLeft = std::max(peakL, curLeft * decay);
  curRight = std::max(peakR, curRight * decay);

  if (slot.getAndClearMidiActivity())
    midiLed.flash();

  noteRangeLabel.setText(getActiveNoteRangeString(), juce::dontSendNotification);

  bool metersChanged = (std::abs(curLeft - prevLeft) > 0.001f ||
                        std::abs(curRight - prevRight) > 0.001f);
  if (metersChanged) {
    prevLeft = curLeft;
    prevRight = curRight;
    if (cachedMeterArea.getWidth() > 0)
      repaint(cachedMeterArea);
  }

  bool stateChanged = false;
  int numBtns = isReturn ? 2 : 3;
  bool anyPlugin = false;
  for (int i = 0; i < 3; ++i) {
    if (i >= numBtns) {
      slotBtns[i].setVisible(false);
      editGuiBtns[i].setVisible(false);
      continue;
    }

    auto name = slot.getPluginName(i);
    if (i >= prevSlotNames.size()) {
      prevSlotNames.add("");
    }
    if (prevSlotNames[i] != name) {
      prevSlotNames.set(i, name);
      slotBtns[i].setButtonText(name.isEmpty() ? "Slot " + juce::String(i + 1)
                                               : name);
    }
    bool hasPlugin = slot.getPluginInstance(i) != nullptr;
    if (hasPlugin)
      anyPlugin = true;
    slotBtns[i].setColour(juce::TextButton::buttonColourId,
                          hasPlugin ? juce::Colours::green.darker(0.3f)
                                    : juce::Colours::darkgrey);
  }
  if (anyPlugin != cachedHasAnyPlugin) {
    cachedHasAnyPlugin = anyPlugin;
    stateChanged = true;
  }
  if (slot.isBypassed() != prevBypassed) {
    prevBypassed = slot.isBypassed();
    bypassButton.setToggleState(prevBypassed, juce::dontSendNotification);
    stateChanged = true;
  }
  if (slot.isFohEnabled() != prevFohEnabled) {
    prevFohEnabled = slot.isFohEnabled();
    fohRoutingBtn.setToggleState(prevFohEnabled, juce::dontSendNotification);
    stateChanged = true;
  }
  if (slot.isIemEnabled() != prevIemEnabled) {
    prevIemEnabled = slot.isIemEnabled();
    iemRoutingBtn.setToggleState(prevIemEnabled, juce::dontSendNotification);
    stateChanged = true;
  }
  if (slot.getLowNote() != prevLowNote || slot.getHighNote() != prevHighNote) {
    prevLowNote = slot.getLowNote();
    prevHighNote = slot.getHighNote();
    stateChanged = true;
  }

  if (std::abs(channelSlider.getValue() - slot.getFohLevel()) > 0.001f)
    channelSlider.setValue(slot.getFohLevel(), juce::dontSendNotification);
  if (isAccordion) {
    if (std::abs(iemOffsetKnob.getValue() - slot.getIemOffset()) > 0.001f)
      iemOffsetKnob.setValue(slot.getIemOffset(), juce::dontSendNotification);
  } else {
    if (std::abs(iemSlider.getValue() - slot.getIemLevel()) > 0.001f)
      iemSlider.setValue(slot.getIemLevel(), juce::dontSendNotification);
  }

  if (stateChanged)
    repaint();
}

void RackSlotComponent::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds().toFloat();

  auto* laf = dynamic_cast<BoutiqueLookAndFeel*>(&getLookAndFeel());
  bool useModern = (laf != nullptr && laf->useModernStyle);

  juce::Colour customColor = slot.getChannelColor();
  juce::Colour bgCol;

  bool hasCustomColor = (customColor != juce::Colour(0xff2a2a2a));

  if (hasCustomColor) {
    bgCol = customColor;
  } else if (isMonitorIn) {
    bgCol = juce::Colours::darkorange.darker(0.4f);
  } else if (isAccordion) {
    bgCol = juce::Colours::darkred.darker(0.3f);
  } else if (isReturn) {
    bgCol = juce::Colours::darkblue.darker(0.5f);
  } else {
    bgCol = (slotIndex % 2 == 0) ? juce::Colour(0xff2a2a2a)
                                 : juce::Colour(0xff333333);
  }

  if (useModern) {
    juce::Colour modernBg = (slotIndex % 2 == 0) ? juce::Colour(0xff16181b)
                                                 : juce::Colour(0xff1d2023);

    if (slot.isBypassed()) {
      modernBg = modernBg.darker(0.5f);
    }

    g.setColour(modernBg);
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(juce::Colour(0xff2a2d32));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    juce::Colour categoryCol = slot.getChannelColor();
    if (categoryCol == juce::Colour(0xff2a2a2a)) {
      if (isMonitorIn) categoryCol = juce::Colours::darkorange;
      else if (isAccordion) categoryCol = juce::Colours::darkred;
      else if (isReturn) categoryCol = juce::Colours::darkblue;
      else categoryCol = juce::Colour(0xff3e444d);
    }

    g.setColour(categoryCol);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 10.0f);

  } else {
    juce::ColourGradient grad(bgCol, 0, 0, bgCol.darker(0.3f),
                              bounds.getWidth(), 0, false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawLine(bounds.getX() + 2, bounds.getY() + 1.5f, bounds.getRight() - 2,
               bounds.getY() + 1.5f, 1.0f);
    g.drawLine(bounds.getX() + 1.5f, bounds.getY() + 2, bounds.getX() + 1.5f,
               bounds.getBottom() - 2, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.drawLine(bounds.getX() + 2, bounds.getBottom() - 1.5f,
               bounds.getRight() - 2, bounds.getBottom() - 1.5f, 1.0f);
    g.drawLine(bounds.getRight() - 1.5f, bounds.getY() + 2,
               bounds.getRight() - 1.5f, bounds.getBottom() - 2, 1.0f);

    auto drawScrew = [&](float cx, float cy) {
      g.setColour(juce::Colours::silver);
      g.fillEllipse(cx - 3, cy - 3, 6, 6);
      g.setColour(juce::Colours::darkgrey);
      g.drawEllipse(cx - 3, cy - 3, 6, 6, 1.0f);
      g.drawLine(cx - 2, cy, cx + 2, cy, 1.0f);
    };
    drawScrew(bounds.getX() + 8, bounds.getY() + 8);
    drawScrew(bounds.getRight() - 8, bounds.getY() + 8);
    drawScrew(bounds.getX() + 8, bounds.getBottom() - 8);
    drawScrew(bounds.getRight() - 8, bounds.getBottom() - 8);
  }

  if (hasCustomColor && !slot.isBypassed()) {
    auto c = customColor.withMultipliedAlpha(0.85f);
    for (int i = 3; i >= 1; --i) {
      g.setColour(c.withMultipliedAlpha(0.07f));
      g.drawRoundedRectangle(bounds.expanded((float)i), 6.0f, (float)i);
    }
    g.setColour(c);
    g.drawRoundedRectangle(bounds, 6.0f, 3.0f);

    auto fb = channelSlider.getBounds().toFloat().expanded(2.0f);
    juce::ColourGradient tg(c.withMultipliedAlpha(0.0f), fb.getCentreX(),
                            fb.getBottom(), c.withMultipliedAlpha(0.22f),
                            fb.getCentreX(), fb.getY(), false);
    g.setGradientFill(tg);
    g.fillRoundedRectangle(fb, 4.0f);
  }

  // Draw Stage-Readable Strip Name
  g.setColour(slot.isBypassed() ? juce::Colours::white.withAlpha(0.4f) : juce::Colours::white);
  g.setFont(juce::Font(13.0f, juce::Font::bold));
  g.drawText(slot.getName(), 2, 100, getWidth() - 4, 18,
             juce::Justification::centred, true);

  // Active VST slot highlight: tint populated slot boxes with track color
  if (cachedHasAnyPlugin && !slot.isBypassed()) {
    juce::Colour trackColor =
        hasCustomColor ? customColor : juce::Colour(0xff00e5ff);
    for (int i = 0; i < 3; ++i) {
      if (slot.getPluginInstance(i) != nullptr) {
        auto sb = slotBtns[i].getBounds().toFloat().expanded(1.0f);
        g.setColour(trackColor.withAlpha(0.12f));
        g.fillRoundedRectangle(sb, 3.0f);
        g.setColour(trackColor.withAlpha(0.6f));
        g.drawRoundedRectangle(sb, 3.0f, 1.0f);
      }
    }
  }

  // Return strip: fill vacant slot button area with a static label
  if (isReturn) {
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    auto vacantArea = slotBtns[2].getBounds();
    if (!vacantArea.isEmpty())
      g.drawText("RETURN", vacantArea, juce::Justification::centred, true);
  }

  int meterWidth = 12;
  int centerX = getWidth() / 2 - meterWidth / 2;
  auto faderBounds = channelSlider.getBounds();
  auto meterArea = juce::Rectangle<int>(centerX, faderBounds.getY(), meterWidth, faderBounds.getHeight());
  drawVerticalMeter(g, meterArea, curLeft, curRight, juce::Colours::green,
                    juce::Colours::yellow, juce::Colours::red);

  {
    float load = juce::jlimit(0.0f, 1.0f, slot.getCpuUsage());
    const int barH = 3;
    juce::Colour loadCol =
        (load < 0.6f) ? juce::Colours::limegreen
                      : (load < 0.85f ? juce::Colours::yellow
                                      : juce::Colours::red);
    g.setColour(loadCol.withMultipliedAlpha(0.30f));
    g.fillRect(bounds.getX(), bounds.getBottom() - (float)barH,
               bounds.getWidth(), (float)barH);
    g.setColour(loadCol);
    g.fillRect(bounds.getX(), bounds.getBottom() - (float)barH,
               bounds.getWidth() * load, (float)barH);
  }
}

void RackSlotComponent::drawVerticalMeter(juce::Graphics &g, juce::Rectangle<int> area, float L,
                                          float R, juce::Colour low, juce::Colour mid,
                                          juce::Colour high) {
  g.setColour(juce::Colours::black);
  g.fillRect(area);

  auto lRect = area.removeFromLeft(area.getWidth() / 2).reduced(1);
  auto rRect = area.reduced(1);

  auto drawSegmentedBar = [&](juce::Rectangle<int> barArea, float val) {
    const int numSegments = 12;
    const int gap = 1;
    float segmentHeight =
        (barArea.getHeight() - (numSegments - 1) * gap) / (float)numSegments;
    int activeSegments =
        (int)(numSegments * juce::jlimit(0.0f, 1.0f, val) + 0.5f);

    for (int i = 0; i < numSegments; ++i) {
      float segY =
          (float)barArea.getBottom() - (i + 1) * segmentHeight - i * gap;
      auto segRect =
          juce::Rectangle<float>((float)barArea.getX(), segY,
                                 (float)barArea.getWidth(), segmentHeight);

      juce::Colour col;
      float pos = i / (float)(numSegments - 1);
      if (pos < 0.6f)
        col = low;
      else if (pos < 0.85f)
        col = mid;
      else
        col = high;

      if (i < activeSegments) {
        g.setColour(col);
        g.fillRect(segRect);
      } else {
        g.setColour(col.withAlpha(0.15f));
        g.fillRect(segRect);
      }
    }
  };

  drawSegmentedBar(lRect, L);
  drawSegmentedBar(rRect, R);
}

void RackSlotComponent::resized() {
  auto bounds = getLocalBounds();

  const int rowHeight = 18;

  // Top area: 3 slot buttons + input selector
  auto topArea = bounds.removeFromTop(105);
  for (int i = 0; i < 3; ++i) {
    auto row = topArea.removeFromTop(20).reduced(2, 1);
    editGuiBtns[i].setBounds(
        row.removeFromRight((int)(row.getWidth() * 0.25f)));
    slotBtns[i].setBounds(row);
  }
  auto inputRow = topArea.removeFromTop(20).reduced(2, 1);
  inputSelector.setBounds(inputRow);
  // MIDI LED next to input selector (right edge, 8x8 chassis bezel)
  midiLed.setBounds(inputRow.getRight() - 10, inputRow.getY() + 6, 8, 8);

  // Bottom area: routing buttons (MUTE, FOH, IEM)
  auto buttonArea = bounds.removeFromBottom(60);
  int btnHeight = 20;
  bypassButton.setBounds(buttonArea.removeFromTop(btnHeight).reduced(2, 1));
  fohRoutingBtn.setBounds(buttonArea.removeFromTop(btnHeight).reduced(2, 1));
  iemRoutingBtn.setBounds(buttonArea.removeFromTop(btnHeight).reduced(2, 1));

  // CC / NR / DYN control row - side by side in 3 equal columns (width / 3)
  auto ctrlRow = bounds.removeFromBottom(rowHeight);
  int colWidth = ctrlRow.getWidth() / 3;
  ccButton.setBounds(ctrlRow.removeFromLeft(colWidth).reduced(1, 1));
  noteRangeButton.setBounds(ctrlRow.removeFromLeft(colWidth).reduced(1, 1));
  customizeButton.setBounds(ctrlRow.reduced(1, 1));

  // Fader area (remaining space, identical Y for instrument and return strips)
  auto faderArea = bounds.reduced(2, 0);

  // Link button + learn buttons at bottom of fader area
  auto linkBtnArea = faderArea.removeFromBottom(rowHeight);
  linkButton.setBounds(linkBtnArea.reduced(4, 0));
  {
    auto learnArea = linkBtnArea.reduced(2, 1);
    int half = learnArea.getWidth() / 2;
    fohLearnBtn.setBounds(learnArea.removeFromLeft(half));
    iemLearnBtn.setBounds(learnArea);
  }

  // Note range label below link area
  noteRangeLabel.setBounds(2, faderArea.getY(), getWidth() - 4, 14);

  // Side buttons + center fader
  int centerX = getWidth() / 2;
  int sideWidth = centerX - 14;
  int rightX = getWidth() - sideWidth - 2;
  int centerY = faderArea.getY() + faderArea.getHeight() / 2;

  arpButton.setBounds(2, centerY - 24, sideWidth, 24);
  samplerButton.setBounds(2, centerY + 4, sideWidth, 24);
  saveStripBtn.setBounds(rightX, centerY - 24, sideWidth, 24);
  loadStripBtn.setBounds(rightX, centerY + 4, sideWidth, 24);

  if (isAccordion) {
    int knobSize = 40;
    iemOffsetKnob.setBounds(centerX - knobSize / 2, faderArea.getY(),
                            knobSize, knobSize);
    faderArea.removeFromTop(knobSize + 5);

    channelSlider.setBounds(centerX - 12, faderArea.getY(), 24,
                            faderArea.getHeight());
  } else {
    channelSlider.setBounds(centerX - 12, faderArea.getY(), 24,
                            faderArea.getHeight());
    iemSlider.setVisible(false);
  }

  int meterWidth = 12;
  int meterCenterX = getWidth() / 2 - meterWidth / 2;
  auto fdrBounds = channelSlider.getBounds();
  cachedMeterArea = juce::Rectangle<int>(meterCenterX, fdrBounds.getY(),
                                          meterWidth, fdrBounds.getHeight());
}

void RackSlotComponent::setSpecialModes(bool monitorIn, bool accordion, bool returns) {
  isMonitorIn = monitorIn;
  isAccordion = accordion;
  isReturn = returns;

  linkButton.setVisible(false);
  iemSlider.setVisible(false);
  iemOffsetKnob.setVisible(isAccordion);

  customizeButton.setVisible(!isReturn && !isMonitorIn);
  arpButton.setVisible(!isReturn && !isMonitorIn);
  samplerButton.setVisible(!isReturn && !isMonitorIn);

  ccButton.setVisible(!isReturn);
  fohLearnBtn.setVisible(!isReturn);
  iemLearnBtn.setVisible(!isReturn);
  noteRangeButton.setVisible(!isReturn);
  noteRangeLabel.setVisible(!isReturn);
  saveStripBtn.setVisible(!isReturn);
  loadStripBtn.setVisible(!isReturn);

  resized();
}

juce::ComboBox &RackSlotComponent::getInputSelector() { return inputSelector; }

juce::Rectangle<int> RackSlotComponent::getNoteRangeButtonBounds() {
  return getLocalArea(this, noteRangeButton.getBounds());
}

void RackSlotComponent::mouseDoubleClick(const juce::MouseEvent &e) {
  if (e.y >= 100 && e.y <= 120) {
    showRenameDialog();
    return;
  }
  if (onOpenEditor)
    onOpenEditor(0);
}

void RackSlotComponent::mouseDown(const juce::MouseEvent &e) {
  if (e.mods.isRightButtonDown()) {
    if (e.eventComponent != nullptr && e.eventComponent != this) {
      showButtonHelpPopup(e.eventComponent);
      return;
    }
    if (e.y < 120) {
      if (onShowChannelStrip)
        onShowChannelStrip();
    }
    return;
  }
  if (e.y <= 8) {
    showColorPalette();
  }
}

void RackSlotComponent::showButtonHelpPopup(juce::Component *src) {
  juce::String title, description;

  auto match = [&](juce::Component &btn, const juce::String &t,
                   const juce::String &d) -> bool {
    if (src == &btn) {
      title = t;
      description = d;
      return true;
    }
    return false;
  };

  if (!(match(ccButton, "CC Parameter Mappings",
              "Opens the assignment manager for mapping MIDI continuous "
              "controllers to plugin parameters on this channel.") ||
        match(noteRangeButton, "Note Range Filter",
              "Restricts this channel strip to a specific key range on your "
              "controller. Notes outside the range are ignored.") ||
        match(customizeButton, "Dynamics Section (DYN)",
              "Opens the channel strip processor: Noise Gate, 3-band EQ, "
              "Compressor, and Aux Send routing for spatial mixing.") ||
        match(arpButton, "MIDI FX (Arpeggiator)",
              "Opens the built-in arpeggiator and harmonizer controls for "
              "this channel.") ||
        match(samplerButton, "Sampler (SMP)",
              "Opens the sample playback engine, allowing you to load and "
              "trigger audio samples across the keyboard.") ||
        match(bypassButton, "Mute Channel",
              "Instantly cuts all audio output for this track. Click again "
              "to restore.") ||
        match(fohRoutingBtn, "FOH Send",
              "Routes this channel's signal to the Front of House mix "
              "(audience speakers).") ||
        match(iemRoutingBtn, "IEM Send",
              "Routes this channel's signal to your In-Ear Monitor mix "
              "(personal headphones).") ||
        match(saveStripBtn, "Save Strip",
              "Saves this entire channel strip configuration to a file for "
              "reuse in other songs.") ||
        match(loadStripBtn, "Load Strip",
              "Loads a previously saved channel strip from a file or the "
              "library sidebar.") ||
        match(fohLearnBtn, "FOH Fader Learn",
              "Click to arm MIDI CC learn for the FOH fader. Send a CC "
              "message to assign it.") ||
        match(iemLearnBtn, "IEM Fader Learn",
              "Click to arm MIDI CC learn for the IEM fader. Send a CC "
              "message to assign it.")))
    return;

  juce::PopupMenu menu;
  menu.addItem(1, "What is this?");
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(src),
      [title, description](int result) {
        if (result == 1) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::InfoIcon, title, description, "Got it");
        }
      });
}

const juce::Array<juce::Colour> &RackSlotComponent::getPalette() {
  static const juce::Array<juce::Colour> p = {
      juce::Colour(0xff2a2a2a), juce::Colours::cyan, juce::Colours::limegreen,
      juce::Colours::orange,    juce::Colours::magenta,
      juce::Colours::yellow,    juce::Colours::red,
      juce::Colours::deepskyblue, juce::Colours::purple,
      juce::Colours::turquoise};
  return p;
}

void RackSlotComponent::showColorPalette() {
  juce::PopupMenu menu;
  menu.addSectionHeader("Channel Color");
  const auto &pal = getPalette();
  int id = 1;
  for (auto &c : pal) {
    juce::String name = (c == juce::Colour(0xff2a2a2a))
                            ? "Default"
                            : c.toDisplayString(true);
    menu.addItem(id++, name, true, slot.getChannelColor() == c);
  }

  juce::Component::SafePointer<RackSlotComponent> safe(this);
  menu.showMenuAsync(juce::PopupMenu::Options(),
                     [safe, pal](int result) {
                       if (safe == nullptr || result <= 0)
                         return;
                       safe->slot.setChannelColor(pal[result - 1]);
                       safe->repaint();
                     });
}

void RackSlotComponent::showRenameDialog() {
  auto *alertWindow = new juce::AlertWindow(
      "Rename Channel",
      "Enter a new name for this channel:", juce::MessageBoxIconType::NoIcon);
  alertWindow->addTextEditor("name", slot.getName(), "Name:");
  alertWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
  alertWindow->addButton("Cancel", 0,
                         juce::KeyPress(juce::KeyPress::escapeKey));

  juce::Component::SafePointer<RackSlotComponent> safe(this);
  alertWindow->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, alertWindow](int result) {
        if (safe != nullptr && result == 1) {
          auto newName = alertWindow->getTextEditorContents("name");
          safe->slot.setName(newName);
          safe->repaint();
        }
        delete alertWindow;
      }));
}

void RackSlotComponent::showLoadStripMenu() {
  auto stripsDir = OpenRigConstants::getAppDirectory().getChildFile("strips");
  juce::Array<juce::File> stripFiles;
  if (stripsDir.isDirectory())
    stripsDir.findChildFiles(stripFiles, juce::File::findFiles, false,
                             "*.orstrip");
  stripFiles.sort();

  if (stripFiles.isEmpty()) {
    if (onLoadStrip)
      onLoadStrip();
    return;
  }

  juce::PopupMenu menu;
  int id = 1;
  for (const auto &f : stripFiles)
    menu.addItem(id++, f.getFileNameWithoutExtension());
  menu.addSeparator();
  menu.addItem(999, "Browse for file...");

  auto filesCopy = stripFiles;
  juce::Component::SafePointer<RackSlotComponent> safe(this);
  menu.showMenuAsync(juce::PopupMenu::Options(),
                     [safe, filesCopy](int result) {
                       if (safe == nullptr || result == 0)
                         return;
                       if (result == 999) {
                         if (safe->onLoadStrip)
                           safe->onLoadStrip();
                         return;
                       }
                       int idx = result - 1;
                       if (idx >= 0 && idx < filesCopy.size()) {
                         if (safe->onLoadStripFile)
                           safe->onLoadStripFile(filesCopy[idx]);
                       }
                     });
}

bool RackSlotComponent::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails &details) {
  if (slot.isBypassed())
    return false;
  return details.description.toString().endsWithIgnoreCase(".orstrip");
}

void RackSlotComponent::itemDropped(
    const juce::DragAndDropTarget::SourceDetails &details) {
  juce::File presetFile(details.description.toString());
  if (presetFile.existsAsFile() && onLoadPresetFile)
    onLoadPresetFile(presetFile);
}

void RackSlotComponent::showPluginConfigMenu(int chainIndex) {
  if (chainIndex < 0 || chainIndex >= slot.getChainSize())
    return;
  if (!slot.getPluginInstance(chainIndex))
    return;

  auto *comp = new PluginStackConfigComp(slot, chainIndex);
  juce::CallOutBox::launchAsynchronously(
      std::unique_ptr<juce::Component>(comp),
      slotBtns[chainIndex].getBounds(), this);
}

void RackSlotComponent::SlotBtnMouseListener::mouseDown(const juce::MouseEvent &e) {
  if (!owner || !e.mods.isRightButtonDown())
    return;
  auto *src = e.originalComponent != nullptr ? e.originalComponent : e.eventComponent;
  for (int i = 0; i < 3; ++i) {
    for (auto *c = src; c != nullptr; c = c->getParentComponent()) {
      if (c == &owner->slotBtns[i]) {
        owner->showPluginConfigMenu(i);
        return;
      }
    }
  }
}

void RackSlotComponent::NoteRangeLabelListener::mouseUp(const juce::MouseEvent &e) {
  if (owner && e.originalComponent == &owner->noteRangeLabel &&
      !e.mods.isRightButtonDown() && owner->onShowNoteRangeDialog) {
    owner->onShowNoteRangeDialog();
  }
}

juce::String RackSlotComponent::getActiveNoteRangeString() const {
  int stripLow = slot.getLowNote();
  int stripHigh = slot.getHighNote();

  int stackLow = 127;
  int stackHigh = 0;
  bool hasInstruments = false;

  for (int i = 0; i < 3; ++i) {
    if (auto* p = slot.getPluginInstance(i)) {
      if (p->getPluginDescription().isInstrument && slot.getChainSlotSettings(i).enabled.load()) {
        stackLow = juce::jmin(stackLow, slot.getChainSlotSettings(i).lowNote.load());
        stackHigh = juce::jmax(stackHigh, slot.getChainSlotSettings(i).highNote.load());
        hasInstruments = true;
      }
    }
  }

  // Intersect strip's global range with combined stack range
  int activeLow = stripLow;
  int activeHigh = stripHigh;

  if (hasInstruments) {
    activeLow = juce::jmax(stripLow, stackLow);
    activeHigh = juce::jmin(stripHigh, stackHigh);
  }

  if (activeLow == 0 && activeHigh == 127)
    return "C-2 - G8 (Full)";

  return midiNoteName(activeLow) + " - " + midiNoteName(activeHigh);
}
