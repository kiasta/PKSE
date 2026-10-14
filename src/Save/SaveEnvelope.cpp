#include "Save/SaveEnvelope.h"

#include <algorithm>
#include <iterator>
#include <string_view>

#include "sha1.h" // memecrypto's SHA-1

#include "Save/RtcFooter.h"
#include "Utils/HelperUtilities.h"

namespace Save
{
    namespace
    {
        /// Every Gen 4 and Gen 5 save (PKHeX SaveUtil.SIZE_G4RAW).
        constexpr size_t NINTENDO_DS_SAVE_SIZE = 0x80000;
        /// Gen 3's 1 Mbit flash.
        constexpr size_t GBA_SAVE_SIZE = 0x20000;
        /// Gen 1 and international Gen 2, then Japanese Gen 2.
        constexpr size_t GAME_BOY_SAVE_SIZES[] = {0x8000, 0x10000};

        /// The saves padding can follow, largest first: a file padded up from a DS save is tried as a DS
        /// save before the GBA save that also fits inside it (the probe chain has the last word either way).
        constexpr size_t PADDED_SAVE_SIZES[] = {NINTENDO_DS_SAVE_SIZE, GBA_SAVE_SIZE, 0x10000, 0x8000};

        constexpr size_t LIBRETRO_GBA_SAVE_RAM_SIZE = 0x22000;
        constexpr size_t LIBRETRO_GBA_EEPROM_SIZE = 0x2000;

        constexpr size_t DESMUME_FOOTER_SIZE = 0x7A;
        constexpr std::string_view DESMUME_FOOTER_SIGNATURE = "|-DESMUME SAVE-|";

        constexpr size_t ACTION_REPLAY_HEADER_SIZE = 0xA4;

        // PKHeX HeaderInfoNSO: the magic, a ROM hash, a build string, an optional clock, then the save's
        // SHA-1 as lowercase hex text, which is always the header's last field.
        constexpr std::string_view SWITCH_ONLINE_MAGIC = "SRAM";
        constexpr size_t SWITCH_ONLINE_BUILD_LENGTH_OFFSET = 0x30;
        constexpr size_t SWITCH_ONLINE_BUILD_OFFSET = 0x34;
        constexpr size_t SWITCH_ONLINE_RTC_SIZE = 0x20;
        constexpr size_t SWITCH_ONLINE_HASH_TEXT_SIZE = SHA1_DIGEST_SIZE * 2;
        constexpr size_t SWITCH_ONLINE_MIN_HEADER_SIZE = 0x68;
        constexpr size_t SWITCH_ONLINE_MAX_HEADER_SIZE = 0xA0;

        // TGB Dual's Windows front end (win32_ui/dialogs.h) writes a signed offset in seconds for the clock
        // after the cartridge RAM of EVERY MBC3 cartridge -- Red and Blue as well as Gold, Silver and Crystal.
        constexpr size_t TGB_DUAL_CLOCK_FOOTER_SIZE = 4;

        // MiSTer's Game Boy and GBA cores save one SD sector more than the save when the game uses the
        // clock, and read the file's size to know which (Gameboy.sv, GBA.sv).
        constexpr size_t MISTER_CLOCK_SECTOR_SIZE = 0x200;
        constexpr size_t MISTER_SAVE_SIZES[] = {0x8000, 0x10000, GBA_SAVE_SIZE};

        // meteor's libretro core reports CartMem::MAX_SIZE + 4 bytes of save RAM: the cartridge memory, then
        // the u32 size LoadCartInferred reads back to choose the chip -- for 1 Mbit flash, the save's size.
        constexpr size_t METEOR_SIZE_FOOTER_SIZE = 4;

        // BizHawk 1.x's VBA Next core (SyncBatteryRam): "GBABATT\0", s32 flash length, s32 EEPROM length,
        // then the flash, then the EEPROM. BizHawk's mGBA core still reads them (MGBAHawk LegacyFix).
        constexpr std::string_view BIZHAWK_BATTERY_MAGIC{"GBABATT\0", 8};
        constexpr size_t BIZHAWK_BATTERY_HEADER_SIZE = 16;

