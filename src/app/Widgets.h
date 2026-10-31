// Theme (Windows 11 dark, violet accent) and custom ImGui widgets.
#pragma once

#include "imgui/imgui.h"

#include <string>

namespace vox::ui {

// Monochrome Windows-11-style palette: near-black canvas, graphite cards,
// white as the only accent. Colour is reserved for status (on / warning / error).
inline const ImU32 kBg = IM_COL32(17, 17, 17, 255);          // main canvas
inline const ImU32 kSidebar = IM_COL32(24, 24, 24, 255);
inline const ImU32 kPanel = IM_COL32(26, 26, 26, 255);       // cards
inline const ImU32 kPanel2 = IM_COL32(32, 32, 32, 255);      // hover / inputs
inline const ImU32 kPanel3 = IM_COL32(42, 42, 42, 255);      // tracks, pressed, selected
inline const ImU32 kBorder = IM_COL32(40, 40, 40, 255);
inline const ImU32 kBorderStrong = IM_COL32(70, 70, 70, 255);
inline const ImU32 kAccent = IM_COL32(236, 236, 236, 255);   // primary = white
inline const ImU32 kAccentHover = IM_COL32(255, 255, 255, 255);
inline const ImU32 kAccentSoft = IM_COL32(43, 43, 43, 255);
inline const ImU32 kOnAccent = IM_COL32(17, 17, 17, 255);    // text on white
inline const ImU32 kTeal = IM_COL32(236, 236, 236, 255);     // value labels
inline const ImU32 kText = IM_COL32(242, 242, 242, 255);
inline const ImU32 kMuted = IM_COL32(142, 142, 142, 255);
inline const ImU32 kDim = IM_COL32(94, 94, 94, 255);
inline const ImU32 kOk = IM_COL32(61, 214, 140, 255);
inline const ImU32 kOkHover = IM_COL32(52, 190, 124, 255);
inline const ImU32 kWarn = IM_COL32(245, 185, 66, 255);
inline const ImU32 kErr = IM_COL32(255, 107, 107, 255);
inline const ImU32 kOff = IM_COL32(52, 52, 52, 255);
inline const ImU32 kOffHover = IM_COL32(62, 62, 62, 255);
inline const ImU32 kGrab = IM_COL32(245, 245, 245, 255);

enum class Icon { Grid, Wave, Sliders, Sparkle, Mic, Bolt, Gear, Search, Star, StarFilled, Back, Headphones, Power };
void drawIcon(Icon icon, ImVec2 center, float size, ImU32 color);

ImVec4 col(ImU32 c);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* caption = nullptr;   // not "small": <rpcndr.h> #defines small as char
    ImFont* title = nullptr;
    ImFont* huge = nullptr;
};
extern Fonts g_fonts;
extern float g_scale;   // DPI scale (1.0 = 96 dpi)

inline float S(float px) { return px * g_scale; }

// Builds fonts (Segoe UI + symbol fallback, Cyrillic) and styles for the given DPI scale.
void applyTheme(float scale);

// ---- widgets
bool toggleSwitch(const char* id, bool* value, const char* label);
bool navItem(const char* id, Icon icon, const char* label, bool selected, ImU32 dot = 0);
bool gradientButton(const char* label, ImVec2 size);      // white → light-grey, dark text
bool valueSlider(const char* id, double* v, double min, double max, double step, double def, float width);
bool beginCard(const char* id, ImVec2 size = ImVec2(0, 0), bool autoHeight = true);
void endCard();
bool pillButton(const char* label, ImVec2 size, ImU32 bg, ImU32 bgHover, ImU32 text, ImFont* font = nullptr);
bool ghostButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool accentButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool segmented(const char* id, const char* const* items, int count, int* current);
void levelMeter(float peak, float width, float height, float markerDb = -999.0f);
void textColored(ImU32 c, const char* text);
void textWrappedColored(ImU32 c, const char* text);
void sectionTitle(const char* text);
void hintTooltip(const char* text);
void statusBadge(const char* text, ImU32 bg, ImU32 fg, float size);

}  // namespace vox::ui
