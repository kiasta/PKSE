#include "Test/UIJourney.h"

#include <cstring>

#include <cstdio>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/TrainerViewScreen.h"
#include "Utils/Logger.h"

namespace Test
{

    namespace
    {

        // FNV-1a. Chosen for being three lines and dependency-free: this only ever answers "are
        // these the same bytes as a moment ago", never "what are they", so collision resistance is
        // not the property that matters.
        constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
        constexpr uint64_t FNV_PRIME = 1099511628211ULL;

        inline void fold(uint64_t &hash, const void *foldedBytes, size_t byteCount)
        {
            const auto *byteValues = static_cast<const unsigned char *>(foldedBytes);
            for (size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex)
            {
                hash ^= byteValues[byteIndex];
                hash *= FNV_PRIME;
            }
        }
        inline void foldU64(uint64_t &hash, uint64_t foldedValue) { fold(hash, &foldedValue, sizeof foldedValue); }

        uint64_t fingerprintPokemon(const ::Pokemon::Pokemon *pokemon)
        {
            uint64_t hash = FNV_OFFSET;
            // absent differs from blank
            if (!pokemon)
            {
                foldU64(hash, 0xEEEEEEEEULL);
                return hash;
            }
            const auto recordBytes = pokemon->getData();
            fold(hash, recordBytes.data(), recordBytes.size());
            return hash;
        }
    }

    UISnapshot UISnapshot::take(const UI::TrainerViewScreen &screen, const Trainer::Trainer &trainer)
    {
        UISnapshot snapshot;
        snapshot.mode = static_cast<int>(screen.selectedMode);
        snapshot.homeIndex = screen.homeMenuIndex;
        snapshot.page = screen.currentPage;
        snapshot.category = screen.selectedCategory;
        snapshot.box = screen.selectedBoxIndex;
        snapshot.slot = screen.selectedItemIndex;
        snapshot.party = screen.selectedPartyIndex;
        snapshot.settingsRow = screen.settingsSelectedRow;
        snapshot.trainerRow = screen.trainerSelectedRow;
        snapshot.detailView = screen.detailViewActive;
        snapshot.detailsModal = screen.details.active;
        snapshot.saveConfirm = screen.saveConfirmActive;
        snapshot.swap = screen.swapActive;
        snapshot.storageMenu = screen.storageMenuActive;
        snapshot.cursorMode = static_cast<int>(screen.cursorMode);
        snapshot.focusPane = screen.storageFocusPane;
        snapshot.storageSaveSlot = screen.stSaveSlot;
        snapshot.storageBankBox = screen.stBankBox;
        snapshot.storageBankSlot = screen.stBankSlot;
        snapshot.goBack = screen.goBack;
        snapshot.exitRequested = screen.exitRequested;
        snapshot.unsaved = screen.hasUnsavedChanges;

        uint64_t hash = FNV_OFFSET;
        fold(hash, trainer.trainerName.data(), trainer.trainerName.size());
        foldU64(hash, trainer.money);
        snapshot.trainerFingerprint = hash;

        hash = FNV_OFFSET;
        for (const auto &pokemon : trainer.party)
        {
            foldU64(hash, fingerprintPokemon(pokemon.get()));
        }
        snapshot.partyFingerprint = hash;

        // Every slot of every box, not just the open one. The cost is the point: a navigation
        // keypress that wrote to a box the user was not even looking at is exactly the bug that
        // would otherwise be found months later by a player.
        hash = FNV_OFFSET;
        for (const auto &box : trainer.boxes)
        {
            for (const auto &pokemon : box)
            {
                foldU64(hash, fingerprintPokemon(pokemon.get()));
            }
        }
        snapshot.boxFingerprint = hash;

        hash = FNV_OFFSET;
        for (const auto &pouch : trainer.items)
        {
            for (const auto &item : pouch)
            {
                foldU64(hash, item.itemId);
                foldU64(hash, item.count);
            }
        }
        snapshot.itemFingerprint = hash;

        hash = FNV_OFFSET;
        for (const auto &boxName : trainer.boxNames)
        {
            fold(hash, boxName.data(), boxName.size());
        }
        snapshot.boxNameFingerprint = hash;

        return snapshot;
    }

