// Modified TimeStretch.h from https://github.com/dar-io-p/potenza-time-stretch (GPL-3.0, dar-io-p).
// MPC insert: absolute sample index, phase delay, pitch switch, snap to the live input.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/**
 * Akai grain stretch for this insert.
 *
 * Two alternating grains. Cyclic is the grain size, 0–2000 samples at
 * 44.1 kHz. Length is how long the audio becomes, 100–2000%. Pitch is the
 * pitch switch. Off keeps the sample's pitch and lets Length change the
 * duration. On keeps the duration and shifts pitch with Length (200% is
 * one octave up).
 *
 * Phase is the delay: 0–30 ms, 50% feedback, no level ceiling. The
 * line stores the unclamped sum, so a bright chop rings at full depth out
 * to 30 ms. It runs on the dry sample when the stretch is off, and on the
 * grains when the stretch is on.
 *
 * Grain bases are absolute sample counts (double). A float base walks off
 * the index grid after a few minutes, and rebasing it by sliding the
 * history every block is what made a slow read strobe the oldest sample.
 */
struct AkaiStretch
{
    static constexpr float kCross = 0.4f;
    static constexpr float kCyclicMax = 2000.0f;
    static constexpr float kCyclicDial = 400.0f;
    static constexpr float kLengthMin = 100.0f;
    static constexpr float kLengthMax = 2000.0f;
    static constexpr float kLengthDial = 100.0f;
    static constexpr float kPhaseMax = 30.0f;
    static constexpr int kDelayCap = 8192;

    bool grains { false };
    bool wantGrains { false };
    float blend { 0.0f };
    float grainSize { 512.0f };
    float stretchInv { 1.0f };
    float pitchComp { 1.0f };
    float grainOffset { 0.0f };
    float windowScale { 0.0f };
    float swapAt { 0.0f };
    float phase[2] { 0.0f, -1.0f };
    double grain[2] { 0.0, 0.0 };
    int playing { 0 };
    double totalPhase { 0.0 };
    int delaySamples { 0 };
    int delayWrite { 0 };
    float delayCur { 0.0f };
    float delayGlide { 1.0f };
    std::array<float, kDelayCap> delay {};

    /**
     * cyclicSamples is 0..2000 at 44.1 kHz. lengthPercent is 100..2000.
     * pitchOn is the pitch switch. phaseMs is 0..30.
     * Restarts the read. Used from reset(), not from the running block.
     */
    void setup(double hostSr, double nativeSr, double startPos,
               bool doGrains, float cyclicSamples, float lengthPercent,
               bool pitchOn, float phaseMs)
    {
        grains = doGrains;
        const double hSr = hostSr > 1.0 ? hostSr : 44100.0;
        delaySamples = phaseMs > 0.05f ? (int) std::lround((double) phaseMs * 0.001 * hSr) : 0;
        if (delaySamples > kDelayCap - 2)
            delaySamples = kDelayCap - 2;
        if (delaySamples < 0)
            delaySamples = 0;
        delayWrite = 0;
        delayCur = (float) delaySamples;
        delay.fill(0.0f);
        tuneDelayGlide(hSr);

        wantGrains = grains;
        blend = grains ? 1.0f : 0.0f;
        if (! grains)
            return;

        applyGrain(nativeSr, cyclicSamples, lengthPercent, pitchOn);
        totalPhase = startPos;
        playing = 0;
        phase[0] = 0.0f;
        phase[1] = -1.0f;
        grain[0] = startPos;
        grain[1] = startPos;
    }

