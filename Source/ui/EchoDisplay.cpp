#include "EchoDisplay.h"

#include "Theme.h"

namespace onirique
{

using namespace theme;

namespace
{
    const Colour electric { 0xff5fd2ff };
    const Colour warm { 0xffff6a45 };

    constexpr float kFloorDb = -48.0f;

    float heightOf (float level) noexcept
    {
        if (level <= 0.0f)
            return 0.0f;

        return jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (level, -100.0f) - kFloorDb) / -kFloorDb);
    }

    float smoothstep (float a, float b, float x) noexcept
    {
        const auto t = jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    String timeText (float ms)
    {
        return ms >= 1000.0f ? String (ms / 1000.0f, 2) + " s" : String (roundToInt (ms)) + " ms";
    }

    /** Reels of tape: a hub, three spokes, and the tape wound round them. */
    void drawReel (Graphics& g, Point<float> centre, float radius, float wound, float angle, Colour colour, float alpha)
    {
        g.setColour (Colour (0xff120607).withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

        // The tape on it, a dark brown ring that grows and shrinks.
        const auto tape = radius * (0.45f + 0.5f * wound);
        g.setColour (Colour (0xff3a1a14).withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (tape * 2.0f, tape * 2.0f).withCentre (centre));

        g.setColour (colour.withAlpha (0.55f * alpha));
        g.drawEllipse (Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.4f);

        for (int k = 0; k < 3; ++k)
        {
            const auto a = angle + (float) k * MathConstants<float>::twoPi / 3.0f;
            const Point<float> tip (centre.x + radius * 0.92f * std::sin (a), centre.y - radius * 0.92f * std::cos (a));
            g.setColour (colour.withAlpha (0.6f * alpha));
            g.drawLine ({ centre, tip }, 2.0f);
        }

        g.setColour (colour.withAlpha (0.9f * alpha));
        g.fillEllipse (Rectangle<float> (radius * 0.36f, radius * 0.36f).withCentre (centre));
        g.setColour (Colour (0xff120607).withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (radius * 0.14f, radius * 0.14f).withCentre (centre));
    }
}

Colour EchoDisplay::characterColour (float c)
{
    return c < 0.5f ? electric.interpolatedWith (bone, c * 2.0f) : bone.interpolatedWith (warm, (c - 0.5f) * 2.0f);
}

String EchoDisplay::zoneName (float c)
{
    if (c < 0.3f) return "PURE DIGITAL";
    if (c < 0.6f) return "DIGITAL";
    if (c < 0.86f) return "TAPE";
    return "WORN TAPE";
}

EchoDisplay::EchoDisplay (EchoBoxProcessor& p) : processor (p)
{
    setOpaque (false);
}

float EchoDisplay::lastHitAgeMs() const
{
    const auto head = processor.getTraceHead();
    const auto reach = EchoBoxProcessor::kTraceFrames - 8;

    // The last frame that rose clearly over the ones before it.
    for (int back = 0; back < reach; ++back)
    {
        const auto now = processor.getTraceFrame (head - back).in;

        if (now < 0.03f)
            continue;

        float before = 0.0f;

        for (int k = 1; k <= 4; ++k)
            before = jmax (before, processor.getTraceFrame (head - back - k).in);

        if (now > before * 1.8f + 0.01f)
            return (float) back * 1000.0f / (float) EchoBoxProcessor::kTraceRate;
    }

    return -1.0f;
}

void EchoDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (deep);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (line);
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    const auto s = processor.readSettings();
    auto inner = bounds.reduced (12.0f, 10.0f);

    auto machine = inner.removeFromLeft (jlimit (190.0f, 260.0f, inner.getWidth() * 0.22f));
    inner.removeFromLeft (12.0f);

    auto trace = inner.removeFromBottom (jlimit (44.0f, 70.0f, inner.getHeight() * 0.24f));
    inner.removeFromBottom (8.0f);

    // Time passes for the reels and the chip whether or not anything plays.
    const auto now = Time::getMillisecondCounterHiRes();
    const auto dt = lastPaint > 0.0 ? (float) jmin (0.1, (now - lastPaint) * 0.001) : 0.0f;
    lastPaint = now;

    const auto heard = jmax (ebx::kMinTimeMs, processor.getHeardTimeMs());

    if (! s.bypass)
    {
        // The reels turn the faster the shorter the time, as the tape does.
        reelAngle += dt * MathConstants<float>::twoPi * jlimit (0.15f, 3.0f, 300.0f / heard)
                     * (1.0f + 0.25f * s.wobble * processor.getWobbleNow());
        reelAngle = std::fmod (reelAngle, MathConstants<float>::twoPi);

        if (! s.freeze)
            scan = std::fmod (scan + dt * 1000.0f / heard, 1.0f);
    }

    paintMachine (g, machine, s);
    paintRepeats (g, inner, s);
    paintTrace (g, trace, s);

    if (s.bypass)
    {
        g.setColour (night.withAlpha (0.55f));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (haze);
        g.setFont (capsFont (13.0f));
        g.drawText ("BYPASS", bounds, Justification::centred, false);
    }
}

