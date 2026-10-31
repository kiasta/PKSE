/**
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
#include <vector>

#include "Enums/Ball.h"
#include "Legality/EncounterTable.h"
#include "Names/LocationNames.h"
#include "Names/MoveNames.h"   // relearn-move names
#include "Names/RibbonNames.h" // the ribbons and marks a Pokemon actually carries
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Gen1Tables.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"

namespace Legality
{

    namespace
    {

        void add(Report &report, Severity severity, std::string text)
        {
            report.issues.push_back(Issue{severity, std::move(text)});
        }

        /// How far a candidate template got before it stopped explaining the Pokemon. **The
        /// ORDER is what gets reported** -- one Pokemon is swept against every row of every
        /// ancestor, and the furthest stage any of them reached is the one the message describes.
        /// So this ranks by how much a failure at that stage actually PROVES.
        enum Stage : uint8_t
        {
            STAGE_NONE = 0,
            STAGE_FORM,
            STAGE_LOCATION,
            STAGE_LEVEL,
            /// An ancestor's row matches the place the Pokemon says it was met, and the only thing
            /// wrong is that the Pokemon has not reached the level that evolution needs.
            ///
            /// ABOVE LOCATION AND LEVEL, and that ordering is the whole finding. A level 15 Seadra
            /// met on Route 24 was reported as *"Route 24 is not a place Seadra can be encountered
            /// in FireRed"* -- true of Seadra, irrelevant to the Pokemon, and it named the one
            /// field that was RIGHT. Horsea is on Route 24; what a level 15 Seadra cannot have
            /// done is evolve, since that takes level 32. Ranked below LOCATION, the dead end
            /// (Seadra's own rows, at other places) buried the explanation. It is above LEVEL for
            /// the same reason: the ancestor's row may cover the met level perfectly well, and
            /// naming a level band it already satisfies would be the same mistake one field over.
            ///
            /// SO IT IS DECLARED AFTER STAGE_LEVEL -- "furthest" is the larger value. It once sat
            /// between LOCATION and LEVEL, which ranked it above the first and silently BELOW the
            /// second, so a Butterfree whose Metapod row fit and whose own row did not was told about
            /// a level band instead of the evolution it could not have made.
            STAGE_EVOLUTION,
            STAGE_FULL
        };

        /// Fixed data a matched template can still disagree with. A bit id only reported
        /// when every template that reached STAGE_FULL sets it.
        enum Violation : uint16_t
        {
            VIOLATION_SHINY_LOCK = 1 << 0,
            VIOLATION_SHINY_FORCED = 1 << 1,
            VIOLATION_BALL = 1 << 2,
            VIOLATION_GENDER = 1 << 3,
            VIOLATION_NATURE = 1 << 4,
            VIOLATION_FLAWLESS = 1 << 5,
            VIOLATION_ALPHA = 1 << 6,
            VIOLATION_NOT_ALPHA = 1 << 7,
        };

        /// HOME rewrites the met location of anything it moves INTO SWSH from a game Sword/Shield
        /// cannot name to one of seven per-origin sentinels (PKHeX LocationsHOME): Legends:
        /// Arceus, BD/SP and S/V, and -- the lowest two -- the Switch FireRed/LeafGreen (SHLG
        /// 59994, SWFR 59995). The real location is gone, so there is nothing left to check against.
        bool isHomeRemappedLocation(uint16_t location)
        {
            return location >= 59994 && location <= 60000;
        }

        /// Met locations that mean "this arrived from outside the game" rather than naming a place in
        /// it: Pokemon GO and HOME (PKHeX Locations.GO7/GO8/HOME8). A Mystery Box Meltan is the common
        /// case, and its encounter lives in tables PKSE does not carry -- so abstain rather than accuse.
        ///
        /// A FALLBACK, not a short circuit. A HOME gift is met at 30018 by definition, so returning here
        /// before the sweep throws away every one of those rows unread. The sweep runs first and this only
        /// speaks when nothing matched.
        bool isExternalTransferLocation(Enums::GameVersion group, uint16_t location)
        {
            if (location == 30012 || location == 30018)
                return true;
            return group == Enums::GameVersion::GG && location == 50;
        }

        /// Did this Pokemon come out of an egg?
        ///
        /// Both dentinals have to be honored. Most formats write 0 for "no egg", but
        /// BDSP write 65535 (PKHeX Locations.Default8bNone) — and BDSP saves in the
        /// wild carry both, since 0 is what an unwritten field stores. Neither value
        /// is ever a real nursery, so treat both as "not an egg".
        bool hasEggLocation(uint16_t eggLocation)
        {
            return eggLocation != 0 && eggLocation != 65535;
        }

        bool breedable(const EncounterTable &lookupTable, uint16_t species)
        {
            if (lookupTable.breedable == nullptr || species > 1025)
                return false;
            return (lookupTable.breedable[species >> 3] >> (species & 7)) & 1;
        }

        /// Is `eggLocation` one of the ids a BRED egg may legitimately carry in this game?
        ///
        /// More than one, because a traded egg carries a different id than a daycare one -- and
        /// Gen 5 has three in all: the daycare, the ordinary link trade, and the one Spin Trade
        /// writes (PKHeX Locations.IsEggLocationBred5, whose own comment calls that last one
        /// incorrect and legal anyway). An unused alternate slot is 0, which is never a real
        /// nursery in a format that has the field at all.
        bool isBredEggLocation(const EggRule &egg, uint16_t eggLocation)
        {
            if (eggLocation == egg.eggLocation)
                return true;
            for (const uint16_t alternate : egg.eggLocationAlternates)
            {
                if (alternate != 0 && eggLocation == alternate)
                    return true;
            }
            return false;
        }

        /// May an egg hatch at this met location, in this version?
        ///
        /// NO MASK MEANS NOT CHECKED, not "nowhere". Every group PKSE has encounter data for ships
        /// one now, but the rule has to hold for a group that does not: reading an absent mask as a
        /// refusal would flag every bred Pokemon in that game, which is the loudest possible way to
        /// be wrong. A location PAST a mask that does exist is still a real no.
        ///
        /// The extra id is Poke Pelago and nothing else -- see EggRule::hatchLocationExtra.
        bool canHatchAt(const EggRule &egg, uint16_t location, uint8_t versionBit)
        {
            if (egg.hatchLocations == nullptr)
                return true;
            if (egg.hatchLocationExtra != 0 && location == egg.hatchLocationExtra)
                return true;
            if (location >= egg.hatchLocationCount)
                return false;
            return (egg.hatchLocations[location] & versionBit) != 0;
        }

        std::string describe(uint16_t species, uint8_t form)
        {
            std::string text = Trainer::getSpeciesName(species);
            if (form != 0)
            {
                text += " (form " + std::to_string(form) + ")";
            }
            return text;
        }

        /// Species name plus its form index, for messages ("Rotom (form 3)").
        std::string locationText(uint8_t originVersion, uint16_t location)
        {
            const char *name = Names::getMetLocationName(originVersion, location);
            std::string itemId = std::to_string(location);
            if (name == nullptr || name[0] == '\0')
                return "location" + itemId;
            return std::string(name) + " (" + itemId + ")";
        }

        const char *kindText(uint8_t kind)
        {
            switch (kind)
            {
            case ENCOUNTER_KIND_WILD:
                return "wild encounter";
            case ENCOUNTER_KIND_STATIC:
                return "static encounter";
            case ENCOUNTER_KIND_GIFT:
                return "gift";
            case ENCOUNTER_KIND_TRADE:
                return "in-game trade";
            case ENCOUNTER_KIND_RAID:
                return "raid";
            case ENCOUNTER_KIND_EVENT:
                return "Mystery Gift distribution";
            default:
                return "encounter";
            }
        }

        constexpr int FLUTE_LEVEL_SHIFT = 4;
        constexpr int DEXNAV_LEVEL_BOOST = 33;

        /// The highest met level a row admits. Let's Go's catch combo spawns a slot's Pokemon one
        /// level above its range (PKHeX EncounterSlot7b.CatchComboBonus), so a level past the table's
        /// own maximum is still that slot's. ORAS's Black Flute adds 4 and a DexNav chain up to 33
        /// (EncounterSlot6AO.IsMatchExact).
        int highestMetLevel(const EncounterRow &row)
        {
            if ((row.flags & ENCOUNTER_FLAG_DEXNAV) != 0)
                return row.levelMax + DEXNAV_LEVEL_BOOST;
            if ((row.flags & ENCOUNTER_FLAG_FLUTE) != 0)
                return row.levelMax + FLUTE_LEVEL_SHIFT;
            return (row.flags & ENCOUNTER_FLAG_CATCH_COMBO) != 0 ? row.levelMax + 1 : row.levelMax;
        }

        /// The lowest met level a row admits: ORAS's White Flute lowers a slot by 4.
        int lowestMetLevel(const EncounterRow &row)
        {
            return (row.flags & ENCOUNTER_FLAG_FLUTE) != 0 ? row.levelMin - FLUTE_LEVEL_SHIFT : row.levelMin;
        }

        /// Does the level a Pokemon was met at fall inside the template's span?
        /// Two games widen it. SWSH's Wild Area: after the post-game the whole area is boosted
        /// to exactly 60 regardless of the slot's own range (PKHeX EncounterArea8.BoostLevel).
        /// ORAS: the Flutes move a slot 4 levels either way and a DexNav chain up to 33 above it
        /// (EncounterSlot6AO.IsMatchExact).
        bool levelMatches(const EncounterRow &row, uint8_t metLevel)
        {
            if (metLevel >= lowestMetLevel(row) && metLevel <= highestMetLevel(row))
                return true;
            return (row.flags & ENCOUNTER_FLAG_BOOST60) != 0 && metLevel == 60;
        }

        /// Count of IVs at 31 — a template guaranteeing N of them cannot produce fewer.
        ///
        /// Deliberately the STORED IVs, never effectiveIV(): the guarantee is about how the Pokemon
        /// was GENERATED, and Hyper Training is something done to it afterwards with Bottle Caps. A
        /// hyper-trained Pokemon reading as six flawless IVs here would match raid templates it was
        /// never produced by, which is the opposite of what this count is for. PKHeX matches on the
        /// raw IVs for the same reason. (Stat MATHS is the other way round -- see effectiveIV.)
        int flawlessCount(const Pokemon::Pokemon &pokemon)
        {
            return (pokemon.ivHP() == 31) + (pokemon.ivATK() == 31) + (pokemon.ivDEF() == 31) + (pokemon.ivSPE() == 31) + (pokemon.ivSPA() == 31) + (pokemon.ivSPD() == 31);
        }

        /// Everything about a matched template that the Pokemon contradicts.
        uint16_t violationsFor(const EncounterRow &row, const Pokemon::Pokemon &pokemon, bool shiny, int flawless)
        {
            uint16_t violations = 0;
            if (row.shiny == ENCOUNTER_SHINY_NEVER && shiny)
                violations |= VIOLATION_SHINY_LOCK;
            if (row.shiny == ENCOUNTER_SHINY_ALWAYS && !shiny)
                violations |= VIOLATION_SHINY_FORCED;
            if (row.fixedBall != 0 && pokemon.ball() != 0 && pokemon.ball() != row.fixedBall)
                violations |= VIOLATION_BALL;
            if (row.gender != ENCOUNTER_GENDER_ANY && pokemon.gender() != row.gender)
                violations |= VIOLATION_GENDER;
            if (row.nature != ENCOUNTER_NATURE_ANY && pokemon.nature() != row.nature)
                violations |= VIOLATION_NATURE;
            if (row.flawlessIVs != 0 && flawless < row.flawlessIVs)
                violations |= VIOLATION_FLAWLESS;
            const bool wantAlpha = (row.flags & ENCOUNTER_FLAG_ALPHA) != 0;
            if (wantAlpha && !pokemon.isAlpha())
                violations |= VIOLATION_ALPHA;
            if (!wantAlpha && pokemon.isAlpha())
                violations |= VIOLATION_NOT_ALPHA;
            return violations;
        }

        /// One (species, form) the Pokemon could have been WHEN CAUGHT.
        struct Candidate
        {
            uint16_t species;
            uint8_t form;
            /// Lowest CURRENT level the Pokemon in hand can be, having been OBTAINED as this
            /// candidate — the highest evolution-level requirement between here and it. 0 for the
            /// species itself, and for every chain whose evolutions are stones, trades or
            /// friendship. See Pokemon::getEvolutionLevelFloor.
            uint8_t levelFloor;
        };

        /**
         * The Pokemon itself, then every ancestor it could have evolved from.
         *
         * The form fallback matters: PKHeX's lineage keys an edge on the destination
         * form, and cosmetic forms decide after the evolution have no edge of their
         * own — an Alcremie-5 has no ancestor row, only Alcremie-0 does. Retrying at
         * form 0 finds Micery instead of concluding the species is unobtainable. It
         * cannot mislead for regional variants, which all carry their own edge case.
         */
        int buildChain(Enums::GameVersion group, uint16_t species, uint8_t form, Candidate *out, int max)
        {
            int chainLength = 0;
            out[chainLength++] = Candidate{species, form, 0};
            uint16_t currentSpecies = species;
            uint8_t currentForm = form;
            uint8_t levelFloor = 0;
            while (chainLength < max)
            {
                uint16_t pSpecies;
                uint8_t pForm;
                // THE POKEMON'S OWN FORM IS ASKED FIRST and form 0 is the fallback. Passing 0 for both
                // resolves an Alolan Raichu to a KANTONIAN Pichu. Which form answered is remembered,
                // because the level floor has to be read off the same edge.
                uint8_t edgeForm = currentForm;
                if (!Pokemon::getPreEvolution(group, currentSpecies, edgeForm, pSpecies, pForm))
                {
                    if (currentForm == 0)
                        break;
                    edgeForm = 0;
                    if (!Pokemon::getPreEvolution(group, currentSpecies, edgeForm, pSpecies, pForm))
                        break;
                }
                bool seen = false;
                for (int chainIndex = 0; chainIndex < chainLength && !seen; ++chainIndex)
                {
                    seen = (out[chainIndex].species == pSpecies && out[chainIndex].form == pForm);
                }
                if (seen)
                    break;
                // The floor ACCUMULATES down the chain: a Venusaur that started as a Bulbasaur had
                // to pass 16 and then 32, so the binding number is the highest edge walked, not the
                // nearest one. getEvolutionLevelFloor answers that for a whole walk; asked one step
                // at a time here, because the walk is already happening.
                const uint8_t edgeFloor = Pokemon::getEvolutionLevelFloor(group, currentSpecies, edgeForm, 1);
                if (edgeFloor > levelFloor)
                    levelFloor = edgeFloor;
                out[chainLength++] = Candidate{pSpecies, pForm, levelFloor};
                currentSpecies = pSpecies;
                currentForm = pForm;
            }
            return chainLength;
        }

        /// Result of sweeping the tables for one Pokemon
        struct MatchState
        {
            Stage stage = STAGE_NONE;
            bool matched = false;
            uint16_t violations = 0xFFFF;
            bool sawFull = false;
            uint8_t bestKind = ENCOUNTER_KIND_WILD;
            uint8_t bestLevelMin = 0;
            uint8_t bestLevelMax = 0;
            int bestLevelGap = 0x7FFFFFFF;
            /// The cheapest evolution the Pokemon has not paid for: the lowest level floor among
            /// the ancestors with a row AT THE PLACE IT SAYS IT WAS MET, and the ancestor it
            /// belongs to. Cheapest, because that is the one nearest to explaining the record.
            uint8_t evolutionFloor = 0;
            uint16_t evolutionFrom = 0;
        };

        /// How far `level` sits outside [min, max]; 0 when inside.
        int levelGap(uint8_t level, uint8_t lowestLevel, uint8_t highestLevel)
        {
            if (level < lowestLevel)
                return lowestLevel - level;
            if (level > highestLevel)
                return level - highestLevel;
            return 0;
        }

        /**
         * `hasMetData` false means the FORMAT records no met location and no met level at all --
         * Gen 1 has neither field, and Gold/Silver leave Gen 2's caught-data word zero (only
         * Crystal writes it). The question Layer 3 asks everywhere else cannot be asked of such a
         * record, so two stages change rather than being faked:
         *
         *   LOCATION is not compared. A row's place is still true, there is just nothing to
         *   compare it against, and comparing against the 0 those accessors return would reject
         *   every row.
         *
         *   LEVEL is compared against the Pokemon's CURRENT level, and only as a floor. A Pokemon
         *   only ever grows, so "caught at or above this row's minimum" is the strongest true
         *   statement available -- it catches a level-2 Mewtwo and nothing else. It is deliberately
         *   weaker than the met-level test it replaces; a record that cannot say where or when it
         *   was caught cannot be held to either.
         */
        void sweep(const EncounterTable &table, const Candidate *chain, int chainLength, uint8_t versionBit, uint16_t metLocation, uint8_t metLevel, const Pokemon::Pokemon &pokemon, bool shiny, int flawless, MatchState &state, bool hasMetData)
        {
            for (int chainIndex = 0; chainIndex < chainLength && !state.matched; ++chainIndex)
            {
                const uint16_t species = chain[chainIndex].species;
                if (species + 1 >= table.speciesIndexLength)
                    continue;
                const uint16_t begin = table.speciesIndex[species];
                const uint16_t end = table.speciesIndex[species + 1];
                const bool formFree = Pokemon::isFormChangeable(species);

                for (uint16_t rowIndex = begin; rowIndex < end; ++rowIndex)
                {
                    const EncounterRow &row = table.rows[rowIndex];
                    if ((row.versions & versionBit) == 0)
                        continue;
                    if ((row.flags & ENCOUNTER_FLAG_EGG) != 0)
                        continue;
                    if (!(row.form == ENCOUNTER_FORM_ANY || row.form == chain[chainIndex].form || formFree))
                    {
                        if (state.stage < STAGE_FORM)
                            state.stage = STAGE_FORM;
                        continue;
                    }
                    if (hasMetData && !encounterLocationMatches(row.location, metLocation))
                    {
                        if (state.stage < STAGE_LOCATION)
                            state.stage = STAGE_LOCATION;
                        continue;
                    }
                    // A ROW FOR AN ANCESTOR ONLY EXPLAINS THE POKEMON IF IT COULD HAVE EVOLVED, and
                    // that is a question about its CURRENT level, not its met level.
                    //
                    // **ASKED AFTER THE PLACE, NOT BEFORE IT**, so the claim is only ever made about
                    // a row that matches where the Pokemon says it was met -- which is what makes
                    // "it would have to evolve from X" a true sentence about THIS record rather than
                    // about some row elsewhere in the game. Asked first, a Seadra met on Horsea's
                    // Route 24 skipped every Horsea row here and was left to be explained by
                    // Seadra's own rows at other places, which is how a correct finding came out
                    // blaming the one field that was right.
                    //
                    // Position 0 in the chain always passes, which is the entire reason this is
                    // chain-aware: Gold and Silver put wild NOCTOWL on Route 2 at level 7, thirteen
                    // levels below the twenty a Hoothoot needs, so a flat "Noctowl must be 20"
                    // would flag a perfectly ordinary catch. A level of 0 means the record does not
                    // report one (a stored-size box entity), so nothing is claimed.
                    if (chain[chainIndex].levelFloor != 0 && pokemon.level() != 0 && pokemon.level() < chain[chainIndex].levelFloor)
                    {
                        if (state.evolutionFloor == 0 || chain[chainIndex].levelFloor < state.evolutionFloor)
                        {
                            state.evolutionFloor = chain[chainIndex].levelFloor;
                            state.evolutionFrom = species;
                        }
                        if (state.stage < STAGE_EVOLUTION)
                            state.stage = STAGE_EVOLUTION;
                        continue;
                    }
                    // A ROW THE GAME RECORDS AT MET LEVEL 0 bounds the CURRENT level instead, and where
                    // the record has a met level it has to be that 0 -- see
                    // ENCOUNTER_FLAG_MET_LEVEL_ZERO. Crystal's in-game trades are the case: compared
                    // as an ordinary span, Kyle's Onix met at level 0 matched no row at all.
                    const bool metLevelIsZero = (row.flags & ENCOUNTER_FLAG_MET_LEVEL_ZERO) != 0;
                    const bool levelFails = !hasMetData ? pokemon.level() < row.levelMin : metLevelIsZero ? metLevel != 0 || pokemon.level() < row.levelMin : !levelMatches(row, metLevel);
                    if (levelFails)
                    {
                        if (state.stage <= STAGE_LEVEL)
                        {
                            const int levelDistance = levelGap(metLevel, row.levelMin, highestMetLevel(row));
                            if (levelDistance < state.bestLevelGap)
                            {
                                state.bestLevelGap = levelDistance;
                                state.bestLevelMin = row.levelMin;
                                state.bestLevelMax = highestMetLevel(row);
                            }
                            state.stage = STAGE_LEVEL;
                        }
                        continue;
                    }

                    const uint16_t violations = violationsFor(row, pokemon, shiny, flawless);
                    if (!state.sawFull)
                        state.bestKind = row.kind;
                    state.sawFull = true;
                    state.stage = STAGE_FULL;
                    state.violations &= violations;
                    if (violations == 0)
                    {
                        state.matched = true;
                        state.bestKind = row.kind;
                        break;
                    }
                }
            }
        }

        /**
         * Which Gen 1 and Gen 2 games a record can have come from.
         *
         * GEN 1 AND GEN 2 TRADE THROUGH THE TIME CAPSULE, and neither format records its game, so
         * PKHeX asks BOTH generations' encounters of any record that could have crossed
         * (EncounterGenerator12). Asking only the format's own group reported Crystal's Articuno --
         * a Red one that came through the Time Capsule -- as a species Johto never had. Two facts
         * narrow the question, both PKHeX's (EncounterEnumerator2):
         *
         *   CRYSTAL'S CAUGHT DATA PINS THE GAME. A record carrying it was caught or hatched in
         *   Crystal, so Crystal alone is asked -- Gold and Silver never write the word, and a Gen 1
         *   game has nowhere to keep it.
         *
         *   A GEN 2 RECORD WITHOUT IT CAME FROM CRYSTAL ONLY IF IT COULD HAVE VISITED GEN 1, because
         *   crossing is the one way to clear the word: its chain has to reach a Gen 1 species
         *   (GBRestrictions.CanVisitGen1). A Celebi with no caught data is not Crystal's GS Ball one.
         *
         * `partnerTable` is the other generation's, set only when the record could have crossed.
         */
        struct TimeCapsuleScope
        {
            uint8_t versionBit;
            const EncounterTable *partnerTable;
            Enums::GameVersion partnerGroup;
        };

        TimeCapsuleScope getTimeCapsuleScope(Enums::GameVersion formatGroup, bool hasMetData, const Candidate *chain, int chainLength, uint8_t versionBit)
        {
            TimeCapsuleScope scope{versionBit, nullptr, Enums::GameVersion::Invalid};
            if (formatGroup == Enums::GameVersion::GSC)
            {
                const uint8_t crystalBit = encounterVersionBit(Enums::GameVersion::C);
                if (hasMetData)
                {
                    scope.versionBit = crystalBit;
                    return scope;
                }
                bool canVisitGen1 = false;
                for (int chainIndex = 0; chainIndex < chainLength; ++chainIndex)
                {
                    canVisitGen1 = canVisitGen1 || chain[chainIndex].species <= Pokemon::MAX_SPECIES_GEN1;
                }
                if (!canVisitGen1)
                {
                    scope.versionBit = static_cast<uint8_t>(versionBit & ~crystalBit);
                    return scope;
                }
                scope.partnerGroup = Enums::GameVersion::RBY;
            }
            else if (formatGroup == Enums::GameVersion::RBY)
            {
                scope.partnerGroup = Enums::GameVersion::GSC;
            }
            else
            {
                return scope;
            }
            scope.partnerTable = getEncounterTable(scope.partnerGroup);
            return scope;
        }

        /**
         * Could this Pokemon have hatched from an egg in this table's game, judged on the one thing a
         * record with no met data still has -- its CURRENT level? The bottom of its chain has to be
         * one the game breeds, and the Pokemon at least the level an egg hatches at and past every
         * evolution level above that.
         *
         * Nothing in such a record EVIDENCES an egg the way met level 0 at a hatch location does, so
         * this only ever explains a Pokemon the sweep could not; where the story fails it says
         * nothing, and the sweep's own finding stands. Routing every breedable species to the egg path
         * instead turned "Butterfree at level 9 would have to evolve from Metapod at 10" into a
         * sentence about a Caterpie egg the record never mentioned.
         */
        bool eggExplainsWithoutMetData(const EncounterTable &table, const Pokemon::Pokemon &pokemon, const Candidate *chain, int chainLength)
        {
            if (!table.egg.hasBreeding || !breedable(table, chain[chainLength - 1].species))
                return false;
            const uint8_t level = pokemon.level();
            if (level != 0 && level < table.egg.hatchCurrentLevel)
                return false;
            const uint8_t evolutionFloor = chain[chainLength - 1].levelFloor;
            return evolutionFloor == 0 || level == 0 || level >= evolutionFloor;
        }

        /// Folds a second sweep into the first, keeping whichever explains the Pokemon further. At the
        /// same LEVEL stage the nearer band wins, and at FULL a violation survives only if every
        /// template that got that far has it -- the same ANY-of rule a single sweep applies.
        void mergeMatchState(MatchState &state, const MatchState &other)
        {
            if (state.matched)
                return;
            if (other.matched || other.stage > state.stage)
            {
                state = other;
                return;
            }
            if (other.stage != state.stage)
                return;
            if (other.stage == STAGE_LEVEL && other.bestLevelGap < state.bestLevelGap)
            {
                state.bestLevelGap = other.bestLevelGap;
                state.bestLevelMin = other.bestLevelMin;
                state.bestLevelMax = other.bestLevelMax;
            }
            if (other.stage == STAGE_EVOLUTION && other.evolutionFloor < state.evolutionFloor)
            {
                state.evolutionFloor = other.evolutionFloor;
                state.evolutionFrom = other.evolutionFrom;
            }
            if (other.stage == STAGE_FULL)
            {
                state.violations &= other.violations;
            }
        }

        /// Is this egg a scripted GIFT egg, rather than a daycare one? BDSP are the
        /// only games that hand out any (Happiny from the Traveling Man, Riolu from
        /// Riley), and each has its own egg-location ID — not the nursery's, and not
        /// a species the daycare would ever produce.
        /**
         * Which Mystery Gift EGG row explains this record, if any.
         *
         * TWO SHAPES, because Gen 3 has no egg-location field. Everywhere else that field IS the
         * distribution's signature and a hatched Pokemon still carries it, so the row is found by
         * comparing it. In Gen 3 the only record that can name a distribution is an egg that has
         * NOT hatched yet: it sits at the event location carrying the row's met level, which is
         * exactly what PKHeX compares before it gives up on a hatched one --
         * `EncounterGift3.IsMatchExact` answers `if (hatchedEgg) return true; // defer egg specific
         * checks to later`. So a hatched Gen 3 gift egg is indistinguishable from a bred one here
         * for the same reason it is there, and is judged as the ordinary egg it looks like.
         *
         * Without the second shape Gen 3's 66 distribution-egg rows could never match anything at
         * all: the sweep skips every EGG row, and the first shape asks for a field the format does
         * not have.
         */
        const EncounterRow *matchingGiftEgg(const EncounterTable &table, const Pokemon::Pokemon &pokemon, const Candidate *chain, int chainLength, uint8_t versionBit, uint16_t eggLocation)
        {
            const bool formatCarriesEggLocation = pokemon.hasEggData();
            for (int chainIndex = 0; chainIndex < chainLength; ++chainIndex)
            {
                const uint16_t species = chain[chainIndex].species;
                if (species + 1 >= table.speciesIndexLength)
                    continue;
                const uint16_t firstRow = table.speciesIndex[species];
                const uint16_t lastRow = table.speciesIndex[species + 1];
                for (uint16_t rowIndex = firstRow; rowIndex < lastRow; ++rowIndex)
                {
                    const EncounterRow &row = table.rows[rowIndex];
                    if ((row.flags & ENCOUNTER_FLAG_EGG) == 0)
                        continue;
                    if ((row.versions & versionBit) == 0)
                        continue;
                    if (formatCarriesEggLocation)
                    {
                        // An egg location of 0 is "no egg location", not a value to match against:
                        // it would pair every ordinary record with a row that records none either.
                        if (eggLocation != 0 && row.eggLocation == eggLocation)
                            return &row;
                        continue;
                    }
                    if (pokemon.isEgg() && pokemon.metLocation() == row.location && pokemon.metLevel() == row.levelMin)
                        return &row;
                }
            }

            return nullptr;
        }

        /**
         * The egg path: A hatched egg has no wild slot to match: what is has is a
         * nursery ID, the set of places the game lets an egg hatch, a fixed hatch level, and
         * the requirement that the bottom of its evolution chain is something the daycare
         * will actually produce.
         */
        void checkEgg(const EncounterTable &table, const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup, uint8_t originVersion, uint8_t versionBit, const Candidate *chain, int chainLength, uint16_t eggLocation, Report &report)
        {
            const EggRule &egg = table.egg;
            const uint16_t baseSpecies = chain[chainLength - 1].species;
            const std::string groupName = Enums::getGameVersionName(originGroup);
            if (!egg.hasBreeding)
            {
                add(report, Severity::Warning, groupName + " has no breeding, but this Pokémon carries an egg location");
                return;
            }
            const EncounterRow *giftEggRow = matchingGiftEgg(table, pokemon, chain, chainLength, versionBit, eggLocation);
            const bool giftEgg = giftEggRow != nullptr;
            // Name a MYSTERY GIFT egg, and only that. Accepting a distribution egg in silence is also what
            // a row that can never match looks like, so nothing could tell the two apart. The other egg rows
            // this matches are the games' OWN gift eggs, and several of them carry the day care's own egg
            // location (SM/USUM's Eevee is 60002), so naming those would tell a player who bred an Eevee
            // that they had been handed one.
            if (giftEgg && giftEggRow->kind == ENCOUNTER_KIND_EVENT)
            {
                add(report, Severity::Info, "Matches a Mystery Gift distribution egg in " + groupName);
            }
            if (eggLocation != 0 && !giftEgg && !isBredEggLocation(egg, eggLocation))
            {
                add(report, Severity::Warning, "Egg location " + locationText(originVersion, eggLocation) + " is not where " + groupName + " hands out eggs");
            }
            // A gift egg is handed over directly, so the daycare's breeding rules do not apply
            // to is — Riolu is unbreedable in BDSP and Riley gives you one anyway.
            if (!giftEgg && !breedable(table, baseSpecies))
            {
                add(report, Severity::Warning, std::string(Trainer::getSpeciesName(baseSpecies)) + " cannot hatch from an egg");
            }
            // not hatched yet, no metadata to check
            if (pokemon.isEgg()) return;

            const uint16_t metLocation = pokemon.metLocation();
            if (!canHatchAt(egg, metLocation, versionBit))
            {
                add(report, Severity::Warning, locationText(originVersion, metLocation) + " is not somewhere an egg can hatch in " + groupName);
            }
            if (pokemon.metLevel() != egg.hatchLevel)
            {
                add(report, Severity::Warning, "A hatched egg is met at level " + std::to_string(egg.hatchLevel) + ", not " + std::to_string(pokemon.metLevel()));
            }
            // A HATCHLING IS NOT BELOW THE LEVEL IT HATCHES AT. Gen 2 and Gen 3 eggs hatch at level
            // 5 and everything since at level 1, so a level 3 Gen 3 record did not come from one
            // whatever its met data says — and that is a different number from the met level this
            // path just checked (0 in Gen 3), which is why the table carries both.
            if (pokemon.level() != 0 && pokemon.level() < egg.hatchCurrentLevel)
            {
                add(report, Severity::Invalid, "An egg hatches at level " + std::to_string(egg.hatchCurrentLevel) + " in " + groupName + ", but this Pokémon is level " + std::to_string(pokemon.level()));
            }
            // WHAT HATCHES IS THE BOTTOM OF THE CHAIN, so everything above it had to be evolved
            // into — and an egg gives no way round an evolution level the way a wild slot can. A
            // level 12 Noctowl has no egg story in any game: the egg was a Hoothoot and Hoothoot
            // becomes Noctowl at 20. Invalid rather than a Warning because it is arithmetic.
            const uint8_t evolutionFloor = chain[chainLength - 1].levelFloor;
            if (evolutionFloor != 0 && pokemon.level() != 0 && pokemon.level() < evolutionFloor)
            {
                add(report, Severity::Invalid, "A hatched egg produces " + std::string(Trainer::getSpeciesName(baseSpecies)) + ", which has to reach level " + std::to_string(evolutionFloor) + " to become " + std::string(Trainer::getSpeciesName(pokemon.speciesID())) + " — this Pokémon is level " + std::to_string(pokemon.level()));
            }
        }

        void reportViolations(uint16_t violations, const Pokemon::Pokemon &pokemon, Report &report)
        {
            if (violations & VIOLATION_SHINY_LOCK)
                add(report, Severity::Invalid, "This encounter is shiny-locked, but the Pokémon is shiny");
            if (violations & VIOLATION_SHINY_FORCED)
                add(report, Severity::Warning, "This encounter is always shiny, but the Pokémon is not shiny");
            if (violations & VIOLATION_BALL)
                add(report, Severity::Warning, std::string("This encounter has a fixed ball, but this is a ") + Enums::getBallName(pokemon.ball()));
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

    namespace
    {
        /// Relearn ("remembered") moves.
        ///
        /// Two things are decidable here and one is not. DECIDABLE: a relearn slot holding a move
        /// the species can never learn at all, and the same move in two slots -- neither is
        /// reachable in-game. NOT decidable: whether a learnable move was legal as the ENCOUNTER's
        /// move specifically -- the egg moves are folded into the learnable pool by
        /// gen_learnsets.py, not kept apart, and no encounter row records its moves. So the pool is
        /// the same one Layer 2 uses for the current moveset, and a move inside it is left alone
        /// rather than guessed at.
        void checkRelearnMoves(const Pokemon::Pokemon &pokemon, Report &report, Enums::GameVersion originGroup, uint16_t species)
        {
            uint16_t relearn[4] = {0, 0, 0, 0};
            bool any = false;
            for (int slot = 0; slot < 4; ++slot)
            {
                relearn[slot] = pokemon.relearnMove(slot);
                any = any || relearn[slot] != 0;
            }
            // nothing set, including every format that has no relearn slots at all
            if (!any) return;

            for (int slot = 0; slot < 4; ++slot)
            {
                if (relearn[slot] == 0)
                    continue;
                if (std::string(Names::getMoveName(relearn[slot])) == "-")
                {
                    add(report, Severity::Invalid, "Unknown relearn move in slot " + std::to_string(slot + 1));
                    continue;
                }
                for (int other = slot + 1; other < 4; ++other)
                    if (relearn[slot] == relearn[other])
                    {
                        add(report, Severity::Invalid, "Duplicate relearn move: " + std::string(Names::getMoveName(relearn[slot])));
                        break;
                    }
            }

            // nullptr means "no table", which is not the same as "cannot learn it" -- reporting the
            // two alike is the standing trap with this project's data tables, and here it would
            // delete a legal move through the transfer sanitizer downstream.
            if (Pokemon::getLearnableBits(species, pokemon.form(), originGroup) == nullptr)
                return;
            for (int slot = 0; slot < 4; ++slot)
            {
                if (relearn[slot] == 0)
                    continue;
                if (!Pokemon::isLearnable(species, pokemon.form(), originGroup, relearn[slot]))
                    add(report, Severity::Warning, "Relearn move may not be learnable: " + std::string(Names::getMoveName(relearn[slot])));
            }
        }

        /// Ribbons and marks.
        ///
        /// The per-ribbon "which game could award this" table is PKHeX's RibbonVerifier, which is a
        /// large port and not attempted. What IS settled by the record alone is the boundary MARKS
        /// sit behind: they arrived with Sword/Shield, and no transfer path adds one afterwards --
        /// so a mark on a Pokemon that originated before Gen 8 cannot have been earned, whatever
        /// encounter it came from. That is a hard call rather than a guess, which is why it is the
        /// one ribbon rule here that is Invalid.
        ///
        /// The egg case is deliberately softer. An unhatched egg has not been anywhere to earn a
        /// ribbon -- but PKHeX's own verifier notes that some EVENT eggs are distributed carrying
        /// them, so a fateful-encounter egg is left alone entirely and everything else is a Warning.
        void checkRibbons(const Pokemon::Pokemon &pokemon, Report &report, Enums::GameVersion originVersion, bool fatefulEncounter)
        {
            // Marks are told apart by their definition rather than by their display name, which is
            // in the active language.
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
            int heldCount = 0;
            int markCount = 0;
            std::string firstMark;
            for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(pokemon.getGameGroup()))
            {
                const uint8_t ribbonValue = Names::getRibbonValue(entityData, ribbon);
                if (ribbonValue == 0)
                    continue;
                ++heldCount;
                if (!Names::isRibbonMark(ribbon))
                    continue;
                if (markCount == 0)
                    firstMark = Names::getRibbonName(ribbon, ribbonValue);
                ++markCount;
            }
            // none set, including every format that stores none
            if (heldCount == 0) return;

            // Origin generation 0 means the format records no origin at all, so there is nothing to
            // compare against -- silence, not a finding.
            const int originGeneration = Enums::getVersionGeneration(static_cast<uint8_t>(originVersion));
            if (markCount > 0 && originGeneration > 0 && originGeneration < 8)
                add(report, Severity::Invalid, "Marks were introduced in Sword/Shield, but this Pokémon carries " + firstMark + " and originated in " + Enums::getGameVersionName(originVersion));

            if (pokemon.isEgg() && !fatefulEncounter)
                add(report, Severity::Warning, "An unhatched egg carries " + std::to_string(heldCount) + " ribbon/mark(s), which it has not been anywhere to earn");
        }

        /// The Pokemon HOME tracker.
        ///
        /// HOME stamps a per-Pokemon id on anything that passes through it, and it is the one piece
        /// of transfer evidence a save carries that cannot be written by the games themselves. That
        /// makes it the check the origin byte cannot be: a Gen 4 Pokemon sitting in a Violet box got
        /// there through HOME and must have a tracker, whatever else its record says.
        ///
        /// PKHeX's rule (HomeTrackerUtil.IsRequired) is "the encounter's context differs from the
        /// entity's, or the encounter predates Gen 8, or it is a HOME gift". PKSE has no encounter
        /// contexts, but it has the two facts those reduce to here -- which generation the record
        /// says it came from, and which generation it is stored in now.
        ///
        /// WARNING, never Invalid, and that is deliberate. PKSE's own bank moves Pokemon along
        /// routes no official transfer offers, and its converted output legitimately has no tracker
        /// -- flagging that as illegal would be flagging the feature, which is the same policy the
        /// met-location check follows. The finding says what is missing and leaves the judgement to
        /// the reader.
        void checkHomeTracker(const Pokemon::Pokemon &pokemon, Report &report, Enums::GameVersion originVersion)
        {
            // no such field, or it is present -- either way nothing to say
            if (!pokemon.hasHomeTracker() || pokemon.homeTracker() != 0) return;

            const int currentGeneration = Enums::getVersionGeneration(static_cast<uint8_t>(Enums::getGroupRepVersion(pokemon.getGameGroup())));
            const int originGeneration = Enums::getVersionGeneration(static_cast<uint8_t>(originVersion));
            // the record does not say where it came from; nothing to compare
            if (originGeneration == 0) return;

            if (originGeneration < 8)
                add(report, Severity::Warning, "Came from " + Enums::getGameVersionName(originVersion) + ", which can only reach this game through Pokémon HOME, but carries no HOME tracker");
            else if (originGeneration != currentGeneration)
                add(report, Severity::Warning, "Came from another generation (" + Enums::getGameVersionName(originVersion) + ") but carries no HOME tracker");
        }

        /// Original Trainer and handling trainer.
        ///
        /// The handler flag is the one part of this that a record can contradict outright: it names
        /// which of the two trainers the game currently gives friendship, affection and memories
        /// to, so setting it to the HANDLER while the handler field is blank points at nobody. Every
        /// other combination is a real state -- an HT with the flag back on the OT is a pokemon that was
        /// traded and traded home again -- and is left alone.
        void checkTrainers(const Pokemon::Pokemon &pokemon, Report &report)
        {
            if (pokemon.otName().empty())
                add(report, Severity::Warning, "This Pokémon has no Original Trainer recorded");

            // Gens 1-5 have no handler field; nothing to be consistent with
            if (!pokemon.hasHandler()) return;

            const bool handlerNamed = !pokemon.htName().empty();
            if (pokemon.currentHandler() == 1 && !handlerNamed)
                add(report, Severity::Invalid, "The handling trainer is marked as the current handler, but no handling trainer is set");
            if (!handlerNamed && pokemon.htFriendship() != 0)
                add(report, Severity::Warning, "Handling-trainer friendship is set (" + std::to_string(pokemon.htFriendship()) + ") but no handling trainer is");
        }
    }

    void checkEncounter(const Pokemon::Pokemon &pokemon, Report &report)
    {
        const uint16_t species = pokemon.speciesID();
        if (species == 0)
            return;

        // The three checks that do not depend on an encounter row. They run FIRST and unconditionally,
        // because everything below returns early for an origin PKSE has no tables for -- and a mark on a
        // Gen 4 Pokemon is wrong whether or not its met location can be explained.
        checkTrainers(pokemon, report);

        const uint8_t originRaw = pokemon.originGame();
        const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(originRaw);
        checkRibbons(pokemon, report, originVersion, pokemon.isFatefulEncounter());
        checkHomeTracker(pokemon, report, originVersion);
        // Relearn moves are judged against the pool of the game the pokemon came FROM, like every other
        // learnability question here -- a Sword Pokemon sitting in a Violet box is still a Sword one.
        // A format that records no origin falls back to its own group, which is the only pool there
        // is for it and matches what the met-location display already does.
        checkRelearnMoves(pokemon, report, pokemon.hasOriginGame() && originRaw != 0 ? Enums::getGameGroup(originVersion) : pokemon.getGameGroup(), species);

        // AN ORIGIN CANNOT BE FROM A LATER GENERATION THAN THE RECORD HOLDING IT. Transfers only
        // ever go forward, so a PK9 claiming Sword is ordinary and a PK3 claiming HeartGold is
        // impossible -- Gen 3's origin field is four bits wide and its games occupy 1-5.
        //
        // This has to be tested before the table lookup, not after. The lookup keys on the version
        // byte alone, so an impossible origin resolves to a real table from the wrong generation
        // and every location in it is measured in a namespace the record never used: a PKSM-built
        // Gen 3 Pokemon stamped with origin 7 reported "location253 is not a place Bulbasaur can
        // be encountered in HeartGold" -- confidently, and about a game it was never in.
        //
        // Reported rather than skipped, and as Invalid, because nothing legitimate produces it:
        // every PKSE conversion clamps the origin to a game the destination format has (see
        // remapPK8toPK3), so this is always a record somebody else built wrong. A generation of 0
        // means the byte matches no game at all, which is a DIFFERENT statement -- that one falls
        // through to the "no encounter data" note below rather than being called impossible.
        const int originGeneration = Enums::getVersionGeneration(originRaw);
        const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup()));
        if (originGeneration != 0 && formatGeneration != 0 && originGeneration > formatGeneration)
        {
            add(report, Severity::Invalid, "Origin game " + Enums::getGameVersionName(originVersion) + " is from Generation " + std::to_string(originGeneration) + ", which this Generation " + std::to_string(formatGeneration) + " record cannot have come from");
            return;
        }

        // PAL PARK AND POKE TRANSFER OVERWRITE THE MET LOCATION; every later transfer preserves it.
        // Moving a Pokemon out of Gen 3 stamps it "Pal Park" and out of Gen 4 "Poke Transfer", both
        // in the DESTINATION's namespace, while the origin byte still names the game it was caught
        // in. Bank and HOME, from Gen 5 on, carry the original met location across instead -- which
        // is why a Sword Pokemon in a Z-A box still matches its Galar encounter.
        //
        // So a record older than its format by that boundary is holding a location the origin
        // game's tables cannot be asked about: Pal Park is location 55 in Gen 4, and 55 in Gen 3 is
        // Granite Cave, so a perfectly ordinary transferred Geodude read as "Granite Cave is not a
        // place Geodude can be encountered in FireRed". Abstain rather than answer in the wrong
        // namespace.
        if (originGeneration != 0 && formatGeneration != 0 && originGeneration < formatGeneration && originGeneration <= 4)
        {
            add(report, Severity::Info, "Transferred out of " + Enums::getGameVersionName(originVersion) + ", which replaced its met location — encounter check skipped");
            return;
        }

        // A FORMAT THAT RECORDS NO ORIGIN FALLS BACK TO ITS OWN GROUP, which is the only pool
        // there is for it -- the same rule checkRelearnMoves already applies a few lines up. A PK1
        // and a PK2 store no version byte anywhere, so "which game did this come from" has exactly
        // one answer available: the group whose format it is in. And because it cannot name WHICH
        // of that group's games, every one of them counts -- a mask of all bits, so a row for any
        // of Red, Green, Blue or Yellow is a row this Pokemon could have come from.
        //
        // A GEN 9 EGG THAT HAS NOT HATCHED has no origin yet either: the game writes the
        // version when it hatches (PKHeX PK9.IsUnhatchedEgg is `Version == 0 && IsEgg`). Its game
        // is the save's own, so it takes the same fallback rather than "origin game ID 0".
        const bool unhatchedEggWithoutOrigin = pokemon.isEgg() && originRaw == 0 && formatGeneration == 9;
        const bool originIsFormatGroup = !pokemon.hasOriginGame() || unhatchedEggWithoutOrigin;
        const EncounterTable *table = originIsFormatGroup ? getEncounterTable(pokemon.getGameGroup()) : getEncounterTable(originVersion);
        const uint8_t versionBit = originIsFormatGroup ? 0xFF : encounterVersionBit(originVersion);

        // No tables for this origin, so say that the check did not run
        if (table == nullptr || versionBit == 0)
        {
            // A format with no origin field at all is a different statement from one whose origin
            // simply has no table: nothing was omitted, the record cannot carry the information.
            // Saying "Origin game ID 0" about a Red/Blue Pokemon reads like corrupt data.
            if (!pokemon.hasOriginGame())
            {
                add(report, Severity::Info, "This format records no origin game — encounter check skipped");
            }
            else
            {
                // NAME THE GAME. "Origin game ID 8" is a number the reader has to go and look up,
                // and it reads like corrupt data rather than the plain coverage gap it is. The id
                // stays, after the name, for the case the name cannot be resolved.
                const std::string originName = Enums::getGameVersionName(originVersion);
                const std::string originLabel = (originName.empty() || originName == "Unknown") ? "Origin game ID " + std::to_string(originRaw) : originName + " (origin game ID " + std::to_string(originRaw) + ")";
                add(report, Severity::Info, originLabel + " has no encounter data — encounter check skipped");
            }
            return;
        }
        const Enums::GameVersion originGroup = originIsFormatGroup ? pokemon.getGameGroup() : Enums::getGameGroup(originVersion);
        const std::string gameName = Enums::getGameVersionName(originGroup);
        // A FORMAT WITH NO ORIGIN BYTE NAMES ITS LOCATIONS THROUGH ITS GROUP. Crystal records a met
        // location with no version beside it, and the name lookup is keyed by version, so every Gen 2
        // finding could only say "location34" where the game says Route 42.
        const uint8_t locationNameVersion = originIsFormatGroup ? Enums::getGroupRepVersion(originGroup) : originRaw;

        const uint16_t metLocation = pokemon.metLocation();
        // The sentinel replaced the met location, so the sweep below would be measuring a number the
        // origin game never wrote: it has to stop here, not merely say so and carry on.
        if (isHomeRemappedLocation(metLocation))
        {
            add(report, Severity::Info, "HOME replaced the met location on transfer — encounter check skipped");
            return;
        }
        const bool arrivedFromOutside = isExternalTransferLocation(originGroup, metLocation);

        Candidate chain[Pokemon::EVO_MAX_CHAIN + 1];
        const int chainLength = buildChain(originGroup, species, pokemon.form(), chain, Pokemon::EVO_MAX_CHAIN + 1);

        // Gen 1 and Gen 2 records narrow which of their own games count, and may have to be asked of
        // the other generation as well -- see getTimeCapsuleScope. Every other format passes through.
        const bool hasMetData = pokemon.hasMetData();
        const TimeCapsuleScope capsule = originIsFormatGroup ? getTimeCapsuleScope(originGroup, hasMetData, chain, chainLength, versionBit) : TimeCapsuleScope{versionBit, nullptr, Enums::GameVersion::Invalid};
        Candidate partnerChain[Pokemon::EVO_MAX_CHAIN + 1];
        const int partnerChainLength = capsule.partnerTable != nullptr ? buildChain(capsule.partnerGroup, species, pokemon.form(), partnerChain, Pokemon::EVO_MAX_CHAIN + 1) : 0;
        const std::string searchedGameName = capsule.partnerTable != nullptr ? gameName + " or " + Enums::getGameVersionName(capsule.partnerGroup) : gameName;

        // An egg location is definitive where the format has the field: Gen 3
        // has none, so a hatched FRLG egg is recognized by its fixed hatch location
        // and level instead, and only after the normal sweep has failed to explain it.
        const uint16_t eggLocation = pokemon.eggLocation();
        const bool fromEgg = hasEggLocation(eggLocation);
        if (fromEgg || pokemon.isEgg())
        {
            checkEgg(*table, pokemon, originGroup, locationNameVersion, capsule.versionBit, chain, chainLength, fromEgg ? eggLocation : 0, report);
            return;
        }

        const bool isShiny = pokemon.isShiny(pokemon.id32(), Trainer::getSpeciesName(species));
        const int flawless = flawlessCount(pokemon);

        MatchState state;
        sweep(*table, chain, chainLength, capsule.versionBit, metLocation, pokemon.metLevel(), pokemon, isShiny, flawless, state, hasMetData);
        if (state.matched)
        {
            add(report, Severity::Info, std::string("Matches a ") + kindText(state.bestKind) + " in " + gameName + (hasMetData ? " at " + locationText(locationNameVersion, metLocation) : ""));
            return;
        }
        if (capsule.partnerTable != nullptr)
        {
            MatchState partnerState;
            sweep(*capsule.partnerTable, partnerChain, partnerChainLength, 0xFF, metLocation, pokemon.metLevel(), pokemon, isShiny, flawless, partnerState, hasMetData);
            if (partnerState.matched)
            {
                add(report, Severity::Info, std::string("Matches a ") + kindText(partnerState.bestKind) + " in " + Enums::getGameVersionName(capsule.partnerGroup) + ", traded through the Time Capsule");
                return;
            }
            mergeMatchState(state, partnerState);
        }

        // Nothing matched, and the met location says the Pokemon came from somewhere PKSE has no
        // tables for. Abstain -- see isExternalTransferLocation.
        if (arrivedFromOutside)
        {
            add(report, Severity::Info, "Came from Pokémon GO/HOME — those encounters are not modelled, encounter check skipped");
            return;
        }

        // GENS 2 AND 3 HIDE THEIR HATCHED EGGS: neither format has an egg-location field, so a
        // bred Pokemon looks exactly like a wild catch until the sweep fails. What gives it away is
        // a MET LEVEL OF 0 (1 in Crystal) at a place an egg is allowed to hatch — a pair no real
        // encounter produces, since the games never write met level 0 for anything caught. Read it
        // as an egg before concluding the Pokemon has no origin. Restricted to formats without the
        // field — everywhere else the field already answered, above.
        if (hasMetData && table->egg.hasBreeding && table->egg.eggLocation == 0 && pokemon.metLevel() == table->egg.hatchLevel && canHatchAt(table->egg, metLocation, capsule.versionBit))
        {
            checkEgg(*table, pokemon, originGroup, locationNameVersion, capsule.versionBit, chain, chainLength, 0, report);
            return;
        }
        // A RECORD WITH NO MET DATA CANNOT SHOW THAT PAIR, so for it an egg is only ever POSSIBLE --
        // PKHeX's VerifyEncounterEgg2 accepts a Gen 2 egg with no caught data outright, and a Gen 1
        // record may have hatched in Gen 2 and crossed back. Without this, Elm's Togepi egg from
        // Gold read as a species Johto never had. Possible is all it is, so it explains a Pokemon
        // only where the story holds, and never replaces a finding the sweep made -- see
        // eggExplainsWithoutMetData.
        if (!hasMetData)
        {
            if (eggExplainsWithoutMetData(*table, pokemon, chain, chainLength))
            {
                add(report, Severity::Info, "Could have hatched from an egg in " + gameName);
                return;
            }
            if (capsule.partnerTable != nullptr && eggExplainsWithoutMetData(*capsule.partnerTable, pokemon, partnerChain, partnerChainLength))
            {
                add(report, Severity::Info, "Could have hatched from an egg in " + Enums::getGameVersionName(capsule.partnerGroup) + ", traded through the Time Capsule");
                return;
            }
        }

        // Every Mystery Gift PKHeX ships for these six games is IN the tables, so a fateful pokemon
        // that still matches nothing is a real signal. It stays a Warning rather than Invalid, because
        // the distributions PKHeX has are the ones it has: a card it never archived, or one for a game
        // with no pickle at all (Gen 3, whose events live in hand-written arrays of a different shape),
        // is a gap in the data and not evidence about the Pokemon.
        const bool event = pokemon.isFatefulEncounter();
        const Severity hard = event ? Severity::Warning : Severity::Invalid;

        switch (state.stage)
        {
        case STAGE_NONE:
            add(report, hard, "No encounter in " + searchedGameName + " produces " + describe(species, pokemon.form()) + (event ? " — event Pokémon are not checked" : ""));
            break;
        case STAGE_EVOLUTION:
            add(report, hard, "Nothing in " + searchedGameName + " produces " + describe(species, pokemon.form()) + " at level " + std::to_string(pokemon.level()) + ": it would have to evolve from " + std::string(Trainer::getSpeciesName(state.evolutionFrom)) + " at level " + std::to_string(state.evolutionFloor) + (event ? " — event Pokémon are not checked" : ""));
            break;
        case STAGE_FORM:
            add(report, hard, describe(species, pokemon.form()) + " has no encounter in " + searchedGameName + " in this form:");
            break;
        case STAGE_LOCATION:
            add(report, hard, locationText(locationNameVersion, metLocation) + " is not a place " + std::string(Trainer::getSpeciesName(species)) + " can be encountered in " + searchedGameName + (event ? " — event Pokémon are not checked" : ""));
            break;
        case STAGE_LEVEL:
        {
            const std::string band = state.bestLevelMin == state.bestLevelMax ? std::to_string(state.bestLevelMin) : std::to_string(state.bestLevelMin) + "-" + std::to_string(state.bestLevelMax);
            // A record with no met data is being judged on its CURRENT level against a floor, so
            // the met-level wording would name a field it does not have.
            if (!hasMetData)
            {
                add(report, Severity::Warning, "The lowest level " + std::string(Trainer::getSpeciesName(species)) + " is available at in " + searchedGameName + " is " + std::to_string(state.bestLevelMin) + ", above this Pokémon's level of " + std::to_string(pokemon.level()));
                break;
            }
            add(report, Severity::Warning, "No encounter for " + std::string(Trainer::getSpeciesName(species)) + " at " + locationText(locationNameVersion, metLocation) + " is met at level " + std::to_string(pokemon.metLevel()) + " (nearest is " + band + ")");
            break;
        }
        case STAGE_FULL:
            reportViolations(state.violations, pokemon, report);
            break;
        }
    }
}
