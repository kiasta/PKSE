# 07 — Data tables

## The single most important rule in this chapter

> **Data-table gaps are the most common source of wrong behaviour in this codebase, and they fail
> quietly.** A missing table entry means *"unknown"*, but callers have historically read it as a
> definitive *negative* — a legality check concluding **illegal**, a transfer sanitizer **deleting a
> move** — when the truth was simply that no data was loaded.

Two rules follow:

1. **When adding a lookup, decide explicitly what absence means, and handle it separately from a
   real negative.**
2. **Never leave a "simplified" / "approximate" placeholder in a getter once the real table
   exists.** A `cbrt()` level approximation outlived its table for months and produced wrong stats
   plus bogus legality errors across a whole generation.

## What is generated, and from what

Everything below is **derived from PKHeX** by a script in `tools/`. **Do not hand-edit the output.**
Rerun the generator.

| Output | Generator | Contents |
|---|---|---|
| `Names/SpeciesNames.cpp` | `gen_speciesnames.py` | Species names, 0–1025 |
| `Names/MoveNames.cpp` | `gen_movenames.py` | Move names |
| `Names/MoveInfo.cpp` | `gen_moveinfo.py` | Per-move type / category / **per-game base PP** |
| `Names/MovePresence.cpp` | `gen_movepresence.py` | Which moves exist in which game |
| `Names/AbilityNames.cpp`, `NatureNames.cpp`, `TypeNames.cpp` | `gen_simplenames.py` | Ability, nature and type names |
| `Names/ItemNames.cpp` | `gen_itemnames.py` | Item names, every generation's id space (plus the `Item3to4` map) |
| `Names/ItemPouches.cpp` | `gen_itempouches.py` | Which item ids legally belong in each pouch of each game |
| `Names/ItemPresence.cpp` | `gen_itempresence.py` | Which items a Pokémon may **hold**, per game |
| `Names/LocationNames.cpp` | `gen_locations.py` | Met-location names, per origin version |
| `Names/FormNames.cpp` | — (hand-curated) | Form display names, **English** |
| `Names/FormNamesLocalized.cpp` | `gen_formnames.py` (+ `tools/formdump/`) | Form display names, the other eight languages |
| `Names/RibbonNames.cpp` | — (hand-transcribed from PKHeX) | Ribbon/mark names, **English**, + per-format bit offsets |
| `Names/RibbonNamesLocalized.cpp` | `gen_ribbonnames.py` | Ribbon/mark names, the other eight languages |
| `Names/TMMoves.cpp` | — (extracted from PKHeX) | Which move a TM/HM/TR item teaches, per game |
| `Pokemon/PersonalInfo1RBY.cpp` … `PersonalInfo9LZA.cpp` (19) | `gen_personaltables.py` | **Per-species data, one file per save-format group** — stats, types, abilities, gender ratio, friendship, growth rate, catch rate, forms |
| `Pokemon/PersonalInfoTable.cpp` | `gen_personal.py` | The CROSS-GAME index only: per-game dex presence, base height/weight, the union form count |
| `Pokemon/LearnsetTable.cpp` | `gen_learnsets.py` | Per-game learnable-move bitsets (~110 k lines) |
| `Pokemon/Experience.cpp` | — (mirrors PKHeX `Experience.cs`) | The six growth-rate EXP tables |
| `Pokemon/EvolutionTable.cpp` | `gen_evolutions.py` | Evolution chains (used to walk pre-evolutions) |
| `Pokemon/DexTable8SWSH.cpp`, `DexTable9SV.cpp`, `DexTable8LA.cpp` | `gen_dextable8swsh.py`, `gen_dextable9sv.py`, `gen_dextable8la.py` | Whole-dex membership, for the games whose dex is not the National Dex |
| `Pokemon/DexTable6XY.cpp`, `6ORAS`, `7SM`, `7USUM`, `7LGPE` | `gen_dexformtables.py` | Pokédex alternate-FORM bit index, for the games whose form region is arbitrarily ordered |
| `Pokemon/Gen1Tables.cpp` | `gen_gen1.py` | What is Gen 1's ALONE: the internal species index, ROM type numbering, its own move PP and item ids |
| `Pokemon/Gen2Tables.cpp` | `gen_personaltables.py` | Gen 2's own item id space |
| `Trainer/Blocks5BW.h` … `Blocks7USUM.h` (6) | `gen_blocktables.py` | The fixed block tables of the six block-list saves |
| `Utils/Gen2Text.h`, `Utils/Gen4Text.h` | `gen_gen2text.py`, `gen_gen4text.py` | Gen 2 and Gen 4 character tables |
| `Legality/EncounterTable.cpp` | `gen_encounters.py` | Every encounter template, per game (~53 k lines) |
| `Pokemon/FormSpriteMapping.cpp` | — (hand-maintained) | Form → PokeAPI sprite id / name stem |
| `romfs/sprites/pokemon_hd/` | `gen_hdsprites.py` | The 3260 sprite PNGs |
| `romfs/sprites/types/` | `gen_typeicons.py` | The 19 type icons — 18 plus Stellar, which is Tera-only |
| `romfs/sprites/marks/` | `gen_marks.py` | The origin markings, from PKHeX's `Markings` |
| `romfs/fonts/` | `gen_fonts.py` | Nunito + both Noto Sans Symbols faces (SIL OFL) |

Rows marked "—" are hand-maintained; everything else is generated output and is never edited by
hand.

**The generators are not part of the build.** `make` only compiles the committed output. They run
occasionally, by hand, when the PKHeX-derived data changes (a new game, DLC, an upstream
correction). `tools/regenerate.py` runs all of them in one go (`--list` says what would run,
`--tables` / `--assets` pick a half, `--ref <sha>` pins `PKHEX_REF`), hashes every file each one
rewrites, and lists the ones that changed.

**PKHeX is the ONLY data source** — nothing is transcribed from pokemondb or any other site.

## Running a generator

```bash
python tools/gen_personal.py
python tools/gen_learnsets.py
python tools/gen_encounters.py
python tools/regenerate.py --tables      # or every table generator at once
# … then REVIEW THE DIFF before committing.
```

**No local PKHeX checkout is needed, by any generator.** `tools/pkhex_source.py` fetches each
resource from GitHub on demand (`pkhex_path()`) and caches it under `tools/.pkhex_cache/`
(gitignored), so re-runs are offline and fast. The one generator that has to BUILD PKHeX.Core rather
than read a file out of it (`gen_formnames.py`, through `tools/formdump/`) gets the whole source the
same way: `pkhex_repo()` downloads a tarball at the resolved ref and caches it beside the per-file
cache.

