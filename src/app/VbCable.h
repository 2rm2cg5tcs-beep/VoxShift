// Downloads the official VB-Audio CABLE driver pack and launches its installer
// elevated. Runs on a background thread; progress is reported through a callback
// (called from that thread) and the window is woken with WM_APP_WAKE.
#pragma once

#include "Platform.h"

#include <functional>
#include <string>

namespace vox {

inline constexpr const wchar_t* kVbCableUrl = L"https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip";
inline constexpr const wchar_t* kVbCablePage = L"https://vb-audio.com/Cable/";

// report(message, finished, ok)
void installVbCableAsync(HWND notify, std::function<void(const std::string&, bool, bool)> report);

}  // namespace vox
