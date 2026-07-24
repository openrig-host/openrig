#pragma once

#include <JuceHeader.h>
#include <cmath>

/**
 * AnalogVuMeter
 *
 * A classic vintage analog needle VU meter matching retro audio hardware aesthetics.
 * Features a cream faceplate, curved dB arc (-20 to +3 dB), red overload zone,
 * dynamic needle ballistics, and a red PEAK clip LED.
 */
class AnalogVuMeter : public juce::Component, private juce::Timer {
public:
    AnalogVuMeter(const juce::String& labelText = "VU")
        : label(labelText)
    {
        startTimerHz(60); // 60Hz smooth needle animation
    }

    ~AnalogVuMeter() override {
        stopTimer();
    }

    /**
     * Update target level from audio thread peak reading (linear magnitude 0.0 to 1.0+)
     */
    void setLevel(float linearLevel) {
        targetLevelLinear.store(linearLevel);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        
        // 1. Outer Dark Bezel / Metal Frame
        g.setColour(juce::Colour(0xff1f2125));
        g.fillRoundedRectangle(bounds, 5.0f);
        
        g.setColour(juce::Colour(0xff3a3d45));
        g.drawRoundedRectangle(bounds, 5.0f, 1.5f);

        // Faceplate area inside bezel
        auto face = bounds.reduced(4.0f);
        
        // 2. Vintage Cream Faceplate (soft cream-to-ivory gradient)
        juce::ColourGradient faceGrad(juce::Colour(0xfffaf6eb), face.getX(), face.getY(),
                                     juce::Colour(0xffefe4c8), face.getX(), face.getBottom(), false);
        g.setGradientFill(faceGrad);
        g.fillRoundedRectangle(face, 3.0f);

        // Inner shadow on faceplate top
        g.setColour(juce::Colours::black.withAlpha(0.12f));
        g.drawRoundedRectangle(face, 3.0f, 1.0f);

        // Clip faceplate content (red arc, scale, needle) strictly inside faceplate bounds
        juce::Graphics::ScopedSaveState faceClip(g);
        g.reduceClipRegion(face.toNearestInt());

        // Center pivot point at bottom center of faceplate
        float pivotX = face.getCentreX();
        float pivotY = face.getBottom() + (face.getHeight() * 0.25f);
        float radius = face.getHeight() * 0.95f;

        // 3. Scale Arc & Ticks (-20dB to +3dB)
        // Angle range: -40 degrees (-20dB) to +40 degrees (+3dB)
        float startAngle = juce::degreesToRadians(-38.0f);
        float endAngle   = juce::degreesToRadians(38.0f);
        float zeroAngle  = dbToAngle(0.0f); // Precise 0 dB start angle position

        // Overload red arc sector (0dB to +3dB)
        juce::Path redArc;
        redArc.addCentredArc(pivotX, pivotY, radius - 4.0f, radius - 4.0f, 0.0f, zeroAngle, endAngle, true);
        g.setColour(juce::Colour(0xffcc3322).withAlpha(0.35f));
        g.strokePath(redArc, juce::PathStrokeType(6.0f));

        // Scale Line
        juce::Path scaleArc;
        scaleArc.addCentredArc(pivotX, pivotY, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff2a2825));
        g.strokePath(scaleArc, juce::PathStrokeType(1.2f));

        // Scale Markings & Text (-20, -10, -7, -5, -3, 0, +3)
        struct DbMark { float db; const char* text; bool isRed; };
        const DbMark marks[] = {
            {-20.0f, "20", false},
            {-10.0f, "10", false},
            {-7.0f,  "7",  false},
            {-5.0f,  "5",  false},
            {-3.0f,  "3",  false},
            { 0.0f,  "0",  true},
            { 3.0f,  "3",  true}
        };

        for (const auto& m : marks) {
            float angle = dbToAngle(m.db);
            float cosA = std::sin(angle);
            float sinA = -std::cos(angle);

            float innerR = radius - (m.isRed ? 8.0f : 6.0f);
            float outerR = radius + 3.0f;

            float x1 = pivotX + innerR * cosA;
            float y1 = pivotY + sinA * innerR;
            float x2 = pivotX + outerR * cosA;
            float y2 = pivotY + sinA * outerR;

            g.setColour(m.isRed ? juce::Colour(0xffcc2211) : juce::Colour(0xff22201d));
            g.drawLine(x1, y1, x2, y2, m.isRed ? 1.8f : 1.2f);

            // Number labels
            float textR = radius - 15.0f;
            float tx = pivotX + textR * cosA;
            float ty = pivotY + sinA * textR;

            g.setFont(juce::FontOptions(face.getHeight() * 0.16f, juce::Font::bold));
            g.drawText(m.text, (int)tx - 12, (int)ty - 7, 24, 14, juce::Justification::centred, false);
        }

