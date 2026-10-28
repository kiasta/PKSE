/**
 * Companion to roundtrip.cpp. That one proves the write path does not corrupt a save; this one
 * proves the READ-side judgement is not slandering it.
 *
 * Layer 3 (encounter matching) is the first check in PKSE that can call a legitimately-obtained
 * Pokemon illegal. Its data is real but not complete -- events are not modelled, wild level spans
 * are merged, and a single wrong offset in the generator turns into thousands of confident wrong
 * verdicts. The only way to measure that off-console is to point it at saves from actual
 * playthroughs, where nearly every Pokemon IS legal, and count what it complains about:
 *
 *     load a real save -> analyze every party and box Pokemon -> histogram the findings
 *
 * A healthy run is mostly "matched", with the leftovers being things the user really did edit.
 * A wall of "not a place X can be encountered" means the tables or the matcher are wrong, not the
 * save. Pass --verbose to print every flagged Pokemon instead of just the counts.
 *
 * Runs on the HOST, not the Switch, exactly like roundtrip.cpp.
 *
 * Build + run:  ./tools/harness/build-legality.sh <save-root> [--verbose]
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

/// The save root, so a skip can name the folder the reader needs to create.
static std::string g_saveRoot;

#include "Encryption/Encryption.h"
#include "Globals.h"     // SIZE_HASH_IN_BYTES
#include "Legality/EncounterTable.h" // the event-row bite check reads the table directly
#include "Legality/Legality.h"
#include "Pokemon/EvolutionTable.h" // the evolution bite check needs the level floors
#include "Pokemon/Pokemon.h"
#include "Trainer/Bank.h"  // makePokemon -- the reported case is built, not found
#include "Save/Block.h"
#include "Save/RtcFooter.h"   // Game Boy saves carry an emulator clock the parsers must not see
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer3RSE.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7USUM.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"

// Same logging stubs as roundtrip.cpp -- Logger.cpp needs POSIX dirent, which MSVC has no header
// for, and a harness has no use for the log.
namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
    void logEventToFile(const std::string &) {}
    // Bank.cpp comes in with the rest of src/Trainer and its SD-card paths reach this. Nothing in
    // this harness opens a bank, so returning "no such file" is both honest and the only outcome a
    // host run could have.
    uint8_t *readAllBytes(const char *, size_t *outSize)
    {
        if (outSize)
        {
            *outSize = 0;
        }
        return nullptr;
    }
}

namespace
{
    bool g_verbose = false;

    std::vector<uint8_t> readFile(const std::string &path)
    {
        std::vector<uint8_t> fileBytes;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            return fileBytes;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (fileSize > 0)
        {
            fileBytes.resize(static_cast<size_t>(fileSize));
            if (std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
            {
                fileBytes.clear();
            }
        }
        std::fclose(file);
        return fileBytes;
    }

    /**
     * Findings are histogrammed by SHAPE, not by exact text: "Route 3 (12) is not a place Nickit can
     * be encountered" and the same sentence about Wooloo are one bug, not two. Trimming at the first
     * digit or quote collapses the species and location out of the message.
     */
    std::string shapeOf(const std::string &text)
    {
        for (size_t characterIndex = 0; characterIndex < text.size(); ++characterIndex)
        {
            if (text[characterIndex] >= '0' && text[characterIndex] <= '9')
            {
                std::string leadingText = text.substr(0, characterIndex);
                while (!leadingText.empty() && leadingText.back() == ' ')
                {
                    leadingText.pop_back();
                }
                return leadingText + " ...";
            }
        }
        return text;
    }

    struct Tally
    {
        int pokemonCount = 0;
        int cleanCount = 0;                 // no Warning/Invalid at all
        int matchedCount = 0;               // Layer 3 found an encounter
        int invalidCount = 0;
        int warningCount = 0;
        std::map<std::string, int> findingCountByShape;
        // Info notes, grouped the same way. "Clean" is not the same as "checked": Layer 3
        // abstains for origins it has no tables for, and says so in a note. Counting those
        // is the only way to see how much of a save the check actually reached.
        std::map<std::string, int> noteCountByText;
        int notedCount = 0;                 // Pokemon carrying at least one abstention note
    };

    void analyzeOne(const ::Pokemon::Pokemon &pokemon, Enums::GameVersion group, Tally &tally)
    {
        if (pokemon.speciesID() == 0)
        {
            return;
        }
        ++tally.pokemonCount;
        const Legality::Report report = Legality::analyze(pokemon, group);
        if (report.ok())
        {
            ++tally.cleanCount;
        }
        bool printedHeader = false;
        bool carriesNote = false;
        for (const auto &issue : report.issues)
        {
            if (issue.severity == Legality::Severity::Info)
            {
                if (issue.text.rfind("Matches a", 0) == 0)
                {
                    ++tally.matchedCount;
                }
                // Notes keep their full text where findings get shaped: they only vary by the
                // origin id, and that id is the interesting part -- it says WHICH games a save
                // is full of that Layer 3 cannot reach.
                else
                {
                    tally.noteCountByText[issue.text]++;
                    carriesNote = true;
                }
                continue;
            }
            if (issue.severity == Legality::Severity::Invalid)
            {
                ++tally.invalidCount;
            }
            else
            {
                ++tally.warningCount;
            }
            tally.findingCountByShape[shapeOf(issue.text)]++;
            if (g_verbose)
            {
                if (!printedHeader)
                {
                    std::printf("    %-14s form %-2u  met %5u lv %-3u  origin %-3u\n", pokemon.species(), pokemon.form(), pokemon.metLocation(), pokemon.metLevel(), pokemon.originGame());
                    printedHeader = true;
                }
                std::printf("        %s %s\n", issue.severity == Legality::Severity::Invalid ? "[illegal]" : "[warning]", issue.text.c_str());
            }
        }
        if (carriesNote)
        {
            ++tally.notedCount;
        }
    }

    int g_bitePassCount = 0;
    int g_biteFailCount = 0;
    int g_wildPassCount = 0;
    int g_wildFailCount = 0;
    int g_wildSkipCount = 0;
    int g_catchComboPassCount = 0;
    int g_catchComboFailCount = 0;
    int g_evolutionPassCount = 0;
    int g_evolutionFailCount = 0;
    int g_evolutionSkipCount = 0;
    int g_evolutionElsewherePassCount = 0;
    int g_evolutionElsewhereFailCount = 0;
    int g_evolutionElsewhereSkipCount = 0;
    int g_seadraFailCount = 0;

    /**
     * Does the checker actually bite?
     *
     * A save full of legal Pokemon produces the same silence whether Layer 3 is working perfectly or
     * not running at all, so a clean sweep on its own proves nothing. This moves one real Pokemon to
     * a met location no game has (past every in-world table, below the 30000 trade/transfer bank) and
     * requires a complaint, then puts it back. Fail here and every "clean" line above is worthless.
     */
    int g_biteSkipCount = 0;

    void biteCheck(const char *label, ::Pokemon::Pokemon &pokemon, Enums::GameVersion group)
    {
        // A FORMAT WITH NO MET LOCATION CANNOT BE GIVEN A BOGUS ONE. Gen 1 has no such field and
        // Gold/Silver leave Gen 2's empty, so setMetLocation is a no-op there and the sweep does not
        // compare locations at all -- the Pokemon stays clean because the check is meaningless, not
        // because the table failed to bite.
        if (!pokemon.hasMetData())
        {
            ++g_biteSkipCount;
            return;
        }
        const uint16_t originalMetLocation = pokemon.metLocation();
        // An EGG LOCATION short-circuits the whole encounter sweep -- checkEgg answers instead, and it
        // says nothing about the met location -- so a hatched donor would stay clean however bogus a
        // place it claims to have been met, and report that as the table failing to bite.
        const uint16_t originalEggLocation = pokemon.eggLocation();
        pokemon.setEggLocation(0);
        // A BOGUS LOCATION THE FORMAT CAN ACTUALLY HOLD. 900 fits a u16, but Gen 3 keeps a byte and Gen 2
        // seven bits, so 900 was stored as 132 and 4 -- real places, which made the pose a real claim
        // rather than an impossible one. The first candidate that reads back exactly is used; none of
        // them is a location any table has in the format that can hold it.
        for (const uint16_t candidate : {uint16_t{900}, uint16_t{250}, uint16_t{125}})
        {
            pokemon.setMetLocation(candidate);
            if (pokemon.metLocation() == candidate)
            {
                break;
            }
        }
        const uint16_t bogusLocation = pokemon.metLocation();
        const Legality::Report report = Legality::analyze(pokemon, group);
        pokemon.setMetLocation(originalMetLocation);
        pokemon.setEggLocation(originalEggLocation);

        bool flagged = false;
        for (const auto &issue : report.issues)
        {
            if (issue.severity == Legality::Severity::Info)
            {
                continue;
            }
            if (issue.text.find("can be encountered") != std::string::npos || issue.text.find("egg can hatch") != std::string::npos)
            {
                flagged = true;
                break;
            }
        }
        if (flagged)
        {
            ++g_bitePassCount;
            return;
        }
        ++g_biteFailCount;
        std::printf("        BITE CHECK FAILED: %s stayed clean with met location %u (%s)\n", pokemon.species(), bogusLocation, label);
    }

    int g_eventPassCount = 0;
    int g_eventFailCount = 0;
    int g_eventSkipCount = 0;
    int g_eventNoRowsCount = 0;
    std::string g_eventSkipNames;   // the games that HAVE distributions and still could not pose one

    /**
     * Do the Mystery Gift distribution rows bite?
     *
     * The event tables are the last piece of Layer 3, and they have the same problem the rest of it
     * has: a save with no event Pokemon in it produces an identical clean sweep whether they work or
     * are missing entirely. Worse, they cannot be spot-checked by eye -- there are ~200 of them across
     * six games, keyed by ids nobody remembers.
     *
     * So this reads the game's OWN table, takes the first distribution row in it, stamps a real
     * Pokemon to look like that row, and requires the checker to report a Mystery Gift distribution.
     * That exercises the whole path end to end: the row was generated, the species index reaches it,
     * the sweep accepts its constraints, and the verdict names it correctly. The Pokemon is put back
     * exactly as it was.
     */
    void eventBiteCheck(const char *label, ::Pokemon::Pokemon &pokemon, Enums::GameVersion group)
    {
        static_cast<void>(group);
        // The donor's OWN table and version, for the reason wildBiteCheck gives: Layer 3 routes on the
        // Pokemon's origin, and half of every paired generation is version-exclusive. Gen 3's
        // distributions make the second half bite -- the 10th Anniversary Bulbasaur is Ruby-only, so
        // posing an Emerald Pokemon as it fails for a reason that says nothing about the table.
        // A PK1/PK2 records no origin at all, so it falls back to its own group with every
        // version counting -- the same fallback checkEncounter applies.
        const bool noOrigin = !pokemon.hasOriginGame();
        const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
        const Legality::EncounterTable *table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
        const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
        if (!table || donorVersionBit == 0)
        {
            ++g_eventSkipCount;
            return;
        }

        const Legality::EncounterRow *wantedRow = nullptr;
        bool wantedRowIsEgg = false;
        uint32_t distributionRowCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < table->rowCount; ++rowIndex)
        {
            const Legality::EncounterRow &row = table->rows[rowIndex];
            if (row.kind != Legality::ENCOUNTER_KIND_EVENT)
            {
                continue;
            }
            if ((row.versions & donorVersionBit) == 0)
            {
                continue;
            }
            ++distributionRowCount;
            // A plain distribution poses most directly, so it is preferred; an egg row is posed below
            // when the game has nothing else -- FireRed, LeafGreen and Emerald have only eggs.
            if (row.flags & Legality::ENCOUNTER_FLAG_EGG)
            {
                continue;
            }
            // THE DONOR'S OWN SHININESS IS NOT PART OF THE POSE and cannot be set from here -- it is a
            // PID/id relationship, not a field. So a row that locks it has to agree with the donor
            // already, or the sweep reaches the row, matches everything else and reports the lock --
            // a correct finding about a pose this check had no business building.
            const bool donorIsShiny = pokemon.isShiny(pokemon.id32(), Trainer::getSpeciesName(pokemon.speciesID()));
            if (row.shiny == Legality::ENCOUNTER_SHINY_NEVER && donorIsShiny)
            {
                continue;
            }
            if (row.shiny == Legality::ENCOUNTER_SHINY_ALWAYS && !donorIsShiny)
            {
                continue;
            }
            wantedRow = &row;
            break;
        }
        if (!wantedRow)
        {
            // The egg fallback. What can name a distribution egg differs by format: one with an
            // egg-location field carries it after hatching, while Gen 3 has none and only an egg that
            // has NOT hatched can say where it came from -- see matchingGiftEgg.
            for (uint32_t rowIndex = 0; rowIndex < table->rowCount; ++rowIndex)
            {
                const Legality::EncounterRow &row = table->rows[rowIndex];
                if (row.kind != Legality::ENCOUNTER_KIND_EVENT)
                {
                    continue;
                }
                if ((row.flags & Legality::ENCOUNTER_FLAG_EGG) == 0)
                {
                    continue;
                }
                if ((row.versions & donorVersionBit) == 0)
                {
                    continue;
                }
                if (pokemon.hasEggData() ? row.eggLocation == 0 : row.location == 0)
                {
                    continue;
                }
                wantedRow = &row;
                wantedRowIsEgg = true;
                break;
            }
        }
        if (!wantedRow)
        {
            if (distributionRowCount == 0)
            {
                ++g_eventNoRowsCount;   // nothing to pose as: this game has no distributions at all
            }
            else
            {
                ++g_eventSkipCount;
                g_eventSkipNames += (g_eventSkipNames.empty() ? "" : ", ") + std::string(label);
            }
            return;
        }

        const uint16_t originalSpecies = pokemon.speciesID();
        const uint8_t originalForm = pokemon.form();
        const uint16_t originalMetLocation = pokemon.metLocation();
        const uint8_t originalMetLevel = pokemon.metLevel();
        const uint8_t originalBall = pokemon.ball();
        const bool originalFateful = pokemon.isFatefulEncounter();

        const uint16_t originalEggLocation = pokemon.eggLocation();
        const bool originalIsEgg = pokemon.isEgg();

        pokemon.setSpecies(wantedRow->species);
        if (wantedRow->form != Legality::ENCOUNTER_FORM_ANY)
        {
            pokemon.setForm(wantedRow->form);
        }
        if (wantedRow->fixedBall)
        {
            pokemon.setBall(wantedRow->fixedBall);
        }
        pokemon.setFatefulEncounter(true);
        if (!wantedRowIsEgg)
        {
            pokemon.setMetLocation(wantedRow->location);
            pokemon.setMetLevel(wantedRow->levelMin);
            // The donor Pokemon may have hatched from an egg, and an egg location routes the whole
            // check down the egg path before the sweep is ever reached -- so the pose has to clear it.
            pokemon.setEggLocation(0);
            pokemon.setEgg(false);
        }
        else if (pokemon.hasEggData())
        {
            pokemon.setEgg(false);                       // a gift egg that hatched, still carrying its origin
            pokemon.setEggLocation(wantedRow->eggLocation);
        }
        else
        {
            pokemon.setEgg(true);                        // Gen 3: only an unhatched egg can name its gift
            pokemon.setEggLocation(0);
            pokemon.setMetLocation(wantedRow->location);
            pokemon.setMetLevel(wantedRow->levelMin);
        }

        const Legality::Report report = Legality::analyze(pokemon, group);

        pokemon.setSpecies(originalSpecies);
        pokemon.setForm(originalForm);
        pokemon.setMetLocation(originalMetLocation);
        pokemon.setMetLevel(originalMetLevel);
        pokemon.setBall(originalBall);
        pokemon.setFatefulEncounter(originalFateful);
        pokemon.setEggLocation(originalEggLocation);
        pokemon.setEgg(originalIsEgg);

        for (const auto &issue : report.issues)
        {
            if (issue.text.find("Mystery Gift distribution") != std::string::npos)
            {
                ++g_eventPassCount;
                return;
            }
        }
        ++g_eventFailCount;
        std::printf("        EVENT CHECK FAILED: %s -- species %u form %u at location %u level %u (shiny=%u ball=%u flawless=%u gender=%u nature=%u) did not match its own distribution row\n", label, wantedRow->species, wantedRow->form, wantedRow->location, wantedRow->levelMin, wantedRow->shiny, wantedRow->fixedBall, wantedRow->flawlessIVs, wantedRow->gender, wantedRow->nature);
        for (const auto &issue : report.issues)
        {
            std::printf("            said: %s\n", issue.text.c_str());
        }
    }


    // THE POSITIVE BITE CHECK, and the one that catches a mis-parsed slot table.
    //
    // A wrong header length or stride does not fail to build and does not fail the negative check --
    // it produces a table full of plausible-looking rows that simply never match anything real, and a
    // sweep of a save full of foreign Pokemon can hide that behind findings that look explainable. So
    // pose a Pokemon as one of the game's OWN wild rows, at its own location and level, and require a
    // match. Every generation added to the table has to pass this before its data is trusted.
    void wildBiteCheck(const char *label, ::Pokemon::Pokemon &pokemon, Enums::GameVersion group)
    {
        static_cast<void>(group);
        // THE TABLE IS THE DONOR'S, NOT THE SAVE'S. Layer 3 routes on the Pokemon's ORIGIN version, so
        // posing an X-origin Pokemon sitting in an Omega Ruby box at an ORAS slot asks the ORAS table
        // for a row the analyzer will never consult -- and fails for that reason alone.
        // A PK1/PK2 records no origin at all, so it falls back to its own group with every
        // version counting -- the same fallback checkEncounter applies.
        const bool noOrigin = !pokemon.hasOriginGame();
        const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
        const Legality::EncounterTable *table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
        if (!table)
        {
            ++g_wildSkipCount;
            return;
        }

        // THE ROW HAS TO BELONG TO THE DONOR'S OWN VERSION TOO. Half of every paired generation is
        // version-exclusive -- Caterpie is HeartGold's and Weedle SoulSilver's -- so posing a
        // SoulSilver Pokemon at a HeartGold-only slot fails for a reason that has nothing to do with
        // whether the table is right, and reads exactly like the failure this check exists to catch.
        const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
        if (donorVersionBit == 0)
        {
            ++g_wildSkipCount;
            return;
        }

        const Legality::EncounterRow *wantedRow = nullptr;
        for (uint32_t rowIndex = 0; rowIndex < table->rowCount; ++rowIndex)
        {
            const Legality::EncounterRow &row = table->rows[rowIndex];
            // A row with a region sentinel for a location, a fixed form or an egg flag is matched
            // through a path this pose does not set up; an ordinary grass slot is the subject here.
            if (row.kind != Legality::ENCOUNTER_KIND_WILD)
            {
                continue;
            }
            if ((row.versions & donorVersionBit) == 0)
            {
                continue;
            }
            // The catch-combo flag only widens the level band, which a pose at levelMin never reaches.
            if (row.location >= 0xFFF0 || (row.flags & ~Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0)
            {
                continue;
            }
            if (row.form != 0)
            {
                continue;
            }
            wantedRow = &row;
            break;
        }
        if (!wantedRow)
        {
            ++g_wildSkipCount;
            return;
        }

        const uint16_t originalSpecies = pokemon.speciesID();
        const uint8_t originalForm = pokemon.form();
        const uint16_t originalMetLocation = pokemon.metLocation();
        const uint8_t originalMetLevel = pokemon.metLevel();
        const uint16_t originalEggLocation = pokemon.eggLocation();
        const bool originalIsEgg = pokemon.isEgg();
        const bool originalFateful = pokemon.isFatefulEncounter();

        pokemon.setSpecies(wantedRow->species);
        pokemon.setForm(0);
        pokemon.setMetLocation(wantedRow->location);
        pokemon.setMetLevel(wantedRow->levelMin);
        pokemon.setEggLocation(0);
        pokemon.setEgg(false);
        pokemon.setFatefulEncounter(false);
        // A format with no met data is judged on its CURRENT level against the row's floor, so a donor
        // below that floor would fail the pose for a reason the table has nothing to do with.
        const uint8_t originalLevel = pokemon.level();
        if (!pokemon.hasMetData() && pokemon.level() < wantedRow->levelMin)
        {
            pokemon.setLevel(wantedRow->levelMin);
        }

        const Legality::Report report = Legality::analyze(pokemon, group);
        // What the pose ACTUALLY produced, captured before the fields are put back -- a setter the
        // format quietly ignores would otherwise look like a table failure.
        const uint16_t posedSpecies = pokemon.speciesID();
        const uint16_t posedMetLocation = pokemon.metLocation();
        const uint8_t posedMetLevel = pokemon.metLevel();
        const uint8_t posedOrigin = pokemon.originGame();

        pokemon.setSpecies(originalSpecies);
        pokemon.setForm(originalForm);
        pokemon.setMetLocation(originalMetLocation);
        pokemon.setMetLevel(originalMetLevel);
        pokemon.setEggLocation(originalEggLocation);
        pokemon.setEgg(originalIsEgg);
        pokemon.setFatefulEncounter(originalFateful);
        if (pokemon.level() != originalLevel)
        {
            pokemon.setLevel(originalLevel);
        }

        for (const auto &issue : report.issues)
        {
            if (issue.text.rfind("Matches a", 0) == 0)
            {
                ++g_wildPassCount;
                return;
            }
        }
        ++g_wildFailCount;
        std::printf("        WILD CHECK FAILED: %s -- species %u at location %u level %u did not match its own wild row (posed as species %u at %u level %u, origin %u)\n", label, wantedRow->species, wantedRow->location, wantedRow->levelMin, posedSpecies, posedMetLocation, posedMetLevel, posedOrigin);
        for (const auto &issue : report.issues)
        {
            std::printf("            said: %s\n", issue.text.c_str());
        }
    }

    // LET'S GO'S CATCH COMBO SPAWNS A SLOT ONE LEVEL ABOVE ITS RANGE, and PKHeX matches every Let's Go wild
    // slot up to LevelMax + 1 (EncounterSlot7b.CatchComboBonus). Read as a plain span, that level was
    // reported as "no encounter is met at level N" on ordinary catches. So pose the donor at a flagged
    // row's max + 1 and require a match, then at max + 2 and require the level finding -- quoting the
    // widened band -- so the allowance is exactly one level and not an open door.
    //
    // The row is chosen so that nothing ELSE can explain the max + 2 pose: its species has no
    // pre-evolution (an ancestor's row at the same place could match it), and no other row for that
    // species at that place reaches max + 2. A game whose table has no flagged rows is not counted.
    void catchComboCheck(const char *label, ::Pokemon::Pokemon &pokemon, Enums::GameVersion group)
    {
        if (!pokemon.hasOriginGame() || !pokemon.hasMetData())
        {
            return;
        }
        const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
        const Legality::EncounterTable *table = Legality::getEncounterTable(donorVersion);
        const uint8_t donorVersionBit = Legality::encounterVersionBit(donorVersion);
        if (!table || donorVersionBit == 0)
        {
            return;
        }
        const Enums::GameVersion donorGroup = Enums::getGameGroup(donorVersion);

        auto highestLevelOf = [](const Legality::EncounterRow &row) { return static_cast<int>(row.levelMax) + ((row.flags & Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0 ? 1 : 0); };
        const Legality::EncounterRow *comboRow = nullptr;
        bool tableHasComboRows = false;
        for (uint32_t rowIndex = 0; rowIndex < table->rowCount && !comboRow; ++rowIndex)
        {
            const Legality::EncounterRow &row = table->rows[rowIndex];
            if ((row.flags & Legality::ENCOUNTER_FLAG_CATCH_COMBO) == 0)
            {
                continue;
            }
            tableHasComboRows = true;
            if (row.kind != Legality::ENCOUNTER_KIND_WILD || (row.versions & donorVersionBit) == 0)
            {
                continue;
            }
            if (row.location >= 0xFFF0 || row.form != 0 || row.levelMax + 2 > 100)
            {
                continue;
            }
            uint16_t preEvolutionSpecies = 0;
            uint8_t preEvolutionForm = 0;
            if (::Pokemon::getPreEvolution(donorGroup, row.species, 0, preEvolutionSpecies, preEvolutionForm))
            {
                continue;
            }
            bool reachedElsewhere = false;
            for (uint16_t otherIndex = table->speciesIndex[row.species]; otherIndex < table->speciesIndex[row.species + 1]; ++otherIndex)
            {
                const Legality::EncounterRow &otherRow = table->rows[otherIndex];
                if (&otherRow == &row || otherRow.location != row.location || (otherRow.versions & donorVersionBit) == 0)
                {
                    continue;
                }
                reachedElsewhere = reachedElsewhere || highestLevelOf(otherRow) >= row.levelMax + 2;
            }
            if (!reachedElsewhere)
            {
                comboRow = &row;
            }
        }
        if (!tableHasComboRows)
        {
            return;
        }
        if (!comboRow)
        {
            ++g_catchComboFailCount;
            std::printf("        CATCH COMBO CHECK FAILED: %s -- the table has catch-combo rows but none could be posed\n", label);
            return;
        }

        const uint16_t originalSpecies = pokemon.speciesID();
        const uint8_t originalForm = pokemon.form();
        const uint16_t originalMetLocation = pokemon.metLocation();
        const uint8_t originalMetLevel = pokemon.metLevel();
        const uint16_t originalEggLocation = pokemon.eggLocation();
        const bool originalIsEgg = pokemon.isEgg();
        const bool originalFateful = pokemon.isFatefulEncounter();
        const uint8_t originalLevel = pokemon.level();

        auto analyzeAt = [&](uint8_t posedMetLevel)
        {
            pokemon.setSpecies(comboRow->species);
            pokemon.setForm(0);
            pokemon.setMetLocation(comboRow->location);
            pokemon.setMetLevel(posedMetLevel);
            pokemon.setEggLocation(0);
            pokemon.setEgg(false);
            pokemon.setFatefulEncounter(false);
            // A met level above the current one is its own finding, which is not the subject here.
            if (pokemon.level() < posedMetLevel)
            {
                pokemon.setLevel(posedMetLevel);
            }
            return Legality::analyze(pokemon, group);
        };
        const uint8_t comboLevel = static_cast<uint8_t>(comboRow->levelMax + 1);
        const Legality::Report comboReport = analyzeAt(comboLevel);
        const Legality::Report beyondReport = analyzeAt(static_cast<uint8_t>(comboRow->levelMax + 2));

        pokemon.setSpecies(originalSpecies);
        pokemon.setForm(originalForm);
        pokemon.setMetLocation(originalMetLocation);
        pokemon.setMetLevel(originalMetLevel);
        pokemon.setEggLocation(originalEggLocation);
        pokemon.setEgg(originalIsEgg);
        pokemon.setFatefulEncounter(originalFateful);
        if (pokemon.level() != originalLevel)
        {
            pokemon.setLevel(originalLevel);
        }

        bool comboMatched = false;
        for (const auto &issue : comboReport.issues)
        {
            comboMatched = comboMatched || issue.text.rfind("Matches a", 0) == 0;
        }
        const std::string widenedBand = "(nearest is " + std::to_string(comboRow->levelMin) + "-" + std::to_string(comboLevel) + ")";
        bool beyondFlagged = false;
        for (const auto &issue : beyondReport.issues)
        {
            beyondFlagged = beyondFlagged || (issue.text.find("is met at level") != std::string::npos && issue.text.find(widenedBand) != std::string::npos);
        }
        if (comboMatched && beyondFlagged)
        {
            ++g_catchComboPassCount;
            return;
        }
        ++g_catchComboFailCount;
        std::printf("        CATCH COMBO CHECK FAILED: %s -- species %u at location %u, slot %u-%u: max+1 %s, max+2 %s\n", label, comboRow->species, comboRow->location, comboRow->levelMin, comboRow->levelMax, comboMatched ? "matched" : "DID NOT MATCH", beyondFlagged ? "flagged" : ("was NOT flagged with " + widenedBand).c_str());
        for (const auto &issue : comboReport.issues)
        {
            std::printf("            max+1 said: %s\n", issue.text.c_str());
        }
        for (const auto &issue : beyondReport.issues)
        {
            std::printf("            max+2 said: %s\n", issue.text.c_str());
        }
    }

    // THE REPORTED CASE, BUILT FROM NOTHING. A Horsea caught on Route 24 in FireRed, edited into a
    // Seadra, was told *"Route 24 is not a place Seadra can be encountered in FireRed"*. Every word of
    // that is true and none of it is the problem: Horsea IS fished on Route 24, and what a Seadra below
    // level 32 cannot have done is evolve. The checker had the right answer and reported the wrong one,
    // because a dead end (Seadra's own rows, at seventeen other places) outranked the explanation.
    //
    // Needs no save, so it runs on every invocation rather than only where a FireRed file happens to
    // be. Both directions, because only the pair is worth anything: one Seadra too low to have evolved
    // and one comfortably past it, which must stay silent.
    void reportedSeadraCheck()
    {
        constexpr uint16_t HORSEA = 116;
        constexpr uint16_t SEADRA = 117;
        constexpr uint16_t ROUTE_24 = 124;      // Gen 3's own location id, not Gen 2's 56
        constexpr uint8_t FIRERED = static_cast<uint8_t>(Enums::GameVersion::FR);

        const Legality::EncounterTable *table = Legality::getEncounterTable(Enums::GameVersion::FR);
        const uint8_t versionBit = Legality::encounterVersionBit(Enums::GameVersion::FR);
        if (!table || versionBit == 0)
        {
            std::printf("        SEADRA CHECK: no FireRed encounter table to ask\n");
            ++g_seadraFailCount;
            return;
        }
        // The pose is only decisive while the tables still say what it assumes, so say so rather than
        // passing quietly if an upstream correction moves either species.
        auto isCaughtAt = [&](uint16_t species, uint16_t location)
        {
            for (uint16_t rowIndex = table->speciesIndex[species]; rowIndex < table->speciesIndex[species + 1]; ++rowIndex)
            {
                const Legality::EncounterRow &row = table->rows[rowIndex];
                if (row.location == location && (row.versions & versionBit) != 0)
                {
                    return true;
                }
            }
            return false;
        };
        if (!isCaughtAt(HORSEA, ROUTE_24) || isCaughtAt(SEADRA, ROUTE_24))
        {
            std::printf("        SEADRA CHECK: the tables no longer describe the reported case (horsea@24=%d seadra@24=%d)\n", isCaughtAt(HORSEA, ROUTE_24) ? 1 : 0, isCaughtAt(SEADRA, ROUTE_24) ? 1 : 0);
            ++g_seadraFailCount;
            return;
        }

        std::vector<std::byte> record(Trainer::Bank::recordSizeFor(Enums::GameVersion::FRLG), std::byte{0});
        auto pokemon = Trainer::Bank::makePokemon(Enums::GameVersion::FRLG, record);
        if (!pokemon)
        {
            std::printf("        SEADRA CHECK: could not build a FireRed entity\n");
            ++g_seadraFailCount;
            return;
        }
        pokemon->setSpecies(SEADRA);
        pokemon->setForm(0);
        pokemon->setOriginGame(FIRERED);
        pokemon->setMetLocation(ROUTE_24);
        pokemon->setMetLevel(10);
        pokemon->setEgg(false);
        pokemon->setFatefulEncounter(false);

        auto saysAbout = [&](const char *needle)
        {
            const Legality::Report report = Legality::analyze(*pokemon, Enums::GameVersion::FRLG);
            for (const auto &issue : report.issues)
            {
                if (issue.text.find(needle) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        };

        // Too low to have evolved: the complaint must name the evolution, not the place.
        pokemon->setLevel(15);
        if (pokemon->level() != 15)
        {
            std::printf("        SEADRA CHECK: the entity would not hold the posed level\n");
            ++g_seadraFailCount;
            return;
        }
        if (saysAbout("is not a place"))
        {
            std::printf("        SEADRA CHECK FAILED: a level 15 Seadra met on Route 24 was blamed on the location, which is the one field the record has right\n");
            ++g_seadraFailCount;
        }
        else if (!saysAbout("would have to evolve from"))
        {
            std::printf("        SEADRA CHECK FAILED: a level 15 Seadra met on Route 24 was not explained as an evolution it cannot have made\n");
            ++g_seadraFailCount;
        }

        // Past the evolution level: Horsea's own row explains it, so Layer 3 must say nothing at all.
        pokemon->setLevel(40);
        if (pokemon->level() == 40 && (saysAbout("is not a place") || saysAbout("would have to evolve from")))
        {
            std::printf("        SEADRA CHECK FAILED: a level 40 Seadra met on Route 24 -- an ordinary evolved Horsea -- was reported as impossible\n");
            ++g_seadraFailCount;
        }
    }

    // THE EVOLUTION BITE CHECK: an ancestor's encounter does not explain a Pokemon that is too
    // low to have evolved into it.
    //
    // The level floor is the one Layer 3 constraint that is about the CURRENT level rather than the
    // met level, and it is the one a clean sweep is least likely to exercise: real saves are full of
    // Pokemon that comfortably clear their own evolution levels. So build the case deliberately --
    // take a wild row for some species, pose the donor as what that species EVOLVES INTO, at the
    // row's own location and met level, and set the current level one below the floor. The only
    // honest answer is a complaint.
    //
    // It is also the check that would catch the floor being read off the wrong edge: a chain whose
    // levels came from form 0 when the Pokemon is a regional variant matches here and nowhere else.
    void evolutionBiteCheck(const char *label, ::Pokemon::Pokemon &pokemon, Enums::GameVersion group)
    {
        const bool noOrigin = !pokemon.hasOriginGame();
        const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
        const Enums::GameVersion donorGroup = noOrigin ? group : Enums::getGameGroup(donorVersion);
        const Legality::EncounterTable *table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
        if (!table)
        {
            ++g_evolutionSkipCount;
            return;
        }
        const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
        if (donorVersionBit == 0)
        {
            ++g_evolutionSkipCount;
            return;
        }
        // THE OTHER GAME BOY GENERATION IS ASKED TOO. Layer 3 checks a Gen 1 or Gen 2 record against both
        // generations' tables, because the Time Capsule carried Pokemon both ways -- so "the evolution is
        // caught nowhere" has to hold in both, or the checker rightly explains the pose as a catch from the
        // other one: Gold and Crystal have a level 7 wild Butterfree.
        const Legality::EncounterTable *timeCapsuleTable = !noOrigin ? nullptr : group == Enums::GameVersion::RBY ? Legality::getEncounterTable(Enums::GameVersion::GSC) : group == Enums::GameVersion::GSC ? Legality::getEncounterTable(Enums::GameVersion::RBY) : nullptr;

        // TWO POSES, AND THE SECOND IS THE ONE THAT BITES.
        //
        // (a) The evolution has NO encounter of its own anywhere in the game. That keeps the first pose
        //     decisive: if the evolution had a row at the same place, the sweep would match it at chain
        //     position 0 where no floor applies -- correct behaviour, and it would make the check pass
        //     for the wrong reason.
        // (b) The evolution HAS rows, just none at the ancestor's place. That is the Seadra shape -- a
        //     Seadra met on Route 24, where Horsea is fished and Seadra is not -- and it is the case
        //     the first pose deliberately excludes. Its own rows failing on LOCATION must not outrank
        //     the ancestor's row failing on the evolution level, or the report blames Route 24, which
        //     is the one thing about the record that is right.
        struct EvolutionPose
        {
            const Legality::EncounterRow *row = nullptr;
            uint16_t evolvedSpecies = 0;
            uint8_t evolvedFloor = 0;
        };
        EvolutionPose nowhere;
        EvolutionPose elsewhere;
        for (uint32_t rowIndex = 0; rowIndex < table->rowCount && !(nowhere.row && elsewhere.row); ++rowIndex)
        {
            const Legality::EncounterRow &row = table->rows[rowIndex];
            if (row.kind != Legality::ENCOUNTER_KIND_WILD)
            {
                continue;
            }
            if ((row.versions & donorVersionBit) == 0)
            {
                continue;
            }
            if (row.location >= 0xFFF0 || (row.flags & ~Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0 || row.form != 0)
            {
                continue;
            }
            for (uint16_t candidateSpecies = 1; candidateSpecies + 1 < table->speciesIndexLength; ++candidateSpecies)
            {
                uint16_t fromSpecies = 0;
                uint8_t fromForm = 0;
                if (!::Pokemon::getPreEvolution(donorGroup, candidateSpecies, 0, fromSpecies, fromForm))
                {
                    continue;
                }
                if (fromSpecies != row.species || fromForm != 0)
                {
                    continue;
                }
                const uint8_t floorLevel = ::Pokemon::getEvolutionLevelFloor(donorGroup, candidateSpecies, 0, 1);
                if (floorLevel < 2)
                {
                    continue;                       // not level-gated: a stone or a trade
                }
                const uint16_t evolvedRowsBegin = table->speciesIndex[candidateSpecies];
                const uint16_t evolvedRowsEnd = table->speciesIndex[candidateSpecies + 1];
                if (evolvedRowsBegin == evolvedRowsEnd)
                {
                    const bool caughtThroughTimeCapsule = timeCapsuleTable != nullptr && candidateSpecies + 1 < timeCapsuleTable->speciesIndexLength && timeCapsuleTable->speciesIndex[candidateSpecies] != timeCapsuleTable->speciesIndex[candidateSpecies + 1];
                    if (!nowhere.row && !caughtThroughTimeCapsule)
                    {
                        nowhere = EvolutionPose{&row, candidateSpecies, floorLevel};
                    }
                    break;
                }
                // The evolution must not be catchable AT THIS PLACE in this version, or it explains
                // itself at chain position 0 and the pose proves nothing.
                bool evolvedIsHereToo = false;
                for (uint16_t evolvedRowIndex = evolvedRowsBegin; evolvedRowIndex < evolvedRowsEnd && !evolvedIsHereToo; ++evolvedRowIndex)
                {
                    const Legality::EncounterRow &evolvedRow = table->rows[evolvedRowIndex];
                    evolvedIsHereToo = (evolvedRow.location == row.location) && (evolvedRow.versions & donorVersionBit) != 0;
                }
                // A FORMAT WITH NO MET LOCATION CANNOT BE POSED THIS WAY. "Its evolution is catchable
                // somewhere else" is a statement about places, and Gen 1 and Gold/Silver record none --
                // the sweep compares no location at all there, so the evolution explains itself at
                // chain position 0 and the pose proves nothing. Same exclusion the bogus-location bite
                // check makes, for the same reason.
                if (!evolvedIsHereToo && !elsewhere.row && pokemon.hasMetData())
                {
                    elsewhere = EvolutionPose{&row, candidateSpecies, floorLevel};
                }
                break;
            }
        }
        if (!nowhere.row && !elsewhere.row)
        {
            ++g_evolutionSkipCount;
            return;
        }

        const uint16_t originalSpecies = pokemon.speciesID();
        const uint8_t originalForm = pokemon.form();
        const uint16_t originalMetLocation = pokemon.metLocation();
        const uint8_t originalMetLevel = pokemon.metLevel();
        const uint8_t originalLevel = pokemon.level();
        const uint16_t originalEggLocation = pokemon.eggLocation();
        const bool originalIsEgg = pokemon.isEgg();
        const bool originalFateful = pokemon.isFatefulEncounter();

        auto runPose = [&](const EvolutionPose &pose, const char *poseShape, int &passCount, int &failCount, int &skipCount)
        {
            if (!pose.row)
            {
                ++skipCount;
                return;
            }
            pokemon.setSpecies(pose.evolvedSpecies);
            pokemon.setForm(0);
            pokemon.setMetLocation(pose.row->location);
            pokemon.setMetLevel(pose.row->levelMin);
            pokemon.setEggLocation(0);
            pokemon.setEgg(false);
            pokemon.setFatefulEncounter(false);
            pokemon.setLevel(static_cast<uint8_t>(pose.evolvedFloor - 1));

            const Legality::Report report = Legality::analyze(pokemon, group);
            const uint8_t posedLevel = pokemon.level();

            pokemon.setSpecies(originalSpecies);
            pokemon.setForm(originalForm);
            pokemon.setMetLocation(originalMetLocation);
            pokemon.setMetLevel(originalMetLevel);
            pokemon.setEggLocation(originalEggLocation);
            pokemon.setEgg(originalIsEgg);
            pokemon.setFatefulEncounter(originalFateful);
            pokemon.setLevel(originalLevel);

            // A format that cannot store the pose cannot be asked the question. Gen 1 and Gen 2 derive
            // level from EXP and take setLevel, but a format that ignored it would leave the donor above
            // the floor and pass this for no reason at all.
            if (posedLevel >= pose.evolvedFloor)
            {
                ++skipCount;
                return;
            }

            // THE WORDING IS THE ASSERTION, not merely that something was reported. Both poses are
            // genuinely illegal, so any number of wrong explanations would still "flag" them -- and a
            // report naming the met location, which is the one field the pose leaves correct, is the
            // defect this check exists to catch.
            for (const auto &issue : report.issues)
            {
                if (issue.text.find("would have to evolve from") != std::string::npos)
                {
                    ++passCount;
                    return;
                }
            }
            ++failCount;
            std::printf("        EVOLUTION CHECK FAILED (%s): %s -- species %u posed at level %u, below the level %u it evolves from species %u at, was not explained as an evolution\n", poseShape, label, pose.evolvedSpecies, posedLevel, pose.evolvedFloor, pose.row->species);
            for (const auto &issue : report.issues)
            {
                std::printf("            said: %s\n", issue.text.c_str());
            }
        };

        runPose(nowhere, "evolution is caught nowhere", g_evolutionPassCount, g_evolutionFailCount, g_evolutionSkipCount);
        runPose(elsewhere, "evolution is caught elsewhere", g_evolutionElsewherePassCount, g_evolutionElsewhereFailCount, g_evolutionElsewhereSkipCount);
    }

    template <typename TrainerT>
    void sweepTrainer(const char *label, TrainerT &trainer)
    {
        Tally tally;
        const Enums::GameVersion group = trainer.getGameGroup();
        for (const auto &partyPokemon : trainer.party)
        {
            if (partyPokemon)
            {
                analyzeOne(*partyPokemon, group, tally);
            }
        }
        for (const auto &box : trainer.boxes)
        {
            for (const auto &boxPokemon : box)
            {
                if (boxPokemon)
                {
                    analyzeOne(*boxPokemon, group, tally);
                }
            }
        }

        std::printf("  %-20s %4d Pokemon | %4d clean | %4d matched an encounter | %3d illegal, %3d warnings\n", label, tally.pokemonCount, tally.cleanCount, tally.matchedCount, tally.invalidCount, tally.warningCount);
        for (const auto &shapeEntry : tally.findingCountByShape)
        {
            std::printf("        %4d x  %s\n", shapeEntry.second, shapeEntry.first.c_str());
        }
        for (const auto &noteEntry : tally.noteCountByText)
        {
            std::printf("        %4d -  %s\n", noteEntry.second, noteEntry.first.c_str());
        }
        const int unexplained = tally.pokemonCount - tally.matchedCount - tally.notedCount;
        if (unexplained > 0)
        {
            std::printf("        %4d ?  neither matched nor noted -- these are the hatched eggs, which take the egg path and get no match note\n", unexplained);
        }

        // The four pose checks need a DONOR, and it must not be an unhatched egg: an egg is routed
        // down the egg path before the encounter sweep is ever reached, so posing one as a wild slot
        // reports nothing at all and reads as a table failure. Prefer a party Pokemon, then any boxed
        // one -- a save whose whole party is eggs is not a reason to skip the checks.
        // A NATIVE donor is preferred over a foreign one, so each save exercises its OWN table rather
        // than whichever game its first Pokemon happened to come from. A box full of imports still
        // gets checked -- against the table its donor actually routes to.
        ::Pokemon::Pokemon *donor = nullptr;
        ::Pokemon::Pokemon *foreignDonor = nullptr;
        auto consider = [&](::Pokemon::Pokemon *candidate)
        {
            if (!candidate || candidate->speciesID() == 0 || candidate->isEgg())
            {
                return;
            }
            const Enums::GameVersion candidateGroup = Enums::getGameGroup(static_cast<Enums::GameVersion>(candidate->originGame()));
            if (candidateGroup == group)
            {
                if (!donor)
                {
                    donor = candidate;
                }
            }
            else if (!foreignDonor)
            {
                foreignDonor = candidate;
            }
        };
        for (auto &partyPokemon : trainer.party)
        {
            consider(partyPokemon.get());
        }
        for (auto &box : trainer.boxes)
        {
            for (auto &boxPokemon : box)
            {
                consider(boxPokemon.get());
            }
        }
        if (!donor)
        {
            donor = foreignDonor;
        }
        // THE LOCATION POSE NEEDS A DONOR THE LOCATION IS WHAT EXPLAINS. Any clean Pokemon is not enough:
        // a hatched one is clean through its egg, and once the pose clears the egg location a species its
        // game never offers can only say "no encounter produces it" -- true, and nothing to do with the
        // location. Sun's first Pokemon is exactly that Shinx. So it takes the first that is clean BECAUSE
        // it matched an encounter, and falls back to the ordinary donor only where no Pokemon did.
        ::Pokemon::Pokemon *locationDonor = nullptr;
        auto considerLocationDonor = [&](::Pokemon::Pokemon *candidate)
        {
            if (locationDonor || !candidate || candidate->speciesID() == 0 || candidate->isEgg() || !candidate->hasMetData())
            {
                return;
            }
            const Legality::Report report = Legality::analyze(*candidate, group);
            bool matched = false;
            for (const auto &issue : report.issues)
            {
                if (issue.severity != Legality::Severity::Info)
                {
                    return;
                }
                matched = matched || issue.text.rfind("Matches a", 0) == 0;
            }
            if (matched)
            {
                locationDonor = candidate;
            }
        };
        for (auto &partyPokemon : trainer.party)
        {
            considerLocationDonor(partyPokemon.get());
        }
        for (auto &box : trainer.boxes)
        {
            for (auto &boxPokemon : box)
            {
                considerLocationDonor(boxPokemon.get());
            }
        }
        if (donor)
        {
            biteCheck(label, locationDonor != nullptr ? *locationDonor : *donor, group);
            eventBiteCheck(label, *donor, group);
            wildBiteCheck(label, *donor, group);
            catchComboCheck(label, *donor, group);
            evolutionBiteCheck(label, *donor, group);
        }
    }

    void sweepFRLG(const std::string &path)
    {
        const std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", "FireRed", SaveTree::missing(g_saveRoot, "FireRed").c_str());
            return;
        }
        Trainer::Trainer3FRLG trainer(saveBytes, "FireRed_e.sav");
        sweepTrainer("FireRed/LeafGreen", trainer);
    }

    // The LOOSE-FILE saves, which are handed their bytes rather than a decrypted block list. Every
    // generation whose encounter tables exist needs a real save swept through it: a wrong slot stride
    // or a version bit in the wrong place does not fail to build, it just reports a boxful of
    // perfectly ordinary Pokemon as impossible.
    template <typename TrainerT>
    void sweepLooseFile(const char *label, const char *title, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, title).c_str());
            return;
        }
        // AN EMULATOR CLOCK FOOTER IS TRIMMED FIRST, as Save::openExternalSave trims it. Gold, Silver and
        // Crystal saves are commonly 0x8010 bytes, and handed over untrimmed they parsed as EMPTY -- so
        // this gate swept "0 Pokemon" for every Gen 2 save and never saw a Gen 2 finding. The rule only
        // ever trims down to 0x8000, 0x10000 or 0x20000, so an exact-size save passes through untouched.
        Save::stripRtcFooter(saveBytes);
        TrainerT trainer(std::move(saveBytes), std::string(title));
        sweepTrainer(label, trainer);
    }

    void sweepLetsGo(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.size() < Trainer::SAVE_SIZE7_LGPE)
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        std::vector<uint8_t> saveArea(saveBytes.begin(), saveBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
        Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(saveArea));
        sweepTrainer(label, trainer);
    }

    void sweepBDSP(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        Trainer::Trainer8BDSP trainer(std::move(saveBytes));
        sweepTrainer(label, trainer);
    }

    template <typename TrainerT>
    void sweepSCBlock(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        std::vector<Save::Block> blocks = Encryption::decrypt(saveBytes.data(), saveBytes.size());
        TrainerT trainer(std::move(blocks));
        sweepTrainer(label, trainer);
    }
}

