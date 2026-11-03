#include "Legality/Encounters/EncounterMoveSeam.h"

#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/LearnSource/Learnset.h"
#include "Legality/LearnSource/LearnEncounter.h"
#include "Legality/LearnSource/LearnGroup.h"
#include "Legality/LearnSource/LearnEntity.h"
#include "Legality/LearnSource/LearnVerifier.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// The IEncounterTemplate facts PKHeX's move verifiers read, from the matched template.
            LearnSource::LearnEncounter getLearnEncounter(const Encounterable &encounter, const Pokemon::Pokemon &pokemon)
            {
                LearnSource::LearnEncounter learnEncounter;
                learnEncounter.speciesId = encounter.speciesId();
                learnEncounter.formId = encounter.formId();
                learnEncounter.levelMin = encounter.levelMin();
                learnEncounter.levelMax = encounter.levelMax();
                learnEncounter.version = encounter.version();
                learnEncounter.generation = encounter.generation();
                learnEncounter.contextGroup = encounter.contextGroup();
                const EncounterTemplateType templateType = encounter.templateType();
                learnEncounter.isInvalid = templateType == EncounterTemplateType::Invalid;
                // PKHeX PK1.Version / PK2.Version read RBY / GSC, which EncounterInvalid(pk) copies; a
                // Game Boy record stores no version byte of its own.
                if (learnEncounter.isInvalid && learnEncounter.version == Enums::GameVersion::Any && pokemon.getGameGroup() == Enums::GameVersion::RBY)
                {
                    learnEncounter.version = Enums::GameVersion::RBY;
                }
                else if (learnEncounter.isInvalid && learnEncounter.version == Enums::GameVersion::Any && pokemon.getGameGroup() == Enums::GameVersion::GSC)
                {
                    learnEncounter.version = Enums::GameVersion::GSC;
                }
                if (const EggTemplate *egg = encounter.asEgg())
                {
                    learnEncounter.isEggTemplate = true;
                    learnEncounter.canHaveVoltTackle = egg->canHaveVoltTackle();
                }
                if (const MovesetTemplate *moveset = encounter.asMoveset())
                {
                    learnEncounter.moves = moveset->moves().moveIds;
                }
                if (const RelearnTemplate *relearn = encounter.asRelearn())
                {
                    learnEncounter.relearnMoves = relearn->relearnMoves().moveIds;
                }
                if (const SingleMoveBonusTemplate *bonus = encounter.asSingleMoveBonus())
                {
                    learnEncounter.isMoveBonusPossible = bonus->isMoveBonusPossible();
                    learnEncounter.isMoveBonusRequired = bonus->isMoveBonusRequired();
                    learnEncounter.moveBonusPossible = bonus->getMoveBonusPossible();
                }
                learnEncounter.isDexNavSlot = templateType == EncounterTemplateType::Slot6AO;
                // PKHeX `enc is EncounterSlot8GO { OriginFormat: PK7 or PB7 } g` and g.GetInitialMoves(pk.MetLevel).
                if (const EncounterSlot8GO *pogo = encounterAs<EncounterSlot8GO>(encounter))
                {
                    const PogoImportFormat originFormat = pogo->originFormat();
                    if (originFormat == PogoImportFormat::PK7 || originFormat == PogoImportFormat::PB7)
                    {
                        const bool isUltraSunUltraMoon = originFormat == PogoImportFormat::PK7;
                        learnEncounter.goImportFormat = isUltraSunUltraMoon ? LearnSource::GoImportFormat::PK7 : LearnSource::GoImportFormat::PB7;
                        const LearnSource::LearnEnvironment environment = isUltraSunUltraMoon ? LearnSource::LearnEnvironment::USUM : LearnSource::LearnEnvironment::GG;
                        LearnSource::getLearnset(environment, pogo->speciesId(), pogo->formId()).setEncounterMoves(pokemon.metLevel(), learnEncounter.goInitialMoves);
                    }
                }
                // A down-levelled raid's original range (EncounterStatic8N.IsDownLeveled) is not
                // reachable from the template interface yet, so its Unobtainable verdicts mean "unknown".
                learnEncounter.hasUnknownMoves = templateType == EncounterTemplateType::Static8N;
                return learnEncounter;
            }

            constexpr uint16_t MR_MIME_SPECIES = 122;
            constexpr uint16_t SUDOWOODO_SPECIES = 185;
            constexpr uint16_t AMBIPOM_SPECIES = 424;
            constexpr uint16_t LICKILICKY_SPECIES = 463;
            constexpr uint16_t TANGROWTH_SPECIES = 465;
            constexpr uint16_t YANMEGA_SPECIES = 469;
            constexpr uint16_t MAMOSWINE_SPECIES = 473;
            constexpr uint16_t SYLVEON_SPECIES = 700;
            constexpr uint16_t TSAREENA_SPECIES = 763;
            constexpr uint16_t NAGANADEL_SPECIES = 804;
            constexpr uint16_t GRAPPLOCT_SPECIES = 853;
            constexpr uint16_t MR_RIME_SPECIES = 866;
            constexpr uint16_t WYRDEER_SPECIES = 899;
            constexpr uint16_t OVERQWIL_SPECIES = 904;
            constexpr uint16_t ANNIHILAPE_SPECIES = 979;
            constexpr uint16_t FARIGIRAF_SPECIES = 981;
            constexpr uint16_t DUDUNSPARCE_SPECIES = 982;
            constexpr uint16_t HYDRAPPLE_SPECIES = 1019;

            constexpr uint16_t STOMP_MOVE = 23;
            constexpr uint16_t MIMIC_MOVE = 102;
            constexpr uint16_t CHARM_MOVE = 204;
            constexpr uint16_t ROLLOUT_MOVE = 205;
            constexpr uint16_t ANCIENT_POWER_MOVE = 246;
            constexpr uint16_t TAUNT_MOVE = 269;
            constexpr uint16_t DRAGON_PULSE_MOVE = 406;
            constexpr uint16_t DOUBLE_HIT_MOVE = 458;
            constexpr uint16_t DISARMING_VOICE_MOVE = 574;
            constexpr uint16_t BABY_DOLL_EYES_MOVE = 608;
            constexpr uint16_t PSYSHIELD_BASH_MOVE = 828;
            constexpr uint16_t BARB_BARRAGE_MOVE = 839;
            constexpr uint16_t HYPER_DRILL_MOVE = 887;
            constexpr uint16_t TWIN_BEAM_MOVE = 888;
            constexpr uint16_t RAGE_FIST_MOVE = 889;
            constexpr uint16_t DRAGON_CHEER_MOVE = 913;

            constexpr uint16_t NO_EVOLUTION_MOVE = 0;
            /// Sylveon's pre-evolution needs any one of EEVEE_FAIRY_MOVES.
            constexpr uint16_t EEVEE_FAIRY_EVOLUTION = 0xFFFF;
            constexpr uint16_t EEVEE_FAIRY_MOVES[] = {CHARM_MOVE, BABY_DOLL_EYES_MOVE, DISARMING_VOICE_MOVE};

            /// PKHeX EvolutionRestrictions.GetSpeciesEvolutionMove: the move a species' pre-evolution
            /// must know to evolve into it.
            uint16_t getSpeciesEvolutionMove(uint16_t speciesId) noexcept
            {
                switch (speciesId)
                {
                case SYLVEON_SPECIES:
                    return EEVEE_FAIRY_EVOLUTION;
                case MR_MIME_SPECIES:
                case SUDOWOODO_SPECIES:
                    return MIMIC_MOVE;
                case AMBIPOM_SPECIES:
                    return DOUBLE_HIT_MOVE;
                case LICKILICKY_SPECIES:
                    return ROLLOUT_MOVE;
                case TANGROWTH_SPECIES:
                case YANMEGA_SPECIES:
                case MAMOSWINE_SPECIES:
                    return ANCIENT_POWER_MOVE;
                case TSAREENA_SPECIES:
                    return STOMP_MOVE;
                case NAGANADEL_SPECIES:
                    return DRAGON_PULSE_MOVE;
                case GRAPPLOCT_SPECIES:
                    return TAUNT_MOVE;
                case WYRDEER_SPECIES:
                    return PSYSHIELD_BASH_MOVE;
                case OVERQWIL_SPECIES:
                    return BARB_BARRAGE_MOVE;
                case ANNIHILAPE_SPECIES:
                    return RAGE_FIST_MOVE;
                case FARIGIRAF_SPECIES:
                    return TWIN_BEAM_MOVE;
                case DUDUNSPARCE_SPECIES:
                    return HYPER_DRILL_MOVE;
                case HYDRAPPLE_SPECIES:
                    return DRAGON_CHEER_MOVE;
                default:
                    return NO_EVOLUTION_MOVE;
                }
            }

            /// PKHeX IsMoveSlotAvailable: a forgotten move needs a slot that is not an egg move.
            bool isMoveSlotAvailable(std::span<const LearnSource::MoveResult> moves) noexcept
            {
                for (const LearnSource::MoveResult &move : moves)
                {
                    if (!LearnSource::isEggSourceMethod(move.info.method))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX IsMoveInRelearn: the move is a relearn move, so it was known at any time.
            bool isMoveInRelearn(const LegalityInfo &info, const LearnSource::LearnEntity &entity, uint16_t moveId) noexcept
            {
                if (entity.isOriginalMovesetDeleted())
                {
                    // PKHeX WasMoveInRelearn.
                    for (int slotIndex = 0; slotIndex < static_cast<int>(entity.moves.size()); ++slotIndex)
                    {
                        if (entity.moves[slotIndex] == moveId)
                        {
                            const LearnSource::LearnMethod method = info.moves[slotIndex].info.method;
                            return LearnSource::isEggSourceMethod(method) || method == LearnSource::LearnMethod::Relearn || method == LearnSource::LearnMethod::Encounter;
                        }
                    }
                    return false;
                }
                if (entity.relearnMoves[0] == 0)
                {
                    return false;
                }
                for (const uint16_t relearnMove : entity.relearnMoves)
                {
                    if (relearnMove == moveId)
                    {
                        return true;
                    }
                }
                return false;
            }

            LearnSource::LearnEntity getLearnEntity(const LegalityInfo &info, const Pokemon::Pokemon &pokemon)
            {
                return LearnSource::getLearnEntity(pokemon, info.allowEraCartGB, info.allowEraCartGBA);
            }

            /// PKHeX MoveResult.AllValid; for a template whose moves PKSE cannot see, an unparsed result
            /// is unknown rather than invalid.
            bool areAllValidOrUnknown(std::span<const LearnSource::MoveResult> results, bool hasUnknownMoves) noexcept
            {
                for (const LearnSource::MoveResult &result : results)
                {
                    if (!result.isValid() && (!hasUnknownMoves || result.info.method != LearnSource::LearnMethod::None))
                    {
                        return false;
                    }
                }
                return true;
            }
        }

        bool verifyRelearnMoves(LegalityInfo &info, const Pokemon::Pokemon &pokemon)
        {
            const LearnSource::LearnEncounter encounter = getLearnEncounter(*info.encounterOriginal(), pokemon);
            const LearnSource::LearnEntity entity = getLearnEntity(info, pokemon);
            LearnSource::verifyRelearnMoves(info.relearn, encounter, entity);
            if (encounter.hasUnknownMoves)
            {
                LearnSource::forgetUnobtainableResults(info.relearn);
            }
            info.hasUnknownRelearnMoves = encounter.hasUnknownMoves;
            return areAllValidOrUnknown(info.relearn, encounter.hasUnknownMoves);
        }

        void fillRelearnValid(LegalityInfo &info)
        {
            info.hasUnknownRelearnMoves = false;
            info.relearn.fill(LearnSource::MoveResult::relearn());
        }

        bool verifyCurrentMoves(LegalityInfo &info, const Pokemon::Pokemon &pokemon)
        {
            const LearnSource::LearnEncounter encounter = getLearnEncounter(*info.encounterMatch(), pokemon);
            const LearnSource::LearnEntity entity = getLearnEntity(info, pokemon);
            LearnSource::verifyCurrentMoves(info.moves, entity, encounter, info.evolutionChainsAllGens());
            if (encounter.hasUnknownMoves)
            {
                LearnSource::forgetUnobtainableResults(info.moves);
            }
            info.hasUnknownMoves = encounter.hasUnknownMoves;
            return areAllValidOrUnknown(info.moves, encounter.hasUnknownMoves);
        }

        bool hasGen2Move(const LegalityInfo &info)
        {
            return LearnSource::isAnyFromContext(Enums::GameVersion::GSC, info.moves);
        }

        // PKHeX PKHeX.Core/Legality/Restrictions/EvolutionRestrictions.cs IsValidEvolutionWithMove
        bool isValidEvolutionWithMove(const LegalityInfo &info, const Pokemon::Pokemon &pokemon)
        {
            const LearnSource::LearnEntity entity = getLearnEntity(info, pokemon);
            // Evolving while knowing a move arrived in Gen 4.
            if (entity.formatGeneration < 4)
            {
                return true;
            }
            const Encounterable &encounter = *info.encounterOriginal();
            uint16_t speciesId = pokemon.speciesID();
            if (encounter.speciesId() == speciesId)
            {
                return true;
            }
            // Every move evolution ends its family except Mr. Mime's, which goes on to Mr. Rime.
            if (speciesId == MR_RIME_SPECIES)
            {
                speciesId = MR_MIME_SPECIES;
                if (encounter.speciesId() == speciesId)
                {
                    return true;
                }
            }
            const uint16_t evolutionMove = getSpeciesEvolutionMove(speciesId);
            if (evolutionMove == NO_EVOLUTION_MOVE)
            {
                return true;
            }
            if (!isMoveSlotAvailable(info.moves))
            {
                return false;
            }
            const std::span<const uint16_t> requiredMoves = evolutionMove == EEVEE_FAIRY_EVOLUTION ? std::span<const uint16_t>(EEVEE_FAIRY_MOVES) : std::span<const uint16_t>(&evolutionMove, 1);
            if (entity.formatGeneration >= 6)
            {
                for (const uint16_t moveId : requiredMoves)
                {
                    if (isMoveInRelearn(info, entity, moveId))
                    {
                        return true;
                    }
                }
            }
            const LearnSource::LearnEncounter learnEncounter = getLearnEncounter(encounter, pokemon);
            const Evolutions::EvolutionHistory pruned = info.evolutionChainsAllGens().pruneKeepPreEvolutions(speciesId);
            const LearnSource::LearnGroup head = LearnSource::getCurrentLearnGroup(entity.contextGroup);
            for (const uint16_t moveId : requiredMoves)
            {
                if (LearnSource::canKnowMove(learnEncounter, moveId, pruned, entity, head, LearnSource::LearnOption::AtAnyTimeChain))
                {
                    return true;
                }
            }
            return false;
        }
    }
}
