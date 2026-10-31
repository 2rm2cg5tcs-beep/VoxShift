#include "VbCable.h"

#include <shellapi.h>
#include <urlmon.h>
#include <wininet.h>

#include <thread>

namespace vox {

namespace {

bool runHidden(const std::wstring& cmdLine, DWORD timeoutMs) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdLine;   // CreateProcessW may modify the buffer
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    const bool done = WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (done) GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return done && code == 0;
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring systemDir() {
    wchar_t buf[MAX_PATH];
    UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf, n);
}

}  // namespace

void installVbCableAsync(HWND notify, std::function<void(const std::string&, bool, bool)> report) {
    std::thread([notify, report] {
        auto say = [&](const std::string& msg, bool finished, bool ok) {
            report(msg, finished, ok);
            PostMessageW(notify, WM_APP_WAKE, 0, 0);
        };
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        wchar_t tmpBuf[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpBuf);
        const std::wstring dir = std::wstring(tmpBuf) + L"Pressvppp_VBCABLE";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring zip = dir + L"\\VBCABLE_Driver_Pack.zip";

        say("Скачиваю драйвер VB-Cable с vb-audio.com…", false, true);
        DeleteUrlCacheEntryW(kVbCableUrl);
        if (FAILED(URLDownloadToFileW(nullptr, kVbCableUrl, zip.c_str(), 0, nullptr)) || !fileExists(zip)) {
            say("Не удалось скачать VB-Cable. Открываю сайт — скачайте и установите вручную.", true, false);
            openUrl(kVbCablePage);
            CoUninitialize();
            return;
        }

        say("Распаковываю…", false, true);
        // tar.exe ships with Windows 10 1803+; PowerShell is the fallback
        bool extracted = runHidden(L"\"" + systemDir() + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + dir + L"\"", 60000);
        if (!extracted)
            extracted = runHidden(L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Force -Path '" +
                                      zip + L"' -DestinationPath '" + dir + L"'\"", 120000);

        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        std::wstring setup = dir + L"\\VBCABLE_Setup_x64.exe";
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 && fileExists(dir + L"\\VBCABLE_Setup_ARM64.exe"))
            setup = dir + L"\\VBCABLE_Setup_ARM64.exe";
        if (!extracted || !fileExists(setup)) {
            say("Архив VB-Cable не распаковался. Открываю папку — запустите VBCABLE_Setup_x64.exe от имени администратора.", true, false);
            openPath(dir);
            CoUninitialize();
            return;
        }

        say("Подтвердите запрос администратора и нажмите «Install Driver» в окне VB-Audio.", false, true);
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = setup.c_str();
        sei.lpDirectory = dir.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            say("Установка отменена. Её можно повторить кнопкой «Установить VB-Cable».", true, false);
            CoUninitialize();
            return;
        }
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
        }
        say("Готово. ПЕРЕЗАГРУЗИТЕ компьютер, чтобы Windows увидела виртуальный кабель.", true, true);
        CoUninitialize();
    }).detach();
}

}  // namespace vox
