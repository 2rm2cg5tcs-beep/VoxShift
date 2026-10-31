// Built-in voices. Speech voices use auto pitch (P_TargetF0): the program
// learns the user's own pitch and moves it to the target, so they sound the
// same for a deep and for a high voice. Levels are loudness-matched on speech.
#include "Presets.h"

namespace vox {

const std::vector<const char*>& presetCategories() {
    static const std::vector<const char*> c = {"Основные", "Люди", "Персонажи", "Монстры", "Роботы", "Связь", "Места", "Приколы", "Мои пресеты"};
    return c;
}

const std::vector<BuiltinPreset>& builtinPresets() {
    static const std::vector<BuiltinPreset> list = {
        {"Без изменений", "Основные", "Ваш голос, только очищенный от шума.", {}},
        {"Глубокий бас", "Основные", "Ниже и бархатнее — уверенный голос.", {{P_Pitch, -2.5}, {P_Formant, -1.5}, {P_Bass, 3.0}, {P_Treble, -1.0}}},
        {"Радиоведущий", "Основные", "Плотный, тёплый голос диктора.", {{P_Pitch, -1.5}, {P_Formant, -1.0}, {P_Bass, 5.0}, {P_Mid, -2.0}, {P_Treble, 3.0}, {P_Level, 0.0}}},
        {"Чуть выше", "Основные", "Немного моложе и легче.", {{P_Pitch, 2.0}, {P_Formant, 1.0}, {P_Level, 0.5}}},
        {"Чуть ниже", "Основные", "Немного старше и солиднее.", {{P_Pitch, -2.0}, {P_Formant, -1.0}, {P_Level, 1.0}}},
        {"Чёткая речь", "Основные", "Подчёркнутая разборчивость для команды.", {{P_Bass, -3.0}, {P_Mid, 2.0}, {P_Treble, 4.0}, {P_Highpass, 110.0}}},
        {"Девушка", "Люди", "Из мужского голоса — естественный женский.", {{P_TargetF0, 210.0}, {P_TargetVtl, 14.4}, {P_Intonation, 1.1}}},
        {"Девушка (звонкая)", "Люди", "Молодой, светлый женский голос.", {{P_TargetF0, 240.0}, {P_TargetVtl, 14.0}, {P_Intonation, 1.2}, {P_Treble, 1.0}}},
        {"Девушка (мягкая)", "Люди", "Нежный женский голос с придыханием.", {{P_TargetF0, 195.0}, {P_TargetVtl, 14.6}, {P_Breath, 0.3}, {P_Treble, -1.5}}},
        {"Женщина", "Люди", "Взрослый женский голос, ниже и спокойнее.", {{P_TargetF0, 180.0}, {P_TargetVtl, 14.9}, {P_Intonation, 1.05}}},
        {"Парень", "Люди", "Из женского голоса — мужской.", {{P_TargetF0, 115.0}, {P_TargetVtl, 16.9}, {P_Intonation, 0.9}}},
        {"Мужчина (бас)", "Люди", "Низкий взрослый мужской голос.", {{P_TargetF0, 88.0}, {P_TargetVtl, 17.8}, {P_Intonation, 0.85}, {P_Bass, 1.5}}},
        {"Ребёнок", "Люди", "Голос маленького ребёнка.", {{P_TargetF0, 280.0}, {P_TargetVtl, 12.4}, {P_Intonation, 1.2}}},
        {"Аниме-девочка", "Персонажи", "Высокий звонкий мультяшный голос.", {{P_TargetF0, 300.0}, {P_TargetVtl, 13.2}, {P_Intonation, 1.3}, {P_Treble, 1.5}}},
        {"Старик", "Персонажи", "Дрожащий голос пожилого человека.", {{P_Pitch, -1.0}, {P_Formant, -0.5}, {P_VibratoDepth, 0.35}, {P_VibratoRate, 6.5}, {P_Bass, -4.0}, {P_Mid, 3.0}, {P_Highpass, 150.0}, {P_Level, 1.5}}},
        {"Великан", "Персонажи", "Огромный и медленный.", {{P_TargetF0, 70.0}, {P_TargetVtl, 20.0}, {P_Intonation, 0.8}, {P_Bass, 3.0}, {P_ReverbMix, 0.18}, {P_ReverbSize, 0.7}}},
        {"Гном", "Персонажи", "Маленький ворчун.", {{P_Pitch, 3.0}, {P_Formant, 5.0}, {P_Mid, 2.0}, {P_Level, -0.5}}},
        {"Гоблин", "Персонажи", "Пронзительный и противный.", {{P_Pitch, 5.0}, {P_Formant, 6.0}, {P_Mid, 5.0}, {P_Treble, 3.0}, {P_Distortion, 4.0}, {P_Level, -3.5}}},
        {"Орк", "Персонажи", "Грубый рычащий воин.", {{P_Pitch, -5.0}, {P_Formant, -3.0}, {P_Distortion, 12.0}, {P_Bass, 4.0}, {P_Mid, 2.0}, {P_Level, -4.5}}},
        {"Тролль", "Персонажи", "Тупой и тяжёлый.", {{P_Pitch, -8.0}, {P_Formant, -2.0}, {P_Bass, 5.0}, {P_Distortion, 6.0}, {P_Lowpass, 6000.0}, {P_Level, -2.0}}},
        {"Бурундук", "Персонажи", "Классический ускоренный голосок.", {{P_Pitch, 12.0}, {P_Formant, 12.0}, {P_Bass, -4.0}, {P_Level, -1.5}}},
        {"Гелий", "Персонажи", "Будто вдохнули шарик.", {{P_Pitch, 2.0}, {P_Formant, 7.0}, {P_Bass, -6.0}, {P_Level, 1.0}}},
        {"Мультяшка", "Персонажи", "Озорной мультперсонаж.", {{P_Pitch, 8.0}, {P_Formant, 6.0}, {P_VibratoDepth, 0.2}, {P_VibratoRate, 7.0}, {P_Treble, 2.0}}},
        {"Демон", "Монстры", "Низкий потусторонний голос с подложкой.", {{P_Pitch, -7.0}, {P_Formant, -4.0}, {P_Dry, 0.25}, {P_Distortion, 6.0}, {P_Bass, 4.0}, {P_ReverbMix, 0.22}, {P_ReverbSize, 0.6}, {P_Level, -2.0}}},
        {"Демон-дуэт", "Монстры", "Ваш голос + демон на октаву ниже.", {{P_Pitch, -12.0}, {P_Formant, -5.0}, {P_Dry, 0.9}, {P_Bass, 3.0}, {P_ReverbMix, 0.15}, {P_Level, -2.0}}},
        {"Монстр", "Монстры", "Рычащее чудовище.", {{P_Pitch, -10.0}, {P_Formant, -6.0}, {P_Distortion, 14.0}, {P_Bass, 5.0}, {P_Mid, 3.0}, {P_Level, -5.5}}},
        {"Зомби", "Монстры", "Хриплый, булькающий, мёртвый.", {{P_Pitch, -4.0}, {P_Formant, -2.0}, {P_Whisper, 0.35}, {P_Distortion, 9.0}, {P_VibratoDepth, 0.4}, {P_VibratoRate, 3.0}, {P_Mid, 4.0}, {P_Lowpass, 5000.0}, {P_Level, -0.5}}},
        {"Призрак", "Монстры", "Шепчущий дух из ниоткуда.", {{P_Pitch, 2.0}, {P_Formant, 1.0}, {P_Whisper, 0.85}, {P_Chorus, 0.4}, {P_ReverbMix, 0.55}, {P_ReverbSize, 0.85}, {P_EchoMix, 0.25}, {P_EchoTime, 0.32}, {P_EchoFeedback, 0.4}, {P_Level, 1.0}}},
        {"Вампир", "Монстры", "Аристократичный зловещий шёпот.", {{P_Pitch, -3.0}, {P_Formant, -1.0}, {P_Whisper, 0.25}, {P_ReverbMix, 0.3}, {P_ReverbSize, 0.75}, {P_Bass, 2.0}, {P_Level, 2.5}}},
        {"Голос из бездны", "Монстры", "Гулкий голос из глубины.", {{P_Pitch, -9.0}, {P_Formant, -6.0}, {P_ReverbMix, 0.6}, {P_ReverbSize, 0.95}, {P_EchoMix, 0.3}, {P_EchoTime, 0.45}, {P_EchoFeedback, 0.5}, {P_Lowpass, 5000.0}, {P_Bass, 5.0}, {P_Level, -3.0}}},
        {"Пришелец", "Монстры", "Инопланетная модуляция.", {{P_Pitch, 3.0}, {P_Formant, 2.0}, {P_RingMix, 0.65}, {P_RingFreq, 55.0}, {P_Chorus, 0.25}, {P_Level, 5.0}}},
        {"Посол галактики", "Монстры", "Вибрирующий космический голос.", {{P_Pitch, -2.0}, {P_Formant, 3.0}, {P_VibratoDepth, 0.6}, {P_VibratoRate, 9.0}, {P_RingMix, 0.3}, {P_RingFreq, 220.0}, {P_ReverbMix, 0.25}, {P_Level, 4.5}}},
        {"Паук", "Монстры", "Шипящий многоголосый кошмар.", {{P_Pitch, 4.0}, {P_Formant, -3.0}, {P_Whisper, 0.4}, {P_Chorus, 0.6}, {P_Distortion, 5.0}, {P_Level, 3.0}}},
        {"Робот", "Роботы", "Классический металлический робот.", {{P_Robot, 1.0}, {P_RingMix, 0.25}, {P_RingFreq, 30.0}, {P_Mid, 3.0}, {P_Level, 3.0}}},
        {"Андроид", "Роботы", "Человечный, но синтетический.", {{P_Robot, 0.55}, {P_Pitch, 1.0}, {P_Chorus, 0.2}, {P_Treble, 2.0}, {P_Level, 4.5}}},
        {"Киборг", "Роботы", "Наполовину человек — с перегрузом.", {{P_Robot, 0.6}, {P_Pitch, -2.0}, {P_Formant, -1.0}, {P_Distortion, 8.0}, {P_Bitcrush, 0.25}}},
        {"Боевой дроид", "Роботы", "Тонкий механический голос.", {{P_Robot, 0.8}, {P_Pitch, 3.0}, {P_Formant, 4.0}, {P_Highpass, 300.0}, {P_Lowpass, 6000.0}, {P_Mid, 4.0}, {P_Level, 0.5}}},
        {"Злой ИИ", "Роботы", "Холодный низкий машинный голос.", {{P_Robot, 0.7}, {P_Pitch, -5.0}, {P_Formant, -3.0}, {P_ReverbMix, 0.25}, {P_ReverbSize, 0.6}, {P_EchoMix, 0.15}, {P_EchoTime, 0.12}, {P_EchoFeedback, 0.3}, {P_Level, 5.0}}},
        {"Ассистент", "Роботы", "Слегка синтетический голос помощника.", {{P_Robot, 0.25}, {P_Chorus, 0.15}, {P_Treble, 2.0}, {P_Bass, -1.0}, {P_Level, 2.5}}},
        {"8-бит", "Роботы", "Ретро-приставка.", {{P_Pitch, 4.0}, {P_Formant, 3.0}, {P_Lofi, 0.65}, {P_Bitcrush, 0.55}, {P_Lowpass, 9000.0}}},
        {"Сломанный бот", "Роботы", "Глючит и хрустит.", {{P_Robot, 0.5}, {P_Bitcrush, 0.75}, {P_Lofi, 0.4}, {P_TremoloDepth, 0.6}, {P_TremoloRate, 13.0}, {P_Level, 6.0}}},
        {"Рация", "Связь", "Полевая рация с хрипом.", {{P_Highpass, 420.0}, {P_Lowpass, 3200.0}, {P_Distortion, 10.0}, {P_Mid, 6.0}, {P_Bitcrush, 0.2}, {P_Level, -3.5}}},
        {"Телефон", "Связь", "Старая телефонная линия.", {{P_Highpass, 380.0}, {P_Lowpass, 3400.0}, {P_Mid, 4.0}, {P_Distortion, 3.0}, {P_Level, 0.0}}},
        {"Старое радио", "Связь", "Ламповый приёмник 40-х.", {{P_Highpass, 500.0}, {P_Lowpass, 4000.0}, {P_Distortion, 7.0}, {P_Mid, 5.0}, {P_Bitcrush, 0.15}, {P_TremoloDepth, 0.08}, {P_TremoloRate, 2.0}, {P_Level, -1.5}}},
        {"Мегафон", "Связь", "Громкоговоритель на площади.", {{P_Highpass, 650.0}, {P_Lowpass, 5000.0}, {P_Distortion, 16.0}, {P_Mid, 8.0}, {P_EchoMix, 0.12}, {P_EchoTime, 0.09}, {P_EchoFeedback, 0.2}, {P_Level, -5.5}}},
        {"Пилот", "Связь", "Связь из кабины самолёта.", {{P_Highpass, 300.0}, {P_Lowpass, 3800.0}, {P_Distortion, 5.0}, {P_Mid, 3.0}, {P_Chorus, 0.15}}},
        {"Шлем", "Связь", "Голос внутри бронешлема.", {{P_Mid, 5.0}, {P_Lowpass, 7000.0}, {P_EchoMix, 0.25}, {P_EchoTime, 0.03}, {P_EchoFeedback, 0.55}, {P_Highpass, 180.0}}},
        {"Под водой", "Связь", "Буль-буль.", {{P_Lowpass, 1200.0}, {P_Chorus, 0.6}, {P_VibratoDepth, 0.5}, {P_VibratoRate, 2.0}, {P_Bass, 4.0}, {P_Level, 3.0}}},
        {"Пещера", "Места", "Каменные своды и эхо.", {{P_EchoMix, 0.35}, {P_EchoTime, 0.28}, {P_EchoFeedback, 0.45}, {P_ReverbMix, 0.4}, {P_ReverbSize, 0.8}, {P_Level, 0.5}}},
        {"Собор", "Места", "Огромное пространство.", {{P_ReverbMix, 0.6}, {P_ReverbSize, 0.97}, {P_Bass, 2.0}, {P_Level, -5.0}}},
        {"Стадион", "Места", "Голос диктора на арене.", {{P_Highpass, 200.0}, {P_Mid, 4.0}, {P_EchoMix, 0.3}, {P_EchoTime, 0.45}, {P_EchoFeedback, 0.3}, {P_ReverbMix, 0.3}, {P_ReverbSize, 0.9}}},
        {"Ванная", "Места", "Кафель и короткое эхо.", {{P_ReverbMix, 0.35}, {P_ReverbSize, 0.35}, {P_Treble, 2.0}}},
        {"Космос", "Места", "Бесконечное пространство.", {{P_Chorus, 0.35}, {P_EchoMix, 0.4}, {P_EchoTime, 0.6}, {P_EchoFeedback, 0.55}, {P_ReverbMix, 0.55}, {P_ReverbSize, 0.95}, {P_Level, -1.0}}},
        {"Пьяный", "Приколы", "Плывущий голос.", {{P_Pitch, -1.0}, {P_VibratoDepth, 1.2}, {P_VibratoRate, 1.3}, {P_Chorus, 0.3}, {P_Level, 2.5}}},
        {"Испуганный", "Приколы", "Дрожащий от страха.", {{P_Pitch, 3.0}, {P_Formant, 1.0}, {P_VibratoDepth, 0.7}, {P_VibratoRate, 10.0}, {P_TremoloDepth, 0.3}, {P_TremoloRate, 9.0}, {P_Level, 2.5}}},
        {"Хор", "Приколы", "Будто говорят трое.", {{P_Chorus, 1.0}, {P_Dry, 0.4}, {P_Pitch, 0.5}, {P_ReverbMix, 0.2}, {P_Level, 1.5}}},
        {"Злодей", "Приколы", "Пафосный голос главного злодея.", {{P_Pitch, -4.0}, {P_Formant, -2.0}, {P_Bass, 5.0}, {P_ReverbMix, 0.3}, {P_ReverbSize, 0.7}, {P_EchoMix, 0.12}, {P_EchoTime, 0.18}, {P_EchoFeedback, 0.25}, {P_Level, 0.5}}},
        {"Комментатор", "Приколы", "Громко и с эхом стадиона.", {{P_Pitch, -1.0}, {P_Mid, 4.0}, {P_Treble, 3.0}, {P_Distortion, 4.0}, {P_EchoMix, 0.2}, {P_EchoTime, 0.22}, {P_EchoFeedback, 0.25}, {P_Level, -0.5}}},
        {"Двойник", "Приколы", "Два ваших голоса — ваш и чуть выше.", {{P_Pitch, 4.0}, {P_Formant, 0.0}, {P_Dry, 1.0}, {P_Level, -2.0}}},
    };
    return list;
}

}  // namespace vox
