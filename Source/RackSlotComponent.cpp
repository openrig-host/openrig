#include "RackSlotComponent.h"
#include "ThemeManager.h"
#include "Logger.h"
#include "IMidiNoteLearner.h"
#include "MainComponent.h"

namespace {
juce::String midiNoteName(double v) {
  static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  int n = juce::jlimit(0, 127, (int)v);
  return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

bool isBlackKey(int note) {
  int n = note % 12;
  return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

// Per-instrument layer config: enable, level, and key range. Bound live to the
// slot's ChainSlotSettings. Implements IMidiNoteLearner so MainComponent can
// route incoming MIDI notes to it for range learning.
class PluginStackConfigComp : public juce::Component, public juce::Timer, public IMidiNoteLearner {
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

    lowNote = cs.lowNote.load();
    highNote = cs.highNote.load();

    // Start in learn mode automatically
    learning = true;
    learnedLow = -1;
    learnedHigh = -1;
    startTimerHz(20);

    // Done button
    doneBtn.setButtonText("DONE");
    doneBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::ok));
    doneBtn.onClick = [this] {
      if (auto* co = findParentComponentOfClass<juce::CallOutBox>())
        co->dismiss();
    };
    addAndMakeVisible(doneBtn);

    // Reset button
    resetBtn.setButtonText("RESET");
    resetBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::danger));
    resetBtn.onClick = [this] {
      learnedLow = -1;
      learnedHigh = -1;
      lowNote = 0;
      highNote = 127;
      auto &cs2 = slot.getChainSlotSettings(idx);
      cs2.lowNote.store(0);
      cs2.highNote.store(127);
      repaint();
    };
    addAndMakeVisible(resetBtn);

    setSize(380, 250);
  }

  ~PluginStackConfigComp() override { stopTimer(); }

  void resized() override {
    auto b = getLocalBounds().reduced(8);
    enableBtn.setBounds(b.removeFromTop(24));
    b.removeFromTop(4);
    levelLabel.setBounds(b.removeFromTop(16));
    levelSlider.setBounds(b.removeFromTop(56));
    b.removeFromTop(4);

    auto btnRow = b.removeFromBottom(28);
    int bw = btnRow.getWidth() / 2;
    doneBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
    resetBtn.setBounds(btnRow.reduced(2));

    b.removeFromBottom(4);
    // Remaining area is the keyboard + range overlay
    keyboardArea = b;
  }

  void paint(juce::Graphics &g) override {
    // Title: learn mode indicator
    auto titleArea = keyboardArea.withHeight(18).translated(0, -2);
    g.setColour(ThemeManager::get(Theme::Role::foh));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("PLAY NOTES TO SET RANGE", titleArea, juce::Justification::centred);

    // Range text
    g.setColour(ThemeManager::get(Theme::Role::text));
    g.setFont(12.0f);
    g.drawText("Low: " + midiNoteName(lowNote), keyboardArea.getX(), keyboardArea.getY() - 2,
               80, 16, juce::Justification::left);
    g.drawText("High: " + midiNoteName(highNote), keyboardArea.getRight() - 80, keyboardArea.getY() - 2,
               80, 16, juce::Justification::right);

    // Draw keyboard
    auto kbArea = keyboardArea.toFloat().withTrimmedTop(16.0f);
    drawKeyboard(g, kbArea);
    drawRangeOverlay(g, kbArea);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    auto kbArea = keyboardArea.toFloat().withTrimmedTop(16.0f);
    if (kbArea.contains(e.position)) {
      learnedLow = -1;
      learnedHigh = -1;
      updateNoteFromMouse(e.position, kbArea);
    }
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    auto kbArea = keyboardArea.toFloat().withTrimmedTop(16.0f);
    if (kbArea.contains(e.position)) {
      updateNoteFromMouse(e.position, kbArea);
    }
  }

  // --- IMidiNoteLearner implementation ---
  void handleMidiNote(int noteNumber) override {
    if (!learning)
      return;

    if (learnedLow < 0) {
      learnedLow = noteNumber;
      learnedHigh = noteNumber;
    } else {
      learnedLow = std::min(learnedLow, noteNumber);
      learnedHigh = std::max(learnedHigh, noteNumber);
    }

    lowNote = learnedLow;
    highNote = learnedHigh;
    auto &cs = slot.getChainSlotSettings(idx);
    cs.lowNote.store(lowNote);
    cs.highNote.store(highNote);
    repaint();
  }

  bool isLearning() const override { return learning; }

