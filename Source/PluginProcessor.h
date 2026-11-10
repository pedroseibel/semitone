#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PitchEngine.h"

class SemitoneProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    enum Mode { smooth = 0, balanced, tight, numModes };

    SemitoneProcessor();
    ~SemitoneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Semitone"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Window length in samples for a mode at the current sample rate (= its latency). */
    int windowForMode (int mode) const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void delayDry (juce::AudioBuffer<float>&, int start, int num, int latency, bool writeToDryBuffer);

    std::atomic<float>* semitones = nullptr;
    std::atomic<float>* cents = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* attackLock = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* outputGain = nullptr;

    semitone::PitchEngine engines[numModes];
    int activeMode = -1;
    int rateScale = 1;
    int channels = 2;

    juce::AudioBuffer<float> delayLine, dryBuffer;
    int delayWrite = 0, delayMask = 0;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SemitoneProcessor)
};
