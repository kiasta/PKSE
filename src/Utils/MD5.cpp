/**
 * Standard, self-contained MD5. See MD5.h for usage and known-answer vectors.
 * Used by BDSP save writing (whole-file MD5 validation); must be byte-exact.
 */

#include "Utils/MD5.h"

#include <cstring>

namespace Utils
{
    namespace
    {
        // NOTE ON NAMING. The variables below keep the names the STANDARD gives them --
        // A, B, C, D, F, g, M, S and K -- rather than being spelled out. These files exist to be checked line by
        // line against RFC 1321, and a reader doing that check needs the two to read the same.
        // This is the one place in PKSE where a single-letter name is the clearer choice.
        // Per-round left-rotate amounts (RFC 1321, section 3.4).
        constexpr uint32_t S[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

        // Precomputed constants K[i] = floor(abs(sin(i + 1)) * 2^32).
        constexpr uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
            0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
            0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
            0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
            0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
            0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
            0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
            0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
            0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

        inline uint32_t leftRotate(uint32_t value, uint32_t bits) noexcept
        {
            return (value << bits) | (value >> (32 - bits));
        }

        // Processes exactly one 64-byte block, updating the four state words.
        void md5Block(const uint8_t *block, uint32_t state[4]) noexcept
        {
            // Decode the block into sixteen little-endian 32-bit words.
            uint32_t M[16];
            for (int index = 0; index < 16; index++)
            {
                M[index] = static_cast<uint32_t>(block[index * 4]) | (static_cast<uint32_t>(block[index * 4 + 1]) << 8) | (static_cast<uint32_t>(block[index * 4 + 2]) << 16) | (static_cast<uint32_t>(block[index * 4 + 3]) << 24);
            }

            uint32_t A = state[0];
            uint32_t B = state[1];
            uint32_t C = state[2];
            uint32_t D = state[3];

            for (int moveIndex = 0; moveIndex < 64; moveIndex++)
            {
                uint32_t F;
                int g;

                if (moveIndex < 16)
                {
                    F = (B & C) | (~B & D);
                    g = moveIndex;
                }
                else if (moveIndex < 32)
                {
                    F = (D & B) | (~D & C);
                    g = (5 * moveIndex + 1) & 15;
                }
                else if (moveIndex < 48)
                {
                    F = B ^ C ^ D;
                    g = (3 * moveIndex + 5) & 15;
                }
                else
                {
                    F = C ^ (B | ~D);
                    g = (7 * moveIndex) & 15;
                }

                F = F + A + K[moveIndex] + M[g];
                A = D;
                D = C;
                C = B;
                B = B + leftRotate(F, S[moveIndex]);
            }

            state[0] += A;
            state[1] += B;
            state[2] += C;
            state[3] += D;
        }
    } // anonymous namespace

    void md5(const uint8_t *data, size_t length, uint8_t out[16])
    {
        // Standard MD5 initialization vector.
        uint32_t state[4] = {
            0x67452301u,
            0xefcdab89u,
            0x98badcfeu,
            0x10325476u};

        // Process every complete 64-byte block straight from the input.
        const size_t fullBlocks = length / 64;
        for (size_t index = 0; index < fullBlocks; index++)
        {
            md5Block(data + index * 64, state);
        }

        // Assemble the final padded block(s): the leftover bytes, a single 0x80
        // terminator, zero padding, then the original length in bits (64-bit LE).
        const size_t remaining = length % 64; // bytes not yet processed (0..63)

        uint8_t buffer[128];
        std::memset(buffer, 0, sizeof(buffer));
        if (remaining > 0)
        {
            std::memcpy(buffer, data + fullBlocks * 64, remaining);
        }
        buffer[remaining] = 0x80;

        // If the leftover + terminator leave room for the 8-byte length, one block
        // suffices (remaining <= 55); otherwise the length spills into a second block.
        const size_t paddedSize = (remaining <= 55) ? 64 : 128;

        const uint64_t bitLen = static_cast<uint64_t>(length) * 8;
        for (int index = 0; index < 8; index++)
        {
            buffer[paddedSize - 8 + index] = static_cast<uint8_t>((bitLen >> (8 * index)) & 0xFF);
        }

        md5Block(buffer, state);
        if (paddedSize == 128)
        {
            md5Block(buffer + 64, state);
        }

        // Emit each state word in little-endian order (conventional MD5 output).
        for (int moveIndex = 0; moveIndex < 4; moveIndex++)
        {
            out[moveIndex * 4] = static_cast<uint8_t>(state[moveIndex] & 0xFF);
            out[moveIndex * 4 + 1] = static_cast<uint8_t>((state[moveIndex] >> 8) & 0xFF);
            out[moveIndex * 4 + 2] = static_cast<uint8_t>((state[moveIndex] >> 16) & 0xFF);
            out[moveIndex * 4 + 3] = static_cast<uint8_t>((state[moveIndex] >> 24) & 0xFF);
        }
    }
}
