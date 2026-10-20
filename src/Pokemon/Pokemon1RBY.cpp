#include "Pokemon/Pokemon1RBY.h"

#include "Pokemon/PersonalInfo1RBY.h" // Gen 1's own per-species table

#include "Pokemon/Experience.h"     // getLevelFromExp / getExpForLevel
#include "Utils/Gen1Text.h"         // the Gen 1 character sets
#include "Utils/StringHelpers.h"  // normalizeGameBoyName / utf8ToUtf16 -- shared with Gen 2

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

    const char *Pokemon1RBY::species() const noexcept { return Names::getSpeciesName(speciesID()); }

    uint8_t Pokemon1RBY::growthRate() const noexcept
    {
        const PersonalRecord &bs = getPersonalInfo1RBY(speciesID(), 0);
        return bs.growthRate;
    }

    // Three sources, in descending authority: the party level byte, the box level byte, then
    // EXP. A record can legitimately have only one of them set -- a box record never had the
    // party byte written -- and returning 0 would make recalculateStats() a no-op and every
    // stat read as 5.
    uint8_t Pokemon1RBY::level() const noexcept
    {
        if (const uint8_t levelValue = b8(0x21))
            return levelValue;
        if (const uint8_t lvBox = b8(0x03))
            return lvBox;
        return getLevelFromExp(exp(), growthRate());
    }

    void Pokemon1RBY::setLevel(uint8_t levelValue) noexcept
    {
        if (levelValue < 1)
            levelValue = 1;
        if (levelValue > 100)
            levelValue = 100;
        setB8(0x21, levelValue);
        setB8(0x03, levelValue); // both, always -- PKHeX's Stat_Level setter does the same
        wr24be(ofsBody() + 0x0E, getExpForLevel(levelValue, growthRate()) & 0xFFFFFFu);
        recalculateStats();
    }

    // Gen 1 stores no gender. This is what the Pokemon BECOMES when it is transferred forward,
    // fixed by the Attack DV against the species' gender ratio (PKHeX GBPKM.Gender).
    uint8_t Pokemon1RBY::gender() const noexcept
    {
        const PersonalRecord &bs = getPersonalInfo1RBY(speciesID(), 0);
        if (bs.hp == 0) // no row for this species -- the empty record, not a Pokemon with no HP
            return 2;
        switch (bs.genderRatio)
        {
        case 255:
            return 2;
        case 254:
            return 1; // always female
        case 0:
            return 0; // always male
        default:
            return ivATK() > (bs.genderRatio >> 4) ? 0 : 1;
        }
    }

    void Pokemon1RBY::setIV(int statIndex, uint8_t value) noexcept
    {
        // HP (index 0) is deliberately absent: its DV is the low bit of the other four and the
        // format has nowhere to store an independent value. Silently accepting a write would
        // report a value that the next read contradicts.
        if (value > 15)
            value = 15;
        int shift;
        switch (statIndex)
        {
        case 1:
            shift = 12;
            break; // ATK
        case 2:
            shift = 8;
            break; // DEF
        case 3:
            shift = 4;
            break; // SPE
        case 4:    // SpA and SpD are the SAME four bits -- Special.
        case 5:
            shift = 0;
            break;
        default:
            return;
        }
        const uint16_t dvBits = static_cast<uint16_t>((dv16() & ~(0xFu << shift)) | (static_cast<uint16_t>(value) << shift));
        setB16(0x1B, dvBits);
        recalculateStats();
    }

    // Stat Experience lives at 0x11 (HP), 0x13, 0x15, 0x17 and 0x19 (Special), 16 bits each.
    size_t Pokemon1RBY::statExpOffset(int statIndex) noexcept
    {
        switch (statIndex)
        {
        case 0:
            return 0x11; // HP
        case 1:
            return 0x13; // ATK
        case 2:
            return 0x15; // DEF
        case 3:
            return 0x17; // SPE
        case 4:          // SpA and SpD both map to the one Special counter.
        case 5:
            return 0x19;
        default:
            return static_cast<size_t>(-1);
        }
    }

    uint16_t Pokemon1RBY::evWide(int statIndex) const noexcept
    {
        const size_t offset = statExpOffset(statIndex);
        return offset == static_cast<size_t>(-1) ? 0 : b16(offset);
    }

    void Pokemon1RBY::setEVWide(int statIndex, uint16_t value) noexcept
    {
        const size_t offset = statExpOffset(statIndex);
        if (offset == static_cast<size_t>(-1))
            return;
        setB16(offset, value);
        recalculateStats();
    }

    void Pokemon1RBY::setMove(int slot, uint16_t moveID) noexcept
    {
        if (slot < 0 || slot > 3)
            return;
        // no Gen 1 id for it; storing one yields a glitch move
        if (moveID > MAX_MOVE_GEN1) return;
        setB8(0x08 + static_cast<size_t>(slot), static_cast<uint8_t>(moveID));
        // PP travels with the move. Leaving the old value behind gives the new move the previous one's PP.
        // PP Ups are cleared with it, because they were bought for a move that is no longer in the slot.
        setB8(0x1D + static_cast<size_t>(slot), getMovePPGen1(moveID) & 0x3F);
    }

    void Pokemon1RBY::setMovePP(int slot, uint8_t powerPoints) noexcept
    {
        if (slot < 0 || slot > 3)
            return;
        const size_t offset = 0x1D + static_cast<size_t>(slot);
        // only 6 bits are PP; the top 2 are PP Ups and must survive
        if (powerPoints > 63) powerPoints = 63;
        setB8(offset, static_cast<uint8_t>((b8(offset) & 0xC0) | powerPoints));
    }

    void Pokemon1RBY::setMovePPUps(int slot, uint8_t ppUps) noexcept
    {
        if (slot < 0 || slot > 3)
            return;
        if (ppUps > 3)
            ppUps = 3;
        const size_t offset = 0x1D + static_cast<size_t>(slot);
        setB8(offset, static_cast<uint8_t>((b8(offset) & 0x3F) | (ppUps << 6)));
        // Raise current PP to the new maximum. Gen 1 applies PP Ups as min(7, base/5) each --
        // not the modern rule -- so the ceiling comes from the Gen 1 table.
        const uint8_t maxPP = getMovePPGen1WithUps(move(slot), ppUps);
        if (maxPP != 0 && movePP(slot) > maxPP)
            setMovePP(slot, maxPP);
    }

    std::u16string Pokemon1RBY::readName(size_t offset) const
    {
        // The in-game-trade marker is a whole-field flag, not a character, and only counts in
        // the first byte. PKHeX renders it '*'.
        if (rd8(offset) == Utils::GEN1_TRADE_OT)
            return std::u16string(1, u'*');
        const bool isJapanese = japanese();
        const size_t length = nameLen();
        std::u16string text;
        for (size_t index = 0; index < length; ++index)
        {
            const char16_t decodedChar = Utils::gen1ToChar(rd8(offset + index), isJapanese);
            // Gen 1 STOPS at a glyphless byte; it does not skip like Gen 3
            if (decodedChar == 0) break;
            text += decodedChar;
        }
        return text;
    }

    void Pokemon1RBY::writeName(size_t offset, const std::u16string &value) noexcept
    {
        const bool isJapanese = japanese();
        const size_t length = nameLen();
        size_t byteCount = 0;
        for (const char16_t character : value)
        {
            // the last byte is reserved for the terminator
            if (byteCount + 1 >= length) break;
            const uint8_t packedByte = Utils::charToGen1(character, isJapanese);
            // no Gen 1 glyph -- end the name here
            if (packedByte == Utils::GEN1_TERMINATOR) break;
            wr8(offset + byteCount, packedByte);
            ++byteCount;
        }
        if (byteCount < length)
            wr8(offset + byteCount, Utils::GEN1_TERMINATOR);

        // BYTES PAST THE TERMINATOR ARE LEFT ALONE. They are trash bytes -- whatever the Game Boy
        // happened to leave in the buffer -- and they are real stored data, not padding we own.
        // Clearing the field first (PKHeX's Clear50 option) looks tidier and is wrong here for two
        // reasons. It breaks the bank's byte-in == byte-out contract: a real OT field of
        // "KIASTA" 0x50 0x00 0x00 0x00 0x00 would come back 0x50-filled, so every record in a bank
        // would read as modified after a no-op edit. And the trash is what PKHeX inspects to guess
        // a Gen 1 record's language and whether it was nicknamed, so overwriting it destroys
        // evidence the legality layer needs. Writing a SHORTER name therefore leaves the tail of
        // the old one behind, exactly as the games themselves do -- readName stops at the
        // terminator, so it is invisible, which is the point.
    }

    bool Pokemon1RBY::canStoreNickname(const std::u16string &value) const noexcept
    {
        if (value.size() > nameLen() - 1)
            return false;
        const bool isJapanese = japanese();
        for (const char16_t character : value)
        {
            if (Utils::charToGen1(character, isJapanese) == Utils::GEN1_TERMINATOR)
                return false;
        }
        return true;
    }

    bool Pokemon1RBY::isNicknamed() const noexcept
    {
        // Gen 1 has no "is nicknamed" flag -- the games compare the stored name against the
        // species name, and so must we.
        //
        // A Japanese record cannot be compared: PKSE has no Gen 1 Japanese species names, so
        // there is nothing to compare against. It answers TRUE, deliberately. The two errors are
        // not symmetric -- calling a real nickname "not nicknamed" DESTROYS it on transfer,
        // while calling a species name a nickname merely keeps a redundant one the user can
        // clear. Bias toward the reversible mistake.
        if (japanese())
            return true;
        const std::u16string stored = Utils::normalizeGameBoyName(nickname());
        if (stored.empty())
            return false;
        return stored != Utils::normalizeGameBoyName(Utils::utf8ToUtf16(species()));
    }

    uint8_t Pokemon1RBY::baseHP() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.hp;
    }
    uint8_t Pokemon1RBY::baseATK() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.atk;
    }
    uint8_t Pokemon1RBY::baseDEF() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.def;
    }
    uint8_t Pokemon1RBY::baseSPE() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.spe;
    }
    // Both special slots report the ONE Special base stat. Gyarados is the standing example:
    // Gen 1 gives it Special 100, while the modern row says SpA 60 / SpD 100. Reading the
    // modern table here computes a wrong special stat and then a bogus legality error.
    uint8_t Pokemon1RBY::baseSPA() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.spa; // Gen 1's single Special, stored in both slots
    }
    uint8_t Pokemon1RBY::baseSPD() const noexcept
    {
        const PersonalRecord &b = getPersonalInfo1RBY(speciesID(), 0);
        return b.spa; // Gen 1's single Special, stored in both slots
    }

    void Pokemon1RBY::setSpecies(uint16_t national) noexcept
    {
        const uint8_t internal = nationalToG1(national);
        // not a Gen 1 species -- refuse rather than store a MissingNo.
        if (internal == 0) return;
        setB8(0x00, internal);
        // Types and catch rate are STORED per-Pokemon, copied from the base-stat table when the
        // Pokemon is created. Leaving the old ones behind produces a Pokemon whose typing and
        // catch rate belong to its previous species -- and the catch rate is also what becomes
        // its held item if it is ever traded up to Gen 2.
        if (const PersonalRecord &bs = getPersonalInfo1RBY(national, 0); bs.hp != 0)
        {
            // TWO CONVENTIONS MEET HERE. The table reports a mono-typed species as
            // type2 == TYPE_NONE, because that is what the rest of PKSE wants -- it is what stops
            // the badge row drawing "Normal Normal" for Rattata. The Gen 1 ROM stores a mono-type
            // as the SAME id TWICE. Writing the sentinel through pkseTypeToG1 turns it into Normal
            // (its answer for a type Gen 1 does not have), so a Pikachu would be stored as
            // Electric/Normal. Repeat type1 instead, which is what the games hold.
            const uint8_t secondType = (bs.type2 == PERSONAL_TYPE_NONE) ? bs.type1 : bs.type2;
            setB8(0x05, pkseTypeToG1(bs.type1));
            setB8(0x06, pkseTypeToG1(secondType));
            setB8(0x07, bs.catchRate);
        }
        syncListHeader(); // the list header carries the species too, and it is what the games read
        recalculateStats();
    }

    void Pokemon1RBY::setShiny(bool makeShiny, uint32_t) noexcept
    {
        // Gen 1 shininess is entirely a DV pattern, so this rewrites DVs -- and that changes the
        // HP DV (derived from their low bits) and therefore every stat. The trainer id argument
        // is ignored: unlike Gen 3+, no id participates.
        uint16_t dvBits = dv16();
        if (makeShiny)
        {
            dvBits = static_cast<uint16_t>((dvBits & 0xF000) | (10u << 8) | (10u << 4) | 10u); // DEF/SPE/SPC = 10
            dvBits = static_cast<uint16_t>(dvBits | 0x2000);                                   // ATK bit 1 set
        }
        else if ((dvBits & 0x2FFF) == 0x2AAA)
        {
            // Only perturb when it currently IS the shiny pattern, so clearing an already
            // non-shiny Pokemon leaves its DVs -- and its stats -- untouched.
            dvBits = static_cast<uint16_t>(dvBits & 0xFFF0); // Special DV 10 -> 0 breaks the pattern
        }
        setB16(0x1B, dvBits);
        recalculateStats();
    }

    void Pokemon1RBY::recalculateStats() noexcept
    {
        const PersonalRecord &bs = getPersonalInfo1RBY(speciesID(), 0);
        if (bs.hp == 0) // no row for this species -- the empty record, not a Pokemon with no HP
            return; // unknown species: leave the stored stats alone rather than zero them
        const uint8_t levelValue = level();
        if (levelValue == 0)
            return;

        // The games hold a ushort[256] table of squares and take the LOWEST index whose square
        // is >= the Stat Experience, then use a quarter of it. Done as an integer scan for
        // exactly that reason: ceil(sqrt()) in floating point disagrees at the perfect squares,
        // and the index is clamped to 255 (ceil(sqrt(65535)) would be 256).
        auto oneStat = [](uint16_t base, uint8_t determinantValue, uint16_t statExp, uint8_t level) -> uint16_t
        {
            uint32_t accumulator = 0;
            while (accumulator < 255u && accumulator * accumulator < statExp)
                ++accumulator;
            const uint32_t effort = accumulator >> 2;
            return static_cast<uint16_t>(((2u * (static_cast<uint32_t>(base) + determinantValue) + effort) * level / 100u) + 5u);
        };

        const uint16_t hitPoints = static_cast<uint16_t>(oneStat(bs.hp, ivHP(), evWide(0), levelValue) + 5 + levelValue);
        setB16(0x22, hitPoints);
        setB16(0x24, oneStat(bs.atk, ivATK(), evWide(1), levelValue));
        setB16(0x26, oneStat(bs.def, ivDEF(), evWide(2), levelValue));
        setB16(0x28, oneStat(bs.spe, ivSPE(), evWide(3), levelValue));
        setB16(0x2A, oneStat(bs.spa, ivSPA(), evWide(4), levelValue)); // one Special stat, one field

        // Current HP is clamped rather than refilled: a Pokemon holding more HP than its maximum
        // is what the games draw as a negative health bar, but silently healing an edited
        // Pokemon to full would discard a real stored value.
        if (statHPCurrent() > hitPoints)
            setB16(0x01, hitPoints);

        // The LEVEL BYTES ARE NOT TOUCHED. Recomputing stats is not a level change, and the two
        // level fields are not both live: a party record maintains 0x21 and leaves 0x03 stale
        // (a real Red save has 0x03 = 0 on a level 2 party member), while a box record has only
        // 0x03. Writing both here rewrote a byte the game never maintains and made an unedited
        // save fail to round-trip. setLevel() syncs them, because that IS a level change.
    }
}
