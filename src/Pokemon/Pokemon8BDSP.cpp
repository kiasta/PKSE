#include <cstdio>
#include <algorithm>

#include "Pokemon/Pokemon8BDSP.h"
#include "Pokemon/PersonalInfo8BDSP.h"
#include "Pokemon/Experience.h"

namespace Names
{
    // Species names are generation-stable, so there is one table and this is the whole of
    // the dependency on it. Forward-declared rather than included: the name table is
    // declared in Trainer/Trainer.h, and a Pokemon-layer source must not include the
    // trainer layer to reach it.
    const char *getSpeciesName(uint16_t speciesId);
}


namespace Pokemon
{
    // Forward declarations for external helper functions
    // These are defined in other files and provide species/item/nature name lookups.
    // BDSP shares the Gen 8/9 name + base-stat tables with Sword/Shield.

    const char *Pokemon8BDSP::species() const noexcept
    {
        return Names::getSpeciesName(speciesID());
    }

    uint8_t Pokemon8BDSP::gender() const noexcept
    {
        // Gen 8 stores gender explicitly (0 male, 1 female, 2 genderless) in bits 2-3 of byte 0x22.
        // That value already encodes a fixed-gender species, and it is independent of the PID, so it
        // survives a shiny or PID edit.
        return (static_cast<uint8_t>(data[0x22]) >> 2) & 0x03;
    }

