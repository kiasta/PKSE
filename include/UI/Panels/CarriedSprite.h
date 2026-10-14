#ifndef UI_PANELS_CARRIED_SPRITE_H
#define UI_PANELS_CARRIED_SPRITE_H

#include <algorithm>
#include <string>

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/SpriteManager.h"
#include "Pokemon/Pokemon.h"

namespace UI
{
    namespace Panels
    {
        /**
         * Draw a Pokemon lifted off the board: a soft ground shadow where it would land, plus the
         * sprite raised above it.
         *
         * Shared by the bank's carried block and the Boxes view's grabbed Pokemon, so "picked up"
         * looks identical in both. The two views reach this state by different routes -- the bank
         * genuinely moves the Pokemon out of its slot into `moveMon`, while the Boxes view leaves it
         * in place and only marks its slot -- but from the player's side both should read the same:
         * the slot it came from looks empty and the Pokemon travels with the cursor.
         */
        inline void drawLiftedMon(PKSEFramebuffer &framebuffer, const ::Pokemon::Pokemon *pk, int centerX, int centerY, int discR)
        {
            if (!pk || pk->speciesID() == 0)
                return;
            const int size = static_cast<int>(discR * 1.75);
            const int lift = std::max(6, discR / 4);
            framebuffer.drawFilledEllipse(centerX, centerY + discR - 2, size / 2 - 4, 5, Color(0, 0, 0, 90));
            if (pk->isEgg())
            {
                framebuffer.drawEgg(centerX, centerY - lift, size);
                return;
            }
            const bool shiny = pk->isShiny(pk->id32(), std::string(pk->species()));
            Sprite *sprite = SpriteManager::getIconSprite(pk->speciesID(), pk->form(), shiny);
            if (sprite && sprite->data)
                framebuffer.drawImageScaled(centerX - size / 2, centerY - lift - size / 2, sprite->width, sprite->height, size, size, sprite->data, sprite->channels);
            if (shiny)
                framebuffer.drawShinyMark(centerX + discR - 15, centerY - lift - discR + 1, 15, Colors::ShinyStar);
        }
    }
}

#endif