**The generators track PKHeX's default branch rather than a pinned commit** (`_DEFAULT_REF =
"master"`), because an upstream data correction is something PKSE wants and the regenerated diff is
the review gate either way — a pin only delays adopting a fix and makes "regenerate" two decisions
instead of one. The branch is resolved to a commit SHA **once per run** and the cache keyed by that
SHA, so a run is internally consistent and the cache cannot serve a stale "master" forever. The
resolved commit is printed (`pkhex_ref()`) rather than stamped into most outputs, so a regeneration
that changes no data produces no diff; `FormNamesLocalized.cpp` is the exception and records the
commit its `FormConverter` dump came from.

| Env var | Effect |
|---|---|
| `PKHEX_REF` | Git ref to fetch (commit / tag / branch). Set to a SHA to reproduce an older table exactly. |
| `PKHEX_LOCAL` | Path to a local PKHeX checkout. Nothing is downloaded. A shortcut for someone who already has one, never a precondition. |

A 404 means the file moved or was renamed on the chosen ref — pin `PKHEX_REF` to a ref that still
has it, or point `PKHEX_LOCAL` at a checkout.

**The diff is the review gate.** Committing regenerated tables without reading the diff is how an
upstream change silently alters PKSE's behaviour. Two habits make the diff readable:

- **When the change is PKSE's, regenerate at the commit that reproduces today's tables**
  (`PKHEX_REF=<sha>`), so upstream data does not ride along buried in your change.
- **Compare a table SEMANTICALLY, not by line count.** Index shifts move thousands of lines without
  changing one answer; the question is which answers changed.

**A generator nobody has run in a while may not run at all.** `gen_evolutions.py` still built its
personal tables the pre-Gen-1/2 way and had been throwing `KeyError: 'GSC'` since those groups joined
`GAMES_TO_EMIT`, which is why `EvolutionTable.cpp` carried no Gen 1, 2, 4, 5, 6 or 7 table at all.
When adding a group, run every generator whose game list you touched, not just the one you came
for — and diff the output, because a table that changes where you did not expect it is the
finding.

## Name tables keep PKHeX's punctuation verbatim

`Ho-Oh`, `Farfetch'd`, `Type: Null`, `Flabébé`, `Nidoran♀`. The UI font loads Noto Sans Symbols as a
NanoVG fallback and can draw all of it. A sanitized species table shipped as a real bug, including
in the Gen 3 nickname it feeds.

## Form ID is required for correct lookups

Regional variants have different stats, abilities and learnsets. **Pass `form` alongside `species`
rather than defaulting it.**

```cpp
const Pokemon::PersonalInfo& pi = Pokemon::getPersonalInfo(species, form);
bool ok = Pokemon::isLearnable(species, form, group, moveId);
```

## `PersonalInfoTable` — the workhorse

```cpp
struct PersonalInfo {
    uint16_t ability1, ability2, abilityHidden;
    uint16_t formIndex;       // table index of this species' form 1 (0 = no alt forms)
    uint8_t  genderRatio;     // 0 = always male, 254 = always female, 255 = genderless,
                              // else a threshold: PID gender-value < ratio means female
    uint8_t  baseFriendship;
    uint8_t  formCount;       // forms across supported games; valid forms are 0..formCount-1
    uint8_t  presence;        // OR of PersonalGameBit
    uint8_t  type1, type2;    // Gen 6+ type ids; type2 == TYPE_NONE (255) when single-typed
    uint16_t height, weight;  // base, for the LGPE absolute size calc
};

enum PersonalGameBit : uint8_t {
    PERSONAL_GAME_GG = 1<<0, PERSONAL_GAME_SWSH = 1<<1, PERSONAL_GAME_BDSP = 1<<2,
    PERSONAL_GAME_PLA = 1<<3, PERSONAL_GAME_SV = 1<<4, PERSONAL_GAME_ZA = 1<<5,
};

const PersonalInfo& getPersonalInfo(uint16_t species, uint8_t form);
```

The table is flat: indices `0..1025` are the form-0 rows, `1026..` are alternate-form rows that a
base row's `formIndex` redirects into — mirroring PKHeX's `FormIndex` redirection. Out-of-range
species or forms fall back to the species' form-0 row.

`presence` (an OR of `PersonalGameBit`) is what conversion, Layer 2 legality and the creator's
species filter test to decide what a given game may hold.

**PKHeX stores "no second ability" as `ability2 == ability1`, not `0`.**

### Every group gets its own personal table

Nineteen of them, `PersonalInfo1RBY` … `PersonalInfo9LZA`, each generated from the single PKHeX
resource that belongs to it — `personal_rb`, `personal_c`, `personal_rs`, … `personal_za`. Ask
`getPersonalRecord(group, species, form)`; the nineteen-way switch lives once, in
`PersonalRecord.cpp`.

**This is not tidiness. The tables genuinely disagree, and sharing one hid it.** Before the split,
one Gen 4 table served Diamond/Pearl, Platinum *and* HeartGold/SoulSilver, one Gen 5 table served
Black/White, and five Switch groups shared a single base-stat file — while `personal_gg` and
`personal_za` were never opened at all. Some of what that cost:

- **Base stats move between generations.** 42 of 386 species differ in Gen 3 alone, nearly all from
  the Gen 6 stat pass: Pikachu is 30 Def / 40 SpD in Gen 3 and 40/50 in Sword. This is not cosmetic —
  PKSE **writes the six computed battle stats into a party record**, so a wrong base stat is saved
  into the game.
- **And within Gen 8/9.** Cresselia's Def/SpD went 120/130 → 110/120 in Gen 9 and Zacian's Attack
  130 → 120; a single modern table gave Sword/Shield, BDSP and Legends: Arceus the *new* numbers.
  Legends: Z-A raises Meditite and Medicham's Attack, which no shared table can express at all.
- **Abilities are per-game.** The S/V slot pair disagrees with Gen 3 for 101 species and always
  carries a hidden ability Gen 3 has no slot for; S/V also moved the Piplup line's hidden ability and
  re-slotted most of Legends: Arceus's Hisuian forms.
- **Types are per-game.** Gen 2 adds Steel and Dark, Gen 6 adds Fairy. Clefairy is Normal in
  Red/Blue, still Normal in Black/White, and Fairy only from X/Y — reading the modern table retypes
  a Pokémon into something its own game has no concept of.

Fields a generation does not have read 0, and 0 is a real ability id and a real friendship value, so
ask the predicates in `PersonalRecord.h` (`personalHasAbilities`, `personalHasHiddenAbility`,
`personalHasForms`, `personalHasSplitSpecial`) rather than reading them as data — the same reason
`Pokemon::hasNature()` and `hasBall()` exist one layer up.

**Two things the tables do NOT answer.** Dex presence is not readable from an empty row: Sword/Shield
and Legends: Arceus zero the ids their game lacks, but Let's Go and Scarlet/Violet carry full stats
for every id regardless, so `PersonalInfoTable`'s `presence` bits remain the cross-game answer. And
Gen 1 has **one Special**, written into both `spa` and `spd`: reading either gives the right number,
but they cannot differ (`personalHasSplitSpecial`).

