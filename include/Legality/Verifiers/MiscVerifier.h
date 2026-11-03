/**
 * The parts of PKSE's port of PKHeX's MiscVerifier (PKHeX.Core/Legality/Verifiers/MiscVerifier.cs and
 * Misc/), split by format the way PKHeX splits it into MiscVerifierG3 ... MiscVerifierPA9, plus the
 * Legends verifiers MiscVerifierPA8 and MiscVerifierPA9 run (LegendsArceusVerifier.cs,
 * LegendsZAVerifier.cs). verifyMisc() in Verifiers.h is the only entry point; these are what it
 * dispatches to, and the helpers the parts share.
 */
#ifndef LEGALITY_VERIFIERS_MISCVERIFIER_H
#define LEGALITY_VERIFIERS_MISCVERIFIER_H

#include <cstdint>
#include <optional>

#include "Enums/GameVersion.h"
#include "Legality/AnalysisContext.h"
#include "Legality/Verifiers/HandlerRules.h"

namespace Legality
{
    /// The encounter is an Alpha (PKHeX IAlphaReadOnly.IsAlpha); empty when the encounter is unknown.
    std::optional<bool> isEncounterAlpha(const AnalysisContext &context);

    /// PKHeX LegalityAnalysis.IsStoredSlot.
    bool isStoredSlot(const AnalysisContext &context, StorageSlotType slotType);

    /// PKHeX DateUtil.IsValidDate.
    bool isValidDate(int year, int month, int day);

    /// PKHeX MiscVerifierHelpers.VerifyStatAlignment.
    void verifyStatAlignment(const AnalysisContext &context);

    /// PKHeX MiscVerifierHelpers.IsObedienceLevelValid.
    bool isObedienceLevelValid(const AnalysisContext &context, uint8_t obedienceLevel);

    /// PKHeX IBattleVersion.IsBattleVersionValid.
    bool isBattleVersionValid(const AnalysisContext &context, uint8_t battleVersion);

    /// PKHeX FullnessRules.Verify.
    void verifyFullness(const AnalysisContext &context, uint8_t fullness, uint8_t enjoyment);

    /// PKHeX EggVerifier, which PKHeX's MiscVerifier and MiscG1Verifier both run first.
    void verifyEggs(const AnalysisContext &context);

    void verifyMiscGameBoy(const AnalysisContext &context);
    void verifyMiscGeneration3(const AnalysisContext &context);
    void verifyMiscGeneration4(const AnalysisContext &context);
    void verifyMiscGeneration5(const AnalysisContext &context);
    void verifyMiscGeneration6(const AnalysisContext &context);
    void verifyMiscGeneration7(const AnalysisContext &context);
    void verifyMisc7LGPE(const AnalysisContext &context);
    void verifyMisc8SWSH(const AnalysisContext &context);
    void verifyMisc8BDSP(const AnalysisContext &context);
    void verifyMisc8LA(const AnalysisContext &context);
    void verifyMisc9SV(const AnalysisContext &context);
    void verifyMisc9LZA(const AnalysisContext &context);

    /// PKHeX LegendsArceusVerifier.
    void verifyLegendsArceus(const AnalysisContext &context);

    /// PKHeX LegendsZAVerifier.
    void verifyLegendsZA(const AnalysisContext &context);
}

#endif
