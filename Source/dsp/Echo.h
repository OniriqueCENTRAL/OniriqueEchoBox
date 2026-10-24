// Onirique EchoBox — the echo itself: three heads on one line, from digital to tape
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ebx
{

constexpr int kMaxChannels = 2;
constexpr int kNumHeads = 3;

constexpr float kMinTimeMs = 20.0f;
constexpr float kMaxTimeMs = 2000.0f;

/** The furthest back any head reads, whatever the time and the heads. */
constexpr float kMaxLineSeconds = 10.0f;

constexpr float kMaxFeedback = 1.1f;
constexpr float kMaxPitch = 12.0f;

/** The heads, as a choice: index + 1 is the mask, head 1 being bit 0. The
    heads sit on the tape at one, two and three times the time. */
constexpr int kNumHeadChoices = 7;
inline const char* const kHeadNames[kNumHeadChoices] = { "1", "2", "1+2", "3", "1+3", "2+3", "1+2+3" };

inline int headMask (int choice) noexcept { return (choice < 0 || choice >= kNumHeadChoices) ? 1 : choice + 1; }

/** Note values for the sync, in quarter notes. */
constexpr int kNumDivisions = 13;
inline const char* const kDivisionNames[kNumDivisions] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                                          "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
inline const float kDivisionBeats[kNumDivisions] = { 0.125f, 1.0f / 6.0f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f,
                                                     2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

/** Everything the echo is told, in plain units. */
struct Settings
{
    float timeMs = 375.0f;      // where head 1 reads
    float feedback = 0.42f;     // 0 .. kMaxFeedback; over 1, it sings by itself
    float mix = 0.35f;          // 0 dry .. 1 echoes alone
    float character = 0.72f;    // 0 pure digital .. 0.5 the turn to tape .. 1 worn tape
    int heads = 0;              // a choice of kHeadNames
    float tone = 0.0f;          // -1 dark .. +1 bright and thin
    float pitch = 0.0f;         // semitones each repeat climbs or falls
    float wobble = 0.25f;       // 0 .. 1: how unsteady the time is
    float pingPong = 0.0f;      // 0 each side its own .. 1 the repeats cross over
    float space = 0.0f;         // 0 .. 1: the reverb the repeats fall into
    bool freeze = false;        // what is on the line goes round and round, untouched
    float delayOutDb = 0.0f;    // the level of the echoes alone, before the mix
    float outDb = 0.0f;
    bool bypass = false;
};

//==============================================================================
// The character knob. Below the middle it is digital, the more exact the
// further left: at 0 each repeat is the very sound it came from, with
// nothing filtered and nothing coloured. Above the middle it is tape, the
// more worn the further right.
inline float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

inline float digitalAmount (float character) noexcept { return clamp01 ((0.5f - character) * 2.0f); }
inline float analogAmount (float character) noexcept  { return clamp01 ((character - 0.5f) * 2.0f); }

/** The high cut in the loop, which each repeat goes through once more: gone
    altogether at the digital end (above what the loop filters at all), a
    gentle 18 kHz in the middle, down to 2.6 kHz on worn tape. Tone moves it. */
inline float loopLowpassHz (const Settings& s) noexcept
{
    const auto d = digitalAmount (s.character), a = analogAmount (s.character);
    auto hz = a > 0.0f ? 18000.0f * std::pow (2600.0f / 18000.0f, std::pow (a, 0.85f))
                       : 18000.0f * std::pow (4.0f, d);

    hz *= std::pow (2.0f, s.tone < 0.0f ? 2.6f * s.tone : 0.8f * s.tone);
    return hz;
}

/** Above this, the loop's high cut is left out. */
inline float openLowpassHz (float sampleRate) noexcept { return 0.45f * sampleRate; }

/** And the low cut: none at the digital end, tape thins out the repeats,
    and a bright tone thins them whatever the character. */
inline float loopHighpassHz (const Settings& s) noexcept
{
    const auto d = digitalAmount (s.character), a = analogAmount (s.character);
    const auto base = (25.0f * (1.0f - d) + 125.0f * a) * std::pow (2.0f, s.tone < 0.0f ? 0.6f * s.tone : 2.2f * s.tone);
    return base + (s.tone > 0.0f ? 350.0f * s.tone * std::sqrt (s.tone) : 0.0f);
}

/** How hard tape is driven: a small signal is passed at its level, a loud
    one is rounded off. */
inline float tapeDrive (float character) noexcept { return 1.0f + 2.6f * analogAmount (character); }

inline int countHeads (int mask) noexcept { return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1); }

/** What comes back in the loop, for each head, so that the loop as a whole
    returns `feedback` of what went in. */
inline float feedbackPerHead (const Settings& s) noexcept { return s.feedback / (float) countHeads (headMask (s.heads)); }

/** What each head is heard at. */
inline float outputPerHead (const Settings& s) noexcept { return 1.0f / std::sqrt ((float) countHeads (headMask (s.heads))); }

/** The gain a repeat loses, beyond the feedback, to the filters and the tape
    on average: for drawing, not for the sound. */
inline float passLoss (const Settings& s) noexcept
{
    const auto a = analogAmount (s.character);
    return (1.0f - 0.1f * a) * (1.0f - 0.12f * std::abs (s.tone));
}

/** The wet and dry gains for a mix: both at full in the middle. */
inline float dryGain (float mix) noexcept { return mix < 0.5f ? 1.0f : 2.0f * (1.0f - mix); }
inline float wetGain (float mix) noexcept { return mix > 0.5f ? 1.0f : 2.0f * mix; }

//==============================================================================
/** The pitch shifter of the loop: two taps sliding along a short line,
    crossfaded so that one is always there while the other jumps back. */
class PitchShifter
{
public:
    void prepare (double sampleRate);
    void reset();
    float process (float x, float ratio) noexcept;

    /** Only keeps its line filled, for when it is not heard. */
    void feed (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
    float window = 2400.0f, phase = 0.0f;
};

/** Where the repeats fall: a small room of four lines, with a spring's chirp
    in front of it that comes in as the character turns to tape. */
class Space
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float& left, float& right, float analog) noexcept;

private:
    struct Line
    {
        std::vector<float> data;
        int length = 1, pos = 0;
        float damp = 0.0f;

        float read() const noexcept { return data[(size_t) pos]; }
        void write (float x) noexcept { data[(size_t) pos] = x; if (++pos >= length) pos = 0; }
    };

    struct Allpass
    {
        std::vector<float> data;
        int length = 1, pos = 0;

        float process (float x, float g) noexcept
        {
            const auto delayed = data[(size_t) pos];
            const auto v = x - g * delayed;
            data[(size_t) pos] = v;
            if (++pos >= length) pos = 0;
            return delayed + g * v;
        }
    };

    std::array<Line, 4> lines;
    std::array<Allpass, 4> diffusers;       // two each side
    std::array<std::array<float, 12>, 2> springState {};
    float feedback = 0.8f, dampCoefficient = 0.3f, springDamp = 0.5f;
};

