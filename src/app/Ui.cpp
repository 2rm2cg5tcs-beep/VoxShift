// All drawing code: sidebar navigation, pages, cards, modals.
#include "App.h"
#include "VbCable.h"
#include "Widgets.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vox {

using namespace ui;

namespace {

const char* kPageTitles[kPageCount] = {"Пресеты голоса", "Голос", "Тембр", "Эффекты",
                                       "Микрофон", "Быстрая настройка", "Настройки"};
const char* kPageSubtitles[kPageCount] = {
    "Выберите голос — он сразу начнёт работать во всех играх и программах.",
    "",
    "",
    "",
    "Очистка микрофона и громкость — общие для всех пресетов.",
    "Один раз — и изменённый голос слышен в любой игре, Discord и других программах.",
    "Устройства, качество, горячие клавиши и поведение программы.",
};

std::string fmtValue(const ParamDef& d, double v) {
    char b[64];
    switch (d.fmt) {
        case Fmt::Semitones:
            if (std::abs(v) < 0.05) return "0";
            std::snprintf(b, sizeof b, "%+.1f пт", v);
            return b;
        case Fmt::Percent:
            std::snprintf(b, sizeof b, "%d%%", int(std::lround(v * 100)));
            return b;
        case Fmt::PercentOff:
            if (v <= 0.004) return "выкл";
            std::snprintf(b, sizeof b, "%d%%", int(std::lround(v * 100)));
            return b;
        case Fmt::Db:
            if (std::abs(v) < 0.05) return "0 дБ";
            std::snprintf(b, sizeof b, "%+.1f дБ", v);
            return b;
        case Fmt::Hz:
        case Fmt::HzHighpass:
        case Fmt::HzLowpass:
            if (d.fmt == Fmt::HzHighpass && v <= 21) return "выкл";
            if (d.fmt == Fmt::HzLowpass && v >= 19900) return "выкл";
            if (v >= 1000) std::snprintf(b, sizeof b, "%.1f кГц", v / 1000.0);
            else std::snprintf(b, sizeof b, "%d Гц", int(std::lround(v)));
            return b;
        case Fmt::Ms:
            std::snprintf(b, sizeof b, "%d мс", int(std::lround(v * 1000)));
            return b;
        case Fmt::RateHz:
            std::snprintf(b, sizeof b, "%.1f Гц", v);
            return b;
        case Fmt::Drive:
            if (v <= 0.05) return "выкл";
            std::snprintf(b, sizeof b, "%.0f дБ", v);
            return b;
        case Fmt::Gate:
            if (v <= -89.5) return "выкл";
            std::snprintf(b, sizeof b, "%.0f дБ", v);
            return b;
        case Fmt::TargetHz:
            if (v < 80.0) return "выкл";
            std::snprintf(b, sizeof b, "%d Гц", int(std::lround(v)));
            return b;
    }
    return "";
}

void verticalGap(float px) { ImGui::Dummy(ImVec2(1, S(px))); }

float rightAlign(float width) {
    const float x = ImGui::GetWindowContentRegionMax().x - width;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), x));
    return x;
}

}  // namespace

