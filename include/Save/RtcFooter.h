/**
 * Gold, Silver and Crystal (and Gen 3) have a real-time clock, and emulators, flashcarts and
 * dumpers append its state after the save proper. The save itself is unchanged; the file is just
 * longer. Every size check in the loose-save probe chain is EXACT (0x8000, 0x10000, 0x20000), so
 * such a file matched nothing at all and PKSE reported "not a save PKSE can open" for a perfectly
 * good Silver save, everywhere the probe chain runs.
 *
 * The rule is PKHeX's `SaveHandlerFooterRTC` verbatim, tolerances included: the footer is the low
 * six bits of the length, it must be either 7 bytes (one flashcart writes an odd one) or an even
 * 0x0C..0x30, and what remains must be a size a Game Boy or GBA save actually is. Guessing more
 * loosely than that would start truncating files that are merely corrupt, which is worse than
 * refusing them -- a truncated save still parses, and then quietly holds the wrong data.
 *
 * Lives in its own header so the rule has ONE home and can be exercised without the Switch SDK:
 * GetSaveFileContents.cpp reaches it through <switch.h>, an off-console build does not.
 */
#ifndef SAVE_RTC_FOOTER_H
#define SAVE_RTC_FOOTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Save
{
    /// How many trailing bytes of a file of this length are an RTC footer; 0 when none are.
    ///
    /// The rule lives here alone because two places need the same answer: opening a save trims the
    /// footer off, and writing one back puts it BACK. They must agree byte for byte or the save
    /// grows or shrinks a little on every round trip.
    inline size_t rtcFooterLength(size_t fileSize) noexcept
    {
        constexpr size_t MIN_RTC_FOOTER_SIZE = 0x0C, MAX_RTC_FOOTER_SIZE = 0x30;
        const size_t footerSize = fileSize & 0x3F;
        if (footerSize == 0)
            return 0;
        const bool plausible = (footerSize == 0x07) || ((footerSize % 2) == 0 && footerSize >= MIN_RTC_FOOTER_SIZE && footerSize <= MAX_RTC_FOOTER_SIZE);
        if (!plausible)
            return 0;
        const size_t withoutFooter = fileSize - footerSize;
        // 0x8000 is Gen 1 and international Gen 2; 0x10000 Japanese Gen 2; 0x20000 Gen 3.
        if (withoutFooter != 0x8000 && withoutFooter != 0x10000 && withoutFooter != 0x20000)
            return 0;
        return footerSize;
    }

    /// Trims an RTC footer in place. Returns true when the file shrank.
    ///
    /// Call it only when nothing recognised the file as it stands: a save that already matches a
    /// size exactly is never trimmed, so this can neither shorten a good file nor reorder the probe
    /// chain -- it just gives a footered save the same chance a bare one gets.
    inline bool stripRtcFooter(std::vector<uint8_t> &bytes)
    {
        const size_t footerSize = rtcFooterLength(bytes.size());
        if (footerSize == 0)
            return false;
        bytes.resize(bytes.size() - footerSize);
        return true;
    }
}

#endif
