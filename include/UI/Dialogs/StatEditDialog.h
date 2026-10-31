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
            EV, // Effort Values (see legalEffortValueCap; total max 510 from Gen 3) or Gen 1/2 Stat Experience
            AV, // Awakening Values (0-200 each, no total cap) - Let's Go only
            HyperTrain // the stat's Hyper Training flag (Bottle Cap) - Gen 7 onward
        };

        /// Why the game could not have Hyper Trained this stat with the given IV, or empty when it
        /// could. Mirrors PKHeX's HyperTrainingVerifier, so the editor offers only what that accepts.
        /// Clearing a flag is never refused; this only gates setting one.
        std::string getHyperTrainBlockedReason(const Pokemon::Pokemon &pokemon, int individualValue);

        /// The most one stat's EV can hold without the edit being illegal: 252 from Gen 6 on, and
        /// the field's own maximum before that (255 in Gens 3-5, 65535 Stat Experience in Gens 1-2),
        /// as PKHeX's EffortValueVerifier rules. "Allow illegal values" lifts the editor to maxEV().
        int legalEffortValueCap(const Pokemon::Pokemon &pokemon);

        void drawStatEditDialog(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer);
    }
}

#endif
