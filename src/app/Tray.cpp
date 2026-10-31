#include "Tray.h"

#include <cwchar>

namespace vox {

static void copyW(wchar_t* dst, size_t cap, const std::wstring& src) {
    wcsncpy_s(dst, cap, src.c_str(), _TRUNCATE);
}

bool Tray::create(HWND hwnd, HICON iconOn, HICON iconOff) {
    on_ = iconOn;
    off_ = iconOff;
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_APP_TRAY;
    nid_.hIcon = enabled_ ? on_ : off_;
    copyW(nid_.szTip, ARRAYSIZE(nid_.szTip), L"Pressvppp");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (added_) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    }
    return added_;
}

void Tray::recreate() {
    if (!nid_.hWnd) return;
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (added_) Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void Tray::destroy() {
    if (added_) Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
}

void Tray::update(bool enabled, const std::string& tooltip) {
    if (!added_) return;
    enabled_ = enabled;
    nid_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.hIcon = enabled ? on_ : off_;
    copyW(nid_.szTip, ARRAYSIZE(nid_.szTip), widen(tooltip));
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Tray::balloon(const std::string& title, const std::string& text) {
    if (!added_) return;
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    copyW(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), widen(title));
    copyW(n.szInfo, ARRAYSIZE(n.szInfo), widen(text));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

}  // namespace vox
