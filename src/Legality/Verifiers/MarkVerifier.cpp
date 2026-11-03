// Port of PKHeX PKHeX.Core/Legality/Verifiers/MarkVerifier.cs: marks and the affixed ribbon.
#include "Legality/Verifiers/Verifiers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "Conversion/Convert.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/RibbonRules.h"
#include "Names/RibbonNames.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t NINCADA_SPECIES = 290;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint8_t POKE_BALL = 4;
        // Moves only a Ninjask learns in Gens 3 and 4, which a Shedinja keeps from the evolution.
        constexpr uint16_t MOVE_SWORDS_DANCE = 14;
        constexpr uint16_t MOVE_AGILITY = 97;
        constexpr uint16_t MOVE_SCREECH = 103;
        constexpr uint16_t MOVE_SLASH = 163;
        constexpr uint16_t MOVE_BATON_PASS = 226;
        constexpr int MOVE_SLOT_COUNT = 4;

        struct MarkRange
        {
            int firstMarkIndex = -1;
            int lastMarkIndex = -1;
        };

        /// RibbonIndex MarkLunchtime..MarkSlump, the encounter marks Sword/Shield introduced.
        MarkRange getEncounterMarkRange(const RibbonArguments &arguments)
        {
            const Names::RibbonDefinition *firstMark = arguments.find("ribbonmarklunchtime");
            const Names::RibbonDefinition *lastMark = arguments.find("ribbonmarkslump");
            if (firstMark == nullptr || lastMark == nullptr)
            {
                return MarkRange{};
            }
            return MarkRange{Names::getRibbonAffixIndex(*firstMark), Names::getRibbonAffixIndex(*lastMark)};
        }

        bool hasRibbonAt(const RibbonArguments &arguments, int ribbonIndex)
        {
            if (ribbonIndex < 0 || static_cast<size_t>(ribbonIndex) >= arguments.ribbons.size())
            {
                return false;
            }
            return arguments.value(arguments.ribbons[ribbonIndex].spriteStem) != 0;
        }

        std::string getRibbonIndexName(const RibbonArguments &arguments, int ribbonIndex)
        {
            if (ribbonIndex < 0 || static_cast<size_t>(ribbonIndex) >= arguments.ribbons.size())
            {
                return std::to_string(ribbonIndex);
            }
            return Names::getRibbonName(arguments.ribbons[ribbonIndex], 0);
        }

        void addAffixed(const RibbonArguments &arguments, Severity severity, int ribbonIndex)
        {
            arguments.context.add(severity, CheckIdentifier::RibbonMark, ResultCode::RibbonMarkingAffixed, getRibbonIndexName(arguments, ribbonIndex));
        }

        void verifyNoMarksPresent(const RibbonArguments &arguments, const MarkRange &markRange)
        {
            for (int markIndex = markRange.firstMarkIndex; markIndex <= markRange.lastMarkIndex; markIndex++)
            {
                if (hasRibbonAt(arguments, markIndex))
                {
                    arguments.context.add(Severity::Invalid, CheckIdentifier::RibbonMark, ResultCode::RibbonMarkingInvalid, getRibbonIndexName(arguments, markIndex));
                }
            }
        }

        void verifyMarksPresent(const RibbonArguments &arguments, const MarkRange &markRange)
        {
            bool hasOneMark = false;
            for (int markIndex = markRange.firstMarkIndex; markIndex <= markRange.lastMarkIndex; markIndex++)
            {
                if (!hasRibbonAt(arguments, markIndex))
                {
                    continue;
                }
                // A Pokemon is met with at most one mark.
                const std::optional<bool> isMarkValid = hasOneMark ? std::optional<bool>(false) : isEncounterMarkValid(arguments, markIndex);
                if (isMarkValid.has_value() && !*isMarkValid)
                {
                    arguments.context.add(Severity::Invalid, CheckIdentifier::RibbonMark, ResultCode::RibbonMarkingInvalid, getRibbonIndexName(arguments, markIndex));
                    return;
                }
                hasOneMark = true;
            }
        }

        bool hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId)
        {
            for (int moveSlot = 0; moveSlot < MOVE_SLOT_COUNT; moveSlot++)
            {
                if (pokemon.move(moveSlot) == moveId)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX MarkVerifier.IsMoveSetEvolvedShedinja. PKSE does not read Sword/Shield's TR flags,
        /// so Agility there counts as the Ninjask move, which only asks for the affixed ribbon.
        bool isMoveSetEvolvedShedinja(const RibbonArguments &arguments)
        {
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            if (hasMove(pokemon, MOVE_SCREECH) || hasMove(pokemon, MOVE_SWORDS_DANCE) || hasMove(pokemon, MOVE_SLASH) || hasMove(pokemon, MOVE_BATON_PASS))
            {
                return true;
            }
            return hasMove(pokemon, MOVE_AGILITY) && arguments.context.formatGroup == Enums::GameVersion::SWSH;
        }

        void ensureHasRibbon(const RibbonArguments &arguments, int ribbonIndex)
        {
            if (!hasRibbonAt(arguments, ribbonIndex))
            {
                addAffixed(arguments, Severity::Invalid, ribbonIndex);
            }
        }

        /// Shedinja keeps the affixed value but none of Nincada's ribbons or marks, so the check
        /// asks whether the Nincada could have had it.
        void verifyShedinjaAffixed(const RibbonArguments &arguments, const MarkRange &markRange, int affixedIndex)
        {
            if (affixedIndex >= markRange.firstMarkIndex && affixedIndex <= markRange.lastMarkIndex)
            {
                const std::optional<bool> isMarkValid = isEncounterMarkValid(arguments, affixedIndex);
                if (isMarkValid.has_value() && !*isMarkValid)
                {
                    addAffixed(arguments, Severity::Invalid, affixedIndex);
                }
                return;
            }
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            if (arguments.encounter.generation <= 4 && (pokemon.ball() != POKE_BALL || isMoveSetEvolvedShedinja(arguments)))
            {
                // Evolved in an earlier generation, where the ribbons carried over.
                ensureHasRibbon(arguments, affixedIndex);
                return;
            }
            if (affixedIndex < 0 || static_cast<size_t>(affixedIndex) >= arguments.ribbons.size())
            {
                return;
            }
            RibbonArguments nincadaArguments = arguments;
            nincadaArguments.speciesId = NINCADA_SPECIES;
            nincadaArguments.forcedRibbon = &arguments.ribbons[affixedIndex];
            RibbonResultList results;
            getRibbonResults(nincadaArguments, results);
            bool isRibbonValid = true;
            for (const RibbonResult &result : results)
            {
                if (result.ribbon == nincadaArguments.forcedRibbon)
                {
                    isRibbonValid = false;
                }
            }
            addAffixed(arguments, isRibbonValid ? Severity::Warning : Severity::Invalid, affixedIndex);
        }

        void verifyAffixedRibbonMark(const RibbonArguments &arguments, const MarkRange &markRange, int affixedIndex)
        {
            const int maxAffixIndex = getMaxAffixValue(arguments);
            if (maxAffixIndex == -1 || affixedIndex > maxAffixIndex)
            {
                addAffixed(arguments, Severity::Invalid, affixedIndex);
                return;
            }
            if (isEncounterMarkLost(arguments))
            {
                verifyShedinjaAffixed(arguments, markRange, affixedIndex);
                return;
            }
            // Legends: Arceus and Legends: Z-A cannot affix, so their own Pokemon must have been elsewhere.
            const Enums::GameVersion encounterContext = arguments.encounter.context;
            const bool isFromLegendsGame = encounterContext == Enums::GameVersion::PLA || encounterContext == Enums::GameVersion::ZA;
            if (isFromLegendsGame && Evolutions::getEvolutionContextGroup(arguments.context.formatGroup) == encounterContext && !arguments.history.hasVisitedExcept({encounterContext}))
            {
                addAffixed(arguments, Severity::Invalid, affixedIndex);
            }
            ensureHasRibbon(arguments, affixedIndex);
        }
    }

    void verifyMarks(const AnalysisContext &context)
    {
        RibbonArguments arguments = makeRibbonArguments(context);
        if (context.encounter.type == EncounterType::Unmatched)
        {
            // Judged by where it came from rather than as PKHeX's EncounterInvalid, which PKSE's
            // weaker matcher would turn into findings on Pokemon PKHeX matches.
            arguments.encounter.context = Evolutions::getEvolutionContextGroup(getOriginContextGroup(context));
        }
        const MarkRange markRange = getEncounterMarkRange(arguments);
        if (markRange.firstMarkIndex < 0)
        {
            return;
        }
        const Pokemon::Pokemon &pokemon = context.pokemon;
        // A Shedinja met as one, or evolved from a Nincada that kept nothing: unknown without the encounter.
        if (pokemon.speciesID() == SHEDINJA_SPECIES && arguments.encounter.speciesId == 0)
        {
            return;
        }
        if (!isEncounterMarkAllowed(arguments))
        {
            verifyNoMarksPresent(arguments, markRange);
        }
        else
        {
            verifyMarksPresent(arguments, markRange);
        }

        const size_t affixedOffset = Conversion::affixedRibbonOffset(context.formatGroup);
        if (affixedOffset == 0 || affixedOffset >= pokemon.getData().size())
        {
            return;
        }
        const uint8_t affixedValue = static_cast<uint8_t>(pokemon.getData()[affixedOffset]);
        if (affixedValue == Conversion::AFFIXED_RIBBON_NONE)
        {
            return;
        }
        verifyAffixedRibbonMark(arguments, markRange, affixedValue);
        if (pokemon.isEgg())
        {
            addAffixed(arguments, Severity::Invalid, affixedValue);
        }
        // A Scarlet/Violet static encounter that comes with the Crafty Mark (PKHeX IEncounterMarkExtra)
        // is not recorded in PKSE's rows, so a missing fixed mark is not checked.
    }
}
