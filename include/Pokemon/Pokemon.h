#ifndef POKEMON_POKEMON_H
#define POKEMON_POKEMON_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <string>
#include <memory>

#include "Enums/GameVersion.h"
#include "Pokemon/PokemonTypes.h"

namespace Pokemon
{
    class Pokemon
    {
    protected:
        /**
         * Internal buffer storing the Pokemon's decrypted data.
         * This buffer is managed by the derived class and should contain
         * all Pokemon information in its decrypted form for easy access.
         */
        std::byte *buffer = nullptr;

        /**
         * Span view of the decrypted Pokemon data.
         * Provides safe, bounds-checked access to the data buffer.
         */
        std::span<std::byte> data;

        /**
         * Actual size of the Pokemon data in bytes.
         * This varies by generation:
         * - Gen 8 (PK8): 344 bytes (party) or 328 bytes (stored)
         * - Gen 7 LGP/E (PK7): 260 bytes
         * - Gen 9 (PK9): varies by format
         */
        size_t dataSize;

    public:
        // Virtual destructor to ensure proper cleanup in derived classes
        virtual ~Pokemon()
        {
            if (buffer)
            {
                delete[] buffer;
                buffer = nullptr;
            }
        }

        Pokemon(const Pokemon &) = delete;
        Pokemon &operator=(const Pokemon &) = delete;

        // Allow move operations for efficient transfers
        Pokemon(Pokemon &&) noexcept = default;
        Pokemon &operator=(Pokemon &&) noexcept = default;

        virtual uint16_t speciesID() const noexcept = 0;

        virtual const char *species() const noexcept = 0;

        virtual std::u16string nickname() const = 0;

        virtual uint8_t formID() const noexcept = 0;

        virtual uint8_t form() const noexcept = 0;

        virtual uint16_t heldItem() const noexcept = 0;

        virtual uint32_t id32() const noexcept = 0;

        virtual uint32_t exp() const noexcept = 0;

        virtual uint16_t ability() const noexcept = 0;

        virtual uint8_t nature() const noexcept = 0;

        virtual uint8_t statNature() const noexcept = 0;

        virtual uint8_t level() const noexcept = 0;

        /// 0 male, 1 female, 2 genderless.
        virtual uint8_t gender() const noexcept = 0;

        virtual const char *genderSymbol() const noexcept = 0;

        virtual uint32_t pid() const noexcept = 0;

        virtual uint32_t encryptionConstant() const noexcept = 0;

        /**
         * Individual Values (IVs) are inherent stat values (0-31) that determine
         * a Pokemon's potential. Higher IVs result in higher final stats.
         */
        virtual uint8_t ivHP() const noexcept = 0;
        virtual uint8_t ivATK() const noexcept = 0;
        virtual uint8_t ivDEF() const noexcept = 0;
        virtual uint8_t ivSPE() const noexcept = 0;
        virtual uint8_t ivSPA() const noexcept = 0;
        virtual uint8_t ivSPD() const noexcept = 0;

        virtual void setIV(int statIndex, uint8_t value) noexcept = 0;

        /**
         * Effort Values (EVs) are earned through battling and training.
         * They provide additional stat points (max 252 per stat, 510 total).
         */
        virtual uint8_t evHP() const noexcept = 0;
        virtual uint8_t evATK() const noexcept = 0;
        virtual uint8_t evDEF() const noexcept = 0;
        virtual uint8_t evSPE() const noexcept = 0;
        virtual uint8_t evSPA() const noexcept = 0;
        virtual uint8_t evSPD() const noexcept = 0;

        virtual void setEV(int statIndex, uint8_t value) noexcept = 0;

        /**
         * Highest value an IV field can hold in this format.
         *
         * 31 everywhere from Gen 3 on, but Gen 1 and Gen 2 store 4-bit DVs and cap at 15.
         * An editor that assumes 31 will happily write a value the format cannot represent,
         * and a legality check that assumes 31 reads a perfect Gen 1 DV spread as mediocre.
         * Ask, don't assume.
         */
        virtual uint8_t maxIV() const noexcept { return 31; }

        /**
         * Highest value an EV field can hold in this format.
         *
         * 255 from Gen 3 on. Gen 1 and Gen 2 have no EVs at all -- they have Stat Experience,
         * a SIXTEEN-bit counter per stat that caps at 65535 and contributes via a square root.
         * It is a different quantity that happens to occupy the same interface slot.
         */
        virtual uint16_t maxEV() const noexcept { return 255; }

