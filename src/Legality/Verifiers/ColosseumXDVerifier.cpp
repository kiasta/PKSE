// Port of PKHeX PKHeX.Core/Legality/Verifiers/CXDVerifier.cs: Colosseum and XD.
// PKHeX's XD Eevee starter check (MethodCXD.TryGetSeedStarterXD) runs only once its PID/IV analysis
// has classed the Pokemon as a Colosseum/XD spread, which PKSE does not compute, so it is not ported.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
    }

    void verifyColosseumXD(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        // PKHeX LegalityAnalysis.ParsePK3 runs this for a Colosseum/XD origin, whatever the format.
        if (context.formatGeneration < 3 || getOriginGeneration(context) != 3 || !pokemon.hasOriginGame() || pokemon.originGame() != VERSION_COLOSSEUM_XD)
        {
            return;
        }
        // Colosseum and XD have a male protagonist only.
        if (pokemon.otGender() == 1)
        {
            context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::Gen3OTGender);
        }
    }
}
