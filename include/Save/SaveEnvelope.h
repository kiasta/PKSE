/**
 * The bytes an emulator, a flashcart, a cartridge dumper or an online service writes around a save. The
 * save inside is the game's own and is untouched; the file is just longer, starts later, or -- for
 * no$gba -- stores it run-length coded. Every size check in the loose-save probe chain is EXACT, so a save
 * in an envelope matches nothing until the envelope is taken off -- and it has to go back on when the file
 * is written, or the program that wrote it no longer reads its own save.
 *
 * The first kinds are PKHeX's save handlers (SaveHandlerFooterRTC, SaveHandlerDeSmuME, SaveHandlerARDS,
 * SaveHandlerNSO). Everything after them PKHeX refuses, and each was read out of the source of the program
 * that writes it -- docs/manual/05-save-pipeline.md has the table, with the line each rule came from.
 *
 * PADDING IS NOT A KIND, because it can sit inside any of them. A flashcart or a dumper hands back the
 * whole chip, and a loader that grows a file never shrinks it, so the save can be followed by space
 * nothing ever wrote -- all 00 or all FF, which is what unwritten flash reads back as. It is taken off
 * only when it is that uniform, and only after every kind has failed on the file as it stands, so it can
 * never shorten a save that opens without it. YSMenu stores its per-game flags in a file's last 8 bytes
 * ("NMSY"), so padding may end with those.
 *
 * Lives apart from the Switch SDK for the reason RtcFooter.h does: the rules can be tested off-console.
 */
#ifndef SAVE_SAVEENVELOPE_H
#define SAVE_SAVEENVELOPE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Save
{
    enum class SaveEnvelopeKind
    {
        None,                       // nothing around the save (padding, if any, is SaveEnvelope::paddingBytes)
        RtcFooter,                  // a Game Boy or GBA real-time clock's state after the save
        LibretroGbaSaveRam,         // RetroArch's VBA Next and Beetle GBA: the flash save, then 8 KiB of EEPROM
        DesmumeFooter,              // DeSmuME and DraStic .dsv: the DS save, then a footer ending "|-DESMUME SAVE-|"
        ActionReplayHeader,         // Action Replay DSi .duc: a 0xA4-byte header, then the DS save
        SwitchOnlineHeader,         // Nintendo Switch Online's Game Boy app: a header holding a SHA-1 of the save
        TgbDualClockFooter,         // TGB Dual for Windows: a Game Boy save, then its clock's offset in 4 bytes
        MisterClockSector,          // MiSTer's Game Boy and GBA cores: the save, then one 512-byte clock sector
        MeteorSizeFooter,           // RetroArch's meteor core: the GBA save, then its own size in 4 bytes
        BizHawkBatteryHeader,       // BizHawk 1.x's GBA core: "GBABATT", two sizes, the flash save, then EEPROM
        ActionReplayMaxDriveHeader, // Action Replay DS and MAX Drive DS: a 500-byte "ARDS000000000001" header
        NocashGbaContainer,         // no$gba: a header, then the save in an "SRAM" entry, as is or run-length coded
    };

    /// Every envelope a file is tried against when it is not a save as it stands: PKHeX's order for its
    /// own four, then the rest. Each is recognised by an exact size or a magic, so no two can claim the
    /// same file and the order decides nothing but which is tried first.
    inline constexpr SaveEnvelopeKind SAVE_ENVELOPE_KINDS[] = {
        SaveEnvelopeKind::RtcFooter,          SaveEnvelopeKind::LibretroGbaSaveRam,
        SaveEnvelopeKind::DesmumeFooter,      SaveEnvelopeKind::ActionReplayHeader,
        SaveEnvelopeKind::SwitchOnlineHeader, SaveEnvelopeKind::TgbDualClockFooter,
        SaveEnvelopeKind::MisterClockSector,  SaveEnvelopeKind::MeteorSizeFooter,
        SaveEnvelopeKind::BizHawkBatteryHeader, SaveEnvelopeKind::ActionReplayMaxDriveHeader,
        SaveEnvelopeKind::NocashGbaContainer,
    };

    /// How no$gba stored the save inside its "SRAM" entry, kept so the file is written back the same way.
    struct NocashSaveEntry
    {
        /// 0: the bytes as they are. 1: no$gba's run-length code. (2, LZ, is refused: see
        /// describeUnreadableSaveContainer.)
        uint32_t compressionMethod = 0;
        /// How many bytes of the save the entry held. no$gba allocates save memory as the game touches
        /// it, so this can be shorter than the save; the rest reads as FF, as unwritten flash does.
        size_t storedSaveLength = 0;
        /// Whether a run-length entry's length counts the u32 of unpacked length in front of the code.
        /// no$gba's own loader reads entry lengths that way; DeSmuME's import accepts either.
        bool lengthCountsUnpackedLength = true;
    };

    struct SaveEnvelope
    {
        SaveEnvelopeKind kind = SaveEnvelopeKind::None;
        std::vector<uint8_t> headerBytes;  // before the save (no$gba: the file header, up to the first entry)
        std::vector<uint8_t> paddingBytes; // straight after the save: space nothing wrote, see above
        std::vector<uint8_t> footerBytes;  // after that (no$gba: every entry after "SRAM")
        NocashSaveEntry nocashSaveEntry;   // NocashGbaContainer only
    };

    /// Splits `fileBytes` into the envelope `kind` writes and what it holds -- the save, or the save and
    /// its padding. False when the file is not shaped that way, which says nothing about whether the
    /// inside is a save.
    bool splitSaveEnvelope(SaveEnvelopeKind kind, const std::vector<uint8_t> &fileBytes, SaveEnvelope &envelope,
                           std::vector<uint8_t> &saveImage);

    /// The file again, around `saveImage` and the envelope's padding. A Switch Online header carries a
    /// SHA-1 of the save the service checks, so it is taken again over the new save rather than copied;
    /// no$gba's entry is coded again the way it was.
    std::vector<uint8_t> joinSaveEnvelope(const SaveEnvelope &envelope, const std::vector<uint8_t> &saveImage);

    /// The file as it stands first, then each envelope in turn, then each again with padding taken off
    /// what it holds; the first save `isSaveImage` accepts wins. Never the other way round: an envelope is
    /// tried only on a file that is not a save, so it can neither shorten a good save nor reorder the
    /// probe chain.
    bool findSaveEnvelope(const std::vector<uint8_t> &fileBytes, bool (*isSaveImage)(const std::vector<uint8_t> &),
                          SaveEnvelope &envelope, std::vector<uint8_t> &saveImage);

    /// True when the file is the save and nothing else, so there is nothing to put back.
    bool isBareSave(const SaveEnvelope &envelope) noexcept;

    /// What wrote the envelope, for the log.
    const char *saveEnvelopeName(SaveEnvelopeKind kind) noexcept;

    /// Why a file PKSE recognises cannot be opened, worded to follow its name ("Ruby.srm" + this):
    /// RetroArch's SaveRAM compression, no$gba's LZ saves, Goomba Color, Retron 5, a ZIP archive, a whole
    /// 3DS save container, or half a GBA save. Each needs something PKSE does not have -- a decompressor,
    /// or the bytes the file lost -- so the honest answer is what the user can change. nullptr otherwise.
    const char *describeUnreadableSaveContainer(const std::vector<uint8_t> &fileBytes) noexcept;
}

#endif // SAVE_SAVEENVELOPE_H
