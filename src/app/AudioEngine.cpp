#include "AudioEngine.h"

#include "Com.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace vox {

namespace {
constexpr int kRate = 48000;
constexpr REFERENCE_TIME kHns = 10000;   // 100-ns units per ms

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

WAVEFORMATEXTENSIBLE floatFormat(WORD channels) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = channels;
    f.Format.nSamplesPerSec = kRate;
    f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = WORD(channels * 4);
    f.Format.nAvgBytesPerSec = kRate * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    f.SubFormat = kSubtypeFloat;
    return f;
}

std::string hrText(HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string friendlyError(HRESULT hr, const std::string& device) {
    const std::string where = device.empty() ? "" : " \xC2\xAB" + device + "\xC2\xBB";
    switch (hr) {
        case AUDCLNT_E_DEVICE_IN_USE:
            return "Устройство" + where + " занято в монопольном режиме другой программой. Закройте её или "
                   "отключите «Разрешить приложениям использовать устройство в монопольном режиме».";
        case AUDCLNT_E_DEVICE_INVALIDATED:
            return "Устройство" + where + " отключено. Подключите его — программа переподключится сама.";
        case E_ACCESSDENIED:
            return "Windows запрещает доступ к микрофону. Параметры → Конфиденциальность → Микрофон → "
                   "разрешите классическим приложениям доступ к микрофону.";
        case AUDCLNT_E_UNSUPPORTED_FORMAT:
            return "Устройство" + where + " не принимает формат 48 кГц. Свойства устройства → Дополнительно → 48000 Гц.";
        default:
            return "Ошибка звука" + where + " (" + hrText(hr) + ").";
    }
}

struct MmcssScope {
    HANDLE h = nullptr;
    MmcssScope() {
        DWORD task = 0;
        h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        if (!h) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    }
    ~MmcssScope() { if (h) AvRevertMmThreadCharacteristics(h); }
};
}  // namespace

// ---------------------------------------------------------------- ring
void SpscRing::write(const float* x, size_t n) {
    const size_t cap = buf_.size();
    size_t w = w_.load(std::memory_order_relaxed);
    const size_t r = r_.load(std::memory_order_acquire);
    const size_t used = w - r;
    if (used + n > cap) {          // consumer stalled: drop the newest data
        drops.fetch_add(1);
        n = cap > used ? cap - used : 0;
    }
    for (size_t i = 0; i < n; ++i) buf_[(w + i) % cap] = x[i];
    w_.store(w + n, std::memory_order_release);
}

void SpscRing::read(float* out, size_t n) {
    const size_t cap = buf_.size();
    size_t r = r_.load(std::memory_order_relaxed);
    const size_t w = w_.load(std::memory_order_acquire);
    if (clearReq_.exchange(false)) {
        r = w;
        primed_ = false;
    }
    size_t avail = w - r;
    if (!primed_) {
        if (avail < target_) {
            std::fill(out, out + n, 0.0f);
            r_.store(r, std::memory_order_release);
            return;
        }
        primed_ = true;
    }
    if (avail > target_ + slack_ + n) {   // clocks drifted: drop the oldest audio
        r += avail - target_ - n;
        avail = w - r;
        drops.fetch_add(1);
    }
    const size_t take = std::min(n, avail);
    for (size_t i = 0; i < take; ++i) out[i] = buf_[(r + i) % cap];
    if (take < n) {
        std::fill(out + take, out + n, 0.0f);
        const size_t f = std::min<size_t>(take, 32);   // short fade at the gap
        for (size_t i = 0; i < f; ++i) out[take - f + i] *= float(f - i) / float(f);
        primed_ = false;
        underruns.fetch_add(1);
    }
    r_.store(r + take, std::memory_order_release);
}

// ---------------------------------------------------------------- streams
struct AudioEngine::Stream {
    AudioDevice device;
    ComPtr<IMMDevice> dev;
    ComPtr<IAudioClient> client;
    HANDLE event = nullptr;
    UINT32 bufferFrames = 0;
    WORD channels = 1;
    REFERENCE_TIME latency = 0;
    ~Stream() { if (event) CloseHandle(event); }
};

