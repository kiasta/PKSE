#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "UI/Panels/HomeMenuPanel.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/SpriteManager.h"
#include "UI/Panels/UnmappedSlot.h"
#include "Trainer/Trainer.h"
#include "Trainer/Bank.h"
#include "Pokemon/Pokemon.h"

using namespace Trainer;

namespace UI
{
    namespace Panels
    {

        // Count live (non-empty) Pokemon in a boxes container.
        template <typename Boxes>
        static int countMons(const Boxes &boxes)
        {
            int count = 0;
            for (const auto &box : boxes)
                for (const auto &pokemon : box)
                    if (pokemon && pokemon->speciesID() != 0)
                        ++count;
            return count;
        }

        // Draw a compact, non-interactive preview of the current box (disc + sprite per slot).
        static void drawBoxPreview(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int boxPreviewX, int boxPreviewY, int boxPreviewWidth, int boxPreviewHeight)
        {
            constexpr int headerH = 40;
            framebuffer.drawFilledRoundedRect(boxPreviewX, boxPreviewY, boxPreviewWidth, headerH, 16, Colors::AccentDim);
            framebuffer.drawFilledRect(boxPreviewX, boxPreviewY + headerH - 16, boxPreviewWidth, 16, Colors::AccentDim);

            const int box = screen.selectedBoxIndex;
            const bool inRange = box >= 0 && box < static_cast<int>(screen.trainer.boxes.size());
            std::string boxName = (inRange && box < static_cast<int>(screen.trainer.boxNames.size())) ? screen.trainer.boxNames[box] : ("Box " + std::to_string(box + 1));
            int boxNameWidth, bnH;
            framebuffer.measureText(boxName, boxNameWidth, bnH, TextStyle::Body);
            framebuffer.drawText(boxPreviewX + (boxPreviewWidth - boxNameWidth) / 2, boxPreviewY + (headerH - bnH) / 2, boxName, Colors::Text);
            if (!inRange)
                return;

            const int slotsPerBox = static_cast<int>(screen.trainer.getSlotsPerBox());
            const int cols = boxGridColumns(slotsPerBox);
            const int rows = 5;
            const int gridX = boxPreviewX + 16, gy = boxPreviewY + headerH + 10;
            const int gridWidth = boxPreviewWidth - 32, gh = boxPreviewHeight - headerH - 20;
            const int colPitch = gridWidth / cols, rowPitch = gh / rows;
            int discR = std::min(colPitch, rowPitch) / 2 - 5;
            if (discR < 14)
                discR = 14;

            const auto &curBox = screen.trainer.boxes[box];
            for (int rIndex = 0; rIndex < rows; ++rIndex)
            {
                for (int columnIndex = 0; columnIndex < cols; ++columnIndex)
                {
                    const int index = rIndex * cols + columnIndex;
                    const int centerX = gridX + columnIndex * colPitch + colPitch / 2;
                    const int centerY = gy + rIndex * rowPitch + rowPitch / 2;
                    const auto &pokemon = curBox[index];
                    const bool empty = !pokemon || pokemon->speciesID() == 0;
                    framebuffer.drawFilledCircle(centerX, centerY, discR, empty ? Colors::Panel : Colors::PanelAlt);
                    if (empty)
                    {
                        framebuffer.drawCircle(centerX, centerY, discR, Colors::Border, 1);
                        if (screen.trainer.isBoxSlotUnmapped(static_cast<size_t>(box), static_cast<size_t>(index)))
                            drawUnmappedSlot(framebuffer, centerX, centerY, discR);
                        continue;
                    }
                    int markerSize = std::min(static_cast<int>(discR * 1.8), colPitch - 4);
                    if (pokemon->isEgg())
                    {
                        framebuffer.drawEgg(centerX, centerY, markerSize); // eggs show as an egg in the box preview too
                    }
                    else
                    {
                        bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
                        Sprite *internalSpeciesId = SpriteManager::getIconSprite(pokemon->speciesID(), pokemon->form(), shiny);
                        if (internalSpeciesId && internalSpeciesId->data)
                        {
                            framebuffer.drawImageScaled(centerX - markerSize / 2, centerY - markerSize / 2, internalSpeciesId->width, internalSpeciesId->height, markerSize, markerSize, internalSpeciesId->data, internalSpeciesId->channels);
                        }
                    }
                }
            }
        }

