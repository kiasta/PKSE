// Port of PKHeX PKHeX.Core/Legality/Verifiers/MovePPVerifier.cs: move PP and PP Ups.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Names/MoveInfo.h"
#include "Pokemon/Experience.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr int MOVE_SLOT_COUNT = 4;

        /// PKHeX MoveHealState: which PP values a slot may hold.
        enum MoveHealState : uint8_t
        {
            MOVE_HEAL_STATE_NONE = 0,
            MOVE_HEAL_STATE_ALLOW_ZERO = 1 << 0,
            MOVE_HEAL_STATE_ALLOW_HEALED = 1 << 1,
            MOVE_HEAL_STATE_ALLOW_USED = 1 << 2,
            MOVE_HEAL_STATE_ALLOW_HEALED_OR_ZERO = MOVE_HEAL_STATE_ALLOW_ZERO | MOVE_HEAL_STATE_ALLOW_HEALED,
            MOVE_HEAL_STATE_ONLY_HEALED = 1 << 3,
            MOVE_HEAL_STATE_ONLY_ZERO = 1 << 4,
            MOVE_HEAL_STATE_ANY = MOVE_HEAL_STATE_ALLOW_ZERO | MOVE_HEAL_STATE_ALLOW_HEALED | MOVE_HEAL_STATE_ALLOW_USED,
        };

        // PKHeX PKM.GetMovePP, and GBPKM.GetMovePP for the Game Boy formats: base PP plus what the PP Ups
        // add. Not clamped, unlike Names::getMoveMaxPP, because a stored PP Up count above 3 is judged
        // by the PP Up check rather than folded away here.
        uint32_t getHealedPP(uint16_t moveId, uint32_t ppUps, Enums::GameVersion formatGroup, int formatGeneration)
        {
            // PKHeX's Z-A PP table (MoveInfo9a.PP) opens with 35 where every other has 0, and the game
            // does store 35 PP in an empty Z-A move slot; Names::getMoveBasePP reads 0 for the empty slot.
            constexpr uint32_t emptySlotBasePPZA = 35;
            const uint32_t basePP = (moveId == 0 && formatGroup == Enums::GameVersion::ZA) ? emptySlotBasePPZA : Names::getMoveBasePP(moveId, formatGroup);
            if (formatGeneration < 3)
            {
                const uint32_t gameBoyBoost = basePP / 5 < 7 ? basePP / 5 : 7;
                return basePP + ppUps * gameBoyBoost;
            }
            return basePP * (5 + ppUps) / 5;
        }

        // PKHeX Legal.IsPPUpAvailable(PKM): Legends: Arceus and Legends: Z-A have no PP Ups.
        bool isPPUpAvailable(Enums::GameVersion formatGroup)
        {
            return formatGroup != Enums::GameVersion::PLA && formatGroup != Enums::GameVersion::ZA;
        }

        // PKHeX Legal.IsPPUpAvailable(ushort).
        bool isPPUpAvailableForMove(uint16_t moveId)
        {
            constexpr uint16_t sketchMove = 166;
            constexpr uint16_t revivalBlessingMove = 863;
            return moveId != 0 && moveId != sketchMove && moveId != revivalBlessingMove;
        }

        // PKHeX PKM.VC.
        bool isVirtualConsoleOrigin(const Pokemon::Pokemon &pokemon)
        {
            constexpr uint8_t firstVirtualConsoleVersion = 35;
            constexpr uint8_t lastVirtualConsoleVersion = 41;
            return pokemon.hasOriginGame() && pokemon.originGame() >= firstVirtualConsoleVersion && pokemon.originGame() <= lastVirtualConsoleVersion;
        }

        // PKHeX LegalityAnalysis.IsStoredSlot: an unknown slot counts as every slot.
        bool isStoredSlot(const AnalysisContext &context, StorageSlotType slotType)
        {
            return context.options.slotType == slotType || context.options.slotType == StorageSlotType::Unknown;
        }

        // PKHeX HomeQuirks.HasEnteredReachingZA. The encounter's context is the origin game's, which for a
        // Z-A record is Z-A unless it came from another game.
        bool hasEnteredHomeReachingZA(const Pokemon::Pokemon &pokemon)
        {
            if (Enums::getGameGroup(static_cast<Enums::GameVersion>(pokemon.originGame())) != Enums::GameVersion::ZA)
            {
                return true;
            }
            const uint8_t scale = pokemon.scale();
            return pokemon.heightScalar() == scale && pokemon.weightScalar() == scale;
        }

        uint32_t getEffortValueTotal(const Pokemon::Pokemon &pokemon)
        {
            return static_cast<uint32_t>(pokemon.evHP()) + pokemon.evATK() + pokemon.evDEF() + pokemon.evSPE() + pokemon.evSPA() + pokemon.evSPD();
        }

        // PKHeX MovePPVerifier.HasLeftBoxAfterAcquisition, reached only by a Gen 3 record. Its
        // encounter is always a Gen 3 one, so the context comparison PKHeX opens with always passes.
        MoveHealState hasLeftBoxAfterAcquisition(const Pokemon::Pokemon &pokemon)
        {
            if (getEffortValueTotal(pokemon) != 0)
            {
                return MOVE_HEAL_STATE_ONLY_HEALED;
            }
            const uint8_t growthRate = Pokemon::getGrowthRate(pokemon.speciesID());
            const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
            if (pokemon.exp() != Pokemon::getExpForLevel(currentLevel, growthRate))
            {
                return MOVE_HEAL_STATE_ONLY_HEALED;
            }
            return pokemon.metLevel() != currentLevel ? MOVE_HEAL_STATE_ONLY_HEALED : MOVE_HEAL_STATE_ANY;
        }

        // PKHeX MovePPVerifier.GetIsStoredHealed: whether a deposit heals PP in this format.
        MoveHealState getIsStoredHealed(const AnalysisContext &context)
        {
            switch (context.formatGeneration)
            {
            case 4:
            case 5:
                return MOVE_HEAL_STATE_ONLY_HEALED;
            case 3:
                return hasLeftBoxAfterAcquisition(context.pokemon);
            default:
                return MOVE_HEAL_STATE_ANY;
            }
        }

        // PKHeX MovePPVerifier.GetPermittedStatePP.
        MoveHealState getPermittedStatePP(const AnalysisContext &context)
        {
            // PKHeX Legal.IsPPUnused: Z-A never spends PP.
            if (context.formatGroup == Enums::GameVersion::ZA)
            {
                if (hasEnteredHomeReachingZA(context.pokemon))
                {
                    return MOVE_HEAL_STATE_ALLOW_HEALED_OR_ZERO;
                }
                return MOVE_HEAL_STATE_ONLY_HEALED;
            }
            if (isStoredSlot(context, StorageSlotType::Party))
            {
                return MOVE_HEAL_STATE_ANY;
            }
            if (context.options.slotType == StorageSlotType::Box)
            {
                return getIsStoredHealed(context);
            }
            return MOVE_HEAL_STATE_ANY;
        }

        void verifyEggPP(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            bool hasPPUps = false;
            bool hasHealedPP = true;
            for (int moveIndex = 0; moveIndex < MOVE_SLOT_COUNT; moveIndex++)
            {
                hasPPUps = hasPPUps || pokemon.movePPUps(moveIndex) != 0;
                hasHealedPP = hasHealedPP && pokemon.movePP(moveIndex) == Names::getMoveBasePP(pokemon.move(moveIndex), context.formatGroup);
            }
            if (hasPPUps)
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggPPUp);
            }
            if (!hasHealedPP)
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggPP);
            }
        }

        // PKHeX VirtualConsolePP.IsPossibleVC1 / IsPossibleVC2: every move existed in the Game Boy game
        // the Pokemon was transferred from.
        bool isPossibleVirtualConsoleMoveset(const Pokemon::Pokemon &pokemon)
        {
            constexpr uint8_t lastVirtualConsoleOneVersion = 38;
            constexpr uint16_t maximumMoveIdGen1 = 165;
            constexpr uint16_t maximumMoveIdGen2 = 251;
            const uint16_t maximumMoveId = pokemon.originGame() <= lastVirtualConsoleOneVersion ? maximumMoveIdGen1 : maximumMoveIdGen2;
            for (int moveIndex = 0; moveIndex < MOVE_SLOT_COUNT; moveIndex++)
            {
                if (pokemon.move(moveIndex) > maximumMoveId)
                {
                    return false;
                }
            }
            return true;
        }

        // PKHeX MovePPVerifier.IsEdgeCaseValues, where PKHeX accepts PP the normal rules would not. True
        // when PKSE cannot tell whether the edge case applies, so the PP is left unjudged.
        bool isEdgeCaseUnknown(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            // A Virtual Console Pokemon still in the box it was transferred to keeps its Game Boy PP.
            // Whether it does is read from PKHeX's per-language Virtual Console PP tables, which PKSE
            // lacks; only a moveset its Game Boy game could not have rules the case out.
            const bool isPokemon7 = context.formatGroup == Enums::GameVersion::SM || context.formatGroup == Enums::GameVersion::USUM;
            if (isPokemon7 && isVirtualConsoleOrigin(pokemon) && isStoredSlot(context, StorageSlotType::Box) && isPossibleVirtualConsoleMoveset(pokemon))
            {
                return true;
            }
            // A Pokewalker Pokemon can arrive with a glitched empty fourth slot holding PP
            // (EncounterStatic4Pokewalker.IsPossibleMovesetEmpty); confirming it needs the course
            // template's moves, which PKSE lacks.
            constexpr uint16_t pokewalkerLocation = 233;
            const bool isPokemon4 = context.formatGeneration == 4;
            const bool isFromPokewalker = Enums::getGameGroup(static_cast<Enums::GameVersion>(pokemon.originGame())) == Enums::GameVersion::HGSS && pokemon.metLocation() == pokewalkerLocation;
            return isPokemon4 && isFromPokewalker && pokemon.move(MOVE_SLOT_COUNT - 1) == 0;
        }
    }


    void verifyMovePP(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (pokemon.isEgg())
        {
            verifyEggPP(context);
            return;
        }

        const bool formatHasPPUps = isPPUpAvailable(context.formatGroup);
        for (int moveIndex = 0; moveIndex < MOVE_SLOT_COUNT; moveIndex++)
        {
            const uint8_t ppUps = pokemon.movePPUps(moveIndex);
            if (ppUps != 0 && (!formatHasPPUps || !isPPUpAvailableForMove(pokemon.move(moveIndex))))
            {
                context.add(Severity::Invalid, CheckIdentifier::CurrentMove, ResultCode::MovePPUpsTooHigh, std::to_string(moveIndex + 1), std::to_string(ppUps));
            }
        }

        if (isEdgeCaseUnknown(context))
        {
            return;
        }

        const MoveHealState allowedStates = getPermittedStatePP(context);
        for (int moveIndex = 0; moveIndex < MOVE_SLOT_COUNT; moveIndex++)
        {
            const uint32_t healedPP = getHealedPP(pokemon.move(moveIndex), pokemon.movePPUps(moveIndex), context.formatGroup, context.formatGeneration);
            const uint32_t currentPP = pokemon.movePP(moveIndex);
            if (currentPP > healedPP)
            {
                context.add(Severity::Invalid, CheckIdentifier::CurrentMove, ResultCode::MovePPTooHigh, std::to_string(moveIndex + 1), std::to_string(currentPP));
                continue;
            }
            if (allowedStates == MOVE_HEAL_STATE_ANY)
            {
                continue;
            }
            if (currentPP == healedPP && (allowedStates == MOVE_HEAL_STATE_ONLY_HEALED || (allowedStates & MOVE_HEAL_STATE_ALLOW_HEALED) != 0))
            {
                continue;
            }
            if (currentPP == 0 && (allowedStates == MOVE_HEAL_STATE_ONLY_ZERO || (allowedStates & MOVE_HEAL_STATE_ALLOW_ZERO) != 0))
            {
                continue;
            }
            if (allowedStates == MOVE_HEAL_STATE_ONLY_ZERO)
            {
                context.add(Severity::Invalid, CheckIdentifier::CurrentMove, ResultCode::MovePPTooHigh, std::to_string(moveIndex + 1), "0");
            }
            else
            {
                context.add(Severity::Invalid, CheckIdentifier::CurrentMove, ResultCode::MovePPExpectHealed, std::to_string(moveIndex + 1), std::to_string(healedPP));
            }
        }
    }
}
