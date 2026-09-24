// Track FX delay probe (2026-09-08 "no feedback" investigation).
//
// Deterministic impulse-response test: a single impulse through the delay
// slot must produce echoes at exact delayTime intervals, each decaying by
// the feedback factor. Catches the whole delay family of silent-skip bugs:
// the internalParamValues.size() guard, push/pop order, feedback math, and
// the sync-derived delay clamps.

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "engine/TrackFXSlot.h"

namespace {

struct DelayProbeResult
{
    std::vector<float> out;      // concatenated render output
    int                 sampleRate = 44100;
};

// One delay slot's full configuration for a probe render: manual Delay Time or
// tempo-synced Division, plus the feedback/mix the C3 extraction moved into the
// shared InternalDelay DSP.
struct DelayProbeConfig
{
    double sampleRate = 44100.0;
    int    blockSize  = 512;
    int    blocks     = 100;
    float  delaySec   = 0.5f;    // param 0 (ignored while sync is on)
    float  feedback   = 0.3f;    // param 1
    float  mix        = 1.0f;    // param 2 (1.0 == pure echo, dry = 0)
    float  sync       = 0.0f;    // param 3 SyncToTempo
    float  division   = 0.0f;    // param 4 Division
    float  damping    = 0.0f;    // param 5 Damping (feedback-loop lowpass, 0 = off)
    double bpm        = 120.0;   // project tempo fed to the slot
};

// Render `blocks` blocks through a prepared delay slot. An impulse is placed
// at sample 0 of block 0.
DelayProbeResult renderDelayImpulse(const DelayProbeConfig& cfg)
{
    HDAW::TrackFXSlot slot("delay");
    slot.prepare({ cfg.sampleRate, (juce::uint32) cfg.blockSize, 2 });
    slot.setTempo(cfg.bpm);                       // the tempo feed Track.cpp:584 provides
    slot.setInternalParam(0, cfg.delaySec);       // Delay Time / sync-derived time
    slot.setInternalParam(1, cfg.feedback);       // Feedback
    slot.setInternalParam(2, cfg.mix);            // Mix
    slot.setInternalParam(3, cfg.sync);           // SyncToTempo
    slot.setInternalParam(4, cfg.division);       // Division
    slot.setInternalParam(5, cfg.damping);        // Damping

    DelayProbeResult r;
    r.sampleRate = (int) cfg.sampleRate;
    juce::MidiBuffer midi;
    r.out.assign((size_t) (cfg.blockSize * cfg.blocks), 0.0f);
    for (int b = 0; b < cfg.blocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, cfg.blockSize);
        buf.clear();
        if (b == 0)
        {
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);
        }
        slot.process(buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < cfg.blockSize; ++s)
                r.out[(size_t) (b * cfg.blockSize + s)] = buf.getSample(ch, s);
    }
    return r;
}

// Legacy signature: manual Delay Time, mix 1.0.
DelayProbeResult renderDelayImpulse(double sampleRate, int blockSize,
                                    float delaySec, float feedback, int blocks)
{
    DelayProbeConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.blockSize  = blockSize;
    cfg.delaySec   = delaySec;
    cfg.feedback   = feedback;
    cfg.mix        = 1.0f;
    cfg.blocks     = blocks;
    return renderDelayImpulse(cfg);
}

// Peak |sample| within +-2 of `index` (the tap lands on the sample the derived
// time rounds to; the assertion is about the tap, not the rounding rule).
float peakNear(const std::vector<float>& out, int index)
{
    float peak = 0.0f;
    for (int i = juce::jmax(0, index - 2); i <= juce::jmin((int) out.size() - 1, index + 2); ++i)
        peak = juce::jmax(peak, std::fabs(out[(size_t) i]));
    return peak;
}

} // namespace