        // DeSmuME's import_duc calls this version 1: the first Action Replay DS's PC software and MAX Drive
        // DS (.duc and .dss). Nothing past the magic is read, by DeSmuME or here.
        constexpr std::string_view ACTION_REPLAY_MAX_DRIVE_MAGIC = "ARDS000000000001";
        constexpr size_t ACTION_REPLAY_MAX_DRIVE_HEADER_SIZE = 500;

        // no$gba's battery file: a 0x40-byte header (the magic, then a date), then entries of a 4-character
        // id, a u32 storage method and a u32 length, ending with "STOP". Unknown entries are skipped by
        // their length, so everything after "SRAM" is kept byte for byte.
        constexpr std::string_view NOCASH_GBA_MAGIC{"NocashGbaBackupMediaSavDataFile\x1A", 32};
        constexpr size_t NOCASH_GBA_HEADER_SIZE = 0x40;
        constexpr std::string_view NOCASH_SAVE_ENTRY_ID = "SRAM";
        constexpr size_t NOCASH_ENTRY_HEADER_SIZE = 12;
        constexpr uint32_t NOCASH_STORED = 0;
        constexpr uint32_t NOCASH_RUN_LENGTH = 1;
        constexpr uint32_t NOCASH_LZ = 2;
        /// no$gba's largest backup chip ("FLASH 8192KBytes" in its own media list); a longer save is
        /// corruption, not a save.
        constexpr size_t NOCASH_LARGEST_SAVE = 0x800000;
        /// The sizes a short no$gba save is filled up to, as DeSmuME's import does: Gen 3's flash, a DS save.
        constexpr size_t NOCASH_SAVE_SIZES[] = {GBA_SAVE_SIZE, NINTENDO_DS_SAVE_SIZE};
        // The run-length code's controls (no$gba's changelog: "RLU ... code 80h dta8 len16").
        constexpr uint8_t RUN_LENGTH_END = 0x00;
        constexpr uint8_t RUN_LENGTH_LONGEST_COPY = 0x7F;
        constexpr uint8_t RUN_LENGTH_LONG_REPEAT = 0x80;
        constexpr size_t RUN_LENGTH_LONGEST_REPEAT = 0xFFFF;
        constexpr size_t RUN_LENGTH_SHORTEST_REPEAT = 3; // a repeat of two costs what copying it does

        // YSMenu keeps its per-game flags in a save file's last 8 bytes, ending in "NMSY" (savconv, r4loader).
        constexpr std::string_view YSMENU_FLAGS_MAGIC = "NMSY";
        constexpr size_t YSMENU_FLAGS_SIZE = 8;

        // Containers PKSE recognises and cannot read; see describeUnreadableSaveContainer.
        constexpr std::string_view RETROARCH_RZIP_MAGIC = "#RZIPv";
        constexpr uint32_t GOOMBA_STATE_IDS[] = {0x57A731D8, 0x57A731D9}; // goombacolor sram.c STATEID, STATEID2
        constexpr std::string_view RETRON_5_MAGIC = "RTN5";
        constexpr std::string_view ZIP_MAGIC = "PK\x03\x04";
        constexpr std::string_view DISA_MAGIC{"DISA\0\0\x04\0", 8}; // GodMode9 disadiff.h
        constexpr size_t DISA_MAGIC_OFFSET = 0x100;

        bool startsWith(const std::vector<uint8_t> &fileBytes, std::string_view prefix, size_t prefixOffset = 0)
        {
            return fileBytes.size() >= prefixOffset + prefix.size() &&
                   std::equal(prefix.begin(), prefix.end(),
                              fileBytes.begin() + static_cast<std::ptrdiff_t>(prefixOffset));
        }

        bool endsWith(const std::vector<uint8_t> &fileBytes, std::string_view suffix)
        {
            return fileBytes.size() >= suffix.size() && startsWith(fileBytes, suffix, fileBytes.size() - suffix.size());
        }

        uint32_t readUInt32At(const std::vector<uint8_t> &fileBytes, size_t fieldOffset)
        {
            return Utils::readUInt32LittleEndian(fileBytes.data() + fieldOffset);
        }

