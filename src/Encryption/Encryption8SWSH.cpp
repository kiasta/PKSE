#include <cstring>

#include "Encryption/Encryption.h"
#include "Encryption/Encryption8SWSH.h"
#include "Utils/HelperUtilities.h"

using namespace Utils;

namespace Encryption
{
    void shuffleArray8SWSH(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue)
    {

        const uint32_t index = shuffleValue * BLOCK_COUNT8_SWSH;
        constexpr uint32_t start = 8;

        std::memcpy(result.data(), data.data(), start);

        const auto end = start + (SIZE_BLOCK8_SWSH * BLOCK_COUNT8_SWSH);

        if (end < data.size())
        {
            const size_t remainingSize = data.size() - end;
            std::memcpy(result.data() + end, data.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT8_SWSH; block++)
        {
            const int srcBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (SIZE_BLOCK8_SWSH * srcBlockIndex);
            const size_t destOffset = start + (SIZE_BLOCK8_SWSH * block);

            std::memcpy(result.data() + destOffset, data.data() + srcOffset, SIZE_BLOCK8_SWSH);
        }
    }

    std::byte *decryptArray8SWSH(std::span<const std::byte> encryptedData)
    {

        const uint32_t encryptionConstantValue = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(encryptedData.data()));

        const uint32_t shuffleIndex = (encryptionConstantValue >> 13) & 31;

        std::byte *decryptedData = new std::byte[encryptedData.size()];
        std::memcpy(decryptedData, encryptedData.data(), encryptedData.size());
        std::span<std::byte> mutableSpan(decryptedData, encryptedData.size());

        cryptPokemon(mutableSpan, encryptionConstantValue, SIZE_BLOCK8_SWSH, BLOCK_COUNT8_SWSH);

        std::byte *unshuffledData = new std::byte[encryptedData.size()];
        std::span<std::byte> resultSpan(unshuffledData, encryptedData.size());
        shuffleArray8SWSH(mutableSpan, resultSpan, shuffleIndex);

        delete[] decryptedData;

        return unshuffledData;
    }

    std::byte *encryptArray8SWSH(std::span<const std::byte> decryptedData, uint32_t personalityValue)
    {

        const uint32_t shuffleValue = (personalityValue >> 13) & 31;

        std::byte *shuffledData = new std::byte[decryptedData.size()];
        std::span<std::byte> shuffledSpan(shuffledData, decryptedData.size());

        const uint32_t index = shuffleValue * BLOCK_COUNT8_SWSH;
        constexpr uint32_t start = 8;

        std::memcpy(shuffledSpan.data(), decryptedData.data(), start);

        const auto end = start + (SIZE_BLOCK8_SWSH * BLOCK_COUNT8_SWSH);

        if (end < decryptedData.size())
        {
            const size_t remainingSize = decryptedData.size() - end;
            std::memcpy(shuffledSpan.data() + end, decryptedData.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT8_SWSH; block++)
        {
            const int destBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (SIZE_BLOCK8_SWSH * block);
            const size_t destOffset = start + (SIZE_BLOCK8_SWSH * destBlockIndex);

            std::memcpy(shuffledSpan.data() + destOffset, decryptedData.data() + srcOffset, SIZE_BLOCK8_SWSH);
        }

        cryptPokemon(shuffledSpan, personalityValue, SIZE_BLOCK8_SWSH, BLOCK_COUNT8_SWSH);

        return shuffledData;
    }
}
