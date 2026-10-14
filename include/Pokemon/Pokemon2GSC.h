/**
 * Wraps a PokeList2 SINGLE-ENTRY LIST, not a bare record -- the same shape Gen 1 uses, and for
 * the same reason: the names live outside the body, so a bare body has nowhere to put them, and
 * the record's LENGTH is the only thing that says whether it is Japanese.
 *
 *   off 0     count            (1)
 *   off 1     species marker   (species id, 0xFF empty, 0xFD EGG)
 *   off 2     0xFF             (list cap)
 *   off 3     body             48 bytes, the party-size PK2
 *   off 51    OT name          11 bytes international / 6 Japanese
 *   off 51+L  nickname         same width
 *   total     73 international / 63 Japanese
 *
 * Offsets are PKHeX's PK2.cs / PokeList2.cs / GBPKM.cs.
 *
 * SHARED WITH GEN 1, and reused rather than reinvented: big-endian reads, 4-bit DVs with a
 * DERIVED HP DV, 16-bit Stat Experience instead of EVs, and the GB stat formula (PKHeX puts
 * GetStat and LoadStats on GBPKM, the common base of PK1 and PK2, so the arithmetic is literally
 * the same function).
 *
 * SIX THINGS DIFFER FROM GEN 1, and each is a trap if you carry a Gen 1 habit across:
 *
 *  1. THE BODY IS 48 BYTES, NOT 44, and the box body is 32, not 33. Neither list size matches
 *     either -- 73/63 here against Gen 1's 69/59.
 *  2. SPECIES IS THE NATIONAL DEX NUMBER. Gen 1's arbitrary internal index (Bulbasaur 0x99) is
 *     gone, so there is no conversion table to route through.
 *  3. SPA AND SPD ARE SEPARATE BASE STATS but share ONE DV and ONE Stat Experience value. Gen 1
 *     has a single Special for everything; Gen 2 splits the base stat only. So the SPD setters
 *     are no-ops and reading SpD gives a different number from SpA.
 *  4. EGGS EXIST, and the flag is the 0xFD marker in the LIST HEADER -- there is no egg bit in
 *     the body. syncListHeader() must not clobber it or every egg in the box hatches.
 *  5. HELD ITEMS, FRIENDSHIP AND POKERUS EXIST (0x01, 0x1B, 0x1C).
 *  6. MET DATA EXISTS IN CRYSTAL ONLY, packed into the 16-bit CaughtData at 0x1D. Gold/Silver
 *     leave it zero, which is what hasMetData() keys on -- 0 is a real met location in Crystal,
 *     so the whole field being zero is the only honest test.
 *
 * There is still no encryption and no checksum, so refreshChecksum() is repurposed to re-sync the
 * list header, keeping the codebase's "mutate, then refreshChecksum" call pattern correct.
 */
#ifndef POKEMON_POKEMON2_GSC_H
#define POKEMON_POKEMON2_GSC_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/PersonalInfo2GSC.h"
#include "Utils/Gen2Text.h" // GEN2_TRADE_OT -- the in-game-trade OT marker

namespace Pokemon
{

    inline constexpr size_t SIZE_2STORED = 32; // box body
    inline constexpr size_t SIZE_2PARTY = 48;  // party body -- what a single-entry list carries
    inline constexpr size_t SIZE_2ULIST = 73;  // 3 + 48 + 11 + 11   international
    inline constexpr size_t SIZE_2JLIST = 63;  // 3 + 48 +  6 +  6   Japanese

    inline constexpr bool isGen2ListSize(size_t byteCount) noexcept
    {
        return byteCount == SIZE_2ULIST || byteCount == SIZE_2JLIST;
    }

    /// List-header markers. 0xFD is Gen 2's ONLY egg storage -- the body has no egg bit.
    inline constexpr uint8_t GEN2_SLOT_EMPTY = 0xFF;
    inline constexpr uint8_t GEN2_SLOT_EGG = 0xFD;

    inline constexpr uint16_t MAX_SPECIES_GEN2 = 251;
    inline constexpr uint16_t MAX_MOVE_GEN2 = 251;

    class Pokemon2GSC final : public Pokemon
    {
    public:
        explicit Pokemon2GSC(std::span<const std::byte> raw)
        {
            dataSize = isGen2ListSize(raw.size()) ? raw.size() : SIZE_2ULIST;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            if (isGen2ListSize(raw.size()))
            {
                std::memcpy(buffer, raw.data(), dataSize);
            }
            else
            {
                buffer[1] = std::byte{GEN2_SLOT_EMPTY};
                buffer[2] = std::byte{GEN2_SLOT_EMPTY};
            }
            data = std::span<std::byte>(buffer, dataSize);
        }

