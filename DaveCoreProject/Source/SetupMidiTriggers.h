#pragma once
#include <JuceHeader.h>
#include "OpenRigConstants.h"

struct SetupMidiTrigger {
    bool isProgramChange = true;
    int number = -1; // CC number or PC number
    int channel = 0; // 0 = omni
    
    juce::String toString() const {
        if (number == -1) return "";
        return (isProgramChange ? "PC " : "CC ") + juce::String(number);
    }
};

class SetupMidiTriggers {
public:
    static SetupMidiTriggers& getInstance() {
        static SetupMidiTriggers instance;
        return instance;
    }
    
    void load() {
        auto file = OpenRigConstants::getAppDirectory().getChildFile("setup_midi_triggers.json");
        if (file.existsAsFile()) {
            auto varObj = juce::JSON::parse(file);
            if (auto* obj = varObj.getDynamicObject()) {
                triggers.clear();
                for (const auto& prop : obj->getProperties()) {
                    auto name = prop.name.toString();
                    auto* triggerObj = prop.value.getDynamicObject();
                    if (triggerObj) {
                        SetupMidiTrigger t;
                        t.isProgramChange = (bool)triggerObj->getProperty("isProgramChange");
                        t.number = (int)triggerObj->getProperty("number");
                        t.channel = (int)triggerObj->getProperty("channel");
                        triggers.set(name, t);
                    }
                }
            }
        }
    }
    
    void save() {
        auto file = OpenRigConstants::getAppDirectory().getChildFile("setup_midi_triggers.json");
        file.getParentDirectory().createDirectory();
        
        auto* obj = new juce::DynamicObject();
        for (auto it = triggers.begin(); it != triggers.end(); ++it) {
            auto* triggerObj = new juce::DynamicObject();
            triggerObj->setProperty("isProgramChange", it.getValue().isProgramChange);
            triggerObj->setProperty("number", it.getValue().number);
            triggerObj->setProperty("channel", it.getValue().channel);
            obj->setProperty(it.getKey(), triggerObj);
        }
        
        juce::FileOutputStream stream(file);
        if (stream.openedOk()) {
            stream.setPosition(0);
            stream.truncate();
            juce::JSON::writeToStream(stream, juce::var(obj));
        }
    }
    
    void setTrigger(const juce::String& setupName, SetupMidiTrigger trigger) {
        // Clear this trigger if bound elsewhere to avoid duplicates
        if (trigger.number != -1) {
            juce::StringArray keysToRemove;
            for (auto it = triggers.begin(); it != triggers.end(); ++it) {
                if (it.getValue().isProgramChange == trigger.isProgramChange && 
                    it.getValue().number == trigger.number && 
                    (trigger.channel == 0 || it.getValue().channel == 0 || it.getValue().channel == trigger.channel)) {
                    keysToRemove.add(it.getKey());
                }
            }
            for (const auto& k : keysToRemove) {
                triggers.remove(k);
            }
        }
        
        triggers.set(setupName, trigger);
        save();
    }
    
    void clearTrigger(const juce::String& setupName) {
        triggers.remove(setupName);
        save();
    }
    
    SetupMidiTrigger getTrigger(const juce::String& setupName) {
        if (triggers.contains(setupName)) {
            return triggers.getReference(setupName);
        }
        SetupMidiTrigger t;
        t.number = -1;
        return t;
    }
    
    juce::String findSetupForTrigger(bool isPC, int number, int channel) const {
        for (auto it = triggers.begin(); it != triggers.end(); ++it) {
            if (it.getValue().isProgramChange == isPC && 
                it.getValue().number == number && 
                (it.getValue().channel == 0 || it.getValue().channel == channel)) {
                return it.getKey();
            }
        }
        return {};
    }

private:
    SetupMidiTriggers() {
        load();
    }
    
    juce::HashMap<juce::String, SetupMidiTrigger> triggers;
};
