#include "IconGen.h"

#include <algorithm>
#include <cmath>

namespace vox {

std::vector<uint8_t> renderLogo(int size, bool active) {
    std::vector<uint8_t> px(size_t(size) * size * 4, 0);
    // monochrome: light tile with dark bars when active, grey tile when the effect is off
    const float top[3] = {active ? 250.f : 120.f, active ? 250.f : 120.f, active ? 250.f : 120.f};
    const float bot[3] = {active ? 200.f : 90.f, active ? 200.f : 90.f, active ? 200.f : 90.f};
    const float bars[5] = {0.30f, 0.55f, 0.85f, 0.60f, 0.38f};
    const float bw = 0.095f, gap = 0.055f, total = 5 * bw + 4 * gap;
    const float radius = 0.22f;
    const int ss = 4;   // supersampling
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float cover = 0, white = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const float u = (x + (sx + 0.5f) / ss) / size, v = (y + (sy + 0.5f) / ss) / size;
                    // rounded square
                    const float dx = std::max({radius - u, u - (1 - radius), 0.0f});
                    const float dy = std::max({radius - v, v - (1 - radius), 0.0f});
                    if (dx * dx + dy * dy > radius * radius) continue;
                    cover += 1;
                    // bars
                    float bx = (1 - total) / 2;
                    for (float h : bars) {
                        const float bh = 0.62f * h, y0 = (1 - bh) / 2, r = bw / 2;
                        if (u >= bx && u <= bx + bw && v >= y0 && v <= y0 + bh) {
                            // rounded caps
                            const float cy = std::clamp(v, y0 + r, y0 + bh - r);
                            const float ddx = u - (bx + r), ddy = v - cy;
                            if (ddx * ddx + ddy * ddy <= r * r) white += 1;
                            break;
                        }
                        bx += bw + gap;
                    }
                }
            const float n = float(ss * ss);
            const float a = cover / n, w = cover > 0 ? white / cover : 0;
            const float t = float(y) / float(size - 1);
            uint8_t* p = &px[(size_t(y) * size + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const float g = top[c] * (1 - t) + bot[c] * t;
                p[c] = uint8_t(std::lround(g * (1 - w) + 17.f * w));
            }
            p[3] = uint8_t(std::lround(a * 255.f));
        }
    return px;
}

HICON createLogoIcon(int size, bool active) {
    const std::vector<uint8_t> rgba = renderLogo(size, active);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;   // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    auto* dst = static_cast<uint8_t*>(bits);
    for (size_t i = 0; i < size_t(size) * size; ++i) {
        dst[i * 4 + 0] = rgba[i * 4 + 2];
        dst[i * 4 + 1] = rgba[i * 4 + 1];
        dst[i * 4 + 2] = rgba[i * 4 + 0];
        dst[i * 4 + 3] = rgba[i * 4 + 3];
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace vox
