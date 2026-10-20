/**
 * Deliberately separate from TestRunner.cpp, and deliberately free of <switch.h>. This table is the
 * one authoritative statement of what the self test consists of and in what order; the runner is a
 * state machine that walks it. Keeping the two apart means adding a suite is a one-line edit to a
 * list rather than a change inside a loop, and it keeps the list compilable off-console -- the
 * runner cannot be, since it enumerates the console's save data.
 *
 * (tools/harness/build-selftest.sh runs the host-safe suites on a PC. It calls them directly rather
 * than through this table, because linking the table would drag in the ones that genuinely need the
 * Switch SDK -- which is also why the sample pool is inline in the header rather than here.)
 */
#include "Test/TestSuite.h"

namespace Test
{

    namespace
    {
        const Suite SUITES[] = {
            {"tables", Scope::Global, tablesPlan, tablesRun},
            {"assets", Scope::Global, assetsPlan, assetsRun},
            // The save picker is the first screen the user meets, and the only one they can reach
            // before a save is loaded -- so it is Global, and runs before any target is opened.
            {"picker", Scope::Global, pickerPlan, pickerRun},
            // The file browser is the other half of that first screen -- the only way a loose save
            // is ever opened -- and it is Global for the same reason: it needs no save, and the
            // thing it is most likely to get wrong is WHICH FILES IT OFFERS, which is decidable
            // before any target is loaded. Unlike "picker" it is host-buildable, so the uihost
            // harness runs it too.
            {"browser", Scope::Global, fileBrowserPlan, fileBrowserRun},
            {"gentables", Scope::PerTarget, generationTablesPlan, generationTablesRun},
            {"entity", Scope::PerTarget, entityPlan, entityRun},
            {"setters", Scope::PerTarget, settersPlan, settersRun},
            {"legality", Scope::PerTarget, legalityPlan, legalityRun},
            // "ui" drives the real screens with synthetic input and verifies that each keypress
            // changed ONLY what it was declared to be allowed to change. It sits here, after every
            // read-only suite and before "save", for two reasons: the journeys navigate rather than
            // edit, so they need the save as the console left it; and "save" mutates the trainer, so
            // running after it would have the UI walking over data the test itself had just changed.
            {"ui", Scope::PerTarget, uiPlan, uiRun},
            // "save" runs LAST of the per-target suites, and the order is load-bearing: its
            // edit-persistence chunk deliberately MUTATES the loaded trainer (money, trainer name,
            // a box name, an item quantity, one Pokemon's IVs / EVs / nickname) to prove the edits
            // survive a write. Every suite above reads the save as it was found -- and "entity" is
            // also where the cross-generation sample pool is cloned from -- so running this first
            // would have them all checking, and banking, Pokemon the test itself had just changed.
            // Pokemon files read the save's Pokemon and write only into the work directory, so they can
            // sit anywhere before "save"; just before it, so they see the save as it was found.
            {"pokemonfile", Scope::PerTarget, pokemonFilePlan, pokemonFileRun},
            {"save", Scope::PerTarget, savePlan, saveRun},
            {"conversion", Scope::Cross, conversionPlan, conversionRun},
            {"bank", Scope::Cross, bankPlan, bankRun},
        };
    }

    const Suite *suites() noexcept { return SUITES; }
    int suiteCount() noexcept { return (int)(sizeof(SUITES) / sizeof(SUITES[0])); }
}
