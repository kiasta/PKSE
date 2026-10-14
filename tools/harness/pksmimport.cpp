/**
 * The importer is the one piece of PKSE that parses somebody else's file format, decrypts nothing
 * (PKSM stores plaintext) and then has to put the encryption BACK on the way in. Getting the stride,
 * the tag mapping or the Gen 3 substructure shuffle wrong produces Pokemon that look almost right,
 * which is exactly the failure a "run it on hardware and look at it" check does not catch. So it is
 * verified here instead, against a real PKSM bank, before it ever reaches the console.
 *
 *   ./tools/harness/build-pksmimport.sh [path/to/pksm_1.bnk]
 *
 * Checks, in order:
 *   1. Container parse: magic / version / box count / size formula.
 *   2. Slot histogram per generation tag, against the file's own contents.
 *   3. Every importable record decodes to a Pokemon whose STORED checksum validates -- which is
 *      only possible if the stride, the payload length and the re-encryption are all right.
 *   4. Byte-exact re-encrypt -> re-decrypt round trip per pokemon (the bank's byte-in == byte-out
 *      contract, enforced here rather than discovered on an SD card).
 *   5. The synthetic Let's Go / Gen 8 paths, which no real Gen 1-7 bank exercises.
 *   6. Box-name sidecar parse (trailing NUL, escapes).
 *   7. commit() placement: whole source boxes land intact, slot positions and names preserved.
 *
 * Exits non-zero on any failure so it can gate a release.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <vector>

#include "Trainer/Bank.h"
#include "Trainer/PKSMBank.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Experience.h"
#include "Encryption/Encryption3FRLG.h"
#include "Encryption/Encryption7LGPE.h"
#include "Encryption/Encryption8SWSH.h"
#include "Utils/HelperUtilities.h"

// Logging + the one filesystem entry point Bank.cpp needs. Stubbed rather than linked so the
// harness stays identical to the other ones and never touches an SD path that doesn't exist here.
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

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

template <typename T>
void checkEq(T got, T want, const std::string& what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s (got %lld, want %lld)\n", ok ? " OK " : "FAIL", what.c_str(),
                static_cast<long long>(got), static_cast<long long>(want));
    if (!ok) ++g_failures;
}

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

std::string u16ToUtf8(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) {
        if (c == 0) break;
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

// Counts the file's slots with a second, deliberately dumb implementation: if this and the parser
// under test agree, both read the same stride. Written from the format spec, not shared with it.
struct RefCounts { size_t empty = 0, perTag[16] = {}, bad = 0, total = 0; };

RefCounts referenceHistogram(const std::vector<uint8_t>& f) {
    RefCounts c;
    if (f.size() < 16 || std::memcmp(f.data(), "PKSMBANK", 8) != 0) return c;
    const uint32_t boxes = Utils::readUInt32LittleEndian(f.data() + 12);
    for (size_t i = 0; i < static_cast<size_t>(boxes) * 30; ++i) {
        const size_t off = 16 + i * 0x150;
        if (off + 0x150 > f.size()) break;
        ++c.total;
        const uint32_t tag = Utils::readUInt32LittleEndian(f.data() + off);
        if (tag == 0xFFFFFFFFu) { ++c.empty; continue; }
        if (tag < 16) ++c.perTag[tag]; else ++c.bad;
    }
    return c;
}

// A decrypted PK8 box record built from nothing, so the Gen 8 branch is exercised even though the
// only real bank on hand holds none. Checksum is the same u16 sum the entity computes.
std::vector<uint8_t> makeSyntheticPK8(uint16_t species, uint8_t level) {
    std::vector<uint8_t> rec(328, 0);
    Utils::writeUInt32LittleEndian(rec.data() + 0x00, 0xDEADBEEF);                 // encryption constant
    Utils::writeUInt16LittleEndian(rec.data() + 0x08, species);                    // species
    Utils::writeUInt32LittleEndian(rec.data() + 0x10, Pokemon::getExpForLevel(level, Pokemon::getGrowthRate(species)));
    Utils::writeUInt32LittleEndian(rec.data() + 0x1C, 0x7FFFFFFFu);                // IV32: all 31s
    Utils::writeUInt32LittleEndian(rec.data() + 0x28, 0x11223344);                 // PID
    uint16_t sum = 0;
    for (size_t i = 0x08; i < 328; i += 2) sum += Utils::readUInt16LittleEndian(rec.data() + i);
    Utils::writeUInt16LittleEndian(rec.data() + 0x06, sum);
    return rec;
}

// The same idea for Let's Go: a decrypted PB7 box record, checksummed over 0x08..0xE8.
std::vector<uint8_t> makeSyntheticPB7(uint16_t species, uint8_t level) {
    std::vector<uint8_t> rec(232, 0);
    Utils::writeUInt32LittleEndian(rec.data() + 0x00, 0xCAFEBABE);
    Utils::writeUInt16LittleEndian(rec.data() + 0x08, species);
    Utils::writeUInt32LittleEndian(rec.data() + 0x10,
        Pokemon::getExpForLevel(level, Pokemon::getGrowthRate(species)));
    Utils::writeUInt32LittleEndian(rec.data() + 0x1C, 0x7FFFFFFFu);
    Utils::writeUInt32LittleEndian(rec.data() + 0x18, 0x11223344);
    uint16_t sum = 0;
    for (size_t i = 0x08; i < 232; i += 2) sum += Utils::readUInt16LittleEndian(rec.data() + i);
    Utils::writeUInt16LittleEndian(rec.data() + 0x06, sum);
    return rec;
}

/// Wrap a set of records into a minimal v3 container so scan() sees a real file.
std::vector<uint8_t> makeBank(uint32_t boxes,
                              const std::vector<std::pair<size_t, std::pair<uint32_t, std::vector<uint8_t>>>>& at) {
    std::vector<uint8_t> f(16 + static_cast<size_t>(boxes) * 30 * 0x150, 0xFF);
    std::memcpy(f.data(), "PKSMBANK", 8);
    Utils::writeUInt32LittleEndian(f.data() + 8, 3);
    Utils::writeUInt32LittleEndian(f.data() + 12, boxes);
    for (const auto& e : at) {
        const size_t off = 16 + e.first * 0x150;
        Utils::writeUInt32LittleEndian(f.data() + off, e.second.first);
        std::memcpy(f.data() + off + 4, e.second.second.data(), e.second.second.size());
    }
    return f;
}

}

int main(int argc, char** argv) {
    const char* home = std::getenv("HOME");
    const std::string path = (argc > 1)
        ? argv[1]
        : std::string(home ? home : ".") + "/pkse-test-saves/pksm_1.bnk";

    std::printf("PKSM bank import harness\n  file: %s\n\n", path.c_str());

    const std::vector<uint8_t> file = readFile(path);
    if (file.empty()) {
        std::printf("  [FAIL] could not read the bank file\n");
        return 1;
    }

    std::printf("--- container + histogram ---\n");
    Trainer::PKSMBankImport imp;
    const bool ok = imp.scan(std::span<const uint8_t>(file.data(), file.size()));
    if (!ok) {
        std::printf("  [FAIL] scan rejected the file: %s\n", imp.report().error.c_str());
        return 1;
    }
    const auto& r = imp.report();
    checkEq<size_t>(r.containerVersion, 3, "container version");
    checkEq<size_t>(file.size(), 16 + r.sourceBoxes * 30 * 0x150, "size matches the box-count formula");
    checkEq<size_t>(r.sourceBoxes, r.headerBoxes, "header box count is honoured (file not truncated)");

    const RefCounts ref = referenceHistogram(file);
    size_t refOccupied = ref.bad;   // a nonsense tag still occupies its slot
    for (int t = 0; t < 16; ++t) refOccupied += ref.perTag[t];
    checkEq<size_t>(r.occupied, refOccupied, "occupied slots agree with an independent walk");
    checkEq<size_t>(ref.empty + refOccupied, ref.total, "every slot accounted for");
    for (int t = 0; t < Trainer::PKSM_GEN_COUNT; ++t) {
        if (r.perGen[t] == 0 && ref.perTag[t] == 0) continue;
        checkEq<size_t>(r.perGen[t], ref.perTag[t],
                        std::string("tag ") + std::to_string(t) + " (" + Trainer::pksmGenName(t) + ") count");
    }

    std::printf("\n  version %u, %zu boxes, %zu occupied\n", r.containerVersion, r.sourceBoxes, r.occupied);
    for (int t = 0; t < Trainer::PKSM_GEN_COUNT; ++t)
        if (r.perGen[t]) std::printf("    %-9s %5zu  %s\n", Trainer::pksmGenName(t), r.perGen[t],
                                     Trainer::pksmGenImportable(t) ? "-> importable" : "-> no PKSE format");
    std::printf("  importable %zu   unsupported %zu   damaged %zu   gen3-non-FRLG %zu\n\n",
                r.importable, r.unsupported, r.damaged, r.gen3NonFRLG);

    check(r.damaged == 0, "no record failed to decode (a wrong stride would fail here)");
    checkEq<size_t>(r.importable + r.unsupported + r.damaged, r.occupied, "every occupied slot classified");

    std::printf("\n--- commit into an empty bank ---\n");
    Trainer::Bank bank;
    for (auto& box : bank.boxes) for (auto& s : box) s.reset();   // ignore whatever load() found
    for (auto& n : bank.boxNames) n.clear();

    // Box names from the sidecar beside the .bnk.
    std::string sidecar = path;
    const size_t dot = sidecar.find_last_of('.');
    if (dot != std::string::npos) sidecar = sidecar.substr(0, dot) + ".json";
    const std::vector<uint8_t> namesRaw = readFile(sidecar);
    std::vector<std::string> srcNames;
    if (!namesRaw.empty()) {
        srcNames = Trainer::parsePKSMBoxNames(std::span<const uint8_t>(namesRaw.data(), namesRaw.size()));
        std::printf("  sidecar %s -> %zu names (first \"%s\")\n", sidecar.c_str(), srcNames.size(),
                    srcNames.empty() ? "" : srcNames[0].c_str());
        check(!srcNames.empty(), "box-name sidecar parsed despite its trailing NUL");
        imp.setBoxNames(srcNames);
    }

    const size_t staged = imp.stagedCount();
    const size_t placed = imp.commit(bank);
    checkEq<size_t>(placed, staged, "every staged Pokemon was placed");
    checkEq<size_t>(imp.report().overflow, static_cast<size_t>(0), "nothing overflowed an empty bank");
    checkEq<size_t>(imp.report().scattered, static_cast<size_t>(0), "no first-fit fallback was needed");
    std::printf("  %zu placed across %zu bank boxes, %zu names carried\n",
                placed, imp.report().boxesUsed, imp.report().namesCarried);

    // Every placed pokemon must survive the bank's own encrypt -> decrypt contract, and its checksum
    // must still validate: that is what Bank::verifyImage enforces on save, checked up front.
    size_t roundTripFails = 0, checksumFails = 0, live = 0;
    for (size_t b = 0; b < Trainer::Bank::BANK_BOX_COUNT; ++b) {
        for (size_t s = 0; s < Trainer::Bank::BANK_SLOTS_PER_BOX; ++s) {
            const auto& pk = bank.boxes[b][s];
            if (!pk) continue;
            ++live;
            if (pk->checksum() != pk->calculateChecksum()) ++checksumFails;
            auto copy = pk->clone();        // clone re-encrypts then re-decrypts through the ctor
            if (!copy || copy->getDataSize() != pk->getDataSize() ||
                std::memcmp(copy->getData().data(), pk->getData().data(), pk->getDataSize()) != 0)
                ++roundTripFails;
        }
    }
    checkEq<size_t>(live, placed, "bank holds exactly the placed count");
    checkEq<size_t>(checksumFails, static_cast<size_t>(0), "every placed Pokemon has a valid checksum");
    checkEq<size_t>(roundTripFails, static_cast<size_t>(0), "every placed Pokemon survives encrypt/decrypt byte-exact");

    // Placement contract: each SOURCE box that holds anything lands whole in the next empty
    // destination box, at the SAME slot positions, with its sidecar name. Rebuilt independently
    // from the file here, so a change to commit()'s packing shows up as a failure rather than as a
    // reshuffled bank nobody notices.
    {
        const uint32_t srcBoxes = Utils::readUInt32LittleEndian(file.data() + 12);
        size_t destBox = 0, checked = 0, mismatches = 0, nameMismatches = 0;
        for (uint32_t sb = 0; sb < srcBoxes; ++sb) {
            std::vector<std::pair<size_t, uint16_t>> want;   // slot -> NATIONAL species, this source box
            for (size_t sl = 0; sl < 30; ++sl) {
                const size_t off = 16 + (static_cast<size_t>(sb) * 30 + sl) * 0x150;
                if (off + 0x150 > file.size()) break;
                const uint32_t tg = Utils::readUInt32LittleEndian(file.data() + off);
                if (tg == 0xFFFFFFFFu) continue;            // empty slot
                // EVERY tag PKSE can house must appear here. A check that covers one tag examines NOTHING
                // against a bank of any other generation and still reports success -- the placement
                // contract goes unverified exactly when a newly-added format most needs checking. The
                // `default` below is what stops that: an unhandled tag FAILS rather than being skipped.
                const uint8_t* rec = file.data() + off + 4;
                switch (tg) {
                case 6u:
                    // Gen 3 species is the INTERNAL id at 0x20 of the canonical record.
                    want.push_back({ sl, Pokemon::gen3InternalToNational(
                        Utils::readUInt16LittleEndian(rec + 0x20)) });
                    break;
                case 7u:
                    // Gen 1 species is the INTERNAL index at the first byte of the PokeList1 body,
                    // which sits 3 bytes into the record behind the count/marker/cap header.
                    want.push_back({ sl, Pokemon::g1ToNational(rec[3]) });
                    break;
                case 8u:
                    // Gen 2 sits at the same body offset but needs no conversion: Gen 2 numbers its
                    // species by National Dex already, which is why PKSE ships no g2ToNational.
                    want.push_back({ sl, rec[3] });
                    break;
                case 0u: case 1u: case 2u: case 3u: case 4u: case 5u:
                    // PK4/PK5/PK6/PK7/PB7/PK8 all keep species at 0x08, and PKSM stores every
                    // record DECRYPTED, so it reads straight out of the file.
                    want.push_back({ sl, Utils::readUInt16LittleEndian(rec + 0x08) });
                    break;
                default:
                    check(false, "placement check knows a species offset for tag " + std::to_string(tg));
                    break;
                }
            }
            if (want.empty()) continue;
            for (const auto& w : want) {
                ++checked;
                const auto& pk = bank.boxes[destBox][w.first];
                if (!pk || pk->speciesID() != w.second) ++mismatches;
            }
            if (sb < srcNames.size() && !srcNames[sb].empty() && bank.boxNames[destBox] != srcNames[sb])
                ++nameMismatches;
            ++destBox;
        }
        checkEq<size_t>(checked, placed, "placement check covered every placed Pokemon");
        checkEq<size_t>(mismatches, static_cast<size_t>(0), "each Pokemon kept its slot position within its box");
        checkEq<size_t>(nameMismatches, static_cast<size_t>(0), "box names travelled with their boxes");
        checkEq<size_t>(destBox, imp.report().boxesUsed, "one destination box per non-empty source box");
    }

    // Sample the first box so a human can eyeball that these are real Pokemon, not plausible noise.
    // One sample PER GENERATION present, not just the first box. A newly added format's failure
    // mode is a plausible WRONG parse -- right stride, wrong offsets -- which a checksum still
    // accepts. Species is machine-checked above; a level of 0, a blank OT or an absurd name is
    // the shape that only shows up when a human looks at one record of each format.
    std::printf("\n  one sample per generation present:\n");
    {
        std::set<Enums::GameVersion> shownGroups;
        for (size_t b = 0; b < Trainer::Bank::BANK_BOX_COUNT; ++b) {
            for (size_t s = 0; s < Trainer::Bank::BANK_SLOTS_PER_BOX; ++s) {
                const auto& pk = bank.boxes[b][s];
                if (!pk || pk->speciesID() == 0) continue;
                // The entity's own group, not a tag: a PKSM tag and a PKSE bank tag are two
                // different numbering schemes and reading one as the other names the wrong game.
                const Enums::GameVersion group = pk->getGameGroup();
                if (!shownGroups.insert(group).second) continue;
                std::printf("    %-14s box %2zu slot %2zu  #%-4u %-12s Lv.%-3u OT %-8s nick %-12s\n",
                            Enums::getGameVersionName(group).c_str(), b + 1, s + 1,
                            pk->speciesID(), pk->species(), pk->level(),
                            u16ToUtf8(pk->otName()).c_str(), u16ToUtf8(pk->nickname()).c_str());
            }
        }
    }

    std::printf("\n--- synthetic Gen 8 + Let's Go records (no real ones in this bank) ---\n");
    {
        auto pk8 = makeSyntheticPK8(25, 50);     // Pikachu, level 50
        auto pb7 = makeSyntheticPB7(133, 36);    // Eevee, level 36
        const auto f = makeBank(1, {
            { 0, { 5u, pk8 } },                  // tag 5 = Gen 8
            { 1, { 4u, pb7 } },                  // tag 4 = LGPE
            // An all-zero PK6 payload: the tag is one PKSE now houses, so it is no longer counted
            // as "unsupported" -- but species 0 is not a Pokemon, so it lands in `damaged`. That
            // is the honest outcome and it keeps a record of the distinction: `unsupported` means
            // "PKSE has no format for this generation", which is now true of no tag at all.
            { 2, { 2u, std::vector<uint8_t>(232, 0) } },   // tag 2 = Gen 6: a supported tag, empty payload
            { 3, { 99u, std::vector<uint8_t>(4, 0) } },    // nonsense tag: damaged, not a crash
        });
        Trainer::PKSMBankImport imp2;
        check(imp2.scan(std::span<const uint8_t>(f.data(), f.size())), "synthetic bank parses");
        const auto& r2 = imp2.report();
        checkEq<size_t>(r2.importable, static_cast<size_t>(2), "2 importable (PK8 + PB7)");
        checkEq<size_t>(r2.unsupported, static_cast<size_t>(0), "no tag is unsupported any more");
        checkEq<size_t>(r2.damaged, static_cast<size_t>(2), "2 damaged (bogus tag + species-0 PK6)");
        checkEq<size_t>(r2.occupied, static_cast<size_t>(4), "a bogus tag still counts as an occupied slot");
        checkEq<size_t>(r2.importable + r2.unsupported + r2.damaged, r2.occupied,
                        "the three outcomes still add up to occupied");

        Trainer::Bank b2;
        for (auto& box : b2.boxes) for (auto& s : box) s.reset();
        imp2.commit(b2);
        const auto& a = b2.boxes[0][0];
        const auto& e = b2.boxes[0][1];
        check(a && a->speciesID() == 25, "Gen 8 record imported as Pikachu");
        check(a && a->level() == 50, "Gen 8 level seeded from EXP (party tail filled)");
        check(a && a->getDataSize() == SIZE_PARTY8_SWSH, "Gen 8 grown to the PARTY record size");
        check(a && a->statHPMax() > 0, "Gen 8 battle stats recalculated");
        check(e && e->speciesID() == 133, "Let's Go record imported as Eevee");
        check(e && e->level() == 36, "Let's Go level derived from EXP");
        check(e && e->getDataSize() == SIZE_PARTY7_LGPE, "Let's Go grown to the PARTY record size");
    }

    // The fallback nobody exercises by accident: once whole empty boxes run out, the rest has to
    // scatter into free slots rather than stop -- a half-import cannot be resumed without
    // duplicating, so "place what fits, report the rest" is the only safe answer.
    std::printf("\n--- placement when the bank is nearly full ---\n");
    {
        // Two source boxes, two Pokemon each.
        auto a = makeSyntheticPK8(1, 5), b = makeSyntheticPK8(4, 5);
        auto c = makeSyntheticPK8(7, 5), d = makeSyntheticPK8(25, 5);
        const auto f = makeBank(2, { { 0, { 5u, a } }, { 1, { 5u, b } },
                                     { 30, { 5u, c } }, { 31, { 5u, d } } });
        Trainer::PKSMBankImport imp3;
        imp3.scan(std::span<const uint8_t>(f.data(), f.size()));
        checkEq<size_t>(imp3.report().sourceBoxesUsed, static_cast<size_t>(2),
                        "two source boxes hold content");

        Trainer::Bank b3;
        for (auto& box : b3.boxes) for (auto& s : box) s.reset();
        // Leave exactly one box empty; put a single occupant in every other box, and fill the very
        // last box completely so the fallback has somewhere to run out.
        auto filler = [&]() {
            auto rec = makeSyntheticPK8(133, 5);
            const auto one = makeBank(1, { { 0, { 5u, rec } } });
            Trainer::PKSMBankImport t;
            t.scan(std::span<const uint8_t>(one.data(), one.size()));
            Trainer::Bank scratchBank;
            for (auto& box : scratchBank.boxes) for (auto& s : box) s.reset();
            t.commit(scratchBank);
            return std::move(scratchBank.boxes[0][0]);
        };
        for (size_t i = 1; i < Trainer::Bank::BANK_BOX_COUNT; ++i) b3.boxes[i][0] = filler();
        for (size_t s = 0; s < Trainer::Bank::BANK_SLOTS_PER_BOX; ++s)
            if (!b3.boxes[Trainer::Bank::BANK_BOX_COUNT - 1][s])
                b3.boxes[Trainer::Bank::BANK_BOX_COUNT - 1][s] = filler();

        imp3.commit(b3);
        const auto& r3 = imp3.report();
        checkEq<size_t>(r3.placed, static_cast<size_t>(4), "all four placed");
        checkEq<size_t>(r3.boxesUsed, static_cast<size_t>(1), "one whole box used (only one was empty)");
        checkEq<size_t>(r3.scattered, static_cast<size_t>(2), "the other box's two scattered into free slots");
        checkEq<size_t>(r3.overflow, static_cast<size_t>(0), "nothing lost while free slots remained");

        // And with NO free slot at all: the count has to come back as overflow, not as a write past
        // the last box.
        Trainer::PKSMBankImport imp4;
        imp4.scan(std::span<const uint8_t>(f.data(), f.size()));
        Trainer::Bank full;
        for (auto& box : full.boxes) for (auto& s : box) if (!s) s = filler();
        imp4.commit(full);
        checkEq<size_t>(imp4.report().placed, static_cast<size_t>(0), "a full bank places nothing");
        checkEq<size_t>(imp4.report().overflow, static_cast<size_t>(4), "and reports all four as overflow");
    }

    std::printf("\n--- box-name sidecar edge cases ---\n");
    {
        auto parse = [](const char* s, size_t n) {
            return Trainer::parsePKSMBoxNames(
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s), n));
        };
        const char withNul[] = "[\n  \"Alpha\",\n  \"Be\\u00e9ta\"\n]\0";
        auto a = parse(withNul, sizeof(withNul) - 1);
        checkEq<size_t>(a.size(), static_cast<size_t>(2), "two names past the trailing NUL");
        check(a.size() == 2 && a[0] == "Alpha", "first name");
        check(a.size() == 2 && a[1] == std::string("Be\xC3\xA9""ta"), "\\uXXXX decoded to UTF-8");
        const char notArray[] = "{\"pksm_1\":150}";
        check(parse(notArray, sizeof(notArray) - 1).empty(), "the banks.json registry yields no names");
        const char truncated[] = "[\"one\", \"tw";
        checkEq<size_t>(parse(truncated, sizeof(truncated) - 1).size(), static_cast<size_t>(1),
                        "a truncated array keeps the complete names");
    }

    std::printf("\n--- rejection paths ---\n");
    {
        Trainer::PKSMBankImport bad;
        std::vector<uint8_t> junk(4096, 0x5A);
        check(!bad.scan(std::span<const uint8_t>(junk.data(), junk.size())), "a non-bank file is refused");
        check(!bad.report().error.empty(), "  ... with a reason");

        std::vector<uint8_t> legacy(232 * 30 * 2, 0);            // headerless pre-2019 bank.bin shape
        Trainer::PKSMBankImport old;
        check(!old.scan(std::span<const uint8_t>(legacy.data(), legacy.size())), "bank.bin is refused");
        check(old.report().error.find("bank.bin") != std::string::npos, "  ... and identified by name");

        std::vector<uint8_t> future(16 + 30 * 0x150, 0xFF);
        std::memcpy(future.data(), "PKSMBANK", 8);
        Utils::writeUInt32LittleEndian(future.data() + 8, 99);
        Utils::writeUInt32LittleEndian(future.data() + 12, 1);
        Trainer::PKSMBankImport fut;
        check(!fut.scan(std::span<const uint8_t>(future.data(), future.size())), "a future version is refused");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