void EchoDisplay::paintMachine (Graphics& g, Rectangle<float> area, const ebx::Settings& s)
{
    const auto colour = characterColour (s.character);
    const auto tapeAlpha = smoothstep (0.4f, 0.75f, s.character);
    const auto chipAlpha = 1.0f - smoothstep (0.25f, 0.6f, s.character);

    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (area, 8.0f);

    auto inner = area.reduced (12.0f, 10.0f);

    // The time, as the machine shows it: a row of lit digits on a chip,
    // a plain number on a deck.
    auto readout = inner.removeFromTop (44.0f);
    const auto synced = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    const auto division = jlimit (0, ebx::kNumDivisions - 1, roundToInt (processor.apvts.getRawParameterValue ("division")->load()));
    const auto top = synced ? String (ebx::kDivisionNames[division]) + String::fromUTF8 (" · ") + timeText (s.timeMs)
                            : timeText (s.timeMs);

    g.setFont (monoFont (24.0f).boldened());

    if (chipAlpha > 0.0f)
    {
        g.setColour (colour.withAlpha (0.08f * chipAlpha));
        // The unlit segments behind the lit ones, as on a real display.
        String ghost;

        for (auto ch : top)
            ghost << (CharacterFunctions::isDigit (ch) ? juce_wchar ('8') : ch);

        g.drawText (ghost, readout.withTrimmedBottom (14.0f), Justification::centredLeft, false);
    }

    g.setColour (colour);
    g.drawText (top, readout.withTrimmedBottom (14.0f), Justification::centredLeft, false);

    g.setFont (capsFont (9.0f));
    g.setColour (haze);
    g.drawText (synced ? "SYNC  " + String (processor.getBpm(), 1) + " BPM" : String ("HEAD 1"),
                readout.removeFromBottom (12.0f), Justification::centredLeft, false);

    auto zone = inner.removeFromBottom (34.0f);
    inner.removeFromBottom (6.0f);
    auto picture = inner;

    // The tape deck: two reels, the tape between them, and the heads it runs past.
    const auto mask = ebx::headMask (s.heads);
    const auto pathY = picture.getBottom() - 14.0f;
    const auto headsLeft = picture.getX() + picture.getWidth() * 0.22f, headsRight = picture.getRight() - picture.getWidth() * 0.22f;

    if (tapeAlpha > 0.0f)
    {
        const auto radius = jmin (picture.getHeight() * 0.36f, picture.getWidth() * 0.2f);
        const Point<float> left (picture.getX() + radius + 4.0f, picture.getY() + radius + 2.0f);
        const Point<float> right (picture.getRight() - radius - 4.0f, picture.getY() + radius + 2.0f);
        const auto wound = 0.5f + 0.25f * std::sin ((float) Time::getMillisecondCounterHiRes() * 0.00005f);

        drawReel (g, left, radius, wound, reelAngle, colour, tapeAlpha);
        drawReel (g, right, radius, 1.0f - wound, reelAngle * 0.8f, colour, tapeAlpha);

        Path tape;
        tape.startNewSubPath (left.x - radius * 0.7f, left.y + radius * 0.7f);
        tape.lineTo (headsLeft - 8.0f, pathY);
        tape.lineTo (headsRight + 8.0f, pathY);
        tape.lineTo (right.x + radius * 0.7f, right.y + radius * 0.7f);
        g.setColour (Colour (0xff6b3a2a).withAlpha (tapeAlpha));
        g.strokePath (tape, PathStrokeType (2.0f));
    }

    // The chip: cells of memory, written one after the other, round and round.
    if (chipAlpha > 0.0f)
    {
        constexpr int columns = 12, rows = 4;
        auto grid = picture.withTrimmedBottom (26.0f).reduced (4.0f, 4.0f);
        const auto cell = jmin (grid.getWidth() / columns, grid.getHeight() / rows);
        grid = grid.withSizeKeepingCentre (cell * columns, cell * rows);

        const auto lit = (int) (scan * columns * rows);

        for (int r = 0; r < rows; ++r)
        {
            for (int k = 0; k < columns; ++k)
            {
                const auto index = r * columns + k;
                const auto since = (lit - index + columns * rows) % (columns * rows);
                const auto strength = s.freeze ? 0.55f : jmax (0.08f, 1.0f - (float) since / 10.0f);

                g.setColour (colour.withAlpha (strength * chipAlpha * 0.85f));
                g.fillRect (Rectangle<float> (grid.getX() + (float) k * cell, grid.getY() + (float) r * cell, cell, cell).reduced (1.5f));
            }
        }
    }

    // The heads, or the taps: three marks, lit when they are read.
    for (int head = 0; head < ebx::kNumHeads; ++head)
    {
        const auto on = (mask & (1 << head)) != 0;
        const auto x = headsLeft + (headsRight - headsLeft) * (float) head / 2.0f;
        const auto block = Rectangle<float> (14.0f, 10.0f).withCentre ({ x, pathY - 1.0f });

        if (on)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillRoundedRectangle (block.expanded (3.0f), 4.0f);
        }

        g.setColour (on ? colour : Colour (0xff3a2427));
        g.fillRoundedRectangle (block, 2.5f);

        g.setColour (on ? night : haze);
        g.setFont (capsFont (8.0f));
        g.drawText (String (head + 1), block, Justification::centred, false);
    }

    // Where the character stands, between its two ends.
    g.setFont (capsFont (11.0f));
    g.setColour (colour);
    g.drawText (zoneName (s.character), zone.removeFromTop (16.0f), Justification::centred, false);

    auto bar = zone.reduced (6.0f, 0.0f).withSizeKeepingCentre (zone.getWidth() - 12.0f, 4.0f).translated (0.0f, 4.0f);
    ColourGradient journey (electric, bar.getX(), 0.0f, warm, bar.getRight(), 0.0f, false);
    journey.addColour (0.5, bone);
    g.setGradientFill (journey);
    g.setOpacity (0.35f);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setOpacity (1.0f);

    const auto dot = Point<float> (bar.getX() + bar.getWidth() * s.character, bar.getCentreY());
    g.setColour (colour.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (dot));
    g.setColour (colour);
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (dot));

    g.setFont (monoFont (8.5f));
    g.setColour (haze);
    g.drawText ("DIG", Rectangle<float> (bar.getX() - 6.0f, bar.getBottom() + 3.0f, 30.0f, 10.0f), Justification::centredLeft, false);
    g.drawText ("TAPE", Rectangle<float> (bar.getRight() - 24.0f, bar.getBottom() + 3.0f, 30.0f, 10.0f), Justification::centredRight, false);
}

