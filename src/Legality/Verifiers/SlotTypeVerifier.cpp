// Port of PKHeX PKHeX.Core/Legality/Verifiers/SlotTypeVerifier.cs: the storage slot the Pokemon is in.
#include "Legality/Verifiers/Verifiers.h"

#include "Pokemon/Pokemon.h"

namespace Legality
{
    void verifySlotType(const AnalysisContext &context)
    {
        const StorageSlotType slotType = context.options.slotType;
        if (slotType == StorageSlotType::Unknown)
        {
            return;
        }
        // PKHeX SlotTypeVerifier.IsSourceValid restricts only the fused and ride slots, which PKSE never
        // reports, so StoredSlotSourceInvalid cannot arise here. IsSourceValidEgg keeps an egg to the box
        // and party (plus Poke Pelago and the Gen 2 day care, which PKSE does not report either).
        if (context.pokemon.isEgg() && slotType != StorageSlotType::Box && slotType != StorageSlotType::Party)
        {
            context.add(Severity::Invalid, CheckIdentifier::SlotType, ResultCode::StoredSourceEgg);
        }
    }
}
