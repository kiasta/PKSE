#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

#include "UI/Dialogs/RibbonEditDialog.h"
#include "UI/Dialogs/DialogFrame.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "UI/SpriteManager.h"
#include "Conversion/Convert.h" // affixedRibbonOffset -- which formats display an affixed ribbon
#include "Names/RibbonNames.h"
#include "Pokemon/Pokemon.h"

namespace UI
{
    namespace Dialogs
    {
        namespace
        {
            constexpr int TILE_PITCH = 68;
            constexpr int TILE_SIZE = 60;
            constexpr int TILE_CORNER_RADIUS = 10;
            // The art is 30x30; drawn at 1.6x it fills the tile and stays crisp enough to tell apart.
            constexpr int RIBBON_ART_SIZE = 48;
            constexpr int AFFIXED_DOT_RADIUS = 7;
            constexpr int GRID_PADDING = 28;
            constexpr int HEADER_HEIGHT = 78; // what drawDialogFrame reserves above the content
            constexpr int FOOTER_HEIGHT = 84;

            std::string describeRibbonValue(const Names::RibbonDefinition &ribbon, uint8_t ribbonValue)
            {
                switch (ribbon.kind)
                {
                case Names::RibbonKind::ContestRank:
                    return ribbonValue == 0 ? "Not won" : "Rank " + std::to_string(ribbonValue) + " of " + std::to_string(ribbon.maxCount);
                case Names::RibbonKind::MemoryCount:
                    return "Count " + std::to_string(ribbonValue) + " of " + std::to_string(ribbon.maxCount);
                case Names::RibbonKind::Flag:
                default:
                    return ribbonValue != 0 ? "Owned" : "Not owned";
                }
            }
        }

        void drawRibbonEditDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            const Pokemon::Pokemon *pokemon = screen.detailsTargetPokemon();
            if (!pokemon)
                return;
            const Enums::GameVersion group = pokemon->getGameGroup();
            const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(group);
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon->getData().data());
            const int ribbonCount = static_cast<int>(ribbons.size());

            // AffixedRibbon is one byte naming a RibbonIndex; 0xFF is none.
            const size_t affixOffset = Conversion::affixedRibbonOffset(group);
            const int affixedIndex = (affixOffset != 0 && affixOffset < pokemon->getData().size() && entityData[affixOffset] != Conversion::AFFIXED_RIBBON_NONE) ? entityData[affixOffset] : -1;

