#include "Legality/Encounters/EncounterStatic8Nest.h"

#include <array>
#include <cstdio>

#include "Legality/Encounters/EncounterData8.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/RaidRNG.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Legality/RNG/Xoroshiro128PlusReversal.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_SHEDINJA = 292;
            constexpr uint16_t SPECIES_ALCREMIE = 869;
            constexpr uint16_t SPECIES_RUNERIGUS = 867;
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
            /// PKHeX's shared-nest down-level floor (GetDownleveledMin).
            constexpr uint8_t SHARED_NEST_MIN_LEVEL = 20;
            constexpr uint8_t DOWN_LEVEL_MAX = 55;
            /// PKHeX EncounterStatic8ND.IndexMinDLC1 / IndexMinDLC2.
            constexpr uint8_t DISTRIBUTION_INDEX_MIN_ISLE_OF_ARMOR = 25;
            constexpr uint8_t DISTRIBUTION_INDEX_MIN_CROWN_TUNDRA = 40;
            /// PKHeX PersonalInfo.RatioMagicMale / Female / Genderless.
            constexpr uint8_t GENDER_RATIO_MALE = 0;
            constexpr uint8_t GENDER_RATIO_FEMALE = 254;
            constexpr uint8_t GENDER_RATIO_GENDERLESS = 255;
            /// PKHeX's anti-shiny PID flip that a raid's shiny reroll can leave behind.
            constexpr uint32_t ANTISHINY_PID_XOR = 0x10000000;

            /// PKHeX EncounterStatic8N.LevelCaps: [rank * 2] is the rank's lowest level, [rank * 2 + 1] its highest.
            constexpr std::array<uint8_t, 10> NEST_LEVEL_CAPS = {15, 20, 25, 30, 35, 40, 45, 50, 55, 60};

            // PKHeX EncounterStatic8Nest.RemapGenderToParam
            uint8_t remapGenderToParameter(uint8_t gender, uint16_t speciesId, uint8_t formId)
            {
                switch (gender)
                {
                case 0:
                    return GENDER_RATIO_MALE;
                case 1:
                    return GENDER_RATIO_FEMALE;
                case 2:
                    return GENDER_RATIO_GENDERLESS;
                default:
                    return Pokemon::getGenderRatio(speciesId, formId, Enums::GameVersion::SWSH);
                }
            }

            // PKHeX EncounterStatic8N.GetInitialDynamaxLevel. PKHeX's Xoroshiro128Plus is a struct passed
            // by value, so every rank rolls from the same state (the one just after the level roll): the
            // copy here is deliberate.
            uint8_t getInitialDynamaxLevel(RNG::Xoroshiro128Plus random, int rank) noexcept
            {
                const int baseValue = rank == 4 ? 6 : rank + 1;
                const uint64_t deltaRange = rank == 4 ? 3 : 2;
                const int boost = static_cast<int>(random.nextInt(deltaRange));
                return static_cast<uint8_t>(baseValue + boost);
            }
        }

        bool EncounterStatic8Nest::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return pokemon.metLocation() == LOCATION_SHARED_NEST8;
        }

        bool EncounterStatic8Nest::isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return pokemon.metLevel() == properties.level;
        }

        // PKHeX EncounterStatic8Nest.IsMatchExact
        bool EncounterStatic8Nest::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (isFormatSwordShield(pokemon) && getDynamaxLevel(pokemon) < properties.dynamaxLevel)
            {
                return false;
            }
            if (properties.version != Enums::GameVersion::SWSH && getOriginVersion(pokemon) != static_cast<uint8_t>(properties.version) && pokemon.metLocation() != LOCATION_SHARED_NEST8)
            {
                return false;
            }
            const RibbonMarks8 marks = readRibbonMarks8(pokemon);
            if (marks.isPresent && marks.hasEncounterMark)
            {
                return false;
            }
            if (pokemon.speciesID() == SPECIES_SHEDINJA && marks.isAffixedEncounterMark8)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (!isMatchLevel(pokemon))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && properties.gender != pokemon.gender())
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SWSH, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            return getFlawlessIVCount(pokemon) >= properties.flawlessIVCount;
        }

        // PKHeX EncounterStatic8Nest.GetMatchRating
        EncounterMatchRating EncounterStatic8Nest::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return isMatchDeferred(pokemon);
        }

        // PKHeX EncounterStatic8Nest.IsMatchDeferred
        EncounterMatchRating EncounterStatic8Nest::isMatchDeferred(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.ability == RNG::AbilityPermission::Any12H)
            {
                return EncounterMatchRating::Match;
            }
            // Hidden-only is strict; an Ability Capsule or Patch can change the others.
            const uint8_t abilityNumber = pokemon.abilityNumber();
            const int singleIndex = RNG::getSingleValue(properties.ability);
            if (abilityNumber == HIDDEN_ABILITY_NUMBER)
            {
                if (properties.ability != RNG::AbilityPermission::OnlyHidden && !canAbilityPatch(8, properties.speciesId, properties.formId, Enums::GameVersion::SWSH, pokemon.speciesID()))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
            }
            else if (singleIndex >= 0 && (1 << singleIndex) != abilityNumber)
            {
                const bool isFirstOrSecond = properties.ability == RNG::AbilityPermission::OnlyFirst || properties.ability == RNG::AbilityPermission::OnlySecond;
                if (isFirstOrSecond && !canAbilityCapsule(8, properties.speciesId, properties.formId, Enums::GameVersion::SWSH))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                // A hidden ability cannot be restored, even through HOME.
                if (properties.ability == RNG::AbilityPermission::OnlyHidden)
                {
                    return EncounterMatchRating::DeferredErrors;
                }
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic8Nest.IsMatchPartial
        bool EncounterStatic8Nest::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (isFormatSwordShield(pokemon) && Encounters::canGigantamax(pokemon) != properties.canGigantamax && !canToggleGigantamax(pokemon.speciesID(), pokemon.form(), properties.speciesId))
            {
                return true;
            }
            const bool isFormArgumentSpecies = properties.speciesId == SPECIES_ALCREMIE || properties.speciesId == SPECIES_RUNERIGUS;
            if (isFormArgumentSpecies && pokemon.hasFormArgument() && pokemon.formArgument() != 0)
            {
                return true;
            }
            if (pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && isPartialMatchHidden(pokemon.speciesID(), properties.speciesId))
            {
                return true;
            }
            if (!isMatchCorrelation(pokemon))
            {
                return true;
            }
            if (properties.shiny == RNG::Shiny::Never && isShiny(pokemon))
            {
                return true;
            }
            if (properties.shiny == RNG::Shiny::Always && !isShiny(pokemon))
            {
                return true;
            }
            return false;
        }

        bool EncounterStatic8Nest::isMatchCorrelation(const Pokemon::Pokemon &pokemon) const
        {
            if (isShiny(pokemon))
            {
                return true;
            }
            uint64_t seed = 0;
            return tryGetSeed(pokemon, seed);
        }

        bool EncounterStatic8Nest::verify(const Pokemon::Pokemon &pokemon, uint64_t seed, bool forceNoShiny) const
        {
            RNG::GenerateParam8 parameters;
            parameters.species = properties.speciesId;
            parameters.genderRatio = remapGenderToParameter(properties.gender, properties.speciesId, properties.formId);
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.ability = properties.ability;
            parameters.shiny = properties.shiny;
            parameters.nature = RNG::RANDOM_NATURE;
            parameters.individualValues = properties.individualValues;
            std::array<int, 6> individualValues{};
            return RNG::RaidRNG::verify(pokemon, seed, individualValues, parameters, forceNoShiny);
        }

        // PKHeX EncounterStatic8Nest.TryGetSeed
        bool EncounterStatic8Nest::tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const
        {
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            const uint32_t PID = pokemon.pid();
            for (const uint32_t candidatePID : {PID, PID ^ ANTISHINY_PID_XOR})
            {
                RNG::XoroMachineSkip seeds(encryptionConstant, candidatePID);
                while (seeds.moveNext())
                {
                    seed = seeds.current();
                    if (isMatchSeed(pokemon, seed))
                    {
                        return true;
                    }
                }
            }
            seed = 0;
            return false;
        }

        std::string EncounterStatic8N::name() const
        {
            char rankText[16];
            if (minRank == maxRank)
            {
                std::snprintf(rankText, sizeof(rankText), "%d", minRank + 1);
            }
            else
            {
                std::snprintf(rankText, sizeof(rankText), "%d-%d", minRank + 1, maxRank + 1);
            }
            char nameText[64];
            std::snprintf(nameText, sizeof(nameText), "Stock Raid Den Encounter [%03d] %s★", nestIndex, rankText);
            return nameText;
        }

        uint8_t EncounterStatic8N::levelMin() const noexcept
        {
            return NEST_LEVEL_CAPS[minRank * 2];
        }

        uint8_t EncounterStatic8N::levelMax() const noexcept
        {
            return NEST_LEVEL_CAPS[(maxRank * 2) + 1];
        }

        // PKHeX EncounterStatic8N.IsMatchLevel
        bool EncounterStatic8N::isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t metLevel = pokemon.metLevel();
            const uint32_t metRank = static_cast<uint32_t>(metLevel) - 15u;
            const uint32_t rank = metRank / 10;
            if (rank > 4 || rank > maxRank)
            {
                return false;
            }
            if (rank <= 1)
            {
                const uint16_t metLocation = pokemon.metLocation();
                if (metLocation <= UINT8_MAX && isInaccessibleRank12Nest8(nestIndex, static_cast<uint8_t>(metLocation)))
                {
                    return false;
                }
            }
            if (rank < minRank)
            {
                return isDownLeveled(pokemon, metRank, metLevel);
            }
            return metRank % 10 <= 5;
        }

        bool EncounterStatic8N::isDownLeveled(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t metLevel = pokemon.metLevel();
            return metLevel != levelMax() && isDownLeveled(pokemon, static_cast<uint32_t>(metLevel) - 15u, metLevel);
        }

        // PKHeX EncounterStatic8N.IsDownLeveled
        bool EncounterStatic8N::isDownLeveled(const Pokemon::Pokemon &pokemon, uint32_t metRank, uint8_t metLevel) const noexcept
        {
            if (metRank > INT32_MAX || metRank % 5 != 0)
            {
                return false;
            }
            if (pokemon.metLocation() == LOCATION_SHARED_NEST8)
            {
                return metLevel >= SHARED_NEST_MIN_LEVEL;
            }
            // A native den drops at most one rank: 1 badge 2-star at 25, 3 badges 3-star at 35.
            const bool isNativeDownLevel = (minRank <= 1 && 1 <= maxRank && metLevel == 25) || (minRank <= 2 && 2 <= maxRank && metLevel == 35);
            return isNativeDownLevel && !isShiny(pokemon);
        }

        bool EncounterStatic8N::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == LOCATION_SHARED_NEST8)
            {
                return true;
            }
            if (metLocation > UINT8_MAX)
            {
                return false;
            }
            for (const uint8_t nestLocation : getNestLocations8(nestIndex))
            {
                if (nestLocation == metLocation)
                {
                    return true;
                }
            }
            return false;
        }

        // PKHeX EncounterStatic8N.IsPossibleSeed
        bool EncounterStatic8N::isPossibleSeed(const Pokemon::Pokemon &pokemon, uint64_t seed, bool checkDynamaxLevel, bool &forceNoShiny) const noexcept
        {
            forceNoShiny = false;
            RNG::Xoroshiro128Plus random(seed);
            const int levelDelta = static_cast<int>(random.nextInt(6));
            const uint8_t metLevel = pokemon.metLevel();
            for (int rank = maxRank; rank >= minRank; rank--)
            {
                // A 5-star raid rolls rand(3) for the Dynamax level, the rest rand(2). Only a PK8
                // keeps the level; HOME does not copy a PB8's.
                if (checkDynamaxLevel && isFormatSwordShield(pokemon))
                {
                    const uint8_t expectedDynamaxLevel = getInitialDynamaxLevel(random, rank);
                    if (expectedDynamaxLevel > getDynamaxLevel(pokemon))
                    {
                        continue;
                    }
                }
                const int rankLevelMin = NEST_LEVEL_CAPS[rank * 2];
                if (rankLevelMin + levelDelta == metLevel)
                {
                    return true;
                }
                if (metLevel > rankLevelMin)
                {
                    break;
                }
                if (isDownLeveled(pokemon, static_cast<uint32_t>(metLevel) - 15u, metLevel))
                {
                    forceNoShiny = true;
                    return true;
                }
            }
            return false;
        }

        bool EncounterStatic8N::isMatchSeed(const Pokemon::Pokemon &pokemon, uint64_t seed) const
        {
            bool forceNoShiny = false;
            if (!isPossibleSeed(pokemon, seed, true, forceNoShiny))
            {
                return false;
            }
            return verify(pokemon, seed, forceNoShiny);
        }

        bool EncounterStatic8NC::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            return metLocation == LOCATION_SHARED_NEST8 || metLocation == LOCATION_WATCHTOWER8;
        }

        // PKHeX EncounterStatic8NC.IsMatchLevel
        bool EncounterStatic8NC::isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel == properties.level)
            {
                return true;
            }
            if (metLevel > properties.level)
            {
                return false;
            }
            if (metLevel < SHARED_NEST_MIN_LEVEL || metLevel > DOWN_LEVEL_MAX)
            {
                return false;
            }
            // The host cannot down-level and be shiny.
            if (pokemon.metLocation() == LOCATION_WATCHTOWER8 && isShiny(pokemon))
            {
                return false;
            }
            return metLevel % 5 == 0;
        }

        std::string EncounterStatic8ND::name() const
        {
            char nameText[64];
            std::snprintf(nameText, sizeof(nameText), "Distribution Raid Den Encounter - %03d", distributionIndex);
            return nameText;
        }

        // PKHeX EncounterStatic8ND.IsMatchLevel
        bool EncounterStatic8ND::isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel <= 25)
            {
                const uint16_t metLocation = pokemon.metLocation();
                if (metLocation <= UINT8_MAX && isInaccessibleRank12DistributionLocation8(static_cast<uint8_t>(metLocation)))
                {
                    return false;
                }
            }
            if (metLevel == properties.level)
            {
                return true;
            }
            if (metLevel > properties.level)
            {
                return false;
            }
            if (metLevel < SHARED_NEST_MIN_LEVEL || metLevel > DOWN_LEVEL_MAX)
            {
                return false;
            }
            if (metLevel % 5 != 0)
            {
                return false;
            }
            if (pokemon.metLocation() == LOCATION_SHARED_NEST8)
            {
                return true;
            }
            // A native den drops at most one rank: 1 badge 2-star at 25, 3 badges 3-star at 35.
            const int badgeCount = (metLevel - 20) / 5;
            return (badgeCount == 1 || badgeCount == 3) && !isShiny(pokemon);
        }

        bool EncounterStatic8ND::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == LOCATION_SHARED_NEST8)
            {
                return true;
            }
            if (distributionIndex >= DISTRIBUTION_INDEX_MIN_CROWN_TUNDRA)
            {
                return isWildAreaSwordShield(metLocation);
            }
            if (distributionIndex >= DISTRIBUTION_INDEX_MIN_ISLE_OF_ARMOR)
            {
                return isWildArea8(metLocation) || isWildArea8Armor(metLocation);
            }
            return isWildArea8(metLocation);
        }

        bool EncounterStatic8ND::isMatchSeed(const Pokemon::Pokemon &pokemon, uint64_t seed) const
        {
            const bool isDownLeveled = pokemon.metLevel() < properties.level;
            return verify(pokemon, seed, isDownLeveled);
        }

        bool EncounterStatic8U::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return pokemon.metLocation() == LOCATION_MAX_LAIR8;
        }
    }
}
