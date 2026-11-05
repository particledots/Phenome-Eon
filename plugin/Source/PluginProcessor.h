// The plugin wrapper. It talks to Logic, reads the control values and passes them, with the audio,
// to the engine. It contains no sound-making code.
//
// One housekeeping job: a few times a second, on the window thread, it checks whether the engine switched
// something off by itself (the Rec and Rec New Splice switches when the reel is full, the Clear Reel, Splice, Shift,
// Delete Splice Marker and Delete All Markers buttons after they have acted) and switches the matching control off
// too, so the buttons always show the truth. The same timer lets the reel file library finish its saves and loads.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <phe/Engine.h>
#include <phe/ReelLibrary.h>
#include "Parameters.h"

class PhenomeEonProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PhenomeEonProcessor();
    ~PhenomeEonProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // For the window: the controls, and a read-only look at the reel (safe to use while audio is running).
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    const phe::Engine& getEngine() const { return engine; }

    // The reel file (milestone 9): save / load the reel as a WAV file with its splice markers, and the folder it lives in.
    phe::ReelLibrary& getReelLibrary() { return library; }
    juce::File getReelFolder() const;
    void setReelFolder (const juce::File& folder);

private:
    phe::Params readParams() const;
    void timerCallback() override;
    void switchOff (const char* parameterId);

    juce::AudioProcessorValueTreeState apvts;
    phe::Engine engine;
    phe::ReelLibrary library { engine };          // after the engine, which it uses
    std::atomic<bool> switchTransientButtonsOff { false };     // set when a saved project is loaded

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhenomeEonProcessor)
};
