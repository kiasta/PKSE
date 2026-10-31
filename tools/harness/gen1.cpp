/**
 * Gen 1 is the first format PKSE reads that is BIG ENDIAN, has no checksum to catch a bad parse,
 * and stores its battle stats. That last property is what makes this harness decisive: the record
 * carries the five numbers the game itself computed, so recomputing them from the base-stat table,
 * the DV unpack and the Gen 1 stat formula and comparing is a real end-to-end proof. A wrong
 * species index, a byte-swapped read, a wrong base stat or a wrong DV nibble all break it. With no
 * checksum there is nothing else that would.
 *
 *   ./tools/harness/build-gen1.sh [path/to/pksm_1.bnk]
 *
 * Checks, in order:
 *   1. Table self-consistency: species index round trip, type mapping, PP bounds.
 *   2. Every Gen 1 record in the bank parses to a valid entity.
 *   3. recalculateStats() reproduces all five STORED battle stats exactly.
 *   4. Byte-exact round trip: construct -> clone -> compare (the bank's byte-in == byte-out rule).
 *   5. Names decode, and re-encoding a decoded name reproduces the original bytes.
 *   6. Setters preserve the invariants they claim to (PP Ups vs PP, Special aliasing, level sync).
 *
 * Exits non-zero on any failure.
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Gen1Tables.h"
#include "Pokemon/PersonalInfo1RBY.h"
#include "Pokemon/Experience.h"
#include "Utils/Gen1Text.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/PokemonTypes.h"
#include "Legality/Legality.h"
#include "Trainer/Trainer1RBY.h"

namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logEventToFile(const std::string &) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
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

    int g_failures = 0;
    int g_checks = 0;

    void check(bool passed, const char *description, const std::string &detail = "")
    {
        ++g_checks;
        if (!passed)
        {
            ++g_failures;
            std::printf("  FAIL  %s%s%s\n", description, detail.empty() ? "" : " -- ", detail.c_str());
        }
    }

    std::string toUtf8(const std::u16string &utf16Text)
    {
        std::string utf8Text;
        for (char16_t codeUnit : utf16Text)
        {
            if (codeUnit < 0x80)
            {
                utf8Text += static_cast<char>(codeUnit);
            }
            else if (codeUnit < 0x800)
            {
                utf8Text += static_cast<char>(0xC0 | (codeUnit >> 6));
                utf8Text += static_cast<char>(0x80 | (codeUnit & 0x3F));
            }
            else
            {
                utf8Text += static_cast<char>(0xE0 | (codeUnit >> 12));
                utf8Text += static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F));
                utf8Text += static_cast<char>(0x80 | (codeUnit & 0x3F));
            }
        }
        return utf8Text;
    }

    void testTables()
    {
        std::printf("[1] Gen 1 tables\n");
        int roundTrippedCount = 0;
        for (uint16_t speciesId = 1; speciesId <= Pokemon::MAX_SPECIES_GEN1; ++speciesId)
        {
            const uint8_t internalIndex = Pokemon::nationalToG1(speciesId);
            check(internalIndex != 0, "species has an internal index", "national " + std::to_string(speciesId));
            if (internalIndex == 0)
            {
                continue;
            }
            check(Pokemon::g1ToNational(internalIndex) == speciesId, "species index round trip", "national " + std::to_string(speciesId) + " -> internal " + std::to_string(internalIndex) + " -> " + std::to_string(Pokemon::g1ToNational(internalIndex)));
            ++roundTrippedCount;

            const Pokemon::PersonalRecord &baseStats = Pokemon::getPersonalInfo1RBY(speciesId, 0);
            check(baseStats.hp != 0, "has a base-stat row", "national " + std::to_string(speciesId));
            if (baseStats.hp == 0)
            {
                continue;
            }
            check(baseStats.hp > 0 && baseStats.atk > 0 && baseStats.spa > 0, "base stats non-zero", "national " + std::to_string(speciesId));
            // Types must be real PKSE ids, and must survive the inverse mapping. A type that does not
            // round trip means the ROM<->PKSE tables disagree, which draws the wrong type icon.
            check(baseStats.type1 != 255, "types mapped", "national " + std::to_string(speciesId));
            check(Pokemon::g1TypeToPKSE(Pokemon::pkseTypeToG1(baseStats.type1)) == baseStats.type1, "type1 round trip", "national " + std::to_string(speciesId));
            // A MONO-TYPED SPECIES REPORTS TYPE_NONE IN SLOT 2, not a repeat of slot 1. That is the
            // table's convention and it is what stops the badge row drawing "Normal Normal"; the ROM
            // itself stores the id twice, and Pokemon1RBY::setSpecies re-expands the sentinel when it
            // writes a record. TYPE_NONE is not a type, so there is nothing to round trip.
            if (baseStats.type2 != Pokemon::PERSONAL_TYPE_NONE)
            {
                check(Pokemon::g1TypeToPKSE(Pokemon::pkseTypeToG1(baseStats.type2)) == baseStats.type2, "type2 round trip", "national " + std::to_string(speciesId));
            }
            // Gender ratio must be one of the canonical values -- an arbitrary byte here would mean
            // the personal_rb byte 0 is NOT the substituted ratio we believe it is.
            const uint8_t genderRatio = baseStats.genderRatio;
            check(genderRatio == 0 || genderRatio == 31 || genderRatio == 63 || genderRatio == 127 || genderRatio == 191 || genderRatio == 225 || genderRatio == 254 || genderRatio == 255, "gender ratio is canonical", "national " + std::to_string(speciesId) + " ratio " + std::to_string(genderRatio));
        }
        std::printf("      %d/%d species round-trip through the internal index\n", roundTrippedCount, Pokemon::MAX_SPECIES_GEN1);

        // Gen 1's unused "Bird" type must NOT map to anything.
        check(Pokemon::g1TypeToPKSE(6) == 255, "ROM type 6 (unused Bird) maps to TYPE_NONE");

        int movesWithPPCount = 0;
        for (uint16_t moveId = 1; moveId <= Pokemon::MAX_MOVE_GEN1; ++moveId)
        {
            const uint8_t movePP = Pokemon::getMovePPGen1(moveId);
            check(movePP > 0 && movePP <= 40, "move PP in range", "move " + std::to_string(moveId) + " pp " + std::to_string(movePP));
            if (movePP)
            {
                ++movesWithPPCount;
            }
            // 3 PP Ups must never exceed the 6-bit PP field the record actually has.
            check(Pokemon::getMovePPGen1WithUps(moveId, 3) <= 63, "PP with 3 Ups fits 6 bits", "move " + std::to_string(moveId));
        }
        std::printf("      %d/%d moves have PP\n", movesWithPPCount, Pokemon::MAX_MOVE_GEN1);
    }

    void testLearnsets()
    {
        std::printf("[1b] Gen 1 learnsets + types\n");
        const auto gameGroup = Enums::GameVersion::RBY;

        int speciesWithPoolCount = 0;
        int learnablePairCount = 0;
        for (uint16_t speciesId = 1; speciesId <= Pokemon::MAX_SPECIES_GEN1; ++speciesId)
        {
            int learnableMoveCount = 0;
            for (uint16_t moveId = 1; moveId <= Pokemon::MAX_MOVE_GEN1; ++moveId)
            {
                if (Pokemon::isLearnable(speciesId, 0, gameGroup, moveId))
                {
                    ++learnableMoveCount;
                }
            }
            // A species with an EMPTY pool is the failure this whole table exists to prevent: every
            // move then reads as illegal and the transfer sanitizer deletes all four.
            check(learnableMoveCount > 0, "species has a non-empty move pool", "national " + std::to_string(speciesId));
            if (learnableMoveCount > 0)
            {
                ++speciesWithPoolCount;
            }
            learnablePairCount += learnableMoveCount;
            // Nothing outside Gen 1's move range may be reachable.
            for (uint16_t moveId = Pokemon::MAX_MOVE_GEN1 + 1; moveId < 300; ++moveId)
            {
                check(!Pokemon::isLearnable(speciesId, 0, gameGroup, moveId), "no post-Gen-1 move is learnable", "national " + std::to_string(speciesId) + " move " + std::to_string(moveId));
            }
        }
        std::printf("      %d/%d species have a pool, %d learnable (species, move) pairs\n", speciesWithPoolCount, Pokemon::MAX_SPECIES_GEN1, learnablePairCount);

        // The initial moves in the personal table are the ones that exist ONLY there -- lvlmove
        // starts above level 1 for most species -- so they are the specific regression to pin.
        check(Pokemon::isLearnable(1, 0, gameGroup, 33), "Bulbasaur can learn Tackle (personal-table initial move)");
        check(Pokemon::isLearnable(1, 0, gameGroup, 45), "Bulbasaur can learn Growl (personal-table initial move)");
        check(Pokemon::isLearnable(1, 0, gameGroup, 73), "Bulbasaur can learn Leech Seed (level-up)");
        check(Pokemon::isLearnable(1, 0, gameGroup, 22), "Bulbasaur can learn Vine Whip (level-up)");
        // Pre-evolution fold: Raichu never learns these itself, it keeps them from Pikachu.
        check(Pokemon::isLearnable(26, 0, gameGroup, 84), "Raichu keeps Thunder Shock from Pikachu (pre-evo fold)");
        check(Pokemon::isLearnable(26, 0, gameGroup, 98), "Raichu keeps Quick Attack from Pikachu (pre-evo fold)");
        // TM/HM out of the personal bitfield.
        check(Pokemon::isLearnable(1, 0, gameGroup, 92), "Bulbasaur can learn Toxic (TM06)");
        check(Pokemon::isLearnable(151, 0, gameGroup, 5), "Mew learns Mega Punch");
        // Mew learns every TM/HM in Gen 1 -- its personal TM bitfield is all 0xFF.
        check(Pokemon::isLearnable(151, 0, gameGroup, 15), "Mew learns Cut (HM01)");
        // Surf-Pikachu (Stadium) is in the pool for the Pikachu line and nothing else.
        check(Pokemon::isLearnable(25, 0, gameGroup, 57), "Pikachu can know Surf (Stadium)");
        check(!Pokemon::isLearnable(19, 0, gameGroup, 57), "Rattata cannot know Surf");
        // A move that plainly should not be reachable.
        check(!Pokemon::isLearnable(1, 0, gameGroup, 143), "Bulbasaur cannot learn Sky Attack");

        // Types come from Gen 1's table, not the modern one.
        {
            using namespace Pokemon;
            TypePair clefairyTypes = getPokemonTypes(35, 0, gameGroup);      // Clefairy: Normal in Gen 1, Fairy from Gen 6
            check(clefairyTypes.type1 == TYPE_NORMAL, "Clefairy is Normal in Gen 1", "got " + std::to_string(clefairyTypes.type1));
            TypePair magnemiteTypes = getPokemonTypes(81, 0, gameGroup);      // Magnemite: Electric, not Electric/Steel
            check(magnemiteTypes.type1 == TYPE_ELECTRIC && !hasSecondType(magnemiteTypes), "Magnemite is pure Electric in Gen 1");
            TypePair rattataTypes = getPokemonTypes(19, 0, gameGroup);      // stored duplicated -> must collapse
            check(rattataTypes.type1 == TYPE_NORMAL && !hasSecondType(rattataTypes), "a Gen 1 mono-type collapses to one badge");
            TypePair gyaradosTypes = getPokemonTypes(130, 0, gameGroup);
            check(gyaradosTypes.type1 == TYPE_WATER && gyaradosTypes.type2 == TYPE_FLYING, "Gyarados is Water/Flying");
        }
    }

    void testCharset()
    {
        std::printf("[2] Gen 1 character sets\n");
        for (int japaneseCharset = 0; japaneseCharset <= 1; ++japaneseCharset)
        {
            int glyphCount = 0;
            for (int byteValue = 0; byteValue < 256; ++byteValue)
            {
                const char16_t glyph = Utils::gen1ToChar(static_cast<uint8_t>(byteValue), japaneseCharset != 0);
                if (glyph == 0)
                {
                    continue;
                }
                ++glyphCount;
                const uint8_t reencodedByte = Utils::charToGen1(glyph, japaneseCharset != 0);
                check(reencodedByte == byteValue, japaneseCharset ? "JP byte round trip" : "EN byte round trip", "byte 0x" + std::to_string(byteValue) + " -> back 0x" + std::to_string(reencodedByte));
            }
            std::printf("      %s: %d glyphs, all round-trip\n", japaneseCharset ? "JP" : "EN", glyphCount);
        }
        check(Utils::gen1ToChar(0x50, false) == 0, "0x50 is a terminator in EN");
        check(Utils::gen1ToChar(0x50, true) == 0, "0x50 is a terminator in JP");
        check(Utils::gen1ToChar(0x80, false) == u'A', "EN 0x80 is 'A'");
        check(Utils::gen1ToChar(0xE8, false) == u'․', "EN 0xE8 is a one-dot leader (MR.MIME)");
    }

    uint32_t readUInt32LittleEndian(const uint8_t *source)
    {
        return static_cast<uint32_t>(source[0]) | (static_cast<uint32_t>(source[1]) << 8) | (static_cast<uint32_t>(source[2]) << 16) | (static_cast<uint32_t>(source[3]) << 24);
    }

    void testBank(const char *bankPath)
    {
        std::printf("[3] Real Gen 1 records from %s\n", bankPath);
        FILE *bankFile = std::fopen(bankPath, "rb");
        if (!bankFile)
        {
            std::printf("  SKIP  cannot open (not an error if you have no bank here)\n");
            return;
        }
        std::fseek(bankFile, 0, SEEK_END);
        const long bankFileSize = std::ftell(bankFile);
        std::fseek(bankFile, 0, SEEK_SET);
        std::vector<uint8_t> bankBytes(static_cast<size_t>(bankFileSize));
        if (std::fread(bankBytes.data(), 1, bankBytes.size(), bankFile) != bankBytes.size())
        {
            std::printf("  FAIL  short read\n");
            ++g_failures;
            std::fclose(bankFile);
            return;
        }
        std::fclose(bankFile);

        if (bankBytes.size() < 16 || std::memcmp(bankBytes.data(), "PKSMBANK", 8) != 0)
        {
            std::printf("  SKIP  not a v3 PKSM bank\n");
            return;
        }
        constexpr size_t headerSize = 16;
        constexpr size_t entrySize = 0x150;
        const size_t entryCount = (bankBytes.size() - headerSize) / entrySize;

        int gen1RecordCount = 0;
        int statsReproducedCount = 0;
        int roundTripCount = 0;
        int namesReencodedCount = 0;
        int shinyCount = 0;
        int nicknamedCount = 0;
        int japaneseCount = 0;
        int legalityCleanCount = 0;
        int legalityWarningCount = 0;
        int invalidShownCount = 0;
        int warningShownCount = 0;
        int glitchMewRefusalCount = 0;
        std::string sampleLines;
        for (size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
        {
            const uint8_t *entry = bankBytes.data() + headerSize + entryIndex * entrySize;
            if (readUInt32LittleEndian(entry) != 7)
            {
                continue;   // PKSM generation tag 7 == Gen 1 (NOT 1 -- see PKSMBank.h)
            }
            ++gen1RecordCount;
            const uint8_t *payload = entry + 4;

            // PKSM writes the whole single-entry list. Its length is not recorded, so recover it the
            // way the format intends: an international list is 69 bytes, a Japanese one 59, and the
            // padding past the end is 0xFF.
            size_t listLength = Pokemon::SIZE_1ULIST;
            if (payload[Pokemon::SIZE_1JLIST] == 0xFF && payload[Pokemon::SIZE_1JLIST - 1] != 0xFF)
            {
                bool paddingIsAllFF = true;
                for (size_t paddingIndex = Pokemon::SIZE_1JLIST; paddingIndex < Pokemon::SIZE_1ULIST; ++paddingIndex)
                {
                    if (payload[paddingIndex] != 0xFF)
                    {
                        paddingIsAllFF = false;
                        break;
                    }
                }
                if (paddingIsAllFF)
                {
                    listLength = Pokemon::SIZE_1JLIST;
                }
            }
            if (listLength == Pokemon::SIZE_1JLIST)
            {
                ++japaneseCount;
            }

            std::span<const std::byte> listBytes(reinterpret_cast<const std::byte *>(payload), listLength);
            Pokemon::Pokemon1RBY pokemon(listBytes);

            const std::string slotLabel = "slot " + std::to_string(entryIndex) + " (" + std::string(pokemon.species()) + ")";
            check(pokemon.isValid(), "record is a valid Gen 1 entity", slotLabel);
            if (!pokemon.isValid())
            {
                continue;
            }

            const uint16_t storedHP = pokemon.statHPMax();
            const uint16_t storedAttack = pokemon.statATK();
            const uint16_t storedDefense = pokemon.statDEF();
            const uint16_t storedSpeed = pokemon.statSPE();
            const uint16_t storedSpecial = pokemon.statSPA();
            pokemon.recalculateStats();
            const bool statsMatch = pokemon.statHPMax() == storedHP && pokemon.statATK() == storedAttack && pokemon.statDEF() == storedDefense && pokemon.statSPE() == storedSpeed && pokemon.statSPA() == storedSpecial;
            check(statsMatch, "recalculateStats reproduces the stored stats", statsMatch ? "" : slotLabel + " stored " + std::to_string(storedHP) + "/" + std::to_string(storedAttack) + "/" + std::to_string(storedDefense) + "/" + std::to_string(storedSpeed) + "/" + std::to_string(storedSpecial) + " got " + std::to_string(pokemon.statHPMax()) + "/" + std::to_string(pokemon.statATK()) + "/" + std::to_string(pokemon.statDEF()) + "/" + std::to_string(pokemon.statSPE()) + "/" + std::to_string(pokemon.statSPA()));
            if (statsMatch)
            {
                ++statsReproducedCount;
            }

            auto clonedPokemon = pokemon.clone();
            check(clonedPokemon != nullptr, "clone", slotLabel);
            if (clonedPokemon)
            {
                const bool cloneIdentical = clonedPokemon->getData().size() == listLength && std::memcmp(clonedPokemon->getData().data(), payload, listLength) == 0;
                check(cloneIdentical, "clone is byte-identical to the source", slotLabel);
                if (cloneIdentical)
                {
                    ++roundTripCount;
                }
            }

            const std::u16string nickname = pokemon.nickname();
            const std::u16string originalTrainerName = pokemon.otName();
            check(!nickname.empty(), "nickname decodes to something", slotLabel);
            {
                Pokemon::Pokemon1RBY probe(listBytes);
                probe.setNickname(nickname);
                probe.setOTName(originalTrainerName);
                const bool nicknameBytesMatch = std::memcmp(probe.getData().data() + probe.ofsNick(), payload + probe.ofsNick(), probe.nameLen()) == 0;
                // The OT field can hold the in-game-trade marker, which is not a name and does not
                // re-encode; exclude only that case, and count it separately below.
                const bool otBytesMatch = pokemon.isInGameTradeOT() || std::memcmp(probe.getData().data() + probe.ofsOT(), payload + probe.ofsOT(), probe.nameLen()) == 0;
                check(nicknameBytesMatch, "nickname re-encodes to the original bytes", slotLabel + " '" + toUtf8(nickname) + "'");
                check(otBytesMatch, "OT name re-encodes to the original bytes", slotLabel + " '" + toUtf8(originalTrainerName) + "'");
                if (nicknameBytesMatch && otBytesMatch)
                {
                    ++namesReencodedCount;
                }
            }

            // Every move a REAL Gen 1 Pokemon is carrying must be in its pool. This is the check
            // that would have caught an empty or wrong learnset table: these mons were caught and
            // trained on hardware, so anything reported unlearnable here is the table being wrong,
            // not the Pokemon.
            for (int moveSlot = 0; moveSlot < 4; ++moveSlot)
            {
                const uint16_t moveId = pokemon.move(moveSlot);
                if (moveId == 0)
                {
                    continue;
                }
                check(Pokemon::isLearnable(pokemon.speciesID(), 0, Enums::GameVersion::RBY, moveId), "a real Pokemon's move is in its learn pool", slotLabel + " move " + std::to_string(moveId));
                // PP must be within what the move can hold with its recorded PP Ups.
                const uint8_t maxPP = Pokemon::getMovePPGen1WithUps(moveId, pokemon.movePPUps(moveSlot));
                check(pokemon.movePP(moveSlot) <= maxPP, "move PP within its Gen 1 maximum", slotLabel + " move " + std::to_string(moveId) + " pp " + std::to_string(pokemon.movePP(moveSlot)) + " max " + std::to_string(maxPP));
            }

            // Every one of these Pokemon was caught and trained on real hardware, so any "Invalid"
            // here is the checker misapplying a rule from a later generation -- the exact failure
            // this pass exists to catch (a saturated Stat Exp read as a 255 EV, ability 0 read as an
            // illegal ability). Warnings are tolerated and counted; Invalid is a hard failure.
            {
                const Legality::Report legalityReport = Legality::analyze(pokemon, Enums::GameVersion::RBY);
                int invalidCount = 0;
                for (const auto &issue : legalityReport.issues)
                {
                    if (issue.severity == Legality::Severity::Invalid)
                    {
                        ++invalidCount;
                        if (invalidShownCount < 6)
                        {
                            ++invalidShownCount;
                            std::printf("      INVALID %s: %s\n", slotLabel.c_str(), issue.text.c_str());
                        }
                    }
                    else if (issue.severity == Legality::Severity::Warning)
                    {
                        ++legalityWarningCount;
                        if (warningShownCount < 4)
                        {
                            ++warningShownCount;
                            std::printf("      warn    %s: %s\n", slotLabel.c_str(), issue.text.c_str());
                        }
                    }
                }
                // THE ONE REFUSAL THAT IS RIGHT. Gen 1's Mew exists only through events, and every Gen 1
                // event fixes a trainer -- the Virtual Console Mew is OT "GF" with trainer id 22796
                // (PKHeX EncounterGift1). A Mew carrying any other trainer came from the Mew glitch,
                // which PKHeX calls illegal too, so its finding is the checker telling the truth rather
                // than misapplying a rule. Counted, so a bank full of them cannot hide a real failure.
                constexpr uint16_t mewSpeciesId = 151;
                constexpr uint16_t virtualConsoleMewTrainerId = 22796;
                const bool isGlitchMew = pokemon.speciesID() == mewSpeciesId && pokemon.tid16() != virtualConsoleMewTrainerId;
                if (isGlitchMew && invalidCount > 0)
                {
                    ++glitchMewRefusalCount;
                    std::printf("      (expected: %s is a glitch Mew, which PKHeX also refuses)\n", slotLabel.c_str());
                }
                else
                {
                    check(invalidCount == 0, "no bogus Invalid legality finding on a real Pokemon", slotLabel);
                }
                if (invalidCount == 0)
                {
                    ++legalityCleanCount;
                }
            }

            if (pokemon.isShiny(0, ""))
            {
                ++shinyCount;
            }
            if (pokemon.isNicknamed())
            {
                ++nicknamedCount;
            }

            const uint8_t levelFromExp = Pokemon::getLevelFromExp(pokemon.exp(), pokemon.growthRate());
            check(pokemon.level() == levelFromExp, "stored level agrees with EXP", slotLabel + " stored " + std::to_string(pokemon.level()) + " vs EXP-derived " + std::to_string(levelFromExp));

            if (gen1RecordCount <= 3)
            {
                sampleLines += "        " + std::string(pokemon.species()) + " '" + toUtf8(nickname) + "'" + " OT '" + toUtf8(originalTrainerName) + "'" + " Lv" + std::to_string(pokemon.level()) + " DV " + std::to_string(pokemon.ivHP()) + "/" + std::to_string(pokemon.ivATK()) + "/" + std::to_string(pokemon.ivDEF()) + "/" + std::to_string(pokemon.ivSPE()) + "/" + std::to_string(pokemon.ivSPA()) + " stats " + std::to_string(storedHP) + "/" + std::to_string(storedAttack) + "/" + std::to_string(storedDefense) + "/" + std::to_string(storedSpeed) + "/" + std::to_string(storedSpecial) + "\n";
            }
        }

        std::printf("      Gen 1 records          : %d  (%d Japanese)\n", gen1RecordCount, japaneseCount);
        std::printf("      battle stats reproduced: %d/%d\n", statsReproducedCount, gen1RecordCount);
        std::printf("      byte-exact round trip  : %d/%d\n", roundTripCount, gen1RecordCount);
        std::printf("      names re-encode exactly: %d/%d\n", namesReencodedCount, gen1RecordCount);
        std::printf("      shiny DV pattern       : %d   nicknamed: %d\n", shinyCount, nicknamedCount);
        std::printf("      legality: %d/%d with no Invalid finding (%d warnings total, %d glitch Mew refused as PKHeX refuses it)\n", legalityCleanCount, gen1RecordCount, legalityWarningCount, glitchMewRefusalCount);
        if (!sampleLines.empty())
        {
            std::printf("%s", sampleLines.c_str());
        }
    }

    void testSetters()
    {
        std::printf("[4] Setter invariants\n");
        Pokemon::Pokemon1RBY pokemon(false);
        pokemon.setSpecies(6);      // Charizard
        pokemon.setLevel(50);

        check(pokemon.speciesID() == 6, "setSpecies survives a read-back");
        check(pokemon.speciesInternal() == Pokemon::nationalToG1(6), "internal index written");
        check(pokemon.rd8(1) == pokemon.speciesInternal(), "list header marker matches the body");
        check(pokemon.rd8(0) == 1, "list count is 1 once occupied");
        check(pokemon.level() == 50, "setLevel survives a read-back");
        check(pokemon.exp() == Pokemon::getExpForLevel(50, pokemon.growthRate()), "setLevel writes matching EXP");
        // Charizard is Fire/Flying. The STORED type bytes must be ROM ids, not PKSE ids.
        check(pokemon.storedType1() == 9 && pokemon.storedType2() == 2, "stored types map back to Fire/Flying", "got " + std::to_string(pokemon.storedType1()) + "/" + std::to_string(pokemon.storedType2()));

        // Special aliasing: writing SpA must be visible through SpD, and vice versa.
        pokemon.setIV(4, 12);
        check(pokemon.ivSPA() == 12 && pokemon.ivSPD() == 12, "SpA and SpD are the one Special DV");
        pokemon.setIV(5, 7);
        check(pokemon.ivSPA() == 7, "writing SpD writes the same Special DV");
        pokemon.setIV(0, 15);
        check(pokemon.ivHP() != 15 || (pokemon.ivATK() & 1), "HP DV is derived, not settable");
        pokemon.setIV(1, 99);
        check(pokemon.ivATK() == 15, "IV writes clamp to the 4-bit maximum");

        pokemon.setEVWide(0, 40000);
        check(pokemon.evWide(0) == 40000, "Stat Experience keeps its full 16-bit width");
        check(pokemon.evHP() == 255, "the 8-bit view saturates rather than truncating");
        pokemon.setEVWide(4, 1234);
        check(pokemon.evWide(5) == 1234, "SpA and SpD are the one Special Stat Exp counter");

        // A move change must bring its own PP.
        pokemon.setMove(0, 63);   // Fire Spin (Gen 1 PP 15)
        check(pokemon.move(0) == 63, "setMove survives a read-back");
        check(pokemon.movePP(0) == Pokemon::getMovePPGen1(63), "setMove resets PP to the move's own value");
        pokemon.setMovePPUps(0, 3);
        check(pokemon.movePPUps(0) == 3, "PP Ups survive a read-back");
        check(pokemon.move(0) == 63, "writing PP Ups does not disturb the move id");
        pokemon.setMovePP(0, 63);
        check(pokemon.movePPUps(0) == 3, "writing PP does not disturb PP Ups");

        // Shininess is a DV pattern, and clearing it must not disturb a non-shiny Pokemon.
        pokemon.setShiny(true, 0);
        check(pokemon.isShiny(0, ""), "setShiny(true) produces the shiny DV pattern");
        check(pokemon.ivDEF() == 10 && pokemon.ivSPE() == 10 && pokemon.ivSPA() == 10, "shiny DVs are DEF/SPE/SPC = 10");
        check((pokemon.ivATK() & 2) != 0, "shiny ATK DV has bit 1 set");
        pokemon.setShiny(false, 0);
        check(!pokemon.isShiny(0, ""), "setShiny(false) clears the pattern");
        const uint16_t dvBefore = pokemon.dv16();
        pokemon.setShiny(false, 0);
        check(pokemon.dv16() == dvBefore, "clearing an already non-shiny Pokemon changes nothing");

        // Names.
        pokemon.setNickname(u"CHARIZARD");
        check(pokemon.nickname() == u"CHARIZARD", "nickname round trip");
        check(!pokemon.isNicknamed(), "a name equal to the species is not a nickname");
        pokemon.setNickname(u"BLAZE");
        check(pokemon.isNicknamed(), "a different name is a nickname");
        pokemon.setNickname(u"ABCDEFGHIJKLMNOP");
        check(pokemon.nickname().size() == 10, "an over-long nickname is capped at 10 characters", "got " + std::to_string(pokemon.nickname().size()));
        check(!pokemon.canStoreNickname(u"ABCDEFGHIJK"), "an 11-character nickname is rejected");
        check(pokemon.canStoreNickname(u"ABCDEFGHIJ"), "a 10-character nickname is accepted");

        // A Japanese list is narrower in every respect.
        Pokemon::Pokemon1RBY japanesePokemon(true);
        check(japanesePokemon.japanese(), "a 59-byte list reports Japanese");
        check(japanesePokemon.getMaxNicknameLength() == 5, "Japanese nicknames cap at 5 characters");
        check(japanesePokemon.getData().size() == Pokemon::SIZE_1JLIST, "Japanese list is 59 bytes");

        // Refusals.
        Pokemon::Pokemon1RBY guard(false);
        guard.setSpecies(6);
        guard.setSpecies(152);   // Chikorita -- not a Gen 1 species
        check(guard.speciesID() == 6, "setSpecies refuses a species Gen 1 does not have");
        guard.setMove(0, 200);   // past MAX_MOVE_GEN1
        check(guard.move(0) == 0, "setMove refuses a move id Gen 1 does not have");
    }

    void testSave(const char *savePath)
    {
        std::printf("[5] Gen 1 save file: %s\n", savePath);
        FILE *saveFile = std::fopen(savePath, "rb");
        if (!saveFile)
        {
            std::printf("  SKIP  cannot open\n");
            return;
        }
        std::fseek(saveFile, 0, SEEK_END);
        const long saveFileSize = std::ftell(saveFile);
        std::fseek(saveFile, 0, SEEK_SET);
        std::vector<uint8_t> saveBytes(static_cast<size_t>(saveFileSize));
        if (std::fread(saveBytes.data(), 1, saveBytes.size(), saveFile) != saveBytes.size())
        {
            std::fclose(saveFile);
            std::printf("  FAIL short read\n");
            ++g_failures;
            return;
        }
        std::fclose(saveFile);

        bool isJapanese = false;
        const bool isGen1Save = Trainer::Trainer1RBY::detect(saveBytes, isJapanese);
        check(isGen1Save, "detected as a Gen 1 save", savePath);
        if (!isGen1Save)
        {
            std::printf("  (size %ld -- not a Gen 1 save, skipping)\n", saveFileSize);
            return;
        }

        const std::vector<uint8_t> originalBytes = saveBytes;
        Trainer::Trainer1RBY trainer(std::move(saveBytes), savePath);
        check(trainer.isValid(), "save parsed", savePath);
        if (!trainer.isValid())
        {
            return;
        }

        // --- the stored checksum must already validate; if it does not, our idea of the covered
        // --- range or the layout is wrong, and every later check is measuring the wrong bytes.
        {
            const auto &storedBytes = originalBytes;
            const size_t checksumOffset = isJapanese ? 0x3594u : 0x3523u;
            uint8_t byteSum = 0;
            for (size_t byteOffset = 0x2598; byteOffset < checksumOffset; ++byteOffset)
            {
                byteSum = static_cast<uint8_t>(byteSum + storedBytes[byteOffset]);
            }
            check(static_cast<uint8_t>(~byteSum) == storedBytes[checksumOffset], "the save's own checksum validates", std::string("stored 0x") + std::to_string(storedBytes[checksumOffset]) + " computed 0x" + std::to_string(static_cast<uint8_t>(~byteSum)));
        }

        std::printf("      locale=%s  game=%s  OT='%s'  ID=%u  money=%u  coins=%u  badges=0x%02X\n", trainer.japanese() ? "JP" : "INT", trainer.gameTitle(), trainer.trainerName.c_str(), trainer.TID16, trainer.money, trainer.coins, trainer.badgeFlags);
        std::printf("      played %02u:%02u:%02u   dex owned=%u seen=%u   rival='%s'\n", trainer.playedHours, trainer.playedMinutes, trainer.playedSeconds, trainer.dexOwned, trainer.dexSeen, trainer.rivalName.c_str());

        // A glitch species in a counted party entry (MissingNo., internal 0x1F) is kept, in place,
        // byte for byte: PKHeX's PokeList1 copies every counted entry. It used to be dropped on load
        // and the list compacted over it on any save.
        {
            const size_t partyOffset = isJapanese ? 0x2ED5u : 0x2F2Cu;
            const size_t partyCount = originalBytes[partyOffset];
            if (partyCount >= 1 && partyCount <= 6)
            {
                const size_t nameLength = isJapanese ? 6u : 11u;
                const size_t partyListLength = 1 + 6 + 1 + (44 + nameLength * 2) * 6;
                std::vector<uint8_t> glitchedBytes = originalBytes;
                glitchedBytes[partyOffset + 1] = 0x1F;
                glitchedBytes[partyOffset + 1 + 6 + 1] = 0x1F;
                Trainer::Trainer1RBY glitchedTrainer(glitchedBytes, savePath);
                check(glitchedTrainer.party.size() == partyCount - 1, "a MissingNo. party entry is not shown as a Pokemon", std::to_string(glitchedTrainer.party.size()));
                const std::vector<uint8_t> writtenBytes = glitchedTrainer.serialize();
                check(std::equal(glitchedBytes.begin() + partyOffset, glitchedBytes.begin() + partyOffset + partyListLength, writtenBytes.begin() + partyOffset), "a MissingNo. party entry is written back in place, byte for byte");
                // The party view shows the kept entry in its own slot (0) and every Pokemon after it.
                const std::vector<size_t> unmappedPartySlots = glitchedTrainer.getUnmappedPartySlots();
                check(unmappedPartySlots.size() == 1 && unmappedPartySlots[0] == 0, "getUnmappedPartySlots reports the MissingNo. in party slot 1", std::to_string(unmappedPartySlots.size()));
                // Fill the party to the room the API reports, the way a caller adding Pokemon must:
                // the kept entry takes a slot, so a full party is five Pokemon plus it, and all six
                // entries write and reload.
                const ::Pokemon::Pokemon *partyDonor = glitchedTrainer.party.empty() ? nullptr : glitchedTrainer.party[0].get();
                for (size_t boxIndex = 0; !partyDonor && boxIndex < glitchedTrainer.boxes.size(); ++boxIndex)
                    for (size_t slotIndex = 0; !partyDonor && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                        partyDonor = glitchedTrainer.boxes[boxIndex][slotIndex].get();
                if (partyDonor)
                {
                    std::unique_ptr<::Pokemon::Pokemon> donorCopy = partyDonor->clone();
                    while (glitchedTrainer.party.size() + glitchedTrainer.getUnmappedPartySlots().size() < Trainer::MAX_PARTY_SLOTS)
                        glitchedTrainer.party.push_back(donorCopy->clone());
                    bool partySlotsMapped = glitchedTrainer.partyIndexAtSlot(0) == -1;
                    for (size_t partySlotIndex = 1; partySlotIndex < Trainer::MAX_PARTY_SLOTS; ++partySlotIndex)
                        partySlotsMapped = partySlotsMapped && glitchedTrainer.partyIndexAtSlot(partySlotIndex) == static_cast<int>(partySlotIndex - 1);
                    check(partySlotsMapped, "partyIndexAtSlot skips the MissingNo.'s party slot");
                    Trainer::Trainer1RBY reloadedTrainer(glitchedTrainer.serialize(), savePath);
                    check(glitchedTrainer.party.size() == Trainer::MAX_PARTY_SLOTS - 1 && reloadedTrainer.party.size() == glitchedTrainer.party.size() && reloadedTrainer.getUnmappedPartySlots() == std::vector<size_t>{0}, "a party filled around a MissingNo. keeps every Pokemon and the MissingNo. on save", std::to_string(glitchedTrainer.party.size()) + " placed, " + std::to_string(reloadedTrainer.party.size()) + " reloaded");
                }
            }
        }

        // The same entry in a BOX: it keeps its slot (isBoxSlotUnmapped), counts toward the box's
        // capacity, and a box filled around it -- every free slot the storage view offers -- writes
        // and reloads every Pokemon and the kept entry. A box slot that read as free let a Pokemon be
        // dropped on the entry, overfilling the list so the last Pokemon was cut on save.
        {
            Trainer::Trainer1RBY probeTrainer(originalBytes, savePath);
            const size_t liveBoxOffset = probeTrainer.currentBoxFlush().liveBoxOffset;
            const size_t boxCapacity = probeTrainer.getSlotsPerBox();
            const size_t boxIndex = probeTrainer.getCurrentBox();
            const size_t storedCount = originalBytes[liveBoxOffset];
            const ::Pokemon::Pokemon *donor = !probeTrainer.party.empty() ? probeTrainer.party[0].get() : nullptr;
            for (size_t slotIndex = 0; !donor && slotIndex < boxCapacity; ++slotIndex)
                donor = probeTrainer.boxes[boxIndex][slotIndex].get();
            if (storedCount <= boxCapacity && donor)
            {
                // Plant MissingNo. (internal 0x1F) as a new last entry, or over the last one in a full box.
                const size_t glitchSlot = std::min(storedCount, boxCapacity - 1);
                std::vector<uint8_t> glitchedBytes = originalBytes;
                glitchedBytes[liveBoxOffset] = static_cast<uint8_t>(glitchSlot + 1);
                glitchedBytes[liveBoxOffset + 1 + glitchSlot] = 0x1F;
                glitchedBytes[liveBoxOffset + 1 + glitchSlot + 1] = 0xFF;
                glitchedBytes[liveBoxOffset + 1 + boxCapacity + 1 + glitchSlot * ::Pokemon::SIZE_1STORED] = 0x1F;
                Trainer::Trainer1RBY glitchedTrainer(glitchedBytes, savePath);
                check(!glitchedTrainer.boxes[boxIndex][glitchSlot] && glitchedTrainer.isBoxSlotUnmapped(boxIndex, glitchSlot), "isBoxSlotUnmapped reports the MissingNo.'s box slot");
                bool otherSlotReported = false;
                for (size_t slotIndex = 0; slotIndex < boxCapacity; ++slotIndex)
                    otherSlotReported = otherSlotReported || (slotIndex != glitchSlot && glitchedTrainer.isBoxSlotUnmapped(boxIndex, slotIndex));
                check(!otherSlotReported && !glitchedTrainer.isBoxSlotUnmapped((boxIndex + 1) % glitchedTrainer.getBoxCount(), glitchSlot), "isBoxSlotUnmapped reports a slot that holds no kept entry");
                const size_t boxListLength = 1 + boxCapacity + 1 + (::Pokemon::SIZE_1STORED + (isJapanese ? 6u : 11u) * 2) * boxCapacity;
                const std::vector<uint8_t> unchangedBytes = glitchedTrainer.serialize();
                check(std::equal(glitchedBytes.begin() + liveBoxOffset, glitchedBytes.begin() + liveBoxOffset + boxListLength, unchangedBytes.begin() + liveBoxOffset), "a boxed MissingNo. is written back in place, byte for byte");

                size_t pokemonCount = 0;
                for (size_t slotIndex = 0; slotIndex < boxCapacity; ++slotIndex)
                {
                    auto &slot = glitchedTrainer.boxes[boxIndex][slotIndex];
                    if (!slot && !glitchedTrainer.isBoxSlotUnmapped(boxIndex, slotIndex))
                        slot = donor->clone();
                    pokemonCount += slot && slot->speciesID() != 0;
                }
                // The writer's order for that box: every Pokemon and the kept entry, in slot order.
                std::vector<::Pokemon::Pokemon *> boxPokemon;
                for (size_t slotIndex = 0; slotIndex < boxCapacity; ++slotIndex)
                    boxPokemon.push_back(glitchedTrainer.boxes[boxIndex][slotIndex].get());
                const std::vector<Trainer::UnmappedListEntry> plantedEntries{Trainer::UnmappedListEntry{glitchSlot, 0x1F, std::vector<uint8_t>(::Pokemon::SIZE_1STORED, 0x1F), {}, {}}};
                const std::vector<Trainer::ListEntrySource> listEntries = Trainer::orderListEntries(boxPokemon, plantedEntries, boxCapacity, true);
                check(listEntries.size() == boxCapacity && Trainer::unmappedListPositions(listEntries) == std::vector<size_t>{glitchSlot}, "orderListEntries keeps every entry of a full box, the kept one at its slot", std::to_string(listEntries.size()));
                Trainer::Trainer1RBY reloadedTrainer(glitchedTrainer.serialize(), savePath);
                size_t reloadedCount = 0;
                for (size_t slotIndex = 0; slotIndex < boxCapacity; ++slotIndex)
                    reloadedCount += reloadedTrainer.boxes[boxIndex][slotIndex] && reloadedTrainer.boxes[boxIndex][slotIndex]->speciesID() != 0;
                check(pokemonCount == boxCapacity - 1 && reloadedCount == pokemonCount && reloadedTrainer.isBoxSlotUnmapped(boxIndex, glitchSlot), "a box filled around a MissingNo. keeps every Pokemon and the MissingNo. on save", std::to_string(pokemonCount) + " placed, " + std::to_string(reloadedCount) + " reloaded");
            }
        }

        // Gen 1's glyph table is the trainer-name check (canStoreBoxName), and a name with a
        // character it lacks is not written at all -- encoding used to stop there and save "R" for
        // an accented "Red" while the editor kept the full name.
        {
            Trainer::Trainer1RBY nameTrainer(originalBytes, savePath);
            const std::string unstorableName = isJapanese ? "\xE3\x81\x82\xC3\xAB" : "R\xC3\xABd"; // "a" + e-diaeresis / "Red" with it
            const std::string storableName = isJapanese ? "\xE3\x81\x82" : "Red";
            check(!nameTrainer.canStoreBoxName(unstorableName) && nameTrainer.canStoreBoxName(storableName), "the Gen 1 charset check accepts an unstorable name or refuses a storable one");
            nameTrainer.trainerName = unstorableName;
            const std::vector<uint8_t> writtenBytes = nameTrainer.serialize();
            const size_t nameByteCount = isJapanese ? 6u : 8u;
            check(std::equal(originalBytes.begin() + 0x2598, originalBytes.begin() + 0x2598 + nameByteCount, writtenBytes.begin() + 0x2598), "an unstorable trainer name was written (truncated) over the saved one");
        }

        // A brand-new save has no trainer name yet, and that is a real state -- the Yellow save here
        // is one. Only require a name once the file shows evidence of having been played.
        const bool wasPlayed = trainer.TID16 != 0 || trainer.playedHours || trainer.playedMinutes || !trainer.party.empty();
        if (wasPlayed)
        {
            check(!trainer.trainerName.empty(), "a played save has a trainer name");
        }
        else
        {
            std::printf("      (never-played save: no trainer name, empty boxes -- expected)\n");
        }
        check(trainer.playedHours <= 255 && trainer.playedMinutes < 60 && trainer.playedSeconds < 60, "play time in range");
        check(trainer.dexOwned <= trainer.dexSeen, "dex owned never exceeds seen", std::to_string(trainer.dexOwned) + " > " + std::to_string(trainer.dexSeen));
        check(trainer.money <= 999999, "money within the BCD ceiling", std::to_string(trainer.money));

        std::printf("      party (%zu):\n", trainer.party.size());
        for (const auto &partyPokemon : trainer.party)
        {
            check(partyPokemon->speciesID() >= 1 && partyPokemon->speciesID() <= Pokemon::MAX_SPECIES_GEN1, "party species in Gen 1 range", std::to_string(partyPokemon->speciesID()));
            std::printf("        %-11s Lv%-3u  %s  stats %u/%u/%u/%u/%u\n", partyPokemon->species(), partyPokemon->level(), toUtf8(partyPokemon->nickname()).c_str(), partyPokemon->statHPMax(), partyPokemon->statATK(), partyPokemon->statDEF(), partyPokemon->statSPE(), partyPokemon->statSPA());
            // A party record carries the stats the game LAST computed, and it recomputes them only on a
            // level-up, an evolution or a trip through the PC. Stat Experience gained since then leaves a
            // stored stat BELOW the formula -- never above it, and never below what the same level gives
            // with none at all -- so that band is what has to hold, and an exact match is the common case.
            // Blue's Charizard has fought since level 72: Defense 147 where the formula now says 148.
            //
            // RECOMPUTED ON CLONES. Calling recalculateStats() on the save's own entity rewrote that
            // Defense byte, and the write-back check below then blamed the save layer for it.
            auto pokemonWithEffort = partyPokemon->clone();
            auto pokemonWithoutEffort = partyPokemon->clone();
            for (int statIndex = 0; statIndex < 5; ++statIndex)
            {
                pokemonWithoutEffort->setEVWide(statIndex, 0);
            }
            pokemonWithEffort->recalculateStats();
            pokemonWithoutEffort->recalculateStats();
            const auto statsOf = [](const ::Pokemon::Pokemon &pokemon) { return std::vector<uint16_t>{pokemon.statHPMax(), pokemon.statATK(), pokemon.statDEF(), pokemon.statSPE(), pokemon.statSPA()}; };
            const std::vector<uint16_t> storedStats = statsOf(*partyPokemon);
            const std::vector<uint16_t> highestStats = statsOf(*pokemonWithEffort);
            const std::vector<uint16_t> lowestStats = statsOf(*pokemonWithoutEffort);
            bool statsWithinBand = true;
            bool statsExact = true;
            for (size_t statIndex = 0; statIndex < storedStats.size(); ++statIndex)
            {
                statsWithinBand = statsWithinBand && storedStats[statIndex] >= lowestStats[statIndex] && storedStats[statIndex] <= highestStats[statIndex];
                statsExact = statsExact && storedStats[statIndex] == highestStats[statIndex];
            }
            check(statsWithinBand, "party stats reproduce from the tables", std::string(partyPokemon->species()));
            if (statsWithinBand && !statsExact)
            {
                std::printf("          (stats predate Stat Experience gained since its last level-up -- legitimate)\n");
            }
        }

        size_t boxPokemonCount = 0;
        for (size_t boxIndex = 0; boxIndex < trainer.getBoxCount(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox(); ++slotIndex)
            {
                if (trainer.boxes[boxIndex][slotIndex])
                {
                    ++boxPokemonCount;
                    check(trainer.boxes[boxIndex][slotIndex]->speciesID() >= 1 && trainer.boxes[boxIndex][slotIndex]->speciesID() <= Pokemon::MAX_SPECIES_GEN1, "box species in Gen 1 range", std::to_string(trainer.boxes[boxIndex][slotIndex]->speciesID()));
                }
            }
        }
        std::printf("      boxes: %zu Pokemon across %zu boxes of %zu\n", boxPokemonCount, trainer.getBoxCount(), trainer.getSlotsPerBox());

        size_t bagItemCount = 0;
        size_t pcItemCount = 0;
        for (const auto &bagItem : trainer.items[0])
        {
            ++bagItemCount;
            check(Pokemon::getItemNameGen1(static_cast<uint8_t>(bagItem.itemId))[0] != '?' || bagItem.itemId == 0, "bag item has a Gen 1 name", "id " + std::to_string(bagItem.itemId));
            // 255, not 99: the quantity field is one byte and a real save can exceed the game's own
            // cap. This save carries Max Elixir x158. Above 99 is a legality question, not a parse one.
            check(bagItem.count >= 1 && bagItem.count <= 255, "bag quantity fits the field", std::to_string(bagItem.count));
        }
        for (const auto &pcItem : trainer.items[1])
        {
            ++pcItemCount;
        }
        std::printf("      items: %zu in the bag, %zu in the PC\n", bagItemCount, pcItemCount);
        if (bagItemCount)
        {
            std::printf("        ");
            size_t shownItemCount = 0;
            for (const auto &shownItem : trainer.items[0])
            {
                if (shownItemCount++ >= 6)
                {
                    break;
                }
                std::printf("%s x%u  ", Pokemon::getItemNameGen1(static_cast<uint8_t>(shownItem.itemId)), shownItem.count);
            }
            std::printf("\n");
        }

        // A save editor that cannot reproduce a file it did not change will corrupt one it did.
        const std::vector<uint8_t> &writtenBytes = trainer.serialize();
        check(writtenBytes.size() == originalBytes.size(), "round trip preserves the file size");
        size_t differingByteCount = 0;
        size_t firstDifferenceOffset = 0;
        for (size_t byteOffset = 0; byteOffset < writtenBytes.size() && byteOffset < originalBytes.size(); ++byteOffset)
        {
            if (writtenBytes[byteOffset] != originalBytes[byteOffset])
            {
                if (!differingByteCount)
                {
                    firstDifferenceOffset = byteOffset;
                }
                ++differingByteCount;
            }
        }
        std::printf("      unedited write-back differs in %zu / %zu bytes%s\n", differingByteCount, writtenBytes.size(), differingByteCount ? "" : "  <- byte-exact");
        if (differingByteCount)
        {
            std::printf("        first difference at 0x%04zX: %02X -> %02X\n", firstDifferenceOffset, originalBytes[firstDifferenceOffset], writtenBytes[firstDifferenceOffset]);
            size_t shownDifferenceCount = 0;
            for (size_t byteOffset = 0; byteOffset < writtenBytes.size() && shownDifferenceCount < 8; ++byteOffset)
            {
                if (writtenBytes[byteOffset] == originalBytes[byteOffset])
                {
                    continue;
                }
                const char *regionName = byteOffset >= 0x4000 ? "box bank" : (byteOffset >= 0x2598 ? "trainer/party area" : "before trainer block");
                std::printf("        0x%04zX %02X -> %02X   (%s)\n", byteOffset, originalBytes[byteOffset], writtenBytes[byteOffset], regionName);
                ++shownDifferenceCount;
            }
        }
        // THE CURRENT BOX IS FLUSHED, and that is the one change an unedited write may make -- see
        // Trainer::Gen1BoxFlush. Allowed only under two proofs, both asserted rather than assumed: every
        // byte that moved lies in the current box's BANK copy, is the boxes-initialised bit, or is the
        // checksum; and afterwards the bank copy equals the live one. Together they say the write only
        // brought a stale copy into line -- the Gen 1 counterpart of Gen 2's mirror check in oldgens.cpp.
        const Trainer::Gen1BoxFlush boxFlush = trainer.currentBoxFlush();
        // A never-flushed bank is uninitialised memory, and the write that first flushes it initialises the
        // other box lists to empty -- only their two header bytes, and only to a count of 0 and a cap.
        const bool bankWasInitialized = (originalBytes[boxFlush.initializedFlagOffset] & 0x80) != 0;
        size_t bytesOutsideFlush = 0;
        size_t flushedByteCount = 0;
        for (size_t byteOffset = 0; byteOffset < writtenBytes.size() && byteOffset < originalBytes.size(); ++byteOffset)
        {
            if (writtenBytes[byteOffset] == originalBytes[byteOffset])
            {
                continue;
            }
            const bool inBankCopy = byteOffset >= boxFlush.bankBoxOffset && byteOffset < boxFlush.bankBoxOffset + boxFlush.boxListLength;
            const bool initializedBit = byteOffset == boxFlush.initializedFlagOffset && (writtenBytes[byteOffset] ^ originalBytes[byteOffset]) == 0x80;
            bool emptyHeaderInitialised = false;
            for (const size_t boxOffset : boxFlush.everyBankBoxOffset)
            {
                emptyHeaderInitialised = emptyHeaderInitialised || (!bankWasInitialized && ((byteOffset == boxOffset && writtenBytes[byteOffset] == 0x00) || (byteOffset == boxOffset + 1 && writtenBytes[byteOffset] == 0xFF)));
            }
            if (inBankCopy || initializedBit || emptyHeaderInitialised || byteOffset == boxFlush.checksumOffset)
            {
                ++flushedByteCount;
            }
            else
            {
                ++bytesOutsideFlush;
            }
        }
        const bool bankMatchesLive = std::equal(writtenBytes.begin() + boxFlush.bankBoxOffset, writtenBytes.begin() + boxFlush.bankBoxOffset + boxFlush.boxListLength, writtenBytes.begin() + boxFlush.liveBoxOffset);
        if (differingByteCount && bytesOutsideFlush == 0 && bankMatchesLive)
        {
            std::printf("        (the current box's bank copy was stale; %zu byte(s) flushed from the live copy)\n", flushedByteCount);
        }
        check(bytesOutsideFlush == 0, "an unedited save writes back byte-for-byte identical, save for the current box's flush", bytesOutsideFlush ? (std::to_string(bytesOutsideFlush) + " bytes differ outside it") : "");
        check(bankMatchesLive, "after writing, the current box's bank copy equals its live copy");

        // A box list ends at its COUNT. The writer touches the count and one 0xFF cap and leaves every
        // marker after it alone, so a reader that went by marker alone brought back whatever sat past the
        // cap. A real Blue save wrote back cleanly and re-read with ELEVEN extra Pokemon -- a Mew and a box
        // of Pidgey and Rattata -- because its never-flushed box bank was only trusted once PKSE set the
        // initialised bit. Nothing about the bytes looked wrong.
        Trainer::Trainer1RBY rereadTrainer(std::vector<uint8_t>(writtenBytes.begin(), writtenBytes.end()), savePath);
        check(rereadTrainer.isValid(), "the save PKSE just wrote reads back as a Gen 1 save");
        const auto speciesAt = [](const ::Pokemon::Pokemon *pokemon) -> unsigned { return (pokemon && pokemon->speciesID() != 0) ? pokemon->speciesID() : 0u; };
        size_t changedSlotCount = 0;
        const size_t partySlotCount = std::max(trainer.party.size(), rereadTrainer.party.size());
        for (size_t partyIndex = 0; partyIndex < partySlotCount; ++partyIndex)
        {
            const unsigned speciesBefore = partyIndex < trainer.party.size() ? speciesAt(trainer.party[partyIndex].get()) : 0u;
            const unsigned speciesAfter = partyIndex < rereadTrainer.party.size() ? speciesAt(rereadTrainer.party[partyIndex].get()) : 0u;
            if (speciesBefore != speciesAfter)
            {
                std::printf("        party %zu: species %u -> %u\n", partyIndex + 1, speciesBefore, speciesAfter);
                ++changedSlotCount;
            }
        }
        for (size_t boxIndex = 0; boxIndex < trainer.boxes.size() && boxIndex < rereadTrainer.boxes.size(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox(); ++slotIndex)
            {
                const unsigned speciesBefore = speciesAt(trainer.boxes[boxIndex][slotIndex].get());
                const unsigned speciesAfter = speciesAt(rereadTrainer.boxes[boxIndex][slotIndex].get());
                if (speciesBefore != speciesAfter)
                {
                    if (changedSlotCount < 12)
                    {
                        std::printf("        box %zu slot %zu: species %u -> %u\n", boxIndex + 1, slotIndex + 1, speciesBefore, speciesAfter);
                    }
                    ++changedSlotCount;
                }
            }
        }
        check(changedSlotCount == 0, "every party and box slot reads back holding what it held before the write", changedSlotCount ? (std::to_string(changedSlotCount) + " slot(s) changed") : "");
    }

}

int main(int argc, char **argv)
{
    std::printf("=== Gen 1 (PK1) harness ===\n\n");
    testTables();
    testLearnsets();
    testCharset();
    testSetters();
    if (argc > 1)
    {
        testBank(argv[1]);
    }
    else
    {
        std::printf("[3] (no bank path given -- skipping real-record checks)\n");
    }
    for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
    {
        testSave(argv[argumentIndex]);
    }
    if (argc <= 2)
    {
        std::printf("[5] (no .sav path given -- skipping save-file checks)\n");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
