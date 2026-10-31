#include "Devices.h"

#include "Com.h"

#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>

#include <algorithm>
#include <atomic>
#include <cctype>

namespace vox {

namespace {

// PKEY_Device_FriendlyName {A45C254E-DF1C-4EFD-8020-67D146A850E0}, 14
const PROPERTYKEY kPkeyFriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

// Vtable layout of the undocumented IPolicyConfig. Only the slot of
// SetDefaultEndpoint matters; the preceding methods are declared as placeholders.
struct IPolicyConfigVox : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL, const void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL, const void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

std::string lower(std::string s) {
    // ASCII lower-casing is enough: all markers are Latin
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsAny(const std::string& name, std::initializer_list<const char*> markers) {
    const std::string n = lower(name);
    for (const char* m : markers)
        if (n.find(m) != std::string::npos) return true;
    return false;
}

ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> e;
    CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, e.putVoid());
    return e;
}

std::string friendlyName(IMMDevice* dev) {
    ComPtr<IPropertyStore> store;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, store.put()))) return {};
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::string name;
    if (SUCCEEDED(store->GetValue(kPkeyFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
        name = narrow(pv.pwszVal);
    PropVariantClear(&pv);
    return name;
}

}  // namespace

bool isVirtualName(const std::string& n) {
    return containsAny(n, {"cable input", "cable output", "vb-audio", "voicemeeter", "virtual cable",
                           "cable-a", "cable-b", "hi-fi cable", "virtual audio"});
}
bool isCablePlayback(const std::string& n) {
    return containsAny(n, {"cable input", "voicemeeter input", "voicemeeter aux input", "hi-fi cable input"});
}
bool isCableRecording(const std::string& n) {
    return containsAny(n, {"cable output", "voicemeeter output", "hi-fi cable output"});
}

std::vector<AudioDevice> listDevices(Flow flow) {
    std::vector<AudioDevice> out;
    auto e = enumerator();
    if (!e) return out;
    ComPtr<IMMDeviceCollection> coll;
    if (FAILED(e->EnumAudioEndpoints(flow == Flow::Render ? eRender : eCapture, DEVICE_STATE_ACTIVE, coll.put())))
        return out;
    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, dev.put()))) continue;
        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id)) || !id) continue;
        AudioDevice d;
        d.id = id;
        CoTaskMemFree(id);
        d.name = friendlyName(dev.get());
        d.isVirtual = isVirtualName(d.name);
        out.push_back(std::move(d));
    }
    std::stable_sort(out.begin(), out.end(), [](const AudioDevice& a, const AudioDevice& b) {
        return a.isVirtual < b.isVirtual;   // real devices first
    });
    return out;
}

std::wstring defaultDeviceId(Flow flow) {
    auto e = enumerator();
    if (!e) return {};
    ComPtr<IMMDevice> dev;
    if (FAILED(e->GetDefaultAudioEndpoint(flow == Flow::Render ? eRender : eCapture, eConsole, dev.put())))
        return {};
    LPWSTR id = nullptr;
    std::wstring r;
    if (SUCCEEDED(dev->GetId(&id)) && id) {
        r = id;
        CoTaskMemFree(id);
    }
    return r;
}

bool setDefaultDevice(const std::wstring& id) {
    if (id.empty()) return false;
    ComPtr<IPolicyConfigVox> pc;
    if (FAILED(CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, kIID_IPolicyConfig, pc.putVoid())))
        return false;
    bool ok = true;
    for (ERole r : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), r)) && ok;
    return ok;
}

static const AudioDevice* byName(const std::vector<AudioDevice>& list, const std::string& name) {
    if (name.empty()) return nullptr;
    for (auto& d : list)
        if (d.name == name) return &d;
    const std::string ln = lower(name);
    for (auto& d : list)
        if (lower(d.name) == ln) return &d;
    return nullptr;
}

AudioDevice resolveMic(const std::string& savedName) {
    auto list = listDevices(Flow::Capture);
    if (auto* d = byName(list, savedName)) return *d;
    const std::wstring def = defaultDeviceId(Flow::Capture);
    for (auto& d : list)
        if (d.id == def && !d.isVirtual) return d;
    // the default is the cable itself (after "make CABLE the default mic"):
    // pick a real microphone instead — never feed our own output back in
    for (auto& d : list)
        if (!d.isVirtual && containsAny(d.name, {"microphone", "mic", "headset", "\xD0\xBC\xD0\xB8\xD0\xBA\xD1\x80\xD0\xBE\xD1\x84\xD0\xBE\xD0\xBD"}))
            return d;
    for (auto& d : list)
        if (!d.isVirtual) return d;
    return {};
}

AudioDevice resolveOutput(const std::string& savedName) {
    auto list = listDevices(Flow::Render);
    if (auto* d = byName(list, savedName)) return *d;
    for (auto& d : list)
        if (isCablePlayback(d.name)) return d;
    return {};
}

AudioDevice resolveMonitor(const std::string& savedName, const std::wstring& excludeId) {
    auto list = listDevices(Flow::Render);
    const AudioDevice* pick = byName(list, savedName);
    if (!pick) {
        const std::wstring def = defaultDeviceId(Flow::Render);
        for (auto& d : list)
            if (d.id == def && !d.isVirtual) pick = &d;
        if (!pick)
            for (auto& d : list)
                if (!d.isVirtual) { pick = &d; break; }
    }
    if (!pick || pick->id == excludeId) return {};
    return *pick;
}

AudioDevice findCableRecording() {
    for (auto& d : listDevices(Flow::Capture))
        if (isCableRecording(d.name)) return d;
    return {};
}

bool cableInstalled() {
    for (auto& d : listDevices(Flow::Render))
        if (isCablePlayback(d.name)) return true;
    return false;
}

int cableIsDefaultMic() {
    const AudioDevice cable = findCableRecording();
    if (cable.id.empty()) return 0;
    const std::wstring def = defaultDeviceId(Flow::Capture);
    if (def.empty()) return -1;
    return def == cable.id ? 1 : 0;
}

// ---------------------------------------------------------------- notifications
namespace {
class NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(HWND hwnd) : hwnd_(hwnd) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return post(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return post(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return post(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return post(); }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    HRESULT post() {
        PostMessageW(hwnd_, WM_APP_DEVICES, 0, 0);
        return S_OK;
    }
    HWND hwnd_;
    std::atomic<ULONG> ref_{1};
};
}  // namespace

bool DeviceWatcher::start(HWND hwnd) {
    stop();
    auto e = enumerator();
    if (!e) return false;
    auto* c = new NotificationClient(hwnd);
    if (FAILED(e->RegisterEndpointNotificationCallback(c))) {
        c->Release();
        return false;
    }
    enumerator_ = e.get();
    enumerator_->AddRef();
    client_ = c;
    return true;
}

void DeviceWatcher::stop() {
    if (enumerator_ && client_) {
        auto* c = static_cast<NotificationClient*>(client_);
        enumerator_->UnregisterEndpointNotificationCallback(c);
        c->Release();
    }
    if (enumerator_) enumerator_->Release();
    enumerator_ = nullptr;
    client_ = nullptr;
}

}  // namespace vox
