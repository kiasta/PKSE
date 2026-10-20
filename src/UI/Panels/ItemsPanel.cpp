#include <algorithm>
#include <string>
#include <vector>

#include "UI/Panels/ItemsPanel.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/ListSearch.h"
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Trainer.h"
#include "Trainer/Inventory9LZA.h"
#include "Trainer/Inventory9SV.h"
#include "Trainer/Inventory8LA.h"
#include "Trainer/Inventory8BDSP.h"
#include "Trainer/Inventory8SWSH.h"
#include "Trainer/Inventory7LGPE.h"
#include "Trainer/Inventory1RBY.h"
#include "Trainer/Inventory3FRLG.h"
#include "Trainer/Inventory3RSE.h"
#include "Enums/GameVersion.h"
#include "Utils/HelperUtilities.h"
#include "Names/MoveNames.h"
#include "Names/TMMoves.h"
#include "Names/ItemNames.h"

using namespace Trainer;
using namespace Enums;
using namespace Utils;

namespace UI
{
    namespace Panels
    {
        const char *pouchDisplayName(const Trainer::Trainer &trainer, int category)
        {
            const char *trainerPouchName = trainer.getItemPouchName(category);
            if (trainerPouchName != nullptr) return trainerPouchName;
            const GameVersion gameGroup = trainer.getGameGroup();
            const char *name = nullptr;
            if (gameGroup == GameVersion::ZA)
                name = getPouchInfo9LZA(static_cast<PouchType9LZA>(category)).name;
            else if (gameGroup == GameVersion::SV)
                name = getPouchInfo9SV(static_cast<PouchType9SV>(category)).name;
            else if (gameGroup == GameVersion::PLA)
                name = getPouchInfo8LA(static_cast<PouchType8LA>(category)).name;
            else if (gameGroup == GameVersion::BDSP)
                name = getPouchInfo8BDSP(static_cast<PouchType8BDSP>(category)).name;
            else if (gameGroup == GameVersion::GG)
                name = getPouchInfo7LGPE(static_cast<PouchType7LGPE>(category)).name;
            else if (gameGroup == GameVersion::RBY)
                name = getPouchInfo1RBY(static_cast<PouchType1RBY>(category)).name;
            else if (gameGroup == GameVersion::FRLG)
                name = getPouchInfo3FRLG(static_cast<PouchType3FRLG>(category)).name;
            else if (gameGroup == GameVersion::RSE)
                // Either layout names its pouches the same -- Ruby/Sapphire and Emerald differ in
                // pouch SIZE and offset, not in what the pockets are called -- so this needs no
                // save to ask, unlike the capacity in TrainerViewScreen::pouchCapacity().
                name = getPouchInfo3RSE(static_cast<PouchType3RSE>(category), Gen3HoennLayout::RubySapphire).name;
            else
                name = getPouchInfo8SWSH(static_cast<PouchType8SWSH>(category)).name;
            return (name != nullptr && name[0] != '\0') ? name : "?";
        }

