// Minimal COM smart pointer + the Core-Audio GUIDs we use (defined locally so the
// build does not depend on which SDK/MinGW headers provide which uuid symbols).
#pragma once

#include "Platform.h"

#include <objbase.h>

namespace vox {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    ~ComPtr() { reset(); }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() { reset(); return &p_; }
    void** putVoid() { reset(); return reinterpret_cast<void**>(&p_); }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

// {BCDE0395-E52F-467C-8E3D-C4579291692E}
inline constexpr GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
// {A95664D2-9614-4F35-A746-DE8DB63617E6}
inline constexpr GUID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
// {1CB9AD4C-DBFA-4C32-B178-C2F568A703B2}
inline constexpr GUID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
// {C8ADBD64-E71E-48A0-A4DE-185C395CD317}
inline constexpr GUID kIID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
// {F294ACFC-3146-4483-A7BF-ADDCA7C260E2}
inline constexpr GUID kIID_IAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
// {7991EEC9-7E89-4D85-8390-6C703CEC60C0}
inline constexpr GUID kIID_IMMNotificationClient = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT {00000003-0000-0010-8000-00AA00389B71}
inline constexpr GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
// IPolicyConfig (undocumented, stable since Windows 7)
inline constexpr GUID kCLSID_PolicyConfigClient = {0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};
inline constexpr GUID kIID_IPolicyConfig = {0xF8679F50, 0x850A, 0x41CF, {0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8}};

// RAII CoInitializeEx for worker threads.
struct ComScope {
    HRESULT hr;
    explicit ComScope(DWORD model = COINIT_MULTITHREADED) : hr(CoInitializeEx(nullptr, model)) {}
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

}  // namespace vox
