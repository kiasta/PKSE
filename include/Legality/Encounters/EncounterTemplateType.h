/**
 * One value per concrete PKHeX encounter template class (PKHeX.Core/Legality/Encounters/Templates
 * and PKHeX.Core/MysteryGifts), which Encounterable::templateType() answers in place of RTTI.
 *
 * Every class is listed up front, generation by generation, so a per-generation port never has to
 * edit this file. Values are not serialized; the order carries no meaning.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTEMPLATETYPE_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTEMPLATETYPE_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        enum class EncounterTemplateType : uint8_t
        {
            Slot1, // PKHeX EncounterSlot1
            Static1, // PKHeX EncounterStatic1
            Trade1, // PKHeX EncounterTrade1
            Gift1, // PKHeX EncounterGift1
            Slot2, // PKHeX EncounterSlot2
            Static2, // PKHeX EncounterStatic2
            Trade2, // PKHeX EncounterTrade2
            Gift2, // PKHeX EncounterGift2
            Egg2, // PKHeX EncounterEgg2
            Slot3, // PKHeX EncounterSlot3
            Slot3Swarm, // PKHeX EncounterSlot3Swarm
            Static3, // PKHeX EncounterStatic3
            Trade3, // PKHeX EncounterTrade3
            Gift3, // PKHeX EncounterGift3
            Gift3JPN, // PKHeX EncounterGift3JPN
            Gift3NY, // PKHeX EncounterGift3NY
            Egg3, // PKHeX EncounterEgg3
            Gift3Colo, // PKHeX EncounterGift3Colo
            Starter3Colo, // PKHeX EncounterStarter3Colo
            Shadow3Colo, // PKHeX EncounterShadow3Colo
            Slot3XD, // PKHeX EncounterSlot3XD
            Static3XD, // PKHeX EncounterStatic3XD
            Trade3XD, // PKHeX EncounterTrade3XD
            Shadow3XD, // PKHeX EncounterShadow3XD
            Slot4, // PKHeX EncounterSlot4
            Static4, // PKHeX EncounterStatic4
            Static4Pokewalker, // PKHeX EncounterStatic4Pokewalker
            Trade4PID, // PKHeX EncounterTrade4PID
            Trade4RanchGift, // PKHeX EncounterTrade4RanchGift
            Egg4, // PKHeX EncounterEgg4
            Slot5, // PKHeX EncounterSlot5
            Static5, // PKHeX EncounterStatic5
            Static5Entree, // PKHeX EncounterStatic5Entree
            Static5N, // PKHeX EncounterStatic5N
            Static5Radar, // PKHeX EncounterStatic5Radar
            Trade5BW, // PKHeX EncounterTrade5BW
            Trade5B2W2, // PKHeX EncounterTrade5B2W2
            Egg5, // PKHeX EncounterEgg5
            Slot6XY, // PKHeX EncounterSlot6XY
            Slot6AO, // PKHeX EncounterSlot6AO
            Static6, // PKHeX EncounterStatic6
            Trade6, // PKHeX EncounterTrade6
            Egg6, // PKHeX EncounterEgg6
            Slot7, // PKHeX EncounterSlot7
            Static7, // PKHeX EncounterStatic7
            Trade7, // PKHeX EncounterTrade7
            Transfer7, // PKHeX EncounterTransfer7
            Egg7, // PKHeX EncounterEgg7
            Slot7GO, // PKHeX EncounterSlot7GO
            Slot7b, // PKHeX EncounterSlot7b
            Static7b, // PKHeX EncounterStatic7b
            Trade7b, // PKHeX EncounterTrade7b
            Slot8, // PKHeX EncounterSlot8
            Static8, // PKHeX EncounterStatic8
            Static8N, // PKHeX EncounterStatic8N
            Static8NC, // PKHeX EncounterStatic8NC
            Static8ND, // PKHeX EncounterStatic8ND
            Static8U, // PKHeX EncounterStatic8U
            Static8Nest, // PKHeX EncounterStatic8Nest
            Trade8, // PKHeX EncounterTrade8
            Egg8, // PKHeX EncounterEgg8
            Slot8GO, // PKHeX EncounterSlot8GO
            Slot8a, // PKHeX EncounterSlot8a
            Static8a, // PKHeX EncounterStatic8a
            Slot8b, // PKHeX EncounterSlot8b
            Static8b, // PKHeX EncounterStatic8b
            Trade8b, // PKHeX EncounterTrade8b
            Egg8b, // PKHeX EncounterEgg8b
            Slot9, // PKHeX EncounterSlot9
            Static9, // PKHeX EncounterStatic9
            Trade9, // PKHeX EncounterTrade9
            Tera9, // PKHeX EncounterTera9
            Dist9, // PKHeX EncounterDist9
            Might9, // PKHeX EncounterMight9
            Fixed9, // PKHeX EncounterFixed9
            Outbreak9, // PKHeX EncounterOutbreak9
            Egg9, // PKHeX EncounterEgg9
            Slot9a, // PKHeX EncounterSlot9a
            Static9a, // PKHeX EncounterStatic9a
            Trade9a, // PKHeX EncounterTrade9a
            Gift9a, // PKHeX EncounterGift9a
            MysteryGiftPGT, // PKHeX PGT
            MysteryGiftPCD, // PKHeX PCD
            MysteryGiftPGF, // PKHeX PGF
            MysteryGiftWC6, // PKHeX WC6
            MysteryGiftWC7, // PKHeX WC7
            MysteryGiftWB7, // PKHeX WB7
            MysteryGiftWR7, // PKHeX WR7
            MysteryGiftWC8, // PKHeX WC8
            MysteryGiftWB8, // PKHeX WB8
            MysteryGiftWA8, // PKHeX WA8
            MysteryGiftWC9, // PKHeX WC9
            MysteryGiftWA9, // PKHeX WA9
            Invalid, // PKHeX EncounterInvalid
        };

        /// PKHeX `enc is MysteryGift`: one of the Wonder Card and raw gift classes.
        constexpr bool isMysteryGift(EncounterTemplateType type) noexcept
        {
            return type >= EncounterTemplateType::MysteryGiftPGT && type <= EncounterTemplateType::MysteryGiftWA9;
        }
    }
}

#endif
