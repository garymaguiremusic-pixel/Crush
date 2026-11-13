#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/DistortionSection.h"

//==============================================================================
class CrushAudioProcessor : public juce::AudioProcessor
{
public:
    CrushAudioProcessor();
    ~CrushAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override          { return JucePlugin_Name; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    /** Read-only meters for the editor (written on the audio thread). */
    struct GuiState
    {
        std::atomic<float> exciteGain  { 0.0f };   // 0..3, HF gain of the exciter
        std::atomic<int>   editorWidth { 0 };      // remembered editor size (0 = default)
    };
    GuiState gui;

private:
    crush::DistortionSection distortion;
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wetGate;
    int currentFrequencyIndex = 0;

    std::atomic<float>* pExcite = nullptr;
    std::atomic<float>* pFrequency = nullptr;
    std::atomic<float>* pBypass = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrushAudioProcessor)
};