AudioEngine::AudioEngine() : voice_(defaultVoice()), global_(defaultGlobal()) {}
AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setVoice(const VoiceValues& v, bool smooth) {
    voice_ = v;
    if (chain_) chain_->setVoice(v, smooth);
}
void AudioEngine::setGlobal(const GlobalValues& g) {
    global_ = g;
    monitorGain_.store(float(dbToGain(g[G_MonitorVolume])));
    if (chain_) chain_->setGlobal(g);
}
void AudioEngine::setEnabled(bool on) {
    enabled_ = on;
    if (chain_) chain_->setEnabled(on);
}
void AudioEngine::setMonitor(bool on) {
    if (on && !monitorOn_.load()) monRing_.clear();
    monitorOn_.store(on);
}

bool AudioEngine::start(const EngineSettings& s) {
    stop();
    status_ = {};
    failed_.store(false);
    stop_.store(false);

    const AudioDevice mic = resolveMic(s.mic);
    if (mic.id.empty()) {
        status_.error = "Микрофон не найден. Подключите микрофон — программа запустит звук сама.";
        return false;
    }
    const AudioDevice out = resolveOutput(s.output);
    if (out.id.empty()) {
        status_.error = "Не найден виртуальный кабель VB-Audio CABLE. Установите его на вкладке "
                        "«Подключение к игре» и перезагрузите компьютер.";
        return false;
    }
    if (isCableRecording(mic.name) && isCablePlayback(out.name)) {
        status_.error = "Микрофоном выбран сам виртуальный кабель — получится петля. Выберите настоящий микрофон.";
        return false;
    }
    const AudioDevice mon = resolveMonitor(s.monitor, out.id);

    const int block = s.bufferMode == 0 ? 240 : (s.bufferMode == 2 ? 960 : 480);
    const REFERENCE_TIME bufHns = REFERENCE_TIME(block) * 10000000LL / kRate * 2;   // 2 periods

    auto open = [&](const AudioDevice& d, bool capture, std::unique_ptr<Stream>& st) -> HRESULT {
        st = std::make_unique<Stream>();
        st->device = d;
        ComPtr<IMMDeviceEnumerator> e;
        HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid());
        if (FAILED(hr)) return hr;
        hr = e->GetDevice(d.id.c_str(), st->dev.put());
        if (FAILED(hr)) return hr;
        hr = st->dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, st->client.putVoid());
        if (FAILED(hr)) return hr;
        st->channels = capture ? 1 : 2;
        WAVEFORMATEXTENSIBLE fmt = floatFormat(st->channels);
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST;
        hr = st->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufHns, 0,
                                    reinterpret_cast<WAVEFORMATEX*>(&fmt), nullptr);
        if (FAILED(hr) && !capture) {          // some render drivers dislike stereo: try mono
            st->client.reset();
            hr = st->dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, st->client.putVoid());
            if (FAILED(hr)) return hr;
            st->channels = 1;
            fmt = floatFormat(1);
            hr = st->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufHns, 0,
                                        reinterpret_cast<WAVEFORMATEX*>(&fmt), nullptr);
        }
        if (FAILED(hr)) return hr;
        st->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hr = st->client->SetEventHandle(st->event);
        if (FAILED(hr)) return hr;
        st->client->GetBufferSize(&st->bufferFrames);
        st->client->GetStreamLatency(&st->latency);
        return S_OK;
    };

    HRESULT hr = open(mic, true, capture_);
    if (FAILED(hr)) {
        status_.error = friendlyError(hr, mic.name);
        logLine("capture open failed %s: %s", mic.name.c_str(), hrText(hr).c_str());
        capture_.reset();
        return false;
    }
    hr = open(out, false, render_);
    if (FAILED(hr)) {
        status_.error = friendlyError(hr, out.name);
        logLine("render open failed %s: %s", out.name.c_str(), hrText(hr).c_str());
        capture_.reset();
        render_.reset();
        return false;
    }
    if (!mon.id.empty()) {
        hr = open(mon, false, monitor_);
        if (FAILED(hr)) {
            logLine("monitor unavailable %s: %s", mon.name.c_str(), hrText(hr).c_str());
            monitor_.reset();
        }
    }

    chain_ = std::make_unique<VoiceChain>(kRate, s.quality);
    if (speakerHz_ > 0.0 || tractCm_ > 0.0) chain_->setSpeaker(speakerHz_, speakerConf_, tractCm_, tractConf_);
    chain_->setVoice(voice_);
    chain_->setGlobal(global_);
    chain_->setEnabled(enabled_);

    const size_t target = size_t(block + block / 2);
    outRing_.configure(target, size_t(block) * 3);
    monRing_.configure(target, size_t(block) * 3);
    outRing_.clear();
    monRing_.clear();
    monitorOn_.store(s.monitorOn);
    monitorGain_.store(float(dbToGain(global_[G_MonitorVolume])));
    lastCaptureTick_.store(nowMs());

    renderThread_ = std::thread([this] { renderLoop(render_.get(), &outRing_, false); });
    if (monitor_) monitorThread_ = std::thread([this] { renderLoop(monitor_.get(), &monRing_, true); });
    captureThread_ = std::thread([this] { captureLoop(capture_.get()); });

    status_.running = true;
    status_.micName = mic.name;
    status_.outputName = out.name;
    status_.monitorName = monitor_ ? mon.name : "";
    status_.latencyMs = 1000.0 * (chain_->latencySamples() + double(target)) / kRate +
                        double(capture_->latency + render_->latency) / double(kHns);
    logLine("audio started: %s -> %s, latency ~%.0f ms", mic.name.c_str(), out.name.c_str(), status_.latencyMs);
    return true;
}

