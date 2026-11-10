#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Window lengths at 44.1/48 kHz. Doubled at 88.2/96 kHz, quadrupled above that.
    constexpr int baseWindow[SemitoneProcessor::numModes] = { 4096, 2048, 1024 };
    constexpr int maxChunk = 1024;
}

SemitoneProcessor::SemitoneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    semitones  = apvts.getRawParameterValue ("semitones");
    cents      = apvts.getRawParameterValue ("cents");
    mode       = apvts.getRawParameterValue ("mode");
    attackLock = apvts.getRawParameterValue ("attack");
    mix        = apvts.getRawParameterValue ("mix");
    outputGain = apvts.getRawParameterValue ("output");

    apvts.addParameterListener ("mode", this);
}

SemitoneProcessor::~SemitoneProcessor()
{
    apvts.removeParameterListener ("mode", this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout SemitoneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "semitones", 1 }, "Semitones",
        NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("st")
            .withStringFromValueFunction ([] (float v, int) { return String (v > 0 ? "+" : "") + String (roundToInt (v)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "cents", 1 }, "Fine",
        NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("ct")
            .withStringFromValueFunction ([] (float v, int) { return String (v > 0 ? "+" : "") + String (roundToInt (v)); })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode", StringArray { "Smooth", "Balanced", "Tight" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "attack", 1 }, "Attack Lock", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    return layout;
}

bool SemitoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

int SemitoneProcessor::windowForMode (int m) const
{
    return baseWindow[juce::jlimit (0, numModes - 1, m)] * rateScale;
}

double SemitoneProcessor::getTailLengthSeconds() const
{
    const double sr = getSampleRate();
    return sr > 0.0 ? (double) getLatencySamples() / sr : 0.0;
}

void SemitoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    rateScale = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);
    channels = juce::jmax (1, getTotalNumOutputChannels());

    for (int m = 0; m < numModes; ++m)
        engines[m].prepare (channels, baseWindow[m] * rateScale);

    const int delaySize = juce::nextPowerOfTwo (baseWindow[0] * rateScale + 1);
    delayLine.setSize (channels, delaySize);
    delayLine.clear();
    delayMask = delaySize - 1;
    delayWrite = 0;
    dryBuffer.setSize (channels, maxChunk);

    mixSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (mix->load() * 0.01f);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGain->load()));

    activeMode = juce::jlimit (0, numModes - 1, juce::roundToInt (mode->load()));
    setLatencySamples (windowForMode (activeMode));
}

void SemitoneProcessor::parameterChanged (const juce::String&, float)
{
    // The mode decides the latency. Tell the host from the message thread.
    triggerAsyncUpdate();
}

void SemitoneProcessor::handleAsyncUpdate()
{
    setLatencySamples (windowForMode (juce::roundToInt (mode->load())));
}

/** Pushes [start, start+num) of the buffer through the dry delay. The delayed signal goes
    to dryBuffer (for the wet/dry mix) or back into the buffer itself (bypass). */
void SemitoneProcessor::delayDry (juce::AudioBuffer<float>& buffer, int start, int num, int latency, bool writeToDryBuffer)
{
    for (int c = 0; c < channels; ++c)
    {
        float* line = delayLine.getWritePointer (c);
        float* io = buffer.getWritePointer (c, start);
        float* dry = dryBuffer.getWritePointer (c);
        int w = delayWrite;

        for (int i = 0; i < num; ++i)
        {
            line[w] = io[i];
            const float delayed = line[(w - latency) & delayMask];
            if (writeToDryBuffer) dry[i] = delayed; else io[i] = delayed;
            w = (w + 1) & delayMask;
        }
    }

    delayWrite = (delayWrite + num) & delayMask;
}

void SemitoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (activeMode < 0 || buffer.getNumChannels() < channels)
        return;

    for (int c = channels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int wantedMode = juce::jlimit (0, numModes - 1, juce::roundToInt (mode->load()));
    if (wantedMode != activeMode)
    {
        activeMode = wantedMode;
        engines[activeMode].reset();
    }

    auto& engine = engines[activeMode];
    const int latency = engine.getLatency();

    const double semis = (double) semitones->load() + (double) cents->load() * 0.01;
    engine.setRatio (std::pow (2.0, semis / 12.0));
    engine.setTransientLock (attackLock->load() > 0.5f);

    mixSmoothed.setTargetValue (mix->load() * 0.01f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputGain->load()));

    const int total = buffer.getNumSamples();

    for (int start = 0; start < total; start += maxChunk)
    {
        const int num = juce::jmin (maxChunk, total - start);

        delayDry (buffer, start, num, latency, true);

        float* io[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        engine.process (io, io, num);

        for (int i = 0; i < num; ++i)
        {
            const float wet = mixSmoothed.getNextValue();
            const float gain = gainSmoothed.getNextValue();

            for (int c = 0; c < channels; ++c)
                io[c][i] = gain * (wet * io[c][i] + (1.0f - wet) * dryBuffer.getReadPointer (c)[i]);
        }
    }
}

void SemitoneProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep the bypassed signal lined up with the latency the host is compensating for.
    if (activeMode < 0 || buffer.getNumChannels() < channels)
        return;

    const int latency = getLatencySamples();
    const int total = buffer.getNumSamples();

    for (int start = 0; start < total; start += maxChunk)
        delayDry (buffer, start, juce::jmin (maxChunk, total - start), juce::jmin (latency, delayMask), false);
}

juce::AudioProcessorEditor* SemitoneProcessor::createEditor()
{
    return new SemitoneEditor (*this);
}

void SemitoneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SemitoneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SemitoneProcessor();
}
