// Windows helpers shared by the whole app: strings, paths, logging, registry,
// sound cues, shell actions.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace vox {

inline constexpr const char* kAppVersion = "2.0.0";
inline constexpr const wchar_t* kAppName = L"Pressvppp";
inline constexpr const wchar_t* kWindowClass = L"PressvpppMainWindow";
inline constexpr UINT WM_APP_SHOW = WM_APP + 1;      // second instance → show window
inline constexpr UINT WM_APP_TRAY = WM_APP + 2;      // tray icon callback
inline constexpr UINT WM_APP_DEVICES = WM_APP + 3;   // audio endpoints changed
inline constexpr UINT WM_APP_WAKE = WM_APP + 4;      // background task finished

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

std::wstring dataDir();          // %APPDATA%\Pressvppp (created on demand)
std::wstring exePath();
std::wstring exeDir();

void logLine(const char* fmt, ...);   // %APPDATA%\Pressvppp\pressvppp.log (rotating, thread-safe)

bool setAutostart(bool enabled);
bool autostartEnabled();

void openUrl(const wchar_t* url);
void openPath(const std::wstring& path);
void openSoundSettings(bool recording);

// 'on' = rising, 'off' = falling, 'switch' = short blip. Asynchronous.
enum class Cue { On, Off, Switch };
void playCue(Cue c);

std::string formatHotkey(UINT mods, UINT vk);   // "Ctrl + Alt + V"

}  // namespace vox
