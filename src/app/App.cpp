#include "App.h"

#include "IconGen.h"
#include "VbCable.h"
#include "Widgets.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <dwmapi.h>
#include <dxgi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace vox {

using namespace ui;

namespace {
constexpr int kHkToggle = 1, kHkNext = 2, kHkPrev = 3, kHkMonitor = 4;
constexpr UINT kCmdOpen = 100, kCmdToggle = 101, kCmdMonitor = 102, kCmdExit = 103, kCmdPresetBase = 1000;
}  // namespace

double App::now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// =====================================================================  run
int App::run(HINSTANCE inst, bool startMinimized, const std::wstring& screenshotDir) {
    screenshotDir_ = screenshotDir;
    cfg_.load();
    if (!screenshotDir_.empty()) {   // deterministic look for CI screenshots
        cfg_ = Config();
        cfg_.firstRunDone = false;
    }
    rebuildPresets();
    if (!findPreset(cfg_.preset)) cfg_.preset = kNeutralPreset;
    if (cfg_.voiceModified) applyVoice(cfg_.voice, true);
    else applyVoice(findPreset(cfg_.preset)->values, false);
    if (!cfg_.firstRunDone) requestTab_ = kPageSetup;

    const bool headless = !screenshotDir_.empty();   // CI: never block on a message box
    if (!createWindow(inst)) {
        logLine("createWindow failed (%lu)", GetLastError());
        if (!headless) MessageBoxW(nullptr, L"Не удалось создать окно Pressvppp.", kAppName, MB_ICONERROR);
        return 1;
    }
    if (!createDevice()) {
        logLine("Direct3D 11 device creation failed");
        if (!headless) MessageBoxW(nullptr, L"Не удалось инициализировать графику (Direct3D 11).", kAppName, MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // layout is fixed; nothing to persist
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    setDpi(GetDpiForWindow(hwnd_));
    applyTheme(float(dpi_) / 96.0f);
    fontsDirty_ = false;

    iconOn_ = createLogoIcon(32, true);
    iconOff_ = createLogoIcon(32, false);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (screenshotDir_.empty()) {
        if (!tray_.create(hwnd_, iconOn_, iconOff_)) cfg_.closeToTray = false;
        watcher_.start(hwnd_);
        applyHotkeys();
        if (cfg_.autostart && !autostartEnabled()) setAutostart(true);
    }

    engine_.setVoice(voice_);
    engine_.setGlobal(cfg_.global);
    engine_.setEnabled(cfg_.enabled);
    if (cfg_.speakerF0 > 0.0) engine_.setSpeaker(cfg_.speakerF0, cfg_.speakerConf);
    if (screenshotDir_.empty()) applyCableDefaultOnStart();
    refreshDevices();
    refreshSetupStatus();

    const bool hide = screenshotDir_.empty() && (startMinimized || cfg_.startMinimized) && tray_.exists();
    if (hide) {
        visible_ = false;
    } else {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }
    if (screenshotDir_.empty()) startAudio();
    updateTray();

    while (running_) {
        const bool active = visible_ && !minimized_;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, !screenshotDir_.empty() ? 1 : ((active && !occluded_) ? 33 : 250), QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running_ = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;
        periodic();
        if ((visible_ && !minimized_) || !screenshotDir_.empty()) renderFrame();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    if (iconOn_) DestroyIcon(iconOn_);
    if (iconOff_) DestroyIcon(iconOff_);
    return 0;
}

// =====================================================================  window
bool App::createWindow(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProcStatic;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hbrBackground = CreateSolidBrush(RGB(14, 16, 22));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    const UINT sysDpi = GetDpiForSystem();
    const int w = MulDiv(1220, int(sysDpi), 96), h = MulDiv(800, int(sysDpi), 96);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int x = wa.left + std::max(0, int(wa.right - wa.left - w) / 2);
    const int y = wa.top + std::max(0, int(wa.bottom - wa.top - h) / 2);
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"Pressvppp — изменение голоса", WS_OVERLAPPEDWINDOW,
                            x, y, std::min<int>(w, wa.right - wa.left), std::min<int>(h, wa.bottom - wa.top),
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    // Windows 11 look: dark title bar matching the app, rounded corners
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = RGB(22, 25, 34), text = RGB(233, 236, 244);
    DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof(text));
    int corner = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    return true;
}

bool App::createDevice() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = E_FAIL;
    // hardware first, then WARP (VMs, remote desktop, broken drivers)
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd, &swap_,
                                           &device_, &got, &context_);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return false;
    // we handle Alt+Enter ourselves (i.e. not at all)
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(swap_->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) && factory) {
        factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    createTarget();
    return true;
}

