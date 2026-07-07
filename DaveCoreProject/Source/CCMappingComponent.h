#pragma once

#include "MidiLearnBus.h"
#include "RackSlot.h"
#include <JuceHeader.h>

/**
    Detects parameter value changes on a plugin. Used by Quick Learn to
    auto-bind when the user wiggles a plugin knob.
*/
class ParamChangeDetector : public juce::AudioProcessorListener {
public:
  std::function<void(juce::AudioProcessor *, int)> onParameterChanged;

  void audioProcessorParameterChanged(juce::AudioProcessor *processor,
                                      int parameterIndex, float) override {
    juce::MessageManager::callAsync([this, processor, parameterIndex] {
      if (onParameterChanged)
        onParameterChanged(processor, parameterIndex);
    });
  }

  void audioProcessorChanged(juce::AudioProcessor *,
                             const juce::AudioProcessorListener::ChangeDetails &) override {}
};

/**
    CC Assignment Manager.
    Manual CC assignment PLUS arm-then-wiggle Learn (mirrors NoteRangeComponent),
    for both plugin parameters and slot FOH/IEM faders. Min/max range editors,
    duplicate/conflict detection, and a per-slot MIDI channel override.

    Lifetime safety: Learn targets bind directly to the stable RackSlot via the
    MidiLearnBus; this component never holds itself into the bus except through
    an owner token and a Component::SafePointer used only for async refresh.
*/
class CCMappingComponent : public juce::Component,
                           public juce::ComboBox::Listener,
                           public juce::Button::Listener,
                           public juce::ListBoxModel {
public:
  using LearnBus = OpenRig::MidiLearnBus;

  CCMappingComponent(RackSlot &s) : slot(s) {
    // --- Fader CC row ---
    addAndMakeVisible(fohLabel);
    fohLabel.setText("FOH CC:", juce::dontSendNotification);
    addAndMakeVisible(fohCCEditor);
    fohCCEditor.setText(juce::String(slot.getFohCC()));
    fohCCEditor.setInputRestrictions(3, "0123456789");
    fohCCEditor.onTextChange = [this] {
      slot.setFohCC(fohCCEditor.getText().getIntValue());
    };
    addAndMakeVisible(fohLearnBtn);
    fohLearnBtn.setButtonText("L");
    fohLearnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
    fohLearnBtn.setTooltip("Learn FOH fader CC");
    fohLearnBtn.onClick = [this] { armFaderLearn(true); };

    addAndMakeVisible(iemLabel);
    iemLabel.setText("IEM CC:", juce::dontSendNotification);
    addAndMakeVisible(iemCCEditor);
    iemCCEditor.setText(juce::String(slot.getIemCC()));
    iemCCEditor.setInputRestrictions(3, "0123456789");
    iemCCEditor.onTextChange = [this] {
      slot.setIemCC(iemCCEditor.getText().getIntValue());
    };
    addAndMakeVisible(iemLearnBtn);
    iemLearnBtn.setButtonText("L");
    iemLearnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
    iemLearnBtn.setTooltip("Learn IEM fader CC");
    iemLearnBtn.onClick = [this] { armFaderLearn(false); };

    // --- MIDI channel override ---
    addAndMakeVisible(chLabel);
    chLabel.setText("MIDI Ch:", juce::dontSendNotification);
    addAndMakeVisible(chSelector);
    chSelector.addItem("Global", 1);
    chSelector.addItem("Omni", 2);
    chSelector.addItem("None (Routed Only)", 3);
    for (int ch = 1; ch <= 16; ++ch)
      chSelector.addItem("Ch " + juce::String(ch), 3 + ch);
    {
      int ov = slot.getMidiChannelOverride();
      int id = (ov == -1) ? 1 : (ov == 0 ? 2 : (ov == -2 ? 3 : 3 + ov));
      chSelector.setSelectedId(id, juce::dontSendNotification);
    }
    chSelector.onChange = [this] {
      int id = chSelector.getSelectedId();
      int ov = (id == 1) ? -1 : (id == 2 ? 0 : (id == 3 ? -2 : id - 3));
      slot.setMidiChannelOverride(ov);
    };

    // --- Mapping list ---
    addAndMakeVisible(mappingList);
    mappingList.setModel(this);
    mappingList.setColour(juce::ListBox::backgroundColourId,
                          juce::Colours::black.withAlpha(0.3f));
    mappingList.setRowHeight(24);

    // --- New mapping controls ---
    addAndMakeVisible(pluginSelector);
    pluginSelector.addItem("Select Plugin...", 0);
    pluginSelector.addListener(this);
    refreshPluginList();

    addAndMakeVisible(paramSelector);
    paramSelector.addItem("Select Parameter...", 0);
    paramSelector.setEnabled(false);

    addAndMakeVisible(minLabel);
    minLabel.setText("Min:", juce::dontSendNotification);
    addAndMakeVisible(minEditor);
    minEditor.setText("0.00");
    minEditor.setInputRestrictions(5, "0123456789.");

    addAndMakeVisible(maxLabel);
    maxLabel.setText("Max:", juce::dontSendNotification);
    addAndMakeVisible(maxEditor);
    maxEditor.setText("1.00");
    maxEditor.setInputRestrictions(5, "0123456789.");

    addAndMakeVisible(ccLabel);
    ccLabel.setText("CC#:", juce::dontSendNotification);
    addAndMakeVisible(ccEditor);
    ccEditor.setInputRestrictions(3, "0123456789");

    addAndMakeVisible(learnBtn);
    learnBtn.setButtonText("LEARN");
    learnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkblue);
    learnBtn.setTooltip("Arm learn: wiggle a knob to bind the selected param");
    learnBtn.addListener(this);

    addAndMakeVisible(mapBtn);
    mapBtn.setButtonText("MAP");
    mapBtn.addListener(this);

    addAndMakeVisible(invBtn);
    invBtn.setButtonText("INV");
    invBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff884400));
    invBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    invBtn.setTooltip("Invert CC direction (pull-down increases value, for B3 drawbars)");
    invBtn.setClickingTogglesState(true);
    invBtn.addListener(this);

    // --- CC Passthrough controls ---
    addAndMakeVisible(passFromLabel);
    passFromLabel.setText("Pass:", juce::dontSendNotification);
    passFromLabel.setTooltip("CC Passthrough: remap incoming CC to a different CC for plugin internal learn");
    addAndMakeVisible(passFromEditor);
    passFromEditor.setInputRestrictions(3, "0123456789");
    passFromEditor.setTooltip("Incoming CC number");

    addAndMakeVisible(passToLabel);
    passToLabel.setText(">", juce::dontSendNotification);
    addAndMakeVisible(passToEditor);
    passToEditor.setInputRestrictions(3, "0123456789");
    passToEditor.setTooltip("Outgoing CC number (what the plugin receives)");

    addAndMakeVisible(passBtn);
    passBtn.setButtonText("PASS");
    passBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff664400));
    passBtn.setTooltip("Add CC passthrough remap (for plugins with internal MIDI learn)");
    passBtn.addListener(this);

    addAndMakeVisible(ck88PresetBtn);
    ck88PresetBtn.setButtonText("CK88");
    ck88PresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff004466));
    ck88PresetBtn.setTooltip("Allow common CK88 CCs on this slot (mod, volume, expression, etc.)");
    ck88PresetBtn.addListener(this);

    // --- Quick Learn ---
    addAndMakeVisible(quickLearnBtn);
    quickLearnBtn.setButtonText("QUICK");
    quickLearnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkmagenta);
    quickLearnBtn.setClickingTogglesState(true);
    quickLearnBtn.setTooltip("Quick Learn: wiggle a CC then wiggle a plugin parameter (either order). Binds instantly.");
    quickLearnBtn.onClick = [this] {
      if (quickLearnBtn.getToggleState())
        armQuickLearn();
      else
        disarmQuickLearn();
    };

    // --- Selected-mapping editor row ---
    addAndMakeVisible(selMinLabel);
    selMinLabel.setText("Sel Min:", juce::dontSendNotification);
    addAndMakeVisible(selMinEditor);
    selMinEditor.setInputRestrictions(5, "0123456789.");
    selMinEditor.onTextChange = [this] { applySelectedRange(); };

    addAndMakeVisible(selMaxLabel);
    selMaxLabel.setText("Max:", juce::dontSendNotification);
    addAndMakeVisible(selMaxEditor);
    selMaxEditor.setInputRestrictions(5, "0123456789.");
    selMaxEditor.onTextChange = [this] { applySelectedRange(); };

    addAndMakeVisible(selLearnBtn);
    selLearnBtn.setButtonText("RE-LEARN");
    selLearnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkblue);
    selLearnBtn.onClick = [this] { armSelectedRelearn(); };

    addAndMakeVisible(selInvBtn);
    selInvBtn.setButtonText("INV");
    selInvBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff884400));
    selInvBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
    selInvBtn.setTooltip("Toggle invert on selected mapping");
    selInvBtn.setClickingTogglesState(true);
    selInvBtn.onClick = [this] { toggleSelectedInvert(); };

    addAndMakeVisible(delBtn);
    delBtn.setButtonText("DELETE");
    delBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    delBtn.onClick = [this] { deleteSelected(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    statusLabel.setFont(juce::FontOptions(12.0f));

    updateMappingList();
    mappingList.updateContent();
    setSize(660, 510);
  }

  ~CCMappingComponent() override {
    // Disarm any target this dialog left armed.
    disarmQuickLearn();
    LearnBus::getInstance().disarm();
  }

  void refreshPluginList() {
    pluginSelector.clear();
    bool hasPlugins = false;
    for (int i = 0; i < slot.getChainSize(); ++i) {
      auto name = slot.getPluginName(i);
      if (name.isNotEmpty()) {
        pluginSelector.addItem(name, i + 1);
        hasPlugins = true;
      }
    }
    if (!hasPlugins) {
      pluginSelector.setText("No Plugins Loaded");
      pluginSelector.setEnabled(false);
    } else {
      pluginSelector.setText("Select Plugin...");
      pluginSelector.setEnabled(true);
    }
  }

  void updateParamSelector(int chainIndex) {
    paramSelector.clear();
    auto params = slot.getParameterNames(chainIndex);
    if (params.isEmpty()) {
      paramSelector.setText("No Params");
      paramSelector.setEnabled(false);
      return;
    }
    int id = 1;
    for (const auto &p : params)
      paramSelector.addItem(p, id++);
    paramSelector.setText("Select Parameter...");
    paramSelector.setEnabled(true);
  }

  juce::String getParamId(int chainIndex, int paramIndex) const {
    if (auto *plugin = slot.getPluginInstance(chainIndex)) {
      auto &params = plugin->getParameters();
      if (paramIndex >= 0 && paramIndex < (int)params.size())
        if (auto *withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[paramIndex]))
          return withId->paramID;
    }
    return {};
  }

  void comboBoxChanged(juce::ComboBox *box) override {
    if (box == &pluginSelector) {
      int id = box->getSelectedId();
      if (id > 0)
        updateParamSelector(id - 1);
      else {
        paramSelector.clear();
        paramSelector.setEnabled(false);
      }
    }
  }

  void buttonClicked(juce::Button *b) override {
    if (b == &mapBtn)
      addManualMapping();
    else if (b == &learnBtn)
      armNewParamLearn();
    else if (b == &passBtn)
      addPassthroughMapping();
    else if (b == &ck88PresetBtn)
      applyCK88Preset();
  }

  // ---- Learn wiring (uses the MidiLearnBus singleton + LearnTarget) ----
  void armFaderLearn(bool isFoh) {
    RackSlot *s = &slot;
    bool foh = isFoh;
    OpenRig::LearnTarget t;
    t.isFader = true;
    t.isFohFader = isFoh;

    juce::Component::SafePointer<CCMappingComponent> safe(this);
    std::function<void(int, int)> cb = [s, foh, safe](int cc, int) {
      if (foh) {
        s->setFohCC(cc);
        s->allowCC(cc);
      } else {
        s->setIemCC(cc);
        s->allowCC(cc);
      }
      juce::MessageManager::callAsync([safe, cc] {
        if (safe)
          safe->refreshAfterCapture(cc);
      });
    };
    LearnBus::getInstance().arm(t, cb);
    statusLabel.setText("LEARNING: " + (isFoh ? juce::String("FOH fader")
                                              : juce::String("IEM fader")),
                        juce::dontSendNotification);
  }

  void armNewParamLearn() {
    int chainIdx = pluginSelector.getSelectedId() - 1;
    int paramIdx = paramSelector.getSelectedId() - 1;
    if (chainIdx < 0 || paramIdx < 0) {
      statusLabel.setText("Select a plugin and parameter first.",
                          juce::dontSendNotification);
      return;
    }
    float mn = minEditor.getText().getFloatValue();
    float mx = maxEditor.getText().getFloatValue();
    armParamLearn(chainIdx, paramIdx, mn, mx);
  }

  void armParamLearn(int chainIdx, int paramIdx, float mn, float mx) {
    juce::String paramId = getParamId(chainIdx, paramIdx);
    juce::String pname = slot.getParameterNames(chainIdx)[paramIdx];

    RackSlot *s = &slot;
    OpenRig::LearnTarget t;
    t.isFader = false;
    t.chainIndex = chainIdx;
    t.paramId = paramId;
    t.parameterIndex = paramIdx;

    juce::Component::SafePointer<CCMappingComponent> safe(this);
    std::function<void(int, int)> cb =
        [s, chainIdx, paramIdx, paramId, mn, mx, safe](int cc, int) {
          s->mapCCToParameter(cc, chainIdx, paramId, paramIdx, mn, mx);
          juce::MessageManager::callAsync([safe, cc] {
            if (safe)
              safe->refreshAfterCapture(cc);
          });
        };
    LearnBus::getInstance().arm(t, cb);
    statusLabel.setText("LEARNING: " + slot.getName() + " -> " + pname,
                        juce::dontSendNotification);
  }

  void armSelectedRelearn() {
    int row = mappingList.getSelectedRow();
    if (row < 0 || row >= (int)mappings.size())
      return;
    auto &m = mappings[row];
    if (m.type == MappingItem::Passthrough)
      return;
    armParamLearn(m.mapping.chainIndex, m.mapping.parameterIndex, 0.0f, 1.0f);
  }

  // ---- Quick Learn ----
  // Wiggle a CC on the controller, then wiggle a plugin parameter (or vice
  // versa). The two captures can arrive in either order; when both are present,
  // the binding is created instantly.
  void armQuickLearn() {
    quickLearn.active = true;
    quickLearn.hasCC = false;
    quickLearn.hasParam = false;

    // Attach parameter-change listeners to all plugins
    paramDetector.onParameterChanged = [this](juce::AudioProcessor *proc,
                                              int paramIdx) {
      if (!quickLearn.active)
        return;
      int chainIdx = -1;
      for (int i = 0; i < slot.getChainSize(); ++i) {
        if (slot.getPluginInstance(i) == proc) {
          chainIdx = i;
          break;
        }
      }
      if (chainIdx < 0)
        return;

      juce::Component::SafePointer<CCMappingComponent> safe(this);
      juce::MessageManager::callAsync([safe, chainIdx, paramIdx] {
        if (safe)
          safe->onParamWiggled(chainIdx, paramIdx);
      });
    };

    for (int i = 0; i < slot.getChainSize(); ++i) {
      if (auto *p = slot.getPluginInstance(i))
        p->addListener(&paramDetector);
    }

    // Arm the learn bus to capture CC (without auto-binding)
    OpenRig::LearnTarget t;
    t.isFader = false;
    t.label = "QUICK LEARN";
    juce::Component::SafePointer<CCMappingComponent> safe(this);
    std::function<void(int, int)> cb = [safe](int cc, int) {
      juce::MessageManager::callAsync([safe, cc] {
        if (safe)
          safe->onCCWiggled(cc);
      });
    };
    LearnBus::getInstance().arm(t, cb);

    statusLabel.setText("QUICK LEARN: wiggle a CC then a plugin knob (either order).",
                        juce::dontSendNotification);
  }

  void disarmQuickLearn() {
    if (!quickLearn.active)
      return;
    quickLearn.active = false;
    quickLearn.hasCC = false;
    quickLearn.hasParam = false;
    quickLearnBtn.setToggleState(false, juce::dontSendNotification);
    for (int i = 0; i < slot.getChainSize(); ++i) {
      if (auto *p = slot.getPluginInstance(i))
        p->removeListener(&paramDetector);
    }
    LearnBus::getInstance().disarm();
  }

  void onCCWiggled(int cc) {
    if (!quickLearn.active)
      return;
    // Re-arm for next wiggle if we're still waiting for a param
    if (!quickLearn.hasParam) {
      quickLearn.hasCC = true;
      quickLearn.cc = cc;
      statusLabel.setText("QUICK LEARN: CC" + juce::String(cc) +
                          " captured. Now move a plugin knob.",
                          juce::dontSendNotification);
      // Re-arm the bus to stay in learn mode
      OpenRig::LearnTarget t;
      t.isFader = false;
      t.label = "QUICK LEARN";
      juce::Component::SafePointer<CCMappingComponent> safe(this);
      std::function<void(int, int)> cb = [safe](int cc2, int) {
        juce::MessageManager::callAsync([safe, cc2] {
          if (safe)
            safe->onCCWiggled(cc2);
        });
      };
      LearnBus::getInstance().arm(t, cb);
      return;
    }

    // Both captured — bind
    completeQuickLearn(cc, quickLearn.chainIndex, quickLearn.paramIndex,
                       quickLearn.paramId);
  }

  void onParamWiggled(int chainIdx, int paramIdx) {
    if (!quickLearn.active)
      return;
    if (paramIdx < 0)
      return;

    juce::String paramId;
    juce::String pname;
    if (auto *plugin = slot.getPluginInstance(chainIdx)) {
      auto &params = plugin->getParameters();
      if (paramIdx < (int)params.size()) {
        pname = params[paramIdx]->getName(64);
        if (auto *withId = dynamic_cast<juce::AudioProcessorParameterWithID *>(
                params[paramIdx]))
          paramId = withId->paramID;
      }
    }

    if (!quickLearn.hasCC) {
      quickLearn.hasParam = true;
      quickLearn.chainIndex = chainIdx;
      quickLearn.paramIndex = paramIdx;
      quickLearn.paramId = paramId;
      quickLearn.paramName = pname;
      statusLabel.setText("QUICK LEARN: " + pname + " captured. Now wiggle a CC.",
                          juce::dontSendNotification);
      return;
    }

    // Both captured — bind
    completeQuickLearn(quickLearn.cc, chainIdx, paramIdx, paramId);
  }

  void completeQuickLearn(int cc, int chainIdx, int paramIdx,
                          const juce::String &paramId) {
    float mn = minEditor.getText().getFloatValue();
    float mx = maxEditor.getText().getFloatValue();
    if (mn < 0.0f || mn > 1.0f)
      mn = 0.0f;
    if (mx < 0.0f || mx > 1.0f)
      mx = 1.0f;
    bool inv = invBtn.getToggleState();
    slot.mapCCToParameter(cc, chainIdx, paramId, paramIdx, mn, mx, inv);

    disarmQuickLearn();
    updateMappingList();
    mappingList.updateContent();
    statusLabel.setText("QUICK LEARN bound: CC" + juce::String(cc) + " -> " +
                            slot.getPluginName(chainIdx) + " :" +
                            slot.getParameterNames(chainIdx)[paramIdx] +
                            (inv ? " [INV]" : ""),
                        juce::dontSendNotification);
  }

  void toggleSelectedInvert() {
    int row = mappingList.getSelectedRow();
    if (row < 0 || row >= (int)mappings.size())
      return;
    auto &m = mappings[row];
    if (m.type == MappingItem::Passthrough)
      return;
    bool newState = selInvBtn.getToggleState();
    slot.setCCInvert(m.cc, newState);
    m.mapping.invert = newState;
    mappingList.repaintRow(row);
    statusLabel.setText("CC" + juce::String(m.cc) + (newState ? " inverted." : " normal."),
                        juce::dontSendNotification);
  }

  // ListBoxModel
  int getNumRows() override { return (int)mappings.size(); }

  void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height,
                        bool rowIsSelected) override {
    (void)rowIsSelected;
    if (rowNumber >= (int)mappings.size())
      return;
    auto &m = mappings[rowNumber];
    g.setColour(rowIsSelected ? juce::Colour(0xff3a5e8e)
                              : juce::Colour(0xff222222));
    g.fillRect(0, 0, width, height - 1);

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);

    if (m.type == MappingItem::Passthrough) {
      juce::String text = "CC" + juce::String(m.cc).paddedLeft('0', 3) +
                          "  >  CC" + juce::String(m.outgoingCC).paddedLeft('0', 3) +
                          "  [PASSTHRU]";
      g.setColour(juce::Colour(0xffccaa44));
      g.drawText(text, 6, 0, width - 8, height, juce::Justification::centredLeft,
                 true);
    } else {
      juce::String pluginName = slot.getPluginName(m.mapping.chainIndex);
      auto params = slot.getParameterNames(m.mapping.chainIndex);
      juce::String paramName = (m.mapping.parameterIndex < params.size())
                                   ? params[m.mapping.parameterIndex]
                                   : "Unknown";
      juce::String text = "CC" + juce::String(m.cc).paddedLeft('0', 3) + "  " +
                          pluginName + " : " + paramName + "  [" +
                          juce::String(m.mapping.minValue, 2) + "-" +
                          juce::String(m.mapping.maxValue, 2) + "]" +
                          (m.mapping.invert ? " [INV]" : "");
      g.drawText(text, 6, 0, width - 8, height, juce::Justification::centredLeft,
                 true);
    }
  }

  void selectedRowsChanged(int lastRowSelected) override {
    bool hasSel = (lastRowSelected >= 0 && lastRowSelected < (int)mappings.size());
    selMinEditor.setEnabled(hasSel);
    selMaxEditor.setEnabled(hasSel);
    selLearnBtn.setEnabled(hasSel);
    delBtn.setEnabled(hasSel);
    selInvBtn.setEnabled(hasSel);
    if (hasSel) {
      auto &m = mappings[lastRowSelected];
      if (m.type == MappingItem::Passthrough) {
        selMinEditor.setEnabled(false);
        selMaxEditor.setEnabled(false);
        selLearnBtn.setEnabled(false);
        selInvBtn.setEnabled(false);
        selMinEditor.setText("--", juce::dontSendNotification);
        selMaxEditor.setText("--", juce::dontSendNotification);
        selInvBtn.setToggleState(false, juce::dontSendNotification);
      } else {
        selMinEditor.setText(juce::String(m.mapping.minValue, 2),
                             juce::dontSendNotification);
        selMaxEditor.setText(juce::String(m.mapping.maxValue, 2),
                             juce::dontSendNotification);
        selInvBtn.setToggleState(m.mapping.invert, juce::dontSendNotification);
      }
    }
  }

  void addManualMapping() {
    int cc = ccEditor.getText().getIntValue();
    int chainIdx = pluginSelector.getSelectedId() - 1;
    int paramIdx = paramSelector.getSelectedId() - 1;
    if (cc < 0 || cc > 127 || chainIdx < 0 || paramIdx < 0) {
      statusLabel.setText("Select plugin, parameter, and a valid CC (0-127).",
                          juce::dontSendNotification);
      return;
    }
    float mn = juce::jlimit(0.0f, 1.0f, minEditor.getText().getFloatValue());
    float mx = juce::jlimit(0.0f, 1.0f, maxEditor.getText().getFloatValue());
    bool inv = invBtn.getToggleState();
    juce::String paramId = getParamId(chainIdx, paramIdx);
    slot.mapCCToParameter(cc, chainIdx, paramId, paramIdx, mn, mx, inv);
    updateMappingList();
    mappingList.updateContent();
    ccEditor.setText("");
    invBtn.setToggleState(false, juce::dontSendNotification);
    statusLabel.setText("Mapped CC" + juce::String(cc) + (inv ? " [INV]." : "."),
                        juce::dontSendNotification);
  }

  void addPassthroughMapping() {
    int inCC = passFromEditor.getText().getIntValue();
    int outCC = passToEditor.getText().getIntValue();
    if (inCC < 0 || inCC > 127 || outCC < 0 || outCC > 127) {
      statusLabel.setText("Enter valid CC numbers (0-127) for passthrough.",
                          juce::dontSendNotification);
      return;
    }
    slot.addCCPassthrough(inCC, outCC);
    updateMappingList();
    mappingList.updateContent();
    passFromEditor.setText("");
    passToEditor.setText("");
    statusLabel.setText("Passthrough: CC" + juce::String(inCC) + " > CC" +
                        juce::String(outCC) + ".",
                        juce::dontSendNotification);
  }

  void applyCK88Preset() {
    using namespace OpenRigConstants;
    slot.allowCC(kCk88ModWheelCC);
    slot.allowCC(kCk88SustainCC);
    slot.allowCC(kCk88ChorusCC);
    slot.allowCC(kCk88ReverbCC);
    slot.allowCC(kCk88Drawbar16CC);
    slot.allowCC(kCk88Drawbar513CC);
    slot.allowCC(kCk88Drawbar8CC);
    slot.allowCC(kCk88Drawbar4CC);
    slot.allowCC(kCk88Drawbar223CC);
    slot.allowCC(kCk88Drawbar2CC);
    slot.allowCC(kCk88Drawbar135CC);
    slot.allowCC(kCk88Drawbar113CC);
    slot.allowCC(kCk88Drawbar1CC);
    statusLabel.setText("CK88 preset: mod(9) drawbars(77-86) delay(92,93) sus(64)",
                        juce::dontSendNotification);
  }

  void deleteSelected() {
    int row = mappingList.getSelectedRow();
    if (row < 0 || row >= (int)mappings.size())
      return;
    auto &m = mappings[row];
    int cc = m.cc;
    if (m.type == MappingItem::Passthrough)
      slot.removeCCPassthrough(cc);
    else
      slot.unmapCC(cc);
    updateMappingList();
    mappingList.updateContent();
    statusLabel.setText("Removed CC" + juce::String(cc),
                        juce::dontSendNotification);
  }

  void applySelectedRange() {
    int row = mappingList.getSelectedRow();
    if (row < 0 || row >= (int)mappings.size())
      return;
    auto &m = mappings[row];
    if (m.type == MappingItem::Passthrough)
      return;
    float mn = juce::jlimit(0.0f, 1.0f, selMinEditor.getText().getFloatValue());
    float mx = juce::jlimit(0.0f, 1.0f, selMaxEditor.getText().getFloatValue());
    // Re-map with existing identity but updated range.
    slot.mapCCToParameter(m.cc, m.mapping.chainIndex, m.mapping.paramId,
                          m.mapping.parameterIndex, mn, mx, m.mapping.invert);
    m.mapping.minValue = mn;
    m.mapping.maxValue = mx;
    mappingList.repaintRow(row);
  }

  void updateMappingList() {
    mappings.clear();
    // Parameter mappings
    auto map = slot.getCCMappings();
    for (const auto &pair : map) {
      MappingItem item;
      item.type = MappingItem::Parameter;
      item.cc = pair.first;
      item.mapping = pair.second;
      mappings.push_back(item);
    }
    // Passthrough mappings
    auto &ptMap = slot.getCCPassthroughMap();
    for (const auto &pair : ptMap) {
      MappingItem item;
      item.type = MappingItem::Passthrough;
      item.cc = pair.first;
      item.outgoingCC = pair.second;
      mappings.push_back(item);
    }
  }

  // Live refresh after a capture lands (called via SafePointer from the bus
  // binding, on the message thread). Reports any conflict the captured CC now
  // has with this slot's fader CCs.
  void refreshAfterCapture(int boundCC) {
    updateMappingList();
    mappingList.updateContent();
    fohCCEditor.setText(juce::String(slot.getFohCC()),
                        juce::dontSendNotification);
    iemCCEditor.setText(juce::String(slot.getIemCC()),
                        juce::dontSendNotification);

    juce::String msg = "Learned CC" + juce::String(boundCC) + ".";
    bool hasParam = slot.getCCMappings().count(boundCC) > 0;
    if (boundCC == slot.getFohCC() && boundCC >= 0)
      msg += "  WARNING: also controls FOH fader.";
    else if (boundCC == slot.getIemCC() && boundCC >= 0)
      msg += "  WARNING: also controls IEM fader.";
    else if (hasParam)
      msg += "  Bound to parameter.";
    statusLabel.setText(msg, juce::dontSendNotification);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("CC Assignment Manager", 10, 10, getWidth() - 20, 24,
               juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(40, 10.0f, (float)getWidth() - 10.0f);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(10);
    r.removeFromTop(40);

    // Fader row
    auto faderRow = r.removeFromTop(28);
    int lblW = 60, editW = 50, btnW = 26;
    fohLabel.setBounds(faderRow.removeFromLeft(lblW));
    fohCCEditor.setBounds(faderRow.removeFromLeft(editW));
    fohLearnBtn.setBounds(faderRow.removeFromLeft(btnW));
    faderRow.removeFromLeft(16);
    iemLabel.setBounds(faderRow.removeFromLeft(lblW));
    iemCCEditor.setBounds(faderRow.removeFromLeft(editW));
    iemLearnBtn.setBounds(faderRow.removeFromLeft(btnW));
    faderRow.removeFromLeft(16);
    chLabel.setBounds(faderRow.removeFromLeft(55));
    chSelector.setBounds(faderRow);

    r.removeFromTop(6);

    // List
    auto listArea = r.removeFromTop(r.getHeight() - 28 - 28 - 28 - 30 - 22);
    mappingList.setBounds(listArea);

    r.removeFromTop(6);

    // Selected-mapping editor row
    auto selRow = r.removeFromTop(26);
    selMinLabel.setBounds(selRow.removeFromLeft(56));
    selMinEditor.setBounds(selRow.removeFromLeft(50));
    selRow.removeFromLeft(6);
    selMaxLabel.setBounds(selRow.removeFromLeft(36));
    selMaxEditor.setBounds(selRow.removeFromLeft(50));
    selRow.removeFromLeft(8);
    selLearnBtn.setBounds(selRow.removeFromLeft(80));
    selRow.removeFromLeft(4);
    selInvBtn.setBounds(selRow.removeFromLeft(36));
    selRow.removeFromLeft(6);
    delBtn.setBounds(selRow.removeFromLeft(70));

    r.removeFromTop(6);

    // Add row
    auto addRow = r.removeFromTop(26);
    learnBtn.setBounds(addRow.removeFromRight(70));
    addRow.removeFromRight(6);
    mapBtn.setBounds(addRow.removeFromRight(60));
    addRow.removeFromRight(4);
    invBtn.setBounds(addRow.removeFromRight(36));
    addRow.removeFromRight(6);
    maxLabel.setBounds(addRow.removeFromRight(30));
    maxEditor.setBounds(addRow.removeFromRight(44));
    addRow.removeFromRight(6);
    minLabel.setBounds(addRow.removeFromRight(30));
    minEditor.setBounds(addRow.removeFromRight(44));
    addRow.removeFromRight(6);
    ccLabel.setBounds(addRow.removeFromRight(34));
    ccEditor.setBounds(addRow.removeFromRight(36));
    addRow.removeFromRight(6);
    paramSelector.setBounds(addRow.removeFromRight(130));
    addRow.removeFromRight(6);
    pluginSelector.setBounds(addRow);

    r.removeFromTop(4);

    // Passthrough row
    auto passRow = r.removeFromTop(26);
    quickLearnBtn.setBounds(passRow.removeFromRight(50));
    passRow.removeFromRight(6);
    ck88PresetBtn.setBounds(passRow.removeFromRight(50));
    passRow.removeFromRight(6);
    passBtn.setBounds(passRow.removeFromRight(50));
    passRow.removeFromRight(6);
    passToEditor.setBounds(passRow.removeFromRight(36));
    passRow.removeFromRight(2);
    passToLabel.setBounds(passRow.removeFromRight(14));
    passRow.removeFromRight(2);
    passFromEditor.setBounds(passRow.removeFromRight(36));
    passRow.removeFromRight(2);
    passFromLabel.setBounds(passRow.removeFromRight(36));

    r.removeFromTop(4);
    statusLabel.setBounds(r);
  }

private:
  RackSlot &slot;

  juce::Label fohLabel, iemLabel, minLabel, maxLabel, selMinLabel, selMaxLabel,
      chLabel, ccLabel, statusLabel, passFromLabel, passToLabel;
  juce::TextEditor fohCCEditor, iemCCEditor, minEditor, maxEditor, selMinEditor,
      selMaxEditor, ccEditor, passFromEditor, passToEditor;
  juce::ComboBox pluginSelector, paramSelector, chSelector;
  juce::TextButton fohLearnBtn, iemLearnBtn, learnBtn, mapBtn, selLearnBtn,
      delBtn, passBtn, ck88PresetBtn, invBtn, selInvBtn, quickLearnBtn;
  juce::ListBox mappingList;

  struct MappingItem {
    enum Type { Parameter, Passthrough };
    Type type = Parameter;
    int cc;               // incoming CC (both types)
    int outgoingCC = -1;  // for Passthrough only
    RackSlot::CCMapping mapping; // for Parameter only
  };
  std::vector<MappingItem> mappings;

  ParamChangeDetector paramDetector;

  struct QuickLearnState {
    bool active = false;
    bool hasCC = false;
    int cc = -1;
    bool hasParam = false;
    int chainIndex = -1;
    int paramIndex = -1;
    juce::String paramId;
    juce::String paramName;
  } quickLearn;

  // Detect duplicate/conflict for a candidate binding label. Returns a
  // human-readable warning suffix (possibly empty).
  juce::String findConflict(const juce::String &label) const {
    (void)label;
    // Conflict is detected at bind time against the slot's current mappings;
    // since the CC is unknown until capture, we can't pre-check precisely.
    // We surface a gentle reminder instead.
    return "  (wiggle a knob now)";
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CCMappingComponent)
};