void AudioEngine::setSpeaker(double hz, double confidence, double tractCm, double tractConf) {
    speakerHz_ = hz;
    speakerConf_ = confidence;
    tractCm_ = tractCm;
    tractConf_ = tractConf;
    if (chain_) chain_->setSpeaker(hz, confidence, tractCm, tractConf);
}

double AudioEngine::vocalTract() const {
    return (chain_ && chain_->vocalTractConfidence() > 0.0) ? chain_->vocalTract() : tractCm_;
}

double AudioEngine::vocalTractConfidence() const {
    return (chain_ && chain_->vocalTractConfidence() > 0.0) ? chain_->vocalTractConfidence() : tractConf_;
}

double AudioEngine::speakerF0() const {
    return (chain_ && chain_->speakerConfidence() > 0.0) ? chain_->speakerF0() : speakerHz_;
}

double AudioEngine::speakerConfidence() const {
    return (chain_ && chain_->speakerConfidence() > 0.0) ? chain_->speakerConfidence() : speakerConf_;
}

void AudioEngine::stop() {
    stop_.store(true);
    for (auto* s : {capture_.get(), render_.get(), monitor_.get()})
        if (s && s->event) SetEvent(s->event);
    for (auto* t : {&captureThread_, &renderThread_, &monitorThread_})
        if (t->joinable()) t->join();
    capture_.reset();
    render_.reset();
    monitor_.reset();
    if (chain_ && chain_->speakerConfidence() > 0.0) {
        speakerHz_ = chain_->speakerF0();
        speakerConf_ = chain_->speakerConfidence();
    }
    if (chain_ && chain_->vocalTractConfidence() > 0.0) {
        tractCm_ = chain_->vocalTract();
        tractConf_ = chain_->vocalTractConfidence();
    }
    chain_.reset();
    status_.running = false;
    inLevel_.store(0);
    outLevel_.store(0);
}

bool AudioEngine::healthy() const {
    if (!status_.running) return true;
    if (failed_.load()) return false;
    return nowMs() - lastCaptureTick_.load() < 2500;
}

