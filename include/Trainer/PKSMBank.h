/**
 * PKSM (the 3DS save manager) keeps a cross-game bank of its own. This reads one and hands back
 * PKSE-native entities ready to drop into Trainer::Bank. PKSE stays AGPL-3.0 with no mixed-
 * license bookkeeping; crediting PKSM is a courtesy, not an obligation.
 *
 * THE ONE THING THAT WILL BITE YOU: PKSM stores each Pokemon **decrypted**, because that is how it
 * holds one in memory and the bank is a verbatim dump of that buffer. PKSE's bank stores NATIVE
 * (encrypted) bytes, and every ::Pokemon entity's constructor decrypts what it is handed. So the
 * import path has to ENCRYPT on the way in -- and for Gen 3 that means re-shuffling the four
 * substructures into their PID-keyed order as well, because PKSM undoes that too. A plain memcpy
 * across produces a bank full of Pokemon that look almost right and are quietly corrupt.
 *
 * The second trap is the generation tag: it is NOT the generation number. Gens 1-3 were added to
 * PKSM late and got appended to the end of its enum, so Gen 3 is 6 and tag 3 is Gen 7 (PKSMGen).
 *
 * Deliberately filesystem-free: scan() takes the file's bytes, so the parser compiles and can be
 * tested on an ordinary desktop compiler exactly like the rest of the save layer.
 */
#ifndef TRAINER_PKSM_BANK_H
#define TRAINER_PKSM_BANK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Pokemon/Pokemon.h"

namespace Trainer
{
    class Bank;

    /**
     * PKSM's frozen on-disk generation tag, stored as a u32 at each entry's offset 0.
     *
     * NOT the generation number -- gens 1-3 were appended to the end of PKSM's enum when they were
     * added, so the low values belong to gens 4-8. Never renumber; this is somebody else's on-disk
     * contract.
     */
    enum class PKSMGen : uint32_t
    {
        Gen4 = 0,
        Gen5 = 1,
        Gen6 = 2,
        Gen7 = 3,
        LGPE = 4,
        Gen8 = 5,
        Gen3 = 6,
        Gen1 = 7,
        Gen2 = 8,
        // PKSM-Core's Generation::NINE. PKSE reads no Gen 9 bank record, so it is counted as
        // unsupported (payloadLengthFor() is 0) rather than damaged.
        Gen9 = 9,
        Empty = 0xFFFFFFFFu, // an unused slot: the whole 336-byte entry is 0xFF-filled
    };

    /// Number of real (non-Empty) tag values, so a per-tag histogram can be a plain array.
    inline constexpr int PKSM_GEN_COUNT = 10;

    /// Human name for a tag value ("Gen 3", "Let's Go", ...). "Unknown" for anything unrecognised.
    const char *pksmGenName(uint32_t bankTag);

    /// True if PKSE has an entity format that can hold this tag's records.
    bool pksmGenImportable(uint32_t bankTag);

    /// What one scan()/commit() pair found and did. Every count is user-facing: the import dialog
    /// reports all of them, because "nothing happened" and "23 Pokemon had nowhere to go" look
    /// identical from the outside otherwise.
    struct PKSMImportReport
    {
        /// Non-empty means the file was not usable at all and nothing else here is meaningful.
        std::string error;

        uint32_t containerVersion = 0; // 1, 2 or 3 -- which .bnk layout was parsed
        size_t sourceBoxes = 0;        // boxes the file actually holds (after clamping to its size)
        size_t headerBoxes = 0;        // boxes its header claims; differs only on a damaged/truncated file
        /// Slots that are not the empty tag -- i.e. hold something, whether or not it parsed.
        /// `importable + unsupported + damaged` always equals this.
        size_t occupied = 0;
        size_t perGen[PKSM_GEN_COUNT] = {}; // occupied slots per tag, indexed by the tag value
        size_t importable = 0;              // entities successfully built (== staged count)
        /// Source boxes holding at least one importable Pokemon -- i.e. how many destination bank
        /// boxes commit() will consume. Not derivable from `importable`: 30 Pokemon spread one per
        /// box need 30 boxes, not one, so the preview has to be told rather than guess.
        size_t sourceBoxesUsed = 0;
        // Records of a generation PKSE has no bank format for: PKSM's Gen 9 tag. A silently
        // dropped record is the one outcome this importer must never produce, so they are counted.
        size_t unsupported = 0;
        size_t damaged = 0;     // unknown tag, or a record that failed to decode / checksum
        /// Of the Gen 3 imports, how many record an origin that is NOT FireRed or LeafGreen. PK3 is
        /// one format across all five GBA games, so a PKSM bank legitimately holds Ruby/Sapphire/
        /// Emerald mons -- and PKSM-generated ones often carry an origin byte that is no Gen 3 game
        /// at all. Either way they import and behave correctly; the count exists because PKSE has no
        /// encounter data for those origins, so the legality checker will say so and the user
        /// deserves to hear it here rather than wonder later.
        size_t gen3NonFRLG = 0;

