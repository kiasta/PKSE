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
 *        (registration of everything in storage). Any other block that changed is a failure, and so
 *        is a changed Legends: Arceus research counter: a counter, unlike a flag, grows every
 *        time it is re-registered.
 *      - Let's Go is compared block by block too: only its Pokedex block may change, and a second
 *        unedited write must then change nothing, because registering what storage holds is
 *        settled by one write.
 *    Brilliant Diamond / Shining Pearl also register the Pokedex on write, in a layout this suite
 *    cannot attribute a byte to, so a change there is a WARNING naming the count and the first
 *    offset. Every other format must come back identical. The semantic re-read follows, so a
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
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"
#include "Encryption/Encryption.h"
#include "Pokemon/DexTable8LA.h"
#include "Pokemon/FormInfo.h"
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

        /// The file inside `directory` that IS the save. Most groups have a fixed name, but Gen 1 and Gen 3
        /// arrive as whatever the dump was called ("FireRed_e.sav", "sav.dat"), so the fallback picks
        /// the largest regular file -- which for a save directory is always the save.
        std::string primarySaveFile(const std::string &directory, Enums::GameVersion group)
        {
            const char *saveFileName = Enums::getSaveFileName(group);
            struct stat entryStatus{};
            if (saveFileName && stat((directory + "/" + saveFileName).c_str(), &entryStatus) == 0 && S_ISREG(entryStatus.st_mode))
            {
                return directory + "/" + saveFileName;
            }

            DIR *directoryHandle = opendir(directory.c_str());
            if (!directoryHandle) return std::string();
            std::string largestFilePath;
            off_t largestFileSize = -1;
            struct dirent *directoryEntry;
            while ((directoryEntry = readdir(directoryHandle)) != nullptr)
            {
                if (directoryEntry->d_type == DT_DIR)
                {
                    continue;
                }
                const std::string entryPath = directory + "/" + directoryEntry->d_name;
                struct stat childStatus{};
                if (stat(entryPath.c_str(), &childStatus) != 0 || !S_ISREG(childStatus.st_mode))
                {
                    continue;
                }
                if (childStatus.st_size > largestFileSize)
                {
                    largestFileSize = childStatus.st_size;
                    largestFilePath = entryPath;
                }
            }
            closedir(directoryHandle);
            return largestFilePath;
        }

        std::vector<uint8_t> readFile(const std::string &path)
        {
            size_t fileLength = 0;
            uint8_t *rawBytes = Utils::readAllBytes(path.c_str(), &fileLength);
            if (!rawBytes) return {};
            std::vector<uint8_t> fileBytes(rawBytes, rawBytes + fileLength);
            delete[] rawBytes;
            return fileBytes;
        }

        /// A reloaded save. Two holders for the same reason the runner has two: Gen 1 is not a
        /// member of TrainerVariant, because a .sav has no title id to dispatch on.
        struct Reloaded
        {
            std::optional<Save::TrainerVariant> variant;
            /// A loose save is owned through the BASE pointer, exactly as Save::openExternalSave
            /// hands it over: the concrete class is not known until the probe chain has run, and
            /// these types are too large and too polymorphic to join TrainerVariant.
            std::unique_ptr<Trainer::Trainer> looseTrainer;
            Trainer::Trainer *trainer = nullptr;
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
            {
                return Save::saveTrainerInfo(*context.trainer, context.workDirectory.c_str(), context.titleId, context.accountUid, false);
            }
            if (context.savePath.empty()) return false;
            return Save::saveExternalSave(*context.trainer, context.savePath.c_str(), false);
        }

        /// Reads the scratch save back. A loose file goes through Save::openExternalSave -- the
        /// same probe chain the file browser and the runner use -- rather than constructing one
        /// generation's Trainer directly, so this needs no per-group knowledge and a new format
        /// arrives here for free.
        bool reloadSave(Context &context, Reloaded &reloaded)
        {
            if (context.titleId != 0)
            {
                reloaded.variant.emplace(Save::readTrainerInfo(context.workDirectory.c_str(), context.titleId));
                reloaded.trainer = std::visit([](auto &trainer) -> Trainer::Trainer * { return &trainer; }, *reloaded.variant);
                return reloaded.trainer != nullptr;
            }
            if (context.savePath.empty()) return false;
            std::vector<uint8_t> saveBytes = readFile(context.savePath);
            if (saveBytes.empty()) return false;
            reloaded.looseTrainer = Save::openExternalSave(std::move(saveBytes), context.savePath, nullptr);
            reloaded.trainer = reloaded.looseTrainer.get();
            return reloaded.trainer != nullptr;
        }

        bool sameEntity(const Pokemon::Pokemon &firstPokemon, const Pokemon::Pokemon &secondPokemon, size_t &firstDifferenceOffset)
        {
            const auto firstBytes = firstPokemon.getData();
            const auto secondBytes = secondPokemon.getData();
            if (firstBytes.size() != secondBytes.size())
            {
                firstDifferenceOffset = 0;
                return false;
            }
            for (size_t byteIndex = 0; byteIndex < firstBytes.size(); ++byteIndex)
            {
                if (firstBytes[byteIndex] != secondBytes[byteIndex])
                {
                    firstDifferenceOffset = byteIndex;
                    return false;
                }
            }
            return true;
        }

        /// Compare the trainer PKSE holds against the one it just read back from its own write.
        /// Every difference is a failure: the write path is supposed to be lossless.
        void compareTrainers(Context &context, const Trainer::Trainer &beforeTrainer, const Trainer::Trainer &afterTrainer, const char *stage)
        {
            const std::string stageTag = std::string("stage=") + stage;

            context.check(beforeTrainer.trainerName == afterTrainer.trainerName, "trainer name did not survive the save", stageTag + " before=\"" + beforeTrainer.trainerName + "\" after=\"" + afterTrainer.trainerName + "\"");
            context.check(beforeTrainer.money == afterTrainer.money, "money did not survive the save", stageTag + " before=" + std::to_string(beforeTrainer.money) + " after=" + std::to_string(afterTrainer.money));
            context.check(beforeTrainer.ID32 == afterTrainer.ID32, "trainer ID did not survive the save", stageTag + " " + hexField("before", beforeTrainer.ID32, 8) + " " + hexField("after", afterTrainer.ID32, 8));
            context.check(beforeTrainer.trainerGender == afterTrainer.trainerGender, "trainer gender did not survive the save", stageTag);
            context.check(beforeTrainer.getCurrentBox() == afterTrainer.getCurrentBox(), "current box did not survive the save", stageTag + " before=" + std::to_string(beforeTrainer.getCurrentBox()) + " after=" + std::to_string(afterTrainer.getCurrentBox()));

            if (!context.check(beforeTrainer.boxes.size() == afterTrainer.boxes.size(), "box count changed across the save", stageTag + " before=" + std::to_string(beforeTrainer.boxes.size()) + " after=" + std::to_string(afterTrainer.boxes.size())))
            {
                return;
            }

            context.check(beforeTrainer.party.size() == afterTrainer.party.size(), "party size changed across the save", stageTag + " before=" + std::to_string(beforeTrainer.party.size()) + " after=" + std::to_string(afterTrainer.party.size()));
            const size_t sharedPartySize = beforeTrainer.party.size() < afterTrainer.party.size() ? beforeTrainer.party.size() : afterTrainer.party.size();
            for (size_t partyIndex = 0; partyIndex < sharedPartySize; ++partyIndex)
            {
                // Occupancy, not the pointer -- see isEmptySlot. Comparing the BYTES of two empty
                // slots is not a test of anything: several games rewrite an unoccupied slot with a
                // canonical blank on purpose, so the bytes legitimately differ while nothing was
                // stored in either one.
                const bool beforeOccupied = !isEmptySlot(beforeTrainer.party[partyIndex].get());
                const bool afterOccupied = !isEmptySlot(afterTrainer.party[partyIndex].get());
                if (!context.check(beforeOccupied == afterOccupied, "a party slot gained or lost a Pokemon across the save", stageTag + " " + partySlot((int)partyIndex) + " before=" + (beforeOccupied ? "pokemon" : "empty") + " after=" + (afterOccupied ? "pokemon" : "empty")))
                {
                    continue;
                }
                if (!beforeOccupied)
                {
                    continue;
                }
                size_t differenceOffset = 0;
                context.check(sameEntity(*beforeTrainer.party[partyIndex], *afterTrainer.party[partyIndex], differenceOffset), "a party Pokemon's bytes changed across the save", stageTag + " " + partySlot((int)partyIndex) + " " + pokemonLabel(*beforeTrainer.party[partyIndex]) + " " + hexField("offset", differenceOffset, 4));
            }

            const size_t slotsPerBox = beforeTrainer.getSlotsPerBox();
            for (size_t boxIndex = 0; boxIndex < beforeTrainer.boxes.size(); ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < slotsPerBox && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    const bool beforeOccupied = !isEmptySlot(beforeTrainer.boxes[boxIndex][slotIndex].get());
                    const bool afterOccupied = !isEmptySlot(afterTrainer.boxes[boxIndex][slotIndex].get());
                    if (!context.check(beforeOccupied == afterOccupied, "a box slot gained or lost a Pokemon across the save", stageTag + " " + slot((int)boxIndex, (int)slotIndex) + " before=" + (beforeOccupied ? "pokemon" : "empty") + " after=" + (afterOccupied ? "pokemon" : "empty")))
                    {
                        continue;
                    }
                    if (!beforeOccupied)
                    {
                        continue;
                    }
                    size_t differenceOffset = 0;
                    context.check(sameEntity(*beforeTrainer.boxes[boxIndex][slotIndex], *afterTrainer.boxes[boxIndex][slotIndex], differenceOffset), "a box Pokemon's bytes changed across the save", stageTag + " " + slot((int)boxIndex, (int)slotIndex) + " " + pokemonLabel(*beforeTrainer.boxes[boxIndex][slotIndex]) + " " + hexField("offset", differenceOffset, 4));
                }
            }

            if (context.check(beforeTrainer.items.size() == afterTrainer.items.size(), "pouch count changed across the save", stageTag + " before=" + std::to_string(beforeTrainer.items.size()) + " after=" + std::to_string(afterTrainer.items.size())))
            {
                for (size_t pouchIndex = 0; pouchIndex < beforeTrainer.items.size(); ++pouchIndex)
                {
                    if (!context.check(beforeTrainer.items[pouchIndex].size() == afterTrainer.items[pouchIndex].size(), "a pouch's item count changed across the save", stageTag + " pouch=" + std::to_string(pouchIndex) + " before=" + std::to_string(beforeTrainer.items[pouchIndex].size()) + " after=" + std::to_string(afterTrainer.items[pouchIndex].size())))
                    {
                        continue;
                    }
                    for (size_t itemIndex = 0; itemIndex < beforeTrainer.items[pouchIndex].size(); ++itemIndex)
                    {
                        const auto &beforeItem = beforeTrainer.items[pouchIndex][itemIndex];
                        const auto &afterItem = afterTrainer.items[pouchIndex][itemIndex];
                        context.check(beforeItem.itemId == afterItem.itemId && beforeItem.count == afterItem.count, "an item changed across the save", stageTag + " pouch=" + std::to_string(pouchIndex) + " index=" + std::to_string(itemIndex) + " before=" + std::to_string(beforeItem.itemId) + "x" + std::to_string(beforeItem.count) + " after=" + std::to_string(afterItem.itemId) + "x" + std::to_string(afterItem.count));
                    }
                }
            }

            if (beforeTrainer.supportsBoxNames())
            {
                const size_t sharedBoxNameCount = beforeTrainer.boxNames.size() < afterTrainer.boxNames.size() ? beforeTrainer.boxNames.size() : afterTrainer.boxNames.size();
                context.check(beforeTrainer.boxNames.size() == afterTrainer.boxNames.size(), "box name count changed across the save", stageTag);
                for (size_t boxNameIndex = 0; boxNameIndex < sharedBoxNameCount; ++boxNameIndex)
                {
                    context.check(beforeTrainer.boxNames[boxNameIndex] == afterTrainer.boxNames[boxNameIndex], "a box name changed across the save", stageTag + " box=" + std::to_string(boxNameIndex + 1) + " before=\"" + beforeTrainer.boxNames[boxNameIndex] + "\" after=\"" + afterTrainer.boxNames[boxNameIndex] + "\"");
                }
            }
        }

        void runStructure(Context &context)
        {
            const Trainer::Trainer &trainer = *context.trainer;

            context.check(trainer.boxes.size() == trainer.getBoxCount(), "the trainer holds a different number of boxes than the game has", "boxes=" + std::to_string(trainer.boxes.size()) + " getBoxCount=" + std::to_string(trainer.getBoxCount()));
            context.check(trainer.getSlotsPerBox() > 0 && trainer.getSlotsPerBox() <= Trainer::BOX_SLOTS, "slots per box is outside the storage array", "slots=" + std::to_string(trainer.getSlotsPerBox()) + " arraySize=" + std::to_string(Trainer::BOX_SLOTS));
            // The rule the whole UI draws boxes from. Every box PKSE handles is five rows with a
            // width that follows the slot count; a size that does not divide into five rows would be
            // drawn with slots that cannot be reached.
            context.check(UI::boxGridColumns((int)trainer.getSlotsPerBox()) * UI::BOX_GRID_ROWS == (int)trainer.getSlotsPerBox(), "this game's box size does not lay out as five rows", "slots=" + std::to_string(trainer.getSlotsPerBox()) + " cols=" + std::to_string(UI::boxGridColumns((int)trainer.getSlotsPerBox())));
            // ZERO is a legal party. A save made before the starter has none, and PKSE can empty
            // one deliberately -- so only the upper bound is a real constraint. Requiring >= 1
            // reported a failure against a genuinely fresh LeafGreen save (party=0, one item, no
            // Pokemon anywhere), which is the test being wrong about the game rather than a bug in it.
            context.check(trainer.getPartySize() <= Trainer::MAX_PARTY_SLOTS, "party size is out of range", "party=" + std::to_string(trainer.getPartySize()) + " max=" + std::to_string(Trainer::MAX_PARTY_SLOTS));
            context.check(trainer.getCurrentBox() < trainer.getBoxCount(), "current box is past the last box", "current=" + std::to_string(trainer.getCurrentBox()) + " boxes=" + std::to_string(trainer.getBoxCount()));

            context.warn(!trainer.trainerName.empty(), "save has no trainer name");
            // The name length is counted in CHARACTERS, so a multi-byte name has to be measured as
            // UTF-16 rather than as bytes -- measuring the UTF-8 would fail on any accented name.
            const size_t trainerNameLength = Utils::utf8ToUtf16(trainer.trainerName).size();
            context.check(trainerNameLength <= trainer.getMaxTrainerNameLength(), "trainer name is longer than this game allows", "name=\"" + trainer.trainerName + "\" length=" + std::to_string(trainerNameLength) + " max=" + std::to_string(trainer.getMaxTrainerNameLength()));
            context.check(trainer.money <= trainer.getMaxMoney(), "money is above this game's maximum", "money=" + std::to_string(trainer.money) + " max=" + std::to_string(trainer.getMaxMoney()));
            context.check(trainer.ID32 == ((uint32_t)trainer.SID16 << 16 | trainer.TID16), "trainer ID32 is not sid16<<16 | tid16", hexField("id32", trainer.ID32, 8) + " tid=" + std::to_string(trainer.TID16) + " sid=" + std::to_string(trainer.SID16));

            if (trainer.supportsBoxNames())
            {
                context.check(trainer.boxNames.size() == trainer.getBoxCount(), "the save has a different number of box names than boxes", "names=" + std::to_string(trainer.boxNames.size()) + " boxes=" + std::to_string(trainer.getBoxCount()));
                for (size_t boxNameIndex = 0; boxNameIndex < trainer.boxNames.size(); ++boxNameIndex)
                {
                    // A name PKSE read but would refuse to write is a charset gap: the moment
                    // anything re-saves, that box loses its name.
                    context.check(trainer.canStoreBoxName(trainer.boxNames[boxNameIndex]), "PKSE can read a box name but would refuse to write it back", "box=" + std::to_string(boxNameIndex + 1) + " name=\"" + trainer.boxNames[boxNameIndex] + "\"");
                    const size_t boxNameLength = Utils::utf8ToUtf16(trainer.boxNames[boxNameIndex]).size();
                    context.check(boxNameLength <= trainer.getMaxBoxNameLength() || trainer.getMaxBoxNameLength() == 0, "a box name is longer than this game allows", "box=" + std::to_string(boxNameIndex + 1) + " length=" + std::to_string(boxNameLength) + " max=" + std::to_string(trainer.getMaxBoxNameLength()));
                }
            }

            size_t totalItemCount = 0;
            for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex)
            {
                const size_t pouchCapacity = trainer.getItemPouchCapacity((int)pouchIndex);
                if (pouchCapacity > 0)
                {
                    context.check(trainer.items[pouchIndex].size() <= pouchCapacity, "a pouch holds more items than it has room for", "pouch=" + std::to_string(pouchIndex) + " held=" + std::to_string(trainer.items[pouchIndex].size()) + " capacity=" + std::to_string(pouchCapacity));
                }
                for (const auto &item : trainer.items[pouchIndex])
                {
                    ++totalItemCount;
                    if (item.itemId == 0)
                    {
                        continue;
                    }
                    const char *itemName = Names::getItemNameFor(context.group, item.itemId);
                    context.check(itemName != nullptr && itemName[0] != '\0' && std::string(itemName) != "???", "an item in the bag has no name in this generation's table", "pouch=" + std::to_string(pouchIndex) + " item=" + std::to_string(item.itemId));
                    // A zero quantity is NORMAL game state, not a defect: key items are stored
                    // with no count at all, and the Gen 7/8 bags keep an entry behind after the
                    // last one is used so the player's sort order survives. Sixteen of these on
                    // two real saves -- Rotom Bike, the Let's Go treasures, a spent Potion -- were
                    // all the games behaving correctly, and they crowded out the warnings that
                    // meant something. Recorded, because PKSE writing a zero WOULD matter, but as
                    // an observation rather than a complaint.
                    if (item.count == 0)
                    {
                        context.note("an item in the bag has a quantity of zero", "pouch=" + std::to_string(pouchIndex) + " item=\"" + (itemName ? itemName : "?") + "\"");
                    }
                }
            }
            context.note("save contents", "boxes=" + std::to_string(trainer.getBoxCount()) + " slotsPerBox=" + std::to_string(trainer.getSlotsPerBox()) + " party=" + std::to_string(trainer.party.size()) + " pouches=" + std::to_string(trainer.items.size()) + " items=" + std::to_string(totalItemCount) + " " + Utils::logField("trainer", trainer.trainerName));
        }

        bool isGen1Image(const std::vector<uint8_t> &saveBytes)
        {
            bool isJapanese = false;
            return Trainer::Trainer1RBY::detect(saveBytes, isJapanese);
        }

        bool isGen2Image(const std::vector<uint8_t> &saveBytes) { return Trainer::Trainer2GSC::detect(saveBytes); }

        /// Where the save image starts inside the file: past an envelope's header, if it has one.
        /// The documented rewrites below are stated in image offsets.
        size_t imageOffsetInFile(const std::vector<uint8_t> &fileBytes, bool (*isImage)(const std::vector<uint8_t> &))
        {
            Save::SaveEnvelope envelope;
            std::vector<uint8_t> saveImage;
            if (!Save::findSaveEnvelope(fileBytes, isImage, envelope, saveImage)) return 0;
            return envelope.headerBytes.size();
        }

        /// Gen 1: every byte that moved is in the current box's bank copy, the boxes-initialised bit,
        /// a first-flush empty header or the checksum, and the bank copy now equals the live one.
        /// The same two proofs tools/harness/gen1.cpp makes. Returns the bytes outside them.
        size_t gen1BytesOutsideFlush(const Trainer::Trainer1RBY &trainer, const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes, size_t imageOffset, bool &bankMatchesLive)
        {
            const Trainer::Gen1BoxFlush boxFlush = trainer.currentBoxFlush();
            const bool bankWasInitialized = (beforeBytes[imageOffset + boxFlush.initializedFlagOffset] & 0x80) != 0;
            size_t outsideByteCount = 0;
            for (size_t fileIndex = 0; fileIndex < beforeBytes.size(); ++fileIndex)
            {
                if (beforeBytes[fileIndex] == afterBytes[fileIndex])
                {
                    continue;
                }
                if (fileIndex < imageOffset)
                {
                    ++outsideByteCount;
                    continue;
                }
                const size_t imageIndex = fileIndex - imageOffset;
                const bool inBankCopy = imageIndex >= boxFlush.bankBoxOffset && imageIndex < boxFlush.bankBoxOffset + boxFlush.boxListLength;
                const bool initializedBit = imageIndex == boxFlush.initializedFlagOffset && (beforeBytes[fileIndex] ^ afterBytes[fileIndex]) == 0x80;
                bool emptyHeaderInitialised = false;
                for (const size_t boxOffset : boxFlush.everyBankBoxOffset)
                {
                    emptyHeaderInitialised = emptyHeaderInitialised || (!bankWasInitialized && ((imageIndex == boxOffset && afterBytes[fileIndex] == 0x00) || (imageIndex == boxOffset + 1 && afterBytes[fileIndex] == 0xFF)));
                }
                if (!(inBankCopy || initializedBit || emptyHeaderInitialised || imageIndex == boxFlush.checksumOffset))
                {
                    ++outsideByteCount;
                }
            }
            const size_t bankCopyOffset = imageOffset + boxFlush.bankBoxOffset;
            const size_t liveCopyOffset = imageOffset + boxFlush.liveBoxOffset;
            bankMatchesLive = bankCopyOffset + boxFlush.boxListLength <= afterBytes.size() && liveCopyOffset + boxFlush.boxListLength <= afterBytes.size() && std::equal(afterBytes.begin() + bankCopyOffset, afterBytes.begin() + bankCopyOffset + boxFlush.boxListLength, afterBytes.begin() + liveCopyOffset);
            return outsideByteCount;
        }

        /// Gen 2: every byte that moved is inside a backup mirror or a checksum word, and every
        /// mirror now equals its source. The same proofs tools/harness/oldgens.cpp makes.
        size_t gen2BytesOutsideMirrors(const Trainer::Trainer2GSC &trainer, const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes, size_t imageOffset, bool &mirrorsMatch)
        {
            const auto mirrors = trainer.mirrorRegions();
            const Trainer::Gen2Offsets &layoutOffsets = Trainer::gen2Offsets(trainer.layout());
            size_t outsideByteCount = 0;
            for (size_t fileIndex = 0; fileIndex < beforeBytes.size(); ++fileIndex)
            {
                if (beforeBytes[fileIndex] == afterBytes[fileIndex])
                {
                    continue;
                }
                if (fileIndex < imageOffset)
                {
                    ++outsideByteCount;
                    continue;
                }
                const size_t imageIndex = fileIndex - imageOffset;
                if (imageIndex == layoutOffsets.overallChecksumPosition || imageIndex == layoutOffsets.overallChecksumPosition + 1u || imageIndex == layoutOffsets.overallChecksumPosition2 || imageIndex == layoutOffsets.overallChecksumPosition2 + 1u)
                {
                    continue;
                }
                bool inMirror = false;
                for (const auto &mirror : mirrors)
                {
                    inMirror = inMirror || (imageIndex >= mirror.destinationOffset && imageIndex < mirror.destinationOffset + mirror.length);
                }
                if (!inMirror)
                {
                    ++outsideByteCount;
                }
            }
            mirrorsMatch = true;
            for (const auto &mirror : mirrors)
            {
                for (size_t byteIndex = 0; byteIndex < mirror.length; ++byteIndex)
                {
                    const size_t destinationIndex = imageOffset + mirror.destinationOffset + byteIndex;
                    const size_t sourceIndex = imageOffset + mirror.sourceOffset + byteIndex;
                    if (destinationIndex >= afterBytes.size() || sourceIndex >= afterBytes.size() || afterBytes[destinationIndex] != afterBytes[sourceIndex])
                    {
                        mirrorsMatch = false;
                    }
                }
            }
            return outsideByteCount;
        }

        /// The SCBlock games' box block and Pokedex blocks: the only blocks an unedited write may change.
        bool scBlockKeys(Enums::GameVersion group, uint32_t &boxBlockKey, std::vector<uint32_t> &pokedexBlockKeys)
        {
            switch (group)
            {
            case Enums::GameVersion::SWSH:
                boxBlockKey = Trainer::BOX8_SWSH;
                pokedexBlockKeys = {Trainer::SAVE_REVISION8_SWSH, Trainer::SAVE_REVISION8_R1_SWSH, Trainer::SAVE_REVISION8_R2_SWSH};
                return true;
            case Enums::GameVersion::PLA:
                boxBlockKey = Trainer::BOX8_LA;
                pokedexBlockKeys = {Trainer::POKEDEX8_LA};
                return true;
            case Enums::GameVersion::SV:
                boxBlockKey = Trainer::BOX9_SV;
                pokedexBlockKeys = {Trainer::ZUKAN9_SV_PALDEA, Trainer::ZUKAN9_SV_KITAKAMI};
                return true;
            case Enums::GameVersion::ZA:
                boxBlockKey = Trainer::BOX9_LZA;
                pokedexBlockKeys = {Trainer::POKEDEX9_LZA};
                return true;
            default:
                return false;
            }
        }

        constexpr Trainer::ResearchField8LA RESEARCH_FIELDS[] = {Trainer::ResearchField8LA::CatchCount, Trainer::ResearchField8LA::AlphaCount, Trainer::ResearchField8LA::LargeCount, Trainer::ResearchField8LA::SmallCount, Trainer::ResearchField8LA::HeavyCount, Trainer::ResearchField8LA::LightCount, Trainer::ResearchField8LA::UpdateCounter};
        const char *const RESEARCH_FIELD_NAMES[] = {"catch", "alpha", "large", "small", "heavy", "light", "updateCounter"};

        /// The trainer's in-memory Legends: Arceus Pokedex block; empty when it has none.
        const std::vector<uint8_t> &pokedexBytes8LA(const Trainer::Trainer &trainer)
        {
            static const std::vector<uint8_t> noPokedexBytes;
            for (const auto &block : trainer.getBlocks())
            {
                if (block.key == Trainer::POKEDEX8_LA)
                {
                    return block.data;
                }
            }
            return noPokedexBytes;
        }

        /// Every species' Legends: Arceus research counters, and the update counter the dex hands out
        /// next, must survive an unedited write: unlike a flag, a counter grows each time it is re-set.
        void checkResearchCountsUnchanged(Context &context, const std::vector<uint8_t> &beforePokedexBytes, const std::vector<uint8_t> &afterPokedexBytes)
        {
            size_t changedFieldCount = 0;
            std::string firstChange;
            for (uint16_t speciesId = 1; speciesId < Pokemon::DEX8LA_MAX_SPECIES; ++speciesId)
            {
                for (size_t fieldIndex = 0; fieldIndex < std::size(RESEARCH_FIELDS); ++fieldIndex)
                {
                    const uint16_t valueBefore = Trainer::Trainer8LA::researchField(beforePokedexBytes, speciesId, RESEARCH_FIELDS[fieldIndex]);
                    const uint16_t valueAfter = Trainer::Trainer8LA::researchField(afterPokedexBytes, speciesId, RESEARCH_FIELDS[fieldIndex]);
                    if (valueBefore == valueAfter)
                    {
                        continue;
                    }
                    if (changedFieldCount == 0)
                    {
                        firstChange = " firstSpeciesId=" + std::to_string(speciesId) + " field=" + RESEARCH_FIELD_NAMES[fieldIndex] + " before=" + std::to_string(valueBefore) + " after=" + std::to_string(valueAfter);
                    }
                    ++changedFieldCount;
                }
            }
            context.check(changedFieldCount == 0, "an unedited write changed Legends: Arceus research", numberField("fields", (long long)changedFieldCount) + firstChange);
            const uint16_t nextCounterBefore = Trainer::Trainer8LA::nextResearchUpdateCounter(beforePokedexBytes);
            const uint16_t nextCounterAfter = Trainer::Trainer8LA::nextResearchUpdateCounter(afterPokedexBytes);
            context.check(nextCounterBefore == nextCounterAfter, "an unedited write moved the Legends: Arceus research update counter", "before=" + std::to_string(nextCounterBefore) + " after=" + std::to_string(nextCounterAfter));
        }

        /// What PKHeX's OnPokeGet adds to each research count for one arriving Pokemon: one catch, one
        /// alpha for an alpha, and one for each size task its species has and its recorded size meets.
        std::vector<uint16_t> arrivalResearchIncrements(const Pokemon::Pokemon8LA &pokemon)
        {
            auto meetsSizeTask = [&](Pokemon::SizeResearchTask8LA task, float sizeValue, bool isUpperBound, double thresholdScale)
            {
                uint8_t taskThreshold = 0;
                if (sizeValue <= 0.0f || !Pokemon::getSizeResearchThreshold8LA(pokemon.speciesID(), task, taskThreshold))
                {
                    return false;
                }
                return isUpperBound ? sizeValue <= taskThreshold / thresholdScale : sizeValue >= taskThreshold / thresholdScale;
            };
            return {1, static_cast<uint16_t>(pokemon.isAlpha()), static_cast<uint16_t>(meetsSizeTask(Pokemon::SizeResearchTask8LA::CatchLarge, pokemon.heightAbsolute(), false, 1.0)), static_cast<uint16_t>(meetsSizeTask(Pokemon::SizeResearchTask8LA::CatchSmall, pokemon.heightAbsolute(), true, 1.0)), static_cast<uint16_t>(meetsSizeTask(Pokemon::SizeResearchTask8LA::CatchHeavy, pokemon.weightAbsolute(), false, 10.0)), static_cast<uint16_t>(meetsSizeTask(Pokemon::SizeResearchTask8LA::CatchLight, pokemon.weightAbsolute(), true, 10.0))};
        }

        /// The six research counts of a species, in RESEARCH_FIELDS order.
        std::vector<uint16_t> researchCounts8LA(const std::vector<uint8_t> &pokedexBytes, uint16_t speciesId)
        {
            std::vector<uint16_t> researchCounts;
            for (size_t fieldIndex = 0; fieldIndex + 1 < std::size(RESEARCH_FIELDS); ++fieldIndex)
            {
                researchCounts.push_back(Trainer::Trainer8LA::researchField(pokedexBytes, speciesId, RESEARCH_FIELDS[fieldIndex]));
            }
            return researchCounts;
        }

        std::string describeResearchCounts(const std::vector<uint16_t> &researchCounts)
        {
            std::string description;
            for (size_t fieldIndex = 0; fieldIndex < researchCounts.size(); ++fieldIndex)
            {
                description += (fieldIndex ? "/" : "") + std::to_string(researchCounts[fieldIndex]);
            }
            return description;
        }

        /// Decrypts both files and checks that only the box block (in empty slots) and the Pokedex
        /// blocks changed. Reports its own findings.
        void checkSCBlockRewrite(Context &context, std::vector<uint8_t> beforeBytes, std::vector<uint8_t> afterBytes, uint32_t boxBlockKey, const std::vector<uint32_t> &pokedexBlockKeys)
        {
            const std::vector<Save::Block> beforeBlocks = Encryption::decrypt(beforeBytes.data(), beforeBytes.size());
            const std::vector<Save::Block> afterBlocks = Encryption::decrypt(afterBytes.data(), afterBytes.size());
            if (!context.check(!beforeBlocks.empty() && beforeBlocks.size() == afterBlocks.size(), "an unedited write changed the save's block list", "before=" + std::to_string(beforeBlocks.size()) + " after=" + std::to_string(afterBlocks.size())))
            {
                return;
            }

            const Trainer::Trainer &trainer = *context.trainer;
            size_t pokedexByteCount = 0;
            size_t emptySlotByteCount = 0;
            for (size_t blockIndex = 0; blockIndex < beforeBlocks.size(); ++blockIndex)
            {
                const Save::Block &beforeBlock = beforeBlocks[blockIndex];
                const Save::Block &afterBlock = afterBlocks[blockIndex];
                const std::string where = hexField("key", beforeBlock.key, 8);
                if (!context.check(beforeBlock.key == afterBlock.key && beforeBlock.type == afterBlock.type && beforeBlock.data.size() == afterBlock.data.size(), "an unedited write changed a block's key, type or size", where))
                {
                    continue;
                }
                if (beforeBlock.data == afterBlock.data)
                {
                    continue;
                }

                size_t changedByteCount = 0;
                for (size_t byteIndex = 0; byteIndex < beforeBlock.data.size(); ++byteIndex)
                {
                    changedByteCount += beforeBlock.data[byteIndex] != afterBlock.data[byteIndex];
                }

                bool isPokedexBlock = false;
                for (uint32_t pokedexBlockKey : pokedexBlockKeys)
                {
                    isPokedexBlock = isPokedexBlock || beforeBlock.key == pokedexBlockKey;
                }
                if (isPokedexBlock)
                {
                    pokedexByteCount += changedByteCount;
                    if (beforeBlock.key == Trainer::POKEDEX8_LA)
                    {
                        checkResearchCountsUnchanged(context, beforeBlock.data, afterBlock.data);
                    }
                    continue;
                }
                if (beforeBlock.key != boxBlockKey)
                {
                    context.fail("an unedited write changed a block it has no reason to touch", where + " changed=" + std::to_string(changedByteCount));
                    continue;
                }

                // The box block: every changed byte must sit in a slot that is empty.
                const size_t storageSlotCount = trainer.getBoxCount() * trainer.getSlotsPerBox();
                const size_t slotRecordSize = storageSlotCount ? beforeBlock.data.size() / storageSlotCount : 0;
                if (!context.check(slotRecordSize > 0, "could not work out the box block's slot size", where + " size=" + std::to_string(beforeBlock.data.size())))
                {
                    continue;
                }
                size_t occupiedSlotByteCount = 0;
                size_t firstOccupiedSlotIndex = 0;
                for (size_t byteIndex = 0; byteIndex < beforeBlock.data.size(); ++byteIndex)
                {
                    if (beforeBlock.data[byteIndex] == afterBlock.data[byteIndex])
                    {
                        continue;
                    }
                    const size_t storageSlotIndex = byteIndex / slotRecordSize;
                    const size_t boxIndex = storageSlotIndex / trainer.getSlotsPerBox();
                    const size_t slotIndex = storageSlotIndex % trainer.getSlotsPerBox();
                    const bool slotIsEmpty = boxIndex >= trainer.boxes.size() || slotIndex >= Trainer::BOX_SLOTS || isEmptySlot(trainer.boxes[boxIndex][slotIndex].get());
                    if (slotIsEmpty)
                    {
                        ++emptySlotByteCount;
                        continue;
                    }
                    if (occupiedSlotByteCount == 0)
                    {
                        firstOccupiedSlotIndex = storageSlotIndex;
                    }
                    ++occupiedSlotByteCount;
                }
                context.check(occupiedSlotByteCount == 0, "an unedited write changed an OCCUPIED box slot", numberField("bytes", (long long)occupiedSlotByteCount) + " " + slot((int)(firstOccupiedSlotIndex / trainer.getSlotsPerBox()), (int)(firstOccupiedSlotIndex % trainer.getSlotsPerBox())));
            }
            if (pokedexByteCount)
            {
                context.note("an unedited write re-registered the Pokedex (documented)", numberField("bytes", (long long)pokedexByteCount));
            }
            if (emptySlotByteCount)
            {
                context.note("an unedited write normalised empty box slots to the canonical blank (documented)", numberField("bytes", (long long)emptySlotByteCount));
            }
        }

        /// Let's Go re-registers everything in storage in the Pokedex on every write, so an unedited
        /// write may change the Pokedex of a save whose flags do not yet show a Pokemon it holds (one
        /// an editor made shiny, say). Every OTHER block must come back byte for byte, which is what
        /// is checked here, block by block, so a writer that strays is named rather than excused.
        void checkLetsGoRewrite(Context &context, const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes)
        {
            if (!context.check(beforeBytes.size() >= Trainer::SAVE_SIZE7_LGPE, "the Let's Go save is smaller than its active area", "size=" + std::to_string(beforeBytes.size())))
            {
                return;
            }
            const std::vector<Save::Block> beforeBlocks = Trainer::createBlocksFromSaveData7LGPE(std::vector<uint8_t>(beforeBytes.begin(), beforeBytes.begin() + Trainer::SAVE_SIZE7_LGPE));
            const std::vector<Save::Block> afterBlocks = Trainer::createBlocksFromSaveData7LGPE(std::vector<uint8_t>(afterBytes.begin(), afterBytes.begin() + Trainer::SAVE_SIZE7_LGPE));
            size_t pokedexByteCount = 0;
            for (const Save::Block &beforeBlock : beforeBlocks)
            {
                for (const Save::Block &afterBlock : afterBlocks)
                {
                    if (afterBlock.key != beforeBlock.key)
                    {
                        continue;
                    }
                    size_t changedByteCount = 0;
                    for (size_t byteIndex = 0; byteIndex < beforeBlock.data.size() && byteIndex < afterBlock.data.size(); ++byteIndex)
                    {
                        changedByteCount += beforeBlock.data[byteIndex] != afterBlock.data[byteIndex] ? 1 : 0;
                    }
                    if (beforeBlock.key == Trainer::ZUKAN7_LGPE)
                    {
                        pokedexByteCount = changedByteCount;
                        continue;
                    }
                    context.check(changedByteCount == 0 && beforeBlock.data.size() == afterBlock.data.size(), "an unedited write changed a Let's Go block other than the Pokedex", hexField("block", beforeBlock.key, 2) + " changed=" + std::to_string(changedByteCount));
                }
            }
            if (pokedexByteCount)
            {
                context.note("an unedited write re-registered the Pokedex (documented)", numberField("bytes", (long long)pokedexByteCount));
            }
        }

        /// The byte-level verdict on an unedited write. See the header for the rules.
        void checkUnmodifiedBytes(Context &context, const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes)
        {
            size_t differingByteCount = 0;
            size_t firstDifferenceOffset = 0;
            for (size_t byteIndex = 0; byteIndex < beforeBytes.size(); ++byteIndex)
            {
                if (beforeBytes[byteIndex] == afterBytes[byteIndex])
                {
                    continue;
                }
                if (differingByteCount == 0)
                {
                    firstDifferenceOffset = byteIndex;
                }
                ++differingByteCount;
            }
            const std::string differenceSummary = "count=" + std::to_string(differingByteCount) + " of " + std::to_string(beforeBytes.size()) + (differingByteCount ? "  " + hexField("firstOffset", firstDifferenceOffset, 6) : "");
            const Enums::GameVersion group = context.group;

            uint32_t boxBlockKey = 0;
            std::vector<uint32_t> pokedexBlockKeys;
            if (scBlockKeys(group, boxBlockKey, pokedexBlockKeys))
            {
                if (differingByteCount)
                {
                    checkSCBlockRewrite(context, beforeBytes, afterBytes, boxBlockKey, pokedexBlockKeys);
                }
                else
                {
                    context.check(differingByteCount == 0, "an unedited write did not reproduce the file byte for byte", differenceSummary);
                }
                return;
            }
            if (group == Enums::GameVersion::RBY)
            {
                bool bankMatchesLive = true;
                const size_t outsideByteCount = gen1BytesOutsideFlush(static_cast<const Trainer::Trainer1RBY &>(*context.trainer), beforeBytes, afterBytes, imageOffsetInFile(beforeBytes, isGen1Image), bankMatchesLive);
                context.check(outsideByteCount == 0, "an unedited write changed bytes outside the current box's flush", differenceSummary + " outside=" + std::to_string(outsideByteCount));
                context.check(bankMatchesLive, "after writing, the current box's bank copy does not equal its live copy");
                return;
            }
            if (group == Enums::GameVersion::GSC)
            {
                bool mirrorsMatch = true;
                const size_t outsideByteCount = gen2BytesOutsideMirrors(static_cast<const Trainer::Trainer2GSC &>(*context.trainer), beforeBytes, afterBytes, imageOffsetInFile(beforeBytes, isGen2Image), mirrorsMatch);
                context.check(outsideByteCount == 0, "an unedited write changed bytes outside the trainer-block backup", differenceSummary + " outside=" + std::to_string(outsideByteCount));
                context.check(mirrorsMatch, "after writing, the trainer-block backup does not equal its source");
                return;
            }
            if (group == Enums::GameVersion::GG)
            {
                checkLetsGoRewrite(context, beforeBytes, afterBytes);
                return;
            }
            if (group == Enums::GameVersion::BDSP)
            {
                context.warn(differingByteCount == 0, "an unedited write changed the save (this format re-registers the Pokedex on write, which this suite cannot tell apart from anything else)", differenceSummary);
                return;
            }
            context.check(differingByteCount == 0, "an unedited write did not reproduce the file byte for byte", differenceSummary);
        }

        void runRoundTrip(Context &context)
        {
            const std::string saveFilePath = context.savePath.empty() ? primarySaveFile(context.workDirectory, context.group) : context.savePath;
            if (!context.check(!saveFilePath.empty(), "no save file found in the working directory", "dir=\"" + context.workDirectory + "\""))
            {
                return;
            }

            const std::vector<uint8_t> beforeBytes = readFile(saveFilePath);
            if (!context.check(!beforeBytes.empty(), "the working copy of the save is empty or unreadable", "path=\"" + saveFilePath + "\""))
            {
                return;
            }

            if (!context.check(writeSave(context), "writing the save back failed", "path=\"" + saveFilePath + "\""))
            {
                return;
            }

            const std::vector<uint8_t> afterBytes = readFile(saveFilePath);
            if (!context.check(afterBytes.size() == beforeBytes.size(), "re-writing the save changed its SIZE", "before=" + std::to_string(beforeBytes.size()) + " after=" + std::to_string(afterBytes.size())))
            {
                return;
            }

            checkUnmodifiedBytes(context, beforeBytes, afterBytes);

            // Whatever the first write registered in the Pokedex, a second must find nothing left to do.
            if (context.group == Enums::GameVersion::GG && afterBytes != beforeBytes)
            {
                if (context.check(writeSave(context), "writing the save back a second time failed", "path=\"" + saveFilePath + "\""))
                {
                    const std::vector<uint8_t> secondBytes = readFile(saveFilePath);
                    context.check(secondBytes == afterBytes, "a second unedited write changed the save again, so the Pokedex registration is not settled by one write");
                }
            }

            Reloaded reloaded;
            if (!context.check(reloadSave(context, reloaded), "the save PKSE just wrote could not be read back", "path=\"" + saveFilePath + "\""))
            {
                return;
            }
            compareTrainers(context, *context.trainer, *reloaded.trainer, "unmodified-roundtrip");
        }

        void runEditPersist(Context &context)
        {
            Trainer::Trainer &trainer = *context.trainer;

            const uint32_t newMoney = trainer.getMaxMoney() >= 123456u ? 123456u : (trainer.getMaxMoney() / 2);
            trainer.money = newMoney;

            const std::string originalTrainerName = trainer.trainerName;
            trainer.trainerName = PROBE_TRAINER_NAME;

            bool didBoxName = false;
            if (trainer.supportsBoxNames() && !trainer.boxNames.empty() && trainer.canStoreBoxName(PROBE_BOX_NAME))
            {
                trainer.boxNames[0] = PROBE_BOX_NAME;
                trainer.markBoxNameDirty(0);
                didBoxName = true;
            }

            // A box OTHER than the open one, so the probe cannot pass by already being the answer.
            const bool canMoveCurrentBox = trainer.getBoxCount() > 1;
            const uint8_t newCurrentBox = (uint8_t)(canMoveCurrentBox ? (trainer.getCurrentBox() + 1) % trainer.getBoxCount() : trainer.getCurrentBox());
            trainer.setCurrentBox(newCurrentBox);

            bool didItem = false;
            size_t itemPouchIndex = 0;
            size_t itemIndex = 0;
            for (size_t pouchIndex = 0; pouchIndex < trainer.items.size() && !didItem; ++pouchIndex)
            {
                for (size_t candidateItemIndex = 0; candidateItemIndex < trainer.items[pouchIndex].size(); ++candidateItemIndex)
                {
                    if (trainer.items[pouchIndex][candidateItemIndex].itemId == 0)
                    {
                        continue;
                    }
                    trainer.items[pouchIndex][candidateItemIndex].count = PROBE_ITEM_COUNT;
                    itemPouchIndex = pouchIndex;
                    itemIndex = candidateItemIndex;
                    didItem = true;
                    break;
                }
            }
            if (!didItem)
            {
                context.skip("no item in the bag to edit, so item persistence was not tested");
            }

            // Find a Pokemon to edit. Boxes first, so an edit lands on the storage write path rather
            // than the party one -- the party is mirrored in more places and is the easier case.
            // OCCUPIED, not merely non-null (isEmptySlot): editing a canonical blank tests nothing,
            // because the box writer stamps a fresh blank over it and every "edit" reads back as 0.
            // The slot is remembered so the pokemon can be looked up in the SAME place afterwards --
            // "the first pokemon I can find" would silently compare two different Pokemon.
            Pokemon::Pokemon *victim = nullptr;
            std::string victimWhere;
            bool victimInParty = false;
            size_t victimBoxIndex = 0;
            size_t victimSlotIndex = 0;
            for (size_t boxIndex = 0; boxIndex < trainer.boxes.size() && !victim; ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (!isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                    {
                        victim = trainer.boxes[boxIndex][slotIndex].get();
                        victimBoxIndex = boxIndex;
                        victimSlotIndex = slotIndex;
                        victimWhere = slot((int)boxIndex, (int)slotIndex);
                        break;
                    }
                }
            }
            for (size_t partySlotIndex = 0; partySlotIndex < trainer.party.size() && !victim; ++partySlotIndex)
            {
                if (!isEmptySlot(trainer.party[partySlotIndex].get()))
                {
                    victim = trainer.party[partySlotIndex].get();
                    victimInParty = true;
                    victimSlotIndex = partySlotIndex;
                    victimWhere = partySlot((int)partySlotIndex);
                }
            }

            uint16_t victimSpeciesId = 0;
            uint16_t probeEV = 0;
            bool didNickname = false;
            bool didEV = false;
            if (victim)
            {
                victimSpeciesId = victim->speciesID();
                victim->setIV(1, PROBE_IV); // Attack IV

                // EVs are not universal: Let's Go trains AVs and its EV setters are deliberate
                // no-ops. Classify the same way the setters suite does -- write, read back, and
                // only carry the persistence check forward if the format took the value at all.
                // Asserting otherwise reports a field the generation does not have as a save bug.
                probeEV = static_cast<uint16_t>(victim->maxEV() > 4 ? 4 : victim->maxEV());
                victim->setEVWide(2, probeEV); // Defence
                didEV = (victim->evWide(2) == probeEV);
                if (!didEV)
                {
                    context.note("this format does not store EVs, so EV persistence was not tested", "title=\"" + Enums::getGameVersionName(context.version) + "\"");
                }

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
                {
                    trainer.mirrorPartyMemberFromParty(victimSlotIndex);
                }
                else
                {
                    trainer.mirrorPartyMemberFromBox(victimBoxIndex, victimSlotIndex);
                }
            }
            else
            {
                context.skip("no Pokemon in the save to edit, so entity persistence was not tested");
            }

            // AN EMPTY SLOT GETS A POKEMON: the path a bank withdrawal and the creator both take, and the
            // one where a writer that only knew how to rewrite occupied slots would lose it. A clone of
            // one this save already holds, so it is a record this format certainly accepts.
            const Pokemon::Pokemon *placeSource = nullptr;
            size_t placedBoxIndex = 0;
            size_t placedSlotIndex = 0;
            bool foundEmptySlot = false;
            for (const auto &partyPokemon : trainer.party)
            {
                if (!placeSource && !isEmptySlot(partyPokemon.get()))
                {
                    placeSource = partyPokemon.get();
                }
            }
            for (size_t boxIndex = 0; boxIndex < trainer.boxes.size() && !(foundEmptySlot && placeSource); ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    if (!placeSource && !isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                    {
                        placeSource = trainer.boxes[boxIndex][slotIndex].get();
                    }
                    if (!foundEmptySlot && isEmptySlot(trainer.boxes[boxIndex][slotIndex].get()))
                    {
                        placedBoxIndex = boxIndex;
                        placedSlotIndex = slotIndex;
                        foundEmptySlot = true;
                    }
                }
            }
            std::unique_ptr<Pokemon::Pokemon> placedExpected;
            if (foundEmptySlot && placeSource)
            {
                auto placedPokemon = placeSource->clone();
                if (context.check(placedPokemon != nullptr, "clone() returned nothing for this format"))
                {
                    placedExpected = placedPokemon->clone();
                    trainer.boxes[placedBoxIndex][placedSlotIndex] = std::move(placedPokemon);
                }
            }
            else
            {
                context.skip(foundEmptySlot ? "no Pokemon in the save to copy, so writing into an empty slot was not tested" : "no empty box slot, so writing into one was not tested");
            }
            // Legends: Arceus counts a Pokemon that arrives toward its species' research, once. A
            // noble form registers nothing at all.
            const bool checksCatchResearch = placedExpected && context.group == Enums::GameVersion::PLA && !Pokemon::isLordForm(placedExpected->speciesID(), placedExpected->form());
            const std::vector<uint16_t> placedResearchCountsBefore = checksCatchResearch ? researchCounts8LA(pokedexBytes8LA(trainer), placedExpected->speciesID()) : std::vector<uint16_t>();
            const uint16_t placedUpdateCounterBefore = checksCatchResearch ? Trainer::Trainer8LA::researchField(pokedexBytes8LA(trainer), placedExpected->speciesID(), Trainer::ResearchField8LA::UpdateCounter) : 0;
            const uint16_t nextUpdateCounterBefore = checksCatchResearch ? Trainer::Trainer8LA::nextResearchUpdateCounter(pokedexBytes8LA(trainer)) : 0;

            // Every box slot as it stands now, so the reload can show nothing else moved.
            std::vector<uint16_t> boxSpeciesBefore;
            for (const auto &box : trainer.boxes)
            {
                for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                {
                    boxSpeciesBefore.push_back(isEmptySlot(box[slotIndex].get()) ? 0 : box[slotIndex]->speciesID());
                }
            }

            // A NEW ITEM, the way the editor adds one in a bag that keeps a record per item id
            // (Brilliant Diamond / Shining Pearl, Scarlet / Violet, Legends: Z-A): a legal id for the
            // pouch the bag does not hold is revived from count 0, or appended. Slot-based bags need
            // the editor's pouch capacity to append, so the ui suite's add-item step covers those.
            bool didNewItem = false;
            size_t newItemPouchIndex = 0;
            uint16_t newItemId = 0;
            if (trainer.itemsAreIdIndexed())
            {
                for (size_t pouchIndex = 0; pouchIndex < trainer.items.size() && !didNewItem; ++pouchIndex)
                {
                    auto &pouch = trainer.items[pouchIndex];
                    for (uint16_t legalItemId : Names::getPouchItems(context.group, pouchIndex))
                    {
                        auto existingItem = std::find_if(pouch.begin(), pouch.end(), [legalItemId](const Trainer::InventoryItem &item) { return item.itemId == legalItemId; });
                        if (existingItem != pouch.end() && existingItem->count != 0)
                        {
                            continue;
                        }
                        if (existingItem != pouch.end())
                        {
                            existingItem->count = 1;
                            existingItem->isNew = true;
                        }
                        else
                        {
                            pouch.push_back(Trainer::InventoryItem{legalItemId, 1, true, false});
                        }
                        newItemPouchIndex = pouchIndex;
                        newItemId = legalItemId;
                        didNewItem = true;
                        break;
                    }
                }
                if (!didNewItem)
                {
                    context.skip("every legal item is already in the bag, so adding a new one was not tested");
                }
            }
            else
            {
                context.note("this bag is slot-based; adding a new item is covered by the ui suite's add-item step");
            }

            if (!context.check(writeSave(context), "writing the edited save failed"))
            {
                return;
            }

            Reloaded reloaded;
            if (!context.check(reloadSave(context, reloaded), "the edited save could not be read back"))
            {
                return;
            }
            const Trainer::Trainer &reloadedTrainer = *reloaded.trainer;

            context.check(reloadedTrainer.money == newMoney, "an edited money value did not persist", "wrote=" + std::to_string(newMoney) + " read=" + std::to_string(reloadedTrainer.money));
            context.check(reloadedTrainer.trainerName == PROBE_TRAINER_NAME, "an edited trainer name did not persist", "wrote=\"" + PROBE_TRAINER_NAME + "\" read=\"" + reloadedTrainer.trainerName + "\"");
            if (didBoxName)
            {
                context.check(!reloadedTrainer.boxNames.empty() && reloadedTrainer.boxNames[0] == PROBE_BOX_NAME, "an edited box name did not persist", "wrote=\"" + PROBE_BOX_NAME + "\" read=\"" + (reloadedTrainer.boxNames.empty() ? std::string("<none>") : reloadedTrainer.boxNames[0]) + "\"");
            }
            // Every format stores which box was open except Let's Go, whose storage is one list with
            // no current box (Trainer7LGPE has no updateCurrentBoxBlock).
            if (!canMoveCurrentBox)
            {
                context.skip("the save has one box, so the current box could not be changed");
            }
            else if (context.group == Enums::GameVersion::GG)
            {
                context.note("Let's Go has no current box to persist");
            }
            else
            {
                context.check(reloadedTrainer.getCurrentBox() == newCurrentBox, "an edited current box did not persist", "wrote=" + std::to_string(newCurrentBox) + " read=" + std::to_string(reloadedTrainer.getCurrentBox()));
            }

            if (didItem)
            {
                const bool itemSlotInRange = itemPouchIndex < reloadedTrainer.items.size() && itemIndex < reloadedTrainer.items[itemPouchIndex].size();
                if (context.check(itemSlotInRange, "the edited item's slot no longer exists after the save", "pouch=" + std::to_string(itemPouchIndex) + " index=" + std::to_string(itemIndex)))
                {
                    context.check(reloadedTrainer.items[itemPouchIndex][itemIndex].count == PROBE_ITEM_COUNT, "an edited item quantity did not persist", "pouch=" + std::to_string(itemPouchIndex) + " index=" + std::to_string(itemIndex) + " wrote=" + std::to_string(PROBE_ITEM_COUNT) + " read=" + std::to_string(reloadedTrainer.items[itemPouchIndex][itemIndex].count));
                }
            }

            if (victim)
            {
                // The SAME slot, not the first occupied one -- a pokemon that moved is a finding of its
                // own, and re-finding it elsewhere would hide that behind a passing field check.
                const Pokemon::Pokemon *reloadedVictim = nullptr;
                if (victimInParty)
                {
                    if (victimSlotIndex < reloadedTrainer.party.size())
                    {
                        reloadedVictim = reloadedTrainer.party[victimSlotIndex].get();
                    }
                }
                else if (victimBoxIndex < reloadedTrainer.boxes.size() && victimSlotIndex < Trainer::BOX_SLOTS)
                {
                    reloadedVictim = reloadedTrainer.boxes[victimBoxIndex][victimSlotIndex].get();
                }
                if (isEmptySlot(reloadedVictim))
                {
                    reloadedVictim = nullptr;
                }

                if (context.check(reloadedVictim != nullptr, "the edited Pokemon is gone after the save", victimWhere))
                {
                    context.check(reloadedVictim->speciesID() == victimSpeciesId, "the edited Pokemon changed species across the save", victimWhere + " before=" + std::to_string(victimSpeciesId) + " after=" + std::to_string(reloadedVictim->speciesID()));
                    context.check(reloadedVictim->ivATK() == PROBE_IV, "an edited IV did not persist", victimWhere + " wrote=" + std::to_string(PROBE_IV) + " read=" + std::to_string(reloadedVictim->ivATK()));
                    if (didEV)
                    {
                        context.check(reloadedVictim->evWide(2) == probeEV, "an edited EV did not persist", victimWhere + " wrote=" + std::to_string(probeEV) + " read=" + std::to_string(reloadedVictim->evWide(2)));
                    }
                    if (didNickname)
                    {
                        context.check(reloadedVictim->nickname() == PROBE_NICKNAME, "an edited nickname did not persist", victimWhere + " wrote=\"" + Utils::utf16ToUtf8(PROBE_NICKNAME) + "\" read=\"" + Utils::utf16ToUtf8(reloadedVictim->nickname()) + "\"");
                    }
                    context.check(reloadedVictim->isStructurallyValid(), "the edited Pokemon is not structurally valid after the save", victimWhere);
                }
            }

            if (placedExpected)
            {
                const std::string placedWhere = slot((int)placedBoxIndex, (int)placedSlotIndex) + " " + pokemonLabel(*placedExpected);
                const Pokemon::Pokemon *reloadedPlaced = placedBoxIndex < reloadedTrainer.boxes.size() && placedSlotIndex < Trainer::BOX_SLOTS ? reloadedTrainer.boxes[placedBoxIndex][placedSlotIndex].get() : nullptr;
                if (context.check(!isEmptySlot(reloadedPlaced), "a Pokemon written into an empty slot is gone after the save", placedWhere))
                {
                    const bool samePokemon = reloadedPlaced->speciesID() == placedExpected->speciesID() && reloadedPlaced->form() == placedExpected->form() && reloadedPlaced->pid() == placedExpected->pid() && reloadedPlaced->id32() == placedExpected->id32() && reloadedPlaced->nickname() == placedExpected->nickname() && reloadedPlaced->exp() == placedExpected->exp() && reloadedPlaced->move(0) == placedExpected->move(0);
                    context.check(samePokemon, "a Pokemon written into an empty slot came back as a different Pokemon", placedWhere);
                    context.check(reloadedPlaced->isStructurallyValid(), "a Pokemon written into an empty slot is not structurally valid after the save", placedWhere);
                }
            }
            {
                // Nothing else in the boxes moved: every slot holds the species it did before the write.
                size_t storageSlotIndex = 0;
                size_t movedSlotCount = 0;
                size_t firstMovedSlotIndex = 0;
                for (const auto &box : reloadedTrainer.boxes)
                {
                    for (size_t slotIndex = 0; slotIndex < reloadedTrainer.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex, ++storageSlotIndex)
                    {
                        const uint16_t speciesIdAfter = isEmptySlot(box[slotIndex].get()) ? 0 : box[slotIndex]->speciesID();
                        if (storageSlotIndex < boxSpeciesBefore.size() && speciesIdAfter != boxSpeciesBefore[storageSlotIndex])
                        {
                            if (!movedSlotCount)
                            {
                                firstMovedSlotIndex = storageSlotIndex;
                            }
                            ++movedSlotCount;
                        }
                    }
                }
                context.check(movedSlotCount == 0 && storageSlotIndex == boxSpeciesBefore.size(), "after the save the boxes hold different Pokemon than were written", numberField("slots", (long long)movedSlotCount) + " firstSlotIndex=" + std::to_string(firstMovedSlotIndex));
            }

            if (didNewItem)
            {
                bool foundNewItem = false;
                uint16_t newItemCount = 0;
                if (newItemPouchIndex < reloadedTrainer.items.size())
                {
                    for (const auto &item : reloadedTrainer.items[newItemPouchIndex])
                    {
                        if (item.itemId == newItemId && item.count != 0)
                        {
                            foundNewItem = true;
                            newItemCount = item.count;
                        }
                    }
                }
                context.check(foundNewItem && newItemCount == 1, "a newly added item did not persist", "pouch=" + std::to_string(newItemPouchIndex) + " item=" + std::to_string(newItemId) + " count=" + std::to_string(newItemCount));
            }

            if (checksCatchResearch)
            {
                const uint16_t placedSpeciesId = placedExpected->speciesID();
                const std::vector<uint16_t> increments = arrivalResearchIncrements(static_cast<const Pokemon::Pokemon8LA &>(*placedExpected));
                std::vector<uint16_t> expectedResearchCounts = placedResearchCountsBefore;
                for (size_t fieldIndex = 0; fieldIndex < expectedResearchCounts.size() && fieldIndex < increments.size(); ++fieldIndex)
                {
                    expectedResearchCounts[fieldIndex] = static_cast<uint16_t>(expectedResearchCounts[fieldIndex] + increments[fieldIndex]);
                }
                const std::vector<uint16_t> researchCountsAfterWrite = researchCounts8LA(pokedexBytes8LA(reloadedTrainer), placedSpeciesId);
                const std::string researchWhere = "speciesId=" + std::to_string(placedSpeciesId) + " catch/alpha/large/small/heavy/light before=" + describeResearchCounts(placedResearchCountsBefore) + " expected=" + describeResearchCounts(expectedResearchCounts) + " after=" + describeResearchCounts(researchCountsAfterWrite);
                context.check(researchCountsAfterWrite == expectedResearchCounts, "a Pokemon written into an empty slot did not count once toward Legends: Arceus research", researchWhere);
                // A species already stamped keeps its update counter. An unstamped one takes the next
                // counter -- and when that is 0, PKHeX's six task updates stamp it twice, so it ends on 1.
                uint16_t expectedUpdateCounter = placedUpdateCounterBefore;
                uint16_t expectedNextUpdateCounter = nextUpdateCounterBefore;
                if (placedUpdateCounterBefore == 0)
                {
                    expectedUpdateCounter = nextUpdateCounterBefore == 0 ? 1 : nextUpdateCounterBefore;
                    expectedNextUpdateCounter = static_cast<uint16_t>(expectedUpdateCounter + 1);
                }
                const uint16_t updateCounterAfter = Trainer::Trainer8LA::researchField(pokedexBytes8LA(reloadedTrainer), placedSpeciesId, Trainer::ResearchField8LA::UpdateCounter);
                const uint16_t nextUpdateCounterAfter = Trainer::Trainer8LA::nextResearchUpdateCounter(pokedexBytes8LA(reloadedTrainer));
                context.check(updateCounterAfter == expectedUpdateCounter && nextUpdateCounterAfter == expectedNextUpdateCounter, "a Pokemon written into an empty slot set the wrong Legends: Arceus research update counter", "speciesId=" + std::to_string(placedSpeciesId) + " species before=" + std::to_string(placedUpdateCounterBefore) + " after=" + std::to_string(updateCounterAfter) + " next before=" + std::to_string(nextUpdateCounterBefore) + " after=" + std::to_string(nextUpdateCounterAfter));

                // The same session saving again, with nothing new, must not count it a second time.
                Reloaded rewritten;
                if (context.check(writeSave(context), "writing the edited save a second time failed") && context.check(reloadSave(context, rewritten), "the save written a second time could not be read back"))
                {
                    checkResearchCountsUnchanged(context, pokedexBytes8LA(reloadedTrainer), pokedexBytes8LA(*rewritten.trainer));
                }
            }
        }

        bool writeFile(const std::string &path, const std::vector<uint8_t> &fileBytes)
        {
            FILE *fileHandle = fopen(path.c_str(), "wb");
            if (!fileHandle) return false;
            const size_t writtenByteCount = fwrite(fileBytes.data(), 1, fileBytes.size(), fileHandle);
            fclose(fileHandle);
            return writtenByteCount == fileBytes.size();
        }

        void runEnvelopes(Context &context)
        {
            // FireRed/LeafGreen open both ways, so an installed copy's save is a GBA file like any other.
            // Every other installed format has no file form to wrap.
            std::string sourcePath = context.savePath;
            if (sourcePath.empty() && context.titleId != 0 && Save::groupOpensFromFile(context.group))
            {
                sourcePath = primarySaveFile(context.workDirectory, context.group);
            }
            if (sourcePath.empty())
            {
                context.note("this format has no file form, so there is no envelope to test");
                return;
            }

            Save::SaveEnvelope sourceEnvelope;
            std::vector<uint8_t> saveImage;
            if (!context.check(Save::findExternalSaveEnvelope(readFile(sourcePath), sourceEnvelope, saveImage), "the save is not a file the loose-save probe chain opens", "path=\"" + sourcePath + "\""))
            {
                return;
            }

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
                {
                    envelopeCases.push_back({kind, alternateLayout, saveImage, std::string("envelope=\"") + Save::saveEnvelopeName(kind) + "\" alternateLayout=" + (alternateLayout ? "yes" : "no")});
                }
            }
            for (const PaddingFixture &fixture : paddingForSaveSize(saveImage.size()))
            {
                envelopeCases.push_back({fixture.kind, false, paddedSaveBytes(fixture, saveImage), std::string("envelope=\"") + Save::saveEnvelopeName(fixture.kind) + "\" paddedTo=" + std::to_string(fixture.paddedSize)});
            }

            if (envelopeCases.empty())
            {
                context.skip("no envelope is defined for a save of this size, so none was tested", "size=" + std::to_string(saveImage.size()));
                return;
            }

            const std::string envelopedPath = context.workDirectory + "/enveloped-save";
            for (const EnvelopeCase &envelopeCase : envelopeCases)
            {
                const std::string &where = envelopeCase.where;
                const std::vector<uint8_t> envelopedFile = buildEnvelopedSave(envelopeCase.kind, envelopeCase.heldBytes, envelopeCase.alternateLayout);
                if (!context.check(writeFile(envelopedPath, envelopedFile), "could not write the enveloped save", where))
                {
                    continue;
                }

                // The fixture is built from the layouts as their writers describe them, not by the code
                // under test, so taking it apart must give back exactly the save that went in.
                Save::SaveEnvelope builtEnvelope;
                std::vector<uint8_t> builtImage;
                if (!context.check(Save::findExternalSaveEnvelope(envelopedFile, builtEnvelope, builtImage) && builtEnvelope.kind == envelopeCase.kind && builtImage == saveImage, "the save could not be taken back out of its envelope intact", where))
                {
                    continue;
                }

                std::unique_ptr<Trainer::Trainer> openedTrainer = Save::openExternalSave(envelopedFile, envelopedPath, nullptr);
                if (context.check(openedTrainer && openedTrainer->getGameGroup() == context.group, "a save inside an envelope does not open as itself", where) && context.check(Save::saveExternalSave(*openedTrainer, envelopedPath.c_str(), false), "writing a save inside an envelope back failed", where))
                {
                    const std::vector<uint8_t> writtenFile = readFile(envelopedPath);
                    Save::SaveEnvelope writtenEnvelope;
                    std::vector<uint8_t> writtenImage;
                    const bool foundAgain = Save::findExternalSaveEnvelope(writtenFile, writtenEnvelope, writtenImage);
                    const size_t paddingLength = envelopeCase.heldBytes.size() - saveImage.size();
                    if (context.check(foundAgain && writtenEnvelope.kind == envelopeCase.kind && writtenEnvelope.paddingBytes.size() == paddingLength, "writing the save back lost its envelope or its padding", where))
                    {
                        // The envelope that went in, around the save that came out: anything else moved.
                        // (A run-length coded no$gba file is coded again, so it is the envelope that has to
                        // match there, not the bytes the fixture's own encoder chose.)
                        context.check(Save::joinSaveEnvelope(builtEnvelope, writtenImage) == writtenFile, "writing the save back changed its envelope", where);
                        // The same, field by field against the fixture's own parse: the padding never
                        // changes, and the header only for Switch Online, whose SHA-1 follows the save.
                        context.check(writtenEnvelope.paddingBytes == builtEnvelope.paddingBytes, "writing the save back changed the padding after it", where);
                        if (envelopeCase.kind != Save::SaveEnvelopeKind::SwitchOnlineHeader)
                        {
                            context.check(writtenEnvelope.headerBytes == builtEnvelope.headerBytes, "writing the save back changed the header before it", where);
                        }
                    }
                }
                remove(envelopedPath.c_str());
            }
        }
    }

    int savePlan(Context &context) { return context.trainer ? ChunkCount : 0; }

    void saveRun(Context &context, int chunk)
    {
        if (!context.trainer) return;
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
