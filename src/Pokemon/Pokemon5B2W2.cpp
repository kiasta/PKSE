#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon5B2W2.h"
#include "Pokemon/PIDSearch.h"
#include "Enums/GameVersion.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PokemonTypes.h"
#include "Utils/HelperUtilities.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName

namespace Pokemon
{

    const char *Pokemon5B2W2::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    // No code page. Gen 4 indexes a glyph table; Gen 5 stores the code units. Sharing Gen 4's
    // converter here turns every name into CJK punctuation that still reads as text.

    std::u16string Pokemon5B2W2::readString(size_t offset, size_t units) const
    {
        std::u16string out;
        out.reserve(units);
        for (size_t index = 0; index < units; ++index)
        {
            const uint16_t codeUnit = rd16(offset + index * 2);
            // PKHeX StringConverter5: BOTH 0xFFFF and 0x0000 terminate. Gen 4 checks only 0xFFFF.
            if (codeUnit == 0xFFFF || codeUnit == 0)
                break;
            out.push_back(static_cast<char16_t>(codeUnit));
        }
        return out;
    }

    void Pokemon5B2W2::writeString(size_t offset, size_t units, const std::u16string &value) noexcept
    {
        size_t writeIndex = 0;
        for (; writeIndex < units && writeIndex < value.size(); ++writeIndex)
            wr16(offset + writeIndex * 2, static_cast<uint16_t>(value[writeIndex]));
        // A name that exactly fills the field gets no terminator -- one past the end would
        // overrun into the next field.
        if (writeIndex < units)
            wr16(offset + writeIndex * 2, 0xFFFF);
    }

    bool Pokemon5B2W2::canStoreNickname(const std::u16string &) const noexcept
    {
        // Gen 5 stores UTF-16 and takes essentially anything, like Gen 8/9. There is no glyph
        // table to fail against, so there is nothing to refuse.
        return true;
    }

    const char *Pokemon5B2W2::genderSymbol() const noexcept
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

    uint8_t Pokemon5B2W2::abilityNumber() const noexcept
    {
        // Gen 5 has a real hidden-ability flag (0x42 bit 0), so unlike Gen 4 this is not a PID
        // inference. PKSE reports the hidden slot as 4, matching Gen 6+'s AbilityNumber encoding.
        if (hasHiddenAbility())
            return 4;
        const PersonalRecord &p = personal();
        const uint16_t abilityId = ability();
        if (p.ability2 != p.ability1 && abilityId == p.ability2)
            return 2;
        return 1;
    }

