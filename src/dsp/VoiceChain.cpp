#include "VoiceChain.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
constexpr size_t kBlock = 240;   // internal block: 5 ms @ 48 kHz
}

VoiceChain::VoiceChain(int sampleRate, Quality quality)
    : sr_(sampleRate), core_(sampleRate, quality), denoiser_(sampleRate),
      psola_(sampleRate, core_.latency() - denoiser_.latency(), denoiser_.latency()), raw_(kBlock),
      pendingVoice_(defaultVoice()), pendingGlobal_(defaultGlobal()),
      v_(defaultVoice()), g_(defaultGlobal()) {
    const double sr = double(sr_);
    rumble_.set(Biquad::Type::Highpass, sr, 70.0, 0.7071);
    gate_.prepare(sr);
    chorus_.prepare(sr);
    echo_.prepare(sr);
    reverb_.prepare(sr);
    comp_.prepare(sr);
    limiter_.prepare(sr);
    limiter_.setThreshold(-1.0);
    for (Smoother* s : {&inGain_, &levelGain_, &outGain_, &ringAmt_}) s->setTime(0.012, sr);
    inGain_.reset(1.0); levelGain_.reset(1.0); outGain_.reset(1.0); ringAmt_.reset(0.0);
    sleepAfter_ = size_t(sr * 2.0);
    wakeLevel_ = dbToGain(-66.0);
    applyPending();
    inGain_.reset(inGain_.target); levelGain_.reset(levelGain_.target); outGain_.reset(outGain_.target);
}

int VoiceChain::coreMode(const VoiceValues& v) {
    if (v[P_Robot] > 0.0 || v[P_Whisper] > 0.0) return 2;
    const bool id = std::abs(v[P_Pitch]) < 0.01 && std::abs(v[P_Formant]) < 0.01 && v[P_TargetF0] < 65.0 &&
                    v[P_TargetVtl] < 9.0 && v[P_VibratoDepth] <= 0.0 && v[P_Breath] <= 0.004;
    return id ? 0 : 1;
}

void VoiceChain::setSpeaker(double hz, double confidence, double tractCm, double tractConf) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pendingSpeakerHz_ = hz;
        pendingSpeakerConf_ = confidence;
        pendingTractCm_ = tractCm;
        pendingTractConf_ = tractConf;
    }
    dirty_.store(true);
}

void VoiceChain::setVoice(const VoiceValues& v, bool smooth) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // a different voice core cannot take over mid-word without a click
        if (coreMode(v) != coreMode(pendingVoice_)) smooth = true;
        for (size_t i = 0; i < v.size(); ++i) {
            const ParamDef& d = kVoiceParams[i];
            const double x = v[i];
            pendingVoice_[i] = (x == x) ? std::clamp(x, d.min, d.max) : d.def;
        }
    }
    if (smooth) duckRequest_.store(true);
    dirty_.store(true);
}

void VoiceChain::setGlobal(const GlobalValues& g) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (size_t i = 0; i < g.size(); ++i) {
            const ParamDef& d = kGlobalParams[i];
            const double x = g[i];
            pendingGlobal_[i] = (x == x) ? std::clamp(x, d.min, d.max) : d.def;
        }
    }
    dirty_.store(true);
}

void VoiceChain::setEnabled(bool on) {
    bool changed;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        changed = pendingEnabled_ != on;
        pendingEnabled_ = on;
    }
    if (changed) duckRequest_.store(true);
    dirty_.store(true);
}

