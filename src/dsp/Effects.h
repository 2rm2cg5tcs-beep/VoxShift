// Small, allocation-free audio effects (mono, float in / float out).
// Everything here is plain portable C++ so it is unit-tested on any OS.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vox {

constexpr double kPiD = 3.14159265358979323846;

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

// One-pole smoother for click-free parameter changes.
struct Smoother {
    double value = 0.0, target = 0.0, coef = 0.0;
    void setTime(double seconds, double sr) { coef = std::exp(-1.0 / std::max(1.0, seconds * sr)); }
    void reset(double v) { value = target = v; }
    inline double next() { return value = target + (value - target) * coef; }
    bool settled() const { return std::abs(value - target) < 1e-6; }
};

// RBJ-cookbook biquad, transposed direct form II.
class Biquad {
public:
    enum class Type { Lowpass, Highpass, Peak, LowShelf, HighShelf };

    void set(Type t, double sr, double freq, double q, double gainDb = 0.0) {
        freq = std::clamp(freq, 10.0, sr * 0.49);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPiD * freq / sr;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        switch (t) {
            case Type::Lowpass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Highpass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::LowShelf: {
                const double sq = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sq); a0 = (A + 1) + (A - 1) * cw + sq;
                a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq; break;
            }
            case Type::HighShelf:
            default: {
                const double sq = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sq); a0 = (A + 1) - (A - 1) * cw + sq;
                a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq; break;
            }
        }
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }
    inline double tick(double x) {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
    void reset() { z1_ = z2_ = 0.0; }

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

// 24 dB/oct Butterworth (two cascaded biquads).
class Filter24 {
public:
    void set(bool highpass, double sr, double freq) {
        const auto t = highpass ? Biquad::Type::Highpass : Biquad::Type::Lowpass;
        a_.set(t, sr, freq, 0.54119610);
        b_.set(t, sr, freq, 1.30656296);
    }
    inline double tick(double x) { return b_.tick(a_.tick(x)); }
    void reset() { a_.reset(); b_.reset(); }

private:
    Biquad a_, b_;
};

// Peak envelope follower with separate attack/release.
struct Envelope {
    double env = 0.0, att = 0.0, rel = 0.0;
    void set(double sr, double attackMs, double releaseMs) {
        att = std::exp(-1.0 / (attackMs * 0.001 * sr));
        rel = std::exp(-1.0 / (releaseMs * 0.001 * sr));
    }
    inline double tick(double x) {
        const double a = std::abs(x);
        env = a > env ? att * env + (1 - att) * a : rel * env + (1 - rel) * a;
        return env;
    }
};

// Downward expander used as a noise gate (smooth, no chatter).
class NoiseGate {
public:
    void prepare(double sr) { env_.set(sr, 1.0, 120.0); gain_.setTime(0.004, sr); hold_ = int(sr * 0.08); gain_.reset(1.0); }
    void setThreshold(double db) { thr_ = dbToGain(db); }
    inline double tick(double x) {
        const double e = env_.tick(x);
        if (e >= thr_) holdLeft_ = hold_;
        double target = 1.0;
        if (holdLeft_ > 0) {
            --holdLeft_;
        } else if (e < thr_) {
            const double r = std::max(e, 1e-9) / thr_;
            target = std::max(r * r * r * r * r * r * r, 0.0);   // ratio 8 expander
        }
        gain_.target = target;
        return x * gain_.next();
    }
    void reset() { env_.env = 0.0; gain_.reset(1.0); holdLeft_ = 0; }

private:
    Envelope env_;
    Smoother gain_;
    double thr_ = 0.001;
    int hold_ = 0, holdLeft_ = 0;
};

class Compressor {
public:
    void prepare(double sr) { env_.set(sr, 6.0, 140.0); }
    void set(double thresholdDb, double ratio, double makeupDb) {
        thr_ = dbToGain(thresholdDb); slope_ = 1.0 / ratio - 1.0; makeup_ = dbToGain(makeupDb);
    }
    inline double tick(double x) {
        const double e = env_.tick(x);
        const double g = e > thr_ ? std::pow(e / thr_, slope_) : 1.0;
        return x * g * makeup_;
    }
    void reset() { env_.env = 0.0; }

private:
    Envelope env_;
    double thr_ = 0.1, slope_ = -0.5, makeup_ = 1.0;
};

// Brick-wall-ish peak limiter (instant attack, smooth release) + safety clip.
class Limiter {
public:
    void prepare(double sr) { rel_ = std::exp(-1.0 / (0.08 * sr)); }
    void setThreshold(double db) { thr_ = dbToGain(db); }
    inline double tick(double x) {
        const double a = std::abs(x);
        const double need = a > thr_ ? thr_ / a : 1.0;
        gain_ = need < gain_ ? need : rel_ * gain_ + (1 - rel_) * need;
        return std::clamp(x * gain_, -thr_, thr_);
    }
    void reset() { gain_ = 1.0; }

private:
    double thr_ = 0.89, rel_ = 0.999, gain_ = 1.0;
};

// Circular delay line with fractional (linear) reads.
class DelayLine {
public:
    void prepare(size_t maxSamples) { buf_.assign(maxSamples + 4, 0.0f); w_ = 0; }
    inline void push(double x) { buf_[w_] = float(x); w_ = (w_ + 1) % buf_.size(); }
    inline double read(double delay) const {   // delay in samples (>= 1)
        const size_t n = buf_.size();
        delay = std::clamp(delay, 1.0, double(n - 3));
        const double pos = double(w_) - delay;
        const double p = pos < 0 ? pos + double(n) : pos;
        const size_t i0 = size_t(p) % n, i1 = (i0 + 1) % n;
        const double f = p - std::floor(p);
        return buf_[i0] * (1.0 - f) + buf_[i1] * f;
    }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }

private:
    std::vector<float> buf_;
    size_t w_ = 0;
};

