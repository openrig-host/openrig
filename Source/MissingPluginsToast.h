#pragma once
#include <JuceHeader.h>
#include "ThemeManager.h"

struct MissingPluginInfo {
    juce::String name;
    juce::String location;
    juce::String path;
};

/**
 * A non-modal, silent warning popup toast that surfaces plugins required by a
 * setup that are not installed on the current host machine.
 *
 * Characteristics:
 * - Silent: No system alert sounds / chimes.
 * - Non-modal: Does not block audio, keyboard input, or interaction with the rig.
 * - Automatically dismisses after 12 seconds with countdown or closes on click.
 */
class MissingPluginsToast : public juce::Component, public juce::Timer {
public:
    MissingPluginsToast(const std::vector<MissingPluginInfo>& missingList, std::function<void()> onDismissCallback)
        : items(missingList), onDismiss(std::move(onDismissCallback))
    {
        setAlwaysOnTop(true);
        startTimer(100);
    }

    ~MissingPluginsToast() override {
        stopTimer();
    }

    int getDesiredHeight() const {
        int headerHeight = 30;
        int subtitleHeight = 20;
        int itemHeight = 22;
        int footerHeight = 28;
        int padding = 20;
        return headerHeight + subtitleHeight + (int)items.size() * itemHeight + footerHeight + padding;
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        // Drop shadow
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(bounds.translated(0, 3.0f), 10.0f);

        // Dark glassmorphic background
        juce::Colour bgCol = ThemeManager::get(Theme::Role::panel).darker(0.35f).withAlpha(0.96f);
        g.setColour(bgCol);
        g.fillRoundedRectangle(bounds, 10.0f);

        // Warning accent border
        juce::Colour warnCol = ThemeManager::get(Theme::Role::warn);
        g.setColour(warnCol.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, 10.0f, 1.8f);

        auto inner = bounds.reduced(14.0f, 10.0f);

        // Header with warning icon and title
        auto headerArea = inner.removeFromTop(24.0f);
        g.setColour(warnCol);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(juce::String::charToString(0x26A0) + "  MISSING PLUGINS (NOT INSTALLED)",
                   headerArea.withTrimmedRight(30.0f), juce::Justification::left, true);

        // Close 'X' in top right
        closeButtonArea = juce::Rectangle<float>(bounds.getRight() - 32.0f, bounds.getY() + 8.0f, 24.0f, 24.0f);
        g.setColour(ThemeManager::get(Theme::Role::textDim));
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText(juce::CharPointer_UTF8("\xc3\x97"), closeButtonArea, juce::Justification::centred, false);

        // Subtitle explanation
        auto subArea = inner.removeFromTop(18.0f);
        g.setColour(ThemeManager::get(Theme::Role::textDim));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("This setup calls for plugins not installed on this machine:", subArea, juce::Justification::left, true);

        inner.removeFromTop(4.0f);

        // Plugin item list
        for (const auto& item : items) {
            auto itemArea = inner.removeFromTop(22.0f);

            // Bullet icon
            g.setColour(warnCol);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::CharPointer_UTF8("\xe2\x80\xa2"), itemArea.removeFromLeft(14.0f), juce::Justification::centred, false);

            // Plugin Name (bold/accent)
            g.setColour(ThemeManager::get(Theme::Role::text));
            g.setFont(juce::Font(juce::FontOptions(13.5f, juce::Font::bold)));
            float nameWidth = g.getCurrentFont().getStringWidth(item.name) + 8.0f;
            g.drawText(item.name, itemArea.removeFromLeft(nameWidth), juce::Justification::left, true);

            // Location tag (dim italic)
            g.setColour(ThemeManager::get(Theme::Role::textDim));
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::italic)));
            g.drawText("[" + item.location + "]", itemArea, juce::Justification::left, true);
        }

        // Footer / auto-dismiss countdown indicator
        auto footerArea = inner.removeFromTop(24.0f);
        dismissButtonArea = juce::Rectangle<float>(bounds.getRight() - 95.0f, bounds.getBottom() - 30.0f, 80.0f, 22.0f);

        g.setColour(ThemeManager::get(Theme::Role::raised).withAlpha(0.85f));
        g.fillRoundedRectangle(dismissButtonArea, 5.0f);
        g.setColour(ThemeManager::get(Theme::Role::border));
        g.drawRoundedRectangle(dismissButtonArea, 5.0f, 1.0f);

        g.setColour(ThemeManager::get(Theme::Role::text));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Dismiss", dismissButtonArea, juce::Justification::centred, false);

        int remainingSecs = (int)std::ceil((autoDismissMs - elapsedMs) / 1000.0f);
        if (remainingSecs > 0) {
            g.setColour(ThemeManager::get(Theme::Role::textFaint));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText("Silent notice - auto-closing in " + juce::String(remainingSecs) + "s",
                       footerArea.withTrimmedRight(95.0f), juce::Justification::left, true);
        }
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override {
        dismiss();
    }

    void timerCallback() override {
        elapsedMs += 100;
        if (elapsedMs >= autoDismissMs) {
            stopTimer();
            dismiss();
            return;
        }
        repaint();
    }

    void dismiss() {
        stopTimer();
        if (onDismiss) {
            auto cb = std::move(onDismiss);
            onDismiss = nullptr;
            cb();
        }
    }

private:
    std::vector<MissingPluginInfo> items;
    std::function<void()> onDismiss;
    juce::Rectangle<float> closeButtonArea;
    juce::Rectangle<float> dismissButtonArea;
    int elapsedMs = 0;
    static constexpr int autoDismissMs = 12000; // 12 seconds auto-dismiss

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MissingPluginsToast)
};
