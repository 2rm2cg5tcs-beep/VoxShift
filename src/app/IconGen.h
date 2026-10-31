// Procedural app icon (rounded gradient square + equaliser bars).
#pragma once

#include "Platform.h"

#include <cstdint>
#include <vector>

namespace vox {

// RGBA, premultiplied = false, top-down rows.
std::vector<uint8_t> renderLogo(int size, bool active);
HICON createLogoIcon(int size, bool active);

}  // namespace vox
