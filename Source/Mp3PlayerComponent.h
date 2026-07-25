#pragma once

#include <JuceHeader.h>
#include "Mp3PlayerProcessor.h"
#include "ThemeManager.h"
#include "BoutiqueLookAndFeel.h"
#include "ChannelStripComponent.h"

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

        titleLabel.setText("MP3 PLAYLIST - Slot 11", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, ThemeManager::get(Theme::Role::accent));
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        closeBtn.onClick = [this] {
            if (closeCallback) closeCallback();
        };
        addAndMakeVisible(closeBtn);

        trackTitleLabel.setText("No track loaded", juce::dontSendNotification);
        trackTitleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        trackTitleLabel.setJustificationType(juce::Justification::centred);
        trackTitleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(trackTitleLabel);

        timeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        timeLabel.setFont(juce::FontOptions(13.0f));
        timeLabel.setJustificationType(juce::Justification::centred);
        timeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(timeLabel);

        positionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        positionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        positionSlider.setRange(0.0, 1.0, 0.001);
        positionSlider.onValueChange = [this] {
            if (isScrubbing && processor.getLength() > 0.0) {
                processor.setPosition(positionSlider.getValue() * processor.getLength());
            }
        };
        positionSlider.onDragStart = [this] { isScrubbing = true; };
        positionSlider.onDragEnd = [this] { isScrubbing = false; };
        addAndMakeVisible(positionSlider);

        prevBtn.setButtonText("|<<");
        prevBtn.setTooltip("Previous Track");
        prevBtn.onClick = [this] { processor.prevTrack(); updateTrackUI(); };
        addAndMakeVisible(prevBtn);

        playPauseBtn.setButtonText(">");
        playPauseBtn.setTooltip("Play / Pause");
        playPauseBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::ok));
        playPauseBtn.onClick = [this] {
            if (processor.isPlaying()) {
                processor.pause();
            } else {
                int selected = listBox.getSelectedRow();
                if (selected >= 0 && selected != processor.getCurrentTrackIndex() && selected < (int)processor.getPlaylist().size()) {
                    processor.playTrack(selected);
                } else {
                    processor.play();
                }
            }
            updateTrackUI();
        };
        addAndMakeVisible(playPauseBtn);

        stopBtn.setButtonText("[]");
        stopBtn.setTooltip("Stop");
        stopBtn.onClick = [this] { processor.stop(); updateTrackUI(); };
        addAndMakeVisible(stopBtn);

        nextBtn.setButtonText(">>|");
        nextBtn.setTooltip("Next Track");
        nextBtn.onClick = [this] { processor.nextTrack(); updateTrackUI(); };
        addAndMakeVisible(nextBtn);

        loopBtn.setButtonText("LOOP: OFF");
        loopBtn.setTooltip("Toggle Playlist Loop Mode");
        loopBtn.onClick = [this] {
            auto mode = processor.getLoopMode();
            if (mode == Mp3PlayerProcessor::LoopMode::Off) mode = Mp3PlayerProcessor::LoopMode::RepeatAll;
            else if (mode == Mp3PlayerProcessor::LoopMode::RepeatAll) mode = Mp3PlayerProcessor::LoopMode::RepeatTrack;
            else mode = Mp3PlayerProcessor::LoopMode::Off;
            processor.setLoopMode(mode);
            updateLoopBtnText();
        };
        addAndMakeVisible(loopBtn);

        shuffleBtn.setButtonText("SHUFFLE: OFF");
        shuffleBtn.setTooltip("Toggle Shuffle Mode");
        shuffleBtn.onClick = [this] {
            bool s = !processor.isShuffleEnabled();
            processor.setShuffleEnabled(s);
            shuffleBtn.setButtonText(s ? "SHUFFLE: ON" : "SHUFFLE: OFF");
            shuffleBtn.setColour(juce::TextButton::buttonColourId, s ? juce::Colours::cyan.darker(0.5f) : ThemeManager::get(Theme::Role::panel));
        };
        addAndMakeVisible(shuffleBtn);

        levelerBtn.setButtonText(processor.isLevelerEnabled() ? "AUTO LEVEL: ON" : "AUTO LEVEL: OFF");
        levelerBtn.setTooltip("Auto-normalize track volumes across your playlist using smart compression");
        levelerBtn.setColour(juce::TextButton::buttonColourId, processor.isLevelerEnabled() ? ThemeManager::get(Theme::Role::ok) : ThemeManager::get(Theme::Role::panel));
        levelerBtn.onClick = [this] {
            bool lev = !processor.isLevelerEnabled();
            processor.setLevelerEnabled(lev);
            levelerBtn.setButtonText(lev ? "AUTO LEVEL: ON" : "AUTO LEVEL: OFF");
            levelerBtn.setColour(juce::TextButton::buttonColourId, lev ? ThemeManager::get(Theme::Role::ok) : ThemeManager::get(Theme::Role::panel));
        };
        addAndMakeVisible(levelerBtn);
        addAndMakeVisible(levelerMeter);

        volSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        volSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 15);
        volSlider.setRange(0.0, 1.5, 0.01);
        volSlider.setValue(processor.getGain(), juce::dontSendNotification);
        volSlider.setTooltip("MP3 Volume");
        volSlider.onValueChange = [this] { processor.setGain((float)volSlider.getValue()); };
        addAndMakeVisible(volSlider);

        listBox.setModel(this);
        listBox.setRowHeight(24);
        addAndMakeVisible(listBox);

        addFilesBtn.setButtonText("Add Files...");
        addFilesBtn.onClick = [this] { chooseFiles(); };
        addAndMakeVisible(addFilesBtn);

        addFolderBtn.setButtonText("Add Folder...");
        addFolderBtn.onClick = [this] { chooseFolder(); };
        addAndMakeVisible(addFolderBtn);

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
        clearBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.4f));
        clearBtn.onClick = [this] {
            processor.clearPlaylist();
            listBox.updateContent();
            updateTrackUI();
        };
        addAndMakeVisible(clearBtn);

        savePlaylistBtn.setButtonText("Save Playlist...");
        savePlaylistBtn.onClick = [this] { savePlaylist(); };
        addAndMakeVisible(savePlaylistBtn);

        loadPlaylistBtn.setButtonText("Load Playlist...");
        loadPlaylistBtn.onClick = [this] { loadPlaylist(); };
        addAndMakeVisible(loadPlaylistBtn);

        startTimer(100);
        updateLoopBtnText();
        updateTrackUI();
    }

    ~Mp3PlayerComponent() override {
        stopTimer();
    }

    void timerCallback() override {
        if (!isScrubbing && processor.getLength() > 0.0) {
            positionSlider.setValue(processor.getPosition() / processor.getLength(), juce::dontSendNotification);
        }
        updateTrackUI();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::background).withAlpha(0.95f));
        g.setColour(ThemeManager::get(Theme::Role::accent));
        g.drawRect(getLocalBounds(), 2);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);

        // Header
        auto header = area.removeFromTop(30);
        closeBtn.setBounds(header.removeFromRight(30));
        titleLabel.setBounds(header);

        area.removeFromTop(8);

        // Current Track Display
        trackTitleLabel.setBounds(area.removeFromTop(24));
        timeLabel.setBounds(area.removeFromTop(18));
        positionSlider.setBounds(area.removeFromTop(20));

        area.removeFromTop(8);

        // Transport Controls Row
        auto transportRow = area.removeFromTop(36);
        int btnW = 45;
        prevBtn.setBounds(transportRow.removeFromLeft(btnW).reduced(2));
        playPauseBtn.setBounds(transportRow.removeFromLeft(btnW).reduced(2));
        stopBtn.setBounds(transportRow.removeFromLeft(btnW).reduced(2));
        nextBtn.setBounds(transportRow.removeFromLeft(btnW).reduced(2));
        
        loopBtn.setBounds(transportRow.removeFromLeft(100).reduced(2));
        shuffleBtn.setBounds(transportRow.removeFromLeft(110).reduced(2));
        
        volSlider.setBounds(transportRow.removeFromRight(60).reduced(2));

        area.removeFromTop(10);

        // Playlist Buttons Row
        auto btnRow = area.removeFromBottom(28);
        int bw = btnRow.getWidth() / 6;
        addFilesBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        addFolderBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        removeBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        clearBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        savePlaylistBtn.setBounds(btnRow.removeFromLeft(bw).reduced(2));
        loadPlaylistBtn.setBounds(btnRow.reduced(2));

        area.removeFromBottom(8);

        // Table
        listBox.setBounds(area);
    }

    // ListBoxModel Implementation
    int getNumRows() override {
        return (int)processor.getPlaylist().size();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(juce::Colours::cyan.withAlpha(0.2f));
        } else if (rowNumber % 2 == 1) {
            g.fillAll(juce::Colours::white.withAlpha(0.03f));
        }

        const auto& list = processor.getPlaylist();
        if (rowNumber >= 0 && rowNumber < (int)list.size()) {
            const auto& item = list[rowNumber];
            bool isCurrent = (rowNumber == processor.getCurrentTrackIndex());

            g.setFont(juce::FontOptions(13.0f, isCurrent ? juce::Font::bold : juce::Font::plain));
            g.setColour(isCurrent ? juce::Colours::cyan : ThemeManager::get(Theme::Role::text));

            juce::String prefix = isCurrent ? (processor.isPlaying() ? " > " : " || ") : "   ";
            g.drawText(prefix + juce::String(rowNumber + 1) + ". " + item.title,
                       10, 0, width - 80, height, juce::Justification::centredLeft, true);

            int mins = (int)(item.durationSeconds / 60);
            int secs = (int)item.durationSeconds % 60;
            juce::String durStr = juce::String::formatted("%02d:%02d", mins, secs);
            g.drawText(durStr, width - 75, 0, 65, height, juce::Justification::centredRight, true);
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        processor.playTrack(row);
        listBox.updateContent();
        updateTrackUI();
    }

    // Drag and drop audio files or folders
    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        for (const auto& f : files) {
            juce::File file(f);
            if (file.isDirectory()) return true;
            juce::String ext = file.getFileExtension().toLowerCase();
            if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".m4a")
                return true;
        }
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override {
        for (const auto& path : files) {
            juce::File file(path);
            if (file.isDirectory()) {
                processor.addFolder(file);
            } else {
                processor.addFile(file);
            }
        }
        listBox.updateContent();
    }

