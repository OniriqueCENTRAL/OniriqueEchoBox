// The echo without a host: where the repeats land, how loud, on which side,
// and that nothing it can be set to blows up.
#include <cstdio>
#include <functional>

#include "TestSignal.h"
#include "dsp/Echo.h"

namespace
{
constexpr double kSr = 48000.0;
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

/** Plays a signal through an echo, both sides the same, in blocks. */
std::array<std::vector<float>, 2> play (const ebx::Settings& s, const std::vector<float>& input, size_t length = 0,
                                        const std::function<void (ebx::Echo&, size_t)>& during = {})
{
    ebx::Echo echo;
    echo.setSettings (s);
    echo.prepare (kSr, 2, 2);

    length = std::max (length, input.size());
    std::array<std::vector<float>, 2> out { std::vector<float> (length), std::vector<float> (length) };
    constexpr size_t block = 256;

    for (size_t at = 0; at < length; at += block)
    {
        const auto n = std::min (block, length - at);

        for (size_t i = 0; i < n; ++i)
            out[0][at + i] = out[1][at + i] = at + i < input.size() ? input[at + i] : 0.0f;

        if (during)
            during (echo, at);

        const float* in[2] = { out[0].data() + at, out[1].data() + at };
        float* o[2] = { out[0].data() + at, out[1].data() + at };
        echo.process (in, o, (int) n);
    }

    return out;
}

ebx::Settings clean()
{
    ebx::Settings s;
    s.timeMs = 100.0f;
    s.feedback = 0.0f;
    s.mix = 1.0f;
    s.character = 0.5f;
    s.wobble = 0.0f;
    s.space = 0.0f;
    return s;
}

size_t loudestBetween (const std::vector<float>& x, size_t from, size_t to)
{
    size_t best = from;

    for (size_t i = from; i < std::min (to, x.size()); ++i)
        if (std::abs (x[i]) > std::abs (x[best]))
            best = i;

    return best;
}

float peakBetween (const std::vector<float>& x, size_t from, size_t to)
{
    float peak = 0.0f;

    for (size_t i = from; i < std::min (to, x.size()); ++i)
        peak = std::max (peak, std::abs (x[i]));

    return peak;
}

double rmsBetween (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;

    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];

    return std::sqrt (sum / (double) std::max<size_t> (1, to - from));
}

std::vector<float> impulse (size_t length)
{
    std::vector<float> x (length, 0.0f);
    x[0] = 0.5f;
    return x;
}

size_t ms (double m) { return (size_t) (m * kSr / 1000.0); }
}

