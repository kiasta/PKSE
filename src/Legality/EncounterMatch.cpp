/**
 * EncounterMatch.cpp - Layer 3 encounter matching. See EncounterMatch.h for the model.
 * 
 * The match runs in stages, and the stage a candidate fails at gets reported.
 * Anything that matched further is a better explanation of the Pokemon than anything
 * that failed earlier, so the report only ever describes the FURTHEST stage reached:
 * 
 *      none ->     species has no encounter at all in this game
 *      form ->     the species does, but never in this form
 *      location -> right form, but never at this met location
 *      level ->    right place, but not at this met level
 *      full ->     a template matched; only its fixed data can still disagree
 * 
 * That ordering is the whole reason a Pokemon does not get three overlapping
 * complaints about one problem.
 */

#include "Legality/EncounterMatch.h"

#include <string>

#include "Enums/Ball.h"
#include "Legality/EncounterTable.h"
#include "Names/LocationNames.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"
#include  "Trainer/Trainer.h"

namespace Legality {

    namespace {

        void add(Report& report, Severity severity, std::string text) {
            report.issues.push_back(Issue{severity, std::move(text)});
        }
        
        /// How far a candidate template got before it stopped explaining the Pokemon
        enum Stage : uint8_t {
            STAGE_NONE = 0,
            STAGE_FORM,
            STAGE_LOCATION,
            STAGE_LEVEL,
            STAGE_FULL
        };
        
        /// Fixed data a matched template can still disagree with. A bit id only reported
        /// when every template that reached STAGE_FULL sets it.
        enum Violation : uint16_t {
            VIOLATION_SHINY_LOCK = 1 << 0,
            VIOLATION_SHINY_FORCED = 1 << 1,
            VIOLATION_BALL = 1 << 2,
            VIOLATION_GENDER = 1 << 3,
            VIOLATION_NATURE = 1 << 4,
            VIOLATION_FLAWLESS = 1 << 5,
            VIOLATION_ALPHA = 1 << 6,
            VIOLATION_NOT_ALPHA = 1 << 7,
        };
        
        /// HOME rewrited the met location of anything it moves INTO SWSH to one
        /// of the five per-origin sentinels (PKHeX LocationsHOME). The real location is gone,
        /// so there is nothing left to check against.
        bool isHomeRemappedLocation(uint16_t location) {
            return location >= 59996 && location <= 60000;
        }
        
        /// Met locations that mean "this arrived from outside the game" rather than naming
        /// a place in it: Pokemon GO and HOME (PKHeX Locations.GO7/GO8/HOME8). Their
        /// encounters live in tables PKSE does not carry, and a Mystery Box Meltan is the
        /// common case — so obstain rather than accuse. None of these IDs appear in any
        /// row, so nothing legitimate is being skipped over.
        bool isExternalTransferLocation(Enums::GameVersion group, uint16_t location) {
            if (location == 30012 || location == 30018) return true;
            return group == Enums::GameVersion::GG && location == 50;
        }

        /// Did this Pokemon come out of an egg?
        ///
        /// Both dentinals have to be honored. Most formats write 0 for "no egg", but
        /// BDSP write 65535 (PKHeX Locations.Default8bNone) — and BDSP saves in the
        /// wild carry both, since 0 is what an unwritten field stores. Neither value
        /// is ever a real nursery, so treat both as "not an egg".
        bool hasEggLocation(uint16_t eggLocation) {
            return eggLocation != 0 && eggLocation != 65535;
        }

        bool breedable(const EncounterTable& t, uint16_t species) {
            if (t.breedable == nullptr || species > 1025) return false;
            return (t.breedable[species >> 3] >> (species & 7)) & 1;
        }

        /// May an egg hatch at this met location, in this version?
        bool canHatchAt(const EggRule& egg, uint16_t location, uint8_t versionBit) {
            if (egg.hatchLocations == nullptr || location >= egg.hatchLocationCount) return false;
            return (egg.hatchLocations[location] & versionBit) != 0;
        }

        std::string describe(uint16_t species, uint8_t form) {
            std::string s = Trainer::getSpeciesName(species);
            if (form != 0) {
                s += " (form " + std::to_string(form) + ")";
            }
            return s;
        }

