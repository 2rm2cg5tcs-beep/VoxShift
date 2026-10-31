#include "Platform.h"

#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <cmath>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <vector>

namespace vox {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring dataDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &base)) && base) {
        dir = std::wstring(base) + L"\\Pressvppp";
        CoTaskMemFree(base);
    } else {
        dir = exeDir() + L"\\data";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    cached = dir;
    return dir;
}

std::wstring exePath() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    buf.resize(n);
    return buf;
}

std::wstring exeDir() {
    std::wstring p = exePath();
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

void logLine(const char* fmt, ...) {
    static std::mutex mtx;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(mtx);
    const std::wstring path = dataDir() + L"\\pressvppp.log";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 1000000) {
        const std::wstring old = path + L".1";
        MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    FILE* f = _wfopen(path.c_str(), L"ab");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                 st.wMinute, st.wSecond, st.wMilliseconds, msg);
    std::fclose(f);
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool setAutostart(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS rc;
    if (enabled) {
        const std::wstring cmd = L"\"" + exePath() + L"\" --minimized";
        rc = RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kAppName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool autostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[2048];
    DWORD size = sizeof(buf), type = 0;
    const bool ok = RegQueryValueExW(key, kAppName, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (!ok) return false;
    // stale path (app moved/updated) counts as "not enabled" so it gets rewritten
    return std::wstring(buf).find(exePath()) != std::wstring::npos;
}

void openUrl(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

void openPath(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void openSoundSettings(bool recording) {
    ShellExecuteW(nullptr, L"open", L"control.exe", recording ? L"mmsys.cpl,,1" : L"mmsys.cpl,,0", nullptr,
                  SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- sound cues
static std::vector<char> makeWav(std::initializer_list<double> freqs, double dur) {
    const int sr = 22050;
    std::vector<int16_t> pcm;
    for (double f : freqs) {
        const int n = int(sr * dur);
        for (int i = 0; i < n; ++i) {
            double env = std::min({1.0, i / 200.0, (n - i) / 400.0});
            pcm.push_back(int16_t(32767 * 0.25 * env * std::sin(2 * 3.14159265358979 * f * i / sr)));
        }
    }
    const uint32_t dataBytes = uint32_t(pcm.size() * 2);
    std::vector<char> w(44 + dataBytes);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&w[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&w[at], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4); put32(4, 36 + dataBytes); std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, sr); put32(28, sr * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&w[36], "data", 4); put32(40, dataBytes);
    std::memcpy(&w[44], pcm.data(), dataBytes);
    return w;
}

void playCue(Cue c) {
    static const std::vector<char> on = makeWav({660, 990}, 0.07);
    static const std::vector<char> off = makeWav({880, 520}, 0.07);
    static const std::vector<char> sw = makeWav({1200}, 0.05);
    const std::vector<char>& w = c == Cue::On ? on : (c == Cue::Off ? off : sw);
    PlaySoundW(reinterpret_cast<LPCWSTR>(w.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

bool writeWav(const std::wstring& path, const float* x, size_t n, int sampleRate) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const uint32_t dataBytes = uint32_t(n * 2);
    char h[44];
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&h[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&h[at], &v, 2); };
    std::memcpy(&h[0], "RIFF", 4); put32(4, 36 + dataBytes); std::memcpy(&h[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, uint32_t(sampleRate)); put32(28, uint32_t(sampleRate * 2));
    put16(32, 2); put16(34, 16);
    std::memcpy(&h[36], "data", 4); put32(40, dataBytes);
    bool ok = fwrite(h, 1, 44, f) == 44;
    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) {
        const float v = std::isfinite(x[i]) ? std::clamp(x[i], -1.0f, 1.0f) : 0.0f;
        pcm[i] = int16_t(std::lround(v * 32767.0f));
    }
    ok = ok && fwrite(pcm.data(), 2, n, f) == n;
    fclose(f);
    return ok;
}

void playWavFile(const std::wstring& path) { PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT); }

void stopWav() { PlaySoundW(nullptr, nullptr, 0); }

// ---------------------------------------------------------------- hotkeys
static std::string keyName(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num " + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_PRIOR: return "PgUp";
        case VK_NEXT: return "PgDn";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_PAUSE: return "Pause";
        case VK_SCROLL: return "Scroll Lock";
        case VK_SPACE: return "Space";
        case VK_LEFT: return "\xE2\x86\x90";
        case VK_RIGHT: return "\xE2\x86\x92";
        case VK_UP: return "\xE2\x86\x91";
        case VK_DOWN: return "\xE2\x86\x93";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2: return "/";
        case VK_OEM_1: return ";";
        case VK_OEM_7: return "'";
        case VK_OEM_4: return "[";
        case VK_OEM_6: return "]";
        case VK_OEM_5: return "\\";
        case VK_OEM_3: return "`";
        default: {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%02X", vk);
            return buf;
        }
    }
}

std::string formatHotkey(UINT mods, UINT vk) {
    if (!vk) return "\xE2\x80\x94";   // em dash
    std::string s;
    if (mods & MOD_CONTROL) s += "Ctrl + ";
    if (mods & MOD_ALT) s += "Alt + ";
    if (mods & MOD_SHIFT) s += "Shift + ";
    if (mods & MOD_WIN) s += "Win + ";
    return s + keyName(vk);
}

}  // namespace vox
