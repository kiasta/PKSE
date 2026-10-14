/**
 * Layout: 8-byte header, four 32-byte blocks (0x08-0x87), then a 100-byte party tail.
 *   stored 136 (0x88)   party 236 (0xEC)
 *
 * FIVE THINGS DIFFER FROM THE GEN 8/9 CLASSES THIS IS MODELLED ON, and each is silent if missed:
 *
 *  1. THE CRYPT SEED IS THE CHECKSUM, not the PID -- so refreshChecksum() must run BEFORE the
 *     record is encrypted. See Encryption4PT.h.
 *  2. NATURE IS DERIVED: `PID % 25`, exactly as in Gen 3. There is no nature byte, so setNature()
 *     has to rebuild the PID rather than write a field. The PID also decides shininess, gender and
 *     the ability slot, so every rebuild goes through rerollPID, which holds the ones not being
 *     changed (and setGender rebuilds it too, since PKHeX requires the two genders to agree).
 *  3. THE EGG AND NICKNAMED FLAGS LIVE IN THE IV WORD at 0x38, bits 30 and 31. Rebuilding IV32
 *     from six IV values clears both. Always read-modify-write.
 *  4. THERE ARE TWO BALL FIELDS. 0x83 is the DP/Pt ball; 0x86 is the one HGSS added for Apricorn
 *     balls, and DP/Pt leave it zero. ball() prefers 0x86 when non-zero; setBall() writes both.
 *  5. THERE IS NO HYPER TRAINING and no handler (HT). hasHyperTraining() stays false and the
 *     HT accessors report absence, so effectiveIV() is the raw IV and the details view hides the
 *     HT row rather than inventing an empty one.
 */
#ifndef POKEMON_POKEMON4_PT_H
#define POKEMON_POKEMON4_PT_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Pokemon/PersonalInfo4PT.h"
#include "Encryption/Encryption4PT.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

namespace Pokemon
{

    class Pokemon4PT final : public Pokemon
    {
    public:
        /// Takes ENCRYPTED bytes, 136 or 236. A length that is neither is normalised to a blank
        /// party-size record rather than rejected -- the base class has no way to report a failed
        /// construction -- so the object is always safe to read; isStructurallyValid() reports it.
        explicit Pokemon4PT(std::span<const std::byte> raw)
        {
            // isSize() accepts only THIS group's two lengths, so there is nothing to
            // exclude. The shared module needed an exclusion because its size test also
            // accepted the other generation's party length.
            if (Encryption::isSize4PT(raw.size()))
            {
                dataSize = raw.size();
                buffer = Encryption::decryptArray4PT(raw);
            }
            else
            {
                dataSize = Encryption::SIZE_PARTY4_PT;
                buffer = new std::byte[dataSize];
                std::memset(buffer, 0, dataSize);
            }
            data = std::span<std::byte>(buffer, dataSize);
        }

        /// A blank party-size record, for the creator and the conversion path.
        Pokemon4PT()
        {
            dataSize = Encryption::SIZE_PARTY4_PT;
            buffer = new std::byte[dataSize];
            std::memset(buffer, 0, dataSize);
            data = std::span<std::byte>(buffer, dataSize);
        }

        ~Pokemon4PT() override = default;
        Pokemon4PT(const Pokemon4PT &) = delete;
        Pokemon4PT &operator=(const Pokemon4PT &) = delete;
        Pokemon4PT(Pokemon4PT &&) noexcept = default;
        Pokemon4PT &operator=(Pokemon4PT &&) noexcept = default;

        std::unique_ptr<Pokemon> clone() const override
        {
            std::byte *encryptedRecord = Encryption::encryptArray4PT(std::span<const std::byte>(data.data(), dataSize));
            auto copiedPokemon = std::make_unique<Pokemon4PT>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        /// The STORAGE FORMAT group, not the origin game. See the header note.
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::PT; }