// =====================================================================  root
void App::drawUi() {
    if (requestTab_ >= 0) {
        tab_ = requestTab_;
        requestTab_ = -1;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    const float sideW = S(268);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(kSidebar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("side", ImVec2(sideW, vp->WorkSize.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);   // rounding/padding apply to this child only, not to the cards inside
    uiSidebar(vp->WorkSize.y);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // hairline between sidebar and content
    ImGui::GetWindowDrawList()->AddLine(ImVec2(vp->WorkPos.x + sideW, vp->WorkPos.y),
                                        ImVec2(vp->WorkPos.x + sideW, vp->WorkPos.y + vp->WorkSize.y), kBorder);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(32), S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("main", ImVec2(0, vp->WorkSize.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    uiMain(vp->WorkSize.x - sideW, vp->WorkSize.y);
    ImGui::EndChild();
    ImGui::End();
    uiModals();
}

// =====================================================================  sidebar
void App::uiSidebar(float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;

    // ---- app "switcher" row
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rh = S(48);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), kPanel2, S(12));
        dl->AddRect(p, ImVec2(p.x + w, p.y + rh), kBorder, S(12));
        // monochrome logo tile
        const ImVec2 lp(p.x + S(8), p.y + S(8));
        const float ls = rh - S(16);
        dl->AddRectFilled(lp, ImVec2(lp.x + ls, lp.y + ls), kAccent, S(8));
        drawIcon(Icon::Wave, ImVec2(lp.x + ls * 0.5f, lp.y + ls * 0.5f), ls * 0.55f, kOnAccent);
        ImGui::PushFont(g_fonts.huge);
        dl->AddText(ImVec2(lp.x + ls + S(12), p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f), kText, "Pressvppp");
        ImGui::PopFont();
        const bool running = engine_.status().running;
        const ImU32 dot = !running ? kErr : (cfg_.enabled ? kOk : kDim);
        dl->AddCircleFilled(ImVec2(p.x + w - S(18), p.y + rh * 0.5f), S(4.5f), dot, 16);
        ImGui::Dummy(ImVec2(w, rh));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", !running ? "Звук не запущен" : (cfg_.enabled ? "Голос изменяется" : "Эффект выключен"));
    }
    verticalGap(12);
    if (navItem("search", Icon::Search, "Поиск пресета", false)) {
        tab_ = kPagePresets;
        focusSearch_ = true;
    }
    {
        const ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        ImGui::PushFont(g_fonts.caption);
        const char* k = "Ctrl F";
        dl->AddText(ImVec2(q.x - S(14) - ImGui::CalcTextSize(k).x, p.y + (q.y - p.y - ImGui::GetTextLineHeight()) * 0.5f), kDim, k);
        ImGui::PopFont();
    }
    auto separator = [&]() {
        verticalGap(8);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), kBorder);
        verticalGap(9);
    };
    separator();
    struct Nav { Page page; Icon icon; };
    const Nav top[] = {{kPagePresets, Icon::Grid}, {kPageVoice, Icon::Wave}, {kPageTone, Icon::Sliders}, {kPageFx, Icon::Sparkle}};
    for (const auto& n : top)
        if (navItem(kPageTitles[n.page], n.icon, n.page == kPagePresets ? "Пресеты" : kPageTitles[n.page], tab_ == n.page))
            tab_ = n.page;
    separator();
    const bool setupDone = cableFound_ && cableDefault_ == 1 && engine_.status().running;
    if (navItem("mic", Icon::Mic, kPageTitles[kPageMic], tab_ == kPageMic)) tab_ = kPageMic;
    if (navItem("setup", Icon::Bolt, kPageTitles[kPageSetup], tab_ == kPageSetup, setupDone ? 0 : kWarn)) {
        tab_ = kPageSetup;
        refreshDevices();
        refreshSetupStatus();
    }
    if (navItem("settings", Icon::Gear, kPageTitles[kPageSettings], tab_ == kPageSettings, hotkeyErrors_.empty() ? 0 : kWarn))
        tab_ = kPageSettings;

    // ---- bottom card: current voice + the big on/off button
    // (compact variant without the icon tile when the window is short)
    const float fullH = S(186), compactH = S(128);
    const float room = h - S(12) - ImGui::GetCursorPosY() - S(10);
    const bool compact = room < fullH;
    const float cardH = compact ? compactH : fullH;
    const float y = h - cardH - S(12);
    if (ImGui::GetCursorPosY() + S(10) < y) ImGui::SetCursorPosY(y);
    else verticalGap(10);
    if (beginCard("voicecard", ImVec2(w, cardH), false)) {
        if (!compact) {
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            const ImVec2 ip = ImGui::GetCursorScreenPos();
            const float is = S(38);
            cdl->AddRectFilled(ip, ImVec2(ip.x + is, ip.y + is), kPanel2, S(10));
            cdl->AddRect(ip, ImVec2(ip.x + is, ip.y + is), kBorder, S(10));
            drawIcon(cfg_.enabled ? Icon::Wave : Icon::Mic, ImVec2(ip.x + is * 0.5f, ip.y + is * 0.5f), S(18),
                     cfg_.enabled ? kText : kMuted);
            if (cfg_.enabled && engine_.status().running)
                cdl->AddCircleFilled(ImVec2(ip.x + is - S(2), ip.y + S(2)), S(4.5f), kOk, 12);
            ImGui::Dummy(ImVec2(is, is));
            verticalGap(10);
        }
        ImGui::PushFont(g_fonts.bold);
        ImGui::TextUnformatted(cfg_.enabled ? "Голос изменён" : "Обычный голос");
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::PushStyleColor(ImGuiCol_Text, col(kMuted));
        const std::string hk = cfg_.hotkeysEnabled && cfg_.hkToggle.valid() ? formatHotkey(cfg_.hkToggle.mods, cfg_.hkToggle.vk) : "";
        ImGui::Text("%s%s", cfg_.preset.c_str(), hk.empty() ? "" : ("  ·  " + hk).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        const float bh = S(40);
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - bh - S(16));
        if (gradientButton(cfg_.enabled ? "Выключить##tg" : "Включить##tg", ImVec2(ImGui::GetContentRegionAvail().x, bh)))
            toggleEnabled();
    }
    endCard();
}

