#include "Legality/RNG/HomeQuirks.h"

#include "Enums/GameVersion.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // PKHeX IPersonalTable.IsPresentInGame: a form past the species' form count is in no game.
            bool isPresentInGame(uint16_t species, uint8_t form, uint8_t presenceBit) noexcept
            {
                if (species > Pokemon::PERSONAL_MAX_SPECIES)
                {
                    return false;
                }
                if (form != 0 && form >= Pokemon::getPersonalInfo(species, 0).formCount)
                {
                    return false;
                }
                return (Pokemon::getPersonalInfo(species, form).presence & presenceBit) != 0;
            }
        }

        bool HomeQuirks::hasEnteredSetZeroScale(uint16_t species, uint8_t form) noexcept
        {
            // Only reachable through a format with a Scale field: PK9, PA9 or PA8.
            return isPresentInGame(species, form, Pokemon::PERSONAL_GAME_SV) || isPresentInGame(species, form, Pokemon::PERSONAL_GAME_ZA) || isPresentInGame(species, form, Pokemon::PERSONAL_GAME_PLA);
        }

        bool HomeQuirks::hasEnteredSetZeroScale(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (pokemon.hasHomeTracker() && pokemon.homeTracker() == 0)
            {
                return false;
            }
            if (pokemon.hasScale() && pokemon.scale() != 0)
            {
                return false;
            }
            return hasEnteredSetZeroScale(pokemon.speciesID(), pokemon.form());
        }

        bool HomeQuirks::isTouchedScaleCopiedOrUntouched(const Pokemon::Pokemon &pokemon, uint8_t expectedScale) noexcept
        {
            // HOME copies Scale into Height, and viewing the save in HOME does too; a tracker proves it was viewed.
            if (pokemon.heightScalar() == pokemon.scale())
            {
                return true;
            }
            if (pokemon.hasHomeTracker() && pokemon.homeTracker() != 0)
            {
                return false;
            }
            return pokemon.heightScalar() == expectedScale;
        }

        bool HomeQuirks::hasSizeScalars(const Pokemon::Pokemon &pokemon) noexcept
        {
            const Enums::GameVersion group = pokemon.getGameGroup();
            return group == Enums::GameVersion::GG || group == Enums::GameVersion::SWSH || group == Enums::GameVersion::BDSP || group == Enums::GameVersion::PLA || group == Enums::GameVersion::SV || group == Enums::GameVersion::ZA;
        }

        bool HomeQuirks::hasEnteredSetAlphaMark(const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            const Enums::GameVersion group = pokemon.getGameGroup();
            const bool isGen8Format = group == Enums::GameVersion::SWSH || group == Enums::GameVersion::BDSP || group == Enums::GameVersion::PLA;
            // Moved before HOME 3.0.0, which began setting it.
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::PLA) && isGen8Format)
            {
                return false;
            }
            // HOME 4.0.0 rewrites an Alpha's scale to 255 when it sets the mark.
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::ZA) && hasSizeScalars(pokemon) && pokemon.heightScalar() == 0)
            {
                return false;
            }
            return true;
        }
    }
}
