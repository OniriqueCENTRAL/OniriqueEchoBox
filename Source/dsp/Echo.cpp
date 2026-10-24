#include "Echo.h"

#include <algorithm>

namespace ebx
{

namespace
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kPi = 3.14159265358979323846f;

    float onePole (double sampleRate, double seconds) noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, seconds) * sampleRate)));
    }

    float cutoffCoefficient (float hz, double sampleRate) noexcept
    {
        return (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * hz / sampleRate));
    }

    int highestHead (int mask) noexcept { return (mask & 4) != 0 ? 3 : ((mask & 2) != 0 ? 2 : 1); }

    /** What is written on the line: digital is clean until it is about to
        go over, then it is held at the top; tape rounds everything off, the
        more so the harder it is driven. */
    float limit (float x) noexcept
    {
        const auto m = std::abs (x);

        if (m <= 0.9f)
            return x;

        const auto y = 0.9f + 0.1f * std::tanh ((m - 0.9f) * 10.0f);
        return x < 0.0f ? -y : y;
    }

    /** The echoes, all of them and their space, kept from running away at
        the extremes: untouched up to full scale, rounded off past it. */
    float ceiling (float x) noexcept
    {
        const auto m = std::abs (x);

        if (m <= 1.0f)
            return x;

        const auto y = 1.0f + 0.4f * std::tanh ((m - 1.0f) / 0.4f);
        return x < 0.0f ? -y : y;
    }
}

//==============================================================================
void PitchShifter::prepare (double sampleRate)
{
    window = (float) (0.055 * sampleRate);

    int size = 1;

    while (size < (int) window + 8)
        size <<= 1;

    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    reset();
}

void PitchShifter::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
    phase = 0.0f;
}

float PitchShifter::process (float x, float ratio) noexcept
{
    buffer[(size_t) write] = x;

    phase += (1.0f - ratio) / window;
    phase -= std::floor (phase);

    auto tap = [this] (float p)
    {
        const auto delay = p * window + 1.0f;
        const auto whole = (int) delay;
        const auto frac = delay - (float) whole;
        const auto a = buffer[(size_t) ((write - whole) & mask)];
        const auto b = buffer[(size_t) ((write - whole - 1) & mask)];
        return a + (b - a) * frac;
    };

    auto second = phase + 0.5f;
    second -= std::floor (second);

    const auto s = std::sin (kPi * phase);
    const auto g1 = s * s;
    const auto y = tap (phase) * g1 + tap (second) * (1.0f - g1);

    write = (write + 1) & mask;
    return y;
}

//==============================================================================
void Space::prepare (double sampleRate)
{
    const auto scale = sampleRate / 48000.0;
    const int lengths[] = { 1153, 1453, 1789, 2111 };
    const int diffusion[] = { 142, 379, 167, 401 };

    for (size_t i = 0; i < lines.size(); ++i)
    {
        lines[i].length = std::max (1, (int) (lengths[i] * scale));
        lines[i].data.assign ((size_t) lines[i].length, 0.0f);
    }

    for (size_t i = 0; i < diffusers.size(); ++i)
    {
        diffusers[i].length = std::max (1, (int) (diffusion[i] * scale));
        diffusers[i].data.assign ((size_t) diffusers[i].length, 0.0f);
    }

    // About two and a half seconds for the room, under two for the spring:
    // the gain each line gives back for its average length.
    const auto average = 1626.0 * scale / sampleRate;
    feedback = (float) std::pow (10.0, -3.0 * average / 2.6);
    springDamp = (float) std::pow (10.0, -3.0 * average / 1.7);

    reset();
}

void Space::reset()
{
    for (auto& l : lines)
    {
        std::fill (l.data.begin(), l.data.end(), 0.0f);
        l.pos = 0;
        l.damp = 0.0f;
    }

    for (auto& d : diffusers)
    {
        std::fill (d.data.begin(), d.data.end(), 0.0f);
        d.pos = 0;
    }

    for (auto& side : springState)
        side.fill (0.0f);
}

