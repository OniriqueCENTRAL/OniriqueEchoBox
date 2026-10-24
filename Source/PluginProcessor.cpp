#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    const Identifier kPresetProperty ("preset");
    const Identifier kSlotProperty ("slot");
    const Identifier kOtherSlot ("OtherSlot");
    const Identifier kSnapshot ("Snapshot");
    const char* const kPresetExtension = ".echobox";

    /** What a preset or an A/B slot leaves as it is: how the plugin is being
        listened to and played, not what it does to the sound. */
    bool isStored (const String& id)
    {
        return id != "bypass" && id != "freeze";
    }

    /** A range that is even to the ear: each doubling of a time takes the
        same travel. */
    NormalisableRange<float> logRange (float low, float high)
    {
        return { low, high,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return jlimit (a, b, v); } };
    }

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    /** A value that can go either way says which way it went. */
    Attributes signedValue (int decimals, const String& unit)
    {
        return Attributes()
            .withStringFromValueFunction ([decimals, unit] (float v, int)
                                          {
                                              const auto text = String (v, decimals);
                                              return (v > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text + " " + unit;
                                          })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    Attributes milliseconds()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (roundToInt (v)) + " ms";
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              const auto v = parseNumber (t);
                                              return t.containsIgnoreCase ("ms") || ! t.containsIgnoreCase ("s") ? v : v * 1000.0f;
                                          });
    }

    Attributes percent()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    /** The character says where on its journey it is. */
    Attributes character()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              if (v < 50.0f)
                                                  return "DIG " + String (roundToInt ((50.0f - v) * 2.0f));
                                              if (v > 50.0f)
                                                  return "TAPE " + String (roundToInt ((v - 50.0f) * 2.0f));
                                              return String ("CLEAN");
                                          })
            .withValueFromStringFunction ([] (const String& t)
                                          {
                                              const auto v = std::abs (parseNumber (t));
                                              if (t.containsIgnoreCase ("d"))
                                                  return 50.0f - v * 0.5f;
                                              if (t.containsIgnoreCase ("t") || t.containsIgnoreCase ("a"))
                                                  return 50.0f + v * 0.5f;
                                              return t.containsIgnoreCase ("c") ? 50.0f : parseNumber (t);
                                          });
    }

    Attributes semitones()
    {
        return Attributes()
            .withStringFromValueFunction ([] (float v, int)
                                          {
                                              const auto n = roundToInt (v);
                                              return n == 0 ? String ("OFF") : (n > 0 ? "+" : "") + String (n) + " st";
                                          })
            .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("o") ? 0.0f : parseNumber (t); });
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;
        const ebx::Settings defaults;
        const NormalisableRange<float> hundred { 0.0f, 100.0f, 1.0f };

        auto number = [&layout] (const String& id, const String& name, NormalisableRange<float> range,
                                 float value, const Attributes& attributes)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, value, attributes));
        };

        // The echo.
        number ("time", "Time", logRange (ebx::kMinTimeMs, ebx::kMaxTimeMs), defaults.timeMs, milliseconds());
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", false));

        StringArray divisions;

        for (auto* name : ebx::kDivisionNames)
            divisions.add (name);

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", divisions, 8));
        number ("feedback", "Feedback", { 0.0f, ebx::kMaxFeedback * 100.0f, 1.0f }, defaults.feedback * 100.0f, percent());
        number ("mix", "Mix", hundred, defaults.mix * 100.0f, percent());
        number ("delayout", "Delay Out", { -30.0f, 6.0f, 0.1f }, defaults.delayOutDb, signedValue (1, "dB"));

        StringArray heads;

        for (auto* name : ebx::kHeadNames)
            heads.add (name);

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "heads", 1 }, "Heads", heads, defaults.heads));

        // The character, from one end to the other.
        number ("character", "Character", { 0.0f, 100.0f, 0.5f }, defaults.character * 100.0f, character());

        // The colour of the repeats.
        number ("tone", "Tone", { -100.0f, 100.0f, 1.0f }, defaults.tone * 100.0f, signedValue (0, "%"));
        number ("pitch", "Pitch", { -ebx::kMaxPitch, ebx::kMaxPitch, 1.0f }, defaults.pitch, semitones());
        number ("wobble", "Wobble", hundred, defaults.wobble * 100.0f, percent());
        number ("pingpong", "Ping-Pong", hundred, defaults.pingPong * 100.0f, percent());
        number ("space", "Space", hundred, defaults.space * 100.0f, percent());

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
        number ("out", "Output", { -24.0f, 12.0f, 0.1f }, defaults.outDb, signedValue (1, "dB"));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

        return layout;
    }

    //==============================================================================
    struct StockPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // whatever differs from the defaults
    };

    // Starting points, set by reasoning rather than by ear. Heads: 0 "1",
    // 1 "2", 2 "1+2", 3 "3", 4 "1+3", 5 "2+3", 6 "1+2+3". Divisions: 2 1/16,
    // 5 1/8, 6 1/8D, 8 1/4.
    const std::vector<StockPreset>& stockPresets()
    {
        static const std::vector<StockPreset> presets
        {
            { "Default", {} },

            // Tape.
            { "Slapback", { { "time", 110.0f }, { "feedback", 8.0f }, { "mix", 40.0f }, { "character", 78.0f },
                            { "wobble", 18.0f }, { "tone", -10.0f } } },

            { "Warm Quarter", { { "sync", 1.0f }, { "division", 8.0f }, { "feedback", 45.0f }, { "mix", 30.0f },
                                { "character", 75.0f } } },

            { "Multi-Head Tape", { { "heads", 6.0f }, { "time", 160.0f }, { "feedback", 55.0f }, { "mix", 40.0f },
                                   { "character", 85.0f }, { "wobble", 35.0f }, { "space", 30.0f } } },

            { "Worn Cassette", { { "time", 280.0f }, { "feedback", 40.0f }, { "character", 100.0f },
                                 { "wobble", 85.0f }, { "tone", -50.0f } } },

            { "Dub Siren", { { "heads", 4.0f }, { "time", 330.0f }, { "feedback", 95.0f }, { "mix", 45.0f },
                             { "character", 92.0f }, { "tone", -35.0f }, { "wobble", 45.0f }, { "space", 25.0f } } },

            { "Infinite Drone", { { "time", 450.0f }, { "feedback", 105.0f }, { "mix", 50.0f }, { "character", 88.0f },
                                  { "tone", -30.0f }, { "wobble", 40.0f }, { "space", 50.0f } } },

            { "Heads 2+3", { { "heads", 5.0f }, { "sync", 1.0f }, { "division", 2.0f }, { "feedback", 50.0f },
                             { "mix", 35.0f }, { "character", 80.0f } } },

            { "Ambient Wash", { { "time", 600.0f }, { "feedback", 80.0f }, { "mix", 45.0f }, { "character", 70.0f },
                                { "tone", -20.0f }, { "wobble", 30.0f }, { "pingpong", 40.0f }, { "space", 85.0f } } },

            // Clean, and digital.
            { "Clean Digital", { { "character", 48.0f }, { "wobble", 0.0f }, { "feedback", 40.0f }, { "mix", 30.0f } } },

            { "Dotted Eighth", { { "sync", 1.0f }, { "division", 6.0f }, { "feedback", 40.0f }, { "mix", 30.0f },
                                 { "character", 60.0f }, { "pingpong", 30.0f } } },

            { "Ping-Pong Eighths", { { "sync", 1.0f }, { "division", 5.0f }, { "feedback", 55.0f }, { "mix", 35.0f },
                                     { "character", 46.0f }, { "wobble", 0.0f }, { "pingpong", 100.0f } } },

            { "Studio Digital", { { "time", 250.0f }, { "feedback", 50.0f }, { "character", 25.0f }, { "wobble", 0.0f } } },

            { "Telephone Line", { { "time", 200.0f }, { "feedback", 35.0f }, { "character", 15.0f }, { "tone", 60.0f },
                                  { "wobble", 10.0f } } },

            { "Metallic Comb", { { "time", 22.0f }, { "feedback", 88.0f }, { "mix", 45.0f }, { "character", 40.0f },
                                 { "wobble", 0.0f } } },

            { "Robot Voice", { { "time", 25.0f }, { "feedback", 85.0f }, { "mix", 50.0f }, { "character", 3.0f },
                               { "wobble", 0.0f } } },

            { "Crystal Ping-Pong", { { "time", 90.0f }, { "feedback", 70.0f }, { "mix", 45.0f }, { "character", 0.0f },
                                     { "wobble", 0.0f }, { "pingpong", 70.0f } } },

            { "Digital Chorus Echo", { { "time", 140.0f }, { "feedback", 35.0f }, { "mix", 40.0f }, { "character", 10.0f },
                                       { "wobble", 55.0f }, { "space", 15.0f } } },

            // Pitch.
            { "Octave Cascade", { { "time", 220.0f }, { "feedback", 60.0f }, { "mix", 40.0f }, { "character", 28.0f },
                                  { "pitch", 12.0f }, { "wobble", 0.0f }, { "space", 20.0f } } },

            { "Shimmer Fifths", { { "time", 300.0f }, { "feedback", 65.0f }, { "mix", 40.0f }, { "character", 45.0f },
                                  { "pitch", 7.0f }, { "tone", 20.0f }, { "wobble", 5.0f }, { "space", 60.0f } } },

            { "Falling Down", { { "time", 180.0f }, { "feedback", 70.0f }, { "character", 35.0f }, { "pitch", -5.0f },
                                { "pingpong", 60.0f }, { "wobble", 10.0f } } },

            { "Sub Cascade", { { "time", 140.0f }, { "feedback", 55.0f }, { "character", 8.0f }, { "pitch", -12.0f },
                                { "wobble", 50.0f } } },

            { "Tape Riser", { { "time", 260.0f }, { "feedback", 72.0f }, { "mix", 40.0f }, { "character", 82.0f },
                              { "pitch", 5.0f }, { "wobble", 30.0f }, { "space", 35.0f } } },
        };

        return presets;
    }

    const StockPreset* findStock (const String& name)
    {
        for (const auto& preset : stockPresets())
            if (name.equalsIgnoreCase (preset.name))
                return &preset;

        return nullptr;
    }

    File presetFile (const String& name)
    {
        return EchoBoxProcessor::getUserPresetFolder()
                   .getChildFile (File::createLegalFileName (name) + kPresetExtension);
    }
}

