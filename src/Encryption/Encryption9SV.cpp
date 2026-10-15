#include <cstring>

#include "Encryption/Encryption.h"
#include "Encryption/Encryption9SV.h"
#include "Utils/HelperUtilities.h"

using namespace Utils;

namespace Encryption
{

    void shuffleArray9SV(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue)
    {

        const uint32_t index = shuffleValue * BLOCK_COUNT9_SV;
        constexpr uint32_t start = 8;

        std::memcpy(result.data(), data.data(), start);

        const auto end = start + (SIZE_BLOCK9_SV * BLOCK_COUNT9_SV);

        if (end < data.size())
        {
            const size_t remainingSize = data.size() - end;
            std::memcpy(result.data() + end, data.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT9_SV; block++)
        {
            const int srcBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (SIZE_BLOCK9_SV * srcBlockIndex);
            const size_t destOffset = start + (SIZE_BLOCK9_SV * block);

            std::memcpy(result.data() + destOffset, data.data() + srcOffset, SIZE_BLOCK9_SV);
        }
    }

    std::byte *decryptArray9SV(std::span<const std::byte> encryptedData)
    {

        const uint32_t personalityValue = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(encryptedData.data()));

        const uint32_t shuffleValue = (personalityValue >> 13) & 31;

        std::byte *decryptedData = new std::byte[encryptedData.size()];
        std::memcpy(decryptedData, encryptedData.data(), encryptedData.size());
        std::span<std::byte> mutableSpan(decryptedData, encryptedData.size());

        cryptPokemon(mutableSpan, personalityValue, SIZE_BLOCK9_SV, BLOCK_COUNT9_SV);

        std::byte *unshuffledData = new std::byte[encryptedData.size()];
        std::span<std::byte> resultSpan(unshuffledData, encryptedData.size());
        shuffleArray9SV(mutableSpan, resultSpan, shuffleValue);

        delete[] decryptedData;

        return unshuffledData;
    }

    std::byte *encryptArray9SV(std::span<const std::byte> decryptedData, uint32_t personalityValue)
    {

        const uint32_t shuffleValue = (personalityValue >> 13) & 31;

        std::byte *shuffledData = new std::byte[decryptedData.size()];
        std::span<std::byte> shuffledSpan(shuffledData, decryptedData.size());

        const uint32_t index = shuffleValue * BLOCK_COUNT9_SV;
        constexpr uint32_t start = 8;

        std::memcpy(shuffledSpan.data(), decryptedData.data(), start);

        const auto end = start + (SIZE_BLOCK9_SV * BLOCK_COUNT9_SV);

        if (end < decryptedData.size())
        {
            const size_t remainingSize = decryptedData.size() - end;
            std::memcpy(shuffledSpan.data() + end, decryptedData.data() + end, remainingSize);
        }

        for (uint32_t block = 0; block < BLOCK_COUNT9_SV; block++)
        {
            const int destBlockIndex = blockPosition[index + block];
            const size_t srcOffset = start + (SIZE_BLOCK9_SV * block);
            const size_t destOffset = start + (SIZE_BLOCK9_SV * destBlockIndex);

            std::memcpy(shuffledSpan.data() + destOffset, decryptedData.data() + srcOffset, SIZE_BLOCK9_SV);
        }

        cryptPokemon(shuffledSpan, personalityValue, SIZE_BLOCK9_SV, BLOCK_COUNT9_SV);

        return shuffledData;
    }
}
