#pragma once
#include <JuceHeader.h>
#include "AssetLoader.h"

/**
 * BoutiqueLookAndFeel
 * Implements a high-end hardware aesthetic for OpenRig using custom vector
 * graphics and SVG assets. This removes dependency on external image assets
 * that were causing visual artifacts.
 */
class BoutiqueLookAndFeel : public juce::LookAndFeel_V4 {
public:
  bool useModernStyle = true;

  BoutiqueLookAndFeel() {
    // Set some professional colors
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::Slider::trackColourId,
              juce::Colours::black.withAlpha(0.4f));
  }
  // ===== Color Scheme System =====
  struct Scheme {
    juce::String name;
    juce::Colour bg, panel, accent, foh, iem, text, textDim, border;
  };

  int currentScheme = 0;

  static juce::StringArray getSchemeNames() {
    return {"Midnight", "Amber", "Crimson"};
  }

  Scheme getActiveScheme() const {
    static Scheme schemes[] = {
      {"Midnight", juce::Colour(0xFF121315),juce::Colour(0xFF1E2124),juce::Colour(0xFF00E5FF),juce::Colour(0xFF00E676),juce::Colour(0xFF29B6F6),juce::Colour(0xFFFFFFFF),juce::Colour(0xFF8E9AA6),juce::Colour(0xFF2A2E33)},
      {"Amber",    juce::Colour(0xFF141210),juce::Colour(0xFF221E1A),juce::Colour(0xFFFFB300),juce::Colour(0xFFFF8F00),juce::Colour(0xFFFF6E40),juce::Colour(0xFFFFF3E0),juce::Colour(0xFFA89080),juce::Colour(0xFF3D352B)},
      {"Crimson",  juce::Colour(0xFF120808),juce::Colour(0xFF1E1010),juce::Colour(0xFFFF1744),juce::Colour(0xFFD50000),juce::Colour(0xFFFF4081),juce::Colour(0xFFFFEBEE),juce::Colour(0xFF8E6060),juce::Colour(0xFF2A1515)},
    };
    return schemes[juce::jlimit(0, 2, currentScheme)];
  }

  void setScheme(int idx) {
    currentScheme = juce::jlimit(0, 2, idx);
    auto s = getActiveScheme();
    setColour(juce::ResizableWindow::backgroundColourId, s.bg);
    setColour(juce::Slider::thumbColourId, s.accent);
    setColour(juce::Slider::trackColourId, s.bg.darker(0.3f));
    setColour(juce::TextButton::buttonColourId, s.panel);
    setColour(juce::TextButton::buttonOnColourId, s.accent);
    setColour(juce::TextButton::textColourOffId, s.text);
    setColour(juce::TextButton::textColourOnId, s.bg);
    setColour(juce::Label::textColourId, s.text);
    setColour(juce::ComboBox::backgroundColourId, s.panel);
    setColour(juce::ComboBox::outlineColourId, s.border);
    setColour(juce::ComboBox::textColourId, s.text);
    setColour(juce::ListBox::backgroundColourId, s.panel);
    setColour(juce::ListBox::textColourId, s.text);
    setColour(juce::ScrollBar::backgroundColourId, s.bg);
    setColour(juce::ScrollBar::thumbColourId, s.border);
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
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(thumbRect.translated(0.0f, useModernStyle ? 2.0f : 3.0f), cornerSize);

    if (useModernStyle) {
      // Modern flat cap body
      g.setColour(juce::Colour(0xff2a2e33));
      g.fillRoundedRectangle(thumbRect, cornerSize);

      // Central accent bar
      juce::Colour accentCol = juce::Colours::white;
      auto id = slider.getComponentID();
      if (id == "foh" || id == "master_foh")
        accentCol = juce::Colour(0xff00e676); // emerald green
      else if (id == "iem" || id == "master_iem")
        accentCol = juce::Colour(0xff29b6f6); // sky blue
      else
        accentCol = juce::Colour(0xff00e5ff); // electric cyan

      float ridgeY = thumbRect.getCentreY();
      float ridgeH = 3.0f;
      juce::Rectangle<float> ridgeRect(thumbRect.getX() + 3, ridgeY - ridgeH * 0.5f,
                                       thumbRect.getWidth() - 6, ridgeH);

      g.setColour(accentCol);
      g.fillRoundedRectangle(ridgeRect, 1.0f);

      // Border
      g.setColour(juce::Colour(0xff16181a));
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
        g.setColour(juce::Colour(0xff0b0c0d)); // Sleek deep black track
      } else {
        // Track background: Blue for masters, Dark Grey for channels
        if (slider.getComponentID() == "master")
          g.setColour(juce::Colours::blue.withAlpha(0.5f));
        else
          g.setColour(juce::Colours::black.withAlpha(0.6f));
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
          g.setColour(useModernStyle ? juce::Colour(0xff2a2e33) : juce::Colours::white.withAlpha(0.4f));
          // Wider ticks
          g.drawLine((float)x + (width - tickWidth - 10) * 0.5f, yPos,
                     (float)x + (width + tickWidth + 10) * 0.5f, yPos, 1.0f);
        } else {
          // Minor ticks
          g.setColour(useModernStyle ? juce::Colour(0xff1e2124) : juce::Colours::white.withAlpha(0.15f));
          g.drawLine((float)x + (width - tickWidth) * 0.5f, yPos,
                     (float)x + (width + tickWidth) * 0.5f, yPos, 0.5f);
        }
      }

    } else {
      // Horizontal logic (just in case)
      juce::Rectangle<float> trackArea((float)x,
                                       (float)y + (height - trackWidth) * 0.5f,
                                       (float)width, (float)trackWidth);
      g.setColour(juce::Colour(0xff0b0c0d));
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
