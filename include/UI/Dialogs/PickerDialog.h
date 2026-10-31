#ifndef UI_DIALOGS_PICKER_DIALOG_H
#define UI_DIALOGS_PICKER_DIALOG_H

#include <string>

namespace UI
{
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI
{
    namespace Dialogs
    {
        // What the reusable selection panel is choosing. Determines the option list and how a
        // pick is applied. For Move, TrainerViewScreen::pickerSlot holds which of the 4 slots.
        enum class PickerKind
        {
            Nature,      // 25 natures
            Gender,      // Male / Female / Genderless
            Move,        // full move list
            Item,        // held-item list (modern ids)
            ItemG3,      // held-item list for a Gen 3 pokemon -- SEPARATE, much smaller id space
            ItemG1,      // held-item list for a Gen 1 pokemon. Gen 1 has no held items, so this exists
                         //   only so the pouch kind below has a sibling; it is never opened.
            ItemG2,      // held-item list for a Gen 2 pokemon -- its own one-byte id space and names
            Level,       // level 1-100
            Friendship,  // 0-255
            Ball,        // ball ids
            Ability,     // ability ids (our table covers 1-261)
            Language,    // language ids 1-10
            Origin,      // origin game-version ids (0-52; see Enums::GameVersion)
            MetLevel,    // met level 1-100
            MetLocation, // met-location ids for the pokemon's origin -- ids supplied via pickerOrder,
                         //   labelled through screen.pickerMetVersion (the picker draw special-cases it)
            Form,        // alternate-form index -- count via pickerCount, labelled by screen.pickerFormSpecies
            StatNature,  // 25 natures, applied to the STAT (mint) nature rather than the real one
            Species,     // 0-1025 (creator: choose the species for a new pokemon)
            // Item creation: the ids that legally belong in the pouch being viewed, supplied
            // through pickerOrder. One kind per item id space: modern, Gen 1, Gen 2 and Gen 3.
            PouchItem,   // add-to-pouch list, modern item ids
            PouchItemG1, // add-to-pouch list, Gen 1 item ids (its TMs are ordinary bag items)
            PouchItemG3, // add-to-pouch list, Gen 3 item ids (all five GBA games)
            PouchItemG2  // add-to-pouch list, Gen 2 item ids
        };

        int pickerOptionCount(PickerKind kind);
        // Display label for an option index of a kind (nature/gender/move name).
        const char *pickerOptionLabel(PickerKind kind, int index);
        const char *pickerTitle(PickerKind kind);

        // The label a row actually SHOWS, including the two kinds that need the pokemon's context:
        // Met Location resolves a location id through the origin version, and Form resolves a form
        // index through the species. Both are invisible to the context-free pickerOptionLabel().
        //
        // The search filter and the draw both go through this on purpose. Filtering on anything
        // other than the string being drawn hides rows the user can plainly see, which is the one
        // way a search box is worse than no search box at all.
        std::string pickerRowLabel(const UI::TrainerViewScreen &screen, PickerKind kind, int value);

        // Draws the centered scrollable picker panel and registers a touch button per visible row
        // (button id = the option's index).
        void drawPickerDialog(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer);
    }
}

#endif
