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

        titleLabel.setText("OPENRIG RESOURCE & DSP INSPECTOR", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        closeBtn.onClick = [this] {
            if (closeCallback) closeCallback();
        };
        addAndMakeVisible(closeBtn);

        refreshBtn.setButtonText("REFRESH STATS");
        refreshBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::raised));
        refreshBtn.onClick = [this] {
            updateData();
        };
        addAndMakeVisible(refreshBtn);

        infoLabel.setText("Note: In-process VST/VST3 memory is pooled into Host Process RAM. DSP CPU % is measured per-slot in real-time.", juce::dontSendNotification);
        infoLabel.setFont(juce::FontOptions(11.0f));
        infoLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::textDim));
        addAndMakeVisible(infoLabel);

        // Configure Table
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff12161a));
        table.setOutlineThickness(1);

        auto& header = table.getHeader();
        header.addColumn("Component / Slot", 1, 190, 120, 300);
        header.addColumn("Loaded Plugin / Asset", 2, 230, 140, 400);
        header.addColumn("DSP CPU %", 3, 110, 80, 160);
        header.addColumn("Status", 4, 90, 60, 140);
        addAndMakeVisible(table);

        updateData();
        startTimer(1000); // Live refresh every second while modal is open
        setSize(740, 520);
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
        float cpuUsage = 0.0f;
        bool hasCpu = false;
        juce::String status;
        bool isCore = false;
    };

    void updateData() {
        memStats = FanfareLog::getMemoryStats();

        rows.clear();
        float totalSlotDsp = 0.0f;

        // 1. Channel Slots
        std::vector<RowData> slotRows;
        for (int i = 0; i < engine.getNumSlots(); ++i) {
            if (auto* slot = engine.getSlot(i)) {
                RowData r;
                r.componentName = "Slot " + juce::String(i + 1) + " (" + slot->getName() + ")";
                juce::String pName = slot->getPluginName(0);
                if (pName.isEmpty()) pName = "(Empty / Passthrough)";
                r.pluginName = pName;
                r.status = slot->isBypassed() ? "Bypassed" : "Active";
                
                if (!slot->isBypassed()) {
                    r.cpuUsage = slot->getCpuUsage();
                    r.hasCpu = true;
                    totalSlotDsp += r.cpuUsage;
                } else {
                    r.hasCpu = false;
                }
                slotRows.push_back(r);
            }
        }

        // 2. Aux Returns
        std::vector<RowData> auxRows;
        for (int i = 0; i < engine.getNumAuxReturns(); ++i) {
            if (auto* slot = engine.getAuxReturn(i)) {
                RowData r;
                r.componentName = "Aux Return " + juce::String(i + 1);
                juce::String pName = slot->getPluginName(0);
                if (pName.isEmpty()) pName = "(Empty / Passthrough)";
                r.pluginName = pName;
                r.status = slot->isBypassed() ? "Bypassed" : "Active";
                
                if (!slot->isBypassed()) {
                    r.cpuUsage = slot->getCpuUsage();
                    r.hasCpu = true;
                    totalSlotDsp += r.cpuUsage;
                } else {
                    r.hasCpu = false;
                }
                auxRows.push_back(r);
            }
        }

        totalEngineDsp = totalSlotDsp;

        // 0. Process Core Row
        RowData coreRow;
        coreRow.componentName = "OpenRig Process Core";
        coreRow.pluginName = "JUCE Host Engine";
        coreRow.cpuUsage = totalEngineDsp;
        coreRow.hasCpu = true;
        coreRow.status = "Active";
        coreRow.isCore = true;
        rows.push_back(coreRow);

        // Append slots and auxes
        for (const auto& sr : slotRows)
            rows.push_back(sr);
        for (const auto& ar : auxRows)
            rows.push_back(ar);

        table.updateContent();
        repaint();
    }

    int getNumRows() override {
        return (int)rows.size();
    }

    void paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(ThemeManager::get(Theme::Role::accent).withAlpha(0.25f));
        } else if (rowNumber == 0) {
            g.fillAll(juce::Colour(0xff161e27));
        } else if (rowNumber % 2 == 1) {
            g.fillAll(juce::Colours::white.withAlpha(0.02f));
        }
    }

    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/) override {
        if (rowNumber < 0 || rowNumber >= (int)rows.size()) return;

        const auto& r = rows[rowNumber];
        g.setFont(juce::FontOptions(13.0f, r.isCore ? juce::Font::bold : juce::Font::plain));

        if (columnId == 1) {
            g.setColour(r.isCore ? ThemeManager::get(Theme::Role::accent) : ThemeManager::get(Theme::Role::text));
            g.drawText(r.componentName, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        } else if (columnId == 2) {
            g.setColour(r.isCore ? ThemeManager::get(Theme::Role::text) : ThemeManager::get(Theme::Role::textDim));
            g.drawText(r.pluginName, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        } else if (columnId == 3) {
            if (r.hasCpu) {
                float pct = r.cpuUsage * 100.0f;
                juce::Colour cpuColour = ThemeManager::get(Theme::Role::ok);
                if (pct >= 40.0f) {
                    cpuColour = juce::Colours::coral;
                } else if (pct >= 15.0f) {
                    cpuColour = ThemeManager::get(Theme::Role::meterMid);
                }
                g.setColour(cpuColour);

                juce::String cpuText;
                if (pct < 0.05f) {
                    cpuText = "< 0.1%";
                } else {
                    cpuText = juce::String(pct, 1) + "%";
                }
                g.drawText(cpuText, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
            } else {
                g.setColour(ThemeManager::get(Theme::Role::textDim));
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
            return juce::String((double)bytes / (1024.0 * 1024.0 * 1024.0), 1) + " MB";
        if (bytes >= 1024)
            return juce::String(bytes / 1024) + " KB";
        return juce::String(bytes) + " B";
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::panel));
        g.setColour(ThemeManager::get(Theme::Role::border));
        g.drawRect(getLocalBounds(), 1);

        auto bounds = getLocalBounds().reduced(15);
        bounds.removeFromTop(35); // Title space

        // 4 Stat Cards Row
        auto cardRow = bounds.removeFromTop(68);
        int numCards = 4;
        int gap = 8;
        int cardW = (cardRow.getWidth() - (gap * (numCards - 1))) / numCards;

        // Card 1: Process Working Set RAM
        drawStatCard(g, cardRow.removeFromLeft(cardW), "PROCESS RAM (WS)", formatBytes(memStats.workingSetBytes), "Physical RAM in use", ThemeManager::get(Theme::Role::accent));
        cardRow.removeFromLeft(gap);

        // Card 2: Private Commit Charge
        drawStatCard(g, cardRow.removeFromLeft(cardW), "COMMIT CHARGE", formatBytes(memStats.privateCommitBytes), "Virtual Memory Allocated", ThemeManager::get(Theme::Role::raised));
        cardRow.removeFromLeft(gap);

        // Card 3: Total Engine DSP Load
        float dspPct = totalEngineDsp * 100.0f;
        juce::String dspStr = (dspPct < 0.1f) ? "< 0.1%" : (juce::String(dspPct, 1) + "%");
        juce::Colour dspCol = (dspPct >= 40.0f) ? juce::Colours::coral : ((dspPct >= 15.0f) ? ThemeManager::get(Theme::Role::meterMid) : ThemeManager::get(Theme::Role::meterLow));
        drawStatCard(g, cardRow.removeFromLeft(cardW), "TOTAL DSP LOAD", dspStr, "Real-Time Audio Thread", dspCol);
        cardRow.removeFromLeft(gap);

        // Card 4: Free System RAM
        drawStatCard(g, cardRow, "FREE SYSTEM RAM", formatBytes(memStats.freeSystemRamBytes), "Load: " + juce::String(memStats.systemRamLoadPercent) + "% of " + formatBytes(memStats.totalSystemRamBytes), ThemeManager::get(Theme::Role::ok));
    }

    void drawStatCard(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& val, const juce::String& sub, juce::Colour accent) {
        g.setColour(juce::Colour(0xff161b22));
        g.fillRoundedRectangle(area.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff30363d));
        g.drawRoundedRectangle(area.toFloat(), 4.0f, 1.0f);

        g.setColour(ThemeManager::get(Theme::Role::textDim));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(title, area.getX() + 8, area.getY() + 6, area.getWidth() - 16, 14, juce::Justification::left);

        g.setColour(accent);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        g.drawText(val, area.getX() + 8, area.getY() + 22, area.getWidth() - 16, 22, juce::Justification::left);

        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::FontOptions(9.5f));
        g.drawText(sub, area.getX() + 8, area.getY() + 46, area.getWidth() - 16, 14, juce::Justification::left);
    }

    void resized() override {
        closeBtn.setBounds(getWidth() - 35, 10, 25, 25);
        titleLabel.setBounds(15, 10, getWidth() - 150, 25);

        int topY = 125;
        refreshBtn.setBounds(15, topY, 130, 26);
        infoLabel.setBounds(155, topY, getWidth() - 170, 26);
        table.setBounds(15, topY + 35, getWidth() - 30, getHeight() - topY - 45);
    }

private:
    FanfareEngine& engine;
    std::function<void()> closeCallback;
    juce::Label titleLabel;
    juce::Label infoLabel;
    juce::TextButton closeBtn;
    juce::TextButton refreshBtn;
    juce::TableListBox table;

    FanfareLog::ProcessMemoryStats memStats;
    float totalEngineDsp = 0.0f;
    std::vector<RowData> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResourceInspectorModal)
};