    std::vector<const char *> UISnapshot::diff(const UISnapshot &other) const
    {
        std::vector<const char *> changedFields;
        auto compareField = [&](bool same, const char *label)
        {
            if (!same)
            {
                changedFields.push_back(label);
            }
        };
        compareField(mode == other.mode, "mode");
        compareField(homeIndex == other.homeIndex, "homeIndex");
        compareField(page == other.page, "page");
        compareField(category == other.category, "category");
        compareField(box == other.box, "box");
        compareField(slot == other.slot, "slot");
        compareField(party == other.party, "party");
        compareField(settingsRow == other.settingsRow, "settingsRow");
        compareField(trainerRow == other.trainerRow, "trainerRow");
        compareField(detailView == other.detailView, "detailView");
        compareField(detailsModal == other.detailsModal, "detailsModal");
        compareField(saveConfirm == other.saveConfirm, "saveConfirm");
        compareField(swap == other.swap, "swap");
        compareField(storageMenu == other.storageMenu, "storageMenu");
        compareField(cursorMode == other.cursorMode, "cursorMode");
        compareField(focusPane == other.focusPane, "focusPane");
        compareField(storageSaveSlot == other.storageSaveSlot, "stSaveSlot");
        compareField(storageBankBox == other.storageBankBox, "stBankBox");
        compareField(storageBankSlot == other.storageBankSlot, "stBankSlot");
        compareField(goBack == other.goBack, "goBack");
        compareField(exitRequested == other.exitRequested, "exitRequested");
        compareField(unsaved == other.unsaved, "unsaved");
        compareField(trainerFingerprint == other.trainerFingerprint, "TRAINER-DATA");
        compareField(partyFingerprint == other.partyFingerprint, "PARTY-DATA");
        compareField(boxFingerprint == other.boxFingerprint, "BOX-DATA");
        compareField(itemFingerprint == other.itemFingerprint, "ITEM-DATA");
        compareField(boxNameFingerprint == other.boxNameFingerprint, "BOXNAME-DATA");
        return changedFields;
    }

    namespace
    {
        /// Which permission each field needs. A field absent from this table can never be allowed,
        /// which is the safe direction: a new snapshot field starts out as "always report it".
        struct FieldPermission
        {
            const char *fieldName;
            uint32_t group;
        };
        const FieldPermission FIELD_PERMISSIONS[] = {
            {"mode", Allow::AllowView | Allow::AllowHome},
            {"homeIndex", Allow::AllowHome},
            {"page", Allow::AllowCursor},
            {"category", Allow::AllowCursor},
            {"box", Allow::AllowCursor},
            {"slot", Allow::AllowCursor},
            {"party", Allow::AllowCursor},
            {"settingsRow", Allow::AllowCursor},
            {"trainerRow", Allow::AllowCursor},
            {"detailView", Allow::AllowView},
            {"detailsModal", Allow::AllowModal},
            {"saveConfirm", Allow::AllowModal},
            {"swap", Allow::AllowSwap},
            {"storageMenu", Allow::AllowStorage},
            {"cursorMode", Allow::AllowStorage},
            {"focusPane", Allow::AllowStorage},
            {"stSaveSlot", Allow::AllowStorage},
            {"stBankBox", Allow::AllowStorage},
            {"stBankSlot", Allow::AllowStorage},
            {"goBack", Allow::AllowExit},
            {"exitRequested", Allow::AllowExit},
            {"unsaved", Allow::AllowUnsaved},
            {"TRAINER-DATA", Allow::AllowTrainer},
            {"PARTY-DATA", Allow::AllowParty},
            {"BOX-DATA", Allow::AllowBoxes},
            {"ITEM-DATA", Allow::AllowItems},
            {"BOXNAME-DATA", Allow::AllowBoxNames},
        };

        bool permitted(const char *field, uint32_t allow)
        {
            for (const auto &permission : FIELD_PERMISSIONS)
            {
                if (std::string(permission.fieldName) == field)
                {
                    return (permission.group & allow) != 0;
                }
            }
            return false;
        }

