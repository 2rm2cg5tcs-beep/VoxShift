// Apparent vocal-tract length from formant dispersion.
//
// For a tube closed at the glottis and open at the lips the resonances are
// spaced evenly: F_i = (2i - 1) · c / (4 · L). The first four formants of a
// voiced frame (LPC envelope peaks) give the spacing ΔF = c / (2L) by least
// squares, and L = c / (2 ΔF). Averaged over many vowels this is a robust
// measure of the speaker's body/throat size — the cue (besides pitch) that
// makes a voice sound male, female or like a child: adult men ≈ 16.5 cm,
// women ≈ 14.5 cm, children ≈ 12 cm on this meter.
//
// The meter keeps a slowly forgetting histogram of the per-frame estimates;
// its median is the speaker's value. No allocation after construction.
#pragma once

#include <atomic>
#include <vector>

namespace vox {

class TractMeter {
public:
    explicit TractMeter(double sampleRate);  // rate of the frames passed to measure()

    // A voiced frame (about 25 ms). Returns the length in cm, or 0 when the
    // formants are not clear enough.
    double measure(const float* frame, int n);
    void learn(double cm);

    double lengthCm() const { return cm_.load(std::memory_order_relaxed); }
    double confidence() const { return conf_.load(std::memory_order_relaxed); }
    void set(double cm, double confidence);

private:
    static constexpr int kOrder = 14, kGrid = 512, kBins = 300;
    static constexpr double kBase = 9.0, kStep = 0.05;  // histogram: 9..24 cm

    double fs_;
    std::vector<double> buf_, cosT_, sinT_, env_;
    std::vector<double> hist_;
    double total_ = 0.0, est_ = 0.0, count_ = 0.0;
    std::atomic<double> cm_{0.0}, conf_{0.0};
};

}  // namespace vox
