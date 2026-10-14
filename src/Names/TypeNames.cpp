// AUTO-GENERATED from PKHeX's types text (id = array index).
// Source: PKHeX.Core/Resources/text/other/{lang}/text_Types_{lang}.txt, one table per language.
// Regenerate with tools/gen_simplenames.py (see tools/pkhex_source.py).
// Index 18 is STELLAR, which is a TERA type and nothing else: no species and no move
// has it, so Names::getTypeCount() still answers 18 and is what bounds personal.type1
// in the tables suite. Names::getTypeNameCount() answers 19 and is what bounds anything
// that names or draws a type, the Tera type included. Those are two different questions
// and conflating them would let a corrupt personal type of 18 pass a check that catches
// it today. NOTE THE STORED VALUE IS NOT 18: PK9 writes Stellar as 99 (PKHeX
// TeraTypeUtil.Stellar) -- Pokemon::teraTypeNameIndex() maps the byte onto this table.
#include <cstdint>
#include <cstddef>

#include "Names/NameLanguage.h"

namespace Names
{
    static const char* const TYPE_NAMES_JA[] = {
        "ノーマル",
        "かくとう",
        "ひこう",
        "どく",
        "じめん",
        "いわ",
        "むし",
        "ゴースト",
        "はがね",
        "ほのお",
        "みず",
        "くさ",
        "でんき",
        "エスパー",
        "こおり",
        "ドラゴン",
        "あく",
        "フェアリー",
        "ステラ",
    };
    static const char* const TYPE_NAMES_EN[] = {
        "Normal",
        "Fighting",
        "Flying",
        "Poison",
        "Ground",
        "Rock",
        "Bug",
        "Ghost",
        "Steel",
        "Fire",
        "Water",
        "Grass",
        "Electric",
        "Psychic",
        "Ice",
        "Dragon",
        "Dark",
        "Fairy",
        "Stellar",
    };
    static const char* const TYPE_NAMES_FR[] = {
        "Normal",
        "Combat",
        "Vol",
        "Poison",
        "Sol",
        "Roche",
        "Insecte",
        "Spectre",
        "Acier",
        "Feu",
        "Eau",
        "Plante",
        "Électrik",
        "Psy",
        "Glace",
        "Dragon",
        "Ténèbres",
        "Fée",
        "Stellaire",
    };
    static const char* const TYPE_NAMES_IT[] = {
        "Normale",
        "Lotta",
        "Volante",
        "Veleno",
        "Terra",
        "Roccia",
        "Coleottero",
        "Spettro",
        "Acciaio",
        "Fuoco",
        "Acqua",
        "Erba",
        "Elettro",
        "Psico",
        "Ghiaccio",
        "Drago",
        "Buio",
        "Folletto",
        "Astrale",
    };
    static const char* const TYPE_NAMES_DE[] = {
        "Normal",
        "Kampf",
        "Flug",
        "Gift",
        "Boden",
        "Gestein",
        "Käfer",
        "Geist",
        "Stahl",
        "Feuer",
        "Wasser",
        "Pflanze",
        "Elektro",
        "Psycho",
        "Eis",
        "Drache",
        "Unlicht",
        "Fee",
        "Stellar",
    };
    static const char* const TYPE_NAMES_ES[] = {
        "Normal",
        "Lucha",
        "Volador",
        "Veneno",
        "Tierra",
        "Roca",
        "Bicho",
        "Fantasma",
        "Acero",
        "Fuego",
        "Agua",
        "Planta",
        "Eléctrico",
        "Psíquico",
        "Hielo",
        "Dragón",
        "Siniestro",
        "Hada",
        "Astral",
    };
    static const char* const TYPE_NAMES_KO[] = {
        "노말",
        "격투",
        "비행",
        "독",
        "땅",
        "바위",
        "벌레",
        "고스트",
        "강철",
        "불꽃",
        "물",
        "풀",
        "전기",
        "에스퍼",
        "얼음",
        "드래곤",
        "악",
        "페어리",
        "스텔라",
    };
    static const char* const TYPE_NAMES_ZH_HANS[] = {
        "一般",
        "格斗",
        "飞行",
        "毒",
        "地面",
        "岩石",
        "虫",
        "幽灵",
        "钢",
        "火",
        "水",
        "草",
        "电",
        "超能力",
        "冰",
        "龙",
        "恶",
        "妖精",
        "星晶",
    };
    static const char* const TYPE_NAMES_ZH_HANT[] = {
        "一般",
        "格鬥",
        "飛行",
        "毒",
        "地面",
        "岩石",
        "蟲",
        "幽靈",
        "鋼",
        "火",
        "水",
        "草",
        "電",
        "超能力",
        "冰",
        "龍",
        "惡",
        "妖精",
        "星晶",
    };

    /// Indexed by Names::displayLanguageIndex(); see Names/NameLanguage.h.
    static const char* const* const TYPE_NAMES_BY_LANGUAGE[] = {
        TYPE_NAMES_JA,
        TYPE_NAMES_EN,
        TYPE_NAMES_FR,
        TYPE_NAMES_IT,
        TYPE_NAMES_DE,
        TYPE_NAMES_ES,
        TYPE_NAMES_KO,
        TYPE_NAMES_ZH_HANS,
        TYPE_NAMES_ZH_HANT,
    };
    static_assert(sizeof(TYPE_NAMES_BY_LANGUAGE) / sizeof(TYPE_NAMES_BY_LANGUAGE[0]) == LANGUAGE_COUNT, "TYPE_NAMES_BY_LANGUAGE must carry one table per language");

    const char *getTypeName(uint8_t id)
    {
        constexpr size_t count = sizeof(TYPE_NAMES_EN) / sizeof(TYPE_NAMES_EN[0]);
        if (id >= count) return "???";
        return TYPE_NAMES_BY_LANGUAGE[displayLanguageIndex()][id];
    }
}
