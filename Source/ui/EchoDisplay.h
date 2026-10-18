// Onirique EchoBox — the screen: the machine, the repeats it will make, and what it just did
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"

namespace onirique
{

/** Three pictures of the same echo.

    On the left, the machine itself, which turns from a memory chip into a
    tape deck as the character goes from digital to analog: the time it is
    set to, its heads, and where on that journey it stands.

    On the right, the repeats one hit will make: one mark for each, as loud
    as it comes back, sharp and stepped when digital, soft and smeared when
    tape, the left side over the right so that a ping-pong is seen to cross,
    with the pitch each one has climbed to. A cursor runs along it from the
    last hit, lighting the repeats as they are heard.

    Under them, what has just gone in and come out, scrolling.

    It only shows; nothing in it is dragged. */
class EchoDisplay final : public Component
{
public:
    explicit EchoDisplay (EchoBoxProcessor&);

    void paint (Graphics&) override;

    /** The colour of the character: electric blue, white in the middle, the
        warm red of tape. */
    static Colour characterColour (float character);

    /** Where on its journey the character stands, in a word. */
    static String zoneName (float character);

private:
    void paintMachine (Graphics&, Rectangle<float>, const ebx::Settings&);
    void paintRepeats (Graphics&, Rectangle<float>, const ebx::Settings&);
    void paintTrace (Graphics&, Rectangle<float>, const ebx::Settings&);

    /** How long ago the last hit came in, in ms, or a negative number if
        there has been none within reach of the trace. */
    float lastHitAgeMs() const;

    EchoBoxProcessor& processor;

    double lastPaint = 0.0;
    float reelAngle = 0.0f, scan = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoDisplay)
};

} // namespace onirique
