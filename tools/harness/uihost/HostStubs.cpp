/**
 * The console services the UI reaches that have no host equivalent, supplied for the harness.
 *
 * Kept to the smallest possible set on purpose. Every stub here is a place where the harness stops
 * testing the real thing, so each one either (a) has no behaviour worth testing off-console --
 * logging sinks -- or (b) is scripted so the TEST decides what it returns, which is more useful than
 * the real thing would be. Anything that would need a stub returning plausible fake DATA is not
 * stubbed at all; the screens that need it are simply not host-drivable, and the harness says so.
 */
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "Utils/Keyboard.h"
#include "Utils/FileUtilities.h"
#include "Utils/HelperUtilities.h"
#include "Save/GetSaveFileContents.h"
#include "Test/TestRunner.h"
#include "HostInput.h"

namespace {
    struct Answer { bool accepted; std::string text; int value; };
    std::deque<Answer> gAnswers;
    int gRaised = 0;
}

namespace UIHost {
    void queueText(const std::string& t) { gAnswers.push_back({true, t, 0}); }
    void queueNumber(int v)              { gAnswers.push_back({true, std::to_string(v), v}); }
    void queueCancel()                   { gAnswers.push_back({false, "", 0}); }
    int  pendingPrompts()                { return static_cast<int>(gAnswers.size()); }
    void clearPrompts()                  { gAnswers.clear(); gRaised = 0; }
    int  promptsRaised()                 { return gRaised; }
}

namespace Utils {
    /// Mirrors the real Keyboard.cpp: refuse outright while the self test owns the screen, and
    /// count it. On console the applet would BLOCK; here it would silently consume a queued answer
    /// and hide the fact that a journey reached a prompt at all.
    int gSuppressed = 0;
    int keyboardPromptsSuppressed() { return gSuppressed; }
    void resetKeyboardSuppressionCount() { gSuppressed = 0; }

    KeyboardResult promptText(const std::string&, const std::string&,
                              const std::string& initial, int maxChars,
                              const std::function<void(const std::string&)>& textChanged) {
        if (g_suppressKeyboard) { ++gSuppressed; return KeyboardResult{}; }
        ++gRaised;
        KeyboardResult r;
        // Nothing queued: behave as a CANCEL, not as an empty accept. An unscripted prompt must not
        // silently blank the field the UI was editing -- that is the exact mistake Keyboard.h warns
        // about, and a harness that made it would manufacture data loss the console never has.
        if (gAnswers.empty()) return r;
        const Answer a = gAnswers.front();
        gAnswers.pop_front();
        r.accepted = a.accepted;
        r.text     = a.accepted ? a.text : std::string();
        // The real keyboard enforces the length cap, so the harness must too -- otherwise a test could
        // "type" a name longer than the game can hold and the truncation bug would never show up.
        if (r.accepted && maxChars > 0 && static_cast<int>(r.text.size()) > maxChars)
            r.text.resize(static_cast<size_t>(maxChars));
        // A live prompt sees the text as it is typed. The whole answer arrives as one edit here, which
        // still drives the caller's per-key path -- the one a live search filters its list on.
        if (r.accepted && textChanged && r.text != initial) textChanged(r.text);
        return r;
    }

    /// The console's keyboard, reached from the on-screen keyboard's hand-off key. Scripted from the
    /// same queue: on console it is simply another prompt that blocks until answered.
    KeyboardResult promptSystemText(const std::string& header, const std::string& guide,
                                    const std::string& initial, int maxChars) {
        return promptText(header, guide, initial, maxChars, nullptr);
    }

    NumberResult promptNumber(const std::string&, int initial, int minValue, int maxValue) {
        if (g_suppressKeyboard) { ++gSuppressed; return NumberResult{}; }
        ++gRaised;
        NumberResult r;
        if (gAnswers.empty()) return r;
        const Answer a = gAnswers.front();
        gAnswers.pop_front();
        if (!a.accepted) return r;
        r.accepted = true;
        r.value    = a.value < minValue ? minValue : (a.value > maxValue ? maxValue : a.value);
        (void)initial;
        return r;
    }

    // Same shape as the other harnesses': the declarations in Logger.h are unchanged, so all ~200
    // call sites build, and nothing is written anywhere.
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logEventToFile(const std::string&) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}

    uint8_t* readAllBytes(const char* path, size_t* outSize) {
        if (outSize) *outSize = 0;
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return nullptr;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 0) { std::fclose(f); return nullptr; }
        auto* buffer = new uint8_t[static_cast<size_t>(n)];
        const size_t got = std::fread(buffer, 1, static_cast<size_t>(n), f);
        std::fclose(f);
        if (got != static_cast<size_t>(n)) { delete[] buffer; return nullptr; }
        if (outSize) *outSize = got;
        return buffer;
    }
}

// src/Utils/FileUtilities.cpp is excluded from this build because half of it is Switch save-data
// plumbing (fsOpenSaveDataFileSystem and friends). The other half is ordinary file work that the
// bank genuinely uses, so it is IMPLEMENTED here against std::filesystem rather than stubbed --
// a bank whose directory operations silently did nothing would make the storage view untestable.
namespace fs = std::filesystem;