// =====================================================================  main area
void App::uiMain(float /*w*/, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float footerH = S(46);

    // ---- breadcrumb row
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rh = ImGui::GetFrameHeight();
        const bool canBack = tab_ != kPagePresets;
        ImGui::InvisibleButton("back", ImVec2(S(22), rh));
        if (canBack && ImGui::IsItemClicked()) tab_ = kPagePresets;
        drawIcon(Icon::Back, ImVec2(p.x + S(9), p.y + rh * 0.5f), S(14), canBack ? kMuted : kDim);
        ImGui::SameLine(0, S(10));
        ImGui::AlignTextToFramePadding();
        textColored(kMuted, "Pressvppp");
        ImGui::SameLine(0, S(8));
        textColored(kDim, "/");
        ImGui::SameLine(0, S(8));
        ImGui::TextUnformatted(tab_ == kPagePresets ? "Пресеты" : kPageTitles[tab_]);

        // right side: monitor + preset arrows + quit
        const float arrowsW = S(38) * 3 + S(6) * 2;
        bool mon = cfg_.monitorEnabled;
        const float monW = S(44) + ImGui::CalcTextSize("Слышать себя").x;
        rightAlign(monW + S(18) + arrowsW);
        if (toggleSwitch("mon", &mon, "Слышать себя")) setMonitor(mon);
        hintTooltip("Выводит изменённый голос в ваши наушники, чтобы подобрать звучание. Нужны наушники — иначе будет эхо.");
        ImGui::SameLine(0, S(18));
        if (ghostButton("◀", ImVec2(S(38), rh))) stepPreset(-1);
        hintTooltip("Предыдущий пресет");
        ImGui::SameLine(0, S(6));
        if (ghostButton("▶", ImVec2(S(38), rh))) stepPreset(+1);
        hintTooltip("Следующий пресет");
        ImGui::SameLine(0, S(6));
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        if (ghostButton("##quit", ImVec2(S(38), rh))) openQuitModal_ = true;
        const bool qh = ImGui::IsItemHovered();
        drawIcon(Icon::Power, ImVec2(pp.x + S(19), pp.y + rh * 0.5f), S(15), qh ? kErr : kMuted);
        hintTooltip("Выйти полностью: программа закроется, звук станет как раньше");
    }
    verticalGap(18);

    // ---- title row
    const bool voicePage = tab_ == kPageVoice || tab_ == kPageTone || tab_ == kPageFx;
    {
        ImGui::PushFont(g_fonts.title);
        ImGui::TextUnformatted(voicePage ? cfg_.preset.c_str() : kPageTitles[tab_]);
        ImGui::PopFont();
        if (voicePage) {
            const PresetItem* cur = findPreset(cfg_.preset);
            const bool fav = std::find(cfg_.favorites.begin(), cfg_.favorites.end(), cfg_.preset) != cfg_.favorites.end();
            const float bw = S(118);
            const bool user = cur && !cur->builtin;
            rightAlign(S(40) + S(8) + bw * (user ? 3 : 2) + S(8) * (user ? 2 : 1));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            if (ghostButton("##fav", ImVec2(S(40), ImGui::GetFrameHeight()))) toggleFavorite();
            drawIcon(fav ? Icon::StarFilled : Icon::Star, ImVec2(sp.x + S(20), sp.y + ImGui::GetFrameHeight() * 0.5f), S(16),
                     fav ? kText : kMuted);
            hintTooltip(fav ? "Убрать из избранного" : "В избранное: такие пресеты переключаются горячими клавишами");
            ImGui::SameLine(0, S(8));
            ImGui::BeginDisabled(!modified_);
            if (ghostButton("↺  Сбросить", ImVec2(bw, 0))) selectPreset(cfg_.preset);
            ImGui::EndDisabled();
            ImGui::SameLine(0, S(8));
            if (accentButton("Сохранить как…", ImVec2(bw, 0))) {
                openSaveModal_ = true;
                std::snprintf(saveName_, sizeof saveName_, "%s", user ? cfg_.preset.c_str() : "");
            }
            if (user) {
                ImGui::SameLine(0, S(8));
                if (ghostButton("Удалить", ImVec2(bw, 0))) deleteUserPreset();
            }
        }
        const PresetItem* cur = findPreset(cfg_.preset);
        std::string sub = voicePage ? (cur ? cur->description : std::string()) : kPageSubtitles[tab_];
        if (voicePage && modified_) sub += sub.empty() ? "Изменён — не забудьте сохранить" : "   ·   изменён";
        if (!sub.empty()) textColored(kMuted, sub.c_str());
    }
    verticalGap(18);

    // ---- page content (scrolls)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("content", ImVec2(0, h - ImGui::GetCursorPosY() - footerH - S(8)), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    switch (tab_) {
        case kPagePresets: uiPresetsPage(); break;
        case kPageVoice:
            uiVoiceBasics();
            uiSliderCard("v2", "Характер", {P_Robot, P_Whisper, P_VibratoDepth, P_VibratoRate});
            break;
        case kPageTone:
            uiSliderCard("t1", "Эквалайзер", {P_Bass, P_Mid, P_Treble});
            uiSliderCard("t2", "Фильтры и громкость", {P_Highpass, P_Lowpass, P_Level});
            break;
        case kPageFx:
            uiSliderCard("f1", "Модуляция", {P_RingMix, P_RingFreq, P_TremoloDepth, P_TremoloRate});
            uiSliderCard("f2", "Искажение", {P_Distortion, P_Bitcrush, P_Lofi});
            uiSliderCard("f3", "Пространство", {P_Chorus, P_EchoMix, P_EchoTime, P_EchoFeedback, P_ReverbMix, P_ReverbSize});
            break;
        case kPageMic: uiMicTab(); break;
        case kPageSetup: uiSetupTab(); break;
        case kPageSettings: uiSettingsTab(); break;
        default: break;
    }
    if (voicePage) {
        ImGui::PushFont(g_fonts.caption);
        textColored(kDim, "Колесо мыши над ползунком — точная настройка, правый клик — вернуть по умолчанию.");
        ImGui::PopFont();
    }
    verticalGap(8);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ---- footer: meters + status
    ImGui::SetCursorPosY(h - footerH);
    const ImVec2 fp = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(fp.x - S(32), fp.y), ImVec2(fp.x + ImGui::GetContentRegionAvail().x + S(32), fp.y), kBorder);
    verticalGap(10);
    const EngineStatus& st = engine_.status();
    ImGui::PushFont(g_fonts.caption);
    const float mh = S(6), mw = S(110);
    auto meter = [&](const char* label, float v) {
        ImGui::AlignTextToFramePadding();
        textColored(kDim, label);
        ImGui::SameLine(0, S(8));
        const float y0 = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y0 + (ImGui::GetFrameHeight() - mh) * 0.5f);
        levelMeter(v, mw, mh);
        ImGui::SameLine(0, S(18));
        ImGui::SetCursorPosY(y0);
    };
    meter("МИК", engine_.inLevel());
    meter("ВЫХОД", st.running ? engine_.outLevel() : 0.0f);
    ImGui::AlignTextToFramePadding();
    if (now() < statusUntil_) textColored(statusColor_, status_.c_str());
    else if (st.running) textColored(kMuted, (st.micName + "  →  " + st.outputName).c_str());
    else textColored(kErr, st.error.empty() ? "Звук не запущен" : st.error.c_str());
    if (st.running) {
        char stats[96];
        std::snprintf(stats, sizeof stats, "≈ %.0f мс   ·   CPU %.1f%%", st.latencyMs, engine_.cpuLoad() * 100.0f);
        rightAlign(ImGui::CalcTextSize(stats).x);
        textColored(kDim, stats);
    }
    ImGui::PopFont();
}

