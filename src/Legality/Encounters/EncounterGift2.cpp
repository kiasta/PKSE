#include "Legality/Encounters/EncounterGift2.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterStatic2.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_CELEBI = 251;
            constexpr int LANGUAGE_JAPANESE = 1;
            constexpr uint16_t TRAINER_ID_STADIUM_JPN = 2000;
            constexpr uint16_t TRAINER_ID_STADIUM_ENG = 2000;
            constexpr uint16_t TRAINER_ID_STADIUM_INT = 2001;
        }

        // PKHeX EncounterGift2.IsMatchExact
        bool EncounterGift2::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isShiny = GameBoyEntity::isShiny(pokemon);
            if (giftShiny == RNG::Shiny::Always && !isShiny)
            {
                return false;
            }
            if (!isMatchEggLocationGameBoy(pokemon, isEgg(), giftEggCycles))
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
            // PKHeX's gifts have no fixed IVs, so its IVs.IsSpecified block never runs.
            if (evolution.formId != 0 && !isFormChangeable(giftSpeciesId, 0, pokemon.formID(), Enums::GameVersion::GSC, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (!isLanguageValid(pokemon.language()))
            {
                return false;
            }
            if (giftCurrentLevel != 0 && giftCurrentLevel > pokemon.level())
            {
                return false;
            }
            // There is no PID to check, so the shiny state is checked here.
            if (giftShiny == RNG::Shiny::Never && isShiny)
            {
                return false;
            }
            if (isEgg() && !pokemon.isEgg())
            {
                return true;
            }
            if (!isTrainerIDValid(pokemon))
            {
                return false;
            }
            if (!isTrainerNameValid(pokemon))
            {
                return false;
            }
            return true;
        }

        bool EncounterGift2::isLanguageValid(int language) const noexcept
        {
            if (language == LANGUAGE_JAPANESE)
            {
                return giftLanguage == LanguageRestriction::Japanese;
            }
            return giftLanguage != LanguageRestriction::Japanese;
        }

        bool EncounterGift2::isTrainerNameValid(const Pokemon::Pokemon &pokemon) const
        {
            const std::u16string trainerName = pokemon.otName();
            switch (giftTrainer)
            {
            case TrainerType::GiftStadiumINT:
                return trainerName == u"Stadion" || trainerName == u"Stade" || trainerName == u"Stadio" || trainerName == u"Estadio";
            case TrainerType::GiftStadiumJPN:
                return trainerName == u"スタジアム";
            case TrainerType::GiftStadiumENG:
                return trainerName == u"Stadium";
            case TrainerType::PokemonCenterNewYork:
                return trainerName == u"PCNYa" || trainerName == u"PCNYb" || trainerName == u"PCNYc" || trainerName == u"PCNYd";
            case TrainerType::Recipient:
                return true;
            }
            return true;
        }

        bool EncounterGift2::isTrainerIDValid(const Pokemon::Pokemon &pokemon) const noexcept
        {
            switch (giftTrainer)
            {
            case TrainerType::GiftStadiumJPN:
                return pokemon.tid16() == TRAINER_ID_STADIUM_JPN;
            case TrainerType::GiftStadiumENG:
                return pokemon.tid16() == TRAINER_ID_STADIUM_ENG;
            case TrainerType::GiftStadiumINT:
                return pokemon.tid16() == TRAINER_ID_STADIUM_INT;
            case TrainerType::Recipient:
            case TrainerType::PokemonCenterNewYork:
                return true;
            }
            return true;
        }

        bool EncounterGift2::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            if (isEgg() && !pokemon.isEgg())
            {
                return true;
            }
            if (!GameBoyEntity::hasCaughtDataField(pokemon))
            {
                return true;
            }
            if (version() == Enums::GameVersion::C)
            {
                if (GameBoyEntity::getCaughtData(pokemon) != 0)
                {
                    return giftLocation == pokemon.metLocation();
                }
                // Nothing can wipe a Crystal Celebi's met data.
                if (pokemon.speciesID() == SPECIES_CELEBI)
                {
                    return false;
                }
            }
            return true;
        }

        bool EncounterGift2::isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (evolution.levelMax < giftLevel)
            {
                return false;
            }
            if (GameBoyEntity::getCaughtData(pokemon) != 0)
            {
                return pokemon.metLevel() == (isEgg() ? 1 : giftLevel);
            }
            return true;
        }
    }
}
