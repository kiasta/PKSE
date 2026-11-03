#include "Legality/Encounters/TemplateRibbons.h"

#include "Legality/Encounters/EncounterGift3.h"
#include "Legality/Encounters/EncounterSlot7GO.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/EncounterTrade9.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWA8.h"
#include "Legality/Encounters/MysteryGiftWA9.h"
#include "Legality/Encounters/MysteryGiftWB7.h"
#include "Legality/Encounters/MysteryGiftWB8.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/MysteryGiftWC9.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX WB7/WC8/WB8.IsHOMEGift: HOME's cards are numbered from 9000.
            constexpr uint16_t HOME_GIFT_CARD_ID_MIN = 9000;

            constexpr std::array<std::string_view, static_cast<size_t>(RibbonIndex::MaxCount)> RIBBON_INDEX_SPRITE_STEMS = {
                "ribbonchampionkalos",
                "ribbonchampiong3",
                "ribbonchampionsinnoh",
                "ribbonbestfriends",
                "ribbontraining",
                "ribbonbattlerskillful",
                "ribbonbattlerexpert",
                "ribboneffort",
                "ribbonalert",
                "ribbonshock",
                "ribbondowncast",
                "ribboncareless",
                "ribbonrelax",
                "ribbonsnooze",
                "ribbonsmile",
                "ribbongorgeous",
                "ribbonroyal",
                "ribbongorgeousroyal",
                "ribbonartist",
                "ribbonfootprint",
                "ribbonrecord",
                "ribbonlegend",
                "ribboncountry",
                "ribbonnational",
                "ribbonearth",
                "ribbonworld",
                "ribbonclassic",
                "ribbonpremier",
                "ribbonevent",
                "ribbonbirthday",
                "ribbonspecial",
                "ribbonsouvenir",
                "ribbonwishing",
                "ribbonchampionbattle",
                "ribbonchampionregional",
                "ribbonchampionnational",
                "ribbonchampionworld",
                "ribboncountmemorycontest",
                "ribboncountmemorybattle",
                "ribbonchampiong6hoenn",
                "ribbonconteststar",
                "ribbonmastercoolness",
                "ribbonmasterbeauty",
                "ribbonmastercuteness",
                "ribbonmastercleverness",
                "ribbonmastertoughness",
                "ribbonchampionalola",
                "ribbonbattleroyale",
                "ribbonbattletreegreat",
                "ribbonbattletreemaster",
                "ribbonchampiongalar",
                "ribbontowermaster",
                "ribbonmasterrank",
                "ribbonmarklunchtime",
                "ribbonmarksleepytime",
                "ribbonmarkdusk",
                "ribbonmarkdawn",
                "ribbonmarkcloudy",
                "ribbonmarkrainy",
                "ribbonmarkstormy",
                "ribbonmarksnowy",
                "ribbonmarkblizzard",
                "ribbonmarkdry",
                "ribbonmarksandstorm",
                "ribbonmarkmisty",
                "ribbonmarkdestiny",
                "ribbonmarkfishing",
                "ribbonmarkcurry",
                "ribbonmarkuncommon",
                "ribbonmarkrare",
                "ribbonmarkrowdy",
                "ribbonmarkabsentminded",
                "ribbonmarkjittery",
                "ribbonmarkexcited",
                "ribbonmarkcharismatic",
                "ribbonmarkcalmness",
                "ribbonmarkintense",
                "ribbonmarkzonedout",
                "ribbonmarkjoyful",
                "ribbonmarkangry",
                "ribbonmarksmiley",
                "ribbonmarkteary",
                "ribbonmarkupbeat",
                "ribbonmarkpeeved",
                "ribbonmarkintellectual",
                "ribbonmarkferocious",
                "ribbonmarkcrafty",
                "ribbonmarkscowling",
                "ribbonmarkkindly",
                "ribbonmarkflustered",
                "ribbonmarkpumpedup",
                "ribbonmarkzeroenergy",
                "ribbonmarkprideful",
                "ribbonmarkunsure",
                "ribbonmarkhumble",
                "ribbonmarkthorny",
                "ribbonmarkvigor",
                "ribbonmarkslump",
                "ribbonhisui",
                "ribbontwinklingstar",
                "ribbonchampionpaldea",
                "ribbonmarkjumbo",
                "ribbonmarkmini",
                "ribbonmarkitemfinder",
                "ribbonmarkpartner",
                "ribbonmarkgourmand",
                "ribbononceinalifetime",
                "ribbonmarkalpha",
                "ribbonmarkmightiest",
                "ribbonmarktitan",
                "ribbonpartner",
            };

            /// PKHeX's Event3 and Event4 interfaces together, as every Mystery Gift card implements them.
            TemplateRibbons getCardRibbons(const RibbonIndexSet &ribbons, bool hasCommon7) noexcept
            {
                TemplateRibbons templateRibbons;
                templateRibbons.hasEvent3 = true;
                templateRibbons.hasEvent4 = true;
                templateRibbons.hasCommon7 = hasCommon7;
                templateRibbons.ribbons = ribbons;
                return templateRibbons;
            }

            void addRibbon(RibbonIndexSet &ribbons, RibbonIndex ribbon) noexcept
            {
                const uint8_t ribbonValue = static_cast<uint8_t>(ribbon);
                ribbons.bits[ribbonValue >> 3] = static_cast<uint8_t>(ribbons.bits[ribbonValue >> 3] | (1u << (ribbonValue & 7)));
            }
        }

        std::string_view getRibbonIndexSpriteStem(RibbonIndex ribbon) noexcept
        {
            const size_t ribbonValue = static_cast<size_t>(ribbon);
            return ribbonValue < RIBBON_INDEX_SPRITE_STEMS.size() ? RIBBON_INDEX_SPRITE_STEMS[ribbonValue] : std::string_view();
        }

        RibbonIndex getRibbonIndexFromSpriteStem(std::string_view spriteStem) noexcept
        {
            for (size_t ribbonValue = 0; ribbonValue < RIBBON_INDEX_SPRITE_STEMS.size(); ++ribbonValue)
            {
                if (RIBBON_INDEX_SPRITE_STEMS[ribbonValue] == spriteStem)
                {
                    return static_cast<RibbonIndex>(ribbonValue);
                }
            }
            return RibbonIndex::MaxCount;
        }

        TemplateRibbons getTemplateRibbons(const Encounterable &encounter) noexcept
        {
            if (const EncounterGift3 *gift3 = encounterAs<EncounterGift3>(encounter))
            {
                // PKHeX EncounterGift3 is IRibbonSetEvent3 only, and of those sets only the National Ribbon.
                TemplateRibbons templateRibbons;
                templateRibbons.hasEvent3 = true;
                if (gift3->giftProperties().hasNationalRibbon)
                {
                    addRibbon(templateRibbons.ribbons, RibbonIndex::National);
                }
                return templateRibbons;
            }
            if (encounterAs<MysteryGiftPGT>(encounter))
            {
                return getCardRibbons(RibbonIndexSet{}, false); // the blank Ranger Manaphy card
            }
            if (const MysteryGiftPCD *card = encounterAs<MysteryGiftPCD>(encounter))
            {
                return getCardRibbons(card->giftData().ribbons, false);
            }
            if (const MysteryGiftPGF *card = encounterAs<MysteryGiftPGF>(encounter))
            {
                return getCardRibbons(card->giftData().ribbons, false);
            }
            if (const MysteryGiftWC6 *card = encounterAs<MysteryGiftWC6>(encounter))
            {
                return getCardRibbons(card->card().ribbons, false);
            }
            if (const MysteryGiftWC7 *card = encounterAs<MysteryGiftWC7>(encounter))
            {
                return getCardRibbons(card->card().ribbons, false);
            }
            if (const MysteryGiftWB7 *card = encounterAs<MysteryGiftWB7>(encounter))
            {
                return getCardRibbons(card->card().ribbons, false);
            }
            if (const MysteryGiftWC8 *card = encounterAs<MysteryGiftWC8>(encounter))
            {
                return getCardRibbons(card->card().ribbons, true);
            }
            if (const MysteryGiftWB8 *card = encounterAs<MysteryGiftWB8>(encounter))
            {
                return getCardRibbons(card->card().ribbons, true);
            }
            if (const MysteryGiftWA8 *card = encounterAs<MysteryGiftWA8>(encounter))
            {
                return getCardRibbons(card->card().ribbons, true);
            }
            if (const MysteryGiftWC9 *card = encounterAs<MysteryGiftWC9>(encounter))
            {
                return getCardRibbons(card->card().ribbons, true);
            }
            if (const MysteryGiftWA9 *card = encounterAs<MysteryGiftWA9>(encounter))
            {
                return getCardRibbons(card->card().ribbons, true);
            }
            if (const EncounterTrade9 *trade = encounterAs<EncounterTrade9>(encounter))
            {
                // PKHeX EncounterTrade9 is IRibbonPartner only: the Academy staff's trades.
                TemplateRibbons templateRibbons;
                if (trade->hasPartnerRibbon())
                {
                    addRibbon(templateRibbons.ribbons, RibbonIndex::Partner);
                }
                return templateRibbons;
            }
            return TemplateRibbons{};
        }

        bool isEncounterDateRestricted(const Encounterable &encounter) noexcept
        {
            if (encounterAs<EncounterSlot7GO>(encounter) || encounterAs<EncounterSlot8GO>(encounter))
            {
                return true;
            }
            if (const MysteryGiftWB7 *card = encounterAs<MysteryGiftWB7>(encounter))
            {
                return card->card().cardId >= HOME_GIFT_CARD_ID_MIN; // PKHeX WB7.IsHOMEGift
            }
            if (const MysteryGiftWC8 *card = encounterAs<MysteryGiftWC8>(encounter))
            {
                return card->isHomeGift();
            }
            if (const MysteryGiftWB8 *card = encounterAs<MysteryGiftWB8>(encounter))
            {
                return card->isDateRestricted();
            }
            return encounterAs<MysteryGiftWA8>(encounter) || encounterAs<MysteryGiftWC9>(encounter) || encounterAs<MysteryGiftWA9>(encounter);
        }
    }
}
