// PKHeX EncounterSlot7GO and EncounterSlot8GO (PKHeX.Core/Legality/Encounters/Templates/GO/).
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterSlot7GO.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/SpeciesCategory.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Legality/Verifiers/SpeciesName.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_FARFETCHD = 83;
            constexpr uint16_t SPECIES_STANTLER = 234;
            constexpr uint16_t SPECIES_QWILFISH = 211;
            constexpr uint16_t SPECIES_SHEDINJA = 292;
            constexpr uint16_t SPECIES_BASCULIN = 550;
            constexpr uint16_t SPECIES_YAMASK = 562;
            constexpr uint16_t SPECIES_PRIMEAPE = 57;
            constexpr uint16_t SPECIES_TIMBURR = 532;
            constexpr uint16_t SPECIES_STUNFISK = 618;
            constexpr uint16_t SPECIES_BISHARP = 625;
            constexpr uint16_t SPECIES_HOOPA = 720;
            constexpr uint16_t SPECIES_SIRFETCHD = 865;
            constexpr uint16_t SPECIES_RUNERIGUS = 867;
            constexpr uint16_t SPECIES_MILCERY = 868;
            constexpr uint16_t SPECIES_ALCREMIE = 869;
            constexpr uint16_t SPECIES_WYRDEER = 899;
            constexpr uint16_t SPECIES_BASCULEGION = 902;
            constexpr uint16_t SPECIES_OVERQWIL = 904;
            constexpr uint16_t SPECIES_ANNIHILAPE = 979;
            constexpr uint16_t SPECIES_KINGAMBIT = 983;
            constexpr uint16_t SPECIES_GHOLDENGO = 1000;

            /// Base friendship 70 becomes 50 when HOME imports a GO Pokemon.
            constexpr uint8_t STANDARD_BASE_FRIENDSHIP = 70;
            constexpr uint8_t HOME_IMPORT_FRIENDSHIP = 50;

            /// PKHeX: the Master Ball could not be used on GO encounters before 2023-05-21.
            constexpr int MASTER_BALL_YEAR = 2023;
            constexpr int MASTER_BALL_MONTH = 5;
            constexpr int MASTER_BALL_DAY = 21;
            constexpr int MET_YEAR_EPOCH = 2000;

            struct DayRange
            {
                int first;
                int last;
            };

            /// PKHeX EncounterSlot8GO.IsValidWildAreaDate: the Pokemon GO Wild Area event days.
            constexpr DayRange WILD_AREA_DAYS[] = {
                {739204, 739207}, // Pokemon GO Wild Area 2024: Fukuoka
                {739211, 739214}, // Pokemon GO Wild Area 2024: Global
                {739560, 739564}, // Pokemon GO Wild Area 2025: Nagasaki
                {739568, 739571}, // Pokemon GO Wild Area 2025: Global
                {739924, 739928}, // Pokemon GO Wild Area 2026: Sendai, Tohoku / Mexico City
                {739932, 739935}, // Pokemon GO Wild Area 2026: Global
            };

            int getMetDayNumber(const Pokemon::Pokemon &pokemon) noexcept
            {
                return getDayNumber(MET_YEAR_EPOCH + pokemon.metYear(), pokemon.metMonth(), pokemon.metDay());
            }

            bool isValidWildAreaDate(int dayNumber) noexcept
            {
                for (const DayRange &range : WILD_AREA_DAYS)
                {
                    if (dayNumber >= range.first && dayNumber <= range.last)
                    {
                        return true;
                    }
                }
                return false;
            }

            bool isPresentIn(uint16_t speciesId, uint8_t formId, uint8_t gameFlag) noexcept
            {
                return (Pokemon::getPersonalInfo(speciesId, formId).presence & gameFlag) != 0;
            }

            /// PKHeX `pk is IFormArgument { FormArgument: 0 }`.
            bool hasFormArgumentZero(const Pokemon::Pokemon &pokemon) noexcept
            {
                return pokemon.hasFormArgument() && pokemon.formArgument() == 0;
            }

            /// PKHeX `pk is IFormArgument { FormArgument: not 0 }`.
            bool hasFormArgumentNonZero(const Pokemon::Pokemon &pokemon) noexcept
            {
                return pokemon.hasFormArgument() && pokemon.formArgument() != 0;
            }

            /// PKHeX EncounterSlot8GO.IsReallySirfetchd: only a HOME-born Farfetch'd evolution keeps
            /// the straight apostrophe in its species-name nickname.
            bool isReallySirfetchd(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.speciesID() != SPECIES_SIRFETCHD)
                {
                    return false;
                }
                if (pokemon.isNicknamed() || !isApostropheFarfetchdLanguage(pokemon.language()))
                {
                    return false;
                }
                return pokemon.nickname() == u"Sirfetch'd";
            }
        }

        // PKHeX EncounterSlot7GO.IsMatchExact: any in-game ball is possible through recapture.
        bool EncounterSlot7GO::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (!isShinyValid(slotShiny, pokemon))
            {
                return false;
            }
            return slotGender == POGO_GENDER_RANDOM || slotGender == pokemon.gender();
        }

        EncounterMatchRating EncounterSlot7GO::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (!isWithinDistributionWindow(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (!areIVsValid(pokemon))
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        Enums::GameVersion EncounterSlot8GO::contextGroup() const noexcept
        {
            switch (slotOriginFormat)
            {
            case PogoImportFormat::PK7:
            case PogoImportFormat::PB7:
                if (isPresentIn(slotSpeciesId, slotFormId, Pokemon::PERSONAL_GAME_BDSP))
                {
                    return Enums::GameVersion::BDSP;
                }
                if (isPresentIn(slotSpeciesId, slotFormId, Pokemon::PERSONAL_GAME_PLA))
                {
                    return Enums::GameVersion::PLA;
                }
                if (isPresentIn(slotSpeciesId, slotFormId, Pokemon::PERSONAL_GAME_SV))
                {
                    return Enums::GameVersion::SV;
                }
                return Enums::GameVersion::SWSH;
            case PogoImportFormat::PA8:
                return Enums::GameVersion::PLA;
            case PogoImportFormat::PK9:
                return Enums::GameVersion::SV;
            default:
                return Enums::GameVersion::SWSH;
            }
        }

        Enums::GameVersion EncounterSlot8GO::originGroup() const noexcept
        {
            switch (slotOriginFormat)
            {
            case PogoImportFormat::PK7:
                return Enums::GameVersion::USUM;
            case PogoImportFormat::PB7:
                return Enums::GameVersion::GG;
            case PogoImportFormat::PA8:
                return Enums::GameVersion::PLA;
            case PogoImportFormat::PK9:
                return Enums::GameVersion::SV;
            default:
                return Enums::GameVersion::SWSH;
            }
        }

        bool EncounterSlot8GO::isBallValid(Enums::Ball ball, uint16_t currentSpeciesId, const Pokemon::Pokemon &pokemon) const noexcept
        {
            // GO does not produce Shedinja when Nincada evolves, so it must have evolved later.
            if (currentSpeciesId == SPECIES_SHEDINJA && currentSpeciesId != slotSpeciesId)
            {
                return ball == Enums::Ball::Poke;
            }
            const int metDayNumber = getMetDayNumber(pokemon);
            if (ball == Enums::Ball::Master && metDayNumber < getDayNumber(MASTER_BALL_YEAR, MASTER_BALL_MONTH, MASTER_BALL_DAY))
            {
                return false;
            }
            if (ball == Enums::Ball::Strange && slotType == PogoType::SpecialResearch && SpeciesCategory::isSpecialPokemon(currentSpeciesId))
            {
                return false;
            }
            if (ball == Enums::Ball::Strange && !isValidWildAreaDate(metDayNumber))
            {
                return false;
            }
            return isPogoBallValid(slotBallRestriction, ball, slotType, slotFlags);
        }

        // PKHeX EncounterSlot8GO.IsMatchExact
        bool EncounterSlot8GO::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (!isBallValid(static_cast<Enums::Ball>(pokemon.ball()), pokemon.speciesID(), pokemon))
            {
                return false;
            }
            if (!isShinyValid(slotShiny, pokemon))
            {
                return false;
            }
            return slotGender == POGO_GENDER_RANDOM || slotGender == pokemon.gender();
        }

        EncounterMatchRating EncounterSlot8GO::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (slotSpeciesId == SPECIES_FARFETCHD && isReallySirfetchd(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (pokemon.hasTeraType() && !RNG::Tera9RNG::isMatchTeraTypePersonalImport(slotSpeciesId, slotFormId, pokemon.teraTypeOriginal()))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        uint8_t EncounterSlot8GO::originalTrainerFriendship() const noexcept
        {
            if ((slotSpeciesId == SPECIES_TIMBURR || slotSpeciesId == SPECIES_STUNFISK) && slotFormId == 0)
            {
                return STANDARD_BASE_FRIENDSHIP;
            }
            if (slotSpeciesId == SPECIES_HOOPA && slotFormId == 1)
            {
                return HOME_IMPORT_FRIENDSHIP;
            }
            const uint8_t baseFriendship = Pokemon::getPersonalRecord(originGroup(), slotSpeciesId, slotFormId).baseFriendship;
            return baseFriendship == STANDARD_BASE_FRIENDSHIP ? HOME_IMPORT_FRIENDSHIP : baseFriendship;
        }

        bool EncounterSlot8GO::isMatchPartial(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (!isWithinDistributionWindow(pokemon))
            {
                return true;
            }
            if (!areIVsValid(pokemon))
            {
                return true;
            }
            // Eevee and Glaceon differ in base friendship; a mismatch yields the other template first.
            if (pokemon.otFriendship() != originalTrainerFriendship())
            {
                return true;
            }
            return isFormArgumentIncorrect(pokemon);
        }

        bool EncounterSlot8GO::isFormArgumentIncorrect(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const bool isEvolved = pokemon.speciesID() != slotSpeciesId;
            switch (slotSpeciesId)
            {
            // Evolved without the form argument changing from its default.
            case SPECIES_YAMASK:
            case SPECIES_QWILFISH:
                return isEvolved && slotFormId == 1 && hasFormArgumentZero(pokemon);
            case SPECIES_BASCULIN:
                return isEvolved && slotFormId == 2 && hasFormArgumentZero(pokemon);
            case SPECIES_MILCERY:
            case SPECIES_STANTLER:
            case SPECIES_PRIMEAPE:
            case SPECIES_BISHARP:
                return isEvolved && hasFormArgumentZero(pokemon);
            // Not evolved, but the form argument changed from its default.
            case SPECIES_RUNERIGUS:
            case SPECIES_ALCREMIE:
            case SPECIES_WYRDEER:
            case SPECIES_OVERQWIL:
            case SPECIES_BASCULEGION:
            case SPECIES_ANNIHILAPE:
            case SPECIES_KINGAMBIT:
            case SPECIES_GHOLDENGO:
                return hasFormArgumentNonZero(pokemon);
            default:
                return false;
            }
        }
    }
}