        /// Species name plus its form index, for messages ("Rotom (form 3)").
        std::string locationText(uint8_t originVersion, uint16_t location) {
            const char* name = Names::getMetLocationName(originVersion, location);
            std::string id = std::to_string(location);
            if (name == nullptr || name[0] == '\0') return "location" + id;
            return std::string(name) + " (" + id + ")";
        }

        const char* kindText(uint8_t kind) {
            switch(kind) {
                case ENC_KIND_WILD: return "wild encounter";
                case ENC_KIND_STATIC: return "static encounter";
                case ENC_KIND_GIFT: return "gift";
                case ENC_KIND_TRADE: return "in-game trade";
                case ENC_KIND_RAID: return "raid";
                default: return "encounter";
            }
        }

        /// Does the level a Pokemon was met at fall inside the template's span?
        /// SWSH's Wild Area is the one exception: after the post-game the
        /// whole area is boosted to exactly 60 regardless of the slot's own
        /// range (PKHeX EncounterArea8.BoostLevel).
        bool levelMatches(const EncounterRow& row, uint8_t metLevel) {
            if (metLevel >= row.levelMin && metLevel <= row.levelMax) return true;
            return (row.flags & ENC_FLAG_BOOST60) != 0 && metLevel == 60;
        }

        /// Count of IVs at 31 — a template guaranteeding N of them cannot produce fewer.
        int flawlessCount(const Pokemon::Pokemon& pk) {
            return
                (pk.ivHP() == 31) +
                (pk.ivATK() == 31) +
                (pk.ivDEF() == 31) +
                (pk.ivSPE() == 31) +
                (pk.ivSPA() == 31) +
                (pk.ivSPD() == 31);
        }

        /// Everything about a matched template that the Pokemon contradicts.
        uint16_t violationsFor(const EncounterRow& row, const Pokemon::Pokemon& pk, bool shiny, int flawless) {
            uint16_t violations = 0;
            if (row.shiny == ENC_SHINY_NEVER && shiny) violations |= VIOLATION_SHINY_LOCK;
            if (row.shiny == ENC_SHINY_ALWAYS && !shiny) violations |= VIOLATION_SHINY_FORCED;
            if (row.fixedBall != 0 && pk.ball() != 0 && pk.ball() != row.fixedBall) violations |= VIOLATION_BALL;
            if (row.gender != ENC_GENDER_ANY && pk.gender() != row.gender) violations |= VIOLATION_GENDER;
            if (row.nature != ENC_NATURE_ANY && pk.nature() != row.nature) violations |= VIOLATION_NATURE;
            if (row.flawlessIVs != 0 && flawless < row.flawlessIVs) violations |= VIOLATION_FLAWLESS;
            const bool wantAlpha = (row.flags & ENC_FLAG_ALPHA) != 0;
            if (wantAlpha && !pk.isAlpha()) violations |= VIOLATION_ALPHA;
            if (!wantAlpha && pk.isAlpha()) violations |= VIOLATION_NOT_ALPHA;
            return violations;
        }

        /// One (species, form) the Pokemon could have been WHEN CAUGHT.
        struct Candidate { uint16_t species; uint8_t form; };

        /**
         * The Pokemon itself, then every ancestor it could have evolved from.
         * 
         * The form fallback matters: PKHeX's lineage keys an edge on the destination
         * form, and cosmetic forms decide after the evolution have no edge of their
         * own — an Alcremie-5 has no ancestor row, only Alcremie-0 does. Retrying at
         * form 0 finds Micery instead of concluding the species is unobtainable. It
         * cannot mislead for regional variants, which all carry their own edge case.
         */
        int buildChain(Enums::GameVersion group, uint16_t species, uint8_t form, Candidate* out, int max) {
            int n = 0;
            out[n++] = Candidate{species, form};
            uint16_t currentSpecies = species;
            uint8_t currentForm = form;
            while (n < max) {
                uint16_t pSpecies;
                uint8_t pForm;
                if (!Pokemon::getPreEvolution(group, currentSpecies, 0, pSpecies, pForm)) {
                    if (currentForm == 0) break;
                    if (!Pokemon::getPreEvolution(group, currentSpecies, 0, pSpecies, pForm)) break;
                }
                bool seen = false;
                for (int i = 0; i < n && !seen; ++i) {
                    seen = (out[i].species == pSpecies && out[i].form == pForm);
                }
                if (seen) break;
                out[n++] = Candidate{pSpecies, pForm};
                currentSpecies = pSpecies;
                currentForm = pForm;
            }
            return n;
        }

