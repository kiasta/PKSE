/**
 * Each table below is a straight transcription of the per-generation ribbon/mark
 * bit-flag definitions in PKHeX. A flag is stored as bit `bit` of the byte at
 * `byteOffset` in the decrypted entity buffer, read exactly like PKHeX's
 * FlagUtil.GetFlag(Data, byteOffset, bit) == ((Data[byteOffset] >> bit) & 1).
 *
 * Sources:
 *   - PKHeX.Core/PKM/Shared/G8PKM.cs  (PK8/PB8: SWSH + BDSP)  bytes 0x34..0x45
 *   - PKHeX.Core/PKM/PA8.cs           (Legends: Arceus)       bytes 0x34..0x45
 *   - PKHeX.Core/PKM/PK9.cs           (SV + Legends: Z-A)     bytes 0x34..0x45
 *   - PKHeX.Core/PKM/PB7.cs           (Let's Go: GG)          bytes 0x30..0x36
 *
 * G8PKM, PA8 and PK9 share the identical 0x34..0x45 HOME ribbon block, so a
 * single Gen-8/9 table serves SWSH, BDSP, PLA, SV and ZA. Gen 7 Let's Go has
 * its own smaller, mark-less block.
 *
 * Display names are derived from the PKHeX bool property names with the leading
 * "Ribbon"/"Mark" stripped and reflowed into human labels (RibbonChampionKalos
 * -> "Kalos Champion", RibbonEffort -> "Effort", RibbonMarkRare -> "Rare Mark").
 * Marks keep a trailing " Mark" so they read distinctly from ribbons.
 */

#include "Names/RibbonNames.h"
#include "Names/NameLanguage.h"

#include <cstddef>

namespace Names
{

    namespace
    {

        struct RibbonFlag
        {
            uint8_t byteOffset;
            uint8_t bit;
            const char *name;
        };