**Deoxys (#386)** is the one Gen 3 caveat that survives: its forme is chosen by the *game* and not
stored in the entity, so FireRed and LeafGreen disagree on that row alone. `PersonalInfo3FRLG`
carries FireRed's (Attack Forme).

Field order in a row is PKHeX's personal-table order — HP, ATK, DEF, SPE, SPA, SPD. **Access by
name, never by position.**

## `LearnsetTable`

```cpp
constexpr uint16_t LEARN_MAX_SPECIES  = 1025;
constexpr uint16_t LEARN_MAX_MOVE_ID  = 920;   // ZA NihilLight
constexpr size_t   LEARN_BITSET_BYTES = 116;   // uniform width for every game

const uint8_t* getLearnableBits(uint16_t species, uint8_t form, Enums::GameVersion group);
bool isLearnable(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t moveId);
```

A set bit means the species+form **can legally know** that move in that game: the union of
level-up / egg / TM / TR / tutor / reminder, then unioned with its whole **pre-evolution chain** (an
evolved Pokémon keeps what it learned earlier).

`getLearnableBits` returns **`nullptr` when the species+form is absent from that game**. That is
"unknown", not "knows nothing" — handle it explicitly.

### A cosmetic form is present without a stats entry of its own

Rows are keyed on a universal form index, and a form only gets one if the game counts it PRESENT —
and until `load_cosmetic_forms` existed, `gen_learnsets.Personal.present()` counted only forms with
their own stats entry (`HasForm`). Unown's letters, Burmy's cloaks, Shellos' seas, Deerling's seasons,
Scatterbug's patterns and the Flabébé line's flowers share their species' entry, so in Gens 4-7 they
were all absent. PKHeX lists them by hand, per table, in `PersonalTable*.IsPresentInGame`'s species
switch; the generator now parses those switches (and refuses an arm in a shape it does not know,
because reading one as "no forms" is the silent failure being removed).

Two tables changed, and both diffs were checked semantically rather than by line count, since index
shifts move thousands of lines without changing a single answer:

- **`EvolutionTable`: 0 edges removed, 229 added**, every one a cosmetic-form edge PKHeX's
  `EvolutionReverseSpecies` has — Burmy to Wormadam and Mothim (Gens 4-7), Shellos to Gastrodon
  (Gens 4-6), Deerling to Sawsbuck (Gen 5 on), and Scatterbug to Spewpa to Vivillon and the Flabébé
  line (Gens 6-7). Without them a Blue Flower Florges walked down to a RED Flabébé, and one met on X/Y's
  Route 7 — where blue ones are — read as a species never there.
- **`LearnsetTable`: 0 rows removed, 797 added, 16 changed — and every change is a gain.** The added
  rows are exactly PKHeX's cosmetic lists; the changed ones are Floette's flowers inheriting
  Flabébé's moves through the new edges. **Those 797 forms' moves had never been checked**: Layer 2
  reads a `nullptr` row as "unknown", correctly, and so said nothing about any of them.

The reader was fixed alongside: **a Gen 1-6 evolution binary is addressed BY SPECIES for every form**
(`EVO_SPECIES_INDEXED`, moved into `gen_learnsets.py` beside the reader that needs it), as
`EvolutionReverseSpecies` walks it. Addressing it by form index sent Floette's own alternate forms,
which do have entries, past the end of the file.

## Encounter tables (legality Layer 3)

`include/Legality/EncounterTable.h`, generated by `gen_encounters.py`. One flat row per template:

```cpp
struct EncounterRow {
    uint16_t species, location, eggLocation;
    uint8_t  form;          // ENCOUNTER_FORM_ANY (0xFF) when the slot randomises it
    uint8_t  levelMin, levelMax;
    uint8_t  versions;      // one bit per game in the group; two bits everywhere except
                            // Ruby/Sapphire/Emerald, the only group of three
    uint8_t  kind;          // WILD / STATIC / GIFT / TRADE / RAID / EVENT (Mystery Gift)
    uint8_t  shiny;         // RANDOM / NEVER (locked) / ALWAYS (forced)
    uint8_t  gender;        // 0 male, 1 female, 2 genderless, ENCOUNTER_GENDER_ANY
    uint8_t  fixedBall;     // 0 = the encounter does not fix the ball
    uint8_t  flawlessIVs;   // guaranteed count of 31 IVs
    uint8_t  nature;        // ENCOUNTER_NATURE_ANY unless forced
    uint8_t  flags;         // FATEFUL / ALPHA / BOOST60 / EGG / MET_LEVEL_ZERO
};

const EncounterTable* getEncounterTable(Enums::GameVersion versionOrGroup);   // nullptr if none
```

Design notes that matter if you extend it (the readers and the per-generation traps are in
chapter 11):

- **New groups are APPENDED to `gen_encounters.py`'s `GAMES` list, never inserted.** Tables are
  emitted in that order, so inserting one rewrites every table after it and buries the real change
  in a diff that is the only review gate the generated data has.

- **Wild and raid rows are MERGED** per (species, form, location, constraints): the level span is
  the union and `flawlessIVs` takes the minimum. Merging is permissive by construction — it can
  widen what the table accepts, never narrow it. Static / gift / trade rows are **never** merged;
  each is a distinct template whose fixed data must stay exact.
- **The ability slot is deliberately absent.** Ability Capsule and Ability Patch let a legitimate
  Pokémon hold a slot its encounter never offered, so the column would only enable a check that
  false-flags real saves. Layer 2 already verifies the ability against the species.
- **Region sentinels**: `ENCOUNTER_LOCATION_SWSH_WILDAREA{,_ISLE_OF_ARMOR,_ALL}` stand for a whole region —
  SW/SH distribution raids, where the den is anonymous — and `ENCOUNTER_LOCATION_ROAMER2` through
  `ROAMER5` stand for the routes a roamer can be cornered on. Resolve them with
  `encounterLocationMatches()`.
- **`ENCOUNTER_LOCATION_EVENT3` (255) is NOT a sentinel** — it is the real Gen 3 location everything
  that reached a cartridge from outside it is met at: every distribution, and Pokémon Colosseum's gifts.
- **`ENCOUNTER_FLAG_MET_LEVEL_ZERO`** marks a row the game records at met level 0, whose span bounds the
  CURRENT level instead — Crystal's in-game trades.
- **Every template in a PKHeX data file must be consumed**, and `assert_all_arrays_used` now means that
  literally: arrays AND single `static readonly Encounter… X = new(…)` declarations. It knew only
  arrays, which is how Crystal's Virtual Console Celebi — one standalone template — never reached the
  table.

## Item names — one function picks the table

```cpp
const char* Names::getItemNameFor(Enums::GameVersion group, uint16_t itemId);
```

> **Generations do not share item numbering, and the divergence is silent: every id names *some*
> item in every table, so the wrong table produces a plausible wrong answer rather than a blank.**

Gen 1 keeps TMs as ordinary bag items at 201–250, which the modern table has nothing at. Gen 3 has
its own smaller space again. Call sites used to carry a `g3Items` bool, which stopped being
expressible the moment a third space existed. **Ask `getItemNameFor` instead**, so adding Gen 2 is
one line in one place rather than a fourth branch at every call site.

Related:

```cpp
uint16_t Names::itemG3ToModern(uint16_t g3Id);   // 0 if no modern equivalent
uint16_t Names::itemModernToG3(uint16_t modernId);
std::vector<uint16_t> Names::getPouchItems(GameVersion group, int pouch);   // legal ids for a pouch
```

## Met locations depend on origin *and* format

```cpp
uint8_t Enums::locationTableVersion(uint8_t originVersion, uint8_t formatVersion, bool isEggLocation);
```

Per PKHeX's `GameStrings.GetGeneration`, for a Gen 5+ format:

- A **Gen 5+ origin** keeps its own table.
- A **Gen 4 EGG** location stays on the Gen 4 table.
- A **Gen 3/4 MET** location (and a Gen 3 egg) is **remapped into the current format's numbering**
  when the mon is transferred up, so it must be named with the *format's* table.
- Pokémon GO (34) uses the current format's numbering.

Without this a Platinum starter link-traded up to Scarlet/Violet reads its met location as "(none)".
The call site looks like:

```cpp
const uint8_t fmtVer = Enums::getGroupRepVersion(p->getGameGroup());
const char* loc = Names::getMetLocationName(
    Enums::locationTableVersion(p->originGame(), fmtVer, /*isEgg=*/false), p->metLocation());
```

## Sprites

Sprite files are named by **PokeAPI id**: base species by national dex number (`925.png`, `925s.png`
for shiny), alternate forms by PokeAPI's 10000+ form ids.

Because `gen_hdsprites.py` mirrors the entire upstream tree, **every render already exists on disk**
— adding a form is one step, a mapping row, with no re-fetch. `SpriteManager` builds paths from ids
and never enumerates the directory, so unreferenced files are inert.

### Two lookups, not one

```cpp
uint32_t    Pokemon::getFormSpriteId(uint16_t species, uint8_t form);     // variant families
const char* Pokemon::getFormSpriteName(uint16_t species, uint8_t form);   // peer families ("" otherwise)
```

`getFormSpriteId` covers the **variant** families — a base plus regional forms, megas, fusions.
PokeAPI keys ~200 renders by **name** instead: the **peer** families, where no form is the default
and all of them get names — `201-a`, `493-fire`, `666-meadow`, `869-ruby-cream-berry-sweet`,
`585-summer`. Those have no numeric id at all. `SpriteManager` tries the name stem first.

`FormSpriteMapping.cpp` is hand-maintained, and a stem or id naming a file that is not on disk
falls back to the base species without any error — so check every new entry against
`romfs/sprites/pokemon_hd/` (and, for an id, against PokeAPI as below).

### Four traps

**A form id is not guessable — look it up.** The ids are not contiguous per generation and do not
follow dex order; a wrong-but-existing id renders a completely different Pokémon with no error.
Resolve against `https://pokeapi.co/api/v2/pokemon/<id>/` and check the returned `name`.
Near-duplicate names are the sharp edge: `10118` is `zygarde-10-power-construct`, while plain
`zygarde-10` is `10181`.

**Form *indices* come from PKHeX, not from the id order.** ~90 species have Mega forms with real
indices in the personal table, so they must be named and drawn correctly even though the picker
never offers them. PKHeX's `FormConverter` decides the ordering and it has real exceptions:
single-mega species put Mega at form 1, but Charizard / Mewtwo / Raichu use Mega X/Y, Absol /
Garchomp / Lucario use Mega + Mega Z, **Slowbro's Mega precedes its Galarian form**, and Meowstic /
Magearna / Tatsugiri / Zygarde / Greninja / Floette each have their own list. Derive indices from
`FormConverter`, then check them against `formCount` in `PersonalInfoTable`.

**Form 1 is not reliably the regional variant.** Both the name and sprite tables group
Alolan/Galarian/Hisuian/Paldean species into fall-through `case` blocks keyed on `formId == 1`.
Indices are assigned in the order forms joined the *series*, so a species that already had an
alternate form keeps it at 1 and the regional variant lands later: **Slowbro** is
Normal/Mega/Galarian, **Darmanitan** is Standard/Zen/Galarian/Galarian Zen. Before adding a species
to one of those groups, check `formCount` — anything above 2 needs its own `case`. When PKHeX and
intuition disagree, the tiebreak is `PersonalInfoTable`: the forms have different abilities, so
Slowbro form 1 = Shell Armor proves it is the Mega — a form table that had it as Galarian shipped
as a bug.

**PokeAPI's default variety is not always game form 0.** For every species but one they agree, so
`getFormSpriteId` short-circuits form 0 to the bare species id. **Maushold (#925) is the
exception**: PokeAPI's `925` is Family of *Four* while the game numbers Family of *Three* as form 0,
so it is special-cased ahead of that short-circuit (form 0 → `10257`, form 1 → `925`).

### When art genuinely does not exist

Add **no** mapping and let `SpriteManager` fall back to the base-species sprite. PokeAPI has no
entry at all for `poltchageist-artisan`, `sinistcha-masterpiece`, the Let's Go partner
Pikachu/Eevee, the PLA Lord/Lady bosses, Arceus's Legend Plate, Mothim's cloaks, Sinistea /
Polteageist Antique, or Scatterbug / Spewpa's patterns (only Vivillon's are drawn).

A missing **shiny** is normal too — the cap Pikachu have none, correctly, since they cannot be
shiny.

## Adding a table

1. **Write the generator** in `tools/gen_<thing>.py`, using `pkhex_source.pkhex_path()` for inputs.
2. **Emit a header + a `.cpp`** with a `DO NOT EDIT BY HAND -- rerun the generator` banner and a
   module docstring recording provenance and any deliberate deviation from PKHeX.
3. **Decide what absence means** and give the accessor a return value that expresses it —
   `nullptr`, a sentinel, or an out-parameter. Never a value that also means something real.
4. **Make a regenerated table that loses rows fail loudly.** Have the generator assert its
   expected counts and exit non-zero on a shortfall, and refuse input in a shape it does not
   recognise rather than reading it as "nothing here" (as `load_cosmetic_forms` and
   `assert_all_arrays_used` do).
5. `python tools/syntax_check.py` — the generated `.cpp` is often large enough to expose a syntax
   error only at scale.

Generators are allowed — encouraged — to be opinionated about PKHeX's data, as long as they say so
loudly. `gen_personal.py` deliberately excludes Gen 7 **Totem** forms even though PKHeX's per-game
rules include them, because `personal_gg` carries Totem Raticate and Totem Marowak rows with **zero
TM flags and an empty level-up learnset** — Let's Go cannot produce or hold one. Left alone, PKSE
inherited a form that is claimed to exist and can learn nothing: the picker offered it, the legality
checker called it legal, Conversion let a mon be converted *into* it, every move then read as
unlearnable — and the bank's transfer sanitizer acts on that by **deleting all four moves**. The
deviation is documented in the generator's docstring, which is where such a decision belongs.

---

## Rules: name and data tables

The rules above cover how tables are produced and looked up. These cover what is in them, each with
the defect it came from.

### Names are per language; stored text is not

**NAME TABLES ARE PER LANGUAGE; STORED TEXT IS NOT.** Two independent halves. *Stored* text — a
nickname, a trainer name — is bytes in the save, decoded by that generation's codec and shown as
written; the Gen 3 table (`GEN3_EN`) is the WESTERN charset PKHeX ships, covering EN/FR/DE/IT/ES
alike, so accented names always decoded correctly. *Table* text — species, moves, abilities,
natures, types, locations — is supplied by PKSE, and was once shipped English-only, so a French save
read "Bulbasaur" where the game says "Bulbizarre". Each generated table is now one array per language
plus a `_BY_LANGUAGE` dispatch indexed by `Names::displayLanguageIndex()`
(`include/Names/NameLanguage.h`), which owns the canonical order; `tools/name_languages.py` mirrors
it so a generator cannot invent its own. The nine are ja/en/fr/it/de/es/ko/zh-Hans/zh-Hant, exactly
matching `Enums::LanguageID`.

**THE LANGUAGE IS NOT USER-SELECTABLE, AND THE TABLES ARE PINNED TO ENGLISH.** It shipped as a
setting defaulting to the console's own (`setGetSystemLanguage`), persisted as the PKHeX suffix
(`language=fr`) rather than an index so reordering could not silently repoint an existing config —
and that is the wrong control. The language a save's names should be read in is a property of the
SAVE, not of whoever is holding the console: a French console showed French names for an English
save. The intent is to take it from the OPENED SAVE instead. Until that lands,
`Names::DISPLAY_LANGUAGE_SELECTABLE` is false, `Utils::loadSettings` pins English as its last act,
and the Settings row is **not drawn at all** — `SETTINGS_ROW_VISIBLE_COUNT` stops the screen one row
short, so it is absent from the drawing loop, the touch targets and the Up/Down wrap alike. Not
shown-and-greyed: a disabled control still reads as something the user ought to be able to reach.
That works because Language is the LAST row, and a `static_assert` says so — a row added after it
would be the one hidden.

**Everything behind that row is intact and is deliberately NOT to be removed** — the nine
per-language tables, `setDisplayLanguage`, the `language=` key, the console probe — because it is
what the save-driven version is built on. Turning the constant true re-enables the lot. **None of
this touches STORED text, and that half already works in every language**; do not conflate the two.

**CHECKING A LOCALIZED TABLE NEEDS A TABLE-WIDE COMPARISON, NOT A SAMPLE.** The property that must
hold, **per table**: each language differs from English somewhere, and wherever English has a name
so does every other language. (An empty string is legitimate — the location tables emit `""` for
blank slots and for PKHeX's em-dash sentinel, in every language including English — so the invariant
has to be the relative one.) **Per table is the whole point**: pooling every name into one list and
asking whether *something* differed passes even when an entire table is broken, because the others
still differ — a deliberately broken `getItemName` went unnoticed that way, and is caught in all
eight other languages when compared per table. A sample ("is Bulbasaur translated?") FAILS ON
CORRECT DATA — species names are largely shared, and only 21 of 1026 differ between English, Italian
and Spanish — while move names differ in ~911 of 921. The failure this exists to catch is a getter
that forgot to index by language and quietly returns English for everyone, which looks perfect to an
English tester.

### Individual tables

**ID 426/427 ARE THE GEN 4 HM07/HM08 SLOTS, and PKHeX cannot name them.** Its flat item table is Gen
5-aligned there — `420..425` are HM01..HM06, `426`/`427` are `???`, and HM07 lives at `737` with no
HM08 in any language. Gen 4 genuinely has eight HMs at `420..427`, which is why
`POUCH_DP_TMHM`/`POUCH_PT_TMHM`/`POUCH_HGSS_TMHM`/`POUCH_BDSP_TMs` each hold exactly 100 contiguous
ids `328..427` while `POUCH_BW_TMHM` stops at 425 and `POUCH_ORAS_TMHM` carries `420..425` plus
`737`. `gen_itemnames.py` fills 426 from id 737 **verbatim** and derives 427 by incrementing its
trailing digit — valid in all nine languages because every one spells an HM as a prefix plus a
two-digit number (`HM07`, `CS07`, `VM07`, `ひでんマシン０７`) — and ASSERTS the 7 rather than
guessing. These ids used to read `TM93..TM100`, so every Gen 4 pouch showed "TM93" where the game
says "HM01".

**GEN 2 HAS A MET-LOCATION TABLE, AND NOT HAVING ONE WAS INVISIBLE.** `getMetLocationName` had cases
for origin versions 1-5, 7-12, 20-34 and 42-52 and nothing for 35-41, so every Gold, Silver and
Crystal record hit `default: return ""`. A `PK2` inherits `originGame() == 0`,
`locationTableVersion` falls through to the format's representative version (Gold, 39), and the
details page printed `(none)` — **the same thing it prints for a field that is genuinely unset**, so
a Crystal Pokémon caught on Route 25 and one with no caught data at all looked identical. Layer 3
said `location57`. The table is `text_gsc_00000_en.txt`, 128 entries (Gen 2's met location is seven
bits), one bank, all nine languages, and Gold/Silver share Crystal's because only Crystal ever writes
the field. Gen 1 needs none: a PK1 has no met data, so the row is never drawn.

**THERE ARE TWO TYPE COUNTS AND THEY ANSWER DIFFERENT QUESTIONS.** `Names::getTypeCount()` is
**18**: how many types a SPECIES or MOVE can be, and the bound a personal row's `type1`/`type2` is
checked against. `Names::getTypeNameCount()` is **19**: how many can be NAMED or DRAWN, Stellar
included, and what bounds the icon set and anything showing a Tera type. Merging them would let a
corrupt personal type of 18 pass a bound that catches it today.

**THE TERA TYPE IS NOT STORED AS A TYPE ID**, and reading it as one gives a plausible wrong answer
rather than an error. S/V keep two bytes — the type the Pokémon was ENCOUNTERED with (`0x94`) and,
if a Tera Shard changed it, the OVERRIDE (`0x95`) — in a vocabulary wider than the eighteen ids:
**Stellar is `99`**, not 18 (18 is only where its NAME sits, PKHeX's
`StellarTypeDisplayStringIndex`), and **`19` in the override byte means "never overridden"** rather
than naming a type. `Pokemon::resolveTeraType()` is PKHeX's `TeraTypeUtil.GetTeraType` and
`teraTypeNameIndex()` is the one place mapping the byte onto the name table. `hasTeraType()` is a
capability predicate for the usual reason — Tera type 0 is Normal — and it is true for
**Scarlet/Violet alone**: Z-A shares the Gen 9 entity layout but has no Terastallization, and PKHeX's
`PA9` implements no `ITeraType`. Tera type is **reported, not edited**: changing it is a Tera Shard
mechanic with its own rules (Ogerpon and Terapagos cannot change theirs at all).

**A FIXED BLOCK TABLE EXISTS ONLY FOR THE SIX GROUPS WHOSE SAVE IS A BLOCK LIST**, one header each:
`Blocks5BW`, `Blocks5B2W2`, `Blocks6XY`, `Blocks6ORAS`, `Blocks7SM`, `Blocks7USUM`
(`gen_blocktables.py`), with the row types in `Trainer/BlockTable.h`. The row type is per PLATFORM
because that is what varies — a DS save stores each block's CRC twice, a 3DS save keeps every
checksum in the BEEF chunk at the end — but the TABLES are per group, because the offsets and lengths
differ. Nothing else gets one: Gens 1-2 are flat SRAM whose offsets shift with the locale, Gen 3 is
fourteen rotated sectors each carrying its own id, Gen 4 is two partitions holding a General and a
Storage block, and BDSP is a flat fixed-offset image. **For the SCBlock games — Let's Go, SwSh, PLA,
S/V, Z-A — a static table is impossible, not merely redundant**: the block list is data inside the
save. Each Gen 5 group's named block indices are **derived from its own table by block name** at
generation time; they used to be five constants shared by BW and B2W2 on the strength of a comment
saying the orders agree up to #27, with only the block COUNTS asserted — the two orders do diverge,
at #32.

**A GEN 4 RECORD HAS TWO MET-LOCATION FIELDS AND THE NEWER ONE WINS.** Diamond and Pearl store the
met and egg locations at `0x80` and `0x7E`; Platinum added places those could not name, so it
introduced a second pair at `0x46` and `0x44` and left the originals holding **"Faraway place"
(3002)** for a DP game to show. `Pokemon4DP`/`4PT`/`4HGSS` read the extended pair first and fall
back to the DP pair, which is what keeps Diamond and Pearl working since they leave `0x46` zero —
PKHeX resolves it identically (`G4PKM.MetLocation`). Reading only the DP pair reported 3002 for every
Pokémon caught in Platinum, HeartGold or SoulSilver: five ordinary Johto route catches in a real
SoulSilver save came back as *"Faraway place is not a place X can be encountered"*, which reads
exactly like hacked data and is not. The SETTER has to write both, the way the games do — a place DP
has no id for goes in the extended field with Faraway left in the DP one; anything they do have goes
in the DP field, and into the extended one only for a game that writes it.

**A POKÉDEX INDEX TABLE EXISTS ONLY WHERE THE DEX NUMBERING IS ARBITRARY.** Most dex blocks are
indexed by arithmetic — Gen 1 and Gen 3 put a species' bit at `species - 1`, BDSP indexes its arrays
the same way, and Z-A keys on the Gen 9 internal species id `SpeciesConverter9` already provides.
Those carry no table and must not grow one; a lookup for `species - 1` is not data. The ones that do
have a table are the ones whose ordering exists only in PKHeX: `DexTable8SWSH` (three separate dex
blocks, own numbering each), `DexTable9SV` (three regional dexes in one block, membership per species
**and form** — Kantonian Meowth is Paldea, Alolan Meowth is Blueberry), `DexTable8LA` (statistics
entries reached through a lookup on `species | (form << 11)`, plus each species' size research
task and threshold from `researchtask_la.pkl`, reached through `personal_la`'s Hisui dex index), and
`DexTable6XY`/`6ORAS`/`7SM`/`7USUM`/`7LGPE` (`gen_dexformtables.py`), whose seen/caught bits are
arithmetic but whose alternate-FORM region is not. **Ten groups write no dex at all** — Gens 2, 4, 5,
6 and 7-3DS — which is deliberate and documented on `Trainer::updatePokedexBlock()`: a no-op leaves
the save exactly as it was rather than half-written.

### Ability slots

**ABILITY SLOTS ARE PER-GAME DATA. Ask `Pokemon::getAbilitySlots(species, form, group)`, never
`getPersonalInfo(...).ability1`.** The cross-game personal table is sourced from S/V because it spans
the whole National Dex, and S/V is the outlier for a couple of dozen rows: it moved the **Piplup
line's** hidden ability from Defiant to Competitive, Shiftry's second from Early Bird to Wind Rider,
Gallade's from none to Sharpness, and it re-slotted most of PLA's **Hisuian forms**. It also has a
hidden slot that **Gen 4 does not have at all**. Reading it for every game reported an ordinary Ultra
Sun Piplup as holding an illegal ability. `getAbilitySlots` is one lookup —
`getPersonalRecord(group, species, form)`, gated by `personalHasAbilities` /
`personalHasHiddenAbility`. It used to be three branches and a generated override list (the 27 rows
where a Switch game disagreed with S/V); all of that was compensation for tables serving more than
one game, and went away with the per-group tables.

**THE SLOTS ARE NOT ALWAYS THE CURRENT SPECIES'.** Legends: Z-A keeps the ability a Pokémon was
ENCOUNTERED with through evolution (PKHeX `AbilityVerifier.VerifyBirthAbility`), so a Fletchling
caught with Big Pecks stays a Big Pecks Fletchinder. The one Fletchinder in a real Z-A save — Big
Pecks in slot 2, never through HOME — was reported "Ability not legal for this species", by two
places each asking their own copy of the slot question. `Pokemon::isAbilityLegal(const Pokemon &)`
is now the only place it is asked: one of the species' slots, or, where the entity's
`hasBirthAbility()` says so, the ability that same slot held on any species in the chain. PKHeX
checks the one encounter it matched; Layer 2 cannot name that encounter, so any ancestor counts,
which can only accept more than PKHeX does.

### Decoding PKHeX's text files

**DECODE PKHeX TEXT BY ITS BOM, NEVER BY TRYING ENCODINGS IN ORDER.** The resources are not uniform —
the Gen 3 item list is UTF-16 while the modern one is UTF-8, and it varies per language. Trying
`utf-16` first and catching `UnicodeDecodeError` looks careful and is wrong: almost any even-length
byte sequence IS valid UTF-16, so the French and Spanish UTF-8 files decoded "successfully" into a
single line of garbage instead of raising.

The same family, one step subtler: **`utf-8` KEEPS a byte-order mark as a character and `utf-8-sig`
strips it.** `gen_locations.py` read with plain `utf-8`, so the BOM rode into entry 0 of every table
that has a real name there — ten tables, nine languages each — and `getMetLocationName(HG, 0)`
answered `"﻿Mystery Zone"`, an invisible glyph handed to the font for every Gen 4 Pokémon with
no met location. It decoded "successfully" and looked right in every diff.

**SWAPPING A HAND-WRITTEN TABLE FOR A GENERATED ONE IS GATED ON THE ENGLISH OUTPUT BEING
IDENTICAL.** That gate is what stops a PKHeX-internal label from shipping as user-facing text.
Abilities, natures and types were hand-written; each was diffed against PKHeX line by line before the
swap, and the only difference anywhere was ability id 0 (PKSE's "None" vs PKHeX's em dash), which
`gen_simplenames.py` preserves — the same substitution `gen_speciesnames.py` makes at species 0.

### Form names

**FORM NAMES KEEP THEIR ENGLISH AND BORROW EVERYTHING ELSE.** `FormNames.cpp`'s hand-curated English
switch is **more faithful to the games than PKHeX's**, which are terse labels for its own dropdown.
Diffed entry by entry: 334 matched, 195 differed, and the games back PKSE nearly every time —
`Alolan`/`Galarian`/`Hisuian` against PKHeX's `Alola`/`Galar`/`Hisui` (53 entries), `Plant
Cloak`/`Red Flower`/`West Sea` against `Plant`/`Red`/`West`, `Male`/`Female` against the bare gender
symbols. So English is never generated. `gen_formnames.py` supplies only the eight other languages
into `FormNamesLocalized.cpp`, and `getFormName` asks English FIRST — if English names nothing (form
0 of most species) no language names anything, so the two can never disagree about which forms
exist. (The diff paid for itself in the other direction too: it caught `Poke Ball` missing its
accent, which PKHeX had right.)

**FORM LABELS COME FROM POKEAPI WHERE IT CARRIES THE GAMES' WORDING, PKHeX OTHERWISE — and PKHeX IS
TERSE ONLY IN THE LATIN LANGUAGES.** Measured on cells where both have a label, PKHeX matches the
game **18%** of the time in French and ~20% in German, Italian and Spanish — it drops the head noun
(`Alola` where the Pokédex titles the form `Forme d'Alola` / `Alola-Form`). But it matches **67% in
Japanese and 83% in Korean**, and its **Chinese is already the games' full wording**
(`阿罗拉的样子`, `攻击形态`, `水井面具`) while PokeAPI carries no Chinese form names at all. So the
CJK columns are mostly PKHeX's and that is the right answer, not a gap: a "0% PokeAPI coverage"
reading for Chinese measures PokeAPI, not PKSE.

**A POKEAPI ROW IS JOINED TO A PKSE ROW THREE WAYS, MOST SPECIFIC FIRST.** (1) PKSE's **own
verified** `FormSpriteMapping` id — the only thing that can separate two forms sharing a label, and
Zygarde needs it, since `10% Forme` is PokeAPI's form label for BOTH the ordinary and the Power
Construct row. (2) The English label matched **within one species**, so a wrong match can only be
another form of the same Pokémon, and a same-species tie is refused rather than guessed (that is what
keeps Minior's seven identically-labelled `Meteor Form` colours out). (3) PokeAPI's full `names`
reduced to a label the same way, which is the only thing that names Greninja's Battle Bond. Three
normalisations make the spellings comparable and **each is symmetric**, so none can equate two
genuinely different forms: the **species' own name is removed, per language** (PokeAPI's
`form_names` is sometimes a full name — `Mega Venusaur`, `Heat Rotom` — and PKSE composes the species
separately, so leaving it in renders `Florizarre (Méga-Florizarre)`; per language is what makes it
work for Japanese and Chinese, which have no spaces, and it also fixed PKHeX's own `加熱洛托姆`); the
generic form-category noun is dropped from both sides; and tokens are sorted, because the two
disagree on order (`Core Blue` / `Blue Core`). **The noun list was found by asking the data** —
sweeping every unmatched row for a same-species label differing by exactly one token turned up
`flower` (Floette) and nothing else. Two guards follow: a **per-language collision check**, because
PokeAPI's German says `Mega-Form` for BOTH of Charizard's megas and would lose a distinction PKHeX
keeps; and a refusal of any **wholly parenthesised** remainder, which is PokeAPI's own
disambiguation (`Zygarde (10%)`) and once displaced PKHeX's better `Forma 10%`. **Arceus and Silvally
are excluded entirely** — `getTypeName()` already names their forms in every language, so a row here
would shadow it. Coverage of the 493 rows needing a translation: **fr 85%, ja 80%, de 67%, es/it/ko
~60%** carry the games' wording; the rest keep PKHeX's label, and 29 labels have no game string at
all (Alcremie's nine creams, the Terastal masks and Zygarde's Power Construct forms, which PKSE
composes itself; Paldean Tauros's breeds; Unown's `!` and `?`).

**NO SOURCE GIVES A COMPOSED NAME, because the games do not compose one.** They show the form as a
separate label — the Pokédex titles it "Alola Form" — and their running text often drops the
regional adjective entirely, so `Alolan Vulpix` is a wiki convention rather than a game string.
Deriving one algorithmically was tried and measured against PokeAPI's composed `names`: 73%
agreement in French, 36% in German, and Italian/Spanish carry no composed names at all.
`getDisplayName`'s composition is therefore PKSE's own affordance, not something to source — so
**English keeps the natural prefix (`Alolan Vulpix`) and every other language gets a
parenthetical** (`Goupix (Forme d'Alola)`, `Vulpix (Alola-Form)`). That consumes the sourced label
verbatim, invents no grammar that cannot be verified, holds for every form family rather than just
regional ones, and is how the French and Italian Pokédex sites present a form themselves. **A form
English does not name composes to a bare species name in EVERY language** — the half that would
regress silently, so treat it as an invariant to check whenever this code changes.

**PKSE DOES NOT RE-IMPLEMENT `FormConverter`** — 1159 lines of context-dependent C# would be a second
copy of logic that has to agree with the first forever. `tools/formdump/` is a small C# program that
calls the real thing and dumps JSON, **unioned across every `EntityContext`** because PKSE names
every form a buffer can hold: Raticate's Totem form exists only in the Gen 7 list, so asking Gen 9
alone loses it. This is the one generator needing more than a text fetch — PKHeX.Core has to be
COMPILED, so it needs the .NET SDK — but, like every other generator, **not a local checkout**:
`pkhex_repo()` fetches the source at the resolved ref (see "Running a generator"). Its output is
committed like every other.

**A TABLE-WIDE LANGUAGE CHECK DOES NOT BITE FOR FORMS, and the reason generalises.** Arceus's plates
and Silvally's memories are named by calling `getTypeName()`, which is itself language-indexed — so
eight form names shift language even when the form dispatcher is completely broken, which is enough
to satisfy "something differed from English" and hide the fault. A deliberately broken `getFormName`
still showed exactly those eight differences and passed. The property that does bite is the direct
contract: **wherever the generated table has a name for a (species, form), `getFormName` must return
exactly that** — the same break then fails for 370 of 383 rows. **When a getter composes part of its
answer from another localized getter, the indirect check is not a check.**

### Ribbon names

**RIBBON NAMES ARE THE GAMES', NOT PKHeX'S, and four of them were simply wrong.** `Battle Royale` is
`Battle Royal Master`; `Champion Battle` / `Champion Regional` / `Champion National` are `Battle
Champion` / `Regional Champion` / `National Champion` — PKSE's own Gen 8/9 block already spelled
those three the right way round at 0x38. Two more carried disambiguation no game shows:
`RibbonChampionG3` is just **Champion Ribbon** in game and `RibbonChampionG6Hoenn` is **Hoenn
Champion Ribbon** — separately named, so neither needs PKSE's `(Gen 3)` or PKHeX's `(Gen3)`. Nothing
checks ribbon names against the games automatically, which is how four wrong ones survived; check a
new one by hand.

**LOCALIZING A NAME-KEYED TABLE MEANS HANDLING AMBIGUITY, NOT HOPING PAST IT.**
`gen_ribbonnames.py` keys on the English string PKSE produces, and several PKHeX keys collapse to one
English label once its `(G3)`/`(G4)` suffixes are stripped — `RibbonG3Cool`, `RibbonG4Cool` and
`RibbonCountG3Cool` all become `Cool`, and they disagree in Japanese, French, Italian and Korean.
Ambiguous labels whose languages disagree are DROPPED rather than guessed, and the one PKSE actually
needs (`Beauty Master` → `RibbonMasterBeauty`, the Gen 8 contest ribbon) is an explicit alias. Its
four siblings need none: BDSP names those contests `Coolness`/`Cuteness`/`Cleverness`/`Toughness`,
which collide with nothing. Every name `RibbonNames.cpp` can produce — 113 flag entries plus 20
composed Gen 3 contest names — is verified to have a row, so a fallback to English means a bug
rather than a gap.

### Origin markings

**ORIGIN MARKINGS ARE THE GAMES' OWN, FETCHED RATHER THAN DRAWN** (`tools/gen_marks.py` →
`romfs/sprites/marks/`, from PKHeX's `Markings` resources). The Kalos pentagon, the Alola clover,
the Galar mark, the Game Boy for a Virtual Console transfer and the rest are what the games stamp on
a Pokémon's summary. `Trainer::originMarkFor()` (`include/Trainer/OriginStamp.h`) is PKHeX's
`OriginMarkUtil.GetOriginMark`, and **its order is load-bearing**: a Virtual Console, GO or Let's Go
Pokémon sits in a Gen 7 FORMAT while carrying its own origin version, so the three specific marks are
tested before the generation lumps — checking Gen 7 first stamps an Alola clover on every transferred
Red/Blue Pokémon. `gen_marks.py`'s `MARK_NAMES` is the file half of `Trainer::OriginMark`; a mark
added to the enum without a row there is a missing file at runtime.

**GEN 3, 4 AND 5 HAVE NO MARK, AND THAT IS THE CORRECT ANSWER RATHER THAN A GAP.** No mark existed
before the Kalos pentagon, so there is nothing to fetch and nothing to invent; those origins are
named in TEXT, which they can be because they carry a version byte. Gen 1 and Gen 2 are the mirror
image: no version byte at all, so the console is the only thing actually known and the mark stands
alone with no name beside it. Gen 6+ get both. (A **Game Boy Advance** mark comes with the Switch
FR/LG release's HOME compatibility; PKHeX carried neither the resource nor the enum entry when this
was written, so it lands as one `MARK_NAMES` entry plus one `OriginMark` row.)

**The markings are BLACK SILHOUETTES WITH AN ALPHA CHANNEL and must be tinted.** Untinted they are
invisible on the dark theme and would stay black on the light one; PKHeX solves it the same way
(`BlackToWhite`). `SpriteManager::getOriginMarkSprite` recolours RGB and keeps alpha — the shape
lives entirely in the alpha channel, so the antialiased edge survives — and caches per colour. Two
traps: `loadSprite` hands over ownership of a fresh decode and caches nothing, so the source must be
released; and `~Sprite()` frees with `stbi_image_free`, so the tinted buffer must come from
`std::malloc` rather than `new[]`.

### Fonts

**CJK NEEDS THE CONSOLE'S OWN FONTS, and that was the actual "non-English names don't render"
bug.** Nunito plus the two Noto Symbols faces carry Latin-1 completely (64/64 accented characters)
and not one CJK glyph, so a Japanese nickname decoded perfectly and drew as nothing.
`PKSEFramebuffer` loads `PlSharedFontType_Standard`, both Chinese faces, Korean and Nintendo Extended
from `pl` shared memory via `nvgCreateFontMem` (`freeData = 0` — the buffers belong to the service,
which is why `main.cpp` holds `pl` open for the process) and chains them as NanoVG fallbacks after
Nunito. They are preferred over a bundled Noto CJK **because they are the same faces the console and
the games render Pokémon names with**, so a nickname looks in PKSE exactly as it does in the game it
came from, and because there is no fetch step to go stale. **Size is not an argument and must not be
used as one anywhere in this project: PKSE runs under title override for the memory headroom, so the
.nro's compiled size is not a constraint.** Nothing should be trimmed, subsetted or omitted to keep it
small.

### Gen 3 text

**Gen 3 text lives in exactly one header, `include/Utils/Gen3Text.h`, and it holds TWO tables**
(`GEN3_EN` + `GEN3_JP`, PKHeX's `StringConverter3` verbatim). GBA saves use a bespoke single-byte
encoding, not ASCII — internationally `A` is `0xBB`, the apostrophe `0xB4`, the gender signs
`0xB5`/`0xB6` — and it once existed as four independent ad-hoc subsets covering only
`A-Z 0-9 ! ? . -`, in `Pokemon3FRLG`, `Trainer3FRLG` (twice) and `Conversion/Convert`. Fixing one left
the others wrong in *different* ways: names were written correctly and read back through a copy that
substituted `?`, which is itself a real Gen 3 character, so the damage was invisible. Route new Gen 3
name code through the shared header and add characters there, once. What legitimately differs per
call site is the *field* policy — who writes a terminator, who preserves the bytes after it — not the
alphabet.

**A BYTE IS A FONT INDEX, SO THE LANGUAGE PICKS THE TABLE, and every entry point takes one.** The
same byte draws a different glyph on a Japanese cartridge than on an international one. Which table
applies is decided by the RECORD's own language byte for a PK3 (`0x12`) and by the SAVE's for a
trainer or box name — a traded Pokémon keeps the bytes of the game it came from, the box it sits in
does not. PKHeX does exactly this (`PK3.Nickname` passes `Language`; `SAV3` passes its own). It fails
silently in both directions: 78 of the first 176 kana have no international slot, so a Japanese name
breaks at the first of them, and **337 of the 386 Gen 3 species' Japanese names come back mangled or
empty** through the wrong table. That shipped — Japanese Dittos imported from a PKSM bank into
FireRed arrived called `DITTO` with a blank OT. Nothing in a Gen 3 save names its language;
`detectLanguage()` measures it from the width of the trainer-name field (PKHeX's `Small+0x06 == 0`),
and like PKHeX and PKSM it can answer only Japanese or English — the five international languages
share one table and differ solely in the quote pair at `0xB1`/`0xB2` (`« »` French, `„ "` German),
which `gen3QuoteLeft`/`gen3QuoteRight` own.

**AN EGG IS THE ONE GEN 3 RECORD WHOSE LANGUAGE BYTE IS NOT A LANGUAGE**, and it is the exception
that the rule above walks straight into. The games stamp every egg — every cartridge, every region —
with the Japanese word for egg as its nickname and **language 1**, and replace both when it hatches.
PKHeX writes the same pair (`PK3.IsEgg` sets `Nickname = "タマゴ"` and `Language = Japanese`), so
this is the format's own convention rather than a quirk of one save. Read out of a real English
FireRed:

| record | 0x12 language | 0x08 nickname | 0x14 OT | 0x13 flags |
|---|---|---|---|---|
| Breloom, ordinary | `02` English | `BC CC BF C6 C9 C9 C7 FF` → BRELOOM | `C5 C3 BB CD CE BB FF` | `02` |
| Bulbasaur, unhatched egg | `01` Japanese | `60 6F 8B FF` → タマゴ | `C5 C3 BB CD CE BB FF` | `06` |
| Charmander, hatched egg | `02` English | `CHARMANDER` | `C5 C3 BB CD CE BB FF` | `02` |

**The OT bytes are identical in all three**, which is the proof that the language byte is not
describing this record's text: it is part of the placeholder, and the OT beside it is still the
cartridge's own charset. Letting it pick the table turned the player's own `KIASTA` into the Japanese
table's full-width `ＫＩＡＳＴＡ` — the same letters, wide — while the nickname came out as タマゴ and
the details page reported Language: Japanese. The hatched row is what says the placeholder is
temporary, and it is why the Language row reads *(set when it hatches)* rather than guessing.

Three rules follow, and each is asked of the ENTITY rather than of the generation, so a format whose
eggs carry real text (Gen 4 on) is untouched:

- `eggTextIsPlaceholder()` — is this record's nickname/language a placeholder at all.
- `otTextLanguage()` — the record's own language, except on an egg, where it is the SAVE's. The
  Trainer hands its language to every record it builds; a bank record has no save and keeps the
  international default, which is what five of Gen 3's six releases share.
- The **nickname getter stays byte-faithful** — it still reads タマゴ — because that is what lets the
  writer put the placeholder back untouched. What a user is shown is the caller's business, and the
  details page shows `Egg`, with the Nickname and Language rows reporting rather than offering an
  edit: an egg is not nameable in the games either, and a real name written into that field is what
  makes the record fail PKHeX's egg-nickname rule.

**`setEgg(true)` writes the whole placeholder** — the 0x13 sanity flag, the nickname and the
language — not just the IV bit. An egg carrying only the bit is a record no save holds.

**A JAPANESE CARTRIDGE HAS NO HALF-WIDTH LATIN**, so a Roman name entered on one is stored
FULL-WIDTH — a real Japanese Ultra Moon record carries `Ｋｉａｓｔａ`, not `Kiasta`. `charToGen3`
retries a half-width character full-width on a Japanese record, which is what lets a name typed on
the Switch keyboard be stored at all. It retries only after the direct lookup fails, so it can never
rewrite a character the Japanese table does hold. PKHeX has no equivalent; PKSM converts the whole
string up front, which is the same fact applied more bluntly.

### Language and names across a transfer

**A LANGUAGE ID IS NOT PORTABLE BACKWARDS.** Korean arrived in Gen 4, both Chinese scripts in Gen 7
and Latin American Spanish in Legends: Z-A, and every remap copies the byte verbatim — so a record
moving down can land claiming a language its destination never had. `Enums::safeLanguageForGroup`
clamps it to English (PKHeX's `Language.cs` `SafeLanguage`), once in `convert()` for every
destination and again inside `remapPK8toPK3`, which has to clamp *before* it writes a name because
the language is what picks the table. **PKSM does not do this at all** — it carries the byte and lets
the encoder fail, which is the same empty-field bug reached through Korean instead of Japanese.

**THE OT NAME IS THE ONE FIELD WITH NO FALLBACK, and it is left BLANK rather than substituted.** A
nickname has the species name to fall back on — Gen 3 has no is-nicknamed flag, so an un-nicknamed
Pokémon is one *holding* its own language's species name, which is why `getSpeciesNameLocalized`
exists. A trainer's name is theirs and there is no stand-in: `"Trainer"` is a real Gen 3
in-game-trade OT that PKSE already writes to mean exactly that, and the receiving player's name would
silently turn a traded Pokémon into a self-caught one. So the encode is ATTEMPTED and only its
failure is reported — which keeps the common case (a Korean or Chinese player whose trainer name is
Latin) working, where deciding from the language label alone would have thrown it away.
`Conversion::canStoreOriginalTrainerName` is the pure check; the Gen 3 confirm dialog says so before
the move, and the `[EVENT]` log records it after.
