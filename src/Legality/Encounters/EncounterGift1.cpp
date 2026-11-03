#include "Legality/Encounters/EncounterGift1.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PSYDUCK = 54;
            constexpr uint16_t TRAINER_ID_STADIUM_JAPANESE = 1999;
            constexpr uint16_t TRAINER_ID_STADIUM_INTERNATIONAL = 2000;
            /// Red/Green's Japanese release date, 27 February 1996.
            constexpr uint16_t TRAINER_ID_VIRTUAL_CONSOLE_MEW = 22796;
            /// The Pokemon Stadium gift boxes, whose catch-rate byte the gift keeps.
            constexpr uint8_t CATCH_RATE_NORMAL_BOX = 167;
            constexpr uint8_t CATCH_RATE_GORGEOUS_BOX = 168;
            constexpr uint8_t LANGUAGE_JAPANESE = 1;

            bool isTourTrainer(const std::u16string &name)
            {
                static constexpr std::array<const char16_t *, 19> TOUR_TRAINERS = {
                    u"YOSHIRA", u"YOSHIRB", u"YOSHIBA", u"YOSHIBB", u"LINKE", u"LINKW", u"LUIGE", u"LUIGW", u"LUIGIC", u"YOSHIC",
                    u"EUROPE", u"SWEDEN", u"NORWAY", u"FINLAND", u"DENMARK", u"AUSTRIA", u"UK", u"NAL", u"MARIO",
                };
                for (const char16_t *tourName : TOUR_TRAINERS)
                {
                    if (name == tourName)
                    {
                        return true;
                    }
                }
                return false;
            }

            /// The Japanese event Mews' trainer names: Nintendo Space World '97 and '99 and the
            /// Next Generation World Hobby Fair Dome Cup.
            bool isJapanTrainer(const std::u16string &name)
            {
                static constexpr std::array<const char16_t *, 12> JAPAN_TRAINERS = {
                    u"マクハリ", u"フクオカ", u"トウキョー", u"オーサカ",
                    u"サッポロ", u"ナゴヤ", u"マリオ", u"クッパ",
                    u"ルイージ", u"ピーチ", u"ヨッシー", u"ドンキー",
                };
                for (const char16_t *japanName : JAPAN_TRAINERS)
                {
                    if (name == japanName)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        RNG::IndividualValueSet EncounterGift1::fixedIVs() const noexcept
        {
            if (giftTrainer == TrainerType::EuropeTour || giftTrainer == TrainerType::JapanTour)
            {
                return RNG::IndividualValueSet{5, 10, 1, 12, 5, 5, true};
            }
            if (giftTrainer == TrainerType::VirtualConsoleMew)
            {
                return RNG::IndividualValueSet{15, 15, 15, 15, 15, 15, true};
            }
            return RNG::IndividualValueSet{};
        }

        // PKHeX EncounterGift1.IsMatchExact
        bool EncounterGift1::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (giftLanguage != LanguageRestriction::Any && GameBoyEntity::isJapanese(pokemon) != (giftLanguage == LanguageRestriction::Japanese))
            {
                return false;
            }
            // A PK1 stores no met location; Gen 2 met data rules out a Gen 1 gift.
            if (GameBoyEntity::getCaughtData(pokemon) != 0)
            {
                return false;
            }
            if (giftLevel > evolution.levelMax)
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(giftSpeciesId, 0, pokemon.formID(), Enums::GameVersion::RBY, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            const RNG::IndividualValueSet individualValues = fixedIVs();
            if (individualValues.isSpecified && getFormatGeneration(pokemon) <= 2 && !isFixedIVSequenceValidNoRandom(individualValues, pokemon))
            {
                return false;
            }
            // The gift's shiny state is RNG::Shiny::Random, so PKHeX's IsShinyValid always passes.
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

        bool EncounterGift1::isTrainerNameValid(const Pokemon::Pokemon &pokemon) const
        {
            if (giftTrainer == TrainerType::Recipient)
            {
                return true;
            }
            const std::u16string trainerName = pokemon.otName();
            if (giftTrainer == TrainerType::EuropeTour)
            {
                return isTourTrainer(trainerName);
            }
            if (giftTrainer == TrainerType::JapanTour)
            {
                return isJapanTrainer(trainerName);
            }
            const bool isLanguageJapanese = pokemon.language() == LANGUAGE_JAPANESE;
            if (giftTrainer == TrainerType::VirtualConsoleMew)
            {
                return trainerName == (isLanguageJapanese ? u"ゲーフリ" : u"GF");
            }
            if (giftTrainer == TrainerType::Stadium)
            {
                if (isLanguageJapanese)
                {
                    return trainerName == u"スタジアム";
                }
                return trainerName == u"STADIUM" || trainerName == u"STADE" || trainerName == u"STADIO" || trainerName == u"ESTADIO";
            }
            return true;
        }

        bool EncounterGift1::isTrainerIDValid(const Pokemon::Pokemon &pokemon) const noexcept
        {
            switch (giftTrainer)
            {
            case TrainerType::VirtualConsoleMew:
                return pokemon.tid16() == TRAINER_ID_VIRTUAL_CONSOLE_MEW;
            case TrainerType::Stadium:
                return pokemon.tid16() == (GameBoyEntity::isJapanese(pokemon) ? TRAINER_ID_STADIUM_JAPANESE : TRAINER_ID_STADIUM_INTERNATIONAL);
            default:
                return true;
            }
        }

        // PKHeX EncounterGift1.GetMatchRating
        EncounterMatchRating EncounterGift1::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.getGameGroup() == Enums::GameVersion::RBY && !isCatchRateValid(GameBoyEntity::getCatchRate(pokemon)))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        bool EncounterGift1::isCatchRateValid(uint8_t rate) const noexcept
        {
            if (ALLOW_GEN1_TRADEBACK && GBRestrictions::isCatchRateHeldItem(rate))
            {
                return true;
            }
            if (isStadium())
            {
                if (giftSpeciesId == SPECIES_PSYDUCK)
                {
                    return rate == CATCH_RATE_GORGEOUS_BOX;
                }
                return rate == CATCH_RATE_NORMAL_BOX || rate == CATCH_RATE_GORGEOUS_BOX;
            }
            return Pokemon::getPersonalRecord(Enums::GameVersion::RBY, giftSpeciesId, 0).catchRate == rate;
        }
    }
}