        /// Resule of sweeping the tables for one Pokemon
        struct MatchState {
            Stage stage = STAGE_NONE;
            bool matched = false;
            uint16_t violations = 0xFFFF;
            bool sawFull = false;
            uint8_t bestKind = ENC_KIND_WILD;
            uint8_t bestLevelMin = 0;
            uint8_t bestLevelMax = 0;
            int bestLevelGap = 0x7FFFFFFF;
        };

        /// How far `level` sits outside [min, max]; 0 when inside.
        int levelGap(uint8_t level, uint8_t low, uint8_t high) {
            if (level < low) return low-level;
            if (level > high) return level-high;
            return 0;
        }

        void sweep(const EncounterTable& table, const Candidate* chain, int chainLength, uint8_t versionBit, uint16_t metLocation, uint8_t metLevel,
            const Pokemon::Pokemon& pk, bool shiny, int flawless, MatchState& state) {
            for (int i = 0; i < chainLength && !state.matched; ++i) {
                const uint16_t species = chain[i].species;
                if (species + 1 >= table.speciesIndexLen) continue;
                const uint16_t begin = table.speciesIndex[species];
                const uint16_t end = table.speciesIndex[species+1];
                const bool formFree = Pokemon::isFormChangeable(species);

                for (uint16_t x = begin; x < end; ++x) {
                    const EncounterRow& row = table.rows[x];
                    if ((row.versions & versionBit) == 0) continue;
                    if ((row.flags & ENC_FLAG_EGG) != 0) continue;
                    if (!(row.form == ENC_FORM_ANY || row.form == chain[i].form || formFree)) {
                        if (state.stage < STAGE_FORM) state.stage = STAGE_FORM;
                        continue;
                    }
                    if (!encounterLocationMatches(row.location, metLocation)) {
                        if (state.stage < STAGE_LOCATION) state.stage = STAGE_LOCATION;
                        continue;
                    }
                    if (!levelMatches(row, metLevel)) {
                        if (state.stage <= STAGE_LEVEL) {
                            const int gap = levelGap(metLevel, row.levelMin, row.levelMax);
                            if (gap < state.bestLevelGap) {
                                state.bestLevelGap = gap;
                                state.bestLevelMin = row.levelMin;
                                state.bestLevelMax = row.levelMax;
                            }
                            state.stage = STAGE_LEVEL;
                        }
                        continue;
                    }

                    const uint16_t violations = violationsFor(row, pk, shiny, flawless);
                    if (!state.sawFull) state.bestKind = row.kind;
                    state.sawFull = true;
                    state.stage = STAGE_FULL;
                    state.violations &= violations;
                    if (violations == 0) {
                        state.matched = true;
                        state.bestKind = row.kind;
                        break;
                    }
                }
            }
        }

        /// Is this egg a scripted GIFT egg, rather than a daycare one? BDSP are the
        /// only games that hand out any (Happiny from the Traveling Man, Riolu from
        /// Riley), and each has its own egg-location ID — not the nursery's, and not
        /// a species the daycare would ever produce.
        bool matchesGiftEgg(const EncounterTable& table, const Candidate* chain, int chainLength, uint8_t versionBit, uint16_t eggLocation) {
            for (int i = 0; i < chainLength; ++i) {
                const uint16_t species = chain[i].species;
                if (species + 1 >= table.speciesIndexLen) continue;
                for (uint16_t x = table.speciesIndex[species]; x < table.speciesIndex[species + 1]; ++x) {
                    const EncounterRow& row = table.rows[x];
                    if ((row.flags & ENC_FLAG_EGG) == 0) continue;
                    if ((row.versions & versionBit) == 0) continue;
                    if (row.eggLocation == eggLocation) return true;
                }
            }

            return false;
        }

