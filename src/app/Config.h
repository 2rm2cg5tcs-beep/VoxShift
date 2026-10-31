// Persistent settings and user presets (%APPDATA%\Pressvppp\*.json).
#pragma once

#include "../dsp/Presets.h"
#include "Platform.h"

#include <string>
#include <vector>

namespace vox {

struct Hotkey {
    UINT mods = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk = 0;
    bool valid() const { return vk != 0; }
    bool operator==(const Hotkey& o) const { return mods == o.mods && vk == o.vk; }
};

struct UserPreset {
    std::string name, description;
    VoiceValues values;
};

struct Config {
    bool enabled = true;
    std::string preset = kDefaultPreset;
    VoiceValues voice = defaultVoice();
    bool voiceModified = false;
    GlobalValues global = defaultGlobal();
    std::vector<std::string> favorites = {"Без изменений", "Девушка", "Демон", "Робот", "Рация", "Бурундук"};
    bool hotkeysFavoritesOnly = true;

    std::string micDevice, outputDevice, monitorDevice;   // "" = automatic
    bool monitorEnabled = false;
    int quality = 0;      // 0 balanced, 1 high
    int bufferMode = 1;   // 0 fast, 1 normal, 2 safe

    // Ctrl+Alt+arrows rotate the screen on many Intel GPUs — PgUp/PgDn instead
    Hotkey hkToggle{MOD_CONTROL | MOD_ALT, 'V'};
    Hotkey hkNext{MOD_CONTROL | MOD_ALT, VK_NEXT};
    Hotkey hkPrev{MOD_CONTROL | MOD_ALT, VK_PRIOR};
    Hotkey hkMonitor{MOD_CONTROL | MOD_ALT, 'M'};
    bool hotkeysEnabled = true;
    bool soundCues = true;

    bool startMinimized = false;
    bool closeToTray = true;
    bool autostart = false;
    bool restoreMicOnExit = false;
    std::string previousDefaultMic;   // endpoint id (UTF-8)
    bool firstRunDone = false;
    bool trayHintShown = false;

    std::vector<UserPreset> userPresets;

    void load();
    void save() const;
};

}  // namespace vox
