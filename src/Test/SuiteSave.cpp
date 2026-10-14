/**
 * Four chunks, the first three in increasing order of what they would catch.
 *
 * 1. STRUCTURE. Cheap invariants about the parsed save -- box counts, name lengths, pouch capacities,
 *    the id32 composition, the box-grid rule the UI draws from. A save that fails one of these is a
 *    save the editor will misdraw or refuse to write, and the failure is usually a one-line offset.
 *
 * 2. THE UNMODIFIED ROUND TRIP. Write the save straight back without changing anything, read it in
 *    again, and compare. This is the single highest-value test in the framework and it is the one
 *    that has already earned its place: run against real Gen 1 saves on the host it found five
 *    separate bugs, every one of them PKSE quietly rewriting bytes it had no business touching --
 *    item quantities clamped to a cap the real save exceeded, a level byte written where the game
 *    left it stale, an empty name re-terminated, a "boxes initialised" bit asserted on a new save,
 *    list markers filled to capacity. An editor that cannot reproduce a file it did not change will
 *    corrupt one it did.
 *
 *    The comparison is of the FILE, byte for byte. A few rewrites are documented and correct, and
 *    each is allowed only where it can be proven to be that rewrite:
 *      - Gen 1 flushes its current-box mirror into the bank copy (the same proof gen1.cpp makes);
 *      - Gen 2 brings its stale trainer-block backup into line (the same proof oldgens.cpp makes);
 *      - the SCBlock games are compared block by block after decryption: the box block may change
 *        only in EMPTY slots (normalised to one canonical blank), and the Pokedex blocks may change
 *        (registration of everything in storage). Any other block that changed is a failure.
 *    Let's Go and Brilliant Diamond / Shining Pearl also register the Pokedex on write, in a layout
 *    this suite cannot attribute a byte to, so a change there is a WARNING naming the count and the
 *    first offset. Every other format must come back identical. The semantic re-read follows, so a
 *    change is described in terms of the data as well as the bytes.
 *
 * 3. EDIT PERSISTENCE. Change money, the trainer name, a box name, the current box, an item count and
 *    a Pokemon's IVs / EVs / nickname; save; read back; check each one individually. This is exactly
 *    the click-through a human does after every change to the save layer, and it is the reason the
 *    edits are checked one at a time -- "the save didn't work" is not a bug report, "the box name
 *    persisted and the item count didn't" is.
 *
 * 4. ENVELOPES. The same save inside every envelope an emulator, dumper or online service writes around
 *    one of its size (Save/SaveEnvelope.h): it must open as itself, and an unedited write must leave the
 *    envelope exactly as it was -- an emulator that cannot read its own file back has lost the save.
 *    A Switch Online header's hash is the one byte range that has to change, and must match the save.
 *
 * Everything happens inside the run's scratch directory. The live game save is never written.
 */
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "Test/TestSuite.h"
#include "Test/SaveEnvelopeFixtures.h"
#include "Save/GetSaveFileContents.h"
#include "Save/SaveEnvelope.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"
#include "Encryption/Encryption.h"
#include "UI/Common.h"
#include "Names/ItemNames.h"
#include "Names/ItemPouches.h"
#include "Utils/EventLog.h"
#include "Utils/FileUtilities.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        enum Chunk
        {
            StructureChunk = 0,
            RoundTripChunk,
            EditPersistChunk,
            EnvelopeChunk,
            ChunkCount
        };

        /// Short ASCII probes. Every charset PKSE reads -- Gen 1's, Gen 3's, UTF-16 -- can spell
        /// these, and they are short enough for a Japanese Gen 1 save's five-character name fields.
        const std::string PROBE_TRAINER_NAME = "PKS";
        const std::string PROBE_BOX_NAME = "PKSE";
        const std::u16string PROBE_NICKNAME = u"PKS";
        constexpr uint16_t PROBE_ITEM_COUNT = 7;
        constexpr uint8_t PROBE_IV = 3;

        /// The file inside `dir` that IS the save. Most groups have a fixed name, but Gen 1 and Gen 3
        /// arrive as whatever the dump was called ("FireRed_e.sav", "sav.dat"), so the fallback picks
        /// the largest regular file -- which for a save directory is always the save.
        std::string primarySaveFile(const std::string &directory, Enums::GameVersion group)
        {
            const char *named = Enums::getSaveFileName(group);
            struct stat entryStatus{};
            if (named && stat((directory + "/" + named).c_str(), &entryStatus) == 0 && S_ISREG(entryStatus.st_mode))
                return directory + "/" + named;

            DIR *d = opendir(directory.c_str());
            if (!d)
                return std::string();
            std::string best;
            off_t bestSize = -1;
            struct dirent *e;
            while ((e = readdir(d)) != nullptr)
            {
                if (e->d_type == DT_DIR)
                    continue;
                const std::string entryPath = directory + "/" + e->d_name;
                struct stat childStatus{};
                if (stat(entryPath.c_str(), &childStatus) != 0 || !S_ISREG(childStatus.st_mode))
                    continue;
                if (childStatus.st_size > bestSize)
                {
                    bestSize = childStatus.st_size;
                    best = entryPath;
                }
            }
            closedir(d);
            return best;
        }

        std::vector<uint8_t> readFile(const std::string &path)
        {
            size_t length = 0;
            uint8_t *raw = Utils::readAllBytes(path.c_str(), &length);
            if (!raw)
                return {};
            std::vector<uint8_t> out(raw, raw + length);
            delete[] raw;
            return out;
        }

        /// A reloaded save. Two holders for the same reason the runner has two: Gen 1 is not a
        /// member of TrainerVariant, because a .sav has no title id to dispatch on.
        struct Reloaded
        {
            std::optional<Save::TrainerVariant> variant;
            /// A loose save is owned through the BASE pointer, exactly as Save::openExternalSave
            /// hands it over: the concrete class is not known until the probe chain has run, and
            /// these types are too large and too polymorphic to join TrainerVariant.
            std::unique_ptr<Trainer::Trainer> loose;
            Trainer::Trainer *ptr = nullptr;
        };

        /// Writes the loaded trainer back into its scratch directory. injectToTitle is FALSE and always
        /// will be: this must never reach the live game save.
        ///
        /// BOTH BRANCHES CALL THE APP'S OWN WRITE PATH. A loose save goes through
        /// Save::saveExternalSave, which already dispatches over every loose group and preserves an RTC
        /// footer -- so what this exercises and what the file browser does cannot drift apart. A private
        /// list of supported groups here is what silently reports "writing the save back failed" for a
        /// write that was never attempted.
        ///
        /// `backupOriginal` is false for the same reason `injectToTitle` is: this is a throwaway copy, and
        /// a backup per write would leave megabytes in the user's own PKSE folder.
        bool writeSave(Context &context)
        {
            if (context.titleId != 0)
                return Save::saveTrainerInfo(*context.trainer, context.workDir.c_str(), context.titleId, context.accountUid, false);
            if (context.savePath.empty())
                return false;
            return Save::saveExternalSave(*context.trainer, context.savePath.c_str(), false);
        }

        /// Reads the scratch save back. A loose file goes through Save::openExternalSave -- the
        /// same probe chain the file browser and the runner use -- rather than constructing one
        /// generation's Trainer directly, so this needs no per-group knowledge and a new format
        /// arrives here for free.
        bool reloadSave(Context &context, Reloaded &out)
        {
            if (context.titleId != 0)
            {
                out.variant.emplace(Save::readTrainerInfo(context.workDir.c_str(), context.titleId));
                out.ptr = std::visit([](auto &trainer) -> Trainer::Trainer *
                                     { return &trainer; }, *out.variant);
                return out.ptr != nullptr;
            }
            if (context.savePath.empty())
                return false;
            std::vector<uint8_t> bytes = readFile(context.savePath);
            if (bytes.empty())
                return false;
            out.loose = Save::openExternalSave(std::move(bytes), context.savePath, nullptr);
            out.ptr = out.loose.get();
            return out.ptr != nullptr;
        }

        bool sameEntity(const Pokemon::Pokemon &abilityId, const Pokemon::Pokemon &b, size_t &firstDiff)
        {
            const auto firstData = abilityId.getData(), db = b.getData();
            if (firstData.size() != db.size())
            {
                firstDiff = 0;
                return false;
            }
            for (size_t daIndex = 0; daIndex < firstData.size(); ++daIndex)
                if (firstData[daIndex] != db[daIndex])
                {
                    firstDiff = daIndex;
                    return false;
                }
            return true;
        }

        /// Compare the trainer PKSE holds against the one it just read back from its own write.
        /// Every difference is a failure: the write path is supposed to be lossless.
        void compareTrainers(Context &context, const Trainer::Trainer &before, const Trainer::Trainer &after, const char *stage)
        {
            const std::string stageTag = std::string("stage=") + stage;

            context.check(before.trainerName == after.trainerName, "trainer name did not survive the save", stageTag + " before=\"" + before.trainerName + "\" after=\"" + after.trainerName + "\"");
            context.check(before.money == after.money, "money did not survive the save", stageTag + " before=" + std::to_string(before.money) + " after=" + std::to_string(after.money));
            context.check(before.ID32 == after.ID32, "trainer ID did not survive the save", stageTag + " " + hex("before", before.ID32, 8) + " " + hex("after", after.ID32, 8));
            context.check(before.trainerGender == after.trainerGender, "trainer gender did not survive the save", stageTag);
            context.check(before.getCurrentBox() == after.getCurrentBox(), "current box did not survive the save", stageTag + " before=" + std::to_string(before.getCurrentBox()) + " after=" + std::to_string(after.getCurrentBox()));

            if (!context.check(before.boxes.size() == after.boxes.size(), "box count changed across the save", stageTag + " before=" + std::to_string(before.boxes.size()) + " after=" + std::to_string(after.boxes.size())))
                return;

            context.check(before.party.size() == after.party.size(), "party size changed across the save", stageTag + " before=" + std::to_string(before.party.size()) + " after=" + std::to_string(after.party.size()));
            const size_t party = before.party.size() < after.party.size() ? before.party.size() : after.party.size();
            for (size_t index = 0; index < party; ++index)
            {
                // Occupancy, not the pointer -- see isEmptySlot. Comparing the BYTES of two empty
                // slots is not a test of anything: several games rewrite an unoccupied slot with a
                // canonical blank on purpose, so the bytes legitimately differ while nothing was
                // stored in either one.
                const bool beforeOccupied = !isEmptySlot(before.party[index].get()), aa = !isEmptySlot(after.party[index].get());
                if (!context.check(beforeOccupied == aa, "a party slot gained or lost a Pokemon across the save", stageTag + " " + partySlot((int)index) + " before=" + (beforeOccupied ? "pokemon" : "empty") + " after=" + (aa ? "pokemon" : "empty")))
                    continue;
                if (!beforeOccupied)
                    continue;
                size_t diff = 0;
                context.check(sameEntity(*before.party[index], *after.party[index], diff), "a party Pokemon's bytes changed across the save", stageTag + " " + partySlot((int)index) + " " + monLabel(*before.party[index]) + " " + hex("offset", diff, 4));
            }

            const size_t slots = before.getSlotsPerBox();
            for (size_t boxIndex = 0; boxIndex < before.boxes.size(); ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < slots && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    const bool beforeOccupied = !isEmptySlot(before.boxes[boxIndex][slotIndex].get()), aa = !isEmptySlot(after.boxes[boxIndex][slotIndex].get());
                    if (!context.check(beforeOccupied == aa, "a box slot gained or lost a Pokemon across the save", stageTag + " " + slot((int)boxIndex, (int)slotIndex) + " before=" + (beforeOccupied ? "pokemon" : "empty") + " after=" + (aa ? "pokemon" : "empty")))
                        continue;
                    if (!beforeOccupied)
                        continue;
                    size_t diff = 0;
                    context.check(sameEntity(*before.boxes[boxIndex][slotIndex], *after.boxes[boxIndex][slotIndex], diff), "a box Pokemon's bytes changed across the save", stageTag + " " + slot((int)boxIndex, (int)slotIndex) + " " + monLabel(*before.boxes[boxIndex][slotIndex]) + " " + hex("offset", diff, 4));
                }
            }

            if (context.check(before.items.size() == after.items.size(), "pouch count changed across the save", stageTag + " before=" + std::to_string(before.items.size()) + " after=" + std::to_string(after.items.size())))
            {
                for (size_t itemIndex = 0; itemIndex < before.items.size(); ++itemIndex)
                {
                    if (!context.check(before.items[itemIndex].size() == after.items[itemIndex].size(), "a pouch's item count changed across the save", stageTag + " pouch=" + std::to_string(itemIndex) + " before=" + std::to_string(before.items[itemIndex].size()) + " after=" + std::to_string(after.items[itemIndex].size())))
                        continue;
                    for (size_t itemSlotIndex = 0; itemSlotIndex < before.items[itemIndex].size(); ++itemSlotIndex)
                    {
                        const auto &dialogX = before.items[itemIndex][itemSlotIndex];
                        const auto &dialogY = after.items[itemIndex][itemSlotIndex];
                        context.check(dialogX.itemId == dialogY.itemId && dialogX.count == dialogY.count, "an item changed across the save", stageTag + " pouch=" + std::to_string(itemIndex) + " index=" + std::to_string(itemSlotIndex) + " before=" + std::to_string(dialogX.itemId) + "x" + std::to_string(dialogX.count) + " after=" + std::to_string(dialogY.itemId) + "x" + std::to_string(dialogY.count));
                    }
                }
            }

            if (before.supportsBoxNames())
            {
                const size_t byteCount = before.boxNames.size() < after.boxNames.size() ? before.boxNames.size() : after.boxNames.size();
                context.check(before.boxNames.size() == after.boxNames.size(), "box name count changed across the save", stageTag);
                for (size_t index = 0; index < byteCount; ++index)
                    context.check(before.boxNames[index] == after.boxNames[index], "a box name changed across the save", stageTag + " box=" + std::to_string(index + 1) + " before=\"" + before.boxNames[index] + "\" after=\"" + after.boxNames[index] + "\"");
            }
        }

        void runStructure(Context &context)
        {
            const Trainer::Trainer &tr = *context.trainer;

            context.check(tr.boxes.size() == tr.getBoxCount(), "the trainer holds a different number of boxes than the game has", "boxes=" + std::to_string(tr.boxes.size()) + " getBoxCount=" + std::to_string(tr.getBoxCount()));
            context.check(tr.getSlotsPerBox() > 0 && tr.getSlotsPerBox() <= Trainer::BOX_SLOTS, "slots per box is outside the storage array", "slots=" + std::to_string(tr.getSlotsPerBox()) + " arraySize=" + std::to_string(Trainer::BOX_SLOTS));
            // The rule the whole UI draws boxes from. Every box PKSE handles is five rows with a
            // width that follows the slot count; a size that does not divide into five rows would be
            // drawn with slots that cannot be reached.
            context.check(UI::boxGridColumns((int)tr.getSlotsPerBox()) * UI::BOX_GRID_ROWS == (int)tr.getSlotsPerBox(), "this game's box size does not lay out as five rows", "slots=" + std::to_string(tr.getSlotsPerBox()) + " cols=" + std::to_string(UI::boxGridColumns((int)tr.getSlotsPerBox())));
            // ZERO is a legal party. A save made before the starter has none, and PKSE can empty
            // one deliberately -- so only the upper bound is a real constraint. Requiring >= 1
            // reported a failure against a genuinely fresh LeafGreen save (report 2026-08-28:
            // "party size is out of range  party=0", trainer="KIASTA", one item, no Pokemon
            // anywhere), which is the test being wrong about the game rather than a bug in it.
            context.check(tr.getPartySize() <= Trainer::MAX_PARTY_SLOTS, "party size is out of range", "party=" + std::to_string(tr.getPartySize()) + " max=" + std::to_string(Trainer::MAX_PARTY_SLOTS));
            context.check(tr.getCurrentBox() < tr.getBoxCount(), "current box is past the last box", "current=" + std::to_string(tr.getCurrentBox()) + " boxes=" + std::to_string(tr.getBoxCount()));

            context.warn(!tr.trainerName.empty(), "save has no trainer name");
            // The name length is counted in CHARACTERS, so a multi-byte name has to be measured as
            // UTF-16 rather than as bytes -- measuring the UTF-8 would fail on any accented name.
            const size_t nameLen = Utils::utf8ToUtf16(tr.trainerName).size();
            context.check(nameLen <= tr.getMaxTrainerNameLength(), "trainer name is longer than this game allows", "name=\"" + tr.trainerName + "\" length=" + std::to_string(nameLen) + " max=" + std::to_string(tr.getMaxTrainerNameLength()));
            context.check(tr.money <= tr.getMaxMoney(), "money is above this game's maximum", "money=" + std::to_string(tr.money) + " max=" + std::to_string(tr.getMaxMoney()));
            context.check(tr.ID32 == ((uint32_t)tr.SID16 << 16 | tr.TID16), "trainer ID32 is not sid16<<16 | tid16", hex("id32", tr.ID32, 8) + " tid=" + std::to_string(tr.TID16) + " sid=" + std::to_string(tr.SID16));

            if (tr.supportsBoxNames())
            {
                context.check(tr.boxNames.size() == tr.getBoxCount(), "the save has a different number of box names than boxes", "names=" + std::to_string(tr.boxNames.size()) + " boxes=" + std::to_string(tr.getBoxCount()));
                for (size_t boxNameIndex = 0; boxNameIndex < tr.boxNames.size(); ++boxNameIndex)
                {
                    // A name PKSE read but would refuse to write is a charset gap: the moment
                    // anything re-saves, that box loses its name.
                    context.check(tr.canStoreBoxName(tr.boxNames[boxNameIndex]), "PKSE can read a box name but would refuse to write it back", "box=" + std::to_string(boxNameIndex + 1) + " name=\"" + tr.boxNames[boxNameIndex] + "\"");
                    const size_t nameLength = Utils::utf8ToUtf16(tr.boxNames[boxNameIndex]).size();
                    context.check(nameLength <= tr.getMaxBoxNameLength() || tr.getMaxBoxNameLength() == 0, "a box name is longer than this game allows", "box=" + std::to_string(boxNameIndex + 1) + " length=" + std::to_string(nameLength) + " max=" + std::to_string(tr.getMaxBoxNameLength()));
                }
            }

            size_t totalItems = 0;
            for (size_t itemIndex = 0; itemIndex < tr.items.size(); ++itemIndex)
            {
                const size_t pouchCapacity = tr.getItemPouchCapacity((int)itemIndex);
                if (pouchCapacity > 0)
                    context.check(tr.items[itemIndex].size() <= pouchCapacity, "a pouch holds more items than it has room for", "pouch=" + std::to_string(itemIndex) + " held=" + std::to_string(tr.items[itemIndex].size()) + " capacity=" + std::to_string(pouchCapacity));
                for (const auto &it : tr.items[itemIndex])
                {
                    ++totalItems;
                    if (it.itemId == 0)
                        continue;
                    const char *itemName = Names::getItemNameFor(context.group, it.itemId);
                    context.check(itemName != nullptr && itemName[0] != '\0' && std::string(itemName) != "???", "an item in the bag has no name in this generation's table", "pouch=" + std::to_string(itemIndex) + " item=" + std::to_string(it.itemId));
                    // A zero quantity is NORMAL game state, not a defect: key items are stored
                    // with no count at all, and the Gen 7/8 bags keep an entry behind after the
                    // last one is used so the player's sort order survives. Sixteen of these on
                    // two real saves -- Rotom Bike, the Let's Go treasures, a spent Potion -- were
                    // all the games behaving correctly, and they crowded out the warnings that
                    // meant something. Recorded, because PKSE writing a zero WOULD matter, but as
                    // an observation rather than a complaint.
                    if (it.count == 0)
                        context.note("an item in the bag has a quantity of zero", "pouch=" + std::to_string(itemIndex) + " item=\"" + (itemName ? itemName : "?") + "\"");
                }
            }
            context.note("save contents", "boxes=" + std::to_string(tr.getBoxCount()) + " slotsPerBox=" + std::to_string(tr.getSlotsPerBox()) + " party=" + std::to_string(tr.party.size()) + " pouches=" + std::to_string(tr.items.size()) + " items=" + std::to_string(totalItems) + " " + Utils::logField("trainer", tr.trainerName));
        }

        bool isGen1Image(const std::vector<uint8_t> &bytes)
        {
            bool japanese = false;
            return Trainer::Trainer1RBY::detect(bytes, japanese);
        }

        bool isGen2Image(const std::vector<uint8_t> &bytes) { return Trainer::Trainer2GSC::detect(bytes); }

        /// Where the save image starts inside the file: past an envelope's header, if it has one.
        /// The documented rewrites below are stated in image offsets.
        size_t imageOffsetInFile(const std::vector<uint8_t> &file, bool (*isImage)(const std::vector<uint8_t> &))
        {
            Save::SaveEnvelope envelope;
            std::vector<uint8_t> image;
            if (!Save::findSaveEnvelope(file, isImage, envelope, image))
                return 0;
            return envelope.headerBytes.size();
        }

        /// Gen 1: every byte that moved is in the current box's bank copy, the boxes-initialised bit,
        /// a first-flush empty header or the checksum, and the bank copy now equals the live one.
        /// The same two proofs tools/harness/gen1.cpp makes. Returns the bytes outside them.
        size_t gen1BytesOutsideFlush(const Trainer::Trainer1RBY &trainer, const std::vector<uint8_t> &before, const std::vector<uint8_t> &after, size_t imageOffset, bool &bankMatchesLive)
        {
            const Trainer::Gen1BoxFlush flush = trainer.currentBoxFlush();
            const bool bankWasInitialized = (before[imageOffset + flush.initializedFlagOffset] & 0x80) != 0;
            size_t outside = 0;
            for (size_t fileIndex = 0; fileIndex < before.size(); ++fileIndex)
            {
                if (before[fileIndex] == after[fileIndex])
                    continue;
                if (fileIndex < imageOffset)
                {
                    ++outside;
                    continue;
                }
                const size_t imageIndex = fileIndex - imageOffset;
                const bool inBankCopy = imageIndex >= flush.bankBoxOffset && imageIndex < flush.bankBoxOffset + flush.boxListLength;
                const bool initializedBit = imageIndex == flush.initializedFlagOffset && (before[fileIndex] ^ after[fileIndex]) == 0x80;
                bool emptyHeaderInitialised = false;
                for (const size_t boxOffset : flush.everyBankBoxOffset)
                    emptyHeaderInitialised = emptyHeaderInitialised || (!bankWasInitialized && ((imageIndex == boxOffset && after[fileIndex] == 0x00) || (imageIndex == boxOffset + 1 && after[fileIndex] == 0xFF)));
                if (!(inBankCopy || initializedBit || emptyHeaderInitialised || imageIndex == flush.checksumOffset))
                    ++outside;
            }
            const size_t bank = imageOffset + flush.bankBoxOffset;
            const size_t live = imageOffset + flush.liveBoxOffset;
            bankMatchesLive = bank + flush.boxListLength <= after.size() && live + flush.boxListLength <= after.size() && std::equal(after.begin() + bank, after.begin() + bank + flush.boxListLength, after.begin() + live);
            return outside;
        }

        /// Gen 2: every byte that moved is inside a backup mirror or a checksum word, and every
        /// mirror now equals its source. The same proofs tools/harness/oldgens.cpp makes.
        size_t gen2BytesOutsideMirrors(const Trainer::Trainer2GSC &trainer, const std::vector<uint8_t> &before, const std::vector<uint8_t> &after, size_t imageOffset, bool &mirrorsMatch)
        {
            const auto mirrors = trainer.mirrorRegions();
            const Trainer::Gen2Offsets &offset = Trainer::gen2Offsets(trainer.layout());
            size_t outside = 0;
            for (size_t fileIndex = 0; fileIndex < before.size(); ++fileIndex)
            {
                if (before[fileIndex] == after[fileIndex])
                    continue;
                if (fileIndex < imageOffset)
                {
                    ++outside;
                    continue;
                }
                const size_t imageIndex = fileIndex - imageOffset;
                if (imageIndex == offset.overallChecksumPosition || imageIndex == offset.overallChecksumPosition + 1u || imageIndex == offset.overallChecksumPosition2 || imageIndex == offset.overallChecksumPosition2 + 1u)
                    continue;
                bool covered = false;
                for (const auto &mirror : mirrors)
                    covered = covered || (imageIndex >= mirror.destinationOffset && imageIndex < mirror.destinationOffset + mirror.length);
                if (!covered)
                    ++outside;
            }
            mirrorsMatch = true;
            for (const auto &mirror : mirrors)
                for (size_t byteIndex = 0; byteIndex < mirror.length; ++byteIndex)
                {
                    const size_t destination = imageOffset + mirror.destinationOffset + byteIndex;
                    const size_t source = imageOffset + mirror.sourceOffset + byteIndex;
                    if (destination >= after.size() || source >= after.size() || after[destination] != after[source])
                        mirrorsMatch = false;
                }
            return outside;
        }

        /// The SCBlock games' box block and Pokedex blocks: the only blocks an unedited write may change.
        bool scBlockKeys(Enums::GameVersion group, uint32_t &boxKey, std::vector<uint32_t> &dexKeys)
        {
            switch (group)
            {
            case Enums::GameVersion::SWSH:
                boxKey = Trainer::BOX8_SWSH;
                dexKeys = {Trainer::SAVE_REVISION8_SWSH, Trainer::SAVE_REVISION8_R1_SWSH,
                           Trainer::SAVE_REVISION8_R2_SWSH};
                return true;
            case Enums::GameVersion::PLA:
                boxKey = Trainer::BOX8_LA;
                dexKeys = {Trainer::POKEDEX8_LA};
                return true;
            case Enums::GameVersion::SV:
                boxKey = Trainer::BOX9_SV;
                dexKeys = {Trainer::ZUKAN9_SV_PALDEA, Trainer::ZUKAN9_SV_KITAKAMI};
                return true;
            case Enums::GameVersion::ZA:
                boxKey = Trainer::BOX9_LZA;
                dexKeys = {Trainer::POKEDEX9_LZA};
                return true;
            default:
                return false;
            }
        }

        /// Decrypts both files and checks that only the box block (in empty slots) and the Pokedex
        /// blocks changed. Reports its own findings.
        void checkSCBlockRewrite(Context &context, std::vector<uint8_t> before, std::vector<uint8_t> after, uint32_t boxKey, const std::vector<uint32_t> &dexKeys)
        {
            const std::vector<Save::Block> beforeBlocks = Encryption::decrypt(before.data(), before.size());
            const std::vector<Save::Block> afterBlocks = Encryption::decrypt(after.data(), after.size());
            if (!context.check(!beforeBlocks.empty() && beforeBlocks.size() == afterBlocks.size(), "an unedited write changed the save's block list", "before=" + std::to_string(beforeBlocks.size()) + " after=" + std::to_string(afterBlocks.size())))
                return;

            const Trainer::Trainer &tr = *context.trainer;
            size_t dexBytes = 0, emptySlotBytes = 0;
            for (size_t blockIndex = 0; blockIndex < beforeBlocks.size(); ++blockIndex)
            {
                const Save::Block &was = beforeBlocks[blockIndex];
                const Save::Block &now = afterBlocks[blockIndex];
                const std::string where = hex("key", was.key, 8);
                if (!context.check(was.key == now.key && was.type == now.type && was.data.size() == now.data.size(), "an unedited write changed a block's key, type or size", where))
                    continue;
                if (was.data == now.data)
                    continue;

                size_t changed = 0;
                for (size_t byteIndex = 0; byteIndex < was.data.size(); ++byteIndex)
                    changed += was.data[byteIndex] != now.data[byteIndex];

                bool isDex = false;
                for (uint32_t dexKey : dexKeys)
                    isDex = isDex || was.key == dexKey;
                if (isDex)
                {
                    dexBytes += changed;
                    continue;
                }
                if (was.key != boxKey)
                {
                    context.fail("an unedited write changed a block it has no reason to touch", where + " changed=" + std::to_string(changed));
                    continue;
                }

                // The box block: every changed byte must sit in a slot that is empty.
                const size_t slotCount = tr.getBoxCount() * tr.getSlotsPerBox();
                const size_t slotSize = slotCount ? was.data.size() / slotCount : 0;
                if (!context.check(slotSize > 0, "could not work out the box block's slot size", where + " size=" + std::to_string(was.data.size())))
                    continue;
                size_t inOccupiedSlots = 0, firstOccupied = 0;
                for (size_t byteIndex = 0; byteIndex < was.data.size(); ++byteIndex)
                {
                    if (was.data[byteIndex] == now.data[byteIndex])
                        continue;
                    const size_t slotIndex = byteIndex / slotSize;
                    const size_t box = slotIndex / tr.getSlotsPerBox();
                    const size_t slotInBox = slotIndex % tr.getSlotsPerBox();
                    const bool empty = box >= tr.boxes.size() || slotInBox >= Trainer::BOX_SLOTS || isEmptySlot(tr.boxes[box][slotInBox].get());
                    if (empty)
                    {
                        ++emptySlotBytes;
                        continue;
                    }
                    if (inOccupiedSlots == 0)
                        firstOccupied = slotIndex;
                    ++inOccupiedSlots;
                }
                context.check(inOccupiedSlots == 0, "an unedited write changed an OCCUPIED box slot", numberField("bytes", (long long)inOccupiedSlots) + " " + slot((int)(firstOccupied / tr.getSlotsPerBox()), (int)(firstOccupied % tr.getSlotsPerBox())));
            }
            if (dexBytes)
                context.note("an unedited write re-registered the Pokedex (documented)", numberField("bytes", (long long)dexBytes));
            if (emptySlotBytes)
                context.note("an unedited write normalised empty box slots to the canonical blank (documented)", numberField("bytes", (long long)emptySlotBytes));
        }

        /// The byte-level verdict on an unedited write. See the header for the rules.
        void checkUnmodifiedBytes(Context &context, const std::vector<uint8_t> &before, const std::vector<uint8_t> &after)
        {
            size_t differing = 0, firstDiff = 0;
            for (size_t byteIndex = 0; byteIndex < before.size(); ++byteIndex)
            {
                if (before[byteIndex] == after[byteIndex])
                    continue;
                if (differing == 0)
                    firstDiff = byteIndex;
                ++differing;
            }
            const std::string summary = "count=" + std::to_string(differing) + " of " + std::to_string(before.size()) + (differing ? "  " + hex("firstOffset", firstDiff, 6) : "");
            const Enums::GameVersion group = context.group;

            uint32_t boxKey = 0;
            std::vector<uint32_t> dexKeys;
            if (scBlockKeys(group, boxKey, dexKeys))
            {
                if (differing)
                    checkSCBlockRewrite(context, before, after, boxKey, dexKeys);
                else
                    context.check(differing == 0, "an unedited write did not reproduce the file byte for byte", summary);
                return;
            }
            if (group == Enums::GameVersion::RBY)
            {
                bool bankMatchesLive = true;
                const size_t outside = gen1BytesOutsideFlush(static_cast<const Trainer::Trainer1RBY &>(*context.trainer), before, after, imageOffsetInFile(before, isGen1Image), bankMatchesLive);
                context.check(outside == 0, "an unedited write changed bytes outside the current box's flush", summary + " outside=" + std::to_string(outside));
                context.check(bankMatchesLive, "after writing, the current box's bank copy does not equal its live copy");
                return;
            }
            if (group == Enums::GameVersion::GSC)
            {
                bool mirrorsMatch = true;
                const size_t outside = gen2BytesOutsideMirrors(static_cast<const Trainer::Trainer2GSC &>(*context.trainer), before, after, imageOffsetInFile(before, isGen2Image), mirrorsMatch);
                context.check(outside == 0, "an unedited write changed bytes outside the trainer-block backup", summary + " outside=" + std::to_string(outside));
                context.check(mirrorsMatch, "after writing, the trainer-block backup does not equal its source");
                return;
            }
            if (group == Enums::GameVersion::GG || group == Enums::GameVersion::BDSP)
            {
                context.warn(differing == 0, "an unedited write changed the save (this format re-registers the Pokedex on write, which this suite cannot tell apart from anything else)", summary);
                return;
            }
            context.check(differing == 0, "an unedited write did not reproduce the file byte for byte", summary);
        }

        void runRoundTrip(Context &context)
        {
            const std::string path = context.savePath.empty() ? primarySaveFile(context.workDir, context.group) : context.savePath;
            if (!context.check(!path.empty(), "no save file found in the working directory", "dir=\"" + context.workDir + "\""))
                return;

            const std::vector<uint8_t> before = readFile(path);
            if (!context.check(!before.empty(), "the working copy of the save is empty or unreadable", "path=\"" + path + "\""))
                return;

            if (!context.check(writeSave(context), "writing the save back failed", "path=\"" + path + "\""))
                return;

            const std::vector<uint8_t> after = readFile(path);
            if (!context.check(after.size() == before.size(), "re-writing the save changed its SIZE", "before=" + std::to_string(before.size()) + " after=" + std::to_string(after.size())))
                return;

            checkUnmodifiedBytes(context, before, after);

            Reloaded again;
            if (!context.check(reloadSave(context, again), "the save PKSE just wrote could not be read back", "path=\"" + path + "\""))
                return;
            compareTrainers(context, *context.trainer, *again.ptr, "unmodified-roundtrip");
        }

        void runEditPersist(Context &context)
        {
            Trainer::Trainer &tr = *context.trainer;

            const uint32_t newMoney = tr.getMaxMoney() >= 123456u ? 123456u : (tr.getMaxMoney() / 2);
            tr.money = newMoney;

            const std::string oldName = tr.trainerName;
            tr.trainerName = PROBE_TRAINER_NAME;

            bool didBoxName = false;
            if (tr.supportsBoxNames() && !tr.boxNames.empty() && tr.canStoreBoxName(PROBE_BOX_NAME))
            {
                tr.boxNames[0] = PROBE_BOX_NAME;
                tr.markBoxNameDirty(0);
                didBoxName = true;
            }

            // A box OTHER than the open one, so the probe cannot pass by already being the answer.
            const bool canMoveCurrentBox = tr.getBoxCount() > 1;
            const uint8_t newCurrentBox = (uint8_t)(canMoveCurrentBox ? (tr.getCurrentBox() + 1) % tr.getBoxCount() : tr.getCurrentBox());
            tr.setCurrentBox(newCurrentBox);

            bool didItem = false;
            size_t itemPouch = 0, itemIndex = 0;
            for (size_t pouchIndex = 0; pouchIndex < tr.items.size() && !didItem; ++pouchIndex)
            {
                for (size_t index = 0; index < tr.items[pouchIndex].size(); ++index)
                {
                    if (tr.items[pouchIndex][index].itemId == 0)
                        continue;
                    tr.items[pouchIndex][index].count = PROBE_ITEM_COUNT;
                    itemPouch = pouchIndex;
                    itemIndex = index;
                    didItem = true;
                    break;
                }
            }
            if (!didItem)
                context.skip("no item in the bag to edit, so item persistence was not tested");

            // Find a Pokemon to edit. Boxes first, so an edit lands on the storage write path rather
            // than the party one -- the party is mirrored in more places and is the easier case.
            // OCCUPIED, not merely non-null (isEmptySlot): editing a canonical blank tests nothing,
            // because the box writer stamps a fresh blank over it and every "edit" reads back as 0.
            // The slot is remembered so the pokemon can be looked up in the SAME place afterwards --
            // "the first pokemon I can find" would silently compare two different Pokemon.
            Pokemon::Pokemon *victim = nullptr;
            std::string victimWhere;
            bool victimInParty = false;
            size_t victimBox = 0, victimSlot = 0;
            for (size_t boxIndex = 0; boxIndex < tr.boxes.size() && !victim; ++boxIndex)
                for (size_t slotIndex = 0; slotIndex < tr.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                    if (!isEmptySlot(tr.boxes[boxIndex][slotIndex].get()))
                    {
                        victim = tr.boxes[boxIndex][slotIndex].get();
                        victimBox = boxIndex;
                        victimSlot = slotIndex;
                        victimWhere = slot((int)boxIndex, (int)slotIndex);
                        break;
                    }
            for (size_t partySlotIndex = 0; partySlotIndex < tr.party.size() && !victim; ++partySlotIndex)
                if (!isEmptySlot(tr.party[partySlotIndex].get()))
                {
                    victim = tr.party[partySlotIndex].get();
                    victimInParty = true;
                    victimSlot = partySlotIndex;
                    victimWhere = partySlot((int)partySlotIndex);
                }

            uint16_t victimSpecies = 0, probeEV = 0;
            bool didNickname = false, didEV = false;
            if (victim)
            {
                victimSpecies = victim->speciesID();
                victim->setIV(1, PROBE_IV); // Attack IV

                // EVs are not universal: Let's Go trains AVs and its EV setters are deliberate
                // no-ops. Classify the same way the setters suite does -- write, read back, and
                // only carry the persistence check forward if the format took the value at all.
                // Asserting otherwise reports a field the generation does not have as a save bug.
                probeEV = static_cast<uint16_t>(victim->maxEV() > 4 ? 4 : victim->maxEV());
                victim->setEVWide(2, probeEV); // Defence
                didEV = (victim->evWide(2) == probeEV);
                if (!didEV)
                    context.note("this format does not store EVs, so EV persistence was not tested", "title=\"" + Enums::getGameVersionName(context.version) + "\"");

                if (victim->canStoreNickname(PROBE_NICKNAME))
                {
                    victim->setNickname(PROBE_NICKNAME);
                    didNickname = true;
                }
                victim->recalculateStats();
                victim->refreshChecksum();
                // The call the editor makes after every details-page edit (mirrorEditedPartyMember).
                // Let's Go keeps a party member twice, in its box slot and as a party copy laid over
                // that slot on save, so a boxed Pokemon that is also in the party loses an edit made
                // without it. Every other format does nothing here.
                if (victimInParty)
                    tr.mirrorPartyMemberFromParty(victimSlot);
                else
                    tr.mirrorPartyMemberFromBox(victimBox, victimSlot);
            }
            else
            {
                context.skip("no Pokemon in the save to edit, so entity persistence was not tested");
            }

            // AN EMPTY SLOT GETS A POKEMON: the path a bank withdrawal and the creator both take, and the
            // one where a writer that only knew how to rewrite occupied slots would lose it. A clone of
            // one this save already holds, so it is a record this format certainly accepts.
            const Pokemon::Pokemon *placeSource = nullptr;
            size_t placedBox = 0, placedSlot = 0;
            bool foundEmpty = false;
            for (const auto &member : tr.party)
                if (!placeSource && !isEmptySlot(member.get()))
                    placeSource = member.get();
            for (size_t boxIndex = 0; boxIndex < tr.boxes.size() && !(foundEmpty && placeSource); ++boxIndex)
                for (size_t slotIndex = 0; slotIndex < tr.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (!placeSource && !isEmptySlot(tr.boxes[boxIndex][slotIndex].get()))
                        placeSource = tr.boxes[boxIndex][slotIndex].get();
                    if (!foundEmpty && isEmptySlot(tr.boxes[boxIndex][slotIndex].get()))
                    {
                        placedBox = boxIndex;
                        placedSlot = slotIndex;
                        foundEmpty = true;
                    }
                }
            std::unique_ptr<Pokemon::Pokemon> placedExpected;
            if (foundEmpty && placeSource)
            {
                auto placed = placeSource->clone();
                if (context.check(placed != nullptr, "clone() returned nothing for this format"))
                {
                    placedExpected = placed->clone();
                    tr.boxes[placedBox][placedSlot] = std::move(placed);
                }
            }
            else
            {
                context.skip(foundEmpty ? "no Pokemon in the save to copy, so writing into an empty slot was not tested" : "no empty box slot, so writing into one was not tested");
            }
            // Every box slot as it stands now, so the reload can show nothing else moved.
            std::vector<uint16_t> boxSpeciesBefore;
            for (const auto &box : tr.boxes)
                for (size_t slotIndex = 0; slotIndex < tr.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                    boxSpeciesBefore.push_back(isEmptySlot(box[slotIndex].get()) ? 0 : box[slotIndex]->speciesID());

            // A NEW ITEM, the way the editor adds one in a bag that keeps a record per item id
            // (Brilliant Diamond / Shining Pearl, Scarlet / Violet, Legends: Z-A): a legal id for the
            // pouch the bag does not hold is revived from count 0, or appended. Slot-based bags need
            // the editor's pouch capacity to append, so the ui suite's add-item step covers those.
            bool didNewItem = false;
            size_t newItemPouch = 0;
            uint16_t newItemId = 0;
            if (tr.itemsAreIdIndexed())
            {
                for (size_t pouchIndex = 0; pouchIndex < tr.items.size() && !didNewItem; ++pouchIndex)
                {
                    auto &pouch = tr.items[pouchIndex];
                    for (uint16_t legalId : Names::getPouchItems(context.group, pouchIndex))
                    {
                        auto held = std::find_if(pouch.begin(), pouch.end(), [legalId](const Trainer::InventoryItem &item) { return item.itemId == legalId; });
                        if (held != pouch.end() && held->count != 0)
                            continue;
                        if (held != pouch.end())
                        {
                            held->count = 1;
                            held->isNew = true;
                        }
                        else
                        {
                            pouch.push_back(Trainer::InventoryItem{legalId, 1, true, false});
                        }
                        newItemPouch = pouchIndex;
                        newItemId = legalId;
                        didNewItem = true;
                        break;
                    }
                }
                if (!didNewItem)
                    context.skip("every legal item is already in the bag, so adding a new one was not tested");
            }
            else
            {
                context.note("this bag is slot-based; adding a new item is covered by the ui suite's add-item step");
            }

            if (!context.check(writeSave(context), "writing the edited save failed"))
                return;

            Reloaded again;
            if (!context.check(reloadSave(context, again), "the edited save could not be read back"))
                return;
            const Trainer::Trainer &re = *again.ptr;

            context.check(re.money == newMoney, "an edited money value did not persist", "wrote=" + std::to_string(newMoney) + " read=" + std::to_string(re.money));
            context.check(re.trainerName == PROBE_TRAINER_NAME, "an edited trainer name did not persist", "wrote=\"" + PROBE_TRAINER_NAME + "\" read=\"" + re.trainerName + "\"");
            if (didBoxName)
                context.check(!re.boxNames.empty() && re.boxNames[0] == PROBE_BOX_NAME, "an edited box name did not persist", "wrote=\"" + PROBE_BOX_NAME + "\" read=\"" + (re.boxNames.empty() ? std::string("<none>") : re.boxNames[0]) + "\"");
            // Every format stores which box was open except Let's Go, whose storage is one list with
            // no current box (Trainer7LGPE has no updateCurrentBoxBlock).
            if (!canMoveCurrentBox)
                context.skip("the save has one box, so the current box could not be changed");
            else if (context.group == Enums::GameVersion::GG)
                context.note("Let's Go has no current box to persist");
            else
                context.check(re.getCurrentBox() == newCurrentBox, "an edited current box did not persist", "wrote=" + std::to_string(newCurrentBox) + " read=" + std::to_string(re.getCurrentBox()));

            if (didItem)
            {
                const bool inRange = itemPouch < re.items.size() && itemIndex < re.items[itemPouch].size();
                if (context.check(inRange, "the edited item's slot no longer exists after the save", "pouch=" + std::to_string(itemPouch) + " index=" + std::to_string(itemIndex)))
                    context.check(re.items[itemPouch][itemIndex].count == PROBE_ITEM_COUNT, "an edited item quantity did not persist", "pouch=" + std::to_string(itemPouch) + " index=" + std::to_string(itemIndex) + " wrote=" + std::to_string(PROBE_ITEM_COUNT) + " read=" + std::to_string(re.items[itemPouch][itemIndex].count));
            }

            if (victim)
            {
                // The SAME slot, not the first occupied one -- a pokemon that moved is a finding of its
                // own, and re-finding it elsewhere would hide that behind a passing field check.
                const Pokemon::Pokemon *found = nullptr;
                if (victimInParty)
                {
                    if (victimSlot < re.party.size())
                        found = re.party[victimSlot].get();
                }
                else if (victimBox < re.boxes.size() && victimSlot < Trainer::BOX_SLOTS)
                {
                    found = re.boxes[victimBox][victimSlot].get();
                }
                if (isEmptySlot(found))
                    found = nullptr;

                if (context.check(found != nullptr, "the edited Pokemon is gone after the save", victimWhere))
                {
                    context.check(found->speciesID() == victimSpecies, "the edited Pokemon changed species across the save", victimWhere + " before=" + std::to_string(victimSpecies) + " after=" + std::to_string(found->speciesID()));
                    context.check(found->ivATK() == PROBE_IV, "an edited IV did not persist", victimWhere + " wrote=" + std::to_string(PROBE_IV) + " read=" + std::to_string(found->ivATK()));
                    if (didEV)
                        context.check(found->evWide(2) == probeEV, "an edited EV did not persist", victimWhere + " wrote=" + std::to_string(probeEV) + " read=" + std::to_string(found->evWide(2)));
                    if (didNickname)
                        context.check(found->nickname() == PROBE_NICKNAME, "an edited nickname did not persist", victimWhere + " wrote=\"" + Utils::utf16ToUtf8(PROBE_NICKNAME) + "\" read=\"" + Utils::utf16ToUtf8(found->nickname()) + "\"");
                    context.check(found->isStructurallyValid(), "the edited Pokemon is not structurally valid after the save", victimWhere);
                }
            }

            if (placedExpected)
            {
                const std::string where = slot((int)placedBox, (int)placedSlot) + " " + monLabel(*placedExpected);
                const Pokemon::Pokemon *placed = placedBox < re.boxes.size() && placedSlot < Trainer::BOX_SLOTS ? re.boxes[placedBox][placedSlot].get() : nullptr;
                if (context.check(!isEmptySlot(placed), "a Pokemon written into an empty slot is gone after the save", where))
                {
                    const bool same = placed->speciesID() == placedExpected->speciesID() && placed->form() == placedExpected->form() && placed->pid() == placedExpected->pid() && placed->id32() == placedExpected->id32() && placed->nickname() == placedExpected->nickname() && placed->exp() == placedExpected->exp() && placed->move(0) == placedExpected->move(0);
                    context.check(same, "a Pokemon written into an empty slot came back as a different Pokemon", where);
                    context.check(placed->isStructurallyValid(), "a Pokemon written into an empty slot is not structurally valid after the save", where);
                }
            }
            {
                // Nothing else in the boxes moved: every slot holds the species it did before the write.
                size_t index = 0, moved = 0, firstMoved = 0;
                for (const auto &box : re.boxes)
                    for (size_t slotIndex = 0; slotIndex < re.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex, ++index)
                    {
                        const uint16_t now = isEmptySlot(box[slotIndex].get()) ? 0 : box[slotIndex]->speciesID();
                        if (index < boxSpeciesBefore.size() && now != boxSpeciesBefore[index])
                        {
                            if (!moved)
                                firstMoved = index;
                            ++moved;
                        }
                    }
                context.check(moved == 0 && index == boxSpeciesBefore.size(), "after the save the boxes hold different Pokemon than were written", numberField("slots", (long long)moved) + " firstSlotIndex=" + std::to_string(firstMoved));
            }

            if (didNewItem)
            {
                bool found = false;
                uint16_t count = 0;
                if (newItemPouch < re.items.size())
                    for (const auto &item : re.items[newItemPouch])
                        if (item.itemId == newItemId && item.count != 0)
                        {
                            found = true;
                            count = item.count;
                        }
                context.check(found && count == 1, "a newly added item did not persist", "pouch=" + std::to_string(newItemPouch) + " item=" + std::to_string(newItemId) + " count=" + std::to_string(count));
            }
        }

        bool writeFile(const std::string &path, const std::vector<uint8_t> &fileBytes)
        {
            FILE *handle = fopen(path.c_str(), "wb");
            if (!handle)
                return false;
            const size_t written = fwrite(fileBytes.data(), 1, fileBytes.size(), handle);
            fclose(handle);
            return written == fileBytes.size();
        }

        void runEnvelopes(Context &context)
        {
            // FireRed/LeafGreen open both ways, so an installed copy's save is a GBA file like any other.
            // Every other installed format has no file form to wrap.
            std::string sourcePath = context.savePath;
            if (sourcePath.empty() && context.titleId != 0 && Save::groupOpensFromFile(context.group))
                sourcePath = primarySaveFile(context.workDir, context.group);
            if (sourcePath.empty())
            {
                context.note("this format has no file form, so there is no envelope to test");
                return;
            }

            Save::SaveEnvelope sourceEnvelope;
            std::vector<uint8_t> saveImage;
            if (!context.check(Save::findExternalSaveEnvelope(readFile(sourcePath), sourceEnvelope, saveImage), "the save is not a file the loose-save probe chain opens", "path=\"" + sourcePath + "\""))
                return;

            // Every envelope a save of this size turns up in, in every layout, then padded the ways it is.
            struct EnvelopeCase
            {
                Save::SaveEnvelopeKind kind;
                bool alternateLayout;
                std::vector<uint8_t> heldBytes; // the save, or the save and its padding
                std::string where;
            };
            std::vector<EnvelopeCase> envelopeCases;
            for (const Save::SaveEnvelopeKind kind : envelopesForSaveSize(saveImage.size()))
            {
                for (const bool alternateLayout : envelopeLayouts(kind))
                    envelopeCases.push_back({kind, alternateLayout, saveImage,
                                             std::string("envelope=\"") + Save::saveEnvelopeName(kind) +
                                                 "\" alternateLayout=" + (alternateLayout ? "yes" : "no")});
            }
            for (const PaddingFixture &fixture : paddingForSaveSize(saveImage.size()))
                envelopeCases.push_back({fixture.kind, false, paddedSaveBytes(fixture, saveImage),
                                         std::string("envelope=\"") + Save::saveEnvelopeName(fixture.kind) +
                                             "\" paddedTo=" + std::to_string(fixture.paddedSize)});

            if (envelopeCases.empty())
            {
                context.skip("no envelope is defined for a save of this size, so none was tested", "size=" + std::to_string(saveImage.size()));
                return;
            }

            const std::string envelopedPath = context.workDir + "/enveloped-save";
            for (const EnvelopeCase &envelopeCase : envelopeCases)
            {
                const std::string &where = envelopeCase.where;
                const std::vector<uint8_t> envelopedFile = buildEnvelopedSave(envelopeCase.kind, envelopeCase.heldBytes, envelopeCase.alternateLayout);
                if (!context.check(writeFile(envelopedPath, envelopedFile), "could not write the enveloped save", where))
                    continue;

                // The fixture is built from the layouts as their writers describe them, not by the code
                // under test, so taking it apart must give back exactly the save that went in.
                Save::SaveEnvelope builtEnvelope;
                std::vector<uint8_t> builtImage;
                if (!context.check(Save::findExternalSaveEnvelope(envelopedFile, builtEnvelope, builtImage) && builtEnvelope.kind == envelopeCase.kind && builtImage == saveImage, "the save could not be taken back out of its envelope intact", where))
                    continue;

                std::unique_ptr<Trainer::Trainer> opened = Save::openExternalSave(envelopedFile, envelopedPath, nullptr);
                if (context.check(opened && opened->getGameGroup() == context.group, "a save inside an envelope does not open as itself", where) && context.check(Save::saveExternalSave(*opened, envelopedPath.c_str(), false), "writing a save inside an envelope back failed", where))
                {
                    const std::vector<uint8_t> written = readFile(envelopedPath);
                    Save::SaveEnvelope writtenEnvelope;
                    std::vector<uint8_t> writtenImage;
                    const bool foundAgain = Save::findExternalSaveEnvelope(written, writtenEnvelope, writtenImage);
                    const size_t paddingLength = envelopeCase.heldBytes.size() - saveImage.size();
                    if (context.check(foundAgain && writtenEnvelope.kind == envelopeCase.kind && writtenEnvelope.paddingBytes.size() == paddingLength, "writing the save back lost its envelope or its padding", where))
                    {
                        // The envelope that went in, around the save that came out: anything else moved.
                        // (A run-length coded no$gba file is coded again, so it is the envelope that has to
                        // match there, not the bytes the fixture's own encoder chose.)
                        context.check(Save::joinSaveEnvelope(builtEnvelope, writtenImage) == written, "writing the save back changed its envelope", where);
                        // The same, field by field against the fixture's own parse: the padding never
                        // changes, and the header only for Switch Online, whose SHA-1 follows the save.
                        context.check(writtenEnvelope.paddingBytes == builtEnvelope.paddingBytes, "writing the save back changed the padding after it", where);
                        if (envelopeCase.kind != Save::SaveEnvelopeKind::SwitchOnlineHeader)
                            context.check(writtenEnvelope.headerBytes == builtEnvelope.headerBytes, "writing the save back changed the header before it", where);
                    }
                }
                remove(envelopedPath.c_str());
            }
        }
    }

    int savePlan(Context &context) { return context.trainer ? ChunkCount : 0; }

    void saveRun(Context &context, int chunk)
    {
        if (!context.trainer)
            return;
        switch (chunk)
        {
        case StructureChunk:
            runStructure(context);
            break;
        case RoundTripChunk:
            runRoundTrip(context);
            break;
        case EditPersistChunk:
            runEditPersist(context);
            break;
        case EnvelopeChunk:
            runEnvelopes(context);
            break;
        default:
            break;
        }
    }
}
