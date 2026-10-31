#include "PsolaVoice.h"

#include <algorithm>
#include <cmath>
#ifdef PSOLA_DEBUG
#include <cstdio>
#endif

namespace vox {

namespace {
constexpr double kPi = 3.14159265358979323846;

size_t nextPow2(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

double besselI0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k) {
        const double h = x / (2.0 * k);
        t *= h * h;
        s += t;
        if (t < 1e-14 * s) break;
    }
    return s;
}

#ifndef PSOLA_AP_ON
#define PSOLA_AP_ON 0.28
#define PSOLA_AP_OFF 0.46
#endif
#ifndef PSOLA_LEARN_AP
#define PSOLA_LEARN_AP 0.3
#endif
constexpr double kApOn = PSOLA_AP_ON, kApOff = PSOLA_AP_OFF;   // YIN aperiodicity: start / keep voicing
constexpr double kLearnAp = PSOLA_LEARN_AP;                     // frames clear enough to learn the speaker from
constexpr double kFmin = 58.0, kFmax = 700.0;
}  // namespace

PsolaVoice::PsolaVoice(int sampleRate, int latency, int analysisAhead)
    : sr_(sampleRate), tract_(double(sampleRate) / std::max(1, int(std::lround(sampleRate / 12000.0)))) {
    const double fs = double(sr_);
    const double sc = fs / 48000.0;
    D_ = latency > 0 ? size_t(latency) : size_t(std::lround(1024 * sc));
    ahead_ = size_t(std::max(0, analysisAhead));
    G_ = std::max<size_t>(8, size_t(std::lround(48 * sc)));   // 1 ms processing step
    H_ = size_t(std::lround(fs * 0.005));                     // 5 ms analysis frames
    U_ = size_t(std::lround(fs * 0.005));                     // grain spacing for unvoiced sounds
    q_ = std::max(1, int(std::lround(fs / 12000.0)));
    fsd_ = fs / q_;
    Wd_ = int(std::lround(0.012 * fsd_));
    tauMin_ = std::max(2, int(std::floor(fsd_ / kFmax)));
    tauMax_ = int(std::ceil(fsd_ / kFmin));
    la_ = Wd_ / 2 + tauMax_ / 2 + 3;

    // decimation low-pass (Kaiser-windowed sinc), cut-off 80 % of the new Nyquist
    if (q_ > 1) {
        const int L = 8 * q_ + 1, M = L / 2;
        const double fc = 0.4 / q_, beta = 7.0, i0b = besselI0(beta);
        decFir_.resize(size_t(L));
        double sum = 0.0;
        for (int i = 0; i < L; ++i) {
            const double d = double(i - M);
            const double sinc = d == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * d) / (kPi * d);
            const double r = d / M;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            decFir_[size_t(i)] = sinc * w;
            sum += sinc * w;
        }
        for (double& h : decFir_) h /= sum;
        decDelay_ = M;
    } else {
        decFir_.assign(1, 1.0);
        decDelay_ = 0;
    }

    const size_t span = D_ + ahead_ + size_t(fs / kFmin) * 4 + 4096;
    in_.assign(nextPow2(span), 0.0f);
    ana_.assign(in_.size(), 0.0f);
    inMask_ = in_.size() - 1;
    dec_.assign(nextPow2(size_t(4 * (la_ + Wd_ + tauMax_) + 64)), 0.0f);
    decMask_ = dec_.size() - 1;
    frames_.assign(kFrameMask + 1, Frame{});
    diff_.assign(size_t(tauMax_) + 2, 0.0);
    cmnd_.assign(size_t(tauMax_) + 2, 1.0);
    marks_.assign(kMarkMask + 1, Mark{});
    acc_.assign(nextPow2(size_t(fs / kFmin) * 8 + D_ + 2048), 0.0);
    wsum_.assign(acc_.size(), 0.0);
    vsum_.assign(acc_.size(), 0.0);
    outMask_ = acc_.size() - 1;
    sinc_.assign(size_t(kPhases + 1) * kTaps, 0.0f);
    fifoIn_.assign(G_, 0.0f);
    fifoAna_.assign(G_, 0.0f);
    fifoOut_.assign(G_, 0.0f);

    hfVoice_.set(Biquad::Type::Highpass, fs, 2200.0, 0.7071);
    noiseHp_.set(Biquad::Type::Highpass, fs, 1500.0, 0.7071);
    noiseLp_.set(Biquad::Type::Lowpass, fs, std::min(8000.0, fs * 0.45), 0.7071);
    envAtt_ = 1.0 - std::exp(-1.0 / (0.0003 * fs));
    envRel_ = 1.0 - std::exp(-1.0 / (0.004 * fs));
    vgateCoef_ = 1.0 - std::exp(-1.0 / (0.006 * fs));

    spk_ = std::log2(125.0);
    hist_.assign(size_t(kHistBins), 0.0);
    lpcBuf_.assign(size_t(0.025 * fsd_) + 1, 0.0f);
    buildSinc(1.0);
    reset();
}