namespace Utils {
    bool copyFile(const char* src, const char* destination) {
        std::error_code ec;
        fs::create_directories(fs::path(destination).parent_path(), ec);
        return fs::copy_file(src, destination, fs::copy_options::overwrite_existing, ec) && !ec;
    }

    bool copyDirectory(const char* src, const char* destination) {
        std::error_code ec;
        fs::create_directories(destination, ec);
        fs::copy(src, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    bool deleteDirectoryRecursive(const char* path) {
        std::error_code ec;
        return fs::remove_all(path, ec) != static_cast<std::uintmax_t>(-1) && !ec;
    }

    /// Fixed, not the wall clock. Two runs of the same tests must produce the same names, or a
    /// backup-path assertion would pass or fail depending on what second it ran in.
    std::string getTimestamp() { return "20260101_000000"; }

    // Genuinely Switch-only: there is no installed title, account or save-data mount off-console.
    // These return failure rather than a plausible path, so a test that reached one fails loudly
    // instead of proceeding on a fiction.
    bool copySaveDataTo(AccountUid, u64, const char*) { return false; }
    std::string backupSaveData(AccountUid, u64, std::string, bool) { return std::string(); }
    bool restoreBackupToTitle(AccountUid, u64, const char*, const char*) { return false; }
}

namespace Utils {
    std::vector<std::string> listBackupDirectories(const char* gameDirectory) {
        std::vector<std::string> out;
        std::error_code ec;
        if (!gameDirectory) return out;
        for (const auto& e : fs::directory_iterator(gameDirectory, ec))
            if (!ec && e.is_directory(ec)) out.push_back(e.path().filename().string());
        return out;
    }

    /// Reads the installed title's control data on console, which does not exist here. An empty
    /// string is what the real one returns for a title it cannot resolve, so the UI already handles
    /// it -- this is the honest answer rather than an invented version number.
    std::string getTitleVersion(u64) { return std::string(); }
}

// SCRIPTED rather than stubbed to a fixed answer. The write path itself is console-only (it
// dispatches on the title id through fs), but the UI's behaviour AROUND it is not: a failed save has
// to report failure and keep the unsaved-changes flag, and a successful one has to clear it. A fixed
// `false` would leave the success branch untested and a fixed `true` would leave the failure branch
// untested, so the test says which it wants.

namespace {
    bool gSaveSucceeds = true;
    int  gSaveCalls    = 0;
}

namespace UIHost {
    void setSaveResult(bool ok) { gSaveSucceeds = ok; }
    int  saveCalls()            { return gSaveCalls; }
    void resetSaveCalls()       { gSaveCalls = 0; }
}

namespace Save {
    bool saveTrainerInfo(Trainer::Trainer&, const char*, u64, AccountUid, bool) {
        ++gSaveCalls;
        return gSaveSucceeds;
    }

    /// Which formats open from a file. The real one reads the LOOSE_FORMATS table, which lives in
    /// the save layer this harness does not link. The trade-partner picker is the only caller and
    /// its console half cannot run here anyway, so the honest host answer is "not from a file" --
    /// it never sends a host run down the browser path by accident.
    bool groupOpensFromFile(Enums::GameVersion) noexcept { return false; }

    /// Reading another TITLE's save, which the donor picker does once a title is chosen. Console
    /// only: it addresses save data by title id through fs. Null is the real one's answer for a
    /// directory holding nothing readable, and the picker already reports that.
    std::unique_ptr<Trainer::Trainer> readTitleSaveAsTrainer(const char*, u64) { return nullptr; }

    /// Opening ANOTHER save, which the trade-partner picker does. The probe chain is not
    /// console-only, but linking it here would drag in the whole save layer for one call, and the
    /// donor logic it feeds is already covered against real saves by the entity suite. Null is what
    /// the real one returns for a file it cannot open, so the UI already handles it -- the picker
    /// reports "Not a save PKSE opens" and changes nothing, which is the branch worth having here.
    std::unique_ptr<Trainer::Trainer> openExternalSave(std::vector<uint8_t>, const std::string&, std::string*,
                                                       std::string*) {
        return nullptr;
    }
}

// src/Test/TestRunner.cpp is console-only (it enumerates save data), but SuiteUI.cpp calls these to
// publish the screen it is driving and to ask for one chunk per frame. Off-console there is no
// display to publish to and no frame loop to yield to, so they are inert here -- which is exactly
// right: the host run exercises the walkthrough's LOGIC, and the presentation is what the console
// run adds.
namespace Test {
    namespace { UI::UIScreen* gHostDriven = nullptr; Runner::DriveCaption gHostCaption; }

    void Runner::setDrivenScreen(UI::UIScreen* s) noexcept { gHostDriven = s; }
    UI::UIScreen* Runner::drivenScreen() noexcept { return gHostDriven; }
    void Runner::setDriveCaption(const DriveCaption& c) { gHostCaption = c; }
    const Runner::DriveCaption& Runner::driveCaption() noexcept { return gHostCaption; }
    void Runner::yieldFrame() noexcept {}
}
