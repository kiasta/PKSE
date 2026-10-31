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
#ifndef ENCRYPTION_ENCRYPTION7_SM_H
#define ENCRYPTION_ENCRYPTION7_SM_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "Encryption/Encryption.h"

namespace Encryption
{
    /// Growth, Attacks, EVs/Contest, Misc.
    constexpr size_t BLOCK_COUNT7_SM = 4;

    constexpr size_t SIZE_BLOCK7_SM = 0x38;

    constexpr size_t SIZE_STORED7_SM = 8 + (BLOCK_COUNT7_SM * SIZE_BLOCK7_SM);

    /// The stored record plus this group's battle-stat tail.
    constexpr size_t SIZE_PARTY7_SM = 0x104;

    /// True for either length this group's crypt accepts -- a group has exactly one party length.
    inline constexpr bool isSize7SM(size_t byteCount) noexcept
    {
        return byteCount == SIZE_STORED7_SM || byteCount == SIZE_PARTY7_SM;
    }

    /// Returns a new[]'d buffer the caller owns.
    std::byte *decryptArray7SM(std::span<const std::byte> encryptedData);

    /// Encrypts one record with `encryptionConstant` as the seed (the value at offset 0x00).
    /// Returns a new[]'d buffer the caller owns.
    std::byte *encryptArray7SM(std::span<const std::byte> decryptedData, uint32_t encryptionConstant);

    /// Same, reading the seed out of the buffer's own offset 0x00. The overload exists because
    /// every caller in the save layer passes exactly that -- an explicit constant is for the
    /// transfer path, where the record is being re-keyed on purpose.
    std::byte *encryptArray7SM(std::span<const std::byte> decryptedData);

    /// The canonical EMPTY slot, `size` bytes, owned by the library.
    ///
    /// An unused party or box slot in a real save does not hold raw zeros -- it holds
    /// encrypt(zeros), whose header is zero (the seed is 0, so the shuffle is the identity and
    /// the first masked word is 0) and whose body is the fixed seed-0 LCG pattern. Stamping zeros
    /// instead rewrites every empty slot in the file, which is what the unedited round trip
    /// catches. Same reasoning as SwSh's canonical encrypted blank, different constant.
    std::span<const std::byte> blankRecord7SM(size_t size);
}

#endif