    /**
     * Update grain size, length, pitch, and delay without restarting the
     * read. Grains that were already running keep their phase. The delay
     * line is cleared only when Phase goes from off to on or on to off,
     * so a stale comb does not play on the first wet step, and a running
     * comb is not wiped on every block.
     */
    void retarget(double hostSr, double nativeSr, bool doGrains,
                  float cyclicSamples, float lengthPercent, bool pitchOn, float phaseMs)
    {
        const double hSr = hostSr > 1.0 ? hostSr : 44100.0;
        tuneDelayGlide(hSr);
        int delayLen = phaseMs > 0.05f ? (int) std::lround((double) phaseMs * 0.001 * hSr) : 0;
        if (delayLen > kDelayCap - 2)
            delayLen = kDelayCap - 2;
        if (delayLen < 0)
            delayLen = 0;
        if (delayLen != delaySamples)
        {
            const bool wasOff = delaySamples < 1;
            const bool nowOff = delayLen < 1;
            delaySamples = delayLen;
            if (wasOff != nowOff)
            {
                delay.fill(0.0f);
                delayCur = nowOff ? 0.0f : (float) delayLen;
            }
            if (delayWrite < 0 || delayWrite >= kDelayCap)
                delayWrite = 0;
        }

        wantGrains = doGrains;
        if (! doGrains)
        {
            if (blend <= 0.0008f)
            {
                blend = 0.0f;
                grains = false;
            }
            return;
        }

        const bool was = grains;
        grains = true;
        applyGrain(nativeSr, cyclicSamples, lengthPercent, pitchOn);
        if (! was)
        {
            playing = 0;
            phase[0] = 0.0f;
            phase[1] = -1.0f;
            grain[0] = totalPhase;
            grain[1] = totalPhase;
        }
    }

    /** Slew the dry/grain mix. coeff is per sample, about 6 ms. */
    void glide(float coeff)
    {
        const float target = wantGrains ? 1.0f : 0.0f;
        blend += (target - blend) * coeff;
        if (! wantGrains && blend <= 0.0008f)
        {
            blend = 0.0f;
            grains = false;
        }
        else if (wantGrains && blend >= 0.999f)
            blend = 1.0f;
    }

    /**
     * Read the ring. writeAbs is one past the last sample written.
     * Samples live at absolute indices [writeAbs - cap, writeAbs).
     */
    float readRing(const float* buf, int cap, int64_t writeAbs) const
    {
        if (! grains || buf == nullptr || cap < 2 || writeAbs < 1)
            return 0.0f;
        if (phase[0] > -0.5f && phase[1] > -0.5f)
        {
            const float window = crossWindow(phase[playing]);
            const double x0 = grain[playing] + (double) phase[playing];
            const double x1 = grain[1 - playing] + (double) phase[1 - playing];
            return lerpRing(buf, cap, writeAbs, x0) * window
                 + lerpRing(buf, cap, writeAbs, x1) * (1.0f - window);
        }
        return lerpRing(buf, cap, writeAbs, grain[playing] + (double) phase[playing]);
    }

    /**
     * pitchDelta is source-samples per output sample (the voice rate).
     * sampleEnd is the exclusive end of the written audio, in the same
     * absolute index as grain[].
     *
     * The swap used to happen only once phase had already stepped past
     * swapAt. swapAt is grainSize * 0.6, which is not an integer, so the
     * crossing sample was thrown away and the new grain started one source
     * sample late. At 100% length that is one sample per grain, about 180
     * samples a second at the default Cyclic. The new grain keeps the
     * fraction of the crossing sample that landed past the swap.
     */
    void advance(float pitchDelta, double sampleEnd)
    {
        if (! grains)
            return;
        const float pd = pitchDelta * pitchComp;
        totalPhase += (double) pd * (double) stretchInv;

        const int other = 1 - playing;
        const double playPos = grain[playing] + (double) phase[playing];
        const double otherPos = grain[other] + (double) phase[other];
        const bool playMove = playPos < sampleEnd;
        const bool otherMove = phase[other] > -0.5f && otherPos < sampleEnd;
        const float playNext = phase[playing] + (playMove ? pd : 0.0f);
        const bool crossNow = playMove && phase[playing] < swapAt && playNext >= swapAt;

        if (playMove)
            phase[playing] += pd;
        if (otherMove)
            phase[other] += pd;
        if (phase[other] >= grainSize)
            phase[other] = -1.0f;

        if (crossNow || phase[playing] >= swapAt)
        {
            float over = crossNow ? playNext - swapAt : 0.0f;
            if (!(over >= 0.0f) || (swapAt > 0.0f && over >= swapAt))
                over = 0.0f;
            phase[other] = over;
            grain[other] = grain[playing] + (double) grainOffset;
            playing = other;
            if (phase[1 - playing] >= grainSize)
                phase[1 - playing] = -1.0f;
        }
    }

