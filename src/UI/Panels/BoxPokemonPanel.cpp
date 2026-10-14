#include <algorithm>
#include <cmath>
#include <string>

#include "UI/Panels/BoxPokemonPanel.h"
#include "UI/Panels/CarriedSprite.h" // drawLiftedMon -- shared with the bank
#include "UI/TrainerViewScreen.h"
#include "Trainer/OriginStamp.h" // originStampLabel -- the Origin row
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/SpriteManager.h"
#include "Trainer/Trainer.h"
#include "Pokemon/PokemonTypes.h"
#include "Names/ItemNames.h"
#include "Names/FormNames.h" // getDisplayName -- variant prefix ("Alolan Raichu", "Combat Breed Tauros")
#include "Legality/Legality.h"  // analyze -- the verdict in the summary panel's bottom-left corner

using namespace Trainer;

namespace UI
{
    namespace Panels
    {

        // Touch-button ids for the box header arrows (slot ids are 0..slotsPerBox-1).
        static constexpr int PREVIOUS_BOX_TOUCH_ID = 1000;
        static constexpr int NEXT_BOX_TOUCH_ID = 1001;
        static constexpr int BOX_NAME_TOUCH_ID = 1002; // tap the name pill to rename the box

        // Draw a type-icon sprite scaled to `h` px tall at (x, y); returns drawn width (0 if none).
        static int drawTypeIcon(PKSEFramebuffer &framebuffer, Sprite *type, int typeIconX, int typeIconY, int typeIconHeight)
        {
            if (!type || !type->data)
                return 0;
            int iconWidth = (type->width * typeIconHeight) / type->height;
            framebuffer.drawImageScaled(typeIconX, typeIconY, type->width, type->height, iconWidth, typeIconHeight, type->data, type->channels);
            return iconWidth;
        }

