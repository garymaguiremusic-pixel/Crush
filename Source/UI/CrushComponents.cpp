#include "CrushComponents.h"
#include "../CustomLookAndFeel.h"
#include "../Parameters.h"

using namespace juce;

//==============================================================================
// CrushKnob
//==============================================================================
CrushKnob::CrushKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRange (0.0, 100.0, 1.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String CrushKnob::getTooltip()
{
    const auto v = (float) getValue();
    String text = label.toUpperCase() + "  " + crush::formatKnobValue (v);
    if (describeValue)
        text << "   (" << describeValue (v) << ")";
    return text;
}

void CrushKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((hovering || dragging) && isEnabled())
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (CrushColours::accent);
            g.drawFittedText (crush::formatKnobValue (getValue()), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

void CrushKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), std::round (v)), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void CrushKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    lastDragY = e.position.y;
    dragValue = getValue();
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void CrushKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental vertical drag: the full range in 200 pixels, snapped to whole steps.
    const double pixels = (double) (lastDragY - e.position.y);
    lastDragY = e.position.y;

    dragValue = jlimit (getMinimum(), getMaximum(), dragValue + pixels * (getMaximum() - getMinimum()) / 200.0);
    const double snapped = std::round (dragValue);
    if (std::abs (snapped - getValue()) > 1.0e-9)
        setValue (snapped, sendNotificationSync);
}

void CrushKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void CrushKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void CrushKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    // One wheel notch = exactly 1.0.
    setValueWithGesture (std::round (getValue()) + (dir > 0.0f ? 1.0 : -1.0));
}

void CrushKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void CrushKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, CrushKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setRange (range.start, range.end, 1.0);
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour  = dimmed ? Colour (0xff6e6e72) : CrushColours::lightGrey;
    const auto valueColour = dimmed ? Colour (0xff6e6e72) : CrushColours::accent;
    auto textArea = b.reduced (7.0f, 0.0f);

    const auto font = CustomLookAndFeel::uiFont (12.0f, true);
    g.setFont (font);

    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (textColour);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.setColour (valueColour);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// DriveMeter
//==============================================================================
void DriveMeter::setLevel (float gain)
{
    const float v = jlimit (0.0f, 1.0f, gain / 3.0f);
    // fast rise, gentle visual fall
    const float shown = v > level ? v : level + (v - level) * 0.25f;

    if (shown >= peak) { peak = shown; peakHoldFrames = 20; }
    else if (peakHoldFrames > 0) --peakHoldFrames;
    else peak = std::max (shown, peak - 0.02f);

    if (std::abs (shown - level) > 0.002f || peakHoldFrames > 0 || peak > shown)
    {
        level = shown;
        repaint();
    }
}

void DriveMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 3.0f);

    auto inner = b.reduced (3.0f, 3.0f);
    constexpr int segments = 24;
    const float gap = 1.5f;
    const float segW = (inner.getWidth() - gap * (segments - 1)) / (float) segments;
    const int lit = roundToInt (level * segments);
    const int peakSeg = jlimit (0, segments - 1, roundToInt (peak * segments) - 1);

    for (int i = 0; i < segments; ++i)
    {
        auto seg = Rectangle<float> (inner.getX() + (float) i * (segW + gap), inner.getY(), segW, inner.getHeight());
        const bool on = i < lit;
        const bool top = i >= segments - 3;               // gain cap region (x3)
        // orange gradient across the meter; the gain-cap region (x3) glows hot
        const auto colour = top ? CrushColours::accentLight
                                : Colour (0xffff8a2a).interpolatedWith (Colour (0xffc4520a), (float) i / (float) (segments - 1));

        if (on)
        {
            g.setColour (CrushColours::accent.withAlpha (0.4f));
            g.fillRoundedRectangle (seg.expanded (1.0f, 1.0f), 1.5f);
            g.setColour (colour);
        }
        else if (i == peakSeg && peak > 0.01f)
        {
            g.setColour (colour.withAlpha (0.7f));
        }
        else
        {
            g.setColour (Colour (0xff2a1a10));
        }
        g.fillRoundedRectangle (seg, 1.0f);
    }
}

//==============================================================================
// HelpButton / HelpOverlay
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    const auto top = isHighlighted ? Colour (0xff343437).brighter (0.12f) : Colour (0xff343437);
    const auto bottom = isHighlighted ? Colour (0xff1a1a1c).brighter (0.12f) : Colour (0xff1a1a1c);
    g.setGradientFill (ColourGradient (isDown ? bottom : top, 0.0f, b.getY(), isDown ? top : bottom, 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (CrushColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (14.0f, 16.0f);
    CustomLookAndFeel::drawPanel (g, box, "Crush - Quick Help");

    const char* lines[] =
    {
        "EXCITE:  one knob, 0 - 100. A high-passed copy of the signal is added",
        "back with a gain that follows its own level (up to x3), then clipped.",
        "The knob is a wet / dry crossfade: 0 = dry, 100 = fully excited.",
        "FREQUENCY:  high-pass corner of the excited band.",
        "7 kHz is the original; 10, 12 and 15 kHz move it higher.",
        "HF DRIVE:  shows the excite gain (full = x3).",
        "Knob: drag, mouse wheel (1 per notch), double-click = default.",
        "Drag the bottom-right corner to resize.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (12.0f, 0.0f).withTrimmedTop (28.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? CrushColours::accent : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (18.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
