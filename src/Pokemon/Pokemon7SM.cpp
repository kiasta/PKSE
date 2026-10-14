#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon7SM.h"
#include "Pokemon/PIDSearch.h"
#include "Enums/GameVersion.h"
#include "Pokemon/Experience.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName
#include "Utils/HelperUtilities.h"

namespace Pokemon
{

    const char *Pokemon7SM::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    std::u16string Pokemon7SM::readString(size_t offset, size_t units) const
    {
        std::u16string out;
        out.reserve(units);
        for (size_t index = 0; index < units; ++index)
        {
            const uint16_t codeUnit = rd16(offset + index * 2);
            if (codeUnit == 0 || codeUnit == 0xFFFF)
                break;
            out.push_back(static_cast<char16_t>(codeUnit));
        }
        return out;
    }

    void Pokemon7SM::writeString(size_t offset, size_t units, const std::u16string &value) noexcept
    {
        size_t writeIndex = 0;
        for (; writeIndex < units && writeIndex < value.size(); ++writeIndex)
            wr16(offset + writeIndex * 2, static_cast<uint16_t>(value[writeIndex]));
        // Zero-fill the remainder. Gen 6/7 name fields are cleared rather than left trailing, and
        // the terminator is 0x0000 (PKHeX StringConverterOption.ClearZero on these fields).
        for (; writeIndex < units; ++writeIndex)
            wr16(offset + writeIndex * 2, 0);
    }

    const char *Pokemon7SM::genderSymbol() const noexcept
    {
        switch (gender())
        {
        case 0:
            return "\xE2\x99\x82";
        case 1:
            return "\xE2\x99\x80";
        default:
            return "";
        }
    }

    int Pokemon7SM::natureModifier(int statIndex) const noexcept
    {
        static const int8_t natureStatModifiers[25][5] = {
            {1, 1, 1, 1, 1},
            {2, 0, 1, 1, 1},
            {2, 1, 0, 1, 1},
            {2, 1, 1, 0, 1},
            {2, 1, 1, 1, 0},
            {0, 2, 1, 1, 1},
            {1, 1, 1, 1, 1},
            {1, 2, 0, 1, 1},
            {1, 2, 1, 0, 1},
            {1, 2, 1, 1, 0},
            {0, 1, 2, 1, 1},
            {1, 0, 2, 1, 1},
            {1, 1, 1, 1, 1},
            {1, 1, 2, 0, 1},
            {1, 1, 2, 1, 0},
            {0, 1, 1, 2, 1},
            {1, 0, 1, 2, 1},
            {1, 1, 0, 2, 1},
            {1, 1, 1, 1, 1},
            {1, 1, 1, 2, 0},
            {0, 1, 1, 1, 2},
            {1, 0, 1, 1, 2},
            {1, 1, 0, 1, 2},
            {1, 1, 1, 0, 2},
            {1, 1, 1, 1, 1},
        };
        const uint8_t count = statNature();
        if (count > 24 || statIndex < 0 || statIndex > 4)
            return 100;
        switch (natureStatModifiers[count][statIndex])
        {
        case 0:
            return 90;
        case 2:
            return 110;
        default:
            return 100;
        }
    }

    uint16_t Pokemon7SM::calculateChecksum() const noexcept
    {
        // PKHeX G6PKM: Add16 over the four blocks, 0x08..0xE8. The party tail is excluded.
        uint32_t sum = 0;
        for (size_t offset = 8; offset + 1 < Encryption::SIZE_STORED7_SM; offset += 2)
            sum += rd16(offset);
        return static_cast<uint16_t>(sum);
    }

