// MP Sample Stretch - a Tubedigga Knob FX / insert effect for Akai MPC Sample
// and other MPC / Force hardware. Same DSP as Tubedigga MP Stretch.
//
// Copyright (C) 2026 Tubedigga
// SPDX-License-Identifier: GPL-3.0-only
// Corresponding Source: https://github.com/tubedigga/mp-stretch
// Time-stretch DSP modified from potenza-time-stretch (GPL-3.0, dar-io-p):
//   https://github.com/dar-io-p/potenza-time-stretch
//
// The grain stretch is AkaiStretch (akai-stretch.h). Cyclic, Length, Pitch,
// Phase, and Delay are the insert, and the MPC sequencer automates them. Neutral
// is Cyclic 400 samples, Length 100%, Pitch off, Phase off. At those
// defaults the live input is passed with no delay. Leaving neutral snaps
// the read onto the sample just written. Returning to neutral snaps again
// and drops the delayed tail, the same instant handoff as the host ENABLE
// bypass.
//
// Pitch off keeps the pitch still while Length changes the duration.
// Pitch on keeps the duration and uses Length as the pitch (200% = +12 st).
// At Delay 0, Pitch on uses the same reader as Pitch on after a delay:
// one read at the Length factor, a fixed 20 ms hop, and a 4 ms crossfade.
// The audible head is anchor + n * factor, so 200% is one octave. The
// splice pays back exactly the samples that run got ahead of the host,
// and does not take that payback out of the slope. A lower Length gives
// the extra margin back, so the anchor stays on the host either way.
// Above Length 350 the
// joins stay at that same spacing. Pitch off at Delay 0 is still the
// grains. A Delay above 0 still waits out the dry hit before that reader starts.
// Phase is the 0–30 ms delay on the dry sample whenever Length and
// Cyclic are home, and on the grains when they are not.
//
// Rate is the internal clock, 22500–44100 Hz, default 44100. At the
// top the audio stays full band. Below that the clock runs slower and
// the top cuts at half the chosen rate. It does not move where Delay
// starts the stretch, how far Length plays, or when the grab clears.
//
// Delay at 0 leaves that path alone, including the snap back onto the live
// chop. Above 0, and only while Length is away from home, a level cross
// arms a one-shot. Delay chooses where the stretch starts. With Pitch off
// the stretch position walks the captured tail at 100/Length, so one second
// of tail at 2000% is twenty seconds of output, cut at 16, and two
// overlapping raised-cosine grains play at the original rate. With Pitch
// on the duration stays put: one read at the Length factor, and one splice
// that pays back exactly the samples that read ran ahead. The shot fades when the
// stretch position reaches the end of the tail, or at 16 seconds.
//
// Phase at 0 is dry. Above 0 the comb is the bright chop into an
// unclamped feedback delay. Moving Phase glides the delay time so the
// read pointer does not jump.

