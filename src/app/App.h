// Pressvppp application: window, D3D11/ImGui rendering, tray, hotkeys and logic.
#pragma once

#include "AudioEngine.h"
#include "Config.h"
#include "Devices.h"
#include "Platform.h"
#include "Tray.h"

#include <d3d11.h>

#include "imgui/imgui.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace vox {

enum Page : int { kPagePresets = 0, kPageVoice, kPageTone, kPageFx, kPageMic, kPageSetup, kPageSettings, kPageCount };

struct PresetItem {
    std::string name, category, description;
    bool builtin = true;
    VoiceValues values{};
};

class App {
public:
    int run(HINSTANCE inst, bool startMinimized, const std::wstring& screenshotDir);

private:
    // ---- window & rendering
    bool createWindow(HINSTANCE inst);
    bool createDevice();
    void destroyDevice();
    void createTarget();
    void destroyTarget();
    void renderFrame();
    bool saveScreenshot(const std::wstring& path);
    static LRESULT CALLBACK wndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);
    void setDpi(UINT dpi);

    // ---- UI (Ui.cpp)
    void drawUi();
    void uiSidebar(float h);
    void uiMain(float w, float h);
    void uiPresetsPage();
    void uiSliderCard(const char* id, const char* title, std::initializer_list<int> params);
    void uiMicTab();
    void uiSetupTab();
    void uiSettingsTab();
    void uiModals();
    bool sliderRow(const ParamDef& def, double& value, int id);

    // ---- logic
    void rebuildPresets();
    const PresetItem* findPreset(const std::string& name) const;
    void selectPreset(const std::string& name, bool cue = false);
    void stepPreset(int step, bool cue = false);
    void applyVoice(const VoiceValues& v, bool modified);
    void voiceChanged();
    void toggleEnabled(bool cue = false);
    void setMonitor(bool on, bool cue = false);
    void toggleFavorite();
    bool saveUserPreset(const std::string& name);
    void deleteUserPreset();

    void startAudio();
    void restartAudio();
    void refreshDevices();
    void refreshSetupStatus();
    void periodic();
    void makeCableDefault();
    void installCable();

    void applyHotkeys();
    void onHotkey(int id);
    void showTrayMenu();
    void showWindow();
    void hideToTray();
    void quit();
    void saveSoon();
    void toast(const std::string& msg, ImU32 color, double seconds = 6.0);
    void updateTray();

    // ---- state
    HWND hwnd_ = nullptr;
    HICON iconOn_ = nullptr, iconOff_ = nullptr, iconBig_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swap_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    bool running_ = true, visible_ = true, minimized_ = false, occluded_ = false;
    UINT dpi_ = 96;
    bool fontsDirty_ = false;
    UINT taskbarCreatedMsg_ = 0;

    Config cfg_;
    AudioEngine engine_;
    Tray tray_;
    DeviceWatcher watcher_;
    std::vector<PresetItem> presets_;

    VoiceValues voice_{};
    bool modified_ = false;
    char search_[128] = {};
    bool focusSearch_ = false;
    int tab_ = 0, requestTab_ = -1;

    std::vector<AudioDevice> mics_, outputs_;
    bool cableFound_ = false;
    int cableDefault_ = -1;

    std::string status_;
    ImU32 statusColor_ = 0;
    double statusUntil_ = 0.0;

    // hotkeys
    std::string hotkeyErrors_;
    int capturingHotkey_ = -1;   // index 0..3 while the capture dialog is open
    Hotkey captured_{};

    // modals
    bool openSaveModal_ = false;
    char saveName_[128] = {};

    // timers (seconds, steady clock)
    double now() const;
    double saveAt_ = -1, lastHealth_ = 0, nextRetry_ = 0, deviceChangeAt_ = -1, lastSetupCheck_ = 0;
    int retryCount_ = 0;

    // background tasks → UI
    std::mutex bgMutex_;
    std::vector<std::pair<std::string, bool>> bgMessages_;   // (text, ok)
    bool cableInstallBusy_ = false;

    // screenshot mode (CI)
    std::wstring screenshotDir_;
    int screenshotStep_ = 0;
};

}  // namespace vox
