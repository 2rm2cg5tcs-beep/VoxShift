#include "Denoiser.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
size_t frameSize(int sr) {
    const double scaled = 512.0 * double(sr) / 48000.0;
    return std::max<size_t>(128, size_t(std::lround(scaled / 32.0)) * 32);
}
}  // namespace

Denoiser::Denoiser(int sampleRate) : sr_(sampleRate), n_(frameSize(sampleRate)), fft_(n_) {
    hop_ = n_ / 4;
    nb_ = n_ / 2 + 1;
    olaGain_ = 1.0 / (4.0 * 0.375);
    window_.resize(n_);
    for (size_t i = 0; i < n_; ++i) window_[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(n_));
    // smoothing constants defined in time, so they do not depend on the frame rate
    const double fps = double(sr_) / double(hop_);
    aPsd_ = std::exp(-1.0 / (0.025 * fps));     // power spectrum: 25 ms
    aNoise_ = std::exp(-1.0 / (0.012 * fps));   // noise floor follows dips quickly
    rise_ = std::pow(10.0, 3.0 / 10.0 / fps);   // ... and creeps up at +3 dB/s
    aRel_ = std::exp(-1.0 / (0.020 * fps));     // gain release 20 ms (attack instant)
    frame_.assign(n_, 0.0);
    spec_.assign(nb_, {});
    reset();
}

void Denoiser::reset() {
    inFifo_.assign(n_, 0.0);
    outAcc_.assign(n_, 0.0);
    outReady_.assign(hop_, 0.0);
    noise_.assign(nb_, 0.0);
    psd_.assign(nb_, 0.0);
    prevGain_.assign(nb_, 1.0);
    tmp_.assign(nb_, 1.0);
    rover_ = 0;
    noiseValid_ = false;
}

void Denoiser::process(float* data, size_t count) {
    const size_t base = n_ - hop_;
    for (size_t i = 0; i < count; ++i) {
        inFifo_[base + rover_] = double(data[i]);
        data[i] = float(outReady_[rover_]);
        if (++rover_ == hop_) {
            rover_ = 0;
            frame();
        }
    }
}

void Denoiser::frame() {
    const double amount = amount_;
    for (size_t i = 0; i < n_; ++i) frame_[i] = inFifo_[i] * window_[i];
    fft_.forward(frame_.data(), spec_.data());

    if (amount > 0.0) {
        if (!noiseValid_) {
            for (size_t k = 0; k < nb_; ++k) {
                psd_[k] = std::norm(spec_[k]);
                noise_[k] = psd_[k] + 1e-18;
            }
            noiseValid_ = true;
        } else {
            for (size_t k = 0; k < nb_; ++k) psd_[k] = aPsd_ * psd_[k] + (1.0 - aPsd_) * std::norm(spec_[k]);
            for (size_t k = 0; k < nb_; ++k) {
                const double s = (k == 0 || k == nb_ - 1) ? psd_[k]
                                                         : 0.25 * psd_[k - 1] + 0.5 * psd_[k] + 0.25 * psd_[k + 1];
                double nz = noise_[k];
                nz = (s < nz) ? aNoise_ * nz + (1.0 - aNoise_) * s : nz * rise_;
                noise_[k] = std::max(nz, 1e-18);
            }
        }
        const double over = 1.0 + 2.2 * amount;
        const double floorG = std::pow(10.0, -(6.0 + 20.0 * amount) / 20.0);
        for (size_t k = 0; k < nb_; ++k) {
            const double nm = std::sqrt(noise_[k] * 2.5);
            tmp_[k] = std::clamp(1.0 - over * nm / (std::abs(spec_[k]) + 1e-9), floorG, 1.0);
        }
        for (size_t k = 0; k < nb_; ++k) {
            double g = (k == 0 || k == nb_ - 1) ? tmp_[k] : 0.25 * tmp_[k - 1] + 0.5 * tmp_[k] + 0.25 * tmp_[k + 1];
            const double prev = prevGain_[k];
            g = (g > prev) ? g : aRel_ * prev + (1.0 - aRel_) * g;  // instant attack, smooth release
            prevGain_[k] = g;
            spec_[k] *= g;
        }
    } else {
        // transparent (Hann² overlap-add reconstructs exactly); the noise
        // estimate restarts when it is switched on again
        noiseValid_ = false;
        std::fill(prevGain_.begin(), prevGain_.end(), 1.0);
    }

    fft_.inverse(spec_.data(), frame_.data());
    for (size_t i = 0; i < n_; ++i) outAcc_[i] += frame_[i] * window_[i] * olaGain_;
    std::copy(outAcc_.begin(), outAcc_.begin() + hop_, outReady_.begin());
    std::copy(outAcc_.begin() + hop_, outAcc_.end(), outAcc_.begin());
    std::fill(outAcc_.end() - hop_, outAcc_.end(), 0.0);
    std::copy(inFifo_.begin() + hop_, inFifo_.end(), inFifo_.begin());
}

}  // namespace vox