#include "mpc_plugin.h"
#include "akai-stretch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class MPStretch : public mpc::Plugin
{
public:
    enum Params
    {
        kCyclic = 0,
        kLength,
        kPitch,
        kPhase,
        kDelay,
        kRate,
        kNumParams
    };

    enum class Shot
    {
        Arm,
        Hold,
        Run
    };

    static constexpr int   kChannels = 2;
    static constexpr float kRateMin  = 22500.0f;
    static constexpr float kRateMax  = 44100.0f;
    static constexpr float kRateDef  = 44100.0f;

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "MP Sample Stretch";
        info.vendor       = "Tubedigga";
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);
        info.version      = 1609;
        info.isInstrument = false;
        info.numInputs    = 2;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    MPStretch() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("cyclic", "Cyclic", 0.0f, AkaiStretch::kCyclicMax, AkaiStretch::kCyclicDial, "", 0));
        addParam(floatParam("length", "Length", AkaiStretch::kLengthMin, AkaiStretch::kLengthMax, AkaiStretch::kLengthDial, "", 0));
        addParam(boolParam("pitch", "Pitch", false));
        addParam(floatParam("phase", "Phase", 0.0f, AkaiStretch::kPhaseMax, 0.0f, "", 1));
        addParam(floatParam("delay", "Delay", 0.0f, 2000.0f, 0.0f, "", 0));
        addParam(floatParam("rate", "Rate", kRateMin, kRateMax, kRateDef, "", 0));
        if (numParams() != kNumParams) mpc::log("MP Sample Stretch: parameter count mismatch");

        const int cap = historyCap();
        for (Channel& c : ch_)
            c.buf.assign((size_t)cap, 0.0f);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        const double tc = 0.006 * sampleRate_;
        glideCoeff_ = tc > 1.0 ? (float)(1.0 - std::exp(-1.0 / tc)) : 1.0f;
        const int cap = historyCap();
        for (Channel& c : ch_)
            if ((int)c.buf.size() != cap)
                c.buf.assign((size_t)cap, 0.0f);
        reset();
    }

    void reset() override
    {
        const Targets t = readTargets();
        const bool neutral = isDry(t);
        for (Channel& c : ch_)
        {
            std::fill(c.buf.begin(), c.buf.end(), 0.0f);
            c.writeAbs = 0;
            c.held = 0.0f;
            c.fade = 0.0f;
            c.mode = neutral ? Mode::Dry : Mode::Stretch;
            c.applied = false;
            c.stretch.setup(sampleRate_, sampleRate_, 0.0, ! neutral,
                            cyclicUsed(t.cyclic), t.length, t.pitchOn, t.phase);
            c.applied = true;
            c.appliedCyclic = t.cyclic;
            c.appliedLength = t.length;
            c.appliedPitch = t.pitchOn;
            c.appliedPhase = t.phase;
            c.appliedGrains = ! neutral;
        }
        shot_ = Shot::Arm;
        hot_ = false;
        below_ = 0;
        endFrozen_ = false;
        hitEnd_ = 0;
        clearTail();
        playLeft_ = 0;
        endFade_ = -1;
        clearRate();
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        if (n <= 0 || ctx.outputs == nullptr) return;
        const int numOut = std::min(ctx.numOutputs, kChannels);
        if (numOut <= 0) return;

        const int cap = (int)ch_[0].buf.size();
        if (cap < 2) return;

        const double sr = ctx.sampleRate > 1.0 ? ctx.sampleRate : sampleRate_;
        const Targets t = readTargets();
        const bool lengthHome = t.length <= AkaiStretch::kLengthDial + 0.5f;
        // Delay at 0, and Length at home, stay on the path below. The
        // one-shot arming must not run in either of those cases.
        if (t.delayMs > 0.5f && ! lengthHome)
        {
            processOneShot(ctx, t, numOut, cap, sr);
            return;
        }
        if (shot_ != Shot::Arm)
        {
            for (int ch = 0; ch < numOut; ++ch)
                ch_[ch].mode = Mode::Dry;
        }
        shot_ = Shot::Arm;
        hot_ = false;
        const bool cyclicHome = std::fabs(t.cyclic - AkaiStretch::kCyclicDial) <= kCyclicHome;
        const bool phaseOff = t.phase <= 0.05f;
        // Length at 100% is unity duration and, with the switch, unity pitch.
        // Cyclic has to be home too before the grains are out of the way.
        const bool dry = lengthHome && cyclicHome && phaseOff;
        const bool phaseOnly = lengthHome && cyclicHome && ! phaseOff;

        for (int i = 0; i < n; ++i)
        {
            float inDry[kChannels];
            for (int ch = 0; ch < numOut; ++ch)
            {
                const float* in = inputFor(ctx, ch);
                inDry[ch] = (in != nullptr) ? in[i] : 0.0f;
                if (! std::isfinite(inDry[ch]))
                    inDry[ch] = 0.0f;
            }
            clockInput(inDry, numOut, sr, t.rateHz);

            for (int ch = 0; ch < numOut; ++ch)
            {
                float* out = ctx.outputs[ch];
                if (out == nullptr) continue;
                Channel& c = ch_[ch];
                AkaiStretch& s = c.stretch;

                const bool cyclicMoved = ! c.applied || c.appliedCyclic != t.cyclic;
                const bool lengthMoved = ! c.applied || c.appliedLength != t.length;
                const bool pitchMoved = ! c.applied || c.appliedPitch != t.pitchOn;
                applyTargets(c, sr, t, ! dry && ! phaseOnly);

                const int64_t slot = c.writeAbs % (int64_t)cap;
                c.buf[(size_t)slot] = inDry[ch];
                c.writeAbs += 1;
                const double live = (double)(c.writeAbs - 1);

                if (dry)
                {
                    spliceOn_ = false;
                    if (c.mode != Mode::Dry)
                    {
                        s.discardDelay();
                        c.held = 0.0f;
                        c.fade = 0.0f;
                    }
                    c.mode = Mode::Dry;
                    s.snapToLive(live);
                    out[i] = inDry[ch];
                    continue;
                }

                if (phaseOnly)
                {
                    spliceOn_ = false;
                    // Stretch is off, so the delay hears the live chop.
                    // Drop whatever the grains had left in the line.
                    if (c.mode != Mode::Phase)
                    {
                        s.discardDelay();
                        c.held = 0.0f;
                        c.fade = 0.0f;
                        s.snapToLive(live);
                    }
                    c.mode = Mode::Phase;
                    float y = s.applyPhase(inDry[ch]);
                    if (! std::isfinite(y))
                        y = inDry[ch];
                    out[i] = y;
                    continue;
                }

                if (t.pitchOn)
                {
                    // Same reader as Pitch on with a delay. One read, fixed hop.
                    if (! spliceOn_)
                        beginPitch(live, sr, tailRates(t));
                    c.mode = Mode::Stretch;
                    s.blend = 1.0f;
                    float grain = mixPitch(c.buf.data(), cap, c.writeAbs);
                    if (! std::isfinite(grain))
                        grain = inDry[ch];
                    const float wet = inDry[ch] + (grain - inDry[ch]) * s.blend;
                    float y = s.applyPhase(wet);
                    if (! std::isfinite(y))
                        y = inDry[ch];
                    out[i] = y;
                    s.glide(glideCoeff_);
                    continue;
                }
                if (spliceOn_)
                {
                    s.snapToLive(live);
                    s.blend = 1.0f;
                    s.wantGrains = true;
                    s.grains = true;
                    c.held = 0.0f;
                    c.fade = 0.0f;
                }

                // Back at unity time (Length 100%, Pitch not shifting) the
                // read must sit on the sample just written. A Cyclic change
                // restarts a grain behind the old one; leaving that in place
                // is the late chop.
                if (c.mode != Mode::Stretch || (lengthHome && (cyclicMoved || lengthMoved || pitchMoved)))
                {
                    s.snapToLive(live);
                    s.blend = 1.0f;
                    s.wantGrains = true;
                    s.grains = true;
                    c.held = 0.0f;
                    c.fade = 0.0f;
                }
                c.mode = Mode::Stretch;

                float grain = s.readRing(c.buf.data(), cap, c.writeAbs);
                if (! std::isfinite(grain))
                    grain = inDry[ch];
                s.advance(1.0f, (double)c.writeAbs);

                double margin = (double)s.grainSize * 2.0;
                if (margin < 128.0) margin = 128.0;
                if (margin > (double)cap * 0.25) margin = (double)cap * 0.25;
                const double newest = (double)c.writeAbs;
                const double oldest = newest - (double)cap;
                if (s.pullIntoWindow(oldest, newest, margin))
                {
                    c.held = grain;
                    c.fade = 1.0f;
                    grain = s.readRing(c.buf.data(), cap, c.writeAbs);
                    if (! std::isfinite(grain))
                        grain = inDry[ch];
                }
                if (c.fade > 0.0f)
                {
                    grain = grain + (c.held - grain) * c.fade;
                    c.fade *= 0.98f;
                    if (c.fade < 0.0008f)
                        c.fade = 0.0f;
                }

                const float wet = inDry[ch] + (grain - inDry[ch]) * s.blend;
                float y = s.applyPhase(wet);
                if (! std::isfinite(y))
                    y = inDry[ch];
                out[i] = y;
                s.glide(glideCoeff_);
            }
            if (t.pitchOn && ! dry && ! phaseOnly && spliceOn_)
            {
                const TailRate rate = tailRates(t);
                stepPitch(rate.read, rate.time);
            }
            else if (! t.pitchOn && spliceOn_)
                spliceOn_ = false;
        }
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        char buf[24];
        if (index == kCyclic)
            std::snprintf(buf, sizeof(buf), "%.0fsmp", (double)plainValue);
        else if (index == kLength)
            std::snprintf(buf, sizeof(buf), "%.0f%%", (double)plainValue);
        else if (index == kPitch)
        {
            out = plainValue >= 0.5f ? "On" : "Off";
            return true;
        }
        else if (index == kPhase)
        {
            if (plainValue <= 0.05f)
            {
                out = "off";
                return true;
            }
            if (plainValue >= 10.0f)
                std::snprintf(buf, sizeof(buf), "%.0fms", (double)plainValue);
            else
                std::snprintf(buf, sizeof(buf), "%.1fms", (double)plainValue);
        }
        else if (index == kDelay)
            std::snprintf(buf, sizeof(buf), "%.0fms", (double)plainValue);
        else if (index == kRate)
            std::snprintf(buf, sizeof(buf), "%.0fHz", (double)plainValue);
        else
            return false;
        out = buf;
        return true;
    }

