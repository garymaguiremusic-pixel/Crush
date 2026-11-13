#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/CrushComponents.h"

//==============================================================================
/** The whole user interface, laid out at its base size (420 x 300). The editor
    below scales it to the window size. */
class CrushPanel : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kBaseWidth = 420, kBaseHeight = 300;

    explicit CrushPanel (CrushAudioProcessor&);
    ~CrushPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    void showFrequency (int index);

    CrushAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    CrushKnob exciteKnob { CrushKnob::Size::Large, "Excite" };
    juce::TextButton freqButtons[crush::kNumFrequencies];
    juce::TextButton bypassButton { "Bypass" };

    LcdDisplay exciteLcd, freqLcd;
    DriveMeter driveMeter;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 500 };

    // Attachments (declared after the controls so they are destroyed first)
    std::unique_ptr<KnobAttachment> exciteAttachment;
    std::unique_ptr<juce::ParameterAttachment> freqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Rectangle<float> excitePanel, freqPanel;
    int shownFrequency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrushPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 70 % .. 250 %); remembers its size. */
class CrushAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit CrushAudioProcessorEditor (CrushAudioProcessor&);
    void resized() override;

private:
    CrushAudioProcessor& processor;
    CrushPanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrushAudioProcessorEditor)
};