        // Simple vector icons for the menu's circular buttons (no icon assets). `col` = glyph color,
        // `bg` = the button fill (used to punch holes). kind: 0 Items (bag), 1 Trainer (person), 2 Settings (gear).
        static void drawMenuIcon(PKSEFramebuffer &framebuffer, int centerX, int centerY, int cornerRadius, int kind, Color iconColor, Color backgroundColor)
        {
            if (kind == 0)
            { // Items: a satchel/bag
                const int boxWidth = 2 * cornerRadius, bh = 2 * cornerRadius - 4;
                const int boxX = centerX - boxWidth / 2, by = centerY - bh / 2 + 4;
                framebuffer.drawFilledRoundedRect(boxX, by, boxWidth, bh, 8, iconColor);                    // body
                framebuffer.drawFilledRoundedRect(centerX - cornerRadius + 4, by - 8, 2 * cornerRadius - 8, 14, 6, iconColor); // flap
                // clasp (punch)
                framebuffer.drawFilledRoundedRect(centerX - 3, centerY - 2, 6, bh / 2, 3, backgroundColor);
            }
            else if (kind == 1)
            {                                                        // Trainer: a person bust
                // head
                framebuffer.drawFilledCircle(centerX, centerY - cornerRadius / 2, cornerRadius / 2 - 1, iconColor);
                const int shapeWidth = 2 * cornerRadius - 6, sh = cornerRadius + 2;
                framebuffer.drawFilledRoundedRect(centerX - shapeWidth / 2, centerY + 2, shapeWidth, sh, shapeWidth / 2, iconColor); // shoulders
            }
            else
            { // Settings: a gear
                const double piRadians = 3.14159265358979323846;
                for (int index = 0; index < 8; ++index)
                {
                    double spokeAngleRadians = index * piRadians / 4.0;
                    int spokeX = centerX + static_cast<int>(std::lround(std::cos(spokeAngleRadians) * (cornerRadius - 2)));
                    int spokeY = centerY + static_cast<int>(std::lround(std::sin(spokeAngleRadians) * (cornerRadius - 2)));
                    framebuffer.drawFilledCircle(spokeX, spokeY, 4, iconColor); // teeth
                }
                framebuffer.drawFilledCircle(centerX, centerY, cornerRadius - 6, iconColor);      // body
                framebuffer.drawFilledCircle(centerX, centerY, (cornerRadius - 6) / 2, backgroundColor); // hole
            }
        }

