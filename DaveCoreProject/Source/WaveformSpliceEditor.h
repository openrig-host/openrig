#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"

class WaveformSpliceEditor : public juce::Component,
                             public juce::ChangeListener {
public:
    WaveformSpliceEditor(std::function<void(float, float)> onRatiosChanged)
        : cache(5),
          thumbnail(256, formatManager, cache),
          onChangedCallback(onRatiosChanged)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener(this);
    }

    ~WaveformSpliceEditor() override {
        thumbnail.removeChangeListener(this);
    }

    void setFile(const juce::File& file, float currentStart, float currentEnd) {
        startRatio = currentStart;
        endRatio = currentEnd;
        
        if (file.existsAsFile()) {
            thumbnail.setSource(new juce::FileInputSource(file));
        } else {
            thumbnail.clear();
        }
        repaint();
    }

    void setRatios(float start, float end) {
        startRatio = start;
        endRatio = end;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        
        // Background
        g.setColour(ThemeManager::get(Theme::Role::trackGroove));
        g.fillRoundedRectangle(bounds, 4.0f);

        // Draw Waveform
        if (thumbnail.getNumChannels() > 0) {
            g.setColour(ThemeManager::get(Theme::Role::iem).withAlpha(0.6f));
            thumbnail.drawChannels(g, getLocalBounds().reduced(2), 0.0, thumbnail.getTotalLength(), 1.0f);

            // Shading out-of-range areas
            float startX = startRatio * getWidth();
            float endX = endRatio * getWidth();

            g.setColour(juce::Colours::black.withAlpha(0.5f));
            // Left shaded block
            g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, startX, (float)getHeight()));
            // Right shaded block
            g.fillRect(juce::Rectangle<float>(endX, 0.0f, (float)getWidth() - endX, (float)getHeight()));

            // Handle lines
            g.setColour(juce::Colours::orange);
            g.drawVerticalLine((int)startX, 0.0f, (float)getHeight());
            
            g.setColour(juce::Colours::yellow);
            g.drawVerticalLine((int)endX, 0.0f, (float)getHeight());

            // Tiny indicator circles at the top of the handles
            g.setColour(juce::Colours::orange);
            g.fillEllipse(startX - 4, 1.0f, 8.0f, 8.0f);
            
            g.setColour(juce::Colours::yellow);
            g.fillEllipse(endX - 4, 1.0f, 8.0f, 8.0f);
        } else {
            g.setColour(juce::Colours::grey.withAlpha(0.4f));
            g.setFont(juce::FontOptions(13.0f, juce::Font::italic));
            g.drawText("[ Select a loaded pad to view/slice waveform ]", getLocalBounds(), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (thumbnail.getNumChannels() == 0) return;
        
        float x = (float)e.x;
        float startX = startRatio * getWidth();
        float endX = endRatio * getWidth();

        // Check which handle is closer
        if (std::abs(x - startX) < std::abs(x - endX)) {
            draggingStart = true;
        } else {
            draggingStart = false;
        }
        
        updateHandlePosition(x);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (thumbnail.getNumChannels() == 0) return;
        updateHandlePosition((float)e.x);
    }

    void changeListenerCallback(juce::ChangeBroadcaster* /*source*/) override {
        repaint();
    }

private:
    void updateHandlePosition(float mouseX) {
        float ratio = juce::jlimit(0.0f, 1.0f, mouseX / getWidth());
        
        if (draggingStart) {
            startRatio = juce::jmin(ratio, endRatio - 0.01f);
        } else {
            endRatio = juce::jmax(ratio, startRatio + 0.01f);
        }
        
        if (onChangedCallback) {
            onChangedCallback(startRatio, endRatio);
        }
        repaint();
    }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache cache;
    juce::AudioThumbnail thumbnail;
    
    std::function<void(float, float)> onChangedCallback;
    
    float startRatio = 0.0f;
    float endRatio = 1.0f;
    bool draggingStart = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformSpliceEditor)
};