        void appendUInt32LittleEndian(std::vector<uint8_t> &fileBytes, uint32_t fieldValue)
        {
            uint8_t fieldBytes[4];
            Utils::writeUInt32LittleEndian(fieldBytes, fieldValue);
            fileBytes.insert(fileBytes.end(), std::begin(fieldBytes), std::end(fieldBytes));
        }

        /// True when the file is one of `saveSizes` followed by exactly `footerSize` bytes.
        template <size_t SizeCount>
        bool isSaveSizePlus(size_t fileSize, size_t footerSize, const size_t (&saveSizes)[SizeCount])
        {
            return std::any_of(std::begin(saveSizes), std::end(saveSizes),
                               [&](size_t saveSize) { return fileSize == saveSize + footerSize; });
        }

        /// 0 unless the file is a whole Switch Online header followed by a whole Game Boy save.
        size_t switchOnlineHeaderSize(const std::vector<uint8_t> &fileBytes)
        {
            const auto isHeaderThenSave = [&](size_t headerSize)
            {
                return headerSize >= SWITCH_ONLINE_MIN_HEADER_SIZE && headerSize <= SWITCH_ONLINE_MAX_HEADER_SIZE &&
                       std::any_of(std::begin(GAME_BOY_SAVE_SIZES), std::end(GAME_BOY_SAVE_SIZES),
                                   [&](size_t saveSize) { return fileBytes.size() == headerSize + saveSize; });
            };
            if (fileBytes.size() < SWITCH_ONLINE_MIN_HEADER_SIZE || !startsWith(fileBytes, SWITCH_ONLINE_MAGIC))
                return 0;

            // A u32 in the file, read as one byte exactly as PKHeX does: no build string is that long.
            size_t headerSize = SWITCH_ONLINE_BUILD_OFFSET + fileBytes[SWITCH_ONLINE_BUILD_LENGTH_OFFSET];
            if (headerSize >= fileBytes.size())
                return 0;
            const bool hasClock = fileBytes[headerSize] != 0;
            headerSize += 1 + (hasClock ? SWITCH_ONLINE_RTC_SIZE : 0) + SWITCH_ONLINE_HASH_TEXT_SIZE;
            return isHeaderThenSave(headerSize) ? headerSize : 0;
        }

        void writeSwitchOnlineSaveHash(std::vector<uint8_t> &headerBytes, const std::vector<uint8_t> &heldBytes)
        {
            unsigned char digest[SHA1_DIGEST_SIZE];
            sha1(digest, heldBytes.data(), static_cast<unsigned long>(heldBytes.size()));

            constexpr char hexDigits[] = "0123456789abcdef";
            uint8_t *hashText = headerBytes.data() + headerBytes.size() - SWITCH_ONLINE_HASH_TEXT_SIZE;
            for (size_t digestIndex = 0; digestIndex < SHA1_DIGEST_SIZE; ++digestIndex)
            {
                hashText[digestIndex * 2] = static_cast<uint8_t>(hexDigits[digest[digestIndex] >> 4]);
                hashText[digestIndex * 2 + 1] = static_cast<uint8_t>(hexDigits[digest[digestIndex] & 0x0F]);
            }
        }

        /// no$gba's run-length code, read as DeSmuME's no_gba_unpackSAV reads it: 00 ends the code, 01-7F
        /// copies that many bytes, 81-FF repeats the next byte (control - 0x80) times, and 80 repeats the
        /// next byte by the u16 after it. `codeEnd` is the offset just past the 00.
        bool decodeNocashRunLength(const std::vector<uint8_t> &fileBytes, size_t codeStart, size_t unpackedLength,
                                   std::vector<uint8_t> &decodedBytes, size_t &codeEnd)
        {
            decodedBytes.clear();
            decodedBytes.reserve(unpackedLength);
            size_t readOffset = codeStart;
            while (readOffset < fileBytes.size())
            {
                const uint8_t control = fileBytes[readOffset++];
                if (control == RUN_LENGTH_END)
                {
                    codeEnd = readOffset;
                    return decodedBytes.size() == unpackedLength;
                }
                const size_t bytesLeftInFile = fileBytes.size() - readOffset;
                const size_t bytesLeftToDecode = unpackedLength - decodedBytes.size();
                if (control <= RUN_LENGTH_LONGEST_COPY)
                {
                    if (control > bytesLeftInFile || control > bytesLeftToDecode)
                        return false;
                    const auto copyStart = fileBytes.begin() + static_cast<std::ptrdiff_t>(readOffset);
                    decodedBytes.insert(decodedBytes.end(), copyStart, copyStart + control);
                    readOffset += control;
                    continue;
                }
                size_t repeatCount = static_cast<size_t>(control - RUN_LENGTH_LONG_REPEAT);
                size_t controlSize = 1;
                if (control == RUN_LENGTH_LONG_REPEAT)
                {
                    if (bytesLeftInFile < 3)
                        return false;
                    repeatCount = Utils::readUInt16LittleEndian(fileBytes.data() + readOffset + 1);
                    controlSize = 3;
                }
                if (bytesLeftInFile < 1 || repeatCount == 0 || repeatCount > bytesLeftToDecode)
                    return false;
                decodedBytes.insert(decodedBytes.end(), repeatCount, fileBytes[readOffset]);
                readOffset += controlSize;
            }
            return false;
        }

