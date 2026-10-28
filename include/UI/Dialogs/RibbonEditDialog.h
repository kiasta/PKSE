#ifndef UI_DIALOGS_RIBBONEDITDIALOG_H
#define UI_DIALOGS_RIBBONEDITDIALOG_H

namespace UI
{
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI
{
    namespace Dialogs
    {
        // The ribbon editor's grid: every ribbon and mark the format stores, one tile each. The
        // input handler steps the cursor and scrolls by these, so they live here rather than in
        // the draw alone. Sixteen columns by seven rows holds the largest table (the HOME block's
        // 111 rows) without scrolling; a longer one would scroll by row.
        constexpr int RIBBON_GRID_COLUMNS = 16;
        constexpr int RIBBON_GRID_VISIBLE_ROWS = 7;
        // A tile's touch id is this plus its index in the format's ribbon table.
        constexpr int RIBBON_TILE_BUTTON_ID_BASE = 200;

        void drawRibbonEditDialog(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer);
    }
}

#endif
