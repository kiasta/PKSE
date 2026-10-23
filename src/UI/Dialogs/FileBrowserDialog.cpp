#include "UI/Dialogs/FileBrowserDialog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "UI/ListSearch.h"
#include "UI/Dialogs/DialogFrame.h"
#include "Utils/Settings.h"

namespace UI
{
    namespace Dialogs
    {

        namespace
        {
            std::string toLower(std::string text)
            {
                for (char &character : text)
                    if (character >= 'A' && character <= 'Z')
                        character = static_cast<char>(character - 'A' + 'a');
                return text;
            }

            bool endsWith(const std::string &text, const std::string &suffix)
            {
                return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
            }

            /// Case-insensitive compare, so "Zelda" doesn't sort above "apple" the way ASCII would.
            bool nameLess(const std::string &a, const std::string &b)
            {
                return toLower(a) < toLower(b);
            }

            std::string joinPath(const std::string &directory, const std::string &leaf)
            {
                if (directory.empty())
                    return leaf;
                return (directory.back() == '/') ? directory + leaf : directory + "/" + leaf;
            }

            bool directoryExists(const std::string &path)
            {
                DIR *d = opendir(path.c_str());
                if (!d)
                    return false;
                closedir(d);
                return true;
            }

            /// The length of `path` without its trailing slashes, keeping the one a root needs
            /// ("sdmc:/"). The browser spells one folder both ways -- a start folder is written
            /// "sdmc:/PKSE/saves/", a folder entered from a listing has no slash -- and a favorite
            /// must match either.
            size_t directoryLength(const std::string &path)
            {
                size_t trimmedPathLength = path.size();
                while (trimmedPathLength > 0 && path[trimmedPathLength - 1] == '/' && !(trimmedPathLength >= 2 && path[trimmedPathLength - 2] == ':'))
                    --trimmedPathLength;
                return trimmedPathLength;
            }

            bool sameDirectory(const std::string &firstPath, const std::string &secondPath)
            {
                const size_t firstLength = directoryLength(firstPath);
                return firstLength == directoryLength(secondPath) && firstPath.compare(0, firstLength, secondPath, 0, firstLength) == 0;
            }

            /// The stored spelling of `directory` when it is a favorite, else nullptr.
            const std::string *findFavoriteFolder(const std::string &directory)
            {
                for (const std::string &favoriteFolder : Utils::favoriteFolders())
                {
                    if (sameDirectory(favoriteFolder, directory))
                        return &favoriteFolder;
                }
                return nullptr;
            }

            std::string humanSize(uint64_t bytes)
            {
                char buffer[32];
                if (bytes >= 1024ull * 1024 * 1024)
                    std::snprintf(buffer, sizeof(buffer), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
                else if (bytes >= 1024ull * 1024)
                    std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024));
                else if (bytes >= 1024)
                    std::snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
                else
                    std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
                return buffer;
            }

            /// Trim a path from the LEFT so the part that matters -- the folder you are in -- survives.
            std::string fitPathFromLeft(PKSEFramebuffer &framebuffer, const std::string &path, int maxW, TextStyle pathStyle = TextStyle::Caption)
            {
                int dialogWidth, h;
                framebuffer.measureText(path, dialogWidth, h, pathStyle);
                if (dialogWidth <= maxW)
                    return path;
                std::string out = path;
                while (!out.empty())
                {
                    out.erase(0, 1);
                    const std::string candidate = "..." + out;
                    framebuffer.measureText(candidate, dialogWidth, h, pathStyle);
                    if (dialogWidth <= maxW)
                        return candidate;
                }
                return "...";
            }
        }

        bool FileBrowserState::atRoot() const
        {
            return directory == SD_ROOT || directory.empty();
        }

