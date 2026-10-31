// Core-Audio endpoint discovery, default-device control and change notifications.
#pragma once

#include "Platform.h"

#include <string>
#include <vector>

struct IMMDeviceEnumerator;

namespace vox {

struct AudioDevice {
    std::wstring id;
    std::string name;      // UTF-8 friendly name, e.g. "CABLE Input (VB-Audio Virtual Cable)"
    bool isVirtual = false;
};

enum class Flow { Render, Capture };

std::vector<AudioDevice> listDevices(Flow flow);
std::wstring defaultDeviceId(Flow flow);
bool setDefaultDevice(const std::wstring& id);   // all three roles; undocumented IPolicyConfig

bool isVirtualName(const std::string& utf8Name);
bool isCablePlayback(const std::string& utf8Name);   // "CABLE Input" etc. (what we write into)
bool isCableRecording(const std::string& utf8Name);  // "CABLE Output" etc. (what games record)

// Resolve user choices ("" = automatic) to concrete endpoints.
AudioDevice resolveMic(const std::string& savedName);       // never returns a virtual cable for ""
AudioDevice resolveOutput(const std::string& savedName);    // "" → CABLE Input
AudioDevice resolveMonitor(const std::string& savedName, const std::wstring& excludeId);

AudioDevice findCableRecording();
bool cableInstalled();
// true / false; -1 when it cannot be determined
int cableIsDefaultMic();

// Posts WM_APP_DEVICES to hwnd whenever endpoints appear, vanish or change state.
class DeviceWatcher {
public:
    bool start(HWND hwnd);
    void stop();
    ~DeviceWatcher() { stop(); }

private:
    IMMDeviceEnumerator* enumerator_ = nullptr;
    void* client_ = nullptr;
};

}  // namespace vox
