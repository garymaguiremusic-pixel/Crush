#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace crush;

namespace
{
    // Host blocks are handled in chunks of this size (a multiple of the 64-sample
    // sub-block, so the DSP's sub-block grid is unchanged).
    constexpr int kChunk = 512;
    static_assert (kChunk % DistortionSection::kSubBlock == 0, "chunk must be a multiple of the sub-block");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout CrushAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // The single knob: 0..100 in whole steps (also for host automation).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::excite, 1 }, "Excite", NormalisableRange<float> (0.0f, 100.0f, 1.0f), kDefaultExcite,
        AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return formatKnobValue (v); })
            .withValueFromStringFunction ([] (const String& s) { return (float) roundToInt (s.getFloatValue()); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::frequency, 1 }, "Frequency",
                                                        getFrequencyNames(), 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    return layout;
}

//==============================================================================
CrushAudioProcessor::CrushAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CrushState", createParameterLayout())
{
    pExcite    = apvts.getRawParameterValue (ParamIDs::excite);
    pFrequency = apvts.getRawParameterValue (ParamIDs::frequency);
    pBypass    = apvts.getRawParameterValue (ParamIDs::bypass);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));

    // Only the Excite stage is used; BitCrusher, Mech and Tube stay at 0 (off).
    distortion.distOn.setImmediate (1.0f);
    distortion.bitCrusher.setImmediate (0.0f);
    distortion.mech.setImmediate (0.0f);
    distortion.tube.setImmediate (0.0f);
    distortion.chainOrder = DistortionSection::ChainOrder::Contiguous;
    distortion.outputHardClip = true;     // the original's final +-0.9999 clip
    distortion.setRampSamples (0);        // parameters jump per block, as calibrated (0)
}

bool CrushAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

//==============================================================================
void CrushAudioProcessor::prepareToPlay (double sampleRate, int)
{
    dryBuffer.setSize (2, kChunk);
    distortion.setSampleRate (sampleRate);

    currentFrequencyIndex = juce::jlimit (0, kNumFrequencies - 1, (int) pFrequency->load());
    distortion.exciter.setFrequency (kFrequencies[currentFrequencyIndex]);
    distortion.excite.setImmediate (exciteToAmount (pExcite->load()));

    wetGate.reset (sampleRate, 0.02);
    wetGate.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    gui.exciteGain.store (0.0f);
}

void CrushAudioProcessor::reset()
{
    distortion.reset();
}

void CrushAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    for (int ch = numIn; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numIn == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);   // mono in -> both channels

    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    const int freqIndex = juce::jlimit (0, kNumFrequencies - 1, (int) pFrequency->load());
    if (freqIndex != currentFrequencyIndex)
    {
        currentFrequencyIndex = freqIndex;
        distortion.exciter.setFrequency (kFrequencies[freqIndex]);
    }

    const float amount = exciteToAmount (pExcite->load());
    distortion.excite.setTarget (amount);
    wetGate.setTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        float* l = left + pos;
        float* r = right + pos;

        const bool fading = wetGate.isSmoothing();
        const bool fullyBypassed = ! fading && wetGate.getCurrentValue() < 0.5f;
        if (fading || fullyBypassed)
        {
            dryBuffer.copyFrom (0, 0, l, n);
            dryBuffer.copyFrom (1, 0, r, n);
        }

        distortion.process (l, r, n);   // keeps running while bypassed: no jumps on return

        if (fullyBypassed)
        {
            juce::FloatVectorOperations::copy (l, dryBuffer.getReadPointer (0), n);
            juce::FloatVectorOperations::copy (r, dryBuffer.getReadPointer (1), n);
        }
        else if (fading)
        {
            const float* dl = dryBuffer.getReadPointer (0);
            const float* dr = dryBuffer.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float g = wetGate.getNextValue();
                l[i] = dl[i] + g * (l[i] - dl[i]);
                r[i] = dr[i] + g * (r[i] - dr[i]);
            }
        }
        // otherwise: fully active, the output is the module's output untouched
    }

    const bool exciting = amount > 0.0f && wetGate.getTargetValue() > 0.5f;
    gui.exciteGain.store (exciting ? distortion.exciter.getLastGain() : 0.0f);
}

//==============================================================================
void CrushAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", gui.editorWidth.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CrushAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            gui.editorWidth.store ((int) state.getProperty ("editorWidth", 0));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* CrushAudioProcessor::createEditor()
{
    return new CrushAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrushAudioProcessor();
}