        void drawHomeMenu(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            screen.touchButtons.clear();

            const int previewX = 32, pY = 96, pW = 560, pH = 540;
            framebuffer.drawSoftShadow(previewX, pY, pW, pH, 16);
            framebuffer.drawFilledRoundedRect(previewX, pY, pW, pH, 16, Colors::Panel);
            drawBoxPreview(screen, framebuffer, previewX, pY, pW, pH);
            framebuffer.drawRoundedRect(previewX, pY, pW, pH, 16, Colors::Border, 1);

            const int rightColumnX = 648, rW = 600;
            const int stored = countMons(screen.trainer.boxes);
            // A kept Gen 1/2 entry PKSE cannot read still fills a party slot.
            int party = static_cast<int>(screen.trainer.getUnmappedPartySlots().size());
            for (const auto &m : screen.trainer.party)
                if (m && m->speciesID() != 0)
                    ++party;
            int bankN = 0;
            if (screen.bank)
                bankN = countMons(screen.bank->boxes);

            struct Pill
            {
                const char *label;
                std::string subtitle;
                int menuIndex;
            };
            Pill pills[3] = {
                {"Pokémon", std::to_string(stored) + " stored", 0},
                {"Party", std::to_string(party) + " / 6", 1},
                {"Storage", std::to_string(bankN) + " in bank", 2},
            };
            const int pillH = 76, pillGap = 18;
            int panelY = 108;
            for (const auto &p : pills)
            {
                const bool focused = (screen.homeMenuIndex == p.menuIndex);
                framebuffer.drawSoftShadow(rightColumnX, panelY, rW, pillH, pillH / 2);
                framebuffer.drawPill(rightColumnX, panelY, rW, pillH, focused ? Colors::Primary : Colors::PanelAlt);
                const Color labelColor = focused ? Colors::PrimaryText : Colors::Text;
                if (focused)
                    // ▶ pointer
                    framebuffer.drawSymbol(rightColumnX - 30, panelY + pillH / 2 - 12, "\xE2\x96\xB6", Colors::Primary);

                int labelWidth, lH;
                framebuffer.measureText(p.label, labelWidth, lH, TextStyle::Heading);
                framebuffer.drawText(rightColumnX + 36, panelY + (pillH - lH) / 2, p.label, labelColor, TextStyle::Heading);

                // Count sub-pill on the right.
                int subtitleWidth, sH;
                framebuffer.measureText(p.subtitle, subtitleWidth, sH, TextStyle::Caption);
                const int subW = subtitleWidth + 28, subH = 30;
                const int subX = rightColumnX + rW - subW - 20, subY = panelY + (pillH - subH) / 2;
                framebuffer.drawPill(subX, subY, subW, subH, focused ? Colors::PrimaryText : Colors::Panel);
                framebuffer.drawText(subX + 14, subY + (subH - sH) / 2, p.subtitle, focused ? Colors::Primary : Colors::TextDim, TextStyle::Caption);

                screen.touchButtons.push_back({100 + p.menuIndex, rightColumnX, panelY, rW, pillH});
                panelY += pillH + pillGap;
            }

            struct Icon
            {
                const char *label;
                const char *glyph;
                int menuIndex;
            };
            Icon icons[3] = {{"Items", "I", 3}, {"Trainer", "T", 4}, {"Settings", "S", 5}};
            const int iconR = 42;
            const int slot = rW / 3;
            const int iconTop = panelY + 22;
            for (int innerIndex = 0; innerIndex < 3; ++innerIndex)
            {
                const auto &ic = icons[innerIndex];
                const int centerX = rightColumnX + slot * innerIndex + slot / 2;
                const int centerY = iconTop + iconR;
                const bool focused = (screen.homeMenuIndex == ic.menuIndex);
                if (focused)
                    framebuffer.drawFilledCircle(centerX, centerY, iconR + 5, Color(Colors::Primary.red, Colors::Primary.green, Colors::Primary.blue, 70));
                framebuffer.drawFilledCircle(centerX, centerY, iconR, focused ? Colors::Primary : Colors::PanelAlt);
                framebuffer.drawCircle(centerX, centerY, iconR, focused ? Colors::Primary : Colors::Border, 2);

                drawMenuIcon(framebuffer, centerX, centerY, iconR - 12, innerIndex, focused ? Colors::PrimaryText : Colors::Text, focused ? Colors::Primary : Colors::PanelAlt);

                int labelWidth, lH;
                framebuffer.measureText(ic.label, labelWidth, lH, TextStyle::Caption);
                framebuffer.drawText(centerX - labelWidth / 2, centerY + iconR + 8, ic.label, focused ? Colors::Primary : Colors::TextDim, TextStyle::Caption);

                screen.touchButtons.push_back({100 + ic.menuIndex, centerX - iconR, centerY - iconR, 2 * iconR, 2 * iconR + 24});
            }
        }
    }
}
