// Port of PKHeX PKHeX.Core/Legality/Verifiers/ItemVerifier.cs: the held item.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Names/ReleasedHeldItemTable.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    void verifyHeldItem(const AnalysisContext &context)
    {
        constexpr uint16_t enigmaBerryItemGen3 = 175;
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const uint16_t heldItem = pokemon.heldItem();
        if (pokemon.isEgg() && heldItem != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::HeldItem, ResultCode::ItemEgg);
        }

        if (!Names::isHeldItemReleased(heldItem, context.formatGroup))
        {
            context.add(Severity::Invalid, CheckIdentifier::HeldItem, ResultCode::ItemUnreleased);
        }
        else if (heldItem == enigmaBerryItemGen3 && Enums::isGen3Group(context.formatGroup))
        {
            // PKHeX ItemVerifier.VerifyEnigmaGen3 -> VerifyEReaderBerry. The verdict rests on the e-Reader
            // berry the open Gen 3 save holds (EReaderBerrySettings), which PKHeX's core never loads on its
            // own: with none loaded the Enigma Berry cannot be held. PKSE does not pass the save's e-Reader
            // berry either, so it gives that same answer. Colosseum, XD and Pokemon Box, which reject the
            // berry outright, are formats PKSE does not open.
            context.add(Severity::Invalid, CheckIdentifier::HeldItem, ResultCode::ItemUnreleased);
        }
    }
}
