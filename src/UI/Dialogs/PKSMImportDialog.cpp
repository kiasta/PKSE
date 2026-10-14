#include "UI/Dialogs/PKSMImportDialog.h"

#include <algorithm>
#include <string>
#include <vector>

#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/Dialogs/EditControls.h" // drawEditChoiceButton -- dialog buttons, drawn and hit-tested together
#include "UI/ScreenChrome.h"
#include "UI/Dialogs/DialogFrame.h"
#include "Trainer/Bank.h"

namespace UI
{
    namespace Dialogs
    {

        namespace
        {
            constexpr int BUTTON_HEIGHT = TouchTargetMin;

            /// One "label ............ value" row; the value is right-aligned so a column of numbers lines up.
            int statRow(PKSEFramebuffer &framebuffer, int rowX, int rowY, int rowWidth, const std::string &label, const std::string &value, Color labelCol, Color valueCol)
            {
                int labelWidth, lh;
                framebuffer.measureText(label, labelWidth, lh, TextStyle::Body);
                int valueWidth, vh;
                framebuffer.measureText(value, valueWidth, vh, TextStyle::Body);
                framebuffer.drawText(rowX, rowY, label, labelCol, TextStyle::Body);
                framebuffer.drawText(rowX + rowWidth - valueWidth, rowY, value, valueCol, TextStyle::Body);
                return std::max(lh, vh) + 8;
            }

            /// The per-generation table. Returns the height it used.
            int drawGenTable(PKSEFramebuffer &framebuffer, int genTableX, int genTableY, int genTableWidth, const Trainer::PKSMImportReport &report)
            {
                int used = 0;
                for (int pKSM_GENIndex = 0; pKSM_GENIndex < Trainer::PKSM_GEN_COUNT; ++pKSM_GENIndex)
                {
                    if (report.perGen[pKSM_GENIndex] == 0)
                        continue;
                    const bool isImportable = Trainer::pksmGenImportable(static_cast<uint32_t>(pKSM_GENIndex));
                    const std::string label = std::string("   ") + Trainer::pksmGenName(static_cast<uint32_t>(pKSM_GENIndex));
                    const std::string value = std::to_string(report.perGen[pKSM_GENIndex]) + (isImportable ? "   import" : "   no PKSE format");
                    used += statRow(framebuffer, genTableX, genTableY + used, genTableWidth, label, value, isImportable ? Colors::Text : Colors::TextDim, isImportable ? Colors::Text : Colors::TextDim);
                }
                return used;
            }

            int genTableRows(const Trainer::PKSMImportReport &report)
            {
                int count = 0;
                for (int pKSM_GENIndex = 0; pKSM_GENIndex < Trainer::PKSM_GEN_COUNT; ++pKSM_GENIndex)
                    if (report.perGen[pKSM_GENIndex])
                        ++count;
                return count;
            }

            /// Y for a centred card that must still clear the nav bar. Both of these dialogs size
            /// themselves from their content -- a bank holding all nine generations makes the preview
            /// tall enough to run under the footer if it is simply centred.
            int cardTop(PKSEFramebuffer &framebuffer, int cardHeight)
            {
                const int lowest = framebuffer.getHeight() - NAV_BAR_HEIGHT - cardHeight - 8;
                return std::max(8, std::min((framebuffer.getHeight() - cardHeight) / 2, lowest));
            }
        }

