#include "SpectralVoice.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kRobotHz = 94.0;

inline double wrapPi(double x) {  // → [-π, π)
    return x - kTwoPi * std::floor((x + kPi) / kTwoPi);
}
inline double finiteOr0(double x) { return std::isfinite(x) ? x : 0.0; }
}  // namespace

double semitonesToRatio(double st) { return std::pow(2.0, st / 12.0); }

static void frameConfig(int sr, Quality q, size_t& n, size_t& osamp) {
    // The frame must resolve the harmonics of a deep male voice (~90 Hz):
    // a Hann main lobe is 4 bins wide, so N >= ~3·sr/f0.
    size_t base = (q == Quality::High) ? 2048 : 1536;
    osamp = (q == Quality::High) ? 8 : 4;
    double scaled = double(base) * double(sr) / 48000.0;
    n = std::max<size_t>(256, size_t(std::lround(scaled / 64.0)) * 64);
}

SpectralVoice::SpectralVoice(int sampleRate, Quality quality, uint32_t seed)
    : sr_(sampleRate), quality_(quality), n_(0), hop_(0), osamp_(0), nb_(0),
      fft_((frameConfig(sampleRate, quality, n_, osamp_), n_)), rng_(seed) {
    hop_ = n_ / osamp_;
    nb_ = n_ / 2 + 1;
    olaGain_ = 1.0 / (double(osamp_) * 0.375);
    incoherentComp_ = 1.0 / (olaGain_ * std::sqrt(double(osamp_) * 35.0 / 128.0));
    expct_ = kTwoPi * double(hop_) / double(n_);

    window_.resize(n_);
    for (size_t i = 0; i < n_; ++i) window_[i] = 0.5 - 0.5 * std::cos(kTwoPi * double(i) / double(n_));

    const size_t cut = std::max<size_t>(12, size_t(sr_ * 0.0014));
    lifter_.assign(n_, 0.0);
    lifter_[0] = 1.0;
    for (size_t i = 1; i <= cut; ++i) {
        double t = 0.5 + 0.5 * std::cos(kPi * double(i) / double(cut + 1));
        lifter_[i] = t;
        lifter_[n_ - i] = t;
    }

    robotPeriod_ = double(sr_) / kRobotHz;
    robotGain_ = 1.0 / std::sqrt(std::max(1.0, robotPeriod_ / double(hop_))) * 1.25;
    robotGain_ *= (quality_ == Quality::High) ? 1.7 : 1.15;

    auto sz = [&](std::vector<double>& v, size_t s) { v.assign(s, 0.0); };
    sz(frame_, n_); sz(logMag_, n_); sz(cep_, n_);
    for (auto* v : {&mag_, &phase_, &trueBin_, &gain_, &env_, &envNew_, &exc_, &synExc_, &synPhase_,
                    &synMag_, &bestMag_, &tmp_})
        sz(*v, nb_);
    spec_.assign(nb_, {});
    out_.assign(nb_, {});
    cspec_.assign(nb_, {});
    peaks_.reserve(nb_);
    fPeak_.reserve(nb_);
    rotPeak_.reserve(nb_);
    shift_.reserve(nb_);
    region_.assign(nb_, 0);
    reset();
}

void SpectralVoice::reset() {
    inFifo_.assign(n_, 0.0);
    outAcc_.assign(n_, 0.0);
    outReady_.assign(hop_, 0.0);
    rover_ = 0;
    frameStart_ = 0;
    lastPhase_.assign(nb_, 0.0);
    rot_.assign(nb_, 0.0);
    rotNew_.assign(nb_, 0.0);
    noise_.assign(nb_, 0.0);
    psd_.assign(nb_, 0.0);
    prevGain_.assign(nb_, 1.0);
    noiseValid_ = false;
    vibPhase_ = 0.0;
}

void SpectralVoice::process(float* data, size_t count) {
    const size_t base = n_ - hop_;
    size_t pos = 0;
    while (pos < count) {
        const size_t r = rover_;
        const size_t n = std::min(hop_ - r, count - pos);
        for (size_t i = 0; i < n; ++i) {
            inFifo_[base + r + i] = double(data[pos + i]);
            data[pos + i] = float(outReady_[r + i]);
        }
        rover_ = r + n;
        pos += n;
        if (rover_ == hop_) {
            rover_ = 0;
            processFrame();
        }
    }
}

