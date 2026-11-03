#include "Legality/Encounters/EncounterStatic5Entree.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX GlobalLinkPromotionLanguage (flags).
            constexpr uint8_t PROMOTION_LANGUAGE_JAPANESE = 1 << 0;
            constexpr uint8_t PROMOTION_LANGUAGE_KOREAN = 1 << 1;
            constexpr uint8_t PROMOTION_LANGUAGE_INTERNATIONAL = 1 << 2;
            constexpr uint8_t PROMOTION_LANGUAGE_NOT_INTERNATIONAL = PROMOTION_LANGUAGE_JAPANESE | PROMOTION_LANGUAGE_KOREAN;
            constexpr uint8_t PROMOTION_LANGUAGE_NOT_KOREAN = PROMOTION_LANGUAGE_JAPANESE | PROMOTION_LANGUAGE_INTERNATIONAL;
            constexpr uint8_t PROMOTION_LANGUAGE_ALL = PROMOTION_LANGUAGE_JAPANESE | PROMOTION_LANGUAGE_KOREAN | PROMOTION_LANGUAGE_INTERNATIONAL;

            // PKHeX GlobalLinkPromotionExtensions.GetLanguages
            uint8_t getPromotionLanguages(GlobalLinkPromotion promotion) noexcept
            {
                switch (promotion)
                {
                case GlobalLinkPromotion::StartersKanto:
                case GlobalLinkPromotion::Croagunk1:
                case GlobalLinkPromotion::Blissey:
                case GlobalLinkPromotion::Dragonite1:
                    return PROMOTION_LANGUAGE_NOT_INTERNATIONAL;
                case GlobalLinkPromotion::Croagunk2:
                case GlobalLinkPromotion::StartersSinnoh2:
                case GlobalLinkPromotion::Dragonite2:
                case GlobalLinkPromotion::Scizor:
                case GlobalLinkPromotion::Garchomp:
                case GlobalLinkPromotion::Tyranitar:
                case GlobalLinkPromotion::Metagross:
                    return PROMOTION_LANGUAGE_INTERNATIONAL;
                case GlobalLinkPromotion::Arceus:
                case GlobalLinkPromotion::Pikachu:
                    return PROMOTION_LANGUAGE_NOT_KOREAN;
                case GlobalLinkPromotion::StartersHoenn:
                case GlobalLinkPromotion::MonkeyUnova:
                case GlobalLinkPromotion::Gothorita2:
                    return PROMOTION_LANGUAGE_JAPANESE;
                case GlobalLinkPromotion::Rayquaza:
                case GlobalLinkPromotion::Jumpluff:
                    return PROMOTION_LANGUAGE_KOREAN;
                default:
                    return PROMOTION_LANGUAGE_ALL;
                }
            }
        }

        // PKHeX GlobalLinkPromotionExtensions.CanBeReceivedBy
        bool canPromotionBeReceivedBy(GlobalLinkPromotion promotion, uint8_t language) noexcept
        {
            if (promotion == GlobalLinkPromotion::NotPromotion)
            {
                return true;
            }
            const uint8_t restriction = getPromotionLanguages(promotion);
            if (restriction == PROMOTION_LANGUAGE_ALL)
            {
                return true;
            }
            if (language == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                return (restriction & PROMOTION_LANGUAGE_JAPANESE) != 0;
            }
            if (language == static_cast<uint8_t>(Enums::LanguageID::Korean))
            {
                return (restriction & PROMOTION_LANGUAGE_KOREAN) != 0;
            }
            return (restriction & PROMOTION_LANGUAGE_INTERNATIONAL) != 0;
        }

        std::string EncounterStatic5Entree::name() const
        {
            // PKHeX names the template list shared by every Gen 5 game "Gen5".
            const char *versionName = properties.version == Enums::GameVersion::Any ? "Gen5" : getVersionSourceName(properties.version);
            return std::string("Entree Forest Encounter (") + versionName + ")";
        }

        // PKHeX EncounterStatic5Entree.IsMatchExact
        bool EncounterStatic5Entree::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (pokemon.metLocation() != LOCATION_ENTREE_FOREST5)
            {
                return false;
            }
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return canPromotionBeReceivedBy(properties.promotion, pokemon.language());
        }
    }
}