        /// The same code, written greedily: three or more equal bytes become a repeat and everything else a
        /// copy of at most 0x7F bytes. Any sequence of these controls decodes to the same save, so where
        /// the code cuts changes the size of the file and never what it holds.
        std::vector<uint8_t> encodeNocashRunLength(const std::vector<uint8_t> &heldBytes, size_t storedLength)
        {
            std::vector<uint8_t> codeBytes;
            size_t copyStart = 0;
            const auto writeCopies = [&](size_t copyEnd)
            {
                while (copyStart < copyEnd)
                {
                    const size_t copyLength = std::min<size_t>(copyEnd - copyStart, RUN_LENGTH_LONGEST_COPY);
                    codeBytes.push_back(static_cast<uint8_t>(copyLength));
                    const auto copyBegin = heldBytes.begin() + static_cast<std::ptrdiff_t>(copyStart);
                    codeBytes.insert(codeBytes.end(), copyBegin, copyBegin + static_cast<std::ptrdiff_t>(copyLength));
                    copyStart += copyLength;
                }
            };

            size_t readOffset = 0;
            while (readOffset < storedLength)
            {
                size_t repeatCount = 1;
                while (readOffset + repeatCount < storedLength && repeatCount < RUN_LENGTH_LONGEST_REPEAT &&
                       heldBytes[readOffset + repeatCount] == heldBytes[readOffset])
                    ++repeatCount;
                if (repeatCount < RUN_LENGTH_SHORTEST_REPEAT)
                {
                    readOffset += repeatCount;
                    continue;
                }
                writeCopies(readOffset);
                if (repeatCount <= RUN_LENGTH_LONGEST_COPY)
                {
                    codeBytes.push_back(static_cast<uint8_t>(RUN_LENGTH_LONG_REPEAT + repeatCount));
                    codeBytes.push_back(heldBytes[readOffset]);
                }
                else
                {
                    codeBytes.push_back(RUN_LENGTH_LONG_REPEAT);
                    codeBytes.push_back(heldBytes[readOffset]);
                    codeBytes.push_back(static_cast<uint8_t>(repeatCount & 0xFF));
                    codeBytes.push_back(static_cast<uint8_t>(repeatCount >> 8));
                }
                readOffset += repeatCount;
                copyStart = readOffset;
            }
            writeCopies(storedLength);
            codeBytes.push_back(RUN_LENGTH_END);
            return codeBytes;
        }

