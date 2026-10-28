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
namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
    void logEventToFile(const std::string&) {}
    // Bank.cpp comes in with the rest of src/Trainer and its SD-card paths reach this. Nothing in
    // this harness opens a bank, so returning "no such file" is both honest and the only outcome a
    // host run could have.
    uint8_t* readAllBytes(const char*, size_t* outSize) { if (outSize) *outSize = 0; return nullptr; }
}

namespace {

bool g_verbose = false;

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

/**
 * Findings are histogrammed by SHAPE, not by exact text: "Route 3 (12) is not a place Nickit can
 * be encountered" and the same sentence about Wooloo are one bug, not two. Trimming at the first
 * digit or quote collapses the species and location out of the message.
 */
std::string shapeOf(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            std::string head = text.substr(0, i);
            while (!head.empty() && head.back() == ' ') head.pop_back();
            return head + " ...";
        }
    }
    return text;
}

struct Tally {
    int total = 0;
    int clean = 0;                 // no Warning/Invalid at all
    int matched = 0;               // Layer 3 found an encounter
    int invalid = 0;
    int warning = 0;
    std::map<std::string, int> byShape;
    // Info notes, grouped the same way. "Clean" is not the same as "checked": Layer 3
    // abstains for origins it has no tables for, and says so in a note. Counting those
    // is the only way to see how much of a save the check actually reached.
    std::map<std::string, int> notes;
    int noted = 0;                 // Pokemon carrying at least one abstention note
};

void analyzeOne(const ::Pokemon::Pokemon& pk, Enums::GameVersion group, Tally& t) {
    if (pk.speciesID() == 0) return;
    ++t.total;
    const Legality::Report rep = Legality::analyze(pk, group);
    if (rep.ok()) ++t.clean;
    bool printed = false;
    bool noted = false;
    for (const auto& is : rep.issues) {
        if (is.severity == Legality::Severity::Info) {
            if (is.text.rfind("Matches a", 0) == 0) ++t.matched;
            // Notes keep their full text where findings get shaped: they only vary by the
            // origin id, and that id is the interesting part -- it says WHICH games a save
            // is full of that Layer 3 cannot reach.
            else { t.notes[is.text]++; noted = true; }
            continue;
        }
        if (is.severity == Legality::Severity::Invalid) ++t.invalid; else ++t.warning;
        t.byShape[shapeOf(is.text)]++;
        if (g_verbose) {
            if (!printed) {
                std::printf("    %-14s form %-2u  met %5u lv %-3u  origin %-3u\n", pk.species(), pk.form(), pk.metLocation(), pk.metLevel(), pk.originGame());
                printed = true;
            }
            std::printf("        %s %s\n", is.severity == Legality::Severity::Invalid ? "[illegal]" : "[warning]", is.text.c_str());
        }
    }
    if (noted) ++t.noted;
}

int g_biteOk = 0, g_biteMiss = 0;
int g_wildOk = 0, g_wildMiss = 0, g_wildSkip = 0;
int g_catchComboOk = 0, g_catchComboMiss = 0;
int g_evoOk = 0, g_evoMiss = 0, g_evoSkip = 0;
int g_evoElsewhereOk = 0, g_evoElsewhereMiss = 0, g_evoElsewhereSkip = 0;
int g_seadraMiss = 0;

/**
 * Does the checker actually bite?
 *
 * A save full of legal Pokemon produces the same silence whether Layer 3 is working perfectly or
 * not running at all, so a clean sweep on its own proves nothing. This moves one real Pokemon to
 * a met location no game has (past every in-world table, below the 30000 trade/transfer bank) and
 * requires a complaint, then puts it back. Fail here and every "clean" line above is worthless.
 */
int g_biteSkip = 0;

void biteCheck(const char* label, ::Pokemon::Pokemon& pk, Enums::GameVersion group) {
    // A FORMAT WITH NO MET LOCATION CANNOT BE GIVEN A BOGUS ONE. Gen 1 has no such field and
    // Gold/Silver leave Gen 2's empty, so setMetLocation is a no-op there and the sweep does not
    // compare locations at all -- the Pokemon stays clean because the check is meaningless, not
    // because the table failed to bite.
    if (!pk.hasMetData()) { ++g_biteSkip; return; }
    const uint16_t original = pk.metLocation();
    // An EGG LOCATION short-circuits the whole encounter sweep -- checkEgg answers instead, and it
    // says nothing about the met location -- so a hatched donor would stay clean however bogus a
    // place it claims to have been met, and report that as the table failing to bite.
    const uint16_t originalEggLocation = pk.eggLocation();
    pk.setEggLocation(0);
    // A BOGUS LOCATION THE FORMAT CAN ACTUALLY HOLD. 900 fits a u16, but Gen 3 keeps a byte and Gen 2
    // seven bits, so 900 was stored as 132 and 4 -- real places, which made the pose a real claim
    // rather than an impossible one. The first candidate that reads back exactly is used; none of
    // them is a location any table has in the format that can hold it.
    for (const uint16_t candidate : {uint16_t{900}, uint16_t{250}, uint16_t{125}}) {
        pk.setMetLocation(candidate);
        if (pk.metLocation() == candidate) break;
    }
    const uint16_t bogusLocation = pk.metLocation();
    const Legality::Report rep = Legality::analyze(pk, group);
    pk.setMetLocation(original);
    pk.setEggLocation(originalEggLocation);

    bool flagged = false;
    for (const auto& is : rep.issues) {
        if (is.severity == Legality::Severity::Info) continue;
        if (is.text.find("can be encountered") != std::string::npos || is.text.find("egg can hatch") != std::string::npos) { flagged = true; break; }
    }
    if (flagged) { ++g_biteOk; return; }
    ++g_biteMiss;
    std::printf("        BITE CHECK FAILED: %s stayed clean with met location %u (%s)\n", pk.species(), bogusLocation, label);
}

