/**
 * Every one of the seven Switch formats can convert to every other, Sun/Moon and Ultra Sun/Moon join
 * them, and Gen 1/2 feed in one-way through Poke Transporter -- ninety-odd ordered pairs before
 * anything is said about species, forms, moves or items. Checking that by hand means depositing a
 * Pokemon in the bank, withdrawing it into another game, opening the details view and reading every
 * row -- times ninety, times however many kinds of Pokemon are worth trying. Nobody does that, which
 * is why this suite exists.
 *
 * It runs from the SAMPLE POOL the per-target sweep filled: a handful of real Pokemon from each
 * generation on the console, cloned on the way past. Real mons rather than synthesised ones on
 * purpose -- the conversions that break are the ones carrying a foreign OT, an origin from a game
 * that is not either endpoint, a form the destination has never heard of, or a HOME tracker.
 *
 * What is asserted, and why each one:
 *
 *   canConvert() and convert() AGREE. canConvert is documented as the pure check for gating the UI
 *   without doing the work. If the two ever disagree, the UI offers a drop the code then refuses,
 *   or refuses one it would have accepted.
 *
 *   IDENTITY SURVIVES. Species, form, IVs, the 32-bit trainer ID and the OT name are what make a
 *   Pokemon that Pokemon; a conversion is a re-format, not a rebuild. (Nature and gender are
 *   deliberately NOT asserted: going to Gen 3 they become PID-derived, because Gen 3 stores
 *   neither.)
 *
 *   MOVES ARE LEGAL WHERE THEY LAND. The sanitizer is required, not cosmetic: Let's Go turns a pokemon
 *   carrying an impossible move into a Bad Egg. So every move that survives must be learnable by
 *   that species in the destination AND exist in the destination at all.
 *
 *   THE AFFIXED RIBBON IS NOT ZERO. "No ribbon" is 0xFF; 0 is a real index, and index 0 is the Kalos
 *   Champion ribbon. A zero-initialised byte hands the pokemon a ribbon it does not own -- which has
 *   already happened twice, from two different directions.
 *
 *   GEN 1 AND GEN 2 GO ONE WAY. Nothing has ever moved a Pokemon back into a Game Boy game, so
 *   every conversion INTO RBY or GSC must come back Unsupported. Out of them is the Poke Transporter
 *   route, and it is checked against the rules Transporter actually followed rather than against the
 *   ordinary "identity survives" ones -- a transfer that did NOT reroll the IVs, floor the EXP, reset
 *   the EVs or stamp a Poke Ball would be the bug.
 */
#include <memory>
#include <string>
#include <vector>