    uint8_t Pokemon8BDSP::baseHP() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).hp;
    }

    uint8_t Pokemon8BDSP::baseATK() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).atk;
    }

    uint8_t Pokemon8BDSP::baseDEF() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).def;
    }

    uint8_t Pokemon8BDSP::baseSPE() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).spe;
    }

    uint8_t Pokemon8BDSP::baseSPA() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).spa;
    }

    uint8_t Pokemon8BDSP::baseSPD() const noexcept
    {
        return getPersonalInfo8BDSP(speciesID(), form()).spd;
    }

    int Pokemon8BDSP::getNatureModifier(int statIndex) const noexcept
    {

        uint8_t nature = statNature();

        // Stats: 0=ATK, 1=DEF, 2=SPE, 3=SPA, 4=SPD
        static const int8_t natureTable[25][5] = {
            {1, 1, 1, 1, 1}, // 0: Hardy (neutral)
            {2, 0, 1, 1, 1}, // 1: Lonely (+Atk, -Def)
            {2, 1, 0, 1, 1}, // 2: Brave (+Atk, -Spe)
            {2, 1, 1, 0, 1}, // 3: Adamant (+Atk, -SpA)
            {2, 1, 1, 1, 0}, // 4: Naughty (+Atk, -SpD)
            {0, 2, 1, 1, 1}, // 5: Bold (-Atk, +Def)
            {1, 1, 1, 1, 1}, // 6: Docile (neutral)
            {1, 2, 0, 1, 1}, // 7: Relaxed (+Def, -Spe)
            {1, 2, 1, 0, 1}, // 8: Impish (+Def, -SpA)
            {1, 2, 1, 1, 0}, // 9: Lax (+Def, -SpD)
            {0, 1, 2, 1, 1}, // 10: Timid (-Atk, +Spe)
            {1, 0, 2, 1, 1}, // 11: Hasty (-Def, +Spe)
            {1, 1, 1, 1, 1}, // 12: Serious (neutral)
            {1, 1, 2, 0, 1}, // 13: Jolly (+Spe, -SpA)
            {1, 1, 2, 1, 0}, // 14: Naive (+Spe, -SpD)
            {0, 1, 1, 2, 1}, // 15: Modest (-Atk, +SpA)
            {1, 0, 1, 2, 1}, // 16: Mild (-Def, +SpA)
            {1, 1, 0, 2, 1}, // 17: Quiet (+SpA, -Spe)
            {1, 1, 1, 1, 1}, // 18: Bashful (neutral)
            {1, 1, 1, 2, 0}, // 19: Rash (+SpA, -SpD)
            {0, 1, 1, 1, 2}, // 20: Calm (-Atk, +SpD)
            {1, 0, 1, 1, 2}, // 21: Gentle (-Def, +SpD)
            {1, 1, 0, 1, 2}, // 22: Sassy (+SpD, -Spe)
            {1, 1, 1, 0, 2}, // 23: Careful (+SpD, -SpA)
            {1, 1, 1, 1, 1}, // 24: Quirky (neutral)
        };

        if (nature >= 25 || statIndex < 0 || statIndex >= 5)
        {
            return 100;
        }

        int modifier = natureTable[nature][statIndex];
        return modifier == 0 ? 90 : (modifier == 2 ? 110 : 100);
    }

    void Pokemon8BDSP::recalculateStats() noexcept
    {

        uint8_t levelValue = level();
        if (levelValue == 0 || levelValue > 100)
        {
            return;
        }

        int hitPoints = ((2 * baseHP() + effectiveIV(HT_HP) + (evHP() / 4)) * levelValue) / 100 + levelValue + 10;
        const uint16_t oldMax = readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x14A)); // before it is overwritten
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x14A), static_cast<uint16_t>(hitPoints));
        if (oldMax != static_cast<uint16_t>(hitPoints))
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x8A), static_cast<uint16_t>(hitPoints));

        int stats[5];
        int baseStats[5] = {baseATK(), baseDEF(), baseSPE(), baseSPA(), baseSPD()};
        // effectiveIV, not ivXXX: a Hyper Trained stat is PLAYED at a maximal IV while the
        // stored IV stays as it was, so recomputing from the raw value writes stats below
        // what the game shows. Named constants because this array is in the STAT-WRITE
        // order (ATK, DEF, SPE, SPA, SPD) and the flag bits are in a DIFFERENT one
        // (HP, ATK, DEF, SPA, SPD, SPE) -- indices would line up wrong and look right.
        int individualValues[5] = {effectiveIV(HT_ATK), effectiveIV(HT_DEF), effectiveIV(HT_SPE),
                      effectiveIV(HT_SPA), effectiveIV(HT_SPD)};
        int effortValues[5] = {evATK(), evDEF(), evSPE(), evSPA(), evSPD()};

        for (int index = 0; index < 5; index++)
        {
            int baseStat = ((2 * baseStats[index] + individualValues[index] + (effortValues[index] / 4)) * levelValue) / 100 + 5;

            int modifier = getNatureModifier(index);
            stats[index] = (baseStat * modifier) / 100;
        }

        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x14C), static_cast<uint16_t>(stats[0])); // ATK
        // DEF
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x14E), static_cast<uint16_t>(stats[1]));
        // SPE
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x150), static_cast<uint16_t>(stats[2]));
        // SPA
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x152), static_cast<uint16_t>(stats[3]));
        // SPD
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x154), static_cast<uint16_t>(stats[4]));
    }

    void Pokemon8BDSP::setLevel(uint8_t level) noexcept
    {
        if (level < 1)
            level = 1;
        if (level > 100)
            level = 100;

        // EXP (0x10) is the source of truth for level: write this level's minimum total EXP.
        uint32_t expValue = getExpForLevel(level, getGrowthRate(speciesID()));
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), expValue);

        // Update the cached party-stat level byte that level() reads (0x148).
        data[0x148] = static_cast<std::byte>(level);

        recalculateStats();
        refreshChecksum();
    }

    void Pokemon8BDSP::setExp(uint32_t value) noexcept
    {
        // Write raw total EXP (0x10), then re-derive and cache the level from it (0x148).
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), value);
        uint8_t level = getLevelFromExp(value, getGrowthRate(speciesID()));
        if (level < 1)
            level = 1;
        if (level > 100)
            level = 100;
        data[0x148] = static_cast<std::byte>(level);
        recalculateStats();
        refreshChecksum();
    }

    void Pokemon8BDSP::regeneratePID(uint32_t trainerID32) noexcept
    {

        bool wasShiny = isShiny(trainerID32, species());
        uint32_t currentPID = pid();
        uint8_t genderByte = currentPID & 0xFF;

        uint32_t encryptionConstantValue = encryptionConstant();
        uint32_t basePID = encryptionConstantValue ^ 0x13371337;

        if (wasShiny)
        {
            uint32_t baseHighWord = basePID >> 16;
            uint32_t tidHigh = trainerID32 >> 16;
            uint32_t tidLow = trainerID32 & 0xFFFF;

            for (int attempt = 0; attempt < 256; attempt++)
            {
                uint32_t highWord = (baseHighWord + attempt) & 0xFFFF;
                uint32_t pidHighWord = highWord ^ tidHigh;

                for (int targetXor = 1; targetXor < 16; targetXor++)
                {
                    uint32_t pidLowWord = pidHighWord ^ targetXor;
                    uint32_t lowWord = pidLowWord ^ tidLow;

                    if ((lowWord & 0xFF) == genderByte)
                    {
                        uint32_t newPID = (highWord << 16) | lowWord;
                        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
                        refreshChecksum();
                        return;
                    }
                }

                // targetXor 0 is a square shiny
                uint32_t pidLowWord = pidHighWord ^ 0;
                uint32_t lowWord = pidLowWord ^ tidLow;
                if ((lowWord & 0xFF) == genderByte)
                {
                    uint32_t newPID = (highWord << 16) | lowWord;
                    writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
                    refreshChecksum();
                    return;
                }
            }

            uint32_t highWord = baseHighWord & 0xFFFF;
            uint32_t pidHighWord = highWord ^ tidHigh;
            uint32_t pidLowWord = pidHighWord ^ 1; // Star shiny
            uint32_t lowWord = pidLowWord ^ tidLow;
            uint32_t newPID = (highWord << 16) | lowWord;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
            refreshChecksum();
        }
        else
        {
            uint32_t newPID = (basePID & 0xFFFFFF00) | genderByte;

            uint32_t xorValue = ((newPID >> 16) ^ (newPID & 0xFFFF) ^ (trainerID32 >> 16) ^ (trainerID32 & 0xFFFF)) & 0xFFFF;
            if (xorValue < 16)
            {
                newPID ^= 0x100;
            }

            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
            refreshChecksum();
        }
    }

    void Pokemon8BDSP::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {

        bool currentlyShiny = isShiny(trainerID32, species());

        if (currentlyShiny == makeShiny)
        {
            return;
        }

        uint32_t currentPID = pid();
        uint8_t genderByte = currentPID & 0xFF;

        if (makeShiny)
        {

            uint32_t encryptionConstantValue = encryptionConstant();
            uint32_t baseHighWord = (encryptionConstantValue ^ 0x13371337) >> 16;
            uint32_t tidHigh = trainerID32 >> 16;
            uint32_t tidLow = trainerID32 & 0xFFFF;

            for (int attempt = 0; attempt < 256; attempt++)
            {
                uint32_t highWord = (baseHighWord + attempt) & 0xFFFF;
                uint32_t pidHighWord = highWord ^ tidHigh;

                for (int targetXor = 1; targetXor < 16; targetXor++)
                {
                    uint32_t pidLowWord = pidHighWord ^ targetXor;
                    uint32_t lowWord = pidLowWord ^ tidLow;

                    if ((lowWord & 0xFF) == genderByte)
                    {
                        uint32_t newPID = (highWord << 16) | lowWord;

                        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
                        refreshChecksum();
                        return;
                    }
                }

                // targetXor 0 is a square shiny
                uint32_t pidLowWord = pidHighWord ^ 0;
                uint32_t lowWord = pidLowWord ^ tidLow;
                if ((lowWord & 0xFF) == genderByte)
                {
                    uint32_t newPID = (highWord << 16) | lowWord;
                    writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
                    refreshChecksum();
                    return;
                }
            }

            uint32_t highWord = baseHighWord & 0xFFFF;
            uint32_t pidHighWord = highWord ^ tidHigh;
            uint32_t pidLowWord = pidHighWord ^ 1; // Star shiny
            uint32_t lowWord = pidLowWord ^ tidLow;
            uint32_t newPID = (highWord << 16) | lowWord;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
            refreshChecksum();
        }
        else
        {

            uint32_t encryptionConstantValue = encryptionConstant();
            uint32_t newPID = (encryptionConstantValue ^ 0x13371337);

            newPID = (newPID & 0xFFFFFF00) | genderByte;

            uint32_t xorComponent = newPID ^ trainerID32;
            uint32_t xorResult = (xorComponent ^ (xorComponent >> 16)) & 0xFFFF;

            if (xorResult < 16)
            {
                newPID ^= 0x00000100;

                xorComponent = newPID ^ trainerID32;
                xorResult = (xorComponent ^ (xorComponent >> 16)) & 0xFFFF;

                if (xorResult < 16)
                {
                    newPID ^= 0x00010000;
                }
            }

            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x1C), newPID);
            refreshChecksum();
        }
    }
}
