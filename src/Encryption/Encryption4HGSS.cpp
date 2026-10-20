#include <vector>
#include <cstring>

#include "Encryption/Encryption4HGSS.h"
#include "Utils/HelperUtilities.h"

using namespace Utils;

namespace Encryption
{
    namespace
    {
        /// The two seeds one of this group's records is crypted with, read out of its header.
        struct Seeds4HGSS
        {
            uint32_t pidValue;
            uint32_t checksum;
        };

        Seeds4HGSS readSeeds(const std::byte *recordBytes) noexcept
        {
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(recordBytes);
            return {readUInt32LittleEndian(bytes), readUInt16LittleEndian(bytes + 6)};
        }

        /// XOR pass over both regions, with the DIFFERENT seeds this generation uses. Symmetric,
        /// so the same call serves encrypt and decrypt.
        void crypt4HGSS(std::span<std::byte> data, const Seeds4HGSS &seeds)
        {
            auto body = data.subspan(8, SIZE_BLOCK4_HGSS * BLOCK_COUNT4_HGSS);
            cryptArray(body, seeds.checksum); // <-- CHECKSUM, not PID
            if (data.size() > SIZE_STORED4_HGSS)
                cryptArray(data.subspan(SIZE_STORED4_HGSS), seeds.pidValue); // the tail keeps the PID
        }
    }

    std::byte *decryptArray4HGSS(std::span<const std::byte> encryptedData)
    {
        const size_t byteCount = encryptedData.size();
        const Seeds4HGSS seeds = readSeeds(encryptedData.data());
        const uint32_t shuffleIndex = (seeds.pidValue >> 13) & 31;

        std::byte *scratch = new std::byte[byteCount];
        std::memcpy(scratch, encryptedData.data(), byteCount);
        crypt4HGSS(std::span<std::byte>(scratch, byteCount), seeds);

        std::byte *out = new std::byte[byteCount];
        shuffleBlocks(std::span<const std::byte>(scratch, byteCount), std::span<std::byte>(out, byteCount), shuffleIndex, SIZE_BLOCK4_HGSS, /*invert=*/false);
        delete[] scratch;
        return out;
    }

    std::byte *encryptArray4HGSS(std::span<const std::byte> decryptedData)
    {
        const size_t byteCount = decryptedData.size();
        // Both seeds come from the DECRYPTED buffer, and the checksum half of that is only right
        // if the caller refreshed it first -- see the header.
        const Seeds4HGSS seeds = readSeeds(decryptedData.data());
        const uint32_t shuffleIndex = (seeds.pidValue >> 13) & 31;

        std::byte *out = new std::byte[byteCount];
        shuffleBlocks(decryptedData, std::span<std::byte>(out, byteCount), shuffleIndex, SIZE_BLOCK4_HGSS, /*invert=*/true);
        crypt4HGSS(std::span<std::byte>(out, byteCount), seeds);
        return out;
    }

    std::span<const std::byte> blankRecord4HGSS(size_t size)
    {
        // Built once per size on first use. Deriving it rather than hardcoding the pattern keeps
        // it correct by construction if the crypt ever changes.
        static std::vector<std::byte> cache[2];
        const int sizeClass = size == SIZE_STORED4_HGSS ? 0 : size == SIZE_PARTY4_HGSS ? 1 : -1;
        if (sizeClass < 0)
            return {};
        if (cache[sizeClass].empty())
        {
            std::vector<std::byte> zero(size, std::byte{0});
            std::byte *encryptedRecord = encryptArray4HGSS(zero);
            cache[sizeClass].assign(encryptedRecord, encryptedRecord + size);
            delete[] encryptedRecord;
        }
        return cache[sizeClass];
    }
}
