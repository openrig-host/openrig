#pragma once

#include "AssetLoader.h"
#include "ChannelStripProcessor.h"
#include "LEDIndicator.h"
#include "MidiLearnBus.h"
#include "FanfareConstants.h"
#include "RackSlot.h"
#include "BoutiqueLookAndFeel.h"
#include <JuceHeader.h>
#include <functional>

class RackSlotComponent : public juce::Component,
                          public juce::Timer,
                          public juce::DragAndDropTarget {
public:
  RackSlotComponent(RackSlot &s, int index, juce::LookAndFeel &laf);
  ~RackSlotComponent() override;

  void timerCallback() override;
  void paint(juce::Graphics &g) override;
  void drawVerticalMeter(juce::Graphics &g, juce::Rectangle<int> area, float L,
                         float R, juce::Colour low, juce::Colour mid,
                         juce::Colour high);
  void resized() override;

  juce::ComboBox &getInputSelector();
  void setSpecialModes(bool monitorIn, bool accordion, bool returns);
  juce::Rectangle<int> getNoteRangeButtonBounds();

  std::function<void(int)> onShowPluginMenu;
  std::function<void(int)> onOpenEditor;
  std::function<void(int)> onEditMidiOut; // edit a chain slot's MIDI OUT destination
  std::function<void(int)> onShowCCDialog; // 0 = none, 1 = FOH, 2 = IEM, 3 = mute learn to arm
  std::function<void()> onShowNoteRangeDialog;
  std::function<void()> onShowChannelStrip;
  std::function<void()> onShowArpeggiator;
  std::function<void()> onShowSampler;
  std::function<void()> onSaveStrip;
  std::function<void()> onLoadStrip;
  std::function<void(const juce::File&)> onLoadStripFile;
  std::function<void(const juce::File&)> onLoadPresetFile;
  std::function<juce::String(int)> getSlotName;
  std::function<void()> onRename;
  void updateOutputSelector();

  bool isCollapsed = false;
  void setCollapsed(bool shouldCollapse);
  bool getIsCollapsed() const { return isCollapsed; }
  std::function<void(bool)> onCollapseChanged;
  std::function<void(bool)> onBypassChanged;
  std::function<void(int deltaX)> onDragWidth;
  std::function<void()> onStartDragWidth;
  bool isDraggingRightEdge = false;

  void mouseDoubleClick(const juce::MouseEvent &e) override;
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;
  void mouseMove(const juce::MouseEvent &e) override;
  void showButtonHelpPopup(juce::Component *src);

  static const juce::Array<juce::Colour> &getPalette();
  void showColorPalette();
  void showRenameDialog();
  void showLoadStripMenu();
  void showPluginConfigMenu(int chainIndex);
  juce::String getActiveNoteRangeString() const;

  bool isInterestedInDragSource(
      const juce::DragAndDropTarget::SourceDetails &details) override;
  void itemDropped(
      const juce::DragAndDropTarget::SourceDetails &details) override;

private:
  // Captures right-clicks on the three slot buttons to open the layer config.
  struct SlotBtnMouseListener : public juce::MouseListener {
    RackSlotComponent *owner = nullptr;
    void mouseDown(const juce::MouseEvent &e) override;
  } slotBtnMouseListener;

  // Routes clicks on the note-range Label to onShowNoteRangeDialog.
  struct NoteRangeLabelListener : public juce::MouseListener {
    RackSlotComponent *owner = nullptr;
    void mouseUp(const juce::MouseEvent &e) override;
  } noteRangeLabelListener;

  // Long-press (600ms) on MUTE / FOH / IEM opens the CC assignment manager
  // with the matching learn armed; the release click is then suppressed.
  struct LongPressMouseListener : public juce::MouseListener,
                                  public juce::Timer {
    RackSlotComponent *owner = nullptr;
    int holdMs = 0;
    bool fired = false;
    int armTarget = 0; // 1 = FOH, 2 = IEM, 3 = mute
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void timerCallback() override;
  } longPressListener;
  bool suppressLongPressClick = false;
  void longPressLearnArmed(int armTarget);

  RackSlot &slot;
  juce::TextButton slotBtns[3];
  juce::TextButton editGuiBtns[3];
  juce::TextButton bypassButton;
  juce::TextButton ccButton;
  juce::TextButton noteRangeButton;
  juce::TextButton customizeButton;
  juce::TextButton arpButton;
  juce::TextButton samplerButton;
  juce::TextButton saveStripBtn;
  juce::TextButton loadStripBtn;
  juce::TextButton expandBtn{"+"};
  juce::TextButton collapseBtn{"-"};
  juce::TextButton collapsedMuteBtn{"M"};

  juce::TextButton fohRoutingBtn;
  juce::TextButton iemRoutingBtn;
  juce::TextButton linkButton;

  juce::Slider channelSlider;
  juce::Slider iemSlider;
  juce::Slider iemOffsetKnob;
  juce::ComboBox inputSelector;
  juce::ComboBox outputSelector;
  juce::Label noteRangeLabel;

  LEDIndicator midiLed{ThemeManager::get(Theme::Role::midiNote)};

  float curLeft = 0.0f;
  float curRight = 0.0f;
  int slotIndex;
  bool isMonitorIn = false;
  bool isAccordion = false;
  bool isReturn = false;

  juce::Rectangle<int> cachedMeterArea;
  float prevLeft = -1.0f;
  float prevRight = -1.0f;
  juce::StringArray prevSlotNames;
  bool prevBypassed = false;
  bool prevFohEnabled = false;
  bool prevIemEnabled = false;
  int prevLowNote = -1;
  int prevHighNote = -1;
  bool cachedHasAnyPlugin = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackSlotComponent)
};
