#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Utils/Logger.h"

namespace Utils
{
    uint16_t readUInt16LittleEndian(const uint8_t *pointer)
    {
        return static_cast<uint16_t>(pointer[0]) | (static_cast<uint16_t>(pointer[1]) << 8);
    }

    int32_t readInt32LittleEndian(const uint8_t *pointer)
    {
        return static_cast<uint32_t>(pointer[0]) | (static_cast<uint32_t>(pointer[1]) << 8) | (static_cast<uint32_t>(pointer[2]) << 16) | (static_cast<uint32_t>(pointer[3]) << 24);
    }

    uint32_t readUInt32LittleEndian(const uint8_t *pointer)
    {
        return static_cast<uint32_t>(pointer[0]) | (static_cast<uint32_t>(pointer[1]) << 8) | (static_cast<uint32_t>(pointer[2]) << 16) | (static_cast<uint32_t>(pointer[3]) << 24);
    }

    uint64_t readUInt64LittleEndian(const uint8_t *pointer)
    {
        return static_cast<uint64_t>(pointer[0]) | (static_cast<uint64_t>(pointer[1]) << 8) | (static_cast<uint64_t>(pointer[2]) << 16) | (static_cast<uint64_t>(pointer[3]) << 24) | (static_cast<uint64_t>(pointer[4]) << 32) | (static_cast<uint64_t>(pointer[5]) << 40) | (static_cast<uint64_t>(pointer[6]) << 48) | (static_cast<uint64_t>(pointer[7]) << 56);
    }

    void writeUInt16LittleEndian(uint8_t *pointer, uint16_t value)
    {
        pointer[0] = static_cast<uint8_t>(value & 0xFF);
        pointer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }

    void writeUInt32LittleEndian(uint8_t *pointer, uint32_t value)
    {
        pointer[0] = static_cast<uint8_t>(value & 0xFF);
        pointer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        pointer[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        pointer[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }

    void writeUInt64LittleEndian(uint8_t *pointer, uint64_t value)
    {
        for (int index = 0; index < 8; ++index)
            pointer[index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFF);
    }

    uint32_t rand32() noexcept
    {
        // Process-lifetime Mersenne Twister, seeded once from libnx's system tick counter
        // (armGetSystemTick, a u64). The high and low halves are folded together so all of the
        // tick's entropy reaches mt19937's 32-bit seed. mt19937 already yields the full 32-bit
        // range, so its output is returned directly. Used by the Pokemon creator for a pokemon's
        // PID / EncryptionConstant, where a unique-per-call value is all that's required.
        static std::mt19937 engine([]() -> std::mt19937::result_type
                                   {
#ifdef __SWITCH__
            const u64 tick = armGetSystemTick();
#else
            // Off-console builds have no system tick, and nothing there generates Pokemon, so this
            // only has to compile and be distinct per run.
            const u64 tick = static_cast<u64>(std::random_device{}());
#endif
            return static_cast<std::mt19937::result_type>(tick ^ (tick >> 32)); }());
        return static_cast<uint32_t>(engine());
    }

    // Title lookups go through the Switch's ns service, so they exist only on console. Deliberately
    // NOT stubbed for the host: a stub returning "UnknownGame" would let host code silently take a
    // wrong path, whereas an unresolved symbol says plainly that this belongs on the Switch.
#ifdef __SWITCH__
    std::string getTitleVersion(u64 titleId)
    {
        // NsApplicationControlData is ~0x24000 bytes: on the heap, as SystemIcons.cpp does, never
        // on the stack.
        auto controlData = std::make_unique<NsApplicationControlData>();
        u64 controlDataSize = 0;
        Result result = nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, controlData.get(), sizeof(NsApplicationControlData), &controlDataSize);
        if (R_FAILED(result))
        {
            char titleDescription[64];
            snprintf(titleDescription, sizeof(titleDescription), "title 0x%016lX, result 0x%x", titleId, result);
            logErrorToFile("Failed to get application control data", titleDescription);
            return "";
        }

        const NacpStruct *nacp = &controlData->nacp;

        // display_version contains the version string like "1.0.1" or "1.3.2"
        if (nacp->display_version[0] != '\0')
        {
            return std::string(nacp->display_version);
        }

        return "";
    }
#endif // __SWITCH__ (getTitleVersion)
}
