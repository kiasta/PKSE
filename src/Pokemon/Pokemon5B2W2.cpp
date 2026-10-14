#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon5B2W2.h"
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

    void Pokemon5B2W2::regeneratePID(uint32_t trainerID32) noexcept
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

    void Pokemon5B2W2::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        const uint8_t wantNature = nature();
        const uint32_t wantSlot = pid() & 1;
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        if (makeShiny)
        {
            // CONSTRUCTED, not searched for: a random PID has to land in the shiny window (8 in 65536)
            // while also matching the two constraints below, and a million random tries missed often
            // enough that "make shiny" silently did nothing about one time in eleven. Picking the low
            // half and deriving a high half inside the window leaves one constraint to match, and
            // walking every low half from a random start always finds one.
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

}
