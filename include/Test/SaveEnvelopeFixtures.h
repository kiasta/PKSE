/**
 * A real save, wrapped the way each emulator, flashcart, dumper or online service writes one. The self
 * test's envelope chunk and the host harness both build their files here, so the console and the host try
 * the same shapes.
 *
 * Only what PKSE reads is faithful: the sizes, the magics, DeSmuME's closing signature, the Switch Online
 * header's layout and no$gba's entries. The rest is a visible filler pattern, which is enough -- every
 * envelope byte PKSE does not interpret has to come back exactly as it went in, and a pattern shows
 * whether it did.
 *
 * Built from the layouts as their writers' source describes them, NOT through joinSaveEnvelope where that
 * can be avoided: a fixture made by the code under test proves only that the code agrees with itself.
 * no$gba's run-length code is the exception -- it is coded here by a second, independent encoder.
 */
#ifndef TEST_SAVEENVELOPEFIXTURES_H
#define TEST_SAVEENVELOPEFIXTURES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

#include "Save/SaveEnvelope.h"

namespace Test
{
    namespace EnvelopeFixture
    {
        constexpr size_t GAME_BOY_SAVE_SIZE = 0x8000;           // Gen 1, international Gen 2
        constexpr size_t JAPANESE_GAME_BOY_SAVE_SIZE = 0x10000; // Japanese Gen 2
        constexpr size_t GBA_SAVE_SIZE = 0x20000;               // Gen 3
        constexpr size_t NINTENDO_DS_SAVE_SIZE = 0x80000;       // Gen 4, Gen 5

        constexpr size_t MGBA_RTC_FOOTER_SIZE = 0x10; // after a GBA save
        constexpr size_t VBAM_RTC_FOOTER_SIZE = 0x30; // BGB and VBA-M, after a Game Boy save
        constexpr size_t LIBRETRO_GBA_EEPROM_SIZE = 0x2000;
        constexpr size_t ACTION_REPLAY_HEADER_SIZE = 0xA4;
        constexpr size_t SWITCH_ONLINE_RTC_SIZE = 0x20;
        constexpr size_t SWITCH_ONLINE_HASH_TEXT_SIZE = 40;
        constexpr size_t TGB_DUAL_CLOCK_FOOTER_SIZE = 4;
        constexpr size_t MISTER_CLOCK_SECTOR_SIZE = 0x200;
        constexpr size_t BIZHAWK_EEPROM_SIZE = 0x2000; // its "auto" save type keeps 8 KiB of EEPROM too
        constexpr size_t ACTION_REPLAY_MAX_DRIVE_HEADER_SIZE = 500;
        constexpr size_t NOCASH_GBA_HEADER_SIZE = 0x40;
        constexpr size_t YSMENU_FLAGS_SIZE = 8;

        inline std::vector<uint8_t> filler(size_t byteCount)
        {
            std::vector<uint8_t> fillerBytes(byteCount);
            for (size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex)
                fillerBytes[byteIndex] = static_cast<uint8_t>(0xA5 ^ byteIndex);
            return fillerBytes;
        }

        inline void appendText(std::vector<uint8_t> &envelopeBytes, std::string_view text)
        {
            envelopeBytes.insert(envelopeBytes.end(), text.begin(), text.end());
        }

        inline void appendUInt32LittleEndian(std::vector<uint8_t> &envelopeBytes, uint32_t fieldValue)
        {
            for (int shift = 0; shift < 32; shift += 8)
                envelopeBytes.push_back(static_cast<uint8_t>(fieldValue >> shift));
        }

        inline void appendBytes(std::vector<uint8_t> &envelopeBytes, const std::vector<uint8_t> &appendedBytes)
        {
            envelopeBytes.insert(envelopeBytes.end(), appendedBytes.begin(), appendedBytes.end());
        }

