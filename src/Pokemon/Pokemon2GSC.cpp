#include <algorithm>
#include <cstring>

#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Experience.h"
#include "Names/MoveInfo.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName
#include "Utils/Gen2Text.h"
#include "Utils/StringHelpers.h" // normalizeGameBoyName / utf8ToUtf16 -- shared with Gen 1

namespace Pokemon
{

    const char *Pokemon2GSC::species() const noexcept
    {
        return Trainer::getSpeciesName(speciesID());
    }

    void Pokemon2GSC::syncListHeader() noexcept
    {
        const uint16_t internalSpeciesId = speciesID();
        if (internalSpeciesId == 0)
        {
            wr8(0, 0);
            wr8(1, GEN2_SLOT_EMPTY);
        }
        else
        {
            wr8(0, 1);
            // Do NOT clobber an egg marker: 0xFD is the ONLY place Gen 2 records "this is an egg",
            // and stamping the species over it hatches the egg silently.
            if (rd8(1) != GEN2_SLOT_EGG)
                wr8(1, static_cast<uint8_t>(internalSpeciesId));
        }
        // Exactly one cap byte -- the games write one, and filling the rest of the list to
        // capacity is one of the five things the Gen 1 round trip caught.
        wr8(2, GEN2_SLOT_EMPTY);
    }

    size_t Pokemon2GSC::statExpOffsetRel(int statIndex) noexcept
    {
        // Accessor order HP ATK DEF SPE SPA SPD -> body offsets. SPA and SPD share ONE field:
        // Gen 2 splits the base stat but not the training.
        switch (statIndex)
        {
        case 0:
            return 0x0B; // HP
        case 1:
            return 0x0D; // ATK
        case 2:
            return 0x0F; // DEF
        case 3:
            return 0x11; // SPE
        case 4:
        case 5:
            return 0x13; // SPC -- both Special slots
        default:
            return static_cast<size_t>(-1);
        }
    }

    uint16_t Pokemon2GSC::evWide(int statIndex) const noexcept
    {
        const size_t offset = statExpOffsetRel(statIndex);
        return offset == static_cast<size_t>(-1) ? 0 : rd16be(b(offset));
    }

    void Pokemon2GSC::setEVWide(int statIndex, uint16_t value) noexcept
    {
        const size_t offset = statExpOffsetRel(statIndex);
        if (offset == static_cast<size_t>(-1))
            return;
        // Writing SpD writes the shared Special field, exactly as writing SpA does. That is the
        // format, not a bug -- but it means "set SpD to X" also moves SpA.
        wr16be(b(offset), value);
        recalculateStats();
    }

    void Pokemon2GSC::setIV(int statIndex, uint8_t value) noexcept
    {
        if (value > 15)
            value = 15;
        uint16_t determinantValue = dv16();
        switch (statIndex)
        {
        case 1:
            determinantValue = static_cast<uint16_t>((determinantValue & 0x0FFF) | (value << 12));
            break; // ATK
        case 2:
            determinantValue = static_cast<uint16_t>((determinantValue & 0xF0FF) | (value << 8));
            break; // DEF
        case 3:
            determinantValue = static_cast<uint16_t>((determinantValue & 0xFF0F) | (value << 4));
            break; // SPE
        case 4:
        case 5:
            determinantValue = static_cast<uint16_t>((determinantValue & 0xFFF0) | value);
            break; // SPC (both)
        // HP is DERIVED from the low bit of the other four -- there is nothing to write.
        default:
            return;
        }
        setDV16(determinantValue);
        recalculateStats();
    }

    void Pokemon2GSC::setMove(int slot, uint16_t moveId) noexcept
    {
        if (slot < 0 || slot > 3)
            return;
        // no Gen 2 id -- storing one yields a glitch move
        if (moveId > MAX_MOVE_GEN2) return;
        wr8(b(0x02) + slot, static_cast<uint8_t>(moveId));
        // PP travels with the move; leaving the old value gives the new move the previous one's
        // PP. PP Ups are cleared with it -- they were bought for a move no longer in the slot.
        const uint8_t powerPoints = Names::getMoveBasePP(moveId, Enums::GameVersion::GSC);
        wr8(b(0x17) + slot, static_cast<uint8_t>(powerPoints > 63 ? 63 : powerPoints));
    }

