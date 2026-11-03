/**
 * The facts about a Pokemon that PKHeX's evolution engine reads from its PKM: read once from the
 * entity, so the engine never touches the save format and a caller can describe a Pokemon that does
 * not exist yet (an encounter template, with EvolutionOrigin's skip-checks option).
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONENTITY_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONENTITY_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Evolutions
    {
        struct EvolutionEntity
        {
            uint16_t speciesId = 0;
            uint8_t formId = 0;
            /// PKHeX PKM.CurrentLevel: the level the EXP gives.
            uint8_t currentLevel = 1;
            uint8_t metLevel = 0;
            uint16_t metLocation = 0;
            uint16_t eggLocation = 0;
            bool isEgg = false;
            /// The save-format group the Pokemon is stored as (Pokemon::getGameGroup()).
            Enums::GameVersion formatGroup = Enums::GameVersion::Invalid;
            /// PKHeX PKM.Format.
            int formatGeneration = 0;
            /// PKHeX PKM.Generation: the generation of the version byte, 0 when it names no game.
            int originGeneration = 0;
            /// PKHeX PKM.Version as stored; 0 when the format stores none.
            uint8_t originVersion = 0;
            /// 0 male, 1 female, 2 genderless.
            uint8_t gender = 0;
            uint8_t natureId = 0;
            uint32_t encryptionConstant = 0;
            /// PKHeX PKM.IsUntraded.
            bool isUntraded = false;
            /// PKHeX IContestStatsReadOnly: the format stores contest stats.
            bool hasContestStats = false;
            uint8_t contestBeauty = 0;
            /// PKHeX ICaughtData2 (a Gen 2 record) and its CaughtData word.
            bool hasCaughtDataField = false;
            uint16_t caughtData = 0;
            /// PKHeX IHomeTrack and IHomeTrack.HasTracker.
            bool hasHomeTrackerField = false;
            bool hasHomeTracker = false;
            /// PKHeX PK8.CanGigantamax; false in every other format.
            bool canGigantamax = false;
            uint16_t abilityId = 0;
            uint8_t abilityNumber = 0;
        };

        /// PKHeX PKM.CurrentLevel: the level the experience reaches on the growth curve of the
        /// format's own personal entry for the form, which differs for a few forms (LGPE's partner
        /// Pikachu and Eevee).
        uint8_t getEntityCurrentLevel(const Pokemon::Pokemon &pokemon);

        /// Reads the facts from a stored Pokemon.
        EvolutionEntity getEvolutionEntity(const Pokemon::Pokemon &pokemon);

        /// PKHeX PKM.Context as a context group (Legality::Evolutions::EVOLUTION_CONTEXT_GROUPS).
        Enums::GameVersion getEntityContextGroup(const EvolutionEntity &entity) noexcept;

        /// PKHeX GameVersion.Context of PKM.Version as a context group; Invalid when the version
        /// names no game (a Game Boy record, which stores none).
        Enums::GameVersion getVersionContextGroup(const EvolutionEntity &entity) noexcept;

        /// PKHeX PKM.SWSH / BDSP / LA / SV / ZA / SM / GO / VC3. A Sword/Shield record answers BDSP,
        /// LA, SV and VC3 from the HOME side-transfer met location instead of the version (PK8).
        bool isVersionSwordShield(const EvolutionEntity &entity) noexcept;
        bool isVersionBrilliantDiamondShiningPearl(const EvolutionEntity &entity) noexcept;
        bool isVersionLegendsArceus(const EvolutionEntity &entity) noexcept;
        bool isVersionScarletViolet(const EvolutionEntity &entity) noexcept;
        bool isVersionLegendsZA(const EvolutionEntity &entity) noexcept;
        bool isVersionSunMoon(const EvolutionEntity &entity) noexcept;
        bool isVersionPokemonGo(const EvolutionEntity &entity) noexcept;
        bool isVersionVirtualConsoleGen3(const EvolutionEntity &entity) noexcept;
    }
}

#endif