void PsolaVoice::reset() {
    std::fill(in_.begin(), in_.end(), 0.0f);
    std::fill(ana_.begin(), ana_.end(), 0.0f);
    std::fill(dec_.begin(), dec_.end(), 0.0f);
    std::fill(fifoIn_.begin(), fifoIn_.end(), 0.0f);
    std::fill(fifoAna_.begin(), fifoAna_.end(), 0.0f);
    std::fill(fifoOut_.begin(), fifoOut_.end(), 0.0f);
    rover_ = 0;
    n_ = 0;
    nd_ = 0;
    framesReady_ = 0;
    peakRms_ = 0.0;
    prevTau_ = 0.0;
    rawState_ = false;
    vibPhase_ = 0.0;
    hfVoice_.reset();
    noiseHp_.reset();
    noiseLp_.reset();
    env_ = vgate_ = 0.0;
    lastF0_.store(0.0, std::memory_order_relaxed);
    restartSynthesis(-int64_t(D_));
}

void PsolaVoice::restartSynthesis(int64_t at) {
    std::fill(acc_.begin(), acc_.end(), 0.0);
    std::fill(wsum_.begin(), wsum_.end(), 0.0);
    std::fill(vsum_.begin(), vsum_.end(), 0.0);
    markCount_ = 1;
    mark(0) = Mark{double(at), double(U_), false};
    phi_ = 0.0;
    sCur_ = double(at);
    sPrev_ = sCur_ - double(U_);
}

void PsolaVoice::setSpeaker(double hz, double confidence) {
    if (!(hz >= kFmin && hz <= kFmax)) return;
    spk_ = std::log2(hz);
    conf_ = std::max(0.0, confidence);
    // seed the histogram with a soft bump at the remembered pitch
    std::fill(hist_.begin(), hist_.end(), 0.0);
    const double w = std::min(conf_, 1500.0);
    const int c = std::clamp(int((spk_ - kHistBase) * kBinsPerOct), 0, kHistBins - 1);
    double sum = 0.0;
    for (int d = -4; d <= 4; ++d) {
        const int k = c + d;
        if (k < 0 || k >= kHistBins) continue;
        const double g = std::exp(-0.5 * (d / 2.0) * (d / 2.0));
        hist_[size_t(k)] = g;
        sum += g;
    }
    for (double& h : hist_) h *= w / sum;
    histTotal_ = w;
    speakerHz_.store(hz, std::memory_order_relaxed);
}

// With auto pitch the formant shift scales with how far the voice is moved:
// a man choosing "Девушка" gets the full shift, a woman choosing it keeps
// her own timbre (instead of turning into a child).
double PsolaVoice::effectiveAlpha() const {
    if (p_.targetVtl > 0.0) {
        // from the speaker's measured tract to the target one. The meter reads
        // a warped voice as only alpha^0.72 shorter (calibrated on 18 male
        // voices), hence the exponent 1/0.72: the result *measures* as the target.
        double user = tract_.confidence() >= 50.0 ? tract_.lengthCm() : 0.0;
        if (user <= 0.0) user = std::exp2(spk_) < 165.0 ? 16.9 : 14.8;  // typical man / woman
        const double a = std::pow(user / p_.targetVtl, 1.39);
        return std::clamp(a, 0.6, 1.7) * p_.formantRatio;
    }
    if (p_.targetF0 <= 0.0 || p_.formantRatio == 1.0) return p_.formantRatio;
    const double s = std::min(1.0, std::abs(std::log2(autoRatio())) / std::log2(1.6));
    return std::pow(p_.formantRatio, s);
}