    void Pokemon7SM::recalculateStats() noexcept
    {
        if (!isPartySize())
            return;
        uint8_t levelValue = level();
        if (levelValue == 0)
            levelValue = getLevelFromExp(exp(), personal().growthRate);
        if (levelValue == 0 || levelValue > 100)
            return;

        const PersonalRecord &p = personal();
        if (p.hp == 0)
            return; // no data -- do NOT write zeroed stats into the save

        // effectiveIV(), not ivXXX(): a hyper-trained stat is PLAYED at maxIV while the stored IV
        // stays put. Gen 6 has no hyper training so the two agree here -- but Pokemon7SM inherits
        // this function, and there they do not.
        const int hitPoints = ((2 * p.hp + effectiveIV(HT_HP) + (evHP() / 4)) * levelValue) / 100 + levelValue + 10;
        const uint16_t oldMax = rd16(0xF2);
        wr16(0xF2, static_cast<uint16_t>(hitPoints));
        if (oldMax != static_cast<uint16_t>(hitPoints))
            wr16(0xF0, static_cast<uint16_t>(hitPoints));

        const int base[5] = {p.atk, p.def, p.spe, p.spa, p.spd};
        // Stat-write order is ATK DEF SPE SPA SPD; the HT flag bits are in a DIFFERENT order
        // (HP ATK DEF SPA SPD SPE), so these use the named constants rather than indices.
        const int individualValues[5] = {effectiveIV(HT_ATK), effectiveIV(HT_DEF), effectiveIV(HT_SPE),
                           effectiveIV(HT_SPA), effectiveIV(HT_SPD)};
        const int evValue[5] = {evATK(), evDEF(), evSPE(), evSPA(), evSPD()};
        for (int index = 0; index < 5; ++index)
        {
            const int raw = ((2 * base[index] + individualValues[index] + (evValue[index] / 4)) * levelValue) / 100 + 5;
            wr16(0xF4 + index * 2, static_cast<uint16_t>((raw * natureModifier(index)) / 100));
        }
        wr8(0xEC, levelValue);
    }

    void Pokemon7SM::setExp(uint32_t value) noexcept
    {
        wr32(0x10, value);
        if (isPartySize())
            wr8(0xEC, getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon7SM::setLevel(uint8_t levelValue) noexcept
    {
        if (levelValue < 1)
            levelValue = 1;
        if (levelValue > 100)
            levelValue = 100;
        wr32(0x10, getExpForLevel(levelValue, personal().growthRate));
        if (isPartySize())
            wr8(0xEC, levelValue);
        recalculateStats();
    }

    bool Pokemon7SM::isShiny(uint32_t trainerID32, std::string) const noexcept
    {
        const uint32_t pidValue = pid();
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        const uint32_t pokemonShinyValue = ((pidValue >> 16) ^ (pidValue & 0xFFFF));
        // Gen 6 onward the threshold is 16, not Gen 3-5's 8.
        return ((trainerShinyValue ^ pokemonShinyValue) >> 4) == 0;
    }

    void Pokemon7SM::regeneratePID(uint32_t trainerID32) noexcept
    {
        // Gen 6 stores nature, gender and the ability slot as their own fields, but the PID still
        // decides shininess, so the new PID holds it. A Pokemon from Gens 3-5 keeps its PID: there it
        // also decides gender, nature and the ability slot, and its encryption constant is the PID.
        const int originGeneration = Enums::getVersionGeneration(originGame());
        if (originGeneration >= 3 && originGeneration <= 5)
            return;
        PIDRequirements requirements;
        requirements.trainerID32 = trainerID32;
        requirements.shinyThreshold = 16; // Gen 6 on
        requirements.wantShiny = isShiny(trainerID32, "") ? 1 : 0;
        uint32_t pidValue = 0;
        if (!findPID(requirements, pidValue))
            return;
        wr32(0x18, pidValue);
        refreshChecksum();
    }

    void Pokemon7SM::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        for (int index = 0; index < 1000000; ++index)
        {
            const uint32_t candidate = Utils::rand32();
            const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
            const uint32_t pokemonShinyValue = ((candidate >> 16) ^ (candidate & 0xFFFF));
            if ((((trainerShinyValue ^ pokemonShinyValue) >> 4) == 0) != makeShiny)
                continue;
            wr32(0x18, candidate);
            refreshChecksum();
            return;
        }
    }
}