private:
    void updateTrackUI() {
        playPauseBtn.setButtonText(processor.isPlaying() ? "||" : ">");
        
        int curIdx = processor.getCurrentTrackIndex();
        const auto& playlist = processor.getPlaylist();
        if (curIdx >= 0 && curIdx < (int)playlist.size()) {
            trackTitleLabel.setText(playlist[curIdx].title, juce::dontSendNotification);
            
            int posSecs = (int)processor.getPosition();
            int lenSecs = (int)processor.getLength();
            juce::String posStr = juce::String::formatted("%02d:%02d", posSecs / 60, posSecs % 60);
            juce::String lenStr = juce::String::formatted("%02d:%02d", lenSecs / 60, lenSecs % 60);
            timeLabel.setText(posStr + " / " + lenStr, juce::dontSendNotification);
        } else {
            trackTitleLabel.setText("No track loaded", juce::dontSendNotification);
            timeLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        }
        listBox.repaint();
    }

    void updateLoopBtnText() {
        auto mode = processor.getLoopMode();
        if (mode == Mp3PlayerProcessor::LoopMode::Off) {
            loopBtn.setButtonText("LOOP: OFF");
            loopBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel));
        } else if (mode == Mp3PlayerProcessor::LoopMode::RepeatAll) {
            loopBtn.setButtonText("LOOP: ALL");
            loopBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.darker(0.3f));
        } else {
            loopBtn.setButtonText("LOOP: ONE");
            loopBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
        }
    }

    void chooseFiles() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Audio Files...",
            processor.getLastFolder(),
            "*.mp3;*.wav;*.flac;*.ogg;*.aiff;*.m4a"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                auto results = fc.getResults();
                for (const auto& file : results) {
                    processor.addFile(file);
                }
                listBox.updateContent();
            }
        );
    }

    void chooseFolder() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Audio Folder...",
            processor.getLastFolder()
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.isDirectory()) {
                    processor.addFolder(file);
                    listBox.updateContent();
                }
            }
        );
    }

    void savePlaylist() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Playlist As...",
            processor.getLastFolder(),
            "*.rigplaylist;*.m3u"
        );
        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.getFullPathName().isNotEmpty()) {
                    if (file.getFileExtension().isEmpty()) {
                        file = file.withFileExtension(".rigplaylist");
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
            "*.rigplaylist;*.m3u"
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
    juce::Label trackTitleLabel;
    juce::Label timeLabel;
    juce::Slider positionSlider;

    juce::TextButton prevBtn;
    juce::TextButton playPauseBtn;
    juce::TextButton stopBtn;
    juce::TextButton nextBtn;
    juce::TextButton loopBtn;
    juce::TextButton shuffleBtn;
    juce::TextButton levelerBtn;
    GainReductionMeter levelerMeter;
    juce::Slider volSlider;

    juce::ListBox listBox;
    juce::TextButton addFilesBtn;
    juce::TextButton addFolderBtn;
    juce::TextButton removeBtn;
    juce::TextButton clearBtn;
    juce::TextButton savePlaylistBtn;
    juce::TextButton loadPlaylistBtn;

    bool isScrubbing = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mp3PlayerComponent)
};
