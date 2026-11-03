#include "Legality/Encounters/EncounterTrade1.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_DUGTRIO = 51;
            constexpr uint16_t SPECIES_HAUNTER = 93;
            constexpr int LANGUAGE_JAPANESE = 1;
            constexpr int LANGUAGE_UNUSED_6 = 6;
            /// PKHeX ParseSettings.AllowGBStadium2 with no Game Boy cartridge save open (its default).
            constexpr bool ALLOW_GB_STADIUM2 = false;

            /// PKHeX EncounterTrade1.HiraganaDugtrio7: Bank rewrites the Japanese Dugtrio's
            /// nickname from "ぐリお" to "ぐりお".
            constexpr const char16_t *HIRAGANA_DUGTRIO7 = u"ぐりお";

            /// PKHeX StringConverter12Transporter.GetTradeNameGen1: the OT Bank gives an in-game trade.
            const char16_t *getTradeNameGen1(int language) noexcept
            {
                switch (language)
                {
                case 1:
                    return u"トレーナー";
                case 2:
                case 5:
                    return u"Trainer";
                case 3:
                    return u"Dresseur";
                case 4:
                    return u"Allenatore";
                case 7:
                    return u"Entrenador";
                default:
                    return u"";
                }
            }

            // PKHeX EncounterTrade1.IsTrainerNameValid
            bool isTrainerNameValid(const Pokemon::Pokemon &pokemon)
            {
                if (getFormatGeneration(pokemon) <= 2)
                {
                    return GameBoyEntity::isTradeTrainerMarker(pokemon);
                }
                return pokemon.otName() == getTradeNameGen1(pokemon.language());
            }

            // PKHeX EncounterTrade1.DetectLanguage
            int detectLanguage(const std::u16string &name, const LanguageNames &names, int start)
            {
                for (int languageIndex = start; languageIndex < static_cast<int>(names.size()); ++languageIndex)
                {
                    if (languageIndex == LANGUAGE_UNUSED_6)
                    {
                        continue;
                    }
                    if (name == names[static_cast<size_t>(languageIndex)])
                    {
                        return languageIndex;
                    }
                }
                return -1;
            }
        }

        RNG::AbilityPermission EncounterTrade1::ability() const noexcept
        {
            return tradeSpeciesId == SPECIES_HAUNTER ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlyHidden;
        }

        bool EncounterTrade1::canObtainMinGSC() const noexcept
        {
            if (!ALLOW_GEN1_TRADEBACK)
            {
                return false;
            }
            if (tradeVersion == Enums::GameVersion::BU && tradeEvolveOnTrade)
            {
                return ALLOW_GB_STADIUM2;
            }
            return true;
        }

        bool EncounterTrade1::isNicknameValid(const Pokemon::Pokemon &pokemon, const std::u16string &nickname) const
        {
            // An international nickname can come from any language the trade can reach.
            if (!GameBoyEntity::isJapanese(pokemon))
            {
                return detectLanguage(nickname, *nicknames, 2) >= 2;
            }
            // Only the Japanese Dugtrio's Hiragana nickname changes on the way to Gen 7.
            if (getFormatGeneration(pokemon) > 2 && tradeVersion == Enums::GameVersion::YW && tradeSpeciesId == SPECIES_DUGTRIO)
            {
                return nickname == HIRAGANA_DUGTRIO7;
            }
            return nickname == (*nicknames)[LANGUAGE_JAPANESE];
        }

        bool EncounterTrade1::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &, int) const
        {
            return isTrainerNameValid(pokemon);
        }

        bool EncounterTrade1::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int) const
        {
            return isNicknameValid(pokemon, nickname);
        }

        std::u16string EncounterTrade1::getNickname(int language) const
        {
            const size_t languageIndex = language >= 0 && static_cast<size_t>(language) < nicknames->size() ? static_cast<size_t>(language) : 0;
            return (*nicknames)[languageIndex];
        }

        // PKHeX EncounterTrade1.GetMatchRating
        EncounterMatchRating EncounterTrade1::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterTrade1.IsMatchExact
        bool EncounterTrade1::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchLevel(pokemon, evolution.levelMax))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(tradeSpeciesId, 0, pokemon.formID(), Enums::GameVersion::RBY, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (tradeVersion == Enums::GameVersion::BU)
            {
                // These trades only exist in the Japanese Blue.
                if (!GameBoyEntity::isJapanese(pokemon))
                {
                    return false;
                }
                // Stadium 2 can move a Pokemon from Gen 2 back without a trade, so it may stay unevolved.
                if (tradeEvolveOnTrade && !ALLOW_GB_STADIUM2 && pokemon.level() < tradeLevelMinRBY)
                {
                    return false;
                }
            }
            return true;
        }

        bool EncounterTrade1::isMatchLevel(const Pokemon::Pokemon &pokemon, uint8_t level) const noexcept
        {
            if (pokemon.getGameGroup() != Enums::GameVersion::RBY || canObtainMinGSC())
            {
                return level >= tradeLevelMinGSC;
            }
            return level >= levelMin();
        }

        bool EncounterTrade1::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (!isTrainerNameValid(pokemon))
            {
                return true;
            }
            if (!isNicknameValid(pokemon, pokemon.nickname()))
            {
                return true;
            }
            if (tradeEvolveOnTrade && pokemon.speciesID() == tradeSpeciesId)
            {
                return false;
            }
            if (ALLOW_GEN1_TRADEBACK)
            {
                return false;
            }
            if (pokemon.getGameGroup() != Enums::GameVersion::RBY)
            {
                return false;
            }
            return GBRestrictions::getCatchRate1(tradeVersion, tradeSpeciesId) != GameBoyEntity::getCatchRate(pokemon);
        }
    }
}
