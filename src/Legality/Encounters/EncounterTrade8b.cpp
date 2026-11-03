#include "Legality/Encounters/EncounterTrade8b.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8b.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_MAGIKARP = 129;
            constexpr uint16_t SPECIES_CHATOT = 441;
        }

        uint8_t EncounterTrade8b::originalTrainerFriendship() const noexcept
        {
            return properties.speciesId == SPECIES_CHATOT ? 35 : 50;
        }

        std::array<uint8_t, 6> EncounterTrade8b::contestStats() const noexcept
        {
            const uint8_t baseContest = properties.speciesId == SPECIES_CHATOT ? 20 : 0;
            return {baseContest, baseContest, baseContest, baseContest, baseContest, 0};
        }

        bool EncounterTrade8b::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            if (language < 0 || language >= static_cast<int>(properties.trainerNames->size()))
            {
                return false;
            }
            return trainerName == getLanguageName(properties.trainerNames, language);
        }

        bool EncounterTrade8b::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            if (language < 0 || language >= static_cast<int>(properties.nicknames->size()))
            {
                return false;
            }
            return nickname == getLanguageName(properties.nicknames, language);
        }

        std::u16string EncounterTrade8b::getNickname(int language) const
        {
            const bool isInRange = language >= 0 && language < static_cast<int>(properties.nicknames->size());
            return getLanguageName(properties.nicknames, isInRange ? language : 0);
        }

        bool EncounterTrade8b::isMagikarpJapaneseTradedBDSP(const Pokemon::Pokemon &pokemon) const
        {
            if (properties.speciesId != SPECIES_MAGIKARP || pokemon.language() != static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return false;
            }
            const std::u16string trainerName = pokemon.otName();
            return trainerName == u"Diamond." || trainerName == u"Pearl.";
        }

        bool EncounterTrade8b::isPijako(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return properties.speciesId == SPECIES_CHATOT && pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::French) && !pokemon.isNicknamed();
        }

        // PKHeX EncounterTrade8b.DetectMeisterMagikarpLanguage
        int EncounterTrade8b::detectMeisterMagikarpLanguage(const std::u16string &nickname, const std::u16string &trainerName, int currentLanguage) const
        {
            // Received on a German game the record says Japanese; on any other language, German.
            constexpr int languageJapanese = static_cast<int>(Enums::LanguageID::Japanese);
            constexpr int languageGerman = static_cast<int>(Enums::LanguageID::German);
            constexpr int languageChineseTraditional = static_cast<int>(Enums::LanguageID::ChineseTraditional);
            if (currentLanguage != languageJapanese && currentLanguage != languageGerman)
            {
                return -1;
            }
            for (int language = 1; language < languageChineseTraditional; language++)
            {
                if (nickname != getLanguageName(properties.nicknames, language))
                {
                    continue;
                }
                if (trainerName != getLanguageName(properties.trainerNames, language))
                {
                    continue;
                }
                // The language was flipped to another id, so it cannot be the one it shows.
                const int shouldNotBe = currentLanguage == languageGerman ? languageGerman : languageJapanese;
                return language != shouldNotBe ? language : 0;
            }
            return -1;
        }

        bool EncounterTrade8b::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            return isMatchLocation8b(pokemon, pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC, LOCATION_LINK_TRADE6_NPC);
        }

        // PKHeX EncounterTrade8b.IsMatchExact
        bool EncounterTrade8b::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (pokemon.encryptionConstant() != properties.encryptionConstant)
            {
                return false;
            }
            if (pokemon.pid() != properties.PID)
            {
                return false;
            }
            if (ContestStatsReadOnly::isContestBelow(pokemon, contestStats()))
            {
                return false;
            }
            if (RNG::HomeQuirks::hasSizeScalars(pokemon) && (pokemon.heightScalar() != properties.heightScalar || pokemon.weightScalar() != properties.weightScalar))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
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
            if (pokemon.id32() != ID32())
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::BDSP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            return isMatchEggLocation8b(pokemon, pokemon.eggLocation() == LOCATION_DEFAULT8B_NONE);
        }
    }
}
