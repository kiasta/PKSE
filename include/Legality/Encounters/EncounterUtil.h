/**
 * Small PKHeX helpers the encounter templates of several generations share: the sentinel form values
 * of PKHeX PKHeX.Core/Legality/Encounters/Data/EncounterUtil.cs, the egg form rules of
 * PKHeX.Core/Legality/Tables/FormInfo.cs and PKHeX.Core/Legality/RNG/Methods/Misc/WurmpleUtil.cs.
 *
 * The helpers are static members of structs named after PKHeX's classes, so callers qualify them:
 * several generators keep file-local copies under the same names that an unqualified call would
 * clash with.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERUTIL_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERUTIL_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncounterUtil.FormDynamic: a slot form at or above it is random, not a form id.
        constexpr uint8_t FORM_DYNAMIC = 30;
        /// PKHeX EncounterUtil.FormVivillon: the pattern follows the console's region.
        constexpr uint8_t FORM_VIVILLON = 30;
        /// PKHeX EncounterUtil.FormRandom: any of the species' forms.
        constexpr uint8_t FORM_RANDOM = 31;

        /// PKHeX FormInfo, the members the egg and static templates read.
        struct FormInfo
        {
            /// PKHeX FormInfo.IsBattleOnlyForm (the format argument changes nothing).
            static bool isBattleOnlyForm(uint16_t speciesId, uint8_t formId) noexcept;
            /// PKHeX FormInfo.GetOutOfBattleForm.
            static uint8_t getOutOfBattleForm(uint16_t speciesId, uint8_t formId, uint8_t formatGeneration) noexcept;
            /// PKHeX FormInfo.GetTotemBaseForm: the form a Totem reverts to on transfer.
            static uint8_t getTotemBaseForm(uint16_t speciesId, uint8_t formId) noexcept;
            /// The form PKHeX's Gen 6 and Gen 7 CreateEggEncounter gives an egg: battle-only forms,
            /// Rotom, Castform and Furfrou revert to their out-of-battle form.
            static uint8_t getEggForm(uint16_t speciesId, uint8_t formId, uint8_t generation) noexcept;
        };

        /// PKHeX WurmpleUtil.
        struct WurmpleUtil
        {
            /// PKHeX WurmpleUtil.IsWurmpleEvoValid: a Silcoon/Beautifly or Cascoon/Dustox whose
            /// encryption constant picks its own line.
            static bool isWurmpleEvolutionValid(const Pokemon::Pokemon &pokemon) noexcept;
        };
    }
}

#endif
