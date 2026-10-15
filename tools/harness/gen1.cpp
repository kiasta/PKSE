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

namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logEventToFile(const std::string&) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
    uint8_t* readAllBytes(const char*, size_t* outSize) { if (outSize) *outSize = 0; return nullptr; }
}

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s%s%s\n", what, detail.empty() ? "" : " -- ", detail.c_str());
    }
}

std::string u16ToUtf8(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

void testTables() {
    std::printf("[1] Gen 1 tables\n");
    int roundTripped = 0;
    for (uint16_t sp = 1; sp <= Pokemon::MAX_SPECIES_GEN1; ++sp) {
        const uint8_t internal = Pokemon::nationalToG1(sp);
        check(internal != 0, "species has an internal index", "national " + std::to_string(sp));
        if (internal == 0) continue;
        check(Pokemon::g1ToNational(internal) == sp, "species index round trip", "national " + std::to_string(sp) + " -> internal " + std::to_string(internal) + " -> " + std::to_string(Pokemon::g1ToNational(internal)));
        ++roundTripped;

        const Pokemon::PersonalRecord& bs = Pokemon::getPersonalInfo1RBY(sp, 0);
        check(bs.hp != 0, "has a base-stat row", "national " + std::to_string(sp));
        if (bs.hp == 0) continue;
        check(bs.hp > 0 && bs.atk > 0 && bs.spa > 0, "base stats non-zero", "national " + std::to_string(sp));
        // Types must be real PKSE ids, and must survive the inverse mapping. A type that does not
        // round trip means the ROM<->PKSE tables disagree, which draws the wrong type icon.
        check(bs.type1 != 255, "types mapped", "national " + std::to_string(sp));
        check(Pokemon::g1TypeToPKSE(Pokemon::pkseTypeToG1(bs.type1)) == bs.type1, "type1 round trip", "national " + std::to_string(sp));
        // A MONO-TYPED SPECIES REPORTS TYPE_NONE IN SLOT 2, not a repeat of slot 1. That is the
        // table's convention and it is what stops the badge row drawing "Normal Normal"; the ROM
        // itself stores the id twice, and Pokemon1RBY::setSpecies re-expands the sentinel when it
        // writes a record. TYPE_NONE is not a type, so there is nothing to round trip.
        if (bs.type2 != Pokemon::PERSONAL_TYPE_NONE)
        {
            check(Pokemon::g1TypeToPKSE(Pokemon::pkseTypeToG1(bs.type2)) == bs.type2, "type2 round trip", "national " + std::to_string(sp));
        }
        // Gender ratio must be one of the canonical values -- an arbitrary byte here would mean
        // the personal_rb byte 0 is NOT the substituted ratio we believe it is.
        const uint8_t g = bs.genderRatio;
        check(g == 0 || g == 31 || g == 63 || g == 127 || g == 191 || g == 225 || g == 254 || g == 255, "gender ratio is canonical", "national " + std::to_string(sp) + " ratio " + std::to_string(g));
    }
    std::printf("      %d/%d species round-trip through the internal index\n", roundTripped, Pokemon::MAX_SPECIES_GEN1);

    // Gen 1's unused "Bird" type must NOT map to anything.
    check(Pokemon::g1TypeToPKSE(6) == 255, "ROM type 6 (unused Bird) maps to TYPE_NONE");

    int ppKnown = 0;
    for (uint16_t m = 1; m <= Pokemon::MAX_MOVE_GEN1; ++m) {
        const uint8_t pp = Pokemon::getMovePPGen1(m);
        check(pp > 0 && pp <= 40, "move PP in range", "move " + std::to_string(m) + " pp " + std::to_string(pp));
        if (pp) ++ppKnown;
        // 3 PP Ups must never exceed the 6-bit PP field the record actually has.
        check(Pokemon::getMovePPGen1WithUps(m, 3) <= 63, "PP with 3 Ups fits 6 bits", "move " + std::to_string(m));
    }
    std::printf("      %d/%d moves have PP\n", ppKnown, Pokemon::MAX_MOVE_GEN1);
}

void testLearnsets() {
    std::printf("[1b] Gen 1 learnsets + types\n");
    const auto G1 = Enums::GameVersion::RBY;

    int withPool = 0, totalMoves = 0;
    for (uint16_t sp = 1; sp <= Pokemon::MAX_SPECIES_GEN1; ++sp) {
        int n = 0;
        for (uint16_t m = 1; m <= Pokemon::MAX_MOVE_GEN1; ++m)
            if (Pokemon::isLearnable(sp, 0, G1, m)) ++n;
        // A species with an EMPTY pool is the failure this whole table exists to prevent: every
        // move then reads as illegal and the transfer sanitizer deletes all four.
        check(n > 0, "species has a non-empty move pool", "national " + std::to_string(sp));
        if (n > 0) ++withPool;
        totalMoves += n;
        // Nothing outside Gen 1's move range may be reachable.
        for (uint16_t m = Pokemon::MAX_MOVE_GEN1 + 1; m < 300; ++m)
            check(!Pokemon::isLearnable(sp, 0, G1, m), "no post-Gen-1 move is learnable", "national " + std::to_string(sp) + " move " + std::to_string(m));
    }
    std::printf("      %d/%d species have a pool, %d learnable (species, move) pairs\n", withPool, Pokemon::MAX_SPECIES_GEN1, totalMoves);

    // The initial moves in the personal table are the ones that exist ONLY there -- lvlmove
    // starts above level 1 for most species -- so they are the specific regression to pin.
    check(Pokemon::isLearnable(1, 0, G1, 33), "Bulbasaur can learn Tackle (personal-table initial move)");
    check(Pokemon::isLearnable(1, 0, G1, 45), "Bulbasaur can learn Growl (personal-table initial move)");
    check(Pokemon::isLearnable(1, 0, G1, 73), "Bulbasaur can learn Leech Seed (level-up)");
    check(Pokemon::isLearnable(1, 0, G1, 22), "Bulbasaur can learn Vine Whip (level-up)");
    // Pre-evolution fold: Raichu never learns these itself, it keeps them from Pikachu.
    check(Pokemon::isLearnable(26, 0, G1, 84), "Raichu keeps Thunder Shock from Pikachu (pre-evo fold)");
    check(Pokemon::isLearnable(26, 0, G1, 98), "Raichu keeps Quick Attack from Pikachu (pre-evo fold)");
    // TM/HM out of the personal bitfield.
    check(Pokemon::isLearnable(1, 0, G1, 92), "Bulbasaur can learn Toxic (TM06)");
    check(Pokemon::isLearnable(151, 0, G1, 5),  "Mew learns Mega Punch");
    // Mew learns every TM/HM in Gen 1 -- its personal TM bitfield is all 0xFF.
    check(Pokemon::isLearnable(151, 0, G1, 15), "Mew learns Cut (HM01)");
    // Surf-Pikachu (Stadium) is in the pool for the Pikachu line and nothing else.
    check(Pokemon::isLearnable(25, 0, G1, 57), "Pikachu can know Surf (Stadium)");
    check(!Pokemon::isLearnable(19, 0, G1, 57), "Rattata cannot know Surf");
    // A move that plainly should not be reachable.
    check(!Pokemon::isLearnable(1, 0, G1, 143), "Bulbasaur cannot learn Sky Attack");

    // Types come from Gen 1's table, not the modern one.
    {
        using namespace Pokemon;
        TypePair clef = getPokemonTypes(35, 0, G1);      // Clefairy: Normal in Gen 1, Fairy from Gen 6
        check(clef.type1 == TYPE_NORMAL, "Clefairy is Normal in Gen 1", "got " + std::to_string(clef.type1));
        TypePair magn = getPokemonTypes(81, 0, G1);      // Magnemite: Electric, not Electric/Steel
        check(magn.type1 == TYPE_ELECTRIC && !hasSecondType(magn), "Magnemite is pure Electric in Gen 1");
        TypePair ratt = getPokemonTypes(19, 0, G1);      // stored duplicated -> must collapse
        check(ratt.type1 == TYPE_NORMAL && !hasSecondType(ratt), "a Gen 1 mono-type collapses to one badge");
        TypePair gyar = getPokemonTypes(130, 0, G1);
        check(gyar.type1 == TYPE_WATER && gyar.type2 == TYPE_FLYING, "Gyarados is Water/Flying");
    }
}

void testCharset() {
    std::printf("[2] Gen 1 character sets\n");
    for (int jp = 0; jp <= 1; ++jp) {
        int glyphs = 0;
        for (int b = 0; b < 256; ++b) {
            const char16_t c = Utils::gen1ToChar(static_cast<uint8_t>(b), jp != 0);
            if (c == 0) continue;
            ++glyphs;
            const uint8_t back = Utils::charToGen1(c, jp != 0);
            check(back == b, jp ? "JP byte round trip" : "EN byte round trip", "byte 0x" + std::to_string(b) + " -> back 0x" + std::to_string(back));
        }
        std::printf("      %s: %d glyphs, all round-trip\n", jp ? "JP" : "EN", glyphs);
    }
    check(Utils::gen1ToChar(0x50, false) == 0, "0x50 is a terminator in EN");
    check(Utils::gen1ToChar(0x50, true)  == 0, "0x50 is a terminator in JP");
    check(Utils::gen1ToChar(0x80, false) == u'A', "EN 0x80 is 'A'");
    check(Utils::gen1ToChar(0xE8, false) == u'․', "EN 0xE8 is a one-dot leader (MR.MIME)");
}

uint32_t rd32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void testBank(const char* path) {
    std::printf("[3] Real Gen 1 records from %s\n", path);
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  SKIP  cannot open (not an error if you have no bank here)\n"); return; }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (std::fread(buffer.data(), 1, buffer.size(), f) != buffer.size()) {
        std::printf("  FAIL  short read\n"); ++g_failures; std::fclose(f); return;
    }
    std::fclose(f);

    if (buffer.size() < 16 || std::memcmp(buffer.data(), "PKSMBANK", 8) != 0) {
        std::printf("  SKIP  not a v3 PKSM bank\n"); return;
    }
    constexpr size_t HDR = 16, ENT = 0x150;
    const size_t entries = (buffer.size() - HDR) / ENT;

    int seen = 0, statsOk = 0, roundTripOk = 0, nameOk = 0, shiny = 0, nicknamed = 0, jp = 0;
    int legalityClean = 0, legalityWarnings = 0, legalityShown = 0, warnShown = 0, glitchMewRefusals = 0;
    std::string firstFew;
    for (size_t i = 0; i < entries; ++i) {
        const uint8_t* e = buffer.data() + HDR + i * ENT;
        if (rd32le(e) != 7) continue;   // PKSM generation tag 7 == Gen 1 (NOT 1 -- see PKSMBank.h)
        ++seen;
        const uint8_t* payload = e + 4;

        // PKSM writes the whole single-entry list. Its length is not recorded, so recover it the
        // way the format intends: an international list is 69 bytes, a Japanese one 59, and the
        // padding past the end is 0xFF.
        size_t listLen = Pokemon::SIZE_1ULIST;
        if (payload[Pokemon::SIZE_1JLIST] == 0xFF && payload[Pokemon::SIZE_1JLIST - 1] != 0xFF) {
            bool allFF = true;
            for (size_t k = Pokemon::SIZE_1JLIST; k < Pokemon::SIZE_1ULIST; ++k)
                if (payload[k] != 0xFF) { allFF = false; break; }
            if (allFF) listLen = Pokemon::SIZE_1JLIST;
        }
        if (listLen == Pokemon::SIZE_1JLIST) ++jp;

        std::span<const std::byte> raw(reinterpret_cast<const std::byte*>(payload), listLen);
        Pokemon::Pokemon1RBY pk(raw);

        const std::string tag = "slot " + std::to_string(i) + " (" + std::string(pk.species()) + ")";
        check(pk.isValid(), "record is a valid Gen 1 entity", tag);
        if (!pk.isValid()) continue;

        const uint16_t sHP = pk.statHPMax(), sAT = pk.statATK(), sDF = pk.statDEF();
        const uint16_t sSP = pk.statSPE(),   sSC = pk.statSPA();
        pk.recalculateStats();
        const bool same = pk.statHPMax() == sHP && pk.statATK() == sAT && pk.statDEF() == sDF && pk.statSPE() == sSP && pk.statSPA() == sSC;
        check(same, "recalculateStats reproduces the stored stats", same ? "" : tag + " stored " + std::to_string(sHP) + "/" + std::to_string(sAT) + "/" + std::to_string(sDF) + "/" + std::to_string(sSP) + "/" + std::to_string(sSC) + " got " + std::to_string(pk.statHPMax()) + "/" + std::to_string(pk.statATK()) + "/" + std::to_string(pk.statDEF()) + "/" + std::to_string(pk.statSPE()) + "/" + std::to_string(pk.statSPA()));
        if (same) ++statsOk;

        auto c = pk.clone();
        check(c != nullptr, "clone", tag);
        if (c) {
            const bool identical = c->getData().size() == listLen && std::memcmp(c->getData().data(), payload, listLen) == 0;
            check(identical, "clone is byte-identical to the source", tag);
            if (identical) ++roundTripOk;
        }

        const std::u16string nick = pk.nickname();
        const std::u16string ot   = pk.otName();
        check(!nick.empty(), "nickname decodes to something", tag);
        {
            Pokemon::Pokemon1RBY probe(raw);
            probe.setNickname(nick);
            probe.setOTName(ot);
            const bool nameBytesMatch = std::memcmp(probe.getData().data() + probe.ofsNick(), payload + probe.ofsNick(), probe.nameLen()) == 0;
            // The OT field can hold the in-game-trade marker, which is not a name and does not
            // re-encode; exclude only that case, and count it separately below.
            const bool otBytesMatch = pk.isInGameTradeOT() || std::memcmp(probe.getData().data() + probe.ofsOT(), payload + probe.ofsOT(), probe.nameLen()) == 0;
            check(nameBytesMatch, "nickname re-encodes to the original bytes", tag + " '" + u16ToUtf8(nick) + "'");
            check(otBytesMatch, "OT name re-encodes to the original bytes", tag + " '" + u16ToUtf8(ot) + "'");
            if (nameBytesMatch && otBytesMatch) ++nameOk;
        }

        // Every move a REAL Gen 1 Pokemon is carrying must be in its pool. This is the check
        // that would have caught an empty or wrong learnset table: these mons were caught and
        // trained on hardware, so anything reported unlearnable here is the table being wrong,
        // not the Pokemon.
        for (int m = 0; m < 4; ++m) {
            const uint16_t mv = pk.move(m);
            if (mv == 0) continue;
            check(Pokemon::isLearnable(pk.speciesID(), 0, Enums::GameVersion::RBY, mv), "a real Pokemon's move is in its learn pool", tag + " move " + std::to_string(mv));
            // PP must be within what the move can hold with its recorded PP Ups.
            const uint8_t maxPP = Pokemon::getMovePPGen1WithUps(mv, pk.movePPUps(m));
            check(pk.movePP(m) <= maxPP, "move PP within its Gen 1 maximum", tag + " move " + std::to_string(mv) + " pp " + std::to_string(pk.movePP(m)) + " max " + std::to_string(maxPP));
        }

        // Every one of these Pokemon was caught and trained on real hardware, so any "Invalid"
        // here is the checker misapplying a rule from a later generation -- the exact failure
        // this pass exists to catch (a saturated Stat Exp read as a 255 EV, ability 0 read as an
        // illegal ability). Warnings are tolerated and counted; Invalid is a hard failure.
        {
            const Legality::Report rep = Legality::analyze(pk, Enums::GameVersion::RBY);
            int invalid = 0;
            for (const auto& is : rep.issues) {
                if (is.severity == Legality::Severity::Invalid) {
                    ++invalid;
                    if (legalityShown < 6) { ++legalityShown;
                        std::printf("      INVALID %s: %s\n", tag.c_str(), is.text.c_str()); }
                } else if (is.severity == Legality::Severity::Warning) {
                    ++legalityWarnings;
                    if (warnShown < 4) { ++warnShown;
                        std::printf("      warn    %s: %s\n", tag.c_str(), is.text.c_str()); }
                }
            }
            // THE ONE REFUSAL THAT IS RIGHT. Gen 1's Mew exists only through events, and every Gen 1
            // event fixes a trainer -- the Virtual Console Mew is OT "GF" with trainer id 22796
            // (PKHeX EncounterGift1). A Mew carrying any other trainer came from the Mew glitch,
            // which PKHeX calls illegal too, so its finding is the checker telling the truth rather
            // than misapplying a rule. Counted, so a bank full of them cannot hide a real failure.
            constexpr uint16_t mewSpeciesId = 151;
            constexpr uint16_t virtualConsoleMewTrainerId = 22796;
            const bool glitchMew = pk.speciesID() == mewSpeciesId && pk.tid16() != virtualConsoleMewTrainerId;
            if (glitchMew && invalid > 0) {
                ++glitchMewRefusals;
                std::printf("      (expected: %s is a glitch Mew, which PKHeX also refuses)\n", tag.c_str());
            } else {
                check(invalid == 0, "no bogus Invalid legality finding on a real Pokemon", tag);
            }
            if (invalid == 0) ++legalityClean;
        }

        if (pk.isShiny(0, "")) ++shiny;
        if (pk.isNicknamed()) ++nicknamed;

        const uint8_t lvExp = Pokemon::getLevelFromExp(pk.exp(), pk.growthRate());
        check(pk.level() == lvExp, "stored level agrees with EXP", tag + " stored " + std::to_string(pk.level()) + " vs EXP-derived " + std::to_string(lvExp));

        if (seen <= 3) {
            firstFew += "        " + std::string(pk.species()) + " '" + u16ToUtf8(nick) + "'" + " OT '" + u16ToUtf8(ot) + "'" + " Lv" + std::to_string(pk.level()) + " DV " + std::to_string(pk.ivHP()) + "/" + std::to_string(pk.ivATK()) + "/" + std::to_string(pk.ivDEF()) + "/" + std::to_string(pk.ivSPE()) + "/" + std::to_string(pk.ivSPA()) + " stats " + std::to_string(sHP) + "/" + std::to_string(sAT) + "/" + std::to_string(sDF) + "/" + std::to_string(sSP) + "/" + std::to_string(sSC) + "\n";
        }
    }

    std::printf("      Gen 1 records          : %d  (%d Japanese)\n", seen, jp);
    std::printf("      battle stats reproduced: %d/%d\n", statsOk, seen);
    std::printf("      byte-exact round trip  : %d/%d\n", roundTripOk, seen);
    std::printf("      names re-encode exactly: %d/%d\n", nameOk, seen);
    std::printf("      shiny DV pattern       : %d   nicknamed: %d\n", shiny, nicknamed);
    std::printf("      legality: %d/%d with no Invalid finding (%d warnings total, %d glitch Mew refused as PKHeX refuses it)\n", legalityClean, seen, legalityWarnings, glitchMewRefusals);
    if (!firstFew.empty()) std::printf("%s", firstFew.c_str());
}