/** The parameters' values, looked up once. */
struct EchoBoxProcessor::Raw
{
    explicit Raw (AudioProcessorValueTreeState& state)
    {
        auto get = [&state] (const String& id) { return state.getRawParameterValue (id); };

        time = get ("time");         sync = get ("sync");         division = get ("division");
        feedback = get ("feedback"); mix = get ("mix");           heads = get ("heads");
        delayOut = get ("delayout");
        character = get ("character"); tone = get ("tone");       pitch = get ("pitch");
        wobble = get ("wobble");     pingPong = get ("pingpong"); space = get ("space");
        freeze = get ("freeze");     out = get ("out");           bypass = get ("bypass");
    }

    std::atomic<float> *time, *sync, *division, *feedback, *mix, *delayOut, *heads, *character, *tone, *pitch,
                       *wobble, *pingPong, *space, *freeze, *out, *bypass;
};

//==============================================================================
EchoBoxProcessor::EchoBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EchoBox", createLayout()),
      raw (std::make_unique<Raw> (apvts))
{
    bypassParameter = apvts.getParameter ("bypass");

    for (size_t i = 0; i < (size_t) kTraceFrames; ++i)
    {
        traceIn[i].store (0.0f);
        traceLeft[i].store (0.0f);
        traceRight[i].store (0.0f);
    }
}

