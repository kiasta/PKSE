/**
 * Wraps a PokeList1 SINGLE-ENTRY LIST, not a bare Pokemon record. That is what Gen 1 actually
 * stores and what PKSM hands us, and it is the only self-describing form: names live outside
 * the 44-byte body, so a bare body has nowhere to put them, and its LENGTH is the only thing
 * that says whether the record is Japanese.
 *
 *   off 0     count            (1)
 *   off 1     species marker   (internal index, or 0xFF for an empty slot)
 *   off 2     0xFF             (list cap)
 *   off 3     body             44 bytes, the party-size PK1
 *   off 47    OT name          11 bytes international / 6 Japanese
 *   off 47+L  nickname         same width
 *   total     69 international / 59 Japanese
 *
 * Offsets and semantics are PKHeX's PK1.cs + PokeList1.cs + GBPKM.cs. FIVE THINGS WILL BITE
 * ANYONE CARRYING HABITS FROM A LATER GENERATION HERE:
 *
 *  1. EVERY MULTI-BYTE FIELD IS BIG ENDIAN. Every other format PKSE handles is little endian.
 *     A byte-swapped read does not fail, it just yields a plausible wrong number -- 0x0016
 *     read backwards is 5632, which still looks like data.
 *  2. THERE IS NO ENCRYPTION AND NO CHECKSUM. Nothing to decrypt on the way in, nothing to
 *     refresh on the way out. refreshChecksum() is repurposed (see below) rather than left
 *     empty, so the ~200 existing "mutate then refreshChecksum" call sites stay correct.
 *  3. DVs ARE 4 BITS, NOT 5, and HP's is DERIVED from the low bit of the other four -- it is
 *     not stored and cannot be set independently. maxIV() reports 15 so no editor writes 31.
 *  4. THERE ARE NO EVs. Gen 1 has Stat Experience: a 16-bit counter per stat, capped at 65535,
 *     contributing through a square root. It is not a 0-255 EV and must not be narrowed into
 *     one -- use evWide()/setEVWide(). The 8-bit accessors saturate and say so.
 *  5. SPECIAL IS ONE STAT. There is no SpA/SpD split, so both interface slots read and write
 *     the same underlying value. Writing SpA and then SpD does not give you two numbers.
 *
 * What Gen 1 simply does not have, reported via the hasXXX() predicates rather than as 0:
 * nature, ability, held item, friendship, ball, met location/level/date, origin game, egg,
 * Pokerus, forms, and a secret ID. Shininess is not a Gen 1 concept either, but the DV pattern
 * that MAKES a transferred pokemon shiny is checkable here, so isShiny() answers it (PKHeX does the
 * same) -- read it as "will be shiny when it leaves", not "is shiny in Red".
 */
#ifndef POKEMON_POKEMON1_RBY_H
#define POKEMON_POKEMON1_RBY_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/Gen1Tables.h"

namespace Pokemon
{

    inline constexpr size_t SIZE_1STORED = 33; // box body; PKSE never holds one of these alone
    inline constexpr size_t SIZE_1PARTY = 44;  // party body -- what a single-entry list carries
    inline constexpr size_t SIZE_1ULIST = 69;  // 3 + 44 + 11 + 11   international
    inline constexpr size_t SIZE_1JLIST = 59;  // 3 + 44 +  6 +  6   Japanese

    /// True for the two lengths a single-entry PokeList1 can have. Anything else is not one.
    inline constexpr bool isGen1ListSize(size_t byteCount) noexcept
    {
        return byteCount == SIZE_1ULIST || byteCount == SIZE_1JLIST;
    }

    class Pokemon1RBY final : public Pokemon
    {
    public:
        /**
         * Takes a PokeList1 single-entry list, 69 or 59 bytes, VERBATIM -- no decryption step,
         * because Gen 1 has none. A length that is neither is accepted rather than rejected (the
         * base class has no way to report a failed construction) and normalised to an empty
         * international list, so the object is always safe to read; isValid() reports the truth.
         */
        explicit Pokemon1RBY(std::span<const std::byte> raw)
        {
            dataSize = isGen1ListSize(raw.size()) ? raw.size() : SIZE_1ULIST;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            if (isGen1ListSize(raw.size()))
            {
                std::memcpy(buffer, raw.data(), dataSize);
            }
            else
            {
                buffer[0] = std::byte{0};    // count 0
                buffer[1] = std::byte{0xFF}; // empty marker
                buffer[2] = std::byte{0xFF}; // list cap
            }
            data = std::span<std::byte>(buffer, dataSize);
        }

