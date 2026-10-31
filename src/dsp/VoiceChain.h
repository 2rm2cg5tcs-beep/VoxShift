// Complete voice chain: mic clean-up → voice core → effects → dynamics.
//
//   input gain → rumble HPF → gate → voice core → ring mod → 3-band EQ →
//   HP/LP 24 dB → distortion → bitcrush → 8-bit → chorus → tremolo → echo →
//   reverb → preset level → compressor → master volume → limiter (−1 dBFS)
//
// Voice core: speech presets use Denoiser + PsolaVoice (pitch-synchronous,
// natural); robot / whisper need the spectral SpectralVoice. Both paths have
// the same latency; switching between them is muted for one latency period.
//
// Thread model: setVoice/setGlobal/setEnabled are called from the UI thread;
// process() runs on the audio thread and never blocks (it picks up new
// settings with try_lock at the start of an internal block).
#pragma once

#include "Effects.h"
#include "Denoiser.h"
#include "Params.h"
#include "PsolaVoice.h"
#include "SpectralVoice.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace vox {

class VoiceChain {
public:
    VoiceChain(int sampleRate, Quality quality);

    void setVoice(const VoiceValues& v, bool smooth = false);
    void setGlobal(const GlobalValues& g);
    void setEnabled(bool on);

    // In-place, any length.
    void process(float* data, size_t count);
    void reset();

    int latencySamples() const { return core_.latency(); }

    // What the chain has learned about the speaker's voice (thread-safe reads).
    double speakerF0() const { return psola_.speakerF0(); }
    double speakerConfidence() const { return speakerConf_.load(std::memory_order_relaxed); }
    double currentF0() const { return psola_.currentF0(); }
    // Seeds the speaker model (e.g. from the saved settings); applied on the audio thread.
    void setSpeaker(double hz, double confidence);
    // Which core a voice needs: 0 = none (identity), 1 = pitch-synchronous, 2 = spectral.
    static int coreMode(const VoiceValues& v);
    int sampleRate() const { return sr_; }
    bool sleeping() const { return sleeping_; }

private:
    void processBlock(float* x, size_t n, bool allowApply);
    void runBlock(float* x, size_t n);
    void applyPending();

    int sr_;
    SpectralVoice core_;
    Denoiser denoiser_;
    PsolaVoice psola_;
    std::vector<float> raw_;
    bool usePsola_ = true;
    size_t mute_ = 0;
    std::atomic<double> speakerConf_{0.0};

    // ---- shared with UI thread
    std::mutex mtx_;
    VoiceValues pendingVoice_;
    GlobalValues pendingGlobal_;
    bool pendingEnabled_ = true;
    double pendingSpeakerHz_ = 0.0, pendingSpeakerConf_ = 0.0;
    std::atomic<bool> dirty_{true};
    std::atomic<bool> duckRequest_{false};

    // ---- audio thread state
    VoiceValues v_;
    GlobalValues g_;
    bool enabled_ = true;
    bool fadeIn_ = false;
    bool sleeping_ = false;
    size_t silent_ = 0, sleepAfter_ = 0;
    double wakeLevel_ = 0.0, inTarget_ = 1.0;

    struct Active {
        bool gate, eqLow, eqMid, eqHigh, hp, lp, dist, crush, lofi, chorus, trem, ring, echo, reverb, comp;
    } a_{};

    Biquad rumble_, lowShelf_, midPeak_, highShelf_;
    Filter24 hp_, lp_;
    NoiseGate gate_;
    Chorus chorus_;
    Echo echo_;
    Reverb reverb_;
    Compressor comp_;
    Limiter limiter_;
    OversampledDrive drive_;
    Lfo ringLfo_, tremLfo_;
    Smoother inGain_, levelGain_, outGain_, ringAmt_;
    double distDrive_ = 1.0, crushStep_ = 0.0, tremDepth_ = 0.0, tremRate_ = 6.0, ringFreq_ = 80.0;
    int holdFactor_ = 1, holdCounter_ = 0;
    double holdValue_ = 0.0;
};

}  // namespace vox