// =====================================================================  presets grid
void App::uiPresetsPage() {
    ImGui::SetNextItemWidth(-1);
    if (focusSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) ImGui::SetKeyboardFocusHere();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40), S(10)));
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::InputTextWithHint("##search", "Поиск: демон, девушка, рация…", search_, sizeof search_);
    ImGui::PopStyleVar();
    drawIcon(Icon::Search, ImVec2(sp.x + S(20), sp.y + ImGui::GetItemRectSize().y * 0.5f), S(14), kMuted);
    verticalGap(6);

    std::string q = search_;
    // case-insensitive search for Cyrillic: compare lower-cased UTF-16
    std::wstring wq = widen(q);
    CharLowerBuffW(wq.data(), DWORD(wq.size()));
    auto matches = [&](const PresetItem& p) {
        if (wq.empty()) return true;
        std::wstring hay = widen(p.name + " " + p.description);
        CharLowerBuffW(hay.data(), DWORD(hay.size()));
        return hay.find(wq) != std::wstring::npos;
    };

    const float gap = S(10);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, int((avail + gap) / (S(200) + gap)));
    const float cw = std::floor((avail - gap * (cols - 1)) / cols), ch = S(84);
    bool any = false;
    for (const char* cat : presetCategories()) {
        std::vector<const PresetItem*> items;
        for (const auto& p : presets_)
            if (p.category == cat && matches(p)) items.push_back(&p);
        if (items.empty()) continue;
        any = true;
        verticalGap(4);
        ImGui::PushFont(g_fonts.caption);
        textColored(kMuted, cat);
        ImGui::PopFont();
        for (size_t i = 0; i < items.size(); ++i) {
            const PresetItem& p = *items[i];
            if (i % size_t(cols) != 0) ImGui::SameLine(0, gap);
            ImGui::PushID(p.name.c_str());
            const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + cw, a.y + ch);
            const bool clicked = ImGui::InvisibleButton("card", ImVec2(cw, ch));
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = p.name == cfg_.preset;
            const bool fav = std::find(cfg_.favorites.begin(), cfg_.favorites.end(), p.name) != cfg_.favorites.end();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, b, selected ? kPanel3 : (hovered ? kPanel2 : kPanel), S(12));
            dl->AddRect(a, b, selected ? kBorderStrong : kBorder, S(12), 0, selected ? S(1.5f) : 1.0f);
            // star (favourite) hit-area in the top-right corner
            const ImVec2 sc(b.x - S(20), a.y + S(20));
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool overStar = hovered && std::abs(mp.x - sc.x) < S(12) && std::abs(mp.y - sc.y) < S(12);
            if (fav || hovered) drawIcon(fav ? Icon::StarFilled : Icon::Star, sc, S(14), fav ? kText : (overStar ? kText : kDim));
            ImGui::PushFont(g_fonts.bold);
            dl->PushClipRect(a, ImVec2(b.x - S(34), b.y), true);
            dl->AddText(ImVec2(a.x + S(14), a.y + S(12)), kText, p.name.c_str());
            dl->PopClipRect();
            ImGui::PopFont();
            ImGui::PushFont(g_fonts.caption);
            dl->PushClipRect(a, ImVec2(b.x - S(8), b.y - S(6)), true);
            dl->AddText(g_fonts.caption, g_fonts.caption->FontSize, ImVec2(a.x + S(14), a.y + S(38)), kMuted,
                        p.description.c_str(), nullptr, cw - S(28));
            dl->PopClipRect();
            ImGui::PopFont();
            if (selected) dl->AddCircleFilled(ImVec2(b.x - S(20), b.y - S(16)), S(4), cfg_.enabled ? kOk : kDim, 12);
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked) {
                if (overStar) {
                    const std::string keep = cfg_.preset;
                    cfg_.preset = p.name;
                    toggleFavorite();
                    cfg_.preset = keep;
                } else {
                    selectPreset(p.name);
                }
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !overStar) tab_ = kPageVoice;
            ImGui::PopID();
        }
    }
    if (!any) textColored(kMuted, "Ничего не найдено.");
}