        /// The 8-bit evXXX() accessors cannot represent Gen 1/2 Stat Experience, and narrowing is not
        /// a rounding error -- 300 Stat Exp truncates to 44 and saturates to 255, and neither is the
        /// number. Anything that displays, edits or reasons about the value must use this.
        virtual uint16_t evWide(int statIndex) const noexcept
        {
            switch (statIndex)
            {
            case 0:
                return evHP();
            case 1:
                return evATK();
            case 2:
                return evDEF();
            case 3:
                return evSPE();
            case 4:
                return evSPA();
            case 5:
                return evSPD();
            default:
                return 0;
            }
        }

        /** Sets EV / Stat Experience at full stored width. Default clamps into the 8-bit setter. */
        virtual void setEVWide(int statIndex, uint16_t value) noexcept
        {
            setEV(statIndex, static_cast<uint8_t>(value > 255 ? 255 : value));
        }

        /**
         * Whether the format stores the property at all.
         *
         * These exist because "0" is a legitimate value for every one of these fields -- nature
         * 0 is Hardy, ability 0 is a real slot, ball 0 is a real id, friendship 0 is a real
         * number -- so a getter returning 0 cannot distinguish "none" from "absent". Gen 1 has
         * none of them, and a details view that prints "Nature: Hardy" for a Red/Blue Pokemon is
         * inventing data. Same reasoning as hasAwakeningValues() / hasPokerus() above; default
         * true so every existing format is unaffected.
         */
        virtual bool hasNature() const noexcept { return true; }
        virtual bool hasAbility() const noexcept { return true; }
        virtual bool hasHeldItem() const noexcept { return true; }
        virtual bool hasFriendship() const noexcept { return true; }
        virtual bool hasBall() const noexcept { return true; }
        virtual bool hasMetData() const noexcept { return true; } // met location / met level
        /// Does the format record the DATE a Pokemon was met? Gens 1-3 do not, and Gen 2 is why
        /// this is separate from hasMetData(): Crystal records a met location and a met level and
        /// no date at all, so one predicate cannot answer for both halves.
        virtual bool hasMetDate() const noexcept { return true; }
        /// Does the format record where and when an EGG was received? Gens 1-3 have no such field
        /// -- PKHeX seals G3PKM.EggLocation at 0 -- so a Gen 3 egg is recognisable only by its met
        /// level and hatch location. Let's Go has the field and no day care to fill it.
        virtual bool hasEggData() const noexcept { return true; }
        virtual bool hasOriginGame() const noexcept { return true; }
        /// Gen 1 and Gen 2 have no PID / encryption constant. Both getters return 0 there, and 0 is
        /// a value a real PID can take, so displaying one would assert something the format cannot.
        virtual bool hasPID() const noexcept { return true; }
        /**
         * Does the format have a gender field that can be WRITTEN?
         *
         * Distinct from whether gender() returns something meaningful. Gen 1 and Gen 2 store no
         * gender at all; what a Pokemon becomes on transfer is derived from its Attack DV against
         * the species ratio, so gender() has a real answer while setGender() has nothing to write
         * to. The editor uses this to show the row read-only rather than offering a picker whose
         * every choice is silently discarded -- the way to change it is to change the Attack DV.
         */
        virtual bool hasStoredGender() const noexcept { return true; }

        /**
         * Hyper Training makes the GAME treat a stat's IV as maximal without changing the IV that
         * is stored. Bottle Caps set a flag; the IV underneath is left exactly as it was.
         *
         * That is why it cannot be ignored by anything that recomputes stats. A Hyper Trained
         * Meowscarada in a real Scarlet/Violet save stores IV 20 in HP, Defence and Special Defence
         * and has flags 0x15; the game plays all three as 31 and stores HP 356 / Def 176 / SpD 176.
         * Recomputing from the raw IVs gives 345 / 165 / 165 -- eleven points low on each,
         * silently, on any edit that touches a stat.
         *
         * The flag layout is one byte, one bit per stat, in PKHeX's order -- HP, ATK, DEF, SPA,
         * SPD, SPE -- and is identical in every format that has it. Only the OFFSET moves, and it
         * genuinely does move: PK8 / PB8 / PK9 keep it at 0x126, PB7 at 0xDE, and PA8 at 0x13E.
         *
         * Gen 1 and Gen 3 predate the mechanic entirely, so the default is "absent" and their
         * recalculation is unaffected. Absence is reported through hasHyperTraining() rather than
         * through a zero byte, for the same reason as the predicates above: 0 is also what "trained
         * nothing" looks like, and a details row must be able to tell the two apart.
         */
        virtual bool hasHyperTraining() const noexcept { return false; }
        virtual uint8_t hyperTrainFlags() const noexcept { return 0; }
        virtual void setHyperTrainFlags(uint8_t) noexcept {}

