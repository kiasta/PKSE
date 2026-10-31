#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon4HGSS.h"
#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Pokemon/PIDSearch.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PokemonTypes.h"
#include "Utils/Gen4Text.h"
#include "Utils/HelperUtilities.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName

namespace Pokemon
{

    namespace
    {
        /// Where HeartGold/SoulSilver hatch eggs: PKHeX EggHatchLocation4.IsValidMet4HGSS, whose
        /// table marks exactly these met locations for HGSS.
        bool isHatchLocationHGSS(uint16_t metLocation)
        {
            constexpr uint16_t firstJohtoLocation = 126;
            constexpr uint16_t lastHatchLocation = 234;
            if (metLocation == 80 || (metLocation >= 112 && metLocation <= 116) || metLocation == 118 || metLocation == 121)
                return true;
            return metLocation >= firstJohtoLocation && metLocation <= lastHatchLocation && metLocation != 171 && metLocation != 233;
        }
    }

    const char *Pokemon4HGSS::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    std::u16string Pokemon4HGSS::readString(size_t offset, size_t units) const
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
            out.push_back(static_cast<char16_t>(Utils::gen4NormalizeGenderSymbol(decodedChar)));
        }
        return out;
    }

    void Pokemon4HGSS::writeString(size_t offset, size_t units, const std::u16string &newValue) noexcept
    {
        // PKHeX StringConverter4.SetString: a half-width name stores its gender signs as the
        // half-width glyphs, a full-width one as the ordinary signs.
        const bool halfWidthText = language() == static_cast<uint8_t>(Enums::LanguageID::Korean) || !isFullWidthText(newValue);
        size_t writeIndex = 0;
        for (; writeIndex < units && writeIndex < newValue.size(); ++writeIndex)
        {
            const uint16_t character = static_cast<uint16_t>(newValue[writeIndex]);
            const uint16_t encodedByte = Utils::charToGen4(halfWidthText ? Utils::gen4UnNormalizeGenderSymbol(character) : character);
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

    bool Pokemon4HGSS::canStoreNickname(const std::u16string &value) const noexcept
    {
        return Utils::gen4CanEncode(value);
    }

    const char *Pokemon4HGSS::genderSymbol() const noexcept
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

    uint8_t Pokemon4HGSS::abilityNumber() const noexcept
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

    int Pokemon4HGSS::natureModifier(int statIndex) const noexcept
    {
        return natureStatPercent(nature(), statIndex);
    }

    uint16_t Pokemon4HGSS::calculateChecksum() const noexcept
    {
        // PKHeX G4PKM: Add16 over the four blocks only, 0x08..0x88. The party tail is excluded.
        uint32_t sum = 0;
        for (size_t offset = 8; offset + 1 < Encryption::SIZE_STORED4_HGSS; offset += 2)
            sum += rd16(offset);
        return static_cast<uint16_t>(sum);
    }

    void Pokemon4HGSS::recalculateStats() noexcept
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

        const int hitPoints = hitPointsForBase(p.hp, ((2 * p.hp + ivHP() + (evHP() / 4)) * levelValue) / 100 + levelValue + 10);
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

    void Pokemon4HGSS::setExp(uint32_t value) noexcept
    {
        wr32(0x10, value);
        if (isPartySize())
            wr8(0x8C, getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon4HGSS::setLevel(uint8_t levelValue) noexcept
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

    bool Pokemon4HGSS::isShiny(uint32_t trainerID32, std::string) const noexcept
    {
        const uint32_t pidValue = pid();
        const uint32_t trainerShinyValue = ((trainerID32 >> 16) ^ (trainerID32 & 0xFFFF));
        const uint32_t pokemonShinyValue = ((pidValue >> 16) ^ (pidValue & 0xFFFF));
        return ((trainerShinyValue ^ pokemonShinyValue) >> 3) == 0; // Gen 3-5 threshold is 8
    }

    // Gen 4 builds shininess, gender, nature (PID % 25) and the ability slot (bit 0) from the PID, and
    // also stores the gender at 0x40, which PKHeX requires to agree with the PID's. Each caller names
    // what it changes and holds the rest; the stored gender is written to match the new PID.
    bool Pokemon4HGSS::rerollPID(int wantShiny, int wantGender, int wantNature, int wantAbilityBit, uint32_t trainerID32) noexcept
    {
        PIDRequirements requirements;
        requirements.trainerID32 = trainerID32;
        requirements.shinyThreshold = 8; // Gen 3-5
        requirements.wantShiny = wantShiny;
        requirements.genderRatio = personal().genderRatio;
        requirements.wantGender = wantGender;
        requirements.wantNature = wantNature;
        requirements.abilityBitIndex = 0;
        requirements.wantAbilityBit = wantAbilityBit;
        requirements.wantWurmpleEvolution = wurmpleEvolutionToHold(speciesID(), pid());
        uint32_t pidValue = 0;
        if (!findPID(requirements, pidValue))
        {
            Utils::logErrorToFile("Pokemon4HGSS::rerollPID: no PID holds shiny=" + std::to_string(wantShiny) + " gender=" + std::to_string(wantGender) + " nature=" + std::to_string(wantNature) + " abilityBit=" + std::to_string(wantAbilityBit) + "; the Pokemon is unchanged");
            return false;
        }
        wr32(0x00, pidValue);
        writeStoredGender(genderFromPID(pidValue, requirements.genderRatio));
        recalculateStats();
        refreshChecksum();
        return true;
    }

    // A legality touch-up asked for nothing to change, so it holds everything the PID decides.
    void Pokemon4HGSS::regeneratePID(uint32_t trainerID32) noexcept
    {
        rerollPID(isShiny(trainerID32, "") ? 1 : 0, gender(), nature(), static_cast<int>(pid() & 1), trainerID32);
    }

    void Pokemon4HGSS::setShiny(bool makeShiny, uint32_t trainerID32) noexcept
    {
        if (isShiny(trainerID32, "") == makeShiny)
            return;
        rerollPID(makeShiny ? 1 : 0, gender(), nature(), static_cast<int>(pid() & 1), trainerID32);
    }

    void Pokemon4HGSS::setNature(uint8_t natureValue) noexcept
    {
        if (natureValue > 24 || natureValue == nature())
            return;
        // Gen 4 stores no nature: it IS `PID % 25`, so changing it means a new PID. Shininess, gender
        // and the ability slot are held, so the nature is the only thing that changes.
        rerollPID(isShiny(id32(), "") ? 1 : 0, gender(), natureValue, static_cast<int>(pid() & 1), id32());
    }

    void Pokemon4HGSS::setAbilityNumber(uint8_t abilityNumber) noexcept
    {
        if (abilityNumber != 1 && abilityNumber != 2)
            return;
        const int wantAbilityBit = abilityNumber == 2 ? 1 : 0;
        if (static_cast<int>(pid() & 1) != wantAbilityBit && !rerollPID(isShiny(id32(), "") ? 1 : 0, gender(), nature(), wantAbilityBit, id32()))
            return;
        // The ability id follows the slot, as in PKHeX's PKM.RefreshAbility.
        const PersonalRecord &personalRecord = personal();
        setAbility(abilityNumber == 2 ? personalRecord.ability2 : personalRecord.ability1);
    }

    bool Pokemon4HGSS::wasCreatedInHGSS() const noexcept
    {
        constexpr uint16_t linkTradeEggLocation = 2002; // PKHeX Locations.LinkTrade4
        const uint8_t originVersion = originGame();
        if (Enums::getVersionGeneration(originVersion) == 3)
            return rd16(0x46) != 0; // PKHeX PossiblyPalParkHGSS
        const bool tradedEgg = eggLocation() == linkTradeEggLocation;
        if (originVersion == static_cast<uint8_t>(Enums::GameVersion::HG) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SS))
        {
            // An egg traded in and hatched outside HeartGold/SoulSilver keeps 0x86 empty.
            if (rd8(0x86) == 0 && tradedEgg && !isHatchLocationHGSS(rd16(0x46)))
                return false;
            return !isFatefulEncounter() || eggLocation() != 0; // Ranger Manaphy was the one egg ever distributed
        }
        // D/P/Pt: only an egg traded in and hatched in HeartGold/SoulSilver keeps one.
        return rd8(0x86) != 0 && tradedEgg && isHatchLocationHGSS(rd16(0x46));
    }

    void Pokemon4HGSS::setBall(uint8_t value) noexcept
    {
        constexpr uint8_t pokeBall = 4;
        constexpr uint8_t sportBall = 24;
        constexpr uint8_t cherishBall = 16;
        wr8(0x83, value <= cherishBall ? value : pokeBall);
        wr8(0x86, wasCreatedInHGSS() ? (value <= sportBall ? value : pokeBall) : 0);
    }

    void Pokemon4HGSS::setGender(uint8_t genderValue) noexcept
    {
        // A species whose gender is fixed, or a PID that already gives this gender: the stored bits
        // are all that change. Otherwise the PID has to give the new gender too.
        const uint8_t genderRatio = personal().genderRatio;
        if (genderValue > 1 || genderRatio == 0 || genderRatio == 254 || genderRatio == 255 || genderFromPID(pid(), genderRatio) == genderValue)
        {
            writeStoredGender(genderValue);
            refreshChecksum();
            return;
        }
        rerollPID(isShiny(id32(), "") ? 1 : 0, genderValue, nature(), static_cast<int>(pid() & 1), id32());
    }
}
