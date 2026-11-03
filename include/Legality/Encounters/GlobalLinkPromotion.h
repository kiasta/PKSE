/**
 * The Pokemon Global Link promotions that put a Pokemon in the Entree Forest, and the languages
 * each was offered in. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen5/GlobalLinkPromotion.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_GLOBALLINKPROMOTION_H
#define LEGALITY_ENCOUNTERS_GLOBALLINKPROMOTION_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX GlobalLinkPromotion.
        enum class GlobalLinkPromotion : uint8_t
        {
            // Black/White
            NotPromotion,
            Eeveelutions,
            StartersKanto,
            StartersHoenn,
            StartersSinnoh,
            Croagunk1,
            Croagunk2,
            Arceus,
            Mamoswine,
            Porygon,
            Rayquaza,
            Blissey,
            Jumpluff,
            Altaria,
            Banette,
            Lucario,
            Togekiss,

            // Black 2/White 2
            Gothorita1,
            Gothorita2,
            Pikachu,
            MonkeyUnova,
            StartersSinnoh2,

            // Both
            Dragonite1,
            Dragonite2,
            Scizor,
            Garchomp,
            Tyranitar,
            Metagross,
        };

        /// PKHeX GlobalLinkPromotionExtensions.CanBeReceivedBy: a game of `language` could take part.
        bool canPromotionBeReceivedBy(GlobalLinkPromotion promotion, uint8_t language) noexcept;
    }
}

#endif