// Echo #k must land at exactly k * delaySec with amplitude feedback^(k-1).
TEST(TrackFxDelay, ImpulseProducesDecayingEchoes)
{
    const double sr = 44100.0;
    const float delaySec = 0.25f;               // 11025 samples
    const float fb = 0.5f;
    const int blocks = 100;                     // 51200 samples > 3 echoes
    const auto r = renderDelayImpulse(sr, 512, delaySec, fb, blocks);

    const int d1 = (int) std::lround(delaySec * sr);            // 11025
    // Echo amplitudes: mix=1.0 → echo k = fb^(k-1), exact for integer delay.
    EXPECT_NEAR(r.out[(size_t) d1], 1.0f, 0.05f)      << "echo #1 @ " << d1;
    EXPECT_NEAR(r.out[(size_t) (2 * d1)], 0.5f, 0.05f) << "echo #2 @ " << 2 * d1;
    EXPECT_NEAR(r.out[(size_t) (3 * d1)], 0.25f, 0.05f) << "echo #3 @ " << 3 * d1;
    // Nothing before the first echo (dry = 0 at mix 1.0, line empty).
    for (int s = 1; s < d1 - 2; ++s)
        ASSERT_NEAR(r.out[(size_t) s], 0.0f, 1e-3f) << "pre-echo @ " << s;
}

// G-C3-1 (the extraction's regression gate): a short delay keeps the exact
// analytic tap pattern the inline DSP produced — delay 0.1 s, feedback 0.5,
// mix 1.0 → 1.0 / 0.5 / 0.25 at 0.1 / 0.2 / 0.3 s.
TEST(TrackFxDelay, ShortDelayTapsStayAnalytic)
{
    const double sr = 44100.0;
    DelayProbeConfig cfg;
    cfg.sampleRate = sr;
    cfg.delaySec   = 0.1f;      // 4410 samples
    cfg.feedback   = 0.5f;
    cfg.mix        = 1.0f;
    cfg.blocks     = 100;       // 51200 samples → 11 echoes
    const auto r = renderDelayImpulse(cfg);

    const int d1 = (int) std::lround(cfg.delaySec * sr);        // 4410
    EXPECT_NEAR(r.out[(size_t) d1], 1.0f, 0.02f)       << "echo #1 @ " << d1;
    EXPECT_NEAR(r.out[(size_t) (2 * d1)], 0.5f, 0.02f) << "echo #2 @ " << 2 * d1;
    EXPECT_NEAR(r.out[(size_t) (3 * d1)], 0.25f, 0.02f) << "echo #3 @ " << 3 * d1;
    EXPECT_NEAR(r.out[(size_t) (4 * d1)], 0.125f, 0.02f) << "echo #4 @ " << 4 * d1;
    // Pure echo (mix 1.0): the dry impulse is not in the output, and the line is
    // empty before the first tap.
    EXPECT_NEAR(r.out[0], 0.0f, 1e-6f);
    for (int s = 1; s < d1 - 2; ++s)
        ASSERT_NEAR(r.out[(size_t) s], 0.0f, 1e-3f) << "pre-echo @ " << s;
}

// The Mix param blends the dry input with the echo, the way the inline code did
// (dry = 1 - mix on the input, mix on the delayed read).
TEST(TrackFxDelay, MixBlendsDryAndWet)
{
    const double sr = 44100.0;
    DelayProbeConfig cfg;
    cfg.sampleRate = sr;
    cfg.delaySec   = 0.05f;     // 2205 samples
    cfg.feedback   = 0.0f;      // one echo only, so the blend is unambiguous
    cfg.mix        = 0.25f;
    cfg.blocks     = 20;
    const auto r = renderDelayImpulse(cfg);

    const int d1 = (int) std::lround(cfg.delaySec * sr);
    EXPECT_NEAR(r.out[0], 0.75f, 0.01f)             << "dry = 1 - mix";
    EXPECT_NEAR(r.out[(size_t) d1], 0.25f, 0.01f)   << "wet = mix";
    EXPECT_NEAR(r.out[(size_t) (2 * d1)], 0.0f, 1e-6f) << "feedback 0 must not repeat";
}

