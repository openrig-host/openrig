#include "ConfigModalOverlay.h"

ConfigModalOverlay::ConfigModalOverlay(Actions a) : actions(std::move(a)) {
  titleLabel.setText("CONFIGURATION", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
  titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF00E5FF));
  titleLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(titleLabel);

  infoLabel.setColour(juce::Label::backgroundColourId,
                      juce::Colour(0xFF101216));
  infoLabel.setColour(juce::Label::textColourId,
                      juce::Colours::white.withAlpha(0.85f));
  infoLabel.setFont(juce::FontOptions(13.0f));
  infoLabel.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(infoLabel);

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
  closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
  closeBtn.onClick = [this] {
    if (actions.onClose)
      actions.onClose();
  };
  addAndMakeVisible(closeBtn);

  refreshInfo();
  updateStressTestButtonState();
  setSize(360, 400);
}

void ConfigModalOverlay::updateStressTestButtonState() {
  if (actions.isStressTestActive && actions.isStressTestActive()) {
    stressTestBtn.setButtonText("STOP STRESS TEST");
    stressTestBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
  } else {
    stressTestBtn.setButtonText("RUN STRESS TEST");
    stressTestBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1D2023));
  }
}

void ConfigModalOverlay::refreshInfo() {
  if (actions.getInfo)
    infoLabel.setText(actions.getInfo(), juce::dontSendNotification);
}

void ConfigModalOverlay::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xE6121315));
  g.setColour(juce::Colour(0xFF00E5FF));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.5f);
}

void ConfigModalOverlay::resized() {
  auto area = getLocalBounds().reduced(14);
  auto top = area.removeFromTop(30);
  titleLabel.setBounds(top.removeFromLeft(top.getWidth() - 40));
  closeBtn.setBounds(top.removeFromRight(30));

  infoLabel.setBounds(area.removeFromTop(70).reduced(2));

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
  b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1D2023));
  b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF00E5FF));
  addAndMakeVisible(b);
}

void ConfigModalOverlay::clicked(std::function<void()> &cb) {
  if (cb)
    cb();
  refreshInfo();
}