private:
  RackSlot &slot;
  int idx;
  int lowNote = 0;
  int highNote = 127;
  bool learning = true;
  int learnedLow = -1;
  int learnedHigh = -1;

  juce::ToggleButton enableBtn;
  juce::Slider levelSlider;
  juce::Label levelLabel;
  juce::TextButton doneBtn, resetBtn;
  juce::Rectangle<int> keyboardArea;

  void timerCallback() override { repaint(); }

  void drawKeyboard(juce::Graphics &g, juce::Rectangle<float> area) {
    float noteWidth = area.getWidth() / 128.0f;
    for (int i = 0; i < 128; ++i) {
      if (!isBlackKey(i)) {
        juce::Rectangle<float> noteRect(area.getX() + i * noteWidth, area.getY(),
                                        noteWidth, area.getHeight());
        g.setColour(juce::Colours::white);
        g.fillRect(noteRect.reduced(0.3f));
        if (i % 12 == 0) {
          g.setColour(juce::Colours::black.withAlpha(0.5f));
          g.setFont(8.0f);
          g.drawText("C" + juce::String(i / 12 - 1), noteRect,
                     juce::Justification::centredBottom);
        }
      }
    }
    for (int i = 0; i < 128; ++i) {
      if (isBlackKey(i)) {
        juce::Rectangle<float> noteRect(area.getX() + i * noteWidth,
                                        area.getY(), noteWidth,
                                        area.getHeight() * 0.6f);
        g.setColour(juce::Colours::black);
        g.fillRect(noteRect);
      }
    }
  }

  void drawRangeOverlay(juce::Graphics &g, juce::Rectangle<float> area) {
    float noteWidth = area.getWidth() / 128.0f;
    float x1 = area.getX() + lowNote * noteWidth;
    float x2 = area.getX() + (highNote + 1) * noteWidth;

    g.setColour(ThemeManager::get(Theme::Role::background).withAlpha(0.6f));
    g.fillRect(area.withWidth(x1 - area.getX()));
    g.fillRect(area.withX(x2).withWidth(area.getRight() - x2));

    g.setColour(ThemeManager::get(Theme::Role::foh).withAlpha(0.4f));
    g.fillRect(juce::Rectangle<float>(x1, area.getY(), x2 - x1, area.getHeight()));

    g.setColour(ThemeManager::get(Theme::Role::foh));
    g.drawVerticalLine((int)x1, area.getY() - 3, area.getBottom() + 3);
    g.drawVerticalLine((int)x2, area.getY() - 3, area.getBottom() + 3);
  }

  void updateNoteFromMouse(juce::Point<float> p, juce::Rectangle<float> area) {
    float normalizedX = (p.getX() - area.getX()) / area.getWidth();
    int note = juce::jlimit(0, 127, (int)(normalizedX * 128.0f));

    if (std::abs(note - lowNote) < std::abs(note - highNote)) {
      lowNote = note;
    } else {
      highNote = note;
    }

    int actualLow = std::min(lowNote, highNote);
    int actualHigh = std::max(lowNote, highNote);
    lowNote = actualLow;
    highNote = actualHigh;

    auto &cs = slot.getChainSlotSettings(idx);
    cs.lowNote.store(actualLow);
    cs.highNote.store(actualHigh);
    repaint();
  }
};
} // namespace

