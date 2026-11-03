#include "Legality/Encounters/LegalityInfo.h"

#include "Legality/AnalysisContext.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGift3.h"
#include "Legality/Encounters/EncounterInvalid.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        LegalityInfo::LegalityInfo(const Pokemon::Pokemon &pokemon, const AnalysisOptions &options) : pokemon(pokemon), entity(Evolutions::getEvolutionEntity(pokemon))
        {
            allowEraCartGBA = allowsGameBoyAdvanceCartridgeEra(options);
            allowEraSwitchGBA = allowsSwitchGameBoyAdvanceEra(options);
            allowEraCartGB = allowsGameBoyCartridgeEra(options);
            // PKHeX LegalInfo.StoreMetadata: an unhatched Scarlet/Violet egg reports generation 0
            // and searches as generation 9.
            const uint8_t originGeneration = static_cast<uint8_t>(entity.originGeneration);
            generation = originGeneration == 0 && pokemon.isEgg() && pokemon.getGameGroup() == Enums::GameVersion::SV ? 9 : originGeneration;
        }

        void LegalityInfo::setEncounterMatch(EncounterReference encounter)
        {
            // The chains depend only on the template's species and minimum level, so PKHeX keeps
            // them when neither changes.
            const bool hadEncounter = matchedEncounter != nullptr && !EncounterInvalid::isTemplateType(matchedEncounter->templateType());
            if (hadEncounter && encounter && (encounter->levelMin() != matchedEncounter->levelMin() || encounter->speciesId() != matchedEncounter->speciesId()))
            {
                evolutionHistory.reset();
            }
            matchedEncounter = std::move(encounter);
            parse.clear();
        }

        bool LegalityInfo::areAllParseValid() const noexcept
        {
            for (const CheckResult &result : parse)
            {
                if (!result.isValid())
                {
                    return false;
                }
            }
            return true;
        }

        void LegalityInfo::reportParse(const AnalysisContext &context) const
        {
            for (const CheckResult &result : parse)
            {
                if (result.judgement == Judgement::Valid)
                {
                    continue;
                }
                const Severity severity = result.judgement == Judgement::Invalid ? Severity::Invalid : Severity::Warning;
                context.add(severity, result.identifier, result.code, result.firstArgument, result.secondArgument);
            }
        }

        const Evolutions::EvolutionHistory &LegalityInfo::evolutionChainsAllGens() const
        {
            if (!evolutionHistory.has_value())
            {
                Evolutions::EvolutionEncounter evolutionEncounter;
                if (!matchedEncounter || EncounterInvalid::isTemplateType(matchedEncounter->templateType()))
                {
                    evolutionEncounter = Evolutions::getInvalidEvolutionEncounter(entity);
                }
                else
                {
                    evolutionEncounter.speciesId = matchedEncounter->speciesId();
                    evolutionEncounter.contextGroup = matchedEncounter->contextGroup();
                    evolutionEncounter.generation = matchedEncounter->generation();
                    evolutionEncounter.levelMin = matchedEncounter->levelMin();
                }
                evolutionHistory = Evolutions::getEvolutionChainsAllGens(entity, evolutionEncounter);
            }
            return *evolutionHistory;
        }
    
        namespace
        {
            // HOME rewrites the met location of anything it moves into Sword/Shield from a game
            // Sword/Shield cannot name to one of these per-origin locations (PKHeX LocationsHOME
            // SHLG 59994 to SHSP 60000).
            constexpr uint16_t HOME_REMAPPED_LOCATION_FIRST = 59994;
            constexpr uint16_t HOME_REMAPPED_LOCATION_LAST = 60000;
            // Pal Park and Poke Transfer replace the met location of a Pokemon leaving Gen 3 or 4.
            constexpr int LAST_GENERATION_WITH_REPLACED_MET_LOCATION = 4;

            /// The game group whose met-location namespace the Pokemon's met location is in, or
            /// Invalid where no game's is: the origin's group, the format's own group for a format
            /// with no origin byte (and an unhatched Scarlet/Violet egg), and Invalid for an origin
            /// with no save-format group (GO, Colosseum/XD, Virtual Console), an origin newer than
            /// its format, a Pokemon that left Gen 3 or 4, or a location HOME remapped.
            Enums::GameVersion getMetLocationGroup(const Pokemon::Pokemon &pokemon)
            {
                const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
                const bool isUnhatchedScarletVioletEgg = pokemon.isEgg() && originVersion == 0 && pokemon.getGameGroup() == Enums::GameVersion::SV;
                if (!pokemon.hasOriginGame() || isUnhatchedScarletVioletEgg)
                {
                    return pokemon.getGameGroup();
                }
                const int originGeneration = Enums::getVersionGeneration(originVersion);
                const int formatGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup()));
                if (originGeneration == 0 || formatGeneration == 0 || originGeneration > formatGeneration)
                {
                    return Enums::GameVersion::Invalid;
                }
                if (originGeneration < formatGeneration && originGeneration <= LAST_GENERATION_WITH_REPLACED_MET_LOCATION)
                {
                    return Enums::GameVersion::Invalid;
                }
                const uint16_t metLocation = pokemon.metLocation();
                if (metLocation >= HOME_REMAPPED_LOCATION_FIRST && metLocation <= HOME_REMAPPED_LOCATION_LAST)
                {
                    return Enums::GameVersion::Invalid;
                }
                return Enums::getGameGroup(static_cast<Enums::GameVersion>(originVersion));
            }
        }

        EncounterInfo makeEncounterInfo(const LegalityInfo &info, const Pokemon::Pokemon &pokemon)
        {
            EncounterInfo encounter;
            const EncounterReference &match = info.encounterMatch();
            if (!match || EncounterInvalid::isTemplateType(match->templateType()))
            {
                encounter.type = EncounterType::Unmatched;
            }
            else if (info.encounterOriginalGameBoy)
            {
                // A Virtual Console transfer: the verifiers that read EncounterInfo answer it from
                // the Pokemon itself; the engine's templates stay on info.
                encounter.type = EncounterType::Unchecked;
            }
            else
            {
                encounter.type = match->encounterType();
            }
            encounter.originGroup = getMetLocationGroup(pokemon);
            return encounter;
        }
    
        const Encounterable *getEncounterMatch(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr)
            {
                return nullptr;
            }
            return context.legalityInfo->encounterMatch().get();
        }

        const Encounterable *getEncounterOriginal(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr)
            {
                return nullptr;
            }
            return context.legalityInfo->encounterOriginal().get();
        }

        bool isEncounterFateful(const AnalysisContext &context)
        {
            if (context.legalityInfo != nullptr && context.legalityInfo->encounterMatch())
            {
                return context.legalityInfo->encounterMatch()->isFatefulEncounter();
            }
            return false;
        }

        bool isEncounterEgg(const AnalysisContext &context)
        {
            if (context.legalityInfo != nullptr && context.legalityInfo->encounterMatch())
            {
                return context.legalityInfo->encounterMatch()->isEgg();
            }
            return context.encounter.type == EncounterType::Egg;
        }

        bool isFixedTrainerGiftEgg(const AnalysisContext &context, const std::u16string &trainerName)
        {
            if (context.legalityInfo == nullptr || !context.legalityInfo->encounterMatch() || !context.pokemon.isEgg())
            {
                return false;
            }
            const EncounterGift3 *gift = encounterAs<EncounterGift3>(*context.legalityInfo->encounterMatch());
            return gift != nullptr && gift->isEgg() && trainerName == gift->giftProperties().originalTrainerName;
        }
    }
}