class Echo {
public:
    void prepare(double sr) { sr_ = sr; line_.prepare(size_t(sr * 1.1)); time_.setTime(0.08, sr); time_.reset(0.25 * sr); }
    void set(double seconds, double feedback, double mix) { time_.target = seconds * sr_; fb_ = feedback; mix_ = mix; }
    inline double tick(double x) {
        const double d = line_.read(time_.next());
        line_.push(x + d * fb_);
        return x * (1.0 - mix_) + d * mix_;
    }
    void reset() { line_.clear(); }

private:
    DelayLine line_;
    Smoother time_;
    double sr_ = 48000, fb_ = 0.35, mix_ = 0.3;
};

// Two-voice chorus (quadrature LFOs), slight feedback.
class Chorus {
public:
    void prepare(double sr) { sr_ = sr; line_.prepare(size_t(sr * 0.05)); }
    void set(double mix, double depth) { mix_ = mix; depth_ = depth; }
    inline double tick(double x) {
        ph_ += 2.0 * kPiD * 1.3 / sr_;
        if (ph_ > 2.0 * kPiD) ph_ -= 2.0 * kPiD;
        const double centre = 0.008 * sr_, swing = 0.006 * sr_ * depth_;
        const double a = line_.read(centre + swing * std::sin(ph_));
        const double b = line_.read(centre * 1.37 + swing * std::cos(ph_));
        const double wet = 0.5 * (a + b);
        line_.push(x + wet * 0.2);
        return x * (1.0 - 0.5 * mix_) + wet * mix_;
    }
    void reset() { line_.clear(); }

private:
    DelayLine line_;
    double sr_ = 48000, mix_ = 0.5, depth_ = 0.3, ph_ = 0.0;
};

// Freeverb (Jezar at Dreampoint, public domain), mono.
class Reverb {
public:
    void prepare(double sr) {
        static const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int apT[4] = {556, 441, 341, 225};
        const double s = sr / 44100.0;
        for (int i = 0; i < 8; ++i) combs_[i].init(size_t(combT[i] * s));
        for (int i = 0; i < 4; ++i) aps_[i].init(size_t(apT[i] * s));
    }
    void set(double roomSize, double mix) {
        fb_ = 0.7 + 0.28 * std::clamp(roomSize, 0.0, 1.0);
        damp_ = 0.4 * 0.45;
        wet_ = mix * 0.9;
        dry_ = 1.0 - 0.4 * mix;
    }
    inline double tick(double x) {
        const double in = x * 0.015;
        double acc = 0.0;
        for (auto& c : combs_) acc += c.tick(in, fb_, damp_);
        for (auto& a : aps_) acc = a.tick(acc);
        return x * dry_ + acc * wet_ * 3.0;
    }
    void reset() {
        for (auto& c : combs_) c.clear();
        for (auto& a : aps_) a.clear();
    }

private:
    struct Comb {
        std::vector<float> b; size_t i = 0; double store = 0.0;
        void init(size_t n) { b.assign(std::max<size_t>(n, 1), 0.0f); i = 0; store = 0.0; }
        void clear() { std::fill(b.begin(), b.end(), 0.0f); store = 0.0; }
        inline double tick(double in, double fb, double damp) {
            const double out = b[i];
            store = out * (1.0 - damp) + store * damp;
            b[i] = float(in + store * fb);
            if (++i >= b.size()) i = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> b; size_t i = 0;
        void init(size_t n) { b.assign(std::max<size_t>(n, 1), 0.0f); i = 0; }
        void clear() { std::fill(b.begin(), b.end(), 0.0f); }
        inline double tick(double in) {
            const double bo = b[i];
            b[i] = float(in + bo * 0.5);
            if (++i >= b.size()) i = 0;
            return bo - in;
        }
    };
    Comb combs_[8];
    Allpass aps_[4];
    double fb_ = 0.84, damp_ = 0.2, wet_ = 0.3, dry_ = 1.0;
};

// Sine oscillator with continuous phase.
struct Lfo {
    double phase = 0.0;
    inline double tick(double freq, double sr) {
        phase += 2.0 * kPiD * freq / sr;
        if (phase >= 2.0 * kPiD) phase -= 2.0 * kPiD;
        return std::sin(phase);
    }
};

}  // namespace vox
