#include <cstdio>
#include <algorithm>
#include <cmath>

#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/PersonalInfo8LA.h"

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

    const char *Pokemon8LA::species() const noexcept
    {
        return Names::getSpeciesName(speciesID());
    }

    uint8_t Pokemon8LA::gender() const noexcept
    {
        // Gen 8 stores gender explicitly (0 male, 1 female, 2 genderless) in bits 2-3 of byte 0x22.
        // That value already encodes a fixed-gender species, and it is independent of the PID, so it
        // survives a shiny or PID edit.
        return (static_cast<uint8_t>(data[0x22]) >> 2) & 0x03;
    }

    uint8_t Pokemon8LA::baseHP() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).hp;
    }

    uint8_t Pokemon8LA::baseATK() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).atk;
    }

    uint8_t Pokemon8LA::baseDEF() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).def;
    }

    uint8_t Pokemon8LA::baseSPE() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).spe;
    }

    uint8_t Pokemon8LA::baseSPA() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).spa;
    }

    uint8_t Pokemon8LA::baseSPD() const noexcept
    {
        return getPersonalInfo8LA(speciesID(), form()).spd;
    }

    int Pokemon8LA::getNatureModifier(int statIndex) const noexcept
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

    namespace
    {
        /**
         * LEGENDS: ARCEUS DOES NOT USE THE MAINLINE STAT FORMULA. Not a variant of it -- a
         * different one, with a different shape (PKHeX `PA8.LoadStats`):
         *
         *     stat = ganbaru(base, iv, gv, level) + scaled(base, level [, nature])
         *
         * The IV does NOT enter the base term at all, EVs do not exist (PLA has GRIT VALUES,
         * 0-10 per stat, at 0xA4-0xA9), and the level term scales the base stat directly instead
         * of the (2*base + iv + ev/4) * level / 100 of every other generation.
         *
         * Using the mainline formula here was wrong by roughly a factor of three at low level:
         * a real level-7 Oshawott stores HP 74 / SpA 50 / Spe 37 and the old code recomputed
         * 26 / 15 / 11. That is not cosmetic -- the constructor recomputes the cache for every
         * BOXED PLA pokemon on load, so the whole box displayed wrong stats, and any edit to a party
         * pokemon wrote the wrong numbers into the save.
         */

        /// PKHeX GanbaruExtensions: multiplier by (grit value + IV bias), capped at 10.
        constexpr uint8_t GANBARU_MULTIPLIER[11] = {0, 2, 3, 4, 7, 8, 9, 14, 15, 16, 25};

        /// A high IV is worth free grit levels before the stored value is added.
        constexpr int ganbaruBias(int individualValue) noexcept
        {
            return individualValue >= 31 ? 3 : individualValue >= 26 ? 2 : individualValue >= 20 ? 1 : 0;
        }

        /// PKHeX PA8.GetGanbaruStat. The float/double split is deliberate: the game does the
        /// sqrt in double and the divide in float, and rounds half AWAY FROM ZERO.
        int ganbaruStat(int baseStat, int individualValue, uint8_t gritValue, uint8_t levelValue) noexcept
        {
            const int index = std::min(static_cast<int>(gritValue) + ganbaruBias(individualValue), 10);
            const double step = std::sqrt(static_cast<double>(baseStat)) * GANBARU_MULTIPLIER[index];
            const float scaled = (static_cast<float>(step) + levelValue) / 2.5f;
            return static_cast<int>(std::round(static_cast<double>(scaled)));
        }

        /// PKHeX PA8.GetStatHp -- HP has no nature term.
        int scaledHP(int baseStat, uint8_t levelValue) noexcept
        {
            return static_cast<int>((((levelValue / 100.0f) + 1.0f) * baseStat) + levelValue);
        }

        /// PKHeX PA8.GetStat, before the nature amplification the caller applies.
        int scaledStat(int baseStat, uint8_t levelValue) noexcept
        {
            return static_cast<int>((((levelValue / 50.0f) + 1.0f) * baseStat) / 1.5f);
        }
    }

    void Pokemon8LA::recalculateStats() noexcept
    {
        uint8_t levelValue = level();
        if (levelValue == 0 || levelValue > 100)
        {
            return;
        }

        // HP. effectiveIV, not ivXXX: a Hyper Trained stat is PLAYED at a maximal IV while the
        // stored IV stays as it was, so recomputing from the raw value writes stats below what
        // the game shows. PKHeX spells the same rule `HT_HP ? 31 : IV_HP`.
        int hitPoints = ganbaruStat(baseHP(), effectiveIV(HT_HP), gritHP(), levelValue) + scaledHP(baseHP(), levelValue);
        const uint16_t oldMax = readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x16A)); // before it is overwritten
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x16A), static_cast<uint16_t>(hitPoints));
        if (oldMax != static_cast<uint16_t>(hitPoints))
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x92), static_cast<uint16_t>(hitPoints));

        // The other five. Every array below is in the STAT-WRITE order (ATK, DEF, SPE, SPA, SPD),
        // which is NOT the order the Hyper Training flags or the grit bytes are stored in -- the
        // indices would line up wrong and look right, so each is spelled out rather than looped.
        int stats[5];
        int baseStats[5] = {baseATK(), baseDEF(), baseSPE(), baseSPA(), baseSPD()};
        int individualValues[5] = {effectiveIV(HT_ATK), effectiveIV(HT_DEF), effectiveIV(HT_SPE),
                      effectiveIV(HT_SPA), effectiveIV(HT_SPD)};
        uint8_t gritValues[5] = {gritATK(), gritDEF(), gritSPE(), gritSPA(), gritSPD()};

        for (int index = 0; index < 5; index++)
        {
            // Nature amplifies ONLY the scaled base term; the ganbaru term is added after it,
            // unamplified. Applying the modifier to the sum inflates a boosted stat.
            const int amplified = (scaledStat(baseStats[index], levelValue) * getNatureModifier(index)) / 100;
            stats[index] = ganbaruStat(baseStats[index], individualValues[index], gritValues[index], levelValue) + amplified;
        }

        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x16C), static_cast<uint16_t>(stats[0])); // ATK
        // DEF
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x16E), static_cast<uint16_t>(stats[1]));
        // SPE
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x170), static_cast<uint16_t>(stats[2]));
        // SPA
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x172), static_cast<uint16_t>(stats[3]));
        // SPD
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x174), static_cast<uint16_t>(stats[4]));
    }

    void Pokemon8LA::setLevel(uint8_t level) noexcept
    {
        if (level < 1)
            level = 1;
        if (level > 100)
            level = 100;

        // PA8 derives level() from EXP (0x10), so EXP is the source of truth: write this level's
        // minimum total EXP. recalculateStats() below reads the new level back from it.
        uint32_t expValue = getExpForLevel(level, getGrowthRate(speciesID()));
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), expValue);

        // Update the cached party-stat level byte to match (0x168; party stats start at 0x16A).
        data[0x168] = static_cast<std::byte>(level);

        recalculateStats();
        refreshChecksum();
    }

    void Pokemon8LA::setExp(uint32_t value) noexcept
    {
        // PA8 derives level() from EXP; write raw EXP (0x10) and re-cache the level byte (0x168).
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), value);
        uint8_t level = getLevelFromExp(value, getGrowthRate(speciesID()));
        if (level < 1)
            level = 1;
        if (level > 100)
            level = 100;
        data[0x168] = static_cast<std::byte>(level);
        recalculateStats();
        refreshChecksum();
    }

    void Pokemon8LA::regeneratePID(uint32_t trainerID32) noexcept
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

    void Pokemon8LA::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
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