void App::createTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
        device_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
}

void App::destroyTarget() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
}

void App::destroyDevice() {
    destroyTarget();
    if (swap_) { swap_->Release(); swap_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void App::setDpi(UINT dpi) {
    if (dpi == 0) dpi = 96;
    if (dpi != dpi_) fontsDirty_ = true;
    dpi_ = dpi;
}

void App::renderFrame() {
    if (!rtv_) return;
    if (occluded_ && screenshotDir_.empty()) {
        if (swap_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) return;
        occluded_ = false;
    }
    if (fontsDirty_) {
        applyTheme(float(dpi_) / 96.0f);
        ImGui_ImplDX11_InvalidateDeviceObjects();
        fontsDirty_ = false;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawUi();
    ImGui::Render();
    const float clear[4] = {14 / 255.f, 16 / 255.f, 22 / 255.f, 1.f};
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // CI screenshot mode: every tab, a few frames each, then exit
    if (!screenshotDir_.empty()) {
        const int tab = screenshotStep_ / 6, sub = screenshotStep_ % 6;
        if (tab >= kPageCount) {
            running_ = false;
        } else {
            if (sub == 0) requestTab_ = tab;
            if (sub == 5) {
                wchar_t name[64];
                swprintf(name, 64, L"\\tab%d.bmp", tab);
                const bool ok = saveScreenshot(screenshotDir_ + name);
                logLine("screenshot tab %d: %s", tab, ok ? "saved" : "FAILED");
            }
            ++screenshotStep_;
        }
    }

    // in CI the session may be locked/occluded: keep rendering for screenshots
    const HRESULT hr = swap_->Present(screenshotDir_.empty() ? 1 : 0, 0);
    occluded_ = screenshotDir_.empty() && hr == DXGI_STATUS_OCCLUDED;
}

bool App::saveScreenshot(const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(device_->CreateTexture2D(&d, nullptr, &staging)) && staging) {
        context_->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            const int w = int(d.Width), h = int(d.Height);
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof(ih);
            ih.biWidth = w;
            ih.biHeight = -h;
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            ih.biCompression = BI_RGB;
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + DWORD(w) * h * 4;
            FILE* f = _wfopen(path.c_str(), L"wb");
            if (f) {
                std::fwrite(&fh, sizeof(fh), 1, f);
                std::fwrite(&ih, sizeof(ih), 1, f);
                std::vector<uint8_t> row(size_t(w) * 4);
                for (int y = 0; y < h; ++y) {
                    const uint8_t* src = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
                    for (int x = 0; x < w; ++x) {   // RGBA → BGRA
                        row[x * 4 + 0] = src[x * 4 + 2];
                        row[x * 4 + 1] = src[x * 4 + 1];
                        row[x * 4 + 2] = src[x * 4 + 0];
                        row[x * 4 + 3] = 255;
                    }
                    std::fwrite(row.data(), 1, row.size(), f);
                }
                std::fclose(f);
                ok = true;
            }
            context_->Unmap(staging, 0);
        }
        staging->Release();
    }
    back->Release();
    return ok;
}

LRESULT CALLBACK App::wndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* self = nullptr;
    if (m == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->wndProc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // hotkey capture dialog: swallow keys before ImGui sees them
    if (capturingHotkey_ >= 0 && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN)) {
        const UINT vk = UINT(w);
        if (vk == VK_ESCAPE) {
            capturingHotkey_ = -1;
            applyHotkeys();
            return 0;
        }
        const bool isMod = vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN ||
                           vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
                           vk == VK_LSHIFT || vk == VK_RSHIFT;
        if (!isMod) {
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
            const bool fkey = vk >= VK_F1 && vk <= VK_F24;
            // a bare letter would fire while typing in game chat
            if (mods != 0 || fkey) captured_ = Hotkey{mods, vk};
        }
        return 0;
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;

    if (m == taskbarCreatedMsg_ && taskbarCreatedMsg_) {
        tray_.recreate();
        updateTray();
        return 0;
    }
    switch (m) {
        case WM_SIZE:
            minimized_ = (w == SIZE_MINIMIZED);
            if (swap_ && w != SIZE_MINIMIZED) {
                destroyTarget();
                swap_->ResizeBuffers(0, UINT(LOWORD(l)), UINT(HIWORD(l)), DXGI_FORMAT_UNKNOWN, 0);
                createTarget();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(l);
            mm->ptMinTrackSize.x = MulDiv(1000, int(dpi_), 96);
            mm->ptMinTrackSize.y = MulDiv(660, int(dpi_), 96);
            return 0;
        }
        case WM_DPICHANGED: {
            setDpi(HIWORD(w));
            const RECT* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            if (cfg_.closeToTray && tray_.exists() && screenshotDir_.empty()) hideToTray();
            else quit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_HOTKEY:
            onHotkey(int(w));
            return 0;
        case WM_APP_TRAY: {
            const UINT ev = LOWORD(l);
            if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONDBLCLK) showWindow();
            else if (ev == WM_CONTEXTMENU || ev == WM_RBUTTONUP) showTrayMenu();
            return 0;
        }
        case WM_APP_SHOW:
            showWindow();
            return 0;
        case WM_APP_DEVICES:
            deviceChangeAt_ = now() + 1.2;   // debounce bursts of notifications
            return 0;
        case WM_APP_WAKE:
            return 0;
        case WM_APP_QUIT:
            quit();
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMRESUMEAUTOMATIC) deviceChangeAt_ = now() + 3.0;
            return TRUE;
        case WM_QUERYENDSESSION:
            syncSpeaker();
            cfg_.save();
            return TRUE;
        case WM_ENDSESSION:
            if (w) {   // Windows shuts down: leave the microphone as it was before us
                engine_.stop();
                restoreDefaultMic();
                cfg_.save();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((w & 0xFFF0) == SC_KEYMENU) return 0;   // no Alt menu beep
            break;
        default:
            break;
    }
    return DefWindowProcW(h, m, w, l);
}

// =====================================================================  presets
void App::rebuildPresets() {
    presets_.clear();
    for (const auto& b : builtinPresets()) presets_.push_back({b.name, b.category, b.description, true, b.full()});
    for (const auto& u : cfg_.userPresets) presets_.push_back({u.name, kUserCategory, u.description, false, u.values});
}

const PresetItem* App::findPreset(const std::string& name) const {
    for (const auto& p : presets_)
        if (p.name == name) return &p;
    return nullptr;
}

void App::applyVoice(const VoiceValues& v, bool modified) {
    voice_ = v;
    modified_ = modified;
    engine_.setVoice(voice_, true);
    cfg_.voice = voice_;
    cfg_.voiceModified = modified;
}

void App::selectPreset(const std::string& name, bool cue) {
    const PresetItem* p = findPreset(name);
    if (!p) return;
    cfg_.preset = p->name;
    applyVoice(p->values, false);
    saveSoon();
    updateTray();
    if (cue && cfg_.soundCues) playCue(Cue::Switch);
}

void App::stepPreset(int step, bool cue) {
    std::vector<std::string> names;
    if (cue && cfg_.hotkeysFavoritesOnly)
        for (const auto& f : cfg_.favorites)
            if (findPreset(f)) names.push_back(f);
    if (names.size() < 2) {
        names.clear();
        for (const auto& p : presets_) names.push_back(p.name);
    }
    if (names.empty()) return;
    auto it = std::find(names.begin(), names.end(), cfg_.preset);
    const int n = int(names.size());
    const int i = it == names.end() ? 0 : ((int(it - names.begin()) + step) % n + n) % n;
    selectPreset(names[size_t(i)], cue);
}

void App::voiceChanged() {
    modified_ = true;
    cfg_.voice = voice_;
    cfg_.voiceModified = true;
    engine_.setVoice(voice_);
    saveSoon();
}

void App::toggleEnabled(bool cue) {
    cfg_.enabled = !cfg_.enabled;
    engine_.setEnabled(cfg_.enabled);
    saveSoon();
    updateTray();
    if (cue && cfg_.soundCues) playCue(cfg_.enabled ? Cue::On : Cue::Off);
}

void App::setMonitor(bool on, bool cue) {
    cfg_.monitorEnabled = on;
    if (on && engine_.status().running && !engine_.monitorAvailable())
        toast("Не найдено устройство для прослушки — выберите наушники в «Настройках».", kWarn);
    engine_.setMonitor(on);
    saveSoon();
    if (cue && cfg_.soundCues) playCue(Cue::Switch);
}

void App::toggleFavorite() {
    auto it = std::find(cfg_.favorites.begin(), cfg_.favorites.end(), cfg_.preset);
    if (it == cfg_.favorites.end()) cfg_.favorites.push_back(cfg_.preset);
    else cfg_.favorites.erase(it);
    saveSoon();
}

bool App::saveUserPreset(const std::string& rawName) {
    std::string name = rawName;
    while (!name.empty() && (name.back() == ' ')) name.pop_back();
    while (!name.empty() && (name.front() == ' ')) name.erase(name.begin());
    if (name.empty()) {
        toast("Введите название пресета.", kErr);
        return false;
    }
    for (const auto& b : builtinPresets())
        if (name == b.name) {
            toast("Это имя занято встроенным пресетом — придумайте другое.", kErr);
            return false;
        }
    bool replaced = false;
    for (auto& u : cfg_.userPresets)
        if (u.name == name) {
            u.values = voice_;
            replaced = true;
        }
    if (!replaced) cfg_.userPresets.push_back({name, "Ваш пресет", voice_});
    rebuildPresets();
    cfg_.preset = name;
    modified_ = false;
    cfg_.voiceModified = false;
    cfg_.save();
    toast("Пресет «" + name + "» сохранён", kOk);
    return true;
}

void App::deleteUserPreset() {
    const std::string name = cfg_.preset;
    auto it = std::find_if(cfg_.userPresets.begin(), cfg_.userPresets.end(),
                           [&](const UserPreset& u) { return u.name == name; });
    if (it == cfg_.userPresets.end()) return;
    cfg_.userPresets.erase(it);
    cfg_.favorites.erase(std::remove(cfg_.favorites.begin(), cfg_.favorites.end(), name), cfg_.favorites.end());
    rebuildPresets();
    selectPreset(kNeutralPreset);
    toast("Пресет «" + name + "» удалён", kMuted);
}

// =====================================================================  audio
void App::startAudio() {
    EngineSettings s;
    s.mic = cfg_.micDevice;
    s.output = cfg_.outputDevice;
    s.monitor = cfg_.monitorDevice;
    s.monitorOn = cfg_.monitorEnabled;
    s.quality = cfg_.quality == 1 ? Quality::High : Quality::Balanced;
    s.bufferMode = cfg_.bufferMode;
    engine_.setVoice(voice_);
    engine_.setGlobal(cfg_.global);
    engine_.setEnabled(cfg_.enabled);
    if (engine_.start(s)) {
        retryCount_ = 0;
    } else {
        ++retryCount_;
        nextRetry_ = now() + (retryCount_ < 3 ? 3.0 : 10.0);
        logLine("audio not started: %s", engine_.status().error.c_str());
    }
    refreshSetupStatus();
    updateTray();
}

void App::restartAudio() {
    engine_.stop();
    startAudio();
}

void App::refreshDevices() {
    mics_ = listDevices(Flow::Capture);
    outputs_ = listDevices(Flow::Render);
}

void App::refreshSetupStatus() {
    cableFound_ = false;
    for (const auto& d : outputs_)
        if (isCablePlayback(d.name)) cableFound_ = true;
    cableDefault_ = cableIsDefaultMic();
    lastSetupCheck_ = now();
}

void App::periodic() {
    const double t = now();
    {
        std::vector<std::pair<std::string, bool>> msgs;
        {
            std::lock_guard<std::mutex> lk(bgMutex_);
            msgs.swap(bgMessages_);
        }
        for (auto& [text, ok] : msgs) toast(text, ok ? kOk : kErr, 10.0);
    }
    if (t >= speakerSyncAt_) {
        // remember the learned voice pitch (every 20 s, when it changed)
        speakerSyncAt_ = t + 20.0;
        const double hz = engine_.speakerF0(), conf = engine_.speakerConfidence();
        if (conf >= 100.0 && (std::abs(hz - cfg_.speakerF0) > 0.5 || conf > cfg_.speakerConf * 1.5 + 50.0)) {
            syncSpeaker();
            saveSoon();
        }
    }
    if (saveAt_ > 0 && t >= saveAt_) {
        saveAt_ = -1;
        cfg_.save();
    }
    if (deviceChangeAt_ > 0 && t >= deviceChangeAt_) {
        deviceChangeAt_ = -1;
        refreshDevices();
        refreshSetupStatus();
        if (!engine_.status().running || !engine_.healthy()) {
            logLine("devices changed - restarting audio");
            restartAudio();
        }
    }
    if (t - lastHealth_ >= 1.0) {
        lastHealth_ = t;
        if (engine_.status().running && !engine_.healthy()) {
            logLine("audio stream died - reconnecting");
            engine_.stop();
            toast("Устройство отключилось — переподключаюсь…", kWarn, 4.0);
            nextRetry_ = t + 1.0;
        } else if (!engine_.status().running && screenshotDir_.empty() && t >= nextRetry_) {
            refreshDevices();
            startAudio();
        }
    }
    if (visible_ && tab_ == kPageSetup && t - lastSetupCheck_ > 5.0) refreshSetupStatus();
}

void App::makeCableDefault() {
    // pin the real microphone first: once the cable is the default recording
    // device, "automatic" must not pick the cable itself
    if (cfg_.micDevice.empty()) {
        const AudioDevice mic = engine_.status().running ? AudioDevice{} : resolveMic("");
        cfg_.micDevice = engine_.status().running ? engine_.status().micName : mic.name;
    }
    const AudioDevice cable = findCableRecording();
    if (cable.id.empty()) {
        toast("Устройство «CABLE Output» не найдено. Установите VB-Cable и перезагрузите компьютер.", kErr, 8.0);
        return;
    }
    cfg_.manageDefaultMic = true;
    const std::wstring prev = defaultDeviceId(Flow::Capture);
    if (!prev.empty() && prev != cable.id) cfg_.previousDefaultMic = narrow(prev);
    if (prev == cable.id) {
        toast("«" + cable.name + "» уже микрофон по умолчанию.", kOk);
    } else if (setDefaultDevice(cable.id)) {
        toast("Готово: «" + cable.name + "» теперь микрофон по умолчанию. При выходе из Pressvppp вернётся прежний.", kOk, 8.0);
    } else {
        toast("Windows не дала сменить устройство — сделайте это вручную в открывшемся окне.", kErr, 8.0);
        openSoundSettings(true);
    }
    cfg_.save();
    refreshSetupStatus();
}

// The cable is the default mic only while the app runs: re-apply it at start
// (the previous microphone was restored when the app last exited).
void App::applyCableDefaultOnStart() {
    if (!cfg_.manageDefaultMic) return;
    const AudioDevice cable = findCableRecording();
    if (cable.id.empty()) return;
    const std::wstring prev = defaultDeviceId(Flow::Capture);
    if (prev == cable.id) return;
    if (!prev.empty()) cfg_.previousDefaultMic = narrow(prev);
    if (cfg_.micDevice.empty()) cfg_.micDevice = resolveMic("").name;
    if (setDefaultDevice(cable.id)) logLine("CABLE Output set as default mic");
    saveSoon();
}

// Hand the microphone back exactly as it was before Pressvppp.
void App::restoreDefaultMic() {
    if (!cfg_.restoreMicOnExit || !cfg_.manageDefaultMic || cfg_.previousDefaultMic.empty()) return;
    const AudioDevice cable = findCableRecording();
    const std::wstring current = defaultDeviceId(Flow::Capture);
    if (!cable.id.empty() && current != cable.id) return;   // the user changed it meanwhile — leave it
    const std::wstring prev = widen(cfg_.previousDefaultMic);
    bool exists = false;
    for (const auto& d : listDevices(Flow::Capture)) exists = exists || d.id == prev;
    if (exists && setDefaultDevice(prev)) logLine("default mic restored");
}

void App::installCable() {
    if (cableInstallBusy_) return;
    cableInstallBusy_ = true;
    toast("Скачиваю VB-Cable…", kMuted, 30.0);
    installVbCableAsync(hwnd_, [this](const std::string& msg, bool finished, bool ok) {
        std::lock_guard<std::mutex> lk(bgMutex_);
        bgMessages_.push_back({msg, ok});
        if (finished) cableInstallBusy_ = false;
    });
}

// =====================================================================  hotkeys & tray
void App::applyHotkeys() {
    for (int id = kHkToggle; id <= kHkMonitor; ++id) UnregisterHotKey(hwnd_, id);
    hotkeyErrors_.clear();
    if (!cfg_.hotkeysEnabled) return;
    const Hotkey* keys[4] = {&cfg_.hkToggle, &cfg_.hkNext, &cfg_.hkPrev, &cfg_.hkMonitor};
    for (int i = 0; i < 4; ++i) {
        const Hotkey& k = *keys[i];
        if (!k.valid()) continue;
        if (!RegisterHotKey(hwnd_, kHkToggle + i, k.mods | MOD_NOREPEAT, k.vk)) {
            if (!hotkeyErrors_.empty()) hotkeyErrors_ += ", ";
            hotkeyErrors_ += formatHotkey(k.mods, k.vk);
        }
    }
    if (!hotkeyErrors_.empty())
        hotkeyErrors_ = "Занято другой программой: " + hotkeyErrors_ + ". Выберите другое сочетание.";
}

void App::onHotkey(int id) {
    switch (id) {
        case kHkToggle: toggleEnabled(true); break;
        case kHkNext: stepPreset(+1, true); break;
        case kHkPrev: stepPreset(-1, true); break;
        case kHkMonitor: setMonitor(!cfg_.monitorEnabled, true); break;
        default: break;
    }
}

void App::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU presetsMenu = CreatePopupMenu();
    auto add = [](HMENU m, UINT flags, UINT_PTR id, const std::string& text) {
        AppendMenuW(m, flags, id, widen(text).c_str());
    };
    // favourites first, then every category as a submenu
    bool anyFav = false;
    for (const auto& f : cfg_.favorites)
        for (size_t i = 0; i < presets_.size(); ++i)
            if (presets_[i].name == f) {
                add(presetsMenu, MF_STRING | (f == cfg_.preset ? MF_CHECKED : 0), kCmdPresetBase + i, f);
                anyFav = true;
            }
    if (anyFav) AppendMenuW(presetsMenu, MF_SEPARATOR, 0, nullptr);
    for (const char* cat : presetCategories()) {
        HMENU sub = nullptr;
        for (size_t i = 0; i < presets_.size(); ++i)
            if (presets_[i].category == cat) {
                if (!sub) sub = CreatePopupMenu();
                add(sub, MF_STRING | (presets_[i].name == cfg_.preset ? MF_CHECKED : 0), kCmdPresetBase + i,
                    presets_[i].name);
            }
        if (sub) AppendMenuW(presetsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), widen(cat).c_str());
    }
    add(menu, MF_STRING | MF_DEFAULT, kCmdOpen, "Открыть Pressvppp");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING | (cfg_.enabled ? MF_CHECKED : 0), kCmdToggle, "Изменение голоса\t" + formatHotkey(cfg_.hkToggle.mods, cfg_.hkToggle.vk));
    add(menu, MF_STRING | (cfg_.monitorEnabled ? MF_CHECKED : 0), kCmdMonitor, "Слышать себя");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(presetsMenu), widen("Пресет: " + cfg_.preset).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, MF_STRING, kCmdExit, "Выйти полностью");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);   // required so the menu closes when clicking elsewhere
    const UINT cmd = UINT(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);   // destroys submenus too

    if (cmd == kCmdOpen) showWindow();
    else if (cmd == kCmdToggle) toggleEnabled();
    else if (cmd == kCmdMonitor) setMonitor(!cfg_.monitorEnabled);
    else if (cmd == kCmdExit) quit();
    else if (cmd >= kCmdPresetBase && cmd < kCmdPresetBase + presets_.size()) selectPreset(presets_[cmd - kCmdPresetBase].name);
}

