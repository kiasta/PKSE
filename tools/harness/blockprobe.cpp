/**
 * A diagnostic, not a test. When PKSE reports something about a save that the console
 * contradicts -- a DLC revision the player does not own, a pouch that reads empty -- the
 * question is always the same: does the block exist, how big is it, and is there anything
 * in it? Guessing at that from the parsing code is how an afternoon disappears.
 *
 * Prints, for a named set of block keys: present / absent, size, and whether every byte is
 * zero. An allocated-but-empty block and an absent one mean very different things, and most
 * "is this DLC installed" logic in both PKSE and PKHeX turns on exactly that distinction.
 *
 * Runs on the HOST. Read-only; it never writes to the save.
 *
 * Build + run:  pwsh tools/harness/build.ps1 -Harness blockprobe -Saves <dir>
 *               (expects <dir>/main, or pass a full path to the file as the second arg)
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <span>
#include <vector>

#include "Encryption/Encryption.h"
#include "Encryption/Encryption8SWSH.h"
#include "Globals.h"
#include "Save/Block.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"

namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
}

namespace
{

    std::vector<uint8_t> readFile(const std::string &filePath)
    {
        std::vector<uint8_t> fileBytes;
        FILE *file = std::fopen(filePath.c_str(), "rb");
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

    struct Probe
    {
        uint32_t key;
        const char *label;
    };

    // The blocks every "which DLC does this save have?" decision in PKSE turns on.
    const Probe PROBES[] = {
        {0x4716C404, "SwSh  Galar Pokedex          (contrast: base-game dex)"},
        {0x3F936BA9, "SwSh  Isle of Armor Pokedex  (revision 1 trigger)"},
        {0x3C9366F0, "SwSh  Crown Tundra Pokedex   (revision 2 trigger)"},
        {0x158DA896, "SwSh  Isle of Armor raid dens"},
        {0x148DA703, "SwSh  Crown Tundra raid dens"},
        {0x100B93DA, "S/V   Kitakami+Blueberry raid dens (Teal Mask trigger)"},
        {0x66A33824, "S/V   Blueberry Points       (Indigo Disk trigger)"},
        {0x0926555A, "Z-A   SAVE_REVISION (u64)"},
    };

}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: blockprobe <save-file-or-dir>\n");
        return 2;
    }
    std::string savePath = argv[1];
    if (savePath.size() < 4 || savePath.substr(savePath.size() - 4) != "main")
    {
        if (!readFile(savePath).size())
        {
            savePath += "/main";
        }
    }

    std::vector<uint8_t> rawSaveBytes = readFile(savePath);
    if (rawSaveBytes.empty())
    {
        std::printf("could not read %s\n", savePath.c_str());
        return 2;
    }
    std::printf("save: %s  (%zu bytes)\n\n", savePath.c_str(), rawSaveBytes.size());

    const std::vector<Save::Block> blocks = Encryption::decrypt(rawSaveBytes.data(), rawSaveBytes.size());
    std::printf("%zu blocks parsed\n\n", blocks.size());

    for (const auto &probe : PROBES)
    {
        const Save::Block *probedBlock = nullptr;
        for (const auto &block : blocks)
        {
            if (block.key == probe.key)
            {
                probedBlock = &block;
                break;
            }
        }
        if (!probedBlock)
        {
            std::printf("  ABSENT   %08X  %s\n", probe.key, probe.label);
            continue;
        }
        // Count the non-zero bytes rather than just answering "any?". Magnitude is the
        // interesting part: a DLC dex that picks up a handful of bytes because it shares
        // species with the base dex means something very different from one that is
        // genuinely populated, and "has data" cannot tell those apart.
        size_t nonZeroByteCount = 0;
        for (uint8_t blockByte : probedBlock->data)
        {
            if (blockByte != 0)
            {
                ++nonZeroByteCount;
            }
        }
        const double nonZeroPercent = probedBlock->data.empty() ? 0.0 : (100.0 * static_cast<double>(nonZeroByteCount) / static_cast<double>(probedBlock->data.size()));
        std::printf("  PRESENT  %08X  size %6zu  nonzero %6zu (%5.1f%%)  %s\n", probe.key, probedBlock->data.size(), nonZeroByteCount, nonZeroPercent, probe.label);
    }

    //
    // The question this answers: what does the GAME put in an EMPTY party slot? If it is literal
    // zeros, then writing zeros back is correct. If it is an encrypted blank (a slot that only
    // decrypts to species 0), then writing zeros hands the game 344 bytes it will decrypt into
    // garbage -- a checksum failure, which the game renders as a Bad Egg.
    {
        // (key, entity size, slot stride, label, decrypt fn) -- SwSh and the two Gen 9 games.
        // S/V and Z-A share a party key and differ only in stride, so the game is told apart by
        // which one parses; here we just try the stride that divides the block evenly.
        struct PartyProbe
        {
            uint32_t key;
            size_t entitySize;
            size_t slotStride;
            const char *label;
            std::byte *(*decrypt)(std::span<const std::byte>);
        };
        const PartyProbe PARTIES[] = {
            {0x2985FE5D, 0x158, 0x158, "SwSh", &Encryption::decryptArray8SWSH},
            {0x3AA1A9AD, 0x158, 0x158, "S/V", &Encryption::decryptArray9SV},
            {0x3AA1A9AD, 0x158, 0x1E0, "Z-A", &Encryption::decryptArray9LZA},
        };

        for (const auto &partyProbe : PARTIES)
        {
            const Save::Block *party = nullptr;
            for (const auto &block : blocks)
            {
                if (block.key == partyProbe.key)
                {
                    party = &block;
                    break;
                }
            }
            if (!party)
            {
                continue;
            }
            // Only report the stride that actually tiles the block -- otherwise a Z-A read of an
            // S/V block would print six slots of nonsense and look like real evidence.
            if (party->data.size() < 6 * partyProbe.slotStride)
            {
                continue;
            }

            std::printf("\nparty block %08X as %s (stride 0x%zX):\n", partyProbe.key, partyProbe.label, partyProbe.slotStride);
            std::printf("  size %zu\n", party->data.size());
            for (size_t slotIndex = 0; slotIndex < 6 && slotIndex * partyProbe.slotStride + partyProbe.entitySize <= party->data.size(); ++slotIndex)
            {
                const uint8_t *slotBytes = party->data.data() + slotIndex * partyProbe.slotStride;
                size_t rawNonZeroCount = 0;
                for (size_t byteIndex = 0; byteIndex < partyProbe.entitySize; ++byteIndex)
                {
                    if (slotBytes[byteIndex] != 0)
                    {
                        ++rawNonZeroCount;
                    }
                }

                // Decrypt the slot to see what the game would see. Species lives at 0x08 of the
                // decrypted entity; a real empty slot decrypts to species 0.
                std::vector<std::byte> encryptedSlot(partyProbe.entitySize);
                std::memcpy(encryptedSlot.data(), slotBytes, partyProbe.entitySize);
                std::byte *decryptedSlot = partyProbe.decrypt(std::span<const std::byte>(encryptedSlot.data(), partyProbe.entitySize));
                const uint16_t species = static_cast<uint16_t>(static_cast<uint8_t>(decryptedSlot[0x08]) | (static_cast<uint8_t>(decryptedSlot[0x09]) << 8));
                delete[] decryptedSlot;

                std::printf("  slot %zu: raw nonzero %4zu / %zu   -> decrypts to species %u%s\n", slotIndex, rawNonZeroCount, partyProbe.entitySize, species, rawNonZeroCount == 0 ? "   <-- RAW ZEROS" : "");
            }
        }
    }

    // What does a ZEROED slot decrypt to? This decides whether "decrypts to species 0" is a safe
    // test for "this slot is already properly empty". If zeros also decrypt to species 0, then the
    // test cannot tell a real blank from a slot an older build corrupted, and a repair pass would
    // skip exactly the slots that need repairing.
    {
        std::vector<std::byte> zeroedSlot(0x158, std::byte{0});
        std::byte *decryptedSlot = Encryption::decryptArray8SWSH(std::span<const std::byte>(zeroedSlot.data(), 0x158));
        const uint16_t species = static_cast<uint16_t>(static_cast<uint8_t>(decryptedSlot[0x08]) | (static_cast<uint8_t>(decryptedSlot[0x09]) << 8));
        size_t decryptedNonZeroCount = 0;
        for (size_t byteIndex = 0; byteIndex < 0x158; ++byteIndex)
        {
            if (decryptedSlot[byteIndex] != std::byte{0})
            {
                ++decryptedNonZeroCount;
            }
        }
        delete[] decryptedSlot;
        std::printf("\nall-zero slot decrypts to: species %u  (%zu/344 decrypted bytes non-zero)\n", species, decryptedNonZeroCount);
    }

    // What each Trainer actually concludes, so the detection and the raw blocks are side by side.
    std::printf("\nwhat PKSE detects:\n");
    {
        std::vector<Save::Block> blocksCopy = blocks;
        Trainer::Trainer8SWSH trainer(std::move(blocksCopy));
        std::printf("  as Sword/Shield   -> revision %d (%s)\n", trainer.saveRevision, trainer.saveRevisionString.c_str());
    }

    // --repair-test: simulate a save already corrupted by the old memset(0) party writer, then
    // check the current writer heals it. Zero party slots 2..5, load, save, and look. Also saves a
    // SECOND time to prove the repaired slot is a fixed point -- if the writer did not recognise
    // its own blank as empty it would rewrite the party on every save forever.
    if (argc > 2 && std::string(argv[2]) == "--repair-test")
    {
        constexpr size_t slotSize = 0x158;
        std::vector<Save::Block> damagedBlocks = blocks;
        Save::Block *party = nullptr;
        for (auto &block : damagedBlocks)
        {
            if (block.key == 0x2985FE5D)
            {
                party = &block;
                break;
            }
        }
        if (!party)
        {
            std::printf("\nno party block; skipping repair test\n");
            return 0;
        }

        std::printf("\n--repair-test: zeroing party slots 2-5 (what the old writer produced)\n");
        for (size_t slotIndex = 2; slotIndex < 6 && (slotIndex + 1) * slotSize <= party->data.size(); ++slotIndex)
        {
            std::memset(&party->data[slotIndex * slotSize], 0, slotSize);
        }

        Trainer::Trainer8SWSH trainer(std::move(damagedBlocks));
        trainer.updatePartyBlock();
        std::vector<Save::Block> repairedBlocks = trainer.getBlocks();

        const Save::Block *repairedParty = nullptr;
        for (const auto &block : repairedBlocks)
        {
            if (block.key == 0x2985FE5D)
            {
                repairedParty = &block;
                break;
            }
        }

        bool allHealed = true;
        for (size_t slotIndex = 0; slotIndex < 6 && (slotIndex + 1) * slotSize <= repairedParty->data.size(); ++slotIndex)
        {
            const uint8_t *slotBytes = repairedParty->data.data() + slotIndex * slotSize;
            size_t rawNonZeroCount = 0;
            for (size_t byteIndex = 0; byteIndex < slotSize; ++byteIndex)
            {
                if (slotBytes[byteIndex] != 0)
                {
                    ++rawNonZeroCount;
                }
            }
            std::vector<std::byte> encryptedSlot(slotSize);
            std::memcpy(encryptedSlot.data(), slotBytes, slotSize);
            std::byte *decryptedSlot = Encryption::decryptArray8SWSH(std::span<const std::byte>(encryptedSlot.data(), slotSize));
            const uint16_t species = static_cast<uint16_t>(static_cast<uint8_t>(decryptedSlot[0x08]) | (static_cast<uint8_t>(decryptedSlot[0x09]) << 8));
            delete[] decryptedSlot;
            const bool stillZeroed = (slotIndex >= 2 && rawNonZeroCount == 0);
            if (stillZeroed)
            {
                allHealed = false;
            }
            std::printf("  slot %zu: raw nonzero %4zu / %zu  species %u%s\n", slotIndex, rawNonZeroCount, slotSize, species, stillZeroed ? "   <-- STILL ZEROS" : "");
        }

        // Idempotence: saving the repaired save again must change nothing.
        Trainer::Trainer8SWSH resavedTrainer(std::move(repairedBlocks));
        resavedTrainer.updatePartyBlock();
        const Save::Block *resavedParty = nullptr;
        for (const auto &block : resavedTrainer.getBlocks())
        {
            if (block.key == 0x2985FE5D)
            {
                resavedParty = &block;
                break;
            }
        }
        const bool stable = resavedParty && repairedParty && resavedParty->data == repairedParty->data;

        std::printf("  repaired: %s     second save identical: %s\n", allHealed ? "YES" : "NO", stable ? "YES" : "NO");
        return (allHealed && stable) ? 0 : 1;
    }
    return 0;
}