int main(int argc, char **argv)
{
    std::string saveRoot;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        if (argument == "--verbose" || argument == "-v")
        {
            g_verbose = true;
        }
        else
        {
            saveRoot = argument;
        }
    }
    if (saveRoot.empty())
    {
        std::printf("usage: legality <save-root> [--verbose]\n  Expects, relative to <save-root>, any of:\n  One folder per TITLE, any filename inside, named as the self test names the\n  title (case, spaces and a leading \"Pokemon\" aside) -- Red/  Silver/  FireRed/\n  Emerald/  HeartGold/  White 2/  Alpha Sapphire/  Ultra Moon/  LetsGoEevee/\n  Sword/  ShiningPearl/  Violet/  Legends Z-A/  Legends Arceus/  and the rest.\n");
        return 2;
    }
    g_saveRoot = saveRoot;

    // Runs first, and needs no save: it is the case that was reported, reproduced from the tables
    // alone, so it is asked on every invocation rather than only where a FireRed file turns up.
    reportedSeadraCheck();

    std::printf("PKSE legality harness\nReal saves are (almost) all legal Pokemon, so every finding here is a claim the\nchecker has to earn. Findings are grouped by shape; counts are what to watch.\n\n");

    // Found by TITLE FOLDER; see SaveTree.h.
    // EVERY TITLE FOLDER THE SELF TEST KNOWS, not a sample of them. A gate that sweeps one save per
    // generation passed while half the saves it had were never opened -- Silver, Yellow, both Let's
    // Go games and Sun/Moon among them, each with findings the console then reported.
    sweepLooseFile<Trainer::Trainer1RBY>("Red", "Red", SaveTree::red(saveRoot));
    sweepLooseFile<Trainer::Trainer1RBY>("Blue", "Blue", SaveTree::blue(saveRoot));
    sweepLooseFile<Trainer::Trainer1RBY>("Yellow", "Yellow", SaveTree::forTitle(saveRoot, "Yellow"));
    sweepLooseFile<Trainer::Trainer2GSC>("Gold", "Gold", SaveTree::forTitle(saveRoot, "Gold"));
    sweepLooseFile<Trainer::Trainer2GSC>("Silver", "Silver", SaveTree::forTitle(saveRoot, "Silver"));
    sweepLooseFile<Trainer::Trainer2GSC>("Crystal", "Crystal", SaveTree::forTitle(saveRoot, "Crystal"));
    sweepFRLG(SaveTree::fireRed(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Ruby", "Ruby", SaveTree::ruby(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Sapphire", "Sapphire", SaveTree::sapphire(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Emerald", "Emerald", SaveTree::emerald(saveRoot));
    sweepLooseFile<Trainer::Trainer4DP>("Diamond", "Diamond", SaveTree::forTitle(saveRoot, "Diamond"));
    sweepLooseFile<Trainer::Trainer4DP>("Pearl", "Pearl", SaveTree::forTitle(saveRoot, "Pearl"));
    sweepLooseFile<Trainer::Trainer4PT>("Platinum", "Platinum", SaveTree::forTitle(saveRoot, "Platinum"));
    sweepLooseFile<Trainer::Trainer4HGSS>("HeartGold", "HeartGold", SaveTree::forTitle(saveRoot, "HeartGold"));
    sweepLooseFile<Trainer::Trainer4HGSS>("SoulSilver", "SoulSilver", SaveTree::forTitle(saveRoot, "SoulSilver"));
    sweepLooseFile<Trainer::Trainer5BW>("Black", "Black", SaveTree::forTitle(saveRoot, "Black"));
    sweepLooseFile<Trainer::Trainer5BW>("White", "White", SaveTree::forTitle(saveRoot, "White"));
    sweepLooseFile<Trainer::Trainer5B2W2>("Black 2", "Black 2", SaveTree::forTitle(saveRoot, "Black 2"));
    sweepLooseFile<Trainer::Trainer5B2W2>("White 2", "White 2", SaveTree::forTitle(saveRoot, "White 2"));
    sweepLooseFile<Trainer::Trainer6XY>("X", "X", SaveTree::forTitle(saveRoot, "X"));
    sweepLooseFile<Trainer::Trainer6XY>("Y", "Y", SaveTree::forTitle(saveRoot, "Y"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Omega Ruby", "Omega Ruby", SaveTree::forTitle(saveRoot, "Omega Ruby"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Alpha Sapphire", "Alpha Sapphire", SaveTree::forTitle(saveRoot, "Alpha Sapphire"));
    sweepLooseFile<Trainer::Trainer7SM>("Sun", "Sun", SaveTree::forTitle(saveRoot, "Sun"));
    sweepLooseFile<Trainer::Trainer7SM>("Moon", "Moon", SaveTree::forTitle(saveRoot, "Moon"));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Sun", "Ultra Sun", SaveTree::ultraSun(saveRoot));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Moon", "Ultra Moon", SaveTree::forTitle(saveRoot, "Ultra Moon"));
    sweepLetsGo("Let's Go Pikachu", SaveTree::letsGoPikachu(saveRoot));
    sweepLetsGo("Let's Go Eevee", SaveTree::forTitle(saveRoot, "Let's Go Eevee"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Sword", SaveTree::forTitle(saveRoot, "Sword"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Shield", SaveTree::shield(saveRoot));
    sweepBDSP("Brilliant Diamond", SaveTree::forTitle(saveRoot, "Brilliant Diamond"));
    sweepBDSP("Shining Pearl", SaveTree::forTitle(saveRoot, "Shining Pearl"));
    sweepSCBlock<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(saveRoot));
    sweepSCBlock<Trainer::Trainer9SV>("Violet", SaveTree::violet(saveRoot));
    sweepSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(saveRoot));

    std::printf("\nwild check:  %d game(s) matched a Pokemon posed at one of their own wild slots", g_wildPassCount);
    if (g_wildFailCount)
    {
        std::printf(", %d DID NOT", g_wildFailCount);
    }
    if (g_wildSkipCount)
    {
        std::printf(" (%d had no plain wild row to pose at)", g_wildSkipCount);
    }
    std::printf("\ncatch combo: %d game(s) matched a Let's Go slot one level above its range and rejected two", g_catchComboPassCount);
    if (g_catchComboFailCount)
    {
        std::printf(", %d DID NOT", g_catchComboFailCount);
    }
    std::printf("\nbite check:  %d game(s) correctly rejected a bogus met location", g_bitePassCount);
    if (g_biteFailCount)
    {
        std::printf(", %d DID NOT", g_biteFailCount);
    }
    if (g_biteSkipCount)
    {
        std::printf(" (%d record no met location at all)", g_biteSkipCount);
    }
    std::printf("\nevent check: %d game(s) matched a Pokemon posed as their own distribution row", g_eventPassCount);
    if (g_eventFailCount)
    {
        std::printf(", %d DID NOT", g_eventFailCount);
    }
    if (g_eventNoRowsCount)
    {
        std::printf(" (%d have no distributions at all)", g_eventNoRowsCount);
    }
    if (g_eventSkipCount)
    {
        std::printf("\n             %d game(s) have distributions that cannot be posed: %s", g_eventSkipCount, g_eventSkipNames.c_str());
    }
    std::printf("\nevo check:   %d game(s) rejected a Pokemon posed below the level it evolves at", g_evolutionPassCount);
    if (g_evolutionFailCount)
    {
        std::printf(", %d DID NOT", g_evolutionFailCount);
    }
    if (g_evolutionSkipCount)
    {
        std::printf(" (%d had no level-gated evolution to pose)", g_evolutionSkipCount);
    }
    std::printf("\nevo check:   %d game(s) said the same of one whose evolution IS catchable elsewhere", g_evolutionElsewherePassCount);
    if (g_evolutionElsewhereFailCount)
    {
        std::printf(", %d BLAMED SOMETHING ELSE", g_evolutionElsewhereFailCount);
    }
    if (g_evolutionElsewhereSkipCount)
    {
        std::printf(" (%d had no such pair to pose)", g_evolutionElsewhereSkipCount);
    }
    std::printf("\nseadra check: the reported case -- a Seadra met where only Horsea is caught -- is %s", g_seadraFailCount == 0 ? "explained as an evolution, both directions" : "WRONG");
    std::printf("\n\nThe sweep reports; it does not pass or fail. Judgement is the point:\ndecide per shape whether the checker is right about these saves. The seven\nbite checks DO fail, because a checker that never fires is not a clean result.\n");
    return (g_biteFailCount == 0 && g_eventFailCount == 0 && g_wildFailCount == 0 && g_catchComboFailCount == 0 && g_evolutionFailCount == 0 && g_evolutionElsewhereFailCount == 0 && g_seadraFailCount == 0) ? 0 : 1;
}