        void drawItems(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int itemsX, int itemsY, int itemsWidth, int itemsHeight)
        {
            framebuffer.drawFilledRoundedRect(itemsX, itemsY, itemsWidth, itemsHeight, 16, Colors::Panel);
            framebuffer.drawRoundedRect(itemsX, itemsY, itemsWidth, itemsHeight, 16, Colors::Border, 1);

            // Header band + pouch name.
            constexpr int headerHeight = 46;
            framebuffer.drawFilledRoundedRect(itemsX, itemsY, itemsWidth, headerHeight, 16, Colors::AccentDim);
            framebuffer.drawFilledRect(itemsX, itemsY + headerHeight - 16, itemsWidth, 16, Colors::AccentDim);
            GameVersion gameGroup = screen.trainer.getGameGroup();
            const char *pouchName = pouchDisplayName(screen.trainer, screen.selectedCategory);
            framebuffer.drawText(itemsX + 22, itemsY + (headerHeight - framebuffer.lineHeight(TextStyle::Heading)) / 2, std::string("Items - ") + pouchName, Colors::Text, TextStyle::Heading);

            screen.touchButtons.clear();

            if (screen.selectedCategory < 0 || screen.selectedCategory >= static_cast<int>(screen.trainer.items.size()))
            {
                framebuffer.drawText(itemsX + 24, itemsY + headerHeight + 30, "Invalid category", Colors::TextDim);
                return;
            }
            const auto &pouch = screen.trainer.items[screen.selectedCategory];
            // Only visible items (count > 0); "owned but empty" slots are hidden but kept in the data.
            std::vector<int> visible = screen.visibleItemIndices();
            drawListSearchBox(framebuffer, itemsX + 24, itemsY + headerHeight + 4, itemsWidth - 48, screen.itemSearch, static_cast<int>(visible.size()), static_cast<int>(pouch.size()));
            if (visible.empty())
            {
                // A filter that hides everything must not read as an empty pocket.
                framebuffer.drawText(itemsX + 24, itemsY + headerHeight + listSearchBoxHeight() + 26, screen.itemSearch.isFiltering() ? "Nothing in this pocket matches that search." : "No items in this category", Colors::TextDim);
                return;
            }
            const int total = static_cast<int>(visible.size());

            // Single-column touch-friendly tiles (this geometry mirrors the nav math in
            // TrainerViewScreen::update() — keep them in sync).
            constexpr int rowPitch = 52, tileH = 46;
            const int itemsPerPage = (itemsHeight - 106 - listSearchBoxHeight()) / rowPitch;
            const int totalPages = (total + itemsPerPage - 1) / itemsPerPage;
            const int tileW = std::min(itemsWidth - 60, 860);
            const int tileX = itemsX + (itemsWidth - tileW) / 2;

            std::string countText = std::to_string(total) + " items";
            if (totalPages > 1)
                countText += "      Page " + std::to_string(screen.currentPage + 1) + " / " + std::to_string(totalPages);
            framebuffer.drawText(tileX, itemsY + headerHeight + listSearchBoxHeight() + 6, countText, Colors::TextDim, TextStyle::Caption);

            const int startIdx = screen.currentPage * itemsPerPage;
            const int endIdx = std::min(startIdx + itemsPerPage, total);
            int rectY = itemsY + headerHeight + listSearchBoxHeight() + 36;
            for (int itemRowIndex = startIdx; itemRowIndex < endIdx; ++itemRowIndex)
            {
                const InventoryItem &item = pouch[visible[itemRowIndex]];
                const bool selected = screen.detailViewActive && itemRowIndex == screen.selectedItemIndex;
                framebuffer.drawSoftShadow(tileX, rectY, tileW, tileH, tileH / 2);
                framebuffer.drawFilledRoundedRect(tileX, rectY, tileW, tileH, 12, selected ? Colors::Primary : Colors::PanelAlt);
                const Color nameCol = selected ? Colors::PrimaryText : (item.isNew ? Colors::Accent : Colors::Text);
                int nameX = tileX + 22;
                if (item.isFavorite)
                {
                    framebuffer.drawSymbol(nameX, rectY + (tileH - 20) / 2, "\xE2\x98\x85", Colors::Yellow);
                    nameX += 24;
                }
                // Each generation numbers items differently, so the group picks the table.
                const char *itemName = Names::getItemNameFor(gameGroup, item.itemId);
                framebuffer.drawText(nameX, rectY + (tileH - framebuffer.lineHeight(TextStyle::Body)) / 2, itemName, nameCol, TextStyle::Body);
                // TM/HM/TR items: show the move the machine teaches (dim, after the item name).
                if (uint16_t tmMove = Names::getTMMove(gameGroup, item.itemId))
                {
                    int itemNameWidth, ih;
                    framebuffer.measureText(itemName, itemNameWidth, ih, TextStyle::Body);
                    const Color moveCol = selected ? Colors::PrimaryText : Colors::TextDim;
                    framebuffer.drawText(nameX + itemNameWidth + 14, rectY + (tileH - framebuffer.lineHeight(TextStyle::Body)) / 2, Names::getMoveName(tmMove), moveCol, TextStyle::Body);
                }
                std::string countText = "\xC3\x97" + std::to_string(item.count); // ×N
                int countTextWidth, ch;
                framebuffer.measureText(countText, countTextWidth, ch, TextStyle::Body);
                framebuffer.drawText(tileX + tileW - 24 - countTextWidth, rectY + (tileH - ch) / 2, countText, selected ? Colors::PrimaryText : Colors::TextDim, TextStyle::Body);
                screen.touchButtons.push_back({itemRowIndex, tileX, rectY, tileW, tileH}); // id = absolute item index
                rectY += rowPitch;
            }
        }
    }
}