        /// The level at which the game offers Hyper Training: 100 in Gens 7 and 8, 50 from Scarlet /
        /// Violet on (PKHeX's GetHyperTrainMinLevel). Only meaningful where hasHyperTraining().
        virtual uint8_t hyperTrainMinimumLevel() const noexcept { return 100; }

        /// Whether this record could have been Hyper Trained at all. Distinct from hasHyperTraining(),
        /// which says the field exists: Legends: Arceus stores the flags but sells no Bottle Caps, so
        /// they can only have been set in a game visited through HOME (PKHeX's IsHyperTrainingAvailable).
        virtual bool isHyperTrainingAvailable() const noexcept { return hasHyperTraining(); }

        /// Stat index, in the IV/HT bit order: 0 HP, 1 ATK, 2 DEF, 3 SPA, 4 SPD, 5 SPE.
        enum HyperTrainStat : int
        {
            HT_HP = 0,
            HT_ATK = 1,
            HT_DEF = 2,
            HT_SPA = 3,
            HT_SPD = 4,
            HT_SPE = 5
        };

        bool isHyperTrained(int statIndex) const noexcept
        {
            if (!hasHyperTraining() || statIndex < 0 || statIndex > 5)
                return false;
            return ((hyperTrainFlags() >> statIndex) & 1) != 0;
        }
        /// Any stat at all -- what a details view asks before drawing an indicator.
        bool isHyperTrained() const noexcept
        {
            return hasHyperTraining() && (hyperTrainFlags() & 0x3F) != 0;
        }
        void setHyperTrained(int statIndex, bool enabled) noexcept
        {
            if (!hasHyperTraining() || statIndex < 0 || statIndex > 5)
                return;
            const uint8_t bit = static_cast<uint8_t>(1u << statIndex);
            setHyperTrainFlags(static_cast<uint8_t>(enabled ? (hyperTrainFlags() | bit) : (hyperTrainFlags() & ~bit)));
        }

        /**
         * The IV the GAME uses for this stat: the stored IV, or the format's maximum where the stat
         * has been Hyper Trained. This is what every stat calculation must read; ivHP() and friends
         * stay the raw stored value, because that is what the legality tables match an encounter
         * against and what an IV editor must show and write.
         */
        uint8_t effectiveIV(int statIndex) const noexcept
        {
            if (isHyperTrained(statIndex))
                return maxIV();
            switch (statIndex)
            {
            case HT_HP:
                return ivHP();
            case HT_ATK:
                return ivATK();
            case HT_DEF:
                return ivDEF();
            case HT_SPA:
                return ivSPA();
            case HT_SPD:
                return ivSPD();
            case HT_SPE:
                return ivSPE();
            default:
                return 0;
            }
        }

        /**
         * Awakening Values (AVs) are unique to Pokemon Let's Go Pikachu/Eevee.
         * They provide additional stat points similar to EVs but earned differently.
         * Max 200 per stat, earned by using candies or catching Pokemon.
         * Default implementation returns 0 for games without AVs.
         */
        virtual uint8_t avHP() const noexcept { return 0; }
        virtual uint8_t avATK() const noexcept { return 0; }
        virtual uint8_t avDEF() const noexcept { return 0; }
        virtual uint8_t avSPE() const noexcept { return 0; }
        virtual uint8_t avSPA() const noexcept { return 0; }
        virtual uint8_t avSPD() const noexcept { return 0; }

        virtual void setAV(int statIndex, uint8_t value) noexcept
        {
            (void)statIndex;
            (void)value;
        }

        virtual bool hasAwakeningValues() const noexcept { return false; }

        /**
         * Is this an Alpha? Legends: Arceus and Legends: Z-A only; every other format
         * has no such concept and is false.
         *
         * The legality checker reads it because an Alpha is a property of the ENCOUNTER
         * (alpha slots and alpha statics are separate templates with their own guaranteed
         * IVs), not something the player can confer afterwards. The editor still lets the
         * flag be set; legality reports the mismatch rather than refusing it.
         */
        virtual bool isAlpha() const noexcept { return false; }

