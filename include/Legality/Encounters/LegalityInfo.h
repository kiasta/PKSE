/**
 * What the encounter search established about one Pokemon: the matched template, the PID/IV
 * correlation, the move results and the evolution chains the later verifiers read. Ported from
 * PKHeX PKHeX.Core/Legality/Structures/LegalInfo.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_LEGALITYINFO_H
#define LEGALITY_ENCOUNTERS_LEGALITYINFO_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Legality/AnalysisContext.h"
#include "Legality/Encounters/CheckResult.h"
#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/LearnSource/MoveResult.h"
#include "Legality/Legality.h"
#include "Legality/RNG/PIDIV.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncounterYieldFlag: a generator yielded its last-resort candidate for this reason.
        enum class EncounterYieldFlag : uint8_t
        {
            None = 0,
            InvalidPIDIV,
            InvalidFrame,
        };

        struct LegalityInfo
        {
            LegalityInfo(const Pokemon::Pokemon &pokemon, const AnalysisOptions &options);

            const Pokemon::Pokemon &pokemon;
            /// PKHeX ParseSettings.AllowEraCartGBA / AllowEraSwitchGBA as InitFromSaveFileData sets
            /// them for the open save (both PKHeX defaults when there is none).
            bool allowEraCartGBA = true;
            bool allowEraSwitchGBA = false;
            /// PKHeX ParseSettings.AllowEraCartGB (Stadium 2's move reminder, read by the move checks).
            bool allowEraCartGB = false;
            Evolutions::EvolutionEntity entity;
            /// PKHeX LegalInfo.Generation: the origin generation the search ran for.
            uint8_t generation = 0;

            /// PKHeX EncounterMatch. Never null after findVerifiedEncounter (EncounterInvalid at worst).
            const EncounterReference &encounterMatch() const noexcept { return matchedEncounter; }
            /// PKHeX's setter: replaces the match and discards the findings made for the previous one.
            void setEncounterMatch(EncounterReference encounter);
            /// PKHeX EncounterOriginal: the Game Boy encounter a Virtual Console transfer came from,
            /// else the match.
            const EncounterReference &encounterOriginal() const noexcept { return encounterOriginalGameBoy ? encounterOriginalGameBoy : matchedEncounter; }
            EncounterReference encounterOriginalGameBoy;

            /// PKHeX Parse: findings the search made about the current match, reported ahead of the
            /// verifiers'.
            std::vector<CheckResult> parse;
            /// PKHeX Parse.TrueForAll(z => z.Valid).
            bool areAllParseValid() const noexcept;
            /// Appends the Fishy and Invalid findings in `parse` to a report (Valid ones are never shown).
            void reportParse(const AnalysisContext &context) const;

            /// PKHeX EvoChainsAllGens, built on first use for the current match.
            const Evolutions::EvolutionHistory &evolutionChainsAllGens() const;

            RNG::PIDIV pidiv;
            /// PKHeX PIDParsed: a generator ran MethodFinder.
            bool hasParsedPIDIV = false;
            void setPIDIV(const RNG::PIDIV &value) noexcept
            {
                pidiv = value;
                hasParsedPIDIV = true;
            }
            EncounterYieldFlag manualFlag = EncounterYieldFlag::None;
            bool frameMatches() const noexcept { return manualFlag != EncounterYieldFlag::InvalidFrame; }
            bool pidivMatches() const noexcept { return manualFlag != EncounterYieldFlag::InvalidPIDIV; }

            // Move results (owned by the move port, which fills them through EncounterMoveSeam).
            /// PKHeX LegalInfo.Moves and Relearn: one verdict per current and relearn move slot.
            std::array<LearnSource::MoveResult, 4> moves{};
            std::array<LearnSource::MoveResult, 4> relearn{};
            /// The matched template fixes moves PKSE cannot see, so an unparsed result above is unknown,
            /// not invalid (set with the results by the move verifiers).
            bool hasUnknownMoves = false;
            bool hasUnknownRelearnMoves = false;

        private:
            EncounterReference matchedEncounter;
            mutable std::optional<Evolutions::EvolutionHistory> evolutionHistory;
        };

        /// The engine's verdict in the short form many verifiers branch on: the matched template's
        /// EncounterType (Unmatched for EncounterInvalid, Unchecked for a Virtual Console transfer,
        /// whose verifiers answer from the Pokemon itself) and the met location's game group.
        EncounterInfo makeEncounterInfo(const LegalityInfo &info, const Pokemon::Pokemon &pokemon);

        /// PKHeX LegalInfo.EncounterMatch for a verifier: the engine's template; null only for a
        /// context built without an analysis (a verifier then skips what needs the template).
        const Encounterable *getEncounterMatch(const AnalysisContext &context);
        /// PKHeX LegalInfo.EncounterOriginal (a Virtual Console transfer's Game Boy encounter), the same way.
        const Encounterable *getEncounterOriginal(const AnalysisContext &context);
        /// PKHeX IFatefulEncounterReadOnly.FatefulEncounter of the matched template; false without one.
        bool isEncounterFateful(const AnalysisContext &context);
        /// PKHeX IEncounterTemplate.IsEgg of the matched template (a bred egg or an egg gift),
        /// answered the same way.
        bool isEncounterEgg(const AnalysisContext &context);
        /// PKHeX TrainerNameVerifier.IsEdgeCaseLength's first case: an unhatched Gen 3 Mystery Gift
        /// egg carrying the gift's own fixed OT name.
        bool isFixedTrainerGiftEgg(const AnalysisContext &context, const std::u16string &trainerName);
    }
}

#endif