void Space::process (float& left, float& right, float analog) noexcept
{
    auto inL = diffusers[1].process (diffusers[0].process (left, 0.6f), 0.6f);
    auto inR = diffusers[3].process (diffusers[2].process (right, 0.6f), 0.6f);

    // A spring smears the high end out behind the low: a chain of allpasses
    // that holds the highs back, the "boing".
    if (analog > 0.001f)
    {
        float* sides[2] = { &inL, &inR };

        for (int c = 0; c < 2; ++c)
        {
            auto x = *sides[c];
            const auto dry = x;

            for (auto& z : springState[(size_t) c])
            {
                const auto y = -0.62f * x + z;
                z = x + 0.62f * y;
                x = y;
            }

            *sides[c] = dry + (x - dry) * analog;
        }
    }

    const auto g = feedback + (springDamp - feedback) * analog;
    const auto damp = 0.22f + 0.33f * analog;

    float v[4];

    for (size_t i = 0; i < 4; ++i)
        v[i] = lines[i].read();

    const auto half = 0.5f * (v[0] + v[1] + v[2] + v[3]);
    const float in[4] = { inL, inL * 0.7f, inR, inR * 0.7f };

    for (size_t i = 0; i < 4; ++i)
    {
        auto& l = lines[i];
        const auto m = (v[i] - half) * g;
        l.damp = m + (l.damp - m) * damp;
        l.write (l.damp + in[i]);
    }

    left = 0.5f * (v[0] + 0.6f * v[1] - 0.3f * v[3]);
    right = 0.5f * (v[2] + 0.6f * v[3] - 0.3f * v[1]);
}

//==============================================================================
void Echo::prepare (double rate, int inputs, int outputs)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    numIn = std::clamp (inputs, 1, kMaxChannels);
    numOut = std::clamp (outputs, 1, kMaxChannels);

    // Room for the furthest head, its unsteadiness, and the reading around it.
    lineSize = (int) (kMaxLineSeconds * sampleRate) + (int) (0.05 * sampleRate) + 8;

    for (auto& c : channels)
    {
        c.line.assign ((size_t) lineSize, 0.0f);
        c.shifter.prepare (sampleRate);
    }

    space.prepare (sampleRate);

    wow.setRate (0.55, sampleRate);
    flutter.setRate (7.1, sampleRate);
    sweep.setRate (0.8, sampleRate);

    reset();
}

void Echo::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.line.begin(), c.line.end(), 0.0f);
        c.write = 0;
        c.lp1 = c.lp2 = c.hp = 0.0f;
        c.looped = 0.0f;
        c.shifter.reset();
    }

    space.reset();

    const auto& s = settings;
    glidedSamples = std::min (std::max (s.timeMs, kMinTimeMs) * sampleRate / 1000.0,
                             (double) (kMaxLineSeconds * sampleRate) / highestHead (headMask (s.heads)));
    freezeAmount = s.freeze ? 1.0f : 0.0f;
    pitchAmount = std::abs (s.pitch) > 0.05f ? 1.0f : 0.0f;
    bypassAmount = s.bypass ? 1.0f : 0.0f;
    outGain = std::pow (10.0f, s.outDb / 20.0f);
    delayOutGain = std::pow (10.0f, s.delayOutDb / 20.0f);
    fadingFrom = glidedSamples;
    fade = 1.0f;
    spaceIdle = true;
    dryAmount = dryGain (s.mix);
    wetAmount = wetGain (s.mix);
    feedbackSmoothed = s.feedback;
    spaceSmoothed = s.space;
    pingSmoothed = s.pingPong;
    for (auto* rotor : { &wow, &flutter, &sweep })
    {
        rotor->s = 0.0f;
        rotor->c = 1.0f;
    }

    drift = driftTarget = driftCountdown = 0.0f;
}