        /// Builds an empty list of the requested locale, for the creator / conversion paths.
        explicit Pokemon1RBY(bool japanese)
        {
            dataSize = japanese ? SIZE_1JLIST : SIZE_1ULIST;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            buffer[1] = std::byte{0xFF};
            buffer[2] = std::byte{0xFF};
            data = std::span<std::byte>(buffer, dataSize);
            // Name fields pad with the terminator, not with zero -- 0x00 happens to read as a
            // terminator too, but 0x50 is what the games write and what a round trip must return.
            for (size_t index = ofsOT(); index < dataSize; ++index)
                buffer[index] = std::byte{0x50};
        }

        ~Pokemon1RBY() override = default;
        Pokemon1RBY(const Pokemon1RBY &) = delete;
        Pokemon1RBY &operator=(const Pokemon1RBY &) = delete;
        Pokemon1RBY(Pokemon1RBY &&) noexcept = default;
        Pokemon1RBY &operator=(Pokemon1RBY &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            return std::make_unique<Pokemon1RBY>(std::span<const std::byte>(data.data(), dataSize));
        }

        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::RBY; }

        bool japanese() const noexcept { return dataSize == SIZE_1JLIST; }
        size_t nameLen() const noexcept { return japanese() ? 6u : 11u; }
        static constexpr size_t ofsBody() noexcept { return 3; }
        size_t ofsOT() const noexcept { return ofsBody() + SIZE_1PARTY; } // 47
        size_t ofsNick() const noexcept { return ofsOT() + nameLen(); }   // 58 / 53

        /// A well-formed, occupied single-entry list. A marker of 0 or 0xFF means empty.
        bool isValid() const noexcept
        {
            const uint8_t mark = rd8(1);
            return mark != 0 && mark != 0xFF && speciesID() != 0 && speciesID() <= MAX_SPECIES_GEN1;
        }

        uint8_t rd8(size_t offset) const noexcept { return offset < dataSize ? static_cast<uint8_t>(data[offset]) : 0; }
        uint16_t rd16be(size_t offset) const noexcept
        {
            return static_cast<uint16_t>((static_cast<uint16_t>(rd8(offset)) << 8) | rd8(offset + 1));
        }
        uint32_t rd24be(size_t offset) const noexcept
        {
            return (static_cast<uint32_t>(rd8(offset)) << 16) | (static_cast<uint32_t>(rd8(offset + 1)) << 8) | rd8(offset + 2);
        }
        void wr8(size_t offset, uint8_t value) noexcept
        {
            if (offset < dataSize)
                data[offset] = static_cast<std::byte>(value);
        }
        void wr16be(size_t offset, uint16_t value) noexcept
        {
            wr8(offset, static_cast<uint8_t>(value >> 8));
            wr8(offset + 1, static_cast<uint8_t>(value));
        }
        void wr24be(size_t offset, uint32_t value) noexcept
        {
            wr8(offset, static_cast<uint8_t>(value >> 16));
            wr8(offset + 1, static_cast<uint8_t>(value >> 8));
            wr8(offset + 2, static_cast<uint8_t>(value));
        }

        // Body-relative accessors. Every offset below is quoted as PKHeX names it in PK1.cs.
        uint8_t b8(size_t bodyOfs) const noexcept { return rd8(ofsBody() + bodyOfs); }
        uint16_t b16(size_t bodyOfs) const noexcept { return rd16be(ofsBody() + bodyOfs); }
        void setB8(size_t bodyOfs, uint8_t value) noexcept { wr8(ofsBody() + bodyOfs, value); }
        void setB16(size_t bodyOfs, uint16_t value) noexcept { wr16be(ofsBody() + bodyOfs, value); }

        uint8_t speciesInternal() const noexcept { return b8(0x00); }
        uint16_t speciesID() const noexcept override { return g1ToNational(speciesInternal()); }
        const char *species() const noexcept override;
        uint8_t formID() const noexcept override { return 0; } // Gen 1 has no forms at all
        uint8_t form() const noexcept override { return 0; }

