#ifndef UI_DIALOGS_PICKER_DIALOG_H
#define UI_DIALOGS_PICKER_DIALOG_H

// Forward declarations
namespace UI {
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI {
namespace Dialogs {
    // What the reusable selection panel is choosing. Determines the option list and how a
    // pick is applied. For Move, TrainerViewScreen::pickerSlot holds which of the 4 slots.
    enum class PickerKind {
        Nature,   // 25 natures
        Gender,   // Male / Female / Genderless
        Move,       // full move list
        Item,       // held-item list (modern ids)
        ItemG3,     // held-item list for a Gen 3 mon -- SEPARATE, much smaller id space
        Level,      // level 1-100
        Friendship, // 0-255
        Ball,       // ball ids
        Ability,    // ability ids (our table covers 1-261)
        Language,   // language ids 1-10
        Origin,     // origin game-version ids (0-52; see Enums::GameVersion)
        MetLevel,   // met level 1-100
        MetLocation,// met-location ids for the mon's origin -- ids supplied via pickerOrder,
                    //   labelled through screen.pickerMetVersion (the picker draw special-cases it)
        Form,       // alternate-form index -- count via pickerCount, labelled by screen.pickerFormSpecies
        StatNature, // 25 natures, applied to the STAT (mint) nature rather than the real one
        Species,    // 0-1025 (creator: choose the species for a new mon)
        // Item creation: the ids that legally belong in the pouch being viewed, supplied
        // through pickerOrder. Two kinds because Gen 3 has its own item id space and name table.
        PouchItem,  // add-to-pouch list, modern item ids
        PouchItemG3 // add-to-pouch list, Gen 3 item ids (FireRed/LeafGreen)
    };

    // Number of options for a kind.
    int pickerOptionCount(PickerKind kind);
    // Display label for an option index of a kind (nature/gender/move name).
    const char* pickerOptionLabel(PickerKind kind, int index);
    // Panel title.
    const char* pickerTitle(PickerKind kind);
    // Whether this picker can filter its options by name.
    bool pickerSupportsSearch(PickerKind kind);

    // Draws the centered scrollable picker panel and registers a touch button per visible row
    // (button id = the option's index).
    void drawPickerDialog(UI::TrainerViewScreen& screen, UI::PKSEFramebuffer& fb);
}
}

#endif