void SpectralVoice::processFrame() {
    const CoreParams& p = params_;
    for (size_t i = 0; i < n_; ++i) frame_[i] = inFifo_[i] * window_[i];
    fft_.forward(frame_.data(), spec_.data());

    for (size_t k = 0; k < nb_; ++k) {
        mag_[k] = std::abs(spec_[k]);
        phase_[k] = std::arg(spec_[k]);
        double dphi = wrapPi(phase_[k] - lastPhase_[k] - double(k) * expct_);
        lastPhase_[k] = phase_[k];
        trueBin_[k] = double(k) + dphi / expct_;
    }

    if (p.denoise > 0.0) {
        denoiseGain(p.denoise);
        for (size_t k = 0; k < nb_; ++k) {
            mag_[k] *= gain_[k];
            spec_[k] *= gain_[k];
        }
    } else {
        noiseValid_ = false;
        std::fill(prevGain_.begin(), prevGain_.end(), 1.0);
    }

    if (p.isIdentity()) {
        const double g = 1.0 + std::max(0.0, p.dry);
        for (size_t k = 0; k < nb_; ++k) out_[k] = spec_[k] * g;
        std::fill(rot_.begin(), rot_.end(), 0.0);
    } else {
        double beta = p.pitchRatio;
        if (p.vibratoDepth > 0.0) {
            vibPhase_ = std::fmod(vibPhase_ + kTwoPi * p.vibratoRate * double(hop_) / double(sr_), kTwoPi);
            beta *= std::pow(2.0, p.vibratoDepth * std::sin(vibPhase_) / 12.0);
        }
        transform(beta);
    }

    fft_.inverse(out_.data(), frame_.data());
    for (size_t i = 0; i < n_; ++i) outAcc_[i] += frame_[i] * window_[i] * olaGain_;
    std::copy(outAcc_.begin(), outAcc_.begin() + hop_, outReady_.begin());
    std::copy(outAcc_.begin() + hop_, outAcc_.end(), outAcc_.begin());
    std::fill(outAcc_.end() - hop_, outAcc_.end(), 0.0);
    std::copy(inFifo_.begin() + hop_, inFifo_.end(), inFifo_.begin());
    frameStart_ += int64_t(hop_);
}

void SpectralVoice::denoiseGain(double amount) {
    const double fps = double(sr_) / double(hop_);
    if (!noiseValid_) {
        for (size_t k = 0; k < nb_; ++k) {
            psd_[k] = mag_[k] * mag_[k];
            noise_[k] = psd_[k] + 1e-18;
        }
        noiseValid_ = true;
    } else {
        const double rise = std::pow(10.0, 3.0 / 10.0 / fps);  // +3 dB/s upward drift
        for (size_t k = 0; k < nb_; ++k) psd_[k] = 0.7 * psd_[k] + 0.3 * mag_[k] * mag_[k];
        for (size_t k = 0; k < nb_; ++k) {
            double s = (k == 0 || k == nb_ - 1) ? psd_[k]
                                                 : 0.25 * psd_[k - 1] + 0.5 * psd_[k] + 0.25 * psd_[k + 1];
            double nz = noise_[k];
            nz = (s < nz) ? 0.5 * nz + 0.5 * s : nz * rise;
            noise_[k] = std::max(nz, 1e-18);
        }
    }
    const double over = 1.0 + 2.2 * amount;
    const double floorG = std::pow(10.0, -(6.0 + 20.0 * amount) / 20.0);
    for (size_t k = 0; k < nb_; ++k) {
        double nm = std::sqrt(noise_[k] * 2.5);
        tmp_[k] = std::clamp(1.0 - over * nm / (mag_[k] + 1e-9), floorG, 1.0);
    }
    for (size_t k = 0; k < nb_; ++k) {
        double g = (k == 0 || k == nb_ - 1) ? tmp_[k] : 0.25 * tmp_[k - 1] + 0.5 * tmp_[k] + 0.25 * tmp_[k + 1];
        double prev = prevGain_[k];
        g = (g > prev) ? g : 0.6 * prev + 0.4 * g;  // fast attack, slow release
        prevGain_[k] = g;
        gain_[k] = g;
    }
}

void SpectralVoice::envelope() {
    for (size_t k = 0; k < nb_; ++k) cspec_[k] = {std::log(mag_[k] + 1e-9), 0.0};
    fft_.inverse(cspec_.data(), cep_.data());
    for (size_t i = 0; i < n_; ++i) cep_[i] *= lifter_[i];
    fft_.forward(cep_.data(), cspec_.data());
    for (size_t k = 0; k < nb_; ++k) env_[k] = std::exp(cspec_[k].real());
}

