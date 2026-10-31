#ifndef UI_PANELS_UNMAPPED_SLOT_H
#define UI_PANELS_UNMAPPED_SLOT_H

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"

namespace UI
{
    namespace Panels
    {
        /// What a slot holding a kept Gen 1/2 entry PKSE cannot read is called wherever it is named
        /// (Trainer::isBoxSlotUnmapped / getUnmappedPartySlots).
        inline constexpr const char *UNMAPPED_SLOT_LABEL = "Unknown species";
        /// The mark drawn on such a slot in place of a sprite: no sprite exists for a species
        /// PKSE cannot name.
        inline constexpr const char *UNMAPPED_SLOT_MARK = "?";

        /**
         * Draw a box slot that holds a kept unmapped entry: an occupied disc with a "?" in place of
         * the sprite. The slot is locked (TrainerViewScreen::storageSlotLocked), so it reads as
         * taken and is never a drop target.
         */
        inline void drawUnmappedSlot(PKSEFramebuffer &framebuffer, int centerX, int centerY, int discRadius)
        {
            framebuffer.drawFilledCircle(centerX, centerY, discRadius, Colors::PanelAlt);
            framebuffer.drawCircle(centerX, centerY, discRadius, Colors::Border, 1);
            int markWidth = 0, markHeight = 0;
            framebuffer.measureText(UNMAPPED_SLOT_MARK, markWidth, markHeight, TextStyle::Heading);
            framebuffer.drawText(centerX - markWidth / 2, centerY - markHeight / 2, UNMAPPED_SLOT_MARK, Colors::TextDim, TextStyle::Heading);
        }
    }
}

#endif
