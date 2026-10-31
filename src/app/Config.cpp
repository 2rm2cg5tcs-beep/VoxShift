#include "Config.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace vox {

using json = nlohmann::json;

namespace {

std::wstring configPath() { return dataDir() + L"\\config.json"; }

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    out = std::move(s);
    return true;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

json voiceToJson(const VoiceValues& v) {
    json j = json::object();
    for (size_t i = 0; i < v.size(); ++i) j[kVoiceParams[i].key] = v[i];
    return j;
}

VoiceValues voiceFromJson(const json& j) {
    VoiceValues v = defaultVoice();
    if (!j.is_object()) return v;
    for (size_t i = 0; i < v.size(); ++i) {
        auto it = j.find(kVoiceParams[i].key);
        if (it != j.end() && it->is_number()) v[i] = clampParam(kVoiceParams[i], it->get<double>());
    }
    return v;
}

json globalToJson(const GlobalValues& g) {
    json j = json::object();
    for (size_t i = 0; i < g.size(); ++i) j[kGlobalParams[i].key] = g[i];
    return j;
}

GlobalValues globalFromJson(const json& j) {
    GlobalValues g = defaultGlobal();
    if (!j.is_object()) return g;
    for (size_t i = 0; i < g.size(); ++i) {
        auto it = j.find(kGlobalParams[i].key);
        if (it != j.end() && it->is_number()) g[i] = clampParam(kGlobalParams[i], it->get<double>());
    }
    return g;
}

json hkToJson(const Hotkey& h) { return json{{"mods", h.mods}, {"vk", h.vk}}; }

void hkFromJson(const json& j, const char* key, Hotkey& h) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return;
    Hotkey r;
    r.mods = it->value("mods", 0u) & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
    r.vk = it->value("vk", 0u) & 0xFF;
    h = r;
}

template <class T>
void get(const json& j, const char* key, T& dst) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        dst = it->get<T>();
    } catch (...) {
    }
}

}  // namespace

void Config::load() {
    std::string text;
    if (!readFile(configPath(), text)) return;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        logLine("config.json is damaged - using defaults");
        return;
    }
    get(j, "enabled", enabled);
    get(j, "preset", preset);
    if (j.contains("voice")) voice = voiceFromJson(j["voice"]);
    get(j, "voiceModified", voiceModified);
    if (j.contains("global")) global = globalFromJson(j["global"]);
    get(j, "favorites", favorites);
    get(j, "hotkeysFavoritesOnly", hotkeysFavoritesOnly);
    get(j, "micDevice", micDevice);
    get(j, "outputDevice", outputDevice);
    get(j, "monitorDevice", monitorDevice);
    get(j, "monitorEnabled", monitorEnabled);
    get(j, "quality", quality);
    get(j, "bufferMode", bufferMode);
    quality = std::clamp(quality, 0, 1);
    bufferMode = std::clamp(bufferMode, 0, 2);
    hkFromJson(j, "hkToggle", hkToggle);
    hkFromJson(j, "hkNext", hkNext);
    hkFromJson(j, "hkPrev", hkPrev);
    hkFromJson(j, "hkMonitor", hkMonitor);
    get(j, "hotkeysEnabled", hotkeysEnabled);
    get(j, "soundCues", soundCues);
    get(j, "startMinimized", startMinimized);
    get(j, "closeToTray", closeToTray);
    get(j, "autostart", autostart);
    get(j, "restoreMicOnExit", restoreMicOnExit);
    get(j, "manageDefaultMic", manageDefaultMic);
    get(j, "previousDefaultMic", previousDefaultMic);
    get(j, "firstRunDone", firstRunDone);
    get(j, "speakerF0", speakerF0);
    get(j, "speakerConf", speakerConf);
    if (!(speakerF0 >= 50.0 && speakerF0 <= 700.0)) speakerF0 = speakerConf = 0.0;
    get(j, "tractCm", tractCm);
    get(j, "tractConf", tractConf);
    if (!(tractCm >= 9.0 && tractCm <= 24.0)) tractCm = tractConf = 0.0;
    tractConf = std::clamp(tractConf, 0.0, 1e7);
    speakerConf = std::clamp(speakerConf, 0.0, 1e7);
    get(j, "trayHintShown", trayHintShown);

    userPresets.clear();
    auto it = j.find("userPresets");
    if (it != j.end() && it->is_array()) {
        for (const auto& p : *it) {
            if (!p.is_object()) continue;
            UserPreset u;
            u.name = p.value("name", std::string());
            if (u.name.empty() || u.name.size() > 120) continue;
            u.description = p.value("description", std::string("Ваш пресет"));
            u.values = voiceFromJson(p.value("values", json::object()));
            userPresets.push_back(std::move(u));
        }
    }
}

void Config::save() const {
    json j;
    j["version"] = kAppVersion;
    j["enabled"] = enabled;
    j["preset"] = preset;
    j["voice"] = voiceToJson(voice);
    j["voiceModified"] = voiceModified;
    j["global"] = globalToJson(global);
    j["favorites"] = favorites;
    j["hotkeysFavoritesOnly"] = hotkeysFavoritesOnly;
    j["micDevice"] = micDevice;
    j["outputDevice"] = outputDevice;
    j["monitorDevice"] = monitorDevice;
    j["monitorEnabled"] = monitorEnabled;
    j["quality"] = quality;
    j["bufferMode"] = bufferMode;
    j["hkToggle"] = hkToJson(hkToggle);
    j["hkNext"] = hkToJson(hkNext);
    j["hkPrev"] = hkToJson(hkPrev);
    j["hkMonitor"] = hkToJson(hkMonitor);
    j["hotkeysEnabled"] = hotkeysEnabled;
    j["soundCues"] = soundCues;
    j["startMinimized"] = startMinimized;
    j["closeToTray"] = closeToTray;
    j["autostart"] = autostart;
    j["restoreMicOnExit"] = restoreMicOnExit;
    j["manageDefaultMic"] = manageDefaultMic;
    j["previousDefaultMic"] = previousDefaultMic;
    j["firstRunDone"] = firstRunDone;
    j["speakerF0"] = speakerF0;
    j["speakerConf"] = speakerConf;
    j["tractCm"] = tractCm;
    j["tractConf"] = tractConf;
    j["trayHintShown"] = trayHintShown;
    json arr = json::array();
    for (const auto& u : userPresets)
        arr.push_back({{"name", u.name}, {"description", u.description}, {"values", voiceToJson(u.values)}});
    j["userPresets"] = arr;
    // replace invalid UTF-8 instead of throwing (names typed by the user)
    const std::string text = j.dump(2, ' ', false, json::error_handler_t::replace);
    if (!writeFileAtomic(configPath(), text)) logLine("could not save config.json");
}

}  // namespace vox
