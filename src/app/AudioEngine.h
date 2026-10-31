// WASAPI audio engine: microphone → VoiceChain → virtual cable (+ optional monitor).
//
// Each endpoint runs on its own event-driven thread registered with MMCSS
// ("Pro Audio"). Capture and render devices have independent clocks, so they
// are joined by lock-free ring buffers that keep latency bounded (excess is
// dropped) and re-prime after an underrun instead of crackling.
#pragma once

#include "../dsp/VoiceChain.h"
#include "Devices.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vox {

// Single-producer / single-consumer float FIFO.
class SpscRing {
public:
    explicit SpscRing(size_t capacity = 48000) : buf_(capacity) {}
    void configure(size_t target, size_t slack) { target_ = target; slack_ = slack; }
    void write(const float* x, size_t n);          // producer
    void read(float* out, size_t n);               // consumer (fills zeros when empty)
    void clear() { clearReq_.store(true); }
    std::atomic<uint32_t> underruns{0}, drops{0};

private:
    std::vector<float> buf_;
    std::atomic<size_t> w_{0}, r_{0};
    size_t target_ = 720, slack_ = 1440;
    bool primed_ = false;
    std::atomic<bool> clearReq_{false};
};

struct EngineSettings {
    std::string mic, output, monitor;   // saved names ("" = automatic)
    bool monitorOn = false;
    Quality quality = Quality::Balanced;
    int bufferMode = 1;                 // 0 fast (5 ms), 1 normal (10 ms), 2 safe (20 ms)
};

struct EngineStatus {
    bool running = false;
    std::string micName, outputName, monitorName, error;
    double latencyMs = 0.0;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Returns false and fills status().error (Russian, user-facing) on failure.
    bool start(const EngineSettings& s);
    void stop();
    bool healthy() const;               // false after a device vanished / thread died

    const EngineStatus& status() const { return status_; }

    void setVoice(const VoiceValues& v, bool smooth = false);
    void setGlobal(const GlobalValues& g);
    void setEnabled(bool on);
    void setMonitor(bool on);
    bool monitorAvailable() const { return monitorThread_.joinable(); }

    float inLevel() const { return inLevel_.load(); }
    float outLevel() const { return outLevel_.load(); }
    float cpuLoad() const { return cpu_.load(); }

    // The pitch of the user's voice as learned by the voice core (kept across
    // restarts of the engine; seeded from the saved settings).
    void setSpeaker(double hz, double confidence, double tractCm, double tractConf);
    double speakerF0() const;
    double speakerConfidence() const;
    double vocalTract() const;
    double vocalTractConfidence() const;
    double currentF0() const { return chain_ ? chain_->currentF0() : 0.0; }

    // Voice test: records the raw microphone for a few seconds (UI thread).
    bool startRecording(double seconds);
    float recordingProgress() const;   // 0..1; 1 = finished
    bool recordingActive() const { return recState_.load() == 1; }
    std::vector<float> takeRecording();

private:
    struct Stream;
    void captureLoop(Stream* st);
    void renderLoop(Stream* st, SpscRing* ring, bool monitor);

    EngineStatus status_;
    std::unique_ptr<VoiceChain> chain_;
    VoiceValues voice_;
    GlobalValues global_;
    bool enabled_ = true;
    double speakerHz_ = 0.0, speakerConf_ = 0.0, tractCm_ = 0.0, tractConf_ = 0.0;
    std::vector<float> rec_;
    size_t recLen_ = 0;
    std::atomic<size_t> recPos_{0};
    std::atomic<int> recState_{0};   // 0 idle, 1 recording, 2 done

    SpscRing outRing_, monRing_;
    std::unique_ptr<Stream> capture_, render_, monitor_;
    std::thread captureThread_, renderThread_, monitorThread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> monitorOn_{false};
    std::atomic<float> inLevel_{0}, outLevel_{0}, cpu_{0}, monitorGain_{0.5f};
    std::atomic<int64_t> lastCaptureTick_{0};
};

}  // namespace vox