void testSetters() {
    std::printf("[4] Setter invariants\n");
    Pokemon::Pokemon1RBY pk(false);
    pk.setSpecies(6);      // Charizard
    pk.setLevel(50);

    check(pk.speciesID() == 6, "setSpecies survives a read-back");
    check(pk.speciesInternal() == Pokemon::nationalToG1(6), "internal index written");
    check(pk.rd8(1) == pk.speciesInternal(), "list header marker matches the body");
    check(pk.rd8(0) == 1, "list count is 1 once occupied");
    check(pk.level() == 50, "setLevel survives a read-back");
    check(pk.exp() == Pokemon::getExpForLevel(50, pk.growthRate()), "setLevel writes matching EXP");
    // Charizard is Fire/Flying. The STORED type bytes must be ROM ids, not PKSE ids.
    check(pk.storedType1() == 9 && pk.storedType2() == 2, "stored types map back to Fire/Flying", "got " + std::to_string(pk.storedType1()) + "/" + std::to_string(pk.storedType2()));

    // Special aliasing: writing SpA must be visible through SpD, and vice versa.
    pk.setIV(4, 12);
    check(pk.ivSPA() == 12 && pk.ivSPD() == 12, "SpA and SpD are the one Special DV");
    pk.setIV(5, 7);
    check(pk.ivSPA() == 7, "writing SpD writes the same Special DV");
    pk.setIV(0, 15);
    check(pk.ivHP() != 15 || (pk.ivATK() & 1), "HP DV is derived, not settable");
    pk.setIV(1, 99);
    check(pk.ivATK() == 15, "IV writes clamp to the 4-bit maximum");

    pk.setEVWide(0, 40000);
    check(pk.evWide(0) == 40000, "Stat Experience keeps its full 16-bit width");
    check(pk.evHP() == 255, "the 8-bit view saturates rather than truncating");
    pk.setEVWide(4, 1234);
    check(pk.evWide(5) == 1234, "SpA and SpD are the one Special Stat Exp counter");

    // A move change must bring its own PP.
    pk.setMove(0, 63);   // Fire Spin (Gen 1 PP 15)
    check(pk.move(0) == 63, "setMove survives a read-back");
    check(pk.movePP(0) == Pokemon::getMovePPGen1(63), "setMove resets PP to the move's own value");
    pk.setMovePPUps(0, 3);
    check(pk.movePPUps(0) == 3, "PP Ups survive a read-back");
    check(pk.move(0) == 63, "writing PP Ups does not disturb the move id");
    pk.setMovePP(0, 63);
    check(pk.movePPUps(0) == 3, "writing PP does not disturb PP Ups");

    // Shininess is a DV pattern, and clearing it must not disturb a non-shiny Pokemon.
    pk.setShiny(true, 0);
    check(pk.isShiny(0, ""), "setShiny(true) produces the shiny DV pattern");
    check(pk.ivDEF() == 10 && pk.ivSPE() == 10 && pk.ivSPA() == 10, "shiny DVs are DEF/SPE/SPC = 10");
    check((pk.ivATK() & 2) != 0, "shiny ATK DV has bit 1 set");
    pk.setShiny(false, 0);
    check(!pk.isShiny(0, ""), "setShiny(false) clears the pattern");
    const uint16_t dvBefore = pk.dv16();
    pk.setShiny(false, 0);
    check(pk.dv16() == dvBefore, "clearing an already non-shiny Pokemon changes nothing");

    // Names.
    pk.setNickname(u"CHARIZARD");
    check(pk.nickname() == u"CHARIZARD", "nickname round trip");
    check(!pk.isNicknamed(), "a name equal to the species is not a nickname");
    pk.setNickname(u"BLAZE");
    check(pk.isNicknamed(), "a different name is a nickname");
    pk.setNickname(u"ABCDEFGHIJKLMNOP");
    check(pk.nickname().size() == 10, "an over-long nickname is capped at 10 characters", "got " + std::to_string(pk.nickname().size()));
    check(!pk.canStoreNickname(u"ABCDEFGHIJK"), "an 11-character nickname is rejected");
    check(pk.canStoreNickname(u"ABCDEFGHIJ"), "a 10-character nickname is accepted");

    // A Japanese list is narrower in every respect.
    Pokemon::Pokemon1RBY jpk(true);
    check(jpk.japanese(), "a 59-byte list reports Japanese");
    check(jpk.getMaxNicknameLength() == 5, "Japanese nicknames cap at 5 characters");
    check(jpk.getData().size() == Pokemon::SIZE_1JLIST, "Japanese list is 59 bytes");

    // Refusals.
    Pokemon::Pokemon1RBY guard(false);
    guard.setSpecies(6);
    guard.setSpecies(152);   // Chikorita -- not a Gen 1 species
    check(guard.speciesID() == 6, "setSpecies refuses a species Gen 1 does not have");
    guard.setMove(0, 200);   // past MAX_MOVE_GEN1
    check(guard.move(0) == 0, "setMove refuses a move id Gen 1 does not have");
}