        const RibbonFlag GEN89_RIBBONS[] = {
            // 0x34 - RibbonChampionKalos .. RibbonEffort
            {0x34, 0, "Kalos Champion"},
            {0x34, 1, "Champion"}, // RibbonChampionG3 -- the games call it simply "Champion Ribbon"
            {0x34, 2, "Sinnoh Champion"},
            {0x34, 3, "Best Friends"},
            {0x34, 4, "Training"},
            {0x34, 5, "Skillful Battler"}, // RibbonBattlerSkillful
            {0x34, 6, "Expert Battler"},   // RibbonBattlerExpert
            {0x34, 7, "Effort"},

            // 0x35 - RibbonAlert .. RibbonGorgeous
            {0x35, 0, "Alert"},
            {0x35, 1, "Shock"},
            {0x35, 2, "Downcast"},
            {0x35, 3, "Careless"},
            {0x35, 4, "Relax"},
            {0x35, 5, "Snooze"},
            {0x35, 6, "Smile"},
            {0x35, 7, "Gorgeous"},

            // 0x36 - RibbonRoyal .. RibbonNational
            {0x36, 0, "Royal"},
            {0x36, 1, "Gorgeous Royal"},
            {0x36, 2, "Artist"},
            {0x36, 3, "Footprint"},
            {0x36, 4, "Record"},
            {0x36, 5, "Legend"},
            {0x36, 6, "Country"},
            {0x36, 7, "National"},

            // 0x37 - RibbonEarth .. RibbonSouvenir
            {0x37, 0, "Earth"},
            {0x37, 1, "World"},
            {0x37, 2, "Classic"},
            {0x37, 3, "Premier"},
            {0x37, 4, "Event"},
            {0x37, 5, "Birthday"},
            {0x37, 6, "Special"},
            {0x37, 7, "Souvenir"},

            // 0x38 - RibbonWishing .. RibbonChampionG6Hoenn
            {0x38, 0, "Wishing"},
            {0x38, 1, "Battle Champion"},        // RibbonChampionBattle
            {0x38, 2, "Regional Champion"},      // RibbonChampionRegional
            {0x38, 3, "National Champion"},      // RibbonChampionNational
            {0x38, 4, "World Champion"},         // RibbonChampionWorld
            {0x38, 5, "Contest Memory"},         // HasContestMemoryRibbon
            {0x38, 6, "Battle Memory"},          // HasBattleMemoryRibbon
            {0x38, 7, "Hoenn Champion"}, // RibbonChampionG6Hoenn -- ORAS shows "Hoenn Champion Ribbon"

            // 0x39 - RibbonContestStar .. RibbonBattleRoyale
            {0x39, 0, "Contest Star"},
            {0x39, 1, "Coolness Master"},   // RibbonMasterCoolness
            {0x39, 2, "Beauty Master"},     // RibbonMasterBeauty
            {0x39, 3, "Cuteness Master"},   // RibbonMasterCuteness
            {0x39, 4, "Cleverness Master"}, // RibbonMasterCleverness
            {0x39, 5, "Toughness Master"},  // RibbonMasterToughness
            {0x39, 6, "Alola Champion"},    // RibbonChampionAlola
            {0x39, 7, "Battle Royal Master"},

            // 0x3A - RibbonBattleTreeGreat .. RibbonMarkDusk
            {0x3A, 0, "Battle Tree Great"},
            {0x3A, 1, "Battle Tree Master"},
            {0x3A, 2, "Galar Champion"}, // RibbonChampionGalar
            {0x3A, 3, "Tower Master"},
            {0x3A, 4, "Master Rank"},
            {0x3A, 5, "Lunchtime Mark"},   // RibbonMarkLunchtime
            {0x3A, 6, "Sleepy-Time Mark"}, // RibbonMarkSleepyTime
            {0x3A, 7, "Dusk Mark"},        // RibbonMarkDusk

            // 0x3B - RibbonMarkDawn .. RibbonMarkSandstorm (all marks)
            {0x3B, 0, "Dawn Mark"},
            {0x3B, 1, "Cloudy Mark"},
            {0x3B, 2, "Rainy Mark"},
            {0x3B, 3, "Stormy Mark"},
            {0x3B, 4, "Snowy Mark"},
            {0x3B, 5, "Blizzard Mark"},
            {0x3B, 6, "Dry Mark"},
            {0x3B, 7, "Sandstorm Mark"},

            // (0x3C RibbonCountMemoryContest, 0x3D RibbonCountMemoryBattle:
            //  byte-value fields, not flags -- omitted.)

            // 0x40 - RibbonMarkMisty .. RibbonMarkAbsentMinded (all marks)
            {0x40, 0, "Misty Mark"},
            {0x40, 1, "Destiny Mark"},
            {0x40, 2, "Fishing Mark"},
            {0x40, 3, "Curry Mark"},
            {0x40, 4, "Uncommon Mark"},
            {0x40, 5, "Rare Mark"},
            {0x40, 6, "Rowdy Mark"},
            {0x40, 7, "Absent-Minded Mark"},

            // 0x41 - RibbonMarkJittery .. RibbonMarkAngry (all marks)
            {0x41, 0, "Jittery Mark"},
            {0x41, 1, "Excited Mark"},
            {0x41, 2, "Charismatic Mark"},
            {0x41, 3, "Calmness Mark"},
            {0x41, 4, "Intense Mark"},
            {0x41, 5, "Zoned-Out Mark"},
            {0x41, 6, "Joyful Mark"},
            {0x41, 7, "Angry Mark"},

            // 0x42 - RibbonMarkSmiley .. RibbonMarkScowling (all marks)
            {0x42, 0, "Smiley Mark"},
            {0x42, 1, "Teary Mark"},
            {0x42, 2, "Upbeat Mark"},
            {0x42, 3, "Peeved Mark"},
            {0x42, 4, "Intellectual Mark"},
            {0x42, 5, "Ferocious Mark"},
            {0x42, 6, "Crafty Mark"},
            {0x42, 7, "Scowling Mark"},

            // 0x43 - RibbonMarkKindly .. RibbonMarkThorny (all marks)
            {0x43, 0, "Kindly Mark"},
            {0x43, 1, "Flustered Mark"},
            {0x43, 2, "Pumped-Up Mark"},
            {0x43, 3, "Zero Energy Mark"},
            {0x43, 4, "Prideful Mark"},
            {0x43, 5, "Unsure Mark"},
            {0x43, 6, "Humble Mark"},
            {0x43, 7, "Thorny Mark"},

            // 0x44 - RibbonMarkVigor .. RibbonMarkItemfinder (mixed)
            {0x44, 0, "Vigor Mark"},      // RibbonMarkVigor
            {0x44, 1, "Slump Mark"},      // RibbonMarkSlump
            {0x44, 2, "Hisui"},           // RibbonHisui
            {0x44, 3, "Twinkling Star"},  // RibbonTwinklingStar
            {0x44, 4, "Paldea Champion"}, // RibbonChampionPaldea
            {0x44, 5, "Jumbo Mark"},      // RibbonMarkJumbo
            {0x44, 6, "Mini Mark"},       // RibbonMarkMini
            {0x44, 7, "Itemfinder Mark"}, // RibbonMarkItemfinder

            // 0x45 - RibbonMarkPartner .. RibbonPartner (mixed; bit 7 unused)
            {0x45, 0, "Partner Mark"},       // RibbonMarkPartner
            {0x45, 1, "Gourmand Mark"},      // RibbonMarkGourmand
            {0x45, 2, "Once-in-a-Lifetime"}, // RibbonOnceInALifetime
            {0x45, 3, "Alpha Mark"},         // RibbonMarkAlpha
            {0x45, 4, "Mightiest Mark"},     // RibbonMarkMightiest
            {0x45, 5, "Titan Mark"},         // RibbonMarkTitan
            {0x45, 6, "Partner"},            // RibbonPartner
        };

