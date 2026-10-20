#include <cstring>

#include "Utils/HelperUtilities.h"
#include "Encryption/Encryption.h"
#include "Encryption/Encryption7LGPE.h"

using namespace Utils;

namespace Encryption
{

    void cryptArray7LGPE(std::span<std::byte> data, uint32_t seed)
    {

        const size_t numUInt16 = data.size() / sizeof(uint16_t);

        for (size_t index = 0; index < numUInt16; ++index)
        {
            seed = (0x41C64E6D * seed) + 0x00006073;

            const uint16_t xorValue = static_cast<uint16_t>(seed >> 16);

            const size_t byteOffset = index * sizeof(uint16_t);
            uint16_t *valuePointer = reinterpret_cast<uint16_t *>(data.data() + byteOffset);

            *valuePointer ^= xorValue;
        }
    }

    void cryptPokemon7LGPE(std::span<std::byte> data, uint32_t personalityValue, size_t blockSize, size_t blockCount)
    {

        constexpr size_t start = 8;              // header (EC + checksum) is not crypted
        const size_t stored = SIZE_STORED7_LGPE; // 0xE8: end of the shuffled/checksummed body

        // PKHeX Decrypt67 crypts the body [8, 0xE8) and the party tail [0xE8, end) as TWO SEPARATE
        // CryptArray passes, each RESTARTING the LCG at the EC -- NOT one continuous pass. A single pass
        // gives the tail (Level / battle stats / CP at 0xEC-0xFF) the wrong keystream: it round-trips
        // inside PKSE but the game decrypts it to garbage (absurd stats / CP). Split at the stored-size
        // boundary so both passes key from the EC. (blockSize/blockCount are unused here.)
        (void)blockSize;
        (void)blockCount;
        if (data.size() > start)
        {
            const size_t bodyEnd = std::min(data.size(), stored);
            cryptArray7LGPE(data.subspan(start, bodyEnd - start), personalityValue);
        }
        if (data.size() > stored)
        {
            cryptArray7LGPE(data.subspan(stored, data.size() - stored), personalityValue);
        }
    }

    void cryptPokemon7LGPE(std::span<std::byte> data, uint32_t personalityValue, size_t blockSize)
    {
        cryptPokemon7LGPE(data, personalityValue, blockSize, BLOCK_COUNT7_LGPE);
    }

    void shuffleArray7LGPE(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue, size_t blockSize)
    {

        const uint32_t index = shuffleValue * BLOCK_COUNT7_LGPE;
        constexpr uint32_t start = 8;

        std::memcpy(result.data(), data.data(), start);

        const auto end = start + (blockSize * BLOCK_COUNT7_LGPE);

        if (end < data.size())
        {
            const size_t remainingSize = data.size() - end;
            std::memcpy(result.data() + end, data.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT7_LGPE; block++)
        {
            const int srcBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (blockSize * srcBlockIndex);
            const size_t destOffset = start + (blockSize * block);

            std::memcpy(result.data() + destOffset, data.data() + srcOffset, blockSize);
        }
    }

    std::byte *decryptArray7LGPE(std::span<const std::byte> encryptedData)
    {

        const uint32_t personalityValue = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(encryptedData.data()));

        const uint32_t shuffleValue = (personalityValue >> 13) & 31;

        std::byte *decryptedData = new std::byte[encryptedData.size()];
        std::memcpy(decryptedData, encryptedData.data(), encryptedData.size());
        std::span<std::byte> mutableSpan(decryptedData, encryptedData.size());

        cryptPokemon7LGPE(mutableSpan, personalityValue, SIZE_BLOCK7_LGPE);

        std::byte *unshuffledData = new std::byte[encryptedData.size()];
        std::span<std::byte> resultSpan(unshuffledData, encryptedData.size());
        shuffleArray7LGPE(mutableSpan, resultSpan, shuffleValue, SIZE_BLOCK7_LGPE);

        delete[] decryptedData;

        return unshuffledData;
    }

    std::byte *encryptArray7LGPE(std::span<const std::byte> decryptedData, uint32_t personalityValue)
    {

        const uint32_t shuffleValue = (personalityValue >> 13) & 31;

        std::byte *shuffledData = new std::byte[decryptedData.size()];
        std::span<std::byte> shuffledSpan(shuffledData, decryptedData.size());

        const uint32_t index = shuffleValue * BLOCK_COUNT7_LGPE;
        constexpr uint32_t start = 8;

        std::memcpy(shuffledSpan.data(), decryptedData.data(), start);

        const auto end = start + (SIZE_BLOCK7_LGPE * BLOCK_COUNT7_LGPE);

        if (end < decryptedData.size())
        {
            const size_t remainingSize = decryptedData.size() - end;
            std::memcpy(shuffledSpan.data() + end, decryptedData.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT7_LGPE; block++)
        {
            const int destBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (SIZE_BLOCK7_LGPE * block);
            const size_t destOffset = start + (SIZE_BLOCK7_LGPE * destBlockIndex);

            std::memcpy(shuffledSpan.data() + destOffset, decryptedData.data() + srcOffset, SIZE_BLOCK7_LGPE);
        }

        cryptPokemon7LGPE(shuffledSpan, personalityValue, SIZE_BLOCK7_LGPE);

        return shuffledData;
    }
}
