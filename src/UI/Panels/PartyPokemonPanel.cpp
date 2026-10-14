#include <string>

#include "UI/Panels/PartyPokemonPanel.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "UI/SpriteManager.h"
#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/PokemonTypes.h"
#include "Names/FormNames.h" // getDisplayName -- variant prefix ("Alolan Raichu", "Combat Breed Tauros")

using namespace Trainer;

namespace UI
{
    namespace Panels
    {

        // Draw a type ICON sprite scaled to `h` px tall at (x, y); returns drawn width (0 if none).
        // Named apart from PKSEFramebuffer::drawTypeBadge deliberately -- that one renders a coloured
        // text pill, this one blits the type's artwork. They are different things, not a duplication.
        static int drawTypeIcon(PKSEFramebuffer &framebuffer, Sprite *type, int typeIconX, int typeIconY, int typeIconHeight)
        {
            if (!type || !type->data)
                return 0;
            int cardWidth = (type->width * typeIconHeight) / type->height;
            framebuffer.drawImageScaled(typeIconX, typeIconY, type->width, type->height, cardWidth, typeIconHeight, type->data, type->channels);
            return cardWidth;
        }

        void drawPartyPokemon(PKSEFramebuffer &framebuffer, const Trainer::Trainer &trainer, int partyPokemonX, int partyPokemonY, int partyPokemonWidth, int partyPokemonHeight, int selectedIndex)
        {
            const auto &party = trainer.party;

            framebuffer.drawCard(partyPokemonX, partyPokemonY, partyPokemonWidth, partyPokemonHeight);
            framebuffer.drawText(partyPokemonX + 16, partyPokemonY + 12, "Party", Colors::Text, TextStyle::Heading);
            framebuffer.drawHDivider(partyPokemonX + 16, partyPokemonY + 48, partyPokemonWidth - 32);

            // Two columns of three slots (1-3 left, 4-6 right).
            constexpr int gutter = 16;
            constexpr int slotGap = 12;
            const int gridTop = partyPokemonY + 58;
            const int colW = (partyPokemonWidth - 3 * gutter) / 2;
            const int colX[2] = {partyPokemonX + gutter, partyPokemonX + gutter + colW + gutter};
            const int slotH = (partyPokemonHeight - (gridTop - partyPokemonY) - 2 * slotGap - gutter) / 3;

            for (int index = 0; index < 6; index++)
            {
                const int columnIndex = (index >= 3) ? 1 : 0;
                const int rowInCol = (index >= 3) ? (index - 3) : index;
                const int cardX = colX[columnIndex];
                const int cardY = gridTop + rowInCol * (slotH + slotGap);
                const bool selected = (selectedIndex >= 0 && index == selectedIndex);

                // Slot card (rounded, HOME-style -- matches the cards used everywhere else in the app). A
                // soft shadow and accent border mark the selection; a square fill with a 4px left edge does
                // not sit right against rounded corners.
                constexpr int slotR = 12;
                if (selected)
                    framebuffer.drawSoftShadow(cardX, cardY, colW, slotH, slotR);
                framebuffer.drawFilledRoundedRect(cardX, cardY, colW, slotH, slotR, selected ? Colors::Selected : Colors::PanelAlt);
                framebuffer.drawRoundedRect(cardX, cardY, colW, slotH, slotR, selected ? Colors::Accent : Colors::Border, selected ? 2 : 1);

                constexpr int pad = 14;
                const Pokemon::Pokemon *pokemon = (index < static_cast<int>(party.size())) ? party[index].get() : nullptr;

                if (!pokemon || pokemon->speciesID() == 0)
                {
                    framebuffer.drawText(cardX + pad, cardY + slotH / 2 - 12, "Slot " + std::to_string(index + 1) + ":  Empty", Colors::TextDim);
                    continue;
                }

                const bool isShiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
                const bool usesAwakeningValues = pokemon->hasAwakeningValues();

                // Header row: slot number, species, gender/shiny/partner markers, level
                int headerX = cardX + pad;
                const int headerY = cardY + 9;

                std::string slotNumberText = std::to_string(index + 1);
                framebuffer.drawText(headerX, headerY, slotNumberText, Colors::Accent);
                int numW, numH;
                framebuffer.measureText(slotNumberText, numW, numH);
                headerX += numW + 10;

                std::string species = Names::getDisplayName(pokemon->speciesID(), pokemon->form(), pokemon->species());
                framebuffer.drawText(headerX, headerY, species, Colors::Text);
                int spriteTextWidth, spH;
                framebuffer.measureText(species, spriteTextWidth, spH);
                headerX += spriteTextWidth + 6;

                std::string gender = pokemon->genderSymbol();
                if (gender != "?" && gender != "Genderless" && gender != "")
                {
                    framebuffer.drawSymbol(headerX, headerY, gender, (gender == "♂") ? Colors::Blue : Colors::Magenta);
                    headerX += 20;
                }
                if (isShiny)
                {
                    framebuffer.drawShinyMark(headerX, headerY, 16, Colors::ShinyStar);
                    headerX += 20;
                }
                if (trainer.isPartyPokemonStarter(index))
                {
                    framebuffer.drawSymbol(headerX, headerY, "♥", Colors::PartnerHeart);
                    headerX += 20;
                }

                std::string levelText = "Lv. " + std::to_string(pokemon->level());
                int levelTextWidth, lvH;
                framebuffer.measureText(levelText, levelTextWidth, lvH);
                framebuffer.drawText(cardX + colW - pad - levelTextWidth, headerY, levelText, Colors::TextDim);

                // Stat table: fixed columns for alignment, caption-size data text
                struct StatRow
                {
                    const char *name;
                    int base, iv, evav, stat;
                };
                const StatRow rows[6] = {
                    {"HP", pokemon->baseHP(), pokemon->ivHP(), usesAwakeningValues ? pokemon->avHP() : pokemon->evHP(), pokemon->statHPMax()},
                    {"Atk", pokemon->baseATK(), pokemon->ivATK(),
                     usesAwakeningValues ? pokemon->avATK() : pokemon->evATK(), pokemon->statATK()},
                    {"Def", pokemon->baseDEF(), pokemon->ivDEF(),
                     usesAwakeningValues ? pokemon->avDEF() : pokemon->evDEF(), pokemon->statDEF()},
                    {"SpA", pokemon->baseSPA(), pokemon->ivSPA(),
                     usesAwakeningValues ? pokemon->avSPA() : pokemon->evSPA(), pokemon->statSPA()},
                    {"SpD", pokemon->baseSPD(), pokemon->ivSPD(),
                     usesAwakeningValues ? pokemon->avSPD() : pokemon->evSPD(), pokemon->statSPD()},
                    {"Spe", pokemon->baseSPE(), pokemon->ivSPE(),
                     usesAwakeningValues ? pokemon->avSPE() : pokemon->evSPE(), pokemon->statSPE()},
                };

                const int labelX = cardX + pad;
                const int cBase = cardX + 56;
                const int ivColumnX = cardX + 104;
                const int evColumnX = cardX + 144;
                const int cStat = cardX + 190;
                int statRowY = cardY + 38;
                constexpr int lineH = 16;

                const Color colHdr = selected ? Colors::Text : Colors::TextDim;
                framebuffer.drawText(cBase, statRowY, "Base", colHdr, TextStyle::Caption);
                framebuffer.drawText(ivColumnX, statRowY, "IV", colHdr, TextStyle::Caption);
                framebuffer.drawText(evColumnX, statRowY, usesAwakeningValues ? "AV" : "EV", colHdr, TextStyle::Caption);
                framebuffer.drawText(cStat, statRowY, "Stat", colHdr, TextStyle::Caption);
                statRowY += lineH;

                for (const auto &r : rows)
                {
                    framebuffer.drawText(labelX, statRowY, r.name, Colors::TextDim, TextStyle::Caption);
                    framebuffer.drawText(cBase, statRowY, std::to_string(r.base), Colors::Text, TextStyle::Caption);
                    framebuffer.drawText(ivColumnX, statRowY, std::to_string(r.iv), Colors::Text, TextStyle::Caption);
                    framebuffer.drawText(evColumnX, statRowY, std::to_string(r.evav), Colors::Text, TextStyle::Caption);
                    framebuffer.drawText(cStat, statRowY, std::to_string(r.stat), Colors::Accent, TextStyle::Caption);
                    statRowY += lineH;
                }

                // Type badges (stacked) + sprite (right side)
                Pokemon::TypePair types = Pokemon::getPokemonTypes(pokemon->speciesID(), pokemon->form(), pokemon->getGameGroup());
                const int typeX = cardX + 286;
                drawTypeIcon(framebuffer, SpriteManager::getTypeSprite(types.type1), typeX, cardY + 42, 16);
                if (Pokemon::hasSecondType(types))
                {
                    drawTypeIcon(framebuffer, SpriteManager::getTypeSprite(types.type2), typeX, cardY + 63, 16);
                }

                Sprite *sprite = SpriteManager::getSprite(pokemon->speciesID(), pokemon->form(), isShiny);
                if (sprite && sprite->data)
                {
                    const int spriteBoxSize = 96;
                    int spriteX = cardX + colW - pad - spriteBoxSize;
                    int spriteY = cardY + 30;
                    framebuffer.drawSpriteIdle(spriteX, spriteY, spriteBoxSize, spriteBoxSize, sprite->width, sprite->height, sprite->data, sprite->channels, static_cast<float>(index) * 1.1f);
                }
            }
        }
    }
}