int g_eventOk = 0, g_eventMiss = 0, g_eventSkip = 0, g_eventSkipNoRows = 0;
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
void eventBiteCheck(const char* label, ::Pokemon::Pokemon& pk, Enums::GameVersion group) {
    (void)group;
    // The donor's OWN table and version, for the reason wildBiteCheck gives: Layer 3 routes on the
    // Pokemon's origin, and half of every paired generation is version-exclusive. Gen 3's
    // distributions make the second half bite -- the 10th Anniversary Bulbasaur is Ruby-only, so
    // posing an Emerald Pokemon as it fails for a reason that says nothing about the table.
    // A PK1/PK2 records no origin at all, so it falls back to its own group with every
    // version counting -- the same fallback checkEncounter applies.
    const bool noOrigin = !pk.hasOriginGame();
    const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pk.originGame());
    const Legality::EncounterTable* table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
    const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
    if (!table || donorVersionBit == 0) { ++g_eventSkip; return; }

    const Legality::EncounterRow* want = nullptr;
    bool wantIsEgg = false;
    uint32_t distributionRows = 0;
    for (uint32_t i = 0; i < table->rowCount; ++i) {
        const Legality::EncounterRow& r = table->rows[i];
        if (r.kind != Legality::ENCOUNTER_KIND_EVENT) continue;
        if ((r.versions & donorVersionBit) == 0) continue;
        ++distributionRows;
        // A plain distribution poses most directly, so it is preferred; an egg row is posed below
        // when the game has nothing else -- FireRed, LeafGreen and Emerald have only eggs.
        if (r.flags & Legality::ENCOUNTER_FLAG_EGG) continue;
        // THE DONOR'S OWN SHININESS IS NOT PART OF THE POSE and cannot be set from here -- it is a
        // PID/id relationship, not a field. So a row that locks it has to agree with the donor
        // already, or the sweep reaches the row, matches everything else and reports the lock --
        // a correct finding about a pose this check had no business building.
        const bool donorIsShiny = pk.isShiny(pk.id32(), Trainer::getSpeciesName(pk.speciesID()));
        if (r.shiny == Legality::ENCOUNTER_SHINY_NEVER && donorIsShiny) continue;
        if (r.shiny == Legality::ENCOUNTER_SHINY_ALWAYS && !donorIsShiny) continue;
        want = &r;
        break;
    }
    if (!want) {
        // The egg fallback. What can name a distribution egg differs by format: one with an
        // egg-location field carries it after hatching, while Gen 3 has none and only an egg that
        // has NOT hatched can say where it came from -- see matchingGiftEgg.
        for (uint32_t i = 0; i < table->rowCount; ++i) {
            const Legality::EncounterRow& r = table->rows[i];
            if (r.kind != Legality::ENCOUNTER_KIND_EVENT) continue;
            if ((r.flags & Legality::ENCOUNTER_FLAG_EGG) == 0) continue;
            if ((r.versions & donorVersionBit) == 0) continue;
            if (pk.hasEggData() ? r.eggLocation == 0 : r.location == 0) continue;
            want = &r;
            wantIsEgg = true;
            break;
        }
    }
    if (!want) {
        if (distributionRows == 0) {
            ++g_eventSkipNoRows;   // nothing to pose as: this game has no distributions at all
        } else {
            ++g_eventSkip;
            g_eventSkipNames += (g_eventSkipNames.empty() ? "" : ", ") + std::string(label);
        }
        return;
    }

    const uint16_t oldSpecies = pk.speciesID();
    const uint8_t  oldForm    = pk.form();
    const uint16_t oldLoc     = pk.metLocation();
    const uint8_t  oldMetLv   = pk.metLevel();
    const uint8_t  oldBall    = pk.ball();
    const bool     oldFateful = pk.isFatefulEncounter();

    const uint16_t oldEggLoc = pk.eggLocation();
    const bool     oldIsEgg   = pk.isEgg();

    pk.setSpecies(want->species);
    if (want->form != Legality::ENCOUNTER_FORM_ANY) pk.setForm(want->form);
    if (want->fixedBall) pk.setBall(want->fixedBall);
    pk.setFatefulEncounter(true);
    if (!wantIsEgg) {
        pk.setMetLocation(want->location);
        pk.setMetLevel(want->levelMin);
        // The donor Pokemon may have hatched from an egg, and an egg location routes the whole
        // check down the egg path before the sweep is ever reached -- so the pose has to clear it.
        pk.setEggLocation(0);
        pk.setEgg(false);
    } else if (pk.hasEggData()) {
        pk.setEgg(false);                       // a gift egg that hatched, still carrying its origin
        pk.setEggLocation(want->eggLocation);
    } else {
        pk.setEgg(true);                        // Gen 3: only an unhatched egg can name its gift
        pk.setEggLocation(0);
        pk.setMetLocation(want->location);
        pk.setMetLevel(want->levelMin);
    }

    const Legality::Report rep = Legality::analyze(pk, group);

    pk.setSpecies(oldSpecies);
    pk.setForm(oldForm);
    pk.setMetLocation(oldLoc);
    pk.setMetLevel(oldMetLv);
    pk.setBall(oldBall);
    pk.setFatefulEncounter(oldFateful);
    pk.setEggLocation(oldEggLoc);
    pk.setEgg(oldIsEgg);

    for (const auto& is : rep.issues) {
        if (is.text.find("Mystery Gift distribution") != std::string::npos) { ++g_eventOk; return; }
    }
    ++g_eventMiss;
    std::printf("        EVENT CHECK FAILED: %s -- species %u form %u at location %u level %u (shiny=%u ball=%u flawless=%u gender=%u nature=%u) did not match its own distribution row\n", label, want->species, want->form, want->location, want->levelMin, want->shiny, want->fixedBall, want->flawlessIVs, want->gender, want->nature);
    for (const auto& is : rep.issues)
        std::printf("            said: %s\n", is.text.c_str());
}