RackSlotComponent::RackSlotComponent(RackSlot &s, int index, juce::LookAndFeel &laf)
    : slot(s), slotIndex(index) {
  slotBtnMouseListener.owner = this;
  longPressListener.owner = this;
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

  addAndMakeVisible(outputSelector);
  outputSelector.setTooltip("Select output target (Main FOH/IEM or Subgroup)");
  outputSelector.onChange = [this]() {
    int selId = outputSelector.getSelectedId();
    int target = selId - 2;
    slot.setOutputTarget(target);
    if (onRename) {
      onRename();
    }
  };

  addAndMakeVisible(fohRoutingBtn);
  fohRoutingBtn.setClickingTogglesState(true);
  fohRoutingBtn.setButtonText("FOH");
  fohRoutingBtn.setTooltip("FOH Send: Routes this channel's signal to the Front of House mix. Hold to learn a FOH fader CC.");
  fohRoutingBtn.setColour(juce::TextButton::buttonOnColourId,
                          ThemeManager::get(Theme::Role::foh));
  fohRoutingBtn.getProperties().set("useToggleSwitch", true);
  fohRoutingBtn.getProperties().set("isOrangeToggle", true);
  fohRoutingBtn.setToggleState(slot.isFohEnabled(),
                               juce::dontSendNotification);
  fohRoutingBtn.onClick = [this]() {
    if (suppressLongPressClick) {
      suppressLongPressClick = false;
      fohRoutingBtn.setToggleState(slot.isFohEnabled(),
                                   juce::dontSendNotification);
      return;
    }
    slot.setFohEnabled(fohRoutingBtn.getToggleState());
  };
  fohRoutingBtn.addMouseListener(&longPressListener, false);

  if (slot.getName() == "Monitor In") {
    fohRoutingBtn.setVisible(false);
  }

  addAndMakeVisible(iemRoutingBtn);
  iemRoutingBtn.setClickingTogglesState(true);
  iemRoutingBtn.setButtonText("IEM");
  iemRoutingBtn.setTooltip("IEM Send: Routes this channel's signal to your In-Ear Monitors mix. Hold to learn an IEM fader CC.");
  iemRoutingBtn.setColour(juce::TextButton::buttonOnColourId,
                          ThemeManager::get(Theme::Role::iem));
  iemRoutingBtn.getProperties().set("useToggleSwitch", true);
  iemRoutingBtn.getProperties().set("isOrangeToggle", true);
  iemRoutingBtn.setToggleState(slot.isIemEnabled(),
                               juce::dontSendNotification);
  iemRoutingBtn.onClick = [this]() {
    if (suppressLongPressClick) {
      suppressLongPressClick = false;
      iemRoutingBtn.setToggleState(slot.isIemEnabled(),
                                   juce::dontSendNotification);
      return;
    }
    slot.setIemEnabled(iemRoutingBtn.getToggleState());
  };
  iemRoutingBtn.addMouseListener(&longPressListener, false);

  if (slot.getName() == "Monitor In") {
    iemRoutingBtn.setEnabled(false);
    iemRoutingBtn.setToggleState(true, juce::dontSendNotification);
  }

  addAndMakeVisible(bypassButton);
  bypassButton.setButtonText("MUTE");
  bypassButton.setTooltip("Mute Channel: Instantly cuts all audio output for this track. Hold to learn a mute CC.");
  bypassButton.setClickingTogglesState(true);
  bypassButton.setColour(juce::TextButton::buttonOnColourId,
                         ThemeManager::get(Theme::Role::danger));
  bypassButton.getProperties().set("useToggleSwitch", true);
  bypassButton.getProperties().set("isOrangeToggle", false);
  bypassButton.onClick = [this] {
    if (suppressLongPressClick) {
      suppressLongPressClick = false;
      bypassButton.setToggleState(slot.isBypassed(), juce::dontSendNotification);
      return;
    }
    bool m = bypassButton.getToggleState();
    slot.setBypass(m);
    if (onBypassChanged)
      onBypassChanged(m);
  };
  bypassButton.addMouseListener(&longPressListener, false);

  addChildComponent(expandBtn);
  expandBtn.setButtonText("+");
  expandBtn.setTooltip("Click to Expand Channel Strip");
  expandBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::accent));
  expandBtn.setColour(juce::TextButton::textColourOffId, ThemeManager::get(Theme::Role::textOnAccent));
  expandBtn.onClick = [this] { setCollapsed(false); };

  addChildComponent(collapsedMuteBtn);
  collapsedMuteBtn.setButtonText("M");
  collapsedMuteBtn.setTooltip("Muted: Click to Unmute and Expand. Hold to learn a mute CC.");
  collapsedMuteBtn.setClickingTogglesState(true);
  collapsedMuteBtn.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::danger));
  collapsedMuteBtn.onClick = [this] {
    if (suppressLongPressClick) {
      suppressLongPressClick = false;
      collapsedMuteBtn.setToggleState(slot.isBypassed(), juce::dontSendNotification);
      return;
    }
    bool m = collapsedMuteBtn.getToggleState();
    slot.setBypass(m);
    bypassButton.setToggleState(m, juce::dontSendNotification);
    if (!m) setCollapsed(false);
    if (onBypassChanged) onBypassChanged(m);
  };
  collapsedMuteBtn.addMouseListener(&longPressListener, false);

  addAndMakeVisible(collapseBtn);
  collapseBtn.setButtonText("-");
  collapseBtn.setTooltip("Collapse Channel Strip");
  collapseBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel).darker(0.35f));
  collapseBtn.setColour(juce::TextButton::textColourOffId, ThemeManager::get(Theme::Role::accent));
  collapseBtn.onClick = [this] { setCollapsed(true); };

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
                             ThemeManager::get(Theme::Role::danger).darker(0.3f));
    editGuiBtns[i].setColour(juce::TextButton::textColourOffId,
                             ThemeManager::get(Theme::Role::text));
    editGuiBtns[i].onClick = [this, i] {
      if (slot.isChainSlotMidiOut(i)) {
        if (onEditMidiOut)
          onEditMidiOut(i);
      } else if (onOpenEditor) {
        onOpenEditor(i);
      }
    };
  }

  addAndMakeVisible(ccButton);
  ccButton.setButtonText("CC");
  ccButton.setTooltip("CC Mapping: Opens the assignment manager for mapping MIDI continuous controllers.");
  ccButton.setColour(juce::TextButton::buttonColourId,
                     ThemeManager::get(Theme::Role::warn));
  ccButton.onClick = [this] {
    if (onShowCCDialog)
      onShowCCDialog(0);
  };

  addAndMakeVisible(noteRangeButton);
  noteRangeButton.setButtonText("NR");
  noteRangeButton.setTooltip("Note Range: Restricts this channel strip to a specific key range on your controller.");
  noteRangeButton.setColour(juce::TextButton::buttonColourId,
                            ThemeManager::get(Theme::Role::midiPC));
  noteRangeButton.onClick = [this] {
    if (onShowNoteRangeDialog)
      onShowNoteRangeDialog();
  };

  addAndMakeVisible(customizeButton);
  customizeButton.setButtonText("DYN");
  customizeButton.setTooltip("Dynamics (DYN): Opens the channel strip processor (Gate, EQ, Compressor).");
  customizeButton.setColour(juce::TextButton::buttonColourId,
                            ThemeManager::get(Theme::Role::raised));
  customizeButton.onClick = [this] {
    if (onShowChannelStrip)
      onShowChannelStrip();
  };

  addAndMakeVisible(arpButton);
  arpButton.setButtonText("MIDI");
  arpButton.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::midiPC).brighter(0.2f));
  arpButton.setTooltip("MIDI FX: Opens the built-in arpeggiator controls.");
  arpButton.onClick = [this] {
    if (onShowArpeggiator)
      onShowArpeggiator();
  };

  addAndMakeVisible(samplerButton);
  samplerButton.setButtonText("SMP");
  samplerButton.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::iem).darker(0.3f));
  samplerButton.setTooltip("Open Sampler");
  samplerButton.onClick = [this] {
    if (onShowSampler)
      onShowSampler();
  };

  addAndMakeVisible(saveStripBtn);
  saveStripBtn.setButtonText("SAVE");
  saveStripBtn.setTooltip("Save this strip to file");
  saveStripBtn.setColour(juce::TextButton::buttonColourId,
                         ThemeManager::get(Theme::Role::ok));
  saveStripBtn.onClick = [this] {
    if (onSaveStrip)
      onSaveStrip();
  };

  addAndMakeVisible(loadStripBtn);
  loadStripBtn.setButtonText("LOAD");
  loadStripBtn.setTooltip("Load a strip from file");
  loadStripBtn.setColour(juce::TextButton::buttonColourId,
                         ThemeManager::get(Theme::Role::iem));
  loadStripBtn.onClick = [this] {
    if (onLoadStripFile || onLoadStrip)
      showLoadStripMenu();
  };

  addAndMakeVisible(midiLed);

  addAndMakeVisible(noteRangeLabel);
  noteRangeLabel.setJustificationType(juce::Justification::centred);
  noteRangeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
  noteRangeLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::warn));
  noteRangeLabelListener.owner = this;
  noteRangeLabel.addMouseListener(&noteRangeLabelListener, false);

  // Register right-click help listeners on control buttons
  for (auto *b : {&ccButton, &noteRangeButton, &customizeButton, &arpButton,
                  &samplerButton, &bypassButton, &fohRoutingBtn, &iemRoutingBtn,
                  &saveStripBtn, &loadStripBtn})
    b->addMouseListener(this, false);

  startTimer(50);
}

