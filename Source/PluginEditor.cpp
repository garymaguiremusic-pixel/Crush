#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kWidth = CrushPanel::kBaseWidth, kHeight = CrushPanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 270;
}

//==============================================================================
CrushPanel::CrushPanel (CrushAudioProcessor& p)
    : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (kWidth, kHeight);
    createBackground();

    auto& apvts = processor.apvts;

    // The one knob ------------------------------------------------------------------
    exciteKnob.describeValue = crush::describeExcite;
    exciteAttachment = std::make_unique<KnobAttachment> (*apvts.getParameter (crush::ParamIDs::excite), exciteKnob);
    addAndMakeVisible (exciteKnob);

    // Frequency buttons (radio group bound to the choice parameter) ----------------------
    auto* freqParam = apvts.getParameter (crush::ParamIDs::frequency);
    freqAttachment = std::make_unique<ParameterAttachment> (*freqParam, [this] (float v) { showFrequency (roundToInt (v)); });

    const auto names = crush::getFrequencyNames();
    for (int i = 0; i < crush::kNumFrequencies; ++i)
    {
        auto& b = freqButtons[i];
        b.setButtonText (names[i]);
        b.setRadioGroupId (1001);
        b.setClickingTogglesState (false);
        b.setTooltip (i == 0 ? "7 kHz: the original exciter band"
                             : "Move the excited band up to " + names[i]);
        b.onClick = [this, i] { freqAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    freqAttachment->sendInitialUpdate();

    // Bypass -----------------------------------------------------------------------------
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass Crush (dry signal only)");
    bypassAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (apvts, crush::ParamIDs::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (exciteLcd);
    addAndMakeVisible (freqLcd);
    driveMeter.setTooltip ("HF drive: the exciter's gain, full = x3");
    addAndMakeVisible (driveMeter);

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    timerCallback();
    startTimerHz (30);
}

CrushPanel::~CrushPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CrushPanel::showFrequency (int index)
{
    index = jlimit (0, crush::kNumFrequencies - 1, index);
    for (int i = 0; i < crush::kNumFrequencies; ++i)
        freqButtons[i].setToggleState (i == index, dontSendNotification);

    shownFrequency = index;
    freqLcd.setContent (index == 0 ? "HIGH-PASS (ORIG)" : "HIGH-PASS", crush::getFrequencyNames()[index]);
}

//==============================================================================
void CrushPanel::resized()
{
    excitePanel = { 8.0f,   40.0f, 196.0f, 222.0f };
    freqPanel   = { 210.0f, 40.0f, 202.0f, 222.0f };

    bypassButton.setBounds (346, 8, 66, 19);

    exciteKnob.setBounds (41, 66, 130, 130);
    exciteLcd.setBounds (26, 220, 160, 26);

    freqButtons[0].setBounds (220, 80, 88, 26);
    freqButtons[1].setBounds (314, 80, 88, 26);
    freqButtons[2].setBounds (220, 112, 88, 26);
    freqButtons[3].setBounds (314, 112, 88, 26);
    driveMeter.setBounds (220, 172, 182, 18);
    freqLcd.setBounds (220, 220, 182, 26);

    helpButton.setBounds (kWidth - 26, kBottomBarY + 5, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void CrushPanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x7a11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 1100; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void CrushPanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Chrome "crush", blue "excite" and a swoosh, in the Virus Delay style.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "crush", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "excite", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (CrushColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (CrushColours::blue.withAlpha (0.0f), sx, sy, CrushColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void CrushPanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (CrushColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, excitePanel, "Excite");
    CustomLookAndFeel::drawPanel (g, freqPanel,   "Frequency");

    CustomLookAndFeel::drawSmallLabel (g, "High-Pass Center", { 220.0f, 64.0f, 182.0f, 12.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "HF Drive",         { 220.0f, 156.0f, 182.0f, 12.0f }, Justification::centredLeft);

    // scale under the drive meter
    g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
    g.setColour (CrushColours::labelText.withAlpha (0.8f));
    const char* marks[] = { "x0", "x1", "x2", "x3" };
    for (int i = 0; i < 4; ++i)
    {
        const float x = 220.0f + 182.0f * (float) i / 3.0f;
        const float left = jlimit (220.0f, 402.0f - 24.0f, x - 12.0f);
        g.drawText (marks[i], Rectangle<float> (left, 192.0f, 24.0f, 11.0f),
                    i == 0 ? Justification::centredLeft : (i == 3 ? Justification::centredRight : Justification::centred));
    }

    // dividers above the LCD rows
    for (auto& panel : { excitePanel, freqPanel })
    {
        g.setColour (CrushColours::bevelDark);
        g.drawHorizontalLine (210, panel.getX() + 8.0f, panel.getRight() - 8.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (211, panel.getX() + 8.0f, panel.getRight() - 8.0f);
    }

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 6.0f, 56.0f, 18.0f);
    g.setGradientFill (ColourGradient (CrushColours::headerTop, 0.0f, tab.getY(), CrushColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (CrushColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (CrushColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("EXCITE", tab, Justification::centred);

    auto inset = bar.reduced (74.0f, 6.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    g.drawText ("CRUSH", inset, Justification::centred);
}

//==============================================================================
void CrushPanel::timerCallback()
{
    const float excite = processor.apvts.getRawParameterValue (crush::ParamIDs::excite)->load();
    const bool bypassed = processor.apvts.getRawParameterValue (crush::ParamIDs::bypass)->load() > 0.5f;

    exciteLcd.setContent ("EXCITE", crush::formatKnobValue (excite));
    exciteLcd.setDimmed (bypassed || excite < 0.5f);
    freqLcd.setDimmed (bypassed);

    driveMeter.setLevel (processor.gui.exciteGain.load());
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
CrushAudioProcessorEditor::CrushAudioProcessorEditor (CrushAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting up the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.gui.editorWidth.load();

    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 7 / 10, kHeight * 7 / 10, kWidth * 5 / 2, kHeight * 5 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 7 / 10, kWidth * 5 / 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void CrushAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.gui.editorWidth.store (getWidth());
}
