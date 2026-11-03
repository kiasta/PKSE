#include "Legality/Encounters/EncounterTransfer7.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_GASTLY = 92;
            constexpr uint16_t SPECIES_HAUNTER = 93;
            constexpr uint16_t SPECIES_GENGAR = 94;
            constexpr uint16_t SPECIES_KOFFING = 109;
            constexpr uint16_t SPECIES_WEEZING = 110;
            constexpr uint16_t SPECIES_MEW = 151;
            constexpr uint16_t SPECIES_UNOWN = 201;
            constexpr uint16_t SPECIES_MISDREAVUS = 200;
            constexpr uint16_t SPECIES_CELEBI = 251;
            /// PKHeX Legal.MaxSpeciesID_1 / MaxSpeciesID_2.
            constexpr uint16_t MAX_SPECIES_ID_1 = 151;
            constexpr uint16_t MAX_SPECIES_ID_2 = 251;
            /// PKHeX GameVersion RD .. YW: the Virtual Console Red/Green/Blue/Yellow ids.
            constexpr uint8_t VERSION_RED = 35;
            constexpr uint8_t VERSION_YELLOW = 38;

            /// PKHeX GetVCSpecies: the most devolved stage whose base friendship matches the record's.
            uint16_t getVirtualConsoleSpecies(std::span<const Evolutions::EvolutionCriteria> chain, const Pokemon::Pokemon &pokemon, uint16_t maxSpeciesId)
            {
                for (size_t stageIndex = chain.size(); stageIndex > 0; stageIndex--)
                {
                    const Evolutions::EvolutionCriteria &stage = chain[stageIndex - 1];
                    if (stage.speciesId > maxSpeciesId || stage.formId != 0)
                    {
                        continue;
                    }
                    if (Pokemon::getPersonalRecord(Enums::GameVersion::SM, stage.speciesId, stage.formId).baseFriendship != pokemon.otFriendship())
                    {
                        continue;
                    }
                    return stage.speciesId;
                }
                return pokemon.speciesID();
            }
        }

        bool TransporterLogic::isHiddenDisallowedVC1(uint16_t speciesId) noexcept
        {
            return speciesId == SPECIES_GASTLY || speciesId == SPECIES_HAUNTER || speciesId == SPECIES_GENGAR || speciesId == SPECIES_KOFFING || speciesId == SPECIES_WEEZING || speciesId == SPECIES_MEW;
        }

        bool TransporterLogic::isHiddenDisallowedVC2(uint16_t speciesId) noexcept
        {
            return isHiddenDisallowedVC1(speciesId) || speciesId == SPECIES_MISDREAVUS || speciesId == SPECIES_UNOWN || speciesId == SPECIES_CELEBI;
        }

        EncounterTransfer7 EncounterTransfer7::getVC1(uint16_t speciesId, uint8_t metLevel) noexcept
        {
            const bool isMew = speciesId == SPECIES_MEW;
            const RNG::AbilityPermission ability = TransporterLogic::isHiddenDisallowedVC1(speciesId) ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlyHidden;
            return EncounterTransfer7(Enums::GameVersion::RBY, speciesId, metLevel, LOCATION_TRANSFER1, isMew ? RNG::Shiny::Never : RNG::Shiny::Random, ability, isMew, isMew ? 5 : 3);
        }

        EncounterTransfer7 EncounterTransfer7::getVC2(uint16_t speciesId, uint8_t metLevel) noexcept
        {
            const bool isMew = speciesId == SPECIES_MEW;
            const bool isFateful = isMew || speciesId == SPECIES_CELEBI;
            const RNG::AbilityPermission ability = TransporterLogic::isHiddenDisallowedVC2(speciesId) ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlyHidden;
            return EncounterTransfer7(Enums::GameVersion::GSC, speciesId, metLevel, LOCATION_TRANSFER2, isMew ? RNG::Shiny::Never : RNG::Shiny::Random, ability, isFateful, isFateful ? 5 : 3);
        }

        std::string EncounterTransfer7::name() const
        {
            return transferVersion == Enums::GameVersion::RBY ? "Virtual Console Transfer (RBY)" : "Virtual Console Transfer (GSC)";
        }

        EncounterTransfer7 getVirtualConsoleTransferEncounter(const Pokemon::Pokemon &pokemon, uint16_t encounterSpeciesId, std::span<const Evolutions::EvolutionCriteria> chain)
        {
            const uint8_t metLevel = pokemon.metLevel();
            const uint8_t version = getOriginVersion(pokemon);
            if (version >= VERSION_RED && version <= VERSION_YELLOW)
            {
                // Only a Pokemon that could come from Gen 1 gets a Gen 1 template: anything only Gen 2
                // has (Entei) falls through even when it claims a Gen 1 origin.
                const uint16_t speciesId = getVirtualConsoleSpecies(chain, pokemon, MAX_SPECIES_ID_1);
                const uint16_t generationOneSpeciesId = speciesId > MAX_SPECIES_ID_1 ? encounterSpeciesId : speciesId;
                if (generationOneSpeciesId <= MAX_SPECIES_ID_1)
                {
                    return EncounterTransfer7::getVC1(generationOneSpeciesId, metLevel);
                }
            }
            const uint16_t speciesId = getVirtualConsoleSpecies(chain, pokemon, MAX_SPECIES_ID_2);
            return EncounterTransfer7::getVC2(speciesId > MAX_SPECIES_ID_2 ? encounterSpeciesId : speciesId, metLevel);
        }
    }
}
