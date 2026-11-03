/**
 * PKSE's ports of PKHeX's legality verifiers (PKHeX.Core/Legality/Verifiers/), one function per
 * PKHeX verifier class, each in its own file under src/Legality/Verifiers/. analyze() calls them in
 * PKHeX's order (LegalityAnalysis.UpdateChecks) with PKHeX's format gates.
 *
 * One function covers every entry point PKHeX gives its class: a Game Boy format takes the class's
 * own Game Boy path (LevelVerifier.VerifyG1, TrainerNameVerifier.VerifyOTGB,
 * MiscVerifier.VerifyMiscG12) inside the same function, and verifyTransfer runs whichever of
 * TransferVerifier's per-generation checks PKHeX's ParsePK* methods call for this Pokemon.
 */
#ifndef LEGALITY_VERIFIERS_VERIFIERS_H
#define LEGALITY_VERIFIERS_VERIFIERS_H

#include "Legality/AnalysisContext.h"

namespace Legality
{
    /// PID, encryption constant and shiny rules (PKHeX PIDVerifier.cs).
    void verifyPID(const AnalysisContext &context);

    /// Language id against the origin and format (PKHeX LanguageVerifier.cs).
    void verifyLanguage(const AnalysisContext &context);

    /// Nickname, nickname flag and egg names (PKHeX NicknameVerifier.cs).
    void verifyNickname(const AnalysisContext &context);

    /// Original trainer name (PKHeX TrainerNameVerifier.cs).
    void verifyTrainerName(const AnalysisContext &context);

    /// Trainer id and secret id (PKHeX TrainerIDVerifier.cs).
    void verifyTrainerId(const AnalysisContext &context);

    /// Individual values (PKHeX IndividualValueVerifier.cs).
    void verifyIndividualValues(const AnalysisContext &context);

    /// Effort values (PKHeX EffortValueVerifier.cs).
    void verifyEffortValues(const AnalysisContext &context);

    /// Level, EXP and met level (PKHeX LevelVerifier.cs).
    void verifyLevel(const AnalysisContext &context);

    /// Ribbons (PKHeX Ribbons/RibbonVerifier.cs).
    void verifyRibbons(const AnalysisContext &context);

    /// Ability and ability number (PKHeX Ability/AbilityVerifier.cs).
    void verifyAbility(const AnalysisContext &context);

    /// Ball (PKHeX Ball/BallVerifier.cs).
    void verifyBall(const AnalysisContext &context);

    /// Form (PKHeX FormVerifier.cs).
    void verifyForm(const AnalysisContext &context);

    /// Per-format miscellaneous fields (PKHeX MiscVerifier.cs).
    void verifyMisc(const AnalysisContext &context);

    /// Move PP and PP Ups (PKHeX MovePPVerifier.cs).
    void verifyMovePP(const AnalysisContext &context);

    /// Gender (PKHeX GenderVerifier.cs).
    void verifyGender(const AnalysisContext &context);

    /// Held item (PKHeX ItemVerifier.cs).
    void verifyHeldItem(const AnalysisContext &context);

    /// Contest stats and sheen (PKHeX ContestStatVerifier.cs).
    void verifyContestStats(const AnalysisContext &context);

    /// Box markings and the favorite flag (PKHeX MarkingVerifier.cs).
    void verifyMarkings(const AnalysisContext &context);

    /// Gen 4 ground tile (PKHeX GroundTileVerifier.cs).
    void verifyGroundTile(const AnalysisContext &context);

    /// The storage slot the Pokemon is in (PKHeX SlotTypeVerifier.cs).
    void verifySlotType(const AnalysisContext &context);

    /// Handling trainer history, friendship and affection (PKHeX HistoryVerifier.cs).
    void verifyHistory(const AnalysisContext &context);

    /// 3DS console region (PKHeX ConsoleRegionVerifier.cs).
    void verifyConsoleRegion(const AnalysisContext &context);

    /// Trainer memories (PKHeX MemoryVerifier.cs).
    void verifyMemories(const AnalysisContext &context);

    /// Super Training medals (PKHeX MedalVerifier.cs).
    void verifyMedals(const AnalysisContext &context);

    /// Hyper Training (PKHeX HyperTrainingVerifier.cs).
    void verifyHyperTraining(const AnalysisContext &context);

    /// String terminators and trash bytes (PKHeX TrashByteVerifier.cs).
    void verifyTrashBytes(const AnalysisContext &context);

    /// Marks and the affixed ribbon (PKHeX MarkVerifier.cs).
    void verifyMarks(const AnalysisContext &context);

    /// Let's Go awakening values (PKHeX AwakenedValueVerifier.cs).
    void verifyAwakeningValues(const AnalysisContext &context);

    /// Transfer between generations and through HOME (PKHeX TransferVerifier.cs).
    void verifyTransfer(const AnalysisContext &context);

    /// Colosseum and XD (PKHeX CXDVerifier.cs).
    void verifyColosseumXD(const AnalysisContext &context);

    /// The per-move verdicts (PKHeX LegalInfo.Moves / Relearn, from LearnVerifier and
    /// LearnVerifierRelearn): one finding per invalid current or relearn move slot.
    void verifyMoves(const AnalysisContext &context);
}

#endif