        /**
         * The egg path: A hatched egg has no wild slot to match: what is has is a
         * nursery ID, the set of places the game lets an egg hatch, a fixed hatch level, and
         * the requirement that the bottom of its evolution chain is something the daycare
         * will actually produce.
         */
        void checkEgg(const EncounterTable& table, const Pokemon::Pokemon& pk, Enums::GameVersion originGroup, uint8_t originVersion, uint8_t versionBit,
            const Candidate* chain, int chainLength, uint16_t eggLocation, Report& report) {
            const EggRule& egg = table.egg;
            const uint16_t baseSpecies = chain[chainLength-1].species;
            const std::string groupName = Enums::getGameVersionName(originGroup);
            if (!egg.hasBreeding) {
                add(report, Severity::Warning, groupName + " has no breeding, but this Pokémon carries an egg location");
                return;
            }
            const bool giftEgg = eggLocation != 0 && matchesGiftEgg(table, chain, chainLength, versionBit, eggLocation);
            if (eggLocation != 0 && !giftEgg && eggLocation != egg.eggLocation && (egg.eggLocationAlt == 0 || eggLocation != egg.eggLocationAlt)) {
                add(report, Severity::Warning, "Egg location " + locationText(originVersion, eggLocation) + " is not where " + groupName + " hands out eggs");
            }
            // A gift egg is handed over directly, so the daycare's breeding rules do not apply
            // to is — Riolu is unbreedable in BDSP and Riley gives you one anyway.
            if (!giftEgg && !breedable(table, baseSpecies)) {
                add(report, Severity::Warning, std::string(Trainer::getSpeciesName(baseSpecies)) + " cannot hatch from an egg");
            }
            if (pk.isEgg()) return; // not hatched yet, no metadata to check

            const uint16_t metLocation = pk.metLocation();
            if (!canHatchAt(egg, metLocation, versionBit)) {
                add(report, Severity::Warning, locationText(originVersion, metLocation) + " is not somewhere an egg can hatch in " + groupName);
            }
            if (pk.metLevel() != egg.hatchLevel) {
                add(report, Severity::Warning, "A hatched egg is met at level " + std::to_string(egg.hatchLevel) + ", not " + std::to_string(pk.metLevel()));
            }
        }

        void reportViolations(uint16_t violations, const Pokemon::Pokemon& pk, Report& report) {
            if (violations & VIOLATION_SHINY_LOCK)
                add(report, Severity::Invalid, "This encounter is shiny-locked, but the Pokémon is shiny");
            if (violations & VIOLATION_SHINY_FORCED)
                add(report, Severity::Warning, "This encounter is always shiny, but the Pokémon is not shiny");
            if (violations & VIOLATION_BALL)
                add(report, Severity::Warning, std::string("This encounter has a fixed ball, but this is a ") + Enums::getBallName(pk.ball()));
            if (violations & VIOLATION_GENDER)
                add(report, Severity::Warning, "The encounter has a different fixed gender than the Pokémon's gender");
            if (violations & VIOLATION_NATURE)
                add(report, Severity::Warning, "The encounter has a different fixed nature than the Pokémon's nature");
            if (violations & VIOLATION_FLAWLESS)
                add(report, Severity::Warning, "The encounter guarantees more 31 IVs than this Pokémon has");
            if (violations & VIOLATION_ALPHA)
                add(report, Severity::Warning, "This is an Alpha-only encounter, but the Pokémon is not an Alpha");
            if (violations & VIOLATION_NOT_ALPHA)
                add(report, Severity::Warning, "The Pokémon is an Alpha, but no Alpha encounter matches");
        }
    }

