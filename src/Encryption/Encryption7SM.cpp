#include <vector>
#include <cstring>

#include "Encryption/Encryption7SM.h"
#include "Utils/HelperUtilities.h"

using namespace Utils;

namespace Encryption
{
    std::byte *decryptArray7SM(std::span<const std::byte> encryptedData)
    {
        const size_t byteCount = encryptedData.size();
        const uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(encryptedData.data()));
        const uint32_t shuffleIndex = (encryptionConstant >> 13) & 31;

        std::byte *scratch = new std::byte[byteCount];
        std::memcpy(scratch, encryptedData.data(), byteCount);
        // One seed for both halves -- the Gen 6+ rule, unlike Gen 4/5.
        cryptPokemon(std::span<std::byte>(scratch, byteCount), encryptionConstant, SIZE_BLOCK7_SM, BLOCK_COUNT7_SM);

        std::byte *out = new std::byte[byteCount];
        shuffleBlocks(std::span<const std::byte>(scratch, byteCount), std::span<std::byte>(out, byteCount), shuffleIndex, SIZE_BLOCK7_SM, /*invert=*/false);
        delete[] scratch;
        return out;
    }

    std::byte *encryptArray7SM(std::span<const std::byte> decryptedData, uint32_t encryptionConstant)
    {
        const size_t byteCount = decryptedData.size();
        const uint32_t shuffleIndex = (encryptionConstant >> 13) & 31;

        std::byte *out = new std::byte[byteCount];
        shuffleBlocks(decryptedData, std::span<std::byte>(out, byteCount), shuffleIndex, SIZE_BLOCK7_SM, /*invert=*/true);
        cryptPokemon(std::span<std::byte>(out, byteCount), encryptionConstant, SIZE_BLOCK7_SM, BLOCK_COUNT7_SM);
        return out;
    }

    std::byte *encryptArray7SM(std::span<const std::byte> decryptedData)
    {
        return encryptArray7SM(decryptedData, static_cast<uint32_t>(decryptedData[0]) | (static_cast<uint32_t>(decryptedData[1]) << 8) | (static_cast<uint32_t>(decryptedData[2]) << 16) | (static_cast<uint32_t>(decryptedData[3]) << 24));
    }

    std::span<const std::byte> blankRecord7SM(size_t size)
    {
        static std::vector<std::byte> cache[2];
        const int sizeClass = size == SIZE_STORED7_SM ? 0 : size == SIZE_PARTY7_SM ? 1 : -1;
        if (sizeClass < 0)
            return {};
        if (cache[sizeClass].empty())
        {
            std::vector<std::byte> zero(size, std::byte{0});
            std::byte *encryptedRecord = encryptArray7SM(zero, 0);
            cache[sizeClass].assign(encryptedRecord, encryptedRecord + size);
            delete[] encryptedRecord;
        }
        return cache[sizeClass];
    }
}