EchoBoxProcessor::~EchoBoxProcessor() = default;

bool EchoBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;

    // One channel can be spread over two; two channels cannot be folded
    // into one.
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

ebx::Settings EchoBoxProcessor::readSettings() const noexcept
{
    ebx::Settings s;
    const auto& r = *raw;

    s.timeMs = r.time->load();

    if (r.sync->load() > 0.5f)
    {
        const auto division = jlimit (0, ebx::kNumDivisions - 1, roundToInt (r.division->load()));
        s.timeMs = (float) (ebx::kDivisionBeats[division] * 60000.0 / jlimit (20.0, 400.0, bpm.load (std::memory_order_relaxed)));
    }

    s.feedback = r.feedback->load() * 0.01f;
    s.mix = r.mix->load() * 0.01f;
    s.delayOutDb = r.delayOut->load();
    s.heads = roundToInt (r.heads->load());
    s.character = r.character->load() * 0.01f;
    s.tone = r.tone->load() * 0.01f;
    s.pitch = (float) roundToInt (r.pitch->load());
    s.wobble = r.wobble->load() * 0.01f;
    s.pingPong = r.pingPong->load() * 0.01f;
    s.space = r.space->load() * 0.01f;
    s.freeze = r.freeze->load() > 0.5f;
    s.outDb = r.out->load();
    s.bypass = r.bypass->load() > 0.5f;

    return s;
}

