#include "Widgets.h"

#include "Platform.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vox::ui {

Fonts g_fonts;
float g_scale = 1.0f;

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static std::string fontPath(const char* file) {
    wchar_t win[MAX_PATH];
    UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    return narrow(std::wstring(win, n)) + "\\Fonts\\" + file;
}

static bool exists(const std::string& p) { return GetFileAttributesW(widen(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

void applyTheme(float scale) {
    g_scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    static ImVector<ImWchar> textRanges, symRanges;
    textRanges.clear();
    symRanges.clear();
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        b.AddText("—–…«»№•·≈→←↑↓×");
        b.BuildRanges(&textRanges);
    }
    {
        ImFontGlyphRangesBuilder b;
        b.AddText("★☆◀▶●○✓✕↺⚠");
        b.BuildRanges(&symRanges);
    }

    std::string regular = fontPath("segoeui.ttf");
    std::string bold = fontPath("seguisb.ttf");   // Segoe UI Semibold
    const std::string symbols = fontPath("seguisym.ttf");
    if (!exists(regular)) regular = fontPath("arial.ttf");
    if (!exists(bold)) bold = fontPath("segoeuib.ttf");
    if (!exists(bold)) bold = regular;
    const bool haveText = exists(regular);
    const bool haveSym = exists(symbols);

    auto add = [&](const std::string& file, float px) -> ImFont* {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        ImFont* f = haveText ? io.Fonts->AddFontFromFileTTF(file.c_str(), px * scale, &cfg, textRanges.Data)
                             : io.Fonts->AddFontDefault();
        if (haveSym && haveText) {
            ImFontConfig m;
            m.MergeMode = true;
            m.OversampleH = 2;
            io.Fonts->AddFontFromFileTTF(symbols.c_str(), px * scale, &m, symRanges.Data);
        }
        return f;
    };
    g_fonts.body = add(regular, 15.0f);
    g_fonts.bold = add(bold, 15.0f);
    g_fonts.small = add(regular, 13.0f);
    g_fonts.title = add(bold, 28.0f);
    g_fonts.huge = add(bold, 17.0f);
    io.FontDefault = g_fonts.body;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(16, 14);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 8;
    st.GrabMinSize = 16;
    st.WindowRounding = 0;
    st.ChildRounding = 14;
    st.FrameRounding = 10;
    st.PopupRounding = 12;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 8;
    st.TabRounding = 8;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.ScaleAllSizes(scale);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = col(kText);
    c[ImGuiCol_TextDisabled] = col(kDim);
    c[ImGuiCol_WindowBg] = col(kBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col(kPanel);
    c[ImGuiCol_Border] = col(kBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = col(kPanel2);
    c[ImGuiCol_FrameBgHovered] = col(kPanel3);
    c[ImGuiCol_FrameBgActive] = col(kPanel3);
    c[ImGuiCol_TitleBg] = col(kPanel);
    c[ImGuiCol_TitleBgActive] = col(kPanel);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = col(kPanel3);
    c[ImGuiCol_ScrollbarGrabHovered] = col(kOffHover);
    c[ImGuiCol_ScrollbarGrabActive] = col(kBorderStrong);
    c[ImGuiCol_CheckMark] = col(kAccent);
    c[ImGuiCol_SliderGrab] = col(kGrab);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_Button] = col(kPanel2);
    c[ImGuiCol_ButtonHovered] = col(kPanel3);
    c[ImGuiCol_ButtonActive] = col(kOffHover);
    c[ImGuiCol_Header] = col(kPanel3);
    c[ImGuiCol_HeaderHovered] = col(kPanel2);
    c[ImGuiCol_HeaderActive] = col(kPanel3);
    c[ImGuiCol_Separator] = col(kBorder);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    c[ImGuiCol_NavCursor] = col(kBorderStrong);
    c[ImGuiCol_TextSelectedBg] = ImVec4(1, 1, 1, 0.18f);
}

// ---------------------------------------------------------------- icons
// Thin line icons drawn as vectors (no icon font needed), ~1.5 px stroke.
void drawIcon(Icon icon, ImVec2 c, float s, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = std::max(1.0f, S(1.5f));
    const float h = s * 0.5f;
    switch (icon) {
        case Icon::Grid: {
            const float g = s * 0.12f, q = (s - g) * 0.5f;
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j) {
                    const ImVec2 a(c.x - h + i * (q + g), c.y - h + j * (q + g));
                    dl->AddRect(a, ImVec2(a.x + q, a.y + q), color, s * 0.08f, 0, t);
                }
            break;
        }
        case Icon::Wave: {
            const float hs[5] = {0.35f, 0.7f, 1.0f, 0.6f, 0.3f};
            for (int i = 0; i < 5; ++i) {
                const float x = c.x - h + s * (0.1f + 0.2f * i);
                dl->AddLine(ImVec2(x, c.y - h * hs[i]), ImVec2(x, c.y + h * hs[i]), color, t);
            }
            break;
        }
        case Icon::Sliders: {
            const float ys[3] = {-0.55f, 0.0f, 0.55f}, ks[3] = {0.3f, -0.35f, 0.1f};
            for (int i = 0; i < 3; ++i) {
                const float y = c.y + h * ys[i];
                dl->AddLine(ImVec2(c.x - h, y), ImVec2(c.x + h, y), color, t);
                dl->AddCircleFilled(ImVec2(c.x + h * ks[i], y), s * 0.13f, kSidebar, 12);
                dl->AddCircle(ImVec2(c.x + h * ks[i], y), s * 0.13f, color, 12, t);
            }
            break;
        }
        case Icon::Sparkle: {
            auto star = [&](ImVec2 p, float r) {
                dl->PathLineTo(ImVec2(p.x, p.y - r));
                dl->PathBezierQuadraticCurveTo(p, ImVec2(p.x + r, p.y));
                dl->PathBezierQuadraticCurveTo(p, ImVec2(p.x, p.y + r));
                dl->PathBezierQuadraticCurveTo(p, ImVec2(p.x - r, p.y));
                dl->PathBezierQuadraticCurveTo(p, ImVec2(p.x, p.y - r));
                dl->PathStroke(color, 0, t);
            };
            star(ImVec2(c.x - s * 0.1f, c.y + s * 0.08f), h * 0.85f);
            star(ImVec2(c.x + s * 0.32f, c.y - s * 0.3f), h * 0.35f);
            break;
        }
        case Icon::Mic: {
            const float w = s * 0.34f;
            dl->AddRect(ImVec2(c.x - w * 0.5f, c.y - h), ImVec2(c.x + w * 0.5f, c.y + s * 0.12f), color, w * 0.5f, 0, t);
            dl->PathArcTo(ImVec2(c.x, c.y - s * 0.02f), s * 0.33f, 0.0f, 3.14159f, 16);
            dl->PathStroke(color, 0, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.31f), ImVec2(c.x, c.y + h), color, t);
            break;
        }
        case Icon::Bolt: {
            const ImVec2 pts[6] = {{c.x + s * 0.08f, c.y - h}, {c.x - s * 0.3f, c.y + s * 0.06f}, {c.x - s * 0.02f, c.y + s * 0.06f},
                                   {c.x - s * 0.08f, c.y + h}, {c.x + s * 0.3f, c.y - s * 0.06f}, {c.x + s * 0.02f, c.y - s * 0.06f}};
            dl->AddPolyline(pts, 6, color, ImDrawFlags_Closed, t);
            break;
        }
        case Icon::Gear: {
            const int teeth = 8;
            for (int i = 0; i < teeth; ++i) {
                const float a = i * 2 * 3.14159f / teeth;
                dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.3f, c.y + std::sin(a) * s * 0.3f),
                            ImVec2(c.x + std::cos(a) * h, c.y + std::sin(a) * h), color, t * 1.6f);
            }
            dl->AddCircle(c, s * 0.32f, color, 20, t);
            dl->AddCircle(c, s * 0.13f, color, 12, t);
            break;
        }
        case Icon::Search:
            dl->AddCircle(ImVec2(c.x - s * 0.08f, c.y - s * 0.08f), s * 0.3f, color, 20, t);
            dl->AddLine(ImVec2(c.x + s * 0.14f, c.y + s * 0.14f), ImVec2(c.x + h, c.y + h), color, t);
            break;
        case Icon::Star:
        case Icon::StarFilled: {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                const float a = -3.14159f / 2 + i * 3.14159f / 5;
                const float r = (i % 2 == 0) ? h : h * 0.45f;
                pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r + s * 0.04f);
            }
            if (icon == Icon::StarFilled) dl->AddConcavePolyFilled(pts, 10, color);
            else dl->AddPolyline(pts, 10, color, ImDrawFlags_Closed, t);
            break;
        }
        case Icon::Back:
            dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), color, t);
            dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x - h + s * 0.32f, c.y - s * 0.32f), color, t);
            dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x - h + s * 0.32f, c.y + s * 0.32f), color, t);
            break;
        case Icon::Headphones:
            dl->PathArcTo(ImVec2(c.x, c.y + s * 0.05f), h * 0.85f, 3.14159f, 2 * 3.14159f, 16);
            dl->PathStroke(color, 0, t);
            dl->AddRectFilled(ImVec2(c.x - h * 0.95f, c.y + s * 0.05f), ImVec2(c.x - h * 0.5f, c.y + h), color, s * 0.06f);
            dl->AddRectFilled(ImVec2(c.x + h * 0.5f, c.y + s * 0.05f), ImVec2(c.x + h * 0.95f, c.y + h), color, s * 0.06f);
            break;
    }
}

