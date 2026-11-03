#include "Legality/Encounters/EncounterEntity.h"

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"
#include "Legality/RNG/ShinyUtil.h"

namespace Legality
{
    namespace Encounters
    {
        uint8_t getFormatGeneration(const Pokemon::Pokemon &pokemon) noexcept
        {
            return static_cast<uint8_t>(Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup())));
        }

        uint8_t getOriginGeneration(const Pokemon::Pokemon &pokemon)
        {
            const Evolutions::EvolutionEntity entity = Evolutions::getEvolutionEntity(pokemon);
            return static_cast<uint8_t>(entity.originGeneration);
        }

        uint8_t getOriginVersion(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.hasOriginGame() ? pokemon.originGame() : 0;
        }

        bool wasEgg(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.isEgg() || pokemon.eggDay() != 0;
        }

        bool hasOriginalMetLevel(const Pokemon::Pokemon &pokemon)
        {
            const uint8_t formatGeneration = getFormatGeneration(pokemon);
            if (formatGeneration < 3 || isVirtualConsole(pokemon))
            {
                return false;
            }
            const uint8_t originGeneration = getOriginGeneration(pokemon);
            return !(originGeneration <= 4 && formatGeneration != originGeneration);
        }

        bool isShiny(const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint8_t formatGeneration = getFormatGeneration(pokemon);
            const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32());
            return shinyXor < (formatGeneration <= 6 ? 8u : 16u);
        }

        bool isVersion(const Pokemon::Pokemon &pokemon, Enums::GameVersion version) noexcept
        {
            return getOriginVersion(pokemon) == static_cast<uint8_t>(version);
        }

        Enums::GameVersion getEntityContextGroup(const Pokemon::Pokemon &pokemon) noexcept
        {
            return Evolutions::getEvolutionContextGroup(pokemon.getGameGroup());
        }

        const char *getVersionSourceName(Enums::GameVersion version) noexcept
        {
            using Enums::GameVersion;
            switch (version)
            {
            case GameVersion::SA:
                return "S";
            case GameVersion::RU:
                return "R";
            case GameVersion::EM:
                return "E";
            case GameVersion::FR:
                return "FR";
            case GameVersion::LG:
                return "LG";
            case GameVersion::RSE:
                return "RSE";
            case GameVersion::FRLG:
                return "FRLG";
            case GameVersion::RD:
                return "RD";
            case GameVersion::GN:
                return "GN";
            case GameVersion::BU:
                return "BU";
            case GameVersion::YW:
                return "YW";
            case GameVersion::GD:
                return "GD";
            case GameVersion::SI:
                return "SI";
            case GameVersion::C:
                return "C";
            case GameVersion::D:
                return "D";
            case GameVersion::P:
                return "P";
            case GameVersion::Pt:
                return "Pt";
            case GameVersion::HG:
                return "HG";
            case GameVersion::SS:
                return "SS";
            case GameVersion::DP:
                return "DP";
            case GameVersion::HGSS:
                return "HGSS";
            case GameVersion::W:
                return "W";
            case GameVersion::B:
                return "B";
            case GameVersion::W2:
                return "W2";
            case GameVersion::B2:
                return "B2";
            case GameVersion::BW:
                return "BW";
            case GameVersion::B2W2:
                return "B2W2";
            case GameVersion::X:
                return "X";
            case GameVersion::Y:
                return "Y";
            case GameVersion::AS:
                return "AS";
            case GameVersion::OR:
                return "OR";
            case GameVersion::XY:
                return "XY";
            case GameVersion::ORAS:
                return "ORAS";
            case GameVersion::SN:
                return "SN";
            case GameVersion::MN:
                return "MN";
            case GameVersion::US:
                return "US";
            case GameVersion::UM:
                return "UM";
            case GameVersion::SM:
                return "SM";
            case GameVersion::USUM:
                return "USUM";
            case GameVersion::GP:
                return "GP";
            case GameVersion::GE:
                return "GE";
            case GameVersion::GG:
                return "GG";
            case GameVersion::RBY:
                return "RBY";
            case GameVersion::GSC:
                return "GSC";
            default:
                return "Any";
            }
        }

        // PKHeX PKHeX.Core/Legality/Tables/FormInfo.cs IsFormChangeable
        bool isFormChangeable(uint16_t speciesId, uint8_t oldFormId, uint8_t newFormId, Enums::GameVersion originContextGroup, Enums::GameVersion currentContextGroup) noexcept
        {
            constexpr uint16_t speciesZygarde = 718;
            constexpr uint16_t speciesDeerling = 585;
            constexpr uint16_t speciesSawsbuck = 586;
            using Enums::GameVersion;
            if (speciesId == speciesZygarde)
            {
                if (currentContextGroup == GameVersion::XY)
                {
                    return false;
                }
                if (currentContextGroup == GameVersion::SM)
                {
                    return newFormId >= 2 || (oldFormId == 1 && newFormId == 0);
                }
                if (currentContextGroup == GameVersion::ZA && originContextGroup == GameVersion::ZA)
                {
                    return newFormId >= 2;
                }
                return true;
            }
            if (speciesId == speciesDeerling || speciesId == speciesSawsbuck)
            {
                if (originContextGroup == GameVersion::BW)
                {
                    return true;
                }
                const bool isEraHome = currentContextGroup == GameVersion::SWSH || currentContextGroup == GameVersion::PLA || currentContextGroup == GameVersion::BDSP || currentContextGroup == GameVersion::SV || currentContextGroup == GameVersion::ZA;
                return isEraHome;
            }
            return Pokemon::isFormChangeable(speciesId);
        }

        // PKHeX PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterStateUtil.cs CanBeWildEncounter
        bool EncounterStateUtil::canBeWildEncounter(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (pokemon.isEgg())
            {
                return false;
            }
            // PKHeX IsMetAsEgg. Brilliant Diamond/Shining Pearl write "none" as 65535, not 0.
            const Enums::GameVersion group = pokemon.getGameGroup();
            if (group == Enums::GameVersion::BDSP)
            {
                return pokemon.eggLocation() == LOCATION_DEFAULT8B_NONE;
            }
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            // A BDSP-origin egg moved to Sword/Shield or Legends: Arceus by HOME keeps only its egg date.
            const uint8_t originVersion = getOriginVersion(pokemon);
            const bool isBrilliantDiamondShiningPearlOrigin = originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP);
            const bool isFormatHoldingEggDate = group == Enums::GameVersion::SWSH || group == Enums::GameVersion::PLA;
            return !(isFormatHoldingEggDate && isBrilliantDiamondShiningPearlOrigin && pokemon.eggDay() != 0);
        }

        // PKHeX PKHeX.Core/PKM/Interfaces/Templates/IContestStatsReadOnly.cs IsContestBelow
        bool ContestStatsReadOnly::isContestBelow(const Pokemon::Pokemon &pokemon, const std::array<uint8_t, 6> &initialStats) noexcept
        {
            if (!pokemon.hasContestStats())
            {
                return false;
            }
            for (int statIndex = 0; statIndex < static_cast<int>(initialStats.size()); statIndex++)
            {
                if (pokemon.contestStat(statIndex) < initialStats[statIndex])
                {
                    return true;
                }
            }
            return false;
        }
    }
}