float Echo::readLine (int channel, double delaySamples) const noexcept
{
    const auto& line = channels[(size_t) channel].line;
    const auto d = std::clamp (delaySamples, 2.0, (double) lineSize - 4.0);

    auto position = (double) channels[(size_t) channel].write - d;

    if (position < 0.0)
        position += lineSize;

    const auto whole = (int) position;
    const auto t = (float) (position - whole);

    auto at = [&] (int k)
    {
        auto i = whole + k;
        if (i < 0) i += lineSize;
        if (i >= lineSize) i -= lineSize;
        return line[(size_t) i];
    };

    // Four points, through a cubic: smooth enough for a time that moves.
    const auto y0 = at (-1), y1 = at (0), y2 = at (1), y3 = at (2);
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

float Echo::shape (float x, float analog, float drive) const noexcept
{
    const auto clean = limit (x);

    if (analog <= 0.0f)
        return clean;

    const auto tape = std::tanh (drive * x) / drive;
    return clean + (tape - clean) * analog;
}

void Echo::process (const float* const* in, float* const* out, int numSamples) noexcept
{
    const auto& s = settings;
    const auto sr = (float) sampleRate;

    const auto mask = headMask (s.heads);
    const auto top = highestHead (mask);
    const auto heard = outputPerHead (s);
    const auto perHead = 1.0f / (float) countHeads (mask);

    // Head 1's time, so that the furthest head stays on the line.
    const auto longest = (double) (kMaxLineSeconds * sampleRate) / top;
    const auto target = std::min (std::max (s.timeMs, kMinTimeMs) * sampleRate / 1000.0, longest);

    // Tape glides to a new time, its pitch bending on the way. Digital jumps
    // to it, crossfading from the old one, so that nothing bends.
    const auto glide = (double) onePole (sampleRate, 0.03 + 0.5 * s.character);
    const auto jumps = s.character < 0.5f;
    const auto fadeStep = 1.0f / (0.04f * sr);

    const auto analog = analogAmount (s.character);
    const auto drive = tapeDrive (s.character);

    // Digital, the repeats are not filtered at all: what comes back is what went in.
    const auto lowpassHz = loopLowpassHz (s);
    const auto filtered = lowpassHz < openLowpassHz (sr);
    const auto lp = cutoffCoefficient (std::min (lowpassHz, openLowpassHz (sr)), sampleRate);
    const auto hp = cutoffCoefficient (loopHighpassHz (s), sampleRate);
    const auto ratio = std::pow (2.0f, std::clamp (s.pitch, -kMaxPitch, kMaxPitch) / 12.0f);
    const auto hiss = (s.freeze ? 0.0f : 1.0f) * analog * analog * 0.00018f;

    // The unsteadiness, in samples: the slow wow and quick flutter of tape,
    // and the even sweep of a digital modulation, the two sides opposed.
    const auto w = std::pow (std::clamp (s.wobble, 0.0f, 1.0f), 1.5f);
    const auto tapeWeight = s.character, chipWeight = 1.0f - s.character;
    const auto wowDepth = w * 2.6f * 0.001f * sr * tapeWeight;
    const auto flutterDepth = w * 0.13f * 0.001f * sr * tapeWeight;
    const auto driftDepth = w * 3.5f * 0.001f * sr * tapeWeight;
    const auto sweepDepth = w * 1.4f * 0.001f * sr * chipWeight;
    const auto driftSmooth = onePole (sampleRate, 0.25);

    const auto quick = onePole (sampleRate, 0.03), slow = onePole (sampleRate, 0.08);
    const auto wantedFreeze = s.freeze ? 1.0f : 0.0f;
    const auto wantedPitch = std::abs (s.pitch) > 0.05f ? 1.0f : 0.0f;
    const auto wantedBypass = s.bypass ? 1.0f : 0.0f;
    const auto wantedOut = std::pow (10.0f, s.outDb / 20.0f);
    const auto wantedDelayOut = std::pow (10.0f, s.delayOutDb / 20.0f);
    const auto wantedDry = dryGain (s.mix), wantedWet = wetGain (s.mix);
    const auto wantedFeedback = std::clamp (s.feedback, 0.0f, kMaxFeedback);

    const auto sides = numOut;

    // The right side's wow and flutter run a little ahead of the left's.
    const auto aheadSin = std::sin (kTwoPi * 0.23f), aheadCos = std::cos (kTwoPi * 0.23f);

    for (auto* rotor : { &wow, &flutter, &sweep })
        rotor->normalise();

    for (int i = 0; i < numSamples; ++i)
    {
        if (! jumps)
            glidedSamples += (target - glidedSamples) * glide;
        else if (fade >= 1.0f && std::abs (target - glidedSamples) > 0.5)
        {
            fadingFrom = glidedSamples;
            glidedSamples = target;
            fade = 0.0f;
        }

        // Equal power: the old time and the new one are not the same sound.
        const auto fading = fade < 1.0f;
        float fadeIn = 1.0f, fadeOut = 0.0f;

        if (fading)
        {
            fade = std::min (1.0f, fade + fadeStep);
            fadeIn = std::sin (fade * kPi * 0.5f);
            fadeOut = std::cos (fade * kPi * 0.5f);
        }

        freezeAmount += (wantedFreeze - freezeAmount) * quick;
        pitchAmount += (wantedPitch - pitchAmount) * slow;
        bypassAmount += (wantedBypass - bypassAmount) * quick;
        outGain += (wantedOut - outGain) * quick;
        delayOutGain += (wantedDelayOut - delayOutGain) * quick;
        dryAmount += (wantedDry - dryAmount) * quick;
        wetAmount += (wantedWet - wetAmount) * quick;
        feedbackSmoothed += (wantedFeedback - feedbackSmoothed) * quick;
        spaceSmoothed += (s.space - spaceSmoothed) * quick;
        pingSmoothed += (s.pingPong - pingSmoothed) * quick;

        // The tape's unsteadiness.
        wow.step();
        flutter.step();
        sweep.step();

        if ((driftCountdown -= 1.0f) <= 0.0f)
        {
            driftTarget = nextNoise();
            driftCountdown = sr * (0.3f + 0.4f * (0.5f + 0.5f * nextNoise()));
        }

        drift += (driftTarget - drift) * driftSmooth;

        wobbleShown = tapeWeight * (0.7f * wow.s + 0.3f * drift) + chipWeight * sweep.s;

        // What the heads hear, run through the loop: the pitch, and the
        // filters of the tape.
        float wet[kMaxChannels] = {}, back[kMaxChannels] = {};
        const auto loopLength = std::max (2.0, std::round (glidedSamples * top));

        for (int c = 0; c < sides; ++c)
        {
            auto& ch = channels[(size_t) c];

            const auto wowC = c == 0 ? wow.s : wow.ahead (aheadSin, aheadCos);
            const auto flutterC = c == 0 ? flutter.s : flutter.ahead (aheadSin, aheadCos);
            const auto unsteady = wowDepth * wowC + flutterDepth * flutterC + driftDepth * drift
                                  + sweepDepth * (c == 0 ? sweep.s : -sweep.s);

            float z = 0.0f, was = 0.0f;

            for (int head = 1; head <= kNumHeads; ++head)
            {
                if ((mask & (1 << (head - 1))) == 0)
                    continue;

                z += readLine (c, glidedSamples * head + unsteady);

                if (fading)
                    was += readLine (c, fadingFrom * head + unsteady);
            }

            if (fading)
                z = z * fadeIn + was * fadeOut;

            if (pitchAmount > 0.0005f)
                z += (ch.shifter.process (z, ratio) - z) * pitchAmount;
            else
                ch.shifter.feed (z);

            if (filtered)
            {
                ch.lp1 += (z - ch.lp1) * lp;
                ch.lp2 += (ch.lp1 - ch.lp2) * lp;
                z = ch.lp2;
            }
            else
            {
                ch.lp1 = ch.lp2 = z;
            }

            // One pole of high cut's complement: what is left over the low cut.
            ch.hp += (z - ch.hp) * hp;
            z -= ch.hp;

            wet[c] = z * heard;
            back[c] = z * perHead * feedbackSmoothed;
        }

        // Onto the line: what came in and what came back, each side its own
        // or crossing over, through the record head. Frozen, the line is
        // left as it was and goes round on itself.
        float inputs[kMaxChannels];

        for (int c = 0; c < sides; ++c)
            inputs[c] = in[std::min (c, numIn - 1)][i];

        const auto p = sides > 1 ? pingSmoothed : 0.0f;
        const auto both = sides > 1 ? 0.5f * (inputs[0] + inputs[1]) : inputs[0];

        for (int c = 0; c < sides; ++c)
        {
            auto& ch = channels[(size_t) c];
            const auto other = sides > 1 ? back[1 - c] : back[c];
            const auto feed = c == 0 ? inputs[0] * (1.0f - p) + both * p : inputs[c] * (1.0f - p);
            const auto returned = back[c] * (1.0f - p) + other * p;

            const auto recorded = shape (feed * (1.0f - freezeAmount) + returned * (1.0f - freezeAmount) + hiss * nextNoise(),
                                         analog, drive);

            auto from = ch.write - (int) loopLength;
            if (from < 0) from += lineSize;

            const auto kept = ch.line[(size_t) from];
            ch.line[(size_t) ch.write] = recorded + (kept - recorded) * freezeAmount;

            if (++ch.write >= lineSize)
                ch.write = 0;
        }

        // The space the repeats fall into.
        auto wetL = wet[0], wetR = sides > 1 ? wet[1] : wet[0];

        // Left running while it is heard at all, so that turning it down lets
        // its tail die away; once it is not, it rests, and starts again empty.
        if (spaceSmoothed > 1.0e-4f || s.space > 0.0f)
        {
            if (spaceIdle)
            {
                space.reset();
                spaceIdle = false;
            }

            auto roomL = wetL, roomR = wetR;
            space.process (roomL, roomR, analog);

            const auto send = spaceSmoothed;
            wetL = wetL * (1.0f - 0.35f * send) + roomL * send * 1.3f;
            wetR = wetR * (1.0f - 0.35f * send) + roomR * send * 1.3f;
        }
        else
        {
            spaceIdle = true;
        }

        const float wetOut[kMaxChannels] = { ceiling (wetL) * delayOutGain, ceiling (wetR) * delayOutGain };

        for (int c = 0; c < sides; ++c)
        {
            const auto dry = inputs[c];
            const auto mixed = (dry * dryAmount + wetOut[c] * wetAmount) * outGain;
            out[c][i] = mixed + (dry - mixed) * bypassAmount;
        }
    }
}

//==============================================================================
std::vector<Repeat> predictRepeats (const Settings& s, float timeMs, float spanMs)
{
    std::vector<Repeat> repeats;

    if (timeMs <= 0.0f)
        return repeats;

    const auto mask = headMask (s.heads);
    const auto steps = std::min (400, (int) (spanMs / timeMs));

    if (steps < 1)
        return repeats;

    const auto p = s.pingPong;
    const auto perHead = s.freeze ? 1.0f / (float) countHeads (mask) : feedbackPerHead (s);
    const auto loss = s.freeze ? 1.0f : passLoss (s);
    const auto heard = outputPerHead (s);

    // What is on each line at each multiple of the time, and the number of
    // trips it has made, weighted by its level.
    std::vector<std::array<float, 2>> line ((size_t) steps + 1, { 0.0f, 0.0f }), trips ((size_t) steps + 1, { 0.0f, 0.0f });

    line[0] = { 1.0f, 1.0f - p };

    for (int k = 1; k <= steps; ++k)
    {
        float z[2] = {}, zt[2] = {};

        for (int c = 0; c < 2; ++c)
        {
            for (int head = 1; head <= kNumHeads; ++head)
            {
                if ((mask & (1 << (head - 1))) == 0 || k - head < 0)
                    continue;

                z[c] += line[(size_t) (k - head)][(size_t) c];
                zt[c] += trips[(size_t) (k - head)][(size_t) c] + line[(size_t) (k - head)][(size_t) c];
            }

            z[c] *= loss;
            zt[c] *= loss;
        }

        Repeat r;
        r.timeMs = timeMs * (float) k;

        for (int c = 0; c < 2; ++c)
        {
            r.level[c] = std::min (1.0f, z[c] * heard);
            r.passes[c] = z[c] > 1.0e-6f ? zt[c] / z[c] : 0.0f;

            const auto o = 1 - c;
            line[(size_t) k][(size_t) c] = std::min (1.2f, perHead * ((1.0f - p) * z[c] + p * z[o]));
            trips[(size_t) k][(size_t) c] = perHead * ((1.0f - p) * zt[c] + p * zt[o]);

            // Saturated, the trips are scaled with what is left of the level.
            const auto unclipped = perHead * ((1.0f - p) * z[c] + p * z[o]);
            if (unclipped > 1.2f)
                trips[(size_t) k][(size_t) c] *= 1.2f / unclipped;
        }

        repeats.push_back (r);
    }

    return repeats;
}

} // namespace ebx
