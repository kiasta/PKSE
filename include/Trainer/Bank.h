/**
 * A HOME-style storage bank: boxes of Pokemon that live OUTSIDE any single save file,
 * persisted to the SD card under sdmc:/PKSE/bank. UNIFIED across all games: every slot
 * carries its own game-group tag + native (encrypted) per-generation bytes, so Pokemon from all
 * six titles coexist in one bank. Deposit is passive (store as-is, byte-in == byte-out) --
 * the bank never converts or mutates a stored pokemon. Only withdraw-INTO-a-save converts, and
 * that cross-generation conversion is the caller's job (see TrainerViewScreen), not the bank's.
 */
#ifndef TRAINER_BANK_H
#define TRAINER_BANK_H

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Pokemon/Pokemon.h"
#include "Enums/GameVersion.h"

namespace Trainer
{
    class Bank
    {
    public:
        /// Number of bank boxes. 8 was not enough to stage a full generation for testing (Gen 3 alone
        /// is 386 Pokemon = 13 boxes); PKSM defaults to 50 and allows up to 500. The on-disk record
        /// table is fixed-size so that slot N sits at a computable offset, which makes the file grow
        /// with this constant -- 200 boxes is ~2.2 MB.
        ///
        /// 200 rather than 100 because a real PKSM bank is commonly 150 boxes, and importing one
        /// copies each source box that holds anything into a whole empty destination box, so the
        /// destination needs room for the user's PKSM layout AND whatever they already had here.
        ///
        /// RAISING this is safe: the header stores the count the file was written with and load()
        /// honours THAT, so a smaller older bank still opens (see load()). LOWERING it is not, and
        /// that is not hypothetical -- this constant was briefly reduced to 100 while a bank written
        /// at 200 sat on the SD card, and every launch logged "file has more boxes than this build
        /// supports" because the next save would have dropped boxes 101-200. Treat it as a ratchet.
        static constexpr size_t BANK_BOX_COUNT = 200;
        static constexpr size_t BANK_SLOTS_PER_BOX = 30; // 6x5 grid per box

        /// Constructs the unified bank and loads any existing on-SD contents. On first run it
        /// also migrates the legacy per-game bank files (gg/swsh/za/... _bank.dat), if present.
        Bank();

        /// Redirects the bank file to `dir` (which must exist) instead of sdmc:/PKSE/bank. Set to ""
        /// to go back to the real one.
        ///
        /// This exists for exactly one caller: the on-console self test, which deposits sample
        /// Pokemon, saves, reloads and byte-compares to prove the bank's byte-in == byte-out
        /// contract. Doing that against the user's real bank would mean a test run rewriting the
        /// file their whole collection lives in, so it gets a scratch bank instead. Legacy
        /// migration is skipped while redirected -- importing the user's old per-group banks into
        /// a throwaway file would be pointless work and a confusing log line.
        ///
        /// Process-wide and NOT thread safe; the self test is the only writer and it runs alone.
        static void setRootOverride(const std::string &directory);
        static bool rootOverridden() noexcept;

        /// (Re)loads the bank from its SD file, discarding any in-memory changes. Clears all
        /// slots first, so this doubles as the "discard changes" path (revert to on-disk state).
        void load();

        /// Writes the bank to its SD file (tagged, encrypted records). Returns true on success.
        bool save() const;

        /// True if the in-memory boxes differ from the last saved/loaded on-disk state.
        /// Used to prompt Save/Discard when leaving the storage view (HOME-style).
        bool hasChanged() const;

        size_t boxCount() const noexcept { return BANK_BOX_COUNT; }
        size_t slotsPerBox() const noexcept { return BANK_SLOTS_PER_BOX; }

        /// The group a record of `group` comes back as after a bank round trip.
        ///
        /// A BANK TAG NAMES AN ENTITY FORMAT, NOT A GAME. Several groups share one -- all five GBA
        /// games store a PK3, DP/Pt/HGSS a PK4, BW/B2W2 a PK5, XY/ORAS a PK6, SM/USUM a PK7 -- and
        /// the tag is a frozen on-disk value, so a banked record has no game of origin to remember.
        /// Reading one back therefore yields the format's representative group (the superset, whose
        /// tables cover the others'): a Ruby PK3 returns FireRed/LeafGreen, a Platinum PK4 returns
        /// HeartGold/SoulSilver.
        ///
        /// Anything comparing a group ACROSS the bank must compare against this rather than against
        /// what it deposited. Returns Invalid for a group the bank cannot store.
        static Enums::GameVersion groupAsBanked(Enums::GameVersion group) noexcept;

        /// Longest bank box name we store/accept (characters). Cosmetic; keeps the names section bounded.
        static constexpr size_t MAX_BOX_NAME_LEN = 24;

        /// Bank Pokemon storage [box][slot]; nullptr = empty (mirrors Trainer::boxes). Slots may
        /// hold mons from different games -- each entity knows its own getGameGroup().
        std::vector<std::array<std::unique_ptr<::Pokemon::Pokemon>, BANK_SLOTS_PER_BOX>> boxes;

