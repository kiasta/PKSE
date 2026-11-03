/**
 * Companion to roundtrip.cpp. That one proves the write path does not corrupt a save; this one
 * proves the READ-side judgement is not slandering it.
 *
 * The legality checker is a port of PKHeX's, so a finding PKHeX would not report is a bug: it calls
 * a legitimately-obtained Pokemon illegal. The only way to measure that off-console is to point it
 * at saves from actual playthroughs, where nearly every Pokemon IS legal, and count what it reports:
 *
 *     load a real save -> analyze every party and box Pokemon -> histogram the findings
 *
 * --json writes every finding with PKHeX's identifier and code, which build-parity.sh diffs against
 * PKHeX's own analysis (tools/legalitydump); see docs/DEV_TESTING.md for the other modes.
 *
 * A healthy run is mostly "matched", with the leftovers being things the user really did edit.
 * A wall of encounter findings means the encounter data or the engine is wrong, not the save. Pass --verbose to print every flagged Pokemon instead of just the counts.
 *
 * Runs on the HOST, not the Switch, exactly like roundtrip.cpp.
 *
 * Build + run:  ./tools/harness/build-legality.sh <save-root> [--verbose]
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <span>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

/// The save root, so a skip can name the folder the reader needs to create.
static std::string g_saveRoot;

#include "Encryption/Encryption.h"
#include "Globals.h"     // SIZE_HASH_IN_BYTES
#include "Legality/AnalysisContext.h"
#include "Legality/Legality.h"
#include "Legality/RNG/MethodFinder.h" // --pidiv: the RNG port against PKHeX's MethodFinder
#include "Legality/Evolutions/EvolutionChainSearch.h" // --evolution: the evolution engine against PKHeX's
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Encounters/EncounterFinder.h" // --encounter: the matched template against PKHeX's
#include "Legality/Encounters/LegalityInfo.h"
#include "Pokemon/EvolutionTable.h" // the evolution bite check needs the level floors
#include "Pokemon/Pokemon.h"
#include "Trainer/PokemonFile.h"
#include "Save/Block.h"
#include "Save/RtcFooter.h"   // Game Boy saves carry an emulator clock the parsers must not see
#include "Save/SaveFileName.h" // a Virtual Console save is told apart by its file name
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer3RSE.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7USUM.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"
#include "Utils/StringHelpers.h"

// Same logging stubs as roundtrip.cpp -- Logger.cpp needs POSIX dirent, which MSVC has no header
// for, and a harness has no use for the log.
namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
    void logEventToFile(const std::string &) {}
    // Bank.cpp comes in with the rest of src/Trainer and its SD-card paths reach this. Nothing in
    // this harness opens a bank, so returning "no such file" is both honest and the only outcome a
    // host run could have.
    uint8_t *readAllBytes(const char *, size_t *outSize)
    {
        if (outSize)
        {
            *outSize = 0;
        }
        return nullptr;
    }
}

namespace
{
    bool g_verbose = false;

    /// --json <file>: every analyzed Pokemon as one JSON line, in tools/legalitydump's shape, so
    /// tools/harness/parity_diff.py can compare PKSE's findings with PKHeX's. A `<file>.manifest`
    /// beside it lists `title=path` for every save opened, which is legalitydump's input.
    FILE *g_jsonFile = nullptr;
    FILE *g_manifestFile = nullptr;
    std::string g_jsonTitle;

    std::string jsonEscape(const std::string &text)
    {
        std::string escapedText;
        for (const char character : text)
        {
            if (character == '"' || character == '\\')
            {
                escapedText += '\\';
                escapedText += character;
            }
            else if (static_cast<unsigned char>(character) < 0x20)
            {
                char escapeBuffer[8];
                std::snprintf(escapeBuffer, sizeof(escapeBuffer), "\\u%04x", static_cast<unsigned char>(character));
                escapedText += escapeBuffer;
            }
            else
            {
                escapedText += character;
            }
        }
        return escapedText;
    }

    const char *severityName(Legality::Severity severity)
    {
        switch (severity)
        {
        case Legality::Severity::Invalid:
            return "Invalid";
        case Legality::Severity::Warning:
            return "Fishy";
        default:
            return "Valid";
        }
    }

    /// --pidiv <file>: MethodFinder's correlation for every Pokemon the sweep analyzes, in
    /// legalitydump pidiv's shape, for tools/harness/pidiv_diff.py.
    FILE *g_pidivFile = nullptr;

    void writePIDIVJson(const ::Pokemon::Pokemon &pokemon, const std::string &slotKey)
    {
        if (!g_pidivFile)
        {
            return;
        }
        const Legality::RNG::PIDIV pidiv = Legality::RNG::MethodFinder::analyze(pokemon);
        std::fprintf(g_pidivFile, "{\"title\":\"%s\",\"slot\":\"%s\",\"species\":%u,\"pid\":%u,\"ec\":%u,\"ivs\":[%u,%u,%u,%u,%u,%u],\"pidType\":\"%s\",\"seed\":%u}\n", jsonEscape(g_jsonTitle).c_str(), slotKey.c_str(), pokemon.speciesID(), pokemon.pid(), pokemon.encryptionConstant(), pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD(), Legality::RNG::getPIDTypeSourceName(pidiv.type), pidiv.originSeed);
    }

    /// --evolution <file>: the evolution engine's per-context chains and origin chain for every
    /// Pokemon, in legalitydump evolution's shape, for tools/harness/evolution_diff.py. The history
    /// needs the encounter PKHeX matched, which PKSE does not find yet, so --evolution-encounters
    /// reads it from legalitydump evolution's own output.
    FILE *g_evolutionFile = nullptr;
    std::map<std::string, Legality::Evolutions::EvolutionEncounter> g_evolutionEncounters;

    std::string getJsonText(const std::string &line, const std::string &key, size_t searchFrom = 0)
    {
        const std::string marker = "\"" + key + "\":\"";
        const size_t start = line.find(marker, searchFrom);
        if (start == std::string::npos)
        {
            return "";
        }
        std::string text;
        for (size_t characterIndex = start + marker.size(); characterIndex < line.size() && line[characterIndex] != '"'; characterIndex++)
        {
            if (line[characterIndex] != '\\' || characterIndex + 1 >= line.size())
            {
                text += line[characterIndex];
                continue;
            }
            characterIndex++;
            if (line[characterIndex] != 'u' || characterIndex + 4 >= line.size())
            {
                text += line[characterIndex];
                continue;
            }
            // System.Text.Json writes every non-ASCII character, and the apostrophe, as \uXXXX.
            std::u16string codeUnits(1, static_cast<char16_t>(std::strtoul(line.substr(characterIndex + 1, 4).c_str(), nullptr, 16)));
            characterIndex += 4;
            const bool isHighSurrogate = codeUnits[0] >= 0xD800 && codeUnits[0] < 0xDC00;
            if (isHighSurrogate && characterIndex + 6 < line.size() && line[characterIndex + 1] == '\\' && line[characterIndex + 2] == 'u')
            {
                codeUnits += static_cast<char16_t>(std::strtoul(line.substr(characterIndex + 3, 4).c_str(), nullptr, 16));
                characterIndex += 6;
            }
            text += Utils::utf16ToUtf8(codeUnits);
        }
        return text;
    }

    long getJsonNumber(const std::string &line, const std::string &key, size_t searchFrom)
    {
        const std::string marker = "\"" + key + "\":";
        const size_t start = line.find(marker, searchFrom);
        return start == std::string::npos ? 0 : std::strtol(line.c_str() + start + marker.size(), nullptr, 10);
    }

    bool loadEvolutionEncounters(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
        {
            return false;
        }
        std::string line;
        while (std::getline(input, line))
        {
            const size_t encounterStart = line.find("\"encounter\":{");
            if (encounterStart == std::string::npos)
            {
                continue;
            }
            Legality::Evolutions::EvolutionEncounter encounter;
            encounter.speciesId = static_cast<uint16_t>(getJsonNumber(line, "species", encounterStart));
            encounter.generation = static_cast<uint8_t>(getJsonNumber(line, "generation", encounterStart));
            encounter.levelMin = static_cast<uint8_t>(getJsonNumber(line, "levelMin", encounterStart));
            encounter.isInvalid = line.find("\"invalid\":true", encounterStart) != std::string::npos;
            const std::string contextName = getJsonText(line, "context", encounterStart);
            for (const Enums::GameVersion contextGroup : Legality::Evolutions::EVOLUTION_CONTEXT_GROUPS)
            {
                if (contextName == Legality::Evolutions::getEvolutionContextSourceName(contextGroup))
                {
                    encounter.contextGroup = contextGroup;
                }
            }
            g_evolutionEncounters[getJsonText(line, "title") + '\n' + getJsonText(line, "slot")] = encounter;
        }
        return true;
    }

    void writeEvolutionChain(const char *chainName, std::span<const Legality::Evolutions::EvolutionCriteria> chain)
    {
        std::fprintf(g_evolutionFile, "\"%s\":[", chainName);
        bool firstStage = true;
        for (const Legality::Evolutions::EvolutionCriteria &stage : chain)
        {
            std::fprintf(g_evolutionFile, "%s[%u,%u,%u,%u,\"%s\"]", firstStage ? "" : ",", stage.speciesId, stage.formId, stage.levelMin, stage.levelMax, Legality::Evolutions::getEvolutionTypeSourceName(stage.method));
            firstStage = false;
        }
        std::fprintf(g_evolutionFile, "]");
    }

    void writeEvolutionJson(const ::Pokemon::Pokemon &pokemon, const std::string &slotKey)
    {
        namespace Evolutions = Legality::Evolutions;
        if (!g_evolutionFile)
        {
            return;
        }
        const Evolutions::EvolutionEntity entity = Evolutions::getEvolutionEntity(pokemon);
        const auto encounterPosition = g_evolutionEncounters.find(g_jsonTitle + '\n' + slotKey);
        const bool hasEncounter = encounterPosition != g_evolutionEncounters.end();
        const Evolutions::EvolutionEncounter encounter = hasEncounter ? encounterPosition->second : Evolutions::getInvalidEvolutionEncounter(entity);
        const Evolutions::EvolutionHistory history = Evolutions::getEvolutionChainsAllGens(entity, encounter);
        std::fprintf(g_evolutionFile, "{\"title\":\"%s\",\"slot\":\"%s\",\"species\":%u,\"form\":%u,\"hasEncounter\":%s,\"history\":{", jsonEscape(g_jsonTitle).c_str(), slotKey.c_str(), pokemon.speciesID(), pokemon.form(), hasEncounter ? "true" : "false");
        bool firstContext = true;
        for (const Enums::GameVersion contextGroup : Evolutions::EVOLUTION_CONTEXT_GROUPS)
        {
            std::fprintf(g_evolutionFile, "%s", firstContext ? "" : ",");
            writeEvolutionChain(Evolutions::getEvolutionContextSourceName(contextGroup), history.getChain(contextGroup));
            firstContext = false;
        }
        std::fprintf(g_evolutionFile, "},");
        const bool isGameBoyFormat = entity.formatGeneration <= 2;
        const Enums::GameVersion originContext = isGameBoyFormat ? Evolutions::getEntityContextGroup(entity) : Evolutions::getVersionContextGroup(entity);
        if (entity.originGeneration == 0 || originContext == Enums::GameVersion::Invalid)
        {
            std::fprintf(g_evolutionFile, "\"origin\":null}\n");
            return;
        }
        const uint8_t originGeneration = static_cast<uint8_t>(entity.originGeneration);
        const Evolutions::EvolutionChain origin = isGameBoyFormat ? Evolutions::getEncounterOriginChainGameBoy(entity, originGeneration, originContext) : Evolutions::getEncounterOriginChain(entity, originGeneration, originContext);
        std::fprintf(g_evolutionFile, "\"origin\":{\"generation\":%u,\"context\":\"%s\",", originGeneration, Evolutions::getEvolutionContextSourceName(originContext));
        writeEvolutionChain("chain", origin.view());
        std::fprintf(g_evolutionFile, "}}\n");
    }

    /// --encounter <file>: the encounter engine's matched template (PKHeX LegalInfo.EncounterOriginal's
    /// LongName) for every Pokemon, for tools/harness/encounter_diff.py.
    FILE *g_encounterFile = nullptr;

    void writeEncounterJson(const ::Pokemon::Pokemon &pokemon, const std::string &slotKey, const Legality::AnalysisOptions &options)
    {
        if (!g_encounterFile)
        {
            return;
        }
        const unsigned originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
        std::fprintf(g_encounterFile, "{\"title\":\"%s\",\"slot\":\"%s\",\"species\":%u,\"originVersion\":%u,", jsonEscape(g_jsonTitle).c_str(), slotKey.c_str(), pokemon.speciesID(), originVersion);
        Legality::Encounters::LegalityInfo info(pokemon, options);
        Legality::Encounters::EncounterFinder::findVerifiedEncounter(pokemon, info);
        Legality::Encounters::EncounterFinder::updateVirtualConsoleTransferInfo(pokemon, info);
        const Legality::Encounters::EncounterReference &encounterOriginal = info.encounterOriginal();
        const std::string longName = encounterOriginal ? encounterOriginal->longName() : std::string();
        std::fprintf(g_encounterFile, "\"encounter\":\"%s\"}\n", jsonEscape(longName).c_str());
    }

    void writeJson(const ::Pokemon::Pokemon &pokemon, const std::string &slotKey, const Legality::Report &report, const Legality::AnalysisOptions &options = Legality::AnalysisOptions{})
    {
        writePIDIVJson(pokemon, slotKey);
        writeEncounterJson(pokemon, slotKey, options);
        writeEvolutionJson(pokemon, slotKey);
        if (!g_jsonFile)
        {
            return;
        }
        std::fprintf(g_jsonFile, "{\"title\":\"%s\",\"slot\":\"%s\",\"species\":%u,\"form\":%u,\"pid\":%u,\"ec\":%u,\"isEgg\":%s,\"results\":[", jsonEscape(g_jsonTitle).c_str(), slotKey.c_str(), pokemon.speciesID(), pokemon.form(), pokemon.pid(), pokemon.encryptionConstant(), pokemon.isEgg() ? "true" : "false");
        bool firstResult = true;
        for (const auto &issue : report.issues)
        {
            std::fprintf(g_jsonFile, "%s{\"identifier\":\"%s\",\"code\":\"%s\",\"severity\":\"%s\",\"source\":\"%s\",\"message\":\"%s\"}", firstResult ? "" : ",", Legality::getCheckIdentifierSourceName(issue.identifier), Legality::getResultCodeSourceName(issue.code), severityName(issue.severity), issue.source == Legality::IssueSource::Move ? "move" : "check", jsonEscape(issue.text).c_str());
            firstResult = false;
        }
        std::fprintf(g_jsonFile, "]}\n");
    }

    void recordManifest(const std::string &title, const std::string &path)
    {
        g_jsonTitle = title;
        if (g_manifestFile)
        {
            std::fprintf(g_manifestFile, "%s=%s\n", title.c_str(), path.c_str());
        }
    }

    std::vector<uint8_t> readFile(const std::string &path)
    {
        std::vector<uint8_t> fileBytes;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            return fileBytes;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (fileSize > 0)
        {
            fileBytes.resize(static_cast<size_t>(fileSize));
            if (std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
            {
                fileBytes.clear();
            }
        }
        std::fclose(file);
        return fileBytes;
    }

    /**
     * Findings are histogrammed by SHAPE, not by exact text: "Route 3 (12) is not a place Nickit can
     * be encountered" and the same sentence about Wooloo are one bug, not two. Trimming at the first
     * digit or quote collapses the species and location out of the message.
     */
    std::string shapeOf(const std::string &text)
    {
        for (size_t characterIndex = 0; characterIndex < text.size(); ++characterIndex)
        {
            if (text[characterIndex] >= '0' && text[characterIndex] <= '9')
            {
                std::string leadingText = text.substr(0, characterIndex);
                while (!leadingText.empty() && leadingText.back() == ' ')
                {
                    leadingText.pop_back();
                }
                return leadingText + " ...";
            }
        }
        return text;
    }

    struct Tally
    {
        int pokemonCount = 0;
        int cleanCount = 0;                 // no Warning/Invalid at all
        int matchedCount = 0;               // the encounter engine matched an encounter
        int invalidCount = 0;
        int warningCount = 0;
        std::map<std::string, int> findingCountByShape;
        // Info notes, grouped the same way. "Clean" is not the same as "checked": the engine
        // abstains for origins it has no tables for, and says so in a note. Counting those
        // is the only way to see how much of a save the check actually reached.
        std::map<std::string, int> noteCountByText;
        int notedCount = 0;                 // Pokemon carrying at least one abstention note
    };

    void analyzeOne(const ::Pokemon::Pokemon &pokemon, Enums::GameVersion group, Tally &tally, const std::string &slotKey, const Legality::AnalysisOptions &options)
    {
        if (pokemon.speciesID() == 0)
        {
            return;
        }
        ++tally.pokemonCount;
        const Legality::Report report = Legality::analyze(pokemon, group, options);
        writeJson(pokemon, slotKey, report, options);
        if (report.ok())
        {
            ++tally.cleanCount;
        }
        bool printedHeader = false;
        bool carriesNote = false;
        for (const auto &issue : report.issues)
        {
            if (issue.severity == Legality::Severity::Info)
            {
                if (issue.text.rfind("Matches a", 0) == 0)
                {
                    ++tally.matchedCount;
                }
                // Notes keep their full text where findings get shaped: they only vary by the
                // origin id, and that id is the interesting part -- it says WHICH games a save
                // is full of that the engine cannot reach.
                else
                {
                    tally.noteCountByText[issue.text]++;
                    carriesNote = true;
                }
                continue;
            }
            if (issue.severity == Legality::Severity::Invalid)
            {
                ++tally.invalidCount;
            }
            else
            {
                ++tally.warningCount;
            }
            tally.findingCountByShape[shapeOf(issue.text)]++;
            if (g_verbose)
            {
                if (!printedHeader)
                {
                    std::printf("    %-14s form %-2u  met %5u lv %-3u  origin %-3u\n", pokemon.species(), pokemon.form(), pokemon.metLocation(), pokemon.metLevel(), pokemon.originGame());
                    printedHeader = true;
                }
                std::printf("        %s %s\n", issue.severity == Legality::Severity::Invalid ? "[illegal]" : "[warning]", issue.text.c_str());
            }
        }
        if (carriesNote)
        {
            ++tally.notedCount;
        }
    }

    template <typename TrainerT>
    void sweepTrainer(const char *label, TrainerT &trainer)
    {
        Tally tally;
        const Enums::GameVersion group = trainer.getGameGroup();
        // PKHeX analyzes a save's slots as StorageSlotType.Party and Box, with the save as the active
        // trainer (tools/legalitydump: ParseSettings.InitFromSaveFileData).
        const Legality::ActiveTrainer activeTrainer = Legality::makeActiveTrainer(trainer);
        Legality::AnalysisOptions partyOptions;
        partyOptions.slotType = Legality::StorageSlotType::Party;
        partyOptions.activeTrainer = &activeTrainer;
        Legality::AnalysisOptions boxOptions;
        boxOptions.slotType = Legality::StorageSlotType::Box;
        boxOptions.activeTrainer = &activeTrainer;
        // A Let's Go party member is a box slot the party points at. PKHeX's SAV7b has no party
        // (HasParty is false) and analyzes that slot once, as a Box slot, so the party copy is skipped.
        bool isPartyInBoxStorage = false;
        for (size_t boxIndex = 0; boxIndex < trainer.boxes.size() && !isPartyInBoxStorage; ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < trainer.boxes[boxIndex].size(); ++slotIndex)
            {
                if (trainer.getPartyPosition(boxIndex, slotIndex) != 0)
                {
                    isPartyInBoxStorage = true;
                    break;
                }
            }
        }
        for (size_t partyIndex = 0; partyIndex < trainer.party.size() && !isPartyInBoxStorage; ++partyIndex)
        {
            if (trainer.party[partyIndex])
            {
                analyzeOne(*trainer.party[partyIndex], group, tally, "party:" + std::to_string(partyIndex), partyOptions);
            }
        }
        for (size_t boxIndex = 0; boxIndex < trainer.boxes.size(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < trainer.boxes[boxIndex].size(); ++slotIndex)
            {
                if (trainer.boxes[boxIndex][slotIndex])
                {
                    analyzeOne(*trainer.boxes[boxIndex][slotIndex], group, tally, "box:" + std::to_string(boxIndex) + ":" + std::to_string(slotIndex), boxOptions);
                }
            }
        }

        std::printf("  %-20s %4d Pokemon | %4d clean | %4d matched an encounter | %3d illegal, %3d warnings\n", label, tally.pokemonCount, tally.cleanCount, tally.matchedCount, tally.invalidCount, tally.warningCount);
        for (const auto &shapeEntry : tally.findingCountByShape)
        {
            std::printf("        %4d x  %s\n", shapeEntry.second, shapeEntry.first.c_str());
        }
        for (const auto &noteEntry : tally.noteCountByText)
        {
            std::printf("        %4d -  %s\n", noteEntry.second, noteEntry.first.c_str());
        }
        const int unexplained = tally.pokemonCount - tally.matchedCount - tally.notedCount;
        if (unexplained > 0)
        {
            std::printf("        %4d ?  neither matched nor noted -- these are the hatched eggs, which take the egg path and get no match note\n", unexplained);
        }
    }

    void sweepFRLG(const std::string &path)
    {
        const std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", "FireRed", SaveTree::missing(g_saveRoot, "FireRed").c_str());
            return;
        }
        recordManifest("FireRed", path);
        Trainer::Trainer3FRLG trainer(saveBytes, std::string(Save::getFileNameFromPath(path)));
        sweepTrainer("FireRed/LeafGreen", trainer);
    }

    // The LOOSE-FILE saves, which are handed their bytes rather than a decrypted block list. Every
    // generation whose encounter tables exist needs a real save swept through it: a wrong slot stride
    // or a version bit in the wrong place does not fail to build, it just reports a boxful of
    // perfectly ordinary Pokemon as impossible.
    template <typename TrainerT>
    void sweepLooseFile(const char *label, const char *title, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, title).c_str());
            return;
        }
        // AN EMULATOR CLOCK FOOTER IS TRIMMED FIRST, as Save::openExternalSave trims it. Gold, Silver and
        // Crystal saves are commonly 0x8010 bytes, and handed over untrimmed they parsed as EMPTY -- so
        // this gate swept "0 Pokemon" for every Gen 2 save and never saw a Gen 2 finding. The rule only
        // ever trims down to 0x8000, 0x10000 or 0x20000, so an exact-size save passes through untouched.
        Save::stripRtcFooter(saveBytes);
        recordManifest(label, path);
        // The path, as Save::openExternalSave passes it: the file's name is what tells a Virtual
        // Console save from a cartridge one.
        TrainerT trainer(std::move(saveBytes), path);
        sweepTrainer(label, trainer);
    }

    void sweepLetsGo(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.size() < Trainer::SAVE_SIZE7_LGPE)
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        recordManifest(label, path);
        std::vector<uint8_t> saveArea(saveBytes.begin(), saveBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
        Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(saveArea));
        sweepTrainer(label, trainer);
    }

    void sweepBDSP(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        recordManifest(label, path);
        Trainer::Trainer8BDSP trainer(std::move(saveBytes));
        sweepTrainer(label, trainer);
    }

    /// --files <dir>: every Pokemon file (.pkX / .ekX ...) under `dir`, analyzed as its own format,
    /// for the parity harness. PKHeX's test tree holds deliberately illegal files, which reach checks
    /// a box of real catches never does.
    void sweepPokemonFiles(const std::string &directory, const std::string &relativeDirectory)
    {
        DIR *directoryStream = opendir(directory.c_str());
        if (!directoryStream)
        {
            return;
        }
        std::vector<std::string> entryNames;
        while (const dirent *entry = readdir(directoryStream))
        {
            const std::string entryName(entry->d_name);
            if (!entryName.empty() && entryName[0] != '.')
            {
                entryNames.push_back(entryName);
            }
        }
        closedir(directoryStream);
        std::sort(entryNames.begin(), entryNames.end());
        for (const auto &entryName : entryNames)
        {
            const std::string entryPath = directory + "/" + entryName;
            const std::string relativePath = relativeDirectory.empty() ? entryName : relativeDirectory + "/" + entryName;
            struct stat entryStatus{};
            if (stat(entryPath.c_str(), &entryStatus) != 0)
            {
                continue;
            }
            if (S_ISDIR(entryStatus.st_mode))
            {
                sweepPokemonFiles(entryPath, relativePath);
                continue;
            }
            if (!Trainer::PokemonFile::supportsFileName(entryName))
            {
                continue;
            }
            const std::vector<uint8_t> fileBytes = readFile(entryPath);
            const Trainer::PokemonFile::LoadResult loaded = Trainer::PokemonFile::parse(std::as_bytes(std::span<const uint8_t>(fileBytes)), entryName);
            if (!loaded)
            {
                if (g_jsonFile)
                {
                    std::fprintf(g_jsonFile, "{\"title\":\"file\",\"slot\":\"%s\",\"error\":\"%s\"}\n", jsonEscape(relativePath).c_str(), jsonEscape(loaded.error).c_str());
                }
                continue;
            }
            g_jsonTitle = "file";
            const Legality::Report report = Legality::analyze(*loaded.pokemon, loaded.pokemon->getGameGroup());
            writeJson(*loaded.pokemon, jsonEscape(relativePath), report);
        }
    }

    template <typename TrainerT>
    void sweepSCBlock(const char *label, const std::string &path)
    {
        std::vector<uint8_t> saveBytes = readFile(path);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        recordManifest(label, path);
        std::vector<Save::Block> blocks = Encryption::decrypt(saveBytes.data(), saveBytes.size());
        TrainerT trainer(std::move(blocks));
        sweepTrainer(label, trainer);
    }
}

