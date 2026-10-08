/**
 * Two places needed the same answer and they had drifted apart. The file browser filtered on
 * `.sav`, `.srm`, `.dat` and `.sgm`; a second scan elsewhere used those four AND the bare name
 * `main`. So a Gen 6 or Gen 7 3DS save -- which is called `main` and has no extension at all --
 * was reachable one way and INVISIBLE in the picker the user actually browses with, even though
 * openExternalSave opens it perfectly well. X ("show all files") revealed it, which is the tell:
 * the file was always openable, it was just never offered.
 *
 * This is a NAME test and nothing more. It decides what to put on screen, cheaply, before
 * anything is read; whether the bytes really are a save is `Save::openExternalSave`'s question and
 * is asked after the user picks. A name that passes here and fails there gets an explained
 * refusal with the file still listed, which is the intended flow.
 *
 * WHAT IS DELIBERATELY ABSENT. Let's Go's `savedata.bin`, BDSP's `SaveData.bin`, Sword/Shield's
 * and Scarlet/Violet's `backup`, and Legends: Arceus's `main2` are all real save-adjacent files,
 * and none of them belongs here: those are Switch titles, they arrive through an installed
 * title id rather than the browser, and none has a row in LOOSE_FORMATS. Listing a file the
 * picker must then refuse is worse than not listing it.
 *
 * Lives in its own header for the reason RtcFooter.h does: the rule has ONE home, and it needs
 * no Switch SDK, so anything off-console can ask the same question the console does.
 */
#ifndef SAVE_SAVEFILENAME_H
#define SAVE_SAVEFILENAME_H

#include <string>
#include <string_view>
#include <vector>

namespace Save
{
    /// Extensions a loose save is commonly given, lowercase and including the dot. Emulators,
    /// cartridge dumpers and save managers each pick their own, so this is a list of habits
    /// rather than a specification -- `00000001.sav`, `sav.dat` and `Pokemon Black.sav` are all
    /// real names of real saves. Several wrap the save in an envelope PKSE takes off
    /// (Save/SaveEnvelope.h): `.dsv` is DeSmuME's and DraStic's, `.duc` and `.dss` Action Replay DS
    /// and MAX Drive dumps. `.fla` and `.saveram` (BizHawk) are in PKHeX's list too. The rest are
    /// a second player's or a second slot's save: `.sa2`-`.sa4` from mGBA's multiplayer and TGB
    /// Dual, `.srm2` from Gearboy's link mode, `.sav1`-`.sav9` from TWiLight Menu++'s save slots.
    /// A DraStic save state is also `.dss`; it is listed, then refused with the file still on screen.
    inline constexpr std::string_view SAVE_FILE_EXTENSIONS[] = {
        ".sav",  ".srm",  ".dat",  ".sgm",  ".dsv",  ".duc",  ".dss",  ".fla",  ".saveram",
        ".sa2",  ".sa3",  ".sa4",  ".srm2", ".sav1", ".sav2", ".sav3", ".sav4", ".sav5",
        ".sav6", ".sav7", ".sav8", ".sav9"};

    /// Whole filenames that carry no extension. A 3DS save is just `main` -- Gen 6's X/Y and
    /// Omega Ruby/Alpha Sapphire, and Gen 7's Sun/Moon and Ultra Sun/Ultra Moon, every one of
    /// them. Matched in full, never as a suffix: a suffix test would also take `domain` and
    /// would still miss nothing it needs to.
    inline constexpr std::string_view SAVE_FILE_EXACT_NAMES[] = {"main"};

    /// Lowercases one ASCII filename. Save filenames are ASCII in practice, and the comparison
    /// only has to be stable rather than locale-correct.
    inline std::string toLowerFileName(std::string_view name)
    {
        std::string lowered(name);
        for (char &character : lowered)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return lowered;
    }

    /// The same two lists as vectors, for callers that take them that way (the file browser).
    /// They exist so nothing has to retype the lists to hand them on -- a second spelling of
    /// this rule is exactly what let two callers disagree about `main`.
    inline std::vector<std::string> saveFileExtensions()
    {
        return {std::begin(SAVE_FILE_EXTENSIONS), std::end(SAVE_FILE_EXTENSIONS)};
    }

    inline std::vector<std::string> saveFileExactNames()
    {
        return {std::begin(SAVE_FILE_EXACT_NAMES), std::end(SAVE_FILE_EXACT_NAMES)};
    }

    /// True when `name` is worth offering as a loose save: it ends in one of the extensions
    /// above, or it IS one of the extensionless names above. Case-insensitive both ways, so a
    /// dumper that wrote `MAIN` or `POKEMON_P.SAV` is not silently skipped.
    inline bool isSaveFileName(std::string_view name)
    {
        const std::string lowered = toLowerFileName(name);
        for (const std::string_view exactName : SAVE_FILE_EXACT_NAMES)
        {
            if (lowered == exactName)
            {
                return true;
            }
        }
        for (const std::string_view extension : SAVE_FILE_EXTENSIONS)
        {
            if (lowered.size() >= extension.size() &&
                lowered.compare(lowered.size() - extension.size(), extension.size(), extension) == 0)
            {
                return true;
            }
        }
        return false;
    }
}

#endif  // SAVE_SAVEFILENAME_H
