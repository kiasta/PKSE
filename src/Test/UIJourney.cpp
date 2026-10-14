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

        inline void fold(uint64_t &h, const void *p, size_t byteCount)
        {
            const auto *b = static_cast<const unsigned char *>(p);
            for (size_t index = 0; index < byteCount; ++index)
            {
                h ^= b[index];
                h *= FNV_PRIME;
            }
        }
        inline void foldU64(uint64_t &h, uint64_t value) { fold(h, &value, sizeof value); }

        uint64_t fingerprintMon(const ::Pokemon::Pokemon *pk)
        {
            uint64_t hash = FNV_OFFSET;
            if (!pk)
            {
                foldU64(hash, 0xEEEEEEEEULL);
                return hash;
            } // absent differs from blank
            const auto recordBytes = pk->getData();
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
        snapshot.stSaveSlot = screen.stSaveSlot;
        snapshot.stBankBox = screen.stBankBox;
        snapshot.stBankSlot = screen.stBankSlot;
        snapshot.goBack = screen.goBack;
        snapshot.exitRequested = screen.exitRequested;
        snapshot.unsaved = screen.hasUnsavedChanges;

        uint64_t hash = FNV_OFFSET;
        fold(hash, trainer.trainerName.data(), trainer.trainerName.size());
        foldU64(hash, trainer.money);
        snapshot.trainerFp = hash;

        hash = FNV_OFFSET;
        for (const auto &pokemon : trainer.party)
            foldU64(hash, fingerprintMon(pokemon.get()));
        snapshot.partyFp = hash;

        // Every slot of every box, not just the open one. The cost is the point: a navigation
        // keypress that wrote to a box the user was not even looking at is exactly the bug that
        // would otherwise be found months later by a player.
        hash = FNV_OFFSET;
        for (const auto &box : trainer.boxes)
            for (const auto &pokemon : box)
                foldU64(hash, fingerprintMon(pokemon.get()));
        snapshot.boxFp = hash;

        hash = FNV_OFFSET;
        for (const auto &pouch : trainer.items)
            for (const auto &it : pouch)
            {
                foldU64(hash, it.itemId);
                foldU64(hash, it.count);
            }
        snapshot.itemFp = hash;

        hash = FNV_OFFSET;
        for (const auto &bn : trainer.boxNames)
            fold(hash, bn.data(), bn.size());
        snapshot.boxNameFp = hash;

        return snapshot;
    }

    std::vector<const char *> UISnapshot::diff(const UISnapshot &offset) const
    {
        std::vector<const char *> out;
        auto compareField = [&](bool same, const char *label)
        { if (!same) out.push_back(label); };
        compareField(mode == offset.mode, "mode");
        compareField(homeIndex == offset.homeIndex, "homeIndex");
        compareField(page == offset.page, "page");
        compareField(category == offset.category, "category");
        compareField(box == offset.box, "box");
        compareField(slot == offset.slot, "slot");
        compareField(party == offset.party, "party");
        compareField(settingsRow == offset.settingsRow, "settingsRow");
        compareField(trainerRow == offset.trainerRow, "trainerRow");
        compareField(detailView == offset.detailView, "detailView");
        compareField(detailsModal == offset.detailsModal, "detailsModal");
        compareField(saveConfirm == offset.saveConfirm, "saveConfirm");
        compareField(swap == offset.swap, "swap");
        compareField(storageMenu == offset.storageMenu, "storageMenu");
        compareField(cursorMode == offset.cursorMode, "cursorMode");
        compareField(focusPane == offset.focusPane, "focusPane");
        compareField(stSaveSlot == offset.stSaveSlot, "stSaveSlot");
        compareField(stBankBox == offset.stBankBox, "stBankBox");
        compareField(stBankSlot == offset.stBankSlot, "stBankSlot");
        compareField(goBack == offset.goBack, "goBack");
        compareField(exitRequested == offset.exitRequested, "exitRequested");
        compareField(unsaved == offset.unsaved, "unsaved");
        compareField(trainerFp == offset.trainerFp, "TRAINER-DATA");
        compareField(partyFp == offset.partyFp, "PARTY-DATA");
        compareField(boxFp == offset.boxFp, "BOX-DATA");
        compareField(itemFp == offset.itemFp, "ITEM-DATA");
        compareField(boxNameFp == offset.boxNameFp, "BOXNAME-DATA");
        return out;
    }

    namespace
    {
        /// Which permission each field needs. A field absent from this table can never be allowed,
        /// which is the safe direction: a new snapshot field starts out as "always report it".
        struct FieldPerm
        {
            const char *name;
            uint32_t group;
        };
        const FieldPerm fieldPermissions[] = {
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
            for (const auto &p : fieldPermissions)
                if (std::string(p.name) == field)
                    return (p.group & allow) != 0;
            return false;
        }

        std::string join(const std::vector<const char *> &value)
        {
            std::string text;
            for (size_t valueIndex = 0; valueIndex < value.size(); ++valueIndex)
            {
                if (valueIndex)
                    text += ",";
                text += value[valueIndex];
            }
            return text.empty() ? std::string("nothing") : text;
        }
    }

    Journey::Journey(Context &c, UI::UIScreen &s, const UI::TrainerViewScreen *t,
                     const Trainer::Trainer *trainerForSnapshot, std::string journeyName)
        : context(c), screen(s), tvs(t), trainer(trainerForSnapshot), name(std::move(journeyName))
    {
        if (tvs && trainer)
        {
            before = UISnapshot::take(*tvs, *trainer);
            haveBefore = true;
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
            if (std::strcmp(changedField, field) == 0)
                return true;
        return false;
    }

    void Journey::verify(const char *what, uint32_t allow)
    {
        ++stepCount;
        // A screen with no save behind it (the save picker) still gets a trace line. There is no
        // data to diff there, and saying so is better than a silent gap in the log.
        if (!tvs || !trainer)
        {
            const std::string line = "  [" + name + "] " + std::string(what) + " -> (no save state to diff)";
            if (context.rep)
                context.rep->addTrace(line);
            Utils::logTest(line);
            lastChangedText = "-";
            lastChangedFields.clear();
            lastBad = false;
            return;
        }
        const UISnapshot after = UISnapshot::take(*tvs, *trainer);
        const std::vector<const char *> changed = haveBefore ? before.diff(after)
                                                             : std::vector<const char *>{};

        std::vector<const char *> illegal;
        for (const char *f : changed)
            if (!permitted(f, allow))
                illegal.push_back(f);

        // The change log the whole design is for: one line per action, whatever the outcome.
        const std::string line = "  [" + name + "] " + what + " -> changed: " + join(changed) +
                                 (illegal.empty() ? "" : "   !! UNDECLARED: " + join(illegal));
        if (context.rep)
            context.rep->addTrace(line);
        Utils::logTest(line);

        lastChangedText = join(changed);
        lastChangedFields = changed;
        lastBad = !illegal.empty();

        for (const char *f : illegal)
        {
            ++findingCount;
            context.fail("a UI action changed something it should not have",
                     where() + " action=\"" + what + "\" field=" + f +
                         " allChanged=" + join(changed));
        }
        if (illegal.empty())
            context.rep ? context.rep->pass() : void();

        before = after;
        haveBefore = true;
    }

    Journey &Journey::act(const char *what, u64 button, uint32_t allow, int presses)
    {
        for (int index = 0; index < presses; ++index)
        {
            pump(button);
            pump(0);
        }
        verify(what, allow);
        return *this;
    }

    Journey &Journey::settle(const char *what, uint32_t allow, int frames)
    {
        for (int index = 0; index < frames; ++index)
            pump(0);
        verify(what, allow);
        return *this;
    }

    Journey &Journey::idle(int frames)
    {
        for (int index = 0; index < frames; ++index)
            pump(0);
        return *this;
    }

    bool Journey::expect(bool passed, const std::string &what, const std::string &detail)
    {
        if (!passed)
            ++findingCount;
        return context.check(passed, what, where() + (detail.empty() ? "" : " " + detail));
    }
}
