#ifndef ENCRYPTION_ENCRYPTION7_LGPE_H
#define ENCRYPTION_ENCRYPTION7_LGPE_H

#include <cstdint>
#include <cstddef>
#include <span>

#include "Encryption/Encryption.h"

namespace Encryption {

    /// Growth, Attacks, EVs/Contest, Misc.
    constexpr size_t BLOCK_COUNT7_LGPE = 4;
    constexpr size_t SIZE_BLOCK7_LGPE = 0x38;

    constexpr size_t SIZE_STORED7_LGPE = 0xE8;

    constexpr size_t SIZE_PARTY7_LGPE = 0x104;

    constexpr size_t SIZE_POKEMON7_LGPE = 260;

    void cryptArray7LGPE(std::span<std::byte> data, uint32_t seed);

    void cryptPokemon7LGPE(std::span<std::byte> data, uint32_t personalityValue, size_t blockSize, size_t blockCount);

    void cryptPokemon7LGPE(std::span<std::byte> data, uint32_t personalityValue, size_t blockSize);

    void shuffleArray7LGPE(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue, size_t blockSize);

    std::byte* decryptArray7LGPE(std::span<const std::byte> encryptedData);

    std::byte* encryptArray7LGPE(std::span<const std::byte> decryptedData, uint32_t personalityValue);
}

#endif