    void checkEncounter(const Pokemon::Pokemon& pk, Report& report) {
        const uint16_t species = pk.speciesID();
        if (species == 0) return;

        const uint8_t originRaw = pk.originGame();
        const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(originRaw);
        const EncounterTable* table = getEncounterTable(originVersion);
        const uint8_t versionBit = encounterVersionBit(originVersion);

        //No tables for this origin, so say that the check did not run
        if (table == nullptr || versionBit == 0) {
            add(report, Severity::Info, "Origin game ID " + std::to_string(originRaw) + " has no encounter data — encounter check skipped");
            return;
        }
        const Enums::GameVersion originGroup = Enums::getGameGroup(originVersion);
        const std::string gameName = Enums::getGameVersionName(originVersion);

        const uint16_t metLocation = pk.metLocation();
        if (isHomeRemappedLocation(metLocation)) {
            add(report, Severity::Info, "HOME replaced the met location on transfer — encounter check skipped");
        }
        if (isExternalTransferLocation(originGroup, metLocation)) {
            add(report, Severity::Info, "Came from Pokémon GO/HOME — those encounters are not modelled, encounter check skipped");
            return;
        }

        Candidate chain[Pokemon::EVO_MAX_CHAIN+1];
        const int chainLength = buildChain(originGroup, species, pk.form(), chain, Pokemon::EVO_MAX_CHAIN+1);

        // An egg location is definitive where the format has the field: Gen 3
        // has none, so a hatched FRLG egg is recognized by its fixed hatch location
        // and level instead, and only after the normal sweep has failed to explain it.
        const uint16_t eggLocation = pk.eggLocation();
        const bool fromEgg = hasEggLocation(eggLocation);
        if (fromEgg || pk.isEgg()) {
            checkEgg(*table, pk, originGroup, originRaw, versionBit, chain, chainLength, fromEgg ? eggLocation : 0, report);
            return;
        }

        const bool isShiny = pk.isShiny(pk.id32(), Trainer::getSpeciesName(species));
        const int flawless = flawlessCount(pk);

        MatchState state;
        sweep(*table, chain, chainLength, versionBit, metLocation, pk.metLevel(), pk, isShiny, flawless, state);

        if (state.matched) {
            add(report, Severity::Info, std::string("Matches a ") + kindText(state.bestKind) + " in " + gameName + " at " + locationText(originRaw, metLocation));
            return;
        }

        // Gen 3 hides its hatched eggs: the format has no egg-location field at all, so a
        // Four Island egg looks exactly like a wild catch until the sweep fails. Read it as
        // an egg before concluding the Pokemon has no origin. Restricted to formats without
        // the field — everywhere else the field already answered, above.
        if (table->egg.hasBreeding && table->egg.eggLocation == 0 &&
        pk.metLevel() == table->egg.hatchLevel &&
        canHatchAt(table->egg, metLocation, versionBit)) {
            checkEgg(*table, pk, originGroup, originRaw, versionBit, chain, chainLength, 0, report);
            return;
        }

        // Mystery Gift/distribution Pokemon are not in the tables at all, so a fateful
        // encounter that fails to match is more likely an event than a forgery. Report it,
        // but never as Invalid.
        const bool event = pk.isFatefulEncounter();
        const Severity hard = event ? Severity::Warning : Severity::Invalid;
        
        switch(state.stage) {
            case STAGE_NONE:
                add(report, hard, "No encounter in " + gameName + " produces " + describe(species, pk.form()) + (event ? " — event Pokémon are not checked" : ""));
                break;
            case STAGE_FORM:
                add(report, hard, describe(species, pk.form()) + " has no encounter in " + gameName + " in this form:");
                break;
            case STAGE_LOCATION:
                add(report, hard, locationText(originRaw, metLocation) + " is not a place " + std::string(Trainer::getSpeciesName(species)) + " can be encountered in " + gameName + (event ? " — event Pokémon are not checked" : ""));
                break;
            case STAGE_LEVEL: {
                const std::string band = state.bestLevelMin == state.bestLevelMax ? std::to_string(state.bestLevelMin) : std::to_string(state.bestLevelMin) + "-" + std::to_string(state.bestLevelMax);
                add(report, Severity::Warning, "No encounter for "+
                    std::string(Trainer::getSpeciesName(species)) +
                    " at " + locationText(originRaw, metLocation) +
                    " is met at level " + std::to_string(pk.metLevel()) +
                    " (nearest is " + band + ")");
                break;
            }
            case STAGE_FULL:
                reportViolations(state.violations, pk, report);
                break;
        }
    }
}