    void Pokemon2GSC::setExp(uint32_t value) noexcept
    {
        // 24-bit field
        if (value > 0xFFFFFF) value = 0xFFFFFF;
        wr24be(b(0x08), value);
        wr8(b(0x1F), getLevelFromExp(value, personal().growthRate));
        recalculateStats();
    }

    void Pokemon2GSC::setLevel(uint8_t levelValue) noexcept
    {
        if (levelValue < 1)
            levelValue = 1;
        if (levelValue > 100)
            levelValue = 100;
        wr24be(b(0x08), getExpForLevel(levelValue, personal().growthRate) & 0xFFFFFF);
        wr8(b(0x1F), levelValue);
        recalculateStats();
    }

    void Pokemon2GSC::recalculateStats() noexcept
    {
        const PersonalRecord &p = personal();
        // unknown species: leave the stored stats rather than zero them
        if (p.hp == 0) return;
        const uint8_t levelValue = level();
        if (levelValue == 0 || levelValue > 100)
            return;

        // The games hold a ushort[256] table of squares and take the LOWEST index whose square is
        // >= the Stat Experience, then a quarter of it. Done as an integer scan for exactly that
        // reason: ceil(sqrt()) in floating point disagrees at the perfect squares, and the index
        // is clamped to 255 (ceil(sqrt(65535)) would be 256).
        auto oneStat = [](uint16_t base, uint8_t determinantValue, uint16_t statExp, uint8_t level) -> uint16_t
        {
            uint32_t accumulator = 0;
            while (accumulator < 255u && accumulator * accumulator < statExp)
                ++accumulator;
            const uint32_t effort = accumulator >> 2;
            return static_cast<uint16_t>(((2u * (static_cast<uint32_t>(base) + determinantValue) + effort) * level / 100u) + 5u);
        };

        const uint16_t hitPoints = static_cast<uint16_t>(oneStat(p.hp, ivHP(), evWide(0), levelValue) + 5 + levelValue);
        const uint16_t oldMax = statHPMax();
        wr16be(b(0x24), hitPoints);
        // Only re-top current HP when max HP moved -- otherwise an edit heals a fainted pokemon.
        if (oldMax != hitPoints)
            wr16be(b(0x22), hitPoints);

        wr16be(b(0x26), oneStat(p.atk, ivATK(), evWide(1), levelValue));
        wr16be(b(0x28), oneStat(p.def, ivDEF(), evWide(2), levelValue));
        wr16be(b(0x2A), oneStat(p.spe, ivSPE(), evWide(3), levelValue));
        // SpA and SpD use DIFFERENT base stats but the SAME DV and the SAME Stat Experience.
        // This is the one place Gen 2 is not Gen 1: there, Special is a single stat outright.
        wr16be(b(0x2C), oneStat(p.spa, ivSPA(), evWide(4), levelValue));
        wr16be(b(0x2E), oneStat(p.spd, ivSPD(), evWide(5), levelValue));
    }

    uint8_t Pokemon2GSC::gender() const noexcept
    {
        const uint8_t ratio = personal().genderRatio;
        if (ratio == 255) return 2;
        // always female
        if (ratio == 254) return 1;
        // always male
        if (ratio == 0) return 0;
        return ivATK() > (ratio >> 4) ? 0 : 1;
    }

    const char *Pokemon2GSC::genderSymbol() const noexcept
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

    bool Pokemon2GSC::isShiny(uint32_t, std::string) const noexcept
    {
        // PKHeX ShinyUtil.GetIsShinyGB: Spe/Def/Spc DVs all 10, and Atk in {2,3,6,7,10,11,14,15}.
        return ivDEF() == 10 && ivSPE() == 10 && ivSPA() == 10 && (ivATK() & 2) == 2;
    }

