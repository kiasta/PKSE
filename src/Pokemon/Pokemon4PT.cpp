#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon4PT.h"
#include "Pokemon/PIDSearch.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PokemonTypes.h"
#include "Utils/Gen4Text.h"
#include "Utils/HelperUtilities.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName

namespace Pokemon
{

    const char *Pokemon4PT::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    std::u16string Pokemon4PT::readString(size_t offset, size_t units) const
    {
        std::u16string out;
        out.reserve(units);
        for (size_t index = 0; index < units; ++index)
        {
            const uint16_t raw = rd16(offset + index * 2);
            if (raw == Utils::GEN4_TERMINATOR || raw == 0)
                break;
            const uint16_t decodedChar = Utils::gen4ToChar(raw);
            if (decodedChar == 0)
                break; // unmapped value: corrupt data, not a glyph -- stop rather than guess
            out.push_back(static_cast<char16_t>(decodedChar));
        }
        return out;
    }

    void Pokemon4PT::writeString(size_t offset, size_t units, const std::u16string &newValue) noexcept
    {
        size_t writeIndex = 0;
        for (; writeIndex < units && writeIndex < newValue.size(); ++writeIndex)
        {
            const uint16_t encodedByte = Utils::charToGen4(static_cast<uint16_t>(newValue[writeIndex]));
            // An unencodable character ends the name here rather than writing a terminator value
            // as if it were a glyph. canStoreNickname() is what stops this being reached.
            if (encodedByte == Utils::GEN4_TERMINATOR)
                break;
            wr16(offset + writeIndex * 2, encodedByte);
        }
        // PKHeX writes a terminator UNLESS the field is exactly full -- one past the end would
        // overrun into the next field.
        if (writeIndex < units)
        {
            wr16(offset + writeIndex * 2, Utils::GEN4_TERMINATOR);
            // Everything after the terminator is left as it was: the games do not clear it, and a
            // round trip has to reproduce whatever was there.
        }
    }

    bool Pokemon4PT::canStoreNickname(const std::u16string &value) const noexcept
    {
        return Utils::gen4CanEncode(value);
    }

    const char *Pokemon4PT::genderSymbol() const noexcept
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

    uint8_t Pokemon4PT::abilityNumber() const noexcept
    {
        // Gen 4 has no ability-slot field: the game picks slot 1 or 2 from the PID's low bit.
        // (PKHeX G4PKM: AbilityNumber => 1 << PIDAbility.) There is no hidden ability.
        const PersonalRecord &p = personal();
        const uint16_t abilityId = ability();
        if (p.ability2 != p.ability1 && abilityId == p.ability2)
            return 2;
        if (abilityId == p.ability1)
            return 1;
        // The stored id matches neither slot -- report the PID's slot rather than invent one.
        return (pid() & 1) ? 2 : 1;
    }

    int Pokemon4PT::natureModifier(int statIndex) const noexcept
    {
        // statIndex is in STAT-WRITE order: 0 ATK, 1 DEF, 2 SPE, 3 SPA, 4 SPD.
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

    uint16_t Pokemon4PT::calculateChecksum() const noexcept
    {
        // PKHeX G4PKM: Add16 over the four blocks only, 0x08..0x88. The party tail is excluded.
        uint32_t sum = 0;
        for (size_t offset = 8; offset + 1 < Encryption::SIZE_STORED4_PT; offset += 2)
            sum += rd16(offset);
        return static_cast<uint16_t>(sum);
    }

    void Pokemon4PT::recalculateStats() noexcept
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

    void Pokemon4PT::setExp(uint32_t value) noexcept
    {
        wr32(0x10, value);
        if (isPartySize())
            wr8(0x8C, getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon4PT::setLevel(uint8_t levelValue) noexcept
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

    bool Pokemon4PT::isShiny(uint32_t trainerID32, std::string) const noexcept
    {
        const uint32_t pidValue = pid();
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        const uint32_t pokemonShinyValue = ((pidValue >> 16) ^ (pidValue & 0xFFFF));
        return ((trainerShinyValue ^ pokemonShinyValue) >> 3) == 0; // Gen 3-5 threshold is 8
    }

    // Gen 4 builds shininess, gender, nature (PID % 25) and the ability slot (bit 0) from the PID, and
    // also stores the gender at 0x40, which PKHeX requires to agree with the PID's. Each caller names
    // what it changes and holds the rest; the stored gender is written to match the new PID.
    bool Pokemon4PT::rerollPID(int wantShiny, int wantGender, int wantNature, uint32_t trainerID32) noexcept
    {
        PIDRequirements requirements;
        requirements.trainerID32 = trainerID32;
        requirements.shinyThreshold = 8; // Gen 3-5
        requirements.wantShiny = wantShiny;
        requirements.genderRatio = personal().genderRatio;
        requirements.wantGender = wantGender;
        requirements.wantNature = wantNature;
        requirements.abilityBitIndex = 0;
        requirements.wantAbilityBit = static_cast<int>(pid() & 1);
        uint32_t pidValue = 0;
        if (!findPID(requirements, pidValue))
        {
            Utils::logErrorToFile("Pokemon4PT::rerollPID: no PID holds shiny=" + std::to_string(wantShiny) +
                                  " gender=" + std::to_string(wantGender) + " nature=" + std::to_string(wantNature) +
                                  "; the Pokemon is unchanged");
            return false;
        }
        wr32(0x00, pidValue);
        writeStoredGender(genderFromPID(pidValue, requirements.genderRatio));
        recalculateStats();
        refreshChecksum();
        return true;
    }

    // A legality touch-up asked for nothing to change, so it holds everything the PID decides.
    void Pokemon4PT::regeneratePID(uint32_t trainerID32) noexcept
    {
        rerollPID(isShiny(trainerID32, "") ? 1 : 0, gender(), nature(), trainerID32);
    }

    void Pokemon4PT::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        rerollPID(makeShiny ? 1 : 0, gender(), nature(), trainerID32);
    }

    void Pokemon4PT::setNature(uint8_t natureValue) noexcept
    {
        if (natureValue > 24 || natureValue == nature())
            return;
        // Gen 4 stores no nature: it IS `PID % 25`, so changing it means a new PID. Shininess, gender
        // and the ability slot are held, so the nature is the only thing that changes.
        rerollPID(isShiny(id32(), "") ? 1 : 0, gender(), natureValue, id32());
    }

    void Pokemon4PT::setGender(uint8_t genderValue) noexcept
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
        rerollPID(isShiny(id32(), "") ? 1 : 0, genderValue, nature(), id32());
    }
}