        void drawPKSMImportPreview(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            const auto &r = screen.pksmImport.importer.report();

            // Sized to the content: the generation table is however many generations the file holds, and
            // the notes below it appear only when they have something to say.
            const int genRows = genTableRows(r);
            const bool noteSkip = (r.unsupported > 0);
            const bool noteDamage = (r.damaged > 0);
            const bool noteG3 = (r.gen3NonFRLG > 0);
            const bool noteTight = (r.sourceBoxesUsed > screen.emptyBankBoxCount());
            const int noteLines = (noteTight ? 1 : 0) + (noteSkip ? 1 : 0) + (noteDamage ? 1 : 0) + (noteG3 ? 2 : 0);

            const int cardWidth = 900;
            const int cardHeight = 78 + 34 + 26 + genRows * 30 + 22 + 30 + noteLines * 24 + 26 + BUTTON_HEIGHT + 20;
            const int cardX = (framebuffer.getWidth() - cardWidth) / 2;
            const int cardY = cardTop(framebuffer, cardHeight);

            int centerY = drawDialogFrame(framebuffer, cardX, cardY, cardWidth, cardHeight, "Import from PKSM Bank", Colors::Text);
            const int centerX = cardX + 28, cw = cardWidth - 56;

            framebuffer.drawText(centerX, centerY, screen.pksmImport.fileName, Colors::Accent, TextStyle::Body);
            centerY += 34;

            framebuffer.drawText(centerX, centerY, "PKSM bank v" + std::to_string(r.containerVersion) + "  -  " + std::to_string(r.sourceBoxes) + " boxes, " + std::to_string(r.occupied) + " Pokemon stored", Colors::TextDim, TextStyle::Caption);
            centerY += 26;

            centerY += drawGenTable(framebuffer, centerX, centerY, cw, r);
            centerY += 10;
            framebuffer.drawHDivider(centerX, centerY, cw);
            centerY += 12;

            // The headline: how many arrive, and into how many boxes. Each PKSM box that holds anything
            // is copied whole into an empty bank box, so the box count is what decides whether it fits --
            // not the Pokemon count.
            framebuffer.drawText(centerX, centerY, std::to_string(r.importable) + " Pokemon will be added, filling " + std::to_string(r.sourceBoxesUsed) + (r.sourceBoxesUsed == 1 ? " empty bank box." : " empty bank boxes."), Colors::Text, TextStyle::Body);
            centerY += 30;

            if (r.sourceBoxesUsed > screen.emptyBankBoxCount())
            {
                framebuffer.drawText(centerX, centerY, "Only " + std::to_string(screen.emptyBankBoxCount()) + " bank boxes are empty; the rest will go into any free slots.", Colors::Warning, TextStyle::Caption);
                centerY += 24;
            }

            if (noteSkip)
            {
                framebuffer.drawText(centerX, centerY, std::to_string(r.unsupported) + " will be skipped - PKSE has no format for their generation.", Colors::Warning, TextStyle::Caption);
                centerY += 24;
            }
            if (noteDamage)
            {
                framebuffer.drawText(centerX, centerY, std::to_string(r.damaged) + " records could not be read and will be skipped.", Colors::Warning, TextStyle::Caption);
                centerY += 24;
            }
            if (noteG3)
            {
                framebuffer.drawText(centerX, centerY, std::to_string(r.gen3NonFRLG) + " Gen 3 records list an origin other than FireRed/LeafGreen.", Colors::TextDim, TextStyle::Caption);
                centerY += 24;
                framebuffer.drawText(centerX, centerY, "They import and behave normally; PKSE has no encounter data for them.", Colors::TextDim, TextStyle::Caption);
                centerY += 24;
            }

            centerY += 4;
            framebuffer.drawText(centerX, centerY, "Nothing is written until you choose Save when leaving Storage.", Colors::TextDim, TextStyle::Caption);

            // Buttons. Ids match the storage dialogs' convention: 0 = cancel, 1 = confirm.
            const int buttonY = cardY + cardHeight - BUTTON_HEIGHT - 16, bw = (cw - 16) / 2;
            screen.touchButtons.clear();
            drawEditChoiceButton(screen, framebuffer, centerX, buttonY, bw, BUTTON_HEIGHT, "B", "Cancel", 0);
            drawEditChoiceButton(screen, framebuffer, centerX + bw + 16, buttonY, bw, BUTTON_HEIGHT, "A", "Import", 1, Colors::Primary, Colors::PrimaryText);
        }