    void Pokemon2GSC::setShiny(bool makeShiny, uint32_t) noexcept
    {
        if (!makeShiny)
        {
            // nudge Defence off the pattern
            if (isShiny(0, "")) setIV(2, 9);
            return;
        }
        setDV16(static_cast<uint16_t>((dv16() & 0x0FFF) | (static_cast<uint16_t>(ivATK() | 2) << 12)));
        setIV(2, 10);
        setIV(3, 10);
        setIV(4, 10);
        recalculateStats();
    }

    // Unown's letter is packed two bits at a time out of four DVs, then divided by 10 -- the
    // games' own formula (PKHeX GBPKM.GetUnownFormValue). Nothing else in Gen 2 has a form.
    uint8_t Pokemon2GSC::unownForm() const noexcept
    {
        if (speciesID() != 201)
            return 0;
        uint16_t packed = 0;
        packed |= static_cast<uint16_t>((ivATK() & 0x6) << 5);
        packed |= static_cast<uint16_t>((ivDEF() & 0x6) << 3);
        packed |= static_cast<uint16_t>((ivSPE() & 0x6) << 1);
        packed |= static_cast<uint16_t>((ivSPA() & 0x6) >> 1);
        return static_cast<uint8_t>(packed / 10);
    }

    bool Pokemon2GSC::isNicknamed() const noexcept
    {
        // Gen 2 has no "is nicknamed" flag either, so the test is Gen 1's: compare the stored
        // name against the species name. Both generations must answer this the same way, which
        // is why the normalisation lives in Utils rather than once per class.
        //
        // A Japanese record cannot be compared -- PKSE has no Japanese species names -- and
        // answers TRUE deliberately, for the reason Gen 1 gives: calling a real nickname "not
        // nicknamed" DESTROYS it on transfer, while keeping a redundant one is reversible.
        if (japanese())
            return true;
        const std::u16string stored = Utils::normalizeGameBoyName(nickname());
        if (stored.empty())
            return false;
        return stored != Utils::normalizeGameBoyName(Utils::utf8ToUtf16(species()));
    }

    std::u16string Pokemon2GSC::readName(size_t offset) const
    {
        std::u16string out;
        const size_t byteCount = nameLen();
        for (size_t index = 0; index < byteCount; ++index)
        {
            const uint8_t byteVal = rd8(offset + index);
            if (byteVal == Utils::GEN2_TERMINATOR)
                break;
            const char16_t character = Utils::gen2ToChar(byteVal, japanese());
            // glyphless byte terminates in Gen 1/2, unlike Gen 3
            if (character == 0) break;
            out.push_back(character);
        }
        return out;
    }

    void Pokemon2GSC::writeName(size_t offset, const std::u16string &value) noexcept
    {
        const size_t byteCount = nameLen();
        // Preserve the field's existing terminator style: the games write 0x50, but a save may
        // legitimately hold 0x00, and re-terminating with the wrong one is a round-trip diff.
        // (One of five bugs found by writing real Gen 1 saves back unedited.)
        bool zeroed = false;
        for (size_t index = 0; index < byteCount; ++index)
        {
            if (rd8(offset + index) == 0)
            {
                zeroed = true;
                break;
            }
        }
        const uint8_t pad = zeroed ? 0x00 : Utils::GEN2_TERMINATOR;
        size_t writeIndex = 0;
        for (; writeIndex + 1 < byteCount && writeIndex < value.size(); ++writeIndex)
        {
            const uint8_t encodedByte = Utils::charToGen2(value[writeIndex], japanese());
            if (encodedByte == 0)
                break;
            wr8(offset + writeIndex, encodedByte);
        }
        for (; writeIndex < byteCount; ++writeIndex)
            wr8(offset + writeIndex, pad);
    }

    std::u16string Pokemon2GSC::nickname() const { return readName(ofsNick()); }
    void Pokemon2GSC::setNickname(const std::u16string &value) noexcept { writeName(ofsNick(), value); }
    std::u16string Pokemon2GSC::otName() const { return readName(ofsOT()); }
    void Pokemon2GSC::setOTName(const std::u16string &value) noexcept { writeName(ofsOT(), value); }

    bool Pokemon2GSC::canStoreNickname(const std::u16string &value) const noexcept
    {
        return Utils::gen2CanEncode(value, japanese());
    }
}
