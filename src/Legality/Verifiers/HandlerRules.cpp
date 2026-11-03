#include "Legality/Verifiers/HandlerRules.h"

#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    Enums::GameVersion getContextGroup(Enums::GameVersion group)
    {
        using Enums::GameVersion;
        switch (group)
        {
        case GameVersion::FRLG:
            return GameVersion::RSE;
        case GameVersion::PT:
        case GameVersion::HGSS:
            return GameVersion::DP;
        case GameVersion::B2W2:
            return GameVersion::BW;
        case GameVersion::ORAS:
            return GameVersion::XY;
        case GameVersion::USUM:
            return GameVersion::SM;
        default:
            return group;
        }
    }

    int getEntityGeneration(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (pokemon.hasOriginGame() && context.formatGeneration > 2 && pokemon.originGame() == 0)
        {
            return 0;
        }
        return getOriginGeneration(context);
    }

    bool isGoHome(const Pokemon::Pokemon &pokemon)
    {
        return pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO && pokemon.metLocation() == LOCATION_GO_HOME;
    }

    bool isGoLetsGo(const Pokemon::Pokemon &pokemon)
    {
        return pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO && pokemon.metLocation() == LOCATION_GO_LETS_GO;
    }

    bool isUntraded(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasHandler() || !pokemon.htName().empty())
        {
            return false;
        }
        // PK9 and PA9 store no version for an egg until it hatches (PKHeX IsUnhatchedEgg).
        const bool storesNoEggVersion = context.formatGroup == Enums::GameVersion::SV || context.formatGroup == Enums::GameVersion::ZA;
        if (storesNoEggVersion && pokemon.isEgg() && pokemon.originGame() == 0)
        {
            return true;
        }
        return context.formatGeneration == getEntityGeneration(context);
    }

    Enums::GameVersion getEncounterContextGroup(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const EncounterInfo &encounter = context.encounter;
        if (encounter.type == EncounterType::Unmatched)
        {
            return getContextGroup(context.formatGroup); // PKHeX EncounterInvalid takes the entity's context
        }
        if (isVirtualConsole(pokemon))
        {
            return Enums::GameVersion::SM; // PKHeX EncounterTransfer7
        }
        if (pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO)
        {
            return isGoLetsGo(pokemon) ? Enums::GameVersion::GG : Enums::GameVersion::Invalid;
        }
        if (encounter.type != EncounterType::Unchecked && encounter.originGroup != Enums::GameVersion::Invalid)
        {
            return getContextGroup(encounter.originGroup);
        }
        return getContextGroup(getOriginContextGroup(context));
    }
}