//==============================================================================
/** The echo: one tape (or one memory) per side, three heads reading it at
    one, two and three times the time, a loop that goes through the
    character on its way back, and the space the repeats fall into. */
class Echo
{
public:
    void prepare (double sampleRate, int numInputs, int numOutputs);
    void reset();
    void setSettings (const Settings& s) noexcept { settings = s; }

    /** In and out may be the same buffers. A single input is fed to both
        sides when there are two outputs. */
    void process (const float* const* in, float* const* out, int numSamples) noexcept;

    /** The time head 1 is gliding towards, and where it is now, in ms. */
    float currentTimeMs() const noexcept { return (float) (glidedSamples / sampleRate * 1000.0); }

    /** The unsteadiness of the tape just now, -1 .. 1, for the reels. */
    float wobbleNow() const noexcept { return wobbleShown; }

private:
    float readLine (int channel, double delaySamples) const noexcept;
    float shape (float x, float analog, float drive) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int numIn = 2, numOut = 2;

    struct Channel
    {
        std::vector<float> line;
        int write = 0;
        float lp1 = 0.0f, lp2 = 0.0f, hp = 0.0f;
        float looped = 0.0f;                // what went back on the line last time
        PitchShifter shifter;
    };

    std::array<Channel, kMaxChannels> channels;
    Space space;
    int lineSize = 0;

    double glidedSamples = 0.0;             // head 1: gliding to its time on tape, there at once in digital
    double fadingFrom = 0.0;                // and in digital, the time it is crossfading away from
    float fade = 1.0f;                      // how far that crossfade is, 1 when there is none
    float freezeAmount = 0.0f, pitchAmount = 0.0f, bypassAmount = 0.0f;
    float outGain = 1.0f, dryAmount = 1.0f, wetAmount = 0.0f, feedbackSmoothed = 0.0f, delayOutGain = 1.0f;
    float spaceSmoothed = 0.0f, pingSmoothed = 0.0f;
    bool spaceIdle = true;

    /** A sine kept by turning a point round a circle: two multiplies a
        sample instead of a call to sin. */
    struct Rotor
    {
        float s = 0.0f, c = 1.0f, ds = 0.0f, dc = 1.0f;

        void setRate (double hz, double sampleRate) noexcept
        {
            ds = (float) std::sin (6.283185307179586 * hz / sampleRate);
            dc = (float) std::cos (6.283185307179586 * hz / sampleRate);
        }

        void step() noexcept { const auto n = s * dc + c * ds; c = c * dc - s * ds; s = n; }

        /** Rounding makes the circle drift; once a block puts it back. */
        void normalise() noexcept { const auto g = 1.0f / std::sqrt (s * s + c * c); s *= g; c *= g; }

        /** The same sine, a fixed angle ahead, given that angle's sine and cosine. */
        float ahead (float sinA, float cosA) const noexcept { return s * cosA + c * sinA; }
    };

    // The unsteadiness: the slow wow, the quick flutter and a wandering
    // drift of tape, and the even sweep of a digital modulation.
    Rotor wow, flutter, sweep;
    float drift = 0.0f, driftTarget = 0.0f, driftCountdown = 0.0f;
    float wobbleShown = 0.0f;
    uint32_t noise = 0x9e3779b9u;

    float nextNoise() noexcept
    {
        noise = noise * 1664525u + 1013904223u;
        return (float) (int32_t) noise / 2147483648.0f;
    }
};

//==============================================================================
/** The repeats an impulse makes, as the drawing shows them: one point per
    multiple of the time, each side, with how many trips round the loop make
    it up on average (which says how far its pitch has gone). */
struct Repeat
{
    float timeMs;
    float level[2];
    float passes[2];
};

std::vector<Repeat> predictRepeats (const Settings&, float timeMs, float spanMs);

} // namespace ebx