#include "Test/TestSuite.h"
#include "Conversion/Convert.h"
#include "Enums/Ball.h"
#include "Globals.h"
#include "Legality/EncounterTable.h"
#include "Pokemon/EvolutionTable.h"
#include "Legality/Legality.h"
#include "Names/ItemPresence.h"
#include "Names/MoveNames.h"
#include "Names/MovePresence.h"
#include "Pokemon/Experience.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/PersonalInfo7USUM.h"
#include "Enums/LanguageID.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Trainer/Bank.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        /// Every destination the matrix is swept against. RBY and GSC are included precisely so the
        /// refusal is tested rather than assumed.
        const Enums::GameVersion DESTINATIONS[] = {
            Enums::GameVersion::RBY,
            Enums::GameVersion::GSC,
            Enums::GameVersion::FRLG,
            Enums::GameVersion::RSE,
            Enums::GameVersion::SM,
            Enums::GameVersion::USUM,
            Enums::GameVersion::GG,
            Enums::GameVersion::SWSH,
            Enums::GameVersion::BDSP,
            Enums::GameVersion::PLA,
            Enums::GameVersion::SV,
            Enums::GameVersion::ZA,
        };
        constexpr int DESTINATION_COUNT = (int)(sizeof(DESTINATIONS) / sizeof(DESTINATIONS[0]));

        /// The formats conversion connects, stated here rather than read from Convert.cpp's gate, so a
        /// gate that wrongly refuses a whole pair fails instead of being filed as a refusal: Gen 3,
        /// Gen 7, Let's Go, and every Switch format. Gens 1 and 2 join one way, as SOURCES only (Poke
        /// Transporter); Gens 4-6 are not connected at all.
        bool inConversionFamily(Enums::GameVersion group)
        {
            using Enums::GameVersion;
            return Enums::isGen3Group(group) || group == GameVersion::SM || group == GameVersion::USUM || group == GameVersion::GG || group == GameVersion::SWSH || group == GameVersion::BDSP || group == GameVersion::PLA || group == GameVersion::SV || group == GameVersion::ZA;
        }

        /// Every format a conversion can START from, for the coverage gap report.
        const Enums::GameVersion SOURCE_GROUPS[] = {
            Enums::GameVersion::RBY,  Enums::GameVersion::GSC, Enums::GameVersion::RSE,  Enums::GameVersion::FRLG,
            Enums::GameVersion::SM,   Enums::GameVersion::USUM, Enums::GameVersion::GG,  Enums::GameVersion::SWSH,
            Enums::GameVersion::BDSP, Enums::GameVersion::PLA, Enums::GameVersion::SV,   Enums::GameVersion::ZA,
        };

        const char *resultName(Conversion::Result result)
        {
            switch (result)
            {
            case Conversion::Result::Ok:
                return "Ok";
            case Conversion::Result::SameGroup:
                return "SameGroup";
            case Conversion::Result::Unsupported:
                return "Unsupported";
            case Conversion::Result::NotInDex:
                return "NotInDex";
            case Conversion::Result::Blocked:
                return "Blocked";
            default:
                return "?";
            }
        }

        int ivOf(const Pokemon::Pokemon &p, int statIndex)
        {
            switch (statIndex)
            {
            case 0:
                return p.ivHP();
            case 1:
                return p.ivATK();
            case 2:
                return p.ivDEF();
            case 3:
                return p.ivSPE();
            case 4:
                return p.ivSPA();
            default:
                return p.ivSPD();
            }
        }

        /// Whether this conversion may RAISE the level. Only one kind can: with Auto-Legalize on, a Pokemon
        /// from outside Gen 3 is REBUILT as a native of a Gen 3 destination, and is re-created at a level
        /// a real encounter there can produce -- the nearest one, so the raise is the smallest that works.
        /// Nothing lowers a level, ever, and nothing else changes one: Gen 3's own two groups trade on real
        /// hardware and are never rebuilt. The console ran this suite under the user's own setting, so with
        /// the toggle on every such raise was reported as a failure the design intends.
        bool rebuildMayRaiseLevel(Enums::GameVersion sourceGroup, Enums::GameVersion destination)
        {
            return g_autoLegalizeTransfers && Enums::isGen3Group(destination) && !Enums::isGen3Group(sourceGroup);
        }

        /// `transporter` = this pair went out of Gen 1/2 through Poke Transporter, which is the one
        /// conversion that deliberately REPLACES things the others preserve. Three assertions below
        /// are about preservation and do not apply to it; they are checked by transporterRules()
        /// against what Transporter actually promises instead.
        void checkConverted(Context &context, const Pokemon::Pokemon &source, const Pokemon::Pokemon &out, Enums::GameVersion destination, const std::string &whereLabel, bool transporter)
        {
            context.check(out.getGameGroup() == destination, "converted Pokemon reports the wrong format group", whereLabel + " reported=" + Enums::getGameVersionName(out.getGameGroup()));
            context.check(out.isStructurallyValid(), "converted Pokemon is not structurally valid", whereLabel);

            context.check(out.speciesID() == source.speciesID(), "conversion changed the species", whereLabel + " before=" + std::to_string(source.speciesID()) + " after=" + std::to_string(out.speciesID()));
            context.check(out.formID() == source.formID(), "conversion changed the form", whereLabel + " before=" + std::to_string(source.formID()) + " after=" + std::to_string(out.formID()));
            if (rebuildMayRaiseLevel(source.getGameGroup(), destination))
            {
                context.check(out.level() >= source.level(), "a Gen 3 rebuild lowered the level", whereLabel + " before=" + std::to_string(source.level()) + " after=" + std::to_string(out.level()));
            }
            else
            {
                context.check(out.level() == source.level(), "conversion changed the level", whereLabel + " before=" + std::to_string(source.level()) + " after=" + std::to_string(out.level()));
            }

            // A REBUILD NEVER LANDS AT THE EVENT LOCATION. Allowing the level to rise is what would let
            // that bug hide: the landing search once took Mystery Gift rows as candidates, and a level 6
            // Bulbasaur "became" the level 70 10th-anniversary distribution -- which still passes a check
            // that only asks whether the level went down. Gen 3 meets Pokemon Colosseum's gifts there
            // too, and they are no more this Pokemon's story. One whose origin IS Gen 3 keeps its own
            // met location, which can be that event, so only a foreign one is held to this.
            const bool originIsGen3 = source.hasOriginGame() && source.originGame() >= 1 && source.originGame() <= 5;
            if (rebuildMayRaiseLevel(source.getGameGroup(), destination) && !originIsGen3)
            {
                context.check(out.metLocation() != Legality::ENCOUNTER_LOCATION_EVENT3, "a Gen 3 rebuild claimed an event or Colosseum gift the Pokemon never received", whereLabel + " met=" + std::to_string(out.metLocation()) + " level=" + std::to_string(out.level()));
            }

            // Transporter rerolls the IVs on purpose -- a Gen 1/2 DV is 4 bits wide and there is no
            // honest widening of it -- so this asks the opposite question there (see below).
            if (!transporter)
            {
                for (int index = 0; index < 6; ++index)
                    context.check(ivOf(out, index) == ivOf(source, index), "conversion changed an IV", whereLabel + " stat=" + std::to_string(index) + " before=" + std::to_string(ivOf(source, index)) + " after=" + std::to_string(ivOf(out, index)));
            }

            context.check(out.id32() == source.id32(), "conversion changed the trainer ID", whereLabel + " " + hex("before", source.id32(), 8) + " " + hex("after", out.id32(), 8));
            // Gen 3 down-converts transcode the OT into its own charset, so a name with characters
            // Gen 3 lacks can legitimately come out different. Anywhere else, a changed OT means the
            // pokemon has lost its origin.
            // A Pokemon received in an in-game Gen 1/2 trade stores a MARKER rather than a name (PKSE
            // renders it "*"), and Bank substitutes the localised "Trainer" for it -- so that one
            // legitimately comes out different too.
            const bool substitutedTradeOT = transporter && source.otName() == std::u16string(u"*");
            const bool nameFitsDestination = !Enums::isGen3Group(destination) || Conversion::canStoreOriginalTrainerName(source, destination);
            if (nameFitsDestination && !substitutedTradeOT)
                context.check(out.otName() == source.otName(), "conversion changed the OT name", whereLabel + " before=\"" + Utils::utf16ToUtf8(source.otName()) + "\" after=\"" + Utils::utf16ToUtf8(out.otName()) + "\"");

            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = out.move(moveIndex);
                if (moveId == 0)
                    continue;
                context.check(Names::isMovePresent(moveId, destination), "conversion left a move the destination game does not have", whereLabel + " move=\"" + Names::getMoveName(moveId) + "\" (" + std::to_string(moveId) + ")");
                // A transferred Pokemon's moveset is legal BECAUSE it learned it in Gen 1/2, and no
                // Gen 7+ learnset records that -- a Yellow Pikachu really does know Surf. Presence in
                // the destination game is the only filter that can honestly apply.
                //
                // SAME-GENERATION TRADES ARE EXEMPT FOR THE SAME REASON, one step down. Gen 3's two
                // groups trade with each other on real hardware, and trading does not revalidate a
                // moveset: a FireRed Charmander carrying Metal Claw keeps it in Ruby, and a Ruby Mew
                // keeps Whirlpool in FireRed, because both learned them legitimately where they came
                // from. The pools genuinely differ (RSE's is wider -- Emerald's Battle Frontier
                // tutors), so checking the DESTINATION's pool would delete moves the cartridges
                // keep. Where an official transfer exists, PKSE does what the games do; only where
                // none exists is the pokemon rebuilt and required to be rebuilt legally.
                const bool sameGenerationTrade = Enums::isGen3Group(source.getGameGroup()) && Enums::isGen3Group(destination);
                if (!transporter && !sameGenerationTrade)
                    context.check(Pokemon::isLearnable(out.speciesID(), out.formID(), destination, moveId), "conversion left a move the species cannot learn in the destination", whereLabel + " move=\"" + Names::getMoveName(moveId) + "\" (" + std::to_string(moveId) + ")");
            }
            if (out.hasHeldItem() && out.heldItem() != 0)
                context.check(Names::isHeldItemPresent(out.heldItem(), destination), "conversion left a held item the destination cannot hold", whereLabel + " item=" + std::to_string(out.heldItem()));
            else if (!out.hasHeldItem())
                context.check(out.heldItem() == 0, "a format with no held-item mechanic came out of conversion holding one", whereLabel + " item=" + std::to_string(out.heldItem()));

            const size_t ribbonOfs = Conversion::affixedRibbonOffset(destination);
            if (ribbonOfs != 0 && ribbonOfs < out.getData().size())
            {
                const uint8_t affixed = (uint8_t)out.getData()[ribbonOfs];
                context.check(affixed != 0, "converted Pokemon's AffixedRibbon is 0, which displays the Kalos Champion ribbon it does not own (\"none\" is 0xFF)", whereLabel + " " + hex("offset", ribbonOfs, 3));
            }
        }

        /// What Poke Transporter PROMISES, as opposed to what it preserves. Every one of these is a
        /// rule the transfer invents rather than carries, so each is checked against the rule rather
        /// than against the source Pokemon (PKHeX PK1.ConvertToPK7 / PK2.ConvertToPK7).
        void transporterRules(Context &context, const Pokemon::Pokemon &source, const Pokemon::Pokemon &out, const std::string &whereLabel)
        {
            const bool fromGen2 = source.getGameGroup() == Enums::GameVersion::GSC;
            const uint16_t species = out.speciesID();
            const bool isEventPair = species == 151 || (fromGen2 && species == 251);

            // Met data is entirely invented: a Poke Ball, the arrival level, and the one hardcoded
            // location Bank stamps -- "Kanto region" out of Gen 1, "Johto region" out of Gen 2.
            context.check(out.ball() == 4, "a transferred Pokemon did not arrive in a Poke Ball", whereLabel + " ball=" + std::to_string(out.ball()));
            context.check(out.metLocation() == (fromGen2 ? 30017 : 30013), "a transferred Pokemon has the wrong met location", whereLabel + " location=" + std::to_string(out.metLocation()));
            context.check(out.metLevel() == out.level(), "a transferred Pokemon's met level is not the level it arrived at", whereLabel + " met=" + std::to_string(out.metLevel()) + " level=" + std::to_string(out.level()));

            // The origin has to name a Gen 1/2 title. PKHeX numbers them 35-41 (Red..Crystal), and a
            // Gen 1 record must never come out claiming a Gen 2 game or the reverse.
            const uint8_t originGame = out.originGame();
            const bool originInRange = fromGen2 ? (originGame >= 39 && originGame <= 41) : (originGame >= 35 && originGame <= 38);
            context.check(originInRange, "a transferred Pokemon's origin game is not one of its own generation's", whereLabel + " origin=" + std::to_string(originGame));

            // EXP is floored to the arrival level, so the two must agree exactly. The curve is Gen
            // 7's own -- that is the table Transporter computed against.
            const uint8_t growthRate = Pokemon::getPersonalInfo7USUM(species, out.formID()).growthRate;
            context.check(out.exp() == Pokemon::getExpForLevel(out.level(), growthRate), "a transferred Pokemon kept partial EXP; Transporter floors it to the level", whereLabel + " exp=" + std::to_string(out.exp()) + " level=" + std::to_string(out.level()));

            // Nature comes from the EXP the Pokemon had BEFORE that flooring. Reading it after would
            // give every transferred Pokemon one of a handful of natures.
            context.check(out.nature() == static_cast<uint8_t>(source.exp() % 25), "a transferred Pokemon's nature is not its source EXP mod 25", whereLabel + " nature=" + std::to_string(out.nature()) + " expected=" + std::to_string(source.exp() % 25));

            // Stat Experience is dropped, not converted.
            context.check(out.evHP() == 0 && out.evATK() == 0 && out.evDEF() == 0 && out.evSPE() == 0 && out.evSPA() == 0 && out.evSPD() == 0, "a transferred Pokemon arrived with EVs; Gen 1/2 Stat Experience is dropped", whereLabel);

            // Three guaranteed 31s, five for the event-only pair. Not "at least" -- the count is the
            // rule, and a sixth would mean the shuffle wrote over a random roll.
            int flawless = 0;
            for (int index = 0; index < 6; ++index)
                if (ivOf(out, index) == 31)
                    ++flawless;
            context.check(flawless >= (isEventPair ? 5 : 3), "a transferred Pokemon has too few flawless IVs", whereLabel + " flawless=" + std::to_string(flawless));

            // Almost everything gets its HIDDEN ability. AbilityNumber is a bit, not an index:
            // 1 = slot 1, 4 = hidden.
            context.check(out.abilityNumber() == 1 || out.abilityNumber() == 4, "a transferred Pokemon has an ability slot Transporter never assigns", whereLabel + " abilityNumber=" + std::to_string(out.abilityNumber()));

            // Shininess has no field to carry it across -- it is a DV pattern in Gen 1/2 and a
            // PID/ID xor here -- so the PID is forced to reproduce the FACT.
            context.check(out.isShiny(out.id32(), "") == source.isShiny(source.id32(), ""), "a transfer changed whether the Pokemon is shiny", whereLabel + std::string(" before=") + (source.isShiny(source.id32(), "") ? "Y" : "N") + " after=" + (out.isShiny(out.id32(), "") ? "Y" : "N"));

            // Language has to be a real one. Gen 1/2 store none and infer it from the record width;
            // 0 is what a format that forgot to answer returns, and no Gen 7+ game accepts it.
            context.check(out.language() != 0, "a transferred Pokemon arrived with no language", whereLabel + " language=" + std::to_string(out.language()));

            context.check(out.isFatefulEncounter() == isEventPair, "the fateful-encounter flag is set on the wrong Pokemon (only Mew and Celebi get it)", whereLabel + std::string(" fateful=") + (out.isFatefulEncounter() ? "Y" : "N"));

            // Dizzy Punch is the one move Transporter refuses to carry out of Gen 2.
            if (fromGen2)
                for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                    context.check(out.move(moveIndex) != 146, "Dizzy Punch survived a Gen 2 transfer", whereLabel);
        }
        /// WHERE A FOREIGN POKEMON LANDS WHEN IT IS WRITTEN INTO GEN 3.
        ///
        /// A Gen 3 met location is a byte in its own id space; every later generation's is a u16 in a
        /// different one. Carrying the number across is meaningless, and carrying it means TRUNCATING it:
        /// 30013, the Gen 1 -> Gen 7 transfer marker, becomes 61 -- "Abandoned Ship", a HOENN location,
        /// stamped on Kanto pokemon that were never in either region.
        ///
        /// So the landing has to be JUSTIFIED rather than merely in range: either a real encounter for
        /// this species in the destination game, or the documented first-town fallback for a species that
        /// game cannot produce. A truncated id is neither, which is what makes this bite. It does not
        /// assert legality -- no transfer into Gen 3 has ever existed, so the pokemon is illegal whatever
        /// it carries; it asserts the location is POSSIBLE there.
        void gen3LandingRules(Context &context, const Pokemon::Pokemon &out, uint8_t destVersion, const std::string &whereLabel)
        {
            const uint16_t location = out.metLocation();
            const uint16_t species = out.speciesID();
            const std::string where = whereLabel + " species=" + std::to_string(species) + " location=" + std::to_string(location);
            if (!context.check(location <= 0xFF, "a Gen 3 met location does not fit the format's byte", where))
                return;

            const auto destination = static_cast<Enums::GameVersion>(destVersion);
            // THE TABLE IS THE RECORD'S OWN ORIGIN, NOT THE DESTINATION'S. The rebuild may stamp
            // the other Gen 3 group -- a Hoothoot in a FireRed save is an EMERALD pokemon, because
            // Kanto has none and Emerald's Safari Zone does -- and asking FireRed's table about a
            // Hoenn location finds nothing and fails for a reason that has nothing to do with the
            // landing being wrong.
            const auto originVersion = static_cast<Enums::GameVersion>(out.originGame());
            const Legality::EncounterTable *table = Legality::getEncounterTable(originVersion);
            // The egg is the DESTINATION's, though: a hatchling is met where the player was walking,
            // which is the game the pokemon is being written into.
            const Legality::EncounterTable *destinationTable = Legality::getEncounterTable(destination);
            const uint8_t fallback = (destVersion == static_cast<uint8_t>(Enums::GameVersion::FR) || destVersion == static_cast<uint8_t>(Enums::GameVersion::LG)) ? 88 : 0; // Pallet Town : Littleroot Town
            // A hatched egg is a legal landing too, and for a species the game cannot produce it is
            // the ONLY one -- Gen 3 records no egg location, so a hatchling carries where it hatched.
            bool justified = (location == fallback);
            if (!justified && destinationTable != nullptr && destinationTable->egg.hatchLocations != nullptr && location < destinationTable->egg.hatchLocationCount)
            {
                justified = (destinationTable->egg.hatchLocations[location] != 0);
            }
            // The whole PRE-EVOLUTION CHAIN counts: a Wartortle is a caught Squirtle that evolved,
            // so a Squirtle location justifies it exactly as the checker treats it.
            uint16_t chain[Pokemon::EVO_MAX_CHAIN + 1] = {species};
            uint8_t chainForms[Pokemon::EVO_MAX_CHAIN + 1] = {};
            const int chainLength = 1 + Pokemon::getPreEvolutionChain(originVersion, species, 0, chain + 1, chainForms + 1, Pokemon::EVO_MAX_CHAIN);
            for (int link = 0; link < chainLength && !justified; ++link)
            {
                const uint16_t linkSpecies = chain[link];
                if (table == nullptr || table->speciesIndex == nullptr || linkSpecies == 0 || linkSpecies + 1 >= table->speciesIndexLength)
                    continue;
                for (uint16_t rowIndex = table->speciesIndex[linkSpecies]; rowIndex < table->speciesIndex[linkSpecies + 1] && !justified; ++rowIndex)
                {
                    justified = (table->rows[rowIndex].location == location);
                }
            }
            context.check(justified, "a pokemon written into Gen 3 landed somewhere that game cannot produce it", where + " (neither an encounter for this species nor the first-town fallback)");
        }

        /// The Poke Transporter pass that needs NO SAVE.
        ///
        /// Most consoles have no Gen 1 or Gen 2 file on them, so the sample-driven sweep above would
        /// leave the whole Transporter route untested on almost every run -- and the cases that are
        /// most likely to break are the rare ones a real Red save has none of: Unown's letter, an
        /// in-game-trade OT, Celebi, the Dizzy Punch removal, Crystal naming itself. These are built
        /// from scratch instead. A blank entity is a legitimate PK1/PK2 (Gen 1 has no checksum and
        /// Gen 2's is on the save, not the record), so nothing here is a fixture pretending to be a
        /// save file.
        void transporterSynthetic(Context &context)
        {
            const Conversion::ReceivingTrainer receiving{Utils::utf8ToUtf16("PKSE"), 0};

            auto transferred = [&](Pokemon::Pokemon &source, const std::string &label)-> std::unique_ptr<Pokemon::Pokemon>
            {
                Conversion::Result result = Conversion::Result::Ok;
                auto out = Conversion::convert(source, Enums::GameVersion::USUM, result, 0, &receiving);
                if (!context.check(out != nullptr && result == Conversion::Result::Ok, "a synthetic Poke Transporter transfer was refused", label + " result=" + resultName(result)))
                    return nullptr;
                checkConverted(context, source, *out, Enums::GameVersion::USUM, label, true);
                transporterRules(context, source, *out, label);
                return out;
            };

            // Exercised with the rebuild ON, which is what it is about; the OFF path is checked
            // separately below.
            const bool restoreLegalizeForLanding = g_autoLegalizeTransfers;
            g_autoLegalizeTransfers = true;
            // Gen 1 -> Poke Transporter -> ... -> PK3. The transporter stamps met location 30013
            // ("Kanto"), which is a Gen 7 id; writing it into Gen 3's byte truncated it to 61,
            // "Abandoned Ship". Both Gen 3 destinations are driven, because FireRed and LeafGreen
            // do not share every encounter and the fallback differs by region.
            for (uint8_t destinationVersion : {static_cast<uint8_t>(Enums::GameVersion::FR),
                                               static_cast<uint8_t>(Enums::GameVersion::LG),
                                               static_cast<uint8_t>(Enums::GameVersion::EM)})
            {
                for (uint16_t species : {25u, 1u, 151u, 129u})
                {
                    Pokemon::Pokemon1RBY pokemon(false);
                    pokemon.setSpecies(species);
                    pokemon.setExp(21012);
                    pokemon.setTID16(12345);
                    pokemon.setDV16(0xABCD);
                    pokemon.recalculateStats();

                    Conversion::Result result = Conversion::Result::Ok;
                    auto landed = Conversion::convert(pokemon, Enums::GameVersion::FRLG, result, destinationVersion, &receiving);
                    const std::string label = "synthetic Gen 1 -> Gen 3 version=" + std::to_string(destinationVersion);
                    if (!context.check(landed != nullptr && result == Conversion::Result::Ok, "a synthetic Gen 1 -> Gen 3 transfer was refused", label + " species=" + std::to_string(species) + " result=" + resultName(result)))
                        continue;
                    gen3LandingRules(context, *landed, destinationVersion, label);
                    // The exact symptom, pinned by name so a regression is unmistakable.
                    context.check(landed->metLocation() != 61, "a transferred Gen 1 pokemon landed on \"Abandoned Ship\" -- the Gen 7 transfer marker 30013 truncated into Gen 3's byte", label + " species=" + std::to_string(species));
                }
            }

            // No game has ever allowed a transfer into Gen 3, so PKSE rebuilds the pokemon as a native
            // Gen 3 one. Rules 1-7 were real defects found against a live PKSM bank of 252 Gen 1/2
            // pokemon, where only 93 of them came out clean; 8 and 9 turned up rebuilding every
            // pokemon in a real collection, whatever generation it came from.
            {
                // These four rules only apply when the user has opted into a rebuild. Off, PKSE
                // leaves an obvious placeholder for them to set by hand -- checked at the end.
                const bool restoreLegalize = g_autoLegalizeTransfers;
                g_autoLegalizeTransfers = true;
                const uint8_t intoFireRed = static_cast<uint8_t>(Enums::GameVersion::FR);
                auto toFireRed = [&](Pokemon::Pokemon &source, const std::string &label)-> std::unique_ptr<Pokemon::Pokemon>
                {
                    Conversion::Result result = Conversion::Result::Ok;
                    auto out = Conversion::convert(source, Enums::GameVersion::FRLG, result, intoFireRed, &receiving);
                    if (!context.check(out != nullptr && result == Conversion::Result::Ok, "a Gen 1/2 -> FireRed rebuild was refused", label + " result=" + resultName(result)))
                        return nullptr;
                    gen3LandingRules(context, *out, intoFireRed, label);
                    return out;
                };

                // 1. UNOWN'S LETTER IS THE PID. A reroll that ignores it renames the pokemon, and the
                //    Tanoby chambers hold five letters each, so a relettered Unown also lands where
                //    its own letter never appears.
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(201);
                    pokemon.setExp(30000);
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.setDV16(0x6A5C); // some letter or other -- whichever it is must survive
                    pokemon.recalculateStats();
                    const uint8_t letterBefore = pokemon.formID();
                    if (auto out = toFireRed(pokemon, "synthetic Unown -> FireRed"))
                    {
                        context.check(out->form() == letterBefore, "a transferred Unown came out as a different letter", "before=" + std::to_string(letterBefore) + " after=" + std::to_string(out->form()));
                    }
                }

                // 2. AN EVOLVED POKEMON IS ITS PRE-EVOLUTION'S ENCOUNTER. Wartortle is never caught
                //    in FireRed; it is a Squirtle that evolved. Looking up the species alone found
                //    nothing and dropped 120 of 252 on the fallback town.
                {
                    Pokemon::Pokemon1RBY pokemon(false);
                    pokemon.setSpecies(8); // Wartortle
                    pokemon.setExp(40000);
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.recalculateStats();
                    if (auto out = toFireRed(pokemon, "synthetic Wartortle -> FireRed"))
                    {
                        const Legality::Report report = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        std::string why;
                        for (const auto &issue : report.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                                continue;
                            why += (why.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(report.problemCount() == 0, "an evolved pokemon rebuilt into Gen 3 is not legal", "Wartortle loc=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()) + " level=" + std::to_string(out->level()) + " -- " + why);
                    }
                }

                // 3. HALF OF KANTO IS VERSION-EXCLUSIVE. Sandshrew exists only in LeafGreen, and the
                //    way you hold one in FireRed is to TRADE for it -- which leaves LeafGreen as the
                //    origin. Stamping the destination instead made 21 pokemon unexplainable.
                {
                    Pokemon::Pokemon1RBY pokemon(false);
                    pokemon.setSpecies(27); // Sandshrew -- LeafGreen only
                    pokemon.setExp(40000);
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.recalculateStats();
                    if (auto out = toFireRed(pokemon, "synthetic Sandshrew -> FireRed"))
                    {
                        context.check(out->originGame() == static_cast<uint8_t>(Enums::GameVersion::LG), "a LeafGreen-exclusive pokemon was not stamped as traded from LeafGreen", "origin=" + std::to_string(out->originGame()));
                    }
                }

                // 4. WHAT THE GAME CANNOT PRODUCE, IT CAN STILL HATCH. FireRed never has a Chikorita,
                //    so no met location makes a caught one legal -- but Gen 3 records no egg location,
                //    so a hatchling is recognised by its hatch location and level alone.
                //
                //    THE SPECIES HAS TO BE ONE NEITHER GEN 3 GROUP PRODUCES, and Chikorita stopped
                //    being one: Emerald hands out a Johto starter at Littleroot Town once the Hoenn
                //    dex is complete, so the cross-group search now explains it with a real gift and
                //    never reaches the egg at all. That is the better answer and the test moved to a
                //    BABY instead -- Elekid, Pichu, Togepi and the rest are catchable nowhere in Gen
                //    3 and breedable everywhere in it.
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(239); // Elekid -- no Gen 3 encounter anywhere, breeds fine
                    pokemon.setExp(40000);
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.recalculateStats();
                    if (auto out = toFireRed(pokemon, "synthetic Elekid -> FireRed"))
                    {
                        const Legality::EncounterTable *table = Legality::getEncounterTable(Enums::GameVersion::FR);
                        const bool hatched = table != nullptr && out->metLocation() < table->egg.hatchLocationCount && table->egg.hatchLocations[out->metLocation()] != 0 && out->metLevel() == table->egg.hatchLevel;
                        context.check(hatched, "a species no Gen 3 game can produce was not rebuilt as a hatched egg", "Elekid location=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()) + " (want the day care's own map, met level " + std::to_string(table != nullptr ? table->egg.hatchLevel : 0) + ")");
                        // A HATCHLING IS NOT BELOW THE LEVEL IT HATCHES AT. Gen 3 eggs hatch at 5,
                        // and the met level a hatched egg carries is 0 -- two different numbers that
                        // were one for a long time, which is what made every bred Gen 3 Pokemon read
                        // as having no encounter at all.
                        context.check(table == nullptr || out->level() >= table->egg.hatchCurrentLevel, "a rebuilt hatchling came out below the level an egg hatches at", "Elekid level=" + std::to_string(out->level()));
                        const Legality::Report report = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        context.check(report.problemCount() == 0, "a pokemon rebuilt as a hatched Gen 3 egg is still flagged", "Elekid loc=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()));
                    }
                }

                // 5. WHERE NOTHING FITS, THE CLOSEST ENCOUNTER WINS -- not the lowest location id.
                //    Machop has three FireRed rows, all wild: Victory Road 32-34 (132), Rock Tunnel
                //    16-17 (138) and Mt. Ember 31-39 (175). A level 15 Machop satisfies none of
                //    them, so the level and wild tests tied three ways and the id tiebreak decided
                //    -- handing Rock Tunnel, ONE level short of legal, a loss to Victory Road by six
                //    id numbers. The checker then reported "nearest is 32-34", which is true of the
                //    place it was sent and useless to the user.
                //
                //    Pinned by location rather than by a re-derivation of the rule: a test that
                //    recomputes "closest" is only ever asserting that the rule equals itself.
                //
                //    THE REBUILD RAISES THE LEVEL TO MEET IT. It did not use to, and the pokemon
                //    stayed flagged for being one level short -- 30 of a 252-pokemon bank were
                //    exactly that. With the toggle on PKSE is re-creating the pokemon as a native of
                //    the destination, so it arrives at a level the encounter can produce: level 16,
                //    met at 16, at Rock Tunnel. Only ever upward, and only with the toggle on.
                {
                    Pokemon::Pokemon1RBY pokemon(false);
                    pokemon.setSpecies(66); // Machop
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.setLevel(15);
                    if (auto out = toFireRed(pokemon, "synthetic level 15 Machop -> FireRed"))
                    {
                        context.check(out->metLocation() == 138, "a pokemon below every encounter's level landed at the lowest location id instead of the closest encounter", "Machop level=" + std::to_string(out->level()) + " location=" + std::to_string(out->metLocation()) + " (want 138 Rock Tunnel 16-17, not 132 Victory Road 32-34)" + " metLevel=" + std::to_string(out->metLevel()));
                        context.check(out->metLevel() == 16 && out->level() == 16, "a rebuild left a pokemon below the encounter it landed on", "Machop metLevel=" + std::to_string(out->metLevel()) + " level=" + std::to_string(out->level()) + " (want 16/16 -- Rock Tunnel starts at 16)");
                        const Legality::Report machopReport = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        context.check(machopReport.problemCount() == 0, "a rebuilt pokemon raised into its encounter's band is still flagged", "Machop level=" + std::to_string(out->level()));
                    }
                }

                // 6. THE OTHER GEN 3 GROUP IS A LANDING TOO. FireRed has no Hoothoot at any level;
                //    EMERALD's Safari Zone has one at 35, and Hoenn and Kanto trade over a link
                //    cable. Stamping FireRed left a record no Gen 3 game can explain, so the rebuild
                //    stamps the game that can -- and raises the pokemon to the level that slot
                //    produces, exactly as rule 5 does for a slot in the destination's own table.
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(163); // Hoothoot -- Route 29 in Gen 2, nowhere in Kanto
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.setLevel(6);
                    if (auto out = toFireRed(pokemon, "synthetic Hoothoot -> FireRed"))
                    {
                        context.check(out->originGame() == static_cast<uint8_t>(Enums::GameVersion::EM) && out->metLocation() == 57 && out->metLevel() == 35 && out->level() == 35, "a species only the other Gen 3 group produces was not stamped with it", "Hoothoot origin=" + std::to_string(out->originGame()) + " location=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()) + " level=" + std::to_string(out->level()) + " (want Emerald 3, Safari Zone 57, 35/35)");
                        const Legality::Report report = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        context.check(report.problemCount() == 0, "a Hoothoot rebuilt onto its Emerald encounter is still flagged", "Hoothoot level=" + std::to_string(out->level()));
                    }
                }

                // 7. AN EGG DOES NOT EXCUSE AN EVOLUTION LEVEL, which is what made a level 12
                //    NOCTOWL read as clean: nothing in FireRed produces one, so the rebuild claimed
                //    it hatched -- and the checker agreed, because a Hoothoot egg is a perfectly
                //    good story right up until you notice a Hoothoot is a Noctowl only from level
                //    20. The Emerald slot is the honest answer and it is the one that has to come
                //    out; what must NOT come out is a hatched egg below the evolution level.
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(164); // Noctowl -- Route 2 in Gen 2, nowhere in Gen 3
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.setLevel(12);
                    if (auto out = toFireRed(pokemon, "synthetic level 12 Noctowl -> FireRed"))
                    {
                        const Legality::EncounterTable *destinationTable = Legality::getEncounterTable(Enums::GameVersion::FR);
                        const bool claimedAnEgg = destinationTable != nullptr && out->metLevel() == destinationTable->egg.hatchLevel && out->metLocation() < destinationTable->egg.hatchLocationCount && destinationTable->egg.hatchLocations[out->metLocation()] != 0;
                        context.check(!claimedAnEgg || out->level() >= 20, "a pokemon below its own evolution level was rebuilt as a hatched egg", "Noctowl level=" + std::to_string(out->level()) + " location=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()));
                        const Legality::Report report = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        std::string why;
                        for (const auto &issue : report.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                                continue;
                            why += (why.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(report.problemCount() == 0, "a Noctowl rebuilt into Gen 3 is not legal", "Noctowl origin=" + std::to_string(out->originGame()) + " loc=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()) + " level=" + std::to_string(out->level()) + " -- " + why);
                    }
                }

                // 8. NOTHING IS REBUILT ONTO THE EVENT LOCATION. Gen 3 meets every Mystery Gift
                //    distribution AND Pokemon Colosseum's gifts at location 255, and Colosseum's are
                //    neither distributions nor fateful -- so leaving out those two kinds of row still
                //    let a level 60 Ho-Oh from Crystal land on the one Colosseum hands out after Mt.
                //    Battle, raised to 70 on the way. No Gen 3 encounter explains a Ho-Oh that received
                //    none of that, so the honest landing is the fallback, at the level it already had.
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(250); // Ho-Oh
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.setLevel(60);
                    if (auto out = toFireRed(pokemon, "synthetic level 60 Ho-Oh -> FireRed"))
                    {
                        context.check(out->metLocation() != Legality::ENCOUNTER_LOCATION_EVENT3 && out->level() == 60, "a rebuild landed on a Pokemon Colosseum gift the Pokemon never received", "Ho-Oh location=" + std::to_string(out->metLocation()) + " level=" + std::to_string(out->level()) + " (want neither location 255 nor a raise to Mt. Battle's 70)");
                    }
                }

                // 9. A MET LEVEL BELOW THE ENCOUNTER IS LIFTED TO IT, AND THE LEVEL STAYS PUT. A
                //    Pokemon can outgrow an encounter's floor after being MET below it: a level 63
                //    Charizard from Ultra Sun, met at level 1, lands on FireRed's Pallet Town gift,
                //    which starts at 5. Keeping the source's met level left it flagged at a row it
                //    otherwise fits -- 97 of 2204 rebuilds of a real collection. The met level is
                //    invented along with the location, so it goes to the floor; the level does not.
                {
                    Pokemon::Pokemon7USUM pokemon;
                    pokemon.setSpecies(6); // Charizard
                    pokemon.setOriginGame(static_cast<uint8_t>(Enums::GameVersion::US));
                    pokemon.setMetLevel(1);
                    pokemon.setLevel(63);
                    pokemon.setBall(static_cast<uint8_t>(Enums::Ball::Poke)); // the gift's own ball
                    pokemon.setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.recalculateStats();
                    pokemon.refreshChecksum();
                    const std::string label = "synthetic level 63 Charizard met at 1 -> FireRed";
                    Conversion::Result result = Conversion::Result::Ok;
                    auto out = Conversion::convert(pokemon, Enums::GameVersion::FRLG, result, intoFireRed, &receiving);
                    if (context.check(out != nullptr && result == Conversion::Result::Ok, "a Gen 7 -> FireRed rebuild was refused", label + " result=" + resultName(result)))
                    {
                        gen3LandingRules(context, *out, intoFireRed, label);
                        context.check(out->metLocation() == 88 && out->metLevel() == 5 && out->level() == 63, "a rebuild left a met level below the floor of the encounter it landed on", "Charizard location=" + std::to_string(out->metLocation()) + " metLevel=" + std::to_string(out->metLevel()) + " level=" + std::to_string(out->level()) + " (want Pallet Town 88, met at 5, still level 63)");
                        const Legality::Report report = Legality::analyze(*out, Enums::GameVersion::FRLG);
                        std::string why;
                        for (const auto &issue : report.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                                continue;
                            why += (why.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(report.problemCount() == 0, "a Pokemon rebuilt onto an encounter it outgrew is still flagged", "Charizard metLevel=" + std::to_string(out->metLevel()) + " -- " + why);
                    }
                }

                // 10. WITH THE TOGGLE OFF, NOTHING IS REBUILT. The user asked to fix it themselves,
                //    so PKSE leaves the first entry of the game's own met-location list -- a value
                //    that is obviously unset rather than one that looks plausible and gets missed.
                g_autoLegalizeTransfers = false;
                {
                    Pokemon::Pokemon2GSC pokemon(false);
                    pokemon.setSpecies(152); // Chikorita -- FireRed has no encounter for it
                    pokemon.setExp(40000);
                    pokemon.setTID16(12345);
                    pokemon.setOTName(Utils::utf8ToUtf16("PKSE"));
                    pokemon.recalculateStats();
                    Conversion::Result result = Conversion::Result::Ok;
                    auto out = Conversion::convert(pokemon, Enums::GameVersion::FRLG, result, intoFireRed, &receiving);
                    if (context.check(out != nullptr && result == Conversion::Result::Ok, "a Gen 1/2 -> FireRed transfer was refused with legalize off", std::string("result=") + resultName(result)))
                    {
                        context.check(out->metLocation() == 0, "with auto-legalize off a transfer still rewrote the met location", "location=" + std::to_string(out->metLocation()));
                    }
                }
                g_autoLegalizeTransfers = restoreLegalize;
            }

            g_autoLegalizeTransfers = restoreLegalizeForLanding;

            // Returning to your origin game is the one backward transfer that is genuinely legal,
            // so a Gen 3 met location has to survive the round trip untouched. It does that by
            // arithmetic (widened to u16 on the way up, narrowed back down), which is exactly why
            // the truncation looked harmless -- this path was never wrong.
            {
                std::vector<std::byte> record(Trainer::Bank::recordSizeFor(Enums::GameVersion::FRLG), std::byte{0});
                auto original = Trainer::Bank::makePokemon(Enums::GameVersion::FRLG, record);
                if (!context.check(original != nullptr, "no Gen 3 entity class for the round trip", "round trip"))
                    return;
                original->setSpecies(1);
                original->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::FR));
                original->setMetLocation(88); // Pallet Town -- a real Gen 3 id
                original->setMetLevel(5);
                original->setExp(1000);
                original->recalculateStats();

                Conversion::Result upResult = Conversion::Result::Ok;
                auto up = Conversion::convert(*original, Enums::GameVersion::SWSH, upResult, 0, &receiving);
                if (context.check(up != nullptr && upResult == Conversion::Result::Ok, "a Gen 3 pokemon could not be sent to Gen 8", "round trip"))
                {
                    Conversion::Result backResult = Conversion::Result::Ok;
                    auto back = Conversion::convert(*up, Enums::GameVersion::FRLG, backResult, static_cast<uint8_t>(Enums::GameVersion::FR), &receiving);
                    if (context.check(back != nullptr && backResult == Conversion::Result::Ok, "a Gen 3 pokemon could not come home again", "round trip"))
                    {
                        context.check(back->metLocation() == 88, "a Gen 3 pokemon returning to its origin game lost its met location", "round trip location=" + std::to_string(back->metLocation()));
                        context.check(back->originGame() == static_cast<uint8_t>(Enums::GameVersion::FR), "a Gen 3 pokemon returning to its origin game lost its origin", "round trip origin=" + std::to_string(back->originGame()));
                    }
                }
            }

            {
                Pokemon::Pokemon1RBY pokemon(false);
                pokemon.setSpecies(25); // Pikachu
                pokemon.setExp(21012);  // not a multiple of 25, so the nature is a real answer
                pokemon.setTID16(12345);
                pokemon.setDV16(0xABCD);
                pokemon.setMove(0, 57); // Surf -- a Gen 7 Pikachu cannot learn it, and it must survive
                pokemon.setMove(1, 84);
                pokemon.setNickname(Utils::utf8ToUtf16("SPARKY"));
                pokemon.setOTName(Utils::utf8ToUtf16("ASH"));
                pokemon.recalculateStats();
                auto out = transferred(pokemon, "synthetic Red Pikachu");
                if (out)
                {
                    context.check(out->nickname() == Utils::utf8ToUtf16("SPARKY"), "a nicknamed Pokemon lost its nickname in transfer", "synthetic Red Pikachu nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
                    context.check(out->isNicknamed(), "a nicknamed Pokemon transferred without the nicknamed flag", "synthetic Red Pikachu");
                    bool keptSurf = false;
                    for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                        keptSurf = keptSurf || out->move(moveIndex) == 57;
                    // The whole point of the presence-only filter: Surf-Pikachu is the canonical
                    // Virtual Console transfer and no Gen 7 learnset admits it.
                    context.check(keptSurf, "the moveset sanitizer deleted a legal Virtual Console move", "synthetic Red Pikachu lost Surf");
                    // ...and having kept it, PKSE must not then call its own output illegal. A
                    // transferred Pokemon's moves are checked against the pool it learned them in
                    // as well as the one it lives in now; without that, every Virtual Console
                    // transfer PKSE performs would immediately warn about itself.
                    for (const auto &issue : Legality::analyze(*out, out->getGameGroup()).issues)
                        context.check(issue.text.find("Move may not be learnable") == std::string::npos, "the legality checker flags a move PKSE's own transfer legitimately carried", "synthetic Red Pikachu issue=\"" + issue.text + "\"");
                    context.check(out->currentHandler() == 1 && !out->htName().empty(), "a transfer with a known receiving trainer left the Pokemon unhandled", "synthetic Red Pikachu");
                }
            }
            {
                // Gengar is on the list Bank refuses the hidden ability to.
                Pokemon::Pokemon1RBY pokemon(false);
                pokemon.setSpecies(94);
                pokemon.setExp(60000);
                pokemon.setMove(0, 95);
                pokemon.setNickname(Utils::utf8ToUtf16("GENGAR"));
                pokemon.setOTName(Utils::utf8ToUtf16("RED"));
                pokemon.recalculateStats();
                auto out = transferred(pokemon, "synthetic Red Gengar");
                if (out)
                    context.check(out->abilityNumber() == 1, "a species Bank withholds the hidden ability from received it anyway", "synthetic Red Gengar abilityNumber=" + std::to_string(out->abilityNumber()));
            }
            {
                // Mew: fateful, five flawless IVs, and its nickname deliberately NOT carried.
                Pokemon::Pokemon1RBY pokemon(false);
                pokemon.setSpecies(151);
                pokemon.setExp(125003);
                pokemon.setMove(0, 1);
                pokemon.setNickname(Utils::utf8ToUtf16("MEWTWO"));
                pokemon.setOTName(Utils::utf8ToUtf16("GF"));
                pokemon.recalculateStats();
                auto out = transferred(pokemon, "synthetic Red Mew");
                if (out)
                    context.check(out->nickname() != Utils::utf8ToUtf16("MEWTWO"), "Mew kept a nickname; Transporter takes the fateful branch instead", "synthetic Red Mew nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
            }

            {
                Pokemon::Pokemon2GSC pokemon(false);
                pokemon.setSpecies(201); // Unown
                pokemon.setExp(8003);
                pokemon.setDV16(0x6AF9);
                pokemon.setMove(0, 233);
                pokemon.setNickname(Utils::utf8ToUtf16("UNOWN"));
                pokemon.setOTName(Utils::utf8ToUtf16("GOLD"));
                pokemon.recalculateStats();
                const uint8_t letter = pokemon.formID();
                context.check(letter != 0, "a synthetic Unown came out as form 0; the DV-derived letter is not being read", "synthetic Gold Unown");
                auto out = transferred(pokemon, "synthetic Gold Unown");
                if (out)
                    context.check(out->formID() == letter, "Unown's letter did not survive the transfer", "synthetic Gold Unown before=" + std::to_string(letter) + " after=" + std::to_string(out->formID()));
            }
            {
                Pokemon::Pokemon2GSC pokemon(false);
                pokemon.setSpecies(155);
                pokemon.setExp(15007);
                pokemon.setMove(0, 52);
                pokemon.setMove(1, 146); // Dizzy Punch -- the one move Transporter refuses to carry
                pokemon.setMove(2, 33);
                pokemon.setNickname(Utils::utf8ToUtf16("Cyndaquil"));
                pokemon.setOTName(Utils::utf8ToUtf16("ETHAN"));
                pokemon.setCaughtData(0x1234); // only Crystal writes this
                pokemon.recalculateStats();
                auto out = transferred(pokemon, "synthetic Crystal Cyndaquil");
                if (out)
                {
                    context.check(out->originGame() == 41, "a record carrying Crystal's caught data did not transfer as Crystal", "synthetic Crystal Cyndaquil origin=" + std::to_string(out->originGame()));
                    // The surviving moves must be PACKED. A hole mid-moveset is not a state the
                    // games produce -- they stop at the first empty slot.
                    context.check(out->move(0) == 52 && out->move(1) == 33 && out->move(2) == 0, "removing Dizzy Punch left a hole in the moveset instead of packing it", "synthetic Crystal Cyndaquil moves=" + std::to_string(out->move(0)) + "/" + std::to_string(out->move(1)) + "/" + std::to_string(out->move(2)));
                }
            }
        }

    }

    //
    // The case this was written for is a real one, reported against a real bank: fourteen Japanese
    // 6IV Dittos imported from PKSM and dropped into FireRed. They arrived called DITTO with no OT
    // at all. Both halves were separate bugs -- the nickname was discarded by policy and rewritten
    // as the ENGLISH species name, and the OT was encoded through the international table, which
    // has no slot for the full-width Latin a Japanese cartridge stores a Roman name in, so it broke
    // at the first character and left the field blank.
    //
    // Synthetic on purpose, like the Poke Transporter pass above: most consoles have no Japanese
    // save, and these are exactly the cases a collection of English saves has none of.
    void gen3LanguageSynthetic(Context &context)
    {
        const uint8_t japanese = static_cast<uint8_t>(Enums::LanguageID::Japanese);
        const uint8_t korean = static_cast<uint8_t>(Enums::LanguageID::Korean);
        const uint8_t english = static_cast<uint8_t>(Enums::LanguageID::English);

        // A Gen 7 Ditto exactly as PKSM's bank holds one: Japanese, never nicknamed (so its
        // nickname field carries the Japanese species name), and a Roman trainer name stored
        // FULL-WIDTH, which is the only way a Japanese cartridge can store one.
        auto japaneseDitto = [&](bool nicknamed, const std::u16string &originalTrainerName)
        {
            auto pokemon = std::make_unique<Pokemon::Pokemon7USUM>();
            pokemon->setSpecies(132);
            pokemon->setExp(21952); // level 28, medium-fast
            pokemon->setTID16(14246);
            pokemon->setLanguage(japanese);
            pokemon->setNickname(nicknamed ? Utils::utf8ToUtf16("メタモン２") : Utils::utf8ToUtf16("メタモン"));
            pokemon->setIsNicknamed(nicknamed);
            pokemon->setOTName(originalTrainerName);
            pokemon->recalculateStats();
            return pokemon;
        };

        auto intoFireRed = [&](Pokemon::Pokemon &source, const std::string &label)-> std::unique_ptr<Pokemon::Pokemon>
        {
            Conversion::Result result = Conversion::Result::Ok;
            auto out = Conversion::convert(source, Enums::GameVersion::FRLG, result, static_cast<uint8_t>(Enums::GameVersion::FR));
            context.check(out != nullptr && result == Conversion::Result::Ok, "a Gen 7 pokemon could not be converted into Gen 3", label + " result=" + std::string(resultName(result)));
            return out;
        };

        // 1. The reported case, end to end.
        {
            const std::u16string fullWidthName = Utils::utf8ToUtf16("Ｋｉａｓｔａ");
            auto source = japaneseDitto(false, fullWidthName);
            auto out = intoFireRed(*source, "synthetic Japanese Ditto");
            if (out)
            {
                context.check(out->language() == japanese, "a Japanese pokemon lost its language converting into Gen 3", "synthetic Japanese Ditto language=" + std::to_string(out->language()));
                context.check(out->nickname() == Utils::utf8ToUtf16("メタモン"), "an un-nicknamed Japanese pokemon did not arrive holding its own language's species name", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
                context.check(out->otName() == fullWidthName, "a full-width Latin OT name was lost converting into Gen 3", "synthetic Japanese Ditto ot=\"" + Utils::utf16ToUtf8(out->otName()) + "\"");
                // The two findings the user actually saw. Both key on an empty OT, so a silent
                // regression here shows up as the report they filed rather than as a failure.
                for (const auto &issue : Legality::analyze(*out, out->getGameGroup()).issues)
                {
                    context.check(issue.text.find("Empty OT name") == std::string::npos && issue.text.find("no Original Trainer") == std::string::npos, "a converted pokemon with an OT name is reported as having none", "synthetic Japanese Ditto issue=\"" + issue.text + "\"");
                }
            }
        }

        // 2. A REAL Japanese nickname is carried, rather than replaced by the species name.
        {
            auto source = japaneseDitto(true, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto out = intoFireRed(*source, "synthetic nicknamed Japanese Ditto");
            if (out)
            {
                context.check(out->nickname() == Utils::utf8ToUtf16("メタモン２"), "a nicknamed Japanese pokemon lost its nickname converting into Gen 3", "synthetic nicknamed Japanese Ditto nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
            }
        }

        // 3. A language Gen 3 never shipped in. Korean has an id Gen 3 reserved and never used, and
        //    no Gen 3 font can draw Hangul -- so the language clamps to English, the nickname falls
        //    back to the ENGLISH species name (which is what the user asked for), and the OT is left
        //    blank rather than filled with a stand-in. canStoreOriginalTrainerName has to agree with
        //    what the conversion then does, because it is what the confirm dialog warns from.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("한국"));
            source->setLanguage(korean);
            source->setNickname(Utils::utf8ToUtf16("메타몽"));
            context.check(!Conversion::canStoreOriginalTrainerName(*source, Enums::GameVersion::FRLG), "a Hangul OT name was reported storable in Gen 3", "synthetic Korean Ditto");
            auto out = intoFireRed(*source, "synthetic Korean Ditto");
            if (out)
            {
                context.check(out->language() == english, "a Korean pokemon kept a language Gen 3 never shipped in", "synthetic Korean Ditto language=" + std::to_string(out->language()));
                context.check(out->nickname() == Utils::utf8ToUtf16("DITTO"), "a pokemon whose language Gen 3 lacks did not fall back to the English species name", "synthetic Korean Ditto nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
                context.check(out->otName().empty(), "an unwritable OT name was replaced with a stand-in instead of left blank", "synthetic Korean Ditto ot=\"" + Utils::utf16ToUtf8(out->otName()) + "\"");
            }
        }

        // 4. ...and a Korean player whose trainer name is Latin keeps it. Deciding from the language
        //    alone rather than from whether the name actually encodes would throw this away, and it
        //    is the common case: the alphabet is what Gen 3 lacks, not the language.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Minho"));
            source->setLanguage(korean);
            context.check(Conversion::canStoreOriginalTrainerName(*source, Enums::GameVersion::FRLG), "a Latin OT name on a Korean record was reported unstorable in Gen 3", "synthetic Korean Ditto, Latin OT");
            auto out = intoFireRed(*source, "synthetic Korean Ditto, Latin OT");
            if (out)
            {
                context.check(out->otName() == Utils::utf8ToUtf16("Minho"), "a Latin OT name on a Korean record was dropped converting into Gen 3", "synthetic Korean Ditto, Latin OT ot=\"" + Utils::utf16ToUtf8(out->otName()) + "\"");
            }
        }

        // 5. The names survive the trip BACK UP, which is the same table read the other way. A Gen 3
        //    record decoded through the international table loses a Japanese name exactly as
        //    silently as writing one through it does.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto down = intoFireRed(*source, "synthetic Japanese Ditto");
            if (down)
            {
                Conversion::Result upResult = Conversion::Result::Ok;
                auto up = Conversion::convert(*down, Enums::GameVersion::USUM, upResult);
                if (context.check(up != nullptr && upResult == Conversion::Result::Ok, "a Japanese Gen 3 pokemon could not be sent back up", std::string("synthetic Japanese Ditto result=") + resultName(upResult)))
                {
                    context.check(up->nickname() == Utils::utf8ToUtf16("メタモン"), "a Japanese nickname was lost coming back out of Gen 3", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(up->nickname()) + "\"");
                    context.check(up->otName() == Utils::utf8ToUtf16("Ｋｉａｓｔａ"), "a full-width Latin OT name was lost coming back out of Gen 3", "synthetic Japanese Ditto ot=\"" + Utils::utf16ToUtf8(up->otName()) + "\"");
                }
            }
        }

        // 6. A Roman name TYPED onto a Japanese Gen 3 record. The Japanese table has no half-width
        //    Latin, so without the full-width retry the editor would refuse a perfectly ordinary
        //    name -- and the cartridge itself stores exactly the full-width form that comes back.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto out = intoFireRed(*source, "synthetic Japanese Ditto");
            if (out)
            {
                const std::u16string typedName = Utils::utf8ToUtf16("Kiasta");
                context.check(out->canStoreNickname(typedName), "a Roman nickname was refused on a Japanese Gen 3 record", "synthetic Japanese Ditto");
                out->setNickname(typedName);
                context.check(out->nickname() == Utils::utf8ToUtf16("Ｋｉａｓｔａ"), "a Roman nickname on a Japanese Gen 3 record did not store full-width", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(out->nickname()) + "\"");
            }
        }
    }

    int conversionPlan(Context &context)
    {
        (void)context;
        // One chunk per sample, plus a final one for the synthetic Poke Transporter pass. That last
        // chunk needs no save at all, so the Gen 1/2 route is covered on every console rather than
        // only on the few that have a Game Boy file sitting on the card.
        return (int)samples().size() + 1;
    }

    void conversionRun(Context &context, int chunk)
    {
        if (chunk == (int)samples().size())
        {
            if (samples().empty())
                context.skip("the cross-generation matrix was not swept: no Pokemon were collected from any save");
            // A format with no sample contributed nothing to the matrix, and that is a gap to say,
            // not a pass to count.
            for (Enums::GameVersion sourceGroup : SOURCE_GROUPS)
            {
                bool sampled = false;
                for (const Sample &sample : samples())
                    sampled = sampled || (sample.group == sourceGroup && sample.pokemon);
                if (!sampled)
                    context.skip("no Pokemon from this format, so conversions FROM it were not tested", "format=\"" + Enums::getGameVersionName(sourceGroup) + "\"");
            }
            transporterSynthetic(context);
            gen3LanguageSynthetic(context);
            return;
        }
        if (chunk > (int)samples().size())
            return;

        const Sample &s = samples()[chunk];
        if (!context.check(s.pokemon != nullptr, "a collected sample holds no Pokemon", s.origin))
            return;
        const Pokemon::Pokemon &source = *s.pokemon;
        const Enums::GameVersion srcGroup = s.group;

        for (int DESTINATION_INDEX = 0; DESTINATION_INDEX < DESTINATION_COUNT; ++DESTINATION_INDEX)
        {
            const Enums::GameVersion destination = DESTINATIONS[DESTINATION_INDEX];
            const std::string whereLabel = s.origin + " -> " + Enums::getGameVersionName(destination) + "  " + monLabel(source);

            Conversion::Result gate = Conversion::Result::Ok;
            const bool allowed = Conversion::canConvert(source, destination, gate);

            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto out = Conversion::convert(source, destination, conversionResult);

            // The gate and the conversion are two entry points onto the same decision. A UI that
            // gates on one and acts on the other is a UI that offers drops it will then refuse.
            context.check(gate == conversionResult, "canConvert() and convert() disagree on the outcome", whereLabel + " canConvert=" + resultName(gate) + " convert=" + resultName(conversionResult));
            context.check(allowed == (gate == Conversion::Result::Ok || gate == Conversion::Result::SameGroup), "canConvert()'s answer disagrees with the reason it gave", whereLabel + " returned=" + (allowed ? "true" : "false") + " result=" + resultName(gate));

            const bool sourceIsVirtualConsole = srcGroup == Enums::GameVersion::RBY || srcGroup == Enums::GameVersion::GSC;
            const bool destIsVirtualConsole = destination == Enums::GameVersion::RBY || destination == Enums::GameVersion::GSC;

            // Nothing comes BACK to a Game Boy game. Refused, not attempted, not best-effort.
            if (destIsVirtualConsole && srcGroup != destination)
            {
                context.check(conversionResult == Conversion::Result::Unsupported, "a conversion INTO Gen 1/2 was not refused", whereLabel + " result=" + resultName(conversionResult));
                context.check(out == nullptr, "a refused Gen 1/2 conversion still produced an entity", whereLabel);
                continue;
            }

            if (destination == srcGroup)
            {
                context.check(conversionResult == Conversion::Result::SameGroup, "converting to the same group did not report SameGroup", whereLabel + " result=" + resultName(conversionResult));
                context.check(out == nullptr, "a same-group conversion produced an entity to throw away", whereLabel);
                continue;
            }

            // Whether the pair is connected at all is known independently of the gate. A connected
            // pair must never come back Unsupported, and an unconnected one must.
            const bool connected = (inConversionFamily(srcGroup) || sourceIsVirtualConsole) && inConversionFamily(destination);
            if (!connected)
            {
                context.check(conversionResult == Conversion::Result::Unsupported, "a conversion between formats PKSE does not connect was not refused", whereLabel + " result=" + resultName(conversionResult));
                context.check(out == nullptr, "a refused conversion still produced an entity", whereLabel);
                continue;
            }
            if (conversionResult != Conversion::Result::Ok)
            {
                context.check(conversionResult != Conversion::Result::Unsupported, "a conversion between connected formats was refused as Unsupported", whereLabel);
                // NotInDex / Blocked are real answers, not failures -- a species the destination
                // does not have cannot go there. They are noted so the matrix's coverage is visible.
                context.check(out == nullptr, "a failed conversion still produced an entity", whereLabel + " result=" + resultName(conversionResult));
                context.note("conversion declined", whereLabel + " result=" + resultName(conversionResult));
                continue;
            }

            if (!context.check(out != nullptr, "conversion reported Ok but produced nothing", whereLabel))
                continue;
            checkConverted(context, source, *out, destination, whereLabel, sourceIsVirtualConsole);
            if (sourceIsVirtualConsole)
            {
                // The Transporter rules are stated in terms of the PK7 the transfer lands in, so
                // they are checked at that endpoint. A Gen 1 Pokemon that carried on to Gen 8/9
                // keeps the origin and met data this asserts, but its EXP, level and moves have then
                // been through the destination's own rules as well.
                if (destination == Enums::GameVersion::SM || destination == Enums::GameVersion::USUM)
                    transporterRules(context, source, *out, whereLabel);
            }

            // Not byte-identical, and never promised to be: the trip drops what the intermediate
            // format cannot hold (Legends: Arceus GVs, Let's Go AVs). What must survive is identity.
            Conversion::Result back = Conversion::Result::Ok;
            auto returned = Conversion::convert(*out, srcGroup, back);
            // Nothing goes back to a Game Boy game, so a Gen 1/2 source has no return trip. Anything
            // else came FROM the source format, so the way back must exist; Blocked is the one honest
            // refusal (Brilliant Diamond / Shining Pearl refuse Spinda and Nincada outright).
            if (!sourceIsVirtualConsole && back != Conversion::Result::Blocked)
                context.check(back == Conversion::Result::Ok && returned != nullptr, "the return trip of a successful conversion was refused", whereLabel + " result=" + resultName(back));
            if (back == Conversion::Result::Ok && returned)
            {
                const std::string roundTripLabel = whereLabel + " (and back)";
                context.check(returned->speciesID() == source.speciesID(), "a round-trip conversion changed the species", roundTripLabel);
                context.check(returned->id32() == source.id32(), "a round-trip conversion changed the trainer ID", roundTripLabel);
                // The way back is never a rebuild, so it carries whatever level the way out arrived at
                // -- the source's own, or the one a Gen 3 rebuild raised it to.
                const uint8_t expectedLevel = rebuildMayRaiseLevel(srcGroup, destination) ? out->level() : source.level();
                context.check(returned->level() == expectedLevel, "a round-trip conversion changed the level", roundTripLabel + " expected=" + std::to_string(expectedLevel) + " got=" + std::to_string(returned->level()));
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                    context.check(ivOf(*returned, statIndex) == ivOf(source, statIndex), "a round-trip conversion changed an IV", roundTripLabel + " stat=" + std::to_string(statIndex));
                context.check(returned->isStructurallyValid(), "a round-trip conversion produced an invalid entity", roundTripLabel);
            }
        }
    }
}
