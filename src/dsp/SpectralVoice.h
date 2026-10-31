// Streaming spectral voice core: pitch and formant shifted independently.
//
// * periodic-Hann STFT with fixed overlap, constant latency of N samples;
// * envelope (cepstral lifter) / excitation split;
// * pitch: Laroche–Dolson peak-region translation with identity phase locking,
//   bins that belong to a merged neighbouring harmonic follow their own frequency;
// * formants: the envelope is warped independently and re-applied;
// * robot (pulse train on a global grid), whisper (noise excitation), vibrato,
//   minimum-statistics spectral noise suppression.
#pragma once

#include "Fft.h"

#include <complex>
#include <cstdint>
#include <random>
#include <vector>

namespace vox {

enum class Quality { Balanced = 0, High = 1 };

struct CoreParams {
    double pitchRatio = 1.0;
    double formantRatio = 1.0;
    double robot = 0.0;
    double whisper = 0.0;
    double vibratoDepth = 0.0;  // semitones
    double vibratoRate = 5.0;   // Hz
    double dry = 0.0;
    double denoise = 0.0;

    bool isIdentity() const {
        return pitchRatio == 1.0 && formantRatio == 1.0 && robot <= 0.0 && whisper <= 0.0 &&
               vibratoDepth <= 0.0;
    }
};

class SpectralVoice {
public:
    SpectralVoice(int sampleRate, Quality quality, uint32_t seed = 12345);

    void reset();
    void setParams(const CoreParams& p) { params_ = p; }  // audio thread only
    // In-place processing of any number of samples; output is delayed by latency().
    void process(float* data, size_t count);

    int latency() const { return int(n_); }
    int fftSize() const { return int(n_); }
    int hop() const { return int(hop_); }

private:
    void processFrame();
    void denoiseGain(double amount);
    void envelope();
    void transform(double beta);
    void shiftPeaks(double beta);

    int sr_;
    Quality quality_;
    size_t n_, hop_, osamp_, nb_;
    double olaGain_, incoherentComp_, expct_, robotPeriod_, robotGain_;
    RealFft fft_;
    std::vector<double> window_, lifter_;

    // streaming state
    std::vector<double> inFifo_, outAcc_, outReady_;
    size_t rover_ = 0;
    int64_t frameStart_ = 0;
    std::vector<double> lastPhase_, rot_, rotNew_;
    std::vector<double> noise_, psd_, prevGain_;
    bool noiseValid_ = false;
    double vibPhase_ = 0.0;
    std::mt19937 rng_;

    // per-frame scratch (preallocated)
    std::vector<double> frame_, mag_, phase_, trueBin_, gain_, env_, envNew_, exc_, logMag_, cep_;
    std::vector<double> synExc_, synPhase_, synMag_, bestMag_, tmp_;
    std::vector<std::complex<double>> spec_, out_, cspec_;
    std::vector<int> peaks_, region_, shift_;
    std::vector<double> fPeak_, rotPeak_;

    CoreParams params_;
};

double semitonesToRatio(double st);

}  // namespace vox
