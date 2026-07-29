#pragma once

#include <JuceHeader.h>
#include "FanfareEngine.h"
#include "ThemeManager.h"

class ResourceInspectorModal : public juce::Component,
                              public juce::TableListBoxModel,
                              public juce::Timer {
public:
    ResourceInspectorModal(FanfareEngine& eng, std::function<void()> onClose)
        : engine(eng), closeCallback(onClose)
    {
        setOpaque(true);

        titleLabel.setText("OPENRIG RESOURCE & MEMORY INSPECTOR", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        closeBtn.onClick = [this] {
            if (closeCallback) closeCallback();
        };
        addAndMakeVisible(closeBtn);

        refreshBtn.setButtonText("REFRESH MEMORY");
        refreshBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
        refreshBtn.onClick = [this] {
            updateData();
        };
        addAndMakeVisible(refreshBtn);

        // Configure Table
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff12161a));
        table.setOutlineThickness(1);

        auto& header = table.getHeader();
        header.addColumn("Component / Slot", 1, 180, 100, 300);
        header.addColumn("Loaded Plugin / Asset", 2, 220, 120, 400);
        header.addColumn("Est. RAM Footprint", 3, 130, 90, 200);
        header.addColumn("Status", 4, 90, 60, 150);
        addAndMakeVisible(table);

        updateData();
        startTimer(1000); // Live refresh every second
        setSize(680, 480);
    }

    ~ResourceInspectorModal() override {
        stopTimer();
    }

    void timerCallback() override {
        updateData();
    }

    struct RowData {
        juce::String componentName;
        juce::String pluginName;
        size_t ramBytes = 0;
        juce::String status;
        bool isHeader = false;
    };

    void updateData() {
        memStats = FanfareLog::getMemoryStats();

        rows.clear();

        // 1. Process Core
        RowData coreRow;
        coreRow.componentName = "OpenRig Process Core";
        coreRow.pluginName = "JUCE Host Engine";
        coreRow.ramBytes = memStats.workingSetBytes;
        coreRow.status = "Active";
        rows.push_back(coreRow);

        // 2. Channel Slots
        for (int i = 0; i < engine.getNumSlots(); ++i) {
            if (auto* slot = engine.getSlot(i)) {
                RowData r;
                r.componentName = "Slot " + juce::String(i + 1) + " (" + slot->getName() + ")";
                juce::String pName = slot->getPluginName(0);
                if (pName.isEmpty()) pName = "(Empty / Passthrough)";
                r.pluginName = pName;
                r.ramBytes = slot->getEstimatedRamBytes();
                r.status = slot->isBypassed() ? "Bypassed" : "Active";
                rows.push_back(r);
            }
        }

        // 3. Aux Returns
        for (int i = 0; i < engine.getNumAuxReturns(); ++i) {
            if (auto* slot = engine.getAuxReturn(i)) {
                RowData r;
                r.componentName = "Aux Return " + juce::String(i + 1);
                juce::String pName = slot->getPluginName(0);
                if (pName.isEmpty()) pName = "(Empty / Passthrough)";
                r.pluginName = pName;
                r.ramBytes = slot->getEstimatedRamBytes();
                r.status = "Active";
                rows.push_back(r);
            }
        }

        table.updateContent();
        repaint();
    }

    int getNumRows() override {
        return (int)rows.size();
    }

    void paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(ThemeManager::get(Theme::Role::accent).withAlpha(0.25f));
        } else if (rowNumber % 2 == 1) {
            g.fillAll(juce::Colours::white.withAlpha(0.02f));
        }
    }

    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/) override {
        if (rowNumber < 0 || rowNumber >= (int)rows.size()) return;

        const auto& r = rows[rowNumber];
        g.setFont(juce::FontOptions(13.0f, rowNumber == 0 ? juce::Font::bold : juce::Font::plain));

        if (columnId == 1) {
            g.setColour(rowNumber == 0 ? ThemeManager::get(Theme::Role::accent) : ThemeManager::get(Theme::Role::text));
            g.drawText(r.componentName, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        } else if (columnId == 2) {
            g.setColour(ThemeManager::get(Theme::Role::textDim));
            g.drawText(r.pluginName, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        } else if (columnId == 3) {
            g.setColour(juce::Colours::cyan);
            if (r.ramBytes > 0) {
                g.drawText(formatBytes(r.ramBytes), 8, 0, width - 16, height, juce::Justification::centredLeft, true);
            } else {
                g.drawText("--", 8, 0, width - 16, height, juce::Justification::centredLeft, true);
            }
        } else if (columnId == 4) {
            g.setColour(r.status == "Active" ? ThemeManager::get(Theme::Role::ok) : juce::Colours::orange);
            g.drawText(r.status, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        }
    }

    static juce::String formatBytes(size_t bytes) {
        if (bytes >= 1024 * 1024 * 1024)
            return juce::String((double)bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
        if (bytes >= 1024 * 1024)
            return juce::String((double)bytes / (1024.0 * 1024.0), 1) + " MB";
        if (bytes >= 1024)
            return juce::String(bytes / 1024) + " KB";
        return juce::String(bytes) + " B";
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::panel));
        g.setColour(ThemeManager::get(Theme::Role::border));
        g.drawRect(getLocalBounds(), 1);

        // Stat Card 1: App Process RAM
        auto bounds = getLocalBounds().reduced(15);
        bounds.removeFromTop(35); // Title space

        auto cardRow = bounds.removeFromTop(65);
        int cardW = (cardRow.getWidth() - 20) / 3;

        drawStatCard(g, cardRow.removeFromLeft(cardW), "OPENRIG PROCESS RAM", formatBytes(memStats.workingSetBytes), "Working Set RAM", ThemeManager::get(Theme::Role::accent));
        cardRow.removeFromLeft(10);
        drawStatCard(g, cardRow.removeFromLeft(cardW), "SYSTEM RAM LOAD", juce::String(memStats.systemRamLoadPercent) + "%", formatBytes(memStats.totalSystemRamBytes - memStats.freeSystemRamBytes) + " / " + formatBytes(memStats.totalSystemRamBytes), ThemeManager::get(Theme::Role::meterMid));
        cardRow.removeFromLeft(10);
        drawStatCard(g, cardRow, "FREE SYSTEM RAM", formatBytes(memStats.freeSystemRamBytes), "Available System Memory", ThemeManager::get(Theme::Role::ok));
    }

    void drawStatCard(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& val, const juce::String& sub, juce::Colour accent) {
        g.setColour(juce::Colour(0xff161b22));
        g.fillRoundedRectangle(area.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff30363d));
        g.drawRoundedRectangle(area.toFloat(), 4.0f, 1.0f);

        g.setColour(ThemeManager::get(Theme::Role::textDim));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(title, area.getX() + 8, area.getY() + 6, area.getWidth() - 16, 14, juce::Justification::left);

        g.setColour(accent);
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(val, area.getX() + 8, area.getY() + 22, area.getWidth() - 16, 22, juce::Justification::left);

        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(sub, area.getX() + 8, area.getY() + 45, area.getWidth() - 16, 14, juce::Justification::left);
    }

    void resized() override {
        closeBtn.setBounds(getWidth() - 35, 10, 25, 25);
        titleLabel.setBounds(15, 10, getWidth() - 150, 25);

        int topY = 125;
        refreshBtn.setBounds(15, topY, 130, 26);
        table.setBounds(15, topY + 35, getWidth() - 30, getHeight() - topY - 50);
    }

private:
    FanfareEngine& engine;
    std::function<void()> closeCallback;
    juce::Label titleLabel;
    juce::TextButton closeBtn;
    juce::TextButton refreshBtn;
    juce::TableListBox table;

    FanfareLog::ProcessMemoryStats memStats;
    std::vector<RowData> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResourceInspectorModal)
};
