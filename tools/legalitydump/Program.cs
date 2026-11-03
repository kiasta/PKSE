// Runs PKHeX's real legality analysis over every Pokemon in a set of saves or entity files and
// writes one JSON object per Pokemon per line. It is the oracle half of the legality parity
// harness: PKSE's own analyzer emits the same shape, and a script diffs the two per check.
using System.Text;
using System.Text.Json;
using PKHeX.Core;

var arguments = new List<string>(args);
string outputPath = null;
int outIndex = arguments.IndexOf("--out");
if (outIndex >= 0)
{
    if (outIndex + 1 >= arguments.Count)
        return Usage();
    outputPath = arguments[outIndex + 1];
    arguments.RemoveRange(outIndex, 2);
}
if (arguments.Count < 2)
    return Usage();

var localization = LegalityLocalizationSet.GetLocalization("en");

using var output = outputPath is null
    ? new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false))
    : new StreamWriter(outputPath, false, new UTF8Encoding(false));
output.AutoFlush = outputPath is null;

var jsonOptions = new JsonWriterOptions { Indented = false };

// pidiv: MethodFinder's PID/IV correlation per Pokemon instead of the legality report, for the
// RNG port's exact comparison (tools/harness/pidiv_diff.py).
bool pidivMode = arguments[0] == "pidiv";
if (pidivMode)
{
    arguments.RemoveAt(0);
    if (arguments.Count < 2)
        return Usage();
}

// evolution: PKHeX's EvolutionChain.GetEvolutionChainsAllGens and EncounterOrigin chain per Pokemon,
// for the evolution engine port's exact comparison (tools/harness/evolution_diff.py).
bool evolutionMode = arguments[0] == "evolution";
if (evolutionMode)
{
    arguments.RemoveAt(0);
    if (arguments.Count < 2)
        return Usage();
}

switch (arguments[0])
{
    case "saves":
        foreach (var titleAndPath in arguments.Skip(1))
            DumpSave(titleAndPath);
        break;
    case "files":
        DumpFiles(arguments[1]);
        break;
    default:
        return Usage();
}
return 0;

static int Usage()
{
    Console.Error.WriteLine("usage: legalitydump saves <title>=<path> [<title>=<path> ...] [--out file.jsonl]");
    Console.Error.WriteLine("       legalitydump files <directory> [--out file.jsonl]");
    Console.Error.WriteLine("       legalitydump pidiv saves|files ... [--out file.jsonl]");
    Console.Error.WriteLine("       legalitydump evolution saves|files ... [--out file.jsonl]");
    return 2;
}

void DumpSave(string titleAndPath)
{
    int separator = titleAndPath.IndexOf('=');
    if (separator <= 0)
    {
        WriteError(titleAndPath, "argument is not <title>=<path>");
        return;
    }
    var title = titleAndPath[..separator];
    var path = titleAndPath[(separator + 1)..];
    if (!File.Exists(path))
    {
        WriteError(title, $"file not found: {path}");
        return;
    }
    // The same entry point PKHeX's GUI opens a file with: custom readers, the plain formats, then
    // the envelope handlers (emulator footers/headers, DeSmuME, etc.).
    if (!SaveUtil.TryGetSaveFile(path, out var save))
    {
        WriteError(title, "PKHeX could not recognise the save");
        return;
    }
    if (!save.IsVersionValid())
    {
        WriteError(title, $"PKHeX loaded the save but its version is not valid ({save.Version})");
        return;
    }

    // What PKHeX's main window does on load (Main.OpenSAV): e-Reader berry, then the active trainer
    // and the cartridge/virtual console era flags that trainer-dependent checks read.
    if (save is SAV3 save3)
        EReaderBerrySettings.LoadFrom(save3);
    ParseSettings.InitFromSaveFileData(save);

    if (save.HasParty)
    {
        for (int partyIndex = 0; partyIndex < save.PartyCount; partyIndex++)
        {
            var pokemon = save.GetPartySlotAtIndex(partyIndex);
            if (pokemon.Species == 0)
                continue;
            DumpPokemon(title, $"party:{partyIndex}", pokemon, save.Personal, StorageSlotType.Party);
        }
    }
    if (save.HasBox)
    {
        for (int boxIndex = 0; boxIndex < save.BoxCount; boxIndex++)
        {
            for (int slotIndex = 0; slotIndex < save.BoxSlotCount; slotIndex++)
            {
                var pokemon = save.GetBoxSlotAtIndex(boxIndex, slotIndex);
                if (pokemon.Species == 0)
                    continue;
                DumpPokemon(title, $"box:{boxIndex}:{slotIndex}", pokemon, save.Personal, StorageSlotType.Box);
            }
        }
    }
    output.Flush();
}