void testSave(const char* path) {
    std::printf("[5] Gen 1 save file: %s\n", path);
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  SKIP  cannot open\n"); return; }
    std::fseek(f, 0, SEEK_END); const long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> raw(static_cast<size_t>(sz));
    if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
        std::fclose(f);
        std::printf("  FAIL short read\n");
        ++g_failures;
        return;
    }
    std::fclose(f);

    bool jp = false;
    const bool isG1 = Trainer::Trainer1RBY::detect(raw, jp);
    check(isG1, "detected as a Gen 1 save", path);
    if (!isG1) { std::printf("  (size %ld -- not a Gen 1 save, skipping)\n", sz); return; }

    const std::vector<uint8_t> original = raw;
    Trainer::Trainer1RBY sav(std::move(raw), path);
    check(sav.isValid(), "save parsed", path);
    if (!sav.isValid()) return;

    // --- the stored checksum must already validate; if it does not, our idea of the covered
    // --- range or the layout is wrong, and every later check is measuring the wrong bytes.
    {
        const auto& d = original;
        const size_t ofs = jp ? 0x3594u : 0x3523u;
        uint8_t sum = 0;
        for (size_t i = 0x2598; i < ofs; ++i) sum = static_cast<uint8_t>(sum + d[i]);
        check(static_cast<uint8_t>(~sum) == d[ofs], "the save's own checksum validates", std::string("stored 0x") + std::to_string(d[ofs]) + " computed 0x" + std::to_string(static_cast<uint8_t>(~sum)));
    }

    std::printf("      locale=%s  game=%s  OT='%s'  ID=%u  money=%u  coins=%u  badges=0x%02X\n", sav.japanese() ? "JP" : "INT", sav.gameTitle(), sav.trainerName.c_str(), sav.TID16, sav.money, sav.coins, sav.badgeFlags);
    std::printf("      played %02u:%02u:%02u   dex owned=%u seen=%u   rival='%s'\n", sav.playedHours, sav.playedMinutes, sav.playedSeconds, sav.dexOwned, sav.dexSeen, sav.rivalName.c_str());

    // A brand-new save has no trainer name yet, and that is a real state -- the Yellow save here
    // is one. Only require a name once the file shows evidence of having been played.
    const bool played = sav.TID16 != 0 || sav.playedHours || sav.playedMinutes || !sav.party.empty();
    if (played) check(!sav.trainerName.empty(), "a played save has a trainer name");
    else std::printf("      (never-played save: no trainer name, empty boxes -- expected)\n");
    check(sav.playedHours <= 255 && sav.playedMinutes < 60 && sav.playedSeconds < 60, "play time in range");
    check(sav.dexOwned <= sav.dexSeen, "dex owned never exceeds seen", std::to_string(sav.dexOwned) + " > " + std::to_string(sav.dexSeen));
    check(sav.money <= 999999, "money within the BCD ceiling", std::to_string(sav.money));

    std::printf("      party (%zu):\n", sav.party.size());
    for (const auto& pk : sav.party) {
        check(pk->speciesID() >= 1 && pk->speciesID() <= Pokemon::MAX_SPECIES_GEN1, "party species in Gen 1 range", std::to_string(pk->speciesID()));
        std::printf("        %-11s Lv%-3u  %s  stats %u/%u/%u/%u/%u\n", pk->species(), pk->level(), u16ToUtf8(pk->nickname()).c_str(), pk->statHPMax(), pk->statATK(), pk->statDEF(), pk->statSPE(), pk->statSPA());
        // A party record carries the stats the game LAST computed, and it recomputes them only on a
        // level-up, an evolution or a trip through the PC. Stat Experience gained since then leaves a
        // stored stat BELOW the formula -- never above it, and never below what the same level gives
        // with none at all -- so that band is what has to hold, and an exact match is the common case.
        // Blue's Charizard has fought since level 72: Defense 147 where the formula now says 148.
        //
        // RECOMPUTED ON CLONES. Calling recalculateStats() on the save's own entity rewrote that
        // Defense byte, and the write-back check below then blamed the save layer for it.
        auto withEffort = pk->clone();
        auto withoutEffort = pk->clone();
        for (int statIndex = 0; statIndex < 5; ++statIndex)
            withoutEffort->setEVWide(statIndex, 0);
        withEffort->recalculateStats();
        withoutEffort->recalculateStats();
        const auto statsOf = [](const ::Pokemon::Pokemon& pokemon) {
            return std::vector<uint16_t>{pokemon.statHPMax(), pokemon.statATK(), pokemon.statDEF(),
                                         pokemon.statSPE(), pokemon.statSPA()};
        };
        const std::vector<uint16_t> stored = statsOf(*pk);
        const std::vector<uint16_t> highest = statsOf(*withEffort);
        const std::vector<uint16_t> lowest = statsOf(*withoutEffort);
        bool withinBand = true, exact = true;
        for (size_t statIndex = 0; statIndex < stored.size(); ++statIndex) {
            withinBand = withinBand && stored[statIndex] >= lowest[statIndex] && stored[statIndex] <= highest[statIndex];
            exact = exact && stored[statIndex] == highest[statIndex];
        }
        check(withinBand, "party stats reproduce from the tables", std::string(pk->species()));
        if (withinBand && !exact)
            std::printf("          (stats predate Stat Experience gained since its last level-up -- legitimate)\n");
    }

    size_t boxMons = 0;
    for (size_t b = 0; b < sav.getBoxCount(); ++b)
        for (size_t s = 0; s < sav.getSlotsPerBox(); ++s)
            if (sav.boxes[b][s]) {
                ++boxMons;
                check(sav.boxes[b][s]->speciesID() >= 1 && sav.boxes[b][s]->speciesID() <= Pokemon::MAX_SPECIES_GEN1, "box species in Gen 1 range", std::to_string(sav.boxes[b][s]->speciesID()));
            }
    std::printf("      boxes: %zu Pokemon across %zu boxes of %zu\n", boxMons, sav.getBoxCount(), sav.getSlotsPerBox());

    size_t bagItems = 0, pcItems = 0;
    for (const auto& it : sav.items[0]) { ++bagItems;
        check(Pokemon::getItemNameGen1(static_cast<uint8_t>(it.itemId))[0] != '?' || it.itemId == 0, "bag item has a Gen 1 name", "id " + std::to_string(it.itemId));
        // 255, not 99: the quantity field is one byte and a real save can exceed the game's own
        // cap. This save carries Max Elixir x158. Above 99 is a legality question, not a parse one.
        check(it.count >= 1 && it.count <= 255, "bag quantity fits the field", std::to_string(it.count)); }
    for (const auto& it : sav.items[1]) ++pcItems;
    std::printf("      items: %zu in the bag, %zu in the PC\n", bagItems, pcItems);
    if (bagItems) {
        std::printf("        ");
        size_t n = 0;
        for (const auto& it : sav.items[0]) {
            if (n++ >= 6) break;
            std::printf("%s x%u  ", Pokemon::getItemNameGen1(static_cast<uint8_t>(it.itemId)), it.count);
        }
        std::printf("\n");
    }

    // A save editor that cannot reproduce a file it did not change will corrupt one it did.
    const std::vector<uint8_t>& out = sav.serialize();
    check(out.size() == original.size(), "round trip preserves the file size");
    size_t diffs = 0; size_t firstDiff = 0;
    for (size_t i = 0; i < out.size() && i < original.size(); ++i)
        if (out[i] != original[i]) { if (!diffs) firstDiff = i; ++diffs; }
    std::printf("      unedited write-back differs in %zu / %zu bytes%s\n", diffs, out.size(), diffs ? "" : "  <- byte-exact");
    if (diffs) {
        std::printf("        first difference at 0x%04zX: %02X -> %02X\n", firstDiff, original[firstDiff], out[firstDiff]);
        size_t shown = 0;
        for (size_t i = 0; i < out.size() && shown < 8; ++i) {
            if (out[i] == original[i]) continue;
            const char* where = i >= 0x4000 ? "box bank" : (i >= 0x2598 ? "trainer/party area" : "before trainer block");
            std::printf("        0x%04zX %02X -> %02X   (%s)\n", i, original[i], out[i], where);
            ++shown;
        }
    }
    // THE CURRENT BOX IS FLUSHED, and that is the one change an unedited write may make -- see
    // Trainer::Gen1BoxFlush. Allowed only under two proofs, both asserted rather than assumed: every
    // byte that moved lies in the current box's BANK copy, is the boxes-initialised bit, or is the
    // checksum; and afterwards the bank copy equals the live one. Together they say the write only
    // brought a stale copy into line -- the Gen 1 counterpart of Gen 2's mirror check in oldgens.cpp.
    const Trainer::Gen1BoxFlush flush = sav.currentBoxFlush();
    // A never-flushed bank is uninitialised memory, and the write that first flushes it initialises the
    // other box lists to empty -- only their two header bytes, and only to a count of 0 and a cap.
    const bool bankWasInitialized = (original[flush.initializedFlagOffset] & 0x80) != 0;
    size_t outsideFlush = 0, flushedBytes = 0;
    for (size_t i = 0; i < out.size() && i < original.size(); ++i) {
        if (out[i] == original[i]) continue;
        const bool inBankCopy = i >= flush.bankBoxOffset && i < flush.bankBoxOffset + flush.boxListLength;
        const bool initializedBit = i == flush.initializedFlagOffset && (out[i] ^ original[i]) == 0x80;
        bool emptyHeaderInitialised = false;
        for (const size_t boxOffset : flush.everyBankBoxOffset)
            emptyHeaderInitialised = emptyHeaderInitialised || (!bankWasInitialized && ((i == boxOffset && out[i] == 0x00) || (i == boxOffset + 1 && out[i] == 0xFF)));
        if (inBankCopy || initializedBit || emptyHeaderInitialised || i == flush.checksumOffset) ++flushedBytes;
        else ++outsideFlush;
    }
    const bool bankMatchesLive = std::equal(out.begin() + flush.bankBoxOffset, out.begin() + flush.bankBoxOffset + flush.boxListLength, out.begin() + flush.liveBoxOffset);
    if (diffs && outsideFlush == 0 && bankMatchesLive)
        std::printf("        (the current box's bank copy was stale; %zu byte(s) flushed from the live copy)\n", flushedBytes);
    check(outsideFlush == 0, "an unedited save writes back byte-for-byte identical, save for the current box's flush", outsideFlush ? (std::to_string(outsideFlush) + " bytes differ outside it") : "");
    check(bankMatchesLive, "after writing, the current box's bank copy equals its live copy");

    // A box list ends at its COUNT. The writer touches the count and one 0xFF cap and leaves every
    // marker after it alone, so a reader that went by marker alone brought back whatever sat past the
    // cap. A real Blue save wrote back cleanly and re-read with ELEVEN extra Pokemon -- a Mew and a box
    // of Pidgey and Rattata -- because its never-flushed box bank was only trusted once PKSE set the
    // initialised bit. Nothing about the bytes looked wrong.
    Trainer::Trainer1RBY again(std::vector<uint8_t>(out.begin(), out.end()), path);
    check(again.isValid(), "the save PKSE just wrote reads back as a Gen 1 save");
    const auto speciesAt = [](const ::Pokemon::Pokemon* pokemon) -> unsigned {
        return (pokemon && pokemon->speciesID() != 0) ? pokemon->speciesID() : 0u;
    };
    size_t slotChanges = 0;
    const size_t partyCount = std::max(sav.party.size(), again.party.size());
    for (size_t index = 0; index < partyCount; ++index) {
        const unsigned before = index < sav.party.size() ? speciesAt(sav.party[index].get()) : 0u;
        const unsigned after = index < again.party.size() ? speciesAt(again.party[index].get()) : 0u;
        if (before != after) {
            std::printf("        party %zu: species %u -> %u\n", index + 1, before, after);
            ++slotChanges;
        }
    }
    for (size_t box = 0; box < sav.boxes.size() && box < again.boxes.size(); ++box)
        for (size_t slot = 0; slot < sav.getSlotsPerBox(); ++slot) {
            const unsigned before = speciesAt(sav.boxes[box][slot].get());
            const unsigned after = speciesAt(again.boxes[box][slot].get());
            if (before != after) {
                if (slotChanges < 12)
                    std::printf("        box %zu slot %zu: species %u -> %u\n", box + 1, slot + 1, before, after);
                ++slotChanges;
            }
        }
    check(slotChanges == 0, "every party and box slot reads back holding what it held before the write", slotChanges ? (std::to_string(slotChanges) + " slot(s) changed") : "");
}

}

int main(int argc, char** argv) {
    std::printf("=== Gen 1 (PK1) harness ===\n\n");
    testTables();
    testLearnsets();
    testCharset();
    testSetters();
    if (argc > 1) testBank(argv[1]);
    else std::printf("[3] (no bank path given -- skipping real-record checks)\n");
    for (int i = 2; i < argc; ++i) testSave(argv[i]);
    if (argc <= 2) std::printf("[5] (no .sav path given -- skipping save-file checks)\n");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
