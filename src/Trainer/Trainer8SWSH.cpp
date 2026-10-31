#include <algorithm>
#include <cstring>
#include <string>

#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Inventory8SWSH.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/DexTable8SWSH.h" // getDexEntry8SWSH -- which of the three dexes a species lives in
#include "Utils/Logger.h"

using namespace Utils;
using namespace Pokemon;

namespace Trainer
{

    void Trainer8SWSH::parseBlock(const Block &block)
    {
        switch (block.key)
        {
        case MY_STATUS8_SWSH:
            parseMyStatusBlock(block);
            break;
        case PARTY8_SWSH:
            parsePartyBlock(block);
            break;
        case MONEY8_SWSH:
            parseMoneyBlock(block);
            break;
        case TRAINER_CARD8_SWSH:
            parseTrainerCardBlock(block);
            break;
        case ITEM8_SWSH:
            parseItemBlock(block);
            break;
        case BOX8_SWSH:
            parseBoxBlock(block);
            break;
        case BOX_LAYOUT8_SWSH:
            parseBoxLayoutBlock(block);
            break;
        case CURRENT_BOX8_SWSH:
            parseCurrentBoxBlock(block);
            break;
        default:
            break;
        }
    }

    void Trainer8SWSH::parseMyStatusBlock(const Block &block)
    {
        if (block.data.size() < 0xA0 + 4)
        {
            logInfoToFile("Insufficient data for UInt32 at offset 0xA0 in MY_STATUS block");
            return;
        }

        // The save names its own game at 0xA4 (PKHeX MyStatus8.Game). Accepted only if it is one
        // of this group's two titles -- anything else is a misparse, not a discovery.
        if (block.data.size() > 0xA4)
        {
            const GameVersion storedVersion = static_cast<GameVersion>(block.data[0xA4]);
            if (storedVersion == GameVersion::SW || storedVersion == GameVersion::SH)
                this->gameVersion = storedVersion;
        }

        this->ID32 = readUInt32LittleEndian(&block.data[0xA0]);
        this->TID16 = readUInt16LittleEndian(&block.data[0xA0]);
        this->SID16 = readUInt16LittleEndian(&block.data[0xA2]);
        this->TID = this->ID32 % 1000000;
        this->SID = this->ID32 / 1000000;

        // OT name (0xB0, 26 bytes) and gender (0xA5) come from MyStatus8 -- the authoritative source PKHeX
        // uses (SAV8SWSH.OT/Gender => MyStatus, alongside ID32 at 0xA0). The Trainer Card block is a
        // display copy whose byte at 0xA5 is NOT the gender field.
        if (block.data.size() >= 0xB0 + 0x1A)
            this->trainerName = utf16ToUtf8(getString(&block.data[0xB0], 0x1A));
        // 0xA5: gender (0=M, 1=F)
        if (block.data.size() > 0xA5) this->trainerGender = block.data[0xA5] & 1;
    }

    void Trainer8SWSH::parsePartyBlock(const Block &block)
    {
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t slot = 0; slot < MAX_PARTY_SLOTS; ++slot)
        {
            const size_t offset = slot * SIZE_PARTY8_SWSH;
            if (offset + SIZE_PARTY8_SWSH > block.data.size())
                break;

            std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY8_SWSH);

            bool isEmptySlot = true;
            for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY8_SWSH && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
            {
                if (slotSpan[slotSpanIndex] != std::byte{0})
                {
                    isEmptySlot = false;
                    break;
                }
            }