        std::string join(const std::vector<const char *> &fieldNames)
        {
            std::string joinedText;
            for (size_t fieldIndex = 0; fieldIndex < fieldNames.size(); ++fieldIndex)
            {
                if (fieldIndex)
                {
                    joinedText += ",";
                }
                joinedText += fieldNames[fieldIndex];
            }
            return joinedText.empty() ? std::string("nothing") : joinedText;
        }
    }

    Journey::Journey(Context &context, UI::UIScreen &screen, const UI::TrainerViewScreen *trainerViewScreen, const Trainer::Trainer *trainer, std::string name) : context(context), screen(screen), trainerViewScreen(trainerViewScreen), trainer(trainer), name(std::move(name))
    {
        if (this->trainerViewScreen && this->trainer)
        {
            snapshotBefore = UISnapshot::take(*this->trainerViewScreen, *this->trainer);
            hasSnapshotBefore = true;
        }
    }

    std::string Journey::where() const { return "journey=" + name; }

    // The console's own loop, with the pad built in memory instead of read from a controller.
    void Journey::pump(u64 held)
    {
        pad.buttons_old = pad.buttons_cur;
        pad.buttons_cur = held;
        screen.update(pad, touch);
    }

    bool Journey::lastChangedField(const char *field) const noexcept
    {
        for (const char *changedField : lastChangedFields)
        {
            if (std::strcmp(changedField, field) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void Journey::verify(const char *what, uint32_t allow)
    {
        ++stepCount;
        // A screen with no save behind it (the save picker) still gets a trace line. There is no
        // data to diff there, and saying so is better than a silent gap in the log.
        if (!trainerViewScreen || !trainer)
        {
            const std::string traceLine = "  [" + name + "] " + std::string(what) + " -> (no save state to diff)";
            if (context.report)
            {
                context.report->addTrace(traceLine);
            }
            Utils::logTest(traceLine);
            lastChangedText = "-";
            lastChangedFields.clear();
            lastChangeUndeclared = false;
            return;
        }
        const UISnapshot snapshotAfter = UISnapshot::take(*trainerViewScreen, *trainer);
        const std::vector<const char *> changedFields = hasSnapshotBefore ? snapshotBefore.diff(snapshotAfter) : std::vector<const char *>{};

        std::vector<const char *> undeclaredFields;
        for (const char *changedField : changedFields)
        {
            if (!permitted(changedField, allow))
            {
                undeclaredFields.push_back(changedField);
            }
        }

        // The change log the whole design is for: one line per action, whatever the outcome.
        const std::string traceLine = "  [" + name + "] " + what + " -> changed: " + join(changedFields) + (undeclaredFields.empty() ? "" : "   !! UNDECLARED: " + join(undeclaredFields));
        if (context.report)
        {
            context.report->addTrace(traceLine);
        }
        Utils::logTest(traceLine);

        lastChangedText = join(changedFields);
        lastChangedFields = changedFields;
        lastChangeUndeclared = !undeclaredFields.empty();

        for (const char *undeclaredField : undeclaredFields)
        {
            ++findingCount;
            context.fail("a UI action changed something it should not have", where() + " action=\"" + what + "\" field=" + undeclaredField + " allChanged=" + join(changedFields));
        }
        if (undeclaredFields.empty())
        {
            context.report ? context.report->pass() : void();
        }

        snapshotBefore = snapshotAfter;
        hasSnapshotBefore = true;
    }

    Journey &Journey::act(const char *what, u64 button, uint32_t allow, int presses)
    {
        for (int pressIndex = 0; pressIndex < presses; ++pressIndex)
        {
            pump(button);
            pump(0);
        }
        verify(what, allow);
        return *this;
    }

    Journey &Journey::settle(const char *what, uint32_t allow, int frames)
    {
        for (int frameIndex = 0; frameIndex < frames; ++frameIndex)
        {
            pump(0);
        }
        verify(what, allow);
        return *this;
    }

    Journey &Journey::idle(int frames)
    {
        for (int frameIndex = 0; frameIndex < frames; ++frameIndex)
        {
            pump(0);
        }
        return *this;
    }

    bool Journey::expect(bool passed, const std::string &what, const std::string &detail)
    {
        if (!passed)
        {
            ++findingCount;
        }
        return context.check(passed, what, where() + (detail.empty() ? "" : " " + detail));
    }
}