void EchoBoxProcessor::prepareToPlay (double sampleRate, int)
{
    license.refresh();
    echo.setSettings (readSettings());
    echo.prepare (sampleRate, getMainBusNumInputChannels(), getMainBusNumOutputChannels());
    traceCountdown = 0;
    tracePeak[0] = tracePeak[1] = tracePeak[2] = 0.0f;
}

void EchoBoxProcessor::reset()
{
    echo.reset();
}

void EchoBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numIn = jmin (getMainBusNumInputChannels(), (int) ebx::kMaxChannels);
    const auto numOut = jmin (getMainBusNumOutputChannels(), (int) ebx::kMaxChannels);
    const auto numSamples = buffer.getNumSamples();

    if (numIn < 1 || numOut < 1 || buffer.getNumChannels() < jmax (numIn, numOut))
        return;

    // The tempo, for a synced time; the last one known when the host says nothing.
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto tempo = position->getBpm())
                bpm.store (*tempo, std::memory_order_relaxed);

    // The trace is cut into stretches of the same length whatever the
    // blocks are: what came in over each, measured before the echo writes
    // over it.
    const auto frameLength = jmax (1, roundToInt (getSampleRate() / kTraceRate));
    float inPeaks[256] = {};
    int numStretches = 0;

    for (int at = 0, left = traceCountdown; at < numSamples && numStretches < 256; ++numStretches)
    {
        const auto length = jlimit (1, numSamples - at, left > 0 ? left : frameLength);

        for (int c = 0; c < numIn; ++c)
            inPeaks[numStretches] = jmax (inPeaks[numStretches], buffer.getMagnitude (c, at, length));

        at += length;
        left = frameLength;
    }

    // The one channel of a mono input is also the first of the two it comes
    // out on: the echo reads each sample before it writes it.
    const float* in[ebx::kMaxChannels] = {};
    float* out[ebx::kMaxChannels] = {};

    for (int c = 0; c < numIn; ++c)
        in[c] = buffer.getReadPointer (c);

    for (int c = 0; c < numOut; ++c)
        out[c] = buffer.getWritePointer (c);

    echo.setSettings (readSettings());
    echo.process (in, out, numSamples);

    // Last, and over every output: what an unlicensed plugin lets through.
    license.process (buffer, getSampleRate(), isNonRealtime());

    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    heardTimeMs.store (echo.currentTimeMs(), std::memory_order_relaxed);
    wobbleNow.store (echo.wobbleNow(), std::memory_order_relaxed);

    // And what went out over the same stretches. A stretch that ends in
    // this block becomes a frame; the last one is carried on into the next.
    for (int at = 0, k = 0; at < numSamples && k < numStretches; ++k)
    {
        const auto length = jlimit (1, numSamples - at, traceCountdown > 0 ? traceCountdown : frameLength);

        tracePeak[0] = jmax (tracePeak[0], inPeaks[k]);
        tracePeak[1] = jmax (tracePeak[1], buffer.getMagnitude (0, at, length));
        tracePeak[2] = jmax (tracePeak[2], buffer.getMagnitude (numOut > 1 ? 1 : 0, at, length));

        at += length;
        traceCountdown = (traceCountdown > 0 ? traceCountdown : frameLength) - length;

        if (traceCountdown <= 0)
        {
            const auto next = traceHead.load (std::memory_order_relaxed) + 1;
            const auto i = (size_t) (next & (kTraceFrames - 1));

            traceIn[i].store (tracePeak[0], std::memory_order_relaxed);
            traceLeft[i].store (tracePeak[1], std::memory_order_relaxed);
            traceRight[i].store (tracePeak[2], std::memory_order_relaxed);
            traceHead.store (next, std::memory_order_release);

            tracePeak[0] = tracePeak[1] = tracePeak[2] = 0.0f;
        }
    }
}

