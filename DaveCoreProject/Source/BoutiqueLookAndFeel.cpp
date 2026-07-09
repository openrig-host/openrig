#include "BoutiqueLookAndFeel.h"
#include "AssetLoader.h"

void BoutiqueLookAndFeel::drawToggleSwitch(juce::Graphics &g,
                                           juce::ToggleButton &button,
                                           const juce::String &svgAsset,
                                           bool isOrange) {
  auto bounds = button.getLocalBounds().toFloat();

  // Use cached SVG instance (loaded once, reused every frame). Select via the
  // already-available bool instead of substring-matching the asset name.
  auto* svg = isOrange ? getCachedSvg(toggleOrangeSvg, svgAsset)
                       : getCachedSvg(toggleSwitchSvg, svgAsset);
  if (svg == nullptr) {
    // Fallback to default button drawing
    juce::LookAndFeel_V4::drawToggleButton(g, button, button.isOver(),
                                           button.isDown());
    return;
  }

  // Set bounds and draw
  svg->setTransformToFit(bounds, juce::RectanglePlacement::centred);

  // Rotate based on toggle state (180 degrees for off, 0 for on)
  float rotation = button.getToggleState() ? 0.0f : 180.0f;
  svg->setTransform(
      juce::AffineTransform::rotation(juce::degreesToRadians(rotation),
                                      bounds.getCentreX(), bounds.getCentreY())
          .scaled(bounds.getWidth() / svg->getWidth(),
                  bounds.getHeight() / svg->getHeight()));

  svg->draw(g, 1.0f);
}

void BoutiqueLookAndFeel::drawSkeuomorphicButtonBackground(
    juce::Graphics &g, juce::Button &button, const juce::Colour &baseColour) {
  auto bounds = button.getLocalBounds().toFloat();
  auto cornerSize = 4.0f;

  // Drop shadow (bottom-right)
  g.setColour(juce::Colours::black.withAlpha(0.4f));
  g.fillRoundedRectangle(bounds.translated(2, 2), cornerSize);

  // Main body gradient (top-left to bottom-right)
  juce::ColourGradient bodyGrad(baseColour.brighter(0.2f), bounds.getX(),
                                bounds.getY(), baseColour.darker(0.3f),
                                bounds.getRight(), bounds.getBottom(), false);
  g.setGradientFill(bodyGrad);
  g.fillRoundedRectangle(bounds, cornerSize);

  // Top highlight (subtle)
  g.setColour(juce::Colours::white.withAlpha(0.15f));
  g.drawRoundedRectangle(bounds.reduced(1), cornerSize, 1.0f);

  // Bottom shadow (subtle)
  g.setColour(juce::Colours::black.withAlpha(0.3f));
  g.drawRoundedRectangle(bounds.translated(0, 1).reduced(1), cornerSize, 1.0f);
}

void BoutiqueLookAndFeel::drawToggleButton(juce::Graphics &g,
                                           juce::ToggleButton &button,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
  // Check if button has specific SVG asset tag
  if (button.getProperties().contains("useToggleSwitch")) {
    bool isOrange = button.getProperties()["isOrangeToggle"];

    // Map to correct SVG filename
    // Use toggle_orange.svg for orange accents, defaults to standard
    // toggle_switch.svg
    juce::String svgAsset =
        isOrange ? "toggle_orange.svg" : "toggle_switch.svg";

    drawToggleSwitch(g, button, svgAsset, isOrange);
    return;
  }

  // Fallback to standard LookAndFeel drawing
  juce::LookAndFeel_V4::drawToggleButton(
      g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void BoutiqueLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                           int width, int height,
                                           float sliderPos,
                                           const float rotaryStartAngle,
                                           const float rotaryEndAngle,
                                           juce::Slider & /*slider*/) {
  auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  auto angle =
      rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // Use cached SVG knob (loaded once, reused every frame)
  auto* svg = getCachedSvg(knobSvg, "volume_knob.svg");

  if (svg != nullptr) {
    // Fit into bounds, then apply rotation around centre.
    juce::RectanglePlacement placement(juce::RectanglePlacement::centred);
    auto transform = placement.getTransformToFit(svg->getDrawableBounds(),
                                                 bounds.reduced(2));
    svg->setTransform(transform.followedBy(juce::AffineTransform::rotation(
        angle, bounds.getCentreX(), bounds.getCentreY())));
    svg->draw(g, 1.0f);
    return;
  }

  // Fallback Vector Drawing
  auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
  auto center = bounds.getCentre();
  float knobRadius = radius * 0.85f;

  // 1. Drop Shadow (simulated)
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.fillEllipse(center.x - knobRadius + 2, center.y - knobRadius + 4,
                knobRadius * 2, knobRadius * 2);

  // 2. Main Knob Body Gradient
  juce::ColourGradient knobGrad(juce::Colours::darkgrey.brighter(0.1f),
                                center.x - knobRadius, center.y - knobRadius,
                                juce::Colours::black, center.x + knobRadius,
                                center.y + knobRadius, false);
  g.setGradientFill(knobGrad);
  g.fillEllipse(center.x - knobRadius, center.y - knobRadius, knobRadius * 2,
                knobRadius * 2);

  // 3. Top Reflection (Gloss)
  g.setColour(juce::Colours::white.withAlpha(0.08f));
  g.fillEllipse(center.x - knobRadius * 0.7f, center.y - knobRadius * 0.8f,
                knobRadius * 1.4f, knobRadius * 0.6f);

  // 4. Subtle Rim
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.drawEllipse(center.x - knobRadius, center.y - knobRadius, knobRadius * 2,
                knobRadius * 2, 1.5f);

  // 5. Indicator Line
  juce::Path p;
  float pointerLength = knobRadius * 0.7f;
  float pointerThickness = 3.0f;
  p.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness,
                 pointerLength);

  p.applyTransform(
      juce::AffineTransform::rotation(angle).translated(center.x, center.y));

  // Draw indicator shadow/glow
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.strokePath(p, juce::PathStrokeType(pointerThickness + 2.0f));

  // Draw indicator
  g.setColour(juce::Colours::white.withAlpha(0.9f));
  g.fillPath(p);
}

void BoutiqueLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                               juce::Button &button,
                                               const juce::Colour &backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown) {
  if (!useModernStyle) {
    juce::LookAndFeel_V4::drawButtonBackground(g, button, backgroundColour,
                                               shouldDrawButtonAsHighlighted,
                                               shouldDrawButtonAsDown);
    return;
  }

  auto bounds = button.getLocalBounds().toFloat();
  auto cornerSize = 4.0f;

  juce::Colour baseCol = juce::Colour(0xff1d2024); // Flat charcoal default

  auto text = button.getButtonText();

  // Highlight specific semantic buttons
  bool isPanic = (text == "PANIC");
  bool isMute = (text == "MUTE" || text == "M");
  bool isActive = button.getToggleState();

  // Assigned setup buttons have darkgreen in the legacy system
  bool isAssignedSetup = (backgroundColour.getGreen() > backgroundColour.getRed() && 
                          backgroundColour.getGreen() > backgroundColour.getBlue() &&
                          text != "FOH" && text.toUpperCase() != "ON" && text != "MIDI MON" &&
                          text != "SAVE" && text != "LOAD" && text != "DYN");

  if (isPanic) {
    baseCol = shouldDrawButtonAsDown ? juce::Colour(0xffb2102f) : juce::Colour(0xff881122);
  } else if (isMute) {
    if (isActive)
      baseCol = juce::Colour(0xffd50000); // Crimson for active mute
    else
      baseCol = juce::Colour(0xff1d2024);
  } else if (isActive) {
    // Currently active item
    if (text == "FOH") {
      baseCol = juce::Colour(0xff00b0ff); // Sky blue
    } else if (text == "IEM" || text == "EARS") {
      baseCol = juce::Colour(0xff00e676); // Green
    } else {
      baseCol = juce::Colour(0xff00e5ff); // Active setup is glowing Cyan!
    }
  } else if (isAssignedSetup) {
    // Assigned setup but not active: subtle dark green background to show it's loaded
    baseCol = juce::Colour(0xff12281a);
  } else {
    // Standard flat buttons in modern mode
    baseCol = juce::Colour(0xff1d2024);
  }

  // Hover and click states
  if (shouldDrawButtonAsDown) {
    baseCol = baseCol.darker(0.2f);
  } else if (shouldDrawButtonAsHighlighted) {
    baseCol = baseCol.brighter(0.15f);
  }

  // Draw flat button body
  g.setColour(baseCol);
  g.fillRoundedRectangle(bounds, cornerSize);

  // Borders
  juce::Colour borderCol = juce::Colour(0xff2e3238);
  if (isPanic && !shouldDrawButtonAsDown) {
    borderCol = juce::Colour(0xffff1744);
  } else if (isActive) {
    borderCol = baseCol.brighter(0.2f);
  } else if (isAssignedSetup) {
    borderCol = juce::Colour(0xff00e676).withAlpha(0.4f); // Subtle green border for assigned setups
  }
  
  g.setColour(borderCol);
  g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);
}

void BoutiqueLookAndFeel::drawButtonText(juce::Graphics &g, juce::TextButton &button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown) {
  if (!useModernStyle) {
    juce::LookAndFeel_V4::drawButtonText(g, button, shouldDrawButtonAsHighlighted,
                                         shouldDrawButtonAsDown);
    return;
  }

  auto bounds = button.getLocalBounds().reduced(2, 0);
  g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                        : juce::TextButton::textColourOffId));

  float fontSize = 11.0f;
  int w = button.getWidth();
  if (w < 26) {
    fontSize = 7.0f; // extra tiny
  } else if (w < 32) {
    fontSize = 8.0f; // tiny
  } else if (w < 45) {
    fontSize = 9.0f; // small
  }

  g.setFont(juce::FontOptions(fontSize, juce::Font::bold));
  
  // If the button is extremely narrow, use single-line fit with 0 horizontal padding
  g.drawFittedText(button.getButtonText(), bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                   juce::Justification::centred, 1);
}