// ---------------------------------------------------------------- widgets
bool toggleSwitch(const char* id, bool* value, const char* label) {
    ImGui::PushID(id);
    const float fh = ImGui::GetFrameHeight();
    const float h = fh * 0.72f, w = h * 1.85f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float textW = label ? ImGui::CalcTextSize(label).x : 0.0f;
    const float yOff = (fh - h) * 0.5f;
    const bool clicked = ImGui::InvisibleButton("sw", ImVec2(w + (label ? S(10) + textW : 0), fh));
    if (clicked) *value = !*value;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = *value ? (hovered ? kAccentHover : kAccent) : (hovered ? kOffHover : kOff);
    dl->AddRectFilled(ImVec2(p.x, p.y + yOff), ImVec2(p.x + w, p.y + yOff + h), bg, h * 0.5f);
    const float r = h * 0.5f - S(3);
    const float cx = *value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, p.y + yOff + h * 0.5f), r, *value ? kOnAccent : IM_COL32(200, 200, 200, 255), 24);
    if (label) dl->AddText(ImVec2(p.x + w + S(10), p.y + (fh - ImGui::GetTextLineHeight()) * 0.5f), kText, label);
    ImGui::PopID();
    return clicked;
}

bool navItem(const char* id, Icon icon, const char* label, bool selected, ImU32 dot) {
    ImGui::PushID(id);
    const float w = ImGui::GetContentRegionAvail().x, h = S(40);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("nav", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), selected ? kPanel3 : kPanel2, S(10));
    const ImU32 fg = selected ? kText : (hovered ? kText : kMuted);
    drawIcon(icon, ImVec2(p.x + S(22), p.y + h * 0.5f), S(17), fg);
    dl->AddText(ImVec2(p.x + S(44), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), fg, label);
    if (dot) dl->AddCircleFilled(ImVec2(p.x + w - S(16), p.y + h * 0.5f), S(4), dot, 12);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopID();
    return clicked;
}