void DumpFiles(string directory)
{
    // A loose file has no save behind it, as when PKHeX's legality tests run it: no active trainer.
    ParseSettings.ClearActiveTrainer();
    var root = Path.GetFullPath(directory);
    var files = Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
        .Where(IsEntityFile)
        .OrderBy(file => file, StringComparer.Ordinal);
    foreach (var file in files)
    {
        var relative = Path.GetRelativePath(root, file);
        var data = File.ReadAllBytes(file);
        if (!EntityDetection.IsSizePlausible(data.Length)
            || !FileUtil.TryGetPKM(data, out var pokemon, Path.GetExtension(file)))
        {
            WriteError("file", "PKHeX could not load the entity file", relative);
            continue;
        }
        DumpPokemon("file", relative, pokemon, pokemon.PersonalInfo, StorageSlotType.None);
    }
    output.Flush();
}

static bool IsEntityFile(string path)
{
    var extension = Path.GetExtension(path).ToLowerInvariant();
    return extension.Length >= 3
        && (extension.StartsWith(".pk") || extension.StartsWith(".ek")
            || extension.StartsWith(".pb") || extension.StartsWith(".pa"));
}

void DumpPokemon(string title, string slot, PKM pokemon, object personal, StorageSlotType slotType)
{
    if (pidivMode)
    {
        DumpPIDIV(title, slot, pokemon);
        return;
    }

    // The slot's right-click Legality report in PKHeX's GUI (ContextMenuSAV) analyses with the save's
    // personal table and the slot's storage type; a loose file uses the entity's own personal info.
    var analysis = personal switch
    {
        IPersonalTable table => new LegalityAnalysis(pokemon, table, slotType),
        IPersonalInfo info => new LegalityAnalysis(pokemon, info, slotType),
        _ => new LegalityAnalysis(pokemon, slotType),
    };
    if (evolutionMode)
    {
        DumpEvolution(title, slot, pokemon, analysis);
        return;
    }
    var context = LegalityLocalizationContext.Create(analysis, localization);

    using var buffer = new MemoryStream();
    using (var writer = new Utf8JsonWriter(buffer, jsonOptions))
    {
        writer.WriteStartObject();
        writer.WriteString("title", title);
        writer.WriteString("slot", slot);
        writer.WriteNumber("species", pokemon.Species);
        writer.WriteNumber("form", pokemon.Form);
        writer.WriteString("format", pokemon.Context.ToString());
        writer.WriteString("version", pokemon.Version.ToString());
        writer.WriteNumber("pid", pokemon.PID);
        writer.WriteNumber("ec", pokemon.EncryptionConstant);
        writer.WriteBoolean("isEgg", pokemon.IsEgg);
        writer.WriteBoolean("valid", analysis.Valid);
        writer.WriteBoolean("parsed", analysis.Parsed);

        var encounter = analysis.Parsed ? analysis.EncounterOriginal : null;
        if (encounter is null)
        {
            writer.WriteNull("encounter");
            writer.WriteNull("encounterType");
        }
        else
        {
            writer.WriteString("encounter", encounter.LongName);
            writer.WriteString("encounterType", encounter.GetType().Name);
        }

        writer.WriteStartArray("results");
        foreach (var result in analysis.Results)
        {
            writer.WriteStartObject();
            writer.WriteString("source", "check");
            writer.WriteString("identifier", result.Identifier.ToString());
            writer.WriteString("code", result.Result.ToString());
            writer.WriteString("severity", result.Judgement.ToString());
            writer.WriteNumber("argument", result.Argument);
            writer.WriteNumber("argument2", result.Argument2);
            writer.WriteNumber("value", result.Value);
            writer.WriteString("message", context.Humanize(result));
            writer.WriteEndObject();
        }

        // Per-move verdicts live in Info.Moves / Info.Relearn, not in Results. PKHeX's report
        // (LegalityFormatting.AddMoves/AddRelearn) prints every current move, and relearn moves only
        // for Format >= 6; the dump keeps that rule so the line sets match the GUI report.
        var info = analysis.Info;
        Span<ushort> currentMoves = stackalloc ushort[4];
        pokemon.GetMoves(currentMoves);
        WriteMoves(writer, context, "CurrentMove", info.Moves, currentMoves, pokemon.Context, false);
        if (pokemon.Format >= 6)
        {
            Span<ushort> relearnMoves = stackalloc ushort[4];
            pokemon.GetRelearnMoves(relearnMoves);
            WriteMoves(writer, context, "RelearnMove", info.Relearn, relearnMoves, pokemon.Context, true);
        }
        writer.WriteEndArray();
        writer.WriteEndObject();
    }
    output.WriteLine(Encoding.UTF8.GetString(buffer.ToArray()));
}

