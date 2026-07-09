#pragma once

#include "RackSlot.h"
#include <JuceHeader.h>
#include <functional>

/**
 * Visual representation of a single RackSlot.
 * Optimized for touch and quick live tweaks.
 */
class RackSlotComponent : public juce::Component {
public:
  RackSlotComponent(RackSlot &s) : slot(s) {
    // Bypass Button
    addAndMakeVisible(bypassButton);
    bypassButton.setButtonText("BYP");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonOnColourId,
                           juce::Colours::red);
    bypassButton.onClick = [this] {
      slot.setBypass(bypassButton.getToggleState());
    };

    // Select Button
    addAndMakeVisible(selectButton);
    selectButton.setButtonText("SEL");
    selectButton.setClickingTogglesState(true);
    selectButton.setRadioGroupId(1234);
    selectButton.onClick = [this] {
      if (onSelect)
        onSelect();
    };

    // FOH Level
    addAndMakeVisible(fohLabel);
    fohLabel.setText("FOH", juce::dontSendNotification);
    fohLabel.setFont(juce::FontOptions(10.0f));

    addAndMakeVisible(fohSlider);
    fohSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fohSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fohSlider.setRange(0.0, 1.0);
    fohSlider.setValue(0.8);
    fohSlider.onValueChange = [this] {
      slot.setFohLevel((float)fohSlider.getValue());
    };

    // IEM Level
    addAndMakeVisible(iemLabel);
    iemLabel.setText("IEM", juce::dontSendNotification);
    iemLabel.setFont(juce::FontOptions(10.0f));

    addAndMakeVisible(iemSlider);
    iemSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    iemSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    iemSlider.setRange(0.0, 1.0);
    iemSlider.setValue(1.0);
    iemSlider.onValueChange = [this] {
      slot.setIemLevel((float)iemSlider.getValue());
    };
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    // Premium dark gradient
    juce::ColourGradient grad(juce::Colour(0xff2d2d2d), 0, 0,
                              juce::Colour(0xff1a1a1a), 0, bounds.getHeight(),
                              false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 6.0f);

    // Cyan glow when selected
    if (selectButton.getToggleState()) {
      g.setColour(juce::Colours::cyan.withAlpha(0.3f));
      g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, 2.0f);
    } else {
      g.setColour(juce::Colours::white.withAlpha(0.05f));
      g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText(slot.getName(), 20, 0, 200, getHeight(),
               juce::Justification::centredLeft);
  }

  void resized() override {
    auto r = getLocalBounds();
    r.removeFromLeft(250); // Space for name

    auto bypassArea = r.removeFromLeft(60).reduced(5);
    bypassButton.setBounds(bypassArea);

    auto selectArea = r.removeFromLeft(60).reduced(5);
    selectButton.setBounds(selectArea);

    auto fohArea = r.removeFromLeft(150).reduced(5);
    fohLabel.setBounds(fohArea.removeFromTop(15));
    fohSlider.setBounds(fohArea);

    auto iemArea = r.removeFromLeft(150).reduced(5);
    iemLabel.setBounds(iemArea.removeFromTop(15));
    iemSlider.setBounds(iemArea);
  }

  std::function<void()> onSelect;

private:
  RackSlot &slot;
  juce::TextButton bypassButton;
  juce::TextButton selectButton;

  juce::Slider fohSlider;
  juce::Label fohLabel;

  juce::Slider iemSlider;
  juce::Label iemLabel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackSlotComponent)
};
