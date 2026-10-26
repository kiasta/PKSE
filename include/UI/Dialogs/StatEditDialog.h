#ifndef UI_DIALOGS_STAT_EDIT_DIALOG_H
#define UI_DIALOGS_STAT_EDIT_DIALOG_H

#include <string>

namespace Pokemon
{
    class Pokemon;
}

namespace UI
{
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI
{
    namespace Dialogs
    {
        enum class StatEditMode
        {
            IV, // Individual Values (0-31)
            EV, // Effort Values (0-252, total max 510) - Gen 8/9
            AV, // Awakening Values (0-200 each, no total cap) - Let's Go only
            HyperTrain // the stat's Hyper Training flag (Bottle Cap) - Gen 7 onward
        };

        /// Why the game could not have Hyper Trained this stat with the given IV, or empty when it
        /// could. Mirrors PKHeX's HyperTrainingVerifier, so the editor offers only what that accepts.
        /// Clearing a flag is never refused; this only gates setting one.
        std::string getHyperTrainBlockedReason(const Pokemon::Pokemon &pokemon, int individualValue);

        void drawStatEditDialog(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer);
    }
}

#endif
