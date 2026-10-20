#ifndef UI_PANELS_STORAGE_PANEL_H
#define UI_PANELS_STORAGE_PANEL_H

namespace UI
{
    class TrainerViewScreen;
    class PKSEFramebuffer;

    namespace Panels
    {
        /**
         * Draws the HOME-style dual-pane storage view: the save's PC boxes on the left and the
         * persistent on-SD bank on the right. The cursor only appears once the view is entered
         * (like Boxes): a pointer arrow colored by the active CursorMode (Menu = red, Move = blue,
         * Multi = green). In Multi mode a green rectangle rubber-bands out from the anchor cell,
         * and once grabbed the whole block rides the cursor -- drawn on top of both panes so the
         * Pokemon stay visible the entire way across the screen.
         */
        void drawStorageView(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int storageViewX, int storageViewY, int storageViewWidth, int storageViewHeight);

        /// Per-Pokemon action menu (Move / Edit / Clone / Export / Release / Cancel).
        void drawStorageActionMenu(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// The box menu (Sort / Search / Cancel), opened with X in the Boxes and Storage views.
        void drawStorageBoxMenu(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// Options for the block in hand (Release all / Put back / Cancel), opened with Minus.
        void drawStorageGroupMenu(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// Release confirmation (single Pokemon or the whole carried block).
        void drawStorageReleaseConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// Save / Discard / Cancel prompt shown when leaving the storage view with bank changes.
        void drawStorageExitConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        void drawCreatorKeepConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        void drawDetailsDiscardConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// Which of two trade evolutions to perform. Raised only when the record cannot say --
        /// Clamperl holding neither of its items -- and for no other species in any game.
        void drawTradeEvolveChoice(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// "Trade Evolve": real save, generated trainer, or cancel. Gen 6+ only -- before that a
        /// trade writes nothing to the record, so there is nothing to choose.
        void drawTradeEvolveConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// The console saves that could be the other side of a trade for the Pokemon being
        /// evolved. A short filtered list, not the save picker's grid.
        void drawTradePartnerTitlePicker(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// The three lossy-transfer acknowledgements, all gated by g_moveWarn -- the one Move warning
        /// setting covers every bank transfer warning, in every generation. Which one is shown when a
        /// move matches more than one goes most-destructive first, in the order below.
        ///
        /// "Convert to Gen 3?" -- the PID is rebuilt and cannot be undone.
        void drawGen3ConvertConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// "Transfer out of Gen 1/2?" -- Poke Transporter rewrites more of a Pokemon than any other
        /// conversion, and it is the one place the user is told the origin game is a guess.
        void drawVirtualConsoleTransferConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
        /// "Moving to/from Let's Go resets AVs/EVs" -- recoverable by re-training.
        void drawLgpeTransferConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer);
    }
}

#endif
