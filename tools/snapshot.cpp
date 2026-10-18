// Renders the editor to a PNG without a host, with a sound running through
// it, so a change to the interface can be looked at straight from the build.
//
//   echobox_snapshot out.png [preset] [width height]
//   echobox_snapshot out.png --browser       the preset browser open, narrowed to two types
//   echobox_snapshot --selftest      the slots, the presets and the saved state, without a window
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignal.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<onirique::EchoBoxProcessor> makeProcessor()
{
    return std::unique_ptr<onirique::EchoBoxProcessor> (static_cast<onirique::EchoBoxProcessor*> (createPluginFilter()));
}

/** Plucks: a short burst every so often, so that the echoes stand apart. */
std::vector<float> makePlucks (double seconds)
{
    auto x = mdxtest::makeVoice (kSr, seconds, 220.0);
    const auto period = (size_t) (kSr * 1.6);

    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) (i % period) / kSr;
        x[i] *= (float) (t < 0.12 ? std::exp (-t * 18.0) : 0.0);
    }

    return x;
}

/** Sets the plugin up, plays into it on the message thread one timer tick at
    a time so the screen can follow, then takes the picture and ends the
    program. */
struct Director final : juce::Timer
{
    Director (juce::File destination, const juce::String& preset, int width, int height)
        : file (std::move (destination))
    {
        processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        const auto browsing = preset == "--browser";

        if (preset.isNotEmpty() && ! browsing)
            processor->loadPreset (preset);

        signal = makePlucks (8.0);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        // This machine may hold no licence for the plugin: the activation
        // page is not what is being looked at.
        for (auto* child : editor->getChildren())
            if (dynamic_cast<onirique::license::Overlay*> (child) != nullptr)
                child->setVisible (false);

        if (browsing)
        {
            using P = onirique::EchoBoxProcessor;

            processor->setTypeFilter (P::pitched | P::rhythmic);
            processor->loadPreset ("Octave Cascade");
            echoBox = dynamic_cast<onirique::EchoBoxEditor*> (editor.get());
        }

        startTimer (30);
    }

    void timerCallback() override
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        // About a tick's worth of audio per tick.
        for (int i = 0; i < 3 && position + kBlock <= signal.size(); ++i)
        {
            buffer.copyFrom (0, 0, signal.data() + position, kBlock);
            buffer.copyFrom (1, 0, signal.data() + position, kBlock);
            processor->processBlock (buffer, midi);
            position += kBlock;
        }

        if (echoBox != nullptr && ticks == 5)
            echoBox->showBrowser();

        // Late enough for the trace to fill, and for the cursor to be on its way.
        if (++ticks > 85)
            finish();
    }

    void finish()
    {
        stopTimer();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    juce::File file;
    std::unique_ptr<onirique::EchoBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    onirique::EchoBoxEditor* echoBox = nullptr;     // only when the browser is what is shown
    std::vector<float> signal;
    size_t position = 0;
    int ticks = 0;
};
}

/** What the processor keeps that is not audio: the two slots, the presets,
    and the state a host saves; and the plugin as a host drives it, on one
    channel and on two. Returns the number of things found wrong. */