bool gradientButton(const char* label, ImVec2 size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32_WHITE, S(10));
    const int v1 = dl->VtxBuffer.Size;
    const ImU32 a = held ? IM_COL32(225, 225, 225, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 b = held ? IM_COL32(190, 190, 190, 255) : (hovered ? IM_COL32(222, 222, 222, 255) : IM_COL32(206, 206, 206, 255));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, v1, p, ImVec2(p.x + size.x, p.y), a, b);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), kOnAccent, label, end);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

bool valueSlider(const char* id, double* v, double min, double max, double step, double def, float width) {
    ImGui::PushID(id);
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("sl", ImVec2(width, fh));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float pad = S(9);
    const float x0 = p.x + pad, x1 = p.x + width - pad, cy = p.y + fh * 0.5f;
    double nv = *v;
    if (active) {
        const float mx = ImGui::GetIO().MousePos.x;
        double f = std::clamp(double(mx - x0) / double(std::max(1.0f, x1 - x0)), 0.0, 1.0);
        nv = min + f * (max - min);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) nv += step * (ImGui::GetIO().MouseWheel > 0 ? 1 : -1);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) nv = def;
    nv = std::round((nv - min) / step) * step + min;
    nv = std::clamp(nv, min, max);
    const bool changed = std::abs(nv - *v) > step * 1e-6;
    if (changed) *v = nv;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = S(4);
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), kPanel3, th);
    const float frac = float((*v - min) / (max - min));
    // bipolar ranges (pitch −12…+12) fill from the centre/zero point
    const float zero = (min < 0 && max > 0) ? float(-min / (max - min)) : 0.0f;
    const float xa = x0 + (x1 - x0) * std::min(zero, frac), xb = x0 + (x1 - x0) * std::max(zero, frac);
    dl->AddRectFilled(ImVec2(xa, cy - th * 0.5f), ImVec2(xb, cy + th * 0.5f), kAccent, th);
    if (zero > 0) dl->AddCircleFilled(ImVec2(x0 + (x1 - x0) * zero, cy), S(2.5f), kDim, 8);
    const float gx = x0 + (x1 - x0) * frac;
    const float r = (active || hovered) ? S(8.5f) : S(7.5f);
    dl->AddCircleFilled(ImVec2(gx, cy + S(1)), r + S(1), IM_COL32(0, 0, 0, 90), 24);
    dl->AddCircleFilled(ImVec2(gx, cy), r, kGrab, 24);
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopID();
    return changed;
}

