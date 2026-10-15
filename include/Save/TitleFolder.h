/**
 * TitleFolder.h - What a Switch title is CALLED, on screen and on the SD card.
 *
 * THREE STRINGS, AND THEY ARE NOT ONE STRING:
 *
 *   titleDisplayLabel() goes under a 184-pixel tile in the save picker. Short.
 *   titleDisplayName()  is prose -- the title bar, and "This replaces X's save data". Readable.
 *   titleFolderName()   is a directory under sdmc:/PKSE holding that title's backups. UNIQUE.
 *
 * They were one string, built from the GameVersion alone, and a GameVersion is not an identity.
 * Gen 3's Switch release ships ONE APPLICATION PER LANGUAGE -- the GBA originals had no in-game
 * language option, so the eShop sells a separate SKU per localisation, twelve ids across the six
 * GBA languages and two games. Every one resolves to the same GameVersion, correctly, because the
 * SAVE is identical. So the picker drew several tiles all reading "LeafGreen" with nothing to
 * choose between them, and every one pointed at sdmc:/PKSE/Pokemon LeafGreen -- opening the French
 * title loaded whatever English bytes were already in that folder, and saving with inject on would
 * have written them into the French title. Nothing failed and nothing logged.
 *
 * THE FOLDER CARRIES THE WHOLE TITLE ID, FOR EVERY TITLE, WITH NO EXCEPTIONS. Not just the ones
 * known to collide -- uniformly, because a rule that applies to the cases somebody has thought of
 * is the rule that broke here. A hand-kept list of somebody else's title ids cannot be complete:
 * an id PKSE has never seen is identified by CONTENT, and content answers with a GROUP, so without
 * the id an Italian FireRed and an Italian LeafGreen collide with each other as well as with every
 * other unlisted Gen 3 SKU. The id is distinct by definition; a name is not, and no amount of
 * curating the name list changes that.
 *
 * The id stays OUT of the two display strings, because it is not something to read -- it is there
 * so the directory cannot lie about which title it belongs to.
 *
 * Lives here rather than inside the picker because the picker and the backup screen both need it,
 * and because a naming rule with two copies is what SaveFileName.h exists to prevent.
 */
#ifndef SAVE_TITLE_FOLDER_H
#define SAVE_TITLE_FOLDER_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"

namespace Save
{
    /// A display name made safe to use as a directory component. Only '/' can appear in a
    /// GameVersion name, and only for a group; everything else is already filesystem-safe.
    inline std::string directorySafeName(const std::string &displayName)
    {
        std::string safeName = displayName;
        for (char &character : safeName)
        {
            if (character == '/')
                character = '-';
        }
        return safeName;
    }