        // Gen 1 has a 16-bit trainer id and NO secret id. id32() is therefore the TID alone --
        // NOT zero-extended garbage in the high half, and shininess never depends on it.
        uint16_t tid16() const noexcept override { return b16(0x0C); }
        uint16_t sid16() const noexcept override { return 0; }
        uint32_t id32() const noexcept override { return tid16(); }
        void setTID16(uint16_t value) noexcept override { setB16(0x0C, value); }
        void setSID16(uint16_t) noexcept override {} // no such field
        void setId32(uint32_t value) noexcept override { setTID16(static_cast<uint16_t>(value & 0xFFFF)); }

        // EXP is 3 bytes big endian at 0x0E. The byte at 0x11 immediately after it is the first
        // half of EV_HP, so a 4-byte write here silently corrupts HP Stat Experience.
        uint32_t exp() const noexcept override { return rd24be(ofsBody() + 0x0E); }
        /// Writes both level bytes to the level the new EXP gives, as PKHeX PK1.Stat_Level does:
        /// level() reads them before the EXP, so leaving them would keep showing the old level.
        void setExp(uint32_t value) noexcept override;

        uint8_t level() const noexcept override;
        void setLevel(uint8_t levelValue) noexcept override;
        uint8_t status() const noexcept { return b8(0x04); }

        // The stored type bytes, mapped from ROM numbering to PKSE TYPE_* ids. The ROM ids are
        // NOT the modern ones (Bug is 7, Ghost 8, and the special types start at 20), so these
        // are converted, never returned raw. They normally agree with the species table; a
        // disagreement is a real signal for the legality layer, which is why they are exposed.
        uint8_t storedType1() const noexcept { return g1TypeToPKSE(b8(0x05)); }
        uint8_t storedType2() const noexcept { return g1TypeToPKSE(b8(0x06)); }

        // Catch rate. A real gameplay field in Gen 1 -- and the byte that BECOMES the held item
        // when the Pokemon is traded up to Gen 2, which is why it is not padding and must survive
        // a round trip untouched.
        uint8_t catchRate() const noexcept { return b8(0x07); }
        void setCatchRate(uint8_t value) noexcept { setB8(0x07, value); }

        uint16_t heldItem() const noexcept override { return 0; }
        uint16_t ability() const noexcept override { return 0; }
        uint8_t nature() const noexcept override { return 0; }
        uint8_t statNature() const noexcept override { return 0; }
        uint32_t pid() const noexcept override { return 0; }
        uint32_t encryptionConstant() const noexcept override { return 0; }
        uint8_t friendship() const noexcept override { return 0; }
        uint8_t otFriendship() const noexcept override { return 0; }
        uint8_t ball() const noexcept override { return 0; }
        uint16_t metLocation() const noexcept override { return 0; }
        uint8_t metLevel() const noexcept override { return 0; }
        uint8_t originGame() const noexcept override { return 0; }
        uint8_t otGender() const noexcept override { return 0; }
        bool isEgg() const noexcept override { return false; }
        bool isPokerusInfected() const noexcept override { return false; }
        bool isPokerusCured() const noexcept override { return false; }
        bool hasPokerus() const noexcept override { return false; }

        bool hasNature() const noexcept override { return false; }
        bool hasAbility() const noexcept override { return false; }
        bool hasHeldItem() const noexcept override { return false; }
        bool hasFriendship() const noexcept override { return false; }
        bool hasBall() const noexcept override { return false; }
        bool hasMetData() const noexcept override { return false; }
        bool hasMetDate() const noexcept override { return false; }
        bool hasEggData() const noexcept override { return false; }
        bool hasOriginGame() const noexcept override { return false; }
        bool hasPID() const noexcept override { return false; }
        bool hasStoredGender() const noexcept override { return false; } // derived from the Attack DV

        // Language is not stored either. The RECORD WIDTH says Japanese or not (a 59-byte list is
        // Japanese); an international record's language is inferred from its names as PKHeX
        // GBPKM.Language does, falling back to English. Always a real PKHeX language id: 0 is not
        // one any later format accepts, and Poke Transporter writes this into the Gen 7 record.
        uint8_t language() const noexcept override;

        // Gen 1 has no gender whatsoever; the concept arrives in Gen 2. What a Gen 1 Pokemon
        // BECOMES on transfer is fixed by its Attack DV against the species' Gen 2 gender ratio,
        // and that ratio is the byte PKHeX substitutes into personal_rb (see Gen1Tables.h).
        uint8_t gender() const noexcept override;
        const char *genderSymbol() const noexcept override
        {
            const uint8_t genderValue = gender();
            return genderValue == 0 ? "♂" : genderValue == 1 ? "♀" : "";
        }

