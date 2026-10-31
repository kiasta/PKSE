#include <algorithm>
#include <string>

#include "UI/Dialogs/StatEditDialog.h"
#include "UI/Dialogs/DialogFrame.h"
#include "UI/Dialogs/EditControls.h" // shared step buttons + A/B choice buttons
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Globals.h"
#include "Enums/GameVersion.h"

using namespace Trainer;

namespace UI
{
    namespace Dialogs
    {
        int legalEffortValueCap(const Pokemon::Pokemon &pokemon)
        {
            constexpr int modernEffortValueCap = 252;
            constexpr int firstGenerationWithModernCap = 6;
            const int fieldMaxEffortValue = pokemon.maxEV();
            const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup()));
            if (formatGeneration < firstGenerationWithModernCap)
                return fieldMaxEffortValue;
            return std::min(fieldMaxEffortValue, modernEffortValueCap);
        }

        std::string getHyperTrainBlockedReason(const Pokemon::Pokemon &pokemon, int individualValue)
        {
            if (!pokemon.isHyperTrainingAvailable())
                return "Not available";
            // A box record in the packed formats carries no level byte; EXP is the authority there.
            uint8_t levelValue = pokemon.level();
            if (levelValue == 0)
                levelValue = Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(pokemon.speciesID()));
            if (levelValue < pokemon.hyperTrainMinimumLevel())
                return "Needs Lv. " + std::to_string(pokemon.hyperTrainMinimumLevel());
            if (individualValue >= pokemon.maxIV())
                return "IV is already " + std::to_string(pokemon.maxIV());
            return std::string();
        }

        void drawStatEditDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            // Resolve the Pokemon being edited (party / box / bank).
            const Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
            if (!pokemon)
                return;

            // Let's Go uses Awakening Values (AVs) instead of EVs (EVs are inert in that game),
            // so the second editable stat is AV for LGPE Pokemon and EV for everything else.
            const bool usesAV = pokemon->hasAwakeningValues();
            // Gen 1/2 have no IVs or EVs: they store 4-bit DVs and a 16-bit Stat Experience counter.
            // Labelling those "IV 0-31" and "EV 0-252" would misname the quantity AND misstate its
            // range, so both come from the format itself.
            const int fmtMaxIV = pokemon->maxIV();
            const int fmtMaxEV = pokemon->maxEV();
            const bool gbStats = (fmtMaxEV > 255); // Stat Experience rather than EVs
            const char *ivLabel = (fmtMaxIV < 31) ? "DV" : "IV";
            const char *evLabel = usesAV ? "AV" : (gbStats ? "Exp" : "EV");

            const char *statNames[] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
            const char *statName = statNames[screen.statEdit.selectedStat];

            const bool showsHyperTraining = pokemon->hasHyperTraining();
            constexpr int dialogWidth = 600;
            const int dialogHeight = showsHyperTraining ? 426 : 384;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = (framebuffer.getHeight() - dialogHeight) / 2;
            int centerY = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, dialogHeight, std::string("Edit ") + statName, Colors::Text);

            screen.touchButtons.clear();

            const bool ivMode = (screen.statEdit.mode == StatEditMode::IV);
            const int labelX = dialogX + 28, valX = dialogX + 180, rangeX = dialogX + dialogWidth - 150;

            // IV row (tap -> select IV, id 10)
            if (ivMode)
                framebuffer.drawSelectionHighlight(dialogX + 16, centerY - 4, dialogWidth - 32, 34);
            framebuffer.drawText(labelX, centerY + 2, ivLabel, ivMode ? Colors::Text : Colors::TextDim);
            framebuffer.drawText(valX, centerY + 2, std::to_string(screen.statEdit.currentIV), ivMode ? Colors::Accent : Colors::Text);
            framebuffer.drawText(rangeX, centerY + 4, "(0 - " + std::to_string(fmtMaxIV) + ")", Colors::TextDim, TextStyle::Caption);
            screen.touchButtons.push_back({10, dialogX + 16, centerY - 4, dialogWidth - 32, 34});
            centerY += 42;

            // Hyper Training row, under the IV it overrides (tap -> select it, tap again -> toggle, id 12).
            const bool hyperTrainMode = (screen.statEdit.mode == StatEditMode::HyperTrain);
            if (showsHyperTraining)
            {
                if (hyperTrainMode)
                    framebuffer.drawSelectionHighlight(dialogX + 16, centerY - 4, dialogWidth - 32, 34);
                framebuffer.drawText(labelX, centerY + 2, "Hyper Trained", hyperTrainMode ? Colors::Text : Colors::TextDim);
                framebuffer.drawText(valX, centerY + 2, screen.statEdit.currentHyperTrained ? "Yes" : "No", hyperTrainMode ? Colors::Accent : Colors::Text);
                const std::string blockedReason = getHyperTrainBlockedReason(*pokemon, screen.statEdit.currentIV);
                const std::string hyperTrainNote = (screen.statEdit.currentHyperTrained || blockedReason.empty()) ? "Plays as " + std::to_string(screen.statEdit.currentHyperTrained ? fmtMaxIV : screen.statEdit.currentIV) : blockedReason;
                framebuffer.drawText(rangeX, centerY + 4, hyperTrainNote, Colors::TextDim, TextStyle::Caption);
                screen.touchButtons.push_back({12, dialogX + 16, centerY - 4, dialogWidth - 32, 34});
                centerY += 42;
            }

            // Second row: AV (Let's Go) or EV (tap -> select it, id 11)
            const bool secondMode = !ivMode && !hyperTrainMode;
            if (secondMode)
                framebuffer.drawSelectionHighlight(dialogX + 16, centerY - 4, dialogWidth - 32, 34);
            framebuffer.drawText(labelX, centerY + 2, evLabel, secondMode ? Colors::Text : Colors::TextDim);
            framebuffer.drawText(valX, centerY + 2, std::to_string(usesAV ? screen.statEdit.currentAV : screen.statEdit.currentEV), secondMode ? Colors::Accent : Colors::Text);
            {
                const std::string evRange = usesAV ? (g_allowIllegalEdits ? std::string("(0 - 255)") : std::string("(0 - 200)")) : ("(0 - " + std::to_string(g_allowIllegalEdits ? fmtMaxEV : legalEffortValueCap(*pokemon)) + ")");
                framebuffer.drawText(rangeX, centerY + 4, evRange, Colors::TextDim, TextStyle::Caption);
            }
            screen.touchButtons.push_back({11, dialogX + 16, centerY - 4, dialogWidth - 32, 34});
            centerY += 44;

            if (usesAV)
            {
                framebuffer.drawText(labelX, centerY, "Awakening Values (each 0-200, no total cap)", Colors::TextDim, TextStyle::Caption);
            }
            else if (gbStats)
            {
                // No running total, because Gen 1/2 impose none -- every stat can hold the maximum at
                // once. Printing an "x / 510" bar here would invent a budget the games do not have.
                // What IS worth saying is that the contribution is a square root, so the last few
                // thousand points are worth almost nothing and the number is not a linear stat bonus.
                framebuffer.drawText(labelX, centerY, "Stat Experience (each 0-65535, no total cap; adds via sqrt)", Colors::TextDim, TextStyle::Caption);
            }
            else
            {
                uint8_t currentEV = 0;
                switch (screen.statEdit.selectedStat)
                {
                case 0:
                    currentEV = pokemon->evHP();
                    break;
                case 1:
                    currentEV = pokemon->evATK();
                    break;
                case 2:
                    currentEV = pokemon->evDEF();
                    break;
                case 3:
                    currentEV = pokemon->evSPA();
                    break;
                case 4:
                    currentEV = pokemon->evSPD();
                    break;
                case 5:
                    currentEV = pokemon->evSPE();
                    break;
                }
                int totalEVs = pokemon->evHP() + pokemon->evATK() + pokemon->evDEF() + pokemon->evSPE() + pokemon->evSPA() + pokemon->evSPD();
                int projectedTotal = totalEVs - currentEV + screen.statEdit.currentEV;
                bool over = projectedTotal > 510;
                framebuffer.drawText(labelX, centerY, "Total EVs: " + std::to_string(projectedTotal) + " / 510", over ? Colors::Red : Colors::TextDim, TextStyle::Caption);
            }

            // Step buttons (ZL -100 | L -10 | < -1 | > +1 | R +10 | ZR +100), then Cancel / Save labelled
            // with B / A. Up/Down still step between the rows (handled in the input path).
            const int choiceButtonHeight = TouchTargetMin, cby = dialogY + dialogHeight - choiceButtonHeight - 14;
            drawEditStepRow(screen, framebuffer, dialogX, dialogWidth, cby - 58 - 14, 58);

            const int choiceButtonWidth = 190;
            drawEditChoiceButton(screen, framebuffer, dialogX + 24, cby, choiceButtonWidth, choiceButtonHeight, "B", "Cancel", 0);
            drawEditChoiceButton(screen, framebuffer, dialogX + dialogWidth - 24 - choiceButtonWidth, cby, choiceButtonWidth, choiceButtonHeight, "A", "Save", 1);
        }
    }
}
