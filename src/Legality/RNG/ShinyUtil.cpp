#include "Legality/RNG/ShinyUtil.h"

#include "Enums/GameVersion.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        bool ShinyUtil::isShiny(const Pokemon::Pokemon &pokemon) noexcept
        {
            const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup()));
            const uint32_t shinyXor = ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32());
            return shinyXor < (formatGeneration <= 5 ? 8u : 16u);
        }
    }
}
