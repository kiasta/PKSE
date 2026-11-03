// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ability/AbilityVerifier.cs and AbilityChangeRules.cs:
// ability and ability number.
//
// PKHeX reads the matched encounter's AbilityPermission; the encounter engine's template answers it.
#include "Legality/Verifiers/Verifiers.h"

#include <optional>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Verifiers/BreedingRules.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/PersonalTableGroup.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t GRANBULL_SPECIES = 210;
        constexpr uint16_t VIBRAVA_SPECIES = 329;
        constexpr uint16_t FLYGON_SPECIES = 330;
        constexpr uint16_t MAX_SPECIES_GENERATION_3 = 386;
        constexpr uint16_t GIRATINA_SPECIES = 487;
        constexpr uint16_t BASCULIN_SPECIES = 550;
        constexpr uint16_t TORNADUS_SPECIES = 641;
        constexpr uint16_t THUNDURUS_SPECIES = 642;
        constexpr uint16_t LANDORUS_SPECIES = 645;
        constexpr uint16_t AEGISLASH_SPECIES = 681;
        constexpr uint16_t ENAMORUS_SPECIES = 905;
        // The Gen 1 and Gen 2 families that Poke Transporter gives their first ability instead of
        // their Hidden one (PKHeX TransporterLogic.IsHiddenDisallowedVC1/VC2).
        constexpr uint16_t GASTLY_SPECIES = 92;
        constexpr uint16_t HAUNTER_SPECIES = 93;
        constexpr uint16_t GENGAR_SPECIES = 94;
        constexpr uint16_t KOFFING_SPECIES = 109;
        constexpr uint16_t WEEZING_SPECIES = 110;
        constexpr uint16_t MEW_SPECIES = 151;
        constexpr uint16_t MISDREAVUS_SPECIES = 200;
        constexpr uint16_t UNOWN_SPECIES = 201;
        constexpr uint16_t CELEBI_SPECIES = 251;

        constexpr uint16_t ROCK_HEAD_ABILITY = 69;
        constexpr uint16_t ADAPTABILITY_ABILITY = 91;
        constexpr uint16_t RECKLESS_ABILITY = 120;

        constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        constexpr uint8_t VERSION_POKEMON_GO = 34;
        constexpr uint8_t VERSION_FIRST_GENERATION_TWO_VIRTUAL_CONSOLE = 39;

        constexpr int ABILITY_NUMBER_FIRST = 1;
        constexpr int ABILITY_NUMBER_SECOND = 2;
        constexpr int ABILITY_NUMBER_HIDDEN = 4;

        /// PKHeX AbilityPermission, with its values.
        enum class AbilityPermission : int8_t
        {
            Any12H = -1,
            Any12 = 0,
            OnlyFirst = 1,
            OnlySecond = 2,
            OnlyHidden = 4,
        };

        /// PKHeX AbilityState.
        enum class AbilityState : uint8_t
        {
            CanMismatch,
            MustMatch,
        };

        bool canBeHidden(AbilityPermission permission)
        {
            return permission == AbilityPermission::Any12H || permission == AbilityPermission::OnlyHidden;
        }

        /// PKHeX AbilityPermission.GetSingleValue: the slot index of a fixed ability.
        int getSingleValue(AbilityPermission permission)
        {
            switch (permission)
            {
            case AbilityPermission::OnlySecond:
                return 1;
            case AbilityPermission::OnlyHidden:
                return 2;
            default:
                return 0;
            }
        }

        /// The abilities a personal entry lists (PKHeX IPersonalAbility12/12H).
        struct AbilitySet
        {
            uint16_t abilities[3] = {};
            int abilityCount = 2;

            int indexOf(uint16_t abilityId) const
            {
                for (int slotIndex = 0; slotIndex < abilityCount; slotIndex++)
                {
                    if (abilities[slotIndex] == abilityId)
                    {
                        return slotIndex;
                    }
                }
                return -1;
            }

            uint16_t getAbilityInSlot(int slotIndex) const { return (slotIndex >= 0 && slotIndex < abilityCount) ? abilities[slotIndex] : 0; }
            bool isAbility12Same() const { return abilities[0] == abilities[1]; }
            bool isAbilityHiddenUnique() const { return abilities[0] != abilities[2]; }
        };

        AbilitySet getAbilitySet(Enums::GameVersion group, uint16_t speciesId, uint8_t formId)
        {
            const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(group, speciesId, formId);
            AbilitySet abilitySet;
            abilitySet.abilities[0] = record.ability1;
            abilitySet.abilities[1] = record.ability2;
            abilitySet.abilities[2] = record.abilityHidden;
            abilitySet.abilityCount = Pokemon::personalHasHiddenAbility(group) ? 3 : 2;
            return abilitySet;
        }

        /// PKHeX PKM.AbilityNumber as each entity class derives it: Gen 4 and Gen 5 from the PID
        /// (Gen 5 below a Hidden Ability flag), Gen 3 from its ability bit, later formats stored.
        int getAbilityNumber(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const int formatGeneration = context.formatGeneration;
            if (formatGeneration == 4 || formatGeneration == 5)
            {
                if (formatGeneration == 5 && pokemon.abilityNumber() == ABILITY_NUMBER_HIDDEN)
                {
                    return ABILITY_NUMBER_HIDDEN;
                }
                if (pokemon.originGame() == VERSION_COLOSSEUM_XD)
                {
                    const AbilitySet abilitySet = getAbilitySet(getPersonalTableGroup(context.formatGroup), pokemon.speciesID(), pokemon.form());
                    const int slotIndex = abilitySet.indexOf(pokemon.ability());
                    return 1 << (slotIndex < 0 ? 0 : slotIndex);
                }
                const uint32_t PID = pokemon.pid();
                const uint32_t abilityBit = getOriginGeneration(context) == 5 ? (PID >> 16) & 1 : PID & 1;
                return 1 << abilityBit;
            }
            return pokemon.abilityNumber();
        }

        bool isValidAbilityBits(int abilityNumber)
        {
            return abilityNumber == ABILITY_NUMBER_FIRST || abilityNumber == ABILITY_NUMBER_SECOND || abilityNumber == ABILITY_NUMBER_HIDDEN;
        }

        bool isCapsulePossible(const Evolutions::EvolutionHistory &history, Enums::GameVersion evolutionContext, Enums::GameVersion group)
        {
            for (const Evolutions::EvolutionCriteria &stage : history.getChain(evolutionContext))
            {
                if (!getAbilitySet(group, stage.speciesId, stage.formId).isAbility12Same())
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX AbilityChangeRules.IsFormChangeDifferentHidden: a form whose base form has a
        /// distinct Hidden Ability it can change back into.
        bool isFormChangeDifferentHidden(const Evolutions::EvolutionCriteria &stage)
        {
            if (stage.formId == 0)
            {
                return false;
            }
            switch (stage.speciesId)
            {
            case GIRATINA_SPECIES:
            case TORNADUS_SPECIES:
            case THUNDURUS_SPECIES:
            case LANDORUS_SPECIES:
            case ENAMORUS_SPECIES:
                return true;
            default:
                return false;
            }
        }

        bool isPatchPossible(const Evolutions::EvolutionHistory &history, Enums::GameVersion evolutionContext, Enums::GameVersion group)
        {
            const std::span<const Evolutions::EvolutionCriteria> stages = history.getChain(evolutionContext);
            for (const Evolutions::EvolutionCriteria &stage : stages)
            {
                if (getAbilitySet(group, stage.speciesId, stage.formId).isAbilityHiddenUnique())
                {
                    return true;
                }
            }
            return !stages.empty() && isFormChangeDifferentHidden(stages[0]);
        }

        /// PKHeX AbilityChangeRules.IsAbilityCapsulePossible.
        bool isAbilityCapsulePossible(const Evolutions::EvolutionHistory &history)
        {
            return isCapsulePossible(history, Enums::GameVersion::SV, Enums::GameVersion::SV) || isCapsulePossible(history, Enums::GameVersion::SWSH, Enums::GameVersion::SWSH) || isCapsulePossible(history, Enums::GameVersion::BDSP, Enums::GameVersion::BDSP) || isCapsulePossible(history, Enums::GameVersion::SM, Enums::GameVersion::USUM) || isCapsulePossible(history, Enums::GameVersion::XY, Enums::GameVersion::ORAS);
        }

        /// PKHeX AbilityChangeRules.IsAbilityPatchPossible.
        bool isAbilityPatchPossible(const Evolutions::EvolutionHistory &history)
        {
            return isPatchPossible(history, Enums::GameVersion::SWSH, Enums::GameVersion::SWSH) || isPatchPossible(history, Enums::GameVersion::BDSP, Enums::GameVersion::BDSP) || isPatchPossible(history, Enums::GameVersion::SV, Enums::GameVersion::SV);
        }

        /// PKHeX AbilityChangeRules.IsAbilityPatchRevertPossible: only Scarlet/Violet revert a patch.
        bool isAbilityPatchRevertPossible(const Evolutions::EvolutionHistory &history, int abilityNumber)
        {
            const std::span<const Evolutions::EvolutionCriteria> stages = history.getChain(Enums::GameVersion::SV);
            if (stages.empty())
            {
                return false;
            }
            bool canRevert = false;
            for (int stageIndex = static_cast<int>(stages.size()) - 1; stageIndex >= 0; stageIndex--)
            {
                const AbilitySet abilitySet = getAbilitySet(Enums::GameVersion::SV, stages[stageIndex].speciesId, stages[stageIndex].formId);
                if (canRevert && !abilitySet.isAbility12Same())
                {
                    return true;
                }
                if (!abilitySet.isAbilityHiddenUnique())
                {
                    continue;
                }
                if (abilityNumber == ABILITY_NUMBER_FIRST || !abilitySet.isAbility12Same())
                {
                    return true;
                }
                canRevert = true;
            }
            return abilityNumber == ABILITY_NUMBER_FIRST && isFormChangeDifferentHidden(stages[0]);
        }

        struct AbilityAnalysis
        {
            const AnalysisContext &context;
            AbilitySet abilities;
            int abilityNumber;
            int originGeneration;
            uint16_t encounterSpecies;
            Evolutions::EvolutionHistory history;

            void addInvalid(ResultCode code, CheckIdentifier identifier = CheckIdentifier::Ability) const { context.add(Severity::Invalid, identifier, code); }

            /// PKHeX GetPIDAbilityMatch.
            void verifyPIDAbilityMatch() const
            {
                if (abilities.getAbilityInSlot(abilityNumber >> 1) != context.pokemon.ability())
                {
                    addInvalid(ResultCode::AbilityMismatchPID);
                }
            }

            /// PKHeX CheckMatch: the final word once nothing else has decided.
            void checkMatch(int generation, AbilityState state) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if ((generation == 3 || generation == 4) && abilityNumber == ABILITY_NUMBER_HIDDEN)
                {
                    addInvalid(ResultCode::AbilityHiddenUnavailable);
                    return;
                }
                if (state != AbilityState::MustMatch)
                {
                    return;
                }
                if (context.formatGeneration == 3)
                {
                    const bool abilityBit = abilityNumber == ABILITY_NUMBER_SECOND;
                    const uint16_t speciesId = pokemon.speciesID();
                    const bool isColosseumXD = pokemon.originGame() == VERSION_COLOSSEUM_XD;
                    // Granbull, Vibrava and Flygon list one ability twice, so either bit is fine.
                    if (abilities.isAbility12Same() && speciesId != GRANBULL_SPECIES && speciesId != VIBRAVA_SPECIES && speciesId != FLYGON_SPECIES)
                    {
                        // A Colosseum/XD shadow sets a random ability bit, so it is left alone.
                        if (abilityBit && !isColosseumXD)
                        {
                            addInvalid(ResultCode::AbilityMismatchFlag, CheckIdentifier::PID);
                            return;
                        }
                    }
                    else if (!isColosseumXD && abilityBit != ((pokemon.encryptionConstant() & 1) == 1))
                    {
                        addInvalid(ResultCode::AbilityMismatchPID, CheckIdentifier::PID);
                        return;
                    }
                }
                else if (context.formatGeneration >= 6)
                {
                    return;
                }
                verifyPIDAbilityMatch();
            }

            /// PKHeX IsAbilityCapsuleModified.
            bool isAbilityCapsuleModified(AbilityPermission permission) const
            {
                return isAbilityCapsulePossible(history) && abilityNumber != ABILITY_NUMBER_HIDDEN && permission != AbilityPermission::OnlyHidden;
            }

            /// PKHeX VerifyFixedAbility.
            void verifyFixedAbility(AbilityState state, AbilityPermission permission, int abilityIndex) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const int fixedIndex = getSingleValue(permission);
                if (originGeneration >= 6)
                {
                    if (isAbilityCapsuleModified(permission))
                    {
                        return;
                    }
                    if (abilityNumber != 1 << fixedIndex)
                    {
                        addInvalid(ResultCode::AbilityMismatch);
                    }
                    return;
                }
                if ((abilityNumber == ABILITY_NUMBER_HIDDEN) != (permission == AbilityPermission::OnlyHidden))
                {
                    addInvalid(ResultCode::AbilityHiddenFail);
                    return;
                }
                const bool hasEvolved = encounterSpecies != 0 && encounterSpecies != pokemon.speciesID();
                if (hasEvolved && state != AbilityState::CanMismatch)
                {
                    // Evolving in Gen 3 keeps the ability bit, so a mismatch stays a mismatch.
                    if (originGeneration == 3)
                    {
                        if (fixedIndex == abilityIndex)
                        {
                            return;
                        }
                        if (context.formatGeneration != 3)
                        {
                            verifyPIDAbilityMatch();
                            return;
                        }
                        addInvalid(ResultCode::AbilityMismatch);
                        return;
                    }
                    checkMatch(originGeneration, AbilityState::MustMatch);
                    return;
                }
                if (fixedIndex == abilityIndex || abilityNumber == 1 << fixedIndex)
                {
                    return;
                }
                if (state == AbilityState::CanMismatch)
                {
                    checkMatch(originGeneration, AbilityState::MustMatch);
                    return;
                }
                if (isAbilityCapsuleModified(permission))
                {
                    return;
                }
                addInvalid(ResultCode::AbilityMismatch);
            }

            /// PKHeX VerifyAbilityGen3Transfer.
            AbilityState verifyAbilityGen3Transfer(uint16_t maxGen3Species) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const bool isColosseumXD = pokemon.originGame() == VERSION_COLOSSEUM_XD;
                const Pokemon::PersonalRecord &generationThree = Pokemon::getPersonalRecord(Enums::GameVersion::RSE, maxGen3Species, 0);
                if (generationThree.ability1 != generationThree.ability2)
                {
                    return isColosseumXD ? AbilityState::CanMismatch : AbilityState::MustMatch;
                }
                if (pokemon.speciesID() != maxGen3Species)
                {
                    return AbilityState::MustMatch;
                }
                const bool evolvedAfterGenerationThree = history.getChain(Enums::GameVersion::DP).size() > 1 || (context.formatGeneration == 5 && history.getChain(Enums::GameVersion::BW).size() > 1);
                if (evolvedAfterGenerationThree)
                {
                    if (pokemon.ability() == generationThree.ability1)
                    {
                        return AbilityState::CanMismatch;
                    }
                    if (pokemon.ability() == abilities.getAbilityInSlot(1))
                    {
                        return AbilityState::MustMatch;
                    }
                }
                if (pokemon.ability() != generationThree.ability1)
                {
                    addInvalid(ResultCode::AbilityMismatch3);
                }
                return AbilityState::CanMismatch;
            }

            /// PKHeX VerifyAbilityPreCapsule.
            AbilityState verifyAbilityPreCapsule() const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (originGeneration != 3)
                {
                    return AbilityState::MustMatch;
                }
                if (context.formatGeneration == 3)
                {
                    return pokemon.originGame() == VERSION_COLOSSEUM_XD ? AbilityState::CanMismatch : AbilityState::MustMatch;
                }
                if (pokemon.speciesID() > MAX_SPECIES_GENERATION_3)
                {
                    return AbilityState::MustMatch;
                }
                const std::span<const Evolutions::EvolutionCriteria> generationThreeStages = history.getChain(Enums::GameVersion::RSE);
                if (generationThreeStages.empty())
                {
                    return AbilityState::MustMatch;
                }
                return verifyAbilityGen3Transfer(generationThreeStages[0].speciesId);
            }

            /// PKHeX VerifyAbility5.
            void verifyAbility5(AbilityPermission permission) const
            {
                if (context.encounter.type == EncounterType::Egg && abilityNumber == ABILITY_NUMBER_HIDDEN && !canBeHidden(permission))
                {
                    addInvalid(ResultCode::AbilityHiddenUnavailable);
                    return;
                }
                checkMatch(5, context.formatGeneration == 5 ? AbilityState::MustMatch : AbilityState::CanMismatch);
            }

            /// PKHeX VerifyAbility345.
            void verifyAbility345(AbilityPermission permission, int abilityIndex) const
            {
                AbilityState state = AbilityState::MustMatch;
                if (!abilities.isAbility12Same())
                {
                    state = verifyAbilityPreCapsule();
                }
                if (permission != AbilityPermission::Any12H)
                {
                    if ((abilityNumber == ABILITY_NUMBER_HIDDEN) != (permission == AbilityPermission::OnlyHidden))
                    {
                        addInvalid(ResultCode::AbilityHiddenFail);
                        return;
                    }
                    if (permission != AbilityPermission::Any12)
                    {
                        verifyFixedAbility(state, permission, abilityIndex);
                        return;
                    }
                }
                if (originGeneration == 5)
                {
                    verifyAbility5(permission);
                    return;
                }
                checkMatch(originGeneration, state);
            }

            /// PKHeX VerifyAbility (format 6 and later).
            void verifyAbility6(AbilityPermission permission, int abilityIndex) const
            {
                if (permission != AbilityPermission::Any12H)
                {
                    if ((abilityNumber == ABILITY_NUMBER_HIDDEN) != (permission == AbilityPermission::OnlyHidden))
                    {
                        addInvalid(ResultCode::AbilityHiddenFail);
                        return;
                    }
                    if (permission != AbilityPermission::Any12)
                    {
                        verifyFixedAbility(AbilityState::CanMismatch, permission, abilityIndex);
                        return;
                    }
                }
                if (originGeneration == 5)
                {
                    verifyAbility5(permission);
                    return;
                }
                if (originGeneration >= 6)
                {
                    // PKHeX VerifyAbility6/7/8BDSP: a bred Hidden Ability the species cannot pass on.
                    const bool checksEgg = originGeneration <= 7 || context.encounter.originGroup == Enums::GameVersion::BDSP;
                    if (checksEgg && abilityNumber == ABILITY_NUMBER_HIDDEN && context.encounter.type == EncounterType::Egg && !canBeHidden(permission))
                    {
                        addInvalid(ResultCode::AbilityHiddenUnavailable);
                    }
                    return;
                }
                checkMatch(originGeneration, AbilityState::CanMismatch);
            }
        };

        /// PKHeX CanAbilityCapsule.
        bool canAbilityCapsule(int formatGeneration, const AbilitySet &abilities)
        {
            return formatGeneration >= 6 && !abilities.isAbility12Same();
        }

        /// PKHeX VerifyAbilityPCD.
        void verifyAbilityPCD(const AbilityAnalysis &analysis, const Encounters::MysteryGiftPCD &card)
        {
            const AnalysisContext &context = analysis.context;
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGeneration >= 6)
            {
                if (!canAbilityCapsule(context.formatGeneration, analysis.abilities))
                {
                    if (analysis.abilityNumber != ABILITY_NUMBER_FIRST)
                    {
                        analysis.checkMatch(4, AbilityState::MustMatch);
                    }
                    return;
                }
                if (analysis.abilityNumber < ABILITY_NUMBER_HIDDEN)
                {
                    return;
                }
            }
            if (card.speciesId() != pokemon.speciesID())
            {
                analysis.checkMatch(4, AbilityState::MustMatch);
                return;
            }
            if (pokemon.ability() != card.giftData().abilityId)
            {
                analysis.addInvalid(ResultCode::AbilityMismatch);
            }
        }

        /// PKHeX VerifyAbilityMG: a Gen 4+ Mystery Gift is judged by the card's own ability.
        void verifyAbilityMysteryGift(const AbilityAnalysis &analysis, const Encounters::Encounterable &gift, AbilityPermission permission)
        {
            const AnalysisContext &context = analysis.context;
            if (const Encounters::MysteryGiftPCD *card = Encounters::encounterAs<Encounters::MysteryGiftPCD>(gift))
            {
                verifyAbilityPCD(analysis, *card);
                return;
            }
            const int abilityNumber = analysis.abilityNumber;
            if (Encounters::encounterAs<Encounters::MysteryGiftPGT>(gift) != nullptr)
            {
                const bool isValid = context.formatGeneration >= 6 ? abilityNumber == ABILITY_NUMBER_FIRST : abilityNumber < ABILITY_NUMBER_HIDDEN;
                if (!isValid)
                {
                    analysis.addInvalid(ResultCode::AbilityMismatchGift);
                }
                return;
            }
            if (permission == AbilityPermission::Any12H)
            {
                return;
            }
            if (permission == AbilityPermission::Any12)
            {
                if (abilityNumber == ABILITY_NUMBER_HIDDEN)
                {
                    analysis.addInvalid(ResultCode::AbilityMismatchGift);
                }
                return;
            }
            if (abilityNumber == static_cast<int>(permission))
            {
                return;
            }
            if (abilityNumber == ABILITY_NUMBER_HIDDEN || permission == AbilityPermission::OnlyHidden)
            {
                analysis.addInvalid(ResultCode::AbilityHiddenFail);
                return;
            }
            if (context.formatGeneration >= 6 && isAbilityCapsulePossible(analysis.history))
            {
                return;
            }
            analysis.addInvalid(context.formatGeneration < 6 ? ResultCode::AbilityMismatchPID : ResultCode::AbilityMismatch);
        }

        /// PKHeX IsEdgeCaseBasculin. Black/White gave wild and bred Blue-Striped Basculin Reckless by
        /// reading the wrong personal entry, Black 2/White 2 bred ones; White's in-game trade and Black
        /// 2/White 2's wild ones have the correct Rock Head. True when this decided the ability.
        bool verifyEdgeCaseBasculin(const AnalysisContext &context, int abilityNumber)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.speciesID() != BASCULIN_SPECIES || pokemon.form() != 1 || abilityNumber == ABILITY_NUMBER_HIDDEN || pokemon.ability() == ADAPTABILITY_ABILITY)
            {
                return false;
            }
            const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
            uint16_t expectedAbility = ROCK_HEAD_ABILITY;
            if (originVersion == Enums::GameVersion::B2 || originVersion == Enums::GameVersion::W2)
            {
                if (context.encounter.type == EncounterType::Egg)
                {
                    expectedAbility = RECKLESS_ABILITY;
                }
            }
            else if (originVersion == Enums::GameVersion::B || originVersion == Enums::GameVersion::W)
            {
                if (context.encounter.type == EncounterType::Unchecked)
                {
                    return true; // whether it is White's in-game trade is unknown
                }
                if (context.encounter.type != EncounterType::Trade)
                {
                    expectedAbility = RECKLESS_ABILITY;
                }
            }
            if (pokemon.ability() != expectedAbility)
            {
                context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityUnexpected);
            }
            return true;
        }

        /// PKHeX VerifyBirthAbility: Legends: Z-A keeps the ability a Pokemon was encountered with.
        void verifyBirthAbility(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr)
            {
                return;
            }
            const uint8_t encounterForm = encounterMatch->formId();
            const int abilityNumber = pokemon.abilityNumber();
            if (!isValidAbilityBits(abilityNumber))
            {
                context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityMismatch);
                return;
            }
            const int abilityIndex = abilityNumber >> 1;
            const uint16_t speciesId = pokemon.speciesID();
            const uint8_t formId = pokemon.form();
            const uint16_t abilityId = pokemon.ability();
            // A battle form may hold the form's own ability.
            if (Pokemon::hasMegaForm(speciesId) || speciesId == AEGISLASH_SPECIES)
            {
                if (abilityId == getAbilitySet(Enums::GameVersion::ZA, speciesId, formId).getAbilityInSlot(abilityIndex))
                {
                    return;
                }
                const bool mustHaveBeenReplaced = speciesId == AEGISLASH_SPECIES ? formId != 0 : Pokemon::isMegaForm(speciesId, formId);
                if (mustHaveBeenReplaced)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityMismatch);
                    return;
                }
            }
            // HOME realigns the ability to the deposited species and form.
            if (pokemon.hasHomeTracker() && pokemon.homeTracker() != 0)
            {
                if (abilityId == getAbilitySet(Enums::GameVersion::ZA, speciesId, formId).getAbilityInSlot(abilityIndex))
                {
                    return;
                }
                if (encounterMatch->speciesId() == speciesId)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityMismatch);
                    return;
                }
            }
            if (abilityId != getAbilitySet(Enums::GameVersion::ZA, encounterMatch->speciesId(), encounterForm).getAbilityInSlot(abilityIndex))
            {
                context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityMismatch);
                return;
            }
            // PKHeX VerifyFinalState. Every Legends: Z-A template is Any12, so only the Hidden slot,
            // which no Z-A encounter gives, is left to decide.
            if (abilityIndex == 2 && !isAbilityPatchPossible(context.evolutionHistory()))
            {
                context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityHiddenUnavailable);
            }
        }
    }

    void verifyAbility(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasAbility())
        {
            return;
        }
        if (context.formatGroup == Enums::GameVersion::ZA)
        {
            verifyBirthAbility(context);
            return;
        }
        const int abilityNumber = getAbilityNumber(context);
        if (context.formatGeneration == 5 && verifyEdgeCaseBasculin(context, abilityNumber))
        {
            return;
        }

        const AbilitySet abilities = getAbilitySet(getPersonalTableGroup(context.formatGroup), pokemon.speciesID(), pokemon.form());
        const uint16_t abilityId = pokemon.ability();
        const int abilityIndex = abilities.indexOf(abilityId);
        if (abilityIndex < 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::Ability, ResultCode::AbilityUnexpected);
            return;
        }

        const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
        const uint16_t encounterSpecies = encounterMatch != nullptr ? encounterMatch->speciesId() : 0;
        const AbilityAnalysis analysis{context, abilities, abilityNumber, getEncounterGeneration(context), encounterSpecies, context.evolutionHistory()};

        if (context.formatGeneration >= 6)
        {
            if (!isValidAbilityBits(abilityNumber))
            {
                analysis.addInvalid(ResultCode::AbilityMismatch);
                return;
            }
            const int slotIndex = abilityNumber >> 1;
            if (slotIndex >= abilities.abilityCount || abilities.getAbilityInSlot(slotIndex) != abilityId)
            {
                analysis.addInvalid(ResultCode::AbilityMismatch);
                return;
            }
            // A Gen 3-5 transfer takes the first ability when both are the same; a capsule cannot
            // flip between two identical abilities.
            const int originGeneration = getOriginGeneration(context);
            if (originGeneration >= 3 && originGeneration <= 5 && abilityNumber != ABILITY_NUMBER_HIDDEN && abilities.isAbility12Same() && abilityNumber != ABILITY_NUMBER_FIRST)
            {
                if (!isAbilityCapsulePossible(analysis.history))
                {
                    analysis.addInvalid(ResultCode::AbilityMismatch);
                }
                return;
            }
        }

        std::optional<AbilityPermission> permission;
        bool isMysteryGift = false;
        if (encounterMatch != nullptr)
        {
            permission = static_cast<AbilityPermission>(static_cast<int8_t>(encounterMatch->ability()));
            isMysteryGift = Encounters::isMysteryGift(encounterMatch->templateType()) && encounterMatch->generation() >= 4;
        }
        if (!permission)
        {
            return;
        }
        if (context.formatGeneration >= 8)
        {
            if (abilityNumber == ABILITY_NUMBER_HIDDEN && (canBeHidden(*permission) || isAbilityPatchPossible(analysis.history)))
            {
                return;
            }
            if (abilityNumber != ABILITY_NUMBER_HIDDEN && *permission == AbilityPermission::OnlyHidden && isAbilityPatchRevertPossible(analysis.history, abilityNumber))
            {
                return;
            }
        }
        if (isMysteryGift)
        {
            verifyAbilityMysteryGift(analysis, *encounterMatch, *permission);
            return;
        }
        if (context.formatGeneration < 6)
        {
            analysis.verifyAbility345(*permission, abilityIndex);
            return;
        }
        analysis.verifyAbility6(*permission, abilityIndex);
    }
}