RackSlotComponent::~RackSlotComponent() {
  channelSlider.setLookAndFeel(nullptr);
  iemSlider.setLookAndFeel(nullptr);
  stopTimer();
}

void RackSlotComponent::timerCallback() {
  float decay = 0.85f;

  float peakL = FanfareLog::amplitudeToLogScale(slot.getLeftPeak() *
                                                slot.getChannelLevel());
  float peakR = FanfareLog::amplitudeToLogScale(slot.getRightPeak() *
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
    bool isMidiOut = slot.isChainSlotMidiOut(i);
    if (isMidiOut)
      name = "\xe2\x86\x92 " + slot.getChainSlotMidiOutName(i) +
             " ch" + juce::String(slot.getChainSlotMidiOutChannel(i));
    if (i >= prevSlotNames.size()) {
      prevSlotNames.add("");
    }
    if (prevSlotNames[i] != name) {
      prevSlotNames.set(i, name);
      slotBtns[i].setButtonText(name.isEmpty() ? "Slot " + juce::String(i + 1)
                                               : name);
    }
    bool hasPlugin = slot.getPluginInstance(i) != nullptr || isMidiOut;
    if (hasPlugin)
      anyPlugin = true;
    editGuiBtns[i].setButtonText(isMidiOut ? "M" : "E");
    slotBtns[i].getProperties().set("isPluginSlot", true);
    slotBtns[i].getProperties().set("hasPlugin", hasPlugin);
    slotBtns[i].setColour(juce::TextButton::buttonColourId,
                          isMidiOut ? ThemeManager::get(Theme::Role::midiPC).darker(0.3f)
                                    : (hasPlugin ? ThemeManager::get(Theme::Role::ok).darker(0.35f)
                                                 : ThemeManager::get(Theme::Role::panel)));
    slotBtns[i].setColour(juce::TextButton::textColourOffId,
                          hasPlugin ? ThemeManager::get(Theme::Role::text)
                                    : ThemeManager::get(Theme::Role::textDim));
  }
  if (anyPlugin != cachedHasAnyPlugin) {
    cachedHasAnyPlugin = anyPlugin;
    stateChanged = true;
  }
  if (slot.isBypassed() != prevBypassed) {
    prevBypassed = slot.isBypassed();
    bypassButton.setToggleState(prevBypassed, juce::dontSendNotification);
    collapsedMuteBtn.setToggleState(prevBypassed, juce::dontSendNotification);
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

void RackSlotComponent::setCollapsed(bool shouldCollapse) {
  if (isCollapsed != shouldCollapse) {
    isCollapsed = shouldCollapse;
    resized();
    repaint();
    if (onCollapseChanged)
      onCollapseChanged(isCollapsed);
  }
}

void RackSlotComponent::paint(juce::Graphics &g) {
  juce::Colour categoryCol = slot.getChannelColor();
  if (categoryCol == juce::Colour(0xff2a2a2a)) {
    if (isMonitorIn) categoryCol = ThemeManager::get(Theme::Role::catMonitor);
    else if (isAccordion) categoryCol = ThemeManager::get(Theme::Role::catAccordion);
    else if (isReturn) categoryCol = ThemeManager::get(Theme::Role::catReturn);
    else if (slotIndex == 1 || slot.getName().containsIgnoreCase("CK88") || slot.getName().containsIgnoreCase("RD88") || slot.getName().containsIgnoreCase("Keyboard"))
      categoryCol = ThemeManager::get(Theme::Role::catKeyboard);
    else categoryCol = ThemeManager::get(Theme::Role::catDefault);
  }
  bool hasDistinctColor = (categoryCol != juce::Colour(0xff2a2a2a) &&
                           categoryCol != ThemeManager::get(Theme::Role::catDefault));

  if (isCollapsed) {
    auto bounds = getLocalBounds().toFloat();

    juce::Colour bg = (slotIndex % 2 == 0) ? ThemeManager::get(Theme::Role::panel)
                                           : ThemeManager::get(Theme::Role::panelAlt);
    if (slot.isBypassed()) {
      bg = bg.darker(0.45f);
      if (hasDistinctColor)
        bg = bg.interpolatedWith(categoryCol, 0.12f);
    } else if (hasDistinctColor) {
      bg = bg.interpolatedWith(categoryCol, 0.28f);
    }

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Category indicator on left edge
    g.setColour(categoryCol);
    g.fillRect(bounds.getX(), bounds.getY() + 4.0f, 4.0f, bounds.getHeight() - 8.0f);

    g.setColour(hasDistinctColor ? categoryCol.withAlpha(slot.isBypassed() ? 0.35f : 0.70f)
                                 : ThemeManager::get(Theme::Role::border));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Vertical text in center
    auto textArea = juce::Rectangle<float>(4.0f, 32.0f, bounds.getWidth() - 8.0f, bounds.getHeight() - 64.0f);
    if (textArea.getHeight() > 30.0f) {
      juce::Graphics::ScopedSaveState sss(g);
      float cx = textArea.getCentreX();
      float cy = textArea.getCentreY();
      g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, cx, cy));

      juce::String nameText = slot.getName();
      if (nameText.isEmpty()) nameText = "Slot " + juce::String(slotIndex + 1);
      juce::String label = juce::String(slotIndex + 1) + ". " + nameText;
      if (slot.isBypassed()) label += " [MUTED]";

      g.setColour(slot.isBypassed() ? ThemeManager::get(Theme::Role::textDim) : ThemeManager::get(Theme::Role::text));
      g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
      juce::Rectangle<float> rotRect(cx - textArea.getHeight() / 2.0f, cy - textArea.getWidth() / 2.0f, textArea.getHeight(), textArea.getWidth());
      g.drawText(label, rotRect, juce::Justification::centred, true);
    }

    // Mini vertical meter on the right edge
    drawVerticalMeter(g, cachedMeterArea, curLeft, curRight,
                      ThemeManager::get(Theme::Role::meterLow),
                      ThemeManager::get(Theme::Role::meterMid),
                      ThemeManager::get(Theme::Role::meterPeak));
    return;
  }
  auto bounds = getLocalBounds().toFloat();

  auto* laf = dynamic_cast<BoutiqueLookAndFeel*>(&getLookAndFeel());
  bool useModern = (laf != nullptr && laf->useModernStyle);

  juce::Colour customColor = slot.getChannelColor();
  juce::Colour bgCol;

  bool hasCustomColor = (customColor != juce::Colour(0xff2a2a2a));

  if (hasCustomColor) {
    bgCol = customColor;
  } else if (isMonitorIn) {
    bgCol = ThemeManager::get(Theme::Role::catMonitor).darker(0.4f);
  } else if (isAccordion) {
    bgCol = ThemeManager::get(Theme::Role::catAccordion).darker(0.3f);
  } else if (isReturn) {
    bgCol = ThemeManager::get(Theme::Role::catReturn).darker(0.5f);
  } else {
    bgCol = (slotIndex % 2 == 0) ? ThemeManager::get(Theme::Role::panel)
                                 : ThemeManager::get(Theme::Role::panelAlt);
  }

  if (useModern) {
    juce::Colour modernBg = (slotIndex % 2 == 0) ? ThemeManager::get(Theme::Role::panel)
                                                 : ThemeManager::get(Theme::Role::panelAlt);

    if (slot.isBypassed()) {
      modernBg = modernBg.darker(0.5f);
      if (hasDistinctColor)
        modernBg = modernBg.interpolatedWith(categoryCol, 0.08f);
    } else if (hasDistinctColor) {
      modernBg = modernBg.interpolatedWith(categoryCol, 0.16f);
    }

    g.setColour(modernBg);
    g.fillRoundedRectangle(bounds, 6.0f);

    // Subtle atmospheric vertical gradient wash of the channel color
    if (hasDistinctColor && !slot.isBypassed()) {
      juce::ColourGradient stripWash(categoryCol.withAlpha(0.20f), bounds.getCentreX(), bounds.getY(),
                                     categoryCol.withAlpha(0.02f), bounds.getCentreX(), bounds.getBottom(), false);
      g.setGradientFill(stripWash);
      g.fillRoundedRectangle(bounds, 6.0f);
    }

    g.setColour(hasDistinctColor && !slot.isBypassed() ? categoryCol.withAlpha(0.55f) : ThemeManager::get(Theme::Role::border));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Category accent bar at the top
    g.setColour(categoryCol);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 10.0f);

  } else {
    if (hasDistinctColor && !slot.isBypassed()) {
      bgCol = bgCol.interpolatedWith(categoryCol, 0.22f);
    }
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
      g.setColour(ThemeManager::get(Theme::Role::knobThumb));
      g.fillEllipse(cx - 3, cy - 3, 6, 6);
      g.setColour(ThemeManager::get(Theme::Role::border));
      g.drawEllipse(cx - 3, cy - 3, 6, 6, 1.0f);
      g.drawLine(cx - 2, cy, cx + 2, cy, 1.0f);
    };
    drawScrew(bounds.getX() + 8, bounds.getY() + 8);
    drawScrew(bounds.getRight() - 8, bounds.getY() + 8);
    drawScrew(bounds.getX() + 8, bounds.getBottom() - 8);
    drawScrew(bounds.getRight() - 8, bounds.getBottom() - 8);
  }

  // Draw Stage-Readable Strip Nameplate (where the writing is, tinted with channel color)
  auto nameplateRect = juce::Rectangle<float>(3.0f, 98.0f, bounds.getWidth() - 6.0f, 32.0f);
  if (hasDistinctColor) {
    juce::Colour badgeCol = slot.isBypassed() ? categoryCol.darker(0.6f).withAlpha(0.35f)
                                              : categoryCol.darker(0.25f);
    juce::ColourGradient badgeGrad(badgeCol.brighter(0.12f), nameplateRect.getX(), nameplateRect.getY(),
                                   badgeCol.darker(0.18f), nameplateRect.getX(), nameplateRect.getBottom(), false);
    g.setGradientFill(badgeGrad);
    g.fillRoundedRectangle(nameplateRect, 4.0f);

    g.setColour(slot.isBypassed() ? categoryCol.withAlpha(0.25f) : categoryCol.withAlpha(0.85f));
    g.drawRoundedRectangle(nameplateRect, 4.0f, 1.0f);
  } else {
    g.setColour(ThemeManager::get(Theme::Role::panel).darker(0.3f));
    g.fillRoundedRectangle(nameplateRect, 4.0f);
    g.setColour(ThemeManager::get(Theme::Role::border));
    g.drawRoundedRectangle(nameplateRect, 4.0f, 1.0f);
  }

  // Draw Channel Name
  juce::Colour nameTextCol = slot.isBypassed() ? ThemeManager::get(Theme::Role::textDim)
                                              : (hasDistinctColor ? juce::Colours::white : ThemeManager::get(Theme::Role::text));
  g.setColour(nameTextCol);
  g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
  g.drawText(slot.getName(), (int)nameplateRect.getX() + 2, (int)nameplateRect.getY() + 1,
             (int)nameplateRect.getWidth() - 4, 16, juce::Justification::centred, true);

  // Active VST slot highlight: tint populated slot boxes with track color
  if (cachedHasAnyPlugin && !slot.isBypassed()) {
    juce::Colour trackColor =
        hasCustomColor ? customColor : ThemeManager::get(Theme::Role::accent);
    for (int i = 0; i < 3; ++i) {
      if (slot.getPluginInstance(i) != nullptr || slot.isChainSlotMidiOut(i)) {
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
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    auto vacantArea = slotBtns[2].getBounds();
    if (!vacantArea.isEmpty())
      g.drawText("RETURN", vacantArea, juce::Justification::centred, true);
  }

  int meterWidth = 12;
  int centerX = getWidth() / 2 - meterWidth / 2;
  auto faderBounds = channelSlider.getBounds();
  auto meterArea = juce::Rectangle<int>(centerX, faderBounds.getY(), meterWidth, faderBounds.getHeight());
  drawVerticalMeter(g, meterArea, curLeft, curRight, ThemeManager::get(Theme::Role::meterLow),
                    ThemeManager::get(Theme::Role::meterMid), ThemeManager::get(Theme::Role::meterPeak));

  {
    float load = juce::jlimit(0.0f, 1.0f, slot.getCpuUsage());
    const int barH = 3;
    juce::Colour loadCol =
        (load < 0.6f) ? ThemeManager::get(Theme::Role::meterLow)
                      : (load < 0.85f ? ThemeManager::get(Theme::Role::meterMid)
                                      : ThemeManager::get(Theme::Role::meterPeak));
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
  g.setColour(ThemeManager::get(Theme::Role::trackGroove));
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
  if (isCollapsed) {
    for (int i = 0; i < 3; ++i) {
      slotBtns[i].setVisible(false);
      editGuiBtns[i].setVisible(false);
    }
    inputSelector.setVisible(false);
    outputSelector.setVisible(false);
    bypassButton.setVisible(false);
    fohRoutingBtn.setVisible(false);
    iemRoutingBtn.setVisible(false);
    ccButton.setVisible(false);
    noteRangeButton.setVisible(false);
    customizeButton.setVisible(false);
    arpButton.setVisible(false);
    samplerButton.setVisible(false);
    saveStripBtn.setVisible(false);
    loadStripBtn.setVisible(false);
    linkButton.setVisible(false);
    channelSlider.setVisible(false);
    iemSlider.setVisible(false);
    iemOffsetKnob.setVisible(false);
    noteRangeLabel.setVisible(false);
    collapseBtn.setVisible(false);

    expandBtn.setVisible(true);
    expandBtn.setBounds(2, 2, getWidth() - 4, 22);

    collapsedMuteBtn.setVisible(true);
    collapsedMuteBtn.setBounds(2, getHeight() - 26, getWidth() - 4, 22);
    collapsedMuteBtn.setToggleState(slot.isBypassed(), juce::dontSendNotification);

    midiLed.setVisible(true);
    midiLed.setBounds(getWidth() / 2 - 4, 26, 8, 8);
    cachedMeterArea = juce::Rectangle<int>(getWidth() - 6, 36, 4, getHeight() - 66);
    return;
  }

  // Expanded mode:
  expandBtn.setVisible(false);
  collapsedMuteBtn.setVisible(false);

  for (int i = 0; i < (isReturn ? 2 : 3); ++i) {
    slotBtns[i].setVisible(true);
    editGuiBtns[i].setVisible(true);
  }
  inputSelector.setVisible(true);
  outputSelector.setVisible(!isReturn);
  bypassButton.setVisible(true);
  fohRoutingBtn.setVisible(!isMonitorIn);
  iemRoutingBtn.setVisible(true);
  channelSlider.setVisible(true);
  ccButton.setVisible(!isReturn);
  noteRangeButton.setVisible(!isReturn);
  noteRangeLabel.setVisible(!isReturn);
  customizeButton.setVisible(!isReturn && !isMonitorIn);
  arpButton.setVisible(!isReturn && !isMonitorIn);
  samplerButton.setVisible(!isReturn && !isMonitorIn);
  saveStripBtn.setVisible(!isReturn);
  loadStripBtn.setVisible(!isReturn);
  iemOffsetKnob.setVisible(isAccordion);
  linkButton.setVisible(false);
  iemSlider.setVisible(false);
  midiLed.setVisible(true);

  auto bounds = getLocalBounds();

  const int rowHeight = 18;

  // Top area: 3 slot buttons + input selector + output selector
  auto topArea = bounds.removeFromTop(125);
  for (int i = 0; i < 3; ++i) {
    auto row = topArea.removeFromTop(20).reduced(2, 1);
    if (i == 0) {
      auto collapseBounds = row.removeFromRight(22);
      collapseBtn.setBounds(collapseBounds);
      collapseBtn.setVisible(true);
      collapseBtn.toFront(true);
    }
    int editBtnWidth = juce::jlimit(20, 24, (int)(row.getWidth() * 0.22f));
    editGuiBtns[i].setBounds(row.removeFromRight(editBtnWidth));
    slotBtns[i].setBounds(row);
  }
  auto inputRow = topArea.removeFromTop(20).reduced(2, 1);
  inputSelector.setBounds(inputRow);
  // MIDI LED next to input selector (right edge, 8x8 chassis bezel)
  midiLed.setBounds(inputRow.getRight() - 10, inputRow.getY() + 6, 8, 8);

  auto outputRow = topArea.removeFromTop(20).reduced(2, 1);
  outputSelector.setBounds(outputRow);

  // Bottom area: routing buttons (MUTE, FOH, IEM) + Learn Buttons
  auto buttonArea = bounds.removeFromBottom(60);
  int btnHeight = 20;
  bypassButton.setBounds(buttonArea.removeFromTop(btnHeight).reduced(2, 1));

  auto fohRow = buttonArea.removeFromTop(btnHeight).reduced(2, 1);
  fohRoutingBtn.setBounds(fohRow);

  auto iemRow = buttonArea.removeFromTop(btnHeight).reduced(2, 1);
  iemRoutingBtn.setBounds(iemRow);

  // CC / NR / DYN control row - side by side in 3 equal columns (width / 3)
  auto ctrlRow = bounds.removeFromBottom(rowHeight);
  int colWidth = ctrlRow.getWidth() / 3;
  ccButton.setBounds(ctrlRow.removeFromLeft(colWidth).reduced(1, 1));
  noteRangeButton.setBounds(ctrlRow.removeFromLeft(colWidth).reduced(1, 1));
  customizeButton.setBounds(ctrlRow.reduced(1, 1));

  // Fader area (remaining space, identical Y for instrument and return strips)
  auto faderArea = bounds.reduced(2, 0);

  // Position the note range label explicitly below the strip name (y = 100)
  noteRangeLabel.setBounds(2, 116, getWidth() - 4, 14);

  // Shift the start of the faderArea down to y = 132 to clear the label
  if (faderArea.getY() < 132) {
    int delta = 132 - faderArea.getY();
    faderArea.removeFromTop(delta);
  }

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

  if (isCollapsed)
    return;

  linkButton.setVisible(false);
  iemSlider.setVisible(false);
  iemOffsetKnob.setVisible(isAccordion);

  customizeButton.setVisible(!isReturn && !isMonitorIn);
  arpButton.setVisible(!isReturn && !isMonitorIn);
  samplerButton.setVisible(!isReturn && !isMonitorIn);

  ccButton.setVisible(!isReturn);
  noteRangeButton.setVisible(!isReturn);
  noteRangeLabel.setVisible(!isReturn);
  saveStripBtn.setVisible(!isReturn);
  loadStripBtn.setVisible(!isReturn);
  outputSelector.setVisible(!isReturn);

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

void RackSlotComponent::mouseMove(const juce::MouseEvent &e) {
  if (!isCollapsed && e.x >= getWidth() - 6) {
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
  } else {
    setMouseCursor(juce::MouseCursor::NormalCursor);
  }
}

void RackSlotComponent::mouseDown(const juce::MouseEvent &e) {
  if (isCollapsed) {
    if (!e.mods.isRightButtonDown()) {
      setCollapsed(false);
      return;
    }
  }

  // Right-edge grab handle to resize expanded channel strip width
  if (!isCollapsed && e.x >= getWidth() - 6 && !e.mods.isRightButtonDown()) {
    isDraggingRightEdge = true;
    if (onStartDragWidth)
      onStartDragWidth();
    return;
  }

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
  if (e.eventComponent == this && e.y <= 8) {
    showColorPalette();
  }
}

void RackSlotComponent::mouseDrag(const juce::MouseEvent &e) {
  if (isDraggingRightEdge) {
    if (onDragWidth)
      onDragWidth(e.getDistanceFromDragStartX());
  }
}

void RackSlotComponent::mouseUp(const juce::MouseEvent &) {
  if (isDraggingRightEdge) {
    isDraggingRightEdge = false;
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
              "to restore. Hold to learn a mute CC.") ||
        match(fohRoutingBtn, "FOH Send",
              "Routes this channel's signal to the Front of House mix "
              "(audience speakers). Hold to learn a FOH fader CC.") ||
        match(iemRoutingBtn, "IEM Send",
              "Routes this channel's signal to your In-Ear Monitor mix "
              "(personal headphones). Hold to learn an IEM fader CC.") ||
        match(saveStripBtn, "Save Strip",
              "Saves this entire channel strip configuration to a file for "
              "reuse in other songs.") ||
        match(loadStripBtn, "Load Strip",
              "Loads a previously saved channel strip from a file or the "
              "library sidebar.")))
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
          if (safe->onRename) {
            safe->onRename();
          }
          safe->repaint();
        }
        delete alertWindow;
      }));
}