    /**
     * The earliest active read has fallen off the ring (before `oldest`).
     * Move both grain bases by the same amount so that read lands at
     * `newest - margin`. Phase inside each grain is kept. This fires once
     * per history of lag, not on every sample.
     * Returns 1 when it moved.
     */
    int pullIntoWindow(double oldest, double newest, double margin)
    {
        if (! grains)
            return 0;
        if (playing != 0 && playing != 1)
            playing = 0;

        const double slack = 32.0;
        double minPos = newest;
        bool any = false;
        for (int i = 0; i < 2; ++i)
        {
            if (! std::isfinite(phase[i]))
                phase[i] = (i == playing) ? 0.0f : -1.0f;
            if (! std::isfinite(grain[i]))
                grain[i] = newest > 0.0 ? newest - 1.0 : 0.0;
            if (phase[i] <= -0.5f)
                continue;
            double pos = grain[i] + (double) phase[i];
            if (! std::isfinite(pos))
            {
                grain[i] = newest > 0.0 ? newest - 1.0 : 0.0;
                phase[i] = (i == playing) ? 0.0f : -1.0f;
                continue;
            }
            if (pos > newest + slack)
            {
                grain[i] -= pos - newest;
                pos = newest;
            }
            any = true;
            if (pos < minPos)
                minPos = pos;
        }
        if (! any || !(minPos < oldest - 1.0))
            return 0;

        double target = newest - margin;
        if (target < oldest)
            target = oldest;
        if (target < 0.0)
            target = 0.0;
        const double delta = target - minPos;
        if (!(std::fabs(delta) > 1.0))
            return 0;
        grain[0] += delta;
        grain[1] += delta;
        totalPhase += delta;
        return 1;
    }

    /**
     * Park both grains on the live sample and drop the lagged read.
     * The next read is the input just written, not the stretch tail.
     * `live` is the absolute index of the newest sample already in the ring.
     */
    void snapToLive(double live)
    {
        if (! std::isfinite(live) || live < 0.0)
            live = 0.0;
        playing = 0;
        phase[0] = 0.0f;
        phase[1] = -1.0f;
        grain[0] = live;
        grain[1] = live;
        totalPhase = live;
    }

    /** Absolute source index the playing grain is reading. */
    double readPosition() const
    {
        const int p = (playing == 0 || playing == 1) ? playing : 0;
        return grain[p] + (double) phase[p];
    }

    /** Forget the phase-delay tail so a bypass does not keep ringing. */
    void discardDelay()
    {
        delay.fill(0.0f);
        delayWrite = 0;
        delayCur = 0.0f;
    }

    /**
     * While the stretch is silent, close a large gap to the live sample.
     * A sample or two of lag is normal tracking and is left alone, so this
     * does not fight advance() and does not clear phase.
     */
    void rejoinLive(double live)
    {
        if (! grains)
            return;
        if (playing != 0 && playing != 1)
            playing = 0;
        if (phase[playing] <= -0.5f)
            return;
        double pos = grain[playing] + (double) phase[playing];
        if (! std::isfinite(pos))
            pos = live;
        const double behind = live - pos;
        if (behind < 64.0 && behind > -32.0)
            return;
        grain[0] += behind;
        grain[1] += behind;
        totalPhase += behind;
    }

