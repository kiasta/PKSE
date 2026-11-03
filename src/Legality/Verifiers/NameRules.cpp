#include "Legality/Verifiers/NameRules.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Pokemon/EvolutionTable.h"
#include "Legality/Verifiers/SpeciesName.h"
#include "Legality/Verifiers/WordFilter.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t VERSION_FIRERED_VIRTUAL_CONSOLE = 56;
        constexpr uint8_t VERSION_LEAFGREEN_VIRTUAL_CONSOLE = 57;
        /// PKHeX LocationsHOME.SHLG and SWFR: where HOME puts a Switch FireRed/LeafGreen Pokemon in Sword/Shield.
        constexpr uint16_t LOCATION_HOME_LEAFGREEN_TO_SHIELD = 59994;
        constexpr uint16_t LOCATION_HOME_FIRERED_TO_SWORD = 59995;
        constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        constexpr uint8_t VERSION_FIRST_VIRTUAL_CONSOLE = 35;
        constexpr uint8_t VERSION_LAST_VIRTUAL_CONSOLE = 41;
        constexpr uint16_t MAX_SPECIES_GENERATION_1 = 151;
        // PKHeX Locations
        constexpr uint16_t LOCATION_LINK_TRADE_4 = 2002;
        constexpr uint16_t LOCATION_LINK_TRADE_5 = 30003;
        // PKHeX Legal.MaxLength*
        constexpr int MAX_LENGTH_TRAINER_ASIAN = 6;
        constexpr int MAX_LENGTH_TRAINER_WESTERN = 12;
        constexpr int MAX_LENGTH_NICKNAME_ASIAN = 6;
        constexpr int MAX_LENGTH_NICKNAME_WESTERN = 12;
        constexpr int MAX_LENGTH_NICKNAME_ASIAN_BEFORE_GENERATION_SIX = 5;
        constexpr int MAX_LENGTH_NICKNAME_WESTERN_BEFORE_GENERATION_SIX = 10;

        bool isChinese(uint8_t language)
        {
            return language == static_cast<uint8_t>(Enums::LanguageID::ChineseSimplified) || language == static_cast<uint8_t>(Enums::LanguageID::ChineseTraditional);
        }

        bool isJapaneseOrKorean(uint8_t language)
        {
            return language == static_cast<uint8_t>(Enums::LanguageID::Japanese) || language == static_cast<uint8_t>(Enums::LanguageID::Korean);
        }

        /// PKHeX Locations.TradedEggLocation.
        uint16_t getTradedEggLocation(const AnalysisContext &context)
        {
            const uint8_t originVersion = context.pokemon.originGame();
            switch (getOriginGeneration(context))
            {
            case 4:
                return LOCATION_LINK_TRADE_4;
            case 5:
                return LOCATION_LINK_TRADE_5;
            case 8:
                if (originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP))
                {
                    return LOCATION_LINK_TRADE_6_NPC;
                }
                return LOCATION_LINK_TRADE_6;
            default:
                return LOCATION_LINK_TRADE_6;
            }
        }
    }

    bool isVirtualConsoleGenerationThree(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGroup == Enums::GameVersion::SWSH)
        {
            return pokemon.metLocation() == LOCATION_HOME_LEAFGREEN_TO_SHIELD || pokemon.metLocation() == LOCATION_HOME_FIRERED_TO_SWORD;
        }
        return pokemon.hasOriginGame() && (pokemon.originGame() == VERSION_FIRERED_VIRTUAL_CONSOLE || pokemon.originGame() == VERSION_LEAFGREEN_VIRTUAL_CONSOLE);
    }

    bool allowsGameBoyAdvanceCrossTransfer(const AnalysisContext &context)
    {
        // PKHeX IsGen3HOMEOriginVirtual(pk, false): a cartridge-era Pokemon that has since reached HOME.
        return allowsGameBoyAdvanceCartridgeEra(context.options) || (context.formatGeneration >= 8 && !isVirtualConsoleGenerationThree(context));
    }

    int getOriginGeneration(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasOriginGame() || context.formatGeneration <= 2)
        {
            return context.formatGeneration;
        }
        const uint8_t originVersion = pokemon.originGame();
        if (originVersion == VERSION_POKEMON_GO)
        {
            return pokemon.metLocation() == LOCATION_GO_LETS_GO ? 7 : 8;
        }
        const int originGeneration = Enums::getVersionGeneration(originVersion);
        if (originGeneration == 1 && pokemon.speciesID() > MAX_SPECIES_GENERATION_1)
        {
            return 2; // a Gen 2 species traded back to a Gen 1 Virtual Console game
        }
        return originGeneration != 0 ? originGeneration : context.formatGeneration;
    }

    Enums::GameVersion getOriginContextGroup(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasOriginGame() || context.formatGeneration <= 2)
        {
            return context.formatGroup;
        }
        const uint8_t originVersion = pokemon.originGame();
        if (originVersion == VERSION_POKEMON_GO)
        {
            return pokemon.metLocation() == LOCATION_GO_LETS_GO ? Enums::GameVersion::GG : Enums::GameVersion::SWSH;
        }
        if (originVersion == VERSION_COLOSSEUM_XD)
        {
            return Enums::GameVersion::RSE;
        }
        const Enums::GameVersion originGroup = Enums::getGameGroup(static_cast<Enums::GameVersion>(originVersion));
        return originGroup != Enums::GameVersion::Invalid ? originGroup : context.formatGroup;
    }

    bool isVirtualConsole(const Pokemon::Pokemon &pokemon)
    {
        if (!pokemon.hasOriginGame())
        {
            return false;
        }
        const uint8_t originVersion = pokemon.originGame();
        return originVersion >= VERSION_FIRST_VIRTUAL_CONSOLE && originVersion <= VERSION_LAST_VIRTUAL_CONSOLE;
    }

    bool isPlayerOriginalTrainer(const AnalysisContext &context)
    {
        switch (context.encounter.type)
        {
        case EncounterType::Trade:
            return false;
        case EncounterType::Event:
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            return encounterMatch != nullptr && encounterMatch->isEgg();
        }
        case EncounterType::Unchecked:
            // A Virtual Console transfer: every Game Boy in-game trade arrives nicknamed, and Gen 1/2
            // has no other fixed-OT encounter.
            return !isNicknamedAsStored(context);
        default:
            return true;
        }
    }

    int getMaxNicknameLength(int generation, uint8_t language)
    {
        if (isChinese(language))
        {
            return MAX_LENGTH_NICKNAME_ASIAN;
        }
        if (isJapaneseOrKorean(language))
        {
            return generation >= 6 ? MAX_LENGTH_NICKNAME_ASIAN : MAX_LENGTH_NICKNAME_ASIAN_BEFORE_GENERATION_SIX;
        }
        return generation >= 6 ? MAX_LENGTH_NICKNAME_WESTERN : MAX_LENGTH_NICKNAME_WESTERN_BEFORE_GENERATION_SIX;
    }

    int getMaxTrainerNameLength(int generation, uint8_t language)
    {
        static_cast<void>(generation); // PKHeX keeps separate pre-Gen 6 constants, equal to these
        if (isChinese(language) || isJapaneseOrKorean(language))
        {
            return MAX_LENGTH_TRAINER_ASIAN;
        }
        return MAX_LENGTH_TRAINER_WESTERN;
    }

    bool isNicknamedAsStored(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGeneration == 3)
        {
            return isNicknamedInLanguage(pokemon.speciesID(), pokemon.nickname(), pokemon.language(), 3);
        }
        // PKHeX compares a Game Boy nickname's whole field with the encoded species name
        // (GBPKM.GetIsNicknamedTrash); the decoded text answers the same for every name the
        // games write. PKSE's own flag stays the answer where the language is unknown.
        if (context.formatGeneration <= 2 && pokemon.language() != 0)
        {
            return isNicknamedInLanguage(pokemon.speciesID(), pokemon.nickname(), pokemon.language(), context.formatGeneration);
        }
        return pokemon.isNicknamed();
    }

    bool wasTradedEgg(const AnalysisContext &context)
    {
        return context.pokemon.hasEggData() && context.pokemon.eggLocation() == getTradedEggLocation(context);
    }

    bool isTradedEgg(const AnalysisContext &context)
    {
        return context.pokemon.hasMetData() && context.pokemon.metLocation() == getTradedEggLocation(context);
    }

    uint8_t getMaxLanguageId(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO)
        {
            return static_cast<uint8_t>(Enums::LanguageID::SpanishL); // HOME, not the format, bounds a GO Pokemon
        }
        switch (getOriginGeneration(context))
        {
        case 1:
        case 3:
            return static_cast<uint8_t>(Enums::LanguageID::Spanish);
        case 2:
        case 4:
        case 5:
        case 6:
            return static_cast<uint8_t>(Enums::LanguageID::Korean);
        case 7:
        case 8:
            return static_cast<uint8_t>(Enums::LanguageID::ChineseTraditional);
        case 9:
            return getOriginContextGroup(context) == Enums::GameVersion::ZA ? static_cast<uint8_t>(Enums::LanguageID::SpanishL) : static_cast<uint8_t>(Enums::LanguageID::ChineseTraditional);
        default:
            return 0;
        }
    }

    bool isValidLanguageId(const AnalysisContext &context)
    {
        const uint8_t language = context.pokemon.language();
        if (language == static_cast<uint8_t>(Enums::LanguageID::UNUSED_6))
        {
            return false;
        }
        if (language > getMaxLanguageId(context))
        {
            return false;
        }
        if (language == 0)
        {
            // A Black/White in-game trade stores no language while it stays in Gen 5 (EncounterTrade5BW).
            const bool isBlackWhiteTrade = context.encounter.type == EncounterType::Trade && getOriginContextGroup(context) == Enums::GameVersion::BW;
            return isBlackWhiteTrade && context.formatGeneration == 5;
        }
        return true;
    }

    bool isGlobalTradeSanitizedTrainer(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGeneration < 4 || getOriginGeneration(context) != 4 || pokemon.isEgg())
        {
            return false;
        }
        if (pokemon.language() != static_cast<uint8_t>(Enums::LanguageID::Korean) || !isValidLanguageId(context))
        {
            return false;
        }
        // PKHeX Gen4GlobalTradeRules.IsRomanizedKoreanTrainerName.
        const std::u16string trainerName = pokemon.otName();
        return trainerName == u"Ahn" || trainerName == u"Han" || trainerName == u"Jeong" || trainerName == u"Hwang";
    }

    int getMaxNumberCount(int originalGeneration)
    {
        return originalGeneration < 6 ? 4 : 5;
    }

    bool containsTooManyNumbers(const std::u16string &text, int originalGeneration)
    {
        if (originalGeneration <= 3)
        {
            return false;
        }
        const int maxNumberCount = getMaxNumberCount(originalGeneration);
        if (static_cast<int>(text.size()) <= maxNumberCount)
        {
            return false;
        }
        int numberCount = 0;
        for (const char16_t character : text)
        {
            if (isNumberCharacter(character))
            {
                ++numberCount;
            }
        }
        return numberCount > maxNumberCount;
    }
}
