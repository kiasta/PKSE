#include "Legality/Encounters/EncounterStatic2.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_KOFFING = 109;
            constexpr uint16_t SPECIES_RAIKOU = 243;
            constexpr uint16_t SPECIES_ENTEI = 244;
            constexpr uint16_t SPECIES_SUICUNE = 245;
            constexpr uint16_t SPECIES_CELEBI = 251;
            constexpr uint16_t MOVE_DIZZY_PUNCH = 146;
            /// Tin Tower, where Crystal's Suicune waits rather than roams.
            constexpr uint8_t LOCATION_TIN_TOWER = 23;
            /// The fixed experience of an unhatched Odd Egg.
            constexpr uint32_t ODD_EGG_EXP = 125;
            constexpr uint8_t UNHATCHED_EGG_TRAINER_GENDER = 0;
            /// Routes 29-46 except 40 and 41, one bit per met location.
            constexpr uint64_t ROAM_LOCATIONS = 0b10'1000'1010'0100'0000'0110'0011'0100'1000'1001'0011'0100ULL;
            constexpr uint16_t ROAM_LOCATION_LAST = 45;

            /// PKHeX EncounterStatic2.DetectOddEggLanguage: the Odd Egg's fixed trainer names. The
            /// Japanese egg is saved first as "なぞナゾ" and then as "なぞ", so both are real.
            bool isOddEggTrainerNameValid(const Pokemon::Pokemon &pokemon)
            {
                const std::u16string trainerName = pokemon.otName();
                if (GameBoyEntity::isJapanese(pokemon))
                {
                    return trainerName == u"なぞナゾ" || trainerName == u"なぞ";
                }
                return trainerName == u"ODD" || trainerName == u"BIZAR" || trainerName == u"Strano" || trainerName == u"Kurios" || trainerName == u"Raro";
            }
        }

        // PKHeX EncounterStatic2 / EncounterGift2 IsMatchEggLocationInternal
        bool isMatchEggLocationGameBoy(const Pokemon::Pokemon &pokemon, bool templateIsEgg, int maximumEggFriendship)
        {
            if (!GameBoyEntity::hasCaughtDataField(pokemon))
            {
                return isMatchEggLocation(pokemon, 0);
            }
            const uint8_t metLevel = pokemon.metLevel();
            const uint16_t metLocation = pokemon.metLocation();
            if (pokemon.isEgg())
            {
                if (!templateIsEgg)
                {
                    return false;
                }
                if (metLocation != 0 && metLevel != 0)
                {
                    return false;
                }
                if (maximumEggFriendship >= 0 && pokemon.otFriendship() > maximumEggFriendship)
                {
                    return false;
                }
                return true;
            }
            if (metLevel == 0)
            {
                return metLocation == 0;
            }
            // Met level 1 is a hatched egg; location 0 is the second floor of every Pokemon Center.
            if (metLevel == 1)
            {
                return true;
            }
            return metLocation != 0;
        }

        RNG::AbilityPermission EncounterStatic2::ability() const noexcept
        {
            return properties.speciesId != SPECIES_KOFFING ? RNG::AbilityPermission::OnlyHidden : RNG::AbilityPermission::OnlyFirst;
        }

        bool EncounterStatic2::isRoaming() const noexcept
        {
            const uint16_t species = properties.speciesId;
            return (species == SPECIES_ENTEI || species == SPECIES_RAIKOU || species == SPECIES_SUICUNE) && properties.location != LOCATION_TIN_TOWER;
        }

        // PKHeX EncounterStatic2.IsMatchExact
        bool EncounterStatic2::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isShiny = GameBoyEntity::isShiny(pokemon);
            if (properties.shiny == RNG::Shiny::Always && !isShiny)
            {
                return false;
            }
            if (isDizzyPunchEgg())
            {
                // The Odd Egg cannot be transferred to Gen 7.
                if (getFormatGeneration(pokemon) > 2)
                {
                    return false;
                }
                if (!GameBoyEntity::hasMove(pokemon, MOVE_DIZZY_PUNCH))
                {
                    return false;
                }
                // An unhatched egg keeps its fixed experience and trainer; hatching changes both.
                if (pokemon.isEgg())
                {
                    if (pokemon.exp() != ODD_EGG_EXP)
                    {
                        return false;
                    }
                    if (pokemon.otGender() != UNHATCHED_EGG_TRAINER_GENDER)
                    {
                        return false;
                    }
                    if (!isOddEggTrainerNameValid(pokemon))
                    {
                        return false;
                    }
                }
            }
            if (!isMatchEggLocationInternal(pokemon))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (!isMatchLevel(pokemon, evolution))
            {
                return false;
            }
            if (properties.individualValues.isSpecified)
            {
                if (properties.shiny == RNG::Shiny::Never && isShiny)
                {
                    return false;
                }
                if (getFormatGeneration(pokemon) <= 2)
                {
                    if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
                    {
                        return false;
                    }
                }
                else if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
                {
                    return false;
                }
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.formID(), Enums::GameVersion::GSC, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        bool EncounterStatic2::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const
        {
            return isMatchEggLocationGameBoy(pokemon, properties.isEgg, -1);
        }

        bool EncounterStatic2::isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (evolution.levelMax < properties.level)
            {
                return false;
            }
            if (GameBoyEntity::getCaughtData(pokemon) != 0)
            {
                return pokemon.metLevel() == (properties.isEgg ? 1 : properties.level);
            }
            return true;
        }

        bool EncounterStatic2::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.isEgg)
            {
                return true;
            }
            if (!GameBoyEntity::hasCaughtDataField(pokemon))
            {
                return true;
            }
            const uint16_t caughtData = GameBoyEntity::getCaughtData(pokemon);
            if (caughtData == 0 && properties.version != Enums::GameVersion::C)
            {
                return true;
            }
            if (isRoaming())
            {
                const uint16_t metLocation = pokemon.metLocation();
                return metLocation <= ROAM_LOCATION_LAST && (ROAM_LOCATIONS & (1ULL << metLocation)) != 0;
            }
            if (properties.version == Enums::GameVersion::C || properties.version == Enums::GameVersion::GSC)
            {
                if (caughtData != 0)
                {
                    return properties.location == pokemon.metLocation();
                }
                // Crystal's Celebi keeps its met data; nothing can wipe it.
                if (properties.speciesId == SPECIES_CELEBI)
                {
                    return false;
                }
            }
            return true;
        }
    }
}
