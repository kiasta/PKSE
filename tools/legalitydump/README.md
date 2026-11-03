# legalitydump: PKHeX's own legality verdicts, dumped as JSON lines

PKSE is porting PKHeX's legality checks. A port can only be shown to agree with PKHeX by running
both over the same Pokemon and comparing every check. This tool is the PKHeX half of that parity
harness: it runs the real `LegalityAnalysis` and writes one JSON object per Pokemon per line. A C++
harness writes the same shape from PKSE's analyzer, and a script diffs the two per Pokemon and per
check.

Like `tools/formdump`, it needs the .NET SDK, because PKHeX.Core has to be compiled. Point
`PKHexCore` at a `PKHeX.Core.csproj`: a checkout (what `PKHEX_LOCAL` names for the generators), or
the downloaded source under `tools/.pkhex_cache/<sha>/_repo/`.

    CORE=~/repos/PKHeX/PKHeX.Core/PKHeX.Core.csproj
    dotnet run -c Release --project tools/legalitydump -p:PKHexCore=$CORE -- \
        saves Emerald=path/to/emerald.sav "Ultra Sun=path/to/main" [--out saves.jsonl]
    dotnet run -c Release --project tools/legalitydump -p:PKHexCore=$CORE -- \
        files ~/repos/PKHeX/Tests [--out files.jsonl]

`saves` opens each file with `SaveUtil.TryGetSaveFile` (PKHeX's GUI entry point, emulator
envelopes included), then does what the GUI's `OpenSAV` does before any analysis:
`EReaderBerrySettings.LoadFrom` for Gen 3 and `ParseSettings.InitFromSaveFileData`. That sets the
active trainer (handler checks) and the cartridge/Virtual Console era flags. A raw Game Boy save
therefore counts as cartridge era. Each slot is analysed the way the slot's right-click Legality
report does it, with the save's personal table and `StorageSlotType.Party`/`Box`. Slots are
`party:<index>` and `box:<box>:<slot>`, zero-based, and empty slots are skipped. A save PKHeX
cannot load gives `{"title":...,"error":...}` and the run continues.

`files` loads every `.pk*/.ek*/.pb*/.pa*` file under the directory with `FileUtil.TryGetPKM`,
with no active trainer, as PKHeX's own legality tests do. The slot is the relative path and the
title is `file`.

## Schema (one line per Pokemon)

    {"title", "slot", "species", "form", "format" (EntityContext), "version" (GameVersion),
     "pid", "ec", "isEgg", "valid", "parsed",
     "encounter" (EncounterOriginal.LongName or null), "encounterType" (its C# type name or null),
     "results": [ ... ]}

Each result:

    {"source": "check" | "move", "identifier", "code", "severity": "Invalid"|"Fishy"|"Valid",
     "argument", "argument2", "value", "message"}

- `source: "check"` is one `CheckResult` from `LegalityAnalysis.Results`, Valid ones included.
  `identifier` is the `CheckIdentifier`, `code` the `LegalityCheckResultCode`, and `argument`,
  `argument2` and `value` are its raw hint fields. `message` is `LegalityLocalizationContext.Humanize`
  in English, the exact line PKHeX's report shows (`"Invalid: ..."`).
- `source: "move"` is one per-slot `MoveResult` from `Info.Moves` (`identifier: "CurrentMove"`), or
  from `Info.Relearn` (`"RelearnMove"`, only when Format >= 6, as in PKHeX's report). `code` is the
  `LearnMethod`, `argument` the zero-based move slot, `argument2` the move id, `value` the expected
  move (`Expect`, for `UnobtainableExpect`), and `message` is `FormatMove`/`FormatRelearn`.
  There is one extra field, `environment` (the `LearnEnvironment`). PKHeX's `Results` also holds
  `CurrentMove`/`RelearnMove` check lines (PP, Plus moves), and `source` keeps the two kinds apart.
