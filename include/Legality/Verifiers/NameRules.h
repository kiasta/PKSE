/**
 * Facts the nickname, trainer name, trainer id, language and trash-byte verifiers share: where the
 * Pokemon's encounter came from, which names its games allowed, and the per-generation limits PKHeX
 * keeps in Legal.cs. Ported from the PKHeX files named at each declaration.
 */
#ifndef LEGALITY_VERIFIERS_NAMERULES_H
#define LEGALITY_VERIFIERS_NAMERULES_H

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/AnalysisContext.h"

namespace Legality
{
    /// The generation the Pokemon was encountered in (PKHeX LegalInfo.Generation / PKM.Generation).
    int getOriginGeneration(const AnalysisContext &context);

    /// The format group standing for the encounter's EntityContext (PKHeX IEncounterTemplate.Context).
    Enums::GameVersion getOriginContextGroup(const AnalysisContext &context);

    /// A Game Boy Pokemon brought forward from the 3DS Virtual Console (PKHeX PKM.VC).
    bool isVirtualConsole(const Pokemon::Pokemon &pokemon);

    /// PKHeX PKM.VC3: a Nintendo Switch FireRed/LeafGreen origin, which a Sword/Shield record shows by
    /// its HOME met location instead.
    bool isVirtualConsoleGenerationThree(const AnalysisContext &context);

    /// PKHeX ParseSettings.AllowGBACrossTransferRSE / AllowGBACrossTransferXD (the two are the same
    /// test): trades with Ruby/Sapphire/Emerald and the GameCube games are possible for this Pokemon.
    bool allowsGameBoyAdvanceCrossTransfer(const AnalysisContext &context);

    /// PKHeX TrainerNameVerifier.IsPlayerOriginalTrainer: false when the encounter fixes the OT
    /// (an in-game trade, or a Mystery Gift other than an egg), whose checks are the encounter's.
    bool isPlayerOriginalTrainer(const AnalysisContext &context);

    /// PKHeX Legal.GetMaxLengthNickname / GetMaxLengthOT.
    int getMaxNicknameLength(int generation, uint8_t language);
    int getMaxTrainerNameLength(int generation, uint8_t language);

    /// PKHeX PKM.IsNicknamed as PKHeX reads it: Gen 1-3 have no flag, so the name is compared with the
    /// species name instead (Gen 1-2 only when the record's language is known).
    bool isNicknamedAsStored(const AnalysisContext &context);

    /// PKHeX PKM.WasTradedEgg (egg location) and PKM.IsTradedEgg (met location).
    bool wasTradedEgg(const AnalysisContext &context);
    bool isTradedEgg(const AnalysisContext &context);

    /// PKHeX LanguageVerifier.IsValidLanguageID with the Gen 5 in-game trade exception unknown.
    uint8_t getMaxLanguageId(const AnalysisContext &context);
    bool isValidLanguageId(const AnalysisContext &context);

    /// The OT is one of the names the Gen 4 GTS gave a Korean Pokemon traded to an international
    /// game, which LanguageVerifier tags as PKHeX's GTSTrainerSanitized result.
    bool isGlobalTradeSanitizedTrainer(const AnalysisContext &context);

    /// PKHeX TrainerNameVerifier.ContainsTooManyNumbers / GetMaxNumberCount.
    bool containsTooManyNumbers(const std::u16string &text, int originalGeneration);
    int getMaxNumberCount(int originalGeneration);
}

#endif
