#include <cstdio>
#include <algorithm>
#include <cmath>

#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/PersonalInfo7LGPE.h"
#include "Pokemon/PersonalInfoTable.h" // the base height and weight the size absolutes scale
#include "Pokemon/Experience.h" // getLevelFromExp / getGrowthRate (real growth-table level)

namespace Trainer
{
    // Forward declaration for species name lookup
    extern const char *getSpeciesName(uint16_t speciesId);
}

namespace Pokemon
{

    namespace
    {
        // PKHeX PB7.GetHeightRatio / GetWeightRatio. The volatile stores keep the multiply from
        // being fused into the add whatever the compiler flags are.
        float heightRatioFor(uint8_t heightScalarValue)
        {
            volatile float scaledRatio = (heightScalarValue / 255.0f) * 0.79999995f;
            return scaledRatio + 0.6f;
        }

        float weightRatioFor(uint8_t weightScalarValue)
        {
            volatile float scaledRatio = (weightScalarValue / 255.0f) * 0.40000004f;
            return scaledRatio + 0.8f;
        }
    }

    float Pokemon7LGPE::heightAbsoluteFor(uint16_t speciesId, uint8_t formId, uint8_t heightScalarValue) noexcept
    {
        const PersonalInfo &personalInfo = getPersonalInfo(speciesId, formId);
        return heightRatioFor(heightScalarValue) * static_cast<float>(personalInfo.height);
    }

    float Pokemon7LGPE::weightAbsoluteFor(uint16_t speciesId, uint8_t formId, uint8_t heightScalarValue, uint8_t weightScalarValue) noexcept
    {
        const PersonalInfo &personalInfo = getPersonalInfo(speciesId, formId);
        volatile float scaledWeight = weightRatioFor(weightScalarValue) * static_cast<float>(personalInfo.weight);
        return heightRatioFor(heightScalarValue) * scaledWeight;
    }

