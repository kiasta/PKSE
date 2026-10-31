#ifndef ENCRYPTION_ENCRYPTION9_LZA_H
#define ENCRYPTION_ENCRYPTION9_LZA_H

#include <cstdint>
#include <cstddef>
#include <span>

#include "Encryption/Encryption.h"

namespace Encryption {

    /// Growth, Attacks, EVs/Contest, Misc.
    constexpr size_t BLOCK_COUNT9_LZA = 4;

    constexpr size_t SIZE_BLOCK9_LZA = 0x50;

    constexpr size_t SIZE_STORED9_LZA = 8 + (BLOCK_COUNT9_LZA * SIZE_BLOCK9_LZA);

    constexpr size_t SIZE_PARTY9_LZA = SIZE_STORED9_LZA + 0x10;

    constexpr size_t GAP_BOX_SLOT9_LZA = 0x40;
    constexpr size_t GAP_PARTY_SLOT9_LZA = 0x88;

    constexpr size_t PARTY_SLOT_SIZE9_LZA = SIZE_PARTY9_LZA + GAP_PARTY_SLOT9_LZA;

    constexpr size_t BOX_SLOT_SIZE9_LZA = SIZE_PARTY9_LZA + GAP_BOX_SLOT9_LZA;

    /// First gap byte of a box or party slot: on a Mega Dimension (save revision != 0) save the game
    /// keeps it 1 in every slot, empty or not -- PKHeX SAV9ZA.WriteSlotBox / Party9a "mark as present".
    constexpr size_t PRESENT_FLAG_OFFSET9_LZA = SIZE_PARTY9_LZA;

    /// A second party-slot footer byte PKHeX's Party9a sets to 1 on every party write, any revision.
    constexpr size_t PARTY_FOOTER_OFFSET9_LZA = SIZE_PARTY9_LZA + 0x48;

    void shuffleArray9LZA(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue);

    std::byte* decryptArray9LZA(std::span<const std::byte> encryptedData);

    std::byte* encryptArray9LZA(std::span<const std::byte> decryptedData, uint32_t personalityValue);
}

#endif