            const int gridWidth = RIBBON_GRID_COLUMNS * TILE_PITCH - (TILE_PITCH - TILE_SIZE);
            const int dialogWidth = gridWidth + 2 * GRID_PADDING;
            const int dialogHeight = HEADER_HEIGHT + RIBBON_GRID_VISIBLE_ROWS * TILE_PITCH + FOOTER_HEIGHT;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = std::max(0, (framebuffer.getHeight() - NAV_BAR_HEIGHT - dialogHeight) / 2);
            const int gridTop = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, dialogHeight, "Ribbons & Marks", Colors::Text);
            const int gridLeft = dialogX + GRID_PADDING;

            screen.touchButtons.clear(); // the editor owns every tap while it is open

            int ownedCount = 0;
            for (const Names::RibbonDefinition &ribbon : ribbons)
            {
                if (Names::getRibbonValue(entityData, ribbon) != 0)
                    ++ownedCount;
            }
            {
                const std::string ownedText = std::to_string(ownedCount) + " of " + std::to_string(ribbonCount) + " owned";
                int ownedTextWidth = 0, ownedTextHeight = 0;
                framebuffer.measureText(ownedText, ownedTextWidth, ownedTextHeight, TextStyle::Caption);
                framebuffer.drawText(dialogX + dialogWidth - 24 - ownedTextWidth, dialogY + 26, ownedText, Colors::TextDim, TextStyle::Caption);
            }

            const int scrollRow = screen.details.ribbonScrollRow;
            for (int ribbonIndex = 0; ribbonIndex < ribbonCount; ++ribbonIndex)
            {
                const int visibleRow = ribbonIndex / RIBBON_GRID_COLUMNS - scrollRow;
                if (visibleRow < 0 || visibleRow >= RIBBON_GRID_VISIBLE_ROWS)
                    continue;
                const Names::RibbonDefinition &ribbon = ribbons[ribbonIndex];
                const uint8_t ribbonValue = Names::getRibbonValue(entityData, ribbon);
                const bool selected = (ribbonIndex == screen.details.selectedRibbonIndex);
                const int tileLeft = gridLeft + (ribbonIndex % RIBBON_GRID_COLUMNS) * TILE_PITCH;
                const int tileTop = gridTop + visibleRow * TILE_PITCH;

                framebuffer.drawFilledRoundedRect(tileLeft, tileTop, TILE_SIZE, TILE_SIZE, TILE_CORNER_RADIUS, selected ? Colors::Selected : Colors::PanelAlt);
                if (selected)
                    framebuffer.drawRoundedRect(tileLeft, tileTop, TILE_SIZE, TILE_SIZE, TILE_CORNER_RADIUS, Colors::Accent, 2);

                const int artLeft = tileLeft + (TILE_SIZE - RIBBON_ART_SIZE) / 2;
                const int artTop = tileTop + (TILE_SIZE - RIBBON_ART_SIZE) / 2;
                if (const Sprite *ribbonSprite = SpriteManager::getRibbonSprite(Names::getRibbonSpriteStem(ribbon, ribbonValue, group)))
                    framebuffer.drawImageScaled(artLeft, artTop, ribbonSprite->width, ribbonSprite->height, RIBBON_ART_SIZE, RIBBON_ART_SIZE, ribbonSprite->data, ribbonSprite->channels);
                else
                    framebuffer.drawText(artLeft + RIBBON_ART_SIZE / 3, artTop + RIBBON_ART_SIZE / 4, "?", Colors::TextDim);
                if (ribbonValue == 0)
                    framebuffer.drawFilledRoundedRect(tileLeft, tileTop, TILE_SIZE, TILE_SIZE, TILE_CORNER_RADIUS, Colors::RibbonUnownedVeil);

                // A count is a number the art cannot show, so it is printed on the tile.
                if (ribbon.kind == Names::RibbonKind::MemoryCount && ribbonValue != 0)
                {
                    const std::string countText = std::to_string(ribbonValue);
                    int countTextWidth = 0, countTextHeight = 0;
                    framebuffer.measureText(countText, countTextWidth, countTextHeight, TextStyle::Caption);
                    framebuffer.drawText(tileLeft + TILE_SIZE - 4 - countTextWidth, tileTop + TILE_SIZE - countTextHeight, countText, Colors::Text, TextStyle::Caption);
                }
                if (affixedIndex >= 0 && Names::getRibbonAffixIndex(ribbon) == affixedIndex)
                {
                    framebuffer.drawFilledCircle(tileLeft + TILE_SIZE - AFFIXED_DOT_RADIUS - 3, tileTop + AFFIXED_DOT_RADIUS + 3, AFFIXED_DOT_RADIUS, Colors::Primary);
                    framebuffer.drawCircle(tileLeft + TILE_SIZE - AFFIXED_DOT_RADIUS - 3, tileTop + AFFIXED_DOT_RADIUS + 3, AFFIXED_DOT_RADIUS, Colors::PrimaryText, 1);
                }

                screen.touchButtons.push_back({RIBBON_TILE_BUTTON_ID_BASE + ribbonIndex, tileLeft, tileTop, TILE_SIZE, TILE_SIZE});
            }

            // The selected ribbon, named in full: the grid has room for art only.
            const int footerTop = gridTop + RIBBON_GRID_VISIBLE_ROWS * TILE_PITCH + 6;
            framebuffer.drawHDivider(dialogX + DIALOG_CORNER_RADIUS, footerTop - 4, dialogWidth - 2 * DIALOG_CORNER_RADIUS);
            const int selectedRibbonIndex = screen.details.selectedRibbonIndex;
            if (selectedRibbonIndex >= 0 && selectedRibbonIndex < ribbonCount)
            {
                const Names::RibbonDefinition &selectedRibbon = ribbons[selectedRibbonIndex];
                const uint8_t selectedValue = Names::getRibbonValue(entityData, selectedRibbon);
                framebuffer.drawText(gridLeft, footerTop + 8, Names::getRibbonName(selectedRibbon, selectedValue), Colors::Text, TextStyle::Heading);
                std::string statusText = describeRibbonValue(selectedRibbon, selectedValue);
                if (affixedIndex >= 0 && Names::getRibbonAffixIndex(selectedRibbon) == affixedIndex)
                    statusText += "  |  Affixed";
                framebuffer.drawText(gridLeft, footerTop + 46, statusText, Colors::TextDim, TextStyle::Caption);
            }
            else
            {
                framebuffer.drawText(gridLeft, footerTop + 8, "This format stores no ribbons.", Colors::TextDim);
            }
        }
    }
}