        uint16_t dv16() const noexcept { return b16(0x1B); }
        void setDV16(uint16_t value) noexcept
        {
            setB16(0x1B, value);
            recalculateStats();
        }
        uint8_t maxIV() const noexcept override { return 15; }

        uint8_t ivATK() const noexcept override { return static_cast<uint8_t>((dv16() >> 12) & 0xF); }
        uint8_t ivDEF() const noexcept override { return static_cast<uint8_t>((dv16() >> 8) & 0xF); }
        uint8_t ivSPE() const noexcept override { return static_cast<uint8_t>((dv16() >> 4) & 0xF); }
        uint8_t ivSPA() const noexcept override { return static_cast<uint8_t>((dv16() >> 0) & 0xF); } // Special
        uint8_t ivSPD() const noexcept override { return ivSPA(); }                                   // same field
        /// HP's DV is not stored -- it is the four other DVs' low bits, so it cannot be set.
        uint8_t ivHP() const noexcept override
        {
            const uint16_t dvBits = dv16();
            return static_cast<uint8_t>((((dvBits >> 12) & 1) << 3) | (((dvBits >> 8) & 1) << 2) | (((dvBits >> 4) & 1) << 1) | ((dvBits >> 0) & 1));
        }
        void setIV(int statIndex, uint8_t value) noexcept override;

        uint16_t maxEV() const noexcept override { return 65535; }
        uint16_t evWide(int statIndex) const noexcept override;
        void setEVWide(int statIndex, uint16_t value) noexcept override;
        /// Lossy on purpose: Stat Experience does not fit in a byte. SATURATES rather than
        /// truncating, so the narrow view is at least monotonic, and callers that care use
        /// evWide(). 300 Stat Exp truncated to 8 bits would read as 44, which is worse than 255.
        uint8_t evHP() const noexcept override { return sat8(evWide(0)); }
        uint8_t evATK() const noexcept override { return sat8(evWide(1)); }
        uint8_t evDEF() const noexcept override { return sat8(evWide(2)); }
        uint8_t evSPE() const noexcept override { return sat8(evWide(3)); }
        uint8_t evSPA() const noexcept override { return sat8(evWide(4)); }
        uint8_t evSPD() const noexcept override { return sat8(evWide(5)); }
        void setEV(int statIndex, uint8_t value) noexcept override { setEVWide(statIndex, value); }