        /// Whether the format stores an Alpha flag at all (Legends: Arceus, Legends: Z-A).
        virtual bool hasAlphaFlag() const noexcept { return false; }

        /**
         * Sets the Alpha flag. Setting it also gives the Pokemon the size the game gives a wild
         * Alpha, because an Alpha of ordinary size matches no encounter; clearing it leaves the
         * size alone, since a maximum-size non-Alpha is an ordinary roll. A no-op where
         * hasAlphaFlag() is false.
         */
        virtual void setAlpha(bool alpha) noexcept { (void)alpha; }

        virtual uint16_t move(int slot) const noexcept
        {
            (void)slot;
            return 0;
        }

        /// Implementations refresh the checksum. PP is NOT set -- that needs a base-PP table; call
        /// setMovePP() where it matters.
        virtual void setMove(int slot, uint16_t moveID) noexcept
        {
            (void)slot;
            (void)moveID;
        }

        virtual uint8_t movePP(int slot) const noexcept
        {
            (void)slot;
            return 0;
        }

        virtual void setMovePP(int slot, uint8_t powerPoints) noexcept
        {
            (void)slot;
            (void)powerPoints;
        }

        virtual uint8_t movePPUps(int slot) const noexcept
        {
            (void)slot;
            return 0;
        }

        virtual void setMovePPUps(int slot, uint8_t ppUps) noexcept
        {
            (void)slot;
            (void)ppUps;
        }

        virtual uint16_t relearnMove(int slot) const noexcept
        {
            (void)slot;
            return 0;
        }

        virtual void setRelearnMove(int slot, uint16_t moveID) noexcept
        {
            (void)slot;
            (void)moveID;
        }

        // Getters default to 0 and setters are no-ops so a format that hasn't been
        // wired yet still compiles. Wired formats must call refreshChecksum() in setters.

        virtual uint8_t originGame() const noexcept { return 0; }
        virtual void setOriginGame(uint8_t version) noexcept { (void)version; }

        /** Storage-format game group (which subclass this is), NOT the origin Version byte. */
        virtual Enums::GameVersion getGameGroup() const noexcept = 0;

        /** Original Trainer visible ID (TID16). */
        virtual uint16_t tid16() const noexcept { return 0; }
        virtual void setTID16(uint16_t value) noexcept { (void)value; }

        /** Original Trainer secret ID (SID16). */
        virtual uint16_t sid16() const noexcept { return 0; }
        virtual void setSID16(uint16_t value) noexcept { (void)value; }

        /** Sets the full 32-bit trainer ID (id32() is the getter). */
        virtual void setId32(uint32_t value) noexcept { (void)value; }

        /** Original Trainer gender (0 = Male, 1 = Female). */
        virtual uint8_t otGender() const noexcept { return 0; }
        virtual void setOTGender(uint8_t value) noexcept { (void)value; }

        /** Original Trainer (base) friendship (0-255). */
        virtual uint8_t otFriendship() const noexcept { return 0; }
        virtual void setOTFriendship(uint8_t value) noexcept { (void)value; }

        /** Language ID the Pokemon was raised in. */
        virtual uint8_t language() const noexcept { return 0; }
        virtual void setLanguage(uint8_t value) noexcept { (void)value; }

        /**
         * True when this record's NICKNAME and LANGUAGE are an egg placeholder rather than anything
         * about the Pokemon inside it.
         *
         * GEN 3 IS THE ONLY FORMAT THAT DOES THIS, and it does it on every cartridge: the games stamp
         * an egg with the Japanese word for egg as its nickname AND language 1, whichever region made
         * it, and PKHeX writes the same pair (`PK3.IsEgg` sets `Nickname = "タマゴ"` and
         * `Language = Japanese`). The OT bytes beside them are NOT part of the placeholder -- they stay
         * in the cartridge's own charset -- so a reader that lets the language byte pick the text table
         * renders a real English save's egg OT as full-width ＫＩＡＳＴＡ.
         *
         * Both halves are replaced when the egg hatches: the two hatched eggs in a real English FireRed
         * save carry language 2 and "CHARMANDER", beside two unhatched ones carrying language 1 and
         * タマゴ. So this answers "do not read these two fields as this Pokemon's", and nothing more.
         */
        virtual bool eggTextIsPlaceholder() const noexcept { return false; }

