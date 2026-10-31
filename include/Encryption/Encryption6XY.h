/**
 * LCG-XOR keyed on the EncryptionConstant at 0x00, four blocks shuffled by (EC >> 13) & 31, and
 * the party tail crypted with the same seed. Structurally the Gen 8 path with a smaller block --
 * 56 (0x38) rather than 80.
 *
 * The checksum is NOT the key here, unlike Gen 4/5, so the familiar order applies:
 * recalculateStats() -> refreshChecksum() -> encrypt.
 *
 * The shuffle value is (EC >> 13) & 31 -- the encryption constant at 0x00, not the PID -- and there
 * are four blocks of 0x38.
 */
#ifndef ENCRYPTION_ENCRYPTION6_XY_H
#define ENCRYPTION_ENCRYPTION6_XY_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "Encryption/Encryption.h"

namespace Encryption
{
    /// Growth, Attacks, EVs/Contest, Misc.
    constexpr size_t BLOCK_COUNT6_XY = 4;

    constexpr size_t SIZE_BLOCK6_XY = 0x38;

    constexpr size_t SIZE_STORED6_XY = 8 + (BLOCK_COUNT6_XY * SIZE_BLOCK6_XY);

    /// The stored record plus this group's battle-stat tail.
    constexpr size_t SIZE_PARTY6_XY = 0x104;

    /// True for either length this group's crypt accepts -- a group has exactly one party length.
    inline constexpr bool isSize6XY(size_t byteCount) noexcept
    {
        return byteCount == SIZE_STORED6_XY || byteCount == SIZE_PARTY6_XY;
    }

    /// Returns a new[]'d buffer the caller owns.
    std::byte *decryptArray6XY(std::span<const std::byte> encryptedData);

    /// Encrypts one record with `encryptionConstant` as the seed (the value at offset 0x00).
    /// Returns a new[]'d buffer the caller owns.
    std::byte *encryptArray6XY(std::span<const std::byte> decryptedData, uint32_t encryptionConstant);

    /// Same, reading the seed out of the buffer's own offset 0x00. The overload exists because
    /// every caller in the save layer passes exactly that -- an explicit constant is for the
    /// transfer path, where the record is being re-keyed on purpose.
    std::byte *encryptArray6XY(std::span<const std::byte> decryptedData);

    /// The canonical EMPTY slot, `size` bytes, owned by the library.
    ///
    /// An unused party or box slot in a real save does not hold raw zeros -- it holds
    /// encrypt(zeros), whose header is zero (the seed is 0, so the shuffle is the identity and
    /// the first masked word is 0) and whose body is the fixed seed-0 LCG pattern. Stamping zeros
    /// instead rewrites every empty slot in the file, which is what the unedited round trip
    /// catches. Same reasoning as SwSh's canonical encrypted blank, different constant.
    std::span<const std::byte> blankRecord6XY(size_t size);
}

#endif