        uint16_t move(int slot) const noexcept override
        {
            return (slot < 0 || slot > 3) ? 0 : b8(0x08 + static_cast<size_t>(slot));
        }
        uint8_t movePP(int slot) const noexcept override
        {
            return (slot < 0 || slot > 3) ? 0 : static_cast<uint8_t>(b8(0x1D + static_cast<size_t>(slot)) & 0x3F);
        }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot < 0 || slot > 3) ? 0 : static_cast<uint8_t>(b8(0x1D + static_cast<size_t>(slot)) >> 6);
        }
        void setMove(int slot, uint16_t moveID) noexcept override;
        void setMovePP(int slot, uint8_t powerPoints) noexcept override;
        void setMovePPUps(int slot, uint8_t ppUps) noexcept override;

        std::u16string nickname() const override { return readName(ofsNick()); }
        std::u16string otName() const override { return readName(ofsOT()); }
        void setNickname(const std::u16string &value) noexcept override { writeName(ofsNick(), value); }
        void setOTName(const std::u16string &value) noexcept override { writeName(ofsOT(), value); }
        int getMaxNicknameLength() const noexcept override { return static_cast<int>(nameLen()) - 1; }
        bool canStoreNickname(const std::u16string &value) const noexcept override;
        bool isNicknamed() const noexcept override;
        /// True when the OT field holds the in-game-trade marker rather than a name. Legality
        /// cares: such a Pokemon was received in a trade inside the game, not caught.
        bool isInGameTradeOT() const noexcept { return rd8(ofsOT()) == 0x5D; }

        uint8_t baseHP() const noexcept override;
        uint8_t baseATK() const noexcept override;
        uint8_t baseDEF() const noexcept override;
        uint8_t baseSPE() const noexcept override;
        uint8_t baseSPA() const noexcept override;
        uint8_t baseSPD() const noexcept override;

        uint16_t statHPMax() const noexcept override { return b16(0x22); }
        uint16_t statATK() const noexcept override { return b16(0x24); }
        uint16_t statDEF() const noexcept override { return b16(0x26); }
        uint16_t statSPE() const noexcept override { return b16(0x28); }
        uint16_t statSPA() const noexcept override { return b16(0x2A); } // Special
        uint16_t statSPD() const noexcept override { return b16(0x2A); } // same field
        uint16_t statHPCurrent() const noexcept override { return b16(0x01); }
        void setStatHPCurrent(uint16_t value) noexcept override { setB16(0x01, value); }

        // A Gen 1 Pokemon is never shiny in Red/Blue -- the games have no such concept. This
        // reports the DV pattern that WILL make it shiny once it reaches a game that does, which
        // is PKHeX's behaviour and the only useful answer. Both arguments are ignored: Gen 1
        // shininess is purely DV-based and involves no trainer id.
        bool isShiny(uint32_t, std::string) const noexcept override
        {
            return (dv16() & 0x2FFF) == 0x2AAA; // DEF/SPE/SPC all 10, ATK bit 1 set
        }
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;
        void regeneratePID(uint32_t) noexcept override {} // no PID exists to regenerate

        // Nothing here is a checksum. refreshChecksum() is repurposed to restore the invariant
        // Gen 1 DOES have -- the list header's species marker must match the body -- so that the
        // established "mutate, then refreshChecksum()" call pattern keeps a record well-formed
        // instead of quietly leaving a stale marker behind.
        uint16_t checksum() const noexcept override { return 0; }
        uint16_t calculateChecksum() const noexcept override { return 0; }
        bool checksumValid() const noexcept override { return true; }
        /// Gen 1's stand-in for a checksum. There is no redundancy in the format to verify, so
        /// this is what internal consistency there is: a well-formed list header naming a real
        /// species, at a level the games can produce. It is weaker than a checksum and says so --
        /// but it rejects the zero-filled and 0xFF-filled records that a damaged bank produces,
        /// which a checksum-only test would wave through for this format.
        bool isStructurallyValid() const noexcept override
        {
            if (!isValid())
                return false;
            // header marker vs body disagree
            if (rd8(1) != speciesInternal()) return false;
            const uint8_t levelValue = level();
            return levelValue >= 1 && levelValue <= 100;
        }
        void refreshChecksum() noexcept override { syncListHeader(); }
        void syncListHeader() noexcept
        {
            const uint8_t internalSpeciesId = speciesInternal();
            wr8(0, internalSpeciesId == 0 ? 0u : 1u);    // count
            wr8(1, internalSpeciesId == 0 ? 0xFFu : internalSpeciesId); // slot marker
            wr8(2, 0xFF);                 // list cap
        }

        void setSpecies(uint16_t national) noexcept override;
        void setForm(uint8_t) noexcept override {}
        void setHeldItem(uint16_t) noexcept override {}
        void setAbility(uint16_t) noexcept override {}
        void setNature(uint8_t) noexcept override {}
        void setStatNature(uint8_t) noexcept override {}
        void setPID(uint32_t) noexcept override {}
        void setEncryptionConstant(uint32_t) noexcept override {}
        void setFriendship(uint8_t) noexcept override {}
        void setEgg(bool) noexcept override {}
        void setGender(uint8_t) noexcept override {} // derived from the Attack DV; set that
        void setBall(uint8_t) noexcept override {}
        void setMetLocation(uint16_t) noexcept override {}
        void setMetLevel(uint8_t) noexcept override {}
        void setOriginGame(uint8_t) noexcept override {}

        void recalculateStats() noexcept override;

        /// Growth rate for this species, from Gen 1's own table. 0 (Medium Fast) if unknown --
        /// callers should gate on isValid() rather than relying on that.
        uint8_t growthRate() const noexcept;

    private:
        static uint8_t sat8(uint16_t value) noexcept { return static_cast<uint8_t>(value > 255 ? 255 : value); }
        std::u16string readName(size_t offset) const;
        void writeName(size_t offset, const std::u16string &value) noexcept;
        /// Offset of one stat's Stat Experience field, or SIZE_MAX for an index with no field.
        static size_t statExpOffset(int statIndex) noexcept;
    };
}

#endif
