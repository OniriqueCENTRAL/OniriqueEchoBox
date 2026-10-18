// Onirique EchoBox — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseGuard.h"
#include "dsp/Echo.h"

namespace onirique
{

using namespace juce;

/** The plugin instance: parameters and the host's tempo in, a block through
    the echo, and what the editor needs to draw it: the time as it is heard,
    and a running trace of what goes in and what comes out. It also keeps
    what is not a parameter: the two settings being compared, and the presets. */
class EchoBoxProcessor final : public AudioProcessor
{
public:
    EchoBoxProcessor();
    ~EchoBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }

    // What the repeats and the space leave ringing once the input has stopped.
    double getTailLengthSeconds() const override { return 12.0; }

    AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The settings as the parameters stand now, the time worked out from
        the tempo when it is synced. */
    ebx::Settings readSettings() const noexcept;

    /** Head 1's time, as set (synced or not), and as heard just now: tape
        takes a moment to get there. */
    float getTimeMs() const noexcept { return readSettings().timeMs; }
    float getHeardTimeMs() const noexcept { return heardTimeMs.load (std::memory_order_relaxed); }
    float getWobbleNow() const noexcept { return wobbleNow.load (std::memory_order_relaxed); }
    double getBpm() const noexcept { return bpm.load (std::memory_order_relaxed); }

    //==============================================================================
    /** A trace of the levels, a frame every so often, for the editor to draw
        what has just been heard. */
    static constexpr int kTraceFrames = 1024;
    static constexpr double kTraceRate = 120.0;

    struct TraceFrame { float in, left, right; };

    /** The frame written last, counting from the start. */
    int getTraceHead() const noexcept { return traceHead.load (std::memory_order_acquire); }
    TraceFrame getTraceFrame (int index) const noexcept
    {
        const auto i = (size_t) (index & (kTraceFrames - 1));
        return { traceIn[i].load (std::memory_order_relaxed), traceLeft[i].load (std::memory_order_relaxed),
                 traceRight[i].load (std::memory_order_relaxed) };
    }

    //==============================================================================
    // A and B: two settings held side by side. Everything below is for the
    // message thread.
    int getActiveSlot() const noexcept { return activeSlot; }

    /** Puts the current settings away and brings the other ones back. */
    void switchToSlot (int slot);

    /** Makes the other slot the same as the one being heard. */
    void copyToOtherSlot();

    //==============================================================================
    // Presets. The stock ones are built in; the user's are files, one each.
    StringArray getStockPresets() const;
    StringArray getUserPresets() const;
    String getPresetName() const;
    bool isUserPreset (const String& name) const;

    void loadPreset (const String& name);

    /** Writes the current settings under a name, and returns the name it was
        given: a stock preset's name cannot be taken. */
    String saveUserPreset (const String& name);
    void deleteUserPreset (const String& name);

    static File getUserPresetFolder();

    //==============================================================================
    // What kind of echo a preset is. Nobody labels them: it is read off the
    // settings, so the user's own presets are sorted like the stock ones.
    enum PresetType { tape = 1, digital = 2, pitched = 4, ambient = 8, rhythmic = 16 };

    /** The names of the types, in the order of their bits. */
    static StringArray getPresetTypeNames();

    /** The types of a preset, as bits of PresetType; 0 if there is no such preset. */
    int getPresetTypes (const String& name) const;

    /** What the preset browser is narrowed to, kept here so that it outlives
        the editor: any of the types chosen (0 for no choice, which is all
        of them), and where a preset comes from. */
    enum PresetSource { anySource = 0, stockOnly = 1, userOnly = 2 };

    int getTypeFilter() const noexcept { return typeFilter; }
    void setTypeFilter (int types) noexcept { typeFilter = types; }
    int getSourceFilter() const noexcept { return sourceFilter; }
    void setSourceFilter (int source) noexcept { sourceFilter = source; }

    /** The presets the browser is narrowed to, the stock ones first. */
    StringArray getFilteredPresets() const;

    /** The settings as they stand, to be put back later with the name they
        went by: what the browser does when it is cancelled. */
    ValueTree getSettings() const { return capture(); }
    void restoreSettings (const ValueTree& settings, const String& presetName);

    AudioProcessorValueTreeState apvts;

    /** The licence ONIRIQUE CENTRAL wrote on this machine for this plugin, under
        the identifier the plugin has in its catalogue. */
    onirique::license::Guard license { "onirique-echobox" };

private:
    /** The values of everything a preset or a slot holds, by parameter. */
    ValueTree capture() const;
    void apply (const ValueTree&);
    void setPresetName (const String&);
    int typesOf (const ValueTree& snapshot) const;

    ebx::Echo echo;

    struct Raw;
    std::unique_ptr<Raw> raw;
    AudioProcessorParameter* bypassParameter = nullptr;

    std::atomic<double> bpm { 120.0 };
    std::atomic<float> heardTimeMs { 375.0f }, wobbleNow { 0.0f };

    std::array<std::atomic<float>, kTraceFrames> traceIn, traceLeft, traceRight;
    std::atomic<int> traceHead { 0 };
    int traceCountdown = 0;
    float tracePeak[3] = {};

    CriticalSection slotLock;
    ValueTree slots[2];
    int activeSlot = 0;
    int typeFilter = 0, sourceFilter = anySource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoBoxProcessor)
};

} // namespace onirique