void VoiceChain::applyPending() {
    {
        std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
        if (!lk.owns_lock()) return;          // UI is writing right now: next block
        dirty_.store(false);
        enabled_ = pendingEnabled_;
        v_ = enabled_ ? pendingVoice_ : defaultVoice();
        g_ = pendingGlobal_;
        if (pendingSpeakerHz_ > 0.0) {
            psola_.setSpeaker(pendingSpeakerHz_, pendingSpeakerConf_);
            pendingSpeakerHz_ = 0.0;
        }
        if (pendingTractCm_ > 0.0) {
            psola_.setVocalTract(pendingTractCm_, pendingTractConf_);
            pendingTractCm_ = 0.0;
        }
    }
    const double sr = double(sr_);
    const VoiceValues& v = v_;
    const GlobalValues& g = g_;

    const double pitchRatio = std::abs(v[P_Pitch]) >= 0.01 ? semitonesToRatio(v[P_Pitch]) : 1.0;
    const double formantRatio = std::abs(v[P_Formant]) >= 0.01 ? semitonesToRatio(v[P_Formant]) : 1.0;
    const bool psola = coreMode(v) != 2;
    if (psola != usePsola_) {
        // the other path has stale buffers: start it clean, stay silent until
        // its first real output arrives, then fade in
        usePsola_ = psola;
        if (psola) {
            denoiser_.reset();
            psola_.reset();
        } else {
            core_.reset();
        }
        mute_ = size_t(core_.latency());
        fadeIn_ = true;
    }
    CoreParams cp;
    cp.pitchRatio = pitchRatio;
    cp.formantRatio = formantRatio;
    cp.robot = v[P_Robot];
    cp.whisper = v[P_Whisper];
    cp.vibratoDepth = v[P_VibratoDepth];
    cp.vibratoRate = v[P_VibratoRate];
    cp.dry = v[P_Dry];
    cp.denoise = g[G_Denoise];
    core_.setParams(cp);

    PsolaParams pp;
    pp.pitchRatio = pitchRatio;
    pp.formantRatio = formantRatio;
    pp.targetF0 = v[P_TargetF0] >= 65.0 ? v[P_TargetF0] : 0.0;
    pp.targetVtl = v[P_TargetVtl] >= 9.0 ? v[P_TargetVtl] : 0.0;
    pp.intonation = v[P_Intonation];
    pp.vibratoDepth = v[P_VibratoDepth];
    pp.vibratoRate = v[P_VibratoRate];
    pp.dry = v[P_Dry];
    pp.breath = v[P_Breath] > 0.004 ? v[P_Breath] : 0.0;
    psola_.setParams(pp);
    denoiser_.setAmount(g[G_Denoise]);

    a_.gate = g[G_Gate] > -89.5;
    gate_.setThreshold(g[G_Gate]);
    wakeLevel_ = dbToGain(a_.gate ? std::max(-66.0, g[G_Gate] - 6.0) : -66.0);

    a_.eqLow = std::abs(v[P_Bass]) >= 0.05;
    a_.eqMid = std::abs(v[P_Mid]) >= 0.05;
    a_.eqHigh = std::abs(v[P_Treble]) >= 0.05;
    lowShelf_.set(Biquad::Type::LowShelf, sr, 220.0, 0.7071, v[P_Bass]);
    midPeak_.set(Biquad::Type::Peak, sr, 1300.0, 0.8, v[P_Mid]);
    highShelf_.set(Biquad::Type::HighShelf, sr, 4200.0, 0.7071, v[P_Treble]);

    a_.hp = v[P_Highpass] > 21.0;
    a_.lp = v[P_Lowpass] < 19900.0;
    hp_.set(true, sr, v[P_Highpass]);
    lp_.set(false, sr, std::min(v[P_Lowpass], sr * 0.45));

    a_.dist = v[P_Distortion] > 0.05;
    distDrive_ = dbToGain(v[P_Distortion]);
    a_.crush = v[P_Bitcrush] > 0.004;
    crushStep_ = 2.0 / std::pow(2.0, 16.0 - 12.0 * v[P_Bitcrush]);
    a_.lofi = v[P_Lofi] > 0.004;
    holdFactor_ = 1 + int(std::lround(v[P_Lofi] * 11.0));
    a_.chorus = v[P_Chorus] > 0.004;
    chorus_.set(std::min(1.0, v[P_Chorus]), 0.15 + 0.35 * v[P_Chorus]);
    a_.trem = v[P_TremoloDepth] > 0.004;
    tremDepth_ = v[P_TremoloDepth];
    tremRate_ = v[P_TremoloRate];
    ringAmt_.target = v[P_RingMix];
    ringFreq_ = v[P_RingFreq];
    a_.ring = v[P_RingMix] > 0.004 || ringAmt_.value > 1e-4;
    a_.echo = v[P_EchoMix] > 0.004;
    echo_.set(v[P_EchoTime], v[P_EchoFeedback], v[P_EchoMix] * 0.6);
    a_.reverb = v[P_ReverbMix] > 0.004;
    reverb_.set(v[P_ReverbSize], v[P_ReverbMix]);

    const double c = g[G_Compress];
    a_.comp = c > 0.004;
    comp_.set(-8.0 - 22.0 * c, 1.5 + 4.5 * c, 9.0 * c);

    const double distComp = a_.dist ? -0.55 * v[P_Distortion] : 0.0;
    levelGain_.target = dbToGain(v[P_Level] + distComp);
    inTarget_ = dbToGain(g[G_InputGain]);
    inGain_.target = inTarget_;
    outGain_.target = dbToGain(g[G_OutputVolume]);
}

void VoiceChain::reset() {
    core_.reset();
    denoiser_.reset();
    psola_.reset();
    for (Biquad* b : {&rumble_, &lowShelf_, &midPeak_, &highShelf_}) b->reset();
    hp_.reset(); lp_.reset(); gate_.reset(); chorus_.reset(); echo_.reset(); reverb_.reset();
    comp_.reset(); limiter_.reset(); drive_.reset();
    holdCounter_ = 0; holdValue_ = 0.0;
}