        /** Poke Ball the Pokemon is contained in. */
        virtual uint8_t ball() const noexcept { return 0; }
        virtual void setBall(uint8_t value) noexcept { (void)value; }

        /** Met location id (where the Pokemon was caught/received). */
        virtual uint16_t metLocation() const noexcept { return 0; }
        virtual void setMetLocation(uint16_t value) noexcept { (void)value; }

        virtual uint8_t metLevel() const noexcept { return 0; }
        virtual void setMetLevel(uint8_t value) noexcept { (void)value; }

        /** Egg location id (0 = not hatched from an egg / not applicable). */
        virtual uint16_t eggLocation() const noexcept { return 0; }
        virtual void setEggLocation(uint16_t value) noexcept { (void)value; }

        /** Met date components (year stored as years-since-2000). */
        virtual uint8_t metYear() const noexcept { return 0; }
        virtual void setMetYear(uint8_t value) noexcept { (void)value; }
        virtual uint8_t metMonth() const noexcept { return 0; }
        virtual void setMetMonth(uint8_t value) noexcept { (void)value; }
        virtual uint8_t metDay() const noexcept { return 0; }
        virtual void setMetDay(uint8_t value) noexcept { (void)value; }

        /** Egg date components (year stored as years-since-2000). */
        virtual uint8_t eggYear() const noexcept { return 0; }
        virtual void setEggYear(uint8_t value) noexcept { (void)value; }
        virtual uint8_t eggMonth() const noexcept { return 0; }
        virtual void setEggMonth(uint8_t value) noexcept { (void)value; }
        virtual uint8_t eggDay() const noexcept { return 0; }
        virtual void setEggDay(uint8_t value) noexcept { (void)value; }

        /** Sets the nickname (UTF-16, max getMaxNicknameLength() chars). Does not change isNicknamed. */
        virtual void setNickname(const std::u16string &value) noexcept { (void)value; }

        /** Nickname capacity in CHARACTERS -- 12 in every format but Gen 3, whose field is 10. */
        virtual int getMaxNicknameLength() const noexcept { return 12; }

        /**
         * Whether this format can represent every character of `value`. Only Gen 3 ever says no: it
         * predates Unicode and stores names in its own single-byte table, so a name the Switch keyboard
         * was happy to produce may be unwritable. Check before setNickname -- the Gen 3 encoder ends the
         * name at the first character it can't map, which silently truncates rather than refusing.
         */
        virtual bool canStoreNickname(const std::u16string &value) const noexcept
        {
            (void)value;
            return true;
        }

        /** "Has a custom nickname" flag. Formats that don't wire it report false / ignore the set. */
        virtual bool isNicknamed() const noexcept { return false; }
        virtual void setIsNicknamed(bool value) noexcept { (void)value; }

        /** Fateful-encounter ("obtained in a fateful encounter") flag; false / no-op where unwired. */
        virtual bool isFatefulEncounter() const noexcept { return false; }
        virtual void setFatefulEncounter(bool value) noexcept { (void)value; }

        /** Original Trainer name (UTF-16; empty if unwired). */
        virtual std::u16string otName() const { return std::u16string(); }
        virtual void setOTName(const std::u16string &value) noexcept { (void)value; }

        /**
         * Does this format have a HANDLING TRAINER at all?
         *
         * Needed for the same reason as hasNature()/hasBall(): the getter cannot answer it. An
         * empty htName() means "never traded" in a format that HAS a handler and "no such field"
         * in one that does not, and those are different facts -- so a details view that hides the
         * row whenever the name is empty tells a Sword/Shield owner nothing about a Pokemon that
         * simply has not been traded yet.
         *
         * Defaults FALSE, unlike the other capability predicates, because the base class genuinely
         * has no handler: htName() returns empty and setHTName() is a no-op. Gens 1-5 leave it
         * alone; Gen 6 onward override it to true.
         */
        virtual bool hasHandler() const noexcept { return false; }

        /** Handling (current) Trainer name (UTF-16; empty if unwired). */
        virtual std::u16string htName() const { return std::u16string(); }
        virtual void setHTName(const std::u16string &value) noexcept { (void)value; }

        /** Handling Trainer gender (0 = Male, 1 = Female). */
        virtual uint8_t htGender() const noexcept { return 0; }
        virtual void setHTGender(uint8_t value) noexcept { (void)value; }

