// Complete voice chain: mic clean-up → spectral voice → effects → dynamics.
//
//   input gain → rumble HPF → gate → SpectralVoice → ring mod → 3-band EQ →
//   HP/LP 24 dB → distortion → bitcrush → 8-bit → chorus → tremolo → echo →
//   reverb → preset level → compressor → master volume → limiter (−1 dBFS)
//
// Thread model: setVoice/setGlobal/setEnabled are called from the UI thread;
// process() runs on the audio thread and never blocks (it picks up new
// settings with try_lock at the start of an internal block).
#pragma once

#include "Effects.h"
#include "Params.h"
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
    int sampleRate() const { return sr_; }
    bool sleeping() const { return sleeping_; }

private:
    void processBlock(float* x, size_t n, bool allowApply);
    void runBlock(float* x, size_t n);
    void applyPending();

    int sr_;
    SpectralVoice core_;

    // ---- shared with UI thread
    std::mutex mtx_;
    VoiceValues pendingVoice_;
    GlobalValues pendingGlobal_;
    bool pendingEnabled_ = true;
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