void RackSlotComponent::showLoadStripMenu() {
  auto stripsDir = FanfareConstants::getAppDirectory().getChildFile("strips");
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
  // Allow the per-layer config (note range/level/enable) for both plugin and
  // MIDI OUT chain slots.
  if (!slot.getPluginInstance(chainIndex) && !slot.isChainSlotMidiOut(chainIndex))
    return;

  auto *comp = new PluginStackConfigComp(slot, chainIndex);
  
  if (auto* mainComp = findParentComponentOfClass<MainComponent>()) {
    mainComp->setActiveMidiNoteLearner(comp);
  }

  juce::CallOutBox::launchAsynchronously(
      std::unique_ptr<juce::Component>(comp),
      slotBtns[chainIndex].getScreenBounds(), nullptr);
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

void RackSlotComponent::LongPressMouseListener::mouseDown(const juce::MouseEvent &e) {
  if (!owner)
    return;
  fired = false;
  holdMs = 0;
  owner->suppressLongPressClick = false;
  if (e.eventComponent == &owner->bypassButton ||
      e.eventComponent == &owner->collapsedMuteBtn)
    armTarget = 3; // mute
  else if (e.eventComponent == &owner->fohRoutingBtn)
    armTarget = 1; // FOH fader
  else
    armTarget = 2; // IEM fader
  startTimer(50);
}

void RackSlotComponent::LongPressMouseListener::mouseDrag(const juce::MouseEvent &e) {
  (void)e;
  stopTimer(); // moved — not a hold, cancel the long press
}

void RackSlotComponent::LongPressMouseListener::mouseUp(const juce::MouseEvent &e) {
  (void)e;
  stopTimer();
}

void RackSlotComponent::LongPressMouseListener::timerCallback() {
  holdMs += 50;
  if (holdMs >= 600 && !fired) {
    fired = true;
    stopTimer();
    owner->longPressLearnArmed(armTarget);
  }
}

void RackSlotComponent::longPressLearnArmed(int armTarget) {
  suppressLongPressClick = true; // the release must not toggle the button
  if (onShowCCDialog)
    onShowCCDialog(armTarget);
}

juce::String RackSlotComponent::getActiveNoteRangeString() const {
  int stripLow = slot.getLowNote();
  int stripHigh = slot.getHighNote();

  int stackLow = 127;
  int stackHigh = 0;
  bool hasInstruments = false;

  for (int i = 0; i < 3; ++i) {
    bool isInst = slot.isChainSlotMidiOut(i);
    if (auto* p = slot.getPluginInstance(i))
      isInst = isInst || p->getPluginDescription().isInstrument;
    if (isInst && slot.getChainSlotSettings(i).enabled.load()) {
      stackLow = juce::jmin(stackLow, slot.getChainSlotSettings(i).lowNote.load());
      stackHigh = juce::jmax(stackHigh, slot.getChainSlotSettings(i).highNote.load());
      hasInstruments = true;
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

void RackSlotComponent::updateOutputSelector() {
  outputSelector.clear(juce::dontSendNotification);
  outputSelector.addItem("Main FOH/IEM", 1);

  int numSlots = FanfareConstants::kNumSlots;
  if (slotIndex >= 0 && slotIndex < numSlots) {
    for (int i = slotIndex + 1; i < numSlots; ++i) {
      juce::String name = "Slot " + juce::String(i + 1);
      if (getSlotName) {
        juce::String customName = getSlotName(i);
        if (customName.isNotEmpty()) {
          name += ": " + customName;
        }
      }
      outputSelector.addItem(name, i + 2);
    }
  }

  int target = slot.getOutputTarget();
  outputSelector.setSelectedId(target + 2, juce::dontSendNotification);
}