        const RibbonFlag GG_RIBBONS[] = {
            // 0x30 (RIB0)
            {0x30, 0, "Kalos Champion"},
            {0x30, 1, "Champion"},
            {0x30, 2, "Sinnoh Champion"},
            {0x30, 3, "Best Friends"},
            {0x30, 4, "Training"},
            {0x30, 5, "Skillful Battler"},
            {0x30, 6, "Expert Battler"},
            {0x30, 7, "Effort"},

            // 0x31 (RIB1)
            {0x31, 0, "Alert"},
            {0x31, 1, "Shock"},
            {0x31, 2, "Downcast"},
            {0x31, 3, "Careless"},
            {0x31, 4, "Relax"},
            {0x31, 5, "Snooze"},
            {0x31, 6, "Smile"},
            {0x31, 7, "Gorgeous"},

            // 0x32 (RIB2)
            {0x32, 0, "Royal"},
            {0x32, 1, "Gorgeous Royal"},
            {0x32, 2, "Artist"},
            {0x32, 3, "Footprint"},
            {0x32, 4, "Record"},
            {0x32, 5, "Legend"},
            {0x32, 6, "Country"},
            {0x32, 7, "National"},

            // 0x33 (RIB3)
            {0x33, 0, "Earth"},
            {0x33, 1, "World"},
            {0x33, 2, "Classic"},
            {0x33, 3, "Premier"},
            {0x33, 4, "Event"},
            {0x33, 5, "Birthday"},
            {0x33, 6, "Special"},
            {0x33, 7, "Souvenir"},

            // 0x34 (RIB4)
            {0x34, 0, "Wishing"},
            {0x34, 1, "Battle Champion"},
            {0x34, 2, "Regional Champion"},
            {0x34, 3, "National Champion"},
            {0x34, 4, "World Champion"},
            {0x34, 5, "Contest Memory"},
            {0x34, 6, "Battle Memory"},
            {0x34, 7, "Hoenn Champion"},

            // 0x35 (RIB5)
            {0x35, 0, "Contest Star"},
            {0x35, 1, "Coolness Master"},
            {0x35, 2, "Beauty Master"},
            {0x35, 3, "Cuteness Master"},
            {0x35, 4, "Cleverness Master"},
            {0x35, 5, "Toughness Master"},
            {0x35, 6, "Alola Champion"},
            {0x35, 7, "Battle Royal Master"},

            // 0x36 (RIB6) - only bits 0-1 are defined
            {0x36, 0, "Battle Tree Great"},
            {0x36, 1, "Battle Tree Master"},
        };