        // HOME-style box: rounded card, indigo header with a centered name pill and ‹ ›
        // arrows, a grid of sprite-on-disc cells, a floating name-cursor on the selection, and a
        // slim selected-info strip at the bottom.
        void drawBoxPokemon(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int boxPokemonX, int boxPokemonY, int boxPokemonWidth, int boxPokemonHeight)
        {
            // Rounded card surface.
            framebuffer.drawFilledRoundedRect(boxPokemonX, boxPokemonY, boxPokemonWidth, boxPokemonHeight, 16, Colors::Panel);

            // Box-slot / arrow touch targets are rebuilt every frame (hit-tested next frame).
            screen.touchButtons.clear();

            if (screen.selectedBoxIndex < 0 || screen.selectedBoxIndex >= static_cast<int>(screen.trainer.boxes.size()))
            {
                framebuffer.drawRoundedRect(boxPokemonX, boxPokemonY, boxPokemonWidth, boxPokemonHeight, 16, Colors::Border, 1);
                framebuffer.drawText(boxPokemonX + 20, boxPokemonY + 70, "No box data available", Colors::TextDim);
                return;
            }

            constexpr int headerH = 46;
            framebuffer.drawFilledRoundedRect(boxPokemonX, boxPokemonY, boxPokemonWidth, headerH, 16, Colors::AccentDim);
            // square the bottom edge
            framebuffer.drawFilledRect(boxPokemonX, boxPokemonY + headerH - 16, boxPokemonWidth, 16, Colors::AccentDim);

            // Centered box-name pill.
            std::string boxName = screen.selectedBoxIndex < static_cast<int>(screen.trainer.boxNames.size()) ? screen.trainer.boxNames[screen.selectedBoxIndex] : ("Box " + std::to_string(screen.selectedBoxIndex + 1));
            int boxNameWidth, bnH;
            framebuffer.measureText(boxName, boxNameWidth, bnH, TextStyle::Body);
            const int pillW = std::min(boxPokemonWidth - 160, boxNameWidth + 44);
            const int pillH = 30;
            const int pillX = boxPokemonX + (boxPokemonWidth - pillW) / 2;
            const int pillY = boxPokemonY + (headerH - pillH) / 2;
            // Amber pill when the header is focused (navigate up to it, or tap it, to rename). The name
            // is a touch target between the two arrow zones.
            const bool headerFocused = screen.detailViewActive && screen.selectedItemIndex == -1;
            framebuffer.drawPill(pillX, pillY, pillW, pillH, headerFocused ? Colors::Primary : Colors::Panel);
            framebuffer.drawText(boxPokemonX + (boxPokemonWidth - boxNameWidth) / 2, pillY + (pillH - bnH) / 2, boxName, headerFocused ? Colors::PrimaryText : Colors::Text);
            screen.touchButtons.push_back({BOX_NAME_TOUCH_ID, boxPokemonX + 64, boxPokemonY, boxPokemonWidth - 128, headerH});

            // ‹ › box arrows — geometric triangles via the symbol font (guaranteed glyphs, unlike
            // Nunito's ‹›); also touch targets -> L/R box change.
            const int arrowY = boxPokemonY + (headerH - bnH) / 2;
            framebuffer.drawSymbol(boxPokemonX + 26, arrowY, "◀", Colors::Text);
            framebuffer.drawSymbol(boxPokemonX + boxPokemonWidth - 40, arrowY, "▶", Colors::Text);
            screen.touchButtons.push_back({PREVIOUS_BOX_TOUCH_ID, boxPokemonX, boxPokemonY, 64, headerH});
            screen.touchButtons.push_back({NEXT_BOX_TOUCH_ID, boxPokemonX + boxPokemonWidth - 64, boxPokemonY, 64, headerH});

            // Box counter (small, right side of the band, left of the › arrow).
            std::string counter = std::to_string(screen.selectedBoxIndex + 1) + " / " + std::to_string(screen.trainer.getBoxCount());
            int countTextWidth, cH;
            framebuffer.measureText(counter, countTextWidth, cH, TextStyle::Caption);
            framebuffer.drawText(boxPokemonX + boxPokemonWidth - 52 - countTextWidth, boxPokemonY + (headerH - cH) / 2, counter, Colors::Text, TextStyle::Caption);

            const auto &currentBox = screen.trainer.boxes[screen.selectedBoxIndex];

            const int slotsPerBox = static_cast<int>(screen.trainer.getSlotsPerBox());
            const int GRID_COLS = boxGridColumns(slotsPerBox);
            const int GRID_ROWS = BOX_GRID_ROWS;

            const int gridTop = boxPokemonY + headerH + 8;
            const int gridBottom = boxPokemonY + boxPokemonHeight - 12; // selection info now lives in the summary side-panel
            const int gridW = boxPokemonWidth - 36;
            const int gridH = gridBottom - gridTop;
            // Pitch comes from the REFERENCE width, not this box's column count, so a 20-slot Gen 1
            // box draws the same size cells as a 30-slot one and simply occupies less width. The
            // columns are then centred, which is what keeps them lined up with the bank's.
            const int colPitch = gridW / BOX_GRID_REFERENCE_COLUMNS;
            const int gridX = boxPokemonX + 18 + boxGridCenterOffset(GRID_COLS, colPitch);
            const int rowPitch = gridH / GRID_ROWS;
            int discR = std::min(colPitch, rowPitch) / 2 - 6;
            if (discR < 18)
                discR = 18;

            // The cursor and anything riding it are drawn last, over the finished grid.
            int selCx = 0, selDiscTop = 0, selDiscR = discR;
            bool haveSel = false;

            // Y grabs the slot under the cursor and a second Y drops it. The grabbed Pokemon is not
            // actually moved out of its slot until then (see the Y handler), but it READS as picked up:
            // its slot is drawn empty and the Pokemon itself travels with the cursor, exactly as a
            // carried one does in the bank. Sourced from the grabbed slot rather than the visible box,
            // so it keeps following the cursor after changing box with L/R.
            const ::Pokemon::Pokemon *carried = nullptr;
            if (screen.swapActive && screen.swapSourceBox >= 0 && screen.swapSourceBox < static_cast<int>(screen.trainer.boxes.size()) && screen.swapSourceSlot >= 0 && screen.swapSourceSlot < slotsPerBox)
            {
                carried = screen.trainer.boxes[screen.swapSourceBox][screen.swapSourceSlot].get();
            }

            const double nowT = framebuffer.getTimeSeconds(); // drives the cursor's bob

            for (int row = 0; row < GRID_ROWS; ++row)
            {
                for (int columnIndex = 0; columnIndex < GRID_COLS; ++columnIndex)
                {
                    const int slotIndex = row * GRID_COLS + columnIndex;
                    const int cellX = gridX + columnIndex * colPitch;
                    const int cellY = gridTop + row * rowPitch;
                    const int centerX = cellX + colPitch / 2;
                    const int centerY = cellY + rowPitch / 2;

                    // Whole-cell tap target (finger-friendly, larger than the disc).
                    screen.touchButtons.push_back({slotIndex, cellX, cellY, colPitch, rowPitch});

                    const bool selected = screen.detailViewActive && slotIndex == screen.selectedItemIndex;
                    const bool grabbed = screen.swapActive && screen.swapSourceBox == screen.selectedBoxIndex && screen.swapSourceSlot == slotIndex;

                    const auto &pokemon = currentBox[slotIndex];
                    std::string speciesName = pokemon ? std::string(pokemon->species()) : "";
                    // A grabbed slot renders empty: its occupant is drawn riding the cursor instead, so
                    // showing it here too would put the same Pokemon on screen twice.
                    const bool empty = grabbed || !pokemon || speciesName == "None" || speciesName == "Empty" || pokemon->speciesID() == 0;

                    // Disc: occupied a touch lighter than empty for subtle depth.
                    framebuffer.drawFilledCircle(centerX, centerY, discR, empty ? Colors::Panel : Colors::PanelAlt);

                    if (empty)
                    {
                        framebuffer.drawCircle(centerX, centerY, discR, Colors::Border, 1);
                    }
                    else
                    {
                        const bool isShiny = pokemon->isShiny(pokemon->id32(), speciesName);
                        int markerSize = std::min(static_cast<int>(discR * 1.8), colPitch - 6);
                        if (pokemon->isEgg())
                        {
                            // Eggs show as an egg in the grid; the summary panel still shows the species.
                            framebuffer.drawEgg(centerX, centerY, markerSize);
                        }
                        else
                        {
                            Sprite *sprite = SpriteManager::getIconSprite(pokemon->speciesID(), pokemon->form(), isShiny);
                            if (sprite && sprite->data)
                            {
                                framebuffer.drawImageScaled(centerX - markerSize / 2, centerY - markerSize / 2, sprite->width, sprite->height, markerSize, markerSize, sprite->data, sprite->channels);
                            }
                        }
                        // Markers: partner heart (top-left), party number (bottom-left), shiny star (top-right).
                        if (screen.trainer.isStarterPokemon(screen.selectedBoxIndex, slotIndex))
                            framebuffer.drawSymbol(centerX - discR, centerY - discR + 2, "♥", Colors::PartnerHeart);
                        int partyPos = screen.trainer.getPartyPosition(screen.selectedBoxIndex, slotIndex);
                        if (partyPos > 0)
                        {
                            // Gold badge + dark digit (bottom-left), legible on any sprite in either theme.
                            const std::string partyPositionText = std::to_string(partyPos);
                            const int boxX = centerX - discR + 9, by = centerY + discR - 9;
                            framebuffer.drawFilledCircle(boxX, by, 9, Colors::PartyBadge);
                            int textWidth, th;
                            framebuffer.measureText(partyPositionText, textWidth, th, TextStyle::Caption);
                            framebuffer.drawText(boxX - textWidth / 2, by - th / 2, partyPositionText, Colors::PartyBadgeText, TextStyle::Caption);
                        }
                        if (isShiny)
                            framebuffer.drawShinyMark(centerX + discR - 15, centerY - discR + 1, 15, Colors::ShinyStar);
                    }

                    // Search highlights rather than filters, as in Storage: hiding slots would move
                    // every Pokemon the player arranged.
                    if (screen.storageSearch.isFiltering())
                    {
                        if (!empty && screen.storageSlotMatchesSearch(0, screen.selectedBoxIndex, slotIndex))
                            framebuffer.drawCircle(centerX, centerY, discR + 2, Colors::Accent, 3);
                        else
                            framebuffer.drawFilledCircle(centerX, centerY, discR, Colors::SearchMissShade);
                    }

                    if (selected)
                    {
                        selCx = centerX;
                        selDiscTop = centerY - discR;
                        selDiscR = discR;
                        haveSel = true;
                    }
                }
            }

            framebuffer.drawRoundedRect(boxPokemonX, boxPokemonY, boxPokemonWidth, boxPokemonHeight, 16, Colors::Border, 1);

            // Cursor last, over the finished grid: the same arrow the bank uses, in this view's own
            // colours -- indigo while browsing, amber while carrying, matching the rings it replaces.
            // Anything in hand rides it, drawn first so the arrow stays on top.
            const Color cursorColor = screen.swapActive ? Colors::Primary : Colors::Accent;
            const int bobOffset = static_cast<int>(std::sin(nowT * 3.4) * 3.0);
            if (haveSel)
            {
                if (carried)
                    drawLiftedMon(framebuffer, carried, selCx, selDiscTop + selDiscR + bobOffset, selDiscR);
                framebuffer.drawPointerCursor(selCx, selDiscTop - 3 + bobOffset, GRID_CURSOR_HEIGHT, cursorColor);
            }
            else if (headerFocused)
            {
                // The box-name pill is a cursor position of its own; point at it too, so navigating up
                // off the top row never leaves the cursor invisible.
                framebuffer.drawPointerCursor(boxPokemonX + boxPokemonWidth / 2, pillY - 2 + bobOffset, GRID_CURSOR_HEIGHT, cursorColor);
            }
        }

