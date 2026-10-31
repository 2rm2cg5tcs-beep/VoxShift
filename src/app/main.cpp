// Pressvppp entry point: single instance, COM, command line (--minimized, --selftest, --screenshot DIR).
#include "App.h"
#include "Com.h"
#include "Devices.h"
#include "Platform.h"

#include <shellapi.h>

#include <cmath>
#include <cstring>
#include <random>

using namespace vox;

// Headless check run by CI on the built exe: every preset renders finite audio,
// device enumeration works. Returns 0 on success.
static int selftest() {
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.0f, 0.05f);
    std::vector<float> x(48000);
    for (auto& s : x) s = nd(rng);
    for (const auto& p : builtinPresets()) {
        VoiceChain chain(48000, Quality::Balanced);
        chain.setVoice(p.full());
        std::vector<float> y = x;
        for (size_t i = 0; i < y.size(); i += 480) chain.process(y.data() + i, std::min<size_t>(480, y.size() - i));
        for (float s : y)
            if (!std::isfinite(s) || std::fabs(s) > 1.0f) return 2;
    }
    ComScope com(COINIT_APARTMENTTHREADED);
    (void)listDevices(Flow::Capture);
    (void)listDevices(Flow::Render);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool minimized = false, test = false;
    std::wstring shots;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--minimized")) minimized = true;
        else if (!wcscmp(argv[i], L"--selftest")) test = true;
        else if (!wcscmp(argv[i], L"--screenshot") && i + 1 < argc) shots = argv[++i];
    }
    LocalFree(argv);
    if (test) return selftest();

    HANDLE mutex = nullptr;
    if (shots.empty()) {
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\Pressvppp.SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // already running: bring that window forward instead
            if (HWND other = FindWindowW(kWindowClass, nullptr)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(other, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(other, WM_APP_SHOW, 0, 0);
            }
            if (mutex) CloseHandle(mutex);
            return 0;
        }
    }

    ComScope com(COINIT_APARTMENTTHREADED);
    logLine("Pressvppp %s starting", kAppVersion);
    int code = 0;
    {
        App app;
        code = app.run(inst, minimized, shots);
    }
    logLine("Pressvppp exited (%d)", code);
    if (mutex) CloseHandle(mutex);
    return code;
}
