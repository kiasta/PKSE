/**
 * PKSE's own files all live at known paths, so until now nothing needed to browse. Importing a PKSM
 * bank does: the file belongs to another app, the user may have copied it anywhere, and PKSM itself
 * offers at least three plausible layouts (`/3ds/PKSM/banks`, an extdata backup, or a hand-copied
 * folder). Guessing one path and failing is not a feature; letting them go and find it is.
 *
 * Deliberately generic -- it knows about directories and an extension filter, nothing about banks --
 * so the next feature that needs a file off the card reuses it instead of growing a second picker.
 *
 * State lives in FileBrowserState, owned by the screen (the same flat, directly-mutated pattern the
 * rest of the UI uses). The screen forwards input; this file owns the listing, the navigation and
 * the drawing.
 */
#ifndef UI_DIALOGS_FILE_BROWSER_DIALOG_H
#define UI_DIALOGS_FILE_BROWSER_DIALOG_H

#include <cstdint>
#include <string>
#include <vector>

#include "UI/Common.h" // TouchButton
#include "UI/ListSearch.h"

namespace UI
{
    class PKSEFramebuffer;

    namespace Dialogs
    {

        /// The SD root as the Switch mounts it. Browsing never goes above this.
        inline constexpr const char *SD_ROOT = "sdmc:/";

        struct FileBrowserState
        {
            struct Entry
            {
                std::string name;
                bool isDir = false;
                bool isParent = false; // the synthetic ".." row
                uint64_t size = 0;     // bytes; only filled for files
            };

            bool active = false;
            std::string title = "Select a File";
            std::string directory = SD_ROOT;  // current directory; the root keeps its trailing slash
            std::vector<Entry> entries; // ".." first (when not at the root), then dirs, then files
            int selectedIndex = 0;
            int scroll = 0;            // index of the first visible row
            bool showAllFiles = false; // X toggles: ignore `extensions` and list every file
            bool truncated = false;    // the directory held more entries than MAX_DIRECTORY_ENTRIES
            /// A note the owner or the browser leaves ("Couldn't open that folder.", why a pick was
            /// refused). Drawn whether or not the folder lists anything: a refusal is about a file
            /// that is still on screen, so it never arrives in an empty folder.
            std::string status;

            /// What the filter keeps, as the browser names it ("Save files", "Bank files"). Only the
            /// owner knows, and the same dialog opens both.
            std::string filterLabel;
            std::string noFilteredFilesMessage; // built from filterLabel once, at open()

            /// The search box. Filtering happens in refresh(), so `entries` only ever
            /// holds rows that survive the query and every mover, hit test and draw below stays
            /// exactly as it was -- there is no second index space to keep straight.
            /// `entriesBeforeSearch` is what the tally reports against.
            ListSearch search;
            int entriesBeforeSearch = 0;

            /// Lowercase extensions including the dot, e.g. {".bnk"}. Empty (together with
            /// `exactFileNames`) lists every file.
            std::vector<std::string> extensions;

            /// Lowercase WHOLE filenames, for saves that carry no extension to match on -- a 3DS
            /// save is just `main`. Matched in full rather than as a suffix, because a suffix test
            /// would also take `domain` and gains nothing. Checked alongside `extensions`: a file
            /// is listed when it matches either.
            std::vector<std::string> exactFileNames;

            /// Set when the user picks a file. The owner consumes it and clears it.
            std::string chosenPath;

            /// Name of the directory we just came up out of, so the cursor lands back on it.
            std::string returnTo;

            /// Rows the card shows at once. Sized so the card (header + rows + footer, centred)
            /// still clears the nav bar at 720p -- one more row and it draws underneath it.
            static constexpr int VISIBLE_ROWS = 8;
            static constexpr size_t MAX_DIRECTORY_ENTRIES = 4000; // guard against a pathological folder

            /// Opens at the first of `startDirs` that exists, falling back to the SD root.
            void open(const std::string &windowTitle,
                      const std::string &filterLabel,
                      const std::vector<std::string> &startDirs,
                      const std::vector<std::string> &exts,
                      const std::vector<std::string> &wholeFileNames = {});
            void close();

            void refresh();       // re-list `dir` (also called after every navigation)
            void move(int delta); // move the selection, keeping it and `scroll` in range
            /// A: descend into a folder, or set chosenPath from a file. Returns true if a file was picked.
            bool activate();
            /// B: go up one level. Returns false at the SD root, where the caller should close instead.
            bool goUp();
            bool atRoot() const;
        };

        /// Draws the browser over whatever is behind it and registers its rows as touch buttons.
        /// Draws the browser and appends its row tap targets to `taps` (cleared first). Takes the
        /// state and the sink rather than a screen, so the title picker can open a save file with
        /// the same dialog the storage view uses.
        void drawFileBrowser(FileBrowserState &st, PKSEFramebuffer &framebuffer, std::vector<TouchButton> &taps);
    }
}

#endif