    void Pokemon7LGPE::resetSizeAbsolutes() noexcept
    {
        constexpr size_t heightAbsoluteOffset = 0x2C;
        constexpr size_t weightAbsoluteOffset = 0xE4;
        if (dataSize < weightAbsoluteOffset + sizeof(float))
            return;
        const float heightValue = heightAbsoluteFor(speciesID(), form(), heightScalar());
        const float weightValue = weightAbsoluteFor(speciesID(), form(), heightScalar(), weightScalar());
        uint32_t heightBits = 0;
        uint32_t weightBits = 0;
        std::memcpy(&heightBits, &heightValue, sizeof(float));
        std::memcpy(&weightBits, &weightValue, sizeof(float));
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + heightAbsoluteOffset), heightBits);
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + weightAbsoluteOffset), weightBits);
        refreshChecksum();
    }

    const char *Pokemon7LGPE::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    uint8_t Pokemon7LGPE::gender() const noexcept
    {
        /**
         * Gender is a STORED 2-bit field at byte 0x1D (bits 1-2): 0=Male, 1=Female,
         * 2=Genderless. It is independent of the PID and the Encryption Constant, so
         * changing shininess must NOT change gender.
         */
        return (static_cast<uint8_t>(data[0x1D]) >> 1) & 0x03;
    }

    uint8_t Pokemon7LGPE::baseHP() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).hp;
    }

    uint8_t Pokemon7LGPE::baseATK() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).atk;
    }

    uint8_t Pokemon7LGPE::baseDEF() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).def;
    }

    uint8_t Pokemon7LGPE::baseSPE() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).spe;
    }

    uint8_t Pokemon7LGPE::baseSPA() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).spa;
    }

    uint8_t Pokemon7LGPE::baseSPD() const noexcept
    {
        return getPersonalInfo7LGPE(speciesID(), form()).spd;
    }

    uint8_t Pokemon7LGPE::level() const noexcept
    {
        /**
         * Level is derived from EXP through the SPECIES' growth-rate table, per PKHeX
         * PKM.CurrentLevel = Experience.GetLevel(EXP, PersonalInfo.EXPGrowth).
         *
         * Do not approximate this with cbrt(exp): the cube root is only the Medium-Fast curve, so it
         * silently under-reports the level of every species on one of the other five curves (a
         * Medium-Slow Charmander holding the level-22 threshold of 7577 EXP read as 19). That error
         * then cascades -- every stat getter below multiplies by level(), and the legality checker
         * raises bogus "level does not match EXP" / "met level above current level" flags.
         */
        return getLevelFromExp(exp(), getGrowthRate(speciesID()));
    }

    void Pokemon7LGPE::setLevel(uint8_t level) noexcept
    {
        // Level is derived from EXP, so write this level's minimum total EXP (0x10); the stat getters
        // read level() back from it. Without this override the level picker was a base no-op for LGPE.
        if (level < 1)
            level = 1;
        if (level > 100)
            level = 100;
        uint32_t expValue = getExpForLevel(level, getGrowthRate(speciesID()));
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), expValue);
        recalculateStats();
        refreshChecksum();
    }

    // Single source of truth for LGPE battle stats: the six display getters below AND recalculateStats()
    // (which writes the stored Level/stat/CP tail the GAME reads) all funnel through here, so PKSE's
    // summary can never drift from what the game shows. Formula per PKHeX PB7:
    //   initial = (IV + 2*Base) * Level / 100
    //   HP   = initial + Level + 10 + AV                         (no nature, no friendship)
    //   else = friendship% * ( nature% * (initial + 5) ) + AV    (friendship% = 100..110; nature% = 90/100/110)
    // index 0..5 = HP, Atk, Def, Spe, SpA, SpD; the nature-table column is index-1 in [Atk,Def,Spe,SpA,SpD]
    // order. EVs are intentionally excluded -- LGPE trains via AVs, not EVs.
    uint16_t Pokemon7LGPE::computeStat(int statIndex) const noexcept
    {
        uint8_t levelValue = level();
        if (levelValue == 0 || levelValue > 100)
            return 1;

        auto baseStat = [&](int statIndex) -> int
        {
            switch (statIndex)
            {
            case 0:
                return baseHP();
            case 1:
                return baseATK();
            case 2:
                return baseDEF();
            case 3:
                return baseSPE();
            case 4:
                return baseSPA();
            default:
                return baseSPD();
            }
        };
        // effectiveIV, not ivXXX: Let's Go has Hyper Training as well as Awakening Values, and a
        // Hyper Trained stat is PLAYED at a maximal IV while the stored IV is left alone. Note the
        // index order here (HP, ATK, DEF, SPE, SPA, SPD) is NOT the flag bit order
        // (HP, ATK, DEF, SPA, SPD, SPE), so these are mapped by name rather than by index.
        auto ivStat = [&](int statIndex) -> int
        {
            switch (statIndex)
            {
            case 0:
                return effectiveIV(HT_HP);
            case 1:
                return effectiveIV(HT_ATK);
            case 2:
                return effectiveIV(HT_DEF);
            case 3:
                return effectiveIV(HT_SPE);
            case 4:
                return effectiveIV(HT_SPA);
            default:
                return effectiveIV(HT_SPD);
            }
        };
        auto avStat = [&](int statIndex) -> int
        {
            switch (statIndex)
            {
            case 0:
                return avHP();
            case 1:
                return avATK();
            case 2:
                return avDEF();
            case 3:
                return avSPE();
            case 4:
                return avSPA();
            default:
                return avSPD();
            }
        };

        const int initial = (ivStat(statIndex) + 2 * baseStat(statIndex)) * levelValue / 100;
        if (statIndex == 0)
            return static_cast<uint16_t>(std::max(1, avStat(0) + initial + levelValue + 10));

        const int natureMod = natureStatPercent(nature(), statIndex - 1);

        const int friendVal = friendship();
        const int scalar = static_cast<int>(((friendVal / 255.0f / 10.0f) + 1.0f) * 100.0f); // 100..110
        const int amplified = (initial + 5) * natureMod / 100;
        const int part = scalar * amplified / 100;
        return static_cast<uint16_t>(std::max(1, avStat(statIndex) + part));
    }

    uint16_t Pokemon7LGPE::statHPMax() const noexcept { return computeStat(0); }

    uint16_t Pokemon7LGPE::statATK() const noexcept { return computeStat(1); }

    uint16_t Pokemon7LGPE::statDEF() const noexcept { return computeStat(2); }

    uint16_t Pokemon7LGPE::statSPE() const noexcept { return computeStat(3); }

    uint16_t Pokemon7LGPE::statSPA() const noexcept { return computeStat(4); }

    uint16_t Pokemon7LGPE::statSPD() const noexcept { return computeStat(5); }

    void Pokemon7LGPE::recalculateStats() noexcept
    {
        // Let's Go stores Level + battle stats + Combat Power in the (un-checksummed) party tail at
        // 0xEC..0xFF. The on-the-fly getters recompute for our display, but the GAME reads these stored
        // bytes -- so a freshly built pokemon (e.g. a cross-game conversion) needs them written or it shows
        // Level/CP 0. Formula per PKHeX PB7 (LoadStats + CalculateCP): the 5 non-HP stats carry the friendship
        // scalar + nature amp + AV; CP = min(10000, BaseCP + AwakeCP).
        const uint16_t species = speciesID();
        uint8_t levelValue = getLevelFromExp(exp(), getGrowthRate(species));
        if (levelValue < 1)
            levelValue = 1;
        else if (levelValue > 100)
            levelValue = 100;
        data[0xEC] = static_cast<std::byte>(levelValue); // Stat_Level

        // Every stat comes from computeStat() (shared with the display getters) so the stored tail the
        // GAME reads is exactly what PKSE shows -- nature, friendship and AV all included.
        const int hitPoints = computeStat(0);
        const int attackStat = computeStat(1);
        const int defenseStat = computeStat(2);
        const int speedStat = computeStat(3);
        const int specialAttackStat = computeStat(4);
        const int specialDefenseStat = computeStat(5);

        auto writeStat = [&](size_t offset, int value)
        {
            if (value < 1)
                value = 1;
            else if (value > 65535)
                value = 65535;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + offset), static_cast<uint16_t>(value));
        };
        const uint16_t oldMax = readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xF2));
        writeStat(0xF2, hitPoints); // Stat_HPMax
        // Stat_HPCurrent
        if (oldMax != static_cast<uint16_t>(hitPoints)) writeStat(0xF0, hitPoints);
        writeStat(0xF4, attackStat);
        writeStat(0xF6, defenseStat); // ATK / DEF
        writeStat(0xF8, speedStat);
        writeStat(0xFA, specialAttackStat); // SPE / SPA
        writeStat(0xFC, specialDefenseStat); // SPD

        // CP (PKHeX CalculateCP): base part from the NON-AV portion of every stat, plus an AV bonus. Each
        // stat's non-AV portion is (stat - its AV), so statSum = (sum of all six stats) - (sum of AVs).
        const int avSum = avHP() + avATK() + avDEF() + avSPE() + avSPA() + avSPD();
        const int statSum = hitPoints + attackStat + defenseStat + speedStat + specialAttackStat + specialDefenseStat - avSum;
        int baseCP = static_cast<int>(static_cast<float>(statSum) * 6.0f * levelValue / 100.0f);
        const int awakeCP = (avSum > 0) ? static_cast<int>(avSum * ((levelValue * 4.0f / 100.0f) + 2.0f)) : 0;
        int combatPower = baseCP + awakeCP;
        if (combatPower > 10000)
            combatPower = 10000;
        else if (combatPower < 0)
            combatPower = 0;
        // Stat_CP
        writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xFE), static_cast<uint16_t>(combatPower));
    }

    void Pokemon7LGPE::regeneratePID(uint32_t trainerID32) noexcept
    {
        /**
         * Re-seeds the Encryption Constant (0x00). In LGPE the EC is ONLY the crypto seed —
         * PID (0x18), gender (0x1D) and shininess (PID-derived) are all independent of it —
         * so re-seeding is harmless and leaves the Pokemon's identity untouched.
         */
        (void)trainerID32;
        uint32_t oldEC = encryptionConstant();
        uint32_t newEC = (oldEC ^ 0x10101010) + speciesID();
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x00), newEC);
        refreshChecksum();
    }

    void Pokemon7LGPE::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        /**
         * Sets shininess by modifying the PID (0x18) only -- NOT the Encryption Constant and NOT the
         * stored gender byte (0x1D). Shiny iff (TID^SID^PIDhi^PIDlo) < 16.
         *
         * That XOR folds the two halves of the PID together, which is the trap: a mask that flips the
         * SAME bit position in both halves (0x10001000, then 0x01000100) cancels itself inside
         * `PIDhi ^ PIDlo` and leaves the shiny value untouched, so flipping bits and hoping cannot work
         * in one direction. A mask has to be asymmetric between the halves to move it.
         *
         * Rather than flip bits, solve for the value outright. Writing
         *     PIDhi' = PIDhi ^ (current ^ target)
         * makes the new XOR exactly `target`, because the old PIDhi cancels out. Target 1 is a star
         * shiny; target 16 is the first non-shiny value, so it is the smallest edit that clears the flag.
         * Only the high word moves, leaving the low byte (the one the other generations guard as their
         * gender byte) untouched.
         */

        if (trainerID32 == 0)
        {
            return; // Cannot set shiny without trainer ID
        }
        if (isShiny(trainerID32, species()) == makeShiny)
        {
            return; // Already in the requested state
        }

        const uint32_t pidValue = pid(); // real PID at 0x18
        const uint16_t pidHigh = static_cast<uint16_t>((pidValue >> 16) & 0xFFFF);
        const uint16_t pidLow = static_cast<uint16_t>(pidValue & 0xFFFF);
        const uint16_t tidHigh = static_cast<uint16_t>((trainerID32 >> 16) & 0xFFFF);
        const uint16_t tidLow = static_cast<uint16_t>(trainerID32 & 0xFFFF);

        const uint16_t current = static_cast<uint16_t>(pidHigh ^ pidLow ^ tidHigh ^ tidLow);
        const uint16_t target = makeShiny ? 1 : 16;
        const uint16_t newHigh = static_cast<uint16_t>(pidHigh ^ (current ^ target));

        const uint32_t newPID = (static_cast<uint32_t>(newHigh) << 16) | pidLow;
        writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x18), newPID);
        refreshChecksum();
    }
}
