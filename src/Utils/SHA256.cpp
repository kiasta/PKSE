#include "Utils/SHA256.h"

namespace Utils
{
        // NOTE ON NAMING. The variables below keep the names the STANDARD gives them --
        // K, w, a..h -- rather than being spelled out. These files exist to be checked line by
        // line against FIPS 180-4, and a reader doing that check needs the two to read the same.
        // This is the one place in PKSE where a single-letter name is the clearer choice.
    // SHA-256 constants (first 32 bits of the fractional parts of the cube roots of the first 64 primes)
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Rotate right
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

// SHA-256 functions
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

    SHA256::SHA256() : bitCount(0), bufferSize(0)
    {
        // Initial hash values (first 32 bits of the fractional parts of the square roots of the first 8 primes)
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
    }

    void SHA256::transform()
    {
        uint32_t messageDigestWord[64];
        uint32_t a, b, c, d, e, f, g, h, t1, t2;

        // Prepare message schedule
        for (int index = 0; index < 16; index++)
        {
            messageDigestWord[index] = (buffer[index * 4] << 24) | (buffer[index * 4 + 1] << 16) | (buffer[index * 4 + 2] << 8) | (buffer[index * 4 + 3]);
        }

        for (int moveIndex = 16; moveIndex < 64; moveIndex++)
        {
            messageDigestWord[moveIndex] = SIG1(messageDigestWord[moveIndex - 2]) + messageDigestWord[moveIndex - 7] + SIG0(messageDigestWord[moveIndex - 15]) + messageDigestWord[moveIndex - 16];
        }

        a = state[0];
        b = state[1];
        c = state[2];
        d = state[3];
        e = state[4];
        f = state[5];
        g = state[6];
        h = state[7];

        // Main loop
        for (int moveIndex = 0; moveIndex < 64; moveIndex++)
        {
            t1 = h + EP1(e) + CH(e, f, g) + K[moveIndex] + messageDigestWord[moveIndex];
            t2 = EP0(a) + MAJ(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void SHA256::update(const uint8_t *data, size_t length)
    {
        for (size_t index = 0; index < length; index++)
        {
            buffer[bufferSize++] = data[index];

            if (bufferSize == PKSE_SHA256_BLOCK_SIZE)
            {
                transform();
                bitCount += 512;
                bufferSize = 0;
            }
        }
    }

    void SHA256::pad()
    {
        size_t bufferIndex = bufferSize;

        // Append the '1' bit
        buffer[bufferIndex++] = 0x80;

        // Pad with zeros until 56 bytes
        if (bufferSize < 56)
        {
            while (bufferIndex < 56)
            {
                buffer[bufferIndex++] = 0x00;
            }
        }
        else
        {
            while (bufferIndex < 64)
            {
                buffer[bufferIndex++] = 0x00;
            }
            transform();
            memset(buffer, 0, 56);
        }

        // Append length in bits as 64-bit big-endian
        uint64_t totalBits = bitCount + (bufferSize * 8);
        buffer[56] = totalBits >> 56;
        buffer[57] = totalBits >> 48;
        buffer[58] = totalBits >> 40;
        buffer[59] = totalBits >> 32;
        buffer[60] = totalBits >> 24;
        buffer[61] = totalBits >> 16;
        buffer[62] = totalBits >> 8;
        buffer[63] = totalBits;

        transform();
    }

    void SHA256::finalize(uint8_t *hash)
    {
        pad();

        // Produce the final hash value (big-endian)
        for (int index = 0; index < 8; index++)
        {
            hash[index * 4] = (state[index] >> 24) & 0xFF;
            hash[index * 4 + 1] = (state[index] >> 16) & 0xFF;
            hash[index * 4 + 2] = (state[index] >> 8) & 0xFF;
            hash[index * 4 + 3] = state[index] & 0xFF;
        }
    }
}