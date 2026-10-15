/**
 * Tables + algorithm verified against PKHeX PokeCrypto.cs (BlockPosition / BlockPositionInvert,
 * CryptArray3). The 0x20-0x4F block is XORed per 32-bit word with (PID ^ OT_ID32)
 * and the four 12-byte substructures are shuffled by (PID % 24).
 */
#include "Encryption/Encryption3FRLG.h"

// The `if (n < 0x50) return` guards below prove every access sits in [0x00, 0x50) <= n, but GCC's
// loop-idiom pass rewrites the fixed 48-byte substructure copies into memcpy and then can't propagate
// the size guard to the new[] buffer -- it false-positives with bogus 2^63-byte ranges. Both warnings
// are the same false positive (accesses are guarded above); suppress them for this file.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wrestrict"
#endif

namespace Encryption
{

    namespace
    {
        // BLOCK_POSITION[sv*4 + blk] = the stored slot that holds canonical block `blk` (0=G,1=A,2=E,3=M)
        // for shuffle value sv (= PID % 24). Un-shuffle: canonical[blk] = stored[BLOCK_POSITION[sv*4+blk]].
        constexpr uint8_t BLOCK_POSITION[24 * 4] = {
            0,1,2,3,  0,1,3,2,  0,2,1,3,  0,3,1,2,  0,2,3,1,  0,3,2,1,   // sv 0-5   (G first)
            1,0,2,3,  1,0,3,2,  2,0,1,3,  3,0,1,2,  2,0,3,1,  3,0,2,1,   // sv 6-11  (A first)
            1,2,0,3,  1,3,0,2,  2,1,0,3,  3,1,0,2,  2,3,0,1,  3,2,0,1,   // sv 12-17 (E first)
            1,2,3,0,  1,3,2,0,  2,1,3,0,  3,1,2,0,  2,3,1,0,  3,2,1,0,   // sv 18-23 (M first)
        };
        // Inverse index: encrypt shuffles using BLOCK_POSITION[BLOCK_POSITION_INVERT[sv]*4 + n].
        constexpr uint8_t BLOCK_POSITION_INVERT[24] = {
            0,1,2,4,3,5,6,7,12,18,13,19,8,10,14,20,16,22,9,11,15,21,17,23,
        };

        inline uint32_t rd32(const std::byte *bytes)
        {
            return static_cast<uint32_t>(static_cast<uint8_t>(bytes[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 8) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[3])) << 24);
        }
        inline void wr32(std::byte *bytes, uint32_t value)
        {
            bytes[0] = static_cast<std::byte>(value);
            bytes[1] = static_cast<std::byte>(value >> 8);
            bytes[2] = static_cast<std::byte>(value >> 16);
            bytes[3] = static_cast<std::byte>(value >> 24);
        }
    }

    uint16_t checksum3FRLG(std::span<const std::byte> bytes)
    {
        uint16_t checksum = 0;
        for (size_t offset = 0x20; offset + 1 < 0x50 && offset + 1 < bytes.size(); offset += 2)
            checksum += static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset])) | (static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8);
        return checksum;
    }

    std::byte *decryptArray3FRLG(std::span<const std::byte> raw)
    {
        const size_t byteCount = raw.size();
        std::byte *out = new std::byte[byteCount]();
        for (size_t index = 0; index < byteCount; ++index)
            out[index] = raw[index];
        // too small to hold the data block
        if (byteCount < 0x50) return out;

        const uint32_t pidValue = rd32(out + 0x00);
        const uint32_t seed = pidValue ^ rd32(out + 0x04); // key = PID ^ OT_ID32
        const uint32_t shuffleIndex = pidValue % 24;

        for (size_t offset = 0x20; offset < 0x50; offset += 4) wr32(out + offset, rd32(out + offset) ^ seed);

        std::byte scratch[48];
        for (int index = 0; index < 48; ++index)
            scratch[index] = out[0x20 + index];
        for (int blockIndex = 0; blockIndex < 4; ++blockIndex)
        { // un-shuffle -> canonical
            const int source = BLOCK_POSITION[shuffleIndex * 4 + blockIndex] * 12;
            for (int index = 0; index < 12; ++index)
                out[0x20 + blockIndex * 12 + index] = scratch[source + index];
        }
        return out;
    }

    std::byte *encryptArray3FRLG(std::span<const std::byte> decryptedRecord)
    {
        const size_t byteCount = decryptedRecord.size();
        std::byte *out = new std::byte[byteCount]();
        for (size_t index = 0; index < byteCount; ++index)
            out[index] = decryptedRecord[index];
        if (byteCount < 0x50)
            return out;

        const uint32_t pidValue = rd32(out + 0x00);
        const uint32_t seed = pidValue ^ rd32(out + 0x04);
        const uint32_t inverseShuffleIndex = BLOCK_POSITION_INVERT[pidValue % 24];

        std::byte scratch[48];
        for (int index = 0; index < 48; ++index) scratch[index] = out[0x20 + index];
        for (int blockIndex = 0; blockIndex < 4; ++blockIndex)
        { // shuffle -> stored order
            const int source = BLOCK_POSITION[inverseShuffleIndex * 4 + blockIndex] * 12;
            for (int index = 0; index < 12; ++index)
                out[0x20 + blockIndex * 12 + index] = scratch[source + index];
        }
        for (size_t offset = 0x20; offset < 0x50; offset += 4) wr32(out + offset, rd32(out + offset) ^ seed);
        return out;
    }
}
