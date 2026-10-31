#ifndef POKEMON_POKEMON7_LGPE_H
#define POKEMON_POKEMON7_LGPE_H

#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Encryption/Encryption7LGPE.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"

using namespace Encryption;
using namespace Pokemon;
using namespace Utils;

namespace Pokemon
{
    class Pokemon7LGPE final : public Pokemon
    {
    public:
        explicit Pokemon7LGPE(std::span<const std::byte> raw)
        {
            buffer = decryptArray7LGPE(raw);
            dataSize = raw.size();
            data = std::span<std::byte>(buffer, dataSize);
        }

        /**
         * Destructor - cleans up decrypted data buffer.
         * The base class Pokemon destructor handles buffer cleanup.
         */
        ~Pokemon7LGPE() override = default;

        Pokemon7LGPE(const Pokemon7LGPE &) = delete;
        Pokemon7LGPE &operator=(const Pokemon7LGPE &) = delete;

        Pokemon7LGPE(Pokemon7LGPE &&) noexcept = default;
        Pokemon7LGPE &operator=(Pokemon7LGPE &&) noexcept = default;

        /** Deep-copy: re-encrypt the decrypted buffer and rebuild via the encrypted-span ctor.
         *  Offset 0x00 is the EC/crypto seed (the PID lives separately at 0x18 in LGPE). */
        std::unique_ptr<Pokemon> clone() const override
        {
            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data()));
            std::byte *encryptedRecord = encryptArray7LGPE(std::span<const std::byte>(data.data(), dataSize), encryptionConstant);
            auto copiedPokemon = std::make_unique<Pokemon7LGPE>(std::span<const std::byte>(encryptedRecord, dataSize));
            delete[] encryptedRecord;
            return copiedPokemon;
        }

        /** Storage-format game group (this subclass), NOT the origin Version byte. */
        Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::GG; }

        uint16_t speciesID() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x08));
        }

        /** Sets species (0x08) and recalculates stats. Without this override the base no-op left a
         *  freshly-CREATED LGPE pokemon at species 0, so the details editor -- which bails on species 0 --
         *  never appeared (the create-Pokemon-shows-no-editor bug). */
        void setSpecies(uint16_t species) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x08), species);
            resetSizeAbsolutes();
            recalculateStats();
            refreshChecksum();
        }

        /** Core-identity setters, also unwired before (base no-ops), so a CREATED LGPE pokemon had
         *  EC / PID / TID / form all zero. EC 0 is the worst: the save's read path treats a zero-EC
         *  slot as EMPTY, so a created pokemon would vanish on reload. */
        void setEncryptionConstant(uint32_t encryptionConstant) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x00), encryptionConstant);
            refreshChecksum();
        }
        void setPID(uint32_t pidValue) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x18), pidValue);
            refreshChecksum();
        }
        void setId32(uint32_t value) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0C), value);
            refreshChecksum();
        }

        /** TID16 / SID16 are the two halves of the id32 above -- TID16 at 0x0C, SID16 at 0x0E,
         *  little-endian, so id32 == (sid16 << 16) | tid16.
         *
         *  These were the last of the core-identity accessors still falling through to the base
         *  class's return-0 / no-op defaults, which made setTID16() and setSID16() silently do
         *  NOTHING on a Let's Go entity: id32() kept reading 0 however the trainer id was set. Every
         *  other format overrides them, so the gap was invisible until the setters suite wrote both
         *  halves and read the whole back (report 2026-08-28: "id32 is not sid16<<16 | tid16",
         *  id32=0x00000000 tid=0x1234 sid=0x5678). Ownership checks compare trainer ids, so a
         *  created or converted pokemon reading id 0 is a wrong OWN/TRADED verdict, not just a blank. */
        uint16_t tid16() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0C));
        }
        void setTID16(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0C), value);
            refreshChecksum();
        }

        uint16_t sid16() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0E));
        }
        void setSID16(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0E), value);
            refreshChecksum();
        }
        /** Form lives in the high 5 bits of 0x1D (bit0 = Fateful, bits1-2 = Gender). */
        void setForm(uint8_t formValue) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x1D]) & 0x07) | ((formValue & 0x1F) << 3);
            data[0x1D] = static_cast<std::byte>(packedByte);
            resetSizeAbsolutes();
            recalculateStats();
            refreshChecksum();
        }

        /** Fateful-encounter flag -- bit 0 of 0x1D (the byte also holds gender bits 1-2 / form bits 3-7,
         *  which setGender/setForm leave untouched). */
        bool isFatefulEncounter() const noexcept override { return (static_cast<uint8_t>(data[0x1D]) & 0x01) != 0; }
        void setFatefulEncounter(bool value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x1D]) & 0xFE) | (value ? 0x01 : 0x00);
            data[0x1D] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        /** Level is EXP-derived; setLevel writes the level's minimum EXP (0x10) and recalcs. Defined in
         *  the .cpp (needs the growth-rate / EXP tables). Without it the level picker was a no-op here. */
        void setLevel(uint8_t level) noexcept override;

        const char *species() const noexcept override;

        uint8_t formID() const noexcept override
        {
            // Byte 0x1D packs Fateful(bit0) + Gender(bits1-2) + Form(bits3-7).
            return static_cast<uint8_t>(data[0x1D]) >> 3;
        }

        uint16_t heldItem() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0A));
        }

        /** Sets the held item id (0x0A). The field exists in PB7; Let's Go doesn't use held items
         *  in-game, so a non-zero value here is cosmetic/illegal but harmless to the save. */
        void setHeldItem(uint16_t item) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x0A), item);
            refreshChecksum();
        }

        uint8_t form() const noexcept override
        {
            // Byte 0x1D packs Fateful(bit0) + Gender(bits1-2) + Form(bits3-7).
            return static_cast<uint8_t>(data[0x1D]) >> 3;
        }

        uint32_t id32() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x0C));
        }

        uint32_t exp() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x10));
        }

        /** Sets total EXP (0x10) directly; level() derives from it, so this also drives the level. */
        void setExp(uint32_t value) noexcept override
        {
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x10), value);
            recalculateStats();
            refreshChecksum();
        }

        /// PB7 keeps the ability as a u8 at 0x14; 0x15 holds AbilityNumber plus its flags.
        uint16_t ability() const noexcept override
        {
            return static_cast<uint8_t>(data[0x14]);
        }
        void setAbility(uint16_t value) noexcept override
        {
            data[0x14] = static_cast<std::byte>(value & 0xFF);
            refreshChecksum();
        }

        /** Ability slot (1 / 2 / H). Location: 0x15 bits 0-2. */
        uint8_t abilityNumber() const noexcept override { return static_cast<uint8_t>(data[0x15]) & 0x07; }
        void setAbilityNumber(uint8_t number) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x15]) & ~0x07) | (number & 0x07);
            data[0x15] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        uint8_t nature() const noexcept override
        {
            return static_cast<uint8_t>(data[0x1C]);
        }

        /** Sets the nature (0x1C). In Let's Go this is also the stat nature, so recalc stats. */
        void setNature(uint8_t value) noexcept override
        {
            data[0x1C] = static_cast<std::byte>(value);
            recalculateStats();
            refreshChecksum();
        }

        /** The packed byte also carries the fateful flag at bit 0 and the form at bits 3-7. */
        void setGender(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0x1D]) & ~0x06) | ((value & 0x03) << 1);
            data[0x1D] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        uint8_t statNature() const noexcept override
        {
            return nature();
        }

        uint32_t encryptionConstant() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x00));
        }

        /// Separate from the encryption constant at 0x00, which is only the crypt seed. Shininess is
        /// derived from this.
        uint32_t pid() const noexcept override
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x18));
        }

        std::u16string nickname() const override
        {
            const uint8_t *nicknameStart = reinterpret_cast<const uint8_t *>(data.data() + 0x40);
            return getString(nicknameStart, 26);
        }

        /** Sets the nickname (0x40, 26 bytes / 12 chars). Does not change the isNicknamed flag. Was
         *  unwired (base no-op), so a created LGPE pokemon showed a BLANK name in-game. */
        void setNickname(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0x40), 26, value, 12);
            refreshChecksum();
        }
        /** "Has a custom nickname" flag -- bit 31 of the packed IV32 (0x74). */
        bool isNicknamed() const noexcept override { return (iv32() & 0x80000000u) != 0; }
        void setIsNicknamed(bool nicknamed) noexcept override
        {
            uint32_t ivValue = iv32();
            if (nicknamed)
                ivValue |= 0x80000000u;
            else
                ivValue &= ~0x80000000u;
            writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x74), ivValue);
            refreshChecksum();
        }

        /** Move ID in a slot. Location: 0x5A + slot*2. */
        uint16_t move(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x5A + slot * 2));
        }
        void setMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x5A + slot * 2), moveID);
            refreshChecksum();
        }

        /** Current PP of a move slot. Location: 0x62 + slot. */
        uint8_t movePP(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x62 + slot]);
        }
        void setMovePP(int slot, uint8_t powerPoints) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x62 + slot] = static_cast<std::byte>(powerPoints);
            refreshChecksum();
        }

        /** PP Ups applied to a move slot. Location: 0x66 + slot. */
        uint8_t movePPUps(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return static_cast<uint8_t>(data[0x66 + slot]);
        }
        void setMovePPUps(int slot, uint8_t ppUps) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            data[0x66 + slot] = static_cast<std::byte>(ppUps);
            refreshChecksum();
        }

        /** Relearn move ID. Location: 0x6A + slot*2. */
        uint16_t relearnMove(int slot) const noexcept override
        {
            if (slot < 0 || slot > 3)
                return 0;
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x6A + slot * 2));
        }
        void setRelearnMove(int slot, uint16_t moveID) noexcept override
        {
            if (slot < 0 || slot > 3)
                return;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x6A + slot * 2), moveID);
            refreshChecksum();
        }

        /**
         * The friendship the game is CURRENTLY using: the OT's while the OT is in charge, the
         * handler's once the pokemon has been traded. Same shape as every other format that has a
         * handler, and the same reason -- the flag at 0x93 is what says whose number counts.
         *
         * This matters more here than anywhere else. Let's Go folds friendship into the stat
         * formula (a 10% bonus at max), so reading the OT's value on a traded Pokemon does not
         * merely mislabel a row -- it recomputes the pokemon's stats and its CP from the wrong number.
         * PKHeX's PB7 derives from G6PKM and answers exactly this way.
         */
        uint8_t friendship() const noexcept override
        {
            return currentHandler() == 0 ? otFriendship() : htFriendship();
        }
        void setFriendship(uint8_t value) noexcept override
        {
            if (currentHandler() == 0)
                setOTFriendship(value);
            else
                setHTFriendship(value);
            refreshChecksum();
        }
        /** Original Trainer friendship (0-255). Location: 0xCA -- PB7's OriginalTrainerFriendship.
         *  Wired alongside htFriendship: the pair is what friendship() chooses between, and leaving
         *  this one on the base no-op meant it read 0 and could not be written at all. */
        uint8_t otFriendship() const noexcept override { return static_cast<uint8_t>(data[0xCA]); }
        void setOTFriendship(uint8_t value) noexcept override
        {
            data[0xCA] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        /** Original Trainer name (0xB0, 26 bytes UTF-16). */
        std::u16string otName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0xB0), 26);
        }
        void setOTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0xB0), 26, value, 12);
            refreshChecksum();
        }
        /// Gen 6 introduced the handler; this format carries one.
        bool hasHandler() const noexcept override { return true; }

        // ...and nothing else the handler block usually carries. LET'S GO DROPPED MEMORIES
        // ENTIRELY, has no handler language byte (Gen 8 introduced it) and no geolocation history,
        // and while it allocates a byte where Gen 6/7 keep HT affection, no game ever writes it
        // (PKHeX marks it Unused). So hasHandlerMemories(), hasHandlerLanguage(),
        // hasHandlerAffection() and hasGeolocation() are all left at their false defaults -- the
        // absence is the fact, not an omission. A trade into Let's Go writes the HT name, gender
        // and friendship and that is all.

        /** Handling (current) Trainer name (0x78, 26 bytes). */
        std::u16string htName() const override
        {
            return getString(reinterpret_cast<const uint8_t *>(data.data() + 0x78), 26);
        }
        void setHTName(const std::u16string &value) noexcept override
        {
            setString(reinterpret_cast<uint8_t *>(data.data() + 0x78), 26, value, 12);
            refreshChecksum();
        }
        /** Handling Trainer gender (0 = Male, 1 = Female). Location: 0x92 (PKHeX PB7.HandlingTrainerGender),
         *  directly before currentHandler below. This was the one HT field left on the base stub, and both
         *  halves of that stub were wrong: the getter returned a hard 0, so every
         *  handler displayed as Male whatever the save said, and the setter was a no-op, so a trainer gender
         *  change could never re-stamp it. The re-stamp still counted those mons as updated (the getter's 0
         *  never matched a Female trainer). */
        uint8_t htGender() const noexcept override { return static_cast<uint8_t>(data[0x92]); }
        void setHTGender(uint8_t value) noexcept override
        {
            data[0x92] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        /** Handling Trainer friendship (0-255). Location: 0xA2. Wired alongside htGender -- same omission,
         *  and an unwired getter that quietly returns 0 is what made the gender bug invisible. */
        uint8_t htFriendship() const noexcept override { return static_cast<uint8_t>(data[0xA2]); }
        void setHTFriendship(uint8_t value) noexcept override
        {
            data[0xA2] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        /** Current handler flag (0 = OT active, 1 = HT active). Location: 0x93. */
        uint8_t currentHandler() const noexcept override { return static_cast<uint8_t>(data[0x93]); }
        void setCurrentHandler(uint8_t value) noexcept override
        {
            data[0x93] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t originGame() const noexcept override { return static_cast<uint8_t>(data[0xDF]); }
        void setOriginGame(uint8_t value) noexcept override
        {
            data[0xDF] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t language() const noexcept override { return static_cast<uint8_t>(data[0xE3]); }
        void setLanguage(uint8_t value) noexcept override
        {
            data[0xE3] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        /** Poke Ball id. Location: 0xDC. */
        uint8_t ball() const noexcept override { return static_cast<uint8_t>(data[0xDC]); }
        void setBall(uint8_t value) noexcept override
        {
            data[0xDC] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        /** Met location. Location: 0xDA (2 bytes). */
        uint16_t metLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xDA));
        }
        void setMetLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xDA), value);
            refreshChecksum();
        }
        /** Egg location. Location: 0xD8 (2 bytes). */
        uint16_t eggLocation() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xD8));
        }
        /// The field exists (a PB7 is a Gen 7 record) and Let's Go has no day care to
        /// fill it, so the row would read "(none)" for every Pokemon these games can hold.
        bool hasEggData() const noexcept override { return false; }
        void setEggLocation(uint16_t value) noexcept override
        {
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xD8), value);
            refreshChecksum();
        }

        /** Met date (0xD4-0xD6) and received-Egg date (0xD1-0xD3); the year byte is years-since-2000.
         *  These were unwired (base no-ops), so a created LGPE pokemon's met date stayed 0/0/2000. */
        uint8_t metYear() const noexcept override { return static_cast<uint8_t>(data[0xD4]); }
        void setMetYear(uint8_t value) noexcept override
        {
            data[0xD4] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metMonth() const noexcept override { return static_cast<uint8_t>(data[0xD5]); }
        void setMetMonth(uint8_t value) noexcept override
        {
            data[0xD5] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t metDay() const noexcept override { return static_cast<uint8_t>(data[0xD6]); }
        void setMetDay(uint8_t value) noexcept override
        {
            data[0xD6] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggYear() const noexcept override { return static_cast<uint8_t>(data[0xD1]); }
        void setEggYear(uint8_t value) noexcept override
        {
            data[0xD1] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggMonth() const noexcept override { return static_cast<uint8_t>(data[0xD2]); }
        void setEggMonth(uint8_t value) noexcept override
        {
            data[0xD2] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        uint8_t eggDay() const noexcept override { return static_cast<uint8_t>(data[0xD3]); }
        void setEggDay(uint8_t value) noexcept override
        {
            data[0xD3] = static_cast<std::byte>(value);
            refreshChecksum();
        }
        /** Met level (bits 0-6). Location: 0xDD. */
        uint8_t metLevel() const noexcept override { return static_cast<uint8_t>(data[0xDD]) & 0x7F; }
        void setMetLevel(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0xDD]) & 0x80) | (value & 0x7F);
            data[0xDD] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }
        /** OT gender (bit 7 of 0xDD). */
        uint8_t otGender() const noexcept override { return (static_cast<uint8_t>(data[0xDD]) >> 7) & 0x01; }
        void setOTGender(uint8_t value) noexcept override
        {
            uint8_t packedByte = (static_cast<uint8_t>(data[0xDD]) & 0x7F) | ((value & 0x01) << 7);
            data[0xDD] = static_cast<std::byte>(packedByte);
            refreshChecksum();
        }

        bool isEgg() const noexcept override
        {
            return false; // Let's Go doesn't have eggs
        }

        bool isPokerusInfected() const noexcept override
        {
            return false; // Let's Go doesn't have Pokerus
        }

        bool isPokerusCured() const noexcept override
        {
            return false; // Let's Go doesn't have Pokerus
        }

        bool isShiny(uint32_t trainerID32, std::string species) const noexcept override
        {
            if (trainerID32 == 0)
            {
                return false;
            }
            // Shininess is PID-based (Gen 6+): shiny iff (TID^SID^PIDhi^PIDlo) < 16.
            uint32_t pidValue = pid();
            uint32_t xorComponent = (pidValue ^ trainerID32);
            uint32_t xorResult = (xorComponent ^ (xorComponent >> 16)) & 0xFFFF;
            return xorResult < 16;
        }

        uint8_t gender() const noexcept override;

        const char *genderSymbol() const noexcept override
        {
            uint8_t genderValue = gender();
            if (genderValue == 0) return "♂";
            if (genderValue == 1) return "♀";
            return "";
        }

        /**
         * Let's Go has NO EV mechanic. It replaced EV training with Awakening Values (below), and
         * nothing in the game reads these bytes: PB7's stat formula is AV + IV + base + level, with no
         * EV term at all (PKHeX `PB7.LoadStats`, mirrored by computeStat()).
         *
         * The bytes are nonetheless real. PB7 inherits the Gen 7 layout, so 0x1E-0x23 exist and PKHeX
         * maps `EV_HP`..`EV_SPD` onto exactly these offsets. On a legitimate Let's Go Pokemon they are
         * always 0 -- the game never writes them, and the bank's converter deliberately leaves them 0
         * when a Pokemon enters LGPE. A non-zero value here therefore means the Pokemon was edited by
         * something else, which is worth being able to SEE. That is the only reason these getters
         * exist: the legality checker reads EVs for every format, and reporting the real bytes beats
         * reporting a hardcoded 0 that would hide the anomaly.
         *
         * Overriding is not optional either way -- the base declares them pure virtual.
         */
        uint8_t evHP() const noexcept override { return static_cast<uint8_t>(data[0x1E]); }
        uint8_t evATK() const noexcept override { return static_cast<uint8_t>(data[0x1F]); }
        uint8_t evDEF() const noexcept override { return static_cast<uint8_t>(data[0x20]); }
        uint8_t evSPE() const noexcept override { return static_cast<uint8_t>(data[0x21]); }
        uint8_t evSPA() const noexcept override { return static_cast<uint8_t>(data[0x22]); }
        uint8_t evSPD() const noexcept override { return static_cast<uint8_t>(data[0x23]); }

        /**
         * Deliberately does nothing. Writing an EV here would change no stat the game computes, while
         * making the Pokemon read as edited to anything that checks -- all cost, no effect. Use setAV().
         *
         * Nothing calls this for Let's Go today: every editor path branches on hasAwakeningValues() and
         * routes to setAV(). This is the backstop for the one that eventually forgets.
         */
        void setEV(int, uint8_t) noexcept override {}

        /**
         * Awakening Values (AVs) - Unique to Pokemon Let's Go!
         * Location: 0x24-0x29 (1 byte each)
         * These provide stat bonuses similar to EVs but earned differently.
         * Max 200 per stat, earned by catching Pokemon of the same species.
         * Each AV point directly adds to the stat (different from EV formula).
         */
        uint8_t avHP() const noexcept override { return static_cast<uint8_t>(data[0x24]); }
        uint8_t avATK() const noexcept override { return static_cast<uint8_t>(data[0x25]); }
        uint8_t avDEF() const noexcept override { return static_cast<uint8_t>(data[0x26]); }
        uint8_t avSPE() const noexcept override { return static_cast<uint8_t>(data[0x27]); }
        uint8_t avSPA() const noexcept override { return static_cast<uint8_t>(data[0x28]); }
        uint8_t avSPD() const noexcept override { return static_cast<uint8_t>(data[0x29]); }

        bool hasAwakeningValues() const noexcept override { return true; }

        /** Hyper Training: one bit per stat -- HP, ATK, DEF, SPA, SPD, SPE -- marking the stat as
         *  played at a maximal IV while the stored IV is left untouched. PB7 keeps it at 0xDE (PKHeX PB7.cs). Let's Go
         * DOES have Hyper Training, alongside its Awakening Values -- easy to assume otherwise, since AVs are the stat
         * mechanic it is known for.
         *
         *  recalculateStats() below reads effectiveIV() rather than ivXXX() because of this; see
         *  the base class for what ignoring it cost. */
        bool hasHyperTraining() const noexcept override { return true; }
        uint8_t hyperTrainFlags() const noexcept override { return static_cast<uint8_t>(data[0xDE]); }
        void setHyperTrainFlags(uint8_t value) noexcept override
        {
            data[0xDE] = static_cast<std::byte>(value);
            refreshChecksum();
        }

        void setAV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6 && value <= 200)
            {
                data[0x24 + statIndex] = static_cast<std::byte>(value);
                recalculateStats();
                refreshChecksum();
            }
        }

        /**
         * Gets the packed IV32 value.
         * Location: 0x74 (4 bytes) - Different location from PK8!
         * Contains all 6 IVs plus special flags.
         */
        uint32_t iv32() const noexcept
        {
            return readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x74));
        }

        /**
         * Individual Values (IVs) - inherent stat potential (0-31).
         * Same bit layout as PK8.
         */
        uint8_t ivHP() const noexcept override { return (iv32() >> 0) & 0x1F; }
        uint8_t ivATK() const noexcept override { return (iv32() >> 5) & 0x1F; }
        uint8_t ivDEF() const noexcept override { return (iv32() >> 10) & 0x1F; }
        uint8_t ivSPE() const noexcept override { return (iv32() >> 15) & 0x1F; }
        uint8_t ivSPA() const noexcept override { return (iv32() >> 20) & 0x1F; }
        uint8_t ivSPD() const noexcept override { return (iv32() >> 25) & 0x1F; }

        void setIV(int statIndex, uint8_t value) noexcept override
        {
            if (statIndex >= 0 && statIndex < 6 && value <= 31)
            {
                uint32_t ivValue = iv32();
                int shift = statIndex * 5;
                uint32_t mask = ~(0x1F << shift);
                ivValue = (ivValue & mask) | ((value & 0x1F) << shift);
                writeUInt32LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x74), ivValue);
                recalculateStats();
                refreshChecksum();
            }
        }

        uint16_t checksum() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x06));
        }

        /// Sums 16-bit words from 0x08 to the STORED size; the party tail is not covered.
        uint16_t calculateChecksum() const noexcept override
        {
            uint16_t checksum = 0;

            // Sum all 16-bit values from offset 0x08 to SIZE_6STORED
            const size_t checksumEnd = std::min(dataSize, SIZE_STORED7_LGPE);
            for (size_t index = 0x08; index < checksumEnd; index += 2)
            {
                checksum += readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + index));
            }

            return checksum;
        }

        void refreshChecksum() noexcept override
        {
            uint16_t newChecksum = calculateChecksum();
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0x06), newChecksum);
        }

        bool checksumValid() const noexcept override
        {
            return checksum() == calculateChecksum();
        }

        uint8_t baseHP() const noexcept override;
        uint8_t baseATK() const noexcept override;
        uint8_t baseDEF() const noexcept override;
        uint8_t baseSPE() const noexcept override;
        uint8_t baseSPA() const noexcept override;
        uint8_t baseSPD() const noexcept override;

        uint8_t level() const noexcept override;

        /**
         * Battle stats in Let's Go = friendship% * ( nature% * ((2*Base + IV) * Level/100 + 5) ) + AV,
         * with HP the usual (no nature / friendship). computeStat() is the ONE implementation; the six
         * getters and recalculateStats() (which writes the stored tail the game reads) both use it so the
         * display can't drift from the game. index 0..5 = HP, Atk, Def, Spe, SpA, SpD.
         */
        uint16_t computeStat(int index) const noexcept;
        uint16_t statHPMax() const noexcept override;
        uint16_t statATK() const noexcept override;
        uint16_t statDEF() const noexcept override;
        uint16_t statSPE() const noexcept override;
        uint16_t statSPA() const noexcept override;
        uint16_t statSPD() const noexcept override;
        uint16_t statHPCurrent() const noexcept override
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xF0));
        }
        void setStatHPCurrent(uint16_t value) noexcept override
        {
            const uint16_t max = statHPMax();
            if (max != 0 && value > max)
                value = max;
            writeUInt16LittleEndian(reinterpret_cast<uint8_t *>(data.data() + 0xF0), value);
        }

        /**
         * Recalculates all battle stats including AVs and friendship bonuses.
         * Let's Go formula:
         * Stat = (((2 * Base + IV + EV/4) * Level / 100) + 5) * Nature * Friendship + AV
         *
         * Where Friendship is a multiplier: (friendship/255 / 10 + 1) ≈ 1.0 to 1.1
         */
        void recalculateStats() noexcept override;

        /// Let's Go uses the encryption constant as the PID, so this rerolls the EC.
        void regeneratePID(uint32_t trainerID32) noexcept override;

        void setShiny(bool makeShiny, uint32_t trainerID32) noexcept override;

        uint16_t cp() const noexcept
        {
            return readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xFE));
        }

        uint8_t heightScalar() const noexcept
        {
            return static_cast<uint8_t>(data[0x3A]);
        }

        uint8_t weightScalar() const noexcept
        {
            return static_cast<uint8_t>(data[0x3B]);
        }

        /// The spirit and mood a Pokemon arrives in the game with (PKHeX PB7.InitialSpiritMood).
        static constexpr uint8_t INITIAL_SPIRIT_MOOD = 100;
        uint8_t spirit() const noexcept { return static_cast<uint8_t>(data[0xAC]); }
        uint8_t mood() const noexcept { return static_cast<uint8_t>(data[0xAD]); }
        /// PKHeX PB7.ResetSpiritMood. The caller refreshes the checksum.
        void resetSpiritMood() noexcept
        {
            data[0xAC] = static_cast<std::byte>(INITIAL_SPIRIT_MOOD);
            data[0xAD] = static_cast<std::byte>(INITIAL_SPIRIT_MOOD);
        }
        /// When the Pokemon arrived in this save (PKHeX PB7.ReceivedDate/ReceivedTime, 0xCB-0xD0):
        /// years since 2000, then month, day, hour, minute, second. The caller refreshes the checksum.
        void setReceivedDateTime(uint8_t receivedYear, uint8_t receivedMonth, uint8_t receivedDay, uint8_t receivedHour, uint8_t receivedMinute, uint8_t receivedSecond) noexcept
        {
            data[0xCB] = static_cast<std::byte>(receivedYear);
            data[0xCC] = static_cast<std::byte>(receivedMonth);
            data[0xCD] = static_cast<std::byte>(receivedDay);
            data[0xCE] = static_cast<std::byte>(receivedHour);
            data[0xCF] = static_cast<std::byte>(receivedMinute);
            data[0xD0] = static_cast<std::byte>(receivedSecond);
        }
        uint8_t receivedYear() const noexcept { return static_cast<uint8_t>(data[0xCB]); }

        float heightAbsolute() const noexcept
        {
            uint32_t bits = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0x2C));
            float result;
            std::memcpy(&result, &bits, sizeof(float));
            return result;
        }

        float weightAbsolute() const noexcept
        {
            uint32_t bits = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(data.data() + 0xE4));
            float result;
            std::memcpy(&result, &bits, sizeof(float));
            return result;
        }

        /**
         * The absolute height and weight a PB7 stores (0x2C, 0xE4) for a species, form and pair of
         * size scalars, exactly as PKHeX PB7.GetHeightAbsolute / GetWeightAbsolute compute them --
         * which MiscVerifierPB7 compares bit for bit. Two things decide the last bit: the weight is
         * `heightRatio * (weightRatio * baseWeight)`, in that grouping, and no multiply may be fused
         * into the following add (aarch64 `fmadd` rounds once where the game rounds twice).
         * Every writer of those floats goes through these.
         */
        static float heightAbsoluteFor(uint16_t speciesId, uint8_t formId, uint8_t heightScalarValue) noexcept;
        static float weightAbsoluteFor(uint16_t speciesId, uint8_t formId, uint8_t heightScalarValue, uint8_t weightScalarValue) noexcept;
        /// Rewrites both stored absolutes from the current species, form and scalars.
        void resetSizeAbsolutes() noexcept;
    };
}

#endif
