/**
 * The ribbon and mark rules the ribbon and mark verifiers share: PKSE's port of PKHeX's
 * Ribbons/RibbonRules.cs, Ribbons/MarkRules.cs and the per-set parsers RibbonVerifier runs
 * (RibbonVerifierCommon3/4/6/7/8/9, Event3/4, Only3, Unique3/4, Mark9).
 *
 * A ribbon is named by its RibbonDefinition::spriteStem, which is PKHeX's property name in lower
 * case, and a format carries one of PKHeX's ribbon sets exactly when its ribbon table holds that
 * set's ribbons.
 *
 * A rule that reads the template (a Mystery Gift's ribbons, a Titan, a seven-star raid, a Partner
 * trade) asks the encounter engine's match.
 */
#ifndef LEGALITY_VERIFIERS_RIBBONRULES_H
#define LEGALITY_VERIFIERS_RIBBONRULES_H

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "Legality/AnalysisContext.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Names/RibbonNames.h"

namespace Legality
{
    namespace Encounters
    {
        class Encounterable;
    }

    /// The encounter template properties the ribbon rules read (PKHeX IEncounterTemplate).
    struct RibbonEncounter
    {
        EncounterType type = EncounterType::Unchecked;
        /// PKHeX IEncounterTemplate.Generation and Context.
        int generation = 0;
        Enums::GameVersion context = Enums::GameVersion::Invalid;
        /// 0 when unknown.
        uint16_t speciesId = 0;
        uint8_t formId = 0;
        std::optional<uint8_t> levelMin;
        /// The encounter engine's template; nullptr only for a context built without an analysis,
        /// where a rule that needs the template answers "unknown" and its check is skipped.
        const Encounters::Encounterable *encounterMatch = nullptr;
    };

    /// PKHeX RibbonVerifierArguments, plus the ribbon table of the Pokemon's format.
    struct RibbonArguments
    {
        const AnalysisContext &context;
        std::span<const Names::RibbonDefinition> ribbons;
        const uint8_t *entityData = nullptr;
        Evolutions::EvolutionHistory history;
        RibbonEncounter encounter;
        /// PKM.Species: the Pokemon's own, or Nincada while MarkVerifier re-checks a Shedinja.
        uint16_t speciesId = 0;
        /// A ribbon read as set whatever the record holds (PKHeX RibbonIndex.Fix on a clone).
        const Names::RibbonDefinition *forcedRibbon = nullptr;

        const Names::RibbonDefinition *find(std::string_view spriteStem) const;
        /// The ribbon's value: 0/1 for a flag, the rank or the count; 0 when the format lacks it.
        uint8_t value(std::string_view spriteStem) const;
        bool has(std::string_view spriteStem) const { return value(spriteStem) != 0; }
    };

    struct RibbonResult
    {
        const Names::RibbonDefinition *ribbon = nullptr;
        /// True: the ribbon should be present and is not. False: it is present and should not be.
        bool isMissing = false;
    };

    /// PKHeX RibbonResultList.
    using RibbonResultList = std::vector<RibbonResult>;

    RibbonArguments makeRibbonArguments(const AnalysisContext &context);

    /// Append the format's ribbon `spriteStem` (PKHeX RibbonResultList.Add); nothing when the format lacks it.
    void addRibbonResult(RibbonResultList &results, const RibbonArguments &arguments, std::string_view spriteStem, bool isMissing = false);

    /// Append the ribbon as invalid when the Pokemon has it.
    void flagRibbonIfSet(RibbonResultList &results, const RibbonArguments &arguments, std::string_view spriteStem);

    /// PKHeX RibbonVerifierMark9.Parse / ParseEgg.
    void parseRibbonSetMark9(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg);

    /// PKHeX RibbonVerifier.GetRibbonResults.
    void getRibbonResults(const RibbonArguments &arguments, RibbonResultList &results);

    /// PKHeX RibbonRules.IsAllowedBattleFrontier (the Gen 3/4 Battle Frontier banlist).
    bool isAllowedBattleFrontier(uint16_t speciesId);

    /// PKHeX MarkRules.IsEncounterMarkLost: Nincada's Shedinja keeps no ribbons or marks.
    bool isEncounterMarkLost(const RibbonArguments &arguments);

    /// PKHeX MarkRules.IsEncounterMarkAllowed.
    bool isEncounterMarkAllowed(const RibbonArguments &arguments);

    /// PKHeX MarkRules.IsEncounterMarkValid for a mark's RibbonIndex; empty when the encounter
    /// properties it needs are unknown.
    std::optional<bool> isEncounterMarkValid(const RibbonArguments &arguments, int markIndex);

    /// PKHeX MarkRules.GetMaxAffixValue: -1 when nothing can be affixed.
    int getMaxAffixValue(const RibbonArguments &arguments);
}

#endif
