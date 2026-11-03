// Port of PKHeX PKHeX.Core/Legality/Verifiers/Egg/EggVerifier.cs, which PKHeX's MiscVerifier runs first.
//
// Not ported, for want of data: the hatch counter's range (EggStateLegality.GetIsEggHatchCyclesValid
// needs each species' hatch cycles and the encounter's own, neither of which PKSE has), TransferEggVersion (needs the encounter's
// EntityContext), EggRelearnFlags (needs the Technical Record flags of every format), and
// MovesShouldMatchRelearnMoves (needs to tell a daycare egg from a gift egg, which PKSE's matcher cannot).
#include <array>
#include <string>

#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/LearnSource/ChainBreed.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/LearnSource/MoveResult.h"
#include "Legality/Verifiers/MiscVerifier.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr int CONTEST_STAT_COUNT = 6;
        constexpr int MOVE_SLOT_COUNT = 4;
        /// PKHeX GameVersion.FRX / LGX: FireRed and LeafGreen on Nintendo Switch.
        constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
        constexpr uint8_t VERSION_LEAFGREEN_SWITCH = 57;

        /// PKHeX Breeding.IsGenderSpeciesDetermination: the two-species lines whose egg's species
        /// follows its gender.
        bool isGenderSpeciesDetermination(uint16_t speciesId)
        {
            using namespace LearnSource;
            return speciesId == NIDORAN_FEMALE_SPECIES || speciesId == NIDORAN_MALE_SPECIES || speciesId == VOLBEAT_SPECIES || speciesId == ILLUMISE_SPECIES;
        }

        /// PKHeX EggVerifier.IsInherited: an egg move, or an inherited level-up move where those count.
        bool isInheritedMove(const LearnSource::MoveResult &result, bool includeInheritedLevelUp)
        {
            const LearnSource::LearnMethod method = result.info.method;
            return method == LearnSource::LearnMethod::EggMove || (includeInheritedLevelUp && method == LearnSource::LearnMethod::InheritLevelUp);
        }

        /// PKHeX ChainBreedUtil.InferFlagsFrom(pk): NintendoSwitchGBA for a FireRed/LeafGreen (Switch)
        /// origin, or ParseSettings.IsExclusivelyNintendoSwitchGBA(pk). That overload reads
        /// `!IsExclusivelyNintendoSwitchGBA() && pk.Format == 3` -- the negation is PKHeX's, kept as is.
        bool isSwitchGameBoyAdvanceEra(const AnalysisContext &context)
        {
            const uint8_t originVersion = context.pokemon.originGame();
            if (originVersion == VERSION_FIRERED_SWITCH || originVersion == VERSION_LEAFGREEN_SWITCH)
            {
                return true;
            }
            const bool isExclusivelySwitchGameBoyAdvance = allowsSwitchGameBoyAdvanceEra(context.options) && !allowsGameBoyAdvanceCartridgeEra(context.options);
            return !isExclusivelySwitchGameBoyAdvance && context.formatGeneration == 3;
        }

        /// PKHeX EggVerifier.VerifyEggBreedingChain: the inherited moves of a bred egg must come from
        /// one chain of fathers. PKHeX also adds a Valid EggBreedChain line when they do; PKSE reports
        /// only findings.
        void verifyEggBreedingChain(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr)
            {
                return;
            }
            const Encounters::Encounterable *encounter = Encounters::getEncounterOriginal(context);
            if (encounter == nullptr || encounter->asEgg() == nullptr)
            {
                return;
            }
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const bool isRelearnSource = encounter->generation() >= 6;
            const LearnSource::BreedingInfo personal = LearnSource::getBreedingInfo(LearnSource::getLearnEnvironment(encounter->version()), encounter->speciesId(), encounter->formId());
            const bool includeInheritedLevelUp = personal.isOnlyFemale() || isGenderSpeciesDetermination(encounter->speciesId());
            const std::array<LearnSource::MoveResult, MOVE_SLOT_COUNT> &results = isRelearnSource ? context.legalityInfo->relearn : context.legalityInfo->moves;

            // Collapse the moves to the ones the breeding chain must explain.
            std::array<uint16_t, MOVE_SLOT_COUNT> inheritedMoves{};
            int inheritedCount = 0;
            for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
            {
                const uint16_t moveId = isRelearnSource ? pokemon.relearnMove(slotIndex) : pokemon.move(slotIndex);
                if (moveId == 0)
                {
                    break;
                }
                if (isInheritedMove(results[slotIndex], includeInheritedLevelUp))
                {
                    inheritedMoves[inheritedCount++] = moveId;
                }
            }
            if (inheritedCount == 0 || (inheritedCount == 1 && !includeInheritedLevelUp))
            {
                return;
            }

            const std::span<const uint16_t> moves(inheritedMoves.data(), inheritedCount);
            if (!LearnSource::isChainBreedValid(encounter->speciesId(), encounter->formId(), encounter->version(), isSwitchGameBoyAdvanceEra(context), moves))
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggMoveCombination);
            }
        }

        /// PKHeX IContestStatsReadOnly.HasContestStats: any contest stat is set.
        bool hasAnyContestStat(const Pokemon::Pokemon &pokemon)
        {
            if (!pokemon.hasContestStats())
            {
                return false;
            }
            for (int statIndex = 0; statIndex < CONTEST_STAT_COUNT; ++statIndex)
            {
                if (pokemon.contestStat(statIndex) != 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX ITechRecord: the Switch formats with Technical Record flags.
        bool hasTechnicalRecords(Enums::GameVersion formatGroup)
        {
            switch (formatGroup)
            {
                case Enums::GameVersion::SWSH:
                case Enums::GameVersion::BDSP:
                case Enums::GameVersion::PLA:
                case Enums::GameVersion::SV:
                case Enums::GameVersion::ZA:
                    return true;
                default:
                    return false;
            }
        }

        /// PKHeX EggVerifier.VerifyCommon, without the hatch counter and record-flag rules.
        void verifyEggCommon(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (hasTechnicalRecords(context.formatGroup) && pokemon.statNature() != pokemon.nature())
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggNature);
            }
        }

        /// PKHeX EggVerifier.VerifyWhileEgg: an unhatched egg's own state.
        void verifyWhileEgg(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            verifyEggCommon(context);

            if (hasAnyContestStat(pokemon))
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggContest);
            }

            // Every Gen 3 egg is Japanese until it hatches.
            const uint8_t japaneseLanguage = static_cast<uint8_t>(Enums::LanguageID::Japanese);
            if (context.formatGeneration == 3 && pokemon.language() != japaneseLanguage)
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::OTLanguageShouldBe, std::string(Enums::getLanguageName(japaneseLanguage)));
            }

            if (pokemon.hasHomeTracker() && pokemon.homeTracker() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferTrackerShouldBeZero);
            }
        }
    }

    void verifyEggs(const AnalysisContext &context)
    {
        verifyEggBreedingChain(context);
        if (context.pokemon.isEgg())
        {
            verifyWhileEgg(context);
        }
    }
}
