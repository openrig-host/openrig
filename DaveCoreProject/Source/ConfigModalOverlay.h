#pragma once

#include <JuceHeader.h>
#include <functional>

class ConfigModalOverlay : public juce::Component {
public:
  struct Actions {
    std::function<void()> onAudioSettings;
    std::function<void()> onScanPlugins;
    std::function<void()> onBusRouting;
    std::function<void()> onResetAudio;
    std::function<void()> onAbout;
    std::function<void()> onClose;
    std::function<juce::String()> getInfo;
    std::function<void()> onStressTestToggle;
    std::function<bool()> isStressTestActive;
  };

  explicit ConfigModalOverlay(Actions a);
  void refreshInfo();
  void updateStressTestButtonState();
  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void addSettingButton(juce::TextButton &b, const juce::String &text);
  void clicked(std::function<void()> &cb);

  Actions actions;
  juce::Label titleLabel;
  juce::Label infoLabel;
  juce::TextButton audioSettingsBtn, scanPluginsBtn, busRoutingBtn,
      resetAudioBtn, aboutBtn, stressTestBtn;
  juce::TextButton closeBtn{"X"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigModalOverlay)
};
