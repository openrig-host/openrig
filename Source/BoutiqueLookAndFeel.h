#pragma once
#include <JuceHeader.h>
#include "AssetLoader.h"
#include "ThemeManager.h"

/**
 * BoutiqueLookAndFeel
 * Implements a high-end hardware aesthetic for OpenRig using custom vector
 * graphics and SVG assets. This removes dependency on external image assets
 * that were causing visual artifacts.
 */
class BoutiqueLookAndFeel : public juce::LookAndFeel_V4 {
public:
  // Mirrors the active theme's `flat` flag. Components that historically read
  // `laf->useModernStyle` keep working; it now reflects the theme.
  bool useModernStyle = true;

  BoutiqueLookAndFeel() {
    // Set default Sans-Serif Typeface for a clean, premium visual design (fixes "vibe codey" default fonts)
    setDefaultSansSerifTypefaceName("Segoe UI");
    applyThemeColors();
  }

  // Push the active theme's colors into the JUCE colour-ID system. Call after
  // switching themes so all LookAndFeel-owned widgets repaint correctly.
  void applyThemeColors() {
    useModernStyle = ThemeManager::getInstance().isFlat();
    const auto& t = ThemeManager::getInstance().active();

    setColour(juce::ResizableWindow::backgroundColourId, t.background);
    setColour(juce::DocumentWindow::backgroundColourId, t.background);
    setColour(juce::Slider::thumbColourId, t.knobThumb);
    setColour(juce::Slider::trackColourId, t.trackGroove);
    setColour(juce::Slider::textBoxTextColourId, t.text);
    setColour(juce::Slider::textBoxBackgroundColourId, t.panel);
    setColour(juce::Slider::textBoxOutlineColourId, t.border);
    setColour(juce::Slider::textBoxHighlightColourId, t.accent);

    setColour(juce::TextButton::buttonColourId, t.raised);
    setColour(juce::TextButton::buttonOnColourId, t.accent);
    setColour(juce::TextButton::textColourOffId, t.text);
    setColour(juce::TextButton::textColourOnId, t.textOnAccent);
    setColour(juce::ToggleButton::textColourId, t.text);
    setColour(juce::ToggleButton::tickColourId, t.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, t.textFaint);

    setColour(juce::Label::textColourId, t.text);
    setColour(juce::Label::backgroundColourId, t.panel);
    setColour(juce::Label::outlineColourId, t.border);

    setColour(juce::ComboBox::backgroundColourId, t.panel);
    setColour(juce::ComboBox::outlineColourId, t.border);
    setColour(juce::ComboBox::textColourId, t.text);
    setColour(juce::ComboBox::arrowColourId, t.textDim);
    setColour(juce::PopupMenu::backgroundColourId, t.panel);
    setColour(juce::PopupMenu::textColourId, t.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.active);

    setColour(juce::ListBox::backgroundColourId, t.panel);
    setColour(juce::ListBox::outlineColourId, t.border);
    setColour(juce::ListBox::textColourId, t.text);

    setColour(juce::ScrollBar::backgroundColourId, t.background);
    setColour(juce::ScrollBar::thumbColourId, t.borderStrong);
    setColour(juce::ScrollBar::trackColourId, t.trackGroove);

    setColour(juce::TextEditor::backgroundColourId, t.panelAlt);
    setColour(juce::TextEditor::textColourId, t.text);
    setColour(juce::TextEditor::outlineColourId, t.border);
    setColour(juce::TextEditor::highlightColourId, t.active);
    setColour(juce::TextEditor::focusedOutlineColourId, t.accent);
  }

  /**
   * Draw a skeuomorphic toggle switch using SVG asset
   *
   * @param g Graphics context
   * @param button Toggle button to draw
   * @param svgAsset Name of SVG asset to use
   * @param isOrange Whether to use orange accent color
   */
  void drawToggleSwitch(juce::Graphics &g, juce::ToggleButton &button,
                        const juce::String &svgAsset, bool isOrange = false);

