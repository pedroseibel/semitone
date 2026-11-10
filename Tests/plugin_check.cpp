// End-to-end checks for the Semitone plug-in.
//
//   SemitoneCheck <snapshot.png> [path/to/Semitone.vst3]
//
// 1. Runs audio through the processor directly: latency, pitch, dry/wet alignment, bypass, state.
// 2. Renders the editor to a PNG.
// 3. If given the built .vst3, loads it the way a DAW would and repeats the pitch check.

#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static void setParam (juce::AudioProcessor& p, const juce::String& name, float plainValue)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == name)
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                param->setValueNotifyingHost (ranged->convertTo0to1 (plainValue));
            else
                param->setValueNotifyingHost (plainValue);   // hosted plug-in: already 0..1
            return;
        }

    check (false, "parameter exists: " + name);
}

/** Feeds a sine through the processor and returns the output (left channel). */
static std::vector<float> runSine (juce::AudioProcessor& p, double freq, int numSamples, int blockSize, bool bypassed = false)
{
    const double sr = p.getSampleRate();
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    static long long phase = 0;

    for (int done = 0; done < numSamples; done += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) (phase + i) / sr);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v * 0.5f);
        }
        phase += blockSize;

        if (bypassed) p.processBlockBypassed (buffer, midi);
        else          p.processBlock (buffer, midi);

        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }

    return out;
}

