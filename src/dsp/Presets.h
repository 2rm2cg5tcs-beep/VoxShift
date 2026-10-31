#pragma once

#include "Params.h"

#include <string>
#include <utility>
#include <vector>

namespace vox {

struct BuiltinPreset {
    const char* name;
    const char* category;
    const char* description;
    std::vector<std::pair<VoiceParamId, double>> values;

    VoiceValues full() const {
        VoiceValues v = defaultVoice();
        for (auto& [id, val] : values) v[id] = val;
        return v;
    }
};

inline constexpr const char* kUserCategory = "Мои пресеты";
inline constexpr const char* kNeutralPreset = "Без изменений";
inline constexpr const char* kDefaultPreset = "Девушка";

const std::vector<const char*>& presetCategories();
const std::vector<BuiltinPreset>& builtinPresets();

inline double clampParam(const ParamDef& d, double v) {
    if (!(v == v)) return d.def;  // NaN
    return v < d.min ? d.min : (v > d.max ? d.max : v);
}

}  // namespace vox