        /// An empty list of the requested locale, for the creator / conversion paths.
        explicit Pokemon2GSC(bool japanese)
        {
            dataSize = japanese ? SIZE_2JLIST : SIZE_2ULIST;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            buffer[1] = std::byte{GEN2_SLOT_EMPTY};
            buffer[2] = std::byte{GEN2_SLOT_EMPTY};
            data = std::span<std::byte>(buffer, dataSize);
            // Name fields pad with the terminator, not zero -- 0x50 is what the games write and
            // what a round trip has to give back.
            for (size_t index = ofsOT(); index < dataSize; ++index)
                buffer[index] = std::byte{0x50};
        }

        ~Pokemon2GSC() override = default;
        Pokemon2GSC(const Pokemon2GSC &) = delete;
        Pokemon2GSC &operator=(const Pokemon2GSC &) = delete;
        Pokemon2GSC(Pokemon2GSC &&) noexcept = default;
        Pokemon2GSC &operator=(Pokemon2GSC &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            return std::make_unique<Pokemon2GSC>(std::span<const std::byte>(data.data(), dataSize));
        }

        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::GSC; }

        bool japanese() const noexcept { return dataSize == SIZE_2JLIST; }
        size_t nameLen() const noexcept { return japanese() ? 6u : 11u; }
        static constexpr size_t ofsBody() noexcept { return 3; }
        size_t ofsOT() const noexcept { return ofsBody() + SIZE_2PARTY; } // 51
        size_t ofsNick() const noexcept { return ofsOT() + nameLen(); }