        void FileBrowserState::open(const std::string &windowTitle, const std::string &filterLabel, const std::vector<std::string> &startDirs, const std::vector<std::string> &exts, const std::vector<std::string> &wholeFileNames)
        {
            title = windowTitle;
            this->filterLabel = filterLabel;
            noFilteredFilesMessage = "No " + toLower(filterLabel) + " here. Press X to show every file.";
            extensions.clear();
            for (const auto &e : exts)
                extensions.push_back(toLower(e));
            exactFileNames.clear();
            for (const std::string &wholeFileName : wholeFileNames)
                exactFileNames.push_back(toLower(wholeFileName));
            showAllFiles = false;
            showFavorites = false;
            chosenPath.clear();
            status.clear();
            returnTo.clear();
            selectedIndex = scroll = 0;

            // Land where the file probably is. A miss costs the user a lot of D-pad, and PKSM's own
            // layout gives several good guesses -- but never assume one exists.
            directory = SD_ROOT;
            for (const auto &d : startDirs)
            {
                if (directoryExists(d))
                {
                    directory = d;
                    break;
                }
            }
            active = true;
            refresh();
        }

        void FileBrowserState::close()
        {
            active = false;
            entries.clear();
            status.clear();
            chosenPath.clear();
        }

        void FileBrowserState::refresh()
        {
            entries.clear();
            truncated = false;

            // No ".." row and no search: the list is the user's own, short, and leaving it is B.
            if (showFavorites)
            {
                for (const std::string &favoriteFolder : Utils::favoriteFolders())
                {
                    Entry favoriteEntry;
                    favoriteEntry.name = favoriteFolder;
                    favoriteEntry.isDir = true;
                    entries.push_back(std::move(favoriteEntry));
                }
                entriesBeforeSearch = static_cast<int>(entries.size());
                returnTo.clear();
                move(0); // keep the cursor in range after a row was removed
                return;
            }

            // The ".." row is a real row rather than a B-only gesture, so the browser is usable by touch
            // alone -- there is no on-screen B button inside the card.
            if (!atRoot())
            {
                Entry parentEntry;
                parentEntry.name = "..";
                parentEntry.isDir = parentEntry.isParent = true;
                entries.push_back(std::move(parentEntry));
            }

            DIR *d = opendir(directory.c_str());
            if (!d)
            {
                status = "Couldn't open that folder.";
                selectedIndex = scroll = 0;
                return;
            }

            std::vector<Entry> dirs, files;
            struct dirent *e = nullptr;
            while ((e = readdir(d)) != nullptr)
            {
                if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
                    continue;
                if (dirs.size() + files.size() >= MAX_DIRECTORY_ENTRIES)
                {
                    truncated = true;
                    break;
                }

                const std::string full = joinPath(directory, e->d_name);
                bool isDir = (e->d_type == DT_DIR);
                struct stat entryStatus{};
                bool haveStat = false;
                if (e->d_type == DT_UNKNOWN)
                { // some devoptabs don't fill d_type
                    if (stat(full.c_str(), &entryStatus) != 0)
                        continue;
                    haveStat = true;
                    isDir = S_ISDIR(entryStatus.st_mode);
                }

                Entry entry;
                entry.name = e->d_name;
                entry.isDir = isDir;
                if (isDir)
                {
                    dirs.push_back(std::move(entry));
                    continue;
                }

                // Filtered out before it costs a stat: a folder full of ROMs shouldn't make listing slow.
                if (!showAllFiles && !(extensions.empty() && exactFileNames.empty()))
                {
                    const std::string lower = toLower(entry.name);
                    bool match = false;
                    // Whole-name matches first -- the list is short, and a save with no extension
                    // at all (a 3DS `main`) can never be found by the suffix test below.
                    for (const std::string &lowerExactName : exactFileNames)
                    {
                        if (lower == lowerExactName)
                        {
                            match = true;
                            break;
                        }
                    }
                    for (const auto &lowerExtension : extensions)
                    {
                        if (match)
                        {
                            break;
                        }
                        if (endsWith(lower, lowerExtension))
                        {
                            match = true;
                        }
                    }
                    if (!match)
                        continue;
                }
                if (!haveStat && stat(full.c_str(), &entryStatus) == 0)
                    haveStat = true;
                entry.size = haveStat ? static_cast<uint64_t>(entryStatus.st_size) : 0;
                files.push_back(std::move(entry));
            }
            closedir(d);

            std::sort(dirs.begin(), dirs.end(), [](const Entry &a, const Entry &b)
                      { return nameLess(a.name, b.name); });
            std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b)
                      { return nameLess(a.name, b.name); });
            entriesBeforeSearch = static_cast<int>(entries.size() + dirs.size() + files.size());
            for (std::vector<Entry> *bucket : {&dirs, &files})
                for (Entry &entry : *bucket)
                    if (search.matches(entry.name.c_str()))
                        entries.push_back(std::move(entry));
            // ".." is never filtered out: a query that matched nothing would otherwise leave no row
            // to press, and the only way back up is that row.

            // Coming back up: put the cursor on the folder we just left, not at the top of a long list.
            selectedIndex = 0;
            if (!returnTo.empty())
            {
                for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
                {
                    if (entries[entryIndex].name == returnTo)
                    {
                        selectedIndex = static_cast<int>(entryIndex);
                        break;
                    }
                }
                returnTo.clear();
            }
            scroll = 0;
            move(0); // pull `scroll` onto the selection
        }

        void FileBrowserState::move(int delta)
        {
            const int count = static_cast<int>(entries.size());
            if (count == 0)
            {
                selectedIndex = scroll = 0;
                return;
            }
            selectedIndex += delta;
            if (selectedIndex < 0)
                selectedIndex = 0;
            if (selectedIndex >= count)
                selectedIndex = count - 1;
            if (selectedIndex < scroll)
                scroll = selectedIndex;
            if (selectedIndex >= scroll + VISIBLE_ROWS)
                scroll = selectedIndex - VISIBLE_ROWS + 1;
            const int maxScroll = std::max(0, count - VISIBLE_ROWS);
            if (scroll > maxScroll)
                scroll = maxScroll;
            if (scroll < 0)
                scroll = 0;
        }

        bool FileBrowserState::activate()
        {
            if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
                return false;
            const Entry &e = entries[selectedIndex];
            if (e.isParent)
            {
                goUp();
                return false;
            }
            if (showFavorites)
            {
                // Checked here rather than left to refresh(): a folder that is gone (another SD
                // card, a deleted emulator) would otherwise open as an empty listing with the
                // favorites list gone, and the way back to it would be ZL with nothing said.
                if (!directoryExists(e.name))
                {
                    status = "That folder isn't on the SD card. ZR removes it from your favorites.";
                    return false;
                }
                directory = e.name;
                showFavorites = false;
                status.clear();
                refresh();
                return false;
            }
            if (e.isDir)
            {
                directory = joinPath(directory, e.name);
                status.clear();
                refresh();
                return false;
            }
            chosenPath = joinPath(directory, e.name);
            return true;
        }

        bool FileBrowserState::goUp()
        {
            if (showFavorites)
            {
                toggleFavoritesView();
                return true;
            }
            if (atRoot())
                return false;
            std::string directoryPath = directory;
            while (!directoryPath.empty() && directoryPath.back() == '/')
                directoryPath.pop_back();
            const size_t slash = directoryPath.find_last_of('/');
            if (slash == std::string::npos)
            {
                directory = SD_ROOT;
            }
            else
            {
                returnTo = directoryPath.substr(slash + 1);
                directoryPath.erase(slash);
                directory = directoryPath.empty() ? std::string(SD_ROOT) : directoryPath;
                // "sdmc:" -> "sdmc:/"
                if (!directory.empty() && directory.back() == ':') directory += '/';
            }
            status.clear();
            refresh();
            return true;
        }

        void FileBrowserState::toggleFavoritesView()
        {
            showFavorites = !showFavorites;
            status.clear();
            selectedIndex = scroll = 0;
            refresh();
        }

        bool FileBrowserState::toggleFavorite()
        {
            std::string favoriteCandidate = directory;
            if (showFavorites)
            {
                if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()))
                    return false;
                favoriteCandidate = entries[selectedIndex].name;
            }
            else
            {
                favoriteCandidate.erase(directoryLength(favoriteCandidate));
            }
            if (const std::string *storedFolder = findFavoriteFolder(favoriteCandidate))
                favoriteCandidate = *storedFolder;
            const bool isFavorite = Utils::toggleFavoriteFolder(favoriteCandidate);
            status.clear();
            if (showFavorites)
                refresh();
            return isFavorite;
        }

        bool FileBrowserState::isCurrentFolderFavorite() const
        {
            return findFavoriteFolder(directory) != nullptr;
        }

        void drawFileBrowser(FileBrowserState &entryStatus, PKSEFramebuffer &framebuffer, std::vector<TouchButton> &taps)
        {

            constexpr int rowH = TouchTargetMin;
            // Bottom padding only. A hint strip inside the card would put a second row of controller badges
            // directly above the screen's nav bar -- two button menus for one dialog. The owning screen
            // names the keys; see SaveSelectScreen::draw and TrainerViewScreen's instruction chain.
            constexpr int footerH = 18;
            // title + path line + the search box under them
            const int headerH = 96 + listSearchBoxHeight();
            const int dialogWidth = 1080;
            const int dialogHeight = headerH + FileBrowserState::VISIBLE_ROWS * rowH + footerH + 12;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = (framebuffer.getHeight() - dialogHeight) / 2;

            framebuffer.drawFilledRect(0, 0, framebuffer.getWidth(), framebuffer.getHeight(), Color(0, 0, 0, 150));
            framebuffer.drawSoftShadow(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS);
            framebuffer.drawFilledRoundedRect(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS, Colors::Panel);
            framebuffer.drawRoundedRect(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS, Colors::Border, 1);
            framebuffer.drawText(dialogX + 24, dialogY + 14, entryStatus.title, Colors::Text, TextStyle::Heading);

            // Current path, trimmed from the left so the deepest folder stays readable. A favorite
            // folder carries a star in front of it, which is all ZR changes on screen.
            if (entryStatus.showFavorites)
            {
                framebuffer.drawText(dialogX + 24, dialogY + 54, "Favorite folders", Colors::TextDim, TextStyle::Caption);
            }
            else
            {
                int pathLeft = dialogX + 24;
                if (entryStatus.isCurrentFolderFavorite())
                {
                    const std::string favoriteStar = "\xE2\x98\x85";
                    int starWidth, starHeight;
                    framebuffer.measureText(favoriteStar, starWidth, starHeight, TextStyle::Caption);
                    framebuffer.drawSymbol(pathLeft, dialogY + 54, favoriteStar, Colors::ShinyStar, TextStyle::Caption);
                    pathLeft += starWidth + 8;
                }
                framebuffer.drawText(pathLeft, dialogY + 54, fitPathFromLeft(framebuffer, entryStatus.directory, dialogWidth - 220 - (pathLeft - dialogX - 24)), Colors::TextDim, TextStyle::Caption);
            }
            {
                const std::string mode = entryStatus.showFavorites ? "Favorites" : entryStatus.showAllFiles ? "All files" : entryStatus.filterLabel;
                int messageWidth, mh;
                framebuffer.measureText(mode, messageWidth, mh, TextStyle::Caption);
                const int panelWidth = messageWidth + 26, ph = 26;
                framebuffer.drawPill(dialogX + dialogWidth - 24 - panelWidth, dialogY + 50, panelWidth, ph, entryStatus.showAllFiles ? Colors::PanelAlt : Colors::AccentDim);
                framebuffer.drawText(dialogX + dialogWidth - 24 - panelWidth + 13, dialogY + 50 + (ph - mh) / 2, mode, Colors::Text, TextStyle::Caption);
            }
            framebuffer.drawFilledRect(dialogX + DIALOG_CORNER_RADIUS, dialogY + 84, dialogWidth - DIALOG_CORNER_RADIUS * 2, 2, Colors::Accent);

            drawListSearchBox(framebuffer, dialogX + 24, dialogY + 92, dialogWidth - 48, entryStatus.search, static_cast<int>(entryStatus.entries.size()), entryStatus.entriesBeforeSearch);

            taps.clear();

            const int listY = dialogY + headerH;
            const int count = static_cast<int>(entryStatus.entries.size());
            // ".." is the way back out, not something the folder holds: counted, every message below went
            // unsaid in any folder but the root.
            const bool hasParentRow = count > 0 && entryStatus.entries.front().isParent;
            if (count == (hasParentRow ? 1 : 0))
            {
                const int messageY = listY + (hasParentRow ? rowH : 0);
                if (entryStatus.showFavorites)
                {
                    framebuffer.drawText(dialogX + 32, messageY + 14, "No favorite folders yet.", Colors::TextDim);
                    framebuffer.drawText(dialogX + 32, messageY + 14 + framebuffer.lineHeight(TextStyle::Body) + 4, "Open a folder and press ZR to add it here.", Colors::TextDim, TextStyle::Caption);
                }
                else if (entryStatus.search.isFiltering())
                {
                    framebuffer.drawText(dialogX + 32, messageY + 14, "Nothing here matches that search.", Colors::TextDim);
                    framebuffer.drawText(dialogX + 32, messageY + 14 + framebuffer.lineHeight(TextStyle::Body) + 4, "Y: change it, or clear it to see the whole folder.", Colors::TextDim, TextStyle::Caption);
                }
                else
                {
                    const char *message = entryStatus.status.empty() ? (entryStatus.showAllFiles ? "This folder is empty." : entryStatus.noFilteredFilesMessage.c_str()) : entryStatus.status.c_str();
                    framebuffer.drawText(dialogX + 32, messageY + 16, message, Colors::TextDim, TextStyle::Body);
                }
            }
            else if (!entryStatus.status.empty())
            {
                // Under the last row, in the strip the footer leaves free, so nothing moves and no row is covered.
                framebuffer.drawText(dialogX + 24, listY + FileBrowserState::VISIBLE_ROWS * rowH + 4, fitPathFromLeft(framebuffer, entryStatus.status, dialogWidth - 48), Colors::Warning, TextStyle::Caption);
            }

            const int last = std::min(count, entryStatus.scroll + FileBrowserState::VISIBLE_ROWS);
            for (int index = entryStatus.scroll; index < last; ++index)
            {
                const auto &e = entryStatus.entries[index];
                const int rectY = listY + (index - entryStatus.scroll) * rowH;
                const bool selected = (index == entryStatus.selectedIndex);
                if (selected)
                    framebuffer.drawSelectionHighlight(dialogX + 12, rectY + 3, dialogWidth - 24, rowH - 6);

                // Folder / parent / file, told apart by a drawn icon rather than by colour alone. A font
                // glyph will not do: a solid triangle in a list means "expand this row", which is not what
                // A does here. The ".." row gets an OPEN folder -- it is the one folder being left rather
                // than entered.
                constexpr int iconSize = 24;
                const int iconX = dialogX + 26, iy = rectY + (rowH - iconSize) / 2;
                if (e.isDir)
                    framebuffer.drawFolderIcon(iconX, iy, iconSize, Colors::Accent, e.isParent);
                else
                    framebuffer.drawFileIcon(iconX, iy, iconSize, Colors::TextDim);

                int textWidth, th;
                // A favorite is a whole path, so it is trimmed from the left like the header's.
                const std::string label = e.isParent ? "..  (up one folder)" : entryStatus.showFavorites ? fitPathFromLeft(framebuffer, e.name, dialogWidth - 62 - 32, TextStyle::Body) : e.name;
                framebuffer.measureText(label, textWidth, th, TextStyle::Body);
                framebuffer.drawText(dialogX + 62, rectY + (rowH - th) / 2, label, selected ? Colors::Text : (e.isDir ? Colors::Text : Colors::TextDim), TextStyle::Body);

                if (!e.isDir)
                {
                    const std::string sizeText = humanSize(e.size);
                    int sizeTextWidth, sh;
                    framebuffer.measureText(sizeText, sizeTextWidth, sh, TextStyle::Caption);
                    framebuffer.drawText(dialogX + dialogWidth - 32 - sizeTextWidth, rectY + (rowH - sh) / 2, sizeText, Colors::TextDim, TextStyle::Caption);
                }
                taps.push_back({index, dialogX + 12, rectY, dialogWidth - 24, rowH});
            }

            drawScrollbar(framebuffer, dialogX + dialogWidth - 14, listY, FileBrowserState::VISIBLE_ROWS * rowH, count * rowH, entryStatus.scroll);

            if (entryStatus.truncated)
            {
                framebuffer.drawText(dialogX + 24, dialogY + dialogHeight - footerH - 24, "Folder has more than " + std::to_string(FileBrowserState::MAX_DIRECTORY_ENTRIES) + " entries; the rest are not listed.", Colors::Warning, TextStyle::Caption);
            }

        }
    }
}