        /// no$gba's battery file, when its "SRAM" entry holds the save as it is or run-length coded. An LZ
        /// entry is not split -- there is no LZ decoder here, and describeUnreadableSaveContainer says what
        /// to do instead.
        bool splitNocashGbaContainer(const std::vector<uint8_t> &fileBytes, SaveEnvelope &envelope,
                                     std::vector<uint8_t> &saveImage)
        {
            constexpr size_t entryStart = NOCASH_GBA_HEADER_SIZE;
            constexpr size_t dataStart = entryStart + NOCASH_ENTRY_HEADER_SIZE;
            if (fileBytes.size() < dataStart || !startsWith(fileBytes, NOCASH_GBA_MAGIC) ||
                !startsWith(fileBytes, NOCASH_SAVE_ENTRY_ID, entryStart))
                return false;

            NocashSaveEntry saveEntry;
            saveEntry.compressionMethod = readUInt32At(fileBytes, entryStart + 4);
            const size_t entryLength = readUInt32At(fileBytes, entryStart + 8);
            std::vector<uint8_t> storedSave;
            size_t entryEnd = 0;
            if (saveEntry.compressionMethod == NOCASH_STORED)
            {
                if (entryLength > fileBytes.size() - dataStart || entryLength > NOCASH_LARGEST_SAVE)
                    return false;
                entryEnd = dataStart + entryLength;
                storedSave.assign(fileBytes.begin() + dataStart,
                                  fileBytes.begin() + static_cast<std::ptrdiff_t>(entryEnd));
            }
            else if (saveEntry.compressionMethod == NOCASH_RUN_LENGTH)
            {
                constexpr size_t codeStart = dataStart + 4;
                if (fileBytes.size() < codeStart)
                    return false;
                const size_t unpackedLength = readUInt32At(fileBytes, dataStart);
                if (unpackedLength > NOCASH_LARGEST_SAVE ||
                    !decodeNocashRunLength(fileBytes, codeStart, unpackedLength, storedSave, entryEnd))
                    return false;
                // The code ends itself, so the length field only has to agree with it -- in either of the two
                // ways it is read.
                const size_t codeLength = entryEnd - codeStart;
                if (entryLength == codeLength + 4)
                    saveEntry.lengthCountsUnpackedLength = true;
                else if (entryLength == codeLength)
                    saveEntry.lengthCountsUnpackedLength = false;
                else
                    return false;
            }
            else
            {
                return false;
            }
            if (storedSave.empty())
                return false;

            saveEntry.storedSaveLength = storedSave.size();
            const size_t *fillSize = std::find_if(std::begin(NOCASH_SAVE_SIZES), std::end(NOCASH_SAVE_SIZES),
                                                  [&](size_t saveSize) { return saveSize >= storedSave.size(); });
            if (fillSize != std::end(NOCASH_SAVE_SIZES))
                storedSave.resize(*fillSize, 0xFF);

            envelope.kind = SaveEnvelopeKind::NocashGbaContainer;
            envelope.headerBytes.assign(fileBytes.begin(), fileBytes.begin() + entryStart);
            envelope.paddingBytes.clear();
            envelope.footerBytes.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(entryEnd), fileBytes.end());
            envelope.nocashSaveEntry = saveEntry;
            saveImage = std::move(storedSave);
            return true;
        }

