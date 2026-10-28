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

    void check(bool passed, const std::string &description)
    {
        std::printf("  [%s] %s\n", passed ? " OK " : "FAIL", description.c_str());
        if (!passed)
        {
            ++g_failures;
        }
    }

    template <typename ValueType>
    void checkEq(ValueType actualValue, ValueType expectedValue, const std::string &description)
    {
        const bool passed = (actualValue == expectedValue);
        std::printf("  [%s] %s (got %lld, want %lld)\n", passed ? " OK " : "FAIL", description.c_str(), static_cast<long long>(actualValue), static_cast<long long>(expectedValue));
        if (!passed)
        {
            ++g_failures;
        }
    }

    std::vector<uint8_t> readFile(const std::string &path)
    {
        std::vector<uint8_t> fileBytes;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            return fileBytes;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (fileSize > 0)
        {
            fileBytes.resize(static_cast<size_t>(fileSize));
            if (std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
            {
                fileBytes.clear();
            }
        }
        std::fclose(file);
        return fileBytes;
    }

    std::string toUtf8UntilNul(const std::u16string &utf16Text)
    {
        std::string utf8Text;
        for (char16_t codeUnit : utf16Text)
        {
            if (codeUnit == 0)
            {
                break;
            }
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

    // Counts the file's slots with a second, deliberately dumb implementation: if this and the parser
    // under test agree, both read the same stride. Written from the format spec, not shared with it.
    struct ReferenceCounts
    {
        size_t emptyCount = 0;
        size_t perTagCounts[16] = {};
        size_t badTagCount = 0;
        size_t totalSlotCount = 0;
    };

    ReferenceCounts referenceHistogram(const std::vector<uint8_t> &bankBytes)
    {
        ReferenceCounts counts;
        if (bankBytes.size() < 16 || std::memcmp(bankBytes.data(), "PKSMBANK", 8) != 0)
        {
            return counts;
        }
        const uint32_t boxCount = Utils::readUInt32LittleEndian(bankBytes.data() + 12);
        for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(boxCount) * 30; ++slotIndex)
        {
            const size_t slotOffset = 16 + slotIndex * 0x150;
            if (slotOffset + 0x150 > bankBytes.size())
            {
                break;
            }
            ++counts.totalSlotCount;
            const uint32_t generationTag = Utils::readUInt32LittleEndian(bankBytes.data() + slotOffset);
            if (generationTag == 0xFFFFFFFFu)
            {
                ++counts.emptyCount;
                continue;
            }
            if (generationTag < 16)
            {
                ++counts.perTagCounts[generationTag];
            }
            else
            {
                ++counts.badTagCount;
            }
        }
        return counts;
    }

    // A decrypted PK8 box record built from nothing, so the Gen 8 branch is exercised even though the
    // only real bank on hand holds none. Checksum is the same u16 sum the entity computes.
    std::vector<uint8_t> makeSyntheticPK8(uint16_t speciesId, uint8_t level)
    {
        std::vector<uint8_t> record(328, 0);
        Utils::writeUInt32LittleEndian(record.data() + 0x00, 0xDEADBEEF);                 // encryption constant
        Utils::writeUInt16LittleEndian(record.data() + 0x08, speciesId);                    // species
        Utils::writeUInt32LittleEndian(record.data() + 0x10, Pokemon::getExpForLevel(level, Pokemon::getGrowthRate(speciesId)));
        Utils::writeUInt32LittleEndian(record.data() + 0x1C, 0x7FFFFFFFu);                // IV32: all 31s
        Utils::writeUInt32LittleEndian(record.data() + 0x28, 0x11223344);                 // PID
        uint16_t checksum = 0;
        for (size_t wordOffset = 0x08; wordOffset < 328; wordOffset += 2)
        {
            checksum += Utils::readUInt16LittleEndian(record.data() + wordOffset);
        }
        Utils::writeUInt16LittleEndian(record.data() + 0x06, checksum);
        return record;
    }

    // The same idea for Let's Go: a decrypted PB7 box record, checksummed over 0x08..0xE8.
    std::vector<uint8_t> makeSyntheticPB7(uint16_t speciesId, uint8_t level)
    {
        std::vector<uint8_t> record(232, 0);
        Utils::writeUInt32LittleEndian(record.data() + 0x00, 0xCAFEBABE);
        Utils::writeUInt16LittleEndian(record.data() + 0x08, speciesId);
        Utils::writeUInt32LittleEndian(record.data() + 0x10, Pokemon::getExpForLevel(level, Pokemon::getGrowthRate(speciesId)));
        Utils::writeUInt32LittleEndian(record.data() + 0x1C, 0x7FFFFFFFu);
        Utils::writeUInt32LittleEndian(record.data() + 0x18, 0x11223344);
        uint16_t checksum = 0;
        for (size_t wordOffset = 0x08; wordOffset < 232; wordOffset += 2)
        {
            checksum += Utils::readUInt16LittleEndian(record.data() + wordOffset);
        }
        Utils::writeUInt16LittleEndian(record.data() + 0x06, checksum);
        return record;
    }

    /// Wrap a set of records into a minimal v3 container so scan() sees a real file.
    std::vector<uint8_t> makeBank(uint32_t boxCount, const std::vector<std::pair<size_t, std::pair<uint32_t, std::vector<uint8_t>>>> &slotRecords)
    {
        std::vector<uint8_t> bankBytes(16 + static_cast<size_t>(boxCount) * 30 * 0x150, 0xFF);
        std::memcpy(bankBytes.data(), "PKSMBANK", 8);
        Utils::writeUInt32LittleEndian(bankBytes.data() + 8, 3);
        Utils::writeUInt32LittleEndian(bankBytes.data() + 12, boxCount);
        for (const auto &slotRecord : slotRecords)
        {
            const size_t slotOffset = 16 + slotRecord.first * 0x150;
            Utils::writeUInt32LittleEndian(bankBytes.data() + slotOffset, slotRecord.second.first);
            std::memcpy(bankBytes.data() + slotOffset + 4, slotRecord.second.second.data(), slotRecord.second.second.size());
        }
        return bankBytes;
    }

}

int main(int argc, char **argv)
{
    const char *homeDirectory = std::getenv("HOME");
    const std::string bankPath = (argc > 1) ? argv[1] : std::string(homeDirectory ? homeDirectory : ".") + "/pkse-test-saves/pksm_1.bnk";

    std::printf("PKSM bank import harness\n  file: %s\n\n", bankPath.c_str());

    const std::vector<uint8_t> bankBytes = readFile(bankPath);
    if (bankBytes.empty())
    {
        std::printf("  [FAIL] could not read the bank file\n");
        return 1;
    }

    std::printf("--- container + histogram ---\n");
    Trainer::PKSMBankImport importer;
    const bool scanned = importer.scan(std::span<const uint8_t>(bankBytes.data(), bankBytes.size()));
    if (!scanned)
    {
        std::printf("  [FAIL] scan rejected the file: %s\n", importer.report().error.c_str());
        return 1;
    }
    const auto &report = importer.report();
    checkEq<size_t>(report.containerVersion, 3, "container version");
    checkEq<size_t>(bankBytes.size(), 16 + report.sourceBoxes * 30 * 0x150, "size matches the box-count formula");
    checkEq<size_t>(report.sourceBoxes, report.headerBoxes, "header box count is honoured (file not truncated)");

    const ReferenceCounts referenceCounts = referenceHistogram(bankBytes);
    size_t referenceOccupiedCount = referenceCounts.badTagCount;   // a nonsense tag still occupies its slot
    for (int tagIndex = 0; tagIndex < 16; ++tagIndex)
    {
        referenceOccupiedCount += referenceCounts.perTagCounts[tagIndex];
    }
    checkEq<size_t>(report.occupied, referenceOccupiedCount, "occupied slots agree with an independent walk");
    checkEq<size_t>(referenceCounts.emptyCount + referenceOccupiedCount, referenceCounts.totalSlotCount, "every slot accounted for");
    for (int tagIndex = 0; tagIndex < Trainer::PKSM_GEN_COUNT; ++tagIndex)
    {
        if (report.perGen[tagIndex] == 0 && referenceCounts.perTagCounts[tagIndex] == 0)
        {
            continue;
        }
        checkEq<size_t>(report.perGen[tagIndex], referenceCounts.perTagCounts[tagIndex], std::string("tag ") + std::to_string(tagIndex) + " (" + Trainer::pksmGenName(tagIndex) + ") count");
    }

    std::printf("\n  version %u, %zu boxes, %zu occupied\n", report.containerVersion, report.sourceBoxes, report.occupied);
    for (int tagIndex = 0; tagIndex < Trainer::PKSM_GEN_COUNT; ++tagIndex)
    {
        if (report.perGen[tagIndex])
        {
            std::printf("    %-9s %5zu  %s\n", Trainer::pksmGenName(tagIndex), report.perGen[tagIndex], Trainer::pksmGenImportable(tagIndex) ? "-> importable" : "-> no PKSE format");
        }
    }
    std::printf("  importable %zu   unsupported %zu   damaged %zu   gen3-non-FRLG %zu\n\n", report.importable, report.unsupported, report.damaged, report.gen3NonFRLG);

    check(report.damaged == 0, "no record failed to decode (a wrong stride would fail here)");
    checkEq<size_t>(report.importable + report.unsupported + report.damaged, report.occupied, "every occupied slot classified");

    std::printf("\n--- commit into an empty bank ---\n");
    Trainer::Bank bank;
    for (auto &box : bank.boxes)
    {
        for (auto &slot : box)
        {
            slot.reset();   // ignore whatever load() found
        }
    }
    for (auto &boxName : bank.boxNames)
    {
        boxName.clear();
    }

    // Box names from the sidecar beside the .bnk.
    std::string sidecarPath = bankPath;
    const size_t extensionDot = sidecarPath.find_last_of('.');
    if (extensionDot != std::string::npos)
    {
        sidecarPath = sidecarPath.substr(0, extensionDot) + ".json";
    }
    const std::vector<uint8_t> sidecarBytes = readFile(sidecarPath);
    std::vector<std::string> sourceBoxNames;
    if (!sidecarBytes.empty())
    {
        sourceBoxNames = Trainer::parsePKSMBoxNames(std::span<const uint8_t>(sidecarBytes.data(), sidecarBytes.size()));
        std::printf("  sidecar %s -> %zu names (first \"%s\")\n", sidecarPath.c_str(), sourceBoxNames.size(), sourceBoxNames.empty() ? "" : sourceBoxNames[0].c_str());
        check(!sourceBoxNames.empty(), "box-name sidecar parsed despite its trailing NUL");
        importer.setBoxNames(sourceBoxNames);
    }

    const size_t stagedCount = importer.stagedCount();
    const size_t placedCount = importer.commit(bank);
    checkEq<size_t>(placedCount, stagedCount, "every staged Pokemon was placed");
    checkEq<size_t>(importer.report().overflow, static_cast<size_t>(0), "nothing overflowed an empty bank");
    checkEq<size_t>(importer.report().scattered, static_cast<size_t>(0), "no first-fit fallback was needed");
    std::printf("  %zu placed across %zu bank boxes, %zu names carried\n", placedCount, importer.report().boxesUsed, importer.report().namesCarried);

    // Every placed pokemon must survive the bank's own encrypt -> decrypt contract, and its checksum
    // must still validate: that is what Bank::verifyImage enforces on save, checked up front.
    size_t roundTripFailCount = 0;
    size_t checksumFailCount = 0;
    size_t liveCount = 0;
    for (size_t boxIndex = 0; boxIndex < Trainer::Bank::BANK_BOX_COUNT; ++boxIndex)
    {
        for (size_t slotIndex = 0; slotIndex < Trainer::Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
        {
            const auto &pokemon = bank.boxes[boxIndex][slotIndex];
            if (!pokemon)
            {
                continue;
            }
            ++liveCount;
            if (pokemon->checksum() != pokemon->calculateChecksum())
            {
                ++checksumFailCount;
            }
            auto clonedPokemon = pokemon->clone();        // clone re-encrypts then re-decrypts through the ctor
            if (!clonedPokemon || clonedPokemon->getDataSize() != pokemon->getDataSize() || std::memcmp(clonedPokemon->getData().data(), pokemon->getData().data(), pokemon->getDataSize()) != 0)
            {
                ++roundTripFailCount;
            }
        }
    }
    checkEq<size_t>(liveCount, placedCount, "bank holds exactly the placed count");
    checkEq<size_t>(checksumFailCount, static_cast<size_t>(0), "every placed Pokemon has a valid checksum");
    checkEq<size_t>(roundTripFailCount, static_cast<size_t>(0), "every placed Pokemon survives encrypt/decrypt byte-exact");

    // Placement contract: each SOURCE box that holds anything lands whole in the next empty
    // destination box, at the SAME slot positions, with its sidecar name. Rebuilt independently
    // from the file here, so a change to commit()'s packing shows up as a failure rather than as a
    // reshuffled bank nobody notices.
    {
        const uint32_t sourceBoxCount = Utils::readUInt32LittleEndian(bankBytes.data() + 12);
        size_t destinationBoxIndex = 0;
        size_t checkedCount = 0;
        size_t slotMismatchCount = 0;
        size_t nameMismatchCount = 0;
        for (uint32_t sourceBoxIndex = 0; sourceBoxIndex < sourceBoxCount; ++sourceBoxIndex)
        {
            std::vector<std::pair<size_t, uint16_t>> expectedSpecies;   // slot -> NATIONAL species, this source box
            for (size_t slotIndex = 0; slotIndex < 30; ++slotIndex)
            {
                const size_t slotOffset = 16 + (static_cast<size_t>(sourceBoxIndex) * 30 + slotIndex) * 0x150;
                if (slotOffset + 0x150 > bankBytes.size())
                {
                    break;
                }
                const uint32_t generationTag = Utils::readUInt32LittleEndian(bankBytes.data() + slotOffset);
                if (generationTag == 0xFFFFFFFFu)
                {
                    continue;            // empty slot
                }
                // EVERY tag PKSE can house must appear here. A check that covers one tag examines NOTHING
                // against a bank of any other generation and still reports success -- the placement
                // contract goes unverified exactly when a newly-added format most needs checking. The
                // `default` below is what stops that: an unhandled tag FAILS rather than being skipped.
                const uint8_t *record = bankBytes.data() + slotOffset + 4;
                switch (generationTag)
                {
                case 6u:
                    // Gen 3 species is the INTERNAL id at 0x20 of the canonical record.
                    expectedSpecies.push_back({slotIndex, Pokemon::gen3InternalToNational(Utils::readUInt16LittleEndian(record + 0x20))});
                    break;
                case 7u:
                    // Gen 1 species is the INTERNAL index at the first byte of the PokeList1 body,
                    // which sits 3 bytes into the record behind the count/marker/cap header.
                    expectedSpecies.push_back({slotIndex, Pokemon::g1ToNational(record[3])});
                    break;
                case 8u:
                    // Gen 2 sits at the same body offset but needs no conversion: Gen 2 numbers its
                    // species by National Dex already, which is why PKSE ships no g2ToNational.
                    expectedSpecies.push_back({slotIndex, record[3]});
                    break;
                case 0u:
                case 1u:
                case 2u:
                case 3u:
                case 4u:
                case 5u:
                    // PK4/PK5/PK6/PK7/PB7/PK8 all keep species at 0x08, and PKSM stores every
                    // record DECRYPTED, so it reads straight out of the file.
                    expectedSpecies.push_back({slotIndex, Utils::readUInt16LittleEndian(record + 0x08)});
                    break;
                default:
                    check(false, "placement check knows a species offset for tag " + std::to_string(generationTag));
                    break;
                }
            }
            if (expectedSpecies.empty())
            {
                continue;
            }
            for (const auto &expectedSlot : expectedSpecies)
            {
                ++checkedCount;
                const auto &pokemon = bank.boxes[destinationBoxIndex][expectedSlot.first];
                if (!pokemon || pokemon->speciesID() != expectedSlot.second)
                {
                    ++slotMismatchCount;
                }
            }
            if (sourceBoxIndex < sourceBoxNames.size() && !sourceBoxNames[sourceBoxIndex].empty() && bank.boxNames[destinationBoxIndex] != sourceBoxNames[sourceBoxIndex])
            {
                ++nameMismatchCount;
            }
            ++destinationBoxIndex;
        }
        checkEq<size_t>(checkedCount, placedCount, "placement check covered every placed Pokemon");
        checkEq<size_t>(slotMismatchCount, static_cast<size_t>(0), "each Pokemon kept its slot position within its box");
        checkEq<size_t>(nameMismatchCount, static_cast<size_t>(0), "box names travelled with their boxes");
        checkEq<size_t>(destinationBoxIndex, importer.report().boxesUsed, "one destination box per non-empty source box");
    }

    // Sample the first box so a human can eyeball that these are real Pokemon, not plausible noise.
    // One sample PER GENERATION present, not just the first box. A newly added format's failure
    // mode is a plausible WRONG parse -- right stride, wrong offsets -- which a checksum still
    // accepts. Species is machine-checked above; a level of 0, a blank OT or an absurd name is
    // the shape that only shows up when a human looks at one record of each format.
    std::printf("\n  one sample per generation present:\n");
    {
        std::set<Enums::GameVersion> shownGroups;
        for (size_t boxIndex = 0; boxIndex < Trainer::Bank::BANK_BOX_COUNT; ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < Trainer::Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
            {
                const auto &pokemon = bank.boxes[boxIndex][slotIndex];
                if (!pokemon || pokemon->speciesID() == 0)
                {
                    continue;
                }
                // The entity's own group, not a tag: a PKSM tag and a PKSE bank tag are two
                // different numbering schemes and reading one as the other names the wrong game.
                const Enums::GameVersion group = pokemon->getGameGroup();
                if (!shownGroups.insert(group).second)
                {
                    continue;
                }
                std::printf("    %-14s box %2zu slot %2zu  #%-4u %-12s Lv.%-3u OT %-8s nick %-12s\n", Enums::getGameVersionName(group).c_str(), boxIndex + 1, slotIndex + 1, pokemon->speciesID(), pokemon->species(), pokemon->level(), toUtf8UntilNul(pokemon->otName()).c_str(), toUtf8UntilNul(pokemon->nickname()).c_str());
            }
        }
    }

    std::printf("\n--- synthetic Gen 8 + Let's Go records (no real ones in this bank) ---\n");
    {
        auto syntheticPK8 = makeSyntheticPK8(25, 50);     // Pikachu, level 50
        auto syntheticPB7 = makeSyntheticPB7(133, 36);    // Eevee, level 36
        const auto syntheticBankBytes = makeBank(1,
            {
                {0, {5u, syntheticPK8}},                  // tag 5 = Gen 8
                {1, {4u, syntheticPB7}},                  // tag 4 = LGPE
                // An all-zero PK6 payload: the tag is one PKSE now houses, so it is no longer counted
                // as "unsupported" -- but species 0 is not a Pokemon, so it lands in `damaged`. That
                // is the honest outcome and it keeps a record of the distinction: `unsupported` means
                // "PKSE has no format for this generation", which is now true of no tag at all.
                {2, {2u, std::vector<uint8_t>(232, 0)}},   // tag 2 = Gen 6: a supported tag, empty payload
                {3, {99u, std::vector<uint8_t>(4, 0)}},    // nonsense tag: damaged, not a crash
            });
        Trainer::PKSMBankImport syntheticImporter;
        check(syntheticImporter.scan(std::span<const uint8_t>(syntheticBankBytes.data(), syntheticBankBytes.size())), "synthetic bank parses");
        const auto &syntheticReport = syntheticImporter.report();
        checkEq<size_t>(syntheticReport.importable, static_cast<size_t>(2), "2 importable (PK8 + PB7)");
        checkEq<size_t>(syntheticReport.unsupported, static_cast<size_t>(0), "no tag is unsupported any more");
        checkEq<size_t>(syntheticReport.damaged, static_cast<size_t>(2), "2 damaged (bogus tag + species-0 PK6)");
        checkEq<size_t>(syntheticReport.occupied, static_cast<size_t>(4), "a bogus tag still counts as an occupied slot");
        checkEq<size_t>(syntheticReport.importable + syntheticReport.unsupported + syntheticReport.damaged, syntheticReport.occupied, "the three outcomes still add up to occupied");

        Trainer::Bank syntheticBank;
        for (auto &box : syntheticBank.boxes)
        {
            for (auto &slot : box)
            {
                slot.reset();
            }
        }
        syntheticImporter.commit(syntheticBank);
        const auto &pikachu = syntheticBank.boxes[0][0];
        const auto &eevee = syntheticBank.boxes[0][1];
        check(pikachu && pikachu->speciesID() == 25, "Gen 8 record imported as Pikachu");
        check(pikachu && pikachu->level() == 50, "Gen 8 level seeded from EXP (party tail filled)");
        check(pikachu && pikachu->getDataSize() == SIZE_PARTY8_SWSH, "Gen 8 grown to the PARTY record size");
        check(pikachu && pikachu->statHPMax() > 0, "Gen 8 battle stats recalculated");
        check(eevee && eevee->speciesID() == 133, "Let's Go record imported as Eevee");
        check(eevee && eevee->level() == 36, "Let's Go level derived from EXP");
        check(eevee && eevee->getDataSize() == SIZE_PARTY7_LGPE, "Let's Go grown to the PARTY record size");
    }

    // The fallback nobody exercises by accident: once whole empty boxes run out, the rest has to
    // scatter into free slots rather than stop -- a half-import cannot be resumed without
    // duplicating, so "place what fits, report the rest" is the only safe answer.
    std::printf("\n--- placement when the bank is nearly full ---\n");
    {
        // Two source boxes, two Pokemon each.
        auto bulbasaurRecord = makeSyntheticPK8(1, 5);
        auto charmanderRecord = makeSyntheticPK8(4, 5);
        auto squirtleRecord = makeSyntheticPK8(7, 5);
        auto pikachuRecord = makeSyntheticPK8(25, 5);
        const auto twoBoxBankBytes = makeBank(2, {{0, {5u, bulbasaurRecord}}, {1, {5u, charmanderRecord}}, {30, {5u, squirtleRecord}}, {31, {5u, pikachuRecord}}});
        Trainer::PKSMBankImport nearlyFullImporter;
        nearlyFullImporter.scan(std::span<const uint8_t>(twoBoxBankBytes.data(), twoBoxBankBytes.size()));
        checkEq<size_t>(nearlyFullImporter.report().sourceBoxesUsed, static_cast<size_t>(2), "two source boxes hold content");

        Trainer::Bank nearlyFullBank;
        for (auto &box : nearlyFullBank.boxes)
        {
            for (auto &slot : box)
            {
                slot.reset();
            }
        }
        // Leave exactly one box empty; put a single occupant in every other box, and fill the very
        // last box completely so the fallback has somewhere to run out.
        auto makeFillerPokemon = [&]()
        {
            auto fillerRecord = makeSyntheticPK8(133, 5);
            const auto oneBoxBankBytes = makeBank(1, {{0, {5u, fillerRecord}}});
            Trainer::PKSMBankImport fillerImporter;
            fillerImporter.scan(std::span<const uint8_t>(oneBoxBankBytes.data(), oneBoxBankBytes.size()));
            Trainer::Bank scratchBank;
            for (auto &box : scratchBank.boxes)
            {
                for (auto &slot : box)
                {
                    slot.reset();
                }
            }
            fillerImporter.commit(scratchBank);
            return std::move(scratchBank.boxes[0][0]);
        };
        for (size_t boxIndex = 1; boxIndex < Trainer::Bank::BANK_BOX_COUNT; ++boxIndex)
        {
            nearlyFullBank.boxes[boxIndex][0] = makeFillerPokemon();
        }
        for (size_t slotIndex = 0; slotIndex < Trainer::Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
        {
            if (!nearlyFullBank.boxes[Trainer::Bank::BANK_BOX_COUNT - 1][slotIndex])
            {
                nearlyFullBank.boxes[Trainer::Bank::BANK_BOX_COUNT - 1][slotIndex] = makeFillerPokemon();
            }
        }

        nearlyFullImporter.commit(nearlyFullBank);
        const auto &nearlyFullReport = nearlyFullImporter.report();
        checkEq<size_t>(nearlyFullReport.placed, static_cast<size_t>(4), "all four placed");
        checkEq<size_t>(nearlyFullReport.boxesUsed, static_cast<size_t>(1), "one whole box used (only one was empty)");
        checkEq<size_t>(nearlyFullReport.scattered, static_cast<size_t>(2), "the other box's two scattered into free slots");
        checkEq<size_t>(nearlyFullReport.overflow, static_cast<size_t>(0), "nothing lost while free slots remained");

        // And with NO free slot at all: the count has to come back as overflow, not as a write past
        // the last box.
        Trainer::PKSMBankImport fullImporter;
        fullImporter.scan(std::span<const uint8_t>(twoBoxBankBytes.data(), twoBoxBankBytes.size()));
        Trainer::Bank fullBank;
        for (auto &box : fullBank.boxes)
        {
            for (auto &slot : box)
            {
                if (!slot)
                {
                    slot = makeFillerPokemon();
                }
            }
        }
        fullImporter.commit(fullBank);
        checkEq<size_t>(fullImporter.report().placed, static_cast<size_t>(0), "a full bank places nothing");
        checkEq<size_t>(fullImporter.report().overflow, static_cast<size_t>(4), "and reports all four as overflow");
    }

    std::printf("\n--- box-name sidecar edge cases ---\n");
    {
        auto parseBoxNames = [](const char *sidecarText, size_t sidecarLength) { return Trainer::parsePKSMBoxNames(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(sidecarText), sidecarLength)); };
        const char withNul[] = "[\n  \"Alpha\",\n  \"Be\\u00e9ta\"\n]\0";
        auto parsedNames = parseBoxNames(withNul, sizeof(withNul) - 1);
        checkEq<size_t>(parsedNames.size(), static_cast<size_t>(2), "two names past the trailing NUL");
        check(parsedNames.size() == 2 && parsedNames[0] == "Alpha", "first name");
        // clang-format off
        check(parsedNames.size() == 2 && parsedNames[1] == std::string("Be\xC3\xA9""ta"), "\\uXXXX decoded to UTF-8");
        // clang-format on
        const char notArray[] = "{\"pksm_1\":150}";
        check(parseBoxNames(notArray, sizeof(notArray) - 1).empty(), "the banks.json registry yields no names");
        const char truncated[] = "[\"one\", \"tw";
        checkEq<size_t>(parseBoxNames(truncated, sizeof(truncated) - 1).size(), static_cast<size_t>(1), "a truncated array keeps the complete names");
    }

    std::printf("\n--- rejection paths ---\n");
    {
        Trainer::PKSMBankImport junkImporter;
        std::vector<uint8_t> junkBytes(4096, 0x5A);
        check(!junkImporter.scan(std::span<const uint8_t>(junkBytes.data(), junkBytes.size())), "a non-bank file is refused");
        check(!junkImporter.report().error.empty(), "  ... with a reason");

        std::vector<uint8_t> legacyBytes(232 * 30 * 2, 0);            // headerless pre-2019 bank.bin shape
        Trainer::PKSMBankImport legacyImporter;
        check(!legacyImporter.scan(std::span<const uint8_t>(legacyBytes.data(), legacyBytes.size())), "bank.bin is refused");
        check(legacyImporter.report().error.find("bank.bin") != std::string::npos, "  ... and identified by name");

        std::vector<uint8_t> futureBytes(16 + 30 * 0x150, 0xFF);
        std::memcpy(futureBytes.data(), "PKSMBANK", 8);
        Utils::writeUInt32LittleEndian(futureBytes.data() + 8, 99);
        Utils::writeUInt32LittleEndian(futureBytes.data() + 12, 1);
        Trainer::PKSMBankImport futureImporter;
        check(!futureImporter.scan(std::span<const uint8_t>(futureBytes.data(), futureBytes.size())), "a future version is refused");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
