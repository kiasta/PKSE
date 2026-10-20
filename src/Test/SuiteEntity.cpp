/**
 * This is the suite that reads a real save the way the details modal does and asks whether what
 * comes back is coherent. It is deliberately run against the USER'S OWN saves rather than against
 * synthetic data, because the interesting failures are the ones a hand-built entity never has: a
 * traded pokemon with a foreign OT, a form index nothing else in the codebase produces, a nickname in a
 * charset the writer cannot spell back, a species that predates the format it is sitting in.
 *
 * Three checks here earn their keep more than the rest:
 *
 *   CLONE IS BYTE-IDENTICAL. The bank's whole contract is byte-in == byte-out, and clone() is the
 *   operation that promise rests on. A clone that differs by one byte is a pokemon that changes every
 *   time it is moved.
 *
 *   STORED STATS vs RECALCULATION (party only). A party record stores the five numbers the GAME
 *   computed. Recomputing them from the base-stat table, the IV unpack, the EV/AV contribution, the
 *   nature and the level formula and comparing is an end-to-end proof of that entire chain -- a
 *   wrong species index, a byte-swapped read, a wrong base stat and a wrong nature all break it.
 *   Reported as a warning rather than a failure, with the per-stat delta, because a pokemon that has
 *   BATTLED since it last levelled up legitimately carries stale stats (EVs and Stat Experience
 *   only fold into the numbers on level-up). A systematic bug shows up as large deltas on every pokemon;
 *   battle drift shows up as a point or two on one.
 *
 *   THE NAME ROUND TRIP. A name PKSE can READ but cannot WRITE is a charset gap, and it is invisible
 *   in normal use because the read and the write are usually the same table -- which is exactly how
 *   Gen 3 ran for months with four different ad-hoc alphabets and names that silently degraded.
 */
#include <string>
#include <vector>