AudioProcessorEditor* EchoBoxProcessor::createEditor()
{
    return new EchoBoxEditor (*this);
}

//==============================================================================
ValueTree EchoBoxProcessor::capture() const
{
    ValueTree snapshot (kSnapshot);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter))
            if (isStored (ranged->paramID))
                snapshot.setProperty (ranged->paramID, ranged->convertFrom0to1 (ranged->getValue()), nullptr);

    return snapshot;
}

void EchoBoxProcessor::apply (const ValueTree& snapshot)
{
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<RangedAudioParameter*> (parameter);

        if (ranged == nullptr || ! isStored (ranged->paramID))
            continue;

        // What a snapshot does not mention goes back to its default, so that
        // an old preset still means the same thing after a parameter is added.
        const auto wanted = snapshot.hasProperty (ranged->paramID)
                                ? ranged->convertTo0to1 ((float) snapshot[ranged->paramID])
                                : ranged->getDefaultValue();

        if (std::abs (wanted - ranged->getValue()) > 1.0e-6f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (wanted);
            ranged->endChangeGesture();
        }
    }
}

void EchoBoxProcessor::switchToSlot (int slot)
{
    slot = jlimit (0, 1, slot);

    if (slot == activeSlot)
        return;

    ValueTree wanted;

    {
        const ScopedLock lock (slotLock);
        slots[activeSlot] = capture();
        wanted = slots[slot];
        activeSlot = slot;
    }

    // A slot never visited starts out as a copy of the one just left.
    if (wanted.isValid())
        apply (wanted);
}

void EchoBoxProcessor::copyToOtherSlot()
{
    const ScopedLock lock (slotLock);
    slots[1 - activeSlot] = capture();
}

//==============================================================================
File EchoBoxProcessor::getUserPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
               .getChildFile ("Onirique").getChildFile ("EchoBox").getChildFile ("Presets");
}

StringArray EchoBoxProcessor::getStockPresets() const
{
    StringArray names;

    for (const auto& preset : stockPresets())
        names.add (preset.name);

    return names;
}

StringArray EchoBoxProcessor::getUserPresets() const
{
    StringArray names;

    for (const auto& file : getUserPresetFolder().findChildFiles (File::findFiles, false,
                                                                  String ("*") + kPresetExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

String EchoBoxProcessor::getPresetName() const
{
    return apvts.state.getProperty (kPresetProperty, "Default").toString();
}

void EchoBoxProcessor::setPresetName (const String& name)
{
    apvts.state.setProperty (kPresetProperty, name, nullptr);
}

bool EchoBoxProcessor::isUserPreset (const String& name) const
{
    return findStock (name) == nullptr && presetFile (name).existsAsFile();
}

void EchoBoxProcessor::loadPreset (const String& name)
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, value] : stock->values)
            snapshot.setProperty (id, value, nullptr);

        apply (snapshot);
        setPresetName (stock->name);
        return;
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
        {
            apply (snapshot);
            setPresetName (name);
        }
    }
}

String EchoBoxProcessor::saveUserPreset (const String& requested)
{
    auto name = File::createLegalFileName (requested.trim());

    if (name.isEmpty())
        return {};

    // The stock presets are not the user's to overwrite.
    if (findStock (name) != nullptr)
        name << " (user)";

    const auto file = presetFile (name);

    if (! file.getParentDirectory().createDirectory().wasOk())
        return {};

    if (const auto xml = capture().createXml())
    {
        if (xml->writeTo (file))
        {
            setPresetName (name);
            return name;
        }
    }

    return {};
}