        /// DeSmuME's mc.cpp: a note, six u32 fields (sizes, type, address width, version), the cookie.
        inline std::vector<uint8_t> desmumeFooter(size_t heldSize)
        {
            std::vector<uint8_t> footerBytes;
            appendText(footerBytes, "|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:");
            const uint32_t saveSize = static_cast<uint32_t>(heldSize);
            for (const uint32_t field : {saveSize, saveSize, 0u, 3u, saveSize, 0u})
                appendUInt32LittleEndian(footerBytes, field);
            appendText(footerBytes, "|-DESMUME SAVE-|");
            return footerBytes;
        }

        /// PKHeX HeaderInfoNSO's layout. The hash is left blank: joinSaveEnvelope fills it in.
        inline std::vector<uint8_t> switchOnlineHeader(bool withClock)
        {
            std::vector<uint8_t> headerBytes;
            appendText(headerBytes, "SRAM");
            appendUInt32LittleEndian(headerBytes, 3);
            appendText(headerBytes, "00112233445566778899aabbccddeeff00112233"); // the ROM's SHA-1
            constexpr std::string_view build = "1.2.0+nso-test";
            appendUInt32LittleEndian(headerBytes, static_cast<uint32_t>(build.size()));
            appendText(headerBytes, build);
            headerBytes.push_back(withClock ? 1 : 0);
            if (withClock)
                appendBytes(headerBytes, filler(SWITCH_ONLINE_RTC_SIZE));
            headerBytes.insert(headerBytes.end(), SWITCH_ONLINE_HASH_TEXT_SIZE, 0);
            return headerBytes;
        }

        /// MiSTer's Game Boy core: a u32 Unix time, the packed clock, then FF to the end of the sector.
        /// Its GBA core instead repeats a 16-byte record opening "TR" -- either way, kept as it is.
        inline std::vector<uint8_t> misterClockSector()
        {
            std::vector<uint8_t> sectorBytes = filler(10);
            sectorBytes.resize(MISTER_CLOCK_SECTOR_SIZE, 0xFF);
            return sectorBytes;
        }

        /// no$gba's run-length code for `savedBytes`, written as plainly as possible -- every repeat as a
        /// long repeat (80, value, u16 count), everything else one-byte copies -- so it shares no choices
        /// with Save's greedy encoder and both have to agree only on what the code MEANS.
        inline std::vector<uint8_t> nocashRunLengthCode(const std::vector<uint8_t> &savedBytes)
        {
            std::vector<uint8_t> codeBytes;
            size_t readOffset = 0;
            while (readOffset < savedBytes.size())
            {
                size_t repeatCount = 1;
                while (readOffset + repeatCount < savedBytes.size() && repeatCount < 0xFFFF && savedBytes[readOffset + repeatCount] == savedBytes[readOffset])
                    ++repeatCount;
                if (repeatCount >= 8)
                {
                    codeBytes.push_back(0x80);
                    codeBytes.push_back(savedBytes[readOffset]);
                    codeBytes.push_back(static_cast<uint8_t>(repeatCount & 0xFF));
                    codeBytes.push_back(static_cast<uint8_t>(repeatCount >> 8));
                    readOffset += repeatCount;
                    continue;
                }
                codeBytes.push_back(0x01);
                codeBytes.push_back(savedBytes[readOffset]);
                ++readOffset;
            }
            codeBytes.push_back(0x00);
            return codeBytes;
        }

