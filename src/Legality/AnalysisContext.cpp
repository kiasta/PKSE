#include "Legality/AnalysisContext.h"

#include <utility>

#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "Utils/StringHelpers.h"

namespace Legality
{
    ActiveTrainer makeActiveTrainer(const Trainer::Trainer &trainer)
    {
        ActiveTrainer activeTrainer;
        activeTrainer.trainerName = Utils::utf8ToUtf16(trainer.trainerName);
        activeTrainer.TID16 = trainer.TID16;
        activeTrainer.SID16 = trainer.SID16;
        activeTrainer.trainerGender = trainer.trainerGender;
        activeTrainer.language = trainer.hasLanguage() ? trainer.language() : 0;
        activeTrainer.gameVersion = trainer.getGameVersion();
        activeTrainer.isVirtualConsole = trainer.isVirtualConsole();
        return activeTrainer;
    }

    int getActiveTrainerGeneration(const ActiveTrainer &activeTrainer)
    {
        // A trainer's version is a single game for most saves and a group for a few.
        const Enums::GameVersion versionGroup = Enums::getGameGroup(activeTrainer.gameVersion);
        const Enums::GameVersion formatGroup = versionGroup == Enums::GameVersion::Invalid ? activeTrainer.gameVersion : versionGroup;
        if (formatGroup == Enums::GameVersion::Invalid)
        {
            return 0;
        }
        return Enums::getVersionGeneration(Enums::getGroupRepVersion(formatGroup));
    }

    bool allowsGameBoyCartridgeEra(const AnalysisOptions &options)
    {
        if (options.activeTrainer == nullptr || options.activeTrainer->isVirtualConsole)
        {
            return false;
        }
        const int activeTrainerGeneration = getActiveTrainerGeneration(*options.activeTrainer);
        return activeTrainerGeneration == 1 || activeTrainerGeneration == 2;
    }

    bool allowsGameBoyAdvanceCartridgeEra(const AnalysisOptions &options)
    {
        if (options.activeTrainer == nullptr)
        {
            return true;
        }
        return !(options.activeTrainer->isVirtualConsole && getActiveTrainerGeneration(*options.activeTrainer) == 3);
    }

    bool allowsSwitchGameBoyAdvanceEra(const AnalysisOptions &options)
    {
        if (options.activeTrainer == nullptr)
        {
            return false;
        }
        const int activeTrainerGeneration = getActiveTrainerGeneration(*options.activeTrainer);
        return (options.activeTrainer->isVirtualConsole && activeTrainerGeneration == 3) || activeTrainerGeneration >= 8;
    }

    const Evolutions::EvolutionHistory &AnalysisContext::evolutionHistory() const
    {
        if (legalityInfo != nullptr)
        {
            return legalityInfo->evolutionChainsAllGens();
        }
        if (evolutionHistoryCache.has_value())
        {
            return *evolutionHistoryCache;
        }
        const Evolutions::EvolutionEntity entity = Evolutions::getEvolutionEntity(pokemon);
        Evolutions::EvolutionEncounter evolutionEncounter;
        if (pokemon.isEgg() || encounter.type == EncounterType::Unmatched)
        {
            evolutionEncounter = Evolutions::getInvalidEvolutionEncounter(entity);
        }
        else
        {
            // Without the encounter engine (a hand-built context) the encounter is unknown, not invalid:
            // species 0 lets every pre-evolution count as the one encountered, and level 1 rules no
            // evolution out.
            const Enums::GameVersion originGroup = encounter.isValid() ? encounter.originGroup : getOriginContextGroup(*this);
            evolutionEncounter.speciesId = 0;
            evolutionEncounter.contextGroup = Evolutions::getEvolutionContextGroup(originGroup);
            evolutionEncounter.generation = static_cast<uint8_t>(getOriginGeneration(*this));
            evolutionEncounter.levelMin = 1;
        }
        evolutionHistoryCache = Evolutions::getEvolutionChainsAllGens(entity, evolutionEncounter);
        return *evolutionHistoryCache;
    }

    void AnalysisContext::add(Severity severity, CheckIdentifier identifier, ResultCode code, const std::string &firstArgument, const std::string &secondArgument) const
    {
        report.issues.push_back(Issue{severity, identifier, code, getResultText(code, firstArgument, secondArgument)});
    }

    void AnalysisContext::addText(Severity severity, CheckIdentifier identifier, ResultCode code, std::string text) const
    {
        report.issues.push_back(Issue{severity, identifier, code, std::move(text)});
    }
}
