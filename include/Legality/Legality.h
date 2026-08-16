/**
 * Legality.h - PKSE legality checker (informational).
 *
 * analyze() runs three layers against a decrypted Pokemon and returns human-readable
 * issues:
 * 
 *      Layer 1 structural      — field ranges: EV/AV caps, nature and ball IDs, known
 *                                species/move/item IDs, name lengths, checksum.
 *      Layer 2 copnsistency    — the Pokemon against itself and its species data: level
 *                                vs EXP, met level vs level, gender vs gender ratio,
 *                                ability against the species' slots, move learnability,
 *                                per-game species presence.
 *      Layer 3 encounter       — the Pokemon against the game it says it came from: is
 *                                there a real encounter template that produces it, at
 *                                that met location and met level, with that shiny state,
 *                                ball, gender and nature? See EncounterMatch.h.
 * 
 * It stays INFORMATIONAL (no auto-fix) and deliberately conservative. Layer 3's data
 * is real but not complete — event/Mystery Gift distributions are not modelled, and
 * neither are PID/seed correlations. So a clean report still means "no problems
 * found", never a guarantee of full legality. Where a check would need data PKSE does
 * not have, it says so as an info note, rather than staying silent.
 *
 * No RTTI / no exceptions: every check calls base Pokemon virtuals; per-gen
 * differences branch on the game group / capability flags, never on concrete type.
 */

#ifndef LEGALITY_LEGALITY_H
#define LEGALITY_LEGALITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "Enums/GameVersion.h"

namespace Pokemon { class Pokemon; }  // fwd decl — no heavy include

namespace Legality {

    enum class Severity : uint8_t { Info, Warning, Invalid };

    struct Issue {
        Severity severity;
        std::string text;
    };

    struct Report {
        std::vector<Issue> issues;

        /// Number of Warning+Invalid issues (Info notes are not counted as problems).
        int problemCount() const noexcept {
            int n = 0;
            for (const auto& i : issues) if (i.severity != Severity::Info) ++n;
            return n;
        }
        bool ok() const noexcept { return problemCount() == 0; }

        /// Is there anything at all to show? A Pokemon with no problems can still have
        /// Info notes — which encounter it matched, or that a check could not run,
        /// so the UI opens its issues list on this, not an ok().
        bool empty() const noexcept { return issues.empty(); }
    };

    /// Analyze a decrypted Pokemon and return its legality issues (informational).
    /// originGroup = the save's format group (Trainer::getGameGroup()). Returns an
    /// empty report for an empty slot (species 0).
    Report analyze(const Pokemon::Pokemon& pk, Enums::GameVersion originGroup);
}

#endif