    float applyPhase(float dry)
    {
        if (delaySamples < 1)
        {
            delayCur = 0.0f;
            return dry;
        }
        if (! std::isfinite(dry))
            dry = 0.0f;
        // A cleared line starts at the new time. A move while the comb
        // is already on glides there, so the read pointer does not jump.
        if (delayCur < 1.0f)
            delayCur = (float) delaySamples;
        else
            delayCur += ((float) delaySamples - delayCur) * delayGlide;
        if (delayCur < 1.0f)
            delayCur = 1.0f;
        if (delayCur > (float) (kDelayCap - 2))
            delayCur = (float) (kDelayCap - 2);

        const int i0 = (int) std::floor(delayCur);
        float frac = delayCur - (float) i0;
        int i1 = i0 + 1;
        if (i1 > kDelayCap - 2)
            i1 = i0;
        if (frac < 0.0f)
            frac = 0.0f;
        const float a0 = delayTap(i0);
        const float a1 = delayTap(i1);
        const float delayed = a0 + (a1 - a0) * frac;
        // y = x + 0.5 y[n-D], stored unclamped. A ceiling here
        // flattens the comb and the 30 ms end stops sounding extreme.
        const float wet = dry + 0.5f * delayed;
        delay[(size_t) delayWrite] = std::isfinite(wet) ? wet : 0.0f;
        delayWrite = delayWrite + 1;
        if (delayWrite >= kDelayCap)
            delayWrite = 0;
        return std::isfinite(wet) ? wet : dry;
    }

private:
    void tuneDelayGlide(double hostSr)
    {
        const double sr = hostSr > 1.0 ? hostSr : 44100.0;
        const double tc = 0.040 * sr;
        delayGlide = tc > 1.0 ? (float) (1.0 - std::exp(-1.0 / tc)) : 1.0f;
    }

    float delayTap(int back) const
    {
        if (back < 1)
            back = 1;
        int read = delayWrite - back;
        while (read < 0)
            read += kDelayCap;
        if (read >= kDelayCap)
            read -= kDelayCap;
        const float s = delay[(size_t) read];
        return std::isfinite(s) ? s : 0.0f;
    }

    void applyGrain(double nativeSr, float cyclicSamples, float lengthPercent, bool pitchOn)
    {
        const double nSr = nativeSr > 1.0 ? nativeSr : 44100.0;
        float g = cyclicSamples * (float) (nSr / 44100.0);
        if (g < 1.0f)
            g = 1.0f;
        grainSize = g;
        const float factor = clampf(lengthPercent / 100.0f, 1.0f, 20.0f);
        // Off: grain rate 1, timeline 1/factor. On: grain rate factor,
        // timeline 1. 200% length with Pitch on is +12 semitones.
        pitchComp = pitchOn ? factor : 1.0f;
        stretchInv = 1.0f / factor;
        const float cPrime = 1.0f - kCross;
        windowScale = 1.0f / (grainSize * kCross);
        swapAt = grainSize * cPrime;
        grainOffset = grainSize * cPrime * stretchInv;
    }

    float crossWindow(float phi) const
    {
        // Left unclamped. Past the overlap the other grain
        // inverts, which is the bright edge the phase delay holds.
        return windowScale * phi;
    }

    static float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static float lerpRing(const float* buf, int cap, int64_t writeAbs, double pos)
    {
        if (! std::isfinite(pos))
            return 0.0f;
        const double newest = (double) (writeAbs - 1);
        const double oldest = (double) writeAbs - (double) cap;
        if (pos > newest)
            pos = newest;
        if (pos < oldest)
            pos = oldest;
        if (pos < 0.0)
            pos = 0.0;
        const int64_t i0 = (int64_t) std::floor(pos);
        int64_t i1 = i0 + 1;
        if ((double) i1 > newest)
            i1 = i0;
        float frac = (float) (pos - (double) i0);
        if (frac < 0.0f)
            frac = 0.0f;
        if (frac > 1.0f)
            frac = 1.0f;
        return at(buf, cap, i0) * (1.0f - frac) + at(buf, cap, i1) * frac;
    }

    static float at(const float* buf, int cap, int64_t index)
    {
        if (index < 0)
            return 0.0f;
        int64_t m = index % (int64_t) cap;
        if (m < 0)
            m += cap;
        return buf[(size_t) m];
    }
};