void DumpEvolution(string title, string slot, PKM pokemon, LegalityAnalysis analysis)
{
    // The encounter LegalInfo.EvoChainsAllGens is built from, and the history recomputed from it so
    // the comparison never depends on LegalInfo's cache.
    var encounter = analysis.Info.EncounterMatch;
    var history = EvolutionChain.GetEvolutionChainsAllGens(pokemon, encounter);
    using var buffer = new MemoryStream();
    using (var writer = new Utf8JsonWriter(buffer, jsonOptions))
    {
        writer.WriteStartObject();
        writer.WriteString("title", title);
        writer.WriteString("slot", slot);
        writer.WriteNumber("species", pokemon.Species);
        writer.WriteNumber("form", pokemon.Form);
        writer.WriteStartObject("encounter");
        writer.WriteNumber("species", encounter.Species);
        writer.WriteString("context", encounter.Context.ToString());
        writer.WriteNumber("generation", encounter.Generation);
        writer.WriteNumber("levelMin", encounter.LevelMin);
        writer.WriteBoolean("invalid", encounter is EncounterInvalid);
        writer.WriteEndObject();
        writer.WriteStartObject("history");
        foreach (var historyContext in EvolutionContexts)
            WriteChain(writer, historyContext.ToString(), history.Get(historyContext));
        writer.WriteEndObject();

        // EncounterOrigin's chain for the Pokemon's own generation and version context: what an
        // encounter generator matches against, with no encounter chosen yet.
        var generation = pokemon.Generation;
        var originContext = pokemon.Format <= 2 ? pokemon.Context : pokemon.Version.GetContextFromSaved();
        if (generation == 0 || originContext == 0)
        {
            writer.WriteNull("origin");
        }
        else
        {
            var origin = pokemon.Format <= 2
                ? EncounterOrigin.GetOriginChain12(pokemon, generation, originContext)
                : EncounterOrigin.GetOriginChain(pokemon, generation, originContext);
            writer.WriteStartObject("origin");
            writer.WriteNumber("generation", generation);
            writer.WriteString("context", originContext.ToString());
            WriteChain(writer, "chain", origin);
            writer.WriteEndObject();
        }
        writer.WriteEndObject();
    }
    output.WriteLine(Encoding.UTF8.GetString(buffer.ToArray()));
}

