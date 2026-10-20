#include <vector>
#include <codecvt>
#include <locale>
#include <cstdio>
#include <span>

#include <sys/types.h>

#include "Globals.h"
#include "Utils/Logger.h"
#include "Utils/FileUtilities.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Utils/SHA256.h"
#include <cstring>

#include "Encryption/Encryption.h"
#include "Globals.h"
#include "Save/Block.h"

using namespace Utils;
using namespace Save;

namespace Encryption
{
    void cryptStaticXorpadBytes(std::vector<uint8_t> &data, size_t dataLength)
    {
        const size_t xpLength = sizeof(StaticXorpad) / sizeof(StaticXorpad[0]);
        const size_t size = xpLength - 1; // 0x7F, not 0x80

        for (size_t offset = 0; offset < dataLength; offset += size)
        {
            size_t chunkSize = (dataLength - offset < xpLength) ? (dataLength - offset) : xpLength;
            for (size_t index = 0; index < chunkSize; index++)
            {
                data[offset + index] ^= StaticXorpad[index];
            }
        }
    }

    std::vector<Block> decrypt(uint8_t *data, size_t dataLength)
    {
        size_t payloadLength = dataLength - SIZE_HASH_IN_BYTES;
        std::vector<uint8_t> payload(data, data + payloadLength);
        cryptStaticXorpadBytes(payload, payloadLength);
        return parseAllBlocks(payload.data(), payloadLength);
    }

    void computeHash(const uint8_t *data, size_t dataLength, uint8_t *hash)
    {
        SHA256 hasher;
        hasher.update(IntroHashBytes, sizeof(IntroHashBytes));
        hasher.update(data, dataLength);
        hasher.update(OutroHashBytes, sizeof(OutroHashBytes));
        hasher.finalize(hash);
    }

    std::vector<uint8_t> encrypt(const std::vector<Block> &blocks)
    {
        std::vector<uint8_t> payload = serializeAllBlocks(blocks);

        cryptStaticXorpadBytes(payload, payload.size());

        uint8_t hash[SIZE_HASH_IN_BYTES];
        computeHash(payload.data(), payload.size(), hash);

        payload.insert(payload.end(), hash, hash + SIZE_HASH_IN_BYTES);

        return payload;
    }

    void cryptArray(std::span<std::byte> data, uint32_t seed)
    {
        const size_t numU16 = data.size() / sizeof(uint16_t);

        for (size_t index = 0; index < numU16; ++index)
        {
            seed = (0x41C64E6D * seed) + 0x00006073;

            const uint16_t xorValue = static_cast<uint16_t>(seed >> 16);

            const size_t byteOffset = index * sizeof(uint16_t);
            uint16_t *valuePtr = reinterpret_cast<uint16_t *>(data.data() + byteOffset);

            *valuePtr ^= xorValue;
        }
    }

    void shuffleBlocks(std::span<const std::byte> data, std::span<std::byte> result, uint32_t shuffleValue, size_t blockSize, bool invert)
    {
        constexpr size_t start = 8; // encryption constant + checksum, never shuffled
        constexpr size_t blockCount = 4;
        const uint32_t index = shuffleValue * blockCount;
        const size_t end = start + (blockSize * blockCount);

        std::memcpy(result.data(), data.data(), start);
        if (end < data.size())
            std::memcpy(result.data() + end, data.data() + end, data.size() - end);

        for (uint32_t block = 0; block < blockCount; ++block)
        {
            const size_t other = start + (blockSize * blockPosition[index + block]);
            const size_t blockStart = start + (blockSize * block);
            // invert == false: reading a shuffled buffer into natural order (decrypt).
            // invert == true:  writing natural order out to shuffled positions (encrypt).
            const size_t source = invert ? blockStart : other;
            const size_t destination = invert ? other : blockStart;
            std::memcpy(result.data() + destination, data.data() + source, blockSize);
        }
    }

    void cryptPokemon(std::span<std::byte> data, uint32_t partyValue, size_t blockSize, size_t blockCount)
    {
        constexpr int start = 8; // Skip first 8 bytes (encryption constant + checksum)
        const int blocksEndValue = blockSize * blockCount;
        const int blocksEnd = start + blocksEndValue;

        auto blocksSpan = data.subspan(start, blocksEndValue);
        cryptArray(blocksSpan, partyValue);

        if (data.size() > static_cast<size_t>(blocksEnd))
        {
            auto partySpan = data.subspan(blocksEnd);
            cryptArray(partySpan, partyValue);
        }
    }
}