        const RibbonFlag FRLG_RIBBONS[] = {
            {0x4D, 7, "Champion"},          // bit 15
            {0x4E, 0, "Winning"},           // bit 16
            {0x4E, 1, "Victory"},           // bit 17
            {0x4E, 2, "Artist"},            // bit 18
            {0x4E, 3, "Effort"},            // bit 19
            {0x4E, 4, "Battle Champion"},   // bit 20
            {0x4E, 5, "Regional Champion"}, // bit 21
            {0x4E, 6, "National Champion"}, // bit 22
            {0x4E, 7, "Country"},           // bit 23
            {0x4F, 0, "National"},          // bit 24
            {0x4F, 1, "Earth"},             // bit 25
            {0x4F, 2, "World"},             // bit 26
        };

        // The five 3-bit contest ribbons, in bit order (Cool at bits 0-2, etc).
        const char *const G3_CONTEST[] = {"Cool", "Beauty", "Cute", "Smart", "Tough"};
        // Level 0 = never won. 1-4 are the contest ranks; the field stores only the
        // HIGHEST rank reached, so that is the one ribbon we report.
        const char *const G3_CONTEST_RANK[] = {nullptr, "", " Super", " Hyper", " Master"};

        /// The active language's wording for a ribbon PKSE named in English. One place, so the
        /// flag collector and the Gen 3 contest composer cannot drift apart.
        std::string localize(const std::string &englishName)
        {
            const size_t languageIndex = displayLanguageIndex();
            if (languageIndex == LANGUAGE_INDEX_ENGLISH)
                return englishName;
            const char *localized = getRibbonNameLocalized(englishName.c_str(), languageIndex);
            return (localized != nullptr && localized[0] != '\0') ? std::string(localized) : englishName;
        }

        std::vector<std::string> collect(const uint8_t *data, const RibbonFlag *table, size_t count)
        {
            std::vector<std::string> out;
            for (size_t i = 0; i < count; ++i)
            {
                const RibbonFlag &f = table[i];
                if ((data[f.byteOffset] >> f.bit) & 1)
                    out.emplace_back(localize(f.name));
            }
            return out;
        }

        std::vector<std::string> collectG3(const uint8_t *data)
        {
            const uint32_t rib = static_cast<uint32_t>(data[0x4C]) | (static_cast<uint32_t>(data[0x4D]) << 8) | (static_cast<uint32_t>(data[0x4E]) << 16) | (static_cast<uint32_t>(data[0x4F]) << 24);
            std::vector<std::string> out;
            for (int i = 0; i < 5; ++i)
            {
                const uint32_t lvl = (rib >> (i * 3)) & 7;
                // 0 = not won; 5-7 are undefined
                if (lvl == 0 || lvl > 4) continue;
                out.emplace_back(localize(std::string(G3_CONTEST[i]) + " Ribbon" + G3_CONTEST_RANK[lvl]));
            }
            const std::vector<std::string> flags = collect(data, FRLG_RIBBONS, sizeof(FRLG_RIBBONS) / sizeof(FRLG_RIBBONS[0]));
            out.insert(out.end(), flags.begin(), flags.end());
            return out;
        }

    }

    std::vector<std::string> getMonRibbons(const uint8_t *entityData, Enums::GameVersion group)
    {
        if (entityData == nullptr)
            return {};

        using GV = Enums::GameVersion;
        switch (group)
        {
        // FireRed/LeafGreen (PK3) -- one u32 at 0x4C, mixed levels + flags.
        case GV::FR:
        case GV::LG:
        case GV::FRLG:
            return collectG3(entityData);

        // Let's Go Pikachu/Eevee (PB7)
        case GV::GP:
        case GV::GE:
        case GV::GG:
            return collect(entityData, GG_RIBBONS, sizeof(GG_RIBBONS) / sizeof(GG_RIBBONS[0]));

        // Every Gen 8/9 HOME-format game shares the same 0x34..0x45 block.
        case GV::SW:
        case GV::SH: // Shield
        case GV::SWSH:
        case GV::BD: // Brilliant Diamond
        case GV::SP: // Shining Pearl
        case GV::BDSP:
        case GV::PLA: // Legends: Arceus
        case GV::SL:
        case GV::VL:  // Violet
        case GV::SV:
        case GV::ZA: // Legends: Z-A
            return collect(entityData, GEN89_RIBBONS, sizeof(GEN89_RIBBONS) / sizeof(GEN89_RIBBONS[0]));

        default:
            return {};
        }
    }
}