  /**
   * Draw a skeuomorphic button background with 3D effect
   */
  void drawSkeuomorphicButtonBackground(juce::Graphics &g, juce::Button &button,
                                        const juce::Colour &baseColour);

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, const float rotaryStartAngle,
                        const float rotaryEndAngle,
                        juce::Slider &slider) override;

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawButtonText(juce::Graphics &g, juce::TextButton &button,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        const juce::Slider::SliderStyle style,
                        juce::Slider &slider) override {
    // Call custom background drawing
    drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos,
                               maxSliderPos, style, slider);

    // Call custom thumb drawing
    drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos,
                          maxSliderPos, style, slider);
  }

  void drawLinearSliderThumb(juce::Graphics &g, int x, int /*y*/, int width,
                             int /*height*/, float sliderPos,
                             float /*minSliderPos*/, float /*maxSliderPos*/,
                             const juce::Slider::SliderStyle /*style*/,
                             juce::Slider &slider) override {

    // Modern Vector Fader Cap
    float thumbWidth = (float)width * (useModernStyle ? 0.65f : 0.75f); // Good width relative to track
    float thumbHeight = thumbWidth * (useModernStyle ? 1.4f : 1.6f);   // Aspect ratio

    // Center it horizontally
    float thumbX = (float)x + ((float)width - thumbWidth) * 0.5f;
    float thumbY = sliderPos - (thumbHeight * 0.5f);

    juce::Rectangle<float> thumbRect(thumbX, thumbY, thumbWidth, thumbHeight);
    float cornerSize = useModernStyle ? 3.0f : 4.0f;

    // 1. Drop Shadow
    g.setColour(ThemeManager::get(Theme::Role::background).withAlpha(0.5f));
    g.fillRoundedRectangle(thumbRect.translated(0.0f, useModernStyle ? 2.0f : 3.0f), cornerSize);

    if (useModernStyle) {
      // Modern flat cap body
      g.setColour(ThemeManager::get(Theme::Role::knobFace));
      g.fillRoundedRectangle(thumbRect, cornerSize);

      // Central accent bar — routed by slider component ID to its semantic bus
      juce::Colour accentCol = ThemeManager::get(Theme::Role::accent);
      auto id = slider.getComponentID();
      if (id == "foh" || id == "master_foh")
        accentCol = ThemeManager::get(Theme::Role::foh);
      else if (id == "iem" || id == "master_iem")
        accentCol = ThemeManager::get(Theme::Role::iem);

      float ridgeY = thumbRect.getCentreY();
      float ridgeH = 3.0f;
      juce::Rectangle<float> ridgeRect(thumbRect.getX() + 3, ridgeY - ridgeH * 0.5f,
                                       thumbRect.getWidth() - 6, ridgeH);

      g.setColour(accentCol);
      g.fillRoundedRectangle(ridgeRect, 1.0f);

      // Border
      g.setColour(ThemeManager::get(Theme::Role::background).darker(0.2f));
      g.drawRoundedRectangle(thumbRect, cornerSize, 1.0f);
    } else {
      // 2. Main Body Gradient (Top-down lighting)
      juce::ColourGradient bodyGrad(
          juce::Colours::grey.brighter(0.2f), thumbRect.getX(), thumbRect.getY(),
          juce::Colours::grey.darker(0.3f), thumbRect.getX(),
          thumbRect.getBottom(), false);
      g.setGradientFill(bodyGrad);
      g.fillRoundedRectangle(thumbRect, cornerSize);

      // 3. Central Ridge / Grip Line (Concave effect)
      float ridgeY = thumbRect.getCentreY();
      float ridgeH = 2.0f;
      juce::Rectangle<float> ridgeRect(thumbRect.getX() + 4,
                                       ridgeY - ridgeH * 0.5f,
                                       thumbRect.getWidth() - 8, ridgeH);

      g.setColour(juce::Colours::black.withAlpha(0.6f));
      g.fillRect(ridgeRect.translated(0, -1)); // Bevel top shade

      g.setColour(juce::Colours::white.withAlpha(0.3f));
      g.fillRect(ridgeRect.translated(0, 1)); // Bevel bottom highlight

      g.setColour(juce::Colours::black.withAlpha(0.8f));
      g.fillRect(ridgeRect); // The groove itself

      // 4. Subtle Border
      g.setColour(juce::Colours::black.withAlpha(0.4f));
      g.drawRoundedRectangle(thumbRect, cornerSize, 1.0f);
    }
  }

  void drawLinearSliderBackground(juce::Graphics &g, int x, int y, int width,
                                  int height, float /*sliderPos*/,
                                  float /*minSliderPos*/,
                                  float /*maxSliderPos*/,
                                  const juce::Slider::SliderStyle /*style*/,
                                  juce::Slider &slider) override {
    auto trackWidth = useModernStyle ? 4.0f : 6.0f;
    auto cornerSize = useModernStyle ? 2.0f : 3.0f;

    if (slider.isVertical()) {
      juce::Rectangle<float> trackArea((float)x + (width - trackWidth) * 0.5f,
                                       (float)y, (float)trackWidth,
                                       (float)height);

      if (useModernStyle) {
        g.setColour(ThemeManager::get(Theme::Role::trackGroove)); // Sleek deep track
      } else {
        // Track background: themed for masters, darker for channels
        if (slider.getComponentID() == "master")
          g.setColour(ThemeManager::get(Theme::Role::iem).withAlpha(0.5f));
        else
          g.setColour(ThemeManager::get(Theme::Role::background).withAlpha(0.6f));
      }

      g.fillRoundedRectangle(trackArea, cornerSize);

      if (!useModernStyle) {
        // Inner track shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.drawRoundedRectangle(trackArea.reduced(1.0f), cornerSize, 1.0f);
      }

      // --- TRICKY LEVEL MARKINGS (Tick Marks) ---
      float tickWidth = useModernStyle ? 6.0f : 8.0f;
      float endY = (float)(y + height);

      for (int i = 0; i <= 10; ++i) {
        float normPos = (float)i / 10.0f;
        float yPos = endY - (normPos * (float)height);

        // Major ticks (every 20%)
        if (i % 2 == 0) {
          g.setColour(useModernStyle ? ThemeManager::get(Theme::Role::borderStrong) : juce::Colours::white.withAlpha(0.4f));
          // Wider ticks
          g.drawLine((float)x + (width - tickWidth - 10) * 0.5f, yPos,
                     (float)x + (width + tickWidth + 10) * 0.5f, yPos, 1.0f);
        } else {
          // Minor ticks
          g.setColour(useModernStyle ? ThemeManager::get(Theme::Role::border) : juce::Colours::white.withAlpha(0.15f));
          g.drawLine((float)x + (width - tickWidth) * 0.5f, yPos,
                     (float)x + (width + tickWidth) * 0.5f, yPos, 0.5f);
        }
      }

    } else {
      // Horizontal logic (just in case)
      juce::Rectangle<float> trackArea((float)x,
                                       (float)y + (height - trackWidth) * 0.5f,
                                       (float)width, (float)trackWidth);
      g.setColour(ThemeManager::get(Theme::Role::trackGroove));
      g.fillRoundedRectangle(trackArea, cornerSize);
    }
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoutiqueLookAndFeel)

protected:
  // Cached SVG drawables — loaded once, reused every repaint (no per-frame
  // allocation). Safe because JUCE painting is single-threaded; each paint
  // call sets the transform immediately before draw().
  std::unique_ptr<juce::Drawable> knobSvg;
  std::unique_ptr<juce::Drawable> toggleSwitchSvg;
  std::unique_ptr<juce::Drawable> toggleOrangeSvg;

  juce::Drawable* getCachedSvg(std::unique_ptr<juce::Drawable>& cache,
                               const juce::String& name) {
    if (!cache)
      cache = AssetLoader::getInstance().loadSVG(name);
    return cache.get();
  }
};
