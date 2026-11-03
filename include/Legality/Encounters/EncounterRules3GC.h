/**
 * The rules every Colosseum and XD template shares: the stored version id, and the met level and
 * location a Gen 3 record keeps and a transferred record loses. Ported from the IsMatchLevel /
 * IsMatchLocation members PKHeX repeats in each template under
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Colo and Gen3/XD.
 *
 * PKSE has no GameVersion for PKHeX's COLO and XD lumps. Both games store version 15 (PKHeX CXD), so
 * the templates return GAME_VERSION_COLOSSEUM_XD and keep PKHeX's "COLO"/"XD" only in their names.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES3GC_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES3GC_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        class Encounterable;

        /// PKHeX GameVersion.CXD: the version id Colosseum and XD both store.
        constexpr Enums::GameVersion GAME_VERSION_COLOSSEUM_XD = static_cast<Enums::GameVersion>(15);

        /// PKHeX Encounters3Colo.TrainerNameDukingSpanish4: Pal Park reads the Spanish "GERMÁN" glyphs as
        /// kana, so a Spanish Duking trade moved past Gen 3 carries this name.
        constexpr const char16_t *TRAINER_NAME_DUKING_SPANISH4 = u"GERM\u3044N";

        /// PKHeX IsMatchLevel: the met level, which a transfer out of Gen 3 loses.
        bool isMatchLevelGameCube(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution, uint8_t level);

        /// PKHeX IsMatchLocation: the met location, which a transfer out of Gen 3 loses (the transfer
        /// location is verified later).
        bool isMatchLocationGameCube(const Pokemon::Pokemon &pokemon, uint16_t location);

        /// PKHeX's common IsMatchExact steps: no egg location, then the met location and level, then
        /// the form.
        bool isMatchExactGameCube(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution, uint16_t speciesId, uint8_t level, uint16_t location);

        /// PKHeX `enc is IShadow3`: a Colosseum or XD Shadow Pokemon.
        bool isShadowEncounter(const Encounterable &encounter) noexcept;
    }
}

#endif
