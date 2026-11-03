// Port of PKHeX PKHeX.Core/Legality/Verifiers/TransferVerifier.cs: transfer between generations and through HOME.
// The tracker rule is PKHeX's Restrictions/HomeTrackerUtil.cs, and the dispatch is LegalityAnalysis.ParsePK3..ParsePK9 and UpdateVCTransferInfo.
#include "Legality/Verifiers/Verifiers.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Legality/Verifiers/ForeignSpecies.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Pokemon.h"
#include "Legality/RNG/ShinyUtil.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t VERSION_POKEMON_GO = 34;
        constexpr uint8_t VERSION_CRYSTAL = 41;
        constexpr uint8_t VERSION_FIRST_VIRTUAL_CONSOLE_GENERATION_ONE = 35;
        constexpr uint8_t VERSION_LAST_VIRTUAL_CONSOLE_GENERATION_ONE = 38;
        constexpr uint8_t VERSION_FIRST_VIRTUAL_CONSOLE_GENERATION_TWO = 39;
        constexpr uint8_t VERSION_LAST_VIRTUAL_CONSOLE_GENERATION_TWO = 41;
        constexpr uint8_t VERSION_LETS_GO_PIKACHU = 42;
        constexpr uint8_t VERSION_LETS_GO_EEVEE = 43;

        // PKHeX Locations and LocationsHOME
        constexpr uint16_t LOCATION_TRANSFER_GENERATION_ONE = 30013;
        constexpr uint16_t LOCATION_TRANSFER_GENERATION_TWO = 30017;
        constexpr uint16_t LOCATION_PAL_PARK = 0x37;
        constexpr uint16_t LOCATION_POKE_TRANSFER = 30001;
        constexpr uint16_t LOCATION_POKE_TRANSFER_CELEBI_UNUSED = 30010;
        constexpr uint16_t LOCATION_POKE_TRANSFER_CELEBI_USED = 30011;
        constexpr uint16_t LOCATION_POKE_TRANSFER_CROWN_UNUSED = 30012;
        constexpr uint16_t LOCATION_POKE_TRANSFER_CROWN_USED = 30013;
        constexpr uint16_t LOCATION_HOME_VIRTUAL_CONSOLE_GENERATION_THREE = 30007;
        constexpr uint16_t LOCATION_NONE_BRILLIANT_DIAMOND_SHINING_PEARL = 0xFFFF;

        constexpr uint16_t MAX_SPECIES_GENERATION_ONE = 151;
        constexpr uint16_t CELEBI_SPECIES = 251;
        constexpr uint16_t RAIKOU_SPECIES = 243;
        constexpr uint16_t ENTEI_SPECIES = 244;
        constexpr uint16_t SUICUNE_SPECIES = 245;
        constexpr uint16_t UNOWN_SPECIES = 201;
        constexpr uint16_t RATICATE_SPECIES = 20;
        constexpr uint16_t MAROWAK_SPECIES = 105;
        constexpr uint16_t GUMSHOOS_SPECIES = 735;
        constexpr uint16_t VIKAVOLT_SPECIES = 738;
        constexpr uint16_t RIBOMBEE_SPECIES = 743;
        constexpr uint16_t ARAQUANID_SPECIES = 752;
        constexpr uint16_t LURANTIS_SPECIES = 754;
        constexpr uint16_t SALAZZLE_SPECIES = 758;
        constexpr uint16_t MIMIKYU_SPECIES = 778;
        constexpr uint16_t TOGEDEMARU_SPECIES = 777;
        constexpr uint16_t KOMMO_O_SPECIES = 784;

        constexpr uint8_t DIVE_BALL = 7;
        constexpr uint8_t PREMIER_BALL = 12;
        /// PKHeX's gender byte for one female in eight, the ratio a Gen 2 shiny can never be female in.
        constexpr uint8_t GENDER_RATIO_ONE_IN_EIGHT_FEMALE = 31;
        constexpr uint8_t GENDER_FEMALE = 1;
        constexpr uint8_t NATURE_COUNT = 25;
        constexpr uint32_t SHINY_XOR_THRESHOLD = 16;

        /// PKHeX Experience.IsValidNatureMetLevel2: the natures a level-2 EXP total can give, per growth rate.
        bool isValidNatureMetLevelTwo(uint8_t growthRate, uint8_t natureId)
        {
            const uint32_t natureBit = 1u << natureId;
            switch (growthRate)
            {
            case 0:
                return (0x01FFFF03u & natureBit) != 0;
            case 4:
                return (0x001FFFC0u & natureBit) != 0;
            case 5:
                return (0x01FFFCFFu & natureBit) != 0;
            default:
                return true;
            }
        }

        /// PKHeX Locale3DS.IsRegionLockedLanguageValidVC: the Virtual Console was region locked.
        bool isRegionLockedLanguageValidVirtualConsole(uint8_t consoleRegion, uint8_t language)
        {
            const auto isLanguage = [language](Enums::LanguageID expected) { return language == static_cast<uint8_t>(expected); };
            switch (consoleRegion)
            {
            case 0:
            case 6:
                return isLanguage(Enums::LanguageID::Japanese);
            case 1:
                return isLanguage(Enums::LanguageID::English) || isLanguage(Enums::LanguageID::Spanish) || isLanguage(Enums::LanguageID::French);
            case 2:
                return isLanguage(Enums::LanguageID::English) || isLanguage(Enums::LanguageID::Spanish) || isLanguage(Enums::LanguageID::French) || isLanguage(Enums::LanguageID::German) || isLanguage(Enums::LanguageID::Italian);
            case 5:
                return isLanguage(Enums::LanguageID::Korean);
            default:
                return false;
            }
        }

        bool isVirtualConsoleGenerationOne(const Pokemon::Pokemon &pokemon)
        {
            return pokemon.hasOriginGame() && pokemon.originGame() >= VERSION_FIRST_VIRTUAL_CONSOLE_GENERATION_ONE && pokemon.originGame() <= VERSION_LAST_VIRTUAL_CONSOLE_GENERATION_ONE;
        }

        bool isVirtualConsoleGenerationTwo(const Pokemon::Pokemon &pokemon)
        {
            return pokemon.hasOriginGame() && pokemon.originGame() >= VERSION_FIRST_VIRTUAL_CONSOLE_GENERATION_TWO && pokemon.originGame() <= VERSION_LAST_VIRTUAL_CONSOLE_GENERATION_TWO;
        }

        /// PKM.IsShiny for the 3DS and Switch formats, the only ones these checks run in.
        bool isShiny(const Pokemon::Pokemon &pokemon)
        {
            return RNG::ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32()) < SHINY_XOR_THRESHOLD;
        }

        /// PKHeX FormInfo.IsTotemForm.
        bool isTotemForm(uint16_t speciesId, uint8_t formId)
        {
            if (formId == 0)
            {
                return false;
            }
            switch (speciesId)
            {
            case MIMIKYU_SPECIES:
                return formId == 2 || formId == 3;
            case RATICATE_SPECIES:
            case MAROWAK_SPECIES:
                return formId == 2;
            case GUMSHOOS_SPECIES:
            case VIKAVOLT_SPECIES:
            case RIBOMBEE_SPECIES:
            case ARAQUANID_SPECIES:
            case LURANTIS_SPECIES:
            case SALAZZLE_SPECIES:
            case TOGEDEMARU_SPECIES:
            case KOMMO_O_SPECIES:
                return formId == 1;
            default:
                return false;
            }
        }

        /// PKHeX FormInfo.GetTotemBaseForm.
        uint8_t getTotemBaseForm(uint16_t speciesId, uint8_t formId)
        {
            return speciesId == MIMIKYU_SPECIES ? 0 : static_cast<uint8_t>(formId - 1);
        }

        /// PKHeX EncounterStatic7.IsTotemNoTransfer: HOME refuses these totems outright.
        bool isTotemNoTransfer(uint16_t speciesId)
        {
            return speciesId == MAROWAK_SPECIES || speciesId == ARAQUANID_SPECIES || speciesId == TOGEDEMARU_SPECIES || speciesId == RIBOMBEE_SPECIES;
        }

        uint8_t getPresenceBit(Enums::GameVersion formatGroup)
        {
            switch (formatGroup)
            {
            case Enums::GameVersion::BDSP:
                return Pokemon::PERSONAL_GAME_BDSP;
            case Enums::GameVersion::PLA:
                return Pokemon::PERSONAL_GAME_PLA;
            case Enums::GameVersion::SV:
                return Pokemon::PERSONAL_GAME_SV;
            case Enums::GameVersion::ZA:
                return Pokemon::PERSONAL_GAME_ZA;
            default:
                return Pokemon::PERSONAL_GAME_SWSH;
            }
        }

        /// PKHeX IPersonalTable.IsPresentInGame: a form past the species' form count is in no game.
        bool isPresentInGame(uint16_t speciesId, uint8_t formId, uint8_t presenceBit)
        {
            if (speciesId > Pokemon::PERSONAL_MAX_SPECIES)
            {
                return false;
            }
            if (formId != 0 && formId >= Pokemon::getPersonalInfo(speciesId, 0).formCount)
            {
                return false;
            }
            return (Pokemon::getPersonalInfo(speciesId, formId).presence & presenceBit) != 0;
        }

        /// PKHeX EvolutionHistory.HasVisitedPLA for a Pokemon from before Gen 8: HOME only lets it
        /// reach Legends: Arceus once it carries a tracker (EvolutionGroupHOME.CheckAllAdjacent),
        /// and then as any stage of its evolution chain that game has.
        bool hasVisitedLegendsArceus(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGroup == Enums::GameVersion::PLA)
            {
                return true;
            }
            if (!pokemon.hasHomeTracker() || pokemon.homeTracker() == 0)
            {
                return false;
            }
            if (isPresentInGame(pokemon.speciesID(), pokemon.form(), Pokemon::PERSONAL_GAME_PLA))
            {
                return true;
            }
            uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN];
            uint8_t chainForms[Pokemon::EVO_MAX_CHAIN];
            const int preEvolutionCount = Pokemon::getPreEvolutionChain(context.formatGroup, pokemon.speciesID(), pokemon.form(), chainSpecies, chainForms, Pokemon::EVO_MAX_CHAIN);
            for (int chainIndex = 0; chainIndex < preEvolutionCount; ++chainIndex)
            {
                if (isPresentInGame(chainSpecies[chainIndex], chainForms[chainIndex], Pokemon::PERSONAL_GAME_PLA))
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX's template generation (IEncounterTemplate.Generation) after UpdateVCTransferInfo
        /// swapped a Virtual Console encounter for its Gen 7 transfer template.
        int getEncounterGeneration(const AnalysisContext &context)
        {
            const int originGeneration = getOriginGeneration(context);
            if (context.encounter.type != EncounterType::Unmatched && (originGeneration == 1 || originGeneration == 2))
            {
                return 7;
            }
            return originGeneration;
        }

        /// PKHeX HomeTrackerUtil.IsRequired. Its HOME-gift case (a WC8/WB8/WA8/WC9/WA9 card id of
        /// 9000 or more) needs the matched card's id, which this check does not read yet.
        bool isHomeTrackerRequired(const AnalysisContext &context)
        {
            const int encounterGeneration = getEncounterGeneration(context);
            if (encounterGeneration < 8)
            {
                return true;
            }
            // PKHeX's EncounterInvalid takes the Pokemon's own context, so only its generation can require a tracker.
            if (context.encounter.type == EncounterType::Unmatched)
            {
                return false;
            }
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO)
            {
                return true;
            }
            return getOriginContextGroup(context) != context.formatGroup;
        }

        /// PKHeX TransferVerifier.VerifyHOMETracker. HOME writes the tracker and only its server can
        /// validate the value, so all a save editor can see is whether there is one.
        void verifyHomeTracker(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.hasHomeTracker() && pokemon.homeTracker() == 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferTrackerMissing);
            }
        }

        /// PKHeX TransferVerifier.VerifyHOMETransfer: HOME 3.0.0 re-rolls a (0,0) size on the way
        /// into Scarlet/Violet and later; the Gen 8 games could still receive one before that.
        void verifyHomeTransferSize(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGroup != Enums::GameVersion::SV && context.formatGroup != Enums::GameVersion::ZA)
            {
                return;
            }
            if (pokemon.hasOriginGame())
            {
                const uint8_t originVersion = pokemon.originGame();
                if (originVersion == VERSION_LETS_GO_PIKACHU || originVersion == VERSION_LETS_GO_EEVEE || originVersion == VERSION_POKEMON_GO)
                {
                    return;
                }
            }
            if (pokemon.heightScalar() == 0 && pokemon.weightScalar() == 0 && !hasVisitedLegendsArceus(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferBad);
            }
        }

        /// PKHeX TransferVerifier.VerifyTransferLegalityG8.
        void verifyHomeTransfer(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (isHomeTrackerRequired(context))
            {
                verifyHomeTracker(context);
            }

            if (getEncounterGeneration(context) < 8)
            {
                verifyHomeTransferSize(context);
                const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                const bool isGenerationSevenStatic = encounterMatch != nullptr && encounterMatch->templateType() == Encounters::EncounterTemplateType::Static7;
                if (isGenerationSevenStatic && isTotemForm(encounterMatch->speciesId(), encounterMatch->formId()))
                {
                    if (isTotemNoTransfer(encounterMatch->speciesId()) || pokemon.form() != getTotemBaseForm(encounterMatch->speciesId(), encounterMatch->formId()))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferBad);
                        return;
                    }
                }
            }

            // From Gen 8 on, each game holds only a selection of the earlier species and forms.
            if (!isPresentInGame(pokemon.speciesID(), pokemon.form(), getPresenceBit(context.formatGroup)))
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferBad);
            }
        }

        /// PKHeX TransferVerifier.VerifyTransferLegalityG3.
        void verifyGenerationThreeTransfer(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (isVirtualConsoleGenerationThree(context))
            {
                if (pokemon.metLocation() != LOCATION_HOME_VIRTUAL_CONSOLE_GENERATION_THREE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferMetLocation);
                }

                // HOME stamps the transfer date, and FireRed/LeafGreen could first transfer on 2026-10-06.
                const std::chrono::year_month_day metDate{std::chrono::year{2000 + pokemon.metYear()}, std::chrono::month{pokemon.metMonth()}, std::chrono::day{pokemon.metDay()}};
                const std::chrono::year_month_day firstTransferDate{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{6}};
                if (!metDate.ok() || metDate < firstTransferDate)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::DateOutsideDistributionWindow);
                }

                const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                if (encounterMatch != nullptr && isForeignToFireRedLeafGreen(encounterMatch->speciesId()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TradeNotAvailable);
                }

                if (pokemon.ball() == DIVE_BALL || pokemon.ball() == PREMIER_BALL)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::BallUnavailable);
                }
            }
            else if (context.formatGeneration == 4)
            {
                if (pokemon.metLocation() != LOCATION_PAL_PARK)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::EggLocationPalPark);
                }
            }
            else if (pokemon.metLocation() != LOCATION_POKE_TRANSFER)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferEggLocationTransporter);
            }
        }

        /// PKHeX TransferVerifier.VerifyTransferLegalityG4. A Lock Capsule transfer of Celebi or a
        /// crowned beast keeps its own met location; everything else arrives through Poke Transfer.
        void verifyGenerationFourTransfer(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == LOCATION_POKE_TRANSFER)
            {
                return;
            }
            switch (pokemon.speciesID())
            {
            case CELEBI_SPECIES:
                if (metLocation != LOCATION_POKE_TRANSFER_CELEBI_UNUSED && metLocation != LOCATION_POKE_TRANSFER_CELEBI_USED)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferMet);
                }
                break;
            case RAIKOU_SPECIES:
            case ENTEI_SPECIES:
            case SUICUNE_SPECIES:
                if (metLocation != LOCATION_POKE_TRANSFER_CROWN_UNUSED && metLocation != LOCATION_POKE_TRANSFER_CROWN_USED)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferMet);
                }
                break;
            default:
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferEggLocationTransporter);
                break;
            }
        }

        /// PKHeX EncounterGenerator7.GetVCStaticTransferEncounter's choice between the Gen 1 and
        /// Gen 2 transfer templates: a Red/Blue/Yellow Pokemon takes the Gen 1 one whenever a stage
        /// of its chain exists in Gen 1. PKHeX also requires that stage's base friendship to match
        /// the OT friendship, which PKSE's personal table (later games' values) cannot answer.
        uint16_t getVirtualConsoleTransferLocation(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (!isVirtualConsoleGenerationOne(pokemon))
            {
                return LOCATION_TRANSFER_GENERATION_TWO;
            }
            if (pokemon.speciesID() <= MAX_SPECIES_GENERATION_ONE)
            {
                return LOCATION_TRANSFER_GENERATION_ONE;
            }
            uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN];
            uint8_t chainForms[Pokemon::EVO_MAX_CHAIN];
            const int preEvolutionCount = Pokemon::getPreEvolutionChain(Enums::GameVersion::USUM, pokemon.speciesID(), pokemon.form(), chainSpecies, chainForms, Pokemon::EVO_MAX_CHAIN);
            for (int chainIndex = 0; chainIndex < preEvolutionCount; ++chainIndex)
            {
                if (chainSpecies[chainIndex] <= MAX_SPECIES_GENERATION_ONE && chainForms[chainIndex] == 0)
                {
                    return LOCATION_TRANSFER_GENERATION_ONE;
                }
            }
            return LOCATION_TRANSFER_GENERATION_TWO;
        }

        /// PKHeX TransferVerifier.VerifyVCEncounter. Its Dizzy Punch flag rewrites a move result,
        /// and its "any move from Gen 2" trigger for the shiny checks needs move origins, neither of
        /// which PKSE has; the original encounter's generation stands in as the record's own.
        void verifyVirtualConsoleEncounter(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.metLocation() != getVirtualConsoleTransferLocation(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferMetLocation);
            }

            const uint16_t expectedEggLocation = context.formatGroup == Enums::GameVersion::BDSP ? LOCATION_NONE_BRILLIANT_DIAMOND_SHINING_PEARL : 0;
            if (pokemon.eggLocation() != expectedEggLocation)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::EggLocationNone);
            }

            const bool shouldCheckShiny = isVirtualConsoleGenerationTwo(pokemon) || getOriginGeneration(context) == 2;
            if (!shouldCheckShiny)
            {
                return;
            }
            if (pokemon.gender() == GENDER_FEMALE)
            {
                // A Gen 2 shiny's Attack DV is fixed, which decides gender for a one-in-eight female species.
                if (Pokemon::getPersonalInfo(pokemon.speciesID(), pokemon.form()).genderRatio == GENDER_RATIO_ONE_IN_EIGHT_FEMALE && isShiny(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::EncounterStaticPIDShiny);
                }
            }
            else if (pokemon.speciesID() == UNOWN_SPECIES)
            {
                // A Gen 2 shiny's DVs spell only Unown I or V.
                constexpr uint8_t unownFormI = 8;
                constexpr uint8_t unownFormV = 21;
                if (pokemon.form() != unownFormI && pokemon.form() != unownFormV && isShiny(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::EncounterStaticPIDShiny);
                }
            }
        }

        /// PKHeX TransferVerifier.VerifyTransferLegalityG12.
        void verifyGameBoyTransfer(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.otGender() == 1 && pokemon.originGame() != VERSION_CRYSTAL)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::Gen2OTGender);
            }

            // Poke Transporter derives the nature from the EXP, which cannot change after a level 100 transfer.
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel == 100)
            {
                const uint8_t transferNature = static_cast<uint8_t>(pokemon.exp() % NATURE_COUNT);
                if (transferNature != pokemon.nature())
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferNature);
                }
            }
            else if (metLevel <= 2)
            {
                if (!isValidNatureMetLevelTwo(Pokemon::getGrowthRate(pokemon.speciesID()), pokemon.nature()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::TransferNature);
                }
            }

            // A star (not square) shiny needs the random transfer PID to land on one by chance.
            const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32());
            if (shinyXor != 0 && shinyXor < SHINY_XOR_THRESHOLD)
            {
                context.add(Severity::Warning, CheckIdentifier::PID, ResultCode::EncounterStaticPIDShiny);
            }

            if (context.formatGeneration == 7 && !isRegionLockedLanguageValidVirtualConsole(pokemon.consoleRegion(), pokemon.language()))
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::OTLanguageCannotTransferToConsoleRegion, std::to_string(pokemon.consoleRegion()));
            }
        }

        /// PKHeX LegalityAnalysis.GetParseFormat: the origin generation, with a Virtual Console
        /// transfer parsed as Gen 7.
        int getParseGeneration(const AnalysisContext &context)
        {
            const int originGeneration = getOriginGeneration(context);
            if (originGeneration == 1 || originGeneration == 2)
            {
                return 7;
            }
            return originGeneration;
        }
    }

    void verifyTransfer(const AnalysisContext &context)
    {
        const int formatGeneration = context.formatGeneration;
        if (formatGeneration < 3)
        {
            return;
        }
        switch (getParseGeneration(context))
        {
        case 3:
            if (formatGeneration > 3)
            {
                verifyGenerationThreeTransfer(context);
            }
            break;
        case 4:
            if (formatGeneration > 4)
            {
                verifyGenerationFourTransfer(context);
            }
            break;
        case 7:
            if (isVirtualConsole(context.pokemon) && context.encounter.type != EncounterType::Unmatched)
            {
                verifyVirtualConsoleEncounter(context);
                verifyGameBoyTransfer(context);
            }
            break;
        default:
            break;
        }
        if (formatGeneration >= 8)
        {
            verifyHomeTransfer(context);
        }
    }
}
