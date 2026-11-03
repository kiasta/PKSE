#include "Legality/Encounters/EncounterTrade9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EncounterTrade9.FixedValueScale: a VALUE size is always 128.
            constexpr uint8_t FIXED_VALUE_SCALE = 128;

            // PKHeX EncounterTrade9.VerifyScalar
            bool verifyScalar(RNG::SizeType9 type, uint8_t scalarValue) noexcept
            {
                if (type == RNG::SizeType9::VALUE)
                {
                    return scalarValue == FIXED_VALUE_SCALE;
                }
                return isSizeWithinRange(type, scalarValue);
            }
        }

        bool EncounterTrade9::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            if (properties.trainerNames == nullptr || language < 0 || language >= static_cast<int>(properties.trainerNames->size()))
            {
                return false;
            }
            return trainerName == getLanguageName(properties.trainerNames, language);
        }

        bool EncounterTrade9::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            if (properties.nicknames == nullptr || language < 0 || language >= static_cast<int>(properties.nicknames->size()))
            {
                return false;
            }
            return nickname == getLanguageName(properties.nicknames, language);
        }

        std::u16string EncounterTrade9::getNickname(int language) const
        {
            if (properties.nicknames == nullptr)
            {
                return std::u16string();
            }
            const bool isInRange = language >= 0 && language < static_cast<int>(properties.nicknames->size());
            return getLanguageName(properties.nicknames, isInRange ? language : 0);
        }

        // PKHeX EncounterTrade9.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.teraType != RNG::GemType::Random && pokemon.hasTeraType() && !RNG::Tera9RNG::isMatchTeraType(properties.teraType, properties.speciesId, properties.formId, pokemon.teraTypeOriginal()))
            {
                return false;
            }
            if (!isMatchLocation9(pokemon, pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC, LOCATION_LINK_TRADE6_NPC, Enums::GameVersion::SV))
            {
                return false;
            }
            if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (!isShinyValid(properties.shiny, pokemon))
            {
                return false;
            }
            if (pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.id32() != properties.ID32)
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SV, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            if (!isMatchEggLocationInternal9(pokemon, 0, Enums::GameVersion::SV))
            {
                return false;
            }
            if (properties.evolveOnTrade && pokemon.speciesID() == properties.speciesId)
            {
                return false;
            }
            if (RNG::HomeQuirks::hasSizeScalars(pokemon))
            {
                if (pokemon.hasScale() && !verifyScalar(properties.scale, pokemon.scale()))
                {
                    return false;
                }
                if (!verifyScalar(properties.weight, pokemon.weightScalar()))
                {
                    return false;
                }
            }
            return true;
        }
    }
}
