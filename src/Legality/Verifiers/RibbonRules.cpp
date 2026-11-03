// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ribbons/: RibbonRules.cs and the per-set parsers
// RibbonVerifier.cs runs (RibbonVerifierCommon3/4/6/7/8/9.cs, RibbonVerifierEvent3/4.cs,
// RibbonVerifierOnly3.cs, RibbonVerifierUnique3/4.cs).
#include "Legality/Verifiers/RibbonRules.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/EncounterStatic6.h"
#include "Legality/Encounters/EncounterStatic7.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/TemplateRibbons.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t PICHU_SPECIES = 172;
        constexpr uint16_t DITTO_SPECIES = 132;
        constexpr uint16_t UNOWN_SPECIES = 201;
        constexpr uint16_t GRENINJA_SPECIES = 658;
        constexpr uint16_t MAGEARNA_SPECIES = 801;
        constexpr uint8_t SPIKY_EARED_PICHU_FORM = 1;
        constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
        constexpr uint8_t VERSION_LEAFGREEN_SWITCH = 57;
        // PKHeX's memory id for defeating the Champion, and for defeating the Battle Maison's or
        // Battle Tower's strongest trainers.
        constexpr uint8_t MEMORY_CHAMPION = 27;
        constexpr uint8_t MEMORY_STRONGEST = 30;
        constexpr int CONTEST_AFFECTION_PER_RIBBON = 20;
        constexpr uint8_t MAX_GEN3_CONTEST_RANK = 4;
        constexpr int WELL_TRAVELED_LEVEL_GAIN = 30;
        constexpr int MAX_WINNING_RIBBON_LEVEL = 50;
        constexpr uint32_t SUPER_TRAINING_UNUSED_BITS = 0b11;
        constexpr uint32_t SUPER_TRAINING_ALL_MEDALS = 0xFFFF'FFFC;

        // PKHeX RibbonRules.BattleFrontierBanlist, ascending.
        constexpr std::array<uint16_t, 43> BATTLE_FRONTIER_BANLIST = {
            150, 151, 249, 250, 251, 382, 383, 384, 385, 386, 483, 484, 487, 489, 490, 491, 492, 493, 494, 643, 644, 646,
            647, 648, 649, 716, 717, 718, 719, 720, 721, 789, 790, 791, 792, 800, 801, 802, 807, 808, 809, 1007, 1008,
        };

        // PKHeX SpeciesCategory.IsMythical.
        constexpr std::array<uint16_t, 23> MYTHICAL_SPECIES = {
            151, 251, 385, 386, 489, 490, 491, 492, 493, 494, 647, 648, 649, 719, 720, 721, 801, 802, 807, 808, 809, 893, 1025,
        };

        // PKHeX PersonalInfo8BDSP.VoicelessBDSP: species that earn the Footprint Ribbon at any level in
        // Brilliant Diamond and Shining Pearl, one bit per species.
        constexpr std::array<uint8_t, 61> VOICELESS_BDSP = {
            0x00, 0x48, 0x00, 0x00, 0x00, 0xC0, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x03,
            0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x32, 0x00, 0x80, 0x00, 0x02, 0x80, 0x00,
            0x00, 0x14, 0x02, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x86, 0x0F, 0x00, 0x00, 0xD0, 0x0E,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x40, 0x00, 0x14, 0x40,
        };

        struct MemoryCounts
        {
            uint8_t contestCount = 0;
            uint8_t battleCount = 0;
        };

        bool isMythical(uint16_t speciesId)
        {
            return std::find(MYTHICAL_SPECIES.begin(), MYTHICAL_SPECIES.end(), speciesId) != MYTHICAL_SPECIES.end();
        }

        bool isVoicelessInBrilliantDiamond(uint16_t speciesId)
        {
            const size_t byteIndex = speciesId >> 3;
            if (byteIndex >= VOICELESS_BDSP.size())
            {
                return false;
            }
            return (VOICELESS_BDSP[byteIndex] & (1 << (speciesId & 7))) != 0;
        }

        uint8_t getOriginVersion(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            return pokemon.hasOriginGame() ? pokemon.originGame() : 0;
        }

        bool isOriginVersion(const RibbonArguments &arguments, Enums::GameVersion firstVersion, Enums::GameVersion secondVersion)
        {
            const uint8_t originVersion = getOriginVersion(arguments);
            return originVersion == static_cast<uint8_t>(firstVersion) || originVersion == static_cast<uint8_t>(secondVersion);
        }

        /// PKHeX PKM.VC3: FireRed or LeafGreen on Nintendo Switch.
        bool isSwitchFireRedLeafGreen(const RibbonArguments &arguments)
        {
            const uint8_t originVersion = getOriginVersion(arguments);
            return originVersion == VERSION_FIRERED_SWITCH || originVersion == VERSION_LEAFGREEN_SWITCH;
        }

        /// PKHeX's IAffection: the Gen 6 and Gen 7 3DS formats.
        bool hasAffection(const RibbonArguments &arguments)
        {
            return arguments.context.pokemon.hasHandlerAffection();
        }

        /// The ribbon's has-flag, separate from its count (PKHeX HasContestMemoryRibbon / HasBattleMemoryRibbon).
        bool hasMemoryRibbonFlag(const RibbonArguments &arguments, std::string_view spriteStem)
        {
            const Names::RibbonDefinition *ribbon = arguments.find(spriteStem);
            if (ribbon == nullptr)
            {
                return false;
            }
            return ((arguments.entityData[ribbon->byteOffset] >> ribbon->bit) & 1u) != 0;
        }

        /// Whether a context without the engine's template (one a test builds by hand) could be one of
        /// PKHeX's ribbon-carrying templates (IRibbonSetEvent3/4, IRibbonSetCommon7); empty when it
        /// could, which skips the check.
        std::optional<bool> isRibbonCarryingEncounter(const RibbonEncounter &encounter)
        {
            switch (encounter.type)
            {
            case EncounterType::Unmatched:
            case EncounterType::Egg:
                return false;
            case EncounterType::Unchecked:
            case EncounterType::Event:
                return std::nullopt;
            default:
                return false;
            }
        }

        /// PKHeX `enc is EncounterStatic7 { Species: Magearna }`: the QR-code Magearna.
        bool isMagearnaQrEncounter(const RibbonEncounter &encounter)
        {
            if (encounter.encounterMatch != nullptr)
            {
                const Encounters::EncounterStatic7 *static7 = Encounters::encounterAs<Encounters::EncounterStatic7>(*encounter.encounterMatch);
                return static7 != nullptr && static7->speciesId() == MAGEARNA_SPECIES;
            }
            return false;
        }

        /// PKHeX `enc is EncounterStatic6 { Species: Pikachu, Form: not 0 }`: ORAS's Cosplay Pikachu.
        bool isCosplayPikachuEncounter(const RibbonEncounter &encounter)
        {
            if (encounter.encounterMatch != nullptr)
            {
                const Encounters::EncounterStatic6 *static6 = Encounters::encounterAs<Encounters::EncounterStatic6>(*encounter.encounterMatch);
                return static6 != nullptr && static6->speciesId() == PIKACHU_SPECIES && static6->formId() != 0;
            }
            return false;
        }

        /// The template's ribbon sets; empty when the engine has no match (the callers fall back).
        std::optional<Encounters::TemplateRibbons> getTemplateRibbons(const RibbonEncounter &encounter)
        {
            if (encounter.encounterMatch == nullptr)
            {
                return std::nullopt;
            }
            return Encounters::getTemplateRibbons(*encounter.encounterMatch);
        }

        /// PKHeX IEncounterServerDate.IsDateRestricted: Pokemon GO, and HOME gifts PKSE cannot tell apart.
        std::optional<bool> hasServerDate(const RibbonArguments &arguments)
        {
            if (arguments.encounter.encounterMatch != nullptr)
            {
                return Encounters::isEncounterDateRestricted(*arguments.encounter.encounterMatch);
            }
            if (getOriginVersion(arguments) == VERSION_POKEMON_GO)
            {
                return true;
            }
            if (arguments.encounter.type == EncounterType::Event || arguments.encounter.type == EncounterType::Unchecked)
            {
                return std::nullopt;
            }
            return false;
        }

        /// The met date is after year/month/day (PKHeX `MetDate > new DateOnly(...)`).
        bool isMetAfter(const RibbonArguments &arguments, int year, int month, int day)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            const int metDate = (2000 + pokemon.metYear()) * 10000 + pokemon.metMonth() * 100 + pokemon.metDay();
            return metDate > year * 10000 + month * 100 + day;
        }

        // ---- RibbonRules.cs ----

        bool isRibbonValidEffort(const Evolutions::EvolutionHistory &history)
        {
            return history.hasVisited(Enums::GameVersion::RSE) || history.hasVisited(Enums::GameVersion::DP) || history.hasVisited(Enums::GameVersion::XY) || history.hasVisited(Enums::GameVersion::SM) || history.hasVisited(Enums::GameVersion::SWSH) || history.hasVisited(Enums::GameVersion::BDSP) || history.hasVisited(Enums::GameVersion::SV);
        }

        bool isRibbonValidBestFriends(const RibbonArguments &arguments)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (history.hasVisited(Enums::GameVersion::SWSH) || history.hasVisited(Enums::GameVersion::BDSP) || history.hasVisited(Enums::GameVersion::SV))
            {
                return true;
            }
            // Max affection cannot drop, so an untraded 3DS-format Pokemon below it never earned the ribbon.
            const bool hasLowAffectionUntraded = hasAffection(arguments) && isUntraded(arguments.context) && arguments.context.pokemon.otAffection() < 255;
            const int formatGeneration = arguments.context.formatGeneration;
            if (history.hasVisited(Enums::GameVersion::XY) && !(formatGeneration == 6 && hasLowAffectionUntraded))
            {
                return true;
            }
            return history.hasVisited(Enums::GameVersion::SM) && !(formatGeneration == 7 && hasLowAffectionUntraded);
        }

        bool isWellTraveled(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            return static_cast<int>(Evolutions::getEntityCurrentLevel(pokemon)) - static_cast<int>(pokemon.metLevel()) >= WELL_TRAVELED_LEVEL_GAIN;
        }

        bool isRibbonValidFootprint(const RibbonArguments &arguments)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (history.hasVisited(Enums::GameVersion::RSE) || history.hasVisited(Enums::GameVersion::DP))
            {
                return true;
            }
            if (arguments.context.formatGeneration < 6)
            {
                return false;
            }
            if ((history.hasVisited(Enums::GameVersion::XY) || history.hasVisited(Enums::GameVersion::SM)) && isWellTraveled(arguments))
            {
                return true;
            }
            if (history.hasVisited(Enums::GameVersion::BDSP))
            {
                for (const Evolutions::EvolutionCriteria &stage : history.getChain(Enums::GameVersion::BDSP))
                {
                    if (isVoicelessInBrilliantDiamond(stage.speciesId))
                    {
                        return true;
                    }
                }
                if (isWellTraveled(arguments))
                {
                    return true;
                }
            }
            return false;
        }

        bool isRibbonObtainableMasterRankSwordShield(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            const RibbonEncounter &encounter = arguments.encounter;
            // A transfer needs the battle-ready mark to enter Ranked.
            if ((encounter.generation < 8 || encounter.context == Enums::GameVersion::GG) && pokemon.hasBattleVersion() && pokemon.battleVersion() == 0)
            {
                return false;
            }
            // Mythicals were allowed in Series 13 only; a server date cannot be changed on the console.
            if (hasServerDate(arguments).value_or(false) && isMythical(arguments.speciesId) && isMetAfter(arguments, 2022, 11, 1))
            {
                return false;
            }
            return true;
        }

        bool isRibbonObtainableMasterRankScarletViolet(const RibbonArguments &arguments)
        {
            if (isSwitchFireRedLeafGreen(arguments))
            {
                return false;
            }
            if (arguments.speciesId == GRENINJA_SPECIES)
            {
                return arguments.context.pokemon.form() == 0;
            }
            if (hasServerDate(arguments).value_or(false) && isMythical(arguments.speciesId) && isMetAfter(arguments, 2026, 1, 5))
            {
                return false;
            }
            return true;
        }

        bool isRibbonValidMasterRank(const RibbonArguments &arguments)
        {
            if (arguments.history.hasVisited(Enums::GameVersion::SWSH) && isRibbonObtainableMasterRankSwordShield(arguments))
            {
                return true;
            }
            return arguments.history.hasVisited(Enums::GameVersion::SV) && isRibbonObtainableMasterRankScarletViolet(arguments);
        }

        bool isRibbonValidSuperTraining(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            // Transfer to Gen 8 wipes the medals along with the flags.
            if (!pokemon.hasSuperTraining())
            {
                return true;
            }
            return (pokemon.superTrainFlags() & ~SUPER_TRAINING_UNUSED_BITS) == SUPER_TRAINING_ALL_MEDALS;
        }

        bool isRibbonValidTowerMaster(const Evolutions::EvolutionHistory &history)
        {
            if (history.hasVisited(Enums::GameVersion::SWSH))
            {
                return true;
            }
            if (!history.hasVisited(Enums::GameVersion::BDSP))
            {
                return false;
            }
            return !isMythical(history.getChain(Enums::GameVersion::BDSP)[0].speciesId);
        }

        bool isRibbonValidWinning(const RibbonArguments &arguments)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (!allowsGameBoyAdvanceCrossTransfer(arguments.context))
            {
                return false;
            }
            if (!history.hasVisited(Enums::GameVersion::RSE))
            {
                return false;
            }
            if (!isAllowedBattleFrontier(history.getChain(Enums::GameVersion::RSE)[0].speciesId))
            {
                return false;
            }
            // Only at level 50 or below; a Gen 3 record still holds the level it was met at.
            if (arguments.context.formatGeneration == 3)
            {
                return arguments.context.pokemon.metLevel() <= MAX_WINNING_RIBBON_LEVEL;
            }
            return arguments.encounter.levelMin.value_or(0) <= MAX_WINNING_RIBBON_LEVEL;
        }

        bool isRibbonValidVictory(const RibbonArguments &arguments)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            return allowsGameBoyAdvanceCrossTransfer(arguments.context) && history.hasVisited(Enums::GameVersion::RSE) && isAllowedBattleFrontier(history.getChain(Enums::GameVersion::RSE)[0].speciesId);
        }

        bool isRibbonValidTwinklingStar(const RibbonArguments &arguments)
        {
            if (!arguments.history.hasVisited(Enums::GameVersion::BDSP))
            {
                return false;
            }
            // Only after every other contest, which awards the Contest Star.
            return !(arguments.find("ribbonconteststar") != nullptr && !arguments.has("ribbonconteststar"));
        }

        /// PKHeX RibbonRules.GetValidRibbonStateNational: true must have it, false must not; empty
        /// when PKSE cannot tell whether the encounter was a Shadow Pokemon. Purifying a Shadow
        /// Pokemon awards it; PKSE has no Colosseum/XD entity, so no record can still be a Shadow.
        std::optional<bool> getValidRibbonStateNational(const RibbonArguments &arguments)
        {
            if (arguments.encounter.generation != 3)
            {
                return false;
            }
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(arguments.context))
            {
                return Encounters::isShadowEncounter(*encounterMatch);
            }
            if (getOriginVersion(arguments) != VERSION_COLOSSEUM_XD)
            {
                return false;
            }
            return std::nullopt;
        }

        bool isEarthRibbonAllowed(const RibbonArguments &arguments)
        {
            return arguments.encounter.generation == 3 && allowsGameBoyAdvanceCrossTransfer(arguments.context);
        }

        bool isAllowedContest4(uint16_t speciesId, uint8_t formId)
        {
            if (speciesId == DITTO_SPECIES || speciesId == UNOWN_SPECIES)
            {
                return false;
            }
            return !(speciesId == PICHU_SPECIES && formId == SPIKY_EARED_PICHU_FORM);
        }

        bool isAllowedContest3(const RibbonArguments &arguments)
        {
            return allowsGameBoyAdvanceCrossTransfer(arguments.context) && arguments.history.hasVisited(Enums::GameVersion::RSE);
        }

        bool isAllowedContest4(const Evolutions::EvolutionHistory &history)
        {
            if (!history.hasVisited(Enums::GameVersion::DP))
            {
                return false;
            }
            const Evolutions::EvolutionCriteria head = history.getChain(Enums::GameVersion::DP)[0];
            return isAllowedContest4(head.speciesId, head.formId);
        }

        bool isAllowedBattleFrontier4(const Evolutions::EvolutionHistory &history)
        {
            if (!history.hasVisited(Enums::GameVersion::DP))
            {
                return false;
            }
            const Evolutions::EvolutionCriteria head = history.getChain(Enums::GameVersion::DP)[0];
            if (head.speciesId == PICHU_SPECIES && head.formId == SPIKY_EARED_PICHU_FORM)
            {
                return false;
            }
            return isAllowedBattleFrontier(head.speciesId);
        }

        MemoryCounts getMaxMemoryCounts(const RibbonArguments &arguments)
        {
            constexpr uint8_t maxContest3 = 20;
            constexpr uint8_t maxContest4 = 20;
            constexpr uint8_t maxBattle4 = 6;
            constexpr uint8_t maxBattleBoth = 8;
            constexpr uint8_t maxBattleBothWithoutWinning = 7;
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (history.hasVisited(Enums::GameVersion::RSE))
            {
                // The Gen 3 head can stand for Gen 4 too: none of the barred species evolve.
                const Evolutions::EvolutionCriteria head = history.getChain(Enums::GameVersion::RSE)[0];
                MemoryCounts counts;
                counts.contestCount = isAllowedContest4(head.speciesId, head.formId) ? maxContest3 + maxContest4 : maxContest3;
                if (isAllowedBattleFrontier(head.speciesId))
                {
                    counts.battleCount = isRibbonValidWinning(arguments) ? maxBattleBoth : maxBattleBothWithoutWinning;
                }
                return counts;
            }
            if (history.hasVisited(Enums::GameVersion::DP))
            {
                const Evolutions::EvolutionCriteria head = history.getChain(Enums::GameVersion::DP)[0];
                MemoryCounts counts;
                counts.contestCount = isAllowedContest4(head.speciesId, head.formId) ? maxContest4 : 0;
                counts.battleCount = isAllowedBattleFrontier(head.speciesId) ? maxBattle4 : 0;
                return counts;
            }
            return MemoryCounts{};
        }

        // ---- RibbonVerifierOnly3.cs ----

        constexpr std::array<std::string_view, 5> GEN3_CONTEST_RIBBONS = {"ribbong3cool", "ribbong3beauty", "ribbong3cute", "ribbong3smart", "ribbong3tough"};

        void parseOnly3(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            const bool allowsContest = !isEgg && isAllowedContest3(arguments);
            for (const std::string_view spriteStem : GEN3_CONTEST_RIBBONS)
            {
                const uint8_t contestRank = arguments.value(spriteStem);
                if (allowsContest ? contestRank > MAX_GEN3_CONTEST_RANK : contestRank != 0)
                {
                    addRibbonResult(results, arguments, spriteStem);
                }
            }
        }

        // ---- RibbonVerifierEvent3.cs ----

        constexpr std::array<std::string_view, 7> EVENT3_RIBBONS = {"ribbonearth", "ribbonnational", "ribboncountry", "ribbonworld", "ribbonchampionbattle", "ribbonchampionregional", "ribbonchampionnational"};

        /// PKHeX's `r.Ribbon != e.Ribbon` comparison against a template's ribbon.
        void compareTemplateRibbon(RibbonResultList &results, const RibbonArguments &arguments, const Encounters::TemplateRibbons &templateRibbons, std::string_view spriteStem)
        {
            const bool isTemplateRibbon = templateRibbons.has(Encounters::getRibbonIndexFromSpriteStem(spriteStem));
            if (arguments.has(spriteStem) != isTemplateRibbon)
            {
                addRibbonResult(results, arguments, spriteStem, isTemplateRibbon);
            }
        }

        void parseEvent3(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (isEgg)
            {
                for (const std::string_view spriteStem : EVENT3_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                return;
            }
            const std::optional<Encounters::TemplateRibbons> templateRibbons = getTemplateRibbons(arguments.encounter);
            if (templateRibbons.has_value() && templateRibbons->hasEvent3)
            {
                // The template's own ribbons, except that the Earth Ribbon can still be earned later.
                if (templateRibbons->has(Encounters::RibbonIndex::Earth))
                {
                    if (!arguments.has("ribbonearth"))
                    {
                        addRibbonResult(results, arguments, "ribbonearth", true);
                    }
                }
                else if (arguments.has("ribbonearth") && !isEarthRibbonAllowed(arguments))
                {
                    addRibbonResult(results, arguments, "ribbonearth");
                }
                for (size_t ribbonIndex = 1; ribbonIndex < EVENT3_RIBBONS.size(); ribbonIndex++)
                {
                    compareTemplateRibbon(results, arguments, *templateRibbons, EVENT3_RIBBONS[ribbonIndex]);
                }
                return;
            }
            if (!templateRibbons.has_value())
            {
                // No template ribbons to compare with (no matched template).
                const std::optional<bool> carriesRibbons = isRibbonCarryingEncounter(arguments.encounter);
                if (!carriesRibbons.has_value() || *carriesRibbons)
                {
                    return;
                }
            }
            // The Earth Ribbon is Colosseum's and XD's Mt. Battle reward.
            if (arguments.has("ribbonearth") && !isEarthRibbonAllowed(arguments))
            {
                addRibbonResult(results, arguments, "ribbonearth");
            }
            const std::optional<bool> nationalRequired = getValidRibbonStateNational(arguments);
            if (nationalRequired.has_value() && arguments.has("ribbonnational") != *nationalRequired)
            {
                addRibbonResult(results, arguments, "ribbonnational", *nationalRequired);
            }
            for (size_t ribbonIndex = 2; ribbonIndex < EVENT3_RIBBONS.size(); ribbonIndex++)
            {
                flagRibbonIfSet(results, arguments, EVENT3_RIBBONS[ribbonIndex]);
            }
        }

        // ---- RibbonVerifierEvent4.cs ----

        constexpr std::array<std::string_view, 8> EVENT4_RIBBONS = {"ribbonclassic", "ribbonwishing", "ribbonpremier", "ribbonevent", "ribbonbirthday", "ribbonspecial", "ribbonchampionworld", "ribbonsouvenir"};

        void parseEvent4(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            // Some event eggs carry ribbons, so an egg is judged against its encounter too.
            const std::optional<Encounters::TemplateRibbons> templateRibbons = getTemplateRibbons(arguments.encounter);
            if (templateRibbons.has_value() && templateRibbons->hasEvent4)
            {
                for (const std::string_view spriteStem : EVENT4_RIBBONS)
                {
                    compareTemplateRibbon(results, arguments, *templateRibbons, spriteStem);
                }
                return;
            }
            if (!templateRibbons.has_value())
            {
                const std::optional<bool> carriesRibbons = isRibbonCarryingEncounter(arguments.encounter);
                if (!carriesRibbons.has_value() || *carriesRibbons)
                {
                    return;
                }
            }
            // The QR-code Magearna is the one non-gift template that comes with the Wishing Ribbon.
            const bool isMagearna7 = !isEgg && isMagearnaQrEncounter(arguments.encounter);
            for (const std::string_view spriteStem : EVENT4_RIBBONS)
            {
                if (isMagearna7 && spriteStem == "ribbonwishing")
                {
                    if (!arguments.has(spriteStem))
                    {
                        addRibbonResult(results, arguments, spriteStem, true);
                    }
                    continue;
                }
                flagRibbonIfSet(results, arguments, spriteStem);
            }
        }

        // ---- RibbonVerifierUnique3.cs ----

        void parseUnique3(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (!isEgg && arguments.history.hasVisited(Enums::GameVersion::RSE))
            {
                if (arguments.has("ribbonwinning") && !isRibbonValidWinning(arguments))
                {
                    addRibbonResult(results, arguments, "ribbonwinning");
                }
                if (arguments.has("ribbonvictory") && !isRibbonValidVictory(arguments))
                {
                    addRibbonResult(results, arguments, "ribbonvictory");
                }
                return;
            }
            flagRibbonIfSet(results, arguments, "ribbonwinning");
            flagRibbonIfSet(results, arguments, "ribbonvictory");
        }

        // ---- RibbonVerifierCommon3.cs ----

        void parseCommon3(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            const bool hasVisitedGen3 = !isEgg && history.hasVisited(Enums::GameVersion::RSE);
            if (arguments.has("ribbonchampiong3") && !hasVisitedGen3)
            {
                addRibbonResult(results, arguments, "ribbonchampiong3");
            }
            // A Master Rank or Link Contest win in Ruby/Sapphire/Emerald.
            if (arguments.has("ribbonartist") && !hasVisitedGen3)
            {
                addRibbonResult(results, arguments, "ribbonartist");
            }
            if (arguments.has("ribboneffort") && (isEgg || !isRibbonValidEffort(history)))
            {
                addRibbonResult(results, arguments, "ribboneffort");
            }
        }

        // ---- RibbonVerifierUnique4.cs ----

        constexpr std::array<std::string_view, 6> ABILITY_RIBBONS = {"ribbonability", "ribbonabilitygreat", "ribbonabilitydouble", "ribbonabilitymulti", "ribbonabilitypair", "ribbonabilityworld"};

        // Each contest's ribbons from the first rank to Master (PKHeX RibbonIndex3 / RibbonIndex4 order).
        constexpr std::array<std::array<std::string_view, 4>, 5> GEN3_CONTEST_RANK_RIBBONS = {{
            {"ribbong3cool", "ribbong3coolsuper", "ribbong3coolhyper", "ribbong3coolmaster"},
            {"ribbong3beauty", "ribbong3beautysuper", "ribbong3beautyhyper", "ribbong3beautymaster"},
            {"ribbong3cute", "ribbong3cutesuper", "ribbong3cutehyper", "ribbong3cutemaster"},
            {"ribbong3smart", "ribbong3smartsuper", "ribbong3smarthyper", "ribbong3smartmaster"},
            {"ribbong3tough", "ribbong3toughsuper", "ribbong3toughhyper", "ribbong3toughmaster"},
        }};
        constexpr std::array<std::array<std::string_view, 4>, 5> GEN4_CONTEST_RANK_RIBBONS = {{
            {"ribbong4cool", "ribbong4coolgreat", "ribbong4coolultra", "ribbong4coolmaster"},
            {"ribbong4beauty", "ribbong4beautygreat", "ribbong4beautyultra", "ribbong4beautymaster"},
            {"ribbong4cute", "ribbong4cutegreat", "ribbong4cuteultra", "ribbong4cutemaster"},
            {"ribbong4smart", "ribbong4smartgreat", "ribbong4smartultra", "ribbong4smartmaster"},
            {"ribbong4tough", "ribbong4toughgreat", "ribbong4toughultra", "ribbong4toughmaster"},
        }};

        /// A higher rank needs every lower one (PKHeX AddMissingContest3/4.CheckSet).
        void addMissingContestRanks(const RibbonArguments &arguments, RibbonResultList &results, const std::array<std::array<std::string_view, 4>, 5> &contestRibbons)
        {
            for (const std::array<std::string_view, 4> &rankRibbons : contestRibbons)
            {
                bool hasHigherRank = arguments.has(rankRibbons[3]);
                for (int rankIndex = 2; rankIndex >= 0; rankIndex--)
                {
                    if (arguments.has(rankRibbons[rankIndex]))
                    {
                        hasHigherRank = true;
                    }
                    else if (hasHigherRank)
                    {
                        addRibbonResult(results, arguments, rankRibbons[rankIndex]);
                    }
                }
            }
        }

        void flagAnyContestRanks(const RibbonArguments &arguments, RibbonResultList &results, const std::array<std::array<std::string_view, 4>, 5> &contestRibbons)
        {
            for (const std::array<std::string_view, 4> &rankRibbons : contestRibbons)
            {
                for (int rankIndex = 3; rankIndex >= 0; rankIndex--)
                {
                    flagRibbonIfSet(results, arguments, rankRibbons[rankIndex]);
                }
            }
        }

        void parseUnique4(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (isEgg || !isAllowedBattleFrontier4(history))
            {
                for (const std::string_view spriteStem : ABILITY_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
            }
            if (!isEgg && isAllowedContest3(arguments))
            {
                addMissingContestRanks(arguments, results, GEN3_CONTEST_RANK_RIBBONS);
            }
            else
            {
                flagAnyContestRanks(arguments, results, GEN3_CONTEST_RANK_RIBBONS);
            }
            if (!isEgg && isAllowedContest4(history))
            {
                addMissingContestRanks(arguments, results, GEN4_CONTEST_RANK_RIBBONS);
            }
            else
            {
                flagAnyContestRanks(arguments, results, GEN4_CONTEST_RANK_RIBBONS);
            }
        }

        // ---- RibbonVerifierCommon4.cs ----

        constexpr std::array<std::string_view, 7> DAILY_RIBBONS = {"ribbonalert", "ribbonshock", "ribbondowncast", "ribboncareless", "ribbonrelax", "ribbonsnooze", "ribbonsmile"};
        constexpr std::array<std::string_view, 3> COSMETIC_RIBBONS = {"ribbongorgeous", "ribbonroyal", "ribbongorgeousroyal"};

        void parseCommon4(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (isEgg)
            {
                for (const std::string_view spriteStem : {"ribbonfootprint", "ribbonrecord", "ribbonlegend", "ribbonchampionsinnoh"})
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                for (const std::string_view spriteStem : DAILY_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                for (const std::string_view spriteStem : COSMETIC_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                return;
            }
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (arguments.has("ribbonfootprint") && !isRibbonValidFootprint(arguments))
            {
                addRibbonResult(results, arguments, "ribbonfootprint");
            }
            // The Record Ribbon was never distributed.
            flagRibbonIfSet(results, arguments, "ribbonrecord");

            const bool hasVisitedGen4 = history.hasVisited(Enums::GameVersion::DP);
            const bool hasVisitedGen6 = history.hasVisited(Enums::GameVersion::XY);
            const bool hasVisitedBrilliantDiamond = history.hasVisited(Enums::GameVersion::BDSP);
            const bool hasVisitedOmegaRuby = hasVisitedGen6 && !(isUntraded(arguments.context) && isOriginVersion(arguments, Enums::GameVersion::X, Enums::GameVersion::Y));
            // Spiky-eared Pichu comes only from its HeartGold/SoulSilver encounter, which has no Sinnoh.
            const bool isSpikyEaredPichu = arguments.speciesId == PICHU_SPECIES && arguments.context.pokemon.form() == SPIKY_EARED_PICHU_FORM;
            const bool hasVisitedSinnoh4 = hasVisitedGen4 && !isSpikyEaredPichu;
            const bool canBeSinnohChampion = hasVisitedSinnoh4 || hasVisitedBrilliantDiamond;
            const bool canHaveCosmetic = hasVisitedSinnoh4 || hasVisitedOmegaRuby || hasVisitedBrilliantDiamond;
            const bool canHaveDaily = hasVisitedGen4 || hasVisitedGen6 || hasVisitedBrilliantDiamond;

            if (arguments.has("ribbonlegend") && !hasVisitedGen4)
            {
                addRibbonResult(results, arguments, "ribbonlegend");
            }
            if (arguments.has("ribbonchampionsinnoh") && !canBeSinnohChampion)
            {
                addRibbonResult(results, arguments, "ribbonchampionsinnoh");
            }
            if (!canHaveDaily)
            {
                for (const std::string_view spriteStem : DAILY_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
            }
            // Omega Ruby/Alpha Sapphire sell each cosmetic ribbon alone; the Ribbon Syndicate sells them in order.
            if (!canHaveCosmetic)
            {
                for (const std::string_view spriteStem : COSMETIC_RIBBONS)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
            }
            else if (!hasVisitedOmegaRuby)
            {
                bool hasPrerequisite = false;
                if (arguments.has("ribbonroyal"))
                {
                    hasPrerequisite = arguments.has("ribbongorgeous");
                    if (!hasPrerequisite)
                    {
                        addRibbonResult(results, arguments, "ribbonroyal");
                    }
                }
                if (!hasPrerequisite && arguments.has("ribbongorgeousroyal"))
                {
                    addRibbonResult(results, arguments, "ribbongorgeousroyal");
                }
            }
        }

        // ---- RibbonVerifierCommon6.cs ----

        constexpr std::array<std::string_view, 5> MASTER_CONTEST_RIBBONS = {"ribbonmastercoolness", "ribbonmasterbeauty", "ribbonmastercuteness", "ribbonmastercleverness", "ribbonmastertoughness"};

        bool isMemoryCountValid(uint8_t memoryCount, bool hasFlag, int formatGeneration, uint8_t maxCount)
        {
            if (memoryCount > maxCount)
            {
                return false;
            }
            // Gen 8 on keeps the has-flag with the count; Gens 6 and 7 never set it.
            const bool expectsFlag = formatGeneration >= 8 && memoryCount != 0;
            return hasFlag == expectsFlag;
        }

        void flagContest6(const RibbonArguments &arguments, RibbonResultList &results)
        {
            flagRibbonIfSet(results, arguments, "ribbonconteststar");
            for (const std::string_view spriteStem : MASTER_CONTEST_RIBBONS)
            {
                flagRibbonIfSet(results, arguments, spriteStem);
            }
        }

        /// PKHeX RibbonVerifierCommon6.GetHasGen6ChampMemory.
        bool hasGen6ChampionMemory(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            if (!pokemon.hasHandlerMemories())
            {
                return false;
            }
            return (arguments.encounter.generation == 6 && pokemon.otMemory() == MEMORY_CHAMPION) || (arguments.context.formatGeneration < 8 && pokemon.htMemory() == MEMORY_CHAMPION);
        }

        void parseCommon6(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (isEgg)
            {
                for (const std::string_view spriteStem : {"ribbonchampionkalos", "ribbonchampiong6hoenn", "ribbonbestfriends", "ribbontraining", "ribbonbattlerskillful", "ribbonbattlerexpert"})
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                flagContest6(arguments, results);
                return;
            }
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            const Evolutions::EvolutionHistory &history = arguments.history;
            const int formatGeneration = arguments.context.formatGeneration;
            if (arguments.find("ribboncountmemorycontest") != nullptr)
            {
                const MemoryCounts maxCounts = getMaxMemoryCounts(arguments);
                if (!isMemoryCountValid(arguments.value("ribboncountmemorycontest"), hasMemoryRibbonFlag(arguments, "ribboncountmemorycontest"), formatGeneration, maxCounts.contestCount))
                {
                    addRibbonResult(results, arguments, "ribboncountmemorycontest");
                }
                if (!isMemoryCountValid(arguments.value("ribboncountmemorybattle"), hasMemoryRibbonFlag(arguments, "ribboncountmemorybattle"), formatGeneration, maxCounts.battleCount))
                {
                    addRibbonResult(results, arguments, "ribboncountmemorybattle");
                }
            }

            const bool hasVisitedGen6 = history.hasVisited(Enums::GameVersion::XY);
            const bool hasVisitedBrilliantDiamond = history.hasVisited(Enums::GameVersion::BDSP);
            // ORAS's Cosplay Pikachu never sees Kalos.
            const bool isCosplayPikachu = isCosplayPikachuEncounter(arguments.encounter);
            const bool hasVisitedKalos = hasVisitedGen6 && !(isUntraded(arguments.context) && isOriginVersion(arguments, Enums::GameVersion::AS, Enums::GameVersion::OR)) && !isCosplayPikachu;
            const bool hasVisitedOmegaRuby = hasVisitedGen6 && !(isUntraded(arguments.context) && isOriginVersion(arguments, Enums::GameVersion::X, Enums::GameVersion::Y));
            const bool canEnterContest = hasVisitedOmegaRuby || hasVisitedBrilliantDiamond;

            const bool hasKalosChampion = arguments.has("ribbonchampionkalos");
            const bool hasHoennChampion = arguments.has("ribbonchampiong6hoenn");
            if (hasKalosChampion && !hasVisitedKalos)
            {
                addRibbonResult(results, arguments, "ribbonchampionkalos");
            }
            if (hasHoennChampion && !hasVisitedOmegaRuby)
            {
                addRibbonResult(results, arguments, "ribbonchampiong6hoenn");
            }
            if (!hasKalosChampion && !hasHoennChampion && hasGen6ChampionMemory(arguments))
            {
                const bool isFromXY = isOriginVersion(arguments, Enums::GameVersion::X, Enums::GameVersion::Y);
                addRibbonResult(results, arguments, isFromXY ? "ribbonchampionkalos" : "ribbonchampiong6hoenn", true);
            }

            if (!canEnterContest)
            {
                flagContest6(arguments, results);
            }
            else
            {
                // Each Gen 6 contest win adds 20 to OT affection.
                if (hasAffection(arguments))
                {
                    const int currentAffection = pokemon.otAffection();
                    int expectedAffection = 0;
                    for (size_t ribbonIndex = 0; ribbonIndex < MASTER_CONTEST_RIBBONS.size(); ribbonIndex++)
                    {
                        if (!arguments.has(MASTER_CONTEST_RIBBONS[ribbonIndex]))
                        {
                            continue;
                        }
                        // PKHeX does not raise the running total for the last ribbon.
                        const int requiredAffection = expectedAffection + CONTEST_AFFECTION_PER_RIBBON;
                        if (ribbonIndex + 1 < MASTER_CONTEST_RIBBONS.size())
                        {
                            expectedAffection = requiredAffection;
                        }
                        if (currentAffection < requiredAffection)
                        {
                            addRibbonResult(results, arguments, MASTER_CONTEST_RIBBONS[ribbonIndex]);
                        }
                    }
                }
                // Winning every contest awards the Contest Star.
                bool hasAllContest = true;
                for (const std::string_view spriteStem : MASTER_CONTEST_RIBBONS)
                {
                    hasAllContest = hasAllContest && arguments.has(spriteStem);
                }
                if (hasAllContest != arguments.has("ribbonconteststar"))
                {
                    addRibbonResult(results, arguments, "ribbonconteststar", hasAllContest);
                }
            }

            if (arguments.has("ribbonbestfriends") && !isRibbonValidBestFriends(arguments))
            {
                addRibbonResult(results, arguments, "ribbonbestfriends");
            }

            if (!hasVisitedGen6)
            {
                flagRibbonIfSet(results, arguments, "ribbontraining");
                flagRibbonIfSet(results, arguments, "ribbonbattlerskillful");
                flagRibbonIfSet(results, arguments, "ribbonbattlerexpert");
                return;
            }
            if (arguments.has("ribbontraining") && !isRibbonValidSuperTraining(arguments))
            {
                addRibbonResult(results, arguments, "ribbontraining");
            }
            if (!isAllowedBattleFrontier(arguments.speciesId))
            {
                flagRibbonIfSet(results, arguments, "ribbonbattlerskillful");
                flagRibbonIfSet(results, arguments, "ribbonbattlerexpert");
            }
        }

        // ---- RibbonVerifierCommon7.cs ----

        void parseCommon7(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            constexpr std::array<std::string_view, 3> battleTreeRibbons = {"ribbonbattleroyale", "ribbonbattletreegreat", "ribbonbattletreemaster"};
            if (isEgg)
            {
                flagRibbonIfSet(results, arguments, "ribbonchampionalola");
                for (const std::string_view spriteStem : battleTreeRibbons)
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                return;
            }
            const Evolutions::EvolutionHistory &history = arguments.history;
            const bool hasVisitedGen7 = history.hasVisited(Enums::GameVersion::SM);
            // PKHeX RibbonRules.IsRibbonValidAlolaChamp: a template that comes with the ribbon must keep it.
            const std::optional<Encounters::TemplateRibbons> templateRibbons = getTemplateRibbons(arguments.encounter);
            if (templateRibbons.has_value())
            {
                if (templateRibbons->hasCommon7 && templateRibbons->has(Encounters::RibbonIndex::ChampionAlola))
                {
                    if (!arguments.has("ribbonchampionalola"))
                    {
                        addRibbonResult(results, arguments, "ribbonchampionalola", true);
                    }
                }
                else if (!hasVisitedGen7)
                {
                    flagRibbonIfSet(results, arguments, "ribbonchampionalola");
                }
            }
            else
            {
                // A Mystery Gift can come with the Alola Champion Ribbon, which PKSE's rows do not record.
                const std::optional<bool> carriesRibbons = isRibbonCarryingEncounter(arguments.encounter);
                if (carriesRibbons.has_value() && !*carriesRibbons && !hasVisitedGen7)
                {
                    flagRibbonIfSet(results, arguments, "ribbonchampionalola");
                }
            }

            if (hasVisitedGen7)
            {
                if (isAllowedBattleFrontier(history.getChain(Enums::GameVersion::SM)[0].speciesId))
                {
                    return;
                }
                flagRibbonIfSet(results, arguments, "ribbonbattleroyale");
                flagRibbonIfSet(results, arguments, "ribbonbattletreemaster");
                // Ultra Sun/Ultra Moon alone give the Great ribbon to a banned species.
                const bool isUltraSunMoon = isOriginVersion(arguments, Enums::GameVersion::US, Enums::GameVersion::UM);
                if (arguments.has("ribbonbattletreegreat") && !(isUltraSunMoon || !isUntraded(arguments.context)))
                {
                    addRibbonResult(results, arguments, "ribbonbattletreegreat");
                }
                return;
            }
            for (const std::string_view spriteStem : battleTreeRibbons)
            {
                flagRibbonIfSet(results, arguments, spriteStem);
            }
        }

        // ---- RibbonVerifierCommon8.cs ----

        void parseCommon8(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (isEgg)
            {
                for (const std::string_view spriteStem : {"ribbonchampiongalar", "ribbontowermaster", "ribbonmasterrank", "ribbontwinklingstar", "ribbonhisui"})
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                return;
            }
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            const Evolutions::EvolutionHistory &history = arguments.history;
            if (arguments.has("ribbontowermaster") && !isRibbonValidTowerMaster(history))
            {
                addRibbonResult(results, arguments, "ribbontowermaster");
            }
            const bool hasVisitedRanked = history.hasVisited(Enums::GameVersion::SWSH) || history.hasVisited(Enums::GameVersion::SV);
            if (!history.hasVisited(Enums::GameVersion::SWSH))
            {
                flagRibbonIfSet(results, arguments, "ribbonchampiongalar");
            }

            if (arguments.has("ribbonmasterrank") && !hasVisitedRanked)
            {
                addRibbonResult(results, arguments, "ribbonmasterrank");
            }
            else
            {
                const RibbonEncounter &encounter = arguments.encounter;
                if (!arguments.has("ribbonchampiongalar"))
                {
                    const bool hasChampionMemory = (encounter.generation == 8 && pokemon.otMemory() == MEMORY_CHAMPION) || (arguments.context.formatGeneration == 8 && pokemon.htMemory() == MEMORY_CHAMPION);
                    if (hasChampionMemory)
                    {
                        addRibbonResult(results, arguments, "ribbonchampiongalar", true);
                    }
                }
                if (arguments.has("ribbonmasterrank") && !isRibbonValidMasterRank(arguments))
                {
                    addRibbonResult(results, arguments, "ribbonmasterrank");
                }
                // The strongest-trainer memory also comes from Gen 6/7's Chatelaines, who bar legends.
                if (!arguments.has("ribbontowermaster") && (pokemon.otMemory() == MEMORY_STRONGEST || pokemon.htMemory() == MEMORY_STRONGEST))
                {
                    const bool lacksSkillful = arguments.find("ribbonbattlerskillful") != nullptr && !arguments.has("ribbonbattlerskillful");
                    if (encounter.generation == 8 || !isAllowedBattleFrontier(arguments.speciesId) || lacksSkillful)
                    {
                        addRibbonResult(results, arguments, "ribbontowermaster", true);
                    }
                }
            }

            if (arguments.has("ribbontwinklingstar") && !isRibbonValidTwinklingStar(arguments))
            {
                addRibbonResult(results, arguments, "ribbontwinklingstar");
            }
            // Earned in Legends: Arceus's photography studio.
            if (arguments.has("ribbonhisui") && !history.hasVisited(Enums::GameVersion::PLA))
            {
                addRibbonResult(results, arguments, "ribbonhisui");
            }
        }

        // ---- RibbonVerifierCommon9.cs ----

        void parseCommon9(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
        {
            if (isEgg)
            {
                for (const std::string_view spriteStem : {"ribbonchampionpaldea", "ribbononceinalifetime", "ribbonpartner"})
                {
                    flagRibbonIfSet(results, arguments, spriteStem);
                }
                return;
            }
            if (arguments.has("ribbonchampionpaldea") && !arguments.history.hasVisited(Enums::GameVersion::SV))
            {
                addRibbonResult(results, arguments, "ribbonchampionpaldea");
            }
            flagRibbonIfSet(results, arguments, "ribbononceinalifetime");
            // PKHeX `r.RibbonPartner != enc is IRibbonPartner { RibbonPartner: true }`.
            const std::optional<Encounters::TemplateRibbons> templateRibbons = getTemplateRibbons(arguments.encounter);
            if (templateRibbons.has_value())
            {
                // PKHeX reports either direction as a plain ribbon result, never as a missing one.
                if (arguments.has("ribbonpartner") != templateRibbons->has(Encounters::RibbonIndex::Partner))
                {
                    addRibbonResult(results, arguments, "ribbonpartner");
                }
                return;
            }
            // Only a Partner in-game trade (or a gift PKSE cannot see into) comes with the Partner Ribbon.
            const EncounterType encounterType = arguments.encounter.type;
            const bool mayBePartnerEncounter = encounterType == EncounterType::Trade || encounterType == EncounterType::Event || encounterType == EncounterType::Unchecked;
            if (!mayBePartnerEncounter)
            {
                flagRibbonIfSet(results, arguments, "ribbonpartner");
            }
        }
    }

    void addRibbonResult(RibbonResultList &results, const RibbonArguments &arguments, std::string_view spriteStem, bool isMissing)
    {
        const Names::RibbonDefinition *ribbon = arguments.find(spriteStem);
        if (ribbon != nullptr)
        {
            results.push_back(RibbonResult{ribbon, isMissing});
        }
    }

    void flagRibbonIfSet(RibbonResultList &results, const RibbonArguments &arguments, std::string_view spriteStem)
    {
        if (arguments.has(spriteStem))
        {
            addRibbonResult(results, arguments, spriteStem);
        }
    }

    const Names::RibbonDefinition *RibbonArguments::find(std::string_view spriteStem) const
    {
        for (const Names::RibbonDefinition &ribbon : ribbons)
        {
            if (spriteStem == ribbon.spriteStem)
            {
                return &ribbon;
            }
        }
        return nullptr;
    }

    uint8_t RibbonArguments::value(std::string_view spriteStem) const
    {
        const Names::RibbonDefinition *ribbon = find(spriteStem);
        if (ribbon == nullptr || entityData == nullptr)
        {
            return 0;
        }
        if (ribbon == forcedRibbon)
        {
            return std::max<uint8_t>(1, Names::getRibbonValue(entityData, *ribbon));
        }
        return Names::getRibbonValue(entityData, *ribbon);
    }

    RibbonArguments makeRibbonArguments(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        RibbonEncounter encounter;
        encounter.type = context.encounter.type;
        encounter.encounterMatch = Encounters::getEncounterMatch(context);
        if (encounter.encounterMatch != nullptr)
        {
            encounter.speciesId = encounter.encounterMatch->speciesId();
            encounter.formId = encounter.encounterMatch->formId();
            encounter.levelMin = encounter.encounterMatch->levelMin();
        }
        if (context.encounter.type == EncounterType::Unmatched)
        {
            // PKHeX EncounterInvalid: the Pokemon's own generation, in its own format's context.
            encounter.generation = getOriginGeneration(context);
            encounter.context = Evolutions::getEvolutionContextGroup(context.formatGroup);
        }
        else if (context.encounter.isValid() && context.encounter.originGroup == Enums::GameVersion::Invalid && encounter.encounterMatch != nullptr)
        {
            // Colosseum/XD has no save-format group; its template still knows its generation.
            encounter.generation = encounter.encounterMatch->generation();
            encounter.context = Evolutions::getEvolutionContextGroup(getOriginContextGroup(context));
        }
        else if (context.encounter.isValid())
        {
            encounter.generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(context.encounter.originGroup));
            encounter.context = Evolutions::getEvolutionContextGroup(context.encounter.originGroup);
        }
        else if (encounter.encounterMatch != nullptr)
        {
            // A Virtual Console transfer is judged as its Gen 7 transfer template.
            encounter.generation = encounter.encounterMatch->generation();
            encounter.context = Enums::GameVersion::SM;
        }
        else
        {
            encounter.generation = getOriginGeneration(context);
            encounter.context = Evolutions::getEvolutionContextGroup(getOriginContextGroup(context));
        }
        return RibbonArguments{context, Names::getRibbonDefinitions(context.formatGroup), reinterpret_cast<const uint8_t *>(pokemon.getData().data()), context.evolutionHistory(), encounter, pokemon.speciesID(), nullptr};
    }

    void getRibbonResults(const RibbonArguments &arguments, RibbonResultList &results)
    {
        if (arguments.ribbons.empty() || arguments.entityData == nullptr)
        {
            return;
        }
        const bool isEgg = arguments.context.pokemon.isEgg();
        const Names::RibbonDefinition *gen3Contest = arguments.find("ribbong3cool");
        if (gen3Contest != nullptr && gen3Contest->kind == Names::RibbonKind::ContestRank)
        {
            parseOnly3(arguments, results, isEgg);
        }
        if (arguments.find("ribbonearth") != nullptr)
        {
            parseEvent3(arguments, results, isEgg);
        }
        if (arguments.find("ribbonclassic") != nullptr)
        {
            parseEvent4(arguments, results, isEgg);
        }
        if (arguments.find("ribbonwinning") != nullptr)
        {
            parseUnique3(arguments, results, isEgg);
        }
        if (arguments.find("ribbonchampiong3") != nullptr)
        {
            parseCommon3(arguments, results, isEgg);
        }
        if (arguments.find("ribbonability") != nullptr)
        {
            parseUnique4(arguments, results, isEgg);
        }
        if (arguments.find("ribbonfootprint") != nullptr)
        {
            parseCommon4(arguments, results, isEgg);
        }
        if (arguments.find("ribbonchampionkalos") != nullptr)
        {
            parseCommon6(arguments, results, isEgg);
        }
        if (arguments.find("ribbonchampionalola") != nullptr)
        {
            parseCommon7(arguments, results, isEgg);
        }
        if (arguments.find("ribbonchampiongalar") != nullptr)
        {
            parseCommon8(arguments, results, isEgg);
        }
        if (arguments.find("ribbonchampionpaldea") != nullptr)
        {
            parseCommon9(arguments, results, isEgg);
        }
        if (arguments.find("ribbonmarkalpha") != nullptr)
        {
            parseRibbonSetMark9(arguments, results, isEgg);
        }
    }

    bool isAllowedBattleFrontier(uint16_t speciesId)
    {
        return !std::binary_search(BATTLE_FRONTIER_BANLIST.begin(), BATTLE_FRONTIER_BANLIST.end(), speciesId);
    }
}
