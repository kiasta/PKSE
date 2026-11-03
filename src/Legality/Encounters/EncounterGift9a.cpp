#include "Legality/Encounters/EncounterGift9a.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/LumioseSolver.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// Indexed by language id (1 Japanese .. 11 Latin American Spanish); 0 and 6 have none.
            using GiftNames9a = std::array<const char16_t *, 12>;

            // PKHeX EncounterGift9a.GetFixedTrainerName and GetGimmighoulFixedNickname. The Spanish AZ
            // is written with a no-break space, as the game writes it.
            constexpr GiftNames9a NAMES_LUCARIO = {nullptr, u"\u30B3\u30EB\u30CB", u"Korrina", u"Corn\u00E9lia", u"Ornella", u"Connie", nullptr, u"Corelia", u"\uCF54\uB974\uB2C8", u"\u53EF\u5C14\u59AE", u"\u53EF\u723E\u59AE", u"Korrina"};
            constexpr GiftNames9a NAMES_FLOETTE = {nullptr, u"\uFF21\uFF3A", u"AZ", u"A.Z.", u"AZ", u"Azett", nullptr, u"A.\u00A0Z.", u"AZ", u"\uFF21\uFF3A", u"\uFF21\uFF3A", u"A.\u00A0Z."};
            constexpr GiftNames9a NAMES_STUNFISK = {nullptr, u"\u30B0\u30E9\u30A6\u30F3", u"Terri", u"Gad", u"Terrence", u"Terry", nullptr, u"Terry", u"\uADF8\uB77C\uC6B4", u"\u5E1D\u5C1A", u"\u5E1D\u5C1A", u"Ren\u00E9"};
            constexpr GiftNames9a NAMES_GIMMIGHOUL = {nullptr, u"\u30BB\u30ED", u"Thelo", u"Th\u00E9lo", u"Petens", u"Will", nullptr, u"Thelo", u"\uD154\uB85C", u"\u6CD5\u624D", u"\u6CD5\u624D", u"Turian"};
            constexpr GiftNames9a NAMES_MAGEARNA = {nullptr, u"\u30B8\u30A7\u30C3\u30C8", u"Jett", u"Bridjet", u"Aviona", u"Jette", nullptr, u"Viona", u"\uC81C\uD2B8", u"\u6377\u6735", u"\u6377\u6735", u"Viona"};
            constexpr GiftNames9a NAMES_GIMMIGHOUL_NICKNAME = {nullptr, u"\u306F\u3053\u3044\u308A", u"Chestly", u"Tr\u00E9sor", u"Bauletto", u"Sch\u00E4tzchen", nullptr, u"Tesorina", u"\uBCF4\uBB34\uB9AC", u"\u5343\u91D1", u"\u5343\u91D1", u"Tesorita"};

            const GiftNames9a *getTrainerNames(TrainerGift9a trainer) noexcept
            {
                switch (trainer)
                {
                case TrainerGift9a::Lucario:
                    return &NAMES_LUCARIO;
                case TrainerGift9a::Floette:
                    return &NAMES_FLOETTE;
                case TrainerGift9a::Stunfisk:
                    return &NAMES_STUNFISK;
                case TrainerGift9a::Gimmighoul:
                    return &NAMES_GIMMIGHOUL;
                case TrainerGift9a::Magearna:
                    return &NAMES_MAGEARNA;
                default:
                    return nullptr;
                }
            }

            /// An empty name for a language PKHeX has no entry for (where PKHeX throws).
            std::u16string getGiftName(const GiftNames9a *names, int language)
            {
                if (names == nullptr || language < 0 || language >= static_cast<int>(names->size()) || (*names)[language] == nullptr)
                {
                    return std::u16string();
                }
                return std::u16string((*names)[language]);
            }

            // PKHeX EncounterGift9a.GetFixedTrainerID32
            uint32_t getTrainerID32(TrainerGift9a trainer) noexcept
            {
                switch (trainer)
                {
                case TrainerGift9a::Lucario:
                    return 912562;
                case TrainerGift9a::Floette:
                    return 1;
                case TrainerGift9a::Stunfisk:
                    return 250932;
                case TrainerGift9a::Gimmighoul:
                    return 115090;
                case TrainerGift9a::Magearna:
                    return 981300;
                default:
                    return 0;
                }
            }

            // PKHeX EncounterGift9a.GetFixedTrainerGender: Korrina is female; Jett's gift is set as male
            // even though Jett is female.
            uint8_t getTrainerGender(TrainerGift9a trainer) noexcept
            {
                return trainer == TrainerGift9a::Lucario ? 1 : 0;
            }

            // PKHeX EncounterGift9a.IsMatchFixedTrainer
            bool isMatchFixedTrainer(const Pokemon::Pokemon &pokemon, TrainerGift9a trainer)
            {
                if (pokemon.id32() != getTrainerID32(trainer))
                {
                    return false;
                }
                if (pokemon.otGender() != getTrainerGender(trainer))
                {
                    return false;
                }
                return pokemon.otName() == getGiftName(getTrainerNames(trainer), pokemon.language());
            }
        }

        bool EncounterGift9a::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)language;
            return trainerName == getGiftName(getTrainerNames(properties.trainer), pokemon.language());
        }

        bool EncounterGift9a::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)language;
            return nickname == getGiftName(&NAMES_GIMMIGHOUL_NICKNAME, pokemon.language());
        }

        std::u16string EncounterGift9a::getNickname(int language) const
        {
            return isFixedNickname() ? getGiftName(&NAMES_GIMMIGHOUL_NICKNAME, language) : std::u16string();
        }

        // PKHeX EncounterGift9a.IsMatchExact
        bool EncounterGift9a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (pokemon.metLocation() != properties.location)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::ZA, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            if (properties.nature != NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.hasAlphaFlag() && pokemon.isAlpha() != properties.isAlpha)
            {
                return false;
            }
            if (properties.trainer != TrainerGift9a::None && !isMatchFixedTrainer(pokemon, properties.trainer))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterGift9a.GetMatchRating (IsMatchPartial, then IsMatchDeferred)
        EncounterMatchRating EncounterGift9a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isShinyPokemon = isShiny(pokemon);
            if ((properties.shiny == RNG::Shiny::Never && isShinyPokemon) || (properties.shiny == RNG::Shiny::Always && !isShinyPokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.scale != 0 && pokemon.hasScale() && pokemon.scale() != properties.scale)
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.shiny != RNG::Shiny::Random && !isShinyValid(properties.shiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (tryGetSeed(pokemon) != SeedCorrelationResult::Success)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterGift9a.TryGetSeed
        SeedCorrelationResult EncounterGift9a::tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (isSeedFound9a(getParameters(), pokemon))
            {
                return SeedCorrelationResult::Success;
            }
            if (isShiny(pokemon) && !RNG::LumioseSolver::SEARCH_SHINY_SINGLE_ROLL)
            {
                return SeedCorrelationResult::Ignore;
            }
            return SeedCorrelationResult::Invalid;
        }

        RNG::GenerateParam9a EncounterGift9a::getParameters() const noexcept
        {
            RNG::GenerateParam9a parameters;
            parameters.genderRatio = getGenderRatio9a(properties.gender, properties.speciesId, properties.formId);
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.rollCount = 1;
            if (properties.isAlpha)
            {
                parameters.correlation = RNG::LumioseCorrelation::PreApplyIVs;
            }
            else if (properties.individualValues.isSpecified || properties.flawlessIVCount != 0)
            {
                parameters.correlation = RNG::LumioseCorrelation::ReApplyIVs;
            }
            parameters.sizeType = properties.scale == 0 ? RNG::SizeType9::RANDOM : RNG::SizeType9::VALUE;
            parameters.scale = properties.scale;
            parameters.nature = properties.nature;
            parameters.ability = RNG::AbilityPermission::Any12;
            parameters.shiny = properties.shiny;
            parameters.individualValues = properties.individualValues;
            return parameters;
        }
    }
}