        // Left minus and Right plus signs
        g.setFont(juce::FontOptions(face.getHeight() * 0.18f, juce::Font::bold));
        g.setColour(juce::Colour(0xff22201d));
        g.drawText("-", (int)face.getX() + 6, (int)face.getY() + 6, 12, 12, juce::Justification::centred);
        g.setColour(juce::Colour(0xffcc2211));
        g.drawText("+", (int)face.getRight() - 24, (int)face.getY() + 6, 12, 12, juce::Justification::centred);

        // Center VU Label
        g.setColour(juce::Colour(0xff2a2825).withAlpha(0.75f));
        g.setFont(juce::FontOptions(face.getHeight() * 0.22f, juce::Font::bold));
        g.drawText(label, (int)face.getX(), (int)face.getBottom() - (int)(face.getHeight() * 0.38f),
                   (int)face.getWidth(), 18, juce::Justification::centred, false);

        // 4. Peak Clip Indicator LED (Top Right)
        float ledX = face.getRight() - 14.0f;
        float ledY = face.getY() + 20.0f;
        float ledR = 4.0f;

        bool isPeaking = (currentLevelLinear > 0.99f || peakHoldCounter > 0);
        g.setColour(isPeaking ? juce::Colour(0xffff2200) : juce::Colour(0xff551100));
        g.fillEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);

        if (isPeaking) {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillEllipse(ledX - 1.5f, ledY - 1.5f, 3.0f, 3.0f);
        }

        g.setFont(juce::FontOptions(face.getHeight() * 0.11f));
        g.setColour(juce::Colour(0xff554433));
        g.drawText("PEAK", (int)ledX - 16, (int)ledY + 4, 32, 10, juce::Justification::centred, false);

        // 5. Dynamic Needle Pointer
        float currentAngle = dbToAngle(currentDb);
        float needleLen = radius - 2.0f;
        float nx = pivotX + needleLen * std::sin(currentAngle);
        float ny = pivotY - needleLen * std::cos(currentAngle);

        // Shadow behind needle
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.drawLine(pivotX + 1.5f, pivotY + 1.0f, nx + 1.5f, ny + 1.0f, 1.5f);

        // Tapered Needle Body
        g.setColour(juce::Colour(0xff181715));
        g.drawLine(pivotX, pivotY, nx, ny, 1.4f);

        // Bottom Pivot Housing Cap
        float capR = face.getHeight() * 0.14f;
        juce::Path capPath;
        capPath.addCentredArc(pivotX, pivotY, capR, capR, 0.0f,
                             juce::degreesToRadians(-90.0f), juce::degreesToRadians(90.0f), true);
        capPath.lineTo(pivotX, pivotY);
        capPath.closeSubPath();

        g.setColour(juce::Colour(0xff222226));
        g.fillPath(capPath);
        g.setColour(juce::Colour(0xff44444a));
        g.strokePath(capPath, juce::PathStrokeType(1.0f));
    }

private:
    void timerCallback() override {
        float targetLin = targetLevelLinear.load();
        
        // Analog VU Ballistics (exponential integration)
        if (targetLin > currentLevelLinear) {
            // Fast attack (~300ms integration)
            currentLevelLinear += (targetLin - currentLevelLinear) * 0.25f;
        } else {
            // Smooth decay
            currentLevelLinear += (targetLin - currentLevelLinear) * 0.12f;
        }

        // Convert linear amplitude to dB (-40dB floor to +3dB ceiling)
        float db = (currentLevelLinear > 0.0001f) ? (20.0f * std::log10(currentLevelLinear)) : -40.0f;
        currentDb += (db - currentDb) * 0.3f;

        // Clip LED hold logic
        if (targetLin >= 0.99f) {
            peakHoldCounter = 15; // ~250ms hold
        } else if (peakHoldCounter > 0) {
            peakHoldCounter--;
        }

        repaint();
    }

    float dbToAngle(float db) const {
        // Map dB (-20dB to +3dB) to arc angle radians
        float clamped = juce::jlimit(-20.0f, 3.0f, db);
        float norm = (clamped + 20.0f) / 23.0f; // 0.0 at -20dB, 1.0 at +3dB
        float startAngle = juce::degreesToRadians(-38.0f);
        float endAngle   = juce::degreesToRadians(38.0f);
        return startAngle + norm * (endAngle - startAngle);
    }

    juce::String label;
    std::atomic<float> targetLevelLinear{0.0f};
    float currentLevelLinear = 0.0f;
    float currentDb = -40.0f;
    int peakHoldCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuMeter)
};
