#include "MainComponent.h"

//==============================================================================
//==============================================================================
MainComponent::MainComponent() {
  // Initialize audio device with 2 inputs and 4 outputs (FOH and IEM)
  setAudioChannels(2, 4);

  // 1. Create UI for each slot in the engine
  for (int i = 0; i < engine.getNumSlots(); ++i) {
    auto *slot = engine.getSlot(i);
    auto *comp = new RackSlotComponent(*slot);

    // MIDI target selection logic
    comp->onSelect = [this, i] { engine.setSelectedSlot(i); };

    rackSlotComponents.add(comp);
    addAndMakeVisible(comp);
  }

  addAndMakeVisible(scanButton);
  scanButton.onClick = [this] { engine.scanForPlugins(); };

  addAndMakeVisible(saveBtn);
  saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
  saveBtn.onClick = [this] {
    engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
  };

  addAndMakeVisible(renameBtn);
  renameBtn.setColour(juce::TextButton::buttonColourId,
                      juce::Colours::darkblue);
  renameBtn.onClick = [this] {
    auto *aw = new juce::AlertWindow("Rename Preset",
                                     "Enter new name for the active preset:",
                                     juce::AlertWindow::QuestionIcon);
    aw->addTextEditor("name",
                      engine.getSceneName(engine.getCurrentSceneIndex()));
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([this, aw](int res) {
          if (res == 1) {
            auto newName = aw->getTextEditorContents("name");
            int idx = engine.getCurrentSceneIndex();
            engine.renameScene(idx, newName);
            if (idx < sceneButtons.size())
              sceneButtons[idx]->setButtonText(newName);
          }
          delete aw;
        }));
  };

  addAndMakeVisible(addPresetBtn);
  addPresetBtn.onClick = [this] {
    engine.createNewScene("NEW PRESET");
    refreshPresetButtons();
  };

  addAndMakeVisible(exitBtn);
  exitBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
  exitBtn.onClick = [] { juce::JUCEApplication::quit(); };

  refreshPresetButtons();

  setSize(1000, 700);

  // Setup MIDI
  auto midiInputs = juce::MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
    deviceManager.addMidiInputDeviceCallback(input.identifier, this);
  }
}

MainComponent::~MainComponent() {
  auto midiInputs = juce::MidiInput::getAvailableDevices();
  for (auto &input : midiInputs)
    deviceManager.removeMidiInputDeviceCallback(input.identifier, this);

  shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
  midiCollector.reset(sampleRate);
  engine.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  juce::MidiBuffer incomingMidi;
  midiCollector.removeNextBlockOfMessages(incomingMidi,
                                          bufferToFill.numSamples);

  engine.processAudio(*bufferToFill.buffer, incomingMidi);
}

void MainComponent::releaseResources() {}

//==============================================================================
void MainComponent::paint(juce::Graphics &g) {
  // Dark premium look
  g.fillAll(juce::Colour(0xff121212));

  g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
  g.setColour(juce::Colours::cyan);
  g.drawText("DaveCore", getLocalBounds().removeFromTop(50),
             juce::Justification::centred, true);

  g.setFont(juce::FontOptions(14.0f));
  g.setColour(juce::Colours::grey);
  g.drawText("The Sovereign Live Performance Engine",
             getLocalBounds().removeFromTop(80).removeFromBottom(30),
             juce::Justification::centred, true);
}

void MainComponent::resized() {
  auto r = getLocalBounds();

  // Header area
  auto header = r.removeFromTop(80);
  exitBtn.setBounds(header.removeFromLeft(60).reduced(10));
  scanButton.setBounds(header.removeFromLeft(150).reduced(10));
  saveBtn.setBounds(header.removeFromRight(120).reduced(5));
  renameBtn.setBounds(header.removeFromRight(120).reduced(5));
  addPresetBtn.setBounds(header.removeFromRight(40).reduced(5));

  // Presets Row
  auto presetArea = header.reduced(5);
  int btnWidth = presetArea.getWidth() / std::max(1, (int)sceneButtons.size());
  for (auto *btn : sceneButtons) {
    btn->setBounds(presetArea.removeFromLeft(btnWidth).reduced(2));
  }

  // Rack Slots
  r.removeFromTop(10); // spacer
  for (auto *c : rackSlotComponents) {
    c->setBounds(r.removeFromTop(65).reduced(10, 2));
  }
}

void MainComponent::refreshPresetButtons() {
  sceneButtons.clear();
  for (int i = 0; i < engine.getNumScenes(); ++i) {
    auto *btn = new juce::TextButton(engine.getSceneName(i));
    btn->setClickingTogglesState(true);
    btn->setRadioGroupId(99);
    btn->onClick = [this, i] { engine.loadScene(i); };

    sceneButtons.add(btn);
    addAndMakeVisible(btn);

    if (i == engine.getCurrentSceneIndex())
      btn->setToggleState(true, juce::dontSendNotification);
  }
  resized();
}
