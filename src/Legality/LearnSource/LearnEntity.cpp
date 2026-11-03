// The PKM facts PKHeX's move verifiers read, with PKHeX PKHeX.Core/Legality/Legal.cs
// IsOriginalMovesetDeleted. See LearnEntity.h.
#include "Legality/LearnSource/LearnEntity.h"

#include "Enums/LanguageID.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Pokemon9SV.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            // PKHeX LocationsHOME: where HOME stamps a Pokemon sent to Sword/Shield from another game.
            constexpr uint16_t LOCATION_HOME_LEAFGREEN_TO_SHIELD = 59994;
            constexpr uint16_t LOCATION_HOME_FIRERED_TO_SWORD = 59995;
            constexpr uint16_t LOCATION_HOME_LEGENDS_ARCEUS_TO_SWORD = 60000;
            // PKHeX GameVersion values PKSE has no enumerator for.
            constexpr uint8_t VERSION_FIRERED_VIRTUAL_CONSOLE = 56;
            constexpr uint8_t VERSION_LEAFGREEN_VIRTUAL_CONSOLE = 57;
            constexpr uint8_t VERSION_RED = 35;
            constexpr uint8_t VERSION_YELLOW = 38;

            /// PKHeX PK8.IsSideTransfer (LocationsHOME.IsLocationSWSH).
            bool isSideTransfer(uint16_t metLocation) noexcept
            {
                return metLocation >= LOCATION_HOME_LEAFGREEN_TO_SHIELD && metLocation <= LOCATION_HOME_LEGENDS_ARCEUS_TO_SWORD;
            }

            bool isVersionIn(uint8_t version, Enums::GameVersion first, Enums::GameVersion second) noexcept
            {
                return version == static_cast<uint8_t>(first) || version == static_cast<uint8_t>(second);
            }
        }

        bool LearnEntity::isOriginalMovesetDeleted() const noexcept
        {
            using Enums::GameVersion;
            switch (formatGroup)
            {
                case GameVersion::PLA:
                    return originVersion != static_cast<uint8_t>(GameVersion::PLA);
                case GameVersion::BDSP:
                    return !isVersionIn(originVersion, GameVersion::BD, GameVersion::SP);
                case GameVersion::SWSH:
                    return battleVersion != 0 || (isSideTransfer(metLocation) && !(speciesId == CELEBI_SPECIES && isVirtualConsoleGeneration3));
                case GameVersion::SV:
                    return !(isVersionIn(originVersion, GameVersion::SL, GameVersion::VL) || (isEgg && originVersion == 0));
                case GameVersion::ZA:
                    return originVersion != static_cast<uint8_t>(GameVersion::ZA);
                default:
                    return false;
            }
        }

        bool LearnEntity::isTechnicalRecordFlagSet(int recordIndex) const noexcept
        {
            if (pokemon == nullptr || formatGroup != Enums::GameVersion::SWSH)
            {
                return false;
            }
            return static_cast<const Pokemon::Pokemon8SWSH *>(pokemon)->isTRFlagSet(recordIndex);
        }

        bool LearnEntity::isTechnicalMachineFlagSet(int machineIndex) const noexcept
        {
            if (pokemon == nullptr || formatGroup != Enums::GameVersion::SV)
            {
                return false;
            }
            return static_cast<const Pokemon::Pokemon9SV *>(pokemon)->isTMFlagSet(machineIndex);
        }

        LearnEntity getLearnEntity(const Pokemon::Pokemon &pokemon, bool allowsGameBoyCartridgeEra, bool allowsGameBoyAdvanceCartridgeEra)
        {
            const Evolutions::EvolutionEntity evolutionEntity = Evolutions::getEvolutionEntity(pokemon);
            LearnEntity entity;
            entity.pokemon = &pokemon;
            entity.speciesId = evolutionEntity.speciesId;
            entity.formId = evolutionEntity.formId;
            entity.formatGroup = evolutionEntity.formatGroup;
            entity.formatGeneration = evolutionEntity.formatGeneration;
            entity.contextGroup = Evolutions::getEvolutionContextGroup(entity.formatGroup);
            entity.originVersion = evolutionEntity.originVersion;
            entity.metLevel = evolutionEntity.metLevel;
            entity.metLocation = evolutionEntity.metLocation;
            entity.isEgg = evolutionEntity.isEgg;
            entity.isKorean = pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean);
            entity.isUntraded = evolutionEntity.isUntraded;
            entity.hasHomeTracker = evolutionEntity.hasHomeTracker;
            entity.battleVersion = pokemon.hasBattleVersion() ? pokemon.battleVersion() : 0;
            entity.isVirtualConsoleGeneration1 = entity.originVersion >= VERSION_RED && entity.originVersion <= VERSION_YELLOW;
            if (entity.formatGroup == Enums::GameVersion::SWSH)
            {
                entity.isVirtualConsoleGeneration3 = entity.metLocation == LOCATION_HOME_LEAFGREEN_TO_SHIELD || entity.metLocation == LOCATION_HOME_FIRERED_TO_SWORD;
            }
            else
            {
                entity.isVirtualConsoleGeneration3 = entity.originVersion == VERSION_FIRERED_VIRTUAL_CONSOLE || entity.originVersion == VERSION_LEAFGREEN_VIRTUAL_CONSOLE;
            }
            if (entity.formatGroup == Enums::GameVersion::RBY)
            {
                entity.catchRate = static_cast<const Pokemon::Pokemon1RBY &>(pokemon).catchRate();
            }
            entity.caughtData = evolutionEntity.caughtData;
            for (int slotIndex = 0; slotIndex < 4; ++slotIndex)
            {
                entity.moves[slotIndex] = pokemon.move(slotIndex);
                entity.relearnMoves[slotIndex] = entity.formatGeneration >= 6 ? pokemon.relearnMove(slotIndex) : 0;
            }
            entity.allowsGameBoyCartridgeEra = allowsGameBoyCartridgeEra;
            // PKHeX ParseSettings.AllowGBACrossTransferRSE: the cartridge era, or a cartridge Pokemon in HOME.
            entity.allowsGameBoyAdvanceCrossTransfer = allowsGameBoyAdvanceCartridgeEra || (entity.formatGeneration >= 8 && !entity.isVirtualConsoleGeneration3);
            return entity;
        }
    }
}
