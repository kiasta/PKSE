/**
 * Rules the Legends: Arceus encounter templates share: the wild slot types, the Alpha Mark rule, the
 * HOME location remap a Legends: Arceus Pokemon moved to Sword/Shield gets, and the form-argument
 * deferral. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen8a/EncounterArea8a.cs
 * (SlotType8a), PKHeX.Core/Legality/Verifiers/Ribbons/MarkRules.cs (IsMarkValidAlpha) and
 * PKHeX.Core/Legality/Encounters/Templates/Gen8a/EncounterSlot8a.cs (IsFormArgMismatch).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES8A_H

#include <cstdint>
#include <span>

#include "Legality/LearnSource/Learnset.h"
#include "Pokemon/MoveRecordTable.h"

namespace Pokemon
{
    class Pokemon;
    class Pokemon8LA;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX SlotType8a.
        enum class SlotType8a : uint8_t
        {
            Standard = 0,
            Distortion = 1,
            Landmark = 2,
            MassOutbreakRegular = 3,
            MassOutbreakMassive = 4,
        };

        /// PKHeX's SlotType8a.ToString(), as its slot LongName prints it.
        const char *getSlotTypeName8a(SlotType8a type) noexcept;

        /// PKHeX PersonalInfo.RatioMagicMale / RatioMagicFemale: the gender ratios that force a gender.
        constexpr uint8_t GENDER_RATIO_MAGIC_MALE = 0;
        constexpr uint8_t GENDER_RATIO_MAGIC_FEMALE = 254;

        /// The species' Legends: Arceus gender ratio, or a forced one for a fixed gender (0 male, 1
        /// female), as PKHeX's GetParams switches.
        uint8_t getGenderRatio8a(uint8_t fixedGender, uint16_t speciesId, uint8_t formId) noexcept;

        /// PKHeX MarkRules.IsMarkValidAlpha: a format with the Gen 9 marks (PA8, PK9, PA9) carries the
        /// Alpha Mark exactly when the encounter was an Alpha that HOME has seen.
        bool isMarkValidAlpha(const Pokemon::Pokemon &pokemon, bool wasAlpha);
        /// PKHeX `pk is IRibbonSetMark9 { RibbonMarkAlpha: false }`.
        bool isMissingAlphaMark(const Pokemon::Pokemon &pokemon);

        /// PKHeX `pk is IAlphaReadOnly a && a.IsAlpha != IsAlpha`.
        bool isAlphaMismatch(const Pokemon::Pokemon &pokemon, bool isAlpha) noexcept;

        /// PKHeX EncounterSlot8a.IsFormArgMismatch: Wyrdeer, Overqwil and Basculegion evolve only with
        /// a form argument, so one met as its pre-evolution with none left is a deferred match.
        bool isFormArgumentMismatch8a(const Pokemon::Pokemon &pokemon, uint16_t templateSpeciesId) noexcept;

        /// PKHeX `LocationsHOME.GetRemapState(Gen8a, pk.Context)` against SWLA: whether the record's
        /// met location is one of Legends: Arceus's own rather than HOME's.
        bool hasOriginalMetLocation8a(const Pokemon::Pokemon &pokemon);

        /// The move-shop index of `moveId` (PKHeX `Permit.RecordPermitIndexes.IndexOf`), or -1.
        int getMoveShopIndex8a(uint16_t moveId) noexcept;

        /// PKHeX MoveShop8MasteryExtensions.IsValidPurchasedEncounter: no permitted purchase of a move
        /// the encounter's learnset already taught by `level`, and no purchased Alpha move unless allowed.
        bool isValidPurchasedEncounter8a(const Pokemon::Pokemon8LA &pokemon, const LearnSource::Learnset &learnset, uint8_t level, uint16_t alphaMove, bool allowPurchasedAlpha) noexcept;

        /// PKHeX MoveShop8MasteryExtensions.IsValidMasteredEncounter: the purchased and mastered flags
        /// of the encounter's initial moves match what the game sets when it generates the Pokemon.
        bool isValidMasteredEncounter8a(const Pokemon::Pokemon8LA &pokemon, std::span<const uint16_t, 4> encounterMoves, const LearnSource::Learnset &learnset, std::span<const Pokemon::LevelUpMove> masteryLevels, uint8_t metLevel, uint16_t alphaMove, bool allowPurchasedAlpha) noexcept;
    }
}

#endif