int selfTest()
{
    int failures = 0;

    auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    auto processor = makeProcessor();

    auto get = [&] (const char* id) { return processor->apvts.getRawParameterValue (id)->load(); };
    auto set = [&] (const char* id, float value)
    {
        auto* p = processor->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // A and B.
    set ("feedback", 30.0f);
    processor->switchToSlot (1);
    check (std::abs (get ("feedback") - 30.0f) < 0.01f, "B starts as a copy of A");
    set ("feedback", 70.0f);
    set ("heads", 6.0f);
    processor->switchToSlot (0);
    check (std::abs (get ("feedback") - 30.0f) < 0.01f && get ("heads") < 0.5f, "back on A, A is as it was left");
    processor->switchToSlot (1);
    check (std::abs (get ("feedback") - 70.0f) < 0.01f && std::abs (get ("heads") - 6.0f) < 0.01f, "and B as it was left");
    processor->copyToOtherSlot();
    processor->switchToSlot (0);
    check (std::abs (get ("feedback") - 70.0f) < 0.01f, "B > A makes A the same as B");

    set ("bypass", 1.0f);
    set ("freeze", 1.0f);
    set ("feedback", 20.0f);
    processor->switchToSlot (1);
    check (get ("bypass") > 0.5f && get ("freeze") > 0.5f, "switching slots leaves bypass and freeze alone");
    set ("bypass", 0.0f);
    set ("freeze", 0.0f);

    // The saved state carries both slots.
    juce::MemoryBlock saved;
    processor->getStateInformation (saved);

    auto second = makeProcessor();
    second->setStateInformation (saved.getData(), (int) saved.getSize());

    auto get2 = [&] (const char* id) { return second->apvts.getRawParameterValue (id)->load(); };
    check (second->getActiveSlot() == 1 && std::abs (get2 ("feedback") - 70.0f) < 0.01f, "a restored state is on the same slot, with its settings");
    second->switchToSlot (0);
    check (std::abs (get2 ("feedback") - 20.0f) < 0.01f, "and has the other slot too");

    // The sync: a quarter note at 120 is half a second.
    set ("sync", 1.0f);
    set ("division", 8.0f);
    check (std::abs (processor->getTimeMs() - 500.0f) < 0.5f, "synced to a quarter at 120 BPM, the time is 500 ms");
    set ("sync", 0.0f);

    // Through the plugin as a host drives it.
    {
        const auto voice = mdxtest::makeVoice (kSr, 2.0, 220.0);

        // All dry, what comes out is what went in.
        auto worst = [&] (int channelsIn, const char* preset)
        {
            auto host = makeProcessor();

            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (channelsIn == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());

            if (! host->setBusesLayout (layout))
                return 0.0;

            host->loadPreset (preset);
            host->apvts.getParameter ("mix")->setValueNotifyingHost (0.0f);

            host->setRateAndBufferSizeDetails (kSr, kBlock);
            host->prepareToPlay (kSr, kBlock);

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            double error = 0.0;

            for (size_t at = 0; at + kBlock <= voice.size(); at += kBlock)
            {
                buffer.clear();

                for (int c = 0; c < channelsIn; ++c)
                    buffer.copyFrom (c, 0, voice.data() + at, kBlock);

                host->processBlock (buffer, midi);

                for (int i = 0; i < kBlock; ++i)
                    for (int c = 0; c < 2; ++c)
                        error = std::max (error, (double) std::abs (buffer.getSample (c, i) - voice[at + (size_t) i]));
            }

            return 20.0 * std::log10 (error + 1.0e-12);
        };

        const auto mono = worst (1, "Multi-Head Tape"), stereo = worst (2, "Electric Glitch");
        std::printf ("    all dry, against the input: %.1f dB from one channel, %.1f dB from two\n", mono, stereo);
        check (mono < -120.0, "one channel, all dry, comes out on both as it went in");
        check (stereo < -120.0, "and so do two");

        auto host = makeProcessor();
        juce::AudioProcessor::BusesLayout folded;
        folded.inputBuses.add (juce::AudioChannelSet::stereo());
        folded.outputBuses.add (juce::AudioChannelSet::mono());
        check (! host->checkBusesLayoutSupported (folded), "two channels are not folded into one");
        check (host->getLatencySamples() == 0, "there is no latency");
    }

    // Every stock preset, played: none of them blows up.
    {
        const auto loud = mdxtest::makeNoise (2.0, kSr, 0.9f);
        double highest = 0.0;

        for (const auto& name : processor->getStockPresets())
        {
            auto host = makeProcessor();
            host->loadPreset (name);
            host->setPlayConfigDetails (2, 2, kSr, kBlock);
            host->prepareToPlay (kSr, kBlock);

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;

            for (size_t at = 0; at + kBlock <= loud.size() * 3; at += kBlock)
            {
                buffer.clear();

                if (at + kBlock <= loud.size())
                {
                    buffer.copyFrom (0, 0, loud.data() + at, kBlock);
                    buffer.copyFrom (1, 0, loud.data() + at, kBlock);
                }

                host->processBlock (buffer, midi);

                const auto peak = (double) buffer.getMagnitude (0, kBlock);
                highest = std::isfinite (peak) ? std::max (highest, peak) : 1.0e9;
            }
        }

        std::printf ("    the loudest any stock preset gets on noise at -1 dB: %.1f dB\n", 20.0 * std::log10 (highest + 1.0e-12));
        check (highest < 4.0, "no stock preset blows up");
    }

    // The types, read off the settings.
    {
        using P = onirique::EchoBoxProcessor;

        check (processor->getPresetTypes ("Slapback") == P::tape, "a slapback is tape");
        check (processor->getPresetTypes ("12-Bit Machine") == P::digital, "a 12-bit machine is digital");
        check (processor->getPresetTypes ("Octave Cascade") == (P::digital | P::pitched), "an octave cascade is digital, with pitch");
        check (processor->getPresetTypes ("Multi-Head Tape") == (P::tape | P::rhythmic), "three heads make a rhythm");
        check (processor->getPresetTypes ("no such preset") == 0, "a preset that is not there has no type");

        int counts[5] = {};

        for (const auto& name : processor->getStockPresets())
            for (int bit = 0; bit < 5; ++bit)
                counts[bit] += (processor->getPresetTypes (name) >> bit) & 1;

        std::printf ("    stock presets: %d tape, %d digital, %d pitch, %d ambient, %d rhythmic\n",
                     counts[0], counts[1], counts[2], counts[3], counts[4]);
        check (*std::min_element (counts, counts + 5) >= 4, "there are several stock presets of each type");
    }

    // Presets.
    check (processor->getStockPresets().size() >= 5, "there are stock presets");
    processor->loadPreset ("Dub Siren");
    check (processor->getPresetName() == "Dub Siren" && std::abs (get ("feedback") - 95.0f) < 0.01f && std::abs (get ("heads") - 4.0f) < 0.01f
               && std::abs (get ("time") - 330.0f) < 0.5f,
           "a stock preset sets what it names");
    processor->loadPreset ("Default");
    check (std::abs (get ("feedback") - 42.0f) < 0.01f && get ("heads") < 0.5f && std::abs (get ("time") - 375.0f) < 0.5f,
           "and Default puts everything back");

    const juce::String name ("zz selftest " + juce::String (juce::Time::currentTimeMillis()));
    set ("pitch", 7.0f);
    set ("space", 33.0f);
    set ("time", 512.0f);
    check (processor->saveUserPreset (name) == name && processor->getUserPresets().contains (name), "a user preset is saved under its name");
    check (processor->saveUserPreset ("Default") == "Default (user)", "a stock preset's name cannot be taken");
    processor->deleteUserPreset ("Default (user)");

    processor->loadPreset ("Default");
    processor->loadPreset (name);
    check (std::abs (get ("pitch") - 7.0f) < 0.01f && std::abs (get ("space") - 33.0f) < 0.01f && std::abs (get ("time") - 512.0f) < 1.0f,
           "and comes back as it was saved");

    processor->deleteUserPreset (name);
    check (! processor->getUserPresets().contains (name) && processor->getPresetName() == "Default", "a user preset can be deleted");
    processor->deleteUserPreset ("Dub Siren");
    check (processor->getStockPresets().contains ("Dub Siren"), "a stock preset cannot");

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const juce::ScopedJuceInitialiser_GUI gui;

    if (juce::String (argv[1]) == "--selftest")
        return selfTest();

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const auto preset = argc > 2 ? juce::String (argv[2]) : juce::String();
    const auto width = argc > 4 ? juce::String (argv[3]).getIntValue() : 1160;
    const auto height = argc > 4 ? juce::String (argv[4]).getIntValue() : 720;

    {
        Director director (file, preset, width, height);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return file.existsAsFile() ? 0 : 1;
}
