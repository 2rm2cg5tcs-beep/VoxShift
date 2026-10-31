// GENERATED from the reference implementation — keep in sync with Presets.cpp.
#pragma once

#include <array>
#include <cstddef>

namespace vox {

enum class Fmt { Semitones, Percent, PercentOff, Db, Hz, HzHighpass, HzLowpass, Ms, RateHz, Drive, Gate };
enum class Group { Voice, Tone, Fx, Mic };

struct ParamDef {
    const char* key;
    const char* label;
    double min, max, def, step;
    Group group;
    Fmt fmt;
    const char* hint;
};

enum VoiceParamId : int {
    P_Pitch,
    P_Formant,
    P_Robot,
    P_Whisper,
    P_VibratoDepth,
    P_VibratoRate,
    P_Dry,
    P_Bass,
    P_Mid,
    P_Treble,
    P_Highpass,
    P_Lowpass,
    P_Level,
    P_RingMix,
    P_RingFreq,
    P_Distortion,
    P_Bitcrush,
    P_Lofi,
    P_Chorus,
    P_TremoloDepth,
    P_TremoloRate,
    P_EchoMix,
    P_EchoTime,
    P_EchoFeedback,
    P_ReverbMix,
    P_ReverbSize,
    kVoiceParamCount
};

enum GlobalParamId : int {
    G_InputGain,
    G_Gate,
    G_Denoise,
    G_Compress,
    G_OutputVolume,
    G_MonitorVolume,
    kGlobalParamCount
};

inline constexpr std::array<ParamDef, kVoiceParamCount> kVoiceParams = {{
    {"pitch", "Высота голоса", -12.0, 12.0, 0.0, 0.5, Group::Voice, Fmt::Semitones, "Выше — тоньше голос, ниже — грубее. В полутонах (12 = октава)."},
    {"formant", "Тембр (форманты)", -12.0, 12.0, 0.0, 0.5, Group::Voice, Fmt::Semitones, "«Размер» горла. Вверх — ребёнок/девушка, вниз — великан."},
    {"robot", "Робот", 0.0, 1.0, 0.0, 0.01, Group::Voice, Fmt::PercentOff, "Металлический монотонный голос."},
    {"whisper", "Шёпот", 0.0, 1.0, 0.0, 0.01, Group::Voice, Fmt::PercentOff, "Убирает звонкость связок — получается шёпот/призрак."},
    {"vibrato_depth", "Дрожание голоса", 0.0, 2.0, 0.0, 0.05, Group::Voice, Fmt::Semitones, "Глубина вибрато в полутонах."},
    {"vibrato_rate", "Скорость дрожания", 0.5, 12.0, 5.0, 0.1, Group::Voice, Fmt::RateHz, ""},
    {"dry", "Оригинальный голос", 0.0, 1.0, 0.0, 0.01, Group::Voice, Fmt::PercentOff, "Подмешивает ваш настоящий голос — эффект «двух голосов»."},
    {"bass", "Низы", -15.0, 15.0, 0.0, 0.5, Group::Tone, Fmt::Db, ""},
    {"mid", "Середина", -15.0, 15.0, 0.0, 0.5, Group::Tone, Fmt::Db, ""},
    {"treble", "Верха", -15.0, 15.0, 0.0, 0.5, Group::Tone, Fmt::Db, ""},
    {"highpass", "Срез низких", 20.0, 1500.0, 20.0, 10.0, Group::Tone, Fmt::HzHighpass, "Убирает всё ниже частоты — «телефон», «рация»."},
    {"lowpass", "Срез высоких", 1000.0, 20000.0, 20000.0, 100.0, Group::Tone, Fmt::HzLowpass, "Убирает всё выше частоты — «под водой», «за стеной»."},
    {"level", "Громкость пресета", -20.0, 12.0, 0.0, 0.5, Group::Tone, Fmt::Db, ""},
    {"ring_mix", "Пришелец (кольц. модулятор)", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"ring_freq", "Частота модулятора", 10.0, 1500.0, 80.0, 5.0, Group::Fx, Fmt::Hz, ""},
    {"distortion", "Перегруз", 0.0, 40.0, 0.0, 1.0, Group::Fx, Fmt::Drive, ""},
    {"bitcrush", "Цифровой хруст", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"lofi", "8-бит (ретро)", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"chorus", "Хор / объём", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"tremolo_depth", "Тремоло", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"tremolo_rate", "Скорость тремоло", 1.0, 20.0, 6.0, 0.5, Group::Fx, Fmt::RateHz, ""},
    {"echo_mix", "Эхо", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"echo_time", "Задержка эха", 0.03, 1.0, 0.25, 0.01, Group::Fx, Fmt::Ms, ""},
    {"echo_feedback", "Повторы эха", 0.0, 0.9, 0.35, 0.01, Group::Fx, Fmt::Percent, ""},
    {"reverb_mix", "Реверберация", 0.0, 1.0, 0.0, 0.01, Group::Fx, Fmt::PercentOff, ""},
    {"reverb_size", "Размер помещения", 0.0, 1.0, 0.5, 0.01, Group::Fx, Fmt::Percent, ""},
}};

inline constexpr std::array<ParamDef, kGlobalParamCount> kGlobalParams = {{
    {"input_gain", "Усиление микрофона", -20.0, 20.0, 0.0, 0.5, Group::Mic, Fmt::Db, ""},
    {"gate", "Порог шумодава (гейт)", -90.0, -20.0, -60.0, 1.0, Group::Mic, Fmt::Gate, "Тише этого уровня — тишина. Отрезает клавиатуру и фон."},
    {"denoise", "Подавление шума", 0.0, 1.0, 0.35, 0.01, Group::Mic, Fmt::PercentOff, "Убирает постоянный шум: вентилятор, гул, шипение."},
    {"compress", "Выравнивание громкости", 0.0, 1.0, 0.5, 0.01, Group::Mic, Fmt::PercentOff, "Тихое громче, громкое тише — вас одинаково слышно."},
    {"output_volume", "Общая громкость", -20.0, 12.0, 0.0, 0.5, Group::Mic, Fmt::Db, ""},
    {"monitor_volume", "Громкость прослушки", -30.0, 6.0, -6.0, 0.5, Group::Mic, Fmt::Db, ""},
}};

using VoiceValues = std::array<double, kVoiceParamCount>;
using GlobalValues = std::array<double, kGlobalParamCount>;

inline VoiceValues defaultVoice() {
    VoiceValues v{};
    for (size_t i = 0; i < v.size(); ++i) v[i] = kVoiceParams[i].def;
    return v;
}
inline GlobalValues defaultGlobal() {
    GlobalValues v{};
    for (size_t i = 0; i < v.size(); ++i) v[i] = kGlobalParams[i].def;
    return v;
}

}  // namespace vox