void EchoDisplay::paintRepeats (Graphics& g, Rectangle<float> area, const ebx::Settings& s)
{
    const auto colour = characterColour (s.character);
    const auto digital = ebx::digitalAmount (s.character), analog = ebx::analogAmount (s.character);

    g.setColour (Colour (0x30000000));
    g.fillRoundedRectangle (area, 8.0f);

    // A band at the top for the pitch, the rest for the repeats.
    auto plot = area.reduced (10.0f, 6.0f);
    const auto pitched = std::abs (s.pitch) >= 1.0f;
    auto pitchBand = pitched ? plot.removeFromTop (22.0f) : Rectangle<float>();

    const auto mid = plot.getCentreY();
    const auto halfHeight = plot.getHeight() * 0.5f - 4.0f;
    const auto spanMs = jlimit (300.0f, 10000.0f, s.timeMs * 10.5f);
    const auto left = plot.getX() + 26.0f, width = plot.getWidth() - 34.0f;
    auto xOf = [=] (float ms) { return left + width * ms / spanMs; };

    // The grid: one line for each multiple of the time, the axis between
    // the two sides.
    g.setFont (monoFont (8.5f));
    const auto labelEvery = jmax (1, (int) std::ceil (36.0f / jmax (1.0f, xOf (s.timeMs) - xOf (0.0f))));

    for (int k = 1; s.timeMs * (float) k <= spanMs; ++k)
    {
        const auto x = xOf (s.timeMs * (float) k);
        g.setColour (line);
        g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());

        if (k % labelEvery == 0)
        {
            g.setColour (haze.withAlpha (0.6f));
            g.drawText (timeText (s.timeMs * (float) k), Rectangle<float> (x + 3.0f, plot.getBottom() - 11.0f, 60.0f, 10.0f),
                        Justification::centredLeft, false);
        }
    }

    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (plot.getX(), mid, plot.getWidth(), 1.0f);

    g.setFont (capsFont (8.5f));
    g.setColour (haze);
    g.drawText ("L", Rectangle<float> (plot.getX(), plot.getY() + 2.0f, 14.0f, 12.0f), Justification::centred, false);
    g.drawText ("R", Rectangle<float> (plot.getX(), plot.getBottom() - 14.0f, 14.0f, 12.0f), Justification::centred, false);

    // The hit itself, where it all starts.
    {
        const auto x = xOf (0.0f);
        g.setColour (bone.withAlpha (0.85f));
        g.fillRect (Rectangle<float> (2.0f, 1.6f * halfHeight * heightOf (ebx::dryGain (s.mix))).withCentre ({ x, mid }));
        g.setFont (capsFont (8.0f));
        g.setColour (haze);
        g.drawText ("IN", Rectangle<float> (x - 12.0f, plot.getY() - 1.0f, 24.0f, 10.0f), Justification::centred, false);
    }

    auto repeats = ebx::predictRepeats (s, s.timeMs, spanMs);

    // As loud as they are heard: the mix and the delay out move them too.
    const auto heardAt = ebx::wetGain (s.mix) * Decibels::decibelsToGain (s.delayOutDb);

    for (auto& r : repeats)
        for (auto& level : r.level)
            level *= heardAt;
    const auto hitAge = s.bypass ? -1.0f : lastHitAgeMs();
    const auto wobble = processor.getWobbleNow() * s.wobble;

    // The space they fall into: a haze behind each, as long as the room rings.
    if (s.space > 0.01f)
    {
        const auto tail = width * jmin (1.0f, 2200.0f / spanMs) * (0.3f + 0.7f * s.space);

        for (const auto& r : repeats)
        {
            const auto x = xOf (r.timeMs);
            const auto h = heightOf (jmax (r.level[0], r.level[1])) * halfHeight * 0.7f;

            if (h <= 0.5f)
                continue;

            ColourGradient fog (colour.withAlpha (0.16f * s.space), x, 0.0f, colour.withAlpha (0.0f), x + tail, 0.0f, false);
            g.setGradientFill (fog);
            g.fillRect (Rectangle<float> (x, mid - h, jmin (tail, plot.getRight() - x), 2.0f * h));
        }
    }

    int index = 0;

    for (const auto& r : repeats)
    {
        ++index;
        const auto baseX = xOf (r.timeMs);

        // Heard just now: the cursor has just gone past it.
        auto flash = 0.0f;

        if (hitAge >= r.timeMs)
            flash = std::exp (-(hitAge - r.timeMs) / 220.0f);

        for (int side = 0; side < 2; ++side)
        {
            const auto h = heightOf (r.level[side]) * halfHeight;

            if (h < 0.75f)
                continue;

            const auto direction = side == 0 ? -1.0f : 1.0f;
            const auto lit = colour.interpolatedWith (Colours::white, 0.6f * flash);
            const auto alpha = 0.55f + 0.45f * jmax (flash, 1.0f - (float) index / 30.0f);

            // Tape: a soft mound, the wider the further back, wavering as the tape does.
            if (analog > 0.0f)
            {
                const auto x = baseX + wobble * (float) index * 1.4f * analog;
                const auto spread = 3.0f + (5.0f + 1.6f * (float) index) * analog;

                Path mound;
                mound.startNewSubPath (x - spread * 2.2f, mid);

                for (int k = 0; k <= 24; ++k)
                {
                    const auto t = -2.2f + 4.4f * (float) k / 24.0f;
                    mound.lineTo (x + t * spread, mid + direction * h * std::exp (-t * t * 0.9f));
                }

                mound.closeSubPath();

                g.setColour (lit.withAlpha (alpha * 0.5f * analog));
                g.fillPath (mound);
                g.setColour (lit.withAlpha (alpha * analog));
                g.strokePath (mound, PathStrokeType (1.2f));
            }

            // Digital: a sharp line with a dot on top, the more exact the more
            // digital, with a glow around it.
            const auto crisp = (1.0f - analog) * (1.0f - analog);

            if (crisp > 0.0f)
            {
                const auto barWidth = 2.0f + digital;
                const auto bar = side == 0 ? Rectangle<float> (baseX - barWidth * 0.5f, mid - h, barWidth, h)
                                           : Rectangle<float> (baseX - barWidth * 0.5f, mid, barWidth, h);

                g.setColour (lit.withAlpha ((0.12f + 0.12f * digital) * crisp * alpha));
                g.fillRect (bar.expanded (3.0f + 3.0f * digital, 0.0f));
                g.setColour (lit.withAlpha (crisp * alpha));
                g.fillRect (bar);

                const auto tip = Point<float> (baseX, mid + direction * h);
                g.setColour (lit.withAlpha (0.3f * digital * crisp * alpha));
                g.fillEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (tip));
                g.setColour (lit.withAlpha (crisp * alpha));
                g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (tip));
            }
        }
    }

    // The pitch: a stair, each repeat a step further up or down.
    if (pitched && ! repeats.empty())
    {
        const auto most = jmax (1.0f, std::abs (s.pitch) * jmin (12.0f, repeats.back().passes[0]));
        Path stair;
        bool started = false;
        float lastY = 0.0f;
        int labelled = 0;
        float lastLabelX = -100.0f;

        g.setFont (monoFont (8.5f));

        for (const auto& r : repeats)
        {
            const auto level = jmax (r.level[0], r.level[1]);

            if (level < 0.004f)
                break;

            const auto passes = r.level[0] >= r.level[1] ? r.passes[0] : r.passes[1];
            const auto semis = s.pitch * passes;
            const auto y = pitchBand.getCentreY() - jlimit (-1.0f, 1.0f, semis / most) * (pitchBand.getHeight() * 0.4f);
            const auto x = xOf (r.timeMs);

            if (! started)
            {
                stair.startNewSubPath (xOf (0.0f), pitchBand.getCentreY());
                started = true;
            }
            else
            {
                stair.lineTo (x, lastY);
            }

            stair.lineTo (x, y);
            lastY = y;

            // Past four octaves the repeats are out of hearing: the stair goes
            // on, the numbers stop.
            if (labelled < 10 && x - lastLabelX > 34.0f && std::abs (semis) <= 48.5f)
            {
                const auto n = roundToInt (semis);
                g.setColour (characterColour (s.character).withAlpha (0.9f));
                g.drawText ((n > 0 ? "+" : "") + String (n), Rectangle<float> (x + 2.0f, y - 11.0f, 34.0f, 10.0f),
                            Justification::centredLeft, false);
                lastLabelX = x;
                ++labelled;
            }
        }

        g.setColour (colour.withAlpha (0.8f));
        g.strokePath (stair, PathStrokeType (1.3f));
    }

    // The cursor, from the last hit.
    if (hitAge >= 0.0f && hitAge <= spanMs)
    {
        const auto x = xOf (hitAge);
        const auto fade = 1.0f - hitAge / spanMs;
        ColourGradient sweep (colour.withAlpha (0.0f), x - 40.0f, 0.0f, colour.withAlpha (0.18f * fade), x, 0.0f, false);
        g.setGradientFill (sweep);
        g.fillRect (Rectangle<float> (x - 40.0f, plot.getY(), 40.0f, plot.getHeight()));
        g.setColour (Colours::white.withAlpha (0.6f * fade));
        g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
    }

    // What is not plain to see from the marks alone.
    g.setFont (capsFont (10.0f));

    if (s.freeze)
    {
        const auto loop = s.timeMs * (float) ((ebx::headMask (s.heads) & 4) != 0 ? 3 : ((ebx::headMask (s.heads) & 2) != 0 ? 2 : 1));
        const auto x0 = xOf (0.0f), x1 = xOf (jmin (loop, spanMs));

        g.setColour (Colour (0xffbfe9ff));
        g.drawText (String::fromUTF8 ("FREEZE  ∞"), plot.removeFromTop (14.0f).withTrimmedRight (6.0f), Justification::centredRight, false);
        g.fillRect (x0, area.getBottom() - 4.0f, x1 - x0, 2.0f);
    }
    else if (s.feedback > 1.0f)
    {
        g.setColour (warm);
        g.drawText ("SELF-OSC", plot.removeFromTop (14.0f).withTrimmedRight (6.0f), Justification::centredRight, false);
    }
}

