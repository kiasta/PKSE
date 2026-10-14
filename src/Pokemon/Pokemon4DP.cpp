#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon4DP.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PokemonTypes.h"
#include "Utils/Gen4Text.h"
#include "Utils/HelperUtilities.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName

namespace Pokemon
{

    const char *Pokemon4DP::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    std::u16string Pokemon4DP::readString(size_t offset, size_t units) const
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

    void Pokemon4DP::writeString(size_t offset, size_t units, const std::u16string &newValue) noexcept
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

    bool Pokemon4DP::canStoreNickname(const std::u16string &value) const noexcept
    {
        return Utils::gen4CanEncode(value);
    }

    const char *Pokemon4DP::genderSymbol() const noexcept
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

    uint8_t Pokemon4DP::abilityNumber() const noexcept
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

    int Pokemon4DP::natureModifier(int statIndex) const noexcept
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

    uint16_t Pokemon4DP::calculateChecksum() const noexcept
    {
        // PKHeX G4PKM: Add16 over the four blocks only, 0x08..0x88. The party tail is excluded.
        uint32_t sum = 0;
        for (size_t offset = 8; offset + 1 < Encryption::SIZE_STORED4_DP; offset += 2)
            sum += rd16(offset);
        return static_cast<uint16_t>(sum);
    }

    void Pokemon4DP::recalculateStats() noexcept
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

    void Pokemon4DP::setExp(uint32_t value) noexcept
    {
        wr32(0x10, value);
        if (isPartySize())
            wr8(0x8C, getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon4DP::setLevel(uint8_t levelValue) noexcept
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

    bool Pokemon4DP::isShiny(uint32_t trainerID32, std::string) const noexcept
    {
        const uint32_t pidValue = pid();
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        const uint32_t pokemonShinyValue = ((pidValue >> 16) ^ (pidValue & 0xFFFF));
        return ((trainerShinyValue ^ pokemonShinyValue) >> 3) == 0; // Gen 3-5 threshold is 8
    }

    void Pokemon4DP::regeneratePID(uint32_t trainerID32) noexcept
    {
        // Preserve nature and the ability slot, which are BOTH PID-derived in Gen 4 -- the same
        // constraint the Gen 3 path already works under.
        const uint8_t wantNature = nature();
        const uint32_t wantSlot = pid() & 1;
        for (int index = 0; index < 100000; ++index)
        {
            const uint32_t candidate = Utils::rand32();
            if (candidate % 25 != wantNature)
                continue;
            if ((candidate & 1) != wantSlot)
                continue;
            wr32(0x00, candidate);
            recalculateStats();
            refreshChecksum();
            return;
        }
    }

    void Pokemon4DP::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        const uint8_t wantNature = nature();
        const uint32_t wantSlot = pid() & 1;
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        if (makeShiny)
        {
            // CONSTRUCTED, not searched for. A random PID has to hit the nature (1 in 25), the ability
            // slot (1 in 2) and the shiny window (8 in 65536) at once, and a million random tries
            // missed all three often enough that "make shiny" silently did nothing about one time in
            // eleven. Picking the low half and deriving a high half inside the shiny window leaves
            // only the nature to match, and walking every low half from a random start always finds
            // one.
            const uint32_t start = Utils::rand32() & 0xFFFF;
            for (uint32_t offset = 0; offset < 0x10000; ++offset)
            {
                const uint32_t low = (start + offset) & 0xFFFF;
                if ((low & 1) != wantSlot)
                    continue;
                for (uint32_t shinyValue = 0; shinyValue < 8; ++shinyValue) // Gen 3-5 threshold is 8
                {
                    const uint32_t high = (low ^ trainerShinyValue ^ shinyValue) & 0xFFFF;
                    const uint32_t candidate = (high << 16) | low;
                    if (candidate % 25 != wantNature)
                        continue;
                    wr32(0x00, candidate);
                    recalculateStats();
                    refreshChecksum();
                    return;
                }
            }
            return;
        }
        // Not shiny: almost every random PID qualifies, so a search finds one at once.
        for (int index = 0; index < 1000000; ++index)
        {
            const uint32_t candidate = Utils::rand32();
            if (candidate % 25 != wantNature)
                continue;
            if ((candidate & 1) != wantSlot)
                continue;
            const uint32_t pokemonShinyValue = ((candidate >> 16) ^ (candidate & 0xFFFF));
            if (((trainerShinyValue ^ pokemonShinyValue) >> 3) == 0)
                continue;
            wr32(0x00, candidate);
            recalculateStats();
            refreshChecksum();
            return;
        }
    }

    void Pokemon4DP::setNature(uint8_t count) noexcept
    {
        if (count > 24)
            return;
        // Gen 4 stores no nature: it IS `PID % 25`, so the only way to change it is to rebuild the
        // PID -- which also re-rolls shininess. Destructive and irreversible, exactly like the Gen 3
        // down-convert, so the UI must confirm before calling this.
        const uint32_t wantSlot = pid() & 1;
        for (int index = 0; index < 100000; ++index)
        {
            const uint32_t candidate = Utils::rand32();
            if (candidate % 25 != count)
                continue;
            if ((candidate & 1) != wantSlot)
                continue;
            wr32(0x00, candidate);
            recalculateStats();
            refreshChecksum();
            return;
        }
    }
}