        bool isValid() const noexcept
        {
            const uint8_t mark = rd8(1);
            return mark != 0 && mark != GEN2_SLOT_EMPTY && speciesID() != 0 && speciesID() <= MAX_SPECIES_GEN2;
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
            wr8(offset + 1, static_cast<uint8_t>(value & 0xFF));
        }
        void wr24be(size_t offset, uint32_t value) noexcept
        {
            wr8(offset, static_cast<uint8_t>((value >> 16) & 0xFF));
            wr8(offset + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
            wr8(offset + 2, static_cast<uint8_t>(value & 0xFF));
        }
        size_t b(size_t relativeOffset) const noexcept { return ofsBody() + relativeOffset; }

        uint16_t speciesID() const noexcept override { return rd8(b(0x00)); }
        void setSpecies(uint16_t value) noexcept override
        {
            if (value == 0 || value > MAX_SPECIES_GEN2)
                return;
            wr8(b(0x00), static_cast<uint8_t>(value));
            syncListHeader();
        }
        const char *species() const noexcept override;
        uint16_t heldItem() const noexcept override { return rd8(b(0x01)); }
        void setHeldItem(uint16_t value) noexcept override { wr8(b(0x01), static_cast<uint8_t>(value)); }
        uint16_t move(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(b(0x02) + slot) : 0;
        }
        void setMove(int slot, uint16_t moveId) noexcept override;
        uint8_t movePP(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? (rd8(b(0x17) + slot) & 0x3F) : 0;
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            const size_t offset = b(0x17) + slot;
            wr8(offset, static_cast<uint8_t>((rd8(offset) & 0xC0) | (powerPoints > 63 ? 63 : powerPoints)));
        }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? ((rd8(b(0x17) + slot) & 0xC0) >> 6) : 0;
        }
        void setMovePPUps(int slot, uint8_t value) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            const size_t offset = b(0x17) + slot;
            wr8(offset, static_cast<uint8_t>((rd8(offset) & 0x3F) | ((value & 3) << 6)));
        }

        uint32_t id32() const noexcept override { return rd16be(b(0x06)); }
        uint16_t tid16() const noexcept override { return rd16be(b(0x06)); }
        void setTID16(uint16_t value) noexcept override { wr16be(b(0x06), value); }
        uint16_t sid16() const noexcept override { return 0; } // Gen 2 has no secret ID
        void setId32(uint32_t value) noexcept override { wr16be(b(0x06), static_cast<uint16_t>(value & 0xFFFF)); }
        uint32_t exp() const noexcept override { return rd24be(b(0x08)); }
        void setExp(uint32_t value) noexcept override;
        uint8_t friendship() const noexcept override { return rd8(b(0x1B)); }
        void setFriendship(uint8_t value) noexcept override { wr8(b(0x1B), value); }
        uint8_t otFriendship() const noexcept override { return rd8(b(0x1B)); }
        void setOTFriendship(uint8_t value) noexcept override { wr8(b(0x1B), value); }

        uint16_t dv16() const noexcept { return rd16be(b(0x15)); }
        void setDV16(uint16_t value) noexcept { wr16be(b(0x15), value); }
        uint8_t maxIV() const noexcept override { return 15; }
        uint8_t ivATK() const noexcept override { return (dv16() >> 12) & 0xF; }
        uint8_t ivDEF() const noexcept override { return (dv16() >> 8) & 0xF; }
        uint8_t ivSPE() const noexcept override { return (dv16() >> 4) & 0xF; }
        /// Special is ONE DV in Gen 2 -- SpA and SpD read the same nibble, and setting SpD is a
        /// no-op. Only the BASE stats split.
        uint8_t ivSPA() const noexcept override { return dv16() & 0xF; }
        uint8_t ivSPD() const noexcept override { return dv16() & 0xF; }
        uint8_t ivHP() const noexcept override
        {
            return static_cast<uint8_t>(((ivATK() & 1) << 3) | ((ivDEF() & 1) << 2) | ((ivSPE() & 1) << 1) | (ivSPA() & 1));
        }
        void setIV(int statIndex, uint8_t value) noexcept override;

        uint16_t maxEV() const noexcept override { return 65535; }
        uint16_t evWide(int statIndex) const noexcept override;
        void setEVWide(int statIndex, uint16_t value) noexcept override;
        static uint8_t sat8(uint16_t value) noexcept { return value > 255 ? 255 : static_cast<uint8_t>(value); }
        uint8_t evHP() const noexcept override { return sat8(evWide(0)); }
        uint8_t evATK() const noexcept override { return sat8(evWide(1)); }
        uint8_t evDEF() const noexcept override { return sat8(evWide(2)); }
        uint8_t evSPE() const noexcept override { return sat8(evWide(3)); }
        uint8_t evSPA() const noexcept override { return sat8(evWide(4)); }
        uint8_t evSPD() const noexcept override { return sat8(evWide(5)); }
        void setEV(int statIndex, uint8_t value) noexcept override { setEVWide(statIndex, value); }

        bool isEgg() const noexcept override { return rd8(1) == GEN2_SLOT_EGG; }
        void setEgg(bool isEgg) noexcept override
        {
            if (isEgg)
                wr8(1, GEN2_SLOT_EGG);
            else
                wr8(1, speciesID() == 0 ? GEN2_SLOT_EMPTY : static_cast<uint8_t>(speciesID()));
        }

        uint16_t caughtData() const noexcept { return rd16be(b(0x1D)); }
        void setCaughtData(uint16_t value) noexcept { wr16be(b(0x1D), value); }
        uint8_t metLevel() const noexcept override { return (caughtData() >> 8) & 0x3F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            setCaughtData(static_cast<uint16_t>((caughtData() & 0xC0FF) | ((value & 0x3F) << 8)));
        }
        uint16_t metLocation() const noexcept override { return caughtData() & 0x7F; }
        void setMetLocation(uint16_t value) noexcept override
        {
            setCaughtData(static_cast<uint16_t>((caughtData() & 0xFF80) | (value & 0x7F)));
        }
        uint8_t otGender() const noexcept override { return (caughtData() >> 7) & 1; }
        void setOTGender(uint8_t value) noexcept override
        {
            setCaughtData(static_cast<uint16_t>((caughtData() & 0xFF7F) | ((value & 1) << 7)));
        }

        uint8_t level() const noexcept override { return rd8(b(0x1F)); }
        void setLevel(uint8_t levelValue) noexcept override;

        uint16_t statHPCurrent() const noexcept override { return rd16be(b(0x22)); }
        void setStatHPCurrent(uint16_t value) noexcept override { wr16be(b(0x22), value); }
        uint16_t statHPMax() const noexcept override { return rd16be(b(0x24)); }
        uint16_t statATK() const noexcept override { return rd16be(b(0x26)); }
        uint16_t statDEF() const noexcept override { return rd16be(b(0x28)); }
        uint16_t statSPE() const noexcept override { return rd16be(b(0x2A)); }
        uint16_t statSPA() const noexcept override { return rd16be(b(0x2C)); }
        uint16_t statSPD() const noexcept override { return rd16be(b(0x2E)); }

        bool isPokerusInfected() const noexcept override { return (rd8(b(0x1C)) & 0xF) != 0; }
        bool isPokerusCured() const noexcept override
        {
            return (rd8(b(0x1C)) & 0xF) == 0 && (rd8(b(0x1C)) >> 4) != 0;
        }
        bool hasPokerus() const noexcept override { return true; }
        void setPokerus(uint8_t value) noexcept override { wr8(b(0x1C), value); }

        std::u16string nickname() const override;
        void setNickname(const std::u16string &value) noexcept override;
        std::u16string otName() const override;
        void setOTName(const std::u16string &value) noexcept override;
        int getMaxNicknameLength() const noexcept override { return japanese() ? 5 : 10; }
        bool canStoreNickname(const std::u16string &value) const noexcept override;
        bool isNicknamed() const noexcept override;
        /// True when the OT field holds the in-game-trade marker rather than a name -- the same
        /// 0x5D Gen 1 uses. Legality cares, and so does the transfer out: Bank replaces such an
        /// OT with the localised "Trainer" rather than carrying a marker byte into Gen 7.
        bool isInGameTradeOT() const noexcept { return rd8(ofsOT()) == Utils::GEN2_TRADE_OT; }

        bool hasNature() const noexcept override { return false; }
        bool hasAbility() const noexcept override { return false; }
        bool hasBall() const noexcept override { return false; }
        bool hasPID() const noexcept override { return false; }
        bool hasOriginGame() const noexcept override { return false; } // a PK2 stores no version
        /// Gender is DERIVED from the Attack DV against the species ratio -- readable, not
        /// writable, so the editor shows the row read-only rather than offering a dead picker.
        bool hasStoredGender() const noexcept override { return false; }
        /// Crystal writes CaughtData; Gold/Silver leave it zero. 0 is a real met location, so the
        /// whole field being zero is the only honest "no met data" test (PKHeX: HasOriginalMetLocation).
        bool hasMetData() const noexcept override { return caughtData() != 0; }
        /// Crystal's caught data is a met location, a met level and a time of day -- no date,
        /// and no egg location anywhere in the format. Both rows would read "(none)" forever.
        bool hasMetDate() const noexcept override { return false; }
        bool hasEggData() const noexcept override { return false; }
        bool hasHeldItem() const noexcept override { return true; }
        bool hasFriendship() const noexcept override { return true; }

        /// Which language a PK2 is written in is not stored -- it is inferred from the RECORD
        /// WIDTH, exactly as in Gen 1: a Japanese list has 6-byte name fields where an
        /// international one has 11. Reported as PKHeX's language ids (1 = Japanese, 2 = English)
        /// with English standing for "some international language", because the format cannot say
        /// which. It has to answer something real: 0 is not a language any later format accepts,
        /// and a Poke Transporter transfer writes this straight into the Gen 7 record.
        uint8_t language() const noexcept override { return japanese() ? 1 : 2; }

        uint8_t gender() const noexcept override;
        const char *genderSymbol() const noexcept override;
        uint8_t nature() const noexcept override { return 0; }
        uint8_t statNature() const noexcept override { return 0; }
        uint16_t ability() const noexcept override { return 0; }
        /// Unown's letter, and nothing else, is a form here -- and it is DERIVED FROM THE DVs
        /// rather than stored, exactly as the games compute it (PKHeX GBPKM.GetUnownFormValue).
        /// Every other species is form 0. This has to be right on the way out: Poke Transporter
        /// carries the letter across to Gen 7, and answering 0 for all of them turns every
        /// transferred Unown into an A.
        uint8_t formID() const noexcept override { return unownForm(); }
        uint8_t form() const noexcept override { return unownForm(); }
        uint32_t pid() const noexcept override { return 0; }
        uint32_t encryptionConstant() const noexcept override { return 0; }

        uint8_t baseHP() const noexcept override { return personal().hp; }
        uint8_t baseATK() const noexcept override { return personal().atk; }
        uint8_t baseDEF() const noexcept override { return personal().def; }
        uint8_t baseSPE() const noexcept override { return personal().spe; }
        uint8_t baseSPA() const noexcept override { return personal().spa; }
        uint8_t baseSPD() const noexcept override { return personal().spd; }

        uint16_t checksum() const noexcept override { return 0; }
        uint16_t calculateChecksum() const noexcept override { return 0; }
        void refreshChecksum() noexcept override { syncListHeader(); }
        bool checksumValid() const noexcept override { return true; }
        bool isStructurallyValid() const noexcept override { return isValid(); }

        void recalculateStats() noexcept override;
        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override;
        void regeneratePID(uint32_t) noexcept override {} // no PID to regenerate
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;

    private:
        const PersonalRecord &personal() const noexcept
        {
            return getPersonalInfo2GSC(speciesID(), 0);
        }
        void syncListHeader() noexcept;
        uint8_t unownForm() const noexcept;
        static size_t statExpOffsetRel(int statIndex) noexcept;
        std::u16string readName(size_t offset) const;
        void writeName(size_t offset, const std::u16string &value) noexcept;
    };
}

#endif