void AudioEngine::captureLoop(Stream* st) {
    ComScope com;
    MmcssScope mmcss;
    ComPtr<IAudioCaptureClient> cap;
    if (FAILED(st->client->GetService(kIID_IAudioCaptureClient, cap.putVoid())) || FAILED(st->client->Start())) {
        failed_.store(true);
        return;
    }
    std::vector<float> buf(48000);
    double cpuAvg = 0.0;
    float inPeak = 0.0f, outPeak = 0.0f;
    while (!stop_.load()) {
        const DWORD w = WaitForSingleObject(st->event, 200);
        if (stop_.load()) break;
        if (w != WAIT_OBJECT_0) continue;
        lastCaptureTick_.store(nowMs());
        for (;;) {
            UINT32 packet = 0;
            HRESULT hr = cap->GetNextPacketSize(&packet);
            if (FAILED(hr)) { failed_.store(true); logLine("capture failed %s", hrText(hr).c_str()); break; }
            if (packet == 0) break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { failed_.store(true); break; }
            const size_t n = std::min<size_t>(frames, buf.size());
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) std::fill(buf.begin(), buf.begin() + n, 0.0f);
            else std::memcpy(buf.data(), data, n * sizeof(float));
            cap->ReleaseBuffer(frames);

            float pk = 0.0f;
            for (size_t i = 0; i < n; ++i) pk = std::max(pk, std::abs(buf[i]));
            inPeak = std::max(pk, inPeak * 0.85f);
            const auto t0 = std::chrono::steady_clock::now();
            chain_->process(buf.data(), n);
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            cpuAvg = 0.95 * cpuAvg + 0.05 * (dt * kRate / std::max<size_t>(1, n));
            pk = 0.0f;
            for (size_t i = 0; i < n; ++i) pk = std::max(pk, std::abs(buf[i]));
            outPeak = std::max(pk, outPeak * 0.85f);
            outRing_.write(buf.data(), n);
            if (monitor_ && monitorOn_.load()) monRing_.write(buf.data(), n);
        }
        inLevel_.store(inPeak);
        outLevel_.store(outPeak);
        cpu_.store(float(cpuAvg));
        if (failed_.load()) break;
    }
    st->client->Stop();
}

void AudioEngine::renderLoop(Stream* st, SpscRing* ring, bool monitor) {
    ComScope com;
    MmcssScope mmcss;
    ComPtr<IAudioRenderClient> rc;
    if (FAILED(st->client->GetService(kIID_IAudioRenderClient, rc.putVoid()))) {
        if (!monitor) failed_.store(true);
        return;
    }
    // pre-fill with silence so the device starts cleanly
    BYTE* data = nullptr;
    if (SUCCEEDED(rc->GetBuffer(st->bufferFrames, &data))) rc->ReleaseBuffer(st->bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(st->client->Start())) {
        if (!monitor) failed_.store(true);
        return;
    }
    std::vector<float> mono(48000);
    while (!stop_.load()) {
        const DWORD w = WaitForSingleObject(st->event, 200);
        if (stop_.load()) break;
        if (w != WAIT_OBJECT_0) continue;
        UINT32 padding = 0;
        HRESULT hr = st->client->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            if (!monitor) { failed_.store(true); logLine("render failed %s", hrText(hr).c_str()); }
            break;
        }
        const UINT32 frames = st->bufferFrames - padding;
        if (frames == 0) continue;
        if (FAILED(rc->GetBuffer(frames, &data))) continue;
        const size_t n = std::min<size_t>(frames, mono.size());
        if (monitor && !monitorOn_.load()) std::fill(mono.begin(), mono.begin() + n, 0.0f);
        else ring->read(mono.data(), n);
        const float g = monitor ? monitorGain_.load() : 1.0f;
        float* outp = reinterpret_cast<float*>(data);
        if (st->channels == 1) {
            for (size_t i = 0; i < n; ++i) outp[i] = mono[i] * g;
        } else {
            for (size_t i = 0; i < n; ++i) {
                const float v = mono[i] * g;
                for (WORD c = 0; c < st->channels; ++c) outp[i * st->channels + c] = v;
            }
        }
        rc->ReleaseBuffer(frames, 0);
    }
    st->client->Stop();
}

}  // namespace vox