// =====================================================================  sliders
bool App::sliderRow(const ParamDef& def, double& value, int id) {
    ImGui::PushID(id);
    const float labelW = S(230), valueW = S(86);
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    const ImVec2 lp = ImGui::GetCursorPos();
    ImGui::TextUnformatted(def.label);
    bool changed = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && value != def.def) {
        value = def.def;
        changed = true;
    }
    hintTooltip(def.hint && *def.hint ? def.hint : nullptr);
    ImGui::SameLine(lp.x + labelW);
    changed |= valueSlider("s", &value, def.min, def.max, def.step, def.def, std::max(S(120), avail - labelW - valueW));
    ImGui::SameLine();
    const std::string txt = fmtValue(def, value);
    ImGui::PushFont(g_fonts.bold);
    const float tw = ImGui::CalcTextSize(txt.c_str()).x;
    ImGui::SetCursorPosX(lp.x + avail - tw);
    ImGui::AlignTextToFramePadding();
    textColored(value == def.def ? kMuted : kText, txt.c_str());
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}

void App::uiSliderCard(const char* id, const char* title, std::initializer_list<int> params) {
    if (beginCard(id, ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        sectionTitle(title);
        verticalGap(4);
        for (int pid : params) {
            if (sliderRow(kVoiceParams[size_t(pid)], voice_[size_t(pid)], pid)) voiceChanged();
        }
    }
    endCard();
}

void App::uiVoiceBasics() {
    if (beginCard("v1", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        sectionTitle("Основа");
        verticalGap(4);
        for (int pid : {P_TargetF0, P_Pitch, P_Formant, P_Breath, P_Dry})
            if (sliderRow(kVoiceParams[size_t(pid)], voice_[size_t(pid)], pid)) voiceChanged();
        verticalGap(2);
        // what the program has learned about the user's voice
        const double hz = engine_.speakerF0(), conf = engine_.speakerConfidence();
        const double shift = std::pow(2.0, voice_[P_Pitch] / 12.0);
        char b[200];
        if (conf >= 100.0) {
            const double out = (voice_[P_TargetF0] >= 80.0 ? voice_[P_TargetF0] : hz) * shift;
            std::snprintf(b, sizeof b, "Ваш голос ≈ %.0f Гц  →  в игре ≈ %.0f Гц", hz, out);
        } else {
            std::snprintf(b, sizeof b, "Скажите пару фраз — программа узнает высоту вашего голоса.");
        }
        ImGui::PushFont(g_fonts.caption);
        textColored(kMuted, b);
        ImGui::PopFont();
    }
    endCard();
}

// =====================================================================  microphone
void App::uiMicTab() {
    if (beginCard("mic1", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        sectionTitle("Очистка и громкость");
        verticalGap(4);
        bool changed = false;
        for (int i = 0; i < kGlobalParamCount; ++i)
            changed |= sliderRow(kGlobalParams[size_t(i)], cfg_.global[size_t(i)], 100 + i);
        if (changed) {
            engine_.setGlobal(cfg_.global);
            saveSoon();
        }
    }
    endCard();
    if (beginCard("mic2", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        sectionTitle("Уровень микрофона");
        textWrappedColored(kMuted, "Жёлтая черта — порог шумодава. В тишине полоска должна быть левее неё, "
                                   "когда говорите — правее. Иначе сдвиньте «Порог шумодава».");
        verticalGap(8);
        const double gate = cfg_.global[G_Gate];
        levelMeter(engine_.inLevel(), ImGui::GetContentRegionAvail().x, S(10), gate > -89.5 ? float(gate) : -999.0f);
        verticalGap(2);
    }
    endCard();
}

// =====================================================================  quick setup
void App::uiSetupTab() {
    const float w = ImGui::GetContentRegionAvail().x;
    if (!cfg_.firstRunDone) {
        if (beginCard("welcome", ImVec2(w, 0))) {
            ImGui::PushFont(g_fonts.huge);
            ImGui::TextUnformatted("Добро пожаловать в Pressvppp");
            ImGui::PopFont();
            textWrappedColored(kMuted, "Программа работает в фоне: закройте окно — она останется в трее возле часов, "
                                       "а голос будет меняться всё время, пока она запущена. В игре нажимайте свою "
                                       "клавишу разговора как обычно.");
            verticalGap(6);
            if (gradientButton("Понятно", ImVec2(S(140), S(36)))) {
                cfg_.firstRunDone = true;
                saveSoon();
            }
        }
        endCard();
    }

    auto step = [&](const char* id, int num, int state, const char* title, const std::string& text,
                    const char* primary, const char* secondary, int which) {
        if (beginCard(id, ImVec2(w, 0))) {
            const ImU32 bg = state == 1 ? kOk : (state == 0 ? IM_COL32(70, 32, 36, 255) : kPanel3);
            const ImU32 fg = state == 1 ? kOnAccent : (state == 0 ? kErr : kText);
            char badge[8];
            std::snprintf(badge, sizeof badge, "%s", state == 1 ? "✓" : (state == 0 ? "!" : ""));
            if (!badge[0]) std::snprintf(badge, sizeof badge, "%d", num);
            statusBadge(badge, bg, fg, S(34));
            ImGui::SameLine(0, S(14));
            ImGui::BeginGroup();
            ImGui::PushFont(g_fonts.bold);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - S(34) - S(14) - S(280) - S(36));
            textColored(state == 0 ? kText : kMuted, text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            const float bw = S(250);
            rightAlign(bw);
            ImGui::BeginGroup();
            if (primary) {
                ImGui::BeginDisabled((which == 1 && cableInstallBusy_) || (which == 2 && !cableFound_));
                if (accentButton(primary, ImVec2(bw, 0))) {
                    if (which == 1) installCable();
                    else if (which == 2) makeCableDefault();
                    else restartAudio();
                }
                ImGui::EndDisabled();
            }
            if (secondary && ghostButton(secondary, ImVec2(bw, 0))) {
                if (which == 1) openUrl(kVbCablePage);
                else if (which == 2) openSoundSettings(true);
            }
            ImGui::EndGroup();
        }
        endCard();
    };

    step("s1", 1, cableFound_ ? 1 : 0, "Виртуальный микрофон VB-Audio CABLE",
         cableFound_ ? "Установлен. Через него игры и программы слышат изменённый голос."
                     : "Не найден. Нажмите «Установить», подтвердите запрос Windows и нажмите «Install Driver», "
                       "затем перезагрузите компьютер.",
         cableFound_ ? nullptr : "Установить VB-Cable", "Сайт VB-Audio", 1);

    std::string t2;
    int s2 = -1;
    if (cableDefault_ == 1) {
        s2 = 1;
        t2 = "«CABLE Output» — микрофон Windows по умолчанию. Игры и программы возьмут изменённый голос сами.";
    } else if (cableFound_) {
        s2 = 0;
        t2 = "Сейчас программы слышат обычный микрофон. Одна кнопка — и они будут слышать Pressvppp. "
             "Ваш настоящий микрофон программа запомнит сама.";
    } else {
        t2 = "Станет доступно после установки VB-Cable.";
    }
    step("s2", 2, s2, "Сделать «CABLE Output» микрофоном по умолчанию", t2,
         cableDefault_ == 1 ? nullptr : "Сделать по умолчанию", "Настройки звука Windows", 2);

    const EngineStatus& st = engine_.status();
    char t3[512];
    if (st.running)
        std::snprintf(t3, sizeof t3, "Работает: %s → %s  (задержка ≈ %.0f мс)", st.micName.c_str(), st.outputName.c_str(), st.latencyMs);
    step("s3", 3, st.running ? 1 : 0, "Обработка голоса", st.running ? t3 : (st.error.empty() ? "Звук не запущен." : st.error),
         "Перезапустить звук", nullptr, 3);

    if (beginCard("note", ImVec2(w, 0))) {
        sectionTitle("Если программа слышит обычный голос");
        textWrappedColored(kMuted,
                           "В некоторых программах микрофон выбирается вручную (Steam → Настройки → Голос, Discord → "
                           "Голос и видео, настройки звука игры). Там выберите «CABLE Output (VB-Audio Virtual Cable)». "
                           "Когда эффект выключен, в «CABLE Output» идёт ваш обычный голос — микрофон не пропадает.");
    }
    endCard();
}

// =====================================================================  settings
void App::uiSettingsTab() {
    const float w = ImGui::GetContentRegionAvail().x;
    const float labelW = S(250);
    auto rowLabel = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelW + ImGui::GetStyle().WindowPadding.x);
    };
    auto deviceCombo = [&](const char* id, std::string& value, const std::vector<AudioDevice>& list, const char* autoLabel,
                           bool skipVirtual) {
        ImGui::SetNextItemWidth(std::min(S(460), w - labelW - S(40)));
        const std::string current = value.empty() ? std::string(autoLabel) : value;
        bool changed = false;
        if (ImGui::BeginCombo(id, current.c_str())) {
            if (ImGui::Selectable(autoLabel, value.empty())) {
                value.clear();
                changed = true;
            }
            for (const auto& d : list) {
                if (skipVirtual && d.isVirtual) continue;
                if (ImGui::Selectable(d.name.c_str(), value == d.name)) {
                    value = d.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (changed) {
            saveSoon();
            restartAudio();
        }
    };

    if (beginCard("dev", ImVec2(w, 0))) {
        sectionTitle("Звуковые устройства");
        verticalGap(4);
        rowLabel("Микрофон (настоящий)");
        deviceCombo("##mic", cfg_.micDevice, mics_, "Автоматически (системный микрофон)", false);
        rowLabel("Вывод изменённого голоса");
        deviceCombo("##out", cfg_.outputDevice, outputs_, "Автоматически (CABLE Input)", false);
        rowLabel("Наушники для «Слышать себя»");
        deviceCombo("##mon", cfg_.monitorDevice, outputs_, "Автоматически (устройство по умолчанию)", true);
        verticalGap(4);
        ImGui::SetCursorPosX(labelW + ImGui::GetStyle().WindowPadding.x);
        if (ghostButton("Обновить список")) refreshDevices();
        ImGui::SameLine();
        if (accentButton("Перезапустить звук")) restartAudio();
    }
    endCard();

    if (beginCard("q", ImVec2(w, 0))) {
        sectionTitle("Качество и задержка");
        verticalGap(4);
        rowLabel("Обработка голоса");
        const char* q[] = {"Баланс — 32 мс", "Максимальное качество — 43 мс"};
        if (segmented("quality", q, 2, &cfg_.quality)) {
            saveSoon();
            restartAudio();
        }
        rowLabel("Аудиобуфер");
        const char* b[] = {"Быстрый 5 мс", "Обычный 10 мс", "Надёжный 20 мс"};
        if (segmented("buffer", b, 3, &cfg_.bufferMode)) {
            saveSoon();
            restartAudio();
        }
        ImGui::SetCursorPosX(labelW + ImGui::GetStyle().WindowPadding.x);
        ImGui::PushFont(g_fonts.caption);
        textColored(kDim, "Если слышны щелчки — выберите «Надёжный».");
        ImGui::PopFont();
    }
    endCard();

    if (beginCard("hk", ImVec2(w, 0))) {
        sectionTitle("Горячие клавиши — работают и в игре");
        verticalGap(4);
        Hotkey* keys[4] = {&cfg_.hkToggle, &cfg_.hkNext, &cfg_.hkPrev, &cfg_.hkMonitor};
        const char* names[4] = {"Включить / выключить голос", "Следующий пресет", "Предыдущий пресет", "Слышать себя"};
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            rowLabel(names[i]);
            const std::string label = formatHotkey(keys[i]->mods, keys[i]->vk);
            ImGui::PushFont(g_fonts.bold);
            ImGui::BeginDisabled();
            ImGui::Button(label.c_str(), ImVec2(S(200), 0));
            ImGui::EndDisabled();
            ImGui::PopFont();
            ImGui::SameLine();
            if (ghostButton("Изменить")) {
                capturingHotkey_ = i;
                captured_ = {};
                for (int id = 1; id <= 4; ++id) UnregisterHotKey(hwnd_, id);
            }
            ImGui::SameLine();
            if (ghostButton("✕", ImVec2(ImGui::GetFrameHeight(), 0))) {
                *keys[i] = {};
                applyHotkeys();
                saveSoon();
            }
            ImGui::PopID();
        }
        if (!hotkeyErrors_.empty()) textWrappedColored(kWarn, hotkeyErrors_.c_str());
    }
    endCard();

    if (beginCard("beh", ImVec2(w, 0))) {
        sectionTitle("Поведение");
        verticalGap(4);
        auto sw = [&](const char* id, bool& v, const char* label, const char* hint) {
            const bool changed = toggleSwitch(id, &v, label);
            hintTooltip(hint);
            if (changed) saveSoon();
            return changed;
        };
        if (sw("b1", cfg_.hotkeysEnabled, "Горячие клавиши включены", nullptr)) applyHotkeys();
        sw("b2", cfg_.hotkeysFavoritesOnly, "Клавишами переключать только избранные пресеты", nullptr);
        sw("b3", cfg_.soundCues, "Звуковой сигнал при переключении", "Короткий сигнал в наушники — понятно, что сработало, даже в игре.");
        if (sw("b4", cfg_.autostart, "Запускать вместе с Windows (в трее)", nullptr) && !setAutostart(cfg_.autostart))
            toast("Не удалось изменить автозапуск", kErr);
        sw("b5", cfg_.startMinimized, "Открывать свёрнутым в трей", nullptr);
        sw("b6", cfg_.closeToTray, "Крестик сворачивает в трей, а не закрывает", nullptr);
        sw("b7", cfg_.restoreMicOnExit, "При выходе вернуть прежний микрофон Windows",
           "После «Выйти полностью» игры и программы снова слышат ваш обычный микрофон. "
           "При следующем запуске Pressvppp снова сделает «CABLE Output» микрофоном по умолчанию.");
    }
    endCard();

    if (beginCard("about", ImVec2(w, 0))) {
        sectionTitle("О программе");
        textColored(kMuted, (std::string("Pressvppp ") + kAppVersion + "  ·  C++ / WASAPI").c_str());
        verticalGap(4);
        if (ghostButton("Папка настроек")) openPath(dataDir());
        ImGui::SameLine();
        if (ghostButton("Журнал работы")) openPath(dataDir() + L"\\pressvppp.log");
        ImGui::SameLine();
        if (ghostButton("Выйти полностью")) openQuitModal_ = true;
    }
    endCard();
}

// =====================================================================  modals
void App::uiModals() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (openSaveModal_) {
        ImGui::OpenPopup("save_preset");
        openSaveModal_ = false;
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    if (ImGui::BeginPopupModal("save_preset", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(g_fonts.huge);
        ImGui::TextUnformatted("Сохранить пресет");
        ImGui::PopFont();
        textColored(kMuted, "Текущие ползунки сохранятся в «Мои пресеты».");
        verticalGap(6);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint("##name", "Название", saveName_, sizeof saveName_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        verticalGap(8);
        const float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (gradientButton("Сохранить", ImVec2(bw, S(38))) || enter) {
            if (saveUserPreset(saveName_)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(10));
        if (ghostButton("Отмена", ImVec2(bw, S(38))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (openQuitModal_) {
        ImGui::OpenPopup("quit_confirm");
        openQuitModal_ = false;
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    if (ImGui::BeginPopupModal("quit_confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(g_fonts.huge);
        ImGui::TextUnformatted("Выйти из Pressvppp полностью?");
        ImGui::PopFont();
        textWrappedColored(kMuted, cfg_.restoreMicOnExit && cfg_.manageDefaultMic
                                       ? "Программа закроется и не останется в трее. Игры и программы снова будут слышать "
                                         "ваш обычный микрофон — как до Pressvppp."
                                       : "Программа закроется и не останется в трее. Изменение голоса прекратится.");
        verticalGap(10);
        const float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (gradientButton("Выйти", ImVec2(bw, S(38)))) {
            ImGui::CloseCurrentPopup();
            PostMessageW(hwnd_, WM_APP_QUIT, 0, 0);   // quit outside of the ImGui frame
        }
        ImGui::SameLine(0, S(10));
        if (ghostButton("Отмена", ImVec2(bw, S(38))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static bool hkOpen = false;
    if (capturingHotkey_ >= 0 && !hkOpen) {
        ImGui::OpenPopup("hotkey");
        hkOpen = true;
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(420), 0));
    if (ImGui::BeginPopupModal("hotkey", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        if (capturingHotkey_ < 0) {
            ImGui::CloseCurrentPopup();
            hkOpen = false;
        } else {
            ImGui::PushFont(g_fonts.huge);
            ImGui::TextUnformatted("Нажмите сочетание клавиш");
            ImGui::PopFont();
            textColored(kMuted, "Например Ctrl + Alt + V или F8.  Esc — отмена.");
            verticalGap(10);
            ImGui::PushFont(g_fonts.title);
            const std::string shown = captured_.valid() ? formatHotkey(captured_.mods, captured_.vk) : "…";
            ImGui::TextUnformatted(shown.c_str());
            ImGui::PopFont();
            verticalGap(6);
            if (captured_.valid()) {
                Hotkey* keys[4] = {&cfg_.hkToggle, &cfg_.hkNext, &cfg_.hkPrev, &cfg_.hkMonitor};
                for (int i = 0; i < 4; ++i)
                    if (i != capturingHotkey_ && *keys[i] == captured_) *keys[i] = {};
                *keys[capturingHotkey_] = captured_;
                capturingHotkey_ = -1;
                applyHotkeys();
                saveSoon();
                ImGui::CloseCurrentPopup();
                hkOpen = false;
            } else if (ghostButton("Отмена", ImVec2(-1, S(36)))) {
                capturingHotkey_ = -1;
                applyHotkeys();
                ImGui::CloseCurrentPopup();
                hkOpen = false;
            }
        }
        ImGui::EndPopup();
    } else if (hkOpen && capturingHotkey_ < 0) {
        hkOpen = false;
    }
    ImGui::PopStyleVar();
}

}  // namespace vox