        /// Optional per-box display names (parallel to `boxes`). Empty = use the default "Bank N"
        /// label. Persisted alongside the mons and included in hasChanged(), so a rename triggers
        /// the Save/Discard prompt on exit exactly like moving a Pokemon does.
        std::array<std::string, BANK_BOX_COUNT> boxNames;

        /// The label to show for a bank box: the user's name if set, else "Bank N" (1-indexed).
        std::string boxDisplayName(size_t box) const;

        /// Which bank box the storage view's bank pane opens on -- where the user left the bank.
        ///
        /// IT LIVES IN THE BANK, AND THAT IS WHY MOVING IT COUNTS AS AN UNSAVED CHANGE. It rides in
        /// serialize(), so hasChanged() reports a box move exactly as it reports a rename or a
        /// deposit, save() persists it and load() -- which is also the Discard path -- puts it back
        /// where it was. Scrolling the bank and then choosing Discard therefore returns you to the
        /// box you last SAVED, not the one you wandered to, which is the whole point: the open box
        /// is part of how you left the bank, so it is kept when the bank is kept and dropped when
        /// the bank is dropped.
        ///
        /// It was briefly a settings.cfg key instead (`g_bankBoxIndex`). That could not work: a
        /// setting is written the moment it changes, so a cursor move became a persisted decision
        /// with no way to undo it, and no way to save it either -- a box change alone left
        /// hasChanged() false, so the Save/Discard prompt never appeared.
        ///
        /// The save side needs no equivalent: a save FILE records the box the player was last on,
        /// so Trainer::getCurrentBox already answers that for the Boxes view and the save pane.
        uint16_t currentBox = 0;

        /// Slots dropped by the last load() because their record didn't decode to a valid Pokemon
        /// (bad checksum, species 0, unknown tag). Non-zero means the bank file was damaged.
        size_t lastLoadRejects() const noexcept { return loadRejects; }

        /// Occupied slots whose bytes did not survive an encrypt->decrypt round trip during the
        /// last save(). The bank's contract is byte-in == byte-out, so non-zero means a PKSE bug.
        ///
        /// Reported rather than enforced ON PURPOSE. A failed bank save blocks leaving the storage
        /// view, so treating a verification miss as a save failure would trap the user in the
        /// UI over what may be a false positive. Writing proceeds; the anomaly is surfaced instead.
        size_t lastVerifyFailures() const noexcept { return verifyFailures; }

        /// Build an entity of `group`'s format over `record`.
        ///
        /// This is the one place that knows which class every save-format group stores, which is
        /// why it is exposed rather than kept to the bank: it is the general "make an entity of
        /// this format" facility, and a second copy of the mapping elsewhere is a table that has
        /// to be kept in step with this one. Returns nullptr for a group with no entity class.
        ///
        /// `record` must be recordSizeFor(group) bytes -- several formats read their locale or
        /// their battle-stat tail from the span's LENGTH, so a short span reads plausible garbage
        /// rather than failing. Gen 1 and Gen 2 have two lengths per group; use recordSizeForTag()
        /// when the on-disk tag is what says which.
        static std::unique_ptr<::Pokemon::Pokemon> makePokemon(Enums::GameVersion group,
                                                               std::span<const std::byte> record);
        /// Encrypt one DECRYPTED record into the form `group`'s entity constructors take -- the other
        /// half of makePokemon(), exposed for the same reason. Gens 1 and 2 have no encryption and get
        /// a plain copy. Returns a `new[]` buffer of `decryptedRecord.size()` bytes the caller
        /// releases with `delete[]`, or nullptr for a group with no entity class.
        static std::byte *encryptRecord(Enums::GameVersion group, std::span<const std::byte> decryptedRecord,
                                        uint32_t encryptionConstant);
        /// Bytes one entity of `group` occupies -- the PARTY size, since several formats keep level
        /// and the battle stats in a tail the accessors index straight into.
        static size_t recordSizeFor(Enums::GameVersion group);
        /// Record size for an on-disk bank tag, for the two groups whose tags share a group but
        /// not a length (Gen 1 and Gen 2, international vs Japanese).
        static size_t recordSizeForTag(uint32_t bankTag);

    private:
        static std::string bankDir(); // sdmc:/PKSE/bank, or the override
        std::string filePath() const;
        std::vector<uint8_t> serialize() const; // full on-disk image of the current boxes
        void migrateLegacyBanks();              // one-time import of the old per-group *_bank.dat files
        /// Re-parse a serialized image and compare each occupied slot back to the live Pokemon.
        /// Returns the number of slots that failed; also fills verifyFailures.
        size_t verifyImage(const std::vector<uint8_t> &image) const;
        mutable std::vector<uint8_t> savedImage; // serialized image as of the last load()/save()
        mutable size_t verifyFailures = 0;
        size_t loadRejects = 0;
    };
}

#endif