        /** Handling Trainer friendship (0-255). */
        virtual uint8_t htFriendship() const noexcept { return 0; }
        virtual void setHTFriendship(uint8_t value) noexcept { (void)value; }

        /** Current handler flag (0 = OT active, 1 = HT active). */
        virtual uint8_t currentHandler() const noexcept { return 0; }
        virtual void setCurrentHandler(uint8_t value) noexcept { (void)value; }

        /**
         * HANDLER LANGUAGE. Generation 8 introduced it: Gens 6 and 7 record WHO handled a Pokemon
         * but not what language they played in. Gated for the same reason hasHandler() is -- 1 is
         * Japanese, a real value, so a getter returning 0 cannot say "no such field".
         */
        virtual bool hasHandlerLanguage() const noexcept { return false; }
        virtual uint8_t htLanguage() const noexcept { return 0; }
        virtual void setHTLanguage(uint8_t value) noexcept { (void)value; }

        /**
         * HANDLER MEMORIES -- what the handling trainer remembers doing with this Pokemon. A trade
         * writes one ("Link trade to <place>"), and all four fields are written, read and cleared
         * together, so they share one predicate rather than four.
         *
         * Gens 6, 7, 8 and 9 have them. **LET'S GO DOES NOT** -- PB7 dropped memories entirely,
         * even though it is otherwise a Gen 6-shaped record with a handler -- and neither do Gens
         * 1-5, which have no handler at all. Memory 0 means "no memory", which is why this is a
         * predicate and not an is-zero test on htMemory().
         */
        virtual bool hasHandlerMemories() const noexcept { return false; }
        virtual uint8_t htMemory() const noexcept { return 0; }
        virtual void setHTMemory(uint8_t value) noexcept { (void)value; }
        virtual uint16_t htMemoryVariable() const noexcept { return 0; }
        virtual void setHTMemoryVariable(uint16_t value) noexcept { (void)value; }
        virtual uint8_t htMemoryIntensity() const noexcept { return 0; }
        virtual void setHTMemoryIntensity(uint8_t value) noexcept { (void)value; }
        virtual uint8_t htMemoryFeeling() const noexcept { return 0; }
        virtual void setHTMemoryFeeling(uint8_t value) noexcept { (void)value; }

        /**
         * HANDLER AFFECTION. Generation 6 and 7 only: Gen 8 folded affection back into friendship,
         * and Let's Go allocates the byte but no game ever writes it (PKHeX marks it Unused), so
         * PB7 answers false here rather than offering a field that does nothing.
         */
        virtual bool hasHandlerAffection() const noexcept { return false; }
        virtual uint8_t htAffection() const noexcept { return 0; }
        virtual void setHTAffection(uint8_t value) noexcept { (void)value; }

        /// Handler geolocation slots a Gen 6/7 record carries -- see hasGeolocation().
        static constexpr int GEOLOCATION_SLOT_COUNT = 5;

        /**
         * GEOLOCATION -- the console country and region of each trainer who has handled this
         * Pokemon. Generation 6 and 7 only; Gen 8 dropped it and Let's Go never had it.
         *
         * **IT IS A FIVE-SLOT HISTORY, NOT ONE VALUE.** Every trade pushes the receiving trainer
         * into slot 0 and trickles the rest down a place, so writing slot 0 alone silently discards
         * the chain the games keep (PKHeX's IGeoTrack.TradeGeoLocation). Slot 0 is the most recent
         * handler.
         *
         * A slotIndex outside 0..GEOLOCATION_SLOT_COUNT-1 reads 0 and writes nothing.
         */
        virtual bool hasGeolocation() const noexcept { return false; }
        virtual uint8_t geolocationCountry(int slotIndex) const noexcept
        {
            (void)slotIndex;
            return 0;
        }
        virtual void setGeolocationCountry(int slotIndex, uint8_t value) noexcept
        {
            (void)slotIndex;
            (void)value;
        }
        virtual uint8_t geolocationRegion(int slotIndex) const noexcept
        {
            (void)slotIndex;
            return 0;
        }
        virtual void setGeolocationRegion(int slotIndex, uint8_t value) noexcept
        {
            (void)slotIndex;
            (void)value;
        }

