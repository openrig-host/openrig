#include "ConfigModalOverlay.h"
#include "ThemeManager.h"

ConfigModalOverlay::ConfigModalOverlay(Actions a) : actions(std::move(a)) {
  titleLabel.setText("CONFIGURATION", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
  titleLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(titleLabel);

  infoLabel.setColour(juce::Label::backgroundColourId,
                      ThemeManager::get(Theme::Role::panelAlt));
  infoLabel.setColour(juce::Label::textColourId,
                      ThemeManager::get(Theme::Role::text).withAlpha(0.85f));
  infoLabel.setFont(juce::FontOptions(13.0f));
  infoLabel.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(infoLabel);

  // Theme selector
  themeLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
  themeLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::textDim));
  themeLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(themeLabel);

  themeCombo.addItemList(ThemeManager::getInstance().getThemeNames(), 1);
  themeCombo.setSelectedItemIndex(ThemeManager::getInstance().getCurrentIndex(),
                                  juce::dontSendNotification);
  themeCombo.onChange = [this] {
    ThemeManager::getInstance().setCurrentByIndex(themeCombo.getSelectedItemIndex());
  };
  addAndMakeVisible(themeCombo);

  addSettingButton(audioSettingsBtn, "AUDIO SETTINGS");
  audioSettingsBtn.onClick = [this] { clicked(actions.onAudioSettings); };
  addSettingButton(scanPluginsBtn, "SCAN FOR PLUGINS");
  scanPluginsBtn.onClick = [this] { clicked(actions.onScanPlugins); };
  addSettingButton(busRoutingBtn, "BUS ROUTING");
  busRoutingBtn.onClick = [this] { clicked(actions.onBusRouting); };
  addSettingButton(resetAudioBtn, "RESET AUDIO");
  resetAudioBtn.onClick = [this] { clicked(actions.onResetAudio); };
  addSettingButton(stressTestBtn, "RUN STRESS TEST");
  stressTestBtn.onClick = [this] {
    clicked(actions.onStressTestToggle);
    updateStressTestButtonState();
  };
  addSettingButton(aboutBtn, "ABOUT OPENRIG");
  aboutBtn.onClick = [this] { clicked(actions.onAbout); };

  closeBtn.setButtonText("X");
  closeBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::danger));
  closeBtn.onClick = [this] {
    if (actions.onClose)
      actions.onClose();
  };
  addAndMakeVisible(closeBtn);

  refreshInfo();
  updateStressTestButtonState();
  setSize(360, 450);
}

void ConfigModalOverlay::updateStressTestButtonState() {
  if (actions.isStressTestActive && actions.isStressTestActive()) {
    stressTestBtn.setButtonText("STOP STRESS TEST");
    stressTestBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::danger).darker(0.3f));
  } else {
    stressTestBtn.setButtonText("RUN STRESS TEST");
    stressTestBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
  }
}

void ConfigModalOverlay::refreshInfo() {
  if (actions.getInfo)
    infoLabel.setText(actions.getInfo(), juce::dontSendNotification);
}

void ConfigModalOverlay::paint(juce::Graphics &g) {
  g.fillAll(ThemeManager::get(Theme::Role::scrim));
  g.setColour(ThemeManager::get(Theme::Role::accent));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.5f);
}

void ConfigModalOverlay::resized() {
  auto area = getLocalBounds().reduced(14);
  auto top = area.removeFromTop(30);
  titleLabel.setBounds(top.removeFromLeft(top.getWidth() - 40));
  closeBtn.setBounds(top.removeFromRight(30));

  infoLabel.setBounds(area.removeFromTop(70).reduced(2));

  // Theme selector row
  auto themeRow = area.removeFromTop(28).reduced(2);
  themeLabel.setBounds(themeRow.removeFromLeft(70));
  themeCombo.setBounds(themeRow);

  int rowH = 36;
  audioSettingsBtn.setBounds(area.removeFromTop(rowH).reduced(2));
  scanPluginsBtn.setBounds(area.removeFromTop(rowH).reduced(2));
  busRoutingBtn.setBounds(area.removeFromTop(rowH).reduced(2));
  resetAudioBtn.setBounds(area.removeFromTop(rowH).reduced(2));
  stressTestBtn.setBounds(area.removeFromTop(rowH).reduced(2));
  aboutBtn.setBounds(area.removeFromTop(rowH).reduced(2));
}

void ConfigModalOverlay::addSettingButton(juce::TextButton &b, const juce::String &text) {
  b.setButtonText(text);
  b.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
  b.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::accent));
  addAndMakeVisible(b);
}

void ConfigModalOverlay::clicked(std::function<void()> &cb) {
  if (cb)
    cb();
  refreshInfo();
}