        uint8_t rd8(size_t offset) const noexcept { return offset < dataSize ? static_cast<uint8_t>(data[offset]) : 0; }
        uint16_t rd16(size_t offset) const noexcept
        {
            return (offset + 1 < dataSize)
                       ? Utils::readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + offset))
                       : 0;
        }
        uint32_t rd32(size_t offset) const noexcept
        {
            return (offset + 3 < dataSize)
                       ? Utils::readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + offset))
                       : 0;
        }
        void wr8(size_t offset, uint8_t value) noexcept
        {
            if (offset < dataSize)
                data[offset] = static_cast<std::byte>(value);
        }
        void wr16(size_t offset, uint16_t value) noexcept
        {
            if (offset + 1 < dataSize)
                Utils::writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + offset), value);
        }
        void wr32(size_t offset, uint32_t value) noexcept
        {
            if (offset + 3 < dataSize)
                Utils::writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + offset), value);
        }

        bool isPartySize() const noexcept { return dataSize >= Encryption::SIZE_PARTY4_PT; }

        uint32_t pid() const noexcept override { return rd32(0x00); }
        void setPID(uint32_t value) noexcept override { wr32(0x00, value); }
        /// Gen 4 has no separate EncryptionConstant; PKHeX aliases it to the PID.
        uint32_t encryptionConstant() const noexcept override { return pid(); }
        void setEncryptionConstant(uint32_t) noexcept override {}
        uint16_t sanity() const noexcept { return rd16(0x04); }
        uint16_t checksum() const noexcept override { return rd16(0x06); }

        uint16_t speciesID() const noexcept override { return rd16(0x08); }
        void setSpecies(uint16_t value) noexcept override { wr16(0x08, value); }
        const char *species() const noexcept override;
        uint16_t heldItem() const noexcept override { return rd16(0x0A); }
        void setHeldItem(uint16_t value) noexcept override { wr16(0x0A, value); }
        uint32_t id32() const noexcept override { return rd32(0x0C); }
        void setId32(uint32_t value) noexcept override { wr32(0x0C, value); }
        uint16_t tid16() const noexcept override { return rd16(0x0C); }
        void setTID16(uint16_t value) noexcept override { wr16(0x0C, value); }
        uint16_t sid16() const noexcept override { return rd16(0x0E); }
        void setSID16(uint16_t value) noexcept override { wr16(0x0E, value); }
        uint32_t exp() const noexcept override { return rd32(0x10); }
        void setExp(uint32_t value) noexcept override;
        uint8_t friendship() const noexcept override { return rd8(0x14); }
        void setFriendship(uint8_t value) noexcept override { wr8(0x14, value); }
        uint8_t otFriendship() const noexcept override { return rd8(0x14); }
        void setOTFriendship(uint8_t value) noexcept override { wr8(0x14, value); }
        uint16_t ability() const noexcept override { return rd8(0x15); }
        void setAbility(uint16_t value) noexcept override { wr8(0x15, static_cast<uint8_t>(value)); }
        uint8_t language() const noexcept override { return rd8(0x17); }
        void setLanguage(uint8_t value) noexcept override { wr8(0x17, value); }

        uint8_t evHP() const noexcept override { return rd8(0x18); }
        uint8_t evATK() const noexcept override { return rd8(0x19); }
        uint8_t evDEF() const noexcept override { return rd8(0x1A); }
        uint8_t evSPE() const noexcept override { return rd8(0x1B); }
        uint8_t evSPA() const noexcept override { return rd8(0x1C); }
        uint8_t evSPD() const noexcept override { return rd8(0x1D); }
        void setEV(int statIndex, uint8_t value) noexcept override
        {
            // Accessor order: HP ATK DEF SPE SPA SPD -- the same order the bytes sit in.
            if (statIndex >= 0 && statIndex < 6)
                wr8(0x18 + statIndex, value);
        }

        uint16_t move(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd16(0x28 + slot * 2) : 0;
        }
        void setMove(int slot, uint16_t moveId) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr16(0x28 + slot * 2, moveId);
        }
        uint8_t movePP(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x30 + slot) : 0;
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x30 + slot, powerPoints);
        }
        uint8_t movePPUps(int slot) const noexcept override
        {
            return (slot >= 0 && slot < 4) ? rd8(0x34 + slot) : 0;
        }
        void setMovePPUps(int slot, uint8_t value) noexcept override
        {
            if (slot >= 0 && slot < 4)
                wr8(0x34 + slot, value);
        }

        uint32_t iv32() const noexcept { return rd32(0x38); }
        /// Read-modify-write ONLY. Bits 30 and 31 are the egg and nicknamed flags -- rebuilding
        /// this word from six IV values silently hatches every egg in the box.
        void setIV32(uint32_t value) noexcept { wr32(0x38, value); }
        uint8_t ivHP() const noexcept override { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }
        void setIV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex < 0 || statIndex > 5)
                return;
            static constexpr int IV_BIT_SHIFTS[6] = {0, 5, 10, 15, 20, 25}; // HP ATK DEF SPE SPA SPD
            const uint32_t newValue = value > 31 ? 31u : value;
            const int bitShift = IV_BIT_SHIFTS[statIndex];
            setIV32((iv32() & ~(0x1Fu << bitShift)) | (newValue << bitShift));
        }

        bool isEgg() const noexcept override { return ((iv32() >> 30) & 1) != 0; }
        void setEgg(bool isEgg) noexcept override
        {
            setIV32((iv32() & ~0x40000000u) | (isEgg ? 0x40000000u : 0u));
        }
        bool isNicknamed() const noexcept override { return ((iv32() >> 31) & 1) != 0; }
        void setIsNicknamed(bool value) noexcept override
        {
            setIV32((iv32() & 0x7FFFFFFFu) | (value ? 0x80000000u : 0u));
        }

        bool isFatefulEncounter() const noexcept override { return (rd8(0x40) & 1) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & ~0x01) | (value ? 1 : 0)));
        }
        uint8_t gender() const noexcept override { return (rd8(0x40) >> 1) & 0x03; }
        void setGender(uint8_t genderValue) noexcept override; // holds the PID to the new gender
        uint8_t formID() const noexcept override { return rd8(0x40) >> 3; }
        uint8_t form() const noexcept override { return formID(); }
        void setForm(uint8_t form) noexcept override
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & 0x07) | (form << 3)));
        }

        std::u16string nickname() const override { return readString(0x48, 11); }
        void setNickname(const std::u16string &value) noexcept override { writeString(0x48, 11, value); }
        int getMaxNicknameLength() const noexcept override { return 10; }
        bool canStoreNickname(const std::u16string &value) const noexcept override;
        uint8_t originGame() const noexcept override { return rd8(0x5F); }
        void setOriginGame(uint8_t value) noexcept override { wr8(0x5F, value); }

        std::u16string otName() const override { return readString(0x68, 8); }
        void setOTName(const std::u16string &value) noexcept override { writeString(0x68, 8, value); }
        uint8_t eggYear() const noexcept override { return rd8(0x78); }
        uint8_t eggMonth() const noexcept override { return rd8(0x79); }
        uint8_t eggDay() const noexcept override { return rd8(0x7A); }
        void setEggYear(uint8_t value) noexcept override { wr8(0x78, value); }
        void setEggMonth(uint8_t value) noexcept override { wr8(0x79, value); }
        void setEggDay(uint8_t value) noexcept override { wr8(0x7A, value); }
        uint8_t metYear() const noexcept override { return rd8(0x7B); }
        uint8_t metMonth() const noexcept override { return rd8(0x7C); }
        uint8_t metDay() const noexcept override { return rd8(0x7D); }
        void setMetYear(uint8_t value) noexcept override { wr8(0x7B, value); }
        void setMetMonth(uint8_t value) noexcept override { wr8(0x7C, value); }
        void setMetDay(uint8_t value) noexcept override { wr8(0x7D, value); }
        /// Whether this game WRITES the extended location fields at all: Platinum and
        /// HeartGold/SoulSilver do, Diamond and Pearl do not.
        static constexpr bool PT_HGSS_LOCATIONS = true;

        /**
         * Write a location into BOTH Gen 4 fields, the way the games do.
         *
         * A place Diamond and Pearl have no id for goes in the extended field with "Faraway place"
         * left in the DP one; anything they do have goes in the DP field, and into the extended
         * field as well only when this game is one that writes it. PKHeX G4PKM.MetLocation's
         * setter, whose `PtHGSS` flag is this class's PT_HGSS_LOCATIONS.
         */
        void writeLocationPair(size_t extendedOffset, size_t dpOffset, uint16_t value) noexcept
        {
            constexpr uint16_t FARAWAY_PLACE = 3002; // PKHeX Locations.Faraway4
            if (value == 0)
            {
                wr16(dpOffset, 0);
                wr16(extendedOffset, 0);
            }
            else if ((value > 111 && value < 2000) || (value > 2010 && value < 3000))
            {
                wr16(dpOffset, FARAWAY_PLACE);
                wr16(extendedOffset, value);
            }
            else
            {
                wr16(dpOffset, value);
                wr16(extendedOffset, PT_HGSS_LOCATIONS ? value : 0);
            }
        }

        /**
         * A GEN 4 RECORD HAS TWO LOCATION FIELDS AND THE NEWER ONE WINS.
         *
         * Diamond and Pearl store the met and egg locations at 0x80 and 0x7E. Platinum added
         * places those u16s could not name, so it introduced a second pair at 0x46 and 0x44 and
         * left the originals holding "Faraway place" (3002) for a DP game to show. Reading only
         * the DP pair therefore reports 3002 for every Pokemon caught in Platinum, HeartGold or
         * SoulSilver -- five ordinary Johto route catches in a real SoulSilver save came back as
         * "Faraway place" and were flagged as impossible, which is exactly what it looks like.
         *
         * PKHeX resolves it the same way (G4PKM.MetLocation: extended first, DP as the fallback),
         * and the fallback is what keeps Diamond and Pearl working, since they leave 0x46 zero.
         */
        uint16_t eggLocation() const noexcept override
        {
            const uint16_t extended = rd16(0x44);
            return extended != 0 ? extended : rd16(0x7E);
        }
        void setEggLocation(uint16_t value) noexcept override { writeLocationPair(0x44, 0x7E, value); }
        uint16_t metLocation() const noexcept override
        {
            const uint16_t extended = rd16(0x46);
            return extended != 0 ? extended : rd16(0x80);
        }
        void setMetLocation(uint16_t value) noexcept override { writeLocationPair(0x46, 0x80, value); }

        bool isPokerusInfected() const noexcept override { return (rd8(0x82) & 0xF) != 0; }
        bool isPokerusCured() const noexcept override { return (rd8(0x82) & 0xF) == 0 && (rd8(0x82) >> 4) != 0; }
        bool hasPokerus() const noexcept override { return true; }
        void setPokerus(uint8_t value) noexcept override { wr8(0x82, value); }

        /// HGSS added an Apricorn-ball field at 0x86; DP/Pt leave it zero and use 0x83. Prefer the
        /// newer one when set, so a Heavy Ball read out of a HeartGold save is not reported as a
        /// Poke Ball.
        uint8_t ball() const noexcept override
        {
            const uint8_t hgss = rd8(0x86);
            return hgss != 0 ? hgss : rd8(0x83);
        }
        void setBall(uint8_t value) noexcept override
        {
            wr8(0x83, value);
            // Write both, as PKHeX's HGSS path does: a record that keeps a stale 0x86 would read
            // back as the old ball through the preference above.
            wr8(0x86, value);
        }

        uint8_t metLevel() const noexcept override { return rd8(0x84) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            wr8(0x84, static_cast<uint8_t>((rd8(0x84) & 0x80) | (value & 0x7F)));
        }
        uint8_t otGender() const noexcept override { return rd8(0x84) >> 7; }
        void setOTGender(uint8_t value) noexcept override
        {
            wr8(0x84, static_cast<uint8_t>((rd8(0x84) & 0x7F) | ((value & 1) << 7)));
        }

        uint8_t level() const noexcept override { return isPartySize() ? rd8(0x8C) : 0; }
        void setLevel(uint8_t levelValue) noexcept override;
        uint16_t statHPCurrent() const noexcept override { return isPartySize() ? rd16(0x8E) : 0; }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            if (isPartySize())
                wr16(0x8E, value);
        }
        uint16_t statHPMax() const noexcept override { return isPartySize() ? rd16(0x90) : 0; }
        uint16_t statATK() const noexcept override { return isPartySize() ? rd16(0x92) : 0; }
        uint16_t statDEF() const noexcept override { return isPartySize() ? rd16(0x94) : 0; }
        uint16_t statSPE() const noexcept override { return isPartySize() ? rd16(0x96) : 0; }
        uint16_t statSPA() const noexcept override { return isPartySize() ? rd16(0x98) : 0; }
        uint16_t statSPD() const noexcept override { return isPartySize() ? rd16(0x9A) : 0; }

        uint8_t nature() const noexcept override { return static_cast<uint8_t>(pid() % 25); }
        uint8_t statNature() const noexcept override { return nature(); }
        void setNature(uint8_t nature) noexcept override;
        void setStatNature(uint8_t) noexcept override {} // no mints before Gen 8
        uint8_t abilityNumber() const noexcept override;
        const char *genderSymbol() const noexcept override;

        bool hasNature() const noexcept override { return true; }
        bool hasAbility() const noexcept override { return true; }
        bool hasHeldItem() const noexcept override { return true; }
        bool hasFriendship() const noexcept override { return true; }
        bool hasBall() const noexcept override { return true; }
        bool hasMetData() const noexcept override { return true; }
        bool hasOriginGame() const noexcept override { return true; }
        bool hasPID() const noexcept override { return true; }
        bool hasStoredGender() const noexcept override { return true; }
        bool hasHyperTraining() const noexcept override { return false; } // Gen 7 onward only

        uint8_t baseHP() const noexcept override { return personal().hp; }
        uint8_t baseATK() const noexcept override { return personal().atk; }
        uint8_t baseDEF() const noexcept override { return personal().def; }
        uint8_t baseSPE() const noexcept override { return personal().spe; }
        uint8_t baseSPA() const noexcept override { return personal().spa; }
        uint8_t baseSPD() const noexcept override { return personal().spd; }

        uint16_t calculateChecksum() const noexcept override;
        void refreshChecksum() noexcept override { wr16(0x06, calculateChecksum()); }
        bool checksumValid() const noexcept override { return checksum() == calculateChecksum(); }
        bool isStructurallyValid() const noexcept override
        {
            return sanity() == 0 && checksumValid() && speciesID() != 0;
        }

        void recalculateStats() noexcept override;
        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override;
        void regeneratePID(uint32_t trainerID32) noexcept override;
        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;

    private:
        // A new PID holding what the caller does not change (-1 = either); false when none can.
        bool rerollPID(int wantShiny, int wantGender, int wantNature, uint32_t trainerID32) noexcept;
        void writeStoredGender(uint8_t genderValue) noexcept
        {
            wr8(0x40, static_cast<uint8_t>((rd8(0x40) & ~0x06) | ((genderValue & 3) << 1)));
        }
        const PersonalRecord &personal() const noexcept
        {
            return getPersonalInfo4PT(speciesID(), form());
        }
        int natureModifier(int statIndex) const noexcept;
        std::u16string readString(size_t offset, size_t units) const;
        void writeString(size_t offset, size_t units, const std::u16string &value) noexcept;
    };
}

#endif
