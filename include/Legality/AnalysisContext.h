/**
 * The state one legality analysis shares between its verifiers: PKSE's counterpart of PKHeX's
 * LegalityAnalysis object, which PKHeX passes to every verifier's Verify().
 *
 * A verifier reads the Pokemon and what the encounter search established about it, and appends
 * findings through add(). Verifiers never modify the Pokemon (the legality layer is read-only with
 * respect to Pokemon data).
 */
#ifndef LEGALITY_ANALYSISCONTEXT_H
#define LEGALITY_ANALYSISCONTEXT_H

#include <cstdint>
#include <optional>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Legality.h"
#include "Legality/ResultCode.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Trainer
{
    class Trainer;
}

namespace Legality
{
    namespace Encounters
    {
        struct LegalityInfo;
    }

    /// Where the analyzed Pokemon is stored, which a few checks depend on (PKHeX StorageSlotType).
    /// `Unknown` is PKHeX's "Ignore": a Pokemon file, or a caller that does not know.
    enum class StorageSlotType : uint8_t
    {
        Unknown,
        Box,
        Party,
    };

    /// The trainer whose save is open, which PKHeX's handler and trainer checks compare a Pokemon
    /// against (PKHeX ParseSettings.ActiveTrainer). Absent for a Pokemon file or a bank slot.
    struct ActiveTrainer
    {
        std::u16string trainerName;
        uint16_t TID16 = 0;
        uint16_t SID16 = 0;
        uint8_t trainerGender = 0;
        /// 0 when the save's language is not known, which skips the checks that compare it.
        uint8_t language = 0;
        Enums::GameVersion gameVersion = Enums::GameVersion::Invalid;
        /// The save is a Virtual Console one (Trainer::isVirtualConsole), which sets PKHeX's era rules.
        bool isVirtualConsole = false;
    };

    /// What the encounter search concluded (PKHeX LegalInfo.EncounterMatch). `Unmatched` is PKHeX's
    /// EncounterInvalid: the search ran and nothing explains the Pokemon. `Unchecked` is a Virtual
    /// Console transfer, which a verifier answers from the Pokemon itself, never as a negative.
    enum class EncounterType : uint8_t
    {
        Unchecked,
        Unmatched,
        Wild,
        Static,
        Gift,
        Trade,
        Raid,
        Event,
        Egg,
    };

    struct EncounterInfo
    {
        EncounterType type = EncounterType::Unchecked;
        /// The game group whose met-location namespace the met location is in; Invalid where none
        /// is (see Encounters::makeEncounterInfo).
        Enums::GameVersion originGroup = Enums::GameVersion::Invalid;

        bool isValid() const noexcept { return type != EncounterType::Unchecked && type != EncounterType::Unmatched; }
    };

    struct AnalysisOptions
    {
        StorageSlotType slotType = StorageSlotType::Unknown;
        const ActiveTrainer *activeTrainer = nullptr;
    };

    /// The open save as PKHeX's ParseSettings.InitFromSaveFileData records it. The language is left
    /// unknown for a save whose trainer layer does not read one (Trainer::hasLanguage).
    ActiveTrainer makeActiveTrainer(const Trainer::Trainer &trainer);

    /// Generation (1-9) of the open save's game (PKHeX ITrainerInfo.Generation); 0 when unknown.
    int getActiveTrainerGeneration(const ActiveTrainer &activeTrainer);

    /// PKHeX ParseSettings' era rules, which InitFromSaveFileData sets from the open save and which
    /// keep their defaults when no save is open. AllowEraCartGB: Game Boy cartridge-era data (Stadium 2,
    /// Game Boy events) is allowed, which only a cartridge Gen 1 or Gen 2 save turns on.
    bool allowsGameBoyCartridgeEra(const AnalysisOptions &options);
    /// AllowEraCartGBA: Game Boy Advance cartridge-era data (trades with Ruby/Sapphire/Emerald and the
    /// GameCube) is allowed; false only with a Nintendo Switch FireRed/LeafGreen save open.
    bool allowsGameBoyAdvanceCartridgeEra(const AnalysisOptions &options);
    /// AllowEraSwitchGBA: the Nintendo Switch FireRed/LeafGreen era, for that save or a Gen 8+ one.
    bool allowsSwitchGameBoyAdvanceEra(const AnalysisOptions &options);

    struct AnalysisContext
    {
        const Pokemon::Pokemon &pokemon;
        /// The format the Pokemon is stored in: its own save-format group.
        Enums::GameVersion formatGroup;
        /// Generation of formatGroup (1-9).
        int formatGeneration;
        const AnalysisOptions &options;
        Report &report;
        EncounterInfo encounter;
        /// What the encounter engine (Legality::Encounters) established: the matched template, the
        /// PID/IV correlation and the evolution chains. Set by every analysis; null only for a
        /// context a test builds by hand.
        const Encounters::LegalityInfo *legalityInfo = nullptr;

        /// PKHeX LegalInfo.EvoChainsAllGens: the encounter engine's; a hand-built context builds one
        /// for an unknown encounter, once, and keeps it.
        const Evolutions::EvolutionHistory &evolutionHistory() const;

        /// Append a finding. Empty `text` uses PKHeX's template for `code`, with the arguments.
        void add(Severity severity, CheckIdentifier identifier, ResultCode code, const std::string &firstArgument = "", const std::string &secondArgument = "") const;
        void addText(Severity severity, CheckIdentifier identifier, ResultCode code, std::string text) const;

        mutable std::optional<Evolutions::EvolutionHistory> evolutionHistoryCache;
    };
}

#endif