        // HOME-style summary side-panel (shown right of the box when entered): big idle HD
        // render, name/level/gender/shiny, type icons, the stat hexagon (actual stats), and a
        // Nature/Ability/Held Item card. Tapping it (id 2000) opens the full editor. Read-only display.
        void drawBoxSummaryPanel(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int boxSummaryPanelX, int boxSummaryPanelY, int boxSummaryPanelWidth, int boxSummaryPanelHeight)
        {
            framebuffer.drawFilledRoundedRect(boxSummaryPanelX, boxSummaryPanelY, boxSummaryPanelWidth, boxSummaryPanelHeight, 16, Colors::Panel);

            // Header band.
            constexpr int headerH = 40;
            framebuffer.drawFilledRoundedRect(boxSummaryPanelX, boxSummaryPanelY, boxSummaryPanelWidth, headerH, 16, Colors::AccentDim);
            framebuffer.drawFilledRect(boxSummaryPanelX, boxSummaryPanelY + headerH - 16, boxSummaryPanelWidth, 16, Colors::AccentDim);
            {
                int textWidth, textHeight;
                framebuffer.measureText("Summary", textWidth, textHeight, TextStyle::Body);
                framebuffer.drawText(boxSummaryPanelX + (boxSummaryPanelWidth - textWidth) / 2, boxSummaryPanelY + (headerH - textHeight) / 2, "Summary", Colors::Text);
            }

            // Resolve the selected Pokemon (must be entered + occupied).
            const Pokemon::Pokemon *p = nullptr;
            if (screen.detailViewActive && screen.selectedBoxIndex >= 0 && screen.selectedBoxIndex < static_cast<int>(screen.trainer.boxes.size()) && screen.selectedItemIndex >= 0 && screen.selectedItemIndex < static_cast<int>(screen.trainer.getSlotsPerBox()))
            {
                const auto &pokemon = screen.trainer.boxes[screen.selectedBoxIndex][screen.selectedItemIndex];
                if (pokemon && pokemon->speciesID() != 0)
                    p = pokemon.get();
            }
            framebuffer.drawRoundedRect(boxSummaryPanelX, boxSummaryPanelY, boxSummaryPanelWidth, boxSummaryPanelHeight, 16, Colors::Border, 1);
            if (!p)
            {
                int textWidth, textHeight;
                framebuffer.measureText("No Pokémon selected", textWidth, textHeight, TextStyle::Caption);
                framebuffer.drawText(boxSummaryPanelX + (boxSummaryPanelWidth - textWidth) / 2, boxSummaryPanelY + boxSummaryPanelHeight / 2, "No Pokémon selected", Colors::TextDim, TextStyle::Caption);
                return;
            }

            std::string species = std::string(p->species());
            const bool isShiny = p->isShiny(p->id32(), species);

            // HD render (idle), centered near the top.
            const int renderSz = 120;
            Sprite *sprite = SpriteManager::getSprite(p->speciesID(), p->form(), isShiny);
            if (sprite && sprite->data)
            {
                framebuffer.drawSpriteIdle(boxSummaryPanelX + (boxSummaryPanelWidth - renderSz) / 2, boxSummaryPanelY + headerH + 6, renderSz, renderSz, sprite->width, sprite->height, sprite->data, sprite->channels, 0.0f);
            }

            int summaryRowY = boxSummaryPanelY + headerH + 6 + renderSz + 4;
            // Name (Heading) + gender + shiny, centered. Variant forms read as "Alolan Raichu" etc.
            {
                std::string display = Names::getDisplayName(p->speciesID(), p->form(), species);
                int nameWidth, nH;
                framebuffer.measureText(display, nameWidth, nH, TextStyle::Heading);
                framebuffer.drawText(boxSummaryPanelX + (boxSummaryPanelWidth - nameWidth) / 2, summaryRowY, display, Colors::Text, TextStyle::Heading);
                int markX = boxSummaryPanelX + (boxSummaryPanelWidth + nameWidth) / 2 + 6;
                const char *g = p->genderSymbol();
                if (g[0] != '\0')
                {
                    framebuffer.drawSymbol(markX, summaryRowY + 6, g, (std::string(g) == "♂") ? Colors::Blue : Colors::Magenta);
                    markX += 20;
                }
                if (isShiny)
                    framebuffer.drawShinyMark(markX, summaryRowY + 6, 16, Colors::ShinyStar);
                summaryRowY += nH + 4;
            }
            // Lv + zero-padded dex no.
            {
                const std::string dexNumberText = dexNumberLabel(p->speciesID());
                std::string subtitleText = "Lv. " + std::to_string(p->level()) + "    No. " + dexNumberText;
                int subtitleWidth, sH;
                framebuffer.measureText(subtitleText, subtitleWidth, sH, TextStyle::Caption);
                framebuffer.drawText(boxSummaryPanelX + (boxSummaryPanelWidth - subtitleWidth) / 2, summaryRowY, subtitleText, Colors::TextDim, TextStyle::Caption);
                summaryRowY += 22;
            }
            // Type icons, centered.
            {
                Pokemon::TypePair types = Pokemon::getPokemonTypes(p->speciesID(), p->form(), p->getGameGroup());
                Sprite *firstTypeIcon = SpriteManager::getTypeSprite(types.type1);
                Sprite *secondTypeIcon = Pokemon::hasSecondType(types) ? SpriteManager::getTypeSprite(types.type2) : nullptr;
                const int textHeight = 20;
                int firstTypeWidth = (firstTypeIcon && firstTypeIcon->data) ? (firstTypeIcon->width * textHeight) / firstTypeIcon->height : 0;
                int secondTypeWidth = (secondTypeIcon && secondTypeIcon->data) ? (secondTypeIcon->width * textHeight) / secondTypeIcon->height : 0;
                int typeIconGap = (secondTypeWidth > 0) ? 8 : 0;
                int typeRowX = boxSummaryPanelX + (boxSummaryPanelWidth - (firstTypeWidth + typeIconGap + secondTypeWidth)) / 2;
                drawTypeIcon(framebuffer, firstTypeIcon, typeRowX, summaryRowY, textHeight);
                typeRowX += firstTypeWidth + typeIconGap;
                drawTypeIcon(framebuffer, secondTypeIcon, typeRowX, summaryRowY, textHeight);
                summaryRowY += textHeight + 10;
            }

            // Stat hexagon (actual stats, HOME vertex order [HP, Atk, Def, Spe, SpD, SpA]).
            float statValues[6] = {
                static_cast<float>(p->statHPMax()), static_cast<float>(p->statATK()), static_cast<float>(p->statDEF()),
                static_cast<float>(p->statSPE()), static_cast<float>(p->statSPD()), static_cast<float>(p->statSPA())};
            const int hexCx = boxSummaryPanelX + boxSummaryPanelWidth / 2;
            // 62, not 70: the quick-info block below grew a fourth row (the origin stamp) and this
            // panel was already full -- three rows ended 15px above the edit hint. The hexagon is
            // the only element here with slack, and the clearance rule below is unchanged, so the
            // bottom vertex label still clears the first info row by the same margin it always did.
            const int hexR = 62;
            // Center the hexagon well below the type badges: its top vertex + HP label extend
            // ~(hexR + 14 + lineHeight) above center, so a small offset would collide with the types.
            const int hexCy = summaryRowY + hexR + 32;
            framebuffer.drawStatHexagon(hexCx, hexCy, hexR, statValues, 6, 255.0f, Color(Colors::Accent.red, Colors::Accent.green, Colors::Accent.blue, 110), Colors::Border, Colors::Accent);
            // Vertex labels: "ABBR value", anchored by side.
            static const char *statAbbreviations[6] = {"HP", "Atk", "Def", "Spe", "SpD", "SpA"};
            static const double axisAngles[6] = {90, 30, -30, 270, 210, 150};
            const double piRadians = 3.14159265358979323846;
            for (int index = 0; index < 6; ++index)
            {
                double axisAngleRadians = axisAngles[index] * piRadians / 180.0;
                int axisLabelX = hexCx + static_cast<int>(std::lround(std::cos(axisAngleRadians) * (hexR + 14)));
                int axisLabelY = hexCy - static_cast<int>(std::lround(std::sin(axisAngleRadians) * (hexR + 14)));
                std::string axisLabelText = std::string(statAbbreviations[index]) + " " + std::to_string(static_cast<int>(statValues[index]));
                int labelWidth, lh;
                framebuffer.measureText(axisLabelText, labelWidth, lh, TextStyle::Caption);
                double axisCosine = std::cos(axisAngleRadians);
                int labelX = (axisCosine > 0.3) ? axisLabelX + 4 : (axisCosine < -0.3) ? axisLabelX - 4 - labelWidth : axisLabelX - labelWidth / 2;
                int labelY = (index == 0) ? axisLabelY - lh : (index == 3) ? axisLabelY : axisLabelY - lh / 2;
                framebuffer.drawText(labelX, labelY, axisLabelText, Colors::Text, TextStyle::Caption);
            }

            // Quick info: Nature / Ability / Held Item (label left, value right).
            // Start below the bottom (Speed) hexagon label so they don't collide.
            int infoRowY = hexCy + hexR + 34;
            auto infoRow = [&](const char *label, const std::string &value)
            {
                framebuffer.drawText(boxSummaryPanelX + 18, infoRowY, label, Colors::TextDim, TextStyle::Caption);
                int valueWidth, vh;
                framebuffer.measureText(value, valueWidth, vh, TextStyle::Caption);
                framebuffer.drawText(boxSummaryPanelX + boxSummaryPanelWidth - 18 - valueWidth, infoRowY, value, Colors::Text, TextStyle::Caption);
                infoRowY += 20;
            };
            infoRow("Nature", getNatureName(p->nature()));
            infoRow("Ability", getAbilityName(p->ability()));
            infoRow("Held Item", p->heldItem() ? std::string(Enums::isGen3Group(p->getGameGroup()) ? Names::getItemNameG3(p->heldItem()) : getItemName(p->heldItem())) : std::string("None"));
            // ORIGIN STAMP. A box always belongs to the open save, so that save is the title
            // context a versionless PK1/PK2 borrows -- see UI::originStampLabel.
            {
                // ORIGIN STAMP: the games' own marking where one exists, the game name where it
                // does not. Gen 3/4/5 have NO mark -- the Kalos pentagon was the first, so there is
                // nothing to draw and nothing to invent; they are named in text instead, which they
                // can be because they carry a version byte. Gen 1/2 are the mirror image: no version
                // byte, so the console is the only thing actually known and the mark stands alone.
                framebuffer.drawText(boxSummaryPanelX + 18, infoRowY, "Origin", Colors::TextDim, TextStyle::Caption);
                int originRightEdge = boxSummaryPanelX + boxSummaryPanelWidth - 18;
                Sprite *markSprite = SpriteManager::getOriginMarkSprite(Trainer::originMarkFor(*p), Colors::Text);
                if (markSprite != nullptr && markSprite->data != nullptr)
                {
                    // 18, not 16: the Scarlet/Violet, Z-A and GO marks carry enough interior
                    // detail to go muddy a couple of pixels smaller. Still inside the 20px pitch.
                    const int markSize = 18;
                    framebuffer.drawImageScaled(originRightEdge - markSize, infoRowY - 3, markSprite->width, markSprite->height, markSize, markSize, markSprite->data, markSprite->channels);
                    originRightEdge -= markSize + 6;
                }
                if (p->hasOriginGame())
                {
                    const std::string originText = Trainer::originStampLabel(*p, &screen.trainer);
                    int originWidth, originHeight;
                    framebuffer.measureText(originText, originWidth, originHeight, TextStyle::Caption);
                    framebuffer.drawText(originRightEdge - originWidth, infoRowY, originText, Colors::Text, TextStyle::Caption);
                }
                infoRowY += 20;
            }

            //
            // LEGALITY VERDICT. The editor page already carries this, but that is a page you have
            // to open per pokemon -- finding the one problem in a box meant opening thirty of them.
            // Here it answers while the cursor moves. Same two colours and the same wording as the
            // editor's row: "no problems found" rather than "Legal" is deliberate, because Layer 3
            // errs toward silence, so a clean report means nothing was found against this pokemon,
            // not that HOME will accept it.
            //
            // Not a tap target: the whole panel is already one (id 2000 -> open the editor), and a
            // second target inside it would make one corner do something different from the rest.
            // The issue LIST stays where it was, on R inside the editor.
            //
            // THE EDIT HINT MOVED FROM CENTRED TO RIGHT-ALIGNED to make room, and it had to: this
            // panel is 452px wide, so a centred hint leaves the left 159px, and "Legality: no
            // problems found" measures 203. The two overlapped by 44px -- which is exactly the kind
            // of arithmetic nobody can eyeball from the source, so the clearance is asserted by a
            // test rather than trusted. Status left, action right also reads better than
            // status-then-action on one centred line.
            const int bottomRowY = boxSummaryPanelY + boxSummaryPanelHeight - 24;
            {
                const Legality::Report summaryReport = Legality::analyze(*p, p->getGameGroup());
                const bool clean = summaryReport.ok();
                const std::string verdict = clean ? std::string("Legality: no problems found") : "Legality: " + std::to_string(summaryReport.problemCount()) + " issue(s)";
                framebuffer.drawText(boxSummaryPanelX + 18, bottomRowY, verdict, clean ? Colors::LegalityClean : Colors::LegalityProblem, TextStyle::Caption);
            }

            // Edit hint + whole-panel tap target (opens the editor for the selected slot).
            const char *hint = "A / tap: Edit";
            int hintWidth, hintHeight;
            framebuffer.measureText(hint, hintWidth, hintHeight, TextStyle::Caption);
            framebuffer.drawText(boxSummaryPanelX + boxSummaryPanelWidth - 18 - hintWidth, bottomRowY, hint, Colors::Accent, TextStyle::Caption);
            screen.touchButtons.push_back({2000, boxSummaryPanelX, boxSummaryPanelY + headerH, boxSummaryPanelWidth,
                                           boxSummaryPanelHeight - headerH});
        }
    }
}
