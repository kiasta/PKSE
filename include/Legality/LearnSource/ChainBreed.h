/**
 * Whether an egg's inherited moves can come from one chain of fathers. Ported from PKHeX
 * PKHeX.Core/Legality/Moves/Breeding/Chain (ChainBreedLegality, ChainBreedTrace, ChainBreedStep,
 * ChainBreedEraFlags and ChainBreedUtil.InferFlagsFrom). EggVerifier.VerifyEggBreedingChain asks it.
 *
 * Before Gen 6 only the father passes egg moves, so every inherited move must reach the egg through
 * a single line of fathers, each of which learned the moves itself or was bred with them; a father
 * can also have been transferred from the generation before with its moveset intact. From Gen 6 on
 * mothers pass egg moves too, and only female-only and two-species (Nidoran, Volbeat/Illumise)
 * lines keep a restriction (PKHeX IsValidRelaxed).
 *
 * The trace PKHeX keeps for its summary text is reduced to what the proof reads: the cycle check
 * and the "a Gen 2 move is already in the chain" test. The summary itself is not ported.
 */
#ifndef LEGALITY_LEARNSOURCE_CHAINBREED_H
#define LEGALITY_LEARNSOURCE_CHAINBREED_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX ChainBreedLegality.IsValid(species, form, version, flags, moves): the moves (up to the
        /// first 0, at most four) can be produced by a valid breeding chain in `version`, the egg's own
        /// game. `isSwitchGameBoyAdvanceEra` is PKHeX ChainBreedEraFlags.NintendoSwitchGBA: the chain
        /// stays within the Nintendo Switch FireRed/LeafGreen, so Emerald's and XD's tutors and XD's
        /// shadow Pokemon are out of reach.
        bool isChainBreedValid(uint16_t speciesId, uint8_t formId, Enums::GameVersion version, bool isSwitchGameBoyAdvanceEra, std::span<const uint16_t> moves);
    }
}

#endif