#include "Test/TestSuite.h"
#include "Enums/GameVersion.h"
#include "Trainer/Trainer.h"
#include "Trainer/TradeDonor.h" // can this save stand in as the other side of a trade?
#include "Names/FormNames.h"
#include "Names/ItemNames.h"
#include "Names/MoveInfo.h"
#include "Names/MoveNames.h"
#include "Names/MovePresence.h"
#include "Pokemon/AbilityInfo.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/Experience.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        /// Chunk 0 is the party; chunk 1..n are the boxes.
        constexpr int PARTY_CHUNK = 0;

        const char *STAT_NAMES[6] = {"HP", "Atk", "Def", "Spe", "SpA", "SpD"};

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

        int statOf(const Pokemon::Pokemon &pokemon, int statIndex)
        {
            switch (statIndex)
            {
            case 0:
                return pokemon.statHPMax();
            case 1:
                return pokemon.statATK();
            case 2:
                return pokemon.statDEF();
            case 3:
                return pokemon.statSPE();
            case 4:
                return pokemon.statSPA();
            default:
                return pokemon.statSPD();
            }
        }

        bool sameBytes(const Pokemon::Pokemon &firstPokemon, const Pokemon::Pokemon &secondPokemon, size_t &firstDifferenceOffset)
        {
            const auto firstBytes = firstPokemon.getData();
            const auto secondBytes = secondPokemon.getData();
            if (firstBytes.size() != secondBytes.size())
            {
                firstDifferenceOffset = 0;
                return false;
            }
            for (size_t byteIndex = 0; byteIndex < firstBytes.size(); ++byteIndex)
            {
                if (firstBytes[byteIndex] != secondBytes[byteIndex])
                {
                    firstDifferenceOffset = byteIndex;
                    return false;
                }
            }
            return true;
        }

        /// Number of differing bytes, and the LAST differing offset. The first one is nearly always
        /// the checksum (0x06 in PK8/PK9), which precedes every field it covers and so says nothing
        /// about what actually moved -- the useful pair is "how many, and how far in".
        size_t countDifferentBytes(const Pokemon::Pokemon &firstPokemon, const Pokemon::Pokemon &secondPokemon, size_t &lastDifferenceOffset)
        {
            const auto firstBytes = firstPokemon.getData();
            const auto secondBytes = secondPokemon.getData();
            const size_t byteCount = firstBytes.size() < secondBytes.size() ? firstBytes.size() : secondBytes.size();
            size_t differentByteCount = 0;
            lastDifferenceOffset = 0;
            for (size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex)
            {
                if (firstBytes[byteIndex] != secondBytes[byteIndex])
                {
                    ++differentByteCount;
                    lastDifferenceOffset = byteIndex;
                }
            }
            return differentByteCount;
        }

        /// One Pokemon, checked. `slotDescription` is already formatted for the report.
        void checkPokemon(Context &context, const Pokemon::Pokemon &pokemon, const std::string &slotDescription, bool isParty)
        {
            const std::string whereLabel = slotDescription + "  " + pokemonLabel(pokemon);

            // CAN THIS SAVE'S OWN TRAINER STAND IN AS A TRADE PARTNER FOR THIS POKEMON? A real save
            // answers both ways for free -- the Pokemon it caught itself must be REFUSED, because a
            // handler matching the OT on name, id and gender claims the player traded with
            // themselves, and anything caught by somebody else must be accepted.
            if (context.trainer != nullptr)
            {
                const Trainer::TradeDonor donor = Trainer::buildTradeDonor(*context.trainer, pokemon);
                const bool isOwnTrainer = Utils::utf8ToUtf16(context.trainer->trainerName) == pokemon.otName() && context.trainer->ID32 == pokemon.id32() && context.trainer->trainerGender == pokemon.otGender();
                Trainer::TradeDonorRefusal expectedRefusal = Trainer::TradeDonorRefusal::None;
                if (!pokemon.hasHandler())
                {
                    expectedRefusal = Trainer::TradeDonorRefusal::NoHandler;
                }
                else if (!Trainer::gamesCanTrade(context.trainer->getGameGroup(), pokemon.getGameGroup()))
                {
                    expectedRefusal = Trainer::TradeDonorRefusal::CannotTrade;
                }
                else if (isOwnTrainer)
                {
                    expectedRefusal = Trainer::TradeDonorRefusal::SameTrainer;
                }

                // NameUnstorable is the one answer a save cannot be made to produce on demand, so
                // it is accepted wherever it lands rather than predicted.
                if (donor.refusal != Trainer::TradeDonorRefusal::NameUnstorable)
                {
                    context.check(donor.refusal == expectedRefusal, "buildTradeDonor gave the wrong answer for this save's own trainer", whereLabel + " refusal=" + std::to_string(static_cast<int>(donor.refusal)) + " expected=" + std::to_string(static_cast<int>(expectedRefusal)));
                }
                if (donor.isAccepted())
                {
                    context.check(donor.partner.trainerName == Utils::utf8ToUtf16(context.trainer->trainerName), "an accepted trade donor did not carry the save trainer's name", whereLabel);
                    context.check(!donor.partner.trainerName.empty(), "an accepted trade donor has no name at all", whereLabel);
                }
            }

            // Everything below reads fields out of the record, so stop once it is known to be wrong.
            if (!context.check(pokemon.isStructurallyValid(), "stored Pokemon fails its own structural validity check (checksum, or the format's stand-in where there is none)", whereLabel)) return;

            const uint16_t speciesId = pokemon.speciesID();
            if (!context.check(speciesId <= Pokemon::PERSONAL_MAX_SPECIES, "species id is beyond the personal table", whereLabel)) return;

            const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(speciesId, 0);
            context.check(pokemon.formID() < personalInfo.formCount || personalInfo.formCount == 0, "form index is beyond the species' form count", whereLabel + " formCount=" + std::to_string(personalInfo.formCount));

            auto clonedPokemon = pokemon.clone();
            if (context.check(clonedPokemon != nullptr, "clone() returned nothing", whereLabel))
            {
                size_t firstDifferenceOffset = 0;
                context.check(sameBytes(pokemon, *clonedPokemon, firstDifferenceOffset), "clone is not byte-identical to its original", whereLabel + " " + hexField("offset", firstDifferenceOffset, 4));
            }

            context.check(pokemon.level() >= 1 && pokemon.level() <= 100, "level out of range", whereLabel + " level=" + std::to_string(pokemon.level()));

            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                context.check(ivOf(pokemon, statIndex) <= pokemon.maxIV(), "IV above this format's maximum", whereLabel + " stat=" + STAT_NAMES[statIndex] + " iv=" + std::to_string(ivOf(pokemon, statIndex)) + " max=" + std::to_string(pokemon.maxIV()));
                // evWide()/maxEV(), never evXXX(): Gen 1's Stat Experience is 16 bits per stat and
                // the 8-bit accessors SATURATE, so a trained pokemon reads as a flat 255 through them.
                context.check(pokemon.evWide(statIndex) <= pokemon.maxEV(), "EV above this format's maximum", whereLabel + " stat=" + STAT_NAMES[statIndex] + " ev=" + std::to_string(pokemon.evWide(statIndex)) + " max=" + std::to_string(pokemon.maxEV()));
            }

            // The 510 total is a modern-format rule. Gen 1 has no combined cap at all (its cap is
            // per-stat, at 65535), so asking the question there would be inventing a rule.
            if (pokemon.maxEV() == 255)
            {
                int effortValueTotal = 0;
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    effortValueTotal += pokemon.evWide(statIndex);
                }
                context.warn(effortValueTotal <= 510, "EV total exceeds the games' 510 limit", whereLabel + " total=" + std::to_string(effortValueTotal));
            }

            int moveCount = 0;
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = pokemon.move(moveIndex);
                if (moveId == 0)
                {
                    continue;
                }
                ++moveCount;
                if (!context.check(moveId <= Pokemon::LEARN_MAX_MOVE_ID, "move id is beyond the move table", whereLabel + " slot=" + std::to_string(moveIndex + 1) + " move=" + std::to_string(moveId)))
                {
                    continue;
                }
                context.check(Names::isMovePresent(moveId, context.group), "Pokemon holds a move that does not exist in this game", whereLabel + " slot=" + std::to_string(moveIndex + 1) + " move=\"" + Names::getMoveName(moveId) + "\"");
                // WARN, not check: this judges a STORED VALUE against the game's limit, exactly as
                // the EV-total, ability-slot, level-vs-EXP and battle-stat checks below it do, and
                // every one of those is a warning. A move id out of range or absent from the game
                // (the two checks above) means PKSE MISREAD the record; a PP above the maximum means
                // the record really holds that, which an edited or injected save legitimately can.
                //
                // A real Ruby save here has 16 of them, all in three consecutive boxes of a
                // living-dex injection, and every flagged value is that move's MODERN PP -- Vine Whip
                // 25 (Gen 6+, Gen 3 is 10), Absorb 25 and Mega Drain 15 (Gen 4+, Gen 3 is 20 and 10).
                // Whatever wrote them used a Gen 6 PP table. The rate is the signal, as with the
                // legality findings: 16 of 565 boxed Pokemon is a save, all 565 would be a bad table.
                const uint8_t maxPP = Names::getMoveMaxPP(moveId, pokemon.movePPUps(moveIndex), context.group);
                if (maxPP > 0)
                {
                    context.warn(pokemon.movePP(moveIndex) <= maxPP, "move PP above the move's maximum", whereLabel + " move=\"" + Names::getMoveName(moveId) + "\" pp=" + std::to_string(pokemon.movePP(moveIndex)) + " max=" + std::to_string(maxPP) + " ppUps=" + std::to_string(pokemon.movePPUps(moveIndex)));
                }
            }
            context.warn(moveCount > 0, "Pokemon has no moves at all", whereLabel);

            // canStoreNickname is the "can PKSE write back what it just read" question. A false here
            // means the reader's alphabet is wider than the writer's, which is silent data loss the
            // moment anything re-saves the name.
            context.check(pokemon.canStoreNickname(pokemon.nickname()), "PKSE can read this nickname but not write it back (charset gap)", whereLabel + " nick=\"" + Utils::utf16ToUtf8(pokemon.nickname()) + "\"");

            // Two different questions, and only one of them is a defect.
            //
            // Does the NAME survive? That is the check below, and a failure there is real data loss.
            //
            // Do the BYTES survive? Not necessarily, and legitimately so: the games leave whatever
            // was in the field before after the name's terminator, and a rewrite zero-fills it. The
            // checksum moves with it. Both are invisible to the game, and reporting them as warnings
            // buried the report -- twenty of them on two real saves, every one of them the residue
            // after a terminator. So the byte diff is a NOTE when the name itself came back intact,
            // and a warning only when it did not, where the bytes say what was lost.
            if (auto renamedPokemon = pokemon.clone())
            {
                const std::u16string originalNickname = pokemon.nickname();
                // Write a DIFFERENT name first and see it land, so a setter that stores nothing cannot
                // pass by leaving the original where it was. Not on an egg still carrying the games'
                // placeholder: an egg has no name of its own (PKHeX writes the placeholder whatever it
                // is given), and a Gen 3 egg's Japanese language would store the probe full-width.
                const std::u16string probeNickname = originalNickname == u"PKS" ? u"TRN" : u"PKS";
                if (!pokemon.eggTextIsPlaceholder() && renamedPokemon->canStoreNickname(probeNickname))
                {
                    renamedPokemon->setNickname(probeNickname);
                    context.check(renamedPokemon->nickname() == probeNickname, "setNickname did not store a new name", whereLabel + " wrote=\"" + Utils::utf16ToUtf8(probeNickname) + "\" read=\"" + Utils::utf16ToUtf8(renamedPokemon->nickname()) + "\"");
                }
                renamedPokemon->setNickname(originalNickname);
                const bool nameIntact = (renamedPokemon->nickname() == originalNickname);
                size_t firstDifferenceOffset = 0;
                size_t lastDifferenceOffset = 0;
                if (!sameBytes(pokemon, *renamedPokemon, firstDifferenceOffset))
                {
                    const size_t differentByteCount = countDifferentBytes(pokemon, *renamedPokemon, lastDifferenceOffset);
                    const std::string differenceDetail = whereLabel + " bytes=" + std::to_string(differentByteCount) + " " + hexField("first", firstDifferenceOffset, 4) + " " + hexField("last", lastDifferenceOffset, 4) + " nick=\"" + Utils::utf16ToUtf8(originalNickname) + "\"";
                    if (nameIntact)
                    {
                        context.note("re-writing a nickname normalises bytes the game left after it", differenceDetail);
                    }
                    else
                    {
                        context.warn(false, "re-writing a Pokemon's own nickname changes its bytes", differenceDetail);
                    }
                }
                context.check(nameIntact, "nickname does not survive a set/get round trip", whereLabel + " wrote=\"" + Utils::utf16ToUtf8(originalNickname) + "\" read=\"" + Utils::utf16ToUtf8(renamedPokemon->nickname()) + "\"");
            }

            if (pokemon.hasHeldItem() && pokemon.heldItem() != 0)
            {
                // "???" is getItemNameFor's out-of-range answer.
                const char *heldItemName = Names::getItemNameFor(context.group, pokemon.heldItem());
                context.check(heldItemName != nullptr && heldItemName[0] != '\0' && std::string(heldItemName) != "???", "held item has no name in this generation's item table", whereLabel + " item=" + std::to_string(pokemon.heldItem()));
            }

            if (pokemon.hasAbility() && pokemon.ability() != 0)
            {
                // Through getAbilitySlots, NOT the modern table directly: slots are per-generation
                // (Gen 4 has no hidden slot at all, and S/V moved several species' abilities), so
                // resolving a Gen 7 Piplup against Scarlet/Violet reported a legitimate starter as
                // holding an illegal ability. The pokemon's OWN group, not the save's -- a bank slot
                // holds a foreign Pokemon.
                const Pokemon::AbilitySlots abilitySlots = Pokemon::getAbilitySlots(speciesId, pokemon.formID(), pokemon.getGameGroup());
                std::string abilitySlotList;
                for (int slotIndex = 0; slotIndex < abilitySlots.count; ++slotIndex)
                {
                    abilitySlotList += (slotIndex ? "/" : "") + std::to_string(abilitySlots.slot[slotIndex]);
                }
                context.warn(Pokemon::isAbilityLegal(pokemon), "ability is not one of the species' ability slots", whereLabel + " ability=" + std::to_string(pokemon.ability()) + " slots=" + abilitySlotList);
            }

            // Only meaningful where the format stores BOTH. A format that derives level from EXP
            // cannot disagree with itself, so the check is trivially true there and costs nothing.
            if (pokemon.exp() > 0)
            {
                const uint8_t levelFromExp = Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(speciesId));
                context.warn(levelFromExp == pokemon.level(), "stored level disagrees with stored EXP", whereLabel + " level=" + std::to_string(pokemon.level()) + " expLevel=" + std::to_string(levelFromExp) + " exp=" + std::to_string(pokemon.exp()));
            }

            if (isParty)
            {
                if (auto recalculatedPokemon = pokemon.clone())
                {
                    int storedStats[6];
                    int recalculatedStats[6];
                    for (int statIndex = 0; statIndex < 6; ++statIndex)
                    {
                        storedStats[statIndex] = statOf(pokemon, statIndex);
                    }
                    recalculatedPokemon->recalculateStats();
                    for (int statIndex = 0; statIndex < 6; ++statIndex)
                    {
                        recalculatedStats[statIndex] = statOf(*recalculatedPokemon, statIndex);
                    }

                    // Gens 1-4 recompute stats only at a level-up, an evolution or a PC withdrawal, so
                    // a stat below the recalculation is Stat Experience or EVs gained since then, and
                    // normal. Only a stat ABOVE it says the formula is wrong.
                    const bool statsLagEffort = Enums::getVersionGeneration(Enums::getGroupRepVersion(context.group)) <= 4;
                    bool statsMatch = true;
                    bool onlyLagging = true;
                    std::string statDeltas;
                    for (int statIndex = 0; statIndex < 6; ++statIndex)
                    {
                        if (storedStats[statIndex] == recalculatedStats[statIndex])
                        {
                            continue;
                        }
                        statsMatch = false;
                        onlyLagging = onlyLagging && storedStats[statIndex] < recalculatedStats[statIndex];
                        statDeltas += std::string(statDeltas.empty() ? "" : " ") + STAT_NAMES[statIndex] + "=" + std::to_string(storedStats[statIndex]) + "->" + std::to_string(recalculatedStats[statIndex]);
                    }
                    if (!statsMatch && statsLagEffort && onlyLagging)
                    {
                        context.note("party Pokemon's stored battle stats lag a recalculation, as the game leaves them until the next level-up", whereLabel + " " + statDeltas);
                    }
                    else
                    {
                        context.warn(statsMatch, "party Pokemon's stored battle stats do not match a recalculation", whereLabel + " " + statDeltas);
                    }
                }
            }
        }

        /// Take a clone of this pokemon for the cross-generation suites, if the pool still has room for
        /// this target. Deliberately biased toward variety: species already sampled are skipped, so
        /// four samples are four different Pokemon rather than four Magikarp from the same box.
        void maybeSample(Context &context, const Pokemon::Pokemon &pokemon, int &takenThisTarget)
        {
            if (takenThisTarget >= SAMPLES_PER_TARGET) return;
            if (pokemon.speciesID() == 0 || !pokemon.isStructurallyValid()) return;
            for (const auto &sample : samples())
            {
                if (sample.version == context.version && sample.pokemon && sample.pokemon->speciesID() == pokemon.speciesID()) return;
            }
            if (auto copiedPokemon = pokemon.clone())
            {
                addSample(context.version, context.group, context.target, std::move(copiedPokemon));
                ++takenThisTarget;
            }
        }
    }

    int entityPlan(Context &context)
    {
        if (!context.trainer) return 0;
        return 1 + (int)context.trainer->getBoxCount();
    }

    void entityRun(Context &context, int chunk)
    {
        Trainer::Trainer *trainer = context.trainer;
        if (!trainer) return;

        // How many samples this target has contributed so far. Recomputed from the pool rather than
        // carried in a member, because the runner rebuilds the Context for every chunk.
        int takenThisTarget = 0;
        for (const auto &sample : samples())
        {
            if (sample.version == context.version)
            {
                ++takenThisTarget;
            }
        }

        if (chunk == PARTY_CHUNK)
        {
            const size_t maxPartySize = trainer->getPartySize();
            context.check(trainer->party.size() <= maxPartySize, "party holds more Pokemon than the game allows", "party=" + std::to_string(trainer->party.size()) + " max=" + std::to_string(maxPartySize));
            int filledSlotCount = 0;
            for (size_t partySlotIndex = 0; partySlotIndex < trainer->party.size(); ++partySlotIndex)
            {
                if (isEmptySlot(trainer->party[partySlotIndex].get()))
                {
                    continue;
                }
                ++filledSlotCount;
                checkPokemon(context, *trainer->party[partySlotIndex], partySlot((int)partySlotIndex), true);
                maybeSample(context, *trainer->party[partySlotIndex], takenThisTarget);
            }
            context.note("party Pokemon checked", "count=" + std::to_string(filledSlotCount));
            return;
        }

        const size_t boxIndex = (size_t)(chunk - 1);
        if (boxIndex >= trainer->boxes.size()) return;

        const size_t slotsPerBox = trainer->getSlotsPerBox();
        int filledSlotCount = 0;
        for (size_t slotIndex = 0; slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
        {
            if (isEmptySlot(trainer->boxes[boxIndex][slotIndex].get()))
            {
                continue;
            }
            // A slot past this game's box width is not addressable in the UI and will not be
            // written back, so a Pokemon sitting there is a Pokemon that quietly disappears.
            if (slotIndex >= slotsPerBox)
            {
                context.fail("Pokemon sits in a slot beyond this game's box size", slot((int)boxIndex, (int)slotIndex) + " boxSize=" + std::to_string(slotsPerBox));
                continue;
            }
            ++filledSlotCount;
            checkPokemon(context, *trainer->boxes[boxIndex][slotIndex], slot((int)boxIndex, (int)slotIndex), false);
            maybeSample(context, *trainer->boxes[boxIndex][slotIndex], takenThisTarget);
        }
        if (filledSlotCount > 0)
        {
            context.note("box Pokemon checked", "box=" + std::to_string(boxIndex + 1) + " count=" + std::to_string(filledSlotCount));
        }
    }
}
