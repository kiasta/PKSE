#ifndef UI_PANELS_ITEMS_PANEL_H
#define UI_PANELS_ITEMS_PANEL_H

#include "Enums/GameVersion.h"

namespace UI
{
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace Trainer
{
    class Trainer;
}

namespace UI
{
    namespace Panels
    {
        void drawItems(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer, int itemsX, int itemsY, int itemsWidth, int itemsHeight);

        /// Display name of a pouch/category index in the trainer's bag ("Medicine", "TMs", ...).
        /// The trainer answers first; groups whose trainer does not fall back to this file's table.
        ///
        /// Shared rather than inlined at the header draw.
        /// Never returns null -- an out-of-range category yields "?" so a log line cannot end
        /// up formatting a null pointer.
        const char *pouchDisplayName(const Trainer::Trainer &trainer, int category);
    }
}

#endif