void App::updateTray() {
    tray_.update(cfg_.enabled, "Pressvppp — " + cfg_.preset + (cfg_.enabled ? " (вкл)" : " (выкл)"));
}

void App::showWindow() {
    visible_ = true;
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void App::hideToTray() {
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
    if (!cfg_.trayHintShown) {
        cfg_.trayHintShown = true;
        tray_.balloon("Pressvppp работает в фоне",
                      "Голос продолжает меняться. Закрыть полностью — кнопка выхода в окне или правый клик по значку → «Выйти полностью».");
        saveSoon();
    }
}

void App::syncSpeaker() {
    if (engine_.speakerConfidence() >= 100.0) {
        cfg_.speakerF0 = std::round(engine_.speakerF0() * 10.0) / 10.0;
        cfg_.speakerConf = std::round(engine_.speakerConfidence());
    }
}

void App::quit() {
    if (!running_) return;
    syncSpeaker();
    cfg_.save();
    for (int id = kHkToggle; id <= kHkMonitor; ++id) UnregisterHotKey(hwnd_, id);
    engine_.stop();
    watcher_.stop();
    restoreDefaultMic();
    tray_.destroy();
    running_ = false;
    logLine("quit requested - shutting down");
    DestroyWindow(hwnd_);
}

void App::saveSoon() { saveAt_ = now() + 0.7; }

void App::toast(const std::string& msg, ImU32 color, double seconds) {
    status_ = msg;
    statusColor_ = color;
    statusUntil_ = now() + seconds;
    if (!visible_ && tray_.exists() && seconds >= 6.0) tray_.balloon("Pressvppp", msg);
}

}  // namespace vox