double PsolaVoice::autoRatio() const {
    if (p_.targetF0 <= 0.0) return 1.0;
    return std::clamp(p_.targetF0 / std::exp2(spk_), 0.35, 3.2);
}

void PsolaVoice::setParams(const PsolaParams& p) {
    const bool id = p.isIdentity();
    if (identity_ && !id) restartSynthesis(n_ - int64_t(D_) + int64_t(G_));
    identity_ = id;
    p_ = p;
    const double a = effectiveAlpha();
    if (std::abs(a - sincAlpha_) > 1e-9) buildSinc(a);
}

void PsolaVoice::buildSinc(double alpha) {
    // windowed-sinc interpolator; when the grain is read faster than real time
    // (formants up) the cut-off drops to avoid aliasing
    sincAlpha_ = alpha;
    const double fc = 0.94 * std::min(1.0, 1.0 / std::max(1e-3, alpha));
    const double beta = 8.0, i0b = besselI0(beta), half = kTaps / 2;
    for (int ph = 0; ph <= kPhases; ++ph) {
        const double frac = double(ph) / kPhases;
        double sum = 0.0;
        float* row = &sinc_[size_t(ph) * kTaps];
        for (int t = 0; t < kTaps; ++t) {
            const double d = double(t) - (half - 1.0) - frac;  // tap t reads sample floor(pos) - 7 + t
            const double x = kPi * fc * d;
            const double s = std::abs(d) < 1e-12 ? 1.0 : std::sin(x) / x;
            const double r = d / half;
            const double w = std::abs(r) < 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / i0b : 0.0;
            row[t] = float(s * w);
            sum += s * w;
        }
        for (int t = 0; t < kTaps; ++t) row[t] = float(row[t] / sum);
    }
}

inline double PsolaVoice::sampleAt(double pos) const {
    const double fl = std::floor(pos);
    int64_t i0 = int64_t(fl);
    const int ph = int((pos - fl) * kPhases + 0.5);
    const float* row = &sinc_[size_t(ph) * kTaps];
    const int64_t base = i0 - (kTaps / 2 - 1);
    double acc = 0.0;
    for (int t = 0; t < kTaps; ++t) acc += double(row[t]) * double(in_[size_t(base + t) & inMask_]);
    return acc;
}

void PsolaVoice::process(float* data, size_t count, const float* ahead) {
    for (size_t i = 0; i < count; ++i) {
        fifoIn_[rover_] = data[i];
        fifoAna_[rover_] = (ahead && ahead_ > 0) ? ahead[i] : data[i];
        data[i] = fifoOut_[rover_];
        if (++rover_ == G_) {
            rover_ = 0;
            tick();
        }
    }
}