        // filled in by commit()
        size_t placed = 0;       // Pokemon actually written into the bank
        size_t overflow = 0;     // importable Pokemon the bank had no room for
        size_t boxesUsed = 0;    // destination bank boxes consumed
        size_t namesCarried = 0; // box names copied across from the .json sidecar
        size_t scattered = 0;    // placed by the first-fit fallback rather than as a whole box
    };

    /**
     * Reads a PKSM bank and stages its importable Pokemon, then commits them into a Trainer::Bank.
     *
     * Split in two on purpose: scan() is pure (it allocates entities and touches nothing else), so
     * the UI can show the user exactly what is about to happen -- how many, of which generations,
     * and what will be skipped -- before anything is written. Nothing reaches the bank until
     * commit(), and even then it lands only in memory: the bank's own Save/Discard prompt on
     * leaving the storage view is what makes it permanent, so an import stays undoable.
     */
    class PKSMBankImport
    {
    public:
        /// Parses `file` and builds an entity for every importable slot. Returns false (with
        /// report().error set) if the file is not a PKSM bank this build can read.
        bool scan(std::span<const uint8_t> file);

        /// Supplies the box names from the `<name>.json` sidecar, so commit() can carry them over.
        /// Optional -- without it the destination boxes keep their default labels.
        void setBoxNames(std::vector<std::string> names) { srcBoxNames = std::move(names); }

        /**
         * Moves the staged Pokemon into `bank` and returns how many landed.
         *
         * Placement keeps the user's own organisation intact: each SOURCE box that holds anything is
         * copied whole into the next completely-empty destination box, at the same slot positions,
         * and takes its name with it. Boxes are packed rather than mapped 1:1 -- a 150-box PKSM bank
         * with content in five of them consumes five PKSE boxes, not 150.
         *
         * If whole empty boxes run out, the remainder falls back to first-fit into any free slot
         * rather than being refused: an import that half-succeeded and then stopped would have to be
         * re-run, and a re-run has no way to tell what it already imported -- it would duplicate.
         * Anything that still doesn't fit is reported as overflow.
         *
         * Staged entities are consumed (moved out), so a second commit() places nothing.
         */
        size_t commit(Bank &bank);

        /// Records a failure that happened before scan() could run -- the file wouldn't open, or came
        /// back empty. Kept here so the caller has ONE place a reason lives and the result screen has
        /// one code path, rather than a parallel "couldn't read it" case that drifts out of step.
        void fail(std::string reason)
        {
            clear();
            importReport.error = std::move(reason);
        }

        const PKSMImportReport &report() const noexcept { return importReport; }
        size_t stagedCount() const noexcept { return staged.size(); }
        bool empty() const noexcept { return staged.empty(); }
        void clear();

    private:
        struct Staged
        {
            uint16_t srcBox = 0;
            uint8_t srcSlot = 0;
            std::unique_ptr<::Pokemon::Pokemon> pokemon;
        };
        std::vector<Staged> staged; // in ascending (box, slot) order -- scan walks the file in order
        std::vector<std::string> srcBoxNames;
        PKSMImportReport importReport;
    };

    /**
     * Parses PKSM's box-name sidecar: a JSON array of strings, one per box.
     *
     * Two quirks are handled here. PKSM writes `length + 1` bytes so the file ends with a trailing
     * NUL, which a strict JSON parser rejects outright; and the array may be shorter than the box
     * count, because PKSM tops short arrays up with generated defaults at load time. Returns
     * whatever names it could read -- a missing or malformed sidecar is not an error, it just means
     * the destination boxes keep their default labels.
     */
    std::vector<std::string> parsePKSMBoxNames(std::span<const uint8_t> json);
}

#endif