        std::vector<uint8_t> joinNocashGbaContainer(const SaveEnvelope &envelope, const std::vector<uint8_t> &heldBytes)
        {
            const NocashSaveEntry &saveEntry = envelope.nocashSaveEntry;
            // The entry holds as much as no$gba stored, unless the save now reaches past that.
            size_t storedLength = std::min(saveEntry.storedSaveLength, heldBytes.size());
            if (std::any_of(heldBytes.begin() + static_cast<std::ptrdiff_t>(storedLength), heldBytes.end(),
                            [](uint8_t heldByte) { return heldByte != 0xFF; }))
                storedLength = heldBytes.size();

            std::vector<uint8_t> fileBytes = envelope.headerBytes;
            fileBytes.insert(fileBytes.end(), NOCASH_SAVE_ENTRY_ID.begin(), NOCASH_SAVE_ENTRY_ID.end());
            appendUInt32LittleEndian(fileBytes, saveEntry.compressionMethod);
            if (saveEntry.compressionMethod == NOCASH_RUN_LENGTH)
            {
                const std::vector<uint8_t> codeBytes = encodeNocashRunLength(heldBytes, storedLength);
                const size_t entryLength = codeBytes.size() + (saveEntry.lengthCountsUnpackedLength ? 4 : 0);
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(entryLength));
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(storedLength));
                fileBytes.insert(fileBytes.end(), codeBytes.begin(), codeBytes.end());
            }
            else
            {
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(storedLength));
                fileBytes.insert(fileBytes.end(), heldBytes.begin(),
                                 heldBytes.begin() + static_cast<std::ptrdiff_t>(storedLength));
            }
            fileBytes.insert(fileBytes.end(), envelope.footerBytes.begin(), envelope.footerBytes.end());
            return fileBytes;
        }

        /// Whether everything in `heldBytes` past `saveSize` is space nothing wrote: one repeated byte, 00 or
        /// FF, which may end with YSMenu's 8 bytes of flags.
        bool isPaddingAfter(const std::vector<uint8_t> &heldBytes, size_t saveSize)
        {
            if (heldBytes.size() <= saveSize)
                return false;
            auto paddingEnd = heldBytes.end();
            if (heldBytes.size() - saveSize >= YSMENU_FLAGS_SIZE && endsWith(heldBytes, YSMENU_FLAGS_MAGIC))
                paddingEnd -= YSMENU_FLAGS_SIZE;
            const auto paddingStart = heldBytes.begin() + static_cast<std::ptrdiff_t>(saveSize);
            if (paddingStart == paddingEnd)
                return true;
            const uint8_t fillByte = *paddingStart;
            return (fillByte == 0x00 || fillByte == 0xFF) &&
                   std::all_of(paddingStart, paddingEnd, [&](uint8_t paddingByte) { return paddingByte == fillByte; });
        }
    }

    bool splitSaveEnvelope(SaveEnvelopeKind kind, const std::vector<uint8_t> &fileBytes, SaveEnvelope &envelope,
                           std::vector<uint8_t> &saveImage)
    {
        size_t headerSize = 0;
        size_t footerSize = 0;
        switch (kind)
        {
        case SaveEnvelopeKind::None:
            break;
        case SaveEnvelopeKind::RtcFooter:
            footerSize = rtcFooterLength(fileBytes.size());
            if (footerSize == 0)
                return false;
            break;
        case SaveEnvelopeKind::LibretroGbaSaveRam:
            if (fileBytes.size() != LIBRETRO_GBA_SAVE_RAM_SIZE)
                return false;
            footerSize = LIBRETRO_GBA_EEPROM_SIZE;
            break;
        case SaveEnvelopeKind::DesmumeFooter:
            // Found by its signature rather than the file's total, which is whatever DeSmuME held: a 1 MiB
            // flashcart save imported into it keeps its padding.
            if (fileBytes.size() <= DESMUME_FOOTER_SIZE || !endsWith(fileBytes, DESMUME_FOOTER_SIGNATURE))
                return false;
            footerSize = DESMUME_FOOTER_SIZE;
            break;
        case SaveEnvelopeKind::ActionReplayHeader:
            // Nothing in the header is checked, as in PKHeX: no other save is this size, and the probe
            // chain still has to accept what is left.
            if (fileBytes.size() != NINTENDO_DS_SAVE_SIZE + ACTION_REPLAY_HEADER_SIZE)
                return false;
            headerSize = ACTION_REPLAY_HEADER_SIZE;
            break;
        case SaveEnvelopeKind::SwitchOnlineHeader:
            headerSize = switchOnlineHeaderSize(fileBytes);
            if (headerSize == 0)
                return false;
            break;
        case SaveEnvelopeKind::TgbDualClockFooter:
            if (!isSaveSizePlus(fileBytes.size(), TGB_DUAL_CLOCK_FOOTER_SIZE, GAME_BOY_SAVE_SIZES))
                return false;
            footerSize = TGB_DUAL_CLOCK_FOOTER_SIZE;
            break;
        case SaveEnvelopeKind::MisterClockSector:
            if (!isSaveSizePlus(fileBytes.size(), MISTER_CLOCK_SECTOR_SIZE, MISTER_SAVE_SIZES))
                return false;
            footerSize = MISTER_CLOCK_SECTOR_SIZE;
            break;
        case SaveEnvelopeKind::MeteorSizeFooter:
            if (fileBytes.size() != GBA_SAVE_SIZE + METEOR_SIZE_FOOTER_SIZE ||
                readUInt32At(fileBytes, GBA_SAVE_SIZE) != GBA_SAVE_SIZE)
                return false;
            footerSize = METEOR_SIZE_FOOTER_SIZE;
            break;
        case SaveEnvelopeKind::BizHawkBatteryHeader:
        {
            if (fileBytes.size() < BIZHAWK_BATTERY_HEADER_SIZE || !startsWith(fileBytes, BIZHAWK_BATTERY_MAGIC))
                return false;
            // The flash is the save; the EEPROM after it is space a flash game never touches.
            const size_t flashLength = readUInt32At(fileBytes, 8);
            const size_t eepromLength = readUInt32At(fileBytes, 12);
            if (fileBytes.size() != BIZHAWK_BATTERY_HEADER_SIZE + flashLength + eepromLength)
                return false;
            headerSize = BIZHAWK_BATTERY_HEADER_SIZE;
            footerSize = eepromLength;
            break;
        }
        case SaveEnvelopeKind::ActionReplayMaxDriveHeader:
            if (fileBytes.size() != NINTENDO_DS_SAVE_SIZE + ACTION_REPLAY_MAX_DRIVE_HEADER_SIZE ||
                !startsWith(fileBytes, ACTION_REPLAY_MAX_DRIVE_MAGIC))
                return false;
            headerSize = ACTION_REPLAY_MAX_DRIVE_HEADER_SIZE;
            break;
        case SaveEnvelopeKind::NocashGbaContainer:
            return splitNocashGbaContainer(fileBytes, envelope, saveImage);
        }

        envelope.kind = kind;
        envelope.headerBytes.assign(fileBytes.begin(), fileBytes.begin() + static_cast<std::ptrdiff_t>(headerSize));
        envelope.paddingBytes.clear();
        envelope.footerBytes.assign(fileBytes.end() - static_cast<std::ptrdiff_t>(footerSize), fileBytes.end());
        envelope.nocashSaveEntry = NocashSaveEntry{};
        saveImage.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(headerSize),
                         fileBytes.end() - static_cast<std::ptrdiff_t>(footerSize));
        return true;
    }

    std::vector<uint8_t> joinSaveEnvelope(const SaveEnvelope &envelope, const std::vector<uint8_t> &saveImage)
    {
        std::vector<uint8_t> heldBytes = saveImage;
        heldBytes.insert(heldBytes.end(), envelope.paddingBytes.begin(), envelope.paddingBytes.end());
        if (envelope.kind == SaveEnvelopeKind::NocashGbaContainer)
            return joinNocashGbaContainer(envelope, heldBytes);

        std::vector<uint8_t> headerBytes = envelope.headerBytes;
        if (envelope.kind == SaveEnvelopeKind::SwitchOnlineHeader && headerBytes.size() >= SWITCH_ONLINE_HASH_TEXT_SIZE)
            writeSwitchOnlineSaveHash(headerBytes, heldBytes);

        std::vector<uint8_t> fileBytes;
        fileBytes.reserve(headerBytes.size() + heldBytes.size() + envelope.footerBytes.size());
        fileBytes.insert(fileBytes.end(), headerBytes.begin(), headerBytes.end());
        fileBytes.insert(fileBytes.end(), heldBytes.begin(), heldBytes.end());
        fileBytes.insert(fileBytes.end(), envelope.footerBytes.begin(), envelope.footerBytes.end());
        return fileBytes;
    }

    bool findSaveEnvelope(const std::vector<uint8_t> &fileBytes, bool (*isSaveImage)(const std::vector<uint8_t> &),
                          SaveEnvelope &envelope, std::vector<uint8_t> &saveImage)
    {
        // Padding is the last thing tried: taking it off first would let a uniform tail that is really a
        // clock footer full of zeros be named padding -- harmless, since both are kept, but wrong in the log.
        for (const bool takePaddingOff : {false, true})
        {
            for (size_t kindIndex = 0; kindIndex <= std::size(SAVE_ENVELOPE_KINDS); ++kindIndex)
            {
                const SaveEnvelopeKind kind =
                    kindIndex == 0 ? SaveEnvelopeKind::None : SAVE_ENVELOPE_KINDS[kindIndex - 1];
                SaveEnvelope candidateEnvelope;
                std::vector<uint8_t> heldBytes;
                if (!splitSaveEnvelope(kind, fileBytes, candidateEnvelope, heldBytes))
                    continue;
                if (!takePaddingOff)
                {
                    if (!isSaveImage(heldBytes))
                        continue;
                    envelope = std::move(candidateEnvelope);
                    saveImage = std::move(heldBytes);
                    return true;
                }
                for (const size_t saveSize : PADDED_SAVE_SIZES)
                {
                    if (!isPaddingAfter(heldBytes, saveSize))
                        continue;
                    std::vector<uint8_t> candidateImage(heldBytes.begin(),
                                                        heldBytes.begin() + static_cast<std::ptrdiff_t>(saveSize));
                    if (!isSaveImage(candidateImage))
                        continue;
                    candidateEnvelope.paddingBytes.assign(heldBytes.begin() + static_cast<std::ptrdiff_t>(saveSize),
                                                          heldBytes.end());
                    envelope = std::move(candidateEnvelope);
                    saveImage = std::move(candidateImage);
                    return true;
                }
            }
        }
        return false;
    }

    bool isBareSave(const SaveEnvelope &envelope) noexcept
    {
        return envelope.kind == SaveEnvelopeKind::None && envelope.paddingBytes.empty();
    }

    const char *saveEnvelopeName(SaveEnvelopeKind kind) noexcept
    {
        switch (kind)
        {
        case SaveEnvelopeKind::None:
            return "no envelope";
        case SaveEnvelopeKind::RtcFooter:
            return "a real-time clock footer";
        case SaveEnvelopeKind::LibretroGbaSaveRam:
            return "VBA Next / Beetle GBA save RAM";
        case SaveEnvelopeKind::DesmumeFooter:
            return "a DeSmuME footer";
        case SaveEnvelopeKind::ActionReplayHeader:
            return "an Action Replay DSi header";
        case SaveEnvelopeKind::SwitchOnlineHeader:
            return "a Nintendo Switch Online header";
        case SaveEnvelopeKind::TgbDualClockFooter:
            return "a TGB Dual clock footer";
        case SaveEnvelopeKind::MisterClockSector:
            return "a MiSTer clock sector";
        case SaveEnvelopeKind::MeteorSizeFooter:
            return "meteor save RAM";
        case SaveEnvelopeKind::BizHawkBatteryHeader:
            return "a BizHawk 1.x battery header";
        case SaveEnvelopeKind::ActionReplayMaxDriveHeader:
            return "an Action Replay DS / MAX Drive header";
        case SaveEnvelopeKind::NocashGbaContainer:
            return "a no$gba save file";
        }
        return "an unknown envelope";
    }

    const char *describeUnreadableSaveContainer(const std::vector<uint8_t> &fileBytes) noexcept
    {
        if (startsWith(fileBytes, RETROARCH_RZIP_MAGIC))
            return "was compressed by RetroArch. Turn off Settings > Saving > SaveRAM Compression, then save in the "
                   "game again";
        if (startsWith(fileBytes, NOCASH_GBA_MAGIC) &&
            startsWith(fileBytes, NOCASH_SAVE_ENTRY_ID, NOCASH_GBA_HEADER_SIZE) &&
            fileBytes.size() >= NOCASH_GBA_HEADER_SIZE + NOCASH_ENTRY_HEADER_SIZE &&
            readUInt32At(fileBytes, NOCASH_GBA_HEADER_SIZE + 4) == NOCASH_LZ)
            return "was compressed by no$gba. Set its SAV/SNA File Format option to Uncompressed, then save in the "
                   "game again";
        if (fileBytes.size() >= 4 && std::find(std::begin(GOOMBA_STATE_IDS), std::end(GOOMBA_STATE_IDS),
                                               readUInt32At(fileBytes, 0)) != std::end(GOOMBA_STATE_IDS))
            return "is a Goomba Color save, which keeps the game's save compressed. PKSE cannot read those yet";
        if (startsWith(fileBytes, RETRON_5_MAGIC))
            return "is a Retron 5 save, which keeps the game's save compressed. PKSE cannot read those yet";
        if (startsWith(fileBytes, ZIP_MAGIC))
            return "is a ZIP archive. Extract the save from it, then open that";
        if (startsWith(fileBytes, DISA_MAGIC, DISA_MAGIC_OFFSET))
            return "is a whole 3DS save container. Export the save with Checkpoint or JKSM and open its main file";
        return nullptr;
    }
}