void PsolaVoice::tick() {
    const int64_t a0 = (ahead_ > 0) ? n_ + int64_t(ahead_) : n_;
    for (size_t j = 0; j < G_; ++j) {
        in_[size_t(n_ + int64_t(j)) & inMask_] = fifoIn_[j];
        const int64_t t = a0 + int64_t(j);
        ana_[size_t(t) & inMask_] = fifoAna_[j];
        if (t % q_ == 0) {
            const int64_t jd = t / q_;
            double s = 0.0;
            for (size_t k = 0; k < decFir_.size(); ++k) s += decFir_[k] * double(ana_[size_t(t - int64_t(k)) & inMask_]);
            dec_[size_t(jd) & decMask_] = float(s);
            nd_ = jd + 1;
        }
    }
    n_ += int64_t(G_);

    // pitch frames whose look-ahead has arrived
    for (;;) {
        const int64_t i = framesReady_;
        const int64_t c = (i * int64_t(H_) + decDelay_ + q_ / 2) / q_;
        if (nd_ <= c + la_) break;
        analyseFrame(i);
        ++framesReady_;
    }

    const int64_t emitStart = n_ - int64_t(D_);
    const int64_t emitEnd = emitStart + int64_t(G_);
    if (!identity_) synthesise(emitStart, emitEnd);

    const double dry = p_.dry;
    for (size_t j = 0; j < G_; ++j) {
        const int64_t t = emitStart + int64_t(j);
        const size_t o = size_t(t) & outMask_;
        const double x = t >= 0 ? double(in_[size_t(t) & inMask_]) : 0.0;
        double y;
        if (identity_) {
            y = x * (1.0 + std::max(0.0, dry));
        } else {
            const double ws = wsum_[o];
            y = acc_[o] / std::max(ws, 1.0);
            const double vf = ws > 1e-6 ? vsum_[o] / ws : 0.0;
            if (p_.breath > 0.0) {
                // replace part of the high harmonics by pitch-synchronous noise
                const double hf = hfVoice_.tick(y);
                const double a = std::abs(hf);
                env_ += (a > env_ ? envAtt_ : envRel_) * (a - env_);
                vgate_ += vgateCoef_ * (vf - vgate_);
                rng_ = rng_ * 1664525u + 1013904223u;
                const double u1 = double(rng_ >> 8) * (1.0 / 16777216.0);
                rng_ = rng_ * 1664525u + 1013904223u;
                const double u2 = double(rng_ >> 8) * (1.0 / 16777216.0);
                const double nz = noiseLp_.tick(noiseHp_.tick((u1 + u2 - 1.0) * 2.449));
                const double b = std::min(1.0, p_.breath) * vgate_;
                y += b * (2.6 * env_ * nz - 0.45 * hf);
            }
            y += dry * x;
        }
        acc_[o] = 0.0;
        wsum_[o] = 0.0;
        vsum_[o] = 0.0;
        fifoOut_[j] = std::isfinite(y) ? float(y) : 0.0f;
    }
}