bool beginCard(const char* id, ImVec2 size, bool autoHeight) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, col(kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (autoHeight) flags |= ImGuiChildFlags_AutoResizeY;
    const bool open = ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar);
    return open;
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

bool pillButton(const char* label, ImVec2 size, ImU32 bg, ImU32 bgHover, ImU32 text, ImFont* font) {
    ImGui::PushStyleColor(ImGuiCol_Button, col(bg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(bgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(bgHover));
    ImGui::PushStyleColor(ImGuiCol_Text, col(text));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size.y * 0.5f);
    if (font) ImGui::PushFont(font);
    const bool r = ImGui::Button(label, size);
    if (font) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return r;
}

bool ghostButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, col(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(kPanel3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(kOffHover));
    ImGui::PushStyleColor(ImGuiCol_Border, col(kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return r;
}

bool accentButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, col(kAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(kAccentHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.85f, 0.85f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, col(kOnAccent));
    ImGui::PushFont(g_fonts.bold);
    const bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return r;
}

bool segmented(const char* id, const char* const* items, int count, int* current) {
    ImGui::PushID(id);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), 0));
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine();
        const bool sel = (*current == i);
        ImGui::PushStyleColor(ImGuiCol_Button, col(sel ? kAccent : kPanel2));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(sel ? kAccentHover : kPanel3));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(sel ? kAccentHover : kOffHover));
        ImGui::PushStyleColor(ImGuiCol_Text, col(sel ? kOnAccent : kText));
        ImGui::PushID(i);
        if (ImGui::Button(items[i]) && !sel) {
            *current = i;
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

void levelMeter(float peak, float width, float height, float markerDb) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), kPanel3, height * 0.5f);
    float frac = 0.0f;
    if (peak > 1e-6f) frac = std::clamp((20.0f * std::log10(peak) + 72.0f) / 72.0f, 0.0f, 1.0f);
    const ImU32 c = frac < 0.83f ? kAccent : (frac < 0.96f ? kWarn : kErr);
    if (frac > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + std::max(height, width * frac), p.y + height), c, height * 0.5f);
    if (markerDb > -100.0f) {
        const float m = std::clamp((markerDb + 72.0f) / 72.0f, 0.0f, 1.0f);
        const float x = p.x + width * m;
        dl->AddLine(ImVec2(x, p.y - S(3)), ImVec2(x, p.y + height + S(3)), kWarn, S(2));
    }
    ImGui::Dummy(ImVec2(width, height));
}

void textColored(ImU32 c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(c));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void textWrappedColored(ImU32 c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(c));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void sectionTitle(const char* text) {
    ImGui::PushFont(g_fonts.bold);
    textColored(kText, text);
    ImGui::PopFont();
}

void hintTooltip(const char* text) {
    if (!text || !*text) return;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(9)));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(340));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }
}

void statusBadge(const char* text, ImU32 bg, ImU32 fg, float size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), bg, S(9));
    ImGui::PushFont(g_fonts.bold);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), fg, text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(size, size));
}

}  // namespace vox::ui
