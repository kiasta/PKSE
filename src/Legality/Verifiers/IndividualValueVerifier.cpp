// Port of PKHeX PKHeX.Core/Legality/Verifiers/IndividualValueVerifier.cs: individual values.
#include "Legality/Verifiers/Verifiers.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterSlot7.h"
#include "Legality/Encounters/EncounterSlot7GO.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/EncounterTrade4RanchGift.h"
#include "Legality/Encounters/EncounterTemplateType.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWA8.h"
#include "Legality/Encounters/MysteryGiftWA9.h"
#include "Legality/Encounters/MysteryGiftWB7.h"
#include "Legality/Encounters/MysteryGiftWB8.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/MysteryGiftWC9.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/MRNGReversal.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Pokemon/AbilityInfo.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
        /// A Sun/Moon SOS chain of 10 gives the hidden ability at 5% with 2 guaranteed perfect IVs.
        constexpr uint8_t SOS_HIDDEN_ABILITY_FLAWLESS_COUNT = 2;

        /// PKHeX PKM.FlawlessIVCount: the stored IVs at the format's maximum.
        int getFlawlessIVCount(const Pokemon::Pokemon &pokemon)
        {
            const uint8_t maximumIV = pokemon.maxIV();
            return (pokemon.ivHP() == maximumIV) + (pokemon.ivATK() == maximumIV) + (pokemon.ivDEF() == maximumIV) + (pokemon.ivSPE() == maximumIV) + (pokemon.ivSPA() == maximumIV) + (pokemon.ivSPD() == maximumIV);
        }

        void verifyFlawlessCount(const AnalysisContext &context, int requiredFlawlessCount)
        {
            if (getFlawlessIVCount(context.pokemon) < requiredFlawlessCount)
            {
                context.add(Severity::Invalid, CheckIdentifier::IndividualValues, ResultCode::IVFlawlessCountAtLeast, std::to_string(requiredFlawlessCount));
            }
        }

        /// PKHeX AbilityVerifier.CanAbilityPatch: whether the Ability Patch could have given this hidden ability.
        bool canAbilityPatch(const AnalysisContext &context)
        {
            if (context.formatGeneration < 8)
            {
                return false;
            }
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Pokemon::AbilitySlots abilitySlots = Pokemon::getAbilitySlots(pokemon.speciesID(), pokemon.form(), context.formatGroup);
            if (abilitySlots.slot[0] != abilitySlots.slot[2] || abilitySlots.slot[1] != abilitySlots.slot[2])
            {
                return true;
            }
            // AbilityChangeRules.IsFormChangeDifferentHidden: another form has a distinct hidden ability.
            constexpr uint16_t formChangeDifferentHiddenSpecies[] = {487, 641, 642, 645, 905};
            for (const uint16_t speciesId : formChangeDifferentHiddenSpecies)
            {
                if (pokemon.speciesID() == speciesId)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX IndividualValueVerifier.VerifyIVsGen7, for a Sun/Moon or Ultra Sun/Ultra Moon wild slot (EncounterSlot7).
        void verifyGenerationSevenSlot(const AnalysisContext &context)
        {
            if (context.pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && !canAbilityPatch(context))
            {
                verifyFlawlessCount(context, SOS_HIDDEN_ABILITY_FLAWLESS_COUNT);
            }
        }
    }

    namespace
    {
        using CardIndividualValues = std::array<int, 6>;

        // PKHeX VerifyIVsMystery: 0xFC-0xFE asks for (value - 0xFB) flawless IVs; above 31 is random.
        constexpr int FLAWLESS_IV_MARKER_FIRST = 0xFC;
        constexpr int FLAWLESS_IV_MARKER_LAST = 0xFE;
        constexpr int FLAWLESS_IV_MARKER_BASE = 0xFB;
        constexpr int MAX_INDIVIDUAL_VALUE = 31;

        template <typename Card>
        CardIndividualValues getCardIndividualValues(const Card &card)
        {
            CardIndividualValues individualValues{};
            for (size_t statIndex = 0; statIndex < individualValues.size(); ++statIndex)
            {
                individualValues[statIndex] = card.card().individualValues[statIndex];
            }
            return individualValues;
        }

        /// PKHeX MysteryGift.HasFixedIVs and GetIVs (HP, Atk, Def, Spe, SpA, SpD); empty for an
        /// encounter that is not a Mystery Gift, or a card with PID/IV-correlated IVs.
        std::optional<CardIndividualValues> getMysteryGiftIndividualValues(const Encounters::Encounterable &encounter)
        {
            namespace Encounters = Legality::Encounters;
            if (const Encounters::MysteryGiftPCD *card = Encounters::encounterAs<Encounters::MysteryGiftPCD>(encounter))
            {
                const RNG::IndividualValueSet fixedIVs = card->fixedIVs();
                if (!fixedIVs.isSpecified)
                {
                    return std::nullopt;
                }
                return CardIndividualValues{fixedIVs.HP, fixedIVs.ATK, fixedIVs.DEF, fixedIVs.SPE, fixedIVs.SPA, fixedIVs.SPD};
            }
            if (Encounters::encounterAs<Encounters::MysteryGiftPGT>(encounter))
            {
                return std::nullopt; // the Ranger Manaphy egg's IVs are not fixed
            }
            if (const Encounters::MysteryGiftPGF *card = Encounters::encounterAs<Encounters::MysteryGiftPGF>(encounter))
            {
                CardIndividualValues individualValues{};
                for (size_t statIndex = 0; statIndex < individualValues.size(); ++statIndex)
                {
                    individualValues[statIndex] = card->giftData().individualValues[statIndex];
                }
                return individualValues;
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWC6>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWC7>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWB7>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWC8>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWB8>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWA8>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWC9>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            if (const auto *card = Encounters::encounterAs<Encounters::MysteryGiftWA9>(encounter))
            {
                return getCardIndividualValues(*card);
            }
            return std::nullopt;
        }

        /// PKHeX IndividualValueVerifier.VerifyIVsMystery.
        void verifyMysteryGiftIndividualValues(const AnalysisContext &context, const CardIndividualValues &individualValues)
        {
            for (const int individualValue : individualValues)
            {
                if (individualValue >= FLAWLESS_IV_MARKER_FIRST && individualValue <= FLAWLESS_IV_MARKER_LAST)
                {
                    verifyFlawlessCount(context, individualValue - FLAWLESS_IV_MARKER_BASE);
                    return;
                }
            }
            // Legal.GetIsFixedIVSequenceValidSkipRand: only the values the card specifies must match.
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const int pokemonIndividualValues[6] = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD()};
            for (size_t statIndex = 0; statIndex < individualValues.size(); ++statIndex)
            {
                const int individualValue = individualValues[statIndex];
                if (individualValue >= 0 && individualValue <= MAX_INDIVIDUAL_VALUE && individualValue != pokemonIndividualValues[statIndex])
                {
                    context.add(Severity::Invalid, CheckIdentifier::IndividualValues, ResultCode::EncounterGiftIVMismatch);
                    return;
                }
            }
        }

        /// PKHeX IndividualValueVerifier.VerifyIVsRanch4: the IVs come from three consecutive
        /// rand() calls, so some seed must produce them.
        void verifyRanchIndividualValues(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            uint32_t seeds[RNG::LCRNG::MAX_IV_SEED_COUNT];
            const int seedCount = RNG::MRNGReversal::getSeedsIVs(seeds, pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPA(), pokemon.ivSPD(), pokemon.ivSPE());
            if (seedCount == 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::IndividualValues, ResultCode::PIDTypeMismatch);
            }
        }
    }

    void verifyIndividualValues(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
        {
            // PKHeX's switch on EncounterMatch: IPogoSlot, IFlawlessIVCount, EncounterSlot7,
            // MysteryGift, EncounterTrade4RanchGift. No card is IFlawlessIVCount, so a card is
            // judged by its own IVs.
            const std::optional<CardIndividualValues> cardIndividualValues = getMysteryGiftIndividualValues(*encounterMatch);
            const Encounters::PogoSlotTemplate *pogo = Encounters::encounterAs<Encounters::EncounterSlot7GO>(encounterMatch);
            if (pogo == nullptr)
            {
                pogo = Encounters::encounterAs<Encounters::EncounterSlot8GO>(encounterMatch);
            }
            if (pogo != nullptr)
            {
                if (!pogo->areIVsValid(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::IndividualValues, ResultCode::IVNotCorrect);
                }
            }
            else if (cardIndividualValues.has_value())
            {
                verifyMysteryGiftIndividualValues(context, *cardIndividualValues);
            }
            else if (Encounters::isMysteryGift(encounterMatch->templateType()))
            {
                // A card with PID/IV-correlated IVs.
            }
            else if (encounterMatch->flawlessIVCount() != 0)
            {
                verifyFlawlessCount(context, encounterMatch->flawlessIVCount());
            }
            else if (Encounters::encounterAs<Encounters::EncounterSlot7>(encounterMatch))
            {
                verifyGenerationSevenSlot(context);
            }
            else if (Encounters::encounterAs<Encounters::EncounterTrade4RanchGift>(encounterMatch))
            {
                verifyRanchIndividualValues(context);
            }
        }

        const uint8_t hitPointsIV = pokemon.ivHP();
        constexpr uint8_t allEqualCeiling = 30;
        if (hitPointsIV < allEqualCeiling && pokemon.ivATK() == hitPointsIV && pokemon.ivDEF() == hitPointsIV && pokemon.ivSPA() == hitPointsIV && pokemon.ivSPD() == hitPointsIV && pokemon.ivSPE() == hitPointsIV)
        {
            context.add(Severity::Warning, CheckIdentifier::IndividualValues, ResultCode::IVAllEqual, std::to_string(hitPointsIV));
        }
    }
}
