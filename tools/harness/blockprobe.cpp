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

namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
}

namespace {

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

struct Probe { uint32_t key; const char* what; };

// The blocks every "which DLC does this save have?" decision in PKSE turns on.
const Probe PROBES[] = {
    { 0x4716C404, "SwSh  Galar Pokedex          (contrast: base-game dex)" },
    { 0x3F936BA9, "SwSh  Isle of Armor Pokedex  (revision 1 trigger)" },
    { 0x3C9366F0, "SwSh  Crown Tundra Pokedex   (revision 2 trigger)" },
    { 0x158DA896, "SwSh  Isle of Armor raid dens" },
    { 0x148DA703, "SwSh  Crown Tundra raid dens" },
    { 0x100B93DA, "S/V   Kitakami+Blueberry raid dens (Teal Mask trigger)" },
    { 0x66A33824, "S/V   Blueberry Points       (Indigo Disk trigger)" },
    { 0x0926555A, "Z-A   SAVE_REVISION (u64)" },
};

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: blockprobe <save-file-or-dir>\n");
        return 2;
    }
    std::string path = argv[1];
    if (path.size() < 4 || path.substr(path.size() - 4) != "main") {
        if (!readFile(path).size()) path += "/main";
    }

    std::vector<uint8_t> raw = readFile(path);
    if (raw.empty()) {
        std::printf("could not read %s\n", path.c_str());
        return 2;
    }
    std::printf("save: %s  (%zu bytes)\n\n", path.c_str(), raw.size());

    const std::vector<Save::Block> blocks = Encryption::decrypt(raw.data(), raw.size());
    std::printf("%zu blocks parsed\n\n", blocks.size());

    for (const auto& p : PROBES) {
        const Save::Block* found = nullptr;
        for (const auto& b : blocks) {
            if (b.key == p.key) { found = &b; break; }
        }
        if (!found) {
            std::printf("  ABSENT   %08X  %s\n", p.key, p.what);
            continue;
        }
        // Count the non-zero bytes rather than just answering "any?". Magnitude is the
        // interesting part: a DLC dex that picks up a handful of bytes because it shares
        // species with the base dex means something very different from one that is
        // genuinely populated, and "has data" cannot tell those apart.
        size_t nonZero = 0;
        for (uint8_t v : found->data) {
            if (v != 0) ++nonZero;
        }
        const double pct = found->data.empty() ? 0.0 : (100.0 * static_cast<double>(nonZero) / static_cast<double>(found->data.size()));
        std::printf("  PRESENT  %08X  size %6zu  nonzero %6zu (%5.1f%%)  %s\n", p.key, found->data.size(), nonZero, pct, p.what);
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
        struct PartyProbe {
            uint32_t key; size_t entity; size_t stride; const char* what;
            std::byte* (*decrypt)(std::span<const std::byte>);
        };
        const PartyProbe PARTIES[] = {
            { 0x2985FE5D, 0x158, 0x158, "SwSh",     &Encryption::decryptArray8SWSH },
            { 0x3AA1A9AD, 0x158, 0x158, "S/V",      &Encryption::decryptArray9SV   },
            { 0x3AA1A9AD, 0x158, 0x1E0, "Z-A",      &Encryption::decryptArray9LZA  },
        };

        for (const auto& pp : PARTIES) {
            const Save::Block* party = nullptr;
            for (const auto& b : blocks) {
                if (b.key == pp.key) { party = &b; break; }
            }
            if (!party) continue;
            // Only report the stride that actually tiles the block -- otherwise a Z-A read of an
            // S/V block would print six slots of nonsense and look like real evidence.
            if (party->data.size() < 6 * pp.stride) continue;

            std::printf("\nparty block %08X as %s (stride 0x%zX):\n", pp.key, pp.what, pp.stride);
            std::printf("  size %zu\n", party->data.size());
            for (size_t i = 0; i < 6 && i * pp.stride + pp.entity <= party->data.size(); ++i) {
                const uint8_t* p = party->data.data() + i * pp.stride;
                size_t nz = 0;
                for (size_t k = 0; k < pp.entity; ++k) if (p[k] != 0) ++nz;

                // Decrypt the slot to see what the game would see. Species lives at 0x08 of the
                // decrypted entity; a real empty slot decrypts to species 0.
                std::vector<std::byte> enc(pp.entity);
                std::memcpy(enc.data(), p, pp.entity);
                std::byte* dec = pp.decrypt(std::span<const std::byte>(enc.data(), pp.entity));
                const uint16_t species = static_cast<uint16_t>(static_cast<uint8_t>(dec[0x08]) | (static_cast<uint8_t>(dec[0x09]) << 8));
                delete[] dec;

                std::printf("  slot %zu: raw nonzero %4zu / %zu   -> decrypts to species %u%s\n", i, nz, pp.entity, species, nz == 0 ? "   <-- RAW ZEROS" : "");
            }
        }
    }

    // What does a ZEROED slot decrypt to? This decides whether "decrypts to species 0" is a safe
    // test for "this slot is already properly empty". If zeros also decrypt to species 0, then the
    // test cannot tell a real blank from a slot an older build corrupted, and a repair pass would
    // skip exactly the slots that need repairing.
    {
        std::vector<std::byte> zero(0x158, std::byte{0});
        std::byte* dec = Encryption::decryptArray8SWSH(std::span<const std::byte>(zero.data(), 0x158));
        const uint16_t sp = static_cast<uint16_t>(static_cast<uint8_t>(dec[0x08]) | (static_cast<uint8_t>(dec[0x09]) << 8));
        size_t nz = 0;
        for (size_t k = 0; k < 0x158; ++k) if (dec[k] != std::byte{0}) ++nz;
        delete[] dec;
        std::printf("\nall-zero slot decrypts to: species %u  (%zu/344 decrypted bytes non-zero)\n", sp, nz);
    }

    // What each Trainer actually concludes, so the detection and the raw blocks are side by side.
    std::printf("\nwhat PKSE detects:\n");
    {
        std::vector<Save::Block> copy = blocks;
        Trainer::Trainer8SWSH t(std::move(copy));
        std::printf("  as Sword/Shield   -> revision %d (%s)\n", t.saveRevision, t.saveRevisionString.c_str());
    }

    // --repair-test: simulate a save already corrupted by the old memset(0) party writer, then
    // check the current writer heals it. Zero party slots 2..5, load, save, and look. Also saves a
    // SECOND time to prove the repaired slot is a fixed point -- if the writer did not recognise
    // its own blank as empty it would rewrite the party on every save forever.
    if (argc > 2 && std::string(argv[2]) == "--repair-test") {
        constexpr size_t slotOffset = 0x158;
        std::vector<Save::Block> dmg = blocks;
        Save::Block* party = nullptr;
        for (auto& b : dmg) { if (b.key == 0x2985FE5D) { party = &b; break; } }
        if (!party) { std::printf("\nno party block; skipping repair test\n"); return 0; }

        std::printf("\n--repair-test: zeroing party slots 2-5 (what the old writer produced)\n");
        for (size_t i = 2; i < 6 && (i + 1) * slotOffset <= party->data.size(); ++i)
            std::memset(&party->data[i * slotOffset], 0, slotOffset);

        Trainer::Trainer8SWSH t(std::move(dmg));
        t.updatePartyBlock();
        std::vector<Save::Block> after = t.getBlocks();

        const Save::Block* fixed = nullptr;
        for (const auto& b : after) { if (b.key == 0x2985FE5D) { fixed = &b; break; } }

        bool allHealed = true;
        for (size_t i = 0; i < 6 && (i + 1) * slotOffset <= fixed->data.size(); ++i) {
            const uint8_t* p = fixed->data.data() + i * slotOffset;
            size_t nz = 0;
            for (size_t k = 0; k < slotOffset; ++k) if (p[k] != 0) ++nz;
            std::vector<std::byte> enc(slotOffset);
            std::memcpy(enc.data(), p, slotOffset);
            std::byte* dec = Encryption::decryptArray8SWSH(std::span<const std::byte>(enc.data(), slotOffset));
            const uint16_t sp = static_cast<uint16_t>(static_cast<uint8_t>(dec[0x08]) | (static_cast<uint8_t>(dec[0x09]) << 8));
            delete[] dec;
            const bool bad = (i >= 2 && nz == 0);
            if (bad) allHealed = false;
            std::printf("  slot %zu: raw nonzero %4zu / %zu  species %u%s\n", i, nz, slotOffset, sp, bad ? "   <-- STILL ZEROS" : "");
        }

        // Idempotence: saving the repaired save again must change nothing.
        Trainer::Trainer8SWSH t2(std::move(after));
        t2.updatePartyBlock();
        const Save::Block* twice = nullptr;
        for (const auto& b : t2.getBlocks()) { if (b.key == 0x2985FE5D) { twice = &b; break; } }
        const bool stable = twice && fixed && twice->data == fixed->data;

        std::printf("  repaired: %s     second save identical: %s\n", allHealed ? "YES" : "NO", stable ? "YES" : "NO");
        return (allHealed && stable) ? 0 : 1;
    }
    return 0;
}
