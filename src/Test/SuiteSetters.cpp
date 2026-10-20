/**
 * Every Pokemon class is a set of raw offsets into a byte buffer, one class per game, most of them
 * near-identical to a sibling. That is the codebase's core abstraction and also its sharpest edge:
 * a copied-and-adjusted class with one offset left at the sibling's value compiles, runs, and gives
 * wrong answers that look like plausible data. Nothing in the app notices, because the getter and
 * the setter usually share the offset -- so a wrong pair round-trips perfectly WITHIN PKSE and only
 * diverges from the game.
 *
 * This suite works on a BLANK entity from Trainer::createBlankPokemon(), never on the user's data,
 * and asks the one question a wrong-offset pair cannot answer: does the field behave like a field?
 *
 * Every field is probed with an EXPECTATION stated independently of the setter:
 *
 *   - where Pokemon has a capability predicate (hasNature(), hasBall(), hasMetDate() ...) the
 *     predicate is the expectation, and the probe checks it in BOTH directions;
 *   - every other field is expected to be stored, except the explicit (group, field) absences in
 *     formatLacksField() below, each taken from PKHeX's entity class for that format.
 *
 * The classification:
 *
 *   read-back == written, field expected            -> correct.
 *   read-back == the previous value, field absent   -> correct: Gen 1 has no ball, and its no-op
 *                                                       setter is right. Listed in a note.
 *   read-back == the previous value, field EXPECTED -> FAILURE. A setter that stores nothing, or
 *                                                       writes somewhere its getter does not read.
 *   read-back == written, field listed as absent    -> FAILURE. The predicate or the absence list is
 *                                                       wrong, and something reading it hides a field.
 *   read-back == anything else                      -> FAILURE. A mask, a shift or an offset is wrong.
 *
 * Each field gets two probe values that both differ from the blank entity's value, so "not stored"
 * cannot be confused with "happened to already hold that value". Id-sized fields are probed with a
 * value above 255 wherever the format's ids reach that far, so a field cut to one byte cannot pass.
 * The field is restored afterwards so each test starts from the same entity.
 */
#include <string>
#include <vector>