/** Frequency from interpolated zero crossings over the last part of a signal. */
static double measureFrequency (const std::vector<float>& s, double sr)
{
    const int from = (int) s.size() / 2;
    double first = -1.0, last = -1.0;
    int count = 0;

    for (int i = from + 1; i < (int) s.size(); ++i)
        if (s[(size_t) i - 1] < 0.0f && s[(size_t) i] >= 0.0f)
        {
            const double t = (double) (i - 1) + (double) -s[(size_t) i - 1] / (double) (s[(size_t) i] - s[(size_t) i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }

    return count > 1 ? (double) (count - 1) * sr / (last - first) : 0.0;
}

static double rms (const std::vector<float>& s)
{
    double e = 0.0;
    for (int i = (int) s.size() / 2; i < (int) s.size(); ++i) e += (double) s[(size_t) i] * s[(size_t) i];
    return std::sqrt (e / (double) (s.size() / 2));
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;
    const int block = 480;

    // ---- 1. the processor itself ---------------------------------------------------
    std::cout << "Processor" << std::endl;
    {
        std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
        p->setPlayConfigDetails (2, 2, sr, block);
        p->prepareToPlay (sr, block);

        check (p->getLatencySamples() == 4096, "Smooth mode reports 4096 samples of latency (got " + juce::String (p->getLatencySamples()) + ")");

        setParam (*p, "Semitones", 7.0f);
        auto y = runSine (*p, 440.0, 96000, block);
        double f = measureFrequency (y, sr);
        check (std::abs (f - 659.255) < 0.05, "440 Hz +7 st comes out at 659.26 Hz (got " + juce::String (f, 3) + ")");
        check (std::abs (rms (y) - 0.5 / std::sqrt (2.0)) < 0.005, "level is unchanged (rms " + juce::String (rms (y), 4) + ")");

        setParam (*p, "Fine", -50.0f);
        y = runSine (*p, 440.0, 96000, block);
        f = measureFrequency (y, sr);
        check (std::abs (f - 440.0 * std::pow (2.0, 6.5 / 12.0)) < 0.05, "+7 st -50 cents comes out at 640.49 Hz (got " + juce::String (f, 3) + ")");

        setParam (*p, "Mix", 0.0f);
        y = runSine (*p, 440.0, 48000, block);
        f = measureFrequency (y, sr);
        check (std::abs (f - 440.0) < 0.01, "mix at 0% passes the dry signal (got " + juce::String (f, 3) + " Hz)");

        // 50% mix at unity pitch only stays at full level if dry and wet are sample-aligned
        setParam (*p, "Semitones", 0.0f);
        setParam (*p, "Fine", 0.0f);
        setParam (*p, "Mix", 50.0f);
        y = runSine (*p, 440.0, 48000, block);
        check (std::abs (rms (y) - 0.5 / std::sqrt (2.0)) < 0.001, "dry and wet are time-aligned (rms at 50% mix " + juce::String (rms (y), 4) + ")");

        y = runSine (*p, 440.0, 48000, block, true);
        check (std::abs (rms (y) - 0.5 / std::sqrt (2.0)) < 0.001, "bypass passes audio at full level");

        setParam (*p, "Mode", 2.0f);
        pump (200);
        check (p->getLatencySamples() == 1024, "Tight mode reports 1024 samples of latency (got " + juce::String (p->getLatencySamples()) + ")");
        setParam (*p, "Mix", 100.0f);
        setParam (*p, "Semitones", -12.0f);
        y = runSine (*p, 1000.0, 48000, block);
        f = measureFrequency (y, sr);
        check (std::abs (f - 500.0) < 0.05, "Tight mode: 1 kHz -12 st comes out at 500 Hz (got " + juce::String (f, 3) + ")");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        std::unique_ptr<juce::AudioProcessor> q (createPluginFilter());
        q->setStateInformation (state.getData(), (int) state.getSize());
        auto* a = dynamic_cast<SemitoneProcessor*> (q.get());
        check (a != nullptr && juce::roundToInt (a->apvts.getRawParameterValue ("semitones")->load()) == -12
                   && juce::roundToInt (a->apvts.getRawParameterValue ("mode")->load()) == 2,
               "saved state restores into a new instance");

        // 96 kHz: windows double so the sound and the latency in ms stay the same
        p->setPlayConfigDetails (2, 2, 96000.0, block);
        p->prepareToPlay (96000.0, block);
        check (p->getLatencySamples() == 2048, "at 96 kHz Tight mode uses a 2048-sample window");

        // ---- 2. editor snapshot ----------------------------------------------------
        if (argc > 1)
        {
            setParam (*p, "Semitones", 7.0f);
            setParam (*p, "Mode", 0.0f);
            setParam (*p, "Fine", -12.0f);
            setParam (*p, "Mix", 80.0f);
            p->setPlayConfigDetails (2, 2, sr, block);
            p->prepareToPlay (sr, block);
            pump (200);

            auto snapshot = [&] (const juce::File& file, bool midMotion = false)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
                editor->setVisible (true);
                pump (300);

                if (midMotion)   // freeze the springs part-way: fader gliding, keys half-pressed, LEDs fading
                    for (auto* child : editor->getChildren())
                    {
                        if (auto* fader = dynamic_cast<ConsoleFader*> (child)) { fader->setValue (3.0, juce::dontSendNotification); fader->valueChanged(); fader->animate (0.06f); }
                        if (auto* key = dynamic_cast<KeyButton*> (child))      { key->setToggleState (! key->getToggleState(), juce::dontSendNotification); key->animate (0.03f); }
                    }

                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                const bool wrote = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
                check (wrote, "editor rendered to " + file.getFullPathName());
            };

            const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
            snapshot (file);

            // a second state: shifted down, shortest mode, switch off, output trimmed
            setParam (*p, "Semitones", -5.0f);
            setParam (*p, "Mode", 2.0f);
            setParam (*p, "Fine", 0.0f);
            setParam (*p, "Attack Lock", 0.0f);
            setParam (*p, "Mix", 100.0f);
            setParam (*p, "Output", -3.5f);
            pump (200);
            snapshot (file.getSiblingFile (file.getFileNameWithoutExtension() + "_alt.png"));
            snapshot (file.getSiblingFile (file.getFileNameWithoutExtension() + "_motion.png"), true);
        }
    }

    // ---- 3. the built VST3, loaded like a host would ----------------------------------
    if (argc > 2)
    {
        std::cout << "VST3 bundle" << std::endl;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, argv[2]);
        check (found.size() == 1, "bundle contains one plug-in");

        if (found.size() > 0)
        {
            std::cout << "        name: " << found[0]->name << ", maker: " << found[0]->manufacturerName
                      << ", category: " << found[0]->category << ", version: " << found[0]->version << std::endl;

            juce::String error;
            auto instance = format.createInstanceFromDescription (*found[0], sr, block, error);
            check (instance != nullptr, "bundle loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

            if (instance != nullptr)
            {
                instance->setPlayConfigDetails (2, 2, sr, block);
                instance->prepareToPlay (sr, block);
                check (instance->getLatencySamples() == 4096, "host sees 4096 samples of latency");
                check (instance->getParameters().size() >= 6, "host sees the parameters (" + juce::String (instance->getParameters().size()) + ")");

                // +5 semitones: normalised (5 + 12) / 24
                setParam (*instance, "Semitones", 17.0f / 24.0f);
                auto y = runSine (*instance, 440.0, 96000, block);
                const double f = measureFrequency (y, sr);
                check (std::abs (f - 440.0 * std::pow (2.0, 5.0 / 12.0)) < 0.05,
                       "through the VST3: 440 Hz +5 st comes out at 587.33 Hz (got " + juce::String (f, 3) + ")");

                check (instance->hasEditor(), "has an editor");
                instance->releaseResources();
            }
        }
    }

    std::cout << (failures == 0 ? "All checks passed." : "Some checks FAILED.") << std::endl;
    return failures == 0 ? 0 : 1;
}
