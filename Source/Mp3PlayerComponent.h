#pragma once

#include <JuceHeader.h>
#include "Mp3PlayerProcessor.h"
#include "ThemeManager.h"
#include "BoutiqueLookAndFeel.h"
#include "ChannelStripComponent.h"
#include "YoutubeDownloadManager.h"

// Preset Bank Button with explicit Right-Click Popup Menu Support
class BankButton : public juce::TextButton {
public:
    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || e.mods.isRightButtonDown()) {
            if (onRightClick) onRightClick();
            return;
        }
        juce::TextButton::mouseDown(e);
    }
};

class Mp3PlayerComponent : public juce::Component,
                           public juce::Timer,
                           public juce::ListBoxModel,
                           public juce::FileDragAndDropTarget {
public:
    Mp3PlayerComponent(Mp3PlayerProcessor& proc, std::function<void()> onClose)
        : processor(proc), closeCallback(onClose),
          levelerMeter(processor.getLevelerReference())
    {
        setOpaque(true);
        setWantsKeyboardFocus(true);

        // Header Title & Close
        titleLabel.setText("DJ PLAYLIST & DUAL-DECK PLAYER - Slot 11", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        closeBtn.onClick = [this] {
            if (closeCallback) closeCallback();
        };
        addAndMakeVisible(closeBtn);

        // 1. Preset Playlist Banks (1..6)
        for (int i = 0; i < 6; ++i) {
            auto btn = std::make_unique<BankButton>();
            int bankIdx = i;
            btn->setButtonText(getBankButtonLabel(bankIdx));
            btn->setTooltip("Left-Click: Load & Play Bank " + juce::String(bankIdx + 1) + "\nRight-Click: Assign Music Folder / Rename");
            btn->onClick = [this, bankIdx] {
                processor.loadBank(bankIdx, true);
                updateBankButtonsState();
                listBox.updateContent();
                updateTrackUI();
            };
            btn->onRightClick = [this, bankIdx] {
                showBankContextMenu(bankIdx);
            };
            addAndMakeVisible(*btn);
            bankButtons.push_back(std::move(btn));
        }

        // 2. DECK A Controls
        deckALabel.setText("DECK A: Idle", juce::dontSendNotification);
        deckALabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        deckALabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(deckALabel);

        deckABpmLabel.setText("-- BPM", juce::dontSendNotification);
        deckABpmLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        deckABpmLabel.setColour(juce::Label::textColourId, juce::Colours::cyan.brighter(0.3f));
        deckABpmLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(deckABpmLabel);

        deckALoadBtn.setButtonText("Load to A...");
        deckALoadBtn.setTooltip("Select audio file to load into Deck A");
        deckALoadBtn.onClick = [this] { chooseFileForDeck(0); };
        addAndMakeVisible(deckALoadBtn);

        deckATrackLabel.setText("No file loaded in Deck A", juce::dontSendNotification);
        deckATrackLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        deckATrackLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(deckATrackLabel);

        deckATimeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        deckATimeLabel.setFont(juce::FontOptions(11.0f));
        deckATimeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(deckATimeLabel);

        deckAPosSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        deckAPosSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        deckAPosSlider.setRange(0.0, 1.0, 0.001);
        deckAPosSlider.onValueChange = [this] {
            if (isScrubbingA && processor.getDeckLength(0) > 0.0) {
                processor.setDeckPosition(0, deckAPosSlider.getValue() * processor.getDeckLength(0));
            }
        };
        deckAPosSlider.onDragStart = [this] { isScrubbingA = true; };
        deckAPosSlider.onDragEnd = [this] { isScrubbingA = false; };
        addAndMakeVisible(deckAPosSlider);

        deckASetInBtn.setButtonText("SET IN");
        deckASetInBtn.setTooltip("Set Cue-In point to current playhead (skips intro chatter/silence)");
        deckASetInBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::teal.darker(0.5f));
        deckASetInBtn.onClick = [this] { processor.setDeckCueIn(0); updateTrackUI(); };
        addAndMakeVisible(deckASetInBtn);

        deckASetOutBtn.setButtonText("SET OUT");
        deckASetOutBtn.setTooltip("Set Cue-Out point to current playhead (triggers Auto-DJ early crossfade)");
        deckASetOutBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::maroon);
        deckASetOutBtn.onClick = [this] { processor.setDeckCueOut(0); updateTrackUI(); };
        addAndMakeVisible(deckASetOutBtn);

        deckAClrCueBtn.setButtonText("CLR");
        deckAClrCueBtn.setTooltip("Clear In/Out Cue points for this track");
        deckAClrCueBtn.onClick = [this] { processor.clearDeckCues(0); updateTrackUI(); };
        addAndMakeVisible(deckAClrCueBtn);

        deckAPlayBtn.setButtonText("> PLAY");
        deckAPlayBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::ok));
        deckAPlayBtn.onClick = [this] { processor.playDeck(0); updateTrackUI(); };
        addAndMakeVisible(deckAPlayBtn);

        deckAPauseBtn.setButtonText("|| PAUSE");
        deckAPauseBtn.onClick = [this] { processor.pauseDeck(0); updateTrackUI(); };
        addAndMakeVisible(deckAPauseBtn);

        deckAStopBtn.setButtonText("[] STOP");
        deckAStopBtn.onClick = [this] { processor.stopDeck(0); updateTrackUI(); };
        addAndMakeVisible(deckAStopBtn);

        deckASyncBtn.setButtonText("SYNC TO B");
        deckASyncBtn.setTooltip("Lock Deck A tempo to Deck B");
        deckASyncBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::cyan.darker(0.6f));
        deckASyncBtn.onClick = [this] {
            processor.syncDeckToOther(0);
            updateTrackUI();
        };
        addAndMakeVisible(deckASyncBtn);

        // 3. DECK B Controls
        deckBLabel.setText("DECK B: Idle", juce::dontSendNotification);
        deckBLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        deckBLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        addAndMakeVisible(deckBLabel);

        deckBBpmLabel.setText("-- BPM", juce::dontSendNotification);
        deckBBpmLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        deckBBpmLabel.setColour(juce::Label::textColourId, juce::Colours::orange.brighter(0.3f));
        deckBBpmLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(deckBBpmLabel);

        deckBLoadBtn.setButtonText("Load to B...");
        deckBLoadBtn.setTooltip("Select audio file to load into Deck B");
        deckBLoadBtn.onClick = [this] { chooseFileForDeck(1); };
        addAndMakeVisible(deckBLoadBtn);

        deckBTrackLabel.setText("No file loaded in Deck B", juce::dontSendNotification);
        deckBTrackLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        deckBTrackLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(deckBTrackLabel);

        deckBTimeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        deckBTimeLabel.setFont(juce::FontOptions(11.0f));
        deckBTimeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(deckBTimeLabel);

        deckBPosSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        deckBPosSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        deckBPosSlider.setRange(0.0, 1.0, 0.001);
        deckBPosSlider.onValueChange = [this] {
            if (isScrubbingB && processor.getDeckLength(1) > 0.0) {
                processor.setDeckPosition(1, deckBPosSlider.getValue() * processor.getDeckLength(1));
            }
        };
        deckBPosSlider.onDragStart = [this] { isScrubbingB = true; };
        deckBPosSlider.onDragEnd = [this] { isScrubbingB = false; };
        addAndMakeVisible(deckBPosSlider);

        deckBSetInBtn.setButtonText("SET IN");
        deckBSetInBtn.setTooltip("Set Cue-In point to current playhead (skips intro chatter/silence)");
        deckBSetInBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::teal.darker(0.5f));
        deckBSetInBtn.onClick = [this] { processor.setDeckCueIn(1); updateTrackUI(); };
        addAndMakeVisible(deckBSetInBtn);

        deckBSetOutBtn.setButtonText("SET OUT");
        deckBSetOutBtn.setTooltip("Set Cue-Out point to current playhead (triggers Auto-DJ early crossfade)");
        deckBSetOutBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::maroon);
        deckBSetOutBtn.onClick = [this] { processor.setDeckCueOut(1); updateTrackUI(); };
        addAndMakeVisible(deckBSetOutBtn);

        deckBClrCueBtn.setButtonText("CLR");
        deckBClrCueBtn.setTooltip("Clear In/Out Cue points for this track");
        deckBClrCueBtn.onClick = [this] { processor.clearDeckCues(1); updateTrackUI(); };
        addAndMakeVisible(deckBClrCueBtn);

        deckBPlayBtn.setButtonText("> PLAY");
        deckBPlayBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::ok));
        deckBPlayBtn.onClick = [this] { processor.playDeck(1); updateTrackUI(); };
        addAndMakeVisible(deckBPlayBtn);

        deckBPauseBtn.setButtonText("|| PAUSE");
        deckBPauseBtn.onClick = [this] { processor.pauseDeck(1); updateTrackUI(); };
        addAndMakeVisible(deckBPauseBtn);

        deckBStopBtn.setButtonText("[] STOP");
        deckBStopBtn.onClick = [this] { processor.stopDeck(1); updateTrackUI(); };
        addAndMakeVisible(deckBStopBtn);

        deckBSyncBtn.setButtonText("SYNC TO A");
        deckBSyncBtn.setTooltip("Lock Deck B tempo to Deck A");
        deckBSyncBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.darker(0.6f));
        deckBSyncBtn.onClick = [this] {
            processor.syncDeckToOther(1);
            updateTrackUI();
        };
        addAndMakeVisible(deckBSyncBtn);

        // 4. Center Crossfader Section
        fadeToABtn.setButtonText("<< FADE A");
        fadeToABtn.setTooltip("Trigger smooth crossfade to Deck A");
        fadeToABtn.onClick = [this] { processor.triggerCrossfadeToDeck(0); };
        addAndMakeVisible(fadeToABtn);

        crossfaderSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        crossfaderSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        crossfaderSlider.setRange(0.0, 1.0, 0.001);
        crossfaderSlider.setValue(0.0, juce::dontSendNotification);
        crossfaderSlider.setTooltip("DJ Equal-Power Crossfader (Left = Deck A, Right = Deck B)");
        crossfaderSlider.onValueChange = [this] {
            if (isManualCrossfading) {
                processor.setManualCrossfadePos((float)crossfaderSlider.getValue());
            }
        };
        crossfaderSlider.onDragStart = [this] { isManualCrossfading = true; };
        crossfaderSlider.onDragEnd = [this] { isManualCrossfading = false; };
        addAndMakeVisible(crossfaderSlider);

        fadeToBBtn.setButtonText("FADE B >>");
        fadeToBBtn.setTooltip("Trigger smooth crossfade to Deck B");
        fadeToBBtn.onClick = [this] { processor.triggerCrossfadeToDeck(1); };
        addAndMakeVisible(fadeToBBtn);

        // Sync Mode Toggle (Clean Vinyl Resampler vs Key-Lock Time-Stretcher)
        updateSyncModeBtnState();
        syncModeBtn.setTooltip("Left-Click to switch DJ Sync Mode:\n• VINYL (CLEAN): High-fidelity continuous resampling (0 artifacts, punchy kick drums, 100% studio clean)\n• KEY-LOCK: WSOLA time-stretching with pitch preservation");
        syncModeBtn.onClick = [this] {
            auto cur = processor.getSyncMode();
            auto next = (cur == Mp3PlayerProcessor::SyncMode::CleanVinyl) 
                ? Mp3PlayerProcessor::SyncMode::KeyLock 
                : Mp3PlayerProcessor::SyncMode::CleanVinyl;
            processor.setSyncMode(next);
            updateSyncModeBtnState();
        };
        addAndMakeVisible(syncModeBtn);

        // Beat-Sync Toggle
        beatSyncBtn.setButtonText(processor.isBeatSyncEnabled() ? "BEAT-SYNC: ON" : "BEAT-SYNC: OFF");
        beatSyncBtn.setTooltip("Automatically match tempo and quantize downbeats during crossfades");
        beatSyncBtn.setColour(juce::TextButton::buttonColourId, processor.isBeatSyncEnabled() ? juce::Colours::teal.darker(0.3f) : ThemeManager::get(Theme::Role::panel));
        beatSyncBtn.onClick = [this] {
            bool bs = !processor.isBeatSyncEnabled();
            processor.setBeatSyncEnabled(bs);
            beatSyncBtn.setButtonText(bs ? "BEAT-SYNC: ON" : "BEAT-SYNC: OFF");
            beatSyncBtn.setColour(juce::TextButton::buttonColourId, bs ? juce::Colours::teal.darker(0.3f) : ThemeManager::get(Theme::Role::panel));
        };
        addAndMakeVisible(beatSyncBtn);

        // Auto-DJ Toggle
        autoDjBtn.setButtonText(processor.isAutoDjEnabled() ? "AUTO-DJ: ON" : "AUTO-DJ: OFF");
        autoDjBtn.setTooltip("Automatically crossfade to next song 5s before track ends with zero dead air");
        autoDjBtn.setColour(juce::TextButton::buttonColourId, processor.isAutoDjEnabled() ? juce::Colours::magenta.darker(0.3f) : ThemeManager::get(Theme::Role::panel));
        autoDjBtn.onClick = [this] {
            bool adj = !processor.isAutoDjEnabled();
            processor.setAutoDjEnabled(adj);
            autoDjBtn.setButtonText(adj ? "AUTO-DJ: ON" : "AUTO-DJ: OFF");
            autoDjBtn.setColour(juce::TextButton::buttonColourId, adj ? juce::Colours::magenta.darker(0.3f) : ThemeManager::get(Theme::Role::panel));
        };
        addAndMakeVisible(autoDjBtn);

        // Crossfade Duration Selector
        updateCrossfadeBtnText();
        crossfadeBtn.setTooltip("Click to cycle Crossfade Duration (Cut, 2s, 4s, 6s, 8s)");
        crossfadeBtn.onClick = [this] {
            float dur = processor.getCrossfadeDuration();
            if (dur <= 0.1f) dur = 2.0f;
            else if (dur <= 2.1f) dur = 4.0f;
            else if (dur <= 4.1f) dur = 6.0f;
            else if (dur <= 6.1f) dur = 8.0f;
            else dur = 0.0f;
            processor.setCrossfadeDuration(dur);
            updateCrossfadeBtnText();
        };
        addAndMakeVisible(crossfadeBtn);

        // 5. Right Master Volume & Auto-Leveler Column
        levelerBtn.setButtonText(processor.isLevelerEnabled() ? "LEVEL: ON" : "LEVEL: OFF");
        levelerBtn.setTooltip("Auto-normalize track volumes across your playlist");
        levelerBtn.setColour(juce::TextButton::buttonColourId, processor.isLevelerEnabled() ? ThemeManager::get(Theme::Role::ok) : ThemeManager::get(Theme::Role::panel));
        levelerBtn.onClick = [this] {
            bool lev = !processor.isLevelerEnabled();
            processor.setLevelerEnabled(lev);
            levelerBtn.setButtonText(lev ? "LEVEL: ON" : "LEVEL: OFF");
            levelerBtn.setColour(juce::TextButton::buttonColourId, lev ? ThemeManager::get(Theme::Role::ok) : ThemeManager::get(Theme::Role::panel));
        };
        addAndMakeVisible(levelerBtn);
        addAndMakeVisible(levelerMeter);

        volLabel.setText("GAIN", juce::dontSendNotification);
        volLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        volLabel.setJustificationType(juce::Justification::centred);
        volLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(volLabel);

        volSlider.setSliderStyle(juce::Slider::LinearVertical);
        volSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
        volSlider.setRange(0.0, 1.5, 0.01);
        volSlider.setValue(processor.getGain(), juce::dontSendNotification);
        volSlider.setTooltip("Master MP3 Output Gain");
        volSlider.onValueChange = [this] { processor.setGain((float)volSlider.getValue()); };
        addAndMakeVisible(volSlider);

        // 6. Bottom Shared Playlist ListBox
        listBox.setModel(this);
        listBox.setRowHeight(24);
        addAndMakeVisible(listBox);

        // 7. Bottom Action Toolbar
        addFilesBtn.setButtonText("Add Files...");
        addFilesBtn.onClick = [this] { chooseFiles(); };
        addAndMakeVisible(addFilesBtn);

        addFolderBtn.setButtonText("Add Folder...");
        addFolderBtn.onClick = [this] { chooseFolder(); };
        addAndMakeVisible(addFolderBtn);

        moveUpBtn.setButtonText("^ Up");
        moveUpBtn.setTooltip("Move selected song UP (Shortcut: Alt+Up)");
        moveUpBtn.onClick = [this] { moveSelectedUp(); };
        addAndMakeVisible(moveUpBtn);

        moveDownBtn.setButtonText("v Dn");
        moveDownBtn.setTooltip("Move selected song DOWN (Shortcut: Alt+Down)");
        moveDownBtn.onClick = [this] { moveSelectedDown(); };
        addAndMakeVisible(moveDownBtn);

        smartArrangeBtn.setButtonText("SMART ARRANGE ⚡");
        smartArrangeBtn.setTooltip("Intelligently arrange playlist sequence by BPM (Smooth Ramp, Minimal Jump, Energy Wave, Fit Duration)");
        smartArrangeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::deepskyblue.darker(0.5f));
        smartArrangeBtn.onClick = [this] { showSmartArrangeMenu(); };
        addAndMakeVisible(smartArrangeBtn);

        grabYoutubeBtn.setButtonText("Grab YouTube...");
        grabYoutubeBtn.setTooltip("Paste YouTube link, pick destination folder, and convert directly to MP3 192k");
        grabYoutubeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        grabYoutubeBtn.onClick = [this] { promptYoutubeDownload(); };
        addAndMakeVisible(grabYoutubeBtn);

        loadToDeckABtn.setButtonText("Load -> Deck A");
        loadToDeckABtn.setColour(juce::TextButton::buttonColourId, juce::Colours::cyan.darker(0.6f));
        loadToDeckABtn.onClick = [this] {
            int sel = listBox.getSelectedRow();
            if (sel >= 0 && sel < (int)processor.getPlaylist().size()) {
                processor.loadFileToDeck(0, processor.getPlaylist()[sel].file, false);
                updateTrackUI();
            }
        };
        addAndMakeVisible(loadToDeckABtn);

        loadToDeckBBtn.setButtonText("Load -> Deck B");
        loadToDeckBBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.darker(0.6f));
        loadToDeckBBtn.onClick = [this] {
            int sel = listBox.getSelectedRow();
            if (sel >= 0 && sel < (int)processor.getPlaylist().size()) {
                processor.loadFileToDeck(1, processor.getPlaylist()[sel].file, false);
                updateTrackUI();
            }
        };
        addAndMakeVisible(loadToDeckBBtn);

        removeBtn.setButtonText("Remove");
        removeBtn.onClick = [this] {
            int selected = listBox.getSelectedRow();
            if (selected >= 0) {
                processor.removeTrack(selected);
                listBox.updateContent();
                updateTrackUI();
            }
        };
        addAndMakeVisible(removeBtn);

        clearBtn.setButtonText("Clear");
        clearBtn.onClick = [this] {
            processor.clearPlaylist();
            listBox.updateContent();
            updateTrackUI();
        };
        addAndMakeVisible(clearBtn);

        savePlaylistBtn.setButtonText("Save...");
        savePlaylistBtn.onClick = [this] { savePlaylist(); };
        addAndMakeVisible(savePlaylistBtn);

        loadPlaylistBtn.setButtonText("Load...");
        loadPlaylistBtn.onClick = [this] { loadPlaylist(); };
        addAndMakeVisible(loadPlaylistBtn);

        exportBtn.setButtonText("EXPORT 💾");
        exportBtn.setTooltip("Export library metadata (CSV, JSON, Extended M3U, SQLite DB Backup)");
        exportBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::teal.darker(0.5f));
        exportBtn.onClick = [this] { showExportMenu(); };
        addAndMakeVisible(exportBtn);

        updateBankButtonsState();
        updateTrackUI();
        startTimerHz(20);
    }

    ~Mp3PlayerComponent() override {
        stopTimer();
    }

    void updateSyncModeBtnState() {
        bool isVinyl = (processor.getSyncMode() == Mp3PlayerProcessor::SyncMode::CleanVinyl);
        syncModeBtn.setButtonText(isVinyl ? "MODE: VINYL (CLEAN)" : "MODE: KEY-LOCK");
        syncModeBtn.setColour(juce::TextButton::buttonColourId, isVinyl ? juce::Colours::teal.darker(0.3f) : juce::Colours::purple.darker(0.3f));
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getModifiers().isAltDown()) {
            if (key.isKeyCode(juce::KeyPress::upKey)) {
                moveSelectedUp();
                return true;
            }
            if (key.isKeyCode(juce::KeyPress::downKey)) {
                moveSelectedDown();
                return true;
            }
        }
        if (key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey)) {
            int sel = listBox.getSelectedRow();
            if (sel >= 0) {
                processor.removeTrack(sel);
                listBox.updateContent();
                updateTrackUI();
                return true;
            }
        }
        return juce::Component::keyPressed(key);
    }

    void moveSelectedUp() {
        int sel = listBox.getSelectedRow();
        if (sel > 0 && sel < (int)processor.getPlaylist().size()) {
            processor.moveTrack(sel, sel - 1);
            listBox.updateContent();
            listBox.selectRow(sel - 1);
            updateTrackUI();
        }
    }

    void moveSelectedDown() {
        int sel = listBox.getSelectedRow();
        if (sel >= 0 && sel < (int)processor.getPlaylist().size() - 1) {
            processor.moveTrack(sel, sel + 1);
            listBox.updateContent();
            listBox.selectRow(sel + 1);
            updateTrackUI();
        }
    }

    void moveSelectedToTop() {
        int sel = listBox.getSelectedRow();
        if (sel > 0 && sel < (int)processor.getPlaylist().size()) {
            processor.moveTrack(sel, 0);
            listBox.updateContent();
            listBox.selectRow(0);
            updateTrackUI();
        }
    }

    void moveSelectedToBottom() {
        int sel = listBox.getSelectedRow();
        int last = (int)processor.getPlaylist().size() - 1;
        if (sel >= 0 && sel < last) {
            processor.moveTrack(sel, last);
            listBox.updateContent();
            listBox.selectRow(last);
            updateTrackUI();
        }
    }

    void showSmartArrangeMenu() {
        juce::PopupMenu m;
        m.addItem(1, "⚡ Smooth BPM Ramp (Low -> High)");
        m.addItem(2, "⚡ Smooth BPM Ramp (High -> Low)");
        m.addItem(3, "🎯 Minimal Tempo Step (Smooth Flow)");
        m.addItem(4, "🌊 Energy Wave (Build & Peak)");
        m.addSeparator();
        m.addItem(5, "⏱️ Fit Playlist to 30 Minutes");
        m.addItem(6, "⏱️ Fit Playlist to 45 Minutes");
        m.addItem(7, "⏱️ Fit Playlist to 60 Minutes");

        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&smartArrangeBtn), [this](int result) {
            if (result == 1) {
                processor.sortPlaylistByBpm(true);
            } else if (result == 2) {
                processor.sortPlaylistByBpm(false);
            } else if (result == 3) {
                processor.sortPlaylistMinimalDelta(0);
            } else if (result == 4) {
                processor.sortPlaylistEnergyWave();
            } else if (result == 5) {
                processor.fitPlaylistToDuration(30.0);
            } else if (result == 6) {
                processor.fitPlaylistToDuration(45.0);
            } else if (result == 7) {
                processor.fitPlaylistToDuration(60.0);
            }
            listBox.updateContent();
            updateTrackUI();
        });
    }

    void showExportMenu() {
        juce::PopupMenu menu;
        menu.addSectionHeader("EXPORT LIBRARY & BACKUP");
        menu.addItem(1, "📊 Export to CSV (Excel / Spreadsheet)");
        menu.addItem(2, "📄 Export to JSON (Portable Backup)");
        menu.addItem(3, "🎵 Export Extended M3U Playlist (With Cue Points)");
        menu.addItem(4, "🗄️ Backup SQLite Database (.db Snapshot)");
        menu.addSeparator();
        menu.addSectionHeader("IMPORT DATA");
        menu.addItem(5, "📥 Import Library Metadata from CSV...");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportBtn), [this](int result) {
            if (result == 1) {
                // Export CSV
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Export Fanfare Library to CSV...",
                    processor.getLastFolder().getChildFile("fanfare_library_export.csv"),
                    "*.csv"
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file != juce::File()) {
                            if (Fanfare::LibraryDatabase::getInstance().exportToCsv(file)) {
                                auto* alert = new juce::AlertWindow("CSV Export Successful", "Exported " + juce::String(Fanfare::LibraryDatabase::getInstance().getTrackCount()) + " tracks to:\n" + file.getFullPathName(), juce::AlertWindow::InfoIcon);
                                alert->addButton("OK", 1);
                                alert->enterModalState(true, nullptr, true);
                            }
                        }
                    }
                );
            } else if (result == 2) {
                // Export JSON
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Export Fanfare Library to JSON...",
                    processor.getLastFolder().getChildFile("fanfare_library_export.json"),
                    "*.json"
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file != juce::File()) {
                            if (Fanfare::LibraryDatabase::getInstance().exportToJson(file)) {
                                auto* alert = new juce::AlertWindow("JSON Export Successful", "Exported JSON backup to:\n" + file.getFullPathName(), juce::AlertWindow::InfoIcon);
                                alert->addButton("OK", 1);
                                alert->enterModalState(true, nullptr, true);
                            }
                        }
                    }
                );
            } else if (result == 3) {
                // Export Extended M3U
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Export Extended M3U Playlist...",
                    processor.getLastFolder().getChildFile("fanfare_playlist_export.m3u"),
                    "*.m3u"
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file != juce::File()) {
                            if (Fanfare::LibraryDatabase::getInstance().exportToM3u(file)) {
                                auto* alert = new juce::AlertWindow("M3U Export Successful", "Exported Extended M3U playlist to:\n" + file.getFullPathName(), juce::AlertWindow::InfoIcon);
                                alert->addButton("OK", 1);
                                alert->enterModalState(true, nullptr, true);
                            }
                        }
                    }
                );
            } else if (result == 4) {
                // Backup SQLite Database
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Backup SQLite Library Database...",
                    processor.getLastFolder().getChildFile("fanfare_library_backup.db"),
                    "*.db"
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file != juce::File()) {
                            if (Fanfare::LibraryDatabase::getInstance().backupDatabase(file)) {
                                auto* alert = new juce::AlertWindow("Database Backup Successful", "Live SQLite database backup created at:\n" + file.getFullPathName(), juce::AlertWindow::InfoIcon);
                                alert->addButton("OK", 1);
                                alert->enterModalState(true, nullptr, true);
                            }
                        }
                    }
                );
            } else if (result == 5) {
                // Import CSV
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Import Library Metadata from CSV...",
                    processor.getLastFolder(),
                    "*.csv"
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this](const juce::FileChooser& fc) {
                        auto file = fc.getResult();
                        if (file.existsAsFile()) {
                            if (Fanfare::LibraryDatabase::getInstance().importFromCsv(file)) {
                                processor.loadBpmCache();
                                listBox.updateContent();
                                updateTrackUI();
                                auto* alert = new juce::AlertWindow("CSV Import Successful", "Successfully imported library metadata from:\n" + file.getFullPathName(), juce::AlertWindow::InfoIcon);
                                alert->addButton("OK", 1);
                                alert->enterModalState(true, nullptr, true);
                            }
                        }
                    }
                );
            }
        });
    }

    // ListBoxModel Callbacks
    int getNumRows() override {
        return (int)processor.getPlaylist().size();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= (int)processor.getPlaylist().size()) return;

        const auto& playlist = processor.getPlaylist();
        const auto& track = playlist[rowNumber];

        if (rowIsSelected) {
            g.setColour(ThemeManager::get(Theme::Role::accent).withAlpha(0.25f));
            g.fillRoundedRectangle(2.0f, 2.0f, (float)width - 4.0f, (float)height - 4.0f, 4.0f);
        }

        // Deck A or Deck B indicators
        bool isDeckA = (processor.isDeckLoaded(0) && processor.getDeckATrack().file == track.file);
        bool isDeckB = (processor.isDeckLoaded(1) && processor.getDeckBTrack().file == track.file);

        if (isDeckA && isDeckB) {
            g.setColour(juce::Colours::yellow);
        } else if (isDeckA) {
            g.setColour(juce::Colours::cyan);
        } else if (isDeckB) {
            g.setColour(juce::Colours::orange);
        } else {
            g.setColour(ThemeManager::get(Theme::Role::text));
        }

        g.setFont(juce::FontOptions(12.0f, (isDeckA || isDeckB) ? juce::Font::bold : juce::Font::plain));

        juce::String deckTag;
        if (isDeckA) deckTag += "[DECK A] ";
        if (isDeckB) deckTag += "[DECK B] ";

        juce::String text = juce::String(rowNumber + 1) + ". " + deckTag + track.title;
        g.drawText(text, 8, 0, width - 260, height, juce::Justification::centredLeft, true);

        // Cue Trim Indicator Tag
        if (track.hasCues()) {
            juce::String cueTag = "[TRIM: " + formatTime(track.cueInSeconds) + "->" + formatTime(track.getEffectiveEnd()) + "]";
            g.setColour(juce::Colours::teal.brighter(0.4f));
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.drawText(cueTag, width - 255, 0, 105, height, juce::Justification::centredLeft, true);
        }

        // BPM Text & Delta from previous track
        juce::String bpmStr;
        if (track.bpm > 0.0f) {
            bpmStr = juce::String((int)std::round(track.bpm)) + " BPM";
            if (rowNumber > 0 && playlist[rowNumber - 1].bpm > 0.0f) {
                float delta = track.bpm - playlist[rowNumber - 1].bpm;
                if (std::abs(delta) >= 1.0f) {
                    bpmStr += (delta > 0 ? " (+" : " (") + juce::String((int)std::round(delta)) + ")";
                }
            }
        } else {
            bpmStr = "-- BPM";
        }
        g.setColour(ThemeManager::get(Theme::Role::accent).withAlpha(0.85f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(bpmStr, width - 145, 0, 75, height, juce::Justification::centredRight, true);

        // Effective Duration text
        double effDur = track.getEffectiveDuration();
        int mins = (int)(effDur / 60);
        int secs = (int)std::fmod(effDur, 60.0);
        juce::String durStr = juce::String::formatted("%02d:%02d", mins, secs);

        g.setColour(track.hasCues() ? juce::Colours::lightgreen : ThemeManager::get(Theme::Role::textDim));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(durStr, width - 65, 0, 55, height, juce::Justification::centredRight, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (row >= 0 && row < (int)processor.getPlaylist().size()) {
            processor.playTrack(row, true);
            updateTrackUI();
            listBox.repaint();
        }
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || e.mods.isRightButtonDown()) {
            showTrackContextMenu(row);
        }
    }

    void showTrackContextMenu(int trackIdx) {
        if (trackIdx < 0 || trackIdx >= (int)processor.getPlaylist().size()) return;
        const auto& track = processor.getPlaylist()[trackIdx];

        juce::PopupMenu m;
        m.addItem(1, "Load into Deck A");
        m.addItem(2, "Load into Deck B");
        m.addSeparator();
        m.addItem(3, "Play Now (Crossfade)");
        m.addSeparator();
        m.addItem(4, "Move Up (Alt+Up)");
        m.addItem(5, "Move Down (Alt+Down)");
        m.addItem(6, "Move to Top");
        m.addItem(7, "Move to Bottom");
        m.addSeparator();
        m.addItem(8, "Set / Edit Track BPM (" + (track.bpm > 0 ? juce::String((int)std::round(track.bpm)) : "None") + ")...");
        m.addSeparator();
        m.addItem(11, "Set Cue-In from Deck A (" + formatTime(processor.getDeckPosition(0)) + ")");
        m.addItem(12, "Set Cue-Out from Deck A (" + formatTime(processor.getDeckPosition(0)) + ")");
        m.addItem(13, "Set Cue-In from Deck B (" + formatTime(processor.getDeckPosition(1)) + ")");
        m.addItem(14, "Set Cue-Out from Deck B (" + formatTime(processor.getDeckPosition(1)) + ")");
        m.addItem(15, "Clear Cue Points");
        m.addSeparator();
        m.addItem(9, "Reveal File in Windows Explorer");
        m.addItem(10, "Remove from Playlist");

        m.showMenuAsync(juce::PopupMenu::Options(), [this, trackIdx, track](int result) {
            if (result == 1) {
                processor.loadFileToDeck(0, track.file, false);
                updateTrackUI();
                listBox.repaint();
            } else if (result == 2) {
                processor.loadFileToDeck(1, track.file, false);
                updateTrackUI();
                listBox.repaint();
            } else if (result == 3) {
                processor.playTrack(trackIdx, true);
                updateTrackUI();
                listBox.repaint();
            } else if (result == 4) {
                moveSelectedUp();
            } else if (result == 5) {
                moveSelectedDown();
            } else if (result == 6) {
                moveSelectedToTop();
            } else if (result == 7) {
                moveSelectedToBottom();
            } else if (result == 8) {
                promptEditBpm(trackIdx);
            } else if (result == 11) {
                processor.setTrackCueIn(trackIdx, processor.getDeckPosition(0));
                updateTrackUI();
            } else if (result == 12) {
                processor.setTrackCueOut(trackIdx, processor.getDeckPosition(0));
                updateTrackUI();
            } else if (result == 13) {
                processor.setTrackCueIn(trackIdx, processor.getDeckPosition(1));
                updateTrackUI();
            } else if (result == 14) {
                processor.setTrackCueOut(trackIdx, processor.getDeckPosition(1));
                updateTrackUI();
            } else if (result == 15) {
                processor.clearTrackCues(trackIdx);
                updateTrackUI();
            } else if (result == 9) {
                YoutubeDownloadManager::openFolderInExplorer(track.file.getParentDirectory());
            } else if (result == 10) {
                processor.removeTrack(trackIdx);
                listBox.updateContent();
                updateTrackUI();
            }
        });
    }

    void promptEditBpm(int trackIdx) {
        if (trackIdx < 0 || trackIdx >= (int)processor.getPlaylist().size()) return;
        float curBpm = processor.getPlaylist()[trackIdx].bpm;

        auto* alert = new juce::AlertWindow("Edit Track BPM", "Enter exact BPM for: " + processor.getPlaylist()[trackIdx].title, juce::AlertWindow::QuestionIcon);
        alert->addTextEditor("bpm", curBpm > 0.0f ? juce::String(curBpm, 1) : "120.0");
        alert->addButton("Save BPM", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        alert->enterModalState(true, juce::ModalCallbackFunction::create([this, alert, trackIdx](int result) {
            if (result == 1) {
                float bpm = (float)alert->getTextEditorContents("bpm").getDoubleValue();
                if (bpm >= 40.0f && bpm <= 250.0f) {
                    processor.setTrackBpm(trackIdx, bpm);
                    updateTrackUI();
                }
            }
            delete alert;
        }), true);
    }

    // Drag and Drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        for (const auto& f : files) {
            juce::String ext = juce::File(f).getFileExtension().toLowerCase();
            if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".aiff" || ext == ".m4a" || ext == ".aac") {
                return true;
            }
        }
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override {
        for (const auto& f : files) {
            processor.addFile(juce::File(f));
        }
        listBox.updateContent();
        updateTrackUI();
    }

    void showBankContextMenu(int bankIdx) {
        if (bankIdx < 0 || bankIdx >= (int)processor.getBanks().size()) return;

        juce::PopupMenu m;
        m.addItem(1, "Assign Music Folder to Preset " + juce::String(bankIdx + 1) + "...");
        m.addItem(2, "Rename Preset Button (" + processor.getBanks()[bankIdx].name + ")...");
        m.addItem(3, "Open Preset Folder in Windows Explorer");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, bankIdx](int result) {
            if (result == 1) {
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Select Music Folder for " + processor.getBanks()[bankIdx].name + "...",
                    processor.getLastFolder()
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this, bankIdx](const juce::FileChooser& fc) {
                        auto dir = fc.getResult();
                        if (dir.isDirectory()) {
                            processor.assignBankFolder(bankIdx, dir);
                            updateBankButtonsState();
                        }
                    }
                );
            } else if (result == 2) {
                promptRenameBank(bankIdx);
            } else if (result == 3) {
                juce::File folder(processor.getBanks()[bankIdx].folderPath);
                if (folder.isDirectory()) {
                    YoutubeDownloadManager::openFolderInExplorer(folder);
                } else {
                    YoutubeDownloadManager::openFolderInExplorer(processor.getLastFolder());
                }
            }
        });
    }

    void promptRenameBank(int bankIdx) {
        auto* alert = new juce::AlertWindow("Rename Preset Bank", "Enter new name for Preset " + juce::String(bankIdx + 1) + ":", juce::AlertWindow::QuestionIcon);
        alert->addTextEditor("name", processor.getBanks()[bankIdx].name);
        alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        alert->enterModalState(true, juce::ModalCallbackFunction::create([this, alert, bankIdx](int result) {
            if (result == 1) {
                juce::String newName = alert->getTextEditorContents("name").trim();
                if (newName.isNotEmpty()) {
                    juce::File folder(processor.getBanks()[bankIdx].folderPath);
                    processor.assignBankFolder(bankIdx, folder, newName);
                    updateBankButtonsState();
                }
            }
            delete alert;
        }), true);
    }

    void chooseFileForDeck(int deckId) {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Audio File for Deck " + juce::String(deckId == 0 ? "A" : "B") + "...",
            processor.getLastFolder(),
            "*.mp3;*.wav;*.flac;*.ogg;*.aiff;*.m4a;*.aac"
        );
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, deckId](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    processor.loadFileToDeck(deckId, file, false);
                    updateTrackUI();
                    listBox.repaint();
                }
            }
        );
    }

    juce::String getBankButtonLabel(int bankIdx) {
        const auto& banks = processor.getBanks();
        if (bankIdx >= 0 && bankIdx < (int)banks.size()) {
            return juce::String(bankIdx + 1) + ": " + banks[bankIdx].name;
        }
        return "BANK " + juce::String(bankIdx + 1);
    }

    void updateBankButtonsState() {
        int activeBank = processor.getActiveBankIndex();
        for (int i = 0; i < (int)bankButtons.size(); ++i) {
            bankButtons[i]->setButtonText(getBankButtonLabel(i));
            bool isCurrentBank = (i == activeBank);
            bankButtons[i]->setColour(
                juce::TextButton::buttonColourId,
                isCurrentBank ? ThemeManager::get(Theme::Role::accent) : ThemeManager::get(Theme::Role::raised)
            );
        }
    }

    void updateCrossfadeBtnText() {
        float dur = processor.getCrossfadeDuration();
        if (dur <= 0.1f) crossfadeBtn.setButtonText("XFADE: CUT");
        else crossfadeBtn.setButtonText("XFADE: " + juce::String((int)dur) + "s");
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::background));

        // Border
        g.setColour(ThemeManager::get(Theme::Role::accent));
        g.drawRect(getLocalBounds(), 2);

        // Deck A Box Outline (Cyan)
        g.setColour(juce::Colours::cyan.withAlpha(0.25f));
        g.drawRoundedRectangle(deckABounds.toFloat(), 6.0f, 2.0f);
        g.setColour(juce::Colours::cyan.withAlpha(0.04f));
        g.fillRoundedRectangle(deckABounds.toFloat(), 6.0f);

        // Deck B Box Outline (Orange)
        g.setColour(juce::Colours::orange.withAlpha(0.25f));
        g.drawRoundedRectangle(deckBBounds.toFloat(), 6.0f, 2.0f);
        g.setColour(juce::Colours::orange.withAlpha(0.04f));
        g.fillRoundedRectangle(deckBBounds.toFloat(), 6.0f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);

        // Header Row (Title & Close)
        auto header = area.removeFromTop(26);
        closeBtn.setBounds(header.removeFromRight(26));
        titleLabel.setBounds(header);

        area.removeFromTop(6);

        // Preset Playlist Banks Row (6 buttons)
        auto bankRow = area.removeFromTop(26);
        int bankW = bankRow.getWidth() / 6;
        for (int i = 0; i < 6; ++i) {
            bankButtons[i]->setBounds(bankRow.removeFromLeft(bankW).reduced(2));
        }

        area.removeFromTop(8);

        // Master Output Fader Column on Far Right
        auto faderCol = area.removeFromRight(52);
        volLabel.setBounds(faderCol.removeFromTop(18));
        levelerBtn.setBounds(faderCol.removeFromTop(22));
        faderCol.removeFromTop(4);
        levelerMeter.setBounds(faderCol.removeFromTop(50));
        faderCol.removeFromTop(6);
        volSlider.setBounds(faderCol);

        area.removeFromRight(8);

        // Bottom Playlist Action Buttons Row (13 buttons)
        auto btnRow = area.removeFromBottom(26);
        int bw = btnRow.getWidth() / 13;
        addFilesBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        addFolderBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        moveUpBtn.setBounds(btnRow.removeFromLeft(bw - 4).reduced(2));
        moveDownBtn.setBounds(btnRow.removeFromLeft(bw - 4).reduced(2));
        smartArrangeBtn.setBounds(btnRow.removeFromLeft(bw + 12).reduced(2));
        grabYoutubeBtn.setBounds(btnRow.removeFromLeft(bw + 8).reduced(2));
        loadToDeckABtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        loadToDeckBBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        removeBtn.setBounds(btnRow.removeFromLeft(bw - 8).reduced(2));
        clearBtn.setBounds(btnRow.removeFromLeft(bw - 8).reduced(2));
        savePlaylistBtn.setBounds(btnRow.removeFromLeft(bw - 4).reduced(2));
        loadPlaylistBtn.setBounds(btnRow.removeFromLeft(bw - 4).reduced(2));
        exportBtn.setBounds(btnRow.reduced(2));

        area.removeFromBottom(8);

        // Dual Deck Area (Top half: Deck A on Left, Deck B on Right)
        auto decksArea = area.removeFromTop(188);
        int deckWidth = (decksArea.getWidth() - 10) / 2;

        deckABounds = decksArea.removeFromLeft(deckWidth);
        decksArea.removeFromLeft(10);
        deckBBounds = decksArea;

        // Layout Deck A Inside deckABounds
        auto aInner = deckABounds.reduced(8);
        auto aHeader = aInner.removeFromTop(22);
        deckALoadBtn.setBounds(aHeader.removeFromRight(75));
        deckABpmLabel.setBounds(aHeader.removeFromRight(80).reduced(4, 0));
        deckALabel.setBounds(aHeader);

        deckATrackLabel.setBounds(aInner.removeFromTop(18));
        deckATimeLabel.setBounds(aInner.removeFromTop(16));
        deckAPosSlider.setBounds(aInner.removeFromTop(16));
        aInner.removeFromTop(2);

        auto aCueRow = aInner.removeFromTop(20);
        int aCueW = aCueRow.getWidth() / 3;
        deckASetInBtn.setBounds(aCueRow.removeFromLeft(aCueW).reduced(2, 0));
        deckASetOutBtn.setBounds(aCueRow.removeFromLeft(aCueW).reduced(2, 0));
        deckAClrCueBtn.setBounds(aCueRow.reduced(2, 0));
        aInner.removeFromTop(4);

        auto aTransport = aInner.removeFromTop(24);
        int aBtnW = aTransport.getWidth() / 4;
        deckAPlayBtn.setBounds(aTransport.removeFromLeft(aBtnW).reduced(2, 0));
        deckAPauseBtn.setBounds(aTransport.removeFromLeft(aBtnW).reduced(2, 0));
        deckAStopBtn.setBounds(aTransport.removeFromLeft(aBtnW).reduced(2, 0));
        deckASyncBtn.setBounds(aTransport.reduced(2, 0));

        // Layout Deck B Inside deckBBounds
        auto bInner = deckBBounds.reduced(8);
        auto bHeader = bInner.removeFromTop(22);
        deckBLoadBtn.setBounds(bHeader.removeFromRight(75));
        deckBBpmLabel.setBounds(bHeader.removeFromRight(80).reduced(4, 0));
        deckBLabel.setBounds(bHeader);

        deckBTrackLabel.setBounds(bInner.removeFromTop(18));
        deckBTimeLabel.setBounds(bInner.removeFromTop(16));
        deckBPosSlider.setBounds(bInner.removeFromTop(16));
        bInner.removeFromTop(2);

        auto bCueRow = bInner.removeFromTop(20);
        int bCueW = bCueRow.getWidth() / 3;
        deckBSetInBtn.setBounds(bCueRow.removeFromLeft(bCueW).reduced(2, 0));
        deckBSetOutBtn.setBounds(bCueRow.removeFromLeft(bCueW).reduced(2, 0));
        deckBClrCueBtn.setBounds(bCueRow.reduced(2, 0));
        bInner.removeFromTop(4);

        auto bTransport = bInner.removeFromTop(24);
        int bBtnW = bTransport.getWidth() / 4;
        deckBPlayBtn.setBounds(bTransport.removeFromLeft(bBtnW).reduced(2, 0));
        deckBPauseBtn.setBounds(bTransport.removeFromLeft(bBtnW).reduced(2, 0));
        deckBStopBtn.setBounds(bTransport.removeFromLeft(bBtnW).reduced(2, 0));
        deckBSyncBtn.setBounds(bTransport.reduced(2, 0));

        area.removeFromTop(6);

        // Center DJ Crossfader Bar (with Sync Mode, Beat-Sync, Auto-DJ, and Duration)
        auto xfadeRow = area.removeFromTop(28);
        fadeToABtn.setBounds(xfadeRow.removeFromLeft(75).reduced(2));
        fadeToBBtn.setBounds(xfadeRow.removeFromRight(75).reduced(2));
        crossfadeBtn.setBounds(xfadeRow.removeFromRight(80).reduced(2));
        autoDjBtn.setBounds(xfadeRow.removeFromRight(95).reduced(2));
        beatSyncBtn.setBounds(xfadeRow.removeFromRight(100).reduced(2));
        syncModeBtn.setBounds(xfadeRow.removeFromRight(135).reduced(2));
        crossfaderSlider.setBounds(xfadeRow.reduced(6, 2));

        area.removeFromTop(6);

        // Shared Playlist ListBox
        listBox.setBounds(area);
    }

    void timerCallback() override {
        // Update Title with total effective time
        titleLabel.setText("FANFARE DUAL-DECK DJ & BREAK MUSIC   |   " + juce::String((int)processor.getPlaylist().size()) + " Tracks (" + formatTime(processor.getTotalPlaylistEffectiveDuration()) + ")", juce::dontSendNotification);

        // 1. Deck A Updates
        bool isDeckAPlaying = processor.isDeckPlaying(0);
        bool isDeckALoaded = processor.isDeckLoaded(0);
        float gainA = processor.getDeckAGain();
        float bpmA = processor.getDeckBpm(0);
        float ratioA = processor.getDeckTempoRatio(0);

        if (isDeckAPlaying) {
            deckALabel.setText("DECK A (LIVE " + juce::String((int)(gainA * 100)) + "%)", juce::dontSendNotification);
            deckALabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        } else if (isDeckALoaded) {
            deckALabel.setText("DECK A (PAUSED)", juce::dontSendNotification);
            deckALabel.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
        } else {
            deckALabel.setText("DECK A (EMPTY)", juce::dontSendNotification);
            deckALabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }

        if (bpmA > 0.0f) {
            if (std::abs(ratioA - 1.0f) > 0.005f) {
                float effectiveBpm = bpmA * ratioA;
                deckABpmLabel.setText(juce::String(effectiveBpm, 1) + " BPM (" + (ratioA >= 1.0f ? "+" : "") + juce::String((int)std::round((ratioA - 1.0f) * 100.0f)) + "%)", juce::dontSendNotification);
                deckABpmLabel.setColour(juce::Label::textColourId, juce::Colours::teal);
            } else {
                deckABpmLabel.setText(juce::String(bpmA, 1) + " BPM", juce::dontSendNotification);
                deckABpmLabel.setColour(juce::Label::textColourId, juce::Colours::cyan.brighter(0.3f));
            }
        } else {
            deckABpmLabel.setText("-- BPM", juce::dontSendNotification);
            deckABpmLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }

        if (isDeckALoaded) {
            deckATrackLabel.setText(processor.getDeckATrack().title, juce::dontSendNotification);
            double posA = processor.getDeckPosition(0);
            double lenA = processor.getDeckLength(0);
            const auto& trkA = processor.getDeckATrack();
            juce::String timeStr = formatTime(posA) + " / " + formatTime(lenA) + " (-" + formatTime(juce::jmax(0.0, lenA - posA)) + ")";
            if (trkA.hasCues()) {
                timeStr += " [IN: " + formatTime(trkA.cueInSeconds) + " | OUT: " + formatTime(trkA.getEffectiveEnd()) + "]";
            }
            deckATimeLabel.setText(timeStr, juce::dontSendNotification);
            if (!isScrubbingA && lenA > 0.0) {
                deckAPosSlider.setValue(posA / lenA, juce::dontSendNotification);
            }
        } else {
            deckATrackLabel.setText("No file loaded in Deck A", juce::dontSendNotification);
            deckATimeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
            deckAPosSlider.setValue(0.0, juce::dontSendNotification);
        }

        // 2. Deck B Updates
        bool isDeckBPlaying = processor.isDeckPlaying(1);
        bool isDeckBLoaded = processor.isDeckLoaded(1);
        float gainB = processor.getDeckBGain();
        float bpmB = processor.getDeckBpm(1);
        float ratioB = processor.getDeckTempoRatio(1);

        if (isDeckBPlaying) {
            deckBLabel.setText("DECK B (LIVE " + juce::String((int)(gainB * 100)) + "%)", juce::dontSendNotification);
            deckBLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        } else if (isDeckBLoaded) {
            deckBLabel.setText("DECK B (PAUSED)", juce::dontSendNotification);
            deckBLabel.setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
        } else {
            deckBLabel.setText("DECK B (EMPTY)", juce::dontSendNotification);
            deckBLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }

        if (bpmB > 0.0f) {
            if (std::abs(ratioB - 1.0f) > 0.005f) {
                float effectiveBpm = bpmB * ratioB;
                deckBBpmLabel.setText(juce::String(effectiveBpm, 1) + " BPM (" + (ratioB >= 1.0f ? "+" : "") + juce::String((int)std::round((ratioB - 1.0f) * 100.0f)) + "%)", juce::dontSendNotification);
                deckBBpmLabel.setColour(juce::Label::textColourId, juce::Colours::teal);
            } else {
                deckBBpmLabel.setText(juce::String(bpmB, 1) + " BPM", juce::dontSendNotification);
                deckBBpmLabel.setColour(juce::Label::textColourId, juce::Colours::orange.brighter(0.3f));
            }
        } else {
            deckBBpmLabel.setText("-- BPM", juce::dontSendNotification);
            deckBBpmLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }

        if (isDeckBLoaded) {
            deckBTrackLabel.setText(processor.getDeckBTrack().title, juce::dontSendNotification);
            double posB = processor.getDeckPosition(1);
            double lenB = processor.getDeckLength(1);
            const auto& trkB = processor.getDeckBTrack();
            juce::String timeStr = formatTime(posB) + " / " + formatTime(lenB) + " (-" + formatTime(juce::jmax(0.0, lenB - posB)) + ")";
            if (trkB.hasCues()) {
                timeStr += " [IN: " + formatTime(trkB.cueInSeconds) + " | OUT: " + formatTime(trkB.getEffectiveEnd()) + "]";
            }
            deckBTimeLabel.setText(timeStr, juce::dontSendNotification);
            if (!isScrubbingB && lenB > 0.0) {
                deckBPosSlider.setValue(posB / lenB, juce::dontSendNotification);
            }
        } else {
            deckBTrackLabel.setText("No file loaded in Deck B", juce::dontSendNotification);
            deckBTimeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
            deckBPosSlider.setValue(0.0, juce::dontSendNotification);
        }

        // 3. Crossfader Position Update
        if (!isManualCrossfading) {
            float manualPos = processor.getManualCrossfadePos();
            if (manualPos >= 0.0f) {
                crossfaderSlider.setValue(manualPos, juce::dontSendNotification);
            } else {
                int active = processor.getActiveDeckIndex();
                crossfaderSlider.setValue((active == 0) ? 0.0 : 1.0, juce::dontSendNotification);
            }
        }
    }

    void updateTrackUI() {
        listBox.repaint();
    }

    juce::String formatTime(double seconds) {
        int m = (int)(seconds / 60);
        int s = (int)std::fmod(seconds, 60.0);
        return juce::String::formatted("%02d:%02d", m, s);
    }

    void chooseFiles() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Add Audio Files to Playlist...",
            processor.getLastFolder(),
            "*.mp3;*.wav;*.flac;*.ogg;*.aiff;*.m4a;*.aac"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                auto results = fc.getResults();
                for (const auto& f : results) {
                    processor.addFile(f);
                }
                listBox.updateContent();
                updateTrackUI();
            }
        );
    }

    void chooseFolder() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Add Music Folder to Playlist...",
            processor.getLastFolder()
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto dir = fc.getResult();
                if (dir.isDirectory()) {
                    processor.addFolder(dir);
                    listBox.updateContent();
                    updateTrackUI();
                }
            }
        );
    }

    void promptYoutubeDownload() {
        juce::File defaultDir = YoutubeDownloadManager::getInstance().getDownloadsDir();
        juce::String currentDest = defaultDir.getFullPathName();

        auto* alert = new juce::AlertWindow(
            "Download Song from YouTube (MP3 192k)",
            "Destination: " + currentDest + "\nPaste YouTube link below:",
            juce::AlertWindow::QuestionIcon
        );
        alert->addTextEditor("url", juce::SystemClipboard::getTextFromClipboard().trim().startsWith("http") ? juce::SystemClipboard::getTextFromClipboard().trim() : "");
        alert->addButton("Look Up & Download", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Look Up & Play Now", 2);
        alert->addButton("Change Save Folder...", 3);
        alert->addButton("Open Downloads Folder", 4);
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        alert->enterModalState(true, juce::ModalCallbackFunction::create([this, alert](int result) {
            juce::String url = alert->getTextEditorContents("url").trim();
            delete alert;

            if (result == 1 || result == 2) {
                bool playNow = (result == 2);
                if (url.isNotEmpty()) {
                    deckATrackLabel.setText("Looking up song details: " + url + "...", juce::dontSendNotification);
                    
                    YoutubeDownloadManager::fetchVideoInfoAsync(url, [this, url, playNow](const YoutubeDownloadManager::VideoInfo& info) {
                        deckATrackLabel.setText(info.success ? ("Found: " + info.title) : "Starting download...", juce::dontSendNotification);
                        showDownloadConfirmation(url, info, playNow);
                    });
                }
            } else if (result == 3) {
                fileChooser = std::make_unique<juce::FileChooser>(
                    "Select Destination Folder for YouTube Downloads...",
                    YoutubeDownloadManager::getInstance().getDownloadsDir()
                );
                fileChooser->launchAsync(
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                    [this](const juce::FileChooser& fc) {
                        auto dir = fc.getResult();
                        if (dir.isDirectory()) {
                            YoutubeDownloadManager::getInstance().setDownloadsDir(dir);
                            promptYoutubeDownload();
                        }
                    }
                );
            } else if (result == 4) {
                YoutubeDownloadManager::openFolderInExplorer(YoutubeDownloadManager::getInstance().getDownloadsDir());
            }
        }), true);
    }

    void showDownloadConfirmation(const juce::String& url, const YoutubeDownloadManager::VideoInfo& info, bool defaultPlayNow) {
        juce::String msg;
        if (info.success) {
            msg = "Is this the correct song?\n\n"
                  "Track:  " + info.title + "\n";
            if (info.uploader.isNotEmpty()) msg += "Artist/Channel:  " + info.uploader + "\n";
            if (info.duration.isNotEmpty()) msg += "Duration:  " + info.duration + "\n";
            msg += "\nSaved as MP3 192k to:\n" + YoutubeDownloadManager::getInstance().getDownloadsDir().getFullPathName();
        } else {
            msg = "Could not preview song title automatically.\n\nURL: " + url + "\n\nDownload anyway as MP3 192k?";
        }

        auto* confirm = new juce::AlertWindow(
            "Confirm Song Download",
            msg,
            info.success ? juce::AlertWindow::QuestionIcon : juce::AlertWindow::WarningIcon
        );

        if (defaultPlayNow) {
            confirm->addButton("Download & Play Now", 2, juce::KeyPress(juce::KeyPress::returnKey));
            confirm->addButton("Download to Playlist", 1);
        } else {
            confirm->addButton("Download (MP3 192k)", 1, juce::KeyPress(juce::KeyPress::returnKey));
            confirm->addButton("Download & Play Now", 2);
        }
        confirm->addButton("Cancel / Wrong Song", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        confirm->enterModalState(true, juce::ModalCallbackFunction::create([this, confirm, url](int result) {
            delete confirm;
            if (result == 1 || result == 2) {
                bool playNow = (result == 2);
                deckATrackLabel.setText("Downloading: " + url + "...", juce::dontSendNotification);
                YoutubeDownloadManager::getInstance().queueDownload(url, playNow, {},
                    [this, playNow](bool success, const juce::File& file, const juce::String& /*title*/, const juce::String& err) {
                        if (success && file.existsAsFile()) {
                            processor.addFile(file);
                            listBox.updateContent();
                            if (playNow) {
                                int newIdx = (int)processor.getPlaylist().size() - 1;
                                processor.playTrack(newIdx, true);
                            }
                            updateTrackUI();
                        } else {
                            auto* errBox = new juce::AlertWindow("Download Failed", err.isNotEmpty() ? err : "Failed to download audio from YouTube URL", juce::AlertWindow::WarningIcon);
                            errBox->addButton("OK", 1);
                            errBox->enterModalState(true, nullptr, true);
                        }
                    },
                    [this](const juce::String& status, float /*progress*/) {
                        deckATrackLabel.setText(status, juce::dontSendNotification);
                    }
                );
            }
        }), true);
    }

    void savePlaylist() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Playlist As...",
            processor.getLastFolder().getChildFile("playlist.m3u"),
            "*.m3u;*.json"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File()) {
                    if (file.getFileExtension().isEmpty()) {
                        file = file.withFileExtension("m3u");
                    }
                    processor.savePlaylist(file);
                    processor.setLastFolder(file);
                }
            }
        );
    }

    void loadPlaylist() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load Playlist...",
            processor.getLastFolder(),
            "*.m3u;*.json"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    processor.loadPlaylist(file);
                    processor.setLastFolder(file);
                    listBox.updateContent();
                    updateTrackUI();
                }
            }
        );
    }

    Mp3PlayerProcessor& processor;
    std::function<void()> closeCallback;

    juce::Label titleLabel;
    juce::TextButton closeBtn;
    std::vector<std::unique_ptr<BankButton>> bankButtons;

    // Deck A Components
    juce::Rectangle<int> deckABounds;
    juce::Label deckALabel;
    juce::Label deckABpmLabel;
    juce::TextButton deckALoadBtn;
    juce::Label deckATrackLabel;
    juce::Label deckATimeLabel;
    juce::Slider deckAPosSlider;
    juce::TextButton deckASetInBtn;
    juce::TextButton deckASetOutBtn;
    juce::TextButton deckAClrCueBtn;
    juce::TextButton deckAPlayBtn;
    juce::TextButton deckAPauseBtn;
    juce::TextButton deckAStopBtn;
    juce::TextButton deckASyncBtn;

    // Deck B Components
    juce::Rectangle<int> deckBBounds;
    juce::Label deckBLabel;
    juce::Label deckBBpmLabel;
    juce::TextButton deckBLoadBtn;
    juce::Label deckBTrackLabel;
    juce::Label deckBTimeLabel;
    juce::Slider deckBPosSlider;
    juce::TextButton deckBSetInBtn;
    juce::TextButton deckBSetOutBtn;
    juce::TextButton deckBClrCueBtn;
    juce::TextButton deckBPlayBtn;
    juce::TextButton deckBPauseBtn;
    juce::TextButton deckBStopBtn;
    juce::TextButton deckBSyncBtn;

    // Center Crossfader & DJ Controls
    juce::TextButton fadeToABtn;
    juce::Slider crossfaderSlider;
    juce::TextButton fadeToBBtn;
    juce::TextButton syncModeBtn;
    juce::TextButton beatSyncBtn;
    juce::TextButton autoDjBtn;
    juce::TextButton crossfadeBtn;

    // Master / Leveler
    juce::TextButton levelerBtn;
    GainReductionMeter levelerMeter;
    juce::Label volLabel;
    juce::Slider volSlider;

    // Playlist
    juce::ListBox listBox;
    juce::TextButton addFilesBtn;
    juce::TextButton addFolderBtn;
    juce::TextButton moveUpBtn;
    juce::TextButton moveDownBtn;
    juce::TextButton smartArrangeBtn;
    juce::TextButton grabYoutubeBtn;
    juce::TextButton loadToDeckABtn;
    juce::TextButton loadToDeckBBtn;
    juce::TextButton removeBtn;
    juce::TextButton clearBtn;
    juce::TextButton savePlaylistBtn;
    juce::TextButton loadPlaylistBtn;
    juce::TextButton exportBtn;

    bool isScrubbingA = false;
    bool isScrubbingB = false;
    bool isManualCrossfading = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mp3PlayerComponent)
};