            if (!isEmptySlot)
            {
                // Decrypt and create Pokemon8SWSH object as unique_ptr
                // Pokemon8SWSH constructor handles decryption automatically
                party.push_back(std::make_unique<Pokemon8SWSH>(slotSpan));
            }
        }
    }

    void Trainer8SWSH::parseMoneyBlock(const Block &block)
    {
        if (block.data.size() < 0x04 + 4)
        {
            return;
        }

        this->money = readUInt32LittleEndian(&block.data[0x04]);
    }

    void Trainer8SWSH::parseTrainerCardBlock(const Block &block)
    {
        // OT name and gender are read from MyStatus8 (the authoritative block), not from this display
        // copy -- see parseMyStatusBlock. Nothing else in the Trainer Card is consumed yet.
        (void)block;
    }

    void Trainer8SWSH::parseItemBlock(const Block &block)
    {
        items.resize(static_cast<size_t>(PouchType8SWSH::Count));
        itemSlot.assign(static_cast<size_t>(PouchType8SWSH::Count), {});

        for (int pIndex = 0; pIndex < static_cast<int>(PouchType8SWSH::Count); pIndex++)
        {
            PouchType8SWSH pouchType = static_cast<PouchType8SWSH>(pIndex);
            const PouchInfo8SWSH &info = getPouchInfo8SWSH(pouchType);

            std::vector<InventoryItem> pouch;
            pouch.reserve(info.maxCount);

            // EVERY slot, not up to the first empty one. The game parks entries at the END of a
            // pouch after a run of empty slots -- a real Sword save keeps used-up items in the last
            // slots of five pouches, and a real Shield save keeps a Miracle Seed there -- so each
            // item's slot is recorded here and updateItemBlock puts it back there.
            for (int itemSlotIndex = 0; itemSlotIndex < info.maxCount; itemSlotIndex++)
            {
                size_t offset = info.offset + (itemSlotIndex * 4);
                if (offset + 4 <= block.data.size())
                {
                    uint32_t itemValue = readUInt32LittleEndian(&block.data[offset]);
                    InventoryItem8SWSH item = InventoryItem8SWSH::fromValue(itemValue);

                    // Only add items with valid IDs (non-zero)
                    if (item.itemId != 0)
                    {
                        pouch.push_back(item);
                        itemSlot[pIndex].push_back(ItemOrigin{static_cast<uint16_t>(itemSlotIndex), item.itemId});
                    }
                }
            }

            items[pIndex] = std::move(pouch);
        }
    }

    void Trainer8SWSH::parseBoxBlock(const Block &block)
    {
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_SWSH; ++boxIndex)
        {
            for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
            {
                const size_t offset = (boxIndex * BOX_SLOTS + slot) * SIZE_PARTY8_SWSH;
                if (offset + SIZE_PARTY8_SWSH > block.data.size())
                {
                    break;
                }

                std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY8_SWSH);

                bool isEmptySlot = true;
                for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY8_SWSH && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
                {
                    if (slotSpan[slotSpanIndex] != std::byte{0})
                    {
                        isEmptySlot = false;
                        break;
                    }
                }

                if (!isEmptySlot)
                {
                    boxes[boxIndex][slot] = std::make_unique<Pokemon8SWSH>(slotSpan);
                }
                else
                {
                    boxes[boxIndex][slot] = nullptr;
                }
            }
        }
    }

    void Trainer8SWSH::parseBoxLayoutBlock(const Block &block)
    {
        for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_SWSH; ++boxIndex)
        {
            size_t offset = boxIndex * BOX_NAME_LENGTH8_SWSH;
            if (offset + BOX_NAME_LENGTH8_SWSH <= block.data.size())
            {
                std::u16string boxNameU16 = getString(block.data.data() + offset, BOX_NAME_LENGTH8_SWSH);
                std::string boxName = utf16ToUtf8(boxNameU16);

                if (boxName.empty())
                {
                    boxName = "Box " + std::to_string(boxIndex + 1);
                }

                boxNames[boxIndex] = boxName;
            }
            else
            {
                boxNames[boxIndex] = "Box " + std::to_string(boxIndex + 1);
            }
        }
    }

    void Trainer8SWSH::detectSaveRevision()
    {
        /**
         * Reports the game's LATEST content set, unconditionally.
         *
         * Sword/Shield record no DLC entitlement anywhere in the save. Everything PKSE could read is a
         * PROXY, and every proxy is wrong for somebody:
         *
         *  - Block PRESENCE reports Crown Tundra for everyone, because a patched v1.3.2 game allocates
         *    both DLC dex blocks in every save it creates, licence or no licence.
         *  - Block CONTENT ("has this dex recorded anything?") reports a lower tier for anyone who owns a
         *    DLC but has not yet seen a Pokemon in it -- including a player who bought the Expansion Pass
         *    and has not started it.
         *
         * Neither question has a right answer, because the save does not carry the fact. Showing the
         * fullest content set is honest about that: it tells the user what the GAME can hold, and leaves
         * what to put in the save to them. Nothing in PKSE gates content on this value -- it is the
         * title-bar label and the event log, and the pickers already offer everything.
         *
         * Legends: Z-A is deliberately NOT treated this way. It stores a real SAVE_REVISION u64, so its
         * label is read rather than inferred, and its value genuinely drives which Pokedex bits get
         * written (Trainer9LZA::zaSecondMegaSlot). Claiming the latest there would write Mega Dimension
         * registration into a base-game save.
         */
        this->saveRevision = 2;
        this->saveRevisionString = "Crown Tundra";
        this->gameVersionString = "v1.3";

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Save revision reported as: %d (%s, %s)", this->saveRevision, this->saveRevisionString.c_str(), this->gameVersionString.c_str());
        logInfoToFile(buffer);
    }

    void Trainer8SWSH::updatePartyBlock()
    {
        /**
         * Updates the PARTY block with modified Pokemon data.
         *
         * Empty party slots must be the game's encrypted "blank" (which DECRYPTS to species 0), NOT
         * literal zeros -- exactly like updateBoxBlock(). The game decrypts every slot and validates it,
         * so a zeroed slot decrypts to garbage and renders a BAD EGG. Measured on a real Shield save: an
         * empty party slot holds 331 non-zero bytes of 344 and decrypts to species 0. It is not a run of
         * zeros. The party is re-serialized on EVERY save, so a memset(0) here corrupts the party even
         * when the edit only touched a box.
         *
         * The party-count tail after the six slots (the block is 2068 bytes, not 6*344 = 2064) is
         * deliberately left untouched: parsePartyBlock treats an encrypted blank as occupied, so empty
         * slots load as species-0 "ghosts" that inflate party.size(). The save's own count is
         * authoritative, not party.size(). Same reasoning as Trainer8LA.
         */
        for (auto &block : blocks)
        {
            if (block.key == PARTY8_SWSH)
            {
                // Ensure the block data is large enough. Note this only ever GROWS the block, so a
                // real save's 4-byte count tail survives.
                size_t requiredSize = MAX_PARTY_SLOTS * SIZE_PARTY8_SWSH;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Which slots ALREADY read as empty, decided on the bytes in the file rather than on
                // party.size()? Those are left byte-for-byte alone below. There is no single canonical blank to
                // stamp: measured on a real save, the game's empty slot is not a zeroed entity and empty slots
                // differ from EACH OTHER too (stale bytes the game never cleared), so the only way to leave an
                // untouched party untouched is to not write to it.
                //
                // "Decrypts to species 0" is NOT sufficient on its own, and the reason is a trap: the cipher
                // seeds its PRNG with the encryption constant, a zeroed slot has EC 0, and the first PRNG word
                // from seed 0 is 0x0000 -- so an all-zero slot decrypts to species 0 as well, while its other
                // bytes are garbage the checksum rejects. A slot wrecked by a memset(0) would therefore look
                // "already empty" and be skipped, which is precisely the slot that needs repairing. A real blank
                // always carries non-zero bytes, so require that too.
                bool alreadyEmpty[MAX_PARTY_SLOTS] = {};
                for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    const size_t byteOffset = partySlotIndex * SIZE_PARTY8_SWSH;
                    if (byteOffset + SIZE_PARTY8_SWSH > block.data.size())
                        break;

                    bool anyNonZero = false;
                    for (size_t byteIndex = 0; byteIndex < SIZE_PARTY8_SWSH && !anyNonZero; ++byteIndex)
                        anyNonZero = (block.data[byteOffset + byteIndex] != 0);
                    // zeroed by an older build -> repair it below
                    if (!anyNonZero) continue;

                    std::vector<std::byte> encryptedRecord(SIZE_PARTY8_SWSH);
                    std::memcpy(encryptedRecord.data(), &block.data[byteOffset], SIZE_PARTY8_SWSH);
                    std::byte *decryptedRecord = decryptArray8SWSH(std::span<const std::byte>(encryptedRecord.data(), SIZE_PARTY8_SWSH));
                    alreadyEmpty[partySlotIndex] = (readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(decryptedRecord) + 0x08) == 0);
                    delete[] decryptedRecord;
                }

                // For a slot that must BECOME empty (a Pokemon was removed), and for repairing a
                // slot an older build zeroed into a Bad Egg, synthesize a blank that decrypts to
                // species 0: an all-zero PK8 encrypted with EC 0 (== PKHeX's new PK8() BlankPKM).
                // Same construction as createBlankPokemon() and updateBoxBlock()'s fallback.
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY8_SWSH, std::byte{0});
                    std::byte *encryptedRecord = encryptArray8SWSH(std::span<const std::byte>(zero.data(), SIZE_PARTY8_SWSH), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord), reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY8_SWSH);
                    delete[] encryptedRecord;
                }

                // Writes an empty slot: keep the game's own bytes when they already read as empty,
                // otherwise lay down the blank. Never zeros -- that is the Bad Egg.
                const auto clearSlot = [&](size_t slotIndex, size_t offset)
                {
                    if (slotIndex < MAX_PARTY_SLOTS && alreadyEmpty[slotIndex])
                        return;
                    std::memcpy(&block.data[offset], blankSlot.data(), SIZE_PARTY8_SWSH);
                };

                for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    const size_t offset = partySlotIndex * SIZE_PARTY8_SWSH;
                    if (slotHoldsLoadedRecord(party[partySlotIndex].get(), &block.data[offset], SIZE_PARTY8_SWSH, decryptArray8SWSH))
                        continue;

                    if (party[partySlotIndex] && party[partySlotIndex]->speciesID() != 0)
                    {
                        const ::Pokemon::Pokemon *pokemon = party[partySlotIndex].get();
                        uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                        std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());

                        std::byte *encryptedData = encryptArray8SWSH(decryptedSpan, encryptionConstant);

                        std::memcpy(&block.data[offset], encryptedData, pokemon->getDataSize());

                        delete[] encryptedData;
                    }
                    else
                    {
                        clearSlot(partySlotIndex, offset); // never zeros -- see the header note
                    }
                }

                // Remaining trailing slots are empty too.
                for (size_t partySlotIndex = party.size(); partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    clearSlot(partySlotIndex, partySlotIndex * SIZE_PARTY8_SWSH);
                }

                break;
            }
        }
    }

    void Trainer8SWSH::updateBoxNameBlock()
    {
        /**
         * The inverse of parseBoxLayoutBlock: 32 names of 0x22 bytes, UTF-16LE, null-terminated and
         * zero-padded (Utils::setString does both, reserving the last slot for the terminator).
         *
         * Deliberately does NOT resize the block the way updateBoxBlock does. BOX_LAYOUT is a fixed
         * 32 * 0x22 region in any real save, so a short block means the save is wrong -- growing it
         * would paper over that and hand the game a block of an unexpected size.
         */
        for (auto &block : blocks)
        {
            if (block.key != BOX_LAYOUT8_SWSH)
                continue;
            for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_SWSH && boxIndex < boxNames.size(); ++boxIndex)
            {
                // never persist a display default
                if (!isBoxNameDirty(boxIndex)) continue;
                const size_t offset = boxIndex * BOX_NAME_LENGTH8_SWSH;
                if (offset + BOX_NAME_LENGTH8_SWSH > block.data.size())
                    break;
                setString(block.data.data() + offset, BOX_NAME_LENGTH8_SWSH, utf8ToUtf16(boxNames[boxIndex]), MAX_BOX_NAME_CHARS8_SWSH);
            }
            break;
        }
    }

    void Trainer8SWSH::parseCurrentBoxBlock(const Block &block)
    {
        // "U32 Box Index" -- a scalar block; PKHeX reads it as a byte (0..31 fits one byte).
        if (block.data.empty())
            return;
        uint8_t box = block.data[0];
        if (box < BOX_COUNT8_SWSH)
            this->currentBox = box;
    }

    void Trainer8SWSH::updateCurrentBoxBlock()
    {
        // Inverse of parseCurrentBoxBlock: write the low byte and clear the rest of the scalar.
        for (auto &block : blocks)
        {
            if (block.key != CURRENT_BOX8_SWSH || block.data.empty())
                continue;
            block.data[0] = static_cast<uint8_t>(currentBox);
            for (size_t moveIndex = 1; moveIndex < block.data.size() && moveIndex < 4; ++moveIndex)
                block.data[moveIndex] = 0;
            break;
        }
    }

    void Trainer8SWSH::updateBoxBlock()
    {
        /**
         * Writes the BOX block back, deciding every slot from its POSITION in the file, the way PKHeX
         * writes only the slot it was asked to set:
         *   - the bytes still hold exactly this slot's in-memory record -> leave them;
         *   - an occupied slot -> encrypt and write it;
         *   - an empty slot whose bytes already read as empty -> leave them;
         *   - otherwise (a Pokemon moved or removed from here, or a zeroed slot) -> a synthesized blank.
         * Never harvest one slot's bytes as "the" blank: after a Move the first empty slot in memory can
         * still hold the moved Pokemon's record in the file, and stamping that over every empty slot
         * cloned it into all of them.
         */
        for (auto &block : blocks)
        {
            if (block.key == BOX8_SWSH)
            {
                size_t requiredSize = BOX_COUNT8_SWSH * BOX_SLOTS * SIZE_PARTY8_SWSH;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // An all-zero PK8 encrypted with EC 0 (== PKHeX's BlankPKM). Never zeros -- the game
                // decrypts every slot, and a zeroed one renders a Bad Egg.
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY8_SWSH, std::byte{0});
                    std::byte *encryptedRecord = encryptArray8SWSH(std::span<const std::byte>(zero.data(), SIZE_PARTY8_SWSH), 0);
                    const uint8_t *encryptedBytes = reinterpret_cast<const uint8_t *>(encryptedRecord);
                    blankSlot.assign(encryptedBytes, encryptedBytes + SIZE_PARTY8_SWSH);
                    delete[] encryptedRecord;
                }

                for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_SWSH; ++boxIndex)
                {
                    for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
                    {
                        const size_t offset = (boxIndex * BOX_SLOTS + slot) * SIZE_PARTY8_SWSH;
                        const auto &pokemon = boxes[boxIndex][slot];
                        if (slotHoldsLoadedRecord(pokemon.get(), &block.data[offset], SIZE_PARTY8_SWSH, decryptArray8SWSH))
                            continue;

                        if (pokemon && pokemon->speciesID() != 0)
                        {
                            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));
                            std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());
                            std::byte *encryptedData = encryptArray8SWSH(decryptedSpan, encryptionConstant);
                            std::memcpy(&block.data[offset], encryptedData, SIZE_PARTY8_SWSH);
                            delete[] encryptedData;
                        }
                        else if (!slotHoldsGameBlank(&block.data[offset], SIZE_PARTY8_SWSH, RECORD_SPECIES_OFFSET, decryptArray8SWSH))
                        {
                            std::memcpy(&block.data[offset], blankSlot.data(), SIZE_PARTY8_SWSH);
                        }
                    }
                }
                break;
            }
        }
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer8SWSH::createBlankPokemon() const
    {
        // Mirror updateBoxBlock()'s encrypted-blank fallback: a zeroed *decrypted* PK8 buffer
        // encrypted with EC/seed 0, then fed to the ctor (which decrypts it straight back to zeros)
        // -> a clean species-0, Sanity-0, checksum-valid entity. Raw zeros in the ctor would decrypt
        // to garbage (BAD EGG); the encrypt->decrypt round-trip is what makes the blank valid.
        std::vector<std::byte> zero(SIZE_PARTY8_SWSH, std::byte{0});
        std::byte *encryptedRecord = encryptArray8SWSH(std::span<const std::byte>(zero.data(), SIZE_PARTY8_SWSH), 0);
        auto clone = std::make_unique<Pokemon8SWSH>(std::span<const std::byte>(encryptedRecord, SIZE_PARTY8_SWSH));
        delete[] encryptedRecord;
        return clone;
    }

    //
    // Sword/Shield have THREE dexes, one save block each with its own numbering: Galar, Isle of
    // Armor and Crown Tundra. A species belongs to exactly one (SWSHDexTable, generated from
    // PKHeX personal_swsh), and its entry is 0x30 bytes at (index - 1) * 0x30 inside that block.
    //
    // Entry layout (PKHeX Zukan8):
    //   0x00  four u64 SEEN regions -- not-shiny/male, not-shiny/female, shiny/male, shiny/female.
    //         Each bit is a FORM index (0-62); bit 63 is Gigantamax.
    //   0x20  u32 caught flags: bit0 Owned, bit1 OwnedGigantamax, bits2-14 languages,
    //         bits15-27 DisplayFormID, bit28 DisplayGigantamax, bits29-30 DisplayGender,
    //         bit31 DisplayShiny
    //   0x24  u32 battled count      0x28/0x2C reserved
    //
    // Same shape as Gen 3 and Let's Go: run over all of storage at save time, only ever set.
    namespace
    {
        constexpr size_t SWSH_OFS_CAUGHT = 0x20;
        constexpr size_t SWSH_SEEN_REGION = 8; // one u64 per region
        constexpr size_t SWSH_OFS_BATTLED = 0x24;
        constexpr uint16_t SPECIES_ALCREMIE = 869;
        constexpr uint16_t SPECIES_ETERNATUS = 890;
        constexpr size_t FORM_ARGUMENT_OFFSET8_SWSH = 0xE4; // PKHeX G8PKM.FormArgument (u32)
        constexpr uint8_t ETERNAMAX_SEEN_BIT = 63;

        // Language id -> dex language slot. Slot 6 (langID 6) is unused, so 7+ shift down by two.
        // Identical rule to Let's Go (PKHeX Zukan8.GetDexLangFlag).
        int swshLangSlot(uint8_t language)
        {
            if (language == 0 || language == 6 || language > 10)
                return -1;
            return (language >= 7) ? language - 2 : language - 1;
        }
    }

    void Trainer8SWSH::updatePokedexBlock()
    {
        // The three dex blocks. The DLC ones are absent (or empty) on a save without that DLC --
        // that is exactly how PKSE already detects the save revision -- so a missing block simply
        // means those species cannot be registered here.
        std::vector<uint8_t> *galar = nullptr;
        std::vector<uint8_t> *armor = nullptr;
        std::vector<uint8_t> *crown = nullptr;
        for (auto &block : blocks)
        {
            if (block.key == SAVE_REVISION8_SWSH)
                galar = &block.data;
            else if (block.key == SAVE_REVISION8_R1_SWSH)
                armor = &block.data;
            else if (block.key == SAVE_REVISION8_R2_SWSH)
                crown = &block.data;
        }
        // no base dex: nothing sane to write
        if (!galar || galar->empty()) return;

        // Each dex block should be exactly (entry count) * 0x30. If one is not, the layout this code
        // assumes is wrong and every write into it would be silently dropped by the bounds check
        // below -- which reads as "the Pokedex just didn't update". Say so instead.
        struct
        {
            const char *name;
            const std::vector<uint8_t> *data;
            uint16_t count;
        } expect[] = {
            {"Galar", galar, ::Pokemon::DEX8SWSH_GALAR_COUNT},
            {"Armor", armor, ::Pokemon::DEX8SWSH_ARMOR_COUNT},
            {"Crown", crown, ::Pokemon::DEX8SWSH_CROWN_COUNT},
        };
        for (const auto &x : expect)
        {
            // DLC the player doesn't have
            if (!x.data || x.data->empty()) continue;
            const size_t want = static_cast<size_t>(x.count) * ::Pokemon::DEX8SWSH_ENTRY_SIZE;
            if (x.data->size() != want)
            {
                logErrorToFile("Pokedex block size unexpected; registrations into it will be skipped", (std::string(x.name) + ": got " + std::to_string(x.data->size()) + ", expected " + std::to_string(want)).c_str());
            }
        }

        auto registerMon = [&](const ::Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->isEgg())
                return;
            const uint16_t species = pokemon->speciesID();
            if (species == 0)
                return;

            const ::Pokemon::DexEntry8SWSH e = ::Pokemon::getDexEntry8SWSH(species);
            if (e.dex == ::Pokemon::Dex8SWSH::None) return;

            std::vector<uint8_t> *dex = nullptr;
            switch (e.dex)
            {
            case ::Pokemon::Dex8SWSH::Galar:
                dex = galar;
                break;
            case ::Pokemon::Dex8SWSH::Armor:
                dex = armor;
                break;
            case ::Pokemon::Dex8SWSH::Crown:
                dex = crown;
                break;
            default:
                return;
            }
            // A DLC dex the player does not own is absent or zero-length; skip rather than allocate
            // one, which would fabricate DLC data in a save that has none.
            if (!dex || dex->empty())
                return;

            const size_t base = static_cast<size_t>(e.index - 1) * ::Pokemon::DEX8SWSH_ENTRY_SIZE;
            if (base + ::Pokemon::DEX8SWSH_ENTRY_SIZE > dex->size())
                return;

            // SEEN: bit = form, inside the u64 for this gender/shiny combination. Forms past 62 have
            // no bit (63 is Gigantamax), so they are recorded against the base form rather than
            // spilling into the neighbouring region.
            //
            // PKHeX Zukan8.SetDex: Alcremie's bit is form * 7 + its FormArgument (the sweet), and
            // Eternamax (Eternatus form 1) sets bit 63 and is otherwise recorded as form 0.
            uint32_t form = pokemon->form();
            const bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
            const uint32_t displayGender = pokemon->gender() & 1u; // genderless -> male, as PKHeX
            const int region = static_cast<int>(displayGender) | (shiny ? 2 : 0);
            const size_t seenOfs = base + static_cast<size_t>(region) * SWSH_SEEN_REGION;
            if (species == SPECIES_ALCREMIE)
            {
                const auto record = pokemon->getData();
                const uint8_t formArgument = record.size() > FORM_ARGUMENT_OFFSET8_SWSH ? static_cast<uint8_t>(record[FORM_ARGUMENT_OFFSET8_SWSH]) : 0;
                form = form * 7 + formArgument;
            }
            else if (species == SPECIES_ETERNATUS && form == 1)
            {
                form = 0;
                (*dex)[seenOfs + (ETERNAMAX_SEEN_BIT >> 3)] |= static_cast<uint8_t>(1u << (ETERNAMAX_SEEN_BIT & 7));
            }
            if (form <= 62)
                (*dex)[seenOfs + (form >> 3)] |= static_cast<uint8_t>(1u << (form & 7));

            // CAUGHT flags (u32 at 0x20).
            const size_t cOfs = base + SWSH_OFS_CAUGHT;
            uint32_t flags = static_cast<uint32_t>((*dex)[cOfs]) | (static_cast<uint32_t>((*dex)[cOfs + 1]) << 8) | (static_cast<uint32_t>((*dex)[cOfs + 2]) << 16) | (static_cast<uint32_t>((*dex)[cOfs + 3]) << 24);
            const bool wasOwned = (flags & 1u) != 0;
            flags |= 1u; // bit 0: owned
            const int lang = swshLangSlot(pokemon->language());
            // bits 2-14: languages obtained
            if (lang >= 0) flags |= 1u << (2 + lang);

            // DisplayFormID (bits 15-27) picks which form the entry shows. Set it only for an entry
            // that was not owned before, so a species whose first catch is an alternate form displays
            // that form -- and an entry the player already has keeps whatever it was showing. Not
            // moved on later saves: registration walks storage, so "latest" would mean "last in box
            // order", which would change the dex display arbitrarily every save.
            if (!wasOwned)
            {
                flags = (flags & ~(0x1FFFu << 15)) | (static_cast<uint32_t>(form & 0x1FFF) << 15);
                // bit 31: display shiny
                if (shiny) flags |= 1u << 31;
                // bits 29-30: display gender, one 2-bit value (PKHeX SetGenderDisplayed)
                flags = (flags & ~(3u << 29)) | (displayGender << 29);
            }
            (*dex)[cOfs] = static_cast<uint8_t>(flags);
            (*dex)[cOfs + 1] = static_cast<uint8_t>(flags >> 8);
            (*dex)[cOfs + 2] = static_cast<uint8_t>(flags >> 16);
            (*dex)[cOfs + 3] = static_cast<uint8_t>(flags >> 24);

            // A registered species has been battled at least once (PKHeX SetDex).
            const size_t battledOffset = base + SWSH_OFS_BATTLED;
            if (readUInt32LittleEndian(&(*dex)[battledOffset]) == 0)
                writeUInt32LittleEndian(&(*dex)[battledOffset], 1);
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());
    }

    void Trainer8SWSH::updateTrainerInfoBlock()
    {
        // OT name (0xB0) goes back into MyStatus8; money into its own block -- the same authoritative
        // places parse reads them, so edits take effect in-game. encrypt() re-hashes.
        for (auto &block : blocks)
        {
            if (block.key == MY_STATUS8_SWSH)
            {
                if (block.data.size() >= 0xB0 + 0x1A)
                    setString(&block.data[0xB0], 0x1A, utf8ToUtf16(trainerName), 12);
            }
            else if (block.key == MONEY8_SWSH)
            {
                if (block.data.size() >= 0x04 + 4)
                    writeUInt32LittleEndian(&block.data[0x04], money);
            }
        }
    }

    void Trainer8SWSH::updateItemBlock()
    {
        /**
         * Updates the ITEM block with modified inventory data.
         *
         * Process:
         * 1. Find the ITEM block
         * 2. Ensure block is large enough for all pouches
         * 3. For each pouch:
         *    a. Put every item that has not changed back in the slot it was read from, and give
         *       the rest the lowest free slot (placePouchPositional)
         *    b. Zero every slot left over
         *
         * The pouch is NOT packed from its first slot: the game leaves entries after a run of empty
         * slots, and packing would move them on every save, edited or not.
         */
        for (auto &block : blocks)
        {
            if (block.key == ITEM8_SWSH)
            {
                size_t maxSize = 4856; // Sum of all pouch sizes * 4 bytes per item
                if (block.data.size() < maxSize)
                {
                    block.data.resize(maxSize, 0);
                }

                for (int pIndex = 0; pIndex < static_cast<int>(PouchType8SWSH::Count); pIndex++)
                {
                    PouchType8SWSH pouchType = static_cast<PouchType8SWSH>(pIndex);
                    const PouchInfo8SWSH &info = getPouchInfo8SWSH(pouchType);
                    const auto &pouch = items[pIndex];
                    const size_t slotCount = static_cast<size_t>(info.maxCount);
                    const std::vector<ItemPlacement> placement = placePouchPositional(slotCount, pouch, pIndex < static_cast<int>(itemSlot.size()) ? itemSlot[pIndex] : std::vector<ItemOrigin>{});

                    for (size_t itemSlotIndex = 0; itemSlotIndex < slotCount; itemSlotIndex++)
                    {
                        writeUInt32LittleEndian(&block.data[info.offset + itemSlotIndex * 4], 0);
                    }
                    for (size_t itemIndex = 0; itemIndex < pouch.size(); itemIndex++)
                    {
                        if (placement[itemIndex].slot == slotCount)
                        {
                            continue;
                        }
                        const InventoryItem &item = pouch[itemIndex];
                        InventoryItem8SWSH item8;
                        item8.itemId = item.itemId;
                        item8.count = item.count;
                        item8.isNew = item.isNew;
                        item8.isFavorite = item.isFavorite;

                        uint32_t itemValue = item8.toValue();
                        writeUInt32LittleEndian(&block.data[info.offset + placement[itemIndex].slot * 4], itemValue);
                    }
                }
                break;
            }
        }
    }
}
