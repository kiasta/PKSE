#ifndef PKSE_HARNESS_SAVETREE_H
#define PKSE_HARNESS_SAVETREE_H

#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

/**
 * Finding a test save by the TITLE it came from, rather than by guessing its filename.
 *
 * A hardcoded path guesses both halves -- a folder abbreviation and a filename
 * (`dp/POKEMON_P.sav`, `hgss/POKEMON_SS.sav`, `gsc/POKEMON_GLD.sav`). The filename depends
 * entirely on which emulator, cartridge dumper or save manager produced the file, so a perfectly
 * good HeartGold save named anything else is simply not found, and the generation is reported as
 * skipped rather than as misfiled.
 *
 * The folder does more than remove a guess. Four pairs of games -- Red/Blue, Gold/Silver,
 * Diamond/Pearl, HeartGold/SoulSilver -- share a save layout with NO version byte anywhere in the
 * file, so their saves are byte-identical in shape and can never say which game they came from.
 * Nothing a harness can read will ever tell it. The person who dumped the save knows, and a folder
 * called `SoulSilver` is where they write it down. `<root>/SoulSilver/` is therefore not a
 * convention for tidiness -- it is the only channel that fact has.
 *
 * One file per folder, any name. If a folder holds several, the first by directory order is used
 */
namespace SaveTree {

    inline bool isFile(const std::string& path) {
        struct stat entryStatus{};
        return stat(path.c_str(), &entryStatus) == 0 && S_ISREG(entryStatus.st_mode);
    }

    /// Every regular file directly inside `directory`, in directory order. Empty if there is no
    /// such directory. Dotfiles are skipped -- a .DS_Store or a .gitkeep is not a save.
    inline std::vector<std::string> filesIn(const std::string& directory) {
        std::vector<std::string> out;
        DIR* dir = opendir(directory.c_str());
        if (!dir) return out;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            const std::string name(entry->d_name);
            if (name.empty() || name[0] == '.') continue;
            const std::string path = directory + "/" + name;
            if (isFile(path)) out.push_back(path);
        }
        closedir(dir);
        return out;
    }

    /// A folder or title name reduced to what the console self test compares: lower case, letters and
    /// digits only, and a leading "pokemon" dropped.
    inline std::string comparableTitle(const std::string& name) {
        std::string letters;
        for (const char character : name) {
            if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
                letters += character;
            else if (character >= 'A' && character <= 'Z')
                letters += static_cast<char>(character - 'A' + 'a');
        }
        const std::string prefix = "pokemon";
        if (letters.size() > prefix.size() && letters.compare(0, prefix.size(), prefix) == 0)
            letters.erase(0, prefix.size());
        return letters;
    }

    /// The folder under `root` whose name is this title, matched the way the console self test
    /// matches one -- case and punctuation aside, with or without a leading "Pokemon". Exact first.
    ///
    /// Exact-only matching made the host harnesses disagree with the console about the same tree: a
    /// save in `LetsGoPikachu/` or `ShiningPearl/` was a tested title on hardware and silently
    /// absent here, so the host gate never saw it.
    inline std::string folderFor(const std::string& root, const std::string& title) {
        const std::string exact = root + "/" + title;
        struct stat entryStatus{};
        if (stat(exact.c_str(), &entryStatus) == 0 && S_ISDIR(entryStatus.st_mode)) return exact;
        DIR* dir = opendir(root.c_str());
        if (!dir) return exact;
        const std::string wanted = comparableTitle(title);
        std::string found = exact;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            const std::string name(entry->d_name);
            if (name.empty() || name[0] == '.') continue;
            if (comparableTitle(name) == wanted) { found = root + "/" + name; break; }
        }
        closedir(dir);
        return found;
    }

    /// The save filed under `<root>/<title>/`, or "" when that folder is absent or empty.
    inline std::string forTitle(const std::string& root, const std::string& title) {
        const std::vector<std::string> files = filesIn(folderFor(root, title));
        if (files.empty()) return std::string();
        if (files.size() > 1)
            std::printf("      note: %s/ holds %zu files; using %s\n", title.c_str(), files.size(), files.front().c_str());
        return files.front();
    }

    /// The title folder if it exists, else the first of the legacy paths that does.
    ///
    /// The fallback is kept so a save tree laid out the old way still runs rather than reporting
    /// every generation as missing -- but it is a fallback, and it prints, so a tree still on the
    /// old layout says so once per save instead of looking identical to one that is not.
    inline std::string resolve(const std::string& root, const std::string& title, std::initializer_list<const char*> legacyPaths) {
        const std::string byTitle = forTitle(root, title);
        if (!byTitle.empty()) return byTitle;
        for (const char* relative : legacyPaths) {
            const std::string path = root + "/" + relative;
            if (isFile(path)) {
                std::printf("      note: %s found at the old path %s -- move it to %s/ so the title is explicit\n", title.c_str(), relative, title.c_str());
                return path;
            }
        }
        return std::string();
    }
    /// What a harness prints when it has no save for a title. Names the FOLDER, because that is
    /// what the reader has to create -- an empty path (which is what a missing folder resolves to)
    /// otherwise prints as "no save at " and tells them nothing.
    inline std::string missing(const std::string& root, const std::string& title) {
        return "no save -- put one in " + root + "/" + title + "/";
    }

    // Named once, here, so a tree laid out any other way cannot be silently invisible to some harnesses and
    // not others. Each entry is the title folder plus the legacy path it replaces; the legacy half exists
    // only so a tree that has not been reorganised yet still runs, and it says so when it fires.

    inline std::string red(const std::string& root) { return resolve(root, "Red", {"Pokemon Red.dat"}); }
    inline std::string blue(const std::string& root) { return forTitle(root, "Blue"); }
    inline std::string fireRed(const std::string& root) { return resolve(root, "FireRed", {"FireRed_e.sav"}); }
    // The Hoenn trio. Each is its own folder even though Ruby and Sapphire share a save layout:
    // nothing in the file says which of the pair it is, so the folder is the only place that can.
    inline std::string ruby(const std::string& root) { return forTitle(root, "Ruby"); }
    inline std::string sapphire(const std::string& root) { return forTitle(root, "Sapphire"); }
    inline std::string emerald(const std::string& root) { return forTitle(root, "Emerald"); }
    inline std::string letsGoPikachu(const std::string& root) {
        return resolve(root, "Let's Go Pikachu", {"gg/savedata.bin"});
    }
    inline std::string letsGoEevee(const std::string& root) { return forTitle(root, "Let's Go Eevee"); }
    inline std::string sword(const std::string& root) { return forTitle(root, "Sword"); }
    inline std::string shield(const std::string& root) { return resolve(root, "Shield", {"shield/main"}); }
    inline std::string legendsZA(const std::string& root) { return resolve(root, "Legends Z-A", {"za/main"}); }
    inline std::string violet(const std::string& root) { return resolve(root, "Violet", {"sv/main"}); }
    inline std::string legendsArceus(const std::string& root) { return resolve(root, "Legends Arceus", {"la/main"}); }
    inline std::string ultraSun(const std::string& root) { return resolve(root, "Ultra Sun", {"usum/main"}); }
}

#endif