void EchoDisplay::paintTrace (Graphics& g, Rectangle<float> area, const ebx::Settings& s)
{
    const auto colour = characterColour (s.character);

    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (8.0f, 4.0f);
    plot.removeFromLeft (26.0f);

    g.setFont (capsFont (8.0f));
    g.setColour (haze);
    g.drawText ("OUT", area.withWidth (30.0f), Justification::centred, false);

    const auto mid = plot.getCentreY();
    const auto half = plot.getHeight() * 0.5f;
    const auto frames = jmin (EchoBoxProcessor::kTraceFrames - 2, (int) (EchoBoxProcessor::kTraceRate * 4.0));
    const auto head = processor.getTraceHead();

    Path out, in;
    out.startNewSubPath (plot.getRight(), mid);

    // The newest on the right, the oldest falling off to the left.
    for (int k = 0; k < frames; ++k)
    {
        const auto f = processor.getTraceFrame (head - k);
        const auto x = plot.getRight() - plot.getWidth() * (float) k / (float) (frames - 1);
        out.lineTo (x, mid - heightOf (f.left) * half);
    }

    for (int k = frames - 1; k >= 0; --k)
    {
        const auto f = processor.getTraceFrame (head - k);
        const auto x = plot.getRight() - plot.getWidth() * (float) k / (float) (frames - 1);
        out.lineTo (x, mid + heightOf (f.right) * half);
    }

    out.closeSubPath();

    g.setColour (colour.withAlpha (0.35f));
    g.fillPath (out);
    g.setColour (colour.withAlpha (0.8f));
    g.strokePath (out, PathStrokeType (1.0f));

    // What came in, a thin white line over it.
    for (int k = 0; k < frames; ++k)
    {
        const auto f = processor.getTraceFrame (head - k);
        const auto x = plot.getRight() - plot.getWidth() * (float) k / (float) (frames - 1);
        const auto y = mid - heightOf (f.in) * half;

        if (k == 0)
            in.startNewSubPath (x, y);
        else
            in.lineTo (x, y);
    }

    g.setColour (bone.withAlpha (0.55f));
    g.strokePath (in, PathStrokeType (1.0f));
}

} // namespace onirique