void SpectralVoice::transform(double beta) {
    const CoreParams& p = params_;
    envelope();
    for (size_t k = 0; k < nb_; ++k) exc_[k] = mag_[k] / env_[k];

    const double alpha = p.formantRatio;
    if (alpha != 1.0) {
        const double last = double(nb_ - 1);
        for (size_t k = 0; k < nb_; ++k) {
            double x = double(k) / alpha;
            if (x >= last) {
                envNew_[k] = env_[nb_ - 1];
            } else {
                size_t i0 = size_t(x);
                double f = x - double(i0);
                envNew_[k] = env_[i0] * (1.0 - f) + env_[i0 + 1] * f;
            }
        }
    } else {
        envNew_ = env_;
    }

    if (beta == 1.0) {
        synExc_ = exc_;
        synPhase_ = phase_;
        std::fill(rot_.begin(), rot_.end(), 0.0);
    } else {
        shiftPeaks(beta);
    }

    const double boost = beta > 1.0 ? std::sqrt(std::min(beta, 2.5)) : 1.0;
    for (size_t k = 0; k < nb_; ++k) {
        synMag_[k] = synExc_[k] * envNew_[k] * boost;
        out_[k] = std::polar(synMag_[k], synPhase_[k]);
    }

    if (p.robot > 0.0) {
        const double tPer = robotPeriod_;
        const double centre = double(frameStart_) + double(n_) / 2.0;
        const double pulse = std::nearbyint(centre / tPer) * tPer - double(frameStart_);
        const double w = p.robot, keep = 1.0 - p.robot;
        for (size_t k = 0; k < nb_; ++k) {
            std::complex<double> r = std::polar(synMag_[k] * robotGain_, -kTwoPi * double(k) * (pulse / double(n_)));
            out_[k] = keep * out_[k] + w * r;
        }
    }

    if (p.whisper > 0.0) {
        double eRef = 0.0, eWh = 1e-18;
        for (size_t k = 0; k < nb_; ++k) {
            eRef += synMag_[k] * synMag_[k];
            eWh += envNew_[k] * envNew_[k];
        }
        const double scale = std::sqrt(eRef / eWh) * incoherentComp_ * 1.35;
        std::uniform_real_distribution<double> uni(-kPi, kPi);
        const double w = p.whisper, keep = 1.0 - p.whisper;
        for (size_t k = 0; k < nb_; ++k) {
            std::complex<double> wh = std::polar(envNew_[k] * scale, uni(rng_));
            out_[k] = keep * out_[k] + w * wh;
        }
    }

    if (p.dry > 0.0)
        for (size_t k = 0; k < nb_; ++k) out_[k] += p.dry * spec_[k];
}

void SpectralVoice::shiftPeaks(double beta) {
    const int nb = int(nb_);
    double mmax = 0.0;
    for (int k = 0; k < nb; ++k) mmax = std::max(mmax, mag_[k]);
    const double thr = mmax * 1e-5;

    peaks_.clear();
    for (int k = 1; k < nb - 1; ++k)
        if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > thr) peaks_.push_back(k);
    if (peaks_.empty()) peaks_.push_back(int(std::max_element(mag_.begin(), mag_.end()) - mag_.begin()));

    // region of each bin = index of nearest peak (boundary = midpoint, ties go right)
    {
        size_t pi = 0;
        for (int k = 0; k < nb; ++k) {
            while (pi + 1 < peaks_.size() && k >= (peaks_[pi] + peaks_[pi + 1]) / 2) ++pi;
            region_[k] = int(pi);
        }
    }

    const size_t np = peaks_.size();
    std::vector<double>& fPeak = fPeak_;
    std::vector<double>& rotPeak = rotPeak_;
    std::vector<int>& shift = shift_;
    fPeak.resize(np); rotPeak.resize(np); shift.resize(np);   // within reserved capacity: no allocation
    for (size_t i = 0; i < np; ++i) {
        const int pk = peaks_[i];
        fPeak[i] = finiteOr0(trueBin_[pk]);
        int centre = int(std::nearbyint(fPeak[i] * beta));
        shift[i] = centre - pk;
        int cc = std::clamp(centre, 0, nb - 1);
        rotPeak[i] = wrapPi(rot_[cc] + (beta - 1.0) * fPeak[i] * expct_);
    }

    std::fill(synExc_.begin(), synExc_.end(), 0.0);
    std::fill(synPhase_.begin(), synPhase_.end(), 0.0);
    std::fill(rotNew_.begin(), rotNew_.end(), 0.0);
    std::fill(bestMag_.begin(), bestMag_.end(), -1.0);

    for (int k = 0; k < nb; ++k) {
        const int reg = region_[k];
        const double tb = finiteOr0(trueBin_[k]);
        // a bin whose own frequency disagrees with its peak belongs to a merged
        // neighbouring harmonic: it follows its own frequency
        const bool stray = std::abs(tb - fPeak[reg]) > 1.0;
        const int binShift = stray ? int(std::nearbyint(tb * (beta - 1.0))) : shift[reg];
        const int tgt = k + binShift;
        if (tgt < 0 || tgt >= nb) continue;
        if (mag_[k] <= bestMag_[tgt]) continue;   // collisions (beta < 1): strongest wins
        bestMag_[tgt] = mag_[k];
        const double r = stray ? rot_[tgt] + (beta - 1.0) * tb * expct_ : rotPeak[reg];
        synExc_[tgt] = exc_[k];
        synPhase_[tgt] = phase_[k] + r;
        rotNew_[tgt] = wrapPi(r);
    }
    std::swap(rot_, rotNew_);
}

}  // namespace vox
