#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"
#include "FanfareEngine.h"

/**
 * Engine & CPU Optimization Modal
 * Provides plain-English, musician-friendly tuning controls for core affinity,
 * hardware DPC shielding, Windows 11 EcoQoS, and parallel/sequential multiprocessing.
 */
class EngineTuningModal : public juce::Component {
public:
    struct Settings {
        bool pCoresOnly = true;
        bool shelterCpu0 = true;
        bool preventPowerThrottling = true;
        bool parallelProcessing = true;
    };

    using SaveCallback = std::function<void(const Settings&)>;

    EngineTuningModal(Settings initialSettings, SaveCallback onSave, std::function<void()> onClose)
        : currentSettings(initialSettings), saveCallback(std::move(onSave)), closeCallback(std::move(onClose))
    {
        setOpaque(true);

        titleLabel.setText("ENGINE & CPU OPTIMIZATION", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::danger));
        closeBtn.onClick = [this] {
            if (closeCallback) closeCallback();
        };
        addAndMakeVisible(closeBtn);

        // Viewport for clean scrolling on smaller laptop displays
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&contentComp, false);
        viewport.setScrollBarsShown(true, false);

        setupControls();
        refreshUIFromSettings();

        applyBtn.setButtonText("APPLY & SAVE SETTINGS");
        applyBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::ok));
        applyBtn.onClick = [this] {
            readSettingsFromUI();
            if (saveCallback)
                saveCallback(currentSettings);
            if (closeCallback)
                closeCallback();
        };
        addAndMakeVisible(applyBtn);

        setSize(640, 560);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::panel).darker(0.35f));

        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(ThemeManager::get(Theme::Role::border));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16, 12);
        auto topRow = bounds.removeFromTop(32);
        closeBtn.setBounds(topRow.removeFromRight(30).withHeight(26));
        titleLabel.setBounds(topRow);

        applyBtn.setBounds(bounds.removeFromBottom(36).reduced(40, 0));
        bounds.removeFromBottom(10);

        viewport.setBounds(bounds);
        layoutContent();
    }