#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        /// A species every generation PKSE handles has in its dex, so the entity stays coherent
        /// while the other fields are exercised.
        constexpr uint16_t PROBE_SPECIES = 25; // Pikachu

        /// A species with two DIFFERENT regular abilities (Run Away / Guts) in every personal table
        /// PKSE reads, Gen 3's included. Needed because the ability probe is meaningless on a
        /// single-ability species -- see the ability block below.
        constexpr uint16_t ABILITY_PROBE_SPECIES = 19; // Rattata

        /// Short enough for a Japanese Gen 1 record (5 characters) and spelled entirely in
        /// characters every charset PKSE reads has, including Gen 1's and Gen 3's. The nickname and
        /// the OT name get DIFFERENT probes, so a name written into the other's slot cannot pass.
        const std::u16string PROBE_NAME = u"PKS";
        const std::u16string PROBE_OT_NAME = u"TRN";

        /// What the test expects a field to do, stated without asking the setter.
        enum class Expect
        {
            Stored,
            Absent,
        };

        /// The fields a format genuinely does not store, for the fields Pokemon has no capability
        /// predicate for. Each row is from PKHeX's entity class for that format (PK1, PK2, PK3, PK4,
        /// PK5, PB7). Anything not listed here is expected to be stored, so a new format or a new
        /// field starts out held to the strict answer.
        bool formatLacksField(Enums::GameVersion group, const std::string &fieldName)
        {
            using Enums::GameVersion;
            const bool isGeneration1 = group == GameVersion::RBY;
            const bool isGeneration2 = group == GameVersion::GSC;
            const bool isGeneration3 = Enums::isGen3Group(group);
            const bool isGeneration4Or5 = group == GameVersion::DP || group == GameVersion::PT || group == GameVersion::HGSS || group == GameVersion::BW || group == GameVersion::B2W2;
            const bool isLetsGo = group == GameVersion::GG;
            const bool hasStatNature = group == GameVersion::SWSH || group == GameVersion::BDSP || group == GameVersion::PLA || group == GameVersion::SV || group == GameVersion::ZA;

            // No secret id before Gen 3: the trainer id is 16 bits.
            if (fieldName == "sid16") return isGeneration1 || isGeneration2;
            // PK1 has no OT gender. PK2 keeps it in the caught data, which every GSC record has room
            // for (Gold and Silver just never write it).
            if (fieldName == "otGender") return isGeneration1;
            // PK1 and PK2 have no language byte; the language belongs to the save.
            if (fieldName == "language") return isGeneration1 || isGeneration2;
            // Gen 1-3 forms are not stored: Unown's comes from its DVs or PID, and Gen 3's Castform
            // and Deoxys forms come from the game.
            if (fieldName == "form") return isGeneration1 || isGeneration2 || isGeneration3;
            // The minted stat nature is Gen 8 onward, and Let's Go has none.
            if (fieldName == "statNature") return !hasStatNature;
            // Gens 3-5 use the PID as the encryption constant; Gens 4 and 5 store no separate one.
            // (Gen 3's setter writes the PID itself, so it round trips there.)
            if (fieldName == "encryptionConstant") return isGeneration4Or5;
            // PK4 and PK5 store the ability id; the slot is derived from the PID, not stored.
            if (fieldName == "abilityNumber") return isGeneration4Or5;
            // Relearn moves are Gen 6 onward.
            if (fieldName.rfind("relearn", 0) == 0) return isGeneration1 || isGeneration2 || isGeneration3 || isGeneration4Or5;
            // The HP DV is derived from the other four in Gens 1 and 2.
            if (fieldName == "ivHP") return isGeneration1 || isGeneration2;
            // PB7 has EV bytes, but Let's Go has no way to earn EVs and PKHeX's legality requires them
            // to be 0, so PKSE deliberately does not write them. Not a missing fieldName: a refusal.
            if (fieldName.rfind("ev", 0) == 0) return isLetsGo;
            // No eggs in Gen 1 or in Let's Go.
            if (fieldName == "egg") return isGeneration1 || isLetsGo;
            return false;
        }

        /// Whether a format's item and move ids go past 255. Gens 1 and 2 keep both in one byte (and
        /// Gen 1 has only 165 moves, so the narrow probes stay under that); everything from Gen 3 on
        /// has more than 255 of each.
        bool hasWideIds(Enums::GameVersion group)
        {
            return group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC;
        }

        /// Gens 3-5: the formats whose PID decides the gender (and whose shiny threshold is 8).
        bool pidDecidesGenderIn(Enums::GameVersion group)
        {
            using Enums::GameVersion;
            return Enums::isGen3Group(group) || group == GameVersion::DP || group == GameVersion::PT || group == GameVersion::HGSS || group == GameVersion::BW || group == GameVersion::B2W2;
        }

        /// Shininess worked out from the record's own numbers, without asking the class: the DV
        /// pattern for Gens 1 and 2, the PID / trainer-id XOR everywhere else, against 8 for Gens 3-5
        /// and 16 from Gen 6 on (the 1-in-4096 odds; PKHeX's ShinyXor threshold per format).
        bool shinyByFormula(const Pokemon::Pokemon &pokemon, Enums::GameVersion group, uint32_t id32)
        {
            using Enums::GameVersion;
            if (group == GameVersion::RBY || group == GameVersion::GSC)
            {
                const uint8_t attack = pokemon.ivATK();
                return pokemon.ivDEF() == 10 && pokemon.ivSPE() == 10 && pokemon.ivSPA() == 10 && (attack == 2 || attack == 3 || attack == 6 || attack == 7 || attack == 10 || attack == 11 || attack == 14 || attack == 15);
            }
            const uint32_t pidValue = pokemon.pid();
            const uint32_t shinyXor = (pidValue >> 16) ^ (pidValue & 0xFFFF) ^ (id32 >> 16) ^ (id32 & 0xFFFF);
            return shinyXor < (pidDecidesGenderIn(group) ? 8u : 16u);
        }
    }

    int settersPlan(Context &context)
    {
        return context.trainer ? 1 : 0;
    }

    void settersRun(Context &context, int)
    {
        Trainer::Trainer *trainer = context.trainer;
        if (!trainer) return;

        auto blankPokemon = trainer->createBlankPokemon();
        if (!context.check(blankPokemon != nullptr, "createBlankPokemon() returned nothing")) return;
        Pokemon::Pokemon &pokemon = *blankPokemon;

        const std::string whereLabel = "blank " + Enums::getGameVersionName(context.group) + " entity";
        std::string notStoredFields;

        // The generic field probe. `readField`/`writeField` are lambdas over the entity so every
        // field reads the same way here regardless of its width. `expectation` is what the format
        // should do with it.
        auto roundTripField = [&](const char *fieldName, auto readField, auto writeField, const std::vector<uint32_t> &probeValues, Expect expectation)
        {
            const uint32_t originalValue = (uint32_t)readField();
            int probeCount = 0;
            int stuckCount = 0;
            int wrongCount = 0;
            std::string firstWrongDetail;
            for (uint32_t newValue : probeValues)
            {
                // A value the field already holds cannot tell "stored" from "ignored" apart, so it
                // is not a probe at all.
                if (newValue == originalValue)
                {
                    continue;
                }
                ++probeCount;
                writeField(newValue);
                const uint32_t readBack = (uint32_t)readField();
                if (readBack == newValue)
                {
                    continue;
                }
                if (readBack == originalValue)
                {
                    ++stuckCount;
                    continue;
                }
                ++wrongCount;
                if (firstWrongDetail.empty())
                {
                    firstWrongDetail = " wrote=" + std::to_string(newValue) + " read=" + std::to_string(readBack);
                }
            }
            const std::string fieldLabel = whereLabel + std::string(" field=") + fieldName;
            if (probeCount == 0)
            {
                context.skip("every probe value for this field was the value it already held, so it could not be tested", fieldLabel);
            }
            else if (wrongCount > 0)
            {
                context.fail("field does not round trip through its setter and getter", fieldLabel + firstWrongDetail);
            }
            else if (stuckCount > 0 && stuckCount < probeCount)
            {
                context.fail("field stores some values and silently drops others", fieldLabel + " dropped=" + std::to_string(stuckCount) + "/" + std::to_string(probeCount));
            }
            else if (expectation == Expect::Stored)
            {
                context.check(stuckCount == 0, "a field this format has was not stored (no-op setter, or a write the getter does not read)", fieldLabel);
            }
            else
            {
                if (context.check(stuckCount == probeCount, "a field this format is expected NOT to have was stored (the capability predicate or the absence list is wrong)", fieldLabel))
                {
                    notStoredFields += std::string(notStoredFields.empty() ? "" : ", ") + fieldName;
                }
            }
            writeField(originalValue);
        };
        // The expectation for a field with no capability predicate.
        auto expected = [&](const std::string &fieldName) { return formatLacksField(context.group, fieldName) ? Expect::Absent : Expect::Stored; };
        // The expectation for a field that has one.
        auto expectedIf = [](bool hasField) { return hasField ? Expect::Stored : Expect::Absent; };
        const bool wideIds = hasWideIds(context.group);

        roundTripField("species", [&] { return pokemon.speciesID(); }, [&](uint32_t newValue) { pokemon.setSpecies((uint16_t)newValue); }, {1u, PROBE_SPECIES}, Expect::Stored);
        pokemon.setSpecies(PROBE_SPECIES); // deliberately NOT restored: everything below wants a real species

        const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(PROBE_SPECIES, 0);
        if (personalInfo.formCount > 2)
        {
            roundTripField("form", [&] { return pokemon.formID(); }, [&](uint32_t newValue) { pokemon.setForm((uint8_t)newValue); }, {1u, 2u}, expected("form"));
        }

        roundTripField("pid", [&] { return pokemon.pid(); }, [&](uint32_t newValue) { pokemon.setPID(newValue); }, {0x12345678u, 0xABCDEF01u}, expectedIf(pokemon.hasPID()));
        if (pokemon.hasPID())
        {
            roundTripField("encryptionConstant", [&] { return pokemon.encryptionConstant(); }, [&](uint32_t newValue) { pokemon.setEncryptionConstant(newValue); }, {0x0BADF00Du, 0x1u}, expected("encryptionConstant"));
        }

        roundTripField("tid16", [&] { return pokemon.tid16(); }, [&](uint32_t newValue) { pokemon.setTID16((uint16_t)newValue); }, {12345u, 0x0102u}, Expect::Stored);
        roundTripField("sid16", [&] { return pokemon.sid16(); }, [&](uint32_t newValue) { pokemon.setSID16((uint16_t)newValue); }, {54321u, 0x0201u}, expected("sid16"));

        // The two halves and the composite must agree. A format that stores them at unrelated
        // offsets would still pass the two probes above and fail here.
        //
        // The expectation is DERIVED from what the format actually stored, not hardcoded: Gen 1 has
        // a 16-bit trainer id and NO secret id, so Pokemon1RBY::sid16() is a constant 0 and its
        // id32() is the TID alone -- correctly. Asserting a fixed 0x56781234 reported that as a
        // failure. What is genuinely universal is that the composite agrees with its own halves,
        // whatever the format chooses to keep, and that still catches two halves stored at
        // unrelated offsets.
        pokemon.setTID16(0x1234);
        pokemon.setSID16(0x5678);
        const uint16_t storedTID16 = pokemon.tid16();
        const uint16_t storedSID16 = pokemon.sid16();
        context.check(pokemon.id32() == ((static_cast<uint32_t>(storedSID16) << 16) | storedTID16), "id32 is not sid16<<16 | tid16", whereLabel + " " + hexField("id32", pokemon.id32(), 8) + " " + hexField("tid16", storedTID16, 4) + " " + hexField("sid16", storedSID16, 4));

        // A format with no secret id has a SIXTEEN-BIT id32, so a 32-bit probe would drop its top
        // half -- a real property of the format, exactly like the four-bit originGame field below,
        // rather than a broken round trip. Size the probe to what the format holds.
        const bool hasSecretId = (storedSID16 == 0x5678);
        roundTripField("id32", [&] { return pokemon.id32(); }, [&](uint32_t newValue) { pokemon.setId32(newValue); }, hasSecretId ? std::vector<uint32_t>{0xDEADBEEFu, 0x00010002u} : std::vector<uint32_t>{0x0000BEEFu, 0x00000002u}, Expect::Stored);

        roundTripField("otGender", [&] { return pokemon.otGender(); }, [&](uint32_t newValue) { pokemon.setOTGender((uint8_t)newValue); }, {1u, 0u}, expected("otGender"));
        roundTripField("language", [&] { return pokemon.language(); }, [&](uint32_t newValue) { pokemon.setLanguage((uint8_t)newValue); }, {2u, 5u}, expected("language"));
        // FireRed and LeafGreen. Deliberately small ids: Gen 3's origin field is FOUR BITS, so a
        // modern game's version byte (Sword is 44) truncates to 12 there -- a real property of the
        // format, not a round-trip failure, and probing with one would report the format as broken.
        roundTripField("originGame", [&] { return pokemon.originGame(); }, [&](uint32_t newValue) { pokemon.setOriginGame((uint8_t)newValue); }, {4u, 5u}, expectedIf(pokemon.hasOriginGame()));

        roundTripField("nature", [&] { return pokemon.nature(); }, [&](uint32_t newValue) { pokemon.setNature((uint8_t)newValue); }, {5u, 24u}, expectedIf(pokemon.hasNature()));
        roundTripField("statNature", [&] { return pokemon.statNature(); }, [&](uint32_t newValue) { pokemon.setStatNature((uint8_t)newValue); }, {7u, 3u}, expected("statNature"));
        if (pokemon.hasAbility())
        {
            // Probed on a species with a REAL ability choice, not on PROBE_SPECIES. Gen 3 stores the
            // ability as a single slot BIT and deliberately refuses to set slot 2 on a species whose
            // two slots resolve to the same ability -- PKHeX's AbilityVerifier only requires the bit
            // to be clear there, and Pokemon3FRLG follows it. Pikachu is such a species, so probing
            // on it asks a question the format is right to answer "no" to.
            pokemon.setSpecies(ABILITY_PROBE_SPECIES);
            const Pokemon::PersonalInfo &abilityPersonalInfo = Pokemon::getPersonalInfo(ABILITY_PROBE_SPECIES, 0);
            roundTripField("ability", [&] { return pokemon.ability(); }, [&](uint32_t newValue) { pokemon.setAbility((uint16_t)newValue); }, {(uint32_t)abilityPersonalInfo.ability1, (uint32_t)abilityPersonalInfo.ability2}, Expect::Stored);
            roundTripField("abilityNumber", [&] { return pokemon.abilityNumber(); }, [&](uint32_t newValue) { pokemon.setAbilityNumber((uint8_t)newValue); }, {1u, 2u}, expected("abilityNumber"));

            // There is deliberately no "the slot resolves to an ability" check. It is not a property
            // these formats share: PK8/PK9 store the ability ID (0x14) and the slot number (0x16) as
            // two INDEPENDENT fields the game keeps in step, so setting the slot alone is not meant
            // to change the id, while Gen 3 stores only the slot bit and derives the id from its own
            // table. The two probes above are the offset test; asserting a relationship between them
            // would fail correct code on five of the seven formats.
            pokemon.setSpecies(PROBE_SPECIES);
        }
        roundTripField("heldItem", [&] { return pokemon.heldItem(); }, [&](uint32_t newValue) { pokemon.setHeldItem((uint16_t)newValue); }, {17u, wideIds ? 275u : 200u}, expectedIf(pokemon.hasHeldItem()));
        roundTripField("friendship", [&] { return pokemon.friendship(); }, [&](uint32_t newValue) { pokemon.setFriendship((uint8_t)newValue); }, {70u, 255u}, expectedIf(pokemon.hasFriendship()));
        roundTripField("ball", [&] { return pokemon.ball(); }, [&](uint32_t newValue) { pokemon.setBall((uint8_t)newValue); }, {4u, 1u}, expectedIf(pokemon.hasBall()));
        roundTripField("gender", [&] { return pokemon.gender(); }, [&](uint32_t newValue) { pokemon.setGender((uint8_t)newValue); }, {0u, 1u}, expectedIf(pokemon.hasStoredGender()));
        // Gen 2's hasMetData() says whether THIS record carries caught data, not whether the format
        // can: every PK2 has the bytes, and a blank one answers false until they are written. So the
        // met fields are expected stored there whatever the blank record says.
        const bool metDataExpected = pokemon.hasMetData() || context.group == Enums::GameVersion::GSC;
        roundTripField("metLocation", [&] { return pokemon.metLocation(); }, [&](uint32_t newValue) { pokemon.setMetLocation((uint16_t)newValue); }, {6u, 1u}, expectedIf(metDataExpected));
        roundTripField("metLevel", [&] { return pokemon.metLevel(); }, [&](uint32_t newValue) { pokemon.setMetLevel((uint8_t)newValue); }, {5u, 50u}, expectedIf(metDataExpected));
        // The date has its own predicate: Gen 3 has a met location and level but no date.
        roundTripField("metYear", [&] { return pokemon.metYear(); }, [&](uint32_t newValue) { pokemon.setMetYear((uint8_t)newValue); }, {24u, 1u}, expectedIf(pokemon.hasMetDate()));
        roundTripField("metMonth", [&] { return pokemon.metMonth(); }, [&](uint32_t newValue) { pokemon.setMetMonth((uint8_t)newValue); }, {12u, 1u}, expectedIf(pokemon.hasMetDate()));
        roundTripField("metDay", [&] { return pokemon.metDay(); }, [&](uint32_t newValue) { pokemon.setMetDay((uint8_t)newValue); }, {28u, 1u}, expectedIf(pokemon.hasMetDate()));
        roundTripField("pokerus", [&] { return pokemon.isPokerusInfected() ? 1u : 0u; }, [&](uint32_t newValue) { pokemon.setPokerus(newValue ? 0x12 : 0); }, {1u, 0u}, expectedIf(pokemon.hasPokerus()));

        // The two probes differ in every bit so a mask error cannot pass both.
        roundTripField("hyperTrainFlags", [&] { return (uint32_t)pokemon.hyperTrainFlags(); }, [&](uint32_t newValue) { pokemon.setHyperTrainFlags((uint8_t)newValue); }, {0x15u, 0x2Au}, expectedIf(pokemon.hasHyperTraining()));

        // A Hyper Trained stat must READ as a maximal IV while the stored IV stays where it was.
        // Both halves matter: the stat calculation depends on the first, and the legality tables and
        // the IV editor depend on the second.
        if (pokemon.hasHyperTraining())
        {
            const uint8_t savedFlags = pokemon.hyperTrainFlags();
            pokemon.setHyperTrainFlags(0);
            pokemon.setIV(0, 0);
            context.check(pokemon.effectiveIV(0) == 0, "effectiveIV does not report the stored IV when the stat is not Hyper Trained", whereLabel + " got=" + std::to_string(pokemon.effectiveIV(0)));
            pokemon.setHyperTrained(Pokemon::Pokemon::HT_HP, true);
            context.check(pokemon.effectiveIV(0) == pokemon.maxIV(), "a Hyper Trained stat does not read as a maximal IV", whereLabel + " got=" + std::to_string(pokemon.effectiveIV(0)) + " maxIV=" + std::to_string(pokemon.maxIV()));
            context.check(pokemon.ivHP() == 0, "Hyper Training changed the STORED IV, which it must never do", whereLabel + " ivHP=" + std::to_string(pokemon.ivHP()));
            pokemon.setHyperTrainFlags(savedFlags);
        }

        static const char *statNames[6] = {"HP", "Atk", "Def", "Spe", "SpA", "SpD"};
        auto readIV = [&](int statIndex) -> uint32_t
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
        };
        for (int statIndex = 0; statIndex < 6; ++statIndex)
        {
            // The HP DV of Gens 1 and 2 is DERIVED from the low bits of the other four -- not stored,
            // not settable -- so it correctly refuses to take a value of its own.
            const std::string fieldName = std::string("iv") + statNames[statIndex];
            roundTripField(fieldName.c_str(), [&] { return readIV(statIndex); }, [&](uint32_t newValue) { pokemon.setIV(statIndex, (uint8_t)newValue); }, {(uint32_t)pokemon.maxIV(), 0u, (uint32_t)(pokemon.maxIV() / 2)}, expected(fieldName));
        }
        for (int statIndex = 0; statIndex < 6; ++statIndex)
        {
            // evWide / setEVWide, never the 8-bit pair: Gen 1's Stat Experience is 16 bits and the
            // narrow accessors saturate, so testing through them would pass while losing data.
            const std::string fieldName = std::string("ev") + statNames[statIndex];
            roundTripField(fieldName.c_str(), [&] { return (uint32_t)pokemon.evWide(statIndex); }, [&](uint32_t newValue) { pokemon.setEVWide(statIndex, (uint16_t)newValue); }, {(uint32_t)pokemon.maxEV(), 0u, (uint32_t)(pokemon.maxEV() / 2)}, expected(fieldName));
        }
        {
            auto readAV = [&](int statIndex) -> uint32_t
            {
                switch (statIndex)
                {
                case 0:
                    return pokemon.avHP();
                case 1:
                    return pokemon.avATK();
                case 2:
                    return pokemon.avDEF();
                case 3:
                    return pokemon.avSPE();
                case 4:
                    return pokemon.avSPA();
                default:
                    return pokemon.avSPD();
                }
            };
            for (int statIndex = 0; statIndex < 6; ++statIndex)
            {
                const std::string fieldName = std::string("av") + statNames[statIndex];
                roundTripField(fieldName.c_str(), [&] { return readAV(statIndex); }, [&](uint32_t newValue) { pokemon.setAV(statIndex, (uint8_t)newValue); }, {200u, 0u, 100u}, expectedIf(pokemon.hasAwakeningValues()));
            }
        }

        for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
        {
            const std::string moveFieldName = "move" + std::to_string(moveIndex + 1);
            roundTripField(moveFieldName.c_str(), [&] { return pokemon.move(moveIndex); }, [&](uint32_t newValue) { pokemon.setMove(moveIndex, (uint16_t)newValue); }, {33u, wideIds ? 266u : 150u}, Expect::Stored);
            const std::string ppFieldName = "movePP" + std::to_string(moveIndex + 1);
            roundTripField(ppFieldName.c_str(), [&] { return pokemon.movePP(moveIndex); }, [&](uint32_t newValue) { pokemon.setMovePP(moveIndex, (uint8_t)newValue); }, {20u, 5u}, Expect::Stored);
            const std::string ppUpsFieldName = "movePPUps" + std::to_string(moveIndex + 1);
            roundTripField(ppUpsFieldName.c_str(), [&] { return pokemon.movePPUps(moveIndex); }, [&](uint32_t newValue) { pokemon.setMovePPUps(moveIndex, (uint8_t)newValue); }, {3u, 1u}, Expect::Stored);
            const std::string relearnFieldName = "relearn" + std::to_string(moveIndex + 1);
            roundTripField(relearnFieldName.c_str(), [&] { return pokemon.relearnMove(moveIndex); }, [&](uint32_t newValue) { pokemon.setRelearnMove(moveIndex, (uint16_t)newValue); }, {33u, 266u}, expected(relearnFieldName));
        }

        // Two legitimate designs live behind these: PK8/PK9 STORE a level byte in the party tail,
        // while PA8, PB7 and PK3 derive it from EXP. Both are correct; what is not correct is a
        // level that follows neither.
        {
            const uint8_t growthRate = Pokemon::getGrowthRate(PROBE_SPECIES);
            const uint32_t level50Exp = Pokemon::getExpForLevel(50, growthRate);
            pokemon.setExp(level50Exp);
            const uint8_t levelAfterExp = pokemon.level();
            if (levelAfterExp == 50)
            {
                context.note("level is derived from EXP in this format", whereLabel);
            }
            else
            {
                pokemon.setLevel(50);
                context.note("level is stored separately from EXP in this format", whereLabel);
            }
            context.check(pokemon.level() == 50, "level does not follow EXP or its own setter", whereLabel + " expLevel=" + std::to_string(levelAfterExp) + " afterSetLevel=" + std::to_string(pokemon.level()));
            context.check(pokemon.exp() == level50Exp, "EXP does not round trip", whereLabel + " wrote=" + std::to_string(level50Exp) + " read=" + std::to_string(pokemon.exp()));
        }

        if (pokemon.canStoreNickname(PROBE_NAME))
        {
            // Both written before either is read, so a name stored in the other's slot fails one of
            // the two checks instead of passing both.
            pokemon.setNickname(PROBE_NAME);
            pokemon.setOTName(PROBE_OT_NAME);
            context.check(pokemon.nickname() == PROBE_NAME, "nickname does not round trip", whereLabel + " wrote=\"" + Utils::utf16ToUtf8(PROBE_NAME) + "\" read=\"" + Utils::utf16ToUtf8(pokemon.nickname()) + "\"");
            context.check(pokemon.otName() == PROBE_OT_NAME, "OT name does not round trip", whereLabel + " wrote=\"" + Utils::utf16ToUtf8(PROBE_OT_NAME) + "\" read=\"" + Utils::utf16ToUtf8(pokemon.otName()) + "\"");
        }
        else
        {
            context.skip("format refused a plain-ASCII probe name, so the name round trip was not tested", whereLabel + " name=\"" + Utils::utf16ToUtf8(PROBE_NAME) + "\"");
        }

        // hasHandler() must agree with what the buffer actually does, in BOTH directions, because
        // the two disagreeing is invisible on screen either way. A format that has the field but
        // reports false hides a real HT the user needs to see -- that is the shape of the bug where
        // a Sword pokemon traded to Shield showed no handler row at all. A format that reports true but
        // cannot store one invents a field, and the details view then prints "(none)" forever for
        // something the game never had.
        {
            const std::u16string handlerProbe = u"HT";
            pokemon.setHTName(handlerProbe);
            const bool handlerStored = (pokemon.htName() == handlerProbe);
            context.check(handlerStored == pokemon.hasHandler(), "hasHandler() disagrees with whether the format can actually store one", whereLabel + " hasHandler=" + (pokemon.hasHandler() ? "1" : "0") + " stored=" + (handlerStored ? "1" : "0"));
            if (pokemon.hasHandler())
            {
                // The flag says who is IN CHARGE, which is what decides whose friendship, affection
                // and memories the game reads. A no-op setter here leaves every traded pokemon reading
                // as still handled by its OT.
                pokemon.setCurrentHandler(1);
                context.check(pokemon.currentHandler() == 1, "the current-handler flag does not round trip", whereLabel + " read=" + std::to_string(pokemon.currentHandler()));
                pokemon.setCurrentHandler(0);
                context.check(pokemon.currentHandler() == 0, "the current-handler flag could not be set back to the OT", whereLabel + " read=" + std::to_string(pokemon.currentHandler()));
                pokemon.setHTName(std::u16string()); // leave the probe entity as it was found
            }
        }

        // Everything a REAL TRADE writes beyond the HT name (see Pokemon/Evolution.h). Each gets its
        // own predicate rather than riding on hasHandler(), because every one of them is genuinely
        // absent from at least one format that HAS a handler: Let's Go has no memories, no language
        // and no geolocation; Gens 6 and 7 have no language; Gen 8 dropped affection and
        // geolocation. Each predicate is checked in BOTH directions for the reason hasHandler() is --
        // reporting true without storing invents a field, reporting false while storing hides one.
        {
            pokemon.setHTLanguage(2); // English. The probe must be non-zero: 0 is "unset".
            context.check((pokemon.htLanguage() == 2) == pokemon.hasHandlerLanguage(), "hasHandlerLanguage() disagrees with whether the format stores one", whereLabel + " has=" + (pokemon.hasHandlerLanguage() ? "1" : "0") + " read=" + std::to_string(pokemon.htLanguage()));
            pokemon.setHTLanguage(0);

            pokemon.setHTAffection(7);
            context.check((pokemon.htAffection() == 7) == pokemon.hasHandlerAffection(), "hasHandlerAffection() disagrees with whether the format stores one", whereLabel + " has=" + (pokemon.hasHandlerAffection() ? "1" : "0") + " read=" + std::to_string(pokemon.htAffection()));
            pokemon.setHTAffection(0);

            // The four memory fields are written and cleared together, so one predicate covers them
            // -- but all four are probed with DIFFERENT values, because a single wrong offset among
            // them is exactly the thing that survives a test of the other three. These are the
            // values a link trade actually writes: memory 4 "Link trade to [place]", variable 9
            // "Pokecenter", intensity 1.
            pokemon.setHTMemory(4);
            pokemon.setHTMemoryVariable(9);
            pokemon.setHTMemoryIntensity(1);
            pokemon.setHTMemoryFeeling(13);
            const bool memoriesStored = pokemon.htMemory() == 4 && pokemon.htMemoryVariable() == 9 && pokemon.htMemoryIntensity() == 1 && pokemon.htMemoryFeeling() == 13;
            context.check(memoriesStored == pokemon.hasHandlerMemories(), "hasHandlerMemories() disagrees with whether the format stores them", whereLabel + " has=" + (pokemon.hasHandlerMemories() ? "1" : "0") + " memory=" + std::to_string(pokemon.htMemory()) + " variable=" + std::to_string(pokemon.htMemoryVariable()) + " intensity=" + std::to_string(pokemon.htMemoryIntensity()) + " feeling=" + std::to_string(pokemon.htMemoryFeeling()));
            pokemon.setHTMemory(0);
            pokemon.setHTMemoryVariable(0);
            pokemon.setHTMemoryIntensity(0);
            pokemon.setHTMemoryFeeling(0);

            // EVERY SLOT IS PROBED, and country and region get different probe values. A format that
            // stored only slot 0 would pass a one-slot test and then silently lose the trade chain on
            // the second trade; one that swapped country and region -- they are stored region FIRST,
            // which is the opposite of how the pair reads -- would pass a same-value test.
            bool everySlotStored = true;
            for (int slotIndex = 0; slotIndex < Pokemon::Pokemon::GEOLOCATION_SLOT_COUNT; ++slotIndex)
            {
                const uint8_t countryProbe = static_cast<uint8_t>(0x10 + slotIndex);
                const uint8_t regionProbe = static_cast<uint8_t>(0x20 + slotIndex);
                pokemon.setGeolocationCountry(slotIndex, countryProbe);
                pokemon.setGeolocationRegion(slotIndex, regionProbe);
                if (pokemon.geolocationCountry(slotIndex) != countryProbe || pokemon.geolocationRegion(slotIndex) != regionProbe)
                {
                    everySlotStored = false;
                }
            }
            context.check(everySlotStored == pokemon.hasGeolocation(), "hasGeolocation() disagrees with whether the format stores the five-slot history", whereLabel + " has=" + (pokemon.hasGeolocation() ? "1" : "0") + " allSlotsStored=" + (everySlotStored ? "1" : "0"));
            for (int slotIndex = 0; slotIndex < Pokemon::Pokemon::GEOLOCATION_SLOT_COUNT; ++slotIndex)
            {
                pokemon.setGeolocationCountry(slotIndex, 0);
                pokemon.setGeolocationRegion(slotIndex, 0);
            }
        }

        // setShiny is exercised even where there is no PID: Gen 1 has none, but the DV pattern that
        // makes a transferred pokemon shiny is real and settable. The answer is checked against the
        // FORMULA, not only against isShiny(): a setShiny and an isShiny that agree on the wrong
        // rule would pass each other. Where the PID also carries the nature (Gens 3 and 4), a shiny
        // reroll must keep it.
        //
        // It is repeated, because the PID is random: Gens 4 and 5 once drew random PIDs until one
        // fit, and gave up silently about one time in eleven. Thirty passes miss a failure that
        // common less than one time in seventeen. Each pass also holds what else the PID decides in
        // Gens 3-5 -- the gender, which PKHeX requires the stored gender to agree with (the species'
        // ratio from the shared table, not the class), and the ability slot's bit (bit 16 for a
        // Pokemon born in Gen 5, bit 0 before) -- since a reroll that ignored them broke both.
        //
        // It runs on Rattata, which has two abilities and both genders, so the bit and the gender are
        // real constraints (Pikachu has one ability in Gen 3, which leaves its bit free).
        {
            pokemon.setSpecies(ABILITY_PROBE_SPECIES);
            const uint32_t id32 = 0x56781234u;
            const uint8_t natureBefore = pokemon.nature();
            const uint8_t genderBefore = pokemon.gender();
            const bool pidDecidesGender = pidDecidesGenderIn(context.group);
            const int originGeneration = Enums::getVersionGeneration(pokemon.originGame());
            const bool usesGeneration5AbilityBit = (context.group == Enums::GameVersion::BW || context.group == Enums::GameVersion::B2W2) && originGeneration != 3 && originGeneration != 4;
            const int abilityBitIndex = !pidDecidesGender ? -1 : usesGeneration5AbilityBit ? 16 : 0;
            const uint32_t abilityBitBefore = abilityBitIndex < 0 ? 0 : (pokemon.pid() >> abilityBitIndex) & 1;
            const uint8_t genderRatio = Pokemon::getPersonalInfo(pokemon.speciesID(), pokemon.form()).genderRatio;
            int notShinyCount = 0;
            int stillShinyCount = 0;
            int natureChangedCount = 0;
            int genderChangedCount = 0;
            int genderDisagreesCount = 0;
            int abilityBitChangedCount = 0;
            std::string firstFailure;
            auto noteFailure = [&](int &failureCount, const char *failureName)
            {
                if (failureCount++ == 0 && firstFailure.empty())
                {
                    firstFailure = std::string(failureName) + " " + hexField("pid", pokemon.pid(), 8);
                }
            };
            auto checkHeld = [&]()
            {
                if (pokemon.hasNature() && pokemon.nature() != natureBefore)
                {
                    noteFailure(natureChangedCount, "nature");
                }
                if (pidDecidesGender && pokemon.gender() != genderBefore)
                {
                    noteFailure(genderChangedCount, "gender");
                }
                if (pidDecidesGender && genderRatio != 0 && genderRatio < 254 && pokemon.gender() != (((pokemon.pid() & 0xFF) < genderRatio) ? 1 : 0))
                {
                    noteFailure(genderDisagreesCount, "pidGender");
                }
                if (abilityBitIndex >= 0 && ((pokemon.pid() >> abilityBitIndex) & 1) != abilityBitBefore)
                {
                    noteFailure(abilityBitChangedCount, "abilityBit");
                }
            };
            constexpr int shinyPassCount = 30;
            for (int passIndex = 0; passIndex < shinyPassCount; ++passIndex)
            {
                pokemon.setShiny(true, id32);
                if (!shinyByFormula(pokemon, context.group, id32))
                {
                    noteFailure(notShinyCount, "setShiny(true)");
                }
                checkHeld();
                pokemon.setShiny(false, id32);
                if (shinyByFormula(pokemon, context.group, id32))
                {
                    noteFailure(stillShinyCount, "setShiny(false)");
                }
                checkHeld();
            }
            const std::string tallyDetail = whereLabel + " passes=" + std::to_string(shinyPassCount) + " first=" + firstFailure;
            context.check(notShinyCount == 0, "setShiny(true) did not make the Pokemon shiny", tallyDetail + " misses=" + std::to_string(notShinyCount));
            context.check(stillShinyCount == 0, "setShiny(false) left the Pokemon shiny", tallyDetail + " misses=" + std::to_string(stillShinyCount));
            context.check(natureChangedCount == 0, "changing shininess changed the nature", tallyDetail + " times=" + std::to_string(natureChangedCount));
            context.check(genderChangedCount == 0, "changing shininess changed the gender", tallyDetail + " times=" + std::to_string(genderChangedCount));
            context.check(genderDisagreesCount == 0, "after a shininess change the stored gender disagrees with the PID's", tallyDetail + " times=" + std::to_string(genderDisagreesCount) + " ratio=" + std::to_string(genderRatio));
            context.check(abilityBitChangedCount == 0, "changing shininess changed the PID's ability-slot bit", tallyDetail + " times=" + std::to_string(abilityBitChangedCount) + " bit=" + std::to_string(abilityBitIndex));

            pokemon.setShiny(true, id32);
            context.check(pokemon.isShiny(id32, pokemon.species()), "isShiny() disagrees with the shiny formula on a shiny Pokemon", whereLabel);

            // regeneratePID is a touch-up that was asked to change nothing a user can see: a shiny
            // Pokemon stays shiny, and keeps its nature and gender. Gens 4-7 once replaced the PID at
            // random, which turned a shiny Pokemon plain.
            if (pokemon.hasPID())
            {
                pokemon.regeneratePID(id32);
                context.check(shinyByFormula(pokemon, context.group, id32), "regeneratePID made a shiny Pokemon plain", whereLabel + " " + hexField("pid", pokemon.pid(), 8));
                context.check(!pokemon.hasNature() || pokemon.nature() == natureBefore, "regeneratePID changed the nature", whereLabel + " after=" + std::to_string(pokemon.nature()));
                context.check(pokemon.gender() == genderBefore, "regeneratePID changed the gender", whereLabel + " after=" + std::to_string(pokemon.gender()));
            }

            // A gender change on a two-gender species: where the PID decides it, the PID has to give
            // the new gender too, or PKHeX reports a PID-gender mismatch.
            if (pidDecidesGender && pokemon.hasStoredGender() && genderRatio != 0 && genderRatio < 254)
            {
                const uint8_t otherGender = genderBefore == 0 ? 1 : 0;
                pokemon.setGender(otherGender);
                context.check(pokemon.gender() == otherGender, "setGender did not change the gender", whereLabel + " wanted=" + std::to_string(otherGender));
                context.check(pokemon.gender() == (((pokemon.pid() & 0xFF) < genderRatio) ? 1 : 0), "after setGender the stored gender disagrees with the PID's", whereLabel + " " + hexField("pid", pokemon.pid(), 8));
                context.check(shinyByFormula(pokemon, context.group, id32), "setGender made a shiny Pokemon plain", whereLabel + " " + hexField("pid", pokemon.pid(), 8));
                pokemon.setGender(genderBefore);
            }

            pokemon.setShiny(false, id32);
            context.check(!pokemon.isShiny(id32, pokemon.species()), "isShiny() disagrees with the shiny formula on a non-shiny Pokemon", whereLabel);
            pokemon.setSpecies(PROBE_SPECIES);
        }

        pokemon.setEgg(true);
        const Expect eggExpected = expected("egg");
        if (eggExpected == Expect::Absent)
        {
            if (context.check(!pokemon.isEgg(), "a format listed as having no eggs stored the egg flag", whereLabel))
            {
                notStoredFields += std::string(notStoredFields.empty() ? "" : ", ") + "egg";
            }
        }
        else if (context.check(pokemon.isEgg(), "egg flag does not round trip", whereLabel))
        {
            // THE EGG BIT IS NOT THE WHOLE EGG in a format that stamps a placeholder with it. Gen 3
            // writes the Japanese word for egg as the nickname and language 1 alongside the flag, on
            // every cartridge, and PKHeX writes the same pair in PK3.IsEgg -- so a record carrying
            // only the bit is one no save holds, and one the legality rule (which compares the
            // nickname against the egg name in the record's OWN language) reads as wrong.
            if (pokemon.eggTextIsPlaceholder())
            {
                context.check(pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese), "an egg was written without the language its games stamp beside the flag", whereLabel);
                context.check(pokemon.nickname() == std::u16string(u"\u30bf\u30de\u30b4"), "an egg was written without the placeholder nickname its games stamp", whereLabel);
            }
            pokemon.setEgg(false);
            context.check(!pokemon.isEgg(), "the egg flag could not be cleared", whereLabel);
        }

        // A record must be self-consistent after being written to. Setters are not required to keep
        // the checksum fresh -- the caller refreshes it, as every edit path does -- so this refreshes
        // and then asks the record to validate. That fails when refreshChecksum() writes the
        // checksum somewhere checksumValid() does not read it, or when a setter above left a field
        // out of the range the record accepts.
        pokemon.refreshChecksum();
        context.check(pokemon.isStructurallyValid(), "entity does not validate after every setter has been exercised and the checksum refreshed", whereLabel);

        // recalculateStats() must produce real numbers for a real species at a real level. A zero
        // here is the shape of a missing base-stat table -- the failure that produced wrong stats
        // across a whole generation once already.
        pokemon.recalculateStats();
        context.check(pokemon.statHPMax() > 0 && pokemon.statATK() > 0 && pokemon.statDEF() > 0 && pokemon.statSPE() > 0 && pokemon.statSPA() > 0 && pokemon.statSPD() > 0, "recalculated stats include a zero (missing base-stat data?)", whereLabel + " hp=" + std::to_string(pokemon.statHPMax()) + " atk=" + std::to_string(pokemon.statATK()) + " def=" + std::to_string(pokemon.statDEF()) + " spe=" + std::to_string(pokemon.statSPE()) + " spa=" + std::to_string(pokemon.statSPA()) + " spd=" + std::to_string(pokemon.statSPD()));

        // The stat FORMULA, against values worked out by hand: a level 50 Pikachu, every IV 31, no
        // EVs, a neutral nature. (2*base + IV) * 50 / 100 + 5, and + level + 10 for HP. Pikachu's
        // Defense and Sp. Def went up in Gen 6. Let's Go (awakening values), the two Legends games
        // (their own systems) and Gens 1-2 (DVs and Stat Experience, covered by build-gen1.sh) use
        // other formulas and are left out.
        {
            using Enums::GameVersion;
            const GameVersion group = context.group;
            const bool usesOtherStatFormula = group == GameVersion::RBY || group == GameVersion::GSC || group == GameVersion::GG || group == GameVersion::PLA || group == GameVersion::ZA;
            if (!usesOtherStatFormula)
            {
                const bool isBeforeGeneration6 = Enums::isGen3Group(group) || group == GameVersion::DP || group == GameVersion::PT || group == GameVersion::HGSS || group == GameVersion::BW || group == GameVersion::B2W2;
                pokemon.setSpecies(PROBE_SPECIES);
                pokemon.setForm(0);
                pokemon.setNature(0); // Hardy, neutral
                pokemon.setStatNature(0);
                pokemon.setHyperTrainFlags(0);
                pokemon.setExp(Pokemon::getExpForLevel(50, Pokemon::getGrowthRate(PROBE_SPECIES)));
                pokemon.setLevel(50);
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    pokemon.setIV(statIndex, 31);
                    pokemon.setEVWide(statIndex, 0);
                }
                pokemon.recalculateStats();
                const int expectedStats[6] = {110, 75, isBeforeGeneration6 ? 50 : 60, 110, 70, isBeforeGeneration6 ? 60 : 70};
                const int calculatedStats[6] = {pokemon.statHPMax(), pokemon.statATK(), pokemon.statDEF(),
                                                pokemon.statSPE(), pokemon.statSPA(), pokemon.statSPD()};
                bool statsMatch = true;
                std::string calculatedStatsDetail;
                for (int statIndex = 0; statIndex < 6; ++statIndex)
                {
                    statsMatch = statsMatch && calculatedStats[statIndex] == expectedStats[statIndex];
                    calculatedStatsDetail += (statIndex ? "/" : "") + std::to_string(calculatedStats[statIndex]);
                }
                context.check(statsMatch && pokemon.nature() == 0, "recalculated stats do not match the game's formula for a known Pokemon", whereLabel + " got=" + calculatedStatsDetail + " nature=" + std::to_string(pokemon.nature()));
            }
        }

        if (!notStoredFields.empty())
        {
            context.note("fields this format does not store (setter is a no-op, which is correct where the generation genuinely has no such field)", whereLabel + " fields=\"" + notStoredFields + "\"");
        }
    }
}
