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
#include <cstring>
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
#include "Pokemon/PersonalRecord.h"
#include "Enums/LanguageID.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Pokemon/Pokemon8LA.h"
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

        const char *resultName(Conversion::Result conversionResult)
        {
            switch (conversionResult)
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

        int ivOf(const Pokemon::Pokemon &pokemon, int statIndex)
        {
            switch (statIndex)
            {
            case 0:
                return pokemon.ivHP();
            case 1:
                return pokemon.ivATK();
            case 2:
                return pokemon.ivDEF();
            case 3:
                return pokemon.ivSPE();
            case 4:
                return pokemon.ivSPA();
            default:
                return pokemon.ivSPD();
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
        void checkConverted(Context &context, const Pokemon::Pokemon &source, const Pokemon::Pokemon &converted, Enums::GameVersion destination, const std::string &whereLabel, bool viaTransporter)
        {
            context.check(converted.getGameGroup() == destination, "converted Pokemon reports the wrong format group", whereLabel + " reported=" + Enums::getGameVersionName(converted.getGameGroup()));
            context.check(converted.isStructurallyValid(), "converted Pokemon is not structurally valid", whereLabel);

            context.check(converted.speciesID() == source.speciesID(), "conversion changed the species", whereLabel + " before=" + std::to_string(source.speciesID()) + " after=" + std::to_string(converted.speciesID()));
            context.check(converted.formID() == source.formID(), "conversion changed the form", whereLabel + " before=" + std::to_string(source.formID()) + " after=" + std::to_string(converted.formID()));
            if (rebuildMayRaiseLevel(source.getGameGroup(), destination))
            {
                context.check(converted.level() >= source.level(), "a Gen 3 rebuild lowered the level", whereLabel + " before=" + std::to_string(source.level()) + " after=" + std::to_string(converted.level()));
            }
            else
            {
                context.check(converted.level() == source.level(), "conversion changed the level", whereLabel + " before=" + std::to_string(source.level()) + " after=" + std::to_string(converted.level()));
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
                context.check(converted.metLocation() != Legality::ENCOUNTER_LOCATION_EVENT3, "a Gen 3 rebuild claimed an event or Colosseum gift the Pokemon never received", whereLabel + " met=" + std::to_string(converted.metLocation()) + " level=" + std::to_string(converted.level()));
            }

            // Transporter rerolls the IVs on purpose -- a Gen 1/2 DV is 4 bits wide and there is no
            // honest widening of it -- so this asks the opposite question there (see below).
            if (!viaTransporter)
            {
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    context.check(ivOf(converted, statIndex) == ivOf(source, statIndex), "conversion changed an IV", whereLabel + " stat=" + std::to_string(statIndex) + " before=" + std::to_string(ivOf(source, statIndex)) + " after=" + std::to_string(ivOf(converted, statIndex)));
                }
            }

            context.check(converted.id32() == source.id32(), "conversion changed the trainer ID", whereLabel + " " + hexField("before", source.id32(), 8) + " " + hexField("after", converted.id32(), 8));
            // Gen 3 down-converts transcode the OT into its own charset, so a name with characters
            // Gen 3 lacks can legitimately come out different. Anywhere else, a changed OT means the
            // pokemon has lost its origin.
            // A Pokemon received in an in-game Gen 1/2 trade stores a MARKER rather than a name (PKSE
            // renders it "*"), and Bank substitutes the localised "Trainer" for it -- so that one
            // legitimately comes out different too.
            const bool substitutedTradeOT = viaTransporter && source.otName() == std::u16string(u"*");
            const bool nameFitsDestination = !Enums::isGen3Group(destination) || Conversion::canStoreOriginalTrainerName(source, destination);
            if (nameFitsDestination && !substitutedTradeOT)
            {
                context.check(converted.otName() == source.otName(), "conversion changed the OT name", whereLabel + " before=\"" + Utils::utf16ToUtf8(source.otName()) + "\" after=\"" + Utils::utf16ToUtf8(converted.otName()) + "\"");
            }

            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = converted.move(moveIndex);
                if (moveId == 0)
                {
                    continue;
                }
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
                if (!viaTransporter && !sameGenerationTrade)
                {
                    context.check(Pokemon::isLearnable(converted.speciesID(), converted.formID(), destination, moveId), "conversion left a move the species cannot learn in the destination", whereLabel + " move=\"" + Names::getMoveName(moveId) + "\" (" + std::to_string(moveId) + ")");
                }
            }
            if (converted.hasHeldItem() && converted.heldItem() != 0)
            {
                // The held-item tables have no Sun/Moon or Ultra Sun/Ultra Moon rows ("no data", not
                // "cannot hold"), so those two are judged by PKHeX's Legal.MaxItemID_7 and _7_USUM.
                const bool heldItemExists = destination == Enums::GameVersion::SM ? converted.heldItem() <= 920 : destination == Enums::GameVersion::USUM ? converted.heldItem() <= 959 : Names::isHeldItemPresent(converted.heldItem(), destination);
                context.check(heldItemExists, "conversion left a held item the destination cannot hold", whereLabel + " item=" + std::to_string(converted.heldItem()));
            }
            else if (!converted.hasHeldItem())
            {
                context.check(converted.heldItem() == 0, "a format with no held-item mechanic came out of conversion holding one", whereLabel + " item=" + std::to_string(converted.heldItem()));
            }

            const size_t ribbonOffset = Conversion::affixedRibbonOffset(destination);
            if (ribbonOffset != 0 && ribbonOffset < converted.getData().size())
            {
                const uint8_t affixedRibbon = (uint8_t)converted.getData()[ribbonOffset];
                context.check(affixedRibbon != 0, "converted Pokemon's AffixedRibbon is 0, which displays the Kalos Champion ribbon it does not own (\"none\" is 0xFF)", whereLabel + " " + hexField("offset", ribbonOffset, 3));
            }

            // LEGENDS: ARCEUS STORES ITS SIZES. Absolute height and weight are floats in the record,
            // not derived on read, and nothing before Legends: Arceus has them, so a conversion that
            // does not compute them lands a Pokemon with no size: the Pokedex shows 0, and the size
            // research tasks can never count it. They must be exactly what its scalars give.
            if (destination == Enums::GameVersion::PLA)
            {
                const auto &convertedPokemon = static_cast<const Pokemon::Pokemon8LA &>(converted);
                auto resizedPokemon = convertedPokemon.clone();
                auto &resized = static_cast<Pokemon::Pokemon8LA &>(*resizedPokemon);
                resized.resetSizeAbsolutes();
                const float convertedHeight = convertedPokemon.heightAbsolute();
                const float convertedWeight = convertedPokemon.weightAbsolute();
                const float resizedHeight = resized.heightAbsolute();
                const float resizedWeight = resized.weightAbsolute();
                const std::string sizeDetail = whereLabel + " height=" + std::to_string(convertedHeight) + " weight=" + std::to_string(convertedWeight);
                context.check(convertedHeight > 0 && convertedWeight > 0, "a Pokemon converted into Legends: Arceus has no height or weight", sizeDetail);
                context.check(std::memcmp(&convertedHeight, &resizedHeight, sizeof(float)) == 0 && std::memcmp(&convertedWeight, &resizedWeight, sizeof(float)) == 0, "a Pokemon converted into Legends: Arceus has a height or weight its size scalars do not give", sizeDetail + " wantHeight=" + std::to_string(resizedHeight) + " wantWeight=" + std::to_string(resizedWeight));
            }
        }

        /// What Poke Transporter PROMISES, as opposed to what it preserves. Every one of these is a
        /// rule the transfer invents rather than carries, so each is checked against the rule rather
        /// than against the source Pokemon (PKHeX PK1.ConvertToPK7 / PK2.ConvertToPK7).
        void transporterRules(Context &context, const Pokemon::Pokemon &source, const Pokemon::Pokemon &converted, const std::string &whereLabel)
        {
            const bool fromGen2 = source.getGameGroup() == Enums::GameVersion::GSC;
            const uint16_t speciesId = converted.speciesID();
            const bool isEventPair = speciesId == 151 || (fromGen2 && speciesId == 251);

            // Met data is entirely invented: a Poke Ball, the arrival level, and the one hardcoded
            // location Bank stamps -- "Kanto region" out of Gen 1, "Johto region" out of Gen 2.
            // Bank's link-trade memory draws its feeling below 10 from the set the MemoryVerifier
            // allows (PKHeX SetTradeMemoryHT6(bank: true), GetRandomFeeling6(4, 10)).
            if (converted.htMemory() != 0)
            {
                const uint8_t memoryFeeling = converted.htMemoryFeeling();
                const bool feelingPermitted = memoryFeeling < 10 && memoryFeeling != 1;
                context.check(feelingPermitted, "a transferred Pokemon's handler memory carries a feeling Bank does not give", whereLabel + " feeling=" + std::to_string(memoryFeeling));
            }
            context.check(converted.ball() == 4, "a transferred Pokemon did not arrive in a Poke Ball", whereLabel + " ball=" + std::to_string(converted.ball()));
            context.check(converted.metLocation() == (fromGen2 ? 30017 : 30013), "a transferred Pokemon has the wrong met location", whereLabel + " location=" + std::to_string(converted.metLocation()));
            context.check(converted.metLevel() == converted.level(), "a transferred Pokemon's met level is not the level it arrived at", whereLabel + " met=" + std::to_string(converted.metLevel()) + " level=" + std::to_string(converted.level()));

            // The origin has to name a Gen 1/2 title. PKHeX numbers them 35-41 (Red..Crystal), and a
            // Gen 1 record must never come out claiming a Gen 2 game or the reverse.
            const uint8_t originGame = converted.originGame();
            const bool originInRange = fromGen2 ? (originGame >= 39 && originGame <= 41) : (originGame >= 35 && originGame <= 38);
            context.check(originInRange, "a transferred Pokemon's origin game is not one of its own generation's", whereLabel + " origin=" + std::to_string(originGame));

            // EXP is floored to the arrival level, so the two must agree exactly. The curve is Gen
            // 7's own -- that is the table Transporter computed against.
            const uint8_t growthRate = Pokemon::getPersonalInfo7USUM(speciesId, converted.formID()).growthRate;
            context.check(converted.exp() == Pokemon::getExpForLevel(converted.level(), growthRate), "a transferred Pokemon kept partial EXP; Transporter floors it to the level", whereLabel + " exp=" + std::to_string(converted.exp()) + " level=" + std::to_string(converted.level()));

            // Nature comes from the EXP the Pokemon had BEFORE that flooring. Reading it after would
            // give every transferred Pokemon one of a handful of natures.
            context.check(converted.nature() == static_cast<uint8_t>(source.exp() % 25), "a transferred Pokemon's nature is not its source EXP mod 25", whereLabel + " nature=" + std::to_string(converted.nature()) + " expected=" + std::to_string(source.exp() % 25));

            // Stat Experience is dropped, not converted.
            context.check(converted.evHP() == 0 && converted.evATK() == 0 && converted.evDEF() == 0 && converted.evSPE() == 0 && converted.evSPA() == 0 && converted.evSPD() == 0, "a transferred Pokemon arrived with EVs; Gen 1/2 Stat Experience is dropped", whereLabel);

            // Three guaranteed 31s, five for the event-only pair. Not "at least" -- the count is the
            // rule, and a sixth would mean the shuffle wrote over a random roll.
            int flawlessIVCount = 0;
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                if (ivOf(converted, statIndex) == 31)
                {
                    ++flawlessIVCount;
                }
            }
            context.check(flawlessIVCount >= (isEventPair ? 5 : 3), "a transferred Pokemon has too few flawless IVs", whereLabel + " flawless=" + std::to_string(flawlessIVCount));

            // Almost everything gets its HIDDEN ability. AbilityNumber is a bit, not an index:
            // 1 = slot 1, 4 = hidden.
            context.check(converted.abilityNumber() == 1 || converted.abilityNumber() == 4, "a transferred Pokemon has an ability slot Transporter never assigns", whereLabel + " abilityNumber=" + std::to_string(converted.abilityNumber()));

            // Shininess has no field to carry it across -- it is a DV pattern in Gen 1/2 and a
            // PID/ID xor here -- so the PID is forced to reproduce the FACT.
            context.check(converted.isShiny(converted.id32(), "") == source.isShiny(source.id32(), ""), "a transfer changed whether the Pokemon is shiny", whereLabel + std::string(" before=") + (source.isShiny(source.id32(), "") ? "Y" : "N") + " after=" + (converted.isShiny(converted.id32(), "") ? "Y" : "N"));

            // Language has to be a real one. Gen 1/2 store none and infer it from the record width;
            // 0 is what a format that forgot to answer returns, and no Gen 7+ game accepts it.
            context.check(converted.language() != 0, "a transferred Pokemon arrived with no language", whereLabel + " language=" + std::to_string(converted.language()));

            context.check(converted.isFatefulEncounter() == isEventPair, "the fateful-encounter flag is set on the wrong Pokemon (only Mew and Celebi get it)", whereLabel + std::string(" fateful=") + (converted.isFatefulEncounter() ? "Y" : "N"));

            // Dizzy Punch is the one move Transporter refuses to carry out of Gen 2.
            if (fromGen2)
            {
                for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                {
                    context.check(converted.move(moveIndex) != 146, "Dizzy Punch survived a Gen 2 transfer", whereLabel);
                }
            }
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
        void gen3LandingRules(Context &context, const Pokemon::Pokemon &converted, uint8_t destinationVersion, const std::string &whereLabel)
        {
            const uint16_t metLocation = converted.metLocation();
            const uint16_t speciesId = converted.speciesID();
            const std::string landingLabel = whereLabel + " species=" + std::to_string(speciesId) + " location=" + std::to_string(metLocation);
            if (!context.check(metLocation <= 0xFF, "a Gen 3 met location does not fit the format's byte", landingLabel))
            {
                return;
            }

            const auto destination = static_cast<Enums::GameVersion>(destinationVersion);
            // THE TABLE IS THE RECORD'S OWN ORIGIN, NOT THE DESTINATION'S. The rebuild may stamp
            // the other Gen 3 group -- a Hoothoot in a FireRed save is an EMERALD pokemon, because
            // Kanto has none and Emerald's Safari Zone does -- and asking FireRed's table about a
            // Hoenn location finds nothing and fails for a reason that has nothing to do with the
            // landing being wrong.
            const auto originVersion = static_cast<Enums::GameVersion>(converted.originGame());
            const Legality::EncounterTable *originTable = Legality::getEncounterTable(originVersion);
            // The egg is the DESTINATION's, though: a hatchling is met where the player was walking,
            // which is the game the pokemon is being written into.
            const Legality::EncounterTable *destinationTable = Legality::getEncounterTable(destination);
            const uint8_t fallbackLocation = (destinationVersion == static_cast<uint8_t>(Enums::GameVersion::FR) || destinationVersion == static_cast<uint8_t>(Enums::GameVersion::LG)) ? 88 : 0; // Pallet Town : Littleroot Town
            // A hatched egg is a legal landing too, and for a species the game cannot produce it is
            // the ONLY one -- Gen 3 records no egg location, so a hatchling carries where it hatched.
            bool justified = (metLocation == fallbackLocation);
            if (!justified && destinationTable != nullptr && destinationTable->egg.hatchLocations != nullptr && metLocation < destinationTable->egg.hatchLocationCount)
            {
                justified = (destinationTable->egg.hatchLocations[metLocation] != 0);
            }
            // The whole PRE-EVOLUTION CHAIN counts: a Wartortle is a caught Squirtle that evolved,
            // so a Squirtle location justifies it exactly as the checker treats it.
            uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN + 1] = {speciesId};
            uint8_t chainForms[Pokemon::EVO_MAX_CHAIN + 1] = {};
            const int chainLength = 1 + Pokemon::getPreEvolutionChain(originVersion, speciesId, 0, chainSpecies + 1, chainForms + 1, Pokemon::EVO_MAX_CHAIN);
            for (int linkIndex = 0; linkIndex < chainLength && !justified; ++linkIndex)
            {
                const uint16_t linkSpeciesId = chainSpecies[linkIndex];
                if (originTable == nullptr || originTable->speciesIndex == nullptr || linkSpeciesId == 0 || linkSpeciesId + 1 >= originTable->speciesIndexLength)
                {
                    continue;
                }
                for (uint16_t rowIndex = originTable->speciesIndex[linkSpeciesId]; rowIndex < originTable->speciesIndex[linkSpeciesId + 1] && !justified; ++rowIndex)
                {
                    justified = (originTable->rows[rowIndex].location == metLocation);
                }
            }
            context.check(justified, "a pokemon written into Gen 3 landed somewhere that game cannot produce it", landingLabel + " (neither an encounter for this species nor the first-town fallback)");
        }

        /// Who the matrix converts INTO. A name, gender and language no real save's handler shares,
        /// so a handler block that was carried rather than written cannot pass for this one.
        const Conversion::ReceivingTrainer MATRIX_RECEIVING_TRAINER{Utils::utf8ToUtf16("PKSE"), 1, static_cast<uint8_t>(Enums::LanguageID::French)};

        /// Sun/Moon and Ultra Sun/Ultra Moon share one record format, so a move between them is not a
        /// transfer into another game and the handler is left as the record had it.
        bool isSameRecordFormat(Enums::GameVersion sourceGroup, Enums::GameVersion destination)
        {
            auto isGen7Record = [](Enums::GameVersion group) { return group == Enums::GameVersion::SM || group == Enums::GameVersion::USUM; };
            return isGen7Record(sourceGroup) && isGen7Record(destination);
        }

        /// The receiving player is the active handler of anything that arrives from another game --
        /// what Transporter, Bank and HOME do, and what PKHeX requires (TransferHandlerFlagRequired).
        /// A FireRed Venusaur once reached Violet with no handler at all, because every route but
        /// Gen 1/2 carried the source's handler state instead.
        void receivingHandlerRules(Context &context, const Pokemon::Pokemon &source, const Pokemon::Pokemon &converted, Enums::GameVersion destination, const Conversion::ReceivingTrainer &receivingTrainer, bool viaTransporter, const std::string &whereLabel)
        {
            if (!converted.hasHandler()) return;
            if (isSameRecordFormat(source.getGameGroup(), destination))
            {
                context.check(converted.currentHandler() == source.currentHandler() && converted.htName() == source.htName(), "a move between Sun/Moon and Ultra Sun/Ultra Moon rewrote the handler", whereLabel + " currentHandler " + std::to_string(source.currentHandler()) + "->" + std::to_string(converted.currentHandler()) + " ht=\"" + Utils::utf16ToUtf8(converted.htName()) + "\"");
                return;
            }
            context.check(converted.currentHandler() == 1, "a Pokemon arriving from another game is not in the receiving player's hands", whereLabel + " currentHandler=" + std::to_string(converted.currentHandler()));
            context.check(converted.htName() == receivingTrainer.name && converted.htGender() == receivingTrainer.gender, "a Pokemon arriving from another game does not name the receiving player as its handler", whereLabel + " ht=\"" + Utils::utf16ToUtf8(converted.htName()) + "\" gender=" + std::to_string(converted.htGender()));
            // A GEN 1/2 POKEMON MET ITS HANDLER IN GEN 7, in Poke Transporter, and a Gen 7 handler block
            // has no language and keeps a memory. Going on into Gen 8+ the handler is the same player, so
            // the block rides on unchanged: HOME writes no handler language for a Gen 7 record
            // (GameDataPK8.CopyFrom(PK7)) and keeps a same-named handler's block (PKH.UpdateHandler),
            // which PKHeX accepts as a Bank memory.
            if (converted.hasHandlerLanguage())
            {
                const uint8_t expectedLanguage = viaTransporter ? 0 : Enums::safeLanguageForGroup(destination, receivingTrainer.language);
                context.check(converted.htLanguage() == expectedLanguage, "the handler's language is not the receiving save's", whereLabel + " htLanguage=" + std::to_string(converted.htLanguage()) + " expected=" + std::to_string(expectedLanguage));
            }
            // Gen 8 on clears the old handler's memories and leaves the new one's to the game, so a
            // Gen 8+ handler has none. Poke Transporter writes Bank's link-trade memory. A Gen 7 trade
            // writes no memory of its own and clears none (PKHeX PK7.TradeHT), so it is not asserted.
            if (converted.hasHandlerMemories() && (viaTransporter || converted.hasHandlerLanguage()))
            {
                const bool memoryExpected = viaTransporter;
                context.check((converted.htMemory() != 0) == memoryExpected, memoryExpected ? "a Poke Transporter Pokemon arrived with a new handler and no handler memory" : "a Gen 8+ Pokemon kept the previous handler's memory under a new handler", whereLabel + " htMemory=" + std::to_string(converted.htMemory()));
            }
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
            const Conversion::ReceivingTrainer receivingTrainer{Utils::utf8ToUtf16("PKSE"), 0};

            auto transferred = [&](Pokemon::Pokemon &source, const std::string &whereLabel)-> std::unique_ptr<Pokemon::Pokemon>
            {
                Conversion::Result conversionResult = Conversion::Result::Ok;
                auto converted = Conversion::convert(source, Enums::GameVersion::USUM, conversionResult, 0, &receivingTrainer);
                if (!context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic Poke Transporter transfer was refused", whereLabel + " result=" + resultName(conversionResult)))
                {
                    return nullptr;
                }
                checkConverted(context, source, *converted, Enums::GameVersion::USUM, whereLabel, true);
                transporterRules(context, source, *converted, whereLabel);
                return converted;
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
                for (uint16_t speciesId : {25u, 1u, 151u, 129u})
                {
                    Pokemon::Pokemon1RBY pokemon(false);
                    pokemon.setSpecies(speciesId);
                    pokemon.setExp(21012);
                    pokemon.setTID16(12345);
                    pokemon.setDV16(0xABCD);
                    pokemon.recalculateStats();

                    Conversion::Result conversionResult = Conversion::Result::Ok;
                    auto landed = Conversion::convert(pokemon, Enums::GameVersion::FRLG, conversionResult, destinationVersion, &receivingTrainer);
                    const std::string whereLabel = "synthetic Gen 1 -> Gen 3 version=" + std::to_string(destinationVersion);
                    if (!context.check(landed != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic Gen 1 -> Gen 3 transfer was refused", whereLabel + " species=" + std::to_string(speciesId) + " result=" + resultName(conversionResult)))
                    {
                        continue;
                    }
                    gen3LandingRules(context, *landed, destinationVersion, whereLabel);
                    // The exact symptom, pinned by name so a regression is unmistakable.
                    context.check(landed->metLocation() != 61, "a transferred Gen 1 pokemon landed on \"Abandoned Ship\" -- the Gen 7 transfer marker 30013 truncated into Gen 3's byte", whereLabel + " species=" + std::to_string(speciesId));
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
                const uint8_t fireRedVersion = static_cast<uint8_t>(Enums::GameVersion::FR);
                auto toFireRed = [&](Pokemon::Pokemon &source, const std::string &whereLabel)-> std::unique_ptr<Pokemon::Pokemon>
                {
                    Conversion::Result conversionResult = Conversion::Result::Ok;
                    auto converted = Conversion::convert(source, Enums::GameVersion::FRLG, conversionResult, fireRedVersion, &receivingTrainer);
                    if (!context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a Gen 1/2 -> FireRed rebuild was refused", whereLabel + " result=" + resultName(conversionResult)))
                    {
                        return nullptr;
                    }
                    gen3LandingRules(context, *converted, fireRedVersion, whereLabel);
                    return converted;
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
                    if (auto converted = toFireRed(pokemon, "synthetic Unown -> FireRed"))
                    {
                        context.check(converted->form() == letterBefore, "a transferred Unown came out as a different letter", "before=" + std::to_string(letterBefore) + " after=" + std::to_string(converted->form()));
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
                    if (auto converted = toFireRed(pokemon, "synthetic Wartortle -> FireRed"))
                    {
                        const Legality::Report legalityReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        std::string problemSummary;
                        for (const auto &issue : legalityReport.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                            {
                                continue;
                            }
                            problemSummary += (problemSummary.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(legalityReport.problemCount() == 0, "an evolved pokemon rebuilt into Gen 3 is not legal", "Wartortle loc=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()) + " level=" + std::to_string(converted->level()) + " -- " + problemSummary);
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
                    if (auto converted = toFireRed(pokemon, "synthetic Sandshrew -> FireRed"))
                    {
                        context.check(converted->originGame() == static_cast<uint8_t>(Enums::GameVersion::LG), "a LeafGreen-exclusive pokemon was not stamped as traded from LeafGreen", "origin=" + std::to_string(converted->originGame()));
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
                    if (auto converted = toFireRed(pokemon, "synthetic Elekid -> FireRed"))
                    {
                        const Legality::EncounterTable *destinationTable = Legality::getEncounterTable(Enums::GameVersion::FR);
                        const bool hatched = destinationTable != nullptr && converted->metLocation() < destinationTable->egg.hatchLocationCount && destinationTable->egg.hatchLocations[converted->metLocation()] != 0 && converted->metLevel() == destinationTable->egg.hatchLevel;
                        context.check(hatched, "a species no Gen 3 game can produce was not rebuilt as a hatched egg", "Elekid location=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()) + " (want the day care's own map, met level " + std::to_string(destinationTable != nullptr ? destinationTable->egg.hatchLevel : 0) + ")");
                        // A HATCHLING IS NOT BELOW THE LEVEL IT HATCHES AT. Gen 3 eggs hatch at 5,
                        // and the met level a hatched egg carries is 0 -- two different numbers that
                        // were one for a long time, which is what made every bred Gen 3 Pokemon read
                        // as having no encounter at all.
                        context.check(destinationTable == nullptr || converted->level() >= destinationTable->egg.hatchCurrentLevel, "a rebuilt hatchling came out below the level an egg hatches at", "Elekid level=" + std::to_string(converted->level()));
                        const Legality::Report legalityReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        context.check(legalityReport.problemCount() == 0, "a pokemon rebuilt as a hatched Gen 3 egg is still flagged", "Elekid loc=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()));
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
                    if (auto converted = toFireRed(pokemon, "synthetic level 15 Machop -> FireRed"))
                    {
                        context.check(converted->metLocation() == 138, "a pokemon below every encounter's level landed at the lowest location id instead of the closest encounter", "Machop level=" + std::to_string(converted->level()) + " location=" + std::to_string(converted->metLocation()) + " (want 138 Rock Tunnel 16-17, not 132 Victory Road 32-34)" + " metLevel=" + std::to_string(converted->metLevel()));
                        context.check(converted->metLevel() == 16 && converted->level() == 16, "a rebuild left a pokemon below the encounter it landed on", "Machop metLevel=" + std::to_string(converted->metLevel()) + " level=" + std::to_string(converted->level()) + " (want 16/16 -- Rock Tunnel starts at 16)");
                        const Legality::Report machopReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        context.check(machopReport.problemCount() == 0, "a rebuilt pokemon raised into its encounter's band is still flagged", "Machop level=" + std::to_string(converted->level()));
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
                    if (auto converted = toFireRed(pokemon, "synthetic Hoothoot -> FireRed"))
                    {
                        context.check(converted->originGame() == static_cast<uint8_t>(Enums::GameVersion::EM) && converted->metLocation() == 57 && converted->metLevel() == 35 && converted->level() == 35, "a species only the other Gen 3 group produces was not stamped with it", "Hoothoot origin=" + std::to_string(converted->originGame()) + " location=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()) + " level=" + std::to_string(converted->level()) + " (want Emerald 3, Safari Zone 57, 35/35)");
                        const Legality::Report legalityReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        context.check(legalityReport.problemCount() == 0, "a Hoothoot rebuilt onto its Emerald encounter is still flagged", "Hoothoot level=" + std::to_string(converted->level()));
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
                    if (auto converted = toFireRed(pokemon, "synthetic level 12 Noctowl -> FireRed"))
                    {
                        const Legality::EncounterTable *destinationTable = Legality::getEncounterTable(Enums::GameVersion::FR);
                        const bool claimedAnEgg = destinationTable != nullptr && converted->metLevel() == destinationTable->egg.hatchLevel && converted->metLocation() < destinationTable->egg.hatchLocationCount && destinationTable->egg.hatchLocations[converted->metLocation()] != 0;
                        context.check(!claimedAnEgg || converted->level() >= 20, "a pokemon below its own evolution level was rebuilt as a hatched egg", "Noctowl level=" + std::to_string(converted->level()) + " location=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()));
                        const Legality::Report legalityReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        std::string problemSummary;
                        for (const auto &issue : legalityReport.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                            {
                                continue;
                            }
                            problemSummary += (problemSummary.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(legalityReport.problemCount() == 0, "a Noctowl rebuilt into Gen 3 is not legal", "Noctowl origin=" + std::to_string(converted->originGame()) + " loc=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()) + " level=" + std::to_string(converted->level()) + " -- " + problemSummary);
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
                    if (auto converted = toFireRed(pokemon, "synthetic level 60 Ho-Oh -> FireRed"))
                    {
                        context.check(converted->metLocation() != Legality::ENCOUNTER_LOCATION_EVENT3 && converted->level() == 60, "a rebuild landed on a Pokemon Colosseum gift the Pokemon never received", "Ho-Oh location=" + std::to_string(converted->metLocation()) + " level=" + std::to_string(converted->level()) + " (want neither location 255 nor a raise to Mt. Battle's 70)");
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
                    const std::string whereLabel = "synthetic level 63 Charizard met at 1 -> FireRed";
                    Conversion::Result conversionResult = Conversion::Result::Ok;
                    auto converted = Conversion::convert(pokemon, Enums::GameVersion::FRLG, conversionResult, fireRedVersion, &receivingTrainer);
                    if (context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a Gen 7 -> FireRed rebuild was refused", whereLabel + " result=" + resultName(conversionResult)))
                    {
                        gen3LandingRules(context, *converted, fireRedVersion, whereLabel);
                        context.check(converted->metLocation() == 88 && converted->metLevel() == 5 && converted->level() == 63, "a rebuild left a met level below the floor of the encounter it landed on", "Charizard location=" + std::to_string(converted->metLocation()) + " metLevel=" + std::to_string(converted->metLevel()) + " level=" + std::to_string(converted->level()) + " (want Pallet Town 88, met at 5, still level 63)");
                        const Legality::Report legalityReport = Legality::analyze(*converted, Enums::GameVersion::FRLG);
                        std::string problemSummary;
                        for (const auto &issue : legalityReport.issues)
                        {
                            if (issue.severity == Legality::Severity::Info)
                            {
                                continue;
                            }
                            problemSummary += (problemSummary.empty() ? "" : "; ") + issue.text;
                        }
                        context.check(legalityReport.problemCount() == 0, "a Pokemon rebuilt onto an encounter it outgrew is still flagged", "Charizard metLevel=" + std::to_string(converted->metLevel()) + " -- " + problemSummary);
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
                    Conversion::Result conversionResult = Conversion::Result::Ok;
                    auto converted = Conversion::convert(pokemon, Enums::GameVersion::FRLG, conversionResult, fireRedVersion, &receivingTrainer);
                    if (context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a Gen 1/2 -> FireRed transfer was refused with legalize off", std::string("result=") + resultName(conversionResult)))
                    {
                        context.check(converted->metLocation() == 0, "with auto-legalize off a transfer still rewrote the met location", "location=" + std::to_string(converted->metLocation()));
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
                std::vector<std::byte> blankRecordBytes(Trainer::Bank::recordSizeFor(Enums::GameVersion::FRLG), std::byte{0});
                auto original = Trainer::Bank::makePokemon(Enums::GameVersion::FRLG, blankRecordBytes);
                if (!context.check(original != nullptr, "no Gen 3 entity class for the round trip", "round trip"))
                {
                    return;
                }
                original->setSpecies(1);
                original->setOriginGame(static_cast<uint8_t>(Enums::GameVersion::FR));
                original->setMetLocation(88); // Pallet Town -- a real Gen 3 id
                original->setMetLevel(5);
                original->setExp(1000);
                original->recalculateStats();

                Conversion::Result upResult = Conversion::Result::Ok;
                auto convertedUp = Conversion::convert(*original, Enums::GameVersion::SWSH, upResult, 0, &receivingTrainer);
                if (context.check(convertedUp != nullptr && upResult == Conversion::Result::Ok, "a Gen 3 pokemon could not be sent to Gen 8", "round trip"))
                {
                    Conversion::Result backResult = Conversion::Result::Ok;
                    auto convertedBack = Conversion::convert(*convertedUp, Enums::GameVersion::FRLG, backResult, static_cast<uint8_t>(Enums::GameVersion::FR), &receivingTrainer);
                    if (context.check(convertedBack != nullptr && backResult == Conversion::Result::Ok, "a Gen 3 pokemon could not come home again", "round trip"))
                    {
                        context.check(convertedBack->metLocation() == 88, "a Gen 3 pokemon returning to its origin game lost its met location", "round trip location=" + std::to_string(convertedBack->metLocation()));
                        context.check(convertedBack->originGame() == static_cast<uint8_t>(Enums::GameVersion::FR), "a Gen 3 pokemon returning to its origin game lost its origin", "round trip origin=" + std::to_string(convertedBack->originGame()));
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
                auto converted = transferred(pokemon, "synthetic Red Pikachu");
                if (converted)
                {
                    context.check(converted->nickname() == Utils::utf8ToUtf16("SPARKY"), "a nicknamed Pokemon lost its nickname in transfer", "synthetic Red Pikachu nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
                    context.check(converted->isNicknamed(), "a nicknamed Pokemon transferred without the nicknamed flag", "synthetic Red Pikachu");
                    bool keptSurf = false;
                    for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                    {
                        keptSurf = keptSurf || converted->move(moveIndex) == 57;
                    }
                    // The whole point of the presence-only filter: Surf-Pikachu is the canonical
                    // Virtual Console transfer and no Gen 7 learnset admits it.
                    context.check(keptSurf, "the moveset sanitizer deleted a legal Virtual Console move", "synthetic Red Pikachu lost Surf");
                    // ...and having kept it, PKSE must not then call its own output illegal. A
                    // transferred Pokemon's moves are checked against the pool it learned them in
                    // as well as the one it lives in now; without that, every Virtual Console
                    // transfer PKSE performs would immediately warn about itself.
                    for (const auto &issue : Legality::analyze(*converted, converted->getGameGroup()).issues)
                    {
                        context.check(issue.text.find("Move may not be learnable") == std::string::npos, "the legality checker flags a move PKSE's own transfer legitimately carried", "synthetic Red Pikachu issue=\"" + issue.text + "\"");
                    }
                    context.check(converted->currentHandler() == 1 && !converted->htName().empty(), "a transfer with a known receiving trainer left the Pokemon unhandled", "synthetic Red Pikachu");
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
                auto converted = transferred(pokemon, "synthetic Red Gengar");
                if (converted)
                {
                    context.check(converted->abilityNumber() == 1, "a species Bank withholds the hidden ability from received it anyway", "synthetic Red Gengar abilityNumber=" + std::to_string(converted->abilityNumber()));
                }
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
                auto converted = transferred(pokemon, "synthetic Red Mew");
                if (converted)
                {
                    context.check(converted->nickname() != Utils::utf8ToUtf16("MEWTWO"), "Mew kept a nickname; Transporter takes the fateful branch instead", "synthetic Red Mew nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
                }
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
                const uint8_t unownLetter = pokemon.formID();
                context.check(unownLetter != 0, "a synthetic Unown came out as form 0; the DV-derived letter is not being read", "synthetic Gold Unown");
                auto converted = transferred(pokemon, "synthetic Gold Unown");
                if (converted)
                {
                    context.check(converted->formID() == unownLetter, "Unown's letter did not survive the transfer", "synthetic Gold Unown before=" + std::to_string(unownLetter) + " after=" + std::to_string(converted->formID()));
                }
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
                auto converted = transferred(pokemon, "synthetic Crystal Cyndaquil");
                if (converted)
                {
                    context.check(converted->originGame() == 41, "a record carrying Crystal's caught data did not transfer as Crystal", "synthetic Crystal Cyndaquil origin=" + std::to_string(converted->originGame()));
                    // The surviving moves must be PACKED. A hole mid-moveset is not a state the
                    // games produce -- they stop at the first empty slot.
                    context.check(converted->move(0) == 52 && converted->move(1) == 33 && converted->move(2) == 0, "removing Dizzy Punch left a hole in the moveset instead of packing it", "synthetic Crystal Cyndaquil moves=" + std::to_string(converted->move(0)) + "/" + std::to_string(converted->move(1)) + "/" + std::to_string(converted->move(2)));
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
        const uint8_t japaneseLanguageId = static_cast<uint8_t>(Enums::LanguageID::Japanese);
        const uint8_t koreanLanguageId = static_cast<uint8_t>(Enums::LanguageID::Korean);
        const uint8_t englishLanguageId = static_cast<uint8_t>(Enums::LanguageID::English);

        // A Gen 7 Ditto exactly as PKSM's bank holds one: Japanese, never nicknamed (so its
        // nickname field carries the Japanese species name), and a Roman trainer name stored
        // FULL-WIDTH, which is the only way a Japanese cartridge can store one.
        auto japaneseDitto = [&](bool nicknamed, const std::u16string &originalTrainerName)
        {
            auto pokemon = std::make_unique<Pokemon::Pokemon7USUM>();
            pokemon->setSpecies(132);
            pokemon->setExp(21952); // level 28, medium-fast
            pokemon->setTID16(14246);
            pokemon->setLanguage(japaneseLanguageId);
            pokemon->setNickname(nicknamed ? Utils::utf8ToUtf16("メタモン２") : Utils::utf8ToUtf16("メタモン"));
            pokemon->setIsNicknamed(nicknamed);
            pokemon->setOTName(originalTrainerName);
            pokemon->recalculateStats();
            return pokemon;
        };

        auto intoFireRed = [&](Pokemon::Pokemon &source, const std::string &whereLabel)-> std::unique_ptr<Pokemon::Pokemon>
        {
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto converted = Conversion::convert(source, Enums::GameVersion::FRLG, conversionResult, static_cast<uint8_t>(Enums::GameVersion::FR));
            context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a Gen 7 pokemon could not be converted into Gen 3", whereLabel + " result=" + std::string(resultName(conversionResult)));
            return converted;
        };

        // 1. The reported case, end to end.
        {
            const std::u16string fullWidthName = Utils::utf8ToUtf16("Ｋｉａｓｔａ");
            auto source = japaneseDitto(false, fullWidthName);
            auto converted = intoFireRed(*source, "synthetic Japanese Ditto");
            if (converted)
            {
                context.check(converted->language() == japaneseLanguageId, "a Japanese pokemon lost its language converting into Gen 3", "synthetic Japanese Ditto language=" + std::to_string(converted->language()));
                context.check(converted->nickname() == Utils::utf8ToUtf16("メタモン"), "an un-nicknamed Japanese pokemon did not arrive holding its own language's species name", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
                context.check(converted->otName() == fullWidthName, "a full-width Latin OT name was lost converting into Gen 3", "synthetic Japanese Ditto ot=\"" + Utils::utf16ToUtf8(converted->otName()) + "\"");
                // The two findings the user actually saw. Both key on an empty OT, so a silent
                // regression here shows up as the report they filed rather than as a failure.
                for (const auto &issue : Legality::analyze(*converted, converted->getGameGroup()).issues)
                {
                    context.check(issue.text.find("Empty OT name") == std::string::npos && issue.text.find("no Original Trainer") == std::string::npos, "a converted pokemon with an OT name is reported as having none", "synthetic Japanese Ditto issue=\"" + issue.text + "\"");
                }
            }
        }

        // 2. A REAL Japanese nickname is carried, rather than replaced by the species name.
        {
            auto source = japaneseDitto(true, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto converted = intoFireRed(*source, "synthetic nicknamed Japanese Ditto");
            if (converted)
            {
                context.check(converted->nickname() == Utils::utf8ToUtf16("メタモン２"), "a nicknamed Japanese pokemon lost its nickname converting into Gen 3", "synthetic nicknamed Japanese Ditto nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
            }
        }

        // 3. A language Gen 3 never shipped in. Korean has an id Gen 3 reserved and never used, and
        //    no Gen 3 font can draw Hangul -- so the language clamps to English, the nickname falls
        //    back to the ENGLISH species name (which is what the user asked for), and the OT is left
        //    blank rather than filled with a stand-in. canStoreOriginalTrainerName has to agree with
        //    what the conversion then does, because it is what the confirm dialog warns from.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("한국"));
            source->setLanguage(koreanLanguageId);
            source->setNickname(Utils::utf8ToUtf16("메타몽"));
            context.check(!Conversion::canStoreOriginalTrainerName(*source, Enums::GameVersion::FRLG), "a Hangul OT name was reported storable in Gen 3", "synthetic Korean Ditto");
            auto converted = intoFireRed(*source, "synthetic Korean Ditto");
            if (converted)
            {
                context.check(converted->language() == englishLanguageId, "a Korean pokemon kept a language Gen 3 never shipped in", "synthetic Korean Ditto language=" + std::to_string(converted->language()));
                context.check(converted->nickname() == Utils::utf8ToUtf16("DITTO"), "a pokemon whose language Gen 3 lacks did not fall back to the English species name", "synthetic Korean Ditto nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
                context.check(converted->otName().empty(), "an unwritable OT name was replaced with a stand-in instead of left blank", "synthetic Korean Ditto ot=\"" + Utils::utf16ToUtf8(converted->otName()) + "\"");
            }
        }

        // 4. ...and a Korean player whose trainer name is Latin keeps it. Deciding from the language
        //    alone rather than from whether the name actually encodes would throw this away, and it
        //    is the common case: the alphabet is what Gen 3 lacks, not the language.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Minho"));
            source->setLanguage(koreanLanguageId);
            context.check(Conversion::canStoreOriginalTrainerName(*source, Enums::GameVersion::FRLG), "a Latin OT name on a Korean record was reported unstorable in Gen 3", "synthetic Korean Ditto, Latin OT");
            auto converted = intoFireRed(*source, "synthetic Korean Ditto, Latin OT");
            if (converted)
            {
                context.check(converted->otName() == Utils::utf8ToUtf16("Minho"), "a Latin OT name on a Korean record was dropped converting into Gen 3", "synthetic Korean Ditto, Latin OT ot=\"" + Utils::utf16ToUtf8(converted->otName()) + "\"");
            }
        }

        // 5. The names survive the trip BACK UP, which is the same table read the other way. A Gen 3
        //    record decoded through the international table loses a Japanese name exactly as
        //    silently as writing one through it does.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto convertedDown = intoFireRed(*source, "synthetic Japanese Ditto");
            if (convertedDown)
            {
                Conversion::Result upResult = Conversion::Result::Ok;
                auto convertedUp = Conversion::convert(*convertedDown, Enums::GameVersion::USUM, upResult);
                if (context.check(convertedUp != nullptr && upResult == Conversion::Result::Ok, "a Japanese Gen 3 pokemon could not be sent back up", std::string("synthetic Japanese Ditto result=") + resultName(upResult)))
                {
                    context.check(convertedUp->nickname() == Utils::utf8ToUtf16("メタモン"), "a Japanese nickname was lost coming back out of Gen 3", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(convertedUp->nickname()) + "\"");
                    context.check(convertedUp->otName() == Utils::utf8ToUtf16("Ｋｉａｓｔａ"), "a full-width Latin OT name was lost coming back out of Gen 3", "synthetic Japanese Ditto ot=\"" + Utils::utf16ToUtf8(convertedUp->otName()) + "\"");
                }
            }
        }

        // 6. A Roman name TYPED onto a Japanese Gen 3 record. The Japanese table has no half-width
        //    Latin, so without the full-width retry the editor would refuse a perfectly ordinary
        //    name -- and the cartridge itself stores exactly the full-width form that comes back.
        {
            auto source = japaneseDitto(false, Utils::utf8ToUtf16("Ｋｉａｓｔａ"));
            auto converted = intoFireRed(*source, "synthetic Japanese Ditto");
            if (converted)
            {
                const std::u16string typedName = Utils::utf8ToUtf16("Kiasta");
                context.check(converted->canStoreNickname(typedName), "a Roman nickname was refused on a Japanese Gen 3 record", "synthetic Japanese Ditto");
                converted->setNickname(typedName);
                context.check(converted->nickname() == Utils::utf8ToUtf16("Ｋｉａｓｔａ"), "a Roman nickname on a Japanese Gen 3 record did not store full-width", "synthetic Japanese Ditto nick=\"" + Utils::utf16ToUtf8(converted->nickname()) + "\"");
            }
        }
    }

    // THE RECEIVING PLAYER BECOMES THE HANDLER, in each of the three ways the games write it. The
    // matrix above proves it happens on every pairing; this proves WHAT is written, which needs
    // fields set to known values first. A FireRed Venusaur is the reported case: it reached Violet
    // with "(none)" for a handler. PKSE cannot build a PK3 from nothing, so it starts as a Gen 7
    // record written down into FireRed with no receiver -- the state a banked FireRed Pokemon is in.
    void receivingHandlerSynthetic(Context &context)
    {
        const uint8_t frenchLanguageId = static_cast<uint8_t>(Enums::LanguageID::French);
        const Conversion::ReceivingTrainer receivingTrainer{Utils::utf8ToUtf16("Kiasta"), 1, frenchLanguageId};
        const Conversion::ReceivingTrainer otherReceivingTrainer{Utils::utf8ToUtf16("Rival"), 0, frenchLanguageId};

        auto convertInto = [&](const Pokemon::Pokemon &source, Enums::GameVersion destination, uint8_t destinationVersion, const Conversion::ReceivingTrainer *receiver, const std::string &whereLabel)-> std::unique_ptr<Pokemon::Pokemon>
        {
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto converted = Conversion::convert(source, destination, conversionResult, destinationVersion, receiver);
            if (!context.check(converted != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic handler conversion was refused", whereLabel + " result=" + resultName(conversionResult)))
            {
                return nullptr;
            }
            return converted;
        };

        auto gen7Venusaur = std::make_unique<Pokemon::Pokemon7USUM>();
        gen7Venusaur->setSpecies(3);
        gen7Venusaur->setExp(117360); // level 50, medium-slow
        gen7Venusaur->setTID16(24680);
        gen7Venusaur->setOTName(Utils::utf8ToUtf16("KIASTA"));
        gen7Venusaur->setOTFriendship(255);
        gen7Venusaur->recalculateStats();
        auto fireRedVenusaur = convertInto(*gen7Venusaur, Enums::GameVersion::FRLG, static_cast<uint8_t>(Enums::GameVersion::FR), nullptr, "synthetic Venusaur -> FireRed");
        if (!fireRedVenusaur) return;

        // 1. Every era's handler block, written onto a record that has no handler field at all.
        //    Legends: Arceus has no Venusaur; the matrix covers it with the Pokemon it does have.
        for (const Enums::GameVersion destination : {Enums::GameVersion::USUM, Enums::GameVersion::GG, Enums::GameVersion::SWSH, Enums::GameVersion::BDSP, Enums::GameVersion::SV, Enums::GameVersion::ZA})
        {
            const std::string whereLabel = "synthetic FireRed Venusaur -> " + Enums::getGameVersionName(destination);
            auto converted = convertInto(*fireRedVenusaur, destination, 0, &receivingTrainer, whereLabel);
            if (!converted) continue;
            receivingHandlerRules(context, *fireRedVenusaur, *converted, destination, receivingTrainer, false, whereLabel);
            // Let's Go copies the friendship it had, because its CP is computed from friendship; every
            // other game starts a new handler at the species' base friendship. Let's Go is the one
            // format with a handler and neither a handler language nor a geolocation history.
            const bool copiesFriendship = !converted->hasHandlerLanguage() && !converted->hasGeolocation();
            const uint8_t expectedFriendship = copiesFriendship ? converted->otFriendship() : Pokemon::getPersonalRecord(destination, converted->speciesID(), converted->form()).baseFriendship;
            context.check(converted->htFriendship() == expectedFriendship, "a new handler did not start at the friendship the games give one", whereLabel + " htFriendship=" + std::to_string(converted->htFriendship()) + " expected=" + std::to_string(expectedFriendship));
            for (const auto &issue : Legality::analyze(*converted, destination).issues)
            {
                context.check(issue.text.find("handling trainer") == std::string::npos, "the legality checker objects to the handler a transfer wrote", whereLabel + " issue=\"" + issue.text + "\"");
            }
        }

        // 2. A handler who is already the receiver is KEPT, friendship and memories included; a
        //    different receiver replaces the block, as a trade to them would.
        auto swordVenusaur = convertInto(*fireRedVenusaur, Enums::GameVersion::SWSH, 0, &receivingTrainer, "synthetic FireRed Venusaur -> Sword/Shield");
        if (swordVenusaur)
        {
            swordVenusaur->setHTFriendship(200);
            swordVenusaur->setHTMemory(5);
            swordVenusaur->refreshChecksum();
            auto returning = convertInto(*swordVenusaur, Enums::GameVersion::BDSP, 0, &receivingTrainer, "synthetic Sword Venusaur -> BDSP, same receiver");
            if (returning)
            {
                context.check(returning->currentHandler() == 1 && returning->htName() == receivingTrainer.name, "a Pokemon returning to its handler is not in their hands", "same receiver currentHandler=" + std::to_string(returning->currentHandler()));
                context.check(returning->htFriendship() == 200 && returning->htMemory() == 5, "a Pokemon returning to its handler lost the friendship or memory it left with", "same receiver htFriendship=" + std::to_string(returning->htFriendship()) + " htMemory=" + std::to_string(returning->htMemory()));
            }
            auto traded = convertInto(*swordVenusaur, Enums::GameVersion::BDSP, 0, &otherReceivingTrainer, "synthetic Sword Venusaur -> BDSP, other receiver");
            if (traded)
            {
                const uint8_t baseFriendship = Pokemon::getPersonalRecord(Enums::GameVersion::BDSP, traded->speciesID(), traded->form()).baseFriendship;
                context.check(traded->htName() == otherReceivingTrainer.name && traded->htGender() == otherReceivingTrainer.gender, "a new receiver did not replace the previous handler", "other receiver ht=\"" + Utils::utf16ToUtf8(traded->htName()) + "\"");
                context.check(traded->htFriendship() == baseFriendship && traded->htMemory() == 0, "a new handler inherited the previous handler's friendship or memory", "other receiver htFriendship=" + std::to_string(traded->htFriendship()) + " htMemory=" + std::to_string(traded->htMemory()));
            }
        }

        // 3. No receiver -- a caller with no save behind it -- keeps the handler the source had, which
        //    for a Gen 3 record is none, rather than inventing one.
        auto unreceived = convertInto(*fireRedVenusaur, Enums::GameVersion::SV, 0, nullptr, "synthetic FireRed Venusaur -> Scarlet/Violet, no receiver");
        if (unreceived)
        {
            context.check(unreceived->currentHandler() == 0 && unreceived->htName().empty(), "a conversion with no receiver invented a handler", "no receiver ht=\"" + Utils::utf16ToUtf8(unreceived->htName()) + "\"");
        }

        // 4. Sun/Moon <-> Ultra Sun/Ultra Moon is one record format: the receiver changes nothing.
        auto sunVenusaur = convertInto(*fireRedVenusaur, Enums::GameVersion::SM, 0, nullptr, "synthetic FireRed Venusaur -> Sun/Moon");
        if (sunVenusaur)
        {
            auto ultraSunVenusaur = convertInto(*sunVenusaur, Enums::GameVersion::USUM, 0, &receivingTrainer, "synthetic Sun Venusaur -> Ultra Sun/Ultra Moon");
            if (ultraSunVenusaur)
            {
                receivingHandlerRules(context, *sunVenusaur, *ultraSunVenusaur, Enums::GameVersion::USUM, receivingTrainer, false, "synthetic Sun Venusaur -> Ultra Sun/Ultra Moon");
                context.check(ultraSunVenusaur->currentHandler() == 0 && ultraSunVenusaur->htName().empty(), "a move between Sun/Moon and Ultra Sun/Ultra Moon stamped a handler", "ht=\"" + Utils::utf16ToUtf8(ultraSunVenusaur->htName()) + "\"");
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

    /// A blank entity of `group`, built the way the bank builds one: an all-zero decrypted record,
    /// encrypted and wrapped.
    std::unique_ptr<Pokemon::Pokemon> blankEntity(Enums::GameVersion group)
    {
        std::vector<std::byte> decryptedRecord(Trainer::Bank::recordSizeFor(group), std::byte{0});
        std::byte *encryptedRecord = Trainer::Bank::encryptRecord(group, decryptedRecord, 0);
        if (!encryptedRecord)
            return nullptr;
        auto pokemon = Trainer::Bank::makePokemon(group, std::span<const std::byte>(encryptedRecord, decryptedRecord.size()));
        delete[] encryptedRecord;
        return pokemon;
    }

    uint8_t recordByte(const Pokemon::Pokemon &pokemon, size_t byteOffset)
    {
        return static_cast<uint8_t>(pokemon.getData()[byteOffset]);
    }

    uint32_t recordWord(const Pokemon::Pokemon &pokemon, size_t byteOffset)
    {
        return static_cast<uint32_t>(recordByte(pokemon, byteOffset)) | (static_cast<uint32_t>(recordByte(pokemon, byteOffset + 1)) << 8) | (static_cast<uint32_t>(recordByte(pokemon, byteOffset + 2)) << 16) | (static_cast<uint32_t>(recordByte(pokemon, byteOffset + 3)) << 24);
    }

    bool sameRecordBytes(const Pokemon::Pokemon &firstPokemon, const Pokemon::Pokemon &secondPokemon, size_t &firstDifferenceOffset)
    {
        const auto firstBytes = firstPokemon.getData();
        const auto secondBytes = secondPokemon.getData();
        firstDifferenceOffset = 0;
        if (firstBytes.size() != secondBytes.size())
            return false;
        for (size_t byteOffset = 0; byteOffset < firstBytes.size(); ++byteOffset)
        {
            if (firstBytes[byteOffset] != secondBytes[byteOffset])
            {
                firstDifferenceOffset = byteOffset;
                return false;
            }
        }
        return true;
    }

    /**
     * Groups that share one entity format, and the Gen 3 -> later route, each checked against
     * PKHeX's own converter output.
     *
     * Siblings: Ruby/Sapphire/Emerald and FireRed/LeafGreen store the same PK3, so a move between
     * them is the same record re-wrapped -- nickname, PID, ribbons, fateful flag and markings
     * included. The narrower sibling refuses what it has no entry for.
     *
     * Gen 3 -> Gen 8 follows Pal Park and the later transfers (PKHeX PK3.ConvertToPK4,
     * PK4.ConvertToPK5, PK5.ConvertToPK6, PK6.ConvertToPK7): met location Poke Transfer (30001),
     * met level the current level, IsNicknamed set only for a real nickname and the species name
     * otherwise in proper case, the PID's bit 31 flipped where Gen 6's shiny window would otherwise
     * make it shiny (the EC keeps the original), the carried ribbons and the contest and battle
     * memory counts, markings blue, and an egg hatched.
     */
    void formatSiblingAndPalParkSynthetic(Context &context)
    {
        using Enums::GameVersion;
        constexpr uint16_t pikachuSpeciesId = 25;
        constexpr uint32_t level20Exp = 8000; // medium-fast, exactly level 20
        constexpr uint32_t shinyWindowPID = 0x0000000C; // xor with TID/SID 0 is 12: shiny in Gen 6, not Gen 3

        auto makeFireRedPikachu = [&](const std::u16string &nickname, bool egg)-> std::unique_ptr<Pokemon::Pokemon>
        {
            auto pokemon = blankEntity(GameVersion::FRLG);
            if (!pokemon)
                return nullptr;
            pokemon->setPID(shinyWindowPID);
            pokemon->setSpecies(pikachuSpeciesId);
            pokemon->setLevel(20); // the Gen 3 class writes EXP through its level, and has no setExp
            pokemon->setLanguage(static_cast<uint8_t>(Enums::LanguageID::English));
            pokemon->setOriginGame(static_cast<uint8_t>(GameVersion::FR));
            pokemon->setBall(4);
            pokemon->setMetLevel(5);
            pokemon->setMetLocation(89); // Viridian Forest
            pokemon->setNickname(nickname);
            pokemon->setOTName(u"RED");
            pokemon->setEgg(egg);
            auto recordBytes = pokemon->getData();
            // Ribbons: Cool count 3, Beauty count 4, Champion (bit 15), Winning (16), Victory (17),
            // Effort (19), and the fateful flag (31).
            const uint32_t ribbonWord = 3u | (4u << 3) | (1u << 15) | (1u << 16) | (1u << 17) | (1u << 19) | (1u << 31);
            for (size_t byteIndex = 0; byteIndex < 4; ++byteIndex)
                recordBytes[0x4C + byteIndex] = static_cast<std::byte>((ribbonWord >> (byteIndex * 8)) & 0xFF);
            recordBytes[0x1B] = std::byte{0x09}; // circle and heart: the two marks Gen 3 does not reorder
            pokemon->recalculateStats();
            pokemon->refreshChecksum();
            return pokemon;
        };

        // 1. FireRed -> Emerald and back: the same record, untouched.
        if (auto fireRedPikachu = makeFireRedPikachu(u"SPARKY", false))
        {
            Conversion::Result conversionResult = Conversion::Result::Ok;
            Conversion::ReceivingTrainer receivingTrainer{u"Other", 0, static_cast<uint8_t>(Enums::LanguageID::English)};
            auto emeraldPikachu = Conversion::convert(*fireRedPikachu, GameVersion::RSE, conversionResult, static_cast<uint8_t>(GameVersion::EM), &receivingTrainer);
            if (context.check(emeraldPikachu != nullptr && conversionResult == Conversion::Result::Ok, "a FireRed Pokemon could not be moved into Emerald", std::string("result=") + resultName(conversionResult)))
            {
                size_t firstDifferenceOffset = 0;
                context.check(emeraldPikachu->getGameGroup() == GameVersion::RSE, "a FireRed Pokemon moved into Emerald is not an Emerald entity");
                context.check(sameRecordBytes(*fireRedPikachu, *emeraldPikachu, firstDifferenceOffset), "a FireRed Pokemon's record changed moving into Emerald (nickname, PID, ribbons, fateful flag and markings are the same bytes)", "offset=" + std::to_string(firstDifferenceOffset));
            }
        }

        // 2. The narrower sibling refuses what it has no entry for.
        {
            auto refusalFor = [&](GameVersion sourceGroup, uint16_t speciesId, uint8_t form, GameVersion destination)
            {
                auto pokemon = blankEntity(sourceGroup);
                if (!pokemon)
                    return Conversion::Result::Unsupported;
                pokemon->setSpecies(speciesId);
                pokemon->setForm(form);
                pokemon->setExp(level20Exp);
                pokemon->refreshChecksum();
                Conversion::Result gateResult = Conversion::Result::Ok;
                Conversion::canConvert(*pokemon, destination, gateResult);
                Conversion::Result convertResult = Conversion::Result::Ok;
                auto converted = Conversion::convert(*pokemon, destination, convertResult);
                context.check(converted == nullptr && convertResult == gateResult, "canConvert and convert disagree about a sibling move", Enums::getGameVersionName(sourceGroup) + " -> " + Enums::getGameVersionName(destination) + " species=" + std::to_string(speciesId));
                return gateResult;
            };
            context.check(refusalFor(GameVersion::PT, 487, 1, GameVersion::DP) == Conversion::Result::NotInDex, "Origin Forme Giratina was let into Diamond/Pearl, which has no such form");
            context.check(refusalFor(GameVersion::USUM, 803, 0, GameVersion::SM) == Conversion::Result::NotInDex, "Poipole was let into Sun/Moon, which predates the Ultra Sun/Ultra Moon additions");
        }

        // 3. FireRed -> Sword/Shield, and back.
        for (const bool nicknamed : {false, true})
        {
            auto fireRedPikachu = makeFireRedPikachu(nicknamed ? u"SPARKY" : u"PIKACHU", false);
            if (!fireRedPikachu)
                continue;
            const std::string whereLabel = std::string("synthetic FireRed Pikachu nicknamed=") + (nicknamed ? "1" : "0");
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto swordPikachu = Conversion::convert(*fireRedPikachu, GameVersion::SWSH, conversionResult);
            if (!context.check(swordPikachu != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic FireRed Pikachu could not be converted into Sword/Shield", whereLabel + " result=" + resultName(conversionResult)))
                continue;
            context.check(swordPikachu->metLocation() == 30001, "a Gen 3 Pokemon did not arrive met at Poke Transfer (PKHeX Locations.Transfer4)", whereLabel + " location=" + std::to_string(swordPikachu->metLocation()));
            context.check(swordPikachu->metLevel() == 20, "a Gen 3 Pokemon's met level is not its level at transfer (PKHeX PK3.ConvertToPK4)", whereLabel + " metLevel=" + std::to_string(swordPikachu->metLevel()) + " level=" + std::to_string(swordPikachu->level()) + " sourceExp=" + std::to_string(fireRedPikachu->exp()) + " sourceLevel=" + std::to_string(fireRedPikachu->level()));
            context.check(swordPikachu->pid() == (shinyWindowPID ^ 0x80000000u) && swordPikachu->encryptionConstant() == shinyWindowPID, "a Gen 3 PID in Gen 6's wider shiny window did not get bit 31 flipped, with the EC keeping the original (PKHeX PK5.GetTransferPID)", whereLabel + " pid=" + std::to_string(swordPikachu->pid()) + " ec=" + std::to_string(swordPikachu->encryptionConstant()));
            context.check(swordPikachu->isNicknamed() == nicknamed, "a Gen 3 Pokemon's IsNicknamed flag does not follow whether its name is the species name", whereLabel);
            context.check(swordPikachu->nickname() == (nicknamed ? u"SPARKY" : u"Pikachu"), "a Gen 3 Pokemon arrived with the wrong name (a species name is rewritten in proper case, a nickname is kept)", whereLabel + " nick=\"" + Utils::utf16ToUtf8(swordPikachu->nickname()) + "\"");
            // PK8 ribbon bits (PKHeX G8PKM): ChampionG3 0x34 bit 1, Effort 0x34 bit 7. Memory counts
            // at 0x3C (contest: 3 + 4) and 0x3D (battle: Winning + Victory), their flags 0x38 bits 5 and 6.
            context.check((recordByte(*swordPikachu, 0x34) & 0x82) == 0x82, "a Gen 3 Pokemon lost its Champion or Effort ribbon", whereLabel + " 0x34=" + std::to_string(recordByte(*swordPikachu, 0x34)));
            context.check(recordByte(*swordPikachu, 0x3C) == 7 && recordByte(*swordPikachu, 0x3D) == 2 && (recordByte(*swordPikachu, 0x38) & 0x60) == 0x60, "a Gen 3 Pokemon's contest and battle ribbons did not become memory ribbons", whereLabel + " contest=" + std::to_string(recordByte(*swordPikachu, 0x3C)) + " battle=" + std::to_string(recordByte(*swordPikachu, 0x3D)));
            context.check((recordByte(*swordPikachu, 0x22) & 0x01) != 0, "a Gen 3 Pokemon lost its fateful-encounter flag", whereLabel);
            context.check((recordWord(*swordPikachu, 0x18) & 0xFFFF) == 0x0041, "a Gen 3 Pokemon's markings did not arrive as blue marks (PKHeX PK6.ConvertToPK7)", whereLabel + " markings=" + std::to_string(recordWord(*swordPikachu, 0x18) & 0xFFFF));

            // A Sun/Moon withdrawal keeps the held item: the held-item tables have no Gen 7 rows,
            // which is "no data", not "cannot hold".
            swordPikachu->setHeldItem(17); // Potion
            swordPikachu->refreshChecksum();
            Conversion::Result moonResult = Conversion::Result::Ok;
            auto moonPikachu = Conversion::convert(*swordPikachu, GameVersion::SM, moonResult);
            if (context.check(moonPikachu != nullptr && moonResult == Conversion::Result::Ok, "a Sword/Shield Pikachu could not be converted into Sun/Moon", whereLabel))
                context.check(moonPikachu->heldItem() == 17, "a Pokemon converted into Sun/Moon lost a held item Sun/Moon has", whereLabel + " item=" + std::to_string(moonPikachu->heldItem()));
            swordPikachu->setHeldItem(0);
            swordPikachu->refreshChecksum();

            // And back into Gen 3: the original PID, ribbons, fateful flag and markings, and no mail.
            Conversion::Result backResult = Conversion::Result::Ok;
            auto backPikachu = Conversion::convert(*swordPikachu, GameVersion::FRLG, backResult, static_cast<uint8_t>(GameVersion::FR));
            if (!context.check(backPikachu != nullptr && backResult == Conversion::Result::Ok, "a converted Pikachu could not be converted back into FireRed", whereLabel + " result=" + resultName(backResult)))
                continue;
            context.check(backPikachu->pid() == shinyWindowPID, "a Gen 3 Pokemon did not get its original PID back on its way home", whereLabel + " pid=" + std::to_string(backPikachu->pid()));
            context.check(recordWord(*backPikachu, 0x4C) == ((1u << 31) | (1u << 19) | (1u << 15)), "a Gen 3 Pokemon's ribbons or fateful flag did not survive a trip to Gen 8 and back", whereLabel + " read=" + std::to_string(recordWord(*backPikachu, 0x4C)) + " pk8=" + std::to_string(recordWord(*swordPikachu, 0x34)) + "/" + std::to_string(recordWord(*swordPikachu, 0x38)));
            context.check(recordByte(*backPikachu, 0x1B) == 0x09, "a Gen 3 Pokemon's markings did not survive a trip to Gen 8 and back", whereLabel + " markings=" + std::to_string(recordByte(*backPikachu, 0x1B)));
            context.check(recordByte(*backPikachu, 0x55) == 0xFF, "a converted Gen 3 party record names a mail slot (0xFF is no mail; PKHeX SAV3.SetPartyValues)", whereLabel + " mail=" + std::to_string(recordByte(*backPikachu, 0x55)));
            context.check(backPikachu->nickname() == fireRedPikachu->nickname(), "a Gen 3 Pokemon's name did not survive a trip to Gen 8 and back", whereLabel + " nick=\"" + Utils::utf16ToUtf8(backPikachu->nickname()) + "\"");
        }

        // 4. A Gen 3 egg hatches on the way out (PKHeX EntityConverter force-hatches).
        if (auto fireRedEgg = makeFireRedPikachu(u"PIKACHU", true))
        {
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto swordEgg = Conversion::convert(*fireRedEgg, GameVersion::SWSH, conversionResult);
            if (context.check(swordEgg != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic Gen 3 egg could not be converted", std::string("result=") + resultName(conversionResult)))
                context.check(!swordEgg->isEgg(), "a Gen 3 egg arrived in a later game still an egg");
        }

        // 5. Sun/Moon ribbons cross Gen 8 and come back (PK7 0x30-0x36 is PK8 0x34-0x3A).
        if (auto moonPikachu = blankEntity(GameVersion::SM))
        {
            moonPikachu->setSpecies(pikachuSpeciesId);
            moonPikachu->setExp(level20Exp);
            moonPikachu->setEncryptionConstant(0x12345678);
            moonPikachu->setOriginGame(static_cast<uint8_t>(GameVersion::MN));
            auto recordBytes = moonPikachu->getData();
            const uint8_t ribbonPattern[7] = {0xA5, 0x5A, 0x00, 0x0F, 0x90, 0x81, 0x03}; // 0x34 bits 5-6 are the memory-ribbon flags, clear with counts 0
            for (size_t byteIndex = 0; byteIndex < 7; ++byteIndex)
                recordBytes[0x30 + byteIndex] = static_cast<std::byte>(ribbonPattern[byteIndex]);
            moonPikachu->recalculateStats();
            moonPikachu->refreshChecksum();
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto swordPikachu = Conversion::convert(*moonPikachu, GameVersion::SWSH, conversionResult);
            if (context.check(swordPikachu != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic Sun/Moon Pikachu could not be converted into Sword/Shield", std::string("result=") + resultName(conversionResult)))
            {
                bool ribbonsCarried = true;
                for (size_t byteIndex = 0; byteIndex < 7; ++byteIndex)
                    ribbonsCarried = ribbonsCarried && recordByte(*swordPikachu, 0x34 + byteIndex) == ribbonPattern[byteIndex];
                context.check(ribbonsCarried, "a Sun/Moon Pokemon's ribbons were dropped converting into Sword/Shield");
                Conversion::Result backResult = Conversion::Result::Ok;
                auto backPikachu = Conversion::convert(*swordPikachu, GameVersion::SM, backResult);
                if (context.check(backPikachu != nullptr && backResult == Conversion::Result::Ok, "a converted Pikachu could not be converted back into Sun/Moon", std::string("result=") + resultName(backResult)))
                {
                    bool ribbonsReturned = true;
                    for (size_t byteIndex = 0; byteIndex < 7; ++byteIndex)
                        ribbonsReturned = ribbonsReturned && recordByte(*backPikachu, 0x30 + byteIndex) == ribbonPattern[byteIndex];
                    context.check(ribbonsReturned, "a Sun/Moon Pokemon's ribbons did not survive a trip to Sword/Shield and back");
                }
            }
        }

        // 6. A Game Boy record's language comes from its names, never the display language
        //    (PKHeX GBPKM.Language and TransferLanguage).
        {
            Pokemon::Pokemon1RBY frenchClefairy(false);
            frenchClefairy.setSpecies(35);
            frenchClefairy.setExp(8000);
            frenchClefairy.setNickname(u"MELOFEE");
            frenchClefairy.setOTName(u"SACHA");
            context.check(frenchClefairy.language() == static_cast<uint8_t>(Enums::LanguageID::French), "a Gen 1 Clefairy named MELOFEE is not read as French", "language=" + std::to_string(frenchClefairy.language()));
            context.check(!frenchClefairy.isNicknamed(), "a French Gen 1 Clefairy carrying its French species name reads as nicknamed");

            Pokemon::Pokemon2GSC germanPikachu(false);
            germanPikachu.setSpecies(pikachuSpeciesId);
            germanPikachu.setExp(8000);
            germanPikachu.setNickname(u"BLITZ");
            germanPikachu.setOTName(u"JÖRG");
            context.check(germanPikachu.language() == static_cast<uint8_t>(Enums::LanguageID::German), "a Gen 2 Pokemon whose OT has a German-only letter is not read as German", "language=" + std::to_string(germanPikachu.language()));

            // A nickname says nothing about language, so Transporter takes the receiving save's.
            Pokemon::Pokemon1RBY nicknamedPikachu(false);
            nicknamedPikachu.setSpecies(pikachuSpeciesId);
            nicknamedPikachu.setExp(8000);
            nicknamedPikachu.setTID16(12345);
            nicknamedPikachu.setNickname(u"ECLAIR");
            nicknamedPikachu.setOTName(u"SACHA");
            nicknamedPikachu.recalculateStats();
            const Conversion::ReceivingTrainer frenchTrainer{u"SACHA", 0, static_cast<uint8_t>(Enums::LanguageID::French)};
            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto transported = Conversion::convert(nicknamedPikachu, GameVersion::USUM, conversionResult, 0, &frenchTrainer);
            if (context.check(transported != nullptr && conversionResult == Conversion::Result::Ok, "a synthetic nicknamed Gen 1 Pikachu could not be transferred", std::string("result=") + resultName(conversionResult)))
                context.check(transported->language() == static_cast<uint8_t>(Enums::LanguageID::French), "a Gen 1 Pokemon whose names say no language did not take the receiving save's language through Poke Transporter", "language=" + std::to_string(transported->language()));
        }
    }

    void conversionRun(Context &context, int chunk)
    {
        if (chunk == (int)samples().size())
        {
            if (samples().empty())
            {
                context.skip("the cross-generation matrix was not swept: no Pokemon were collected from any save");
            }
            // A format with no sample contributed nothing to the matrix, and that is a gap to say,
            // not a pass to count.
            for (Enums::GameVersion sourceGroup : SOURCE_GROUPS)
            {
                bool sampled = false;
                for (const Sample &sample : samples())
                {
                    sampled = sampled || (sample.group == sourceGroup && sample.pokemon);
                }
                if (!sampled)
                {
                    context.skip("no Pokemon from this format, so conversions FROM it were not tested", "format=\"" + Enums::getGameVersionName(sourceGroup) + "\"");
                }
            }
            transporterSynthetic(context);
            gen3LanguageSynthetic(context);
            receivingHandlerSynthetic(context);
            formatSiblingAndPalParkSynthetic(context);
            return;
        }
        if (chunk > (int)samples().size())
        {
            return;
        }

        const Sample &sample = samples()[chunk];
        if (!context.check(sample.pokemon != nullptr, "a collected sample holds no Pokemon", sample.origin))
        {
            return;
        }
        const Pokemon::Pokemon &source = *sample.pokemon;
        const Enums::GameVersion sourceGroup = sample.group;

        for (int destinationIndex = 0; destinationIndex < DESTINATION_COUNT; ++destinationIndex)
        {
            const Enums::GameVersion destination = DESTINATIONS[destinationIndex];
            const std::string whereLabel = sample.origin + " -> " + Enums::getGameVersionName(destination) + "  " + pokemonLabel(source);

            Conversion::Result gateResult = Conversion::Result::Ok;
            const bool conversionAllowed = Conversion::canConvert(source, destination, gateResult);

            Conversion::Result conversionResult = Conversion::Result::Ok;
            auto converted = Conversion::convert(source, destination, conversionResult, 0, &MATRIX_RECEIVING_TRAINER);

            // The gate and the conversion are two entry points onto the same decision. A UI that
            // gates on one and acts on the other is a UI that offers drops it will then refuse.
            context.check(gateResult == conversionResult, "canConvert() and convert() disagree on the outcome", whereLabel + " canConvert=" + resultName(gateResult) + " convert=" + resultName(conversionResult));
            context.check(conversionAllowed == (gateResult == Conversion::Result::Ok || gateResult == Conversion::Result::SameGroup), "canConvert()'s answer disagrees with the reason it gave", whereLabel + " returned=" + (conversionAllowed ? "true" : "false") + " result=" + resultName(gateResult));

            const bool sourceIsVirtualConsole = sourceGroup == Enums::GameVersion::RBY || sourceGroup == Enums::GameVersion::GSC;
            const bool destinationIsVirtualConsole = destination == Enums::GameVersion::RBY || destination == Enums::GameVersion::GSC;

            // Nothing comes BACK to a Game Boy game. Refused, not attempted, not best-effort.
            if (destinationIsVirtualConsole && sourceGroup != destination)
            {
                context.check(conversionResult == Conversion::Result::Unsupported, "a conversion INTO Gen 1/2 was not refused", whereLabel + " result=" + resultName(conversionResult));
                context.check(converted == nullptr, "a refused Gen 1/2 conversion still produced an entity", whereLabel);
                continue;
            }

            if (destination == sourceGroup)
            {
                context.check(conversionResult == Conversion::Result::SameGroup, "converting to the same group did not report SameGroup", whereLabel + " result=" + resultName(conversionResult));
                context.check(converted == nullptr, "a same-group conversion produced an entity to throw away", whereLabel);
                continue;
            }

            // Whether the pair is connected at all is known independently of the gate. A connected
            // pair must never come back Unsupported, and an unconnected one must.
            const bool pairConnected = (inConversionFamily(sourceGroup) || sourceIsVirtualConsole) && inConversionFamily(destination);
            if (!pairConnected)
            {
                context.check(conversionResult == Conversion::Result::Unsupported, "a conversion between formats PKSE does not connect was not refused", whereLabel + " result=" + resultName(conversionResult));
                context.check(converted == nullptr, "a refused conversion still produced an entity", whereLabel);
                continue;
            }
            if (conversionResult != Conversion::Result::Ok)
            {
                context.check(conversionResult != Conversion::Result::Unsupported, "a conversion between connected formats was refused as Unsupported", whereLabel);
                // NotInDex / Blocked are real answers, not failures -- a species the destination
                // does not have cannot go there. They are noted so the matrix's coverage is visible.
                context.check(converted == nullptr, "a failed conversion still produced an entity", whereLabel + " result=" + resultName(conversionResult));
                context.note("conversion declined", whereLabel + " result=" + resultName(conversionResult));
                continue;
            }

            if (!context.check(converted != nullptr, "conversion reported Ok but produced nothing", whereLabel))
            {
                continue;
            }
            checkConverted(context, source, *converted, destination, whereLabel, sourceIsVirtualConsole);
            receivingHandlerRules(context, source, *converted, destination, MATRIX_RECEIVING_TRAINER, sourceIsVirtualConsole, whereLabel);
            if (sourceIsVirtualConsole)
            {
                // The Transporter rules are stated in terms of the PK7 the transfer lands in, so
                // they are checked at that endpoint. A Gen 1 Pokemon that carried on to Gen 8/9
                // keeps the origin and met data this asserts, but its EXP, level and moves have then
                // been through the destination's own rules as well.
                if (destination == Enums::GameVersion::SM || destination == Enums::GameVersion::USUM)
                {
                    transporterRules(context, source, *converted, whereLabel);
                }
            }

            // Not byte-identical, and never promised to be: the trip drops what the intermediate
            // format cannot hold (Legends: Arceus GVs, Let's Go AVs). What must survive is identity.
            Conversion::Result returnResult = Conversion::Result::Ok;
            auto returned = Conversion::convert(*converted, sourceGroup, returnResult);
            // Nothing goes back to a Game Boy game, so a Gen 1/2 source has no return trip. Anything
            // else came FROM the source format, so the way back must exist; Blocked is the one honest
            // refusal (Brilliant Diamond / Shining Pearl refuse Spinda and Nincada outright).
            if (!sourceIsVirtualConsole && returnResult != Conversion::Result::Blocked)
            {
                context.check(returnResult == Conversion::Result::Ok && returned != nullptr, "the return trip of a successful conversion was refused", whereLabel + " result=" + resultName(returnResult));
            }
            if (returnResult == Conversion::Result::Ok && returned)
            {
                const std::string roundTripLabel = whereLabel + " (and back)";
                context.check(returned->speciesID() == source.speciesID(), "a round-trip conversion changed the species", roundTripLabel);
                context.check(returned->id32() == source.id32(), "a round-trip conversion changed the trainer ID", roundTripLabel);
                // The way back is never a rebuild, so it carries whatever level the way out arrived at
                // -- the source's own, or the one a Gen 3 rebuild raised it to.
                const uint8_t expectedLevel = rebuildMayRaiseLevel(sourceGroup, destination) ? converted->level() : source.level();
                context.check(returned->level() == expectedLevel, "a round-trip conversion changed the level", roundTripLabel + " expected=" + std::to_string(expectedLevel) + " got=" + std::to_string(returned->level()));
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    context.check(ivOf(*returned, statIndex) == ivOf(source, statIndex), "a round-trip conversion changed an IV", roundTripLabel + " stat=" + std::to_string(statIndex));
                }
                context.check(returned->isStructurallyValid(), "a round-trip conversion produced an invalid entity", roundTripLabel);
            }
        }
    }
}