static void WriteChain(Utf8JsonWriter writer, string name, ReadOnlySpan<EvoCriteria> chain)
{
    writer.WriteStartArray(name);
    foreach (var stage in chain)
    {
        writer.WriteStartArray();
        writer.WriteNumberValue(stage.Species);
        writer.WriteNumberValue(stage.Form);
        writer.WriteNumberValue(stage.LevelMin);
        writer.WriteNumberValue(stage.LevelMax);
        writer.WriteStringValue(stage.Method.ToString());
        writer.WriteEndArray();
    }
    writer.WriteEndArray();
}

void DumpPIDIV(string title, string slot, PKM pokemon)
{
    var pidiv = MethodFinder.Analyze(pokemon);
    using var buffer = new MemoryStream();
    using (var writer = new Utf8JsonWriter(buffer, jsonOptions))
    {
        writer.WriteStartObject();
        writer.WriteString("title", title);
        writer.WriteString("slot", slot);
        writer.WriteNumber("species", pokemon.Species);
        writer.WriteNumber("pid", pokemon.PID);
        writer.WriteNumber("ec", pokemon.EncryptionConstant);
        writer.WriteStartArray("ivs");
        for (int statIndex = 0; statIndex < 6; statIndex++)
            writer.WriteNumberValue(pokemon.GetIV(statIndex));
        writer.WriteEndArray();
        writer.WriteNumber("format", pokemon.Format);
        writer.WriteNumber("version", (int)pokemon.Version);
        writer.WriteString("pidType", pidiv.Type.ToString());
        writer.WriteNumber("seed", pidiv.OriginSeed);
        writer.WriteEndObject();
    }
    output.WriteLine(Encoding.UTF8.GetString(buffer.ToArray()));
}

static void WriteMoves(Utf8JsonWriter writer, in LegalityLocalizationContext context, string identifier,
                       ReadOnlySpan<MoveResult> moveResults, ReadOnlySpan<ushort> moveIds,
                       EntityContext currentContext, bool isRelearn)
{
    for (int moveIndex = 0; moveIndex < moveResults.Length; moveIndex++)
    {
        var move = moveResults[moveIndex];
        writer.WriteStartObject();
        // PKHeX's Results ALSO hold CheckIdentifier.CurrentMove/RelearnMove lines (PP, Plus moves,
        // etc.), so "source" separates a per-slot MoveResult from those.
        writer.WriteString("source", "move");
        writer.WriteString("identifier", identifier);
        writer.WriteString("code", move.Info.Method.ToString());
        writer.WriteString("severity", move.Judgement.ToString());
        writer.WriteNumber("argument", moveIndex);
        writer.WriteNumber("argument2", moveIndex < moveIds.Length ? moveIds[moveIndex] : 0);
        writer.WriteNumber("value", (uint)move.Expect);
        writer.WriteString("message", isRelearn
            ? context.FormatRelearn(move, moveIndex + 1)
            : context.FormatMove(move, moveIndex + 1, currentContext));
        writer.WriteString("environment", move.Info.Environment.ToString());
        writer.WriteEndObject();
    }
}

void WriteError(string title, string error, string slot = null)
{
    using var buffer = new MemoryStream();
    using (var writer = new Utf8JsonWriter(buffer, jsonOptions))
    {
        writer.WriteStartObject();
        writer.WriteString("title", title);
        if (slot is not null)
            writer.WriteString("slot", slot);
        writer.WriteString("error", error);
        writer.WriteEndObject();
    }
    output.WriteLine(Encoding.UTF8.GetString(buffer.ToArray()));
    output.Flush();
}

partial class Program
{
    static readonly EntityContext[] EvolutionContexts =
    [
        EntityContext.Gen1, EntityContext.Gen2, EntityContext.Gen3, EntityContext.Gen4, EntityContext.Gen5,
        EntityContext.Gen6, EntityContext.Gen7, EntityContext.Gen8, EntityContext.Gen9,
        EntityContext.Gen7b, EntityContext.Gen8a, EntityContext.Gen8b, EntityContext.Gen9a,
    ];
}