        /// Reads a no$gba file's run-length "SRAM" entry back, written apart from Save's decoder for the
        /// reason the encoder above is: so what PKSE writes is read by something that is not PKSE. Empty
        /// when the file is not a run-length coded no$gba file or the code does not decode to its length.
        inline std::vector<uint8_t> nocashRunLengthDecode(const std::vector<uint8_t> &fileBytes)
        {
            constexpr size_t codeStart = NOCASH_GBA_HEADER_SIZE + 16; // "SRAM", method, length, unpacked length
            const auto fieldAt = [&](size_t fieldOffset)
            {
                return static_cast<size_t>(fileBytes[fieldOffset]) | static_cast<size_t>(fileBytes[fieldOffset + 1]) << 8 | static_cast<size_t>(fileBytes[fieldOffset + 2]) << 16 | static_cast<size_t>(fileBytes[fieldOffset + 3]) << 24;
            };
            if (fileBytes.size() < codeStart || fieldAt(NOCASH_GBA_HEADER_SIZE + 4) != 1)
                return {};
            const size_t unpackedLength = fieldAt(NOCASH_GBA_HEADER_SIZE + 12);
            std::vector<uint8_t> decodedBytes;
            for (size_t readOffset = codeStart; readOffset < fileBytes.size();)
            {
                const uint8_t control = fileBytes[readOffset];
                if (control == 0x00)
                    return decodedBytes.size() == unpackedLength ? decodedBytes : std::vector<uint8_t>{};
                if (control < 0x80 && readOffset + 1 + control <= fileBytes.size())
                {
                    const auto copyStart = fileBytes.begin() + static_cast<std::ptrdiff_t>(readOffset + 1);
                    decodedBytes.insert(decodedBytes.end(), copyStart, copyStart + control);
                    readOffset += 1 + control;
                }
                else if (control == 0x80 && readOffset + 4 <= fileBytes.size())
                {
                    const size_t repeatCount = fileBytes[readOffset + 2] | fileBytes[readOffset + 3] << 8;
                    decodedBytes.insert(decodedBytes.end(), repeatCount, fileBytes[readOffset + 1]);
                    readOffset += 4;
                }
                else if (control > 0x80 && readOffset + 2 <= fileBytes.size())
                {
                    const size_t repeatCount = static_cast<size_t>(control - 0x80);
                    decodedBytes.insert(decodedBytes.end(), repeatCount, fileBytes[readOffset + 1]);
                    readOffset += 2;
                }
                else
                {
                    return {};
                }
                if (decodedBytes.size() > unpackedLength)
                    return {};
            }
            return {};
        }

