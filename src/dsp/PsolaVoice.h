// Natural-sounding voice transform for speech: pitch-synchronous overlap-add
// (TD-PSOLA) with grain resampling for the formants.
//
// * a YIN pitch tracker (on a 12 kHz copy) measures the voice's period every 5 ms;
// * analysis marks are placed once per period and refined by cross-correlation
//   with the previous period, so every grain is cut at the same phase of the
//   glottal cycle;
// * synthesis places grains at the new period. A grain is read from the input
//   at speed `formantRatio`, which moves the formants independently of pitch;
//   neighbouring analysis grains are cross-faded, so no period is repeated
//   verbatim;
// * unvoiced sounds (s, f, sh, breath, noise) use plain overlap-add (only the
//   formant warp applies), so they are never "pitched";
// * auto pitch: the speaker's typical pitch is learned on the fly (running
//   median of the voiced frames) and moved to `targetF0`, so "Девушка" lands at
//   ~210 Hz for a deep and for a high male voice alike;
// * breathiness: part of the high harmonics is replaced by noise that follows
//   the pitch pulses (the aspiration of a lighter, female phonation).
//
// The analysis may run on a copy of the signal that is `analysisAhead` samples
// earlier than the one being transformed (the chain feeds it the raw mic while
// the grains are cut from the denoised signal, which lags by the denoiser's
// latency) — that look-ahead is free.
//
// Constant latency, no allocation in process().
#pragma once

#include "Effects.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace vox {

struct PsolaParams {
    double pitchRatio = 1.0;    // fixed factor on top of the automatic one
    double targetF0 = 0.0;      // Hz; > 0 moves the speaker's typical pitch here
    double formantRatio = 1.0;
    double vibratoDepth = 0.0;  // semitones
    double vibratoRate = 5.0;   // Hz
    double dry = 0.0;           // original voice mixed in
    double breath = 0.0;        // 0..1

    bool isIdentity() const {
        return pitchRatio == 1.0 && targetF0 <= 0.0 && formantRatio == 1.0 && vibratoDepth <= 0.0 &&
               breath <= 0.0;
    }
};

class PsolaVoice {
public:
    // latency < 0: default (1024 samples @ 48 kHz)
    PsolaVoice(int sampleRate, int latency = -1, int analysisAhead = 0);

    void reset();  // clears the signal state, keeps what was learned about the speaker
    void setParams(const PsolaParams& p);  // audio thread
    // In place. `ahead` (may be null when analysisAhead == 0) is the same signal
    // `analysisAhead` samples earlier.
    void process(float* data, size_t count, const float* ahead = nullptr);

    int latency() const { return int(D_); }

    // Typical pitch of the current speaker (Hz) and how many voiced frames it is based on.
    double speakerF0() const { return speakerHz_.load(std::memory_order_relaxed); }
    double speakerConfidence() const { return conf_; }
    void setSpeaker(double hz, double confidence);
    // Pitch of the last analysed frame (0 = not voiced), for meters.
    double currentF0() const { return lastF0_.load(std::memory_order_relaxed); }
    double autoRatio() const;

private:
    struct Frame {
        float f0 = 0.0f, ap = 1.0f, rms = 0.0f;
        bool raw = false;
    };
    struct Mark {
        double pos = 0.0, T = 0.0;
        bool voiced = false;
    };

    void tick();
    void analyseFrame(int64_t i);
    bool rawVoiced(int64_t i) const;
    bool voicedAt(int64_t i) const;
    double periodNear(int64_t i, double fallback) const;
    bool extendOne(int64_t availAna);
    double refine(double prev, double cand, double T) const;
    double onsetPeak(double from, double len) const;
    void synthesise(int64_t emitStart, int64_t emitEnd);
    double positionOf(double phi) const;
    void addGrain(double s, double TsL, double TsR, double alpha, double m0, double m1, double f, bool voiced,
                  double gain, int64_t emitStart);
    double sampleAt(double pos) const;
    double anaAt(int64_t t) const { return ana_[size_t(t) & inMask_]; }
    void buildSinc(double alpha);
    double beta() const;
    void updateSpeaker(double f0);
    void restartSynthesis(int64_t at);
    Mark& mark(int64_t k) { return marks_[size_t(k) & kMarkMask]; }
    const Mark& mark(int64_t k) const { return marks_[size_t(k) & kMarkMask]; }

    static constexpr size_t kMarkMask = 255;
    static constexpr size_t kFrameMask = 63;
    static constexpr int kTaps = 16, kPhases = 512;

    // configuration
    int sr_;
    size_t D_, G_, H_, U_, ahead_;
    int q_;
    double fsd_;
    int Wd_, tauMin_, tauMax_, la_;
    std::vector<double> decFir_;
    int decDelay_;

    // signal history; absolute time t -> ring index t & inMask_
    std::vector<float> in_, ana_;
    size_t inMask_;
    int64_t n_ = 0;  // samples of `data` received
    std::vector<float> dec_;
    size_t decMask_;
    int64_t nd_ = 0;

    // pitch analysis
    std::vector<Frame> frames_;
    int64_t framesReady_ = 0;
    double peakRms_ = 0.0, prevTau_ = 0.0;
    bool rawState_ = false;
    std::vector<double> diff_, cmnd_;

    // marks and synthesis
    std::vector<Mark> marks_;
    int64_t markCount_ = 0;
    double phi_ = 0.0, sCur_ = 0.0, sPrev_ = 0.0, vibPhase_ = 0.0;
    std::vector<double> acc_, wsum_, vsum_;
    size_t outMask_;
    std::vector<float> sinc_;
    double sincAlpha_ = -1.0;

    // G-sample I/O blocks
    std::vector<float> fifoIn_, fifoAna_, fifoOut_;
    size_t rover_ = 0;

    // breath
    Biquad hfVoice_, noiseHp_, noiseLp_;
    double env_ = 0.0, vgate_ = 0.0, envAtt_ = 0.0, envRel_ = 0.0, vgateCoef_ = 0.0;
    uint32_t rng_ = 0x9E3779B9u;

    // speaker model (log2 Hz)
    static constexpr int kHistBins = 200;
    static constexpr double kBinsPerOct = 48.0, kHistBase = 5.6;  // 2^5.6 = 48.5 Hz
    std::vector<double> hist_;
    double histTotal_ = 0.0;
    double spk_ = 0.0, conf_ = 0.0;
    std::atomic<double> speakerHz_{125.0};
    std::atomic<double> lastF0_{0.0};

    PsolaParams p_;
    bool identity_ = true;
};

}  // namespace vox