int main(int argc, char **argv)
{
    std::string saveRoot;
    std::string filesDirectory;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        if (argument == "--verbose" || argument == "-v")
        {
            g_verbose = true;
        }
        else if (argument == "--files" && argumentIndex + 1 < argc)
        {
            filesDirectory = argv[++argumentIndex];
        }
        else if (argument == "--encounter" && argumentIndex + 1 < argc)
        {
            const std::string encounterPath = argv[++argumentIndex];
            g_encounterFile = std::fopen(encounterPath.c_str(), "w");
            if (!g_encounterFile)
            {
                std::printf("cannot write %s\n", encounterPath.c_str());
                return 2;
            }
        }
        else if (argument == "--pidiv" && argumentIndex + 1 < argc)
        {
            const std::string pidivPath = argv[++argumentIndex];
            g_pidivFile = std::fopen(pidivPath.c_str(), "w");
            if (!g_pidivFile)
            {
                std::printf("cannot write %s\n", pidivPath.c_str());
                return 2;
            }
        }
        else if (argument == "--evolution" && argumentIndex + 1 < argc)
        {
            const std::string evolutionPath = argv[++argumentIndex];
            g_evolutionFile = std::fopen(evolutionPath.c_str(), "w");
            if (!g_evolutionFile)
            {
                std::printf("cannot write %s\n", evolutionPath.c_str());
                return 2;
            }
        }
        else if (argument == "--evolution-encounters" && argumentIndex + 1 < argc)
        {
            const std::string encountersPath = argv[++argumentIndex];
            if (!loadEvolutionEncounters(encountersPath))
            {
                std::printf("cannot read %s\n", encountersPath.c_str());
                return 2;
            }
        }
        else if (argument == "--json" && argumentIndex + 1 < argc)
        {
            const std::string jsonPath = argv[++argumentIndex];
            g_jsonFile = std::fopen(jsonPath.c_str(), "w");
            g_manifestFile = std::fopen((jsonPath + ".manifest").c_str(), "w");
            if (!g_jsonFile || !g_manifestFile)
            {
                std::printf("cannot write %s\n", jsonPath.c_str());
                return 2;
            }
        }
        else
        {
            saveRoot = argument;
        }
    }
    if (!filesDirectory.empty())
    {
        sweepPokemonFiles(filesDirectory, "");
        return 0;
    }
    if (saveRoot.empty())
    {
        std::printf("usage: legality <save-root> [--verbose]\n  Expects, relative to <save-root>, any of:\n  One folder per TITLE, any filename inside, named as the self test names the\n  title (case, spaces and a leading \"Pokemon\" aside) -- Red/  Silver/  FireRed/\n  Emerald/  HeartGold/  White 2/  Alpha Sapphire/  Ultra Moon/  LetsGoEevee/\n  Sword/  ShiningPearl/  Violet/  Legends Z-A/  Legends Arceus/  and the rest.\n");
        return 2;
    }
    g_saveRoot = saveRoot;

    std::printf("PKSE legality harness\nReal saves are (almost) all legal Pokemon, so every finding here is a claim the\nchecker has to earn. Findings are grouped by shape; counts are what to watch.\n\n");

    // Found by TITLE FOLDER; see SaveTree.h.
    // EVERY TITLE FOLDER THE SELF TEST KNOWS, not a sample of them. A gate that sweeps one save per
    // generation passed while half the saves it had were never opened -- Silver, Yellow, both Let's
    // Go games and Sun/Moon among them, each with findings the console then reported.
    sweepLooseFile<Trainer::Trainer1RBY>("Red", "Red", SaveTree::red(saveRoot));
    sweepLooseFile<Trainer::Trainer1RBY>("Blue", "Blue", SaveTree::blue(saveRoot));
    sweepLooseFile<Trainer::Trainer1RBY>("Yellow", "Yellow", SaveTree::forTitle(saveRoot, "Yellow"));
    sweepLooseFile<Trainer::Trainer2GSC>("Gold", "Gold", SaveTree::forTitle(saveRoot, "Gold"));
    sweepLooseFile<Trainer::Trainer2GSC>("Silver", "Silver", SaveTree::forTitle(saveRoot, "Silver"));
    sweepLooseFile<Trainer::Trainer2GSC>("Crystal", "Crystal", SaveTree::forTitle(saveRoot, "Crystal"));
    sweepFRLG(SaveTree::fireRed(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Ruby", "Ruby", SaveTree::ruby(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Sapphire", "Sapphire", SaveTree::sapphire(saveRoot));
    sweepLooseFile<Trainer::Trainer3RSE>("Emerald", "Emerald", SaveTree::emerald(saveRoot));
    sweepLooseFile<Trainer::Trainer4DP>("Diamond", "Diamond", SaveTree::forTitle(saveRoot, "Diamond"));
    sweepLooseFile<Trainer::Trainer4DP>("Pearl", "Pearl", SaveTree::forTitle(saveRoot, "Pearl"));
    sweepLooseFile<Trainer::Trainer4PT>("Platinum", "Platinum", SaveTree::forTitle(saveRoot, "Platinum"));
    sweepLooseFile<Trainer::Trainer4HGSS>("HeartGold", "HeartGold", SaveTree::forTitle(saveRoot, "HeartGold"));
    sweepLooseFile<Trainer::Trainer4HGSS>("SoulSilver", "SoulSilver", SaveTree::forTitle(saveRoot, "SoulSilver"));
    sweepLooseFile<Trainer::Trainer5BW>("Black", "Black", SaveTree::forTitle(saveRoot, "Black"));
    sweepLooseFile<Trainer::Trainer5BW>("White", "White", SaveTree::forTitle(saveRoot, "White"));
    sweepLooseFile<Trainer::Trainer5B2W2>("Black 2", "Black 2", SaveTree::forTitle(saveRoot, "Black 2"));
    sweepLooseFile<Trainer::Trainer5B2W2>("White 2", "White 2", SaveTree::forTitle(saveRoot, "White 2"));
    sweepLooseFile<Trainer::Trainer6XY>("X", "X", SaveTree::forTitle(saveRoot, "X"));
    sweepLooseFile<Trainer::Trainer6XY>("Y", "Y", SaveTree::forTitle(saveRoot, "Y"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Omega Ruby", "Omega Ruby", SaveTree::forTitle(saveRoot, "Omega Ruby"));
    sweepLooseFile<Trainer::Trainer6ORAS>("Alpha Sapphire", "Alpha Sapphire", SaveTree::forTitle(saveRoot, "Alpha Sapphire"));
    sweepLooseFile<Trainer::Trainer7SM>("Sun", "Sun", SaveTree::forTitle(saveRoot, "Sun"));
    sweepLooseFile<Trainer::Trainer7SM>("Moon", "Moon", SaveTree::forTitle(saveRoot, "Moon"));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Sun", "Ultra Sun", SaveTree::ultraSun(saveRoot));
    sweepLooseFile<Trainer::Trainer7USUM>("Ultra Moon", "Ultra Moon", SaveTree::forTitle(saveRoot, "Ultra Moon"));
    sweepLetsGo("Let's Go Pikachu", SaveTree::letsGoPikachu(saveRoot));
    sweepLetsGo("Let's Go Eevee", SaveTree::forTitle(saveRoot, "Let's Go Eevee"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Sword", SaveTree::forTitle(saveRoot, "Sword"));
    sweepSCBlock<Trainer::Trainer8SWSH>("Shield", SaveTree::shield(saveRoot));
    sweepBDSP("Brilliant Diamond", SaveTree::forTitle(saveRoot, "Brilliant Diamond"));
    sweepBDSP("Shining Pearl", SaveTree::forTitle(saveRoot, "Shining Pearl"));
    sweepSCBlock<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(saveRoot));
    sweepSCBlock<Trainer::Trainer9SV>("Violet", SaveTree::violet(saveRoot));
    sweepSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(saveRoot));

    return 0;
}
