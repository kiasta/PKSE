#ifndef UI_PANELS_PARTY_POKEMON_PANEL_H
#define UI_PANELS_PARTY_POKEMON_PANEL_H

#include <vector>
#include <memory>
#include <cstdint>

namespace UI
{
    class PKSEFramebuffer;
}
namespace Pokemon
{
    class Pokemon;
}
namespace Trainer
{
    class Trainer;
}

namespace UI
{
    namespace Panels
    {
        void drawPartyPokemon(UI::PKSEFramebuffer &framebuffer, const Trainer::Trainer &trainer, int partyPokemonX, int partyPokemonY, int partyPokemonWidth, int partyPokemonHeight, int selectedIndex = -1);
    }
}

#endif
