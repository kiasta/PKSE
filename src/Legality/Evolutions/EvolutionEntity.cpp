#include "Legality/Evolutions/EvolutionEntity.h"

#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon8SWSH.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            // PKHeX LocationsHOME: where HOME records a side transfer into Sword/Shield.
            constexpr uint16_t LOCATION_HOME_LEAFGREEN_TO_SHIELD = 59994;
            constexpr uint16_t LOCATION_HOME_FIRERED_TO_SWORD = 59995;
            constexpr uint16_t LOCATION_HOME_VIOLET_TO_SHIELD = 59996;
            constexpr uint16_t LOCATION_HOME_SCARLET_TO_SWORD = 59997;
            constexpr uint16_t LOCATION_HOME_SHINING_PEARL_TO_SHIELD = 59998;
            constexpr uint16_t LOCATION_HOME_BRILLIANT_DIAMOND_TO_SWORD = 59999;
            constexpr uint16_t LOCATION_HOME_LEGENDS_ARCEUS_TO_SWORD = 60000;

            using Enums::GameVersion;

            bool isVersion(const EvolutionEntity &entity, GameVersion firstVersion, GameVersion secondVersion)
            {
                return entity.originVersion == static_cast<uint8_t>(firstVersion) || entity.originVersion == static_cast<uint8_t>(secondVersion);
            }

            bool isSwordShieldRecord(const EvolutionEntity &entity)
            {
                return entity.formatGroup == GameVersion::SWSH;
            }

            /// PKHeX PKM.Generation for a format from Generation 3 on.
            int getOriginGeneration(const EvolutionEntity &entity)
            {
                if (isVersionScarletViolet(entity) || isVersionLegendsZA(entity))
                {
                    return 9;
                }
                const uint8_t version = entity.originVersion;
                const bool isGen8Version = version >= static_cast<uint8_t>(GameVersion::SW) && version <= static_cast<uint8_t>(GameVersion::SP) && version != 46;
                if (isGen8Version || (isVersionPokemonGo(entity) && entity.metLocation == LOCATION_GO_HOME))
                {
                    return 8;
                }
                const bool isGen7Version = version >= static_cast<uint8_t>(GameVersion::SN) && version <= static_cast<uint8_t>(GameVersion::UM);
                const bool isLetsGoVersion = isVersion(entity, GameVersion::GP, GameVersion::GE);
                if (isGen7Version || isLetsGoVersion || (isVersionPokemonGo(entity) && entity.metLocation == LOCATION_GO_LETS_GO))
                {
                    return 7;
                }
                if (version >= static_cast<uint8_t>(GameVersion::X) && version <= static_cast<uint8_t>(GameVersion::OR))
                {
                    return 6;
                }
                if (version >= static_cast<uint8_t>(GameVersion::W) && version <= static_cast<uint8_t>(GameVersion::B2))
                {
                    return 5;
                }
                if (version == static_cast<uint8_t>(GameVersion::HG) || version == static_cast<uint8_t>(GameVersion::SS) || (version >= static_cast<uint8_t>(GameVersion::D) && version <= static_cast<uint8_t>(GameVersion::Pt)))
                {
                    return 4;
                }
                constexpr uint8_t versionColosseumXD = 15;
                constexpr uint8_t versionFireRedSwitch = 56;
                constexpr uint8_t versionLeafGreenSwitch = 57;
                if ((version >= static_cast<uint8_t>(GameVersion::SA) && version <= static_cast<uint8_t>(GameVersion::LG)) || version == versionColosseumXD || version == versionFireRedSwitch || version == versionLeafGreenSwitch)
                {
                    return 3;
                }
                if (version >= static_cast<uint8_t>(GameVersion::RD) && version <= static_cast<uint8_t>(GameVersion::YW))
                {
                    return 1;
                }
                if (version >= static_cast<uint8_t>(GameVersion::GD) && version <= static_cast<uint8_t>(GameVersion::C))
                {
                    return 2;
                }
                return 0;
            }

            /// PKHeX PKM.IsUntraded: only the formats with a handling trainer override PKHeX's false.
            bool isUntraded(const ::Pokemon::Pokemon &pokemon, const EvolutionEntity &entity)
            {
                switch (entity.formatGroup)
                {
                case GameVersion::XY:
                case GameVersion::ORAS:
                case GameVersion::SM:
                case GameVersion::USUM:
                case GameVersion::SWSH:
                case GameVersion::BDSP:
                case GameVersion::PLA:
                    return pokemon.htName().empty() && entity.formatGeneration == entity.originGeneration;
                case GameVersion::SV:
                case GameVersion::ZA:
                {
                    const bool isUnhatchedEgg = entity.originVersion == 0 && entity.isEgg;
                    return pokemon.htName().empty() && (isUnhatchedEgg || entity.formatGeneration == entity.originGeneration);
                }
                default:
                    return false;
                }
            }

            bool hasHomeTrackerFormat(GameVersion formatGroup)
            {
                return formatGroup == GameVersion::SWSH || formatGroup == GameVersion::PLA || formatGroup == GameVersion::BDSP || formatGroup == GameVersion::SV || formatGroup == GameVersion::ZA;
            }
        }

        uint8_t getEntityCurrentLevel(const ::Pokemon::Pokemon &pokemon)
        {
            const ::Pokemon::PersonalRecord &record = ::Pokemon::getPersonalRecord(pokemon.getGameGroup(), pokemon.speciesID(), pokemon.form());
            const bool hasRecord = &record != &::Pokemon::PERSONAL_RECORD_EMPTY;
            const uint8_t growthRate = hasRecord ? record.growthRate : ::Pokemon::getGrowthRate(pokemon.speciesID());
            return ::Pokemon::getLevelFromExp(pokemon.exp(), growthRate);
        }

        EvolutionEntity getEvolutionEntity(const ::Pokemon::Pokemon &pokemon)
        {
            EvolutionEntity entity;
            entity.speciesId = pokemon.speciesID();
            entity.formId = pokemon.form();
            entity.metLevel = pokemon.metLevel();
            entity.metLocation = pokemon.metLocation();
            entity.eggLocation = pokemon.eggLocation();
            entity.isEgg = pokemon.isEgg();
            entity.formatGroup = pokemon.getGameGroup();
            entity.formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(entity.formatGroup));
            entity.currentLevel = getEntityCurrentLevel(pokemon);
            entity.originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            entity.originGeneration = entity.formatGeneration <= 2 ? entity.formatGeneration : getOriginGeneration(entity);
            entity.gender = pokemon.gender();
            entity.natureId = pokemon.nature();
            entity.encryptionConstant = pokemon.encryptionConstant();
            entity.isUntraded = isUntraded(pokemon, entity);
            entity.hasContestStats = pokemon.hasContestStats();
            constexpr int beautyStatIndex = 1;
            entity.contestBeauty = entity.hasContestStats ? pokemon.contestStat(beautyStatIndex) : 0;
            if (entity.formatGroup == GameVersion::GSC)
            {
                entity.hasCaughtDataField = true;
                entity.caughtData = static_cast<const ::Pokemon::Pokemon2GSC &>(pokemon).caughtData();
            }
            entity.hasHomeTrackerField = hasHomeTrackerFormat(entity.formatGroup);
            entity.hasHomeTracker = entity.hasHomeTrackerField && pokemon.homeTracker() != 0;
            if (isSwordShieldRecord(entity))
            {
                entity.canGigantamax = static_cast<const ::Pokemon::Pokemon8SWSH &>(pokemon).canGigantamax();
            }
            entity.abilityId = pokemon.ability();
            entity.abilityNumber = pokemon.abilityNumber();
            return entity;
        }

        Enums::GameVersion getEntityContextGroup(const EvolutionEntity &entity) noexcept
        {
            return getEvolutionContextGroup(entity.formatGroup);
        }

        // PKHeX GameVersion.GetContextFromSaved
        Enums::GameVersion getVersionContextGroup(const EvolutionEntity &entity) noexcept
        {
            constexpr uint8_t versionChampions = 53;
            if (entity.originVersion == versionChampions)
            {
                return GameVersion::ZA;
            }
            if (isVersionPokemonGo(entity) || entity.originVersion == 0)
            {
                return GameVersion::Invalid;
            }
            constexpr uint8_t versionColosseumXD = 15;
            constexpr uint8_t versionFireRedSwitch = 56;
            constexpr uint8_t versionLeafGreenSwitch = 57;
            if (entity.originVersion == versionColosseumXD || entity.originVersion == versionFireRedSwitch || entity.originVersion == versionLeafGreenSwitch)
            {
                return GameVersion::RSE;
            }
            return getEvolutionContextGroup(Enums::getGameGroup(static_cast<GameVersion>(entity.originVersion)));
        }

        bool isVersionSwordShield(const EvolutionEntity &entity) noexcept
        {
            return isVersion(entity, GameVersion::SW, GameVersion::SH);
        }

        bool isVersionBrilliantDiamondShiningPearl(const EvolutionEntity &entity) noexcept
        {
            if (isSwordShieldRecord(entity))
            {
                return entity.metLocation == LOCATION_HOME_BRILLIANT_DIAMOND_TO_SWORD || entity.metLocation == LOCATION_HOME_SHINING_PEARL_TO_SHIELD;
            }
            return isVersion(entity, GameVersion::BD, GameVersion::SP);
        }

        bool isVersionLegendsArceus(const EvolutionEntity &entity) noexcept
        {
            if (isSwordShieldRecord(entity))
            {
                return entity.metLocation == LOCATION_HOME_LEGENDS_ARCEUS_TO_SWORD;
            }
            return entity.originVersion == static_cast<uint8_t>(GameVersion::PLA);
        }

        bool isVersionScarletViolet(const EvolutionEntity &entity) noexcept
        {
            if (isSwordShieldRecord(entity))
            {
                return entity.metLocation == LOCATION_HOME_SCARLET_TO_SWORD || entity.metLocation == LOCATION_HOME_VIOLET_TO_SHIELD;
            }
            return isVersion(entity, GameVersion::SL, GameVersion::VL);
        }

        bool isVersionLegendsZA(const EvolutionEntity &entity) noexcept
        {
            return entity.originVersion == static_cast<uint8_t>(GameVersion::ZA);
        }

        bool isVersionSunMoon(const EvolutionEntity &entity) noexcept
        {
            return isVersion(entity, GameVersion::SN, GameVersion::MN);
        }

        bool isVersionPokemonGo(const EvolutionEntity &entity) noexcept
        {
            return entity.originVersion == VERSION_POKEMON_GO;
        }

        bool isVersionVirtualConsoleGen3(const EvolutionEntity &entity) noexcept
        {
            if (isSwordShieldRecord(entity))
            {
                return entity.metLocation == LOCATION_HOME_FIRERED_TO_SWORD || entity.metLocation == LOCATION_HOME_LEAFGREEN_TO_SHIELD;
            }
            constexpr uint8_t versionFireRedSwitch = 56;
            constexpr uint8_t versionLeafGreenSwitch = 57;
            return entity.originVersion == versionFireRedSwitch || entity.originVersion == versionLeafGreenSwitch;
        }
    }
}