// G-C3-3: tempo sync is real on the track slot too — the derived tap follows the
// Division AND the project BPM the engine pushes (Track.cpp:584 -> setTempo).
TEST(TrackFxDelay, SyncDivisionAndTempoMoveTheTaps)
{
    const double sr = 44100.0;
    auto synced = [sr](float division, double bpm)
    {
        DelayProbeConfig cfg;
        cfg.sampleRate = sr;
        cfg.blocks     = 60;        // 30720 samples > the slowest tap below
        cfg.sync       = 1.0f;
        cfg.division   = division;
        cfg.bpm        = bpm;
        cfg.feedback   = 0.0f;      // one tap per division: unambiguous
        cfg.mix        = 1.0f;
        return renderDelayImpulse(cfg);
    };

    // Division 0 = 1/8 note, 6 = 1/4 note; beat fractions 0.125 / 0.25.
    const auto eighth120  = synced(0.0f, 120.0);   // 0.0625 s = 2756 samples
    const auto quarter120 = synced(6.0f, 120.0);   // 0.125  s = 5513 samples
    const auto quarter60  = synced(6.0f, 60.0);    // 0.25   s = 11025 samples

    EXPECT_NEAR(peakNear(eighth120.out, 2756), 1.0f, 0.02f)  << "1/8 @ 120 BPM";
    EXPECT_NEAR(peakNear(quarter120.out, 5513), 1.0f, 0.02f) << "1/4 @ 120 BPM";
    EXPECT_NEAR(peakNear(quarter60.out, 11025), 1.0f, 0.02f) << "1/4 @ 60 BPM";
    EXPECT_LT(peakNear(eighth120.out, 5513), 1e-3f)  << "division 0 tapped at the 1/4 position";
    EXPECT_LT(peakNear(quarter120.out, 2756), 1e-3f)  << "division 6 tapped at the 1/8 position";
    EXPECT_LT(peakNear(quarter60.out, 5513), 1e-3f)   << "the tempo change did not move the tap";

    // Manual mode still ignores Division/tempo entirely: SyncToTempo off means
    // param 0 seconds, whatever the BPM is.
    DelayProbeConfig manual;
    manual.sampleRate = sr;
    manual.delaySec   = 0.05f;
    manual.feedback   = 0.0f;
    manual.mix        = 1.0f;
    manual.bpm        = 60.0;
    manual.blocks     = 20;
    const auto manualTap = renderDelayImpulse(manual);
    EXPECT_NEAR(peakNear(manualTap.out, 2205), 1.0f, 0.02f) << "manual Delay Time";
}

// Damping (param 5) is a one-pole lowpass INSIDE the feedback loop — classic
// dub: repeats darken echo after echo — and it must NEVER touch the wet/dry
// output mix. A plausible bug must fail this test:
//   * filter not wired into the loop (param ignored) -> echo #2 stays at the
//     analytic fb = 0.5, contradicting the attenuated bound below;
//   * filter wired AFTER the wet mix -> echo #1 (the raw input impulse) is
//     attenuated, contradicting the raw-input bound below.
// The Damping 0 default is a hard bypass: the exact analytic taps are already
// asserted by ImpulseProducesDecayingEchoes / ShortDelayTapsStayAnalytic above
// (both render every default), so no duplicate default run here.
TEST(TrackFxDelay, DampingDarkensOnlyTheFeedbackRepeats)
{
    const double sr = 44100.0;
    DelayProbeConfig cfg;
    cfg.sampleRate = sr;
    cfg.delaySec   = 0.1f;      // 4410 samples
    cfg.feedback   = 0.5f;
    cfg.mix        = 1.0f;
    cfg.blocks     = 40;        // 20480 samples > 4 echoes

    const auto undamped = renderDelayImpulse(cfg);   // Damping 0 = hard bypass
    cfg.damping    = 1.0f;                           // fc ~ 200 Hz at the top
    const auto damped = renderDelayImpulse(cfg);

    const int d1 = (int) std::lround(cfg.delaySec * sr);        // 4410
    // Bypass path is bit-exact: the undamped render keeps the analytic taps
    // the default-param tests above pin.
    EXPECT_NEAR(undamped.out[(size_t) d1], 1.0f, 0.02f)       << "undamped echo #1";
    EXPECT_NEAR(undamped.out[(size_t) (2 * d1)], 0.5f, 0.02f) << "undamped echo #2";

    // Echo #1 is the raw input impulse — the damping filter sits between pop
    // and push, so the output mix never sees it.
    EXPECT_NEAR(damped.out[(size_t) d1], 1.0f, 0.02f) << "damped echo #1 must stay the raw input";
    // Later echoes come back measurably darker: strictly below the undamped
    // analytic taps, and decaying tap over tap.
    EXPECT_LT(damped.out[(size_t) (2 * d1)], 0.5f * undamped.out[(size_t) (2 * d1)])
        << "echo #2 must be attenuated by the in-loop filter";
    EXPECT_LT(damped.out[(size_t) (3 * d1)], damped.out[(size_t) (2 * d1)])
        << "echo #3 must be darker than echo #2";
    EXPECT_LT(damped.out[(size_t) (4 * d1)], damped.out[(size_t) (3 * d1)])
        << "echo #4 must be darker than echo #3";
}
