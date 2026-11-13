#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob with the Virus Delay feel:
      - vertical drag and the scroll wheel move in whole steps (1.0) only
      - double-click resets to the default value
      - small knobs show their label underneath, which swaps to the value while
        hovering or dragging
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class CrushKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    CrushKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (float)> describeValue;   // real meaning for the tooltip

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setValueWithGesture (double v);

    Size knobSize;
    juce::String label;
    double defaultValue = 50.0;
    float lastDragY = 0.0f;
    double dragValue = 0.0;          // un-snapped drag position
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Connects a CrushKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, CrushKnob&);
    ~KnobAttachment();

private:
    CrushKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "EXCITE      50". */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/** Segmented meter showing the exciter's HF gain (0..3). */
class DriveMeter : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void setLevel (float gain0to3);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f, peak = 0.0f;
    int peakHoldFrames = 0;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