void EchoBoxProcessor::deleteUserPreset (const String& name)
{
    if (! isUserPreset (name))
        return;

    presetFile (name).deleteFile();

    // The settings stay as they are; they are just no longer anyone's.
    if (getPresetName() == name)
        setPresetName ("Default");
}

//==============================================================================
StringArray EchoBoxProcessor::getPresetTypeNames()
{
    return { "Tape", "Digital", "Pitch", "Ambient", "Rhythmic" };
}

int EchoBoxProcessor::typesOf (const ValueTree& snapshot) const
{
    // What a snapshot does not mention is at its default, as when it is loaded.
    auto value = [&] (const char* id)
    {
        if (snapshot.hasProperty (id))
            return (float) snapshot[id];

        auto* parameter = apvts.getParameter (id);
        return parameter->convertFrom0to1 (parameter->getDefaultValue());
    };

    int types = 0;

    // One side of the middle or the other: every preset is one or the other.
    types |= value ("character") >= 50.0f ? tape : digital;

    if (std::abs (value ("pitch")) >= 1.0f)
        types |= pitched;

    // What hangs in the air: a lot of space, or repeats that hardly die.
    if (value ("space") >= 35.0f || value ("feedback") >= 80.0f)
        types |= ambient;

    // What makes a pattern: more than one head, repeats that cross over,
    // or a time on the beat.
    if (ebx::countHeads (ebx::headMask (roundToInt (value ("heads")))) > 1 || value ("pingpong") >= 50.0f || value ("sync") > 0.5f)
        types |= rhythmic;

    return types;
}

int EchoBoxProcessor::getPresetTypes (const String& name) const
{
    if (const auto* stock = findStock (name))
    {
        ValueTree snapshot (kSnapshot);

        for (const auto& [id, value] : stock->values)
            snapshot.setProperty (id, value, nullptr);

        return typesOf (snapshot);
    }

    if (const auto xml = parseXML (presetFile (name)))
    {
        const auto snapshot = ValueTree::fromXml (*xml);

        if (snapshot.hasType (kSnapshot))
            return typesOf (snapshot);
    }

    return 0;
}

StringArray EchoBoxProcessor::getFilteredPresets() const
{
    StringArray names;

    auto add = [&] (const StringArray& from)
    {
        for (const auto& name : from)
            if (typeFilter == 0 || (getPresetTypes (name) & typeFilter) != 0)
                names.add (name);
    };

    if (sourceFilter != userOnly)
        add (getStockPresets());

    if (sourceFilter != stockOnly)
        add (getUserPresets());

    return names;
}

void EchoBoxProcessor::restoreSettings (const ValueTree& settings, const String& presetName)
{
    apply (settings);
    setPresetName (presetName);
}

//==============================================================================
void EchoBoxProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();

    {
        const ScopedLock lock (slotLock);

        state.setProperty (kSlotProperty, activeSlot, nullptr);
        state.removeChild (state.getChildWithName (kOtherSlot), nullptr);

        if (slots[1 - activeSlot].isValid())
        {
            ValueTree other (kOtherSlot);
            other.copyPropertiesFrom (slots[1 - activeSlot], nullptr);
            state.appendChild (other, nullptr);
        }
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void EchoBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = ValueTree::fromXml (*xml);
    const auto other = state.getChildWithName (kOtherSlot);

    {
        const ScopedLock lock (slotLock);

        activeSlot = jlimit (0, 1, (int) state.getProperty (kSlotProperty, 0));
        slots[activeSlot] = {};
        slots[1 - activeSlot] = {};

        if (other.isValid())
        {
            ValueTree snapshot (kSnapshot);
            snapshot.copyPropertiesFrom (other, nullptr);
            slots[1 - activeSlot] = snapshot;
        }
    }

    state.removeChild (other, nullptr);
    apvts.replaceState (state);
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::EchoBoxProcessor();
}