        /**
         * Does this format carry a Pokemon HOME tracker?
         *
         * Same reasoning as hasHandler(), and the same trap: the tracker is a u64 and ZERO is the
         * real "never been through HOME" value, so homeTracker() == 0 cannot distinguish a
         * never-transferred Sword Pokemon from a Gen 3 record that has no such field. Defaults
         * FALSE -- HOME arrived with Gen 8, and every format before it genuinely has nowhere to
         * put one.
         */
        virtual bool hasHomeTracker() const noexcept { return false; }

        /**
         * The Pokemon HOME tracker: a per-Pokemon id HOME stamps on anything that passes through
         * it, and the only durable evidence that a transfer happened. 0 means never tracked.
         *
         * It is what makes "this came from another game" checkable at all in Gen 8/9 -- the origin
         * byte says which game a Pokemon was caught in, but nothing stops that byte being written
         * by hand, whereas a tracker can only be issued by HOME.
         */
        virtual uint64_t homeTracker() const noexcept { return 0; }

        /**
         * Does this format carry a Tera type?
         *
         * Same reasoning as hasHomeTracker(), and the same trap in a smaller field: the stored
         * byte is a type id and ZERO is Normal, a perfectly real Tera type, so teraType() == 0
         * cannot distinguish a Normal-Tera Scarlet Pokemon from a Gen 3 record that has no such
         * field. Defaults FALSE -- Terastallization is Scarlet/Violet's alone. Legends: Z-A is
         * Gen 9 and shares the entity layout, but it has no Terastallization and PKHeX's PA9
         * does not implement ITeraType, so it answers false like every other format.
         */
        virtual bool hasTeraType() const noexcept { return false; }

        /**
         * The Tera type the Pokemon was originally encountered with (PK9 0x94).
         * Only meaningful when hasTeraType(). May be TERA_TYPE_STELLAR (99).
         */
        virtual uint8_t teraTypeOriginal() const noexcept { return TYPE_NORMAL; }

        /**
         * The Tera type a Tera Shard changed it to (PK9 0x95), or TERA_TYPE_OVERRIDE_NONE (19)
         * when it was never changed. Only meaningful when hasTeraType().
         */
        virtual uint8_t teraTypeOverride() const noexcept { return TERA_TYPE_OVERRIDE_NONE; }

        /// Not virtual: every format that has one resolves it the same way, so a subclass overrides
        /// the two stored bytes instead.
        uint8_t teraType() const noexcept
        {
            return resolveTeraType(teraTypeOriginal(), teraTypeOverride());
        }
        virtual void setHomeTracker(uint64_t value) noexcept { (void)value; }

        // Getters for most of these already exist above (speciesID/heldItem/ability/
        // nature/statNature/form/pid/encryptionConstant/friendship/isEgg). These add
        // the write side. Implementations refresh the checksum, and recalculate stats
        // when the field affects them (species/form/statNature).

        virtual void setSpecies(uint16_t species) noexcept { (void)species; }
        virtual void setForm(uint8_t form) noexcept { (void)form; }
        virtual void setHeldItem(uint16_t item) noexcept { (void)item; }
        virtual void setAbility(uint16_t ability) noexcept { (void)ability; }

        /** Ability slot number (which of the species' abilities: 1/2/H). */
        virtual uint8_t abilityNumber() const noexcept { return 0; }
        virtual void setAbilityNumber(uint8_t number) noexcept { (void)number; }

        /**
         * THE ABILITY IS FIXED AT ENCOUNTER, not re-read when the Pokemon evolves. Legends: Z-A keeps
         * the ability the encountered species had in the same slot, so a Fletchling caught with Big
         * Pecks is still a Big Pecks Fletchinder, although Fletchinder's own slots are Flame Body and
         * Gale Wings (PKHeX AbilityVerifier.VerifyBirthAbility). Everywhere else evolving re-reads the
         * slot, so the default is false. See Pokemon::isAbilityLegal(const Pokemon &).
         */
        virtual bool hasBirthAbility() const noexcept { return false; }

        virtual void setNature(uint8_t nature) noexcept { (void)nature; }
        virtual void setStatNature(uint8_t nature) noexcept { (void)nature; }
        virtual void setPID(uint32_t pidValue) noexcept { (void)pidValue; }
        virtual void setEncryptionConstant(uint32_t encryptionConstant) noexcept { (void)encryptionConstant; }
        virtual void setFriendship(uint8_t value) noexcept { (void)value; }
        virtual void setEgg(bool isEgg) noexcept { (void)isEgg; }

        /** Sets gender (0 = Male, 1 = Female, 2 = Genderless). gender() is the getter. */
        virtual void setGender(uint8_t gender) noexcept { (void)gender; }

