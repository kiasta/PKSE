/**
 * THEY ARE NOT INTERCHANGEABLE, and nothing in either save format will tell you if you pick the
 * wrong one: the write succeeds and the game rejects the file.
 *
 *   crc16ccitt   Gen 4 save blocks (SAV4)  AND  Gen 6 save blocks (BlockInfo6)
 *   crc16Invert  Gen 7 (3DS) save blocks   (BlockInfo7)
 *   crc16NoInvert                          (BlockInfo7b -- Let's Go; unused today)
 *
 * PKHeX Checksums.CRC16_CCITT / CRC16Invert / CRC16NoInvert.
 */
#ifndef UTILS_CRC16_H
#define UTILS_CRC16_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace Utils {

    /**
     * CRC16-CCITT, the bitwise form PKHeX uses for Gen 4 and Gen 6 blocks.
     * Not the table-driven CRC below -- different algorithm, different output.
     */
    inline uint16_t crc16ccitt(const uint8_t* data, size_t length) noexcept {
        uint8_t top = 0xFF, bot = 0xFF;
        for (size_t index = 0; index < length; ++index) {
            int tableIndex = data[index] ^ top;
            tableIndex ^= (tableIndex >> 4);
            top = static_cast<uint8_t>(bot ^ (tableIndex >> 3) ^ (tableIndex << 4));
            bot = static_cast<uint8_t>(tableIndex ^ (tableIndex << 5));
        }
        return static_cast<uint16_t>((top << 8) | bot);
    }

    namespace detail {
        /// CRC-16/ARC table (reversed polynomial 0xA001). Generated rather than transcribed from
        /// PKHeX's 256 literals -- fewer digits to get wrong -- and pinned by the asserts below.
        inline constexpr std::array<uint16_t, 256> CRC16_TABLE = [] {
            std::array<uint16_t, 256> lookupTable{};
            for (uint32_t index = 0; index < 256; ++index) {
                uint32_t remainder = index;
                for (int bitIndex = 0; bitIndex < 8; ++bitIndex)
                    remainder = (remainder & 1) ? ((remainder >> 1) ^ 0xA001u) : (remainder >> 1);
                lookupTable[index] = static_cast<uint16_t>(remainder);
            }
            return lookupTable;
        }();

        // The first entries of PKHeX's table. If the polynomial above is ever wrong, this is a
        // build failure rather than a save the game silently refuses.
        static_assert(CRC16_TABLE[0] == 0x0000 && CRC16_TABLE[1] == 0xC0C1 && CRC16_TABLE[2] == 0xC181 && CRC16_TABLE[3] == 0x0140 && CRC16_TABLE[4] == 0xC301 && CRC16_TABLE[255] == 0x4040, "CRC-16/ARC table does not match PKHeX's Checksums.crc16");

        inline uint16_t crc16(const uint8_t* bytes, size_t byteCount, uint16_t initial) noexcept {
            uint32_t checksum = initial;
            for (size_t index = 0; index < byteCount; ++index)
                checksum = CRC16_TABLE[static_cast<uint8_t>(bytes[index] ^ checksum)] ^ (checksum >> 8);
            return static_cast<uint16_t>(checksum);
        }
    }

    /// Gen 7 (3DS) save blocks. PKHeX Checksums.CRC16Invert.
    inline uint16_t crc16Invert(const uint8_t* bytes, size_t byteCount) noexcept {
        return static_cast<uint16_t>(~detail::crc16(bytes, byteCount, 0xFFFFu));
    }

    /// Let's Go save blocks. PKHeX Checksums.CRC16NoInvert.
    inline uint16_t crc16NoInvert(const uint8_t* bytes, size_t byteCount) noexcept {
        return detail::crc16(bytes, byteCount, 0);
    }
}

#endif  // UTILS_CRC16_H
