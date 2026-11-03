// Port of PKHeX PKHeX.Core/Legality/Verifiers/GroundTileVerifier.cs: the Gen 4 ground tile.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        // PKHeX GroundTileVerifier.IsGroundTileValid: only a Gen 4 wild slot or static encounter
        // (IGroundTypeTile) records a tile; every other template must leave it empty. False also when the
        // encounter may be one of those two, whose permitted tiles PKSE's rows do not carry.
        bool isGroundTileForbidden(const AnalysisContext &context)
        {
            if (getOriginGeneration(context) != 4)
            {
                return true;
            }
            constexpr uint16_t daycareEggLocationGen4 = 2000;
            constexpr uint16_t linkTradeEggLocationGen4 = 2002;
            switch (context.encounter.type)
            {
            case EncounterType::Trade:
            case EncounterType::Event:
                return true;
            case EncounterType::Egg:
            {
                // A bred egg; an egg some character hands over is a static encounter instead.
                const uint16_t eggLocation = context.pokemon.eggLocation();
                const bool isBredEgg = eggLocation == daycareEggLocationGen4 || eggLocation == linkTradeEggLocationGen4;
                return isBredEgg;
            }
            default:
                // An Unmatched search may still be a slot PKHeX matches, and Unchecked has no template.
                return false;
            }
        }
    }

    void verifyGroundTile(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasGroundTile() || pokemon.groundTile() == 0)
        {
            return;
        }
        if (isGroundTileForbidden(context))
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::EncounterTypeMismatch);
        }
    }
}