    int Pokemon5B2W2::natureModifier(int statIndex) const noexcept
    {
        // statIndex is in STAT-WRITE order: 0 ATK, 1 DEF, 2 SPE, 3 SPA, 4 SPD.
        static const int8_t natureStatModifiers[25][5] = {
            {1, 1, 1, 1, 1},
            {2, 0, 1, 1, 1},
            {2, 1, 0, 1, 1},
            {2, 1, 1, 0, 1},
            {2, 1, 1, 1, 0},
            {0, 2, 1, 1, 1},
            {1, 1, 0, 1, 1},
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
        const uint8_t count = nature();
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

    uint16_t Pokemon5B2W2::calculateChecksum() const noexcept
    {
        // PKHeX G4PKM: Add16 over the four blocks only, 0x08..0x88. The party tail is excluded.
        uint32_t sum = 0;
        for (size_t offset = 8; offset + 1 < Encryption::SIZE_STORED5_B2W2; offset += 2)
            sum += rd16(offset);
        return static_cast<uint16_t>(sum);
    }

    void Pokemon5B2W2::recalculateStats() noexcept
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
            return; // no data for this species -- do NOT write zeroed stats

        const int hitPoints = ((2 * p.hp + ivHP() + (evHP() / 4)) * levelValue) / 100 + levelValue + 10;
        const uint16_t oldMax = rd16(0x90);
        wr16(0x90, static_cast<uint16_t>(hitPoints));
        // Only re-top current HP when max HP actually moved; blindly setting current = max heals
        // a fainted Pokemon behind the player's back.
        if (oldMax != static_cast<uint16_t>(hitPoints))
            wr16(0x8E, static_cast<uint16_t>(hitPoints));

        const int base[5] = {p.atk, p.def, p.spe, p.spa, p.spd};
        const int individualValues[5] = {ivATK(), ivDEF(), ivSPE(), ivSPA(), ivSPD()};
        const int evValue[5] = {evATK(), evDEF(), evSPE(), evSPA(), evSPD()};
        for (int index = 0; index < 5; ++index)
        {
            const int raw = ((2 * base[index] + individualValues[index] + (evValue[index] / 4)) * levelValue) / 100 + 5;
            wr16(0x92 + index * 2, static_cast<uint16_t>((raw * natureModifier(index)) / 100));
        }
        wr8(0x8C, levelValue);
    }

    void Pokemon5B2W2::setExp(uint32_t value) noexcept
    {
        wr32(0x10, value);
        if (isPartySize())
            wr8(0x8C, getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon5B2W2::setLevel(uint8_t levelValue) noexcept
    {
        if (levelValue < 1)
            levelValue = 1;
        if (levelValue > 100)
            levelValue = 100;
        wr32(0x10, getExpForLevel(levelValue, personal().growthRate));
        if (isPartySize())
            wr8(0x8C, levelValue);
        recalculateStats();
    }

    bool Pokemon5B2W2::isShiny(uint32_t trainerID32, std::string) const noexcept
    {
        const uint32_t pidValue = pid();
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        const uint32_t pokemonShinyValue = ((pidValue >> 16) ^ (pidValue & 0xFFFF));
        return ((trainerShinyValue ^ pokemonShinyValue) >> 3) == 0; // Gen 3-5 threshold is 8
    }

    // Gen 5 builds shininess, gender and the ability slot from the PID, and also stores the gender at
    // 0x40, which PKHeX requires to agree with the PID's. The nature is its own byte, except on a
    // Pokemon from Gens 3-4, whose nature is still `PID % 25` and whose ability slot is bit 0 rather
    // than Gen 5's bit 16 (PKHeX PKM.PIDAbility). Each caller names what it changes and holds the
    // rest; the stored gender is written to match the new PID.
    bool Pokemon5B2W2::rerollPID(int wantShiny, int wantGender, uint32_t trainerID32) noexcept
    {
        const int originGeneration = Enums::getVersionGeneration(originGame());
        const bool pidHoldsNature = originGeneration == 3 || originGeneration == 4;
        PIDRequirements requirements;
        requirements.trainerID32 = trainerID32;
        requirements.shinyThreshold = 8; // Gen 3-5
        requirements.wantShiny = wantShiny;
        requirements.genderRatio = personal().genderRatio;
        requirements.wantGender = wantGender;
        requirements.wantNature = pidHoldsNature ? static_cast<int>(pid() % 25) : -1;
        requirements.abilityBitIndex = pidHoldsNature ? 0 : 16;
        requirements.wantAbilityBit = static_cast<int>((pid() >> requirements.abilityBitIndex) & 1);
        uint32_t pidValue = 0;
        if (!findPID(requirements, pidValue))
        {
            Utils::logErrorToFile("Pokemon5B2W2::rerollPID: no PID holds shiny=" + std::to_string(wantShiny) +
                                  " gender=" + std::to_string(wantGender) + "; the Pokemon is unchanged");
            return false;
        }
        wr32(0x00, pidValue);
        writeStoredGender(genderFromPID(pidValue, requirements.genderRatio));
        recalculateStats();
        refreshChecksum();
        return true;
    }

    // A legality touch-up asked for nothing to change, so it holds everything the PID decides.
    void Pokemon5B2W2::regeneratePID(uint32_t trainerID32) noexcept
    {
        rerollPID(isShiny(trainerID32, "") ? 1 : 0, gender(), trainerID32);
    }

    void Pokemon5B2W2::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        rerollPID(makeShiny ? 1 : 0, gender(), trainerID32);
    }

    void Pokemon5B2W2::setGender(uint8_t genderValue) noexcept
    {
        // A species whose gender is fixed, or a PID that already gives this gender: the stored bits
        // are all that change. Otherwise the PID has to give the new gender too.
        const uint8_t genderRatio = personal().genderRatio;
        if (genderValue > 1 || genderRatio == 0 || genderRatio == 254 || genderRatio == 255 ||
            genderFromPID(pid(), genderRatio) == genderValue)
        {
            writeStoredGender(genderValue);
            refreshChecksum();
            return;
        }
        rerollPID(isShiny(id32(), "") ? 1 : 0, genderValue, id32());
    }

}