        /// no$gba's battery file, as its own writer lays one out: the magic, the date, the "SRAM" entry,
        /// a "LIFE" entry of flash wear counters, then "STOP". `runLengthCoded` picks "Compressed
        /// (fast/rlu)" over "Uncompressed".
        inline std::vector<uint8_t> nocashGbaFile(const std::vector<uint8_t> &heldBytes, bool runLengthCoded)
        {
            std::vector<uint8_t> fileBytes;
            appendText(fileBytes, std::string_view("NocashGbaBackupMediaSavDataFile\x1A", 32));
            appendBytes(fileBytes, {0x36, 0x00, 0x03, 0x00, 0x04, 0x0A, 0xEA, 0x07, 0x14, 0x1E});
            fileBytes.resize(NOCASH_GBA_HEADER_SIZE, 0x00);

            appendText(fileBytes, "SRAM");
            appendUInt32LittleEndian(fileBytes, runLengthCoded ? 1 : 0);
            if (runLengthCoded)
            {
                const std::vector<uint8_t> codeBytes = nocashRunLengthCode(heldBytes);
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(4 + codeBytes.size()));
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(heldBytes.size()));
                appendBytes(fileBytes, codeBytes);
            }
            else
            {
                appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(heldBytes.size()));
                appendBytes(fileBytes, heldBytes);
            }

            appendText(fileBytes, "LIFE");
            appendUInt32LittleEndian(fileBytes, 0);
            appendUInt32LittleEndian(fileBytes, 0x20);
            appendBytes(fileBytes, filler(0x20));
            appendText(fileBytes, "STOP");
            appendUInt32LittleEndian(fileBytes, 0);
            appendUInt32LittleEndian(fileBytes, 0);
            return fileBytes;
        }
    }

    /// The envelopes a save of this size really turns up in.
    inline std::vector<Save::SaveEnvelopeKind> envelopesForSaveSize(size_t saveImageSize)
    {
        using Save::SaveEnvelopeKind;
        switch (saveImageSize)
        {
        case EnvelopeFixture::GAME_BOY_SAVE_SIZE:
        case EnvelopeFixture::JAPANESE_GAME_BOY_SAVE_SIZE:
            return {SaveEnvelopeKind::RtcFooter, SaveEnvelopeKind::SwitchOnlineHeader,
                    SaveEnvelopeKind::TgbDualClockFooter, SaveEnvelopeKind::MisterClockSector};
        case EnvelopeFixture::GBA_SAVE_SIZE:
            return {SaveEnvelopeKind::RtcFooter,         SaveEnvelopeKind::LibretroGbaSaveRam,
                    SaveEnvelopeKind::MisterClockSector, SaveEnvelopeKind::MeteorSizeFooter,
                    SaveEnvelopeKind::BizHawkBatteryHeader, SaveEnvelopeKind::NocashGbaContainer};
        case EnvelopeFixture::NINTENDO_DS_SAVE_SIZE:
            return {SaveEnvelopeKind::DesmumeFooter, SaveEnvelopeKind::ActionReplayHeader,
                    SaveEnvelopeKind::ActionReplayMaxDriveHeader, SaveEnvelopeKind::NocashGbaContainer};
        default:
            return {};
        }
    }

    /// The layouts `kind` is built in, as buildEnvelopedSave's `alternateLayout`: a Switch Online header
    /// with and without a clock, a no$gba file run-length coded and stored as it is. Every other kind has one.
    inline std::vector<bool> envelopeLayouts(Save::SaveEnvelopeKind kind)
    {
        if (kind == Save::SaveEnvelopeKind::SwitchOnlineHeader || kind == Save::SaveEnvelopeKind::NocashGbaContainer)
            return {false, true};
        return {false};
    }

    /// Padding a save of this size really turns up with, and in what: GBE+ pads a Game Boy save to
    /// 128 KiB, a YSMenu kernel makes a DS save 1 MiB and writes its flags at the end, and DeSmuME keeps
    /// that 1 MiB when one is imported into it.
    struct PaddingFixture
    {
        size_t paddedSize;
        uint8_t fillByte;
        bool withYsMenuFlags;
        Save::SaveEnvelopeKind kind;
    };

    inline std::vector<PaddingFixture> paddingForSaveSize(size_t saveImageSize)
    {
        using Save::SaveEnvelopeKind;
        switch (saveImageSize)
        {
        case EnvelopeFixture::GAME_BOY_SAVE_SIZE:
        case EnvelopeFixture::JAPANESE_GAME_BOY_SAVE_SIZE:
            return {{EnvelopeFixture::GBA_SAVE_SIZE, 0x00, false, SaveEnvelopeKind::None}};
        case EnvelopeFixture::GBA_SAVE_SIZE:
            return {{EnvelopeFixture::GBA_SAVE_SIZE * 2, 0xFF, false, SaveEnvelopeKind::None}};
        case EnvelopeFixture::NINTENDO_DS_SAVE_SIZE:
            return {{EnvelopeFixture::NINTENDO_DS_SAVE_SIZE * 2, 0xFF, true, SaveEnvelopeKind::None},
                    {EnvelopeFixture::NINTENDO_DS_SAVE_SIZE * 2, 0x00, false, SaveEnvelopeKind::DesmumeFooter}};
        default:
            return {};
        }
    }

    /// `saveImage`, then the padding `fixture` describes -- the bytes a padded envelope holds.
    inline std::vector<uint8_t> paddedSaveBytes(const PaddingFixture &fixture, const std::vector<uint8_t> &saveImage)
    {
        std::vector<uint8_t> heldBytes = saveImage;
        heldBytes.resize(fixture.paddedSize, fixture.fillByte);
        if (fixture.withYsMenuFlags)
        {
            const uint8_t ysMenuFlags[EnvelopeFixture::YSMENU_FLAGS_SIZE] = {0x00, 0x00, 0x0F, 0x00,
                                                                             'N',  'M',  'S',  'Y'};
            std::copy(std::begin(ysMenuFlags), std::end(ysMenuFlags), heldBytes.end() - static_cast<std::ptrdiff_t>(EnvelopeFixture::YSMENU_FLAGS_SIZE));
        }
        return heldBytes;
    }

    /// `heldBytes` -- a save, or a save and its padding -- inside `kind`, laid out as the program that
    /// writes that envelope lays it out. `alternateLayout` picks the second layout a kind comes in: a
    /// Switch Online header with a clock (a Gen 2 cartridge has one), or a no$gba file stored as it is
    /// rather than run-length coded. Kinds with one layout ignore it.
    inline std::vector<uint8_t> buildEnvelopedSave(Save::SaveEnvelopeKind kind, const std::vector<uint8_t> &heldBytes, bool alternateLayout = false)
    {
        using namespace EnvelopeFixture;
        std::vector<uint8_t> fileBytes;
        switch (kind)
        {
        case Save::SaveEnvelopeKind::None:
            return heldBytes;
        case Save::SaveEnvelopeKind::RtcFooter:
            fileBytes = heldBytes;
            appendBytes(fileBytes, filler(heldBytes.size() == GBA_SAVE_SIZE ? MGBA_RTC_FOOTER_SIZE : VBAM_RTC_FOOTER_SIZE));
            return fileBytes;
        case Save::SaveEnvelopeKind::LibretroGbaSaveRam:
            fileBytes = heldBytes;
            fileBytes.resize(heldBytes.size() + LIBRETRO_GBA_EEPROM_SIZE, 0xFF); // EEPROM a flash game never writes
            return fileBytes;
        case Save::SaveEnvelopeKind::DesmumeFooter:
            fileBytes = heldBytes;
            appendBytes(fileBytes, desmumeFooter(heldBytes.size()));
            return fileBytes;
        case Save::SaveEnvelopeKind::ActionReplayHeader:
            fileBytes = filler(ACTION_REPLAY_HEADER_SIZE);
            appendBytes(fileBytes, heldBytes);
            return fileBytes;
        case Save::SaveEnvelopeKind::SwitchOnlineHeader:
        {
            // The one header that cannot be built without the code under test: its SHA-1 has to be right.
            Save::SaveEnvelope envelope;
            envelope.kind = kind;
            envelope.headerBytes = switchOnlineHeader(alternateLayout);
            return Save::joinSaveEnvelope(envelope, heldBytes);
        }
        case Save::SaveEnvelopeKind::TgbDualClockFooter:
            fileBytes = heldBytes;
            appendBytes(fileBytes, filler(TGB_DUAL_CLOCK_FOOTER_SIZE));
            return fileBytes;
        case Save::SaveEnvelopeKind::MisterClockSector:
            fileBytes = heldBytes;
            appendBytes(fileBytes, misterClockSector());
            return fileBytes;
        case Save::SaveEnvelopeKind::MeteorSizeFooter:
            fileBytes = heldBytes;
            appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(heldBytes.size()));
            return fileBytes;
        case Save::SaveEnvelopeKind::BizHawkBatteryHeader:
            appendText(fileBytes, std::string_view("GBABATT\0", 8));
            appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(heldBytes.size()));
            appendUInt32LittleEndian(fileBytes, static_cast<uint32_t>(BIZHAWK_EEPROM_SIZE));
            appendBytes(fileBytes, heldBytes);
            fileBytes.resize(fileBytes.size() + BIZHAWK_EEPROM_SIZE, 0xFF);
            return fileBytes;
        case Save::SaveEnvelopeKind::ActionReplayMaxDriveHeader:
            appendText(fileBytes, "ARDS000000000001");
            appendBytes(fileBytes, filler(ACTION_REPLAY_MAX_DRIVE_HEADER_SIZE - fileBytes.size()));
            appendBytes(fileBytes, heldBytes);
            return fileBytes;
        case Save::SaveEnvelopeKind::NocashGbaContainer:
            return nocashGbaFile(heldBytes, !alternateLayout);
        }
        return fileBytes;
    }
}

#endif // TEST_SAVEENVELOPEFIXTURES_H