private:
    class ContentComponent : public juce::Component {
    public:
        void paint(juce::Graphics& g) override {
            // Background cards for each section
            for (const auto& r : cardBounds) {
                g.setColour(ThemeManager::get(Theme::Role::panel).withAlpha(0.6f));
                g.fillRoundedRectangle(r.toFloat(), 6.0f);
                g.setColour(ThemeManager::get(Theme::Role::border).withAlpha(0.4f));
                g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);
            }
        }
        std::vector<juce::Rectangle<int>> cardBounds;
    };

    void setupControls() {
        auto setupRadioGroup = [](juce::ToggleButton& b, int groupId) {
            b.setRadioGroupId(groupId);
            b.setColour(juce::ToggleButton::textColourId, ThemeManager::get(Theme::Role::text));
            b.setColour(juce::ToggleButton::tickColourId, ThemeManager::get(Theme::Role::accent));
        };

        auto configureHelpLabel = [](juce::Label& lbl) {
            lbl.setFont(juce::FontOptions(12.0f));
            lbl.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::textDim));
            lbl.setJustificationType(juce::Justification::topLeft);
        };

        // Section 0: AC Power Supply Advisory
        contentComp.addAndMakeVisible(acWarningTitleLabel);
        acWarningTitleLabel.setText("STAGE ADVISORY: ALWAYS RUN ON AC POWER SUPPLY", juce::dontSendNotification);
        acWarningTitleLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        acWarningTitleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::warn));

        contentComp.addAndMakeVisible(acWarningBodyLabel);
        acWarningBodyLabel.setText(
            "Always keep your laptop plugged into mains power during rehearsals and gigs. When running on battery, Windows and the laptop motherboard aggressively downclock the CPU (often cutting clock frequencies by 50%+), sleep USB ports, and trigger frequent battery polling interrupts. No software optimization can prevent audio dropouts if the laptop enters battery-saving power states.",
            juce::dontSendNotification);
        configureHelpLabel(acWarningBodyLabel);

        // Section 1: CPU Core Assignment
        contentComp.addAndMakeVisible(coreSectionLabel);
        coreSectionLabel.setText("1. CPU CORE ASSIGNMENT (HYBRID INTEL / AMD LAPTOPS)", juce::dontSendNotification);
        coreSectionLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        coreSectionLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));

        setupRadioGroup(pCoresOnlyBtn, 101);
        pCoresOnlyBtn.setButtonText("Auto: Fast Performance Cores Only (Recommended)");
        contentComp.addAndMakeVisible(pCoresOnlyBtn);

        setupRadioGroup(allCoresBtn, 101);
        allCoresBtn.setButtonText("Use All Cores (Desktop Workstations / Uniform CPUs)");
        contentComp.addAndMakeVisible(allCoresBtn);

        coreHelpLabel.setText(
            "What it does: Modern laptop processors (Intel 12th-14th Gen / Core Ultra) split into fast Performance cores and Efficiency cores. "
            "Auto pins real-time audio & DSP workers exclusively to fast Performance cores while offloading UI graphics, disk streaming, and background song preloading onto Efficiency cores so they never collide.\n"
            "* When to use: Leave on Auto for laptops. Eliminates sudden dropouts where the CPU meter appears low.\n"
            "* What it CANNOT fix: Won't help if your audio buffer is too tiny for a giant 20-plugin rig.",
            juce::dontSendNotification);
        configureHelpLabel(coreHelpLabel);
        contentComp.addAndMakeVisible(coreHelpLabel);

        // Section 2: Shelter CPU 0
        contentComp.addAndMakeVisible(shelterSectionLabel);
        shelterSectionLabel.setText("2. SHELTER CPU 0 (HARDWARE INTERRUPT SHIELD)", juce::dontSendNotification);
        shelterSectionLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        shelterSectionLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));

        shelterToggle.setButtonText("Shelter CPU 0 (Shield audio from Wi-Fi & USB driver spikes - Recommended)");
        shelterToggle.setColour(juce::ToggleButton::textColourId, ThemeManager::get(Theme::Role::text));
        shelterToggle.setColour(juce::ToggleButton::tickColourId, ThemeManager::get(Theme::Role::accent));
        contentComp.addAndMakeVisible(shelterToggle);

        shelterHelpLabel.setText(
            "Why protect CPU Core 0? By architectural default in Windows, nearly all motherboard hardware interrupts (IRQs and DPCs)—including USB host controllers, ACPI battery/thermal sensors, trackpads, and Wi-Fi adapters—are routed directly to Core 0 (the Bootstrap Processor). When a hardware interrupt fires, the CPU immediately pauses running software to service it. If real-time audio runs on Core 0, an interrupt burst delays the audio callback by several milliseconds, causing clicks, glitches, and buffer underruns even when total CPU load is very low. Sheltering reserves Core 0 exclusively for background system interrupts, leaving clean Performance cores for uninterrupted audio DSP.\n"
            "* When to use: Keep ON for all live gigs to shield audio from hardware interrupt spikes.\n"
            "* What it CANNOT fix: Won't fix audio dropouts from a loose USB cable or failing interface hardware.",
            juce::dontSendNotification);
        configureHelpLabel(shelterHelpLabel);
        contentComp.addAndMakeVisible(shelterHelpLabel);

        // Section 3: Windows Power Throttling
        contentComp.addAndMakeVisible(powerSectionLabel);
        powerSectionLabel.setText("3. WINDOWS 11 POWER THROTTLING (EcoQoS)", juce::dontSendNotification);
        powerSectionLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        powerSectionLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));

        powerThrottlingToggle.setButtonText("Prevent Windows 11 Power Throttling (HighQoS Unlocked - Recommended)");
        powerThrottlingToggle.setColour(juce::ToggleButton::textColourId, ThemeManager::get(Theme::Role::text));
        powerThrottlingToggle.setColour(juce::ToggleButton::tickColourId, ThemeManager::get(Theme::Role::accent));
        contentComp.addAndMakeVisible(powerThrottlingToggle);

        powerHelpLabel.setText(
            "What it does: Windows 11 automatically flags background or moderate-load audio threads for 'Efficiency Mode' to conserve power, "
            "cutting CPU clock speeds in half. This forces Windows to keep full execution frequency unlocked.\n"
            "* When to use: Keep ON so CPU clock speeds never downclock while waiting between songs.\n"
            "* Note: Remember to keep AC power connected so the laptop hardware controller doesn't override this.\n"
            "* What it CANNOT fix: Does not overclock your PC or stop overheating if air vents are blocked.",
            juce::dontSendNotification);
        configureHelpLabel(powerHelpLabel);
        contentComp.addAndMakeVisible(powerHelpLabel);

        // Section 4: Processing Pipeline (Parallel vs Sequential)
        contentComp.addAndMakeVisible(pipeSectionLabel);
        pipeSectionLabel.setText("4. DSP PROCESSING PIPELINE (PARALLEL VS SEQUENTIAL)", juce::dontSendNotification);
        pipeSectionLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        pipeSectionLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));

        setupRadioGroup(parallelBtn, 102);
        parallelBtn.setButtonText("Parallel Multi-Threading (Recommended - Distributes VSTs across cores)");
        contentComp.addAndMakeVisible(parallelBtn);

        setupRadioGroup(sequentialBtn, 102);
        sequentialBtn.setButtonText("Sequential Inline (Single-core, zero thread-scheduling overhead)");
        contentComp.addAndMakeVisible(sequentialBtn);

        pipeHelpLabel.setText(
            "What it does: Parallel distributes active rack slots across dedicated worker cores simultaneously, "
            "essential for heavy 6+ instrument rigs. Sequential evaluates all plugins one-by-one on one core.\n"
            "* When to use Parallel: Default for modern multi-core rigs running heavy synths & Kontakt.\n"
            "* When to use Sequential: Low-spec dual-core PCs or very simple setups (1-3 plugins).",
            juce::dontSendNotification);
        configureHelpLabel(pipeHelpLabel);
        contentComp.addAndMakeVisible(pipeHelpLabel);

        layoutContent();
    }

    void layoutContent() {
        contentComp.cardBounds.clear();
        int y = 4;
        int w = juce::jmax(540, viewport.getMaximumVisibleWidth() - 24);
        int cardMargin = 12;

        auto addCard = [&](int height) -> juce::Rectangle<int> {
            juce::Rectangle<int> card(cardMargin, y, w, height);
            contentComp.cardBounds.push_back(card);
            y += height + 10;
            return card.reduced(10, 8);
        };

        // Section 0: AC Power Warning Card
        {
            auto inner = addCard(95);
            acWarningTitleLabel.setBounds(inner.removeFromTop(22));
            acWarningBodyLabel.setBounds(inner);
        }

        // Card 1: CPU Core Assignment
        {
            auto inner = addCard(155);
            coreSectionLabel.setBounds(inner.removeFromTop(22));
            pCoresOnlyBtn.setBounds(inner.removeFromTop(24));
            allCoresBtn.setBounds(inner.removeFromTop(24));
            coreHelpLabel.setBounds(inner);
        }

        // Card 2: Shelter CPU 0 (expanded explanation)
        {
            auto inner = addCard(180);
            shelterSectionLabel.setBounds(inner.removeFromTop(22));
            shelterToggle.setBounds(inner.removeFromTop(24));
            shelterHelpLabel.setBounds(inner);
        }

        // Card 3: Windows Power Throttling
        {
            auto inner = addCard(160);
            powerSectionLabel.setBounds(inner.removeFromTop(22));
            powerThrottlingToggle.setBounds(inner.removeFromTop(24));
            powerHelpLabel.setBounds(inner);
        }

        // Card 4: DSP Processing Pipeline
        {
            auto inner = addCard(155);
            pipeSectionLabel.setBounds(inner.removeFromTop(22));
            parallelBtn.setBounds(inner.removeFromTop(24));
            sequentialBtn.setBounds(inner.removeFromTop(24));
            pipeHelpLabel.setBounds(inner);
        }

        contentComp.setBounds(0, 0, viewport.getMaximumVisibleWidth(), y + 16);
    }

    void refreshUIFromSettings() {
        pCoresOnlyBtn.setToggleState(currentSettings.pCoresOnly, juce::dontSendNotification);
        allCoresBtn.setToggleState(!currentSettings.pCoresOnly, juce::dontSendNotification);

        shelterToggle.setToggleState(currentSettings.shelterCpu0, juce::dontSendNotification);
        powerThrottlingToggle.setToggleState(currentSettings.preventPowerThrottling, juce::dontSendNotification);

        parallelBtn.setToggleState(currentSettings.parallelProcessing, juce::dontSendNotification);
        sequentialBtn.setToggleState(!currentSettings.parallelProcessing, juce::dontSendNotification);
    }

    void readSettingsFromUI() {
        currentSettings.pCoresOnly = pCoresOnlyBtn.getToggleState();
        currentSettings.shelterCpu0 = shelterToggle.getToggleState();
        currentSettings.preventPowerThrottling = powerThrottlingToggle.getToggleState();
        currentSettings.parallelProcessing = parallelBtn.getToggleState();
    }

    Settings currentSettings;
    SaveCallback saveCallback;
    std::function<void()> closeCallback;

    juce::Label titleLabel;
    juce::TextButton closeBtn{"X"};
    juce::TextButton applyBtn;

    juce::Viewport viewport;
    ContentComponent contentComp;

    // Controls
    juce::Label acWarningTitleLabel, acWarningBodyLabel;
    juce::Label coreSectionLabel, shelterSectionLabel, powerSectionLabel, pipeSectionLabel;
    juce::ToggleButton pCoresOnlyBtn, allCoresBtn;
    juce::ToggleButton shelterToggle;
    juce::ToggleButton powerThrottlingToggle;
    juce::ToggleButton parallelBtn, sequentialBtn;

    juce::Label coreHelpLabel, shelterHelpLabel, powerHelpLabel, pipeHelpLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineTuningModal)
};

