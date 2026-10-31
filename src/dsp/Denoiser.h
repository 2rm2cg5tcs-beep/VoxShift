// Short-frame spectral noise suppression (minimum-statistics noise floor,
// spectral subtraction with smoothed gains). 512-point frames at 48 kHz:
// ~11 ms latency, enough resolution to remove fans, hum and hiss before the
// pitch-synchronous voice core. With amount = 0 it is an exact delay.
#pragma once

#include "Fft.h"

#include <complex>
#include <vector>

namespace vox {

class Denoiser {
public:
    explicit Denoiser(int sampleRate);

    void reset();
    void setAmount(double a) { amount_ = a; }  // 0..1
    void process(float* data, size_t count);   // in place, delayed by latency()
    int latency() const { return int(n_); }

private:
    void frame();

    int sr_;
    size_t n_, hop_, nb_;
    double olaGain_, aPsd_, aNoise_, rise_, aRel_;
    RealFft fft_;
    std::vector<double> window_, inFifo_, outAcc_, outReady_, frame_;
    std::vector<double> noise_, psd_, prevGain_, tmp_;
    std::vector<std::complex<double>> spec_;
    size_t rover_ = 0;
    bool noiseValid_ = false;
    double amount_ = 0.0;
};

}  // namespace vox