private:
    // Fixed capture ring: 16 seconds at 44100 Hz. Sized when the
    // plugin loads, and not scaled when the host rate changes.
    static constexpr int kHistorySamples = 705600;
    static constexpr char   kUid[5]         = "Pi2G";
    // A 0–2000 Cyclic travel stepped like a Q-link (~128 detents) cannot
    // sit on 400. The neighbours are about 394 and 409. The old neutral
    // test was ±0.5, so it never armed again after the knob moved, and
    // Length back at 100% kept playing the lagged read at 1:1.
    static constexpr float  kCyclicHome     = 16.0f;
    // Display stays 0–2000 and home stays the displayed 400 step.
    // The grain size is 20 samples higher, so a displayed 0 is 20.
    static constexpr float  kCyclicOffset   = 20.0f;
    static constexpr float  kOnset          = 0.02f;
    // Tails stay audible well under the onset. This is the line that still
    // counts as the hit, so the stretch does not drop the decay to dry.
    static constexpr float  kAudible        = 0.0015f;
    // End of the captured one-shot. 0.0015 was the loud body only, so a
    // one second remainder froze after a fraction of that and 2000%
    // turned the fragment into about four seconds.
    static constexpr float  kTailFloor      = 0.00001f;
    // Pitch On only. The fade is a few milliseconds. The hop is the single
    // splice interval, longer than the fade so the second read does not stay up.
    // Above this Length the hop does not get shorter, so joins do not stack.
    static constexpr double kPitchFadeSec   = 0.004;
    static constexpr double kPitchHopSec    = 0.020;
    static constexpr double kPitchJoinCap   = 3.5;
    enum class Mode
    {
        Dry,
        Phase,
        Stretch
    };

    struct Channel
    {
        AkaiStretch        stretch;
        std::vector<float> buf;
        int64_t            writeAbs = 0;
        float              held = 0.0f;
        float              fade = 0.0f;
        Mode               mode = Mode::Dry;
        bool               applied = false;
        float              appliedCyclic = 0.0f;
        float              appliedLength = 0.0f;
        bool               appliedPitch = false;
        float              appliedPhase = 0.0f;
        bool               appliedGrains = false;
    };

    struct Targets
    {
        float cyclic = AkaiStretch::kCyclicDial;
        float length = AkaiStretch::kLengthDial;
        bool  pitchOn = false;
        float phase = 0.0f;
        float delayMs = 0.0f;
        float rateHz = kRateDef;
    };

    static int historyCap()
    {
        return kHistorySamples;
    }

    static float clampRange(float v, float lo, float hi)
    {
        if (! std::isfinite(v) || v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    Targets readTargets() const
    {
        Targets t;
        t.cyclic = clampRange(get(kCyclic), 0.0f, AkaiStretch::kCyclicMax);
        t.length = clampRange(get(kLength), AkaiStretch::kLengthMin, AkaiStretch::kLengthMax);
        t.pitchOn = getBool(kPitch);
        t.phase = clampRange(get(kPhase), 0.0f, AkaiStretch::kPhaseMax);
        t.delayMs = clampRange(get(kDelay), 0.0f, 2000.0f);
        t.rateHz = clampRange(get(kRate), kRateMin, kRateMax);
        return t;
    }

    void processOneShot(mpc::ProcessContext& ctx, const Targets& t, int numOut, int cap, double sr)
    {
        const int n = ctx.numFrames;
        const int delaySamps = std::max(1, (int)std::lround(t.delayMs * 0.001 * sr));
        // Marks the end of the source hit. It does not stop playback.
        const int silenceHang = std::max(1, (int)std::lround(0.120 * sr));
        const int fadeSamps = std::max(1, (int)std::lround(0.015 * sr));
        // Longest output from the moment the stretch kicks in. A shorter
        // capture ends when the forward read reaches it, not by padding.
        const int playCap = std::max(1, (int)std::lround(16.0 * sr));

        for (int i = 0; i < n; ++i)
        {
            float inDry[kChannels];
            float peak = 0.0f;
            for (int ch = 0; ch < numOut; ++ch)
            {
                const float* in = inputFor(ctx, ch);
                float x = (in != nullptr) ? in[i] : 0.0f;
                if (! std::isfinite(x))
                    x = 0.0f;
                inDry[ch] = x;
                const float a = std::fabs(x);
                if (a > peak)
                    peak = a;
            }
            // Peak above is the raw hit, so the release still follows the
            // source. The clock only changes the audio that is stored.
            clockInput(inDry, numOut, sr, t.rateHz);

            for (int ch = 0; ch < numOut; ++ch)
            {
                Channel& c = ch_[ch];
                c.buf[(size_t)(c.writeAbs % (int64_t)cap)] = inDry[ch];
                c.writeAbs += 1;
            }
            const double live = (double)(ch_[0].writeAbs - 1);

            if (peak >= kTailFloor)
            {
                if (! endFrozen_)
                    hitEnd_ = (int64_t)live;
                below_ = 0;
            }
            else if (! endFrozen_)
            {
                below_ += 1;
                if (below_ >= silenceHang)
                    endFrozen_ = true;
            }
            const bool sourceEnded = endFrozen_ && hitEnd_ <= (int64_t)live;

            if (shot_ == Shot::Arm && ! hot_ && peak >= kOnset)
            {
                waitLeft_ = delaySamps;
                below_ = 0;
                endFrozen_ = false;
                hitEnd_ = (int64_t)live;
                endFade_ = -1;
                hot_ = true;
                shot_ = Shot::Hold;
            }
            if (peak < kAudible)
                hot_ = false;

            bool startNow = false;
            if (shot_ == Shot::Hold)
            {
                if (sourceEnded)
                {
                    // The hit finished during the wait. Do not stretch silence.
                    shot_ = Shot::Arm;
                    hot_ = false;
                    below_ = 0;
                    endFrozen_ = false;
                    playLeft_ = 0;
                    endFade_ = -1;
                }
                else
                {
                    waitLeft_ -= 1;
                    if (waitLeft_ <= 0)
                    {
                        startNow = true;
                        shot_ = Shot::Run;
                        playLeft_ = playCap;
                        endFade_ = -1;
                        if (t.pitchOn)
                            beginPitch(live, sr, tailRates(t));
                        else
                        {
                            beginTail(live, sr, tailRates(t));
                            spliceOn_ = false;
                        }
                    }
                }
            }

            const bool running = (shot_ == Shot::Run) || startNow;
            const TailRate rate = tailRates(t);
            if (running && ! startNow)
            {
                if (t.pitchOn && ! spliceOn_)
                    beginPitch(tailSrc_, sr, rate);
                else if (! t.pitchOn && spliceOn_)
                {
                    beginTail(tailSrc_, sr, rate);
                    spliceOn_ = false;
                }
            }
            for (int ch = 0; ch < numOut; ++ch)
            {
                float* out = ctx.outputs[ch];
                if (out == nullptr)
                    continue;
                Channel& c = ch_[ch];
                AkaiStretch& s = c.stretch;
                applyTargets(c, sr, t, false);

                if (! running)
                {
                    if (c.mode != Mode::Dry)
                    {
                        s.discardDelay();
                        c.held = 0.0f;
                        c.fade = 0.0f;
                    }
                    c.mode = Mode::Dry;
                    out[i] = inDry[ch];
                    continue;
                }

                if (startNow)
                {
                    s.discardDelay();
                    c.held = 0.0f;
                    c.fade = 0.0f;
                }
                c.mode = Mode::Stretch;
                float y = t.pitchOn
                    ? mixPitch(c.buf.data(), cap, c.writeAbs)
                    : mixTail(c.buf.data(), cap, c.writeAbs, rate.read);
                if (! std::isfinite(y))
                    y = 0.0f;
                outGrain_[ch] = y;
            }

            if (running && endFade_ < 0)
            {
                if (playLeft_ > 0)
                    playLeft_ -= 1;
                if (t.pitchOn)
                    stepPitch(rate.read, rate.time);
                else
                    stepTail(rate.read, rate.time);
                // The stretch position has to move. Comparing it with the
                // live sample on the first tick ends the shot at the kick-in.
                const bool forwardDone = endFrozen_
                    && tailSrc_ > tailOrigin_
                    && tailSrc_ >= (double)hitEnd_;
                if (playLeft_ <= 0 || forwardDone)
                    endFade_ = fadeSamps;
            }
            float gain = 1.0f;
            bool releaseNow = false;
            if (endFade_ >= 0)
            {
                gain = (float)endFade_ / (float)fadeSamps;
                if (gain < 0.0f)
                    gain = 0.0f;
                if (endFade_ == 0)
                    releaseNow = true;
                else
                    endFade_ -= 1;
            }

            if (running)
            {
                for (int ch = 0; ch < numOut; ++ch)
                {
                    float* out = ctx.outputs[ch];
                    if (out == nullptr)
                        continue;
                    Channel& c = ch_[ch];
                    float y = c.stretch.applyPhase(outGrain_[ch] * gain);
                    if (! std::isfinite(y))
                        y = 0.0f;
                    out[i] = y;
                    c.stretch.glide(glideCoeff_);
                }
            }

            if (releaseNow)
            {
                shot_ = Shot::Arm;
                hot_ = false;
                below_ = 0;
                endFrozen_ = false;
                playLeft_ = 0;
                endFade_ = -1;
                clearTail();
            }
        }
    }

    struct TailRate
    {
        double read = 1.0;
        double time = 1.0;
    };

    // Playback rate stays 1 unless Pitch is on. The stretch position is
    // the slower one: 100/Length. Those are not the same number.
    static TailRate tailRates(const Targets& t)
    {
        float factor = t.length / 100.0f;
        if (factor < 1.0f)
            factor = 1.0f;
        if (factor > 20.0f)
            factor = 20.0f;
        TailRate r;
        if (t.pitchOn)
        {
            r.read = (double)factor;
            r.time = 1.0;
        }
        else
        {
            r.read = 1.0;
            r.time = 1.0 / (double)factor;
        }
        return r;
    }

    void clearTail()
    {
        tailPhase_[0] = -1.0;
        tailPhase_[1] = -1.0;
        tailBase_[0] = 0.0;
        tailBase_[1] = 0.0;
        tailHop_ = 0;
        tailGrainN_ = 0;
        tailSrc_ = 0.0;
        tailOrigin_ = 0.0;
        spliceOn_ = false;
        spliceHandoff_ = false;
        splicePos_ = 0.0;
        spliceFrom_ = 0.0;
        spliceBack_ = 0.0;
        spliceLead_ = 0.0;
        spliceRate_ = 1.0;
        spliceFade_ = 0;
        spliceFadeN_ = 0;
        spliceHop_ = 0;
        spliceLeft_ = 0;
        spliceElapsed_ = 0;
        spliceAhead_ = 0.0;
    }

    void beginTail(double live, double sr, const TailRate& rate)
    {
        int G = (int)std::lround(0.080 * sr);
        if (G < 256)
            G = 256;
        if (G > 8192)
            G = 8192;
        if (G & 1)
            ++G;
        const double Hs = 0.5 * (double)G;
        tailGrainN_ = G;
        tailHop_ = (int)Hs;
        tailOrigin_ = live;
        tailSrc_ = live;
        // Grain 0 is at the peak of the raised cosine, reading the live
        // sample at full level, so the handoff from the dry hit does not
        // open a hole. Grain 1 is just entering, window at zero.
        tailBase_[0] = live - Hs * rate.read;
        tailPhase_[0] = Hs;
        tailBase_[1] = live + Hs * (rate.time - rate.read);
        tailPhase_[1] = 0.0;
    }

    float mixTail(const float* buf, int cap, int64_t writeAbs, double readRate) const
    {
        const double G = (double)tailGrainN_;
        if (G < 2.0)
            return 0.0f;
        const double twopi = 6.28318530717958647692;
        float acc = 0.0f;
        for (int i = 0; i < 2; ++i)
        {
            const double ph = tailPhase_[i];
            if (ph < 0.0 || ph >= G)
                continue;
            const double pos = tailBase_[i] + ph * readRate;
            const float w = 0.5f * (1.0f - (float)std::cos(twopi * ph / G));
            acc += w * readTail(buf, cap, writeAbs, pos);
        }
        return acc;
    }

    void stepTail(double readRate, double timeRate)
    {
        const double G = (double)tailGrainN_;
        if (G < 2.0)
            return;
        const double Hs = 0.5 * G;
        for (int i = 0; i < 2; ++i)
        {
            if (tailPhase_[i] < 0.0)
                continue;
            tailPhase_[i] += 1.0;
            if (tailPhase_[i] >= G)
                tailPhase_[i] = -1.0;
        }
        tailSrc_ += timeRate;
        if (tailHop_ > 0)
            tailHop_ -= 1;
        if (tailHop_ > 0)
            return;
        int slot = 0;
        if (tailPhase_[0] >= 0.0 && tailPhase_[1] < 0.0)
            slot = 1;
        else if (tailPhase_[1] >= 0.0 && tailPhase_[0] < 0.0)
            slot = 0;
        else
            slot = (tailPhase_[0] >= tailPhase_[1]) ? 0 : 1;
        // Window starts at zero, so placing the base behind the stretch
        // position is not a click. The peak of this grain lands on the
        // stretch position, which is what moves at 100/Length.
        tailBase_[slot] = tailSrc_ + Hs * (timeRate - readRate);
        tailPhase_[slot] = 0.0;
        tailHop_ = (int)Hs;
    }

    // Pitch On. One fractional read at the Length factor. The audible
    // position is splicePos_ + elapsed * factor, so a hop of samples is
    // spaced by the factor (200% = one octave) and the ledger cannot
    // change that spacing. At the splice the anchor jumps back by exactly
    // the samples that run got ahead of the host. The second read exists
    // only for the fade.
    void beginPitch(double live, double sr, const TailRate& rate)
    {
        double factor = rate.read;
        if (factor < 1.0)
            factor = 1.0;
        if (factor > 20.0)
            factor = 20.0;
        int fadeN = (int)std::lround(kPitchFadeSec * sr);
        if (fadeN < 8)
            fadeN = 8;
        int hop = (int)std::lround(kPitchHopSec * sr);
        if (hop < fadeN + 1)
            hop = fadeN + 1;
        spliceFadeN_ = fadeN;
        spliceHop_ = hop;
        spliceLeft_ = hop;
        spliceElapsed_ = 0;
        spliceAhead_ = 0.0;
        spliceRate_ = factor;
        spliceBack_ = (double)(hop - 1) * (factor - 1.0);
        spliceLead_ = (double)fadeN * factor;
        tailOrigin_ = live;
        tailSrc_ = live;
        tailPhase_[0] = -1.0;
        tailPhase_[1] = -1.0;
        tailGrainN_ = 0;
        spliceOn_ = true;
        if (!(factor > 1.0) || !(spliceBack_ > 0.0))
        {
            splicePos_ = live;
            spliceFrom_ = live;
            spliceFade_ = 0;
            spliceHandoff_ = false;
            return;
        }
        // Last sample of the hop lands on the sample just written. Every
        // earlier sample is behind it by a factor step, so the read stays
        // inside the ring and is not clamped back to the live write.
        spliceFrom_ = live;
        splicePos_ = live - spliceBack_;
        spliceFade_ = fadeN;
        spliceHandoff_ = true;
    }

    float mixPitch(const float* buf, int cap, int64_t writeAbs) const
    {
        const double played = splicePos_ + (double)spliceElapsed_ * spliceRate_;
        const float now = readTail(buf, cap, writeAbs, played);
        if (spliceFade_ <= 0 || spliceFadeN_ <= 1)
            return now;
        const int denom = spliceFadeN_ - 1;
        const float t = 1.0f - (float)(spliceFade_ - 1) / (float)denom;
        const float w = 0.5f * (1.0f - std::cos(3.14159265358979323846f * t));
        const float old = readTail(buf, cap, writeAbs, spliceFrom_);
        return old * (1.0f - w) + now * w;
    }

    void stepPitch(double readRate, double timeRate)
    {
        if (readRate < 1.0)
            readRate = 1.0;
        if (readRate > 20.0)
            readRate = 20.0;
        if (timeRate < 0.0)
            timeRate = 0.0;
        // Keep the sample that was just played put when Length moves.
        // played = anchor + elapsed * rate, so the anchor shifts by the
        // rate change and the next step is still exactly the new factor.
        if (spliceElapsed_ > 0 && readRate != spliceRate_)
            splicePos_ += (double)spliceElapsed_ * (spliceRate_ - readRate);
        spliceRate_ = readRate;
        tailSrc_ += timeRate;

        if (spliceFade_ > 0)
        {
            // Opening fade leaves the sample just written. After that the
            // outgoing head keeps the Length rate, same as the audible one.
            spliceFrom_ += spliceHandoff_ ? timeRate : readRate;
            spliceFade_ -= 1;
            if (spliceFade_ <= 0)
                spliceHandoff_ = false;
        }

        if (readRate <= 1.0)
        {
            splicePos_ = tailSrc_;
            spliceAhead_ = 0.0;
            spliceElapsed_ = 0;
            spliceFade_ = 0;
            spliceHandoff_ = false;
            return;
        }

        // 20 ms hop. Above Length 350 (kPitchJoinCap) the read still follows
        // Length, but a join may not come any sooner than this same spacing.
        int hop = spliceHop_;
        if (hop < spliceFadeN_ + 1)
            hop = spliceFadeN_ + 1;
        if (hop < 2)
            hop = 2;
        if (readRate > kPitchJoinCap && hop < spliceHop_)
            hop = spliceHop_;

        // Margin for this factor: the hop must end on the host, not past
        // it. Too little and the read clamps to the live sample. Too much,
        // left over from a higher Length, and the read stays late.
        const double surplus = readRate - timeRate;
        const double need = surplus > 0.0 ? (double)(hop - 1) * surplus : 0.0;
        if (spliceElapsed_ == 0 && !(spliceAhead_ > 0.0))
        {
            const double place = tailSrc_ - need;
            if (std::fabs(splicePos_ - place) > 1.5)
                splicePos_ = place;
        }

        // The audible head is splicePos_ + elapsed * readRate. Stepping
        // the anchor here would let the payback cancel the factor.
        spliceAhead_ += readRate - timeRate;
        spliceElapsed_ += 1;
        if (spliceLeft_ > 0)
            spliceLeft_ -= 1;

        if (spliceFade_ > 0 || spliceElapsed_ < hop)
            return;

        // Where a factor run would go next, and where the host timeline
        // says this hop must start. The difference is the payback. Landing
        // on the host, not on the previous factor's margin, is what keeps
        // a downward Length move at 1:1.
        const double continueAt = splicePos_ + (double)spliceElapsed_ * readRate;
        const double place = tailSrc_ - need;
        spliceAhead_ = continueAt - place;
        // Outgoing head starts a fade earlier in the same factor run, so
        // the crossfade stays at the Length rate instead of on the live write.
        spliceFrom_ = splicePos_ + (double)(spliceElapsed_ - 1) * readRate
            - (double)spliceFadeN_ * readRate;
        splicePos_ = place;
        spliceBack_ = spliceAhead_;
        spliceAhead_ = 0.0;
        spliceElapsed_ = 0;
        spliceFade_ = spliceFadeN_;
        spliceLeft_ = hop;
        spliceHandoff_ = false;
    }

    // Fractional read. The stretch position moves slowly; the grain phase
    // moves at the playback rate. Neither one snaps to a new sample.
    static float readTail(const float* buf, int cap, int64_t writeAbs, double pos)
    {
        if (buf == nullptr || cap < 2 || writeAbs < 1 || ! std::isfinite(pos))
            return 0.0f;
        const double newest = (double)(writeAbs - 1);
        const double oldest = (double)writeAbs - (double)cap;
        if (pos > newest)
            pos = newest;
        if (pos < oldest)
            pos = oldest;
        if (pos < 0.0)
            pos = 0.0;
        const int64_t i0 = (int64_t)std::floor(pos);
        int64_t i1 = i0 + 1;
        if ((double)i1 > newest)
            i1 = i0;
        float frac = (float)(pos - (double)i0);
        if (frac < 0.0f)
            frac = 0.0f;
        if (frac > 1.0f)
            frac = 1.0f;
        auto at = [&](int64_t index) {
            if (index < 0)
                return 0.0f;
            int64_t m = index % (int64_t)cap;
            if (m < 0)
                m += cap;
            const float s = buf[(size_t)m];
            return std::isfinite(s) ? s : 0.0f;
        };
        const float a = at(i0);
        const float b = at(i1);
        return a + (b - a) * frac;
    }

    static float cyclicUsed(float displayed)
    {
        return displayed + kCyclicOffset;
    }

    static bool isDry(const Targets& t)
    {
        return std::fabs(t.cyclic - AkaiStretch::kCyclicDial) <= kCyclicHome
            && t.length <= AkaiStretch::kLengthDial + 0.5f
            && t.phase <= 0.05f;
    }

    static void applyTargets(Channel& c, double sr, const Targets& t, bool doGrains)
    {
        if (c.applied && c.appliedCyclic == t.cyclic && c.appliedLength == t.length
            && c.appliedPitch == t.pitchOn && c.appliedPhase == t.phase
            && c.appliedGrains == doGrains)
            return;
        c.stretch.retarget(sr, sr, doGrains, cyclicUsed(t.cyclic), t.length, t.pitchOn, t.phase);
        c.applied = true;
        c.appliedCyclic = t.cyclic;
        c.appliedLength = t.length;
        c.appliedPitch = t.pitchOn;
        c.appliedPhase = t.phase;
        c.appliedGrains = doGrains;
    }

    static const float* inputFor(const mpc::ProcessContext& ctx, int ch)
    {
        if (ctx.inputs == nullptr || ctx.numInputs <= 0) return nullptr;
        if (ch < ctx.numInputs && ctx.inputs[ch] != nullptr) return ctx.inputs[ch];
        return ctx.inputs[0];
    }

    // Slower internal clock. At 44100, or at the host rate, this is a
    // bypass. Below that, a lowpass at half the chosen rate and a hold
    // on that clock cut the top. The stretch timeline is not touched.
    void clearRate()
    {
        rateLive_ = false;
        rateCut_ = -1.0f;
        rateHost_ = 0.0;
        rateClock_ = 1.0;
        for (int s = 0; s < 3; ++s)
        {
            for (int ch = 0; ch < kChannels; ++ch)
            {
                rateZ1_[s][ch] = 0.0f;
                rateZ2_[s][ch] = 0.0f;
            }
        }
        rateHeld_[0] = 0.0f;
        rateHeld_[1] = 0.0f;
    }

    void designRate(double fc, double sr)
    {
        const double q[3] = { 1.30656296488, 0.54119610015, 0.70710678118 };
        for (int s = 0; s < 3; ++s)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * fc / sr;
            const double cosw = std::cos(w0);
            const double sinw = std::sin(w0);
            const double alpha = sinw / (2.0 * q[s]);
            const double a0 = 1.0 + alpha;
            rateB0_[s] = (float)(((1.0 - cosw) * 0.5) / a0);
            rateB1_[s] = (float)((1.0 - cosw) / a0);
            rateB2_[s] = rateB0_[s];
            rateA1_[s] = (float)((-2.0 * cosw) / a0);
            rateA2_[s] = (float)((1.0 - alpha) / a0);
        }
    }

    float rateBiquad(int stage, int ch, float x)
    {
        const float y = rateB0_[stage] * x + rateZ1_[stage][ch];
        rateZ1_[stage][ch] = rateB1_[stage] * x - rateA1_[stage] * y + rateZ2_[stage][ch];
        rateZ2_[stage][ch] = rateB2_[stage] * x - rateA2_[stage] * y;
        if (std::fabs(rateZ1_[stage][ch]) < 1.0e-15f)
            rateZ1_[stage][ch] = 0.0f;
        if (std::fabs(rateZ2_[stage][ch]) < 1.0e-15f)
            rateZ2_[stage][ch] = 0.0f;
        return y;
    }

    void clockInput(float* samples, int nch, double hostSr, float rateHz)
    {
        if (nch <= 0 || ! (hostSr > 1.0))
            return;
        if (rateHz >= kRateMax - 0.5f || rateHz + 1.0f >= hostSr)
        {
            if (rateLive_)
                clearRate();
            return;
        }

        const double fc = std::min(static_cast<double>(rateHz) * 0.5, hostSr * 0.47);
        if (! rateLive_ || std::fabs(rateCut_ - (float)fc) > 0.5f || std::fabs(rateHost_ - hostSr) > 0.5)
        {
            const bool arm = ! rateLive_;
            designRate(fc, hostSr);
            if (arm)
            {
                for (int s = 0; s < 3; ++s)
                {
                    for (int ch = 0; ch < kChannels; ++ch)
                    {
                        rateZ1_[s][ch] = 0.0f;
                        rateZ2_[s][ch] = 0.0f;
                    }
                }
                rateHeld_[0] = 0.0f;
                rateHeld_[1] = 0.0f;
                rateClock_ = 1.0;
            }
            rateCut_ = (float)fc;
            rateHost_ = hostSr;
            rateLive_ = true;
        }

        float shaped[kChannels];
        for (int ch = 0; ch < nch; ++ch)
        {
            float x = rateBiquad(0, ch, samples[ch]);
            shaped[ch] = rateBiquad(1, ch, x);
        }
        rateClock_ += static_cast<double>(rateHz) / hostSr;
        if (rateClock_ >= 1.0)
        {
            rateClock_ -= 1.0;
            if (rateClock_ > 2.0)
                rateClock_ = 0.0;
            for (int ch = 0; ch < nch; ++ch)
                rateHeld_[ch] = shaped[ch];
        }
        for (int ch = 0; ch < nch; ++ch)
        {
            const float y = rateBiquad(2, ch, rateHeld_[ch]);
            samples[ch] = std::isfinite(y) ? y : 0.0f;
        }
    }

    Channel ch_[kChannels];
    double  sampleRate_ = 44100.0;
    float   glideCoeff_ = 0.004f;
    Shot    shot_ = Shot::Arm;
    bool    hot_ = false;
    bool    endFrozen_ = false;
    int     below_ = 0;
    int     waitLeft_ = 0;
    int     endFade_ = -1;
    int64_t hitEnd_ = 0;
    double  tailBase_[2] {};
    double  tailPhase_[2] { -1.0, -1.0 };
    double  tailSrc_ = 0.0;
    double  tailOrigin_ = 0.0;
    int     tailHop_ = 0;
    int     tailGrainN_ = 0;
    bool    spliceOn_ = false;
    bool    spliceHandoff_ = false;
    double  splicePos_ = 0.0;
    double  spliceFrom_ = 0.0;
    double  spliceBack_ = 0.0;
    double  spliceLead_ = 0.0;
    double  spliceRate_ = 1.0;
    int     spliceFade_ = 0;
    int     spliceFadeN_ = 0;
    int     spliceHop_ = 0;
    int     spliceLeft_ = 0;
    int     spliceElapsed_ = 0;
    double  spliceAhead_ = 0.0;
    int     playLeft_ = 0;
    float   outGrain_[kChannels] {};
    float   rateB0_[3] {};
    float   rateB1_[3] {};
    float   rateB2_[3] {};
    float   rateA1_[3] {};
    float   rateA2_[3] {};
    float   rateZ1_[3][kChannels] {};
    float   rateZ2_[3][kChannels] {};
    float   rateHeld_[kChannels] {};
    double  rateClock_ = 1.0;
    double  rateHost_ = 0.0;
    float   rateCut_ = -1.0f;
    bool    rateLive_ = false;
};

MPC_PLUGIN_ENTRY(MPStretch)
