// Port of PKHeX PKHeX.Core/Legality/Verifiers/MemoryVerifier.cs: trainer memories, with the rules it reads
// from PKHeX.Core/Legality/Restrictions/Memories (Memories.cs, MemoryRules.cs, MemoryPermissions.cs,
// MemoryContext6.cs, MemoryContext8.cs).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Conversion/Convert.h"
#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterTrade6.h"
#include "Legality/Encounters/EncounterTrade7.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Legality/Verifiers/MemoryTable.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Names/ItemPresence.h"
#include "Names/ReleasedHeldItemTable.h"
#include "Names/RibbonNames.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t ORIGINAL_TRAINER = 0;
        constexpr uint8_t HANDLING_TRAINER = 1;

        // PKHeX MemorySource flags.
        constexpr uint8_t MEMORY_SOURCE_NONE = 0;
        constexpr uint8_t MEMORY_SOURCE_GENERATION_6 = 1 << 0;
        constexpr uint8_t MEMORY_SOURCE_BANK = 1 << 1;
        constexpr uint8_t MEMORY_SOURCE_SWSH = 1 << 2;
        constexpr uint8_t MEMORY_SOURCE_DELETED = 1 << 3;

        // PKHeX MemoryArgType.
        constexpr uint8_t MEMORY_ARGUMENT_GENERAL_LOCATION = 1;
        constexpr uint8_t MEMORY_ARGUMENT_SPECIFIC_LOCATION = 2;

        constexpr uint8_t MAX_MEMORY_ID_XY = 64;
        constexpr uint8_t MAX_MEMORY_ID_ORAS = 69;
        constexpr uint8_t MAX_MEMORY_ID_SWSH = 89;
        constexpr uint8_t MAX_MEMORY_INTENSITY = 7;
        constexpr uint16_t SWSH_POKEMON_CENTER_GENERAL_LOCATION = 9;
        constexpr uint16_t SWSH_SHARED_NEST_LOCATION = 162;
        constexpr uint8_t SWSH_DANGEROUS_PLACE_GENERAL_LOCATION = 79;
        constexpr uint16_t BOBS_FOOD_TIN_ITEM = 1085;
        constexpr uint16_t BACHS_FOOD_TIN_ITEM = 1086;
        constexpr uint16_t FIRST_TECHNICAL_RECORD_ITEM = 1130;
        constexpr uint16_t TECHNICAL_RECORD_COUNT = 100;
        constexpr std::string_view CURRY_MARK_SPRITE_STEM = "ribbonmarkcurry";

        // Memory ids whose rules single them out.
        constexpr uint8_t MEMORY_LINK_TRADE = 4;
        constexpr uint8_t MEMORY_HATCHED = 2;
        constexpr uint8_t MEMORY_CAUGHT = 1;
        constexpr uint8_t MEMORY_POKEMON_CENTER = 6;
        constexpr uint8_t MEMORY_CAPTURE_WITNESSED = 14;
        constexpr uint8_t MEMORY_BOX_INSPECTED = 85;

        constexpr uint16_t SMEARGLE_SPECIES = 235;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint16_t DITTO_SPECIES = 132;
        constexpr uint16_t RUNERIGUS_SPECIES = 867;
        constexpr uint16_t ZACIAN_SPECIES = 888;
        constexpr uint16_t ZAMAZENTA_SPECIES = 889;
        constexpr uint16_t ETERNATUS_SPECIES = 890;
        constexpr uint16_t URSHIFU_SPECIES = 892;
        constexpr uint16_t GLASTRIER_SPECIES = 896;
        constexpr uint16_t SPECTRIER_SPECIES = 897;
        constexpr uint16_t CALYREX_SPECIES = 898;

        constexpr uint16_t STRUGGLE_MOVE = 165;
        constexpr uint16_t BEHEMOTH_BLADE_MOVE = 781;
        constexpr uint16_t BEHEMOTH_BASH_MOVE = 782;
        constexpr uint16_t FIRST_DYNAMAX_MOVE = 757;
        constexpr uint16_t LAST_DYNAMAX_MOVE = 774;
        constexpr uint16_t MAX_MOVE_ID_ORAS = 621;
        // HM01-HM07 in the order HIDDEN_MACHINE_MEMORIES lists them.
        constexpr uint16_t HIDDEN_MACHINE_MOVES[] = {15, 19, 57, 70, 127, 249, 291};
        constexpr int SURF_HIDDEN_MACHINE_INDEX = 2;

        /// PKHeX Memories.GetContext: X/Y, Omega Ruby/Alpha Sapphire and Sun/Moon's memories follow
        /// Generation 6's rules, everything later Sword/Shield's.
        enum class MemoryContext : uint8_t
        {
            Generation6,
            Generation8,
        };

        /// PKHeX MemoryVariableSet.
        struct TrainerMemory
        {
            uint8_t handler;
            uint8_t memoryId;
            uint16_t variable;
            uint8_t intensity;
            uint8_t feeling;
        };

        /// A finding held back until the source it was judged against is chosen.
        struct MemoryFinding
        {
            Severity severity;
            ResultCode code;
            std::string firstArgument;
            std::string secondArgument;
        };

        using MemoryFindings = std::vector<MemoryFinding>;

        template <typename Element, size_t Count>
        bool containsValue(const Element (&table)[Count], uint32_t searchedValue)
        {
            for (const Element &entry : table)
            {
                if (entry == searchedValue)
                {
                    return true;
                }
            }
            return false;
        }

        const char *getHandlerName(uint8_t handler)
        {
            return handler == ORIGINAL_TRAINER ? "OT" : "HT";
        }

        void addInvalid(MemoryFindings &findings, ResultCode code, std::string firstArgument = "", std::string secondArgument = "")
        {
            findings.push_back(MemoryFinding{Severity::Invalid, code, std::move(firstArgument), std::move(secondArgument)});
        }

        bool hasNoInvalidFinding(const MemoryFindings &findings)
        {
            for (const MemoryFinding &finding : findings)
            {
                if (finding.severity == Severity::Invalid)
                {
                    return false;
                }
            }
            return true;
        }

        TrainerMemory readMemory(const Pokemon::Pokemon &pokemon, uint8_t handler)
        {
            if (handler == ORIGINAL_TRAINER)
            {
                return TrainerMemory{ORIGINAL_TRAINER, pokemon.otMemory(), pokemon.otMemoryVariable(), pokemon.otMemoryIntensity(), pokemon.otMemoryFeeling()};
            }
            return TrainerMemory{HANDLING_TRAINER, pokemon.htMemory(), pokemon.htMemoryVariable(), pokemon.htMemoryIntensity(), pokemon.htMemoryFeeling()};
        }

        Enums::GameVersion getVersion(const Pokemon::Pokemon &pokemon)
        {
            return static_cast<Enums::GameVersion>(pokemon.originGame());
        }

        MemoryContext getMemoryContext(Enums::GameVersion contextGroup)
        {
            return contextGroup == Enums::GameVersion::XY || contextGroup == Enums::GameVersion::SM ? MemoryContext::Generation6 : MemoryContext::Generation8;
        }

        bool isSpeciesInGame(uint16_t speciesId, uint8_t formId, uint8_t gameBit)
        {
            if (speciesId == 0 || speciesId > Pokemon::PERSONAL_MAX_SPECIES)
            {
                return false;
            }
            return (Pokemon::getPersonalInfo(speciesId, formId).presence & gameBit) != 0;
        }

        /// PKHeX MemoryRules.GetPossibleSources: which memory-writing games the Pokemon can have been in
        /// on its way from its encounter to its format.
        uint8_t getPossibleSources(const AnalysisContext &context)
        {
            const Evolutions::EvolutionHistory &history = context.evolutionHistory();
            uint8_t sources = MEMORY_SOURCE_NONE;
            if (history.hasVisited(Enums::GameVersion::XY))
            {
                sources |= MEMORY_SOURCE_GENERATION_6 | MEMORY_SOURCE_BANK;
            }
            if (history.hasVisited(Enums::GameVersion::SM))
            {
                sources |= MEMORY_SOURCE_BANK; // Generation 7 in-game trades also come with fixed memories.
            }
            if (history.hasVisited(Enums::GameVersion::SWSH))
            {
                sources |= MEMORY_SOURCE_SWSH;
            }
            if (history.hasVisited(Enums::GameVersion::SV) || history.hasVisited(Enums::GameVersion::ZA))
            {
                sources |= MEMORY_SOURCE_DELETED;
            }
            return sources;
        }

        /// The matched encounter's IsEgg; empty when the encounter is not known.
        std::optional<bool> isEncounterEgg(const AnalysisContext &context)
        {
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                return encounterMatch->isEgg();
            }
            return std::nullopt;
        }

        /// PKHeX MemoryRules.ReviseSourcesHandler.
        uint8_t reviseSourcesHandler(const AnalysisContext &context, uint8_t sources)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (isUntraded(context))
            {
                // Traded eggs in Sword/Shield get a handler memory but no handler name.
                if (getEncounterContextGroup(context) == Enums::GameVersion::SWSH && isEncounterEgg(context).value_or(true) && context.formatGroup == Enums::GameVersion::SWSH)
                {
                    const uint16_t location = pokemon.isEgg() ? pokemon.metLocation() : pokemon.eggLocation();
                    if (location == LOCATION_LINK_TRADE_6)
                    {
                        return sources;
                    }
                }
                return MEMORY_SOURCE_NONE;
            }
            // A Generation 6 or Bank memory would have been replaced on Switch, where the language is set.
            if (pokemon.hasHandlerLanguage() && pokemon.htLanguage() != 0)
            {
                sources &= static_cast<uint8_t>(~(MEMORY_SOURCE_GENERATION_6 | MEMORY_SOURCE_BANK));
            }
            return sources;
        }

        uint8_t getMaxLanguageIdForFormat(const AnalysisContext &context)
        {
            using Enums::LanguageID;
            switch (context.formatGeneration)
            {
            case 6:
                return static_cast<uint8_t>(LanguageID::Korean);
            case 7:
            case 8:
                return static_cast<uint8_t>(LanguageID::ChineseTraditional);
            case 9:
                return static_cast<uint8_t>(context.formatGroup == Enums::GameVersion::ZA ? LanguageID::SpanishL : LanguageID::ChineseTraditional);
            default:
                return 0;
            }
        }

        /// PKHeX MemoryVerifier.GetIsHTLanguageValid.
        bool isHandlerLanguageValid(const AnalysisContext &context, uint8_t source)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t language = pokemon.htLanguage();
            if (language > getMaxLanguageIdForFormat(context))
            {
                return false;
            }
            if (source == MEMORY_SOURCE_GENERATION_6 || source == MEMORY_SOURCE_BANK)
            {
                return language == 0;
            }
            const bool untraded = isUntraded(context);
            const bool validOtherwise = untraded ? language == 0 : true;
            // Scarlet/Violet gifts (EncounterStatic9.GiftWithLanguage) set the handler language too;
            // this check does not read that flag from the matched template yet, so either reading is accepted.
            const bool mayBeGiftWithLanguage = getEncounterContextGroup(context) == Enums::GameVersion::SV && (context.encounter.type == EncounterType::Gift || context.encounter.type == EncounterType::Static || context.encounter.type == EncounterType::Unchecked);
            if (mayBeGiftWithLanguage)
            {
                const bool validAsGift = language != 0 && (!untraded || language == pokemon.language());
                return validOtherwise || validAsGift;
            }
            return validOtherwise;
        }

        void verifyHandlerLanguage(const AnalysisContext &context, uint8_t source, MemoryFindings &findings)
        {
            if (!context.pokemon.hasHandlerLanguage())
            {
                return;
            }
            if (!isHandlerLanguageValid(context, source))
            {
                addInvalid(findings, ResultCode::MemoryHandlerLanguage);
            }
        }

        /// PKHeX MemoryVerifier.VerifyOTMemoryIs.
        void verifyOriginalTrainerMemoryIs(const Pokemon::Pokemon &pokemon, uint8_t memoryId, uint8_t intensity, uint16_t variable, uint8_t feeling, MemoryFindings &findings)
        {
            const char *handlerName = getHandlerName(ORIGINAL_TRAINER);
            if (pokemon.otMemory() != memoryId)
            {
                addInvalid(findings, ResultCode::MemoryIndexId, handlerName, std::to_string(memoryId));
            }
            if (pokemon.otMemoryIntensity() != intensity)
            {
                addInvalid(findings, ResultCode::MemoryIndexIntensity, handlerName, std::to_string(intensity));
            }
            if (pokemon.otMemoryVariable() != variable)
            {
                addInvalid(findings, ResultCode::MemoryIndexVariable, handlerName, std::to_string(variable));
            }
            if (pokemon.otMemoryFeeling() != feeling)
            {
                addInvalid(findings, ResultCode::MemoryIndexFeel, handlerName, std::to_string(feeling));
            }
        }

        void verifyHandlingTrainerMemoryNone(const Pokemon::Pokemon &pokemon, MemoryFindings &findings)
        {
            if (pokemon.htMemory() != 0 || pokemon.htMemoryVariable() != 0 || pokemon.htMemoryIntensity() != 0 || pokemon.htMemoryFeeling() != 0)
            {
                addInvalid(findings, ResultCode::MemoryCleared, getHandlerName(HANDLING_TRAINER));
            }
        }

        // ---- MemoryContext6 / MemoryContext8 ----

        bool canObtainMemoryXY(uint8_t memoryId)
        {
            return memoryId <= MAX_MEMORY_ID_XY && !containsValue(MEMORIES_NOT_IN_XY, memoryId);
        }

        bool canObtainMemoryORAS(uint8_t memoryId)
        {
            return memoryId <= MAX_MEMORY_ID_ORAS && !containsValue(MEMORIES_NOT_IN_ORAS, memoryId);
        }

        bool canObtainMemorySWSH(uint8_t memoryId)
        {
            return memoryId <= MAX_MEMORY_ID_SWSH && !containsValue(MEMORIES_NOT_IN_SWSH, memoryId);
        }

        bool canObtainMemoryOT(MemoryContext memoryContext, Enums::GameVersion version, uint8_t memoryId)
        {
            using Enums::GameVersion;
            if (memoryContext == MemoryContext::Generation6)
            {
                if (version == GameVersion::X || version == GameVersion::Y)
                {
                    return canObtainMemoryXY(memoryId);
                }
                if (version == GameVersion::OR || version == GameVersion::AS)
                {
                    return canObtainMemoryORAS(memoryId);
                }
                return false;
            }
            if (version == GameVersion::SW || version == GameVersion::SH)
            {
                return canObtainMemorySWSH(memoryId);
            }
            return memoryId == 0;
        }

        bool canObtainMemoryHT(MemoryContext memoryContext, uint8_t memoryId)
        {
            return memoryContext == MemoryContext::Generation6 ? memoryId <= MAX_MEMORY_ID_ORAS : canObtainMemorySWSH(memoryId);
        }

        bool hasPokemonCenter(MemoryContext memoryContext, Enums::GameVersion version, uint16_t location)
        {
            using Enums::GameVersion;
            if (memoryContext == MemoryContext::Generation8)
            {
                return location == SWSH_POKEMON_CENTER_GENERAL_LOCATION;
            }
            if (location > UINT8_MAX)
            {
                return false;
            }
            if (version == GameVersion::Any)
            {
                return containsValue(POKEMON_CENTER_LOCATIONS_XY, location) || containsValue(POKEMON_CENTER_LOCATIONS_ORAS, location);
            }
            if (version == GameVersion::X || version == GameVersion::Y)
            {
                return containsValue(POKEMON_CENTER_LOCATIONS_XY, location);
            }
            return containsValue(POKEMON_CENTER_LOCATIONS_ORAS, location);
        }

        template <size_t Count>
        bool isFlagSet(const uint8_t (&flags)[Count], uint16_t speciesId)
        {
            const size_t byteIndex = speciesId >> 3;
            if (byteIndex >= Count)
            {
                return false;
            }
            return (flags[byteIndex] & (1 << (speciesId & 7))) != 0;
        }

        /// PKHeX MemoryPermissions.GetCanBeCaptured.
        bool canBeCaptured(uint16_t speciesId, MemoryContext memoryContext, Enums::GameVersion version)
        {
            using Enums::GameVersion;
            if (memoryContext == MemoryContext::Generation6)
            {
                switch (version)
                {
                case GameVersion::Any:
                    return isFlagSet(CAPTURE_FLAGS_X, speciesId) || isFlagSet(CAPTURE_FLAGS_Y, speciesId) || isFlagSet(CAPTURE_FLAGS_AS, speciesId) || isFlagSet(CAPTURE_FLAGS_OR, speciesId);
                case GameVersion::X:
                    return isFlagSet(CAPTURE_FLAGS_X, speciesId);
                case GameVersion::Y:
                    return isFlagSet(CAPTURE_FLAGS_Y, speciesId);
                case GameVersion::AS:
                    return isFlagSet(CAPTURE_FLAGS_AS, speciesId);
                case GameVersion::OR:
                    return isFlagSet(CAPTURE_FLAGS_OR, speciesId);
                default:
                    return false;
                }
            }
            switch (version)
            {
            case GameVersion::Any:
                return isFlagSet(CAPTURE_FLAGS_SW, speciesId) || isFlagSet(CAPTURE_FLAGS_SH, speciesId);
            case GameVersion::SW:
                return isFlagSet(CAPTURE_FLAGS_SW, speciesId);
            case GameVersion::SH:
                return isFlagSet(CAPTURE_FLAGS_SH, speciesId);
            default:
                return false;
            }
        }

        /// PKHeX MemoryPermissions.GetCanFishSpecies.
        bool canFishSpecies(uint16_t speciesId, MemoryContext memoryContext, Enums::GameVersion version)
        {
            using Enums::GameVersion;
            if (memoryContext == MemoryContext::Generation6)
            {
                switch (version)
                {
                case GameVersion::Any:
                    return containsValue(FISHING_SPECIES_XY, speciesId) || containsValue(FISHING_SPECIES_ORAS, speciesId) || containsValue(FISHING_SPECIES_X, speciesId) || containsValue(FISHING_SPECIES_Y, speciesId);
                case GameVersion::X:
                    return containsValue(FISHING_SPECIES_XY, speciesId) || containsValue(FISHING_SPECIES_X, speciesId);
                case GameVersion::Y:
                    return containsValue(FISHING_SPECIES_XY, speciesId) || containsValue(FISHING_SPECIES_Y, speciesId);
                case GameVersion::OR:
                case GameVersion::AS:
                    return containsValue(FISHING_SPECIES_ORAS, speciesId);
                default:
                    return false;
                }
            }
            if (version == GameVersion::Any || version == GameVersion::SW || version == GameVersion::SH)
            {
                return containsValue(FISHING_SPECIES_SWSH, speciesId);
            }
            return false;
        }

        /// PKHeX MemoryPermissions.GetCanDynamaxTrainer (always asked about Sword/Shield).
        bool canDynamaxTrainer(uint16_t speciesId, Enums::GameVersion version)
        {
            const bool inBoth = containsValue(DYNAMAX_TRAINER_SPECIES_SWSH, speciesId);
            if (version == Enums::GameVersion::SW)
            {
                return inBoth || containsValue(DYNAMAX_TRAINER_SPECIES_SW, speciesId);
            }
            if (version == Enums::GameVersion::SH)
            {
                return inBoth || containsValue(DYNAMAX_TRAINER_SPECIES_SH, speciesId);
            }
            return inBoth || containsValue(DYNAMAX_TRAINER_SPECIES_SW, speciesId) || containsValue(DYNAMAX_TRAINER_SPECIES_SH, speciesId);
        }

        bool canBuyItem(MemoryContext memoryContext, uint16_t itemId, Enums::GameVersion version)
        {
            using Enums::GameVersion;
            if (memoryContext == MemoryContext::Generation6)
            {
                if (version == GameVersion::Any)
                {
                    return containsValue(PURCHASABLE_ITEMS_ORAS, itemId) || containsValue(PURCHASABLE_ITEMS_XY, itemId);
                }
                if (version == GameVersion::X || version == GameVersion::Y)
                {
                    return containsValue(PURCHASABLE_ITEMS_XY, itemId);
                }
                if (version == GameVersion::AS || version == GameVersion::OR)
                {
                    return containsValue(PURCHASABLE_ITEMS_ORAS, itemId);
                }
                return false;
            }
            if (itemId == BOBS_FOOD_TIN_ITEM)
            {
                return version == GameVersion::SW || version == GameVersion::Any;
            }
            if (itemId == BACHS_FOOD_TIN_ITEM)
            {
                return version == GameVersion::SH || version == GameVersion::Any;
            }
            const bool isTechnicalRecord = itemId >= FIRST_TECHNICAL_RECORD_ITEM && itemId < FIRST_TECHNICAL_RECORD_ITEM + TECHNICAL_RECORD_COUNT;
            return isTechnicalRecord || containsValue(PURCHASABLE_ITEMS_SWSH, itemId);
        }

        bool canPlantBerry(MemoryContext memoryContext, uint16_t itemId)
        {
            return memoryContext == MemoryContext::Generation6 && containsValue(BERRIES_XY, itemId);
        }

        bool canHoldItem(MemoryContext memoryContext, uint16_t itemId)
        {
            return Names::isHeldItemReleased(itemId, memoryContext == MemoryContext::Generation6 ? Enums::GameVersion::XY : Enums::GameVersion::SWSH);
        }

        bool canWinLotoId(MemoryContext memoryContext, uint16_t itemId)
        {
            if (memoryContext == MemoryContext::Generation6)
            {
                return containsValue(LOTO_ID_PRIZES_GENERATION_6, itemId);
            }
            return itemId < UINT8_MAX && containsValue(LOTO_ID_PRIZES_SWSH, itemId);
        }

        bool canHaveIntensity(MemoryContext memoryContext, uint8_t memoryId, uint8_t intensity)
        {
            if (intensity > MAX_MEMORY_INTENSITY)
            {
                return false;
            }
            if (memoryContext == MemoryContext::Generation6)
            {
                return memoryId < std::size(MEMORY_FEELINGS_GENERATION_6) && MEMORY_MINIMUM_INTENSITY_GENERATION_6[memoryId] <= intensity;
            }
            return memoryId < std::size(MEMORY_FEELINGS_GENERATION_8) && MEMORY_MINIMUM_INTENSITY_GENERATION_8[memoryId] <= intensity;
        }

        uint8_t getMinimumIntensity(MemoryContext memoryContext, uint8_t memoryId)
        {
            if (memoryContext == MemoryContext::Generation6)
            {
                return memoryId < std::size(MEMORY_MINIMUM_INTENSITY_GENERATION_6) ? MEMORY_MINIMUM_INTENSITY_GENERATION_6[memoryId] : 0;
            }
            return memoryId < std::size(MEMORY_MINIMUM_INTENSITY_GENERATION_8) ? MEMORY_MINIMUM_INTENSITY_GENERATION_8[memoryId] : 0;
        }

        bool canHaveFeeling(MemoryContext memoryContext, uint8_t memoryId, uint8_t feeling, uint16_t variable)
        {
            if (memoryContext == MemoryContext::Generation6)
            {
                if (memoryId >= std::size(MEMORY_FEELINGS_GENERATION_6))
                {
                    return false;
                }
                // Bank's "came through Link Trade somewhere" rolls 0-9 rather than reading the table.
                if (memoryId == MEMORY_LINK_TRADE && variable == 0)
                {
                    return feeling < 10;
                }
                return feeling < 32 && (MEMORY_FEELINGS_GENERATION_6[memoryId] & (1u << feeling)) != 0;
            }
            // Sword/Shield store the feeling plus one, so 0 is never written.
            if (memoryId >= std::size(MEMORY_FEELINGS_GENERATION_8) || feeling == 0)
            {
                return false;
            }
            return feeling <= 32 && (MEMORY_FEELINGS_GENERATION_8[memoryId] & (1u << (feeling - 1))) != 0;
        }

        bool isGeneralLocationSWSH(uint8_t location)
        {
            return containsValue(GENERAL_LOCATIONS_SWSH, location);
        }

        /// PKHeX MemoryContext8.IsInvalidGenLoc8Other.
        bool isInvalidGeneralLocationOther(uint8_t memoryId, uint16_t variable)
        {
            if (variable > UINT8_MAX)
            {
                return true;
            }
            const uint8_t location = static_cast<uint8_t>(variable);
            static constexpr uint8_t bikeLocations[] = {1, 8, 12, 22, 33, 35, 37, 40, 41, 44, 47, 48, 49, 50, 51, 53, 65, 71, 72, 75, 76, 77, 78};
            static constexpr uint8_t pickUpLocations[] = {8, 12, 22, 33, 35, 37, 40, 41, 44, 47, 48, 49, 50, 51, 53, 65, 71, 72, 75, 76, 77};
            static constexpr uint8_t signLocations[] = {1, 8, 12, 22, 33, 35, 37, 44, 47, 53, 71, 72, 76, 77};
            static constexpr uint8_t benchLocations[] = {8, 12, 22, 28, 33, 35, 37, 38, 44, 53, 77};
            switch (memoryId)
            {
            case MEMORY_LINK_TRADE:
                // Every "dangerous place" is locked behind the Y-Comm.
                return !isGeneralLocationSWSH(location) || location == SWSH_DANGEROUS_PLACE_GENERAL_LOCATION;
            case 32:
                return !containsValue(bikeLocations, location);
            case 39:
                return !containsValue(pickUpLocations, location);
            case 42:
                return !containsValue(signLocations, location);
            case 70:
                return !containsValue(benchLocations, location);
            default:
                return !isGeneralLocationSWSH(location);
            }
        }

        /// PKHeX IRibbonSetMark8.RibbonMarkCurry, or (for Shedinja, which keeps none of Nincada's marks)
        /// the Curry Mark as its affixed ribbon.
        bool hasCurryMark(const AnalysisContext &context, bool includeAffixedShedinja)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
            for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(context.formatGroup))
            {
                if (std::string_view(ribbon.spriteStem) != CURRY_MARK_SPRITE_STEM)
                {
                    continue;
                }
                if (Names::getRibbonValue(entityData, ribbon) != 0)
                {
                    return true;
                }
                if (!includeAffixedShedinja || pokemon.speciesID() != SHEDINJA_SPECIES)
                {
                    return false;
                }
                const size_t affixedOffset = Conversion::affixedRibbonOffset(context.formatGroup);
                if (affixedOffset == 0 || affixedOffset >= pokemon.getData().size())
                {
                    return false;
                }
                const uint8_t affixedValue = static_cast<uint8_t>(pokemon.getData()[affixedOffset]);
                return affixedValue != Conversion::AFFIXED_RIBBON_NONE && static_cast<int>(affixedValue) == Names::getRibbonAffixIndex(ribbon);
            }
            return false;
        }

        /// PKHeX MemoryContext8.IsWildEncounter; empty when the encounter is not known.
        std::optional<bool> isWildEncounterSWSH(const AnalysisContext &context)
        {
            const EncounterInfo &encounter = context.encounter;
            // PKSE's search misses encounters PKHeX finds (Wild Area levels), so an unmatched Pokemon's
            // encounter kind is unknown here rather than PKHeX's EncounterInvalid.
            if (encounter.type == EncounterType::Unchecked || encounter.type == EncounterType::Unmatched)
            {
                return std::nullopt;
            }
            const bool isSWSH = encounter.originGroup == Enums::GameVersion::SWSH;
            const bool isWildKind = encounter.type == EncounterType::Wild || encounter.type == EncounterType::Static || encounter.type == EncounterType::Raid;
            if (!isSWSH || !isWildKind)
            {
                return false;
            }
            return !hasCurryMark(context, true);
        }

        /// PKHeX MemoryContext8.IsWildEncounterMeet; empty where PKSE cannot tell (whether a Sword/Shield
        /// Mystery Gift was a HOME gift).
        std::optional<bool> isWildEncounterMeetSWSH(const AnalysisContext &context)
        {
            const EncounterInfo &encounter = context.encounter;
            // PKSE's search misses encounters PKHeX finds (Wild Area levels), so an unmatched Pokemon's
            // encounter kind is unknown here rather than PKHeX's EncounterInvalid.
            if (encounter.type == EncounterType::Unchecked || encounter.type == EncounterType::Unmatched)
            {
                return std::nullopt;
            }
            if (encounter.originGroup != Enums::GameVersion::SWSH)
            {
                return false;
            }
            switch (encounter.type)
            {
            case EncounterType::Trade:
            case EncounterType::Gift:
                return true;
            case EncounterType::Event:
                return std::nullopt;
            case EncounterType::Wild:
                // PKHeX IsCurryEncounter: Shedinja counts whatever its marks say.
                return context.pokemon.speciesID() == SHEDINJA_SPECIES || hasCurryMark(context, false);
            default:
                return false;
            }
        }

        /// PKHeX MemoryContext8.IsInvalidGenLoc8: the memories that name where the Pokemon was met.
        std::optional<bool> isInvalidEncounterGeneralLocation(const AnalysisContext &context, uint8_t memoryId, uint16_t variable)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (variable > UINT8_MAX)
            {
                return true;
            }
            std::optional<bool> encounterAllows = true;
            if (memoryId == MEMORY_CAUGHT)
            {
                encounterAllows = isWildEncounterSWSH(context);
            }
            else if (memoryId == MEMORY_HATCHED)
            {
                encounterAllows = isEncounterEgg(context);
            }
            else
            {
                encounterAllows = isWildEncounterMeetSWSH(context);
            }
            if (!encounterAllows.has_value())
            {
                return std::nullopt;
            }
            if (!*encounterAllows)
            {
                return true;
            }
            const uint8_t location = static_cast<uint8_t>(variable);
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation > UINT8_MAX)
            {
                return memoryId != 3 || !isGeneralLocationSWSH(location); // a gift
            }
            if (memoryId == MEMORY_HATCHED && pokemon.eggLocation() == 0)
            {
                return true;
            }
            if (metLocation == SWSH_SHARED_NEST_LOCATION)
            {
                return !isGeneralLocationSWSH(location) || location == SWSH_DANGEROUS_PLACE_GENERAL_LOCATION;
            }
            const size_t areaIndex = metLocation >> 1;
            if ((metLocation & 1) == 0 && areaIndex < std::size(GENERAL_LOCATION_BY_MET_LOCATION_SWSH) && GENERAL_LOCATION_BY_MET_LOCATION_SWSH[areaIndex] != 0)
            {
                return location != GENERAL_LOCATION_BY_MET_LOCATION_SWSH[areaIndex];
            }
            for (const MetLocationGeneralLocations &row : MET_LOCATION_GENERAL_LOCATIONS_SWSH)
            {
                if (row.metLocation != metLocation)
                {
                    continue;
                }
                for (uint8_t locationOffset = 0; locationOffset < row.locationCount; ++locationOffset)
                {
                    if (GENERAL_LOCATIONS_BY_MET_LOCATION_SWSH[row.firstIndex + locationOffset] == location)
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }

        /// PKHeX MemoryContext.IsInvalidGeneralLocationMemoryValue.
        std::optional<bool> isInvalidGeneralLocationMemory(const AnalysisContext &context, MemoryContext memoryContext, uint8_t memoryId, uint16_t variable)
        {
            if (memoryContext == MemoryContext::Generation6 || memoryId >= std::size(MEMORY_ARGUMENT_TYPES))
            {
                return false;
            }
            // Generation 8 reads a specific location as a general one (PKHeX Memories.GetMemoryArgType).
            const uint8_t argumentType = MEMORY_ARGUMENT_TYPES[memoryId];
            if (argumentType != MEMORY_ARGUMENT_GENERAL_LOCATION && argumentType != MEMORY_ARGUMENT_SPECIFIC_LOCATION)
            {
                return false;
            }
            if (memoryId == 1 || memoryId == 2 || memoryId == 3)
            {
                return isInvalidEncounterGeneralLocation(context, memoryId, variable);
            }
            return isInvalidGeneralLocationOther(memoryId, variable);
        }

        /// PKHeX MemoryContext8.IsValidMemory29: "encountered {2} when it was with {1}".
        bool isValidLegendEncounterMemory(uint16_t encounteredSpecies, uint16_t speciesId, Enums::GameVersion version, uint8_t handler)
        {
            if (encounteredSpecies != ZACIAN_SPECIES && encounteredSpecies != ZAMAZENTA_SPECIES && encounteredSpecies != ETERNATUS_SPECIES && encounteredSpecies != CALYREX_SPECIES)
            {
                return false;
            }
            if (handler == HANDLING_TRAINER)
            {
                return true; // met in another game, so story progress does not apply
            }
            if (version == Enums::GameVersion::SW && encounteredSpecies == ZAMAZENTA_SPECIES)
            {
                return false;
            }
            if (version == Enums::GameVersion::SH && encounteredSpecies == ZACIAN_SPECIES)
            {
                return false;
            }
            // These legends are caught only after Eternatus.
            if (speciesId == ZACIAN_SPECIES || speciesId == ZAMAZENTA_SPECIES || speciesId == GLASTRIER_SPECIES || speciesId == SPECTRIER_SPECIES || speciesId == CALYREX_SPECIES)
            {
                return encounteredSpecies != ETERNATUS_SPECIES;
            }
            return true;
        }

        bool isInvalidMiscMemory(MemoryContext memoryContext, const TrainerMemory &memory, uint16_t speciesId, Enums::GameVersion version)
        {
            return memoryContext == MemoryContext::Generation8 && memory.memoryId == 29 && !isValidLegendEncounterMemory(memory.variable, speciesId, version, memory.handler);
        }

        /// Learnable in any of the games whose moves a memory of this context can refer to. PKSE's
        /// learnsets are each game's whole pool (every method, pre-evolutions included), so this is a
        /// superset of PKHeX's LearnGroup6/LearnGroup8 answer.
        bool isLearnableInMemoryGames(const AnalysisContext &context, MemoryContext memoryContext, uint16_t moveId)
        {
            using Enums::GameVersion;
            const Pokemon::Pokemon &pokemon = context.pokemon;
            static constexpr GameVersion generation6Groups[] = {GameVersion::XY, GameVersion::ORAS, GameVersion::B2W2, GameVersion::BW, GameVersion::HGSS, GameVersion::PT, GameVersion::DP, GameVersion::RSE, GameVersion::FRLG};
            static constexpr GameVersion generation8Groups[] = {GameVersion::SWSH, GameVersion::USUM, GameVersion::SM, GameVersion::XY, GameVersion::ORAS, GameVersion::B2W2, GameVersion::BW, GameVersion::HGSS, GameVersion::PT, GameVersion::DP, GameVersion::RSE, GameVersion::FRLG, GameVersion::GSC, GameVersion::RBY, GameVersion::BDSP, GameVersion::PLA, GameVersion::SV, GameVersion::GG};
            auto isLearnableIn = [&](GameVersion group)
            {
                return Pokemon::isLearnable(pokemon.speciesID(), pokemon.form(), group, moveId);
            };
            if (memoryContext == MemoryContext::Generation6)
            {
                for (GameVersion group : generation6Groups)
                {
                    if (isLearnableIn(group))
                    {
                        return true;
                    }
                }
                return false;
            }
            for (GameVersion group : generation8Groups)
            {
                if (isLearnableIn(group))
                {
                    return true;
                }
            }
            return false;
        }

        bool hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId)
        {
            for (int moveSlot = 0; moveSlot < 4; ++moveSlot)
            {
                if (pokemon.move(moveSlot) == moveId)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX MemoryPermissions.CanKnowMove; empty where the answer rests on data PKSE lacks (an
        /// encounter's fixed moves, the moves Sword/Shield dummied out).
        std::optional<bool> canKnowMove(const AnalysisContext &context, const TrainerMemory &memory, MemoryContext memoryContext, bool battleOnly)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t moveId = memory.variable;
            if (moveId == 0)
            {
                return false;
            }
            if (hasMove(pokemon, moveId))
            {
                return true;
            }
            if (pokemon.isEgg())
            {
                return false;
            }
            if (pokemon.speciesID() == SMEARGLE_SPECIES)
            {
                if (Pokemon::isLearnable(SMEARGLE_SPECIES, 0, memoryContext == MemoryContext::Generation6 ? Enums::GameVersion::ORAS : Enums::GameVersion::SWSH, moveId))
                {
                    return true;
                }
            }
            else if (isLearnableInMemoryGames(context, memoryContext, moveId))
            {
                return true;
            }
            if (battleOnly)
            {
                if ((moveId == BEHEMOTH_BLADE_MOVE && pokemon.speciesID() == ZACIAN_SPECIES) || (moveId == BEHEMOTH_BASH_MOVE && pokemon.speciesID() == ZAMAZENTA_SPECIES))
                {
                    return true;
                }
                if (memoryContext == MemoryContext::Generation8 && moveId >= FIRST_DYNAMAX_MOVE && moveId <= LAST_DYNAMAX_MOVE)
                {
                    return true;
                }
                if (pokemon.speciesID() == DITTO_SPECIES)
                {
                    if (moveId == STRUGGLE_MOVE)
                    {
                        return false;
                    }
                    if (memoryContext == MemoryContext::Generation6)
                    {
                        return moveId <= MAX_MOVE_ID_ORAS;
                    }
                    return std::nullopt;
                }
            }
            // The encounter's own moves and relearn moves count too, and PKSE's rows do not carry them.
            switch (context.encounter.type)
            {
            case EncounterType::Wild:
            case EncounterType::Egg:
            case EncounterType::Unmatched:
                return false;
            default:
                return std::nullopt;
            }
        }

        /// PKHeX MemoryPermissions.GetCanRelearnMove: a move reminder move is always in the game's pool.
        bool canRelearnMove(const AnalysisContext &context, uint16_t moveId, MemoryContext memoryContext)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (memoryContext == MemoryContext::Generation6)
            {
                return Pokemon::isLearnable(pokemon.speciesID(), pokemon.form(), Enums::GameVersion::XY, moveId) || Pokemon::isLearnable(pokemon.speciesID(), pokemon.form(), Enums::GameVersion::ORAS, moveId);
            }
            return Pokemon::isLearnable(pokemon.speciesID(), pokemon.form(), Enums::GameVersion::SWSH, moveId);
        }

        /// PKHeX PersonalInfo6AO.GetIsLearnHM: can this Omega Ruby/Alpha Sapphire species use the HM?
        bool canLearnHiddenMachineORAS(uint16_t speciesId, uint8_t formId, int hiddenMachineIndex)
        {
            if (speciesId == 0 || Pokemon::getPersonalRecord(Enums::GameVersion::ORAS, speciesId, formId).hp == 0)
            {
                return false;
            }
            return Pokemon::isLearnable(speciesId, formId, Enums::GameVersion::ORAS, HIDDEN_MACHINE_MOVES[hiddenMachineIndex]);
        }

        // ---- MemoryVerifier ----

        void verifyCommonMemoryRest(const TrainerMemory &memory, MemoryContext memoryContext, MemoryFindings &findings)
        {
            const char *handlerName = getHandlerName(memory.handler);
            if (!canHaveIntensity(memoryContext, memory.memoryId, memory.intensity))
            {
                addInvalid(findings, ResultCode::MemoryIndexIntensityMin, handlerName, std::to_string(getMinimumIntensity(memoryContext, memory.memoryId)));
                return;
            }
            if (!canHaveFeeling(memoryContext, memory.memoryId, memory.feeling, memory.variable))
            {
                addInvalid(findings, ResultCode::MemoryFeelInvalid, handlerName);
            }
        }

        void verifyHiddenMachineMemory(const AnalysisContext &context, MemoryContext memoryContext, const TrainerMemory &memory, int hiddenMachineIndex, MemoryFindings &findings)
        {
            const char *handlerName = getHandlerName(memory.handler);
            if (memoryContext != MemoryContext::Generation6)
            {
                addInvalid(findings, ResultCode::MemoryArgumentBadMove, handlerName); // Sword/Shield has no HMs
                return;
            }
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch != nullptr && encounterMatch->speciesId() == SMEARGLE_SPECIES)
            {
                verifyCommonMemoryRest(memory, memoryContext, findings);
                return;
            }
            // Omega Ruby/Alpha Sapphire's HM permissions cover Generations 3-5, so the current Generation 6
            // species alone decides.
            const Evolutions::EvolutionHistory &history = context.evolutionHistory();
            const std::span<const Evolutions::EvolutionCriteria> generation6Stages = history.getChain(Enums::GameVersion::XY);
            if (!generation6Stages.empty() && canLearnHiddenMachineORAS(generation6Stages.front().speciesId, generation6Stages.front().formId, hiddenMachineIndex))
            {
                verifyCommonMemoryRest(memory, memoryContext, findings);
                return;
            }
            addInvalid(findings, ResultCode::MemoryArgumentBadMove, handlerName);
        }

        void verifyCommonMemory(const AnalysisContext &context, uint8_t handler, MemoryContext memoryContext, MemoryFindings &findings)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const TrainerMemory memory = readMemory(pokemon, handler);
            const char *handlerName = getHandlerName(handler);
            const std::string variableText = std::to_string(memory.variable);
            const Enums::GameVersion version = getVersion(pokemon);
            const Enums::GameVersion handlerVersion = handler == ORIGINAL_TRAINER ? version : Enums::GameVersion::Any;

            for (size_t hiddenMachineIndex = 0; hiddenMachineIndex < std::size(HIDDEN_MACHINE_MEMORIES); ++hiddenMachineIndex)
            {
                if (HIDDEN_MACHINE_MEMORIES[hiddenMachineIndex] == memory.memoryId)
                {
                    verifyHiddenMachineMemory(context, memoryContext, memory, static_cast<int>(hiddenMachineIndex), findings);
                    return;
                }
            }

            const std::optional<bool> invalidLocation = isInvalidGeneralLocationMemory(context, memoryContext, memory.memoryId, memory.variable);
            if (!invalidLocation.has_value())
            {
                return;
            }
            if (*invalidLocation)
            {
                addInvalid(findings, ResultCode::MemoryArgumentBadLocation, handlerName);
                return;
            }
            if (isInvalidMiscMemory(memoryContext, memory, pokemon.speciesID(), version))
            {
                addInvalid(findings, ResultCode::MemoryArgumentBadId, handlerName);
                return;
            }

            switch (memory.memoryId)
            {
            case 19:
                // A tall building is the only place Urshifu evolves, a vast field the only place Runerigus does.
                if ((pokemon.speciesID() == URSHIFU_SPECIES && memory.variable != 34) || (pokemon.speciesID() == RUNERIGUS_SPECIES && memory.variable != 72))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadLocation, handlerName);
                    return;
                }
                break;
            case 21:
                if (memoryContext != MemoryContext::Generation6 || !canLearnHiddenMachineORAS(memory.variable, 0, SURF_HIDDEN_MACHINE_INDEX))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadMove, handlerName);
                    return;
                }
                break;
            case 16:
            case 48:
            case 80:
            case 81:
            {
                const std::optional<bool> knowsMove = canKnowMove(context, memory, memoryContext, memory.memoryId == 16);
                if (!knowsMove.has_value())
                {
                    return;
                }
                if (!*knowsMove)
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadMove, handlerName);
                    return;
                }
                break;
            }
            case 49:
                if (memory.variable == 0 || !canRelearnMove(context, memory.variable, memoryContext))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadMove, handlerName);
                    return;
                }
                break;
            case 71:
                if (!canDynamaxTrainer(memory.variable, handlerVersion))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, variableText);
                    return;
                }
                break;
            case 72:
            case 82:
            case 83:
            case 87:
                if (!isSpeciesInGame(memory.variable, 0, Pokemon::PERSONAL_GAME_SWSH))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, variableText);
                    return;
                }
                break;
            case 7:
                if (!canFishSpecies(memory.variable, memoryContext, handlerVersion))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, variableText);
                    return;
                }
                break;
            case 9:
            case 60:
            case 75:
                if (memoryContext == MemoryContext::Generation8 && !isSpeciesInGame(memory.variable, 0, Pokemon::PERSONAL_GAME_SWSH))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, variableText);
                    return;
                }
                break;
            case 53:
                if (memoryContext == MemoryContext::Generation8 && pokemon.hasHyperTraining() && !pokemon.isHyperTrained())
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadId, handlerName);
                    return;
                }
                break;
            case 5:
            case 34:
            case 40:
            case 51:
            case 84:
            case 88:
            {
                bool itemAllowed = true;
                if (memory.memoryId == 5)
                {
                    itemAllowed = canBuyItem(memoryContext, memory.variable, handlerVersion);
                }
                else if (memory.memoryId == 34)
                {
                    itemAllowed = canPlantBerry(memoryContext, memory.variable);
                }
                else if (memory.memoryId == 40)
                {
                    itemAllowed = canHoldItem(memoryContext, memory.variable);
                }
                else if (memory.memoryId == 51)
                {
                    itemAllowed = canWinLotoId(memoryContext, memory.variable);
                }
                else
                {
                    itemAllowed = Names::isHeldItemPresent(memory.variable, Enums::GameVersion::SWSH) && !pokemon.isEgg();
                }
                if (!itemAllowed)
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadItem, handlerName, variableText);
                    return;
                }
                break;
            }
            default:
                break;
            }
            verifyCommonMemoryRest(memory, memoryContext, findings);
        }

        /// PKHeX MemoryVerifier.CanHaveMemoryForOT, for a Pokemon that is not an egg.
        bool canHaveMemoryForOriginalTrainer(const Pokemon::Pokemon &pokemon, Enums::GameVersion encounterContext, uint8_t memoryId)
        {
            using Enums::GameVersion;
            switch (encounterContext)
            {
            case GameVersion::RBY:
            case GameVersion::GSC:
            case GameVersion::SM:
                return memoryId == MEMORY_LINK_TRADE; // Virtual Console transfers get only Bank's memory
            case GameVersion::XY:
                return true;
            case GameVersion::SWSH:
                return !(isGoHome(pokemon) || pokemon.metLocation() == LOCATION_HOME_GIFT); // HOME writes no memories
            default:
                return false;
            }
        }

        /// The encounter fixes the original trainer's memory to values PKSE's rows do not carry (a
        /// Generation 6-8 Mystery Gift or Generation 6/8 in-game trade), so it cannot be checked.
        bool hasUnknownFixedOriginalTrainerMemory(const AnalysisContext &context, Enums::GameVersion encounterContext)
        {
            using Enums::GameVersion;
            const bool fixesMemories = encounterContext == GameVersion::XY || encounterContext == GameVersion::SM || encounterContext == GameVersion::SWSH;
            if (!fixesMemories)
            {
                return false;
            }
            const EncounterInfo &encounter = context.encounter;
            switch (encounter.type)
            {
            case EncounterType::Event:
            {
                const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                return encounterMatch == nullptr || !encounterMatch->isEgg();
            }
            case EncounterType::Trade:
                return encounterContext != GameVersion::SM;
            case EncounterType::Unchecked:
                return context.pokemon.isFatefulEncounter() || !isPlayerOriginalTrainer(context);
            default:
                return false;
            }
        }

        /// A memory the encounter gives its original trainer and nothing can replace.
        struct FixedTrainerMemory
        {
            uint8_t memoryId = 0;
            uint8_t intensity = 0;
            uint16_t variable = 0;
            uint8_t feeling = 0;
        };

        /// The card's own original trainer is its distributor's, so no player ever handles it as
        /// the original trainer (PKHeX `OTGender != 3`).
        constexpr uint8_t CARD_TRAINER_GENDER_RECIPIENT = 3;

        template <typename Card>
        std::optional<FixedTrainerMemory> getCardTrainerMemory(const Card &card)
        {
            // PKHeX's AllowHandleOTGen6/7/8 settings default to off, so a fixed card memory stays.
            if (card.isEgg() || card.card().trainerGender == CARD_TRAINER_GENDER_RECIPIENT)
            {
                return std::nullopt;
            }
            return FixedTrainerMemory{card.card().trainerMemory, card.card().trainerMemoryIntensity, card.card().trainerMemoryVariable, card.card().trainerMemoryFeeling};
        }

        /// PKHeX MemoryVerifier.VerifyOTMemory's first switch: WC6/WC7/WC8 that are not eggs and not
        /// the recipient's, and the in-game trades (IMemoryOTReadOnly).
        std::optional<FixedTrainerMemory> getFixedOriginalTrainerMemory(const Encounters::Encounterable &encounter)
        {
            namespace Encounters = Legality::Encounters;
            if (const Encounters::MysteryGiftWC6 *card = Encounters::encounterAs<Encounters::MysteryGiftWC6>(encounter))
            {
                return getCardTrainerMemory(*card);
            }
            if (const Encounters::MysteryGiftWC7 *card = Encounters::encounterAs<Encounters::MysteryGiftWC7>(encounter))
            {
                return getCardTrainerMemory(*card);
            }
            if (const Encounters::MysteryGiftWC8 *card = Encounters::encounterAs<Encounters::MysteryGiftWC8>(encounter))
            {
                return getCardTrainerMemory(*card);
            }
            if (const Encounters::EncounterTrade6 *trade = Encounters::encounterAs<Encounters::EncounterTrade6>(encounter))
            {
                return FixedTrainerMemory{trade->trainerMemory(), trade->trainerMemoryIntensity(), trade->trainerMemoryVariable(), trade->trainerMemoryFeeling()};
            }
            if (Encounters::encounterAs<Encounters::EncounterTrade7>(encounter))
            {
                using Encounters::EncounterTrade7;
                return FixedTrainerMemory{EncounterTrade7::TRAINER_MEMORY, EncounterTrade7::TRAINER_MEMORY_INTENSITY, EncounterTrade7::TRAINER_MEMORY_VARIABLE, EncounterTrade7::TRAINER_MEMORY_FEELING};
            }
            if (const Encounters::EncounterTrade8 *trade = Encounters::encounterAs<Encounters::EncounterTrade8>(encounter))
            {
                return FixedTrainerMemory{trade->trainerMemory(), trade->trainerMemoryIntensity(), trade->trainerMemoryVariable(), trade->trainerMemoryFeeling()};
            }
            return std::nullopt;
        }

        void verifyOriginalTrainerMemory(const AnalysisContext &context, MemoryFindings &findings)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Enums::GameVersion encounterContext = getEncounterContextGroup(context);
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                if (const std::optional<FixedTrainerMemory> fixedMemory = getFixedOriginalTrainerMemory(*encounterMatch))
                {
                    verifyOriginalTrainerMemoryIs(pokemon, fixedMemory->memoryId, fixedMemory->intensity, fixedMemory->variable, fixedMemory->feeling, findings);
                    return;
                }
            }
            else if (hasUnknownFixedOriginalTrainerMemory(context, encounterContext))
            {
                return;
            }
            else if (context.encounter.type == EncounterType::Trade && encounterContext == Enums::GameVersion::SM)
            {
                verifyOriginalTrainerMemoryIs(pokemon, 1, 3, 40, 5, findings); // EncounterTrade7's fixed memory
                return;
            }

            const uint8_t memoryId = pokemon.otMemory();
            if (pokemon.isEgg())
            {
                // Sword/Shield erroneously give a traded egg the link trade memory, or the box-inspect one.
                const bool isSwordShieldEggMemory = (pokemon.metLocation() == LOCATION_LINK_TRADE_6 && memoryId == MEMORY_LINK_TRADE) || memoryId == MEMORY_BOX_INSPECTED;
                if (encounterContext != Enums::GameVersion::SWSH || !isSwordShieldEggMemory)
                {
                    verifyOriginalTrainerMemoryIs(pokemon, 0, 0, 0, 0, findings);
                    return;
                }
            }
            else if (!canHaveMemoryForOriginalTrainer(pokemon, encounterContext, memoryId))
            {
                verifyOriginalTrainerMemoryIs(pokemon, 0, 0, 0, 0, findings);
                return;
            }

            const MemoryContext memoryContext = getMemoryContext(encounterContext);
            const Enums::GameVersion version = getVersion(pokemon);
            const char *handlerName = getHandlerName(ORIGINAL_TRAINER);
            if (!canObtainMemoryOT(memoryContext, version, memoryId))
            {
                addInvalid(findings, ResultCode::MemoryArgumentBadId, handlerName);
            }

            switch (memoryId)
            {
            case 0:
                // Sword/Shield trades do not set the handler's memory right away.
                findings.push_back(MemoryFinding{memoryContext == MemoryContext::Generation8 ? Severity::Warning : Severity::Invalid, ResultCode::MemoryMissingOT, "", ""});
                verifyOriginalTrainerMemoryIs(pokemon, 0, 0, 0, 0, findings);
                return;
            case MEMORY_HATCHED:
            {
                const std::optional<bool> encounterEgg = isEncounterEgg(context);
                if (encounterEgg.has_value() && !*encounterEgg)
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadHatch, handlerName);
                }
                break;
            }
            case MEMORY_LINK_TRADE:
                if (memoryContext == MemoryContext::Generation6)
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadOTEgg, handlerName);
                    return;
                }
                break;
            case MEMORY_POKEMON_CENTER:
                if (!hasPokemonCenter(memoryContext, version, pokemon.otMemoryVariable()))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadLocation, handlerName);
                    return;
                }
                break;
            case MEMORY_CAPTURE_WITNESSED:
                if (!canBeCaptured(pokemon.otMemoryVariable(), memoryContext, version))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, std::to_string(pokemon.otMemoryVariable()));
                }
                return;
            default:
                break;
            }
            verifyCommonMemory(context, ORIGINAL_TRAINER, memoryContext, findings);
        }

        /// PKHeX MemoryVerifier.WasTradedSWSHEgg.
        bool wasTradedSwordShieldEgg(const Pokemon::Pokemon &pokemon)
        {
            const Enums::GameVersion version = getVersion(pokemon);
            const bool isSwordShield = version == Enums::GameVersion::SW || version == Enums::GameVersion::SH;
            return isSwordShield && (pokemon.isEgg() ? pokemon.metLocation() : pokemon.eggLocation()) == LOCATION_LINK_TRADE_6;
        }

        /// PKHeX MemoryVerifier.VerifyHTMemoryTransferTo7: Bank adds the link trade memory, and a
        /// Generation 7 trade between games clears it.
        void verifyHandlingTrainerMemoryTransferTo7(const AnalysisContext &context, MemoryFindings &findings)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.htMemory() == 0)
            {
                verifyHandlingTrainerMemoryNone(pokemon, findings);
                return;
            }
            // Withdrawn by the trainer who handled it in an earlier generation: the old memory stays.
            const int originGeneration = getOriginGeneration(context);
            if (originGeneration >= 3 && originGeneration < 7 && pokemon.currentHandler() == 1)
            {
                return;
            }
            const char *handlerName = getHandlerName(HANDLING_TRAINER);
            if (pokemon.htMemory() != MEMORY_LINK_TRADE)
            {
                addInvalid(findings, ResultCode::MemoryIndexLinkHandler, handlerName, std::to_string(MEMORY_LINK_TRADE));
            }
            if (pokemon.htMemoryVariable() != 0)
            {
                addInvalid(findings, ResultCode::MemoryIndexArgumentHandler, handlerName, "0");
            }
            if (pokemon.htMemoryIntensity() != 1)
            {
                addInvalid(findings, ResultCode::MemoryIndexIntensityHandler1, handlerName, "1");
            }
            if (pokemon.htMemoryFeeling() >= 10)
            {
                addInvalid(findings, ResultCode::MemoryIndexFeelHandlerAtMost9, handlerName);
            }
        }

        void verifyHandlingTrainerMemory(const AnalysisContext &context, MemoryContext memoryContext, MemoryFindings &findings)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t memoryId = pokemon.htMemory();
            if (isUntraded(context))
            {
                // An untraded Sword/Shield link trade egg is given the handler's link trade memory anyway.
                if (!(memoryId == MEMORY_LINK_TRADE && wasTradedSwordShieldEgg(pokemon)))
                {
                    verifyHandlingTrainerMemoryNone(pokemon, findings);
                    return;
                }
            }
            if (context.formatGeneration == 7)
            {
                verifyHandlingTrainerMemoryTransferTo7(context, findings);
                return;
            }

            const char *handlerName = getHandlerName(HANDLING_TRAINER);
            if (!canObtainMemoryHT(memoryContext, memoryId))
            {
                addInvalid(findings, ResultCode::MemoryArgumentBadId, handlerName);
            }

            switch (memoryId)
            {
            case 0:
            {
                // Sword/Shield's memory roll is off by one, so it sometimes writes none.
                if (memoryContext == MemoryContext::Generation6)
                {
                    addInvalid(findings, ResultCode::MemoryMissingHandler);
                }
                else
                {
                    const Enums::GameVersion version = getVersion(pokemon);
                    const bool isSwordShield = version == Enums::GameVersion::SW || version == Enums::GameVersion::SH;
                    if (context.formatGroup == Enums::GameVersion::SWSH || isSwordShield)
                    {
                        findings.push_back(MemoryFinding{Severity::Warning, ResultCode::MemoryMissingHandler, "", ""});
                    }
                }
                verifyHandlingTrainerMemoryNone(pokemon, findings);
                return;
            }
            case MEMORY_CAUGHT:
                addInvalid(findings, ResultCode::MemoryArgumentBadCatch, handlerName);
                return;
            case MEMORY_HATCHED:
                addInvalid(findings, ResultCode::MemoryArgumentBadHatch, handlerName);
                return;
            case MEMORY_POKEMON_CENTER:
                if (!hasPokemonCenter(memoryContext, Enums::GameVersion::Any, pokemon.htMemoryVariable()))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadLocation, handlerName);
                    return;
                }
                break;
            case MEMORY_CAPTURE_WITNESSED:
                if (!canBeCaptured(pokemon.htMemoryVariable(), memoryContext, Enums::GameVersion::Any))
                {
                    addInvalid(findings, ResultCode::MemoryArgumentBadSpecies, handlerName, std::to_string(pokemon.htMemoryVariable()));
                }
                return;
            default:
                break;
            }
            verifyCommonMemory(context, HANDLING_TRAINER, memoryContext, findings);
        }

        /// PKHeX MemoryVerifier.VerifyHTMemoryContextVisited: memories survive a move between formats,
        /// so the handler's may come from any memory-writing game visited; the first that explains it
        /// wins, and if none does, the last one tried is reported.
        void verifyHandlingTrainerMemoryVisited(const AnalysisContext &context, uint8_t sources, MemoryFindings &findings)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            MemoryFindings attemptFindings;
            if ((sources & MEMORY_SOURCE_GENERATION_6) != 0)
            {
                attemptFindings.clear();
                verifyHandlingTrainerMemory(context, MemoryContext::Generation6, attemptFindings);
                verifyHandlerLanguage(context, MEMORY_SOURCE_GENERATION_6, attemptFindings);
                if (hasNoInvalidFinding(attemptFindings))
                {
                    findings.insert(findings.end(), attemptFindings.begin(), attemptFindings.end());
                    return;
                }
            }
            if ((sources & MEMORY_SOURCE_SWSH) != 0)
            {
                attemptFindings.clear();
                verifyHandlingTrainerMemory(context, MemoryContext::Generation8, attemptFindings);
                verifyHandlerLanguage(context, MEMORY_SOURCE_SWSH, attemptFindings);
                if (hasNoInvalidFinding(attemptFindings))
                {
                    findings.insert(findings.end(), attemptFindings.begin(), attemptFindings.end());
                    return;
                }
            }
            if ((sources & MEMORY_SOURCE_BANK) != 0)
            {
                attemptFindings.clear();
                verifyHandlingTrainerMemoryTransferTo7(context, attemptFindings);
                verifyHandlerLanguage(context, MEMORY_SOURCE_BANK, attemptFindings);
                if (hasNoInvalidFinding(attemptFindings))
                {
                    findings.insert(findings.end(), attemptFindings.begin(), attemptFindings.end());
                    return;
                }
            }
            if ((sources & MEMORY_SOURCE_DELETED) != 0)
            {
                attemptFindings.clear();
                verifyHandlingTrainerMemoryNone(pokemon, attemptFindings);
                verifyHandlerLanguage(context, MEMORY_SOURCE_DELETED, attemptFindings);
            }
            findings.insert(findings.end(), attemptFindings.begin(), attemptFindings.end());
        }
    }

    void verifyMemories(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasHandlerMemories())
        {
            return; // PKHeX runs this verifier only for ITrainerMemories formats, which Let's Go is not
        }
        MemoryFindings findings;
        const uint8_t sources = getPossibleSources(context);
        if (sources == MEMORY_SOURCE_NONE)
        {
            verifyOriginalTrainerMemoryIs(pokemon, 0, 0, 0, 0, findings);
            verifyHandlingTrainerMemoryNone(pokemon, findings);
        }
        else
        {
            verifyOriginalTrainerMemory(context, findings);
            const uint8_t handlerSources = reviseSourcesHandler(context, sources);
            if (handlerSources == MEMORY_SOURCE_NONE)
            {
                verifyHandlingTrainerMemoryNone(pokemon, findings);
                verifyHandlerLanguage(context, MEMORY_SOURCE_NONE, findings);
            }
            else
            {
                verifyHandlingTrainerMemoryVisited(context, handlerSources, findings);
            }
        }
        for (const MemoryFinding &finding : findings)
        {
            context.add(finding.severity, CheckIdentifier::Memory, finding.code, finding.firstArgument, finding.secondArgument);
        }
    }
}