void VoiceChain::process(float* data, size_t count) {
    // drivers occasionally deliver NaN/Inf garbage on hiccups
    for (size_t i = 0; i < count; ++i) {
        float s = data[i];
        data[i] = std::isfinite(s) ? std::clamp(s, -4.0f, 4.0f) : 0.0f;
    }
    for (size_t pos = 0; pos < count; pos += kBlock) processBlock(data + pos, std::min(kBlock, count - pos), true);
}

void VoiceChain::processBlock(float* x, size_t n, bool) {
    // ---- idle mode: while you are silent the chain sleeps (≈0 % CPU)
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(x[i]));
    if (double(peak) * inTarget_ > wakeLevel_) {
        silent_ = 0;
        if (sleeping_) {
            sleeping_ = false;
            reset();
            fadeIn_ = true;
        }
    } else {
        silent_ += n;
        if (!sleeping_ && silent_ > sleepAfter_ && !duckRequest_.load()) {
            sleeping_ = true;
            runBlock(x, n);                       // last block: fade the tails out
            for (size_t i = 0; i < n; ++i) x[i] *= float(1.0 - double(i) / double(n));
            return;
        }
    }
    if (sleeping_) {
        if (dirty_.load()) applyPending();
        duckRequest_.store(false);
        std::fill(x, x + n, 0.0f);
        return;
    }

    if (duckRequest_.exchange(false)) {
        // finish this block with the old settings, fading to silence; the new
        // settings are applied on the next block, which fades back in
        runBlock(x, n);
        for (size_t i = 0; i < n; ++i) x[i] *= float(1.0 - double(i) / double(n));
        fadeIn_ = true;
        return;
    }
    if (dirty_.load()) applyPending();
    runBlock(x, n);
    if (mute_ > 0) {
        // a freshly started core: silence until its first real output
        const size_t m = std::min(mute_, n);
        std::fill(x, x + m, 0.0f);
        mute_ -= m;
        if (m == n) return;
        const size_t r = n - m;
        for (size_t i = 0; i < r; ++i) x[m + i] *= float(double(i) / double(r));
        fadeIn_ = false;
        return;
    }
    if (fadeIn_) {
        fadeIn_ = false;
        for (size_t i = 0; i < n; ++i) x[i] *= float(double(i) / double(n));
    }
}

void VoiceChain::runBlock(float* x, size_t n) {
    const double sr = double(sr_);
    const Active a = a_;

    for (size_t i = 0; i < n; ++i) {
        double s = double(x[i]) * inGain_.next();
        s = rumble_.tick(s);
        if (a.gate) s = gate_.tick(s);
        x[i] = float(s);
    }

    if (usePsola_) {
        // the pitch analysis gets the signal before the denoiser: its latency
        // becomes free look-ahead
        std::copy(x, x + n, raw_.data());
        denoiser_.process(x, n);
        psola_.process(x, n, raw_.data());
        speakerConf_.store(psola_.speakerConfidence(), std::memory_order_relaxed);
    } else {
        core_.process(x, n);
    }

    for (size_t i = 0; i < n; ++i) {
        double s = x[i];
        if (a.ring) {
            const double amt = ringAmt_.next();
            const double car = ringLfo_.tick(ringFreq_, sr);
            s = s * (1.0 - amt) + s * car * 1.4 * amt;
        }
        if (a.eqLow) s = lowShelf_.tick(s);
        if (a.eqMid) s = midPeak_.tick(s);
        if (a.eqHigh) s = highShelf_.tick(s);
        if (a.hp) s = hp_.tick(s);
        if (a.lp) s = lp_.tick(s);
        if (a.dist) s = drive_.tick(s, distDrive_);
        if (a.crush) s = std::round(s / crushStep_) * crushStep_;
        if (a.lofi && holdFactor_ > 1) {
            if (holdCounter_ == 0) holdValue_ = s;
            s = holdValue_;
            if (++holdCounter_ >= holdFactor_) holdCounter_ = 0;
        }
        if (a.chorus) s = chorus_.tick(s);
        if (a.trem) {
            const double l = tremLfo_.tick(tremRate_, sr);
            s *= 1.0 - tremDepth_ * 0.5 + tremDepth_ * 0.5 * l;
        }
        if (a.echo) s = echo_.tick(s);
        if (a.reverb) s = reverb_.tick(s);
        s *= levelGain_.next();
        if (a.comp) s = comp_.tick(s);
        s *= outGain_.next();
        s = limiter_.tick(s);
        x[i] = std::isfinite(s) ? float(s) : 0.0f;
    }
    if (a.ring && ringAmt_.target <= 0.004 && ringAmt_.value < 1e-4) a_.ring = false;
}

}  // namespace vox