// ------------------------------------------------------------------ analysis
void PsolaVoice::analyseFrame(int64_t i) {
    const int64_t c = (i * int64_t(H_) + decDelay_ + q_ / 2) / q_;
    const int W = Wd_;
    auto x = [&](int64_t k) { return double(dec_[size_t(k) & decMask_]); };

    // symmetric YIN difference: both halves centred on the frame for every lag
    diff_[0] = 0.0;
    for (int tau = 1; tau <= tauMax_; ++tau) {
        const int64_t a = c - W / 2 - tau / 2;
        double s = 0.0;
        for (int j = 0; j < W; ++j) {
            const double d = x(a + j) - x(a + j + tau);
            s += d * d;
        }
        diff_[size_t(tau)] = s;
    }
    cmnd_[0] = 1.0;
    double run = 0.0;
    for (int tau = 1; tau <= tauMax_; ++tau) {
        run += diff_[size_t(tau)];
        cmnd_[size_t(tau)] = run > 1e-20 ? diff_[size_t(tau)] * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = tauMin_; tau < tauMax_; ++tau) {
        if (cmnd_[size_t(tau)] < 0.2) {
            while (tau + 1 < tauMax_ && cmnd_[size_t(tau + 1)] < cmnd_[size_t(tau)]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = tauMin_;
        for (int tau = tauMin_; tau < tauMax_; ++tau)
            if (cmnd_[size_t(tau)] < cmnd_[size_t(best)]) best = tau;
    }
    // octave-jump guard: stay on the previous period if it is almost as good
    if (rawState_ && prevTau_ > 0.0) {
        const double r = best / prevTau_;
        if (r < 0.71 || r > 1.41) {
            const int lo = std::max(tauMin_, int(prevTau_ * 0.85)), hi = std::min(tauMax_ - 1, int(prevTau_ * 1.18) + 1);
            int alt = -1;
            for (int tau = lo; tau <= hi; ++tau)
                if (alt < 0 || cmnd_[size_t(tau)] < cmnd_[size_t(alt)]) alt = tau;
            if (alt > 0 && cmnd_[size_t(alt)] < cmnd_[size_t(best)] + 0.12) best = alt;
        }
    }
    double tauF = best;
    if (best > 1 && best < tauMax_) {
        const double a = cmnd_[size_t(best - 1)], b = cmnd_[size_t(best)], cc = cmnd_[size_t(best + 1)];
        const double den = a - 2.0 * b + cc;
        if (std::abs(den) > 1e-12) tauF += std::clamp(0.5 * (a - cc) / den, -0.5, 0.5);
    }
    const double ap = cmnd_[size_t(best)];
    const double f0 = fsd_ / tauF;

    double e = 0.0;
    for (int j = -W / 2; j < W / 2; ++j) e += x(c + j) * x(c + j);
    const double rms = std::sqrt(e / W);
    peakRms_ = std::max(rms, peakRms_ * 0.997);
    const bool loud = rms > std::max(3e-4, peakRms_ * 0.01);
    const bool inRange = f0 >= kFmin && f0 <= kFmax;
    const bool raw = loud && inRange && ap < (rawState_ ? kApOff : kApOn);
    rawState_ = raw;
    prevTau_ = raw ? tauF : 0.0;

    Frame& f = frames_[size_t(i) & kFrameMask];
    f.f0 = float(f0);
    f.ap = float(ap);
    f.rms = float(rms);
    f.raw = raw;
    lastF0_.store(raw ? f0 : 0.0, std::memory_order_relaxed);
#ifdef PSOLA_DEBUG
    std::fprintf(stderr, "F i=%lld t=%lld f0=%.1f ap=%.3f rms=%.5f peak=%.5f raw=%d best=%d\n", (long long)i,
                 (long long)(i * int64_t(H_)), f0, ap, rms, peakRms_, int(raw), best);
#endif
    // the speaker model learns only from clear, loud voiced frames (not from
    // hum or murmur in the pauses)
    if (raw && ap < kLearnAp && rms > 0.15 * peakRms_) {
        updateSpeaker(f0);
        if ((i & 1) == 0) {  // vocal tract: every 10 ms is plenty
            const int N = int(lpcBuf_.size());
            for (int j = 0; j < N; ++j) lpcBuf_[size_t(j)] = dec_[size_t(c - N / 2 + j) & decMask_];
            const double cm = tract_.measure(lpcBuf_.data(), N);
            if (cm > 0.0) tract_.learn(cm);
        }
    }
}

// Speaker model: a slowly forgetting histogram of the pitch of clear voiced
// frames (quarter-semitone bins, ~30 s of speech memory). Its median is the
// speaker's typical pitch: robust to octave errors, intonation peaks and hum.
void PsolaVoice::updateSpeaker(double f0) {
    const double pos = (std::log2(f0) - kHistBase) * kBinsPerOct;
    const int bin = std::clamp(int(pos), 0, kHistBins - 1);
    const double keep = 1.0 - 1.0 / 6000.0;
    for (double& h : hist_) h *= keep;
    histTotal_ = histTotal_ * keep + 1.0;
    hist_[size_t(bin)] += 1.0;
    conf_ = std::min(conf_ + 1.0, 1e7);
    if (histTotal_ < 20.0) return;  // not enough evidence yet
    double run = 0.0, half = 0.5 * histTotal_;
    int k = 0;
    for (; k < kHistBins; ++k) {
        if (run + hist_[size_t(k)] >= half) break;
        run += hist_[size_t(k)];
    }
    k = std::min(k, kHistBins - 1);
    const double frac = hist_[size_t(k)] > 0.0 ? (half - run) / hist_[size_t(k)] : 0.5;
    const double median = kHistBase + (double(k) + frac) / kBinsPerOct;
    // follow quickly while learning, then at most ~1 semitone per second
    const double maxStep = conf_ < 400.0 ? 1.0 : (1.0 / 12.0) / 200.0;
    spk_ += std::clamp(median - spk_, -maxStep, maxStep);
    speakerHz_.store(std::exp2(spk_), std::memory_order_relaxed);
}

bool PsolaVoice::rawVoiced(int64_t i) const {
    return i >= 0 && i < framesReady_ && i > framesReady_ - int64_t(kFrameMask) && frames_[size_t(i) & kFrameMask].raw;
}

// Voicing with look-ahead where available: short gaps inside voiced speech are
// bridged (a 5–15 ms drop-out would otherwise flip the pitch back to the
// original for a moment), voicing starts one frame early.
bool PsolaVoice::voicedAt(int64_t i) const {
    if (rawVoiced(i)) return true;
    if (rawVoiced(i + 1) && (i + 2 >= framesReady_ || rawVoiced(i + 2))) return true;
    if (!(rawVoiced(i - 1) || rawVoiced(i - 2) || rawVoiced(i - 3))) return false;
    for (int64_t j = i + 1; j <= i + 3; ++j) {
        if (j >= framesReady_) return true;
        if (rawVoiced(j)) return true;
    }
    return false;
}

double PsolaVoice::periodNear(int64_t i, double fallback) const {
    const double fs = double(sr_);
    for (int d = 0; d <= 3; ++d) {
        if (rawVoiced(i - d)) return fs / frames_[size_t(i - d) & kFrameMask].f0;
        if (d && rawVoiced(i + d)) return fs / frames_[size_t(i + d) & kFrameMask].f0;
    }
    return fallback;
}

double PsolaVoice::refine(double prev, double cand, double T) const {
    // normalised cross-correlation of the period around the previous mark with
    // the candidate region; coarse (step 2) then fine (step 1) search
    const int h = std::max(8, int(T * 0.5));
    const int r = std::max(2, int(T * 0.2));
    const int64_t p = int64_t(std::lround(prev));
    const int64_t c = int64_t(std::lround(cand));
    double eRef = 0.0;
    for (int j = -h; j < h; ++j) eRef += anaAt(p + j) * anaAt(p + j);
    auto score = [&](int o) {
        double cr = 0.0, e = 0.0;
        for (int j = -h; j < h; ++j) {
            const double a = anaAt(c + o + j);
            cr += a * anaAt(p + j);
            e += a * a;
        }
        const double nr = cr / std::sqrt(e * eRef + 1e-20);
        const double z = double(o) / r;
        return nr - 0.15 * z * z;  // mild preference for the predicted position
    };
    int bestO = 0;
    double bestS = -1e9;
    for (int o = -r; o <= r; o += 2) {
        const double s = score(o);
        if (s > bestS) { bestS = s; bestO = o; }
    }
    const int c0 = bestO;
    double sm = 0.0, sp = 0.0;
    for (int o = c0 - 1; o <= c0 + 1; o += 2) {
        if (o < -r || o > r) continue;
        const double s = score(o);
        if (s > bestS) { bestS = s; bestO = o; }
    }
    if (bestO > -r && bestO < r) {
        sm = score(bestO - 1);
        sp = score(bestO + 1);
        const double den = sm - 2.0 * bestS + sp;
        const double off = std::abs(den) > 1e-12 ? std::clamp(0.5 * (sm - sp) / den, -0.5, 0.5) : 0.0;
        return double(c + bestO) + off;
    }
    return double(c + bestO);
}

double PsolaVoice::onsetPeak(double from, double len) const {
    // first mark of a voiced run: the strongest (smoothed) energy in the next period
    const int64_t a = int64_t(std::ceil(from)), b = int64_t(std::floor(from + len));
    const int w = std::max(2, int(sr_ * 0.0007));
    double run = 0.0;
    for (int64_t k = a - w; k <= a + w; ++k) run += anaAt(k) * anaAt(k);
    double best = run;
    int64_t bi = a;
    for (int64_t k = a + 1; k <= b; ++k) {
        run += anaAt(k + w) * anaAt(k + w) - anaAt(k - w - 1) * anaAt(k - w - 1);
        if (run > best) { best = run; bi = k; }
    }
    return double(bi);
}

bool PsolaVoice::extendOne(int64_t availAna) {
    if (markCount_ - int64_t(std::floor(phi_)) >= int64_t(kMarkMask) - 4) return false;
    const Mark last = mark(markCount_ - 1);
    const double guess = last.pos + (last.voiced ? last.T : double(U_));
    if (framesReady_ == 0) {
        mark(markCount_++) = Mark{last.pos + U_, double(U_), false};
        return true;
    }
    int64_t fi = int64_t(std::lround(guess / double(H_)));
    fi = std::clamp<int64_t>(fi, std::max<int64_t>(0, framesReady_ - 48), framesReady_ - 1);
    if (!voicedAt(fi)) {
        mark(markCount_++) = Mark{last.pos + U_, double(U_), false};
        return true;
    }
    const double margin = kTaps + 2;
    if (last.voiced) {
        const double Test = periodNear(fi, last.T);
        const double T = std::clamp(Test, 0.8 * last.T, 1.25 * last.T);
        const double cand = last.pos + T;
        if (cand + 0.7 * T + margin >= double(availAna)) return false;
        const double pos = refine(last.pos, cand, T);
        mark(markCount_++) = Mark{pos, pos - last.pos, true};
    } else {
        const double Test = periodNear(fi, double(sr_) / 150.0);
        const double from = last.pos + 0.5 * double(U_);
        if (from + Test + sr_ * 0.001 + margin >= double(availAna)) return false;
        mark(markCount_++) = Mark{onsetPeak(from, Test), Test, true};
    }
    return true;
}

double PsolaVoice::beta(double period) const {
    double b = p_.pitchRatio * autoRatio();
    if (p_.targetF0 > 0.0 && p_.intonation != 1.0 && period > 0.0) {
        // stretch the melody around the speaker's typical pitch
        const double dev = std::log2(double(sr_) / period) - spk_;
        b *= std::exp2(std::clamp(dev * (p_.intonation - 1.0), -5.0 / 12.0, 5.0 / 12.0));
    }
    if (p_.vibratoDepth > 0.0) b *= std::exp2(p_.vibratoDepth * std::sin(vibPhase_) / 12.0);
    return std::clamp(b, 0.3, 3.5);
}

// ------------------------------------------------------------------ synthesis
// Synthesis is driven by a fractional analysis-mark index phi: every grain
// advances phi by 1/beta in voiced speech (by 1 in unvoiced) and sits at the
// time position of phi on the mark sequence. So the time axis is preserved
// exactly, an unchanged pitch reproduces the input sample for sample, and a
// voiced run always starts on its first glottal pulse.
double PsolaVoice::positionOf(double phi) const {
    const int64_t k = int64_t(std::floor(phi));
    const double f = phi - double(k);
    const int64_t last = markCount_ - 1;
    if (k + 1 <= last) return mark(k).pos + f * (mark(k + 1).pos - mark(k).pos);
    const Mark& m = mark(last);
    return m.pos + (phi - double(last)) * (m.voiced ? m.T : double(U_));
}

void PsolaVoice::synthesise(int64_t emitStart, int64_t emitEnd) {
    const int64_t availIn = n_;
    const int64_t availAna = n_ + int64_t(ahead_);
    const double alpha = effectiveAlpha();
    if (std::abs(alpha - sincAlpha_) > 0.003) buildSinc(alpha);
    const double margin = double(G_) + 64.0;
    const double guard = kTaps;
    for (int loop = 0; loop < 4096; ++loop) {
        if (sPrev_ >= double(emitEnd) + margin) break;
        int64_t k = int64_t(std::floor(phi_));
        while (markCount_ <= k + 2)
            if (!extendOne(availAna)) break;
        k = std::min<int64_t>(k, markCount_ - 1);
        const double f0 = std::clamp(phi_ - double(k), 0.0, 1.0);
        const Mark& a = mark(k);
        const bool haveB = k + 1 < markCount_;
        const Mark* b = haveB ? &mark(k + 1) : nullptr;
        const bool voiced = a.voiced;
        const bool pair = voiced && haveB && b->voiced;
        const double Tin = pair ? b->pos - a.pos : a.T;

        double dphi, gain = 1.0;
        if (voiced) {
            const double bt = beta(Tin);
            dphi = 1.0 / bt;
            gain = bt < 1.0 ? std::min(2.0, std::sqrt(1.0 / bt)) : 1.0;
        } else {
            // step onto the next mark exactly when voicing begins there
            dphi = (haveB && b->voiced) ? std::max(1e-3, 1.0 - f0) : 1.0;
        }
        const double phiNext = phi_ + dphi;
        while (markCount_ <= int64_t(std::floor(phiNext)) + 1)
            if (!extendOne(availAna)) break;
        const double sNext = std::max(positionOf(phiNext), sCur_ + 4.0);

        double TsL = std::max(4.0, sCur_ - sPrev_), TsR = sNext - sCur_;
        double f = pair ? f0 : 0.0;
        if (voiced) {
            // a grain never reaches further than one input period on either side
            TsL = std::min(TsL, Tin / alpha);
            TsR = std::min(TsR, Tin / alpha);
            if (f > 0.0 && b->pos + alpha * TsR + guard >= double(availIn)) f = 0.0;   // data not there yet
            const double room = (double(availIn) - guard - a.pos) / alpha;
            if (TsR > room) TsR = std::max(4.0, room);
            addGrain(sCur_, TsL, TsR, alpha, a.pos, haveB ? b->pos : a.pos, f, true, gain, emitStart);
        } else {
            addGrain(sCur_, TsL, TsR, alpha, sCur_, sCur_, 0.0, false, 1.0, emitStart);
        }
#ifdef PSOLA_DEBUG
        std::fprintf(stderr, "G s=%.1f v=%d k=%lld a=%.1f b=%.1f f=%.3f P=%.1f TsL=%.1f TsR=%.1f\n", sCur_, int(voiced),
                     (long long)k, a.pos, haveB ? b->pos : -1.0, f, sNext - sCur_, TsL, TsR);
#endif
        if (p_.vibratoDepth > 0.0)
            vibPhase_ = std::fmod(vibPhase_ + 2.0 * kPi * p_.vibratoRate * (sNext - sCur_) / double(sr_), 2.0 * kPi);
        sPrev_ = sCur_;
        sCur_ = sNext;
        phi_ = phiNext;
    }
}

void PsolaVoice::addGrain(double s, double TsL, double TsR, double alpha, double m0, double m1, double f,
                          bool voiced, double gain, int64_t emitStart) {
    const int64_t j0 = std::max<int64_t>(int64_t(std::ceil(s - TsL)), emitStart);
    const int64_t j1 = int64_t(std::floor(s + TsR));
    const double invL = kPi / TsL, invR = kPi / TsR;
    // Neighbouring noise grains read with a formant warp are uncorrelated where
    // they overlap: they need power-complementary (sqrt-Hann) cross-fades or the
    // noise would dip 3 dB between grains. Voiced grains are pitch-synchronous
    // (correlated) and use plain Hann. wsum always collects the Hann weight.
    const bool powerFade = !voiced && std::abs(alpha - 1.0) > 0.01;
    for (int64_t j = j0; j <= j1; ++j) {
        const double tau = double(j) - s;
        const double h = 0.5 + 0.5 * std::cos(tau * (tau < 0.0 ? invL : invR));
        if (h <= 0.0) continue;
        const double w = powerFade ? std::sqrt(h) : h;
        double v;
        if (f <= 0.0) v = sampleAt(m0 + alpha * tau);
        else if (f >= 1.0) v = sampleAt(m1 + alpha * tau);
        else v = (1.0 - f) * sampleAt(m0 + alpha * tau) + f * sampleAt(m1 + alpha * tau);
        const size_t o = size_t(j) & outMask_;
        acc_[o] += gain * w * v;
        wsum_[o] += h;
        if (voiced) vsum_[o] += h;
    }
}

}  // namespace vox
