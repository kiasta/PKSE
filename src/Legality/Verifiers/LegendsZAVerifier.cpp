// Port of PKHeX PKHeX.Core/Legality/Verifiers/LegendsZAVerifier.cs. CheckLearnset is not ported: it
// rewrites the move parse's results, and PKSE has no move verifier for it to feed.
#include "Legality/Verifiers/MiscVerifier.h"

#include <cstdint>
#include <span>
#include <string>

#include "Enums/Ball.h"
#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterStatic9a.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftWA9.h"
#include "Legality/Evolutions/EvolutionType.h"
#include "Names/MoveNames.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/PersonalInfo9LZA.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t RAICHU_SPECIES = 26;
        constexpr uint16_t ONIX_SPECIES = 95;
        constexpr uint16_t STEELIX_SPECIES = 208;
        constexpr uint16_t ABSOL_SPECIES = 359;
        constexpr uint16_t ROSERADE_SPECIES = 407;
        constexpr uint16_t ABOMASNOW_SPECIES = 460;
        constexpr uint16_t GALLADE_SPECIES = 475;
        constexpr uint16_t ROTOM_SPECIES = 479;
        constexpr uint16_t SANDILE_SPECIES = 551;
        constexpr uint16_t KROKOROK_SPECIES = 552;
        constexpr uint16_t WHIRLIPEDE_SPECIES = 544;
        constexpr uint16_t SCOLIPEDE_SPECIES = 545;
        constexpr uint16_t ESPURR_SPECIES = 677;
        constexpr uint16_t MEOWSTIC_SPECIES = 678;
        constexpr uint16_t HONEDGE_SPECIES = 679;
        constexpr uint16_t DOUBLADE_SPECIES = 680;
        constexpr uint16_t AEGISLASH_SPECIES = 681;
        constexpr uint16_t MALAMAR_SPECIES = 687;
        constexpr uint16_t HELIOLISK_SPECIES = 695;
        constexpr uint16_t AURORUS_SPECIES = 699;
        constexpr uint16_t HOOPA_SPECIES = 720;

        constexpr uint16_t TELEPORT_MOVE = 100;
        constexpr uint16_t CONFUSE_RAY_MOVE = 109;
        constexpr uint16_t ROCK_BLAST_MOVE = 350;
        constexpr uint16_t SHADOW_SNEAK_MOVE = 425;
        constexpr uint16_t SACRED_SWORD_MOVE = 533;
        constexpr uint16_t SNARL_MOVE = 555;
        constexpr uint16_t DRAINING_KISS_MOVE = 577;
        constexpr uint16_t PHANTOM_FORCE_MOVE = 566;
        constexpr uint16_t ICE_HAMMER_MOVE = 665;
        constexpr uint16_t MOONBLAST_MOVE = 585;
        constexpr uint16_t OCTOLOCK_MOVE = 753;
        constexpr uint16_t SHED_TAIL_MOVE = 880;
        constexpr uint16_t MORTAL_SPIN_MOVE = 866;

        constexpr uint8_t LEGENDS_ZA_VERSION = 52;
        constexpr uint16_t HYPERSPACE_LOCATION_FIRST = 273;
        constexpr uint16_t HYPERSPACE_LOCATION_LAST = 3000;

        int findMoveIndex(const uint16_t *moves, int moveCount, uint16_t moveId)
        {
            for (int moveIndex = 0; moveIndex < moveCount; ++moveIndex)
            {
                if (moves[moveIndex] == moveId)
                {
                    return moveIndex;
                }
            }
            return -1;
        }

        /// The encounter's species and form, falling back to the Pokemon's own when the template is unknown.
        void getEncounterSpeciesForm(const AnalysisContext &context, uint16_t &speciesId, uint8_t &formId)
        {
            speciesId = context.pokemon.speciesID();
            formId = context.pokemon.form();
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                speciesId = encounterMatch->speciesId();
                formId = encounterMatch->formId();
            }
        }

        /// PKHeX LegendsZAVerifier.WasPossiblyObtainedBeforeDLC.
        bool wasPossiblyObtainedBeforeDLC(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.originGame() != LEGENDS_ZA_VERSION)
            {
                return false;
            }
            if (pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari) || pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Beast))
            {
                return false;
            }
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            uint16_t location = pokemon.metLocation();
            if (encounterMatch != nullptr)
            {
                location = encounterMatch->location();
            }
            if (location >= HYPERSPACE_LOCATION_FIRST && location <= HYPERSPACE_LOCATION_LAST)
            {
                return false;
            }
            uint16_t encounterSpeciesId = 0;
            uint8_t encounterFormId = 0;
            getEncounterSpeciesForm(context, encounterSpeciesId, encounterFormId);
            if (!Pokemon::isLumioseNative9LZA(encounterSpeciesId, encounterFormId))
            {
                return false;
            }
            const bool isStatic = encounterMatch != nullptr ? Encounters::encounterAs<Encounters::EncounterStatic9a>(*encounterMatch) != nullptr : context.encounter.type == EncounterType::Static;
            const bool isDLCStatic = isStatic && (encounterSpeciesId == SANDILE_SPECIES || encounterSpeciesId == KROKOROK_SPECIES);
            return !isDLCStatic;
        }

        /// PKHeX LegendsZAVerifier.IsPermittedUnsetPlusMove: moves the DLC added, which an older Pokemon never got flagged.
        bool isPermittedUnsetPlusMove(uint16_t speciesId, uint16_t moveId)
        {
            switch (speciesId)
            {
                case PIKACHU_SPECIES:
                case RAICHU_SPECIES:
                    return moveId == DRAINING_KISS_MOVE;
                case ONIX_SPECIES:
                case STEELIX_SPECIES:
                    return moveId == ROCK_BLAST_MOVE;
                case ABSOL_SPECIES:
                    return moveId == SNARL_MOVE || moveId == PHANTOM_FORCE_MOVE || moveId == CONFUSE_RAY_MOVE || moveId == SHADOW_SNEAK_MOVE;
                case ROSERADE_SPECIES:
                case WHIRLIPEDE_SPECIES:
                case SCOLIPEDE_SPECIES:
                    return moveId == MORTAL_SPIN_MOVE;
                case ABOMASNOW_SPECIES:
                case AURORUS_SPECIES:
                    return moveId == ICE_HAMMER_MOVE;
                case GALLADE_SPECIES:
                case HONEDGE_SPECIES:
                case DOUBLADE_SPECIES:
                case AEGISLASH_SPECIES:
                    return moveId == SACRED_SWORD_MOVE;
                case MEOWSTIC_SPECIES:
                    return moveId == MOONBLAST_MOVE || moveId == TELEPORT_MOVE;
                case ESPURR_SPECIES:
                    return moveId == TELEPORT_MOVE;
                case MALAMAR_SPECIES:
                    return moveId == OCTOLOCK_MOVE;
                case HELIOLISK_SPECIES:
                    return moveId == SHED_TAIL_MOVE;
                default:
                    return false;
            }
        }

        /// PKHeX LegendsZAVerifier.IsTradeEvoSkip: the head was reached by a trade evolution, and no pre-evolution
        /// had to pass the level of the move's Plus flag before evolving.
        bool isTradeEvolutionSkip(std::span<const Evolutions::EvolutionCriteria> stages, uint16_t moveId)
        {
            if (stages.size() <= 1 || !Evolutions::isTradeEvolution(stages[0].method))
            {
                return false;
            }
            const uint8_t headLevelMinimum = stages[0].levelMin;
            for (size_t stageIndex = 1; stageIndex < stages.size(); ++stageIndex)
            {
                for (const Pokemon::LevelUpMove &plusMove : Pokemon::getPlusLevels9LZA(stages[stageIndex].speciesId, stages[stageIndex].formId))
                {
                    if (plusMove.moveId == moveId)
                    {
                        if (plusMove.level <= headLevelMinimum)
                        {
                            return false;
                        }
                        break;
                    }
                }
            }
            return true;
        }

        /// PKHeX LegendsZAVerifier.CheckPlusMoveFlags: every Plus level reached must have set its flag.
        void verifyPlusLevelFlags(const AnalysisContext &context, const Pokemon::Pokemon9LZA &pokemon, uint8_t formId, uint8_t currentLevel, std::span<const Evolutions::EvolutionCriteria> stages)
        {
            for (const Pokemon::LevelUpMove &plusMove : Pokemon::getPlusLevels9LZA(pokemon.speciesID(), formId))
            {
                if (plusMove.level > currentLevel)
                {
                    break;
                }
                const int plusIndex = findMoveIndex(Pokemon::PLUS_MOVES_9LZA, Pokemon::PLUS_MOVE_COUNT_9LZA, plusMove.moveId);
                if (plusIndex < 0 || pokemon.isPlusFlagSet(plusIndex))
                {
                    continue;
                }
                if (isTradeEvolutionSkip(stages, plusMove.moveId))
                {
                    continue;
                }
                if (wasPossiblyObtainedBeforeDLC(context) && isPermittedUnsetPlusMove(pokemon.speciesID(), plusMove.moveId))
                {
                    continue;
                }
                context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::PlusMoveSufficientLevelMissing, Names::getMoveName(plusMove.moveId), std::to_string(plusMove.level));
            }
        }

        /**
         * PKHeX LegendsZAVerifier.CanLearnMovePlus: a TM or a level-up move can be Plus'd by a Seed of Mastery,
         * a level-up move only once the stage's highest level reaches it.
         */
        bool canLearnMovePlus(uint16_t speciesId, uint8_t formId, int machineIndex, uint16_t moveId, uint8_t levelMaximum)
        {
            if (machineIndex >= 0 && Pokemon::isTMPermitted9LZA(speciesId, formId, machineIndex))
            {
                return true;
            }
            for (const Pokemon::LevelUpMove &levelUpMove : Pokemon::getLevelUpMoves9LZA(speciesId, formId))
            {
                if (levelUpMove.moveId == moveId)
                {
                    return levelUpMove.level <= levelMaximum;
                }
            }
            return false;
        }

        /// PKHeX FormChangeUtil.IsFormChangeDifferentMoves for generation 9.
        bool isFormChangeDifferentMoves(uint16_t speciesId)
        {
            switch (speciesId)
            {
                case 386: // Deoxys
                case 487: // Giratina
                case 492: // Shaymin
                case ROTOM_SPECIES:
                case HOOPA_SPECIES:
                case 641: // Tornadus
                case 642: // Thundurus
                case 645: // Landorus
                case 892: // Urshifu
                case 905: // Enamorus
                case 646: // Kyurem
                case 800: // Necrozma
                case 898: // Calyrex
                    return true;
                default:
                    return false;
            }
        }

        /// PKHeX LegendsZAVerifier.CheckFlagsPlus.
        void verifyPlusFlags(const AnalysisContext &context, const Pokemon::Pokemon9LZA &pokemon)
        {
            if (pokemon.hasImpossiblePlusFlag())
            {
                context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::PlusMoveCountInvalid);
            }

            const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::ZA);
            const int stageCount = static_cast<int>(stages.size());
            const uint16_t speciesId = pokemon.speciesID();
            const uint8_t formId = pokemon.form();
            const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
            verifyPlusLevelFlags(context, pokemon, formId, currentLevel, stages);
            if (formId != 0 && (speciesId == ROTOM_SPECIES || speciesId == HOOPA_SPECIES))
            {
                verifyPlusLevelFlags(context, pokemon, 0, currentLevel, stages);
            }

            uint16_t invalidMoveId = 0;
            bool hasMultipleInvalid = false;
            for (int plusIndex = 0; plusIndex < Pokemon::PLUS_MOVE_COUNT_9LZA && !hasMultipleInvalid; ++plusIndex)
            {
                if (!pokemon.isPlusFlagSet(plusIndex))
                {
                    continue;
                }
                const uint16_t moveId = Pokemon::PLUS_MOVES_9LZA[plusIndex];
                const int machineIndex = findMoveIndex(Pokemon::TM_MOVES_9LZA, Pokemon::TM_COUNT_9LZA, moveId);
                bool isLearnable = false;
                for (int stageIndex = 0; stageIndex < stageCount && !isLearnable; ++stageIndex)
                {
                    isLearnable = canLearnMovePlus(stages[stageIndex].speciesId, stages[stageIndex].formId, machineIndex, moveId, stages[stageIndex].levelMax);
                }
                if (!isLearnable && stageCount != 0 && isFormChangeDifferentMoves(speciesId))
                {
                    // Rotom and Hoopa reach another form's own move by changing form. PKHeX walks 0..FormCount inclusive.
                    const uint8_t formCount = Pokemon::getPersonalInfo9LZA(speciesId, formId).formCount;
                    for (uint8_t otherFormId = 0; otherFormId <= formCount && !isLearnable; ++otherFormId)
                    {
                        if (otherFormId != formId)
                        {
                            isLearnable = canLearnMovePlus(stages[0].speciesId, otherFormId, machineIndex, moveId, stages[0].levelMax);
                        }
                    }
                }
                if (isLearnable)
                {
                    continue;
                }
                if (invalidMoveId != 0)
                {
                    hasMultipleInvalid = true;
                }
                invalidMoveId = moveId;
            }
            if (hasMultipleInvalid)
            {
                context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::PlusMoveMultipleInvalid);
            }
            else if (invalidMoveId != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::PlusMoveInvalid, Names::getMoveName(invalidMoveId));
            }
        }

        /// PKHeX LegendsZAVerifier.CheckFlagsTM: a wild Alpha comes with its Alpha move already Plus'd.
        void verifyAlphaMoveFlag(const AnalysisContext &context, const Pokemon::Pokemon9LZA &pokemon)
        {
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                // PKHeX: only a Legends: Z-A Alpha that is not a WA9 card.
                if (encounterMatch->contextGroup() != Enums::GameVersion::ZA || Encounters::encounterAs<Encounters::MysteryGiftWA9>(*encounterMatch) != nullptr)
                {
                    return;
                }
            }
            else
            {
                const EncounterInfo &encounter = context.encounter;
                if (encounter.originGroup != Enums::GameVersion::ZA || encounter.type == EncounterType::Event)
                {
                    return;
                }
            }
            if (!isEncounterAlpha(context).value_or(false))
            {
                return;
            }
            uint16_t encounterSpeciesId = 0;
            uint8_t encounterFormId = 0;
            getEncounterSpeciesForm(context, encounterSpeciesId, encounterFormId);
            const uint16_t alphaMoveId = Pokemon::getAlphaMove9LZA(encounterSpeciesId, encounterFormId);
            const int plusIndex = findMoveIndex(Pokemon::PLUS_MOVES_9LZA, Pokemon::PLUS_MOVE_COUNT_9LZA, alphaMoveId);
            if (plusIndex >= 0 && !pokemon.isPlusFlagSet(plusIndex))
            {
                context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::PlusMoveAlphaMissing, Names::getMoveName(alphaMoveId));
            }
        }
    }

    void verifyLegendsZA(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon9LZA &>(context.pokemon);
        verifyAlphaMoveFlag(context, pokemon);
        verifyPlusFlags(context, pokemon);
        if (pokemon.levelBoost() != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::LevelBoostNotZero);
        }
    }
}