        /**
         * Sets the Pokemon's level (level() is the getter). Writes the level's minimum
         * total EXP, refreshes the cached party-level byte, then recalculates stats and
         * checksum. Implementations clamp to [1,100].
         */
        virtual void setLevel(uint8_t level) noexcept {} // default no-op; overridden where supported

        /** Sets total EXP directly and re-derives the level; no-op where unwired. */
        virtual void setExp(uint32_t value) noexcept { (void)value; }

        /**
         * Base stats are determined by species and don't change per individual.
         * These are looked up from the species data table.
         */
        virtual uint8_t baseHP() const noexcept = 0;
        virtual uint8_t baseATK() const noexcept = 0;
        virtual uint8_t baseDEF() const noexcept = 0;
        virtual uint8_t baseSPE() const noexcept = 0;
        virtual uint8_t baseSPA() const noexcept = 0;
        virtual uint8_t baseSPD() const noexcept = 0;

        /**
         * These are the actual stats used in battle, calculated from:
         * - Base stats (species-dependent)
         * - IVs (individual values)
         * - EVs (effort values)
         * - Nature (stat modifiers)
         * - Level
         */
        virtual uint16_t statHPMax() const noexcept = 0;
        virtual uint16_t statATK() const noexcept = 0;
        virtual uint16_t statDEF() const noexcept = 0;
        virtual uint16_t statSPE() const noexcept = 0;
        virtual uint16_t statSPA() const noexcept = 0;
        virtual uint16_t statSPD() const noexcept = 0;
        virtual uint16_t statHPCurrent() const noexcept { return statHPMax(); }
        virtual void setStatHPCurrent(uint16_t value) noexcept { (void)value; }

        virtual uint8_t friendship() const noexcept = 0;

        virtual bool isEgg() const noexcept = 0;

        virtual bool isShiny(uint32_t trainerID32, std::string species) const noexcept = 0;

        virtual bool isPokerusInfected() const noexcept = 0;

        virtual bool isPokerusCured() const noexcept = 0;

        /**
         * Sets the raw Pokerus byte (high nibble = strain, low nibble = days remaining). Canonical
         * editor values: 0x00 = none, 0x12 = freshly infected (strain 1, 2 days), 0x10 = cured.
         * Default no-op for games with no Pokerus mechanic (Let's Go).
         */
        virtual void setPokerus(uint8_t /*value*/) noexcept {}

        /** Whether this game actually has the Pokerus mechanic, so editing it is meaningful. */
        virtual bool hasPokerus() const noexcept { return false; }

        virtual uint16_t checksum() const noexcept = 0;

        virtual uint16_t calculateChecksum() const noexcept = 0;

        /**
         * Updates the stored checksum to match current data.
         * This MUST be called after any data modifications.
         */
        virtual void refreshChecksum() noexcept = 0;

        virtual bool checksumValid() const noexcept = 0;

        /**
         * Is this record structurally sound enough to keep?
         *
         * The bank uses this to decide whether bytes off the SD card decode to a real Pokemon or
         * to a ghost that would re-encrypt into a Bad Egg on withdrawal. For every format from
         * Gen 3 on the checksum answers it, which is why that WAS the test -- but Gen 1 has no
         * checksum at all, so a checksum-only test accepts any 69 bytes of garbage as valid.
         * Formats without one override this with whatever internal consistency they do have.
         */
        virtual bool isStructurallyValid() const noexcept { return checksum() == calculateChecksum(); }

        /**
         * Recalculates all battle stats based on current IVs, EVs, nature, and level.
         * This should be called after modifying any stat-affecting values.
         */
        virtual void recalculateStats() noexcept = 0;

        virtual void regeneratePID(uint32_t trainerID32) noexcept = 0;

        virtual void setShiny(bool makeShiny, uint32_t trainerID32) noexcept = 0;

        size_t getDataSize() const noexcept { return dataSize; }

        /// Writing through this span leaves the checksum stale; call refreshChecksum() afterwards.
        std::span<std::byte> getData() noexcept { return data; }

        std::span<const std::byte> getData() const noexcept { return data; }

        /** Deep-copy this Pokemon into a new owning instance (nullptr if unsupported). */
        virtual std::unique_ptr<Pokemon> clone() const { return nullptr; }

    protected:
        // Default constructor for derived classes
        Pokemon() = default;
    };
}

#endif