        void drawPKSMImportResult(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            const auto &state = screen.pksmImport;
            const auto &r = state.importer.report();
            const bool failed = !r.error.empty();

            // Three outcomes share this screen: the file was unusable, the file was fine but held
            // nothing PKSE can store, and a completed import. They differ in what there is to say.
            std::string title, headline;
            if (failed)
            {
                title = "Can't Import This File";
                headline = r.error;
            }
            else if (!state.committed)
            {
                title = "Nothing to Import";
                headline = "This bank holds no Pokemon in a format PKSE can store.";
            }
            else
            {
                title = "Import Complete";
                headline = std::to_string(r.placed) + " Pokemon added to the bank.";
            }

            struct Row
            {
                std::string label, value;
                bool warn;
            };
            std::vector<Row> rows;
            if (!failed)
            {
                if (state.committed)
                {
                    rows.push_back({"Bank boxes used", std::to_string(r.boxesUsed), false});
                    if (r.namesCarried)
                        rows.push_back({"Box names carried over", std::to_string(r.namesCarried), false});
                    if (r.scattered)
                        rows.push_back({"Placed in free slots (no empty box left)", std::to_string(r.scattered), true});
                    if (r.overflow)
                        rows.push_back({"Didn't fit - bank is full", std::to_string(r.overflow), true});
                }
                if (r.unsupported)
                    rows.push_back({"Skipped - no PKSE format", std::to_string(r.unsupported), true});
                if (r.damaged)
                    rows.push_back({"Skipped - unreadable record", std::to_string(r.damaged), true});
            }

            // Wrap the headline by hand: an error string can be a sentence and a half.
            std::vector<std::string> headlineLines;
            {
                const int maxW = 900 - 56;
                std::string line;
                size_t searchStart = 0;
                while (searchStart <= headline.size())
                {
                    const size_t spacePosition = headline.find(' ', searchStart);
                    const std::string word = headline.substr(searchStart, (spacePosition == std::string::npos ? headline.size() : spacePosition) - searchStart);
                    const std::string candidate = line.empty() ? word : line + " " + word;
                    int textWidth, th;
                    framebuffer.measureText(candidate, textWidth, th, TextStyle::Body);
                    if (textWidth > maxW && !line.empty())
                    {
                        headlineLines.push_back(line);
                        line = word;
                    }
                    else
                    {
                        line = candidate;
                    }
                    if (spacePosition == std::string::npos)
                        break;
                    searchStart = spacePosition + 1;
                }
                if (!line.empty())
                    headlineLines.push_back(line);
            }

            const bool tail = state.committed && r.placed > 0;
            const int cardWidth = 900;
            const int cardHeight = 78 + static_cast<int>(headlineLines.size()) * 30 + 14 + static_cast<int>(rows.size()) * 30 + (tail ? 34 : 0) + 18 + BUTTON_HEIGHT + 20;
            const int cardX = (framebuffer.getWidth() - cardWidth) / 2;
            const int cardY = cardTop(framebuffer, cardHeight);

            int centerY = drawDialogFrame(framebuffer, cardX, cardY, cardWidth, cardHeight, title, failed ? Colors::Warning : Colors::Text);
            const int centerX = cardX + 28, cw = cardWidth - 56;

            for (const auto &l : headlineLines)
            {
                framebuffer.drawText(centerX, centerY, l, Colors::Text, TextStyle::Body);
                centerY += 30;
            }
            if (!rows.empty())
            {
                centerY += 6;
                framebuffer.drawHDivider(centerX, centerY, cw);
                centerY += 10;
                for (const auto &row : rows)
                    centerY += statRow(framebuffer, centerX, centerY, cw, "   " + row.label, row.value, Colors::TextDim, row.warn ? Colors::Warning : Colors::Text);
            }
            if (tail)
            {
                centerY += 8;
                framebuffer.drawText(centerX, centerY, "Choose Save when you leave Storage to keep them.", Colors::TextDim, TextStyle::Caption);
            }

            const int buttonY = cardY + cardHeight - BUTTON_HEIGHT - 16, bw = 220;
            const int boxX = cardX + (cardWidth - bw) / 2;
            screen.touchButtons.clear();
            // The only button on this dialog, so it takes the cancel id -- there is nothing to
            // confirm, just an outcome to dismiss.
            drawEditChoiceButton(screen, framebuffer, boxX, buttonY, bw, BUTTON_HEIGHT, "A", "OK", 0, Colors::Primary, Colors::PrimaryText);
        }
    }
}