    /// The whole title id, for a name that has to be unique rather than readable.
    inline std::string titleIdText(uint64_t titleId)
    {
        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(titleId));
        return buffer;
    }

    /**
     * Enough of the title id to tell two tiles apart, short enough to fit under one.
     *
     * The folder takes the whole id because it needs a guarantee; a tile has 184 pixels and the
     * full sixteen digits do not fit beside a game name. These are the digits that actually vary
     * between titles: the leading `0100` is shared by every Switch application and the low three
     * nibbles are the save-data type. A hint, not the guarantee -- which is why it is a separate
     * function from titleIdText rather than a substring of it.
     */
    inline std::string titleIdHint(uint64_t titleId)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "#%05llX", static_cast<unsigned long long>((titleId >> 12) & 0xFFFFFull));
        return buffer;
    }

    /// The localisation suffix a per-language SKU carries, or "" for a title that ships one build
    /// worldwide. `gameVersion` is passed in because the caller may have recovered it from CONTENT
    /// when the id was unrecognised, in which case there is no language to name.
    inline std::string titleLanguageSuffix(uint64_t titleId)
    {
        const uint8_t titleLanguage = Enums::getTitleLanguage(titleId);
        if (titleLanguage == 0)
            return "";
        return std::string(" (") + Enums::getLanguageName(titleLanguage) + ")";
    }

    /// Short name under the picker tile. Carries the localisation when PKSE knows it and an id
    /// fragment when it does not, so two SKUs of one game are never drawn identically.
    inline std::string titleDisplayLabel(uint64_t titleId, Enums::GameVersion gameVersion)
    {
        const std::string label = Enums::getGameVersionName(gameVersion);
        const std::string languageSuffix = titleLanguageSuffix(titleId);
        if (!languageSuffix.empty())
            return label + languageSuffix;
        if (Enums::getGameVersion(titleId) == Enums::GameVersion::Invalid)
            return label + " " + titleIdHint(titleId); // identified by content; the id is all that differs
        return label;
    }

    /// The title as PROSE -- the backup screen's title bar, and the save dialog's "This replaces X's
    /// save data". Keeps the slash a group name carries, because on screen the pair is the honest
    /// answer and nothing here is a path.
    inline std::string titleDisplayName(uint64_t titleId, Enums::GameVersion gameVersion)
    {
        return "Pokemon " + titleDisplayLabel(titleId, gameVersion);
    }

    /**
     * Directory under sdmc:/PKSE holding this title's backups.
     *
     * UNIQUE PER TITLE ID is the whole contract, and the title id is the only thing that delivers
     * it, so every folder carries the whole id -- no exceptions, no list of the titles thought
     * likely to collide. The game name is in front of it purely so the card is browsable.
     *
     * `directorySafeName` is what keeps a GROUP name usable here: a save identified by content
     * resolves only to a group, and every group name is a pair or triple ("FireRed/LeafGreen",
     * "Red/Blue/Yellow"), whose slash would otherwise ask for a folder one level deeper.
     */
    inline std::string titleFolderName(uint64_t titleId, Enums::GameVersion gameVersion)
    {
        return "Pokemon " + directorySafeName(Enums::getGameVersionName(gameVersion)) + " " + titleIdText(titleId);
    }

    /**
     * What titleFolderName() answered BEFORE folders carried the title id.
     *
     * Kept because renaming a backup folder orphans every backup already in it, on every user's
     * card and not just the one where the bug was found. `Save::migrateLegacyTitleFolders` moves
     * them across; this is the only thing that says where to look. **Anything changing the naming
     * rule above has to leave this alone** -- it describes what is already on disk, not what PKSE
     * writes, so "fixing" it to match is how the migration stops finding anything.
     */
    inline std::string legacyTitleFolderName(Enums::GameVersion gameVersion)
    {
        return "Pokemon " + directorySafeName(Enums::getGameVersionName(gameVersion));
    }

    /// A Pokemon title the console actually has, as the migration needs to see it.
    struct InstalledTitle
    {
        uint64_t titleId;
        /// The version its folder was BUILT from. Not re-derivable from the id: an id PKSE does not
        /// recognise is resolved by CONTENT, and getGameVersion() answers Invalid for it.
        Enums::GameVersion gameVersion;
    };

    /**
     * Could another installed title have produced this one's legacy folder name?
     *
     * THE MIGRATION'S ONE HARD QUESTION, and the reason it cannot simply rename everything it finds.
     * The legacy name came from the GAME, so `Pokemon Shield` can only ever have been Shield's and
     * moves safely -- but on a console with two LeafGreen SKUs installed, `Pokemon LeafGreen` could
     * have been either, and the two saves are the same format so nothing in the bytes can settle it.
     * Those are left where they are and named in the log rather than guessed at.
     *
     * Kept here, pure and away from the filesystem, so it can be tested off-console -- the same
     * reason Save/RtcFooter.h is its own header.
     */
    inline bool legacyFolderIsAmbiguous(const InstalledTitle &title, const std::vector<InstalledTitle> &installed)
    {
        const std::string legacyName = legacyTitleFolderName(title.gameVersion);
        int couldHaveProducedIt = 0;
        for (const InstalledTitle &other : installed)
        {
            if (legacyTitleFolderName(other.gameVersion) == legacyName)
                ++couldHaveProducedIt;
        }
        return couldHaveProducedIt != 1;
    }
}

#endif // SAVE_TITLE_FOLDER_H
