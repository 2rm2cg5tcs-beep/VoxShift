// Notification-area (tray) icon with a context menu built on demand.
#pragma once

#include "Platform.h"

#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

namespace vox {

class Tray {
public:
    bool create(HWND hwnd, HICON iconOn, HICON iconOff);
    void destroy();
    void update(bool enabled, const std::string& tooltip);
    void balloon(const std::string& title, const std::string& text);
    bool exists() const { return added_; }
    // Re-add after Explorer restarts (TaskbarCreated broadcast).
    void recreate();

private:
    NOTIFYICONDATAW nid_{};
    HICON on_ = nullptr, off_ = nullptr;
    bool added_ = false, enabled_ = true;
};

}  // namespace vox