// THE POSITIVE BITE CHECK, and the one that catches a mis-parsed slot table.
//
// A wrong header length or stride does not fail to build and does not fail the negative check --
// it produces a table full of plausible-looking rows that simply never match anything real, and a
// sweep of a save full of foreign Pokemon can hide that behind findings that look explainable. So
// pose a Pokemon as one of the game's OWN wild rows, at its own location and level, and require a
// match. Every generation added to the table has to pass this before its data is trusted.
void wildBiteCheck(const char* label, ::Pokemon::Pokemon& pk, Enums::GameVersion group) {
    (void)group;
    // THE TABLE IS THE DONOR'S, NOT THE SAVE'S. Layer 3 routes on the Pokemon's ORIGIN version, so
    // posing an X-origin Pokemon sitting in an Omega Ruby box at an ORAS slot asks the ORAS table
    // for a row the analyzer will never consult -- and fails for that reason alone.
    // A PK1/PK2 records no origin at all, so it falls back to its own group with every
    // version counting -- the same fallback checkEncounter applies.
    const bool noOrigin = !pk.hasOriginGame();
    const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pk.originGame());
    const Legality::EncounterTable* table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
    if (!table) { ++g_wildSkip; return; }

    // THE ROW HAS TO BELONG TO THE DONOR'S OWN VERSION TOO. Half of every paired generation is
    // version-exclusive -- Caterpie is HeartGold's and Weedle SoulSilver's -- so posing a
    // SoulSilver Pokemon at a HeartGold-only slot fails for a reason that has nothing to do with
    // whether the table is right, and reads exactly like the failure this check exists to catch.
    const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
    if (donorVersionBit == 0) { ++g_wildSkip; return; }

    const Legality::EncounterRow* want = nullptr;
    for (uint32_t i = 0; i < table->rowCount; ++i) {
        const Legality::EncounterRow& r = table->rows[i];
        // A row with a region sentinel for a location, a fixed form or an egg flag is matched
        // through a path this pose does not set up; an ordinary grass slot is the subject here.
        if (r.kind != Legality::ENCOUNTER_KIND_WILD) continue;
        if ((r.versions & donorVersionBit) == 0) continue;
        // The catch-combo flag only widens the level band, which a pose at levelMin never reaches.
        if (r.location >= 0xFFF0 || (r.flags & ~Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0) continue;
        if (r.form != 0) continue;
        want = &r;
        break;
    }
    if (!want) { ++g_wildSkip; return; }

    const uint16_t oldSpecies = pk.speciesID();
    const uint8_t  oldForm    = pk.form();
    const uint16_t oldLoc     = pk.metLocation();
    const uint8_t  oldMetLv   = pk.metLevel();
    const uint16_t oldEggLoc  = pk.eggLocation();
    const bool     oldIsEgg   = pk.isEgg();
    const bool     oldFateful = pk.isFatefulEncounter();

    pk.setSpecies(want->species);
    pk.setForm(0);
    pk.setMetLocation(want->location);
    pk.setMetLevel(want->levelMin);
    pk.setEggLocation(0);
    pk.setEgg(false);
    pk.setFatefulEncounter(false);
    // A format with no met data is judged on its CURRENT level against the row's floor, so a donor
    // below that floor would fail the pose for a reason the table has nothing to do with.
    const uint8_t oldLevel = pk.level();
    if (!pk.hasMetData() && pk.level() < want->levelMin) pk.setLevel(want->levelMin);

    const Legality::Report rep = Legality::analyze(pk, group);
    // What the pose ACTUALLY produced, captured before the fields are put back -- a setter the
    // format quietly ignores would otherwise look like a table failure.
    const uint16_t poseSpecies = pk.speciesID();
    const uint16_t poseLoc     = pk.metLocation();
    const uint8_t  poseMetLv   = pk.metLevel();
    const uint8_t  poseOrigin  = pk.originGame();

    pk.setSpecies(oldSpecies);
    pk.setForm(oldForm);
    pk.setMetLocation(oldLoc);
    pk.setMetLevel(oldMetLv);
    pk.setEggLocation(oldEggLoc);
    pk.setEgg(oldIsEgg);
    pk.setFatefulEncounter(oldFateful);
    if (pk.level() != oldLevel) pk.setLevel(oldLevel);

    for (const auto& is : rep.issues) {
        if (is.text.rfind("Matches a", 0) == 0) { ++g_wildOk; return; }
    }
    ++g_wildMiss;
    std::printf("        WILD CHECK FAILED: %s -- species %u at location %u level %u did not match its own wild row (posed as species %u at %u level %u, origin %u)\n", label, want->species, want->location, want->levelMin, poseSpecies, poseLoc, poseMetLv, poseOrigin);
    for (const auto& is : rep.issues)
        std::printf("            said: %s\n", is.text.c_str());
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
void catchComboCheck(const char* label, ::Pokemon::Pokemon& pokemon, Enums::GameVersion group) {
    if (!pokemon.hasOriginGame() || !pokemon.hasMetData()) return;
    const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
    const Legality::EncounterTable* table = Legality::getEncounterTable(donorVersion);
    const uint8_t donorVersionBit = Legality::encounterVersionBit(donorVersion);
    if (!table || donorVersionBit == 0) return;
    const Enums::GameVersion donorGroup = Enums::getGameGroup(donorVersion);

    auto highestLevelOf = [](const Legality::EncounterRow& row) {
        return static_cast<int>(row.levelMax) + ((row.flags & Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0 ? 1 : 0);
    };
    const Legality::EncounterRow* comboRow = nullptr;
    bool tableHasComboRows = false;
    for (uint32_t rowIndex = 0; rowIndex < table->rowCount && !comboRow; ++rowIndex) {
        const Legality::EncounterRow& row = table->rows[rowIndex];
        if ((row.flags & Legality::ENCOUNTER_FLAG_CATCH_COMBO) == 0) continue;
        tableHasComboRows = true;
        if (row.kind != Legality::ENCOUNTER_KIND_WILD || (row.versions & donorVersionBit) == 0) continue;
        if (row.location >= 0xFFF0 || row.form != 0 || row.levelMax + 2 > 100) continue;
        uint16_t preEvolutionSpecies = 0;
        uint8_t preEvolutionForm = 0;
        if (::Pokemon::getPreEvolution(donorGroup, row.species, 0, preEvolutionSpecies, preEvolutionForm)) continue;
        bool reachedElsewhere = false;
        for (uint16_t otherIndex = table->speciesIndex[row.species]; otherIndex < table->speciesIndex[row.species + 1]; ++otherIndex) {
            const Legality::EncounterRow& otherRow = table->rows[otherIndex];
            if (&otherRow == &row || otherRow.location != row.location || (otherRow.versions & donorVersionBit) == 0) continue;
            reachedElsewhere = reachedElsewhere || highestLevelOf(otherRow) >= row.levelMax + 2;
        }
        if (!reachedElsewhere) comboRow = &row;
    }
    if (!tableHasComboRows) return;
    if (!comboRow) {
        ++g_catchComboMiss;
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

    auto analyzeAt = [&](uint8_t posedMetLevel) {
        pokemon.setSpecies(comboRow->species);
        pokemon.setForm(0);
        pokemon.setMetLocation(comboRow->location);
        pokemon.setMetLevel(posedMetLevel);
        pokemon.setEggLocation(0);
        pokemon.setEgg(false);
        pokemon.setFatefulEncounter(false);
        // A met level above the current one is its own finding, which is not the subject here.
        if (pokemon.level() < posedMetLevel) pokemon.setLevel(posedMetLevel);
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
    if (pokemon.level() != originalLevel) pokemon.setLevel(originalLevel);

    bool comboMatched = false;
    for (const auto& issue : comboReport.issues) comboMatched = comboMatched || issue.text.rfind("Matches a", 0) == 0;
    const std::string widenedBand = "(nearest is " + std::to_string(comboRow->levelMin) + "-" + std::to_string(comboLevel) + ")";
    bool beyondFlagged = false;
    for (const auto& issue : beyondReport.issues) beyondFlagged = beyondFlagged || (issue.text.find("is met at level") != std::string::npos && issue.text.find(widenedBand) != std::string::npos);
    if (comboMatched && beyondFlagged) { ++g_catchComboOk; return; }
    ++g_catchComboMiss;
    std::printf("        CATCH COMBO CHECK FAILED: %s -- species %u at location %u, slot %u-%u: max+1 %s, max+2 %s\n", label, comboRow->species, comboRow->location, comboRow->levelMin, comboRow->levelMax, comboMatched ? "matched" : "DID NOT MATCH", beyondFlagged ? "flagged" : ("was NOT flagged with " + widenedBand).c_str());
    for (const auto& issue : comboReport.issues) std::printf("            max+1 said: %s\n", issue.text.c_str());
    for (const auto& issue : beyondReport.issues) std::printf("            max+2 said: %s\n", issue.text.c_str());
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
void reportedSeadraCheck() {
    constexpr uint16_t HORSEA = 116, SEADRA = 117;
    constexpr uint16_t ROUTE_24 = 124;      // Gen 3's own location id, not Gen 2's 56
    constexpr uint8_t  FIRERED = static_cast<uint8_t>(Enums::GameVersion::FR);

    const Legality::EncounterTable* table = Legality::getEncounterTable(Enums::GameVersion::FR);
    const uint8_t versionBit = Legality::encounterVersionBit(Enums::GameVersion::FR);
    if (!table || versionBit == 0) {
        std::printf("        SEADRA CHECK: no FireRed encounter table to ask\n");
        ++g_seadraMiss;
        return;
    }
    // The pose is only decisive while the tables still say what it assumes, so say so rather than
    // passing quietly if an upstream correction moves either species.
    auto isCaughtAt = [&](uint16_t species, uint16_t location) {
        for (uint16_t rowIndex = table->speciesIndex[species]; rowIndex < table->speciesIndex[species + 1]; ++rowIndex) {
            const Legality::EncounterRow& row = table->rows[rowIndex];
            if (row.location == location && (row.versions & versionBit) != 0) return true;
        }
        return false;
    };
    if (!isCaughtAt(HORSEA, ROUTE_24) || isCaughtAt(SEADRA, ROUTE_24)) {
        std::printf("        SEADRA CHECK: the tables no longer describe the reported case (horsea@24=%d seadra@24=%d)\n", isCaughtAt(HORSEA, ROUTE_24) ? 1 : 0, isCaughtAt(SEADRA, ROUTE_24) ? 1 : 0);
        ++g_seadraMiss;
        return;
    }

    std::vector<std::byte> record(Trainer::Bank::recordSizeFor(Enums::GameVersion::FRLG), std::byte{0});
    auto pk = Trainer::Bank::makePokemon(Enums::GameVersion::FRLG, record);
    if (!pk) {
        std::printf("        SEADRA CHECK: could not build a FireRed entity\n");
        ++g_seadraMiss;
        return;
    }
    pk->setSpecies(SEADRA);
    pk->setForm(0);
    pk->setOriginGame(FIRERED);
    pk->setMetLocation(ROUTE_24);
    pk->setMetLevel(10);
    pk->setEgg(false);
    pk->setFatefulEncounter(false);

    auto saysAbout = [&](const char* needle) {
        const Legality::Report rep = Legality::analyze(*pk, Enums::GameVersion::FRLG);
        for (const auto& is : rep.issues)
            if (is.text.find(needle) != std::string::npos) return true;
        return false;
    };

    // Too low to have evolved: the complaint must name the evolution, not the place.
    pk->setLevel(15);
    if (pk->level() != 15) {
        std::printf("        SEADRA CHECK: the entity would not hold the posed level\n");
        ++g_seadraMiss;
        return;
    }
    if (saysAbout("is not a place")) {
        std::printf("        SEADRA CHECK FAILED: a level 15 Seadra met on Route 24 was blamed on the location, which is the one field the record has right\n");
        ++g_seadraMiss;
    } else if (!saysAbout("would have to evolve from")) {
        std::printf("        SEADRA CHECK FAILED: a level 15 Seadra met on Route 24 was not explained as an evolution it cannot have made\n");
        ++g_seadraMiss;
    }

    // Past the evolution level: Horsea's own row explains it, so Layer 3 must say nothing at all.
    pk->setLevel(40);
    if (pk->level() == 40 && (saysAbout("is not a place") || saysAbout("would have to evolve from"))) {
        std::printf("        SEADRA CHECK FAILED: a level 40 Seadra met on Route 24 -- an ordinary evolved Horsea -- was reported as impossible\n");
        ++g_seadraMiss;
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
void evolutionBiteCheck(const char* label, ::Pokemon::Pokemon& pk, Enums::GameVersion group) {
    const bool noOrigin = !pk.hasOriginGame();
    const Enums::GameVersion donorVersion = static_cast<Enums::GameVersion>(pk.originGame());
    const Enums::GameVersion donorGroup = noOrigin ? group : Enums::getGameGroup(donorVersion);
    const Legality::EncounterTable* table = Legality::getEncounterTable(noOrigin ? group : donorVersion);
    if (!table) { ++g_evoSkip; return; }
    const uint8_t donorVersionBit = noOrigin ? 0xFF : Legality::encounterVersionBit(donorVersion);
    if (donorVersionBit == 0) { ++g_evoSkip; return; }
    // THE OTHER GAME BOY GENERATION IS ASKED TOO. Layer 3 checks a Gen 1 or Gen 2 record against both
    // generations' tables, because the Time Capsule carried Pokemon both ways -- so "the evolution is
    // caught nowhere" has to hold in both, or the checker rightly explains the pose as a catch from the
    // other one: Gold and Crystal have a level 7 wild Butterfree.
    const Legality::EncounterTable* timeCapsuleTable = !noOrigin ? nullptr : group == Enums::GameVersion::RBY ? Legality::getEncounterTable(Enums::GameVersion::GSC) : group == Enums::GameVersion::GSC ? Legality::getEncounterTable(Enums::GameVersion::RBY) : nullptr;

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
    struct EvolutionPose {
        const Legality::EncounterRow* row = nullptr;
        uint16_t evolvedSpecies = 0;
        uint8_t evolvedFloor = 0;
    };
    EvolutionPose nowhere, elsewhere;
    for (uint32_t rowIndex = 0; rowIndex < table->rowCount && !(nowhere.row && elsewhere.row); ++rowIndex) {
        const Legality::EncounterRow& row = table->rows[rowIndex];
        if (row.kind != Legality::ENCOUNTER_KIND_WILD) continue;
        if ((row.versions & donorVersionBit) == 0) continue;
        if (row.location >= 0xFFF0 || (row.flags & ~Legality::ENCOUNTER_FLAG_CATCH_COMBO) != 0 || row.form != 0) continue;
        for (uint16_t candidate = 1; candidate + 1 < table->speciesIndexLength; ++candidate) {
            uint16_t fromSpecies = 0;
            uint8_t fromForm = 0;
            if (!::Pokemon::getPreEvolution(donorGroup, candidate, 0, fromSpecies, fromForm)) continue;
            if (fromSpecies != row.species || fromForm != 0) continue;
            const uint8_t floorLevel = ::Pokemon::getEvolutionLevelFloor(donorGroup, candidate, 0, 1);
            if (floorLevel < 2) continue;                       // not level-gated: a stone or a trade
            const uint16_t begin = table->speciesIndex[candidate];
            const uint16_t end = table->speciesIndex[candidate + 1];
            if (begin == end) {
                const bool caughtThroughTimeCapsule = timeCapsuleTable != nullptr && candidate + 1 < timeCapsuleTable->speciesIndexLength && timeCapsuleTable->speciesIndex[candidate] != timeCapsuleTable->speciesIndex[candidate + 1];
                if (!nowhere.row && !caughtThroughTimeCapsule) nowhere = EvolutionPose{&row, candidate, floorLevel};
                break;
            }
            // The evolution must not be catchable AT THIS PLACE in this version, or it explains
            // itself at chain position 0 and the pose proves nothing.
            bool evolvedIsHereToo = false;
            for (uint16_t evolvedRow = begin; evolvedRow < end && !evolvedIsHereToo; ++evolvedRow) {
                const Legality::EncounterRow& other = table->rows[evolvedRow];
                evolvedIsHereToo = (other.location == row.location) && (other.versions & donorVersionBit) != 0;
            }
            // A FORMAT WITH NO MET LOCATION CANNOT BE POSED THIS WAY. "Its evolution is catchable
            // somewhere else" is a statement about places, and Gen 1 and Gold/Silver record none --
            // the sweep compares no location at all there, so the evolution explains itself at
            // chain position 0 and the pose proves nothing. Same exclusion the bogus-location bite
            // check makes, for the same reason.
            if (!evolvedIsHereToo && !elsewhere.row && pk.hasMetData())
                elsewhere = EvolutionPose{&row, candidate, floorLevel};
            break;
        }
    }
    if (!nowhere.row && !elsewhere.row) { ++g_evoSkip; return; }

    const uint16_t oldSpecies = pk.speciesID();
    const uint8_t  oldForm    = pk.form();
    const uint16_t oldLoc     = pk.metLocation();
    const uint8_t  oldMetLv   = pk.metLevel();
    const uint8_t  oldLevel   = pk.level();
    const uint16_t oldEggLoc  = pk.eggLocation();
    const bool     oldIsEgg   = pk.isEgg();
    const bool     oldFateful = pk.isFatefulEncounter();

    auto runPose = [&](const EvolutionPose& pose, const char* shape, int& ok, int& miss, int& skip) {
        if (!pose.row) { ++skip; return; }
        pk.setSpecies(pose.evolvedSpecies);
        pk.setForm(0);
        pk.setMetLocation(pose.row->location);
        pk.setMetLevel(pose.row->levelMin);
        pk.setEggLocation(0);
        pk.setEgg(false);
        pk.setFatefulEncounter(false);
        pk.setLevel(static_cast<uint8_t>(pose.evolvedFloor - 1));

        const Legality::Report rep = Legality::analyze(pk, group);
        const uint8_t poseLevel = pk.level();

        pk.setSpecies(oldSpecies);
        pk.setForm(oldForm);
        pk.setMetLocation(oldLoc);
        pk.setMetLevel(oldMetLv);
        pk.setEggLocation(oldEggLoc);
        pk.setEgg(oldIsEgg);
        pk.setFatefulEncounter(oldFateful);
        pk.setLevel(oldLevel);

        // A format that cannot store the pose cannot be asked the question. Gen 1 and Gen 2 derive
        // level from EXP and take setLevel, but a format that ignored it would leave the donor above
        // the floor and pass this for no reason at all.
        if (poseLevel >= pose.evolvedFloor) { ++skip; return; }

        // THE WORDING IS THE ASSERTION, not merely that something was reported. Both poses are
        // genuinely illegal, so any number of wrong explanations would still "flag" them -- and a
        // report naming the met location, which is the one field the pose leaves correct, is the
        // defect this check exists to catch.
        for (const auto& is : rep.issues) {
            if (is.text.find("would have to evolve from") != std::string::npos) { ++ok; return; }
        }
        ++miss;
        std::printf("        EVOLUTION CHECK FAILED (%s): %s -- species %u posed at level %u, below the level %u it evolves from species %u at, was not explained as an evolution\n", shape, label, pose.evolvedSpecies, poseLevel, pose.evolvedFloor, pose.row->species);
        for (const auto& is : rep.issues)
            std::printf("            said: %s\n", is.text.c_str());
    };

    runPose(nowhere, "evolution is caught nowhere", g_evoOk, g_evoMiss, g_evoSkip);
    runPose(elsewhere, "evolution is caught elsewhere", g_evoElsewhereOk, g_evoElsewhereMiss, g_evoElsewhereSkip);
}

template <typename TrainerT>
void sweepTrainer(const char* label, TrainerT& trainer) {
    Tally t;
    const Enums::GameVersion group = trainer.getGameGroup();
    for (const auto& p : trainer.party)
        if (p) analyzeOne(*p, group, t);
    for (const auto& box : trainer.boxes)
        for (const auto& p : box)
            if (p) analyzeOne(*p, group, t);

    std::printf("  %-20s %4d Pokemon | %4d clean | %4d matched an encounter | %3d illegal, %3d warnings\n", label, t.total, t.clean, t.matched, t.invalid, t.warning);
    for (const auto& kv : t.byShape)
        std::printf("        %4d x  %s\n", kv.second, kv.first.c_str());
    for (const auto& kv : t.notes)
        std::printf("        %4d -  %s\n", kv.second, kv.first.c_str());
    const int unexplained = t.total - t.matched - t.noted;
    if (unexplained > 0)
        std::printf("        %4d ?  neither matched nor noted -- these are the hatched eggs, which take the egg path and get no match note\n", unexplained);

    // The four pose checks need a DONOR, and it must not be an unhatched egg: an egg is routed
    // down the egg path before the encounter sweep is ever reached, so posing one as a wild slot
    // reports nothing at all and reads as a table failure. Prefer a party Pokemon, then any boxed
    // one -- a save whose whole party is eggs is not a reason to skip the checks.
    // A NATIVE donor is preferred over a foreign one, so each save exercises its OWN table rather
    // than whichever game its first Pokemon happened to come from. A box full of imports still
    // gets checked -- against the table its donor actually routes to.
    ::Pokemon::Pokemon* donor = nullptr;
    ::Pokemon::Pokemon* foreignDonor = nullptr;
    auto consider = [&](::Pokemon::Pokemon* candidate) {
        if (!candidate || candidate->speciesID() == 0 || candidate->isEgg()) return;
        const Enums::GameVersion candidateGroup = Enums::getGameGroup(static_cast<Enums::GameVersion>(candidate->originGame()));
        if (candidateGroup == group) { if (!donor) donor = candidate; }
        else if (!foreignDonor) { foreignDonor = candidate; }
    };
    for (auto& p : trainer.party) consider(p.get());
    for (auto& box : trainer.boxes)
        for (auto& p : box) consider(p.get());
    if (!donor) donor = foreignDonor;
    // THE LOCATION POSE NEEDS A DONOR THE LOCATION IS WHAT EXPLAINS. Any clean Pokemon is not enough:
    // a hatched one is clean through its egg, and once the pose clears the egg location a species its
    // game never offers can only say "no encounter produces it" -- true, and nothing to do with the
    // location. Sun's first Pokemon is exactly that Shinx. So it takes the first that is clean BECAUSE
    // it matched an encounter, and falls back to the ordinary donor only where no Pokemon did.
    ::Pokemon::Pokemon* locationDonor = nullptr;
    auto considerLocationDonor = [&](::Pokemon::Pokemon* candidate) {
        if (locationDonor || !candidate || candidate->speciesID() == 0 || candidate->isEgg() || !candidate->hasMetData())
            return;
        const Legality::Report report = Legality::analyze(*candidate, group);
        bool matched = false;
        for (const auto& issue : report.issues) {
            if (issue.severity != Legality::Severity::Info) return;
            matched = matched || issue.text.rfind("Matches a", 0) == 0;
        }
        if (matched) locationDonor = candidate;
    };
    for (auto& partyPokemon : trainer.party) considerLocationDonor(partyPokemon.get());
    for (auto& box : trainer.boxes)
        for (auto& boxPokemon : box) considerLocationDonor(boxPokemon.get());
    if (donor) {
        biteCheck(label, locationDonor != nullptr ? *locationDonor : *donor, group);
        eventBiteCheck(label, *donor, group);
        wildBiteCheck(label, *donor, group);
        catchComboCheck(label, *donor, group);
        evolutionBiteCheck(label, *donor, group);
    }
}

void sweepFRLG(const std::string& path) {
    const std::vector<uint8_t> data = readFile(path);
    if (data.empty()) {
        std::printf("  SKIP  %-18s (%s)\n", "FireRed", SaveTree::missing(g_saveRoot, "FireRed").c_str());
        return;
    }
    Trainer::Trainer3FRLG trainer(data, "FireRed_e.sav");
    sweepTrainer("FireRed/LeafGreen", trainer);
}

// The LOOSE-FILE saves, which are handed their bytes rather than a decrypted block list. Every
// generation whose encounter tables exist needs a real save swept through it: a wrong slot stride
// or a version bit in the wrong place does not fail to build, it just reports a boxful of
// perfectly ordinary Pokemon as impossible.
template <typename TrainerT>
void sweepLooseFile(const char* label, const char* title, const std::string& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.empty()) {
        std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, title).c_str());
        return;
    }
    // AN EMULATOR CLOCK FOOTER IS TRIMMED FIRST, as Save::openExternalSave trims it. Gold, Silver and
    // Crystal saves are commonly 0x8010 bytes, and handed over untrimmed they parsed as EMPTY -- so
    // this gate swept "0 Pokemon" for every Gen 2 save and never saw a Gen 2 finding. The rule only
    // ever trims down to 0x8000, 0x10000 or 0x20000, so an exact-size save passes through untouched.
    Save::stripRtcFooter(data);
    TrainerT trainer(std::move(data), std::string(title));
    sweepTrainer(label, trainer);
}

void sweepLetsGo(const char* label, const std::string& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.size() < Trainer::SAVE_SIZE7_LGPE) {
        std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    std::vector<uint8_t> area(data.begin(), data.begin() + Trainer::SAVE_SIZE7_LGPE);
    Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
    sweepTrainer(label, trainer);
}

void sweepBDSP(const char* label, const std::string& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.empty()) {
        std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    Trainer::Trainer8BDSP trainer(std::move(data));
    sweepTrainer(label, trainer);
}

template <typename TrainerT>
void sweepSCBlock(const char* label, const std::string& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.empty()) {
        std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    std::vector<Save::Block> blocks = Encryption::decrypt(data.data(), data.size());
    TrainerT trainer(std::move(blocks));
    sweepTrainer(label, trainer);
}

}

int main(int argc, char** argv) {
    std::string root;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose" || a == "-v") g_verbose = true;
        else root = a;
    }
    if (root.empty()) {
        std::printf("usage: legality <save-root> [--verbose]\n  Expects, relative to <save-root>, any of:\n  One folder per TITLE, any filename inside, named as the self test names the\n  title (case, spaces and a leading \"Pokemon\" aside) -- Red/  Silver/  FireRed/\n  Emerald/  HeartGold/  White 2/  Alpha Sapphire/  Ultra Moon/  LetsGoEevee/\n  Sword/  ShiningPearl/  Violet/  Legends Z-A/  Legends Arceus/  and the rest.\n");
        return 2;
    }
    g_saveRoot = root;

    // Runs first, and needs no save: it is the case that was reported, reproduced from the tables
    // alone, so it is asked on every invocation rather than only where a FireRed file turns up.
    reportedSeadraCheck();

    std::printf("PKSE legality harness\nReal saves are (almost) all legal Pokemon, so every finding here is a claim the\nchecker has to earn. Findings are grouped by shape; counts are what to watch.\n\n");

    // Found by TITLE FOLDER; see SaveTree.h.
    // EVERY TITLE FOLDER THE SELF TEST KNOWS, not a sample of them. A gate that sweeps one save per
    // generation passed while half the saves it had were never opened -- Silver, Yellow, both Let's
    // Go games and Sun/Moon among them, each with findings the console then reported.
    sweepLooseFile<Trainer::Trainer1RBY>("Red",          "Red",        SaveTree::red(root));
    sweepLooseFile<Trainer::Trainer1RBY>("Blue",         "Blue",       SaveTree::blue(root));
    sweepLooseFile<Trainer::Trainer1RBY>("Yellow",       "Yellow",     SaveTree::forTitle(root, "Yellow"));
    sweepLooseFile<Trainer::Trainer2GSC>("Gold",         "Gold",       SaveTree::forTitle(root, "Gold"));
    sweepLooseFile<Trainer::Trainer2GSC>("Silver",       "Silver",     SaveTree::forTitle(root, "Silver"));
    sweepLooseFile<Trainer::Trainer2GSC>("Crystal",      "Crystal",    SaveTree::forTitle(root, "Crystal"));
    sweepFRLG(SaveTree::fireRed(root));
    sweepLooseFile<Trainer::Trainer3RSE>("Ruby",         "Ruby",       SaveTree::ruby(root));
    sweepLooseFile<Trainer::Trainer3RSE>("Sapphire",     "Sapphire",   SaveTree::sapphire(root));
    sweepLooseFile<Trainer::Trainer3RSE>("Emerald",      "Emerald",    SaveTree::emerald(root));
    sweepLooseFile<Trainer::Trainer4DP>("Diamond",       "Diamond",    SaveTree::forTitle(root, "Diamond"));
    sweepLooseFile<Trainer::Trainer4DP>("Pearl",         "Pearl",      SaveTree::forTitle(root, "Pearl"));
    sweepLooseFile<Trainer::Trainer4PT>("Platinum",      "Platinum",   SaveTree::forTitle(root, "Platinum"));
    sweepLooseFile<Trainer::Trainer4HGSS>("HeartGold",   "HeartGold",  SaveTree::forTitle(root, "HeartGold"));
    sweepLooseFile<Trainer::Trainer4HGSS>("SoulSilver",  "SoulSilver", SaveTree::forTitle(root, "SoulSilver"));
    sweepLooseFile<Trainer::Trainer5BW>("Black",         "Black",      SaveTree::forTitle(root, "Black"));
    sweepLooseFile<Trainer::Trainer5BW>("White",         "White",      SaveTree::forTitle(root, "White"));
    sweepLooseFile<Trainer::Trainer5B2W2>("Black 2",     "Black 2",    SaveTree::forTitle(root, "Black 2"));
    sweepLooseFile<Trainer::Trainer5B2W2>("White 2",     "White 2",    SaveTree::forTitle(root, "White 2"));
    sweepLooseFile<Trainer::Trainer6XY>("X",             "X",          SaveTree::forTitle(root, "X"));
    sweepLooseFile<Trainer::Trainer6XY>("Y",             "Y",          SaveTree::forTitle(root, "Y"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Omega Ruby",  "Omega Ruby", SaveTree::forTitle(root, "Omega Ruby"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Alpha Sapphire", "Alpha Sapphire", SaveTree::forTitle(root, "Alpha Sapphire"));
    sweepLooseFile<Trainer::Trainer7SM>("Sun",           "Sun",        SaveTree::forTitle(root, "Sun"));
    sweepLooseFile<Trainer::Trainer7SM>("Moon",          "Moon",       SaveTree::forTitle(root, "Moon"));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Sun",   "Ultra Sun",  SaveTree::ultraSun(root));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Moon",  "Ultra Moon", SaveTree::forTitle(root, "Ultra Moon"));
    sweepLetsGo("Let's Go Pikachu", SaveTree::letsGoPikachu(root));
    sweepLetsGo("Let's Go Eevee", SaveTree::forTitle(root, "Let's Go Eevee"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Sword",        SaveTree::forTitle(root, "Sword"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Shield",       SaveTree::shield(root));
    sweepBDSP("Brilliant Diamond", SaveTree::forTitle(root, "Brilliant Diamond"));
    sweepBDSP("Shining Pearl", SaveTree::forTitle(root, "Shining Pearl"));
    sweepSCBlock<Trainer::Trainer9LZA>("Legends Z-A",   SaveTree::legendsZA(root));
    sweepSCBlock<Trainer::Trainer9SV>("Violet",         SaveTree::violet(root));
    sweepSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(root));

    std::printf("\nwild check:  %d game(s) matched a Pokemon posed at one of their own wild slots", g_wildOk);
    if (g_wildMiss) std::printf(", %d DID NOT", g_wildMiss);
    if (g_wildSkip) std::printf(" (%d had no plain wild row to pose at)", g_wildSkip);
    std::printf("\ncatch combo: %d game(s) matched a Let's Go slot one level above its range and rejected two", g_catchComboOk);
    if (g_catchComboMiss) std::printf(", %d DID NOT", g_catchComboMiss);
    std::printf("\nbite check:  %d game(s) correctly rejected a bogus met location", g_biteOk);
    if (g_biteMiss) std::printf(", %d DID NOT", g_biteMiss);
    if (g_biteSkip) std::printf(" (%d record no met location at all)", g_biteSkip);
    std::printf("\nevent check: %d game(s) matched a Pokemon posed as their own distribution row", g_eventOk);
    if (g_eventMiss) std::printf(", %d DID NOT", g_eventMiss);
    if (g_eventSkipNoRows) std::printf(" (%d have no distributions at all)", g_eventSkipNoRows);
    if (g_eventSkip)
        std::printf("\n             %d game(s) have distributions that cannot be posed: %s", g_eventSkip, g_eventSkipNames.c_str());
    std::printf("\nevo check:   %d game(s) rejected a Pokemon posed below the level it evolves at", g_evoOk);
    if (g_evoMiss) std::printf(", %d DID NOT", g_evoMiss);
    if (g_evoSkip) std::printf(" (%d had no level-gated evolution to pose)", g_evoSkip);
    std::printf("\nevo check:   %d game(s) said the same of one whose evolution IS catchable elsewhere", g_evoElsewhereOk);
    if (g_evoElsewhereMiss) std::printf(", %d BLAMED SOMETHING ELSE", g_evoElsewhereMiss);
    if (g_evoElsewhereSkip) std::printf(" (%d had no such pair to pose)", g_evoElsewhereSkip);
    std::printf("\nseadra check: the reported case -- a Seadra met where only Horsea is caught -- is %s", g_seadraMiss == 0 ? "explained as an evolution, both directions" : "WRONG");
    std::printf("\n\nThe sweep reports; it does not pass or fail. Judgement is the point:\ndecide per shape whether the checker is right about these saves. The seven\nbite checks DO fail, because a checker that never fires is not a clean result.\n");
    return (g_biteMiss == 0 && g_eventMiss == 0 && g_wildMiss == 0 && g_catchComboMiss == 0 && g_evoMiss == 0 && g_evoElsewhereMiss == 0 && g_seadraMiss == 0) ? 0 : 1;
}