int main()
{
    std::printf ("EchoBox, offline\n");

    // Clean, one head.
    {
        const auto out = play (clean(), impulse (ms (600)));
        const auto at = loudestBetween (out[0], ms (20), ms (600));
        std::printf ("    clean, 100 ms: the repeat lands at %.2f ms\n", (double) at * 1000.0 / kSr);
        check (std::abs ((double) at - (double) ms (100)) <= 2.0, "a repeat lands at the time");
        check (peakBetween (out[0], ms (150), ms (600)) < 1.0e-3f, "and with no feedback, only one");
        check (peakBetween (out[0], 0, ms (90)) < 1.0e-3f, "and nothing before it");
    }

    // Full digital: each repeat is the very sound, untouched.
    {
        auto s = clean();
        s.character = 0.0f;
        s.feedback = 0.5f;
        const auto noise = mdxtest::makeNoise (0.05, kSr, 0.5f);
        const auto out = play (s, noise, ms (300));
        double first = 0.0, second = 0.0;

        for (size_t i = 0; i < noise.size(); ++i)
        {
            first = std::max (first, (double) std::abs (out[0][ms (100) + i] - noise[i]));
            second = std::max (second, (double) std::abs (out[0][ms (200) + i] - 0.5f * noise[i]));
        }

        std::printf ("    full digital, against the input: %.1f dB on the first repeat, %.1f dB on the second\n",
                     20.0 * std::log10 (first + 1e-12), 20.0 * std::log10 (second + 1e-12));
        check (first < 1.0e-5 && second < 1.0e-5, "full digital, the repeats are the input itself, not crushed nor filtered");
    }

    // Feedback.
    {
        auto s = clean();
        s.feedback = 0.5f;
        const auto out = play (s, impulse (ms (600)));
        // By its energy; each trip round the loop also loses a little of its
        // top end, which an impulse is mostly made of.
        const auto first = rmsBetween (out[0], ms (90), ms (110)), second = rmsBetween (out[0], ms (190), ms (210));
        std::printf ("    feedback 50 %%: second repeat at %.2f of the first\n", second / first);
        check (second / first > 0.38 && second / first < 0.53, "each repeat comes back at the feedback");
    }

    // Heads.
    {
        auto s = clean();
        s.heads = 6;    // 1+2+3
        const auto out = play (s, impulse (ms (500)));
        const auto a = peakBetween (out[0], ms (95), ms (105)), b = peakBetween (out[0], ms (195), ms (205)),
                   c = peakBetween (out[0], ms (295), ms (305));
        check (a > 0.1f && b > 0.1f && c > 0.1f, "three heads, three repeats at one, two and three times");
        check (peakBetween (out[0], ms (310), ms (500)) < 1.0e-3f, "and none after them without feedback");

        s.heads = 3;    // 3 alone
        const auto three = play (s, impulse (ms (500)));
        check (peakBetween (three[0], ms (90), ms (290)) < 1.0e-3f && peakBetween (three[0], ms (295), ms (305)) > 0.2f,
               "head 3 alone reads at three times");
    }

    // Ping-pong.
    {
        auto s = clean();
        s.feedback = 0.8f;
        s.pingPong = 1.0f;
        const auto out = play (s, impulse (ms (500)));
        const auto l1 = peakBetween (out[0], ms (95), ms (105)), r1 = peakBetween (out[1], ms (95), ms (105));
        const auto l2 = peakBetween (out[0], ms (195), ms (205)), r2 = peakBetween (out[1], ms (195), ms (205));
        check (l1 > 0.2f && r1 < 0.01f && r2 > 0.15f && l2 < 0.01f, "ping-pong: left first, then right");
    }

    // Freeze.
    {
        auto s = clean();
        s.feedback = 0.3f;
        s.character = 0.85f;
        s.wobble = 0.4f;
        const auto noise = mdxtest::makeNoise (1.0, kSr, 0.3f);
        const auto out = play (s, noise, ms (6000), [] (ebx::Echo& echo, size_t at)
        {
            if (at >= ms (1000))
            {
                auto frozen = clean();
                frozen.feedback = 0.3f;
                frozen.character = 0.85f;
                frozen.wobble = 0.4f;
                frozen.freeze = true;
                echo.setSettings (frozen);
            }
        });

        const auto early = rmsBetween (out[0], ms (1200), ms (2200)), late = rmsBetween (out[0], ms (5000), ms (6000));
        std::printf ("    frozen: %.1f dB after one second, %.1f dB after five\n",
                     20.0 * std::log10 (early + 1e-12), 20.0 * std::log10 (late + 1e-12));
        check (early > 0.01 && std::abs (20.0 * std::log10 (late / early)) < 1.5, "frozen, the line goes round without losing anything");
    }

    // Pitch: a tone, an octave up on its first repeat.
    {
        auto s = clean();
        s.timeMs = 300.0f;
        s.pitch = 12.0f;
        auto tone = mdxtest::makeTone (0.2, kSr, 300.0, 0.4f);
        const auto out = play (s, tone, ms (800));

        int crossings = 0;
        for (size_t i = ms (380); i < ms (480); ++i)
            crossings += (out[0][i - 1] < 0.0f) != (out[0][i] < 0.0f);

        const auto hz = crossings / 2.0 / 0.1;
        std::printf ("    300 Hz, +12: the first repeat at %.0f Hz\n", hz);
        check (std::abs (hz - 600.0) < 40.0, "pitch +12 puts the repeat an octave up");
    }

    // Dry.
    {
        auto s = clean();
        s.mix = 0.0f;
        s.feedback = 0.9f;
        s.character = 0.0f;
        const auto noise = mdxtest::makeNoise (0.5, kSr, 0.5f);
        const auto out = play (s, noise);
        double error = 0.0;

        for (size_t i = 0; i < noise.size(); ++i)
            error = std::max (error, (double) std::abs (out[0][i] - noise[i]));

        check (error < 1.0e-6, "mix at 0 is the input untouched");

        s.mix = 1.0f;
        s.bypass = true;
        const auto bypassed = play (s, noise);
        error = 0.0;

        for (size_t i = 0; i < noise.size(); ++i)
            error = std::max (error, (double) std::abs (bypassed[1][i] - noise[i]));

        check (error < 1.0e-6, "and so is bypass");
    }

    // Nothing blows up, from one end of the character to the other, fed
    // back past the top.
    {
        const auto noise = mdxtest::makeNoise (1.0, kSr, 0.9f);
        float highest = 0.0f;
        bool finite = true;

        for (float character = 0.0f; character <= 1.0001f; character += 0.125f)
        {
            for (int heads = 0; heads < ebx::kNumHeadChoices; heads += 3)
            {
                ebx::Settings s;
                s.timeMs = 60.0f + 300.0f * character;
                s.character = character;
                s.feedback = ebx::kMaxFeedback;
                s.heads = heads;
                s.pitch = character < 0.5f ? 7.0f : -5.0f;
                s.wobble = 1.0f;
                s.space = 1.0f;
                s.pingPong = 0.5f;
                s.mix = 1.0f;

                const auto out = play (s, noise, ms (4000));

                for (const auto& side : out)
                    for (auto v : side)
                    {
                        finite = finite && std::isfinite (v);
                        highest = std::max (highest, std::abs (v));
                    }
            }
        }

        std::printf ("    the loudest, fed back at 110 %% with everything up: %.1f dB\n", 20.0 * std::log10 (highest + 1e-12));
        check (finite && highest < 1.5f, "nothing blows up");
    }

    // The drawing agrees with the sound.
    {
        auto s = clean();
        s.feedback = 0.5f;
        s.heads = 2;    // 1+2
        const auto repeats = ebx::predictRepeats (s, 100.0f, 1000.0f);
        check (repeats.size() == 10 && repeats[0].level[0] > 0.0f && repeats[1].level[0] > repeats[0].level[0] * 0.9f,
               "the drawing has a repeat at each multiple of the time");
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
