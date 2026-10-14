# 11 — The legality checker

## What it is, and what it deliberately is not

```cpp
// include/Legality/Legality.h
enum class Severity : uint8_t { Info, Warning, Invalid };

struct Issue  { Severity severity; std::string text; };

struct Report {
    std::vector<Issue> issues;
    int  problemCount() const noexcept;   // Warning + Invalid (Info notes are not problems)
    bool ok() const noexcept;             // problemCount() == 0
    bool empty() const noexcept;          // NOTHING to show, not even an Info note
};

Report analyze(const Pokemon::Pokemon& pk, Enums::GameVersion originGroup);
```

**It is informational.** It never blocks an edit and never auto-fixes anything. PKSE can edit saves,
so a user's box may genuinely hold something illegal, and that is their business.

**A clean report means "no problems found", never "this is legal."** Event / Mystery Gift
distributions are not modelled, and neither are PID/seed correlations. Where a check would need data
PKSE does not have, it says so as an **Info** note rather than staying silent.

`ok()` and `empty()` are different questions, and the UI uses both. A Pokémon with no problems can
still have Info notes worth reading (which encounter it matched, or that a check could not run) — so
the details modal opens its issue list on `empty()`, not on `ok()`.

**No RTTI, no exceptions.** Every check calls base `Pokemon` virtuals; per-generation differences
branch on the game group or a capability flag, never on concrete type.

## The three layers

| Layer | File | Question |
|---|---|---|
| **1 — structural** | `Legality.cpp` | Are the field values in range? EV/AV caps, nature and ball ids, known species/move/item ids, name lengths, checksum. |
| **2 — consistency** | `Legality.cpp` | Is the Pokémon consistent with itself and its species data? Level vs EXP, met level vs level, gender vs gender ratio, ability against the species' slots, move learnability, per-game species presence. |
| **3 — encounter** | `EncounterMatch.cpp` | Is there a **real encounter** in the game it says it came from that could have produced it? |

Layers 1 and 2 run in `analyze()`; layer 3 is `checkEncounter(pk, report)`, called at the end of it.

## Layers 1 and 2, in order

The checks in `analyze()`, in the order they run (grep `// ---- L` to find each):

```
L1  species id known (name-table sentinel = "Unknown")
L1  nature range (+ stat/mint nature on Gen 8+)
L1  EVs                         <- three completely different rules; see below
L1  AVs (Let's Go only, <= 200 each)
L1/L2 ability slot valid + ability id legal for the species/form
L1  moves are known ids + no duplicates
L2  move learnability          <- union of the origin's pool and the current one; see below
L1  held item is a known id (skipped when none)
L1  ball / language ranges (skipped when unwired == 0)
L2  level <-> EXP               <- EXP-derived level is authoritative
L2  met level <= current level
L2  gender vs the species gender ratio
L2  species/form obtainable in this game (personal presence bitmask)
L2  OT presence (a non-egg should have an OT name + a nonzero trainer id)
L1  name lengths (<= 12 UTF-16 chars)
L3  encounter matching
```

### A transferred Pokémon is checked against two learn pools, not one

The pool is a game's own learn methods unioned with the species' whole pre-evolution chain, and it
stays a **Warning** rather than Invalid because it cannot express *when* a move was legal (tutors
that came and went, event moves, trade-backs).

**A Pokémon that came through Poké Transporter learned its moves in Gen 1 or Gen 2, and no later
learn pool records that** — a Yellow Pikachu really does know Surf. So such a mon is checked against
the union of the pool it learned in and the pool it lives in now:

```cpp
const uint8_t originVersion = pk.hasOriginGame() ? pk.originGame() : 0;
const GameVersion transferGroup =
      (originVersion >= 35 && originVersion <= 38) ? GameVersion::RBY   // Red / Green / Blue / Yellow
    : (originVersion >= 39 && originVersion <= 41) ? GameVersion::GSC   // Gold / Silver / Crystal
                                                   : originGroup;
```

Both halves are load-bearing. Checking only the modern pool flags every legitimate Virtual Console
move — **which PKSE's own transfer produces**, so the editor would immediately warn about its own
output. Checking only the old pool flags every TM taught after the Pokémon arrived. The origin byte
is what says which case this is, and it is trustworthy here: nothing but a transfer writes a Gen 1/2
version into a modern record.

### "Optional fields that are unwired read 0 and are skipped"

That is the file's stated policy, and it is what keeps a format with fewer fields from producing a
wall of false flags. In practice, prefer the **capability predicate** (`hasBall()`, `hasNature()`,
`hasPID()`) over the `== 0` heuristic where one exists — chapter 06.

### The EV check is three rules, not one

This is the best worked example in the file of "the same interface slot means different things in
different formats".

```cpp
if (pk.hasAwakeningValues()) {
    // Let's Go has NO EV training. Nothing in the game writes these bytes and the stat formula
    // has no EV term, so on a legitimate Pokemon they are all 0.
    //
    // The 252/510 caps are the WRONG QUESTION here, not merely a differently-worded one: they
    // would pass a fully trained 510 spread as perfectly legal while a lone stray 4 slipped by
    // in silence. What matters is non-zero at all, so that is what is checked.
    //
    // Reported as a WARNING, not Invalid: PKHeX has this exact rule but leaves it DISABLED, so
    // calling it illegal outright would be a stronger claim than the reference is willing to make.
    if (evTotal != 0) add(r, Severity::Warning, "Let's Go has no EVs, but this Pokemon carries …");

} else if (pk.maxEV() > 255) {
    // Gen 1/2 have Stat Experience: 16-bit per stat, cap 65535, NO combined limit. Every value
    // the field can physically hold is legal and there is nothing here to check. That is a real
    // answer, not a gap.
    //
    // Applying the 252/510 rule anyway would be LOUDLY wrong: ev[] comes from the 8-bit
    // accessors, which SATURATE for these formats, so any well-trained Red/Blue Pokemon reads
    // 255 in all six slots and gets reported six times for "EV over 252" plus once for "EV total
    // over 510" -- seven Invalid findings on a Pokemon legitimately trained on a Game Boy.

} else {
    for (int i = 0; i < 6; ++i)
        if (ev[i] > 252) add(r, Severity::Invalid, …);
    if (evTotal > 510) add(r, Severity::Invalid, …);
}
```

Three lessons, all generalisable:

1. **Ask the format what it is, do not assume.** `hasAwakeningValues()`, `maxEV()`.
2. **The right *question* differs, not just the threshold.** For Let's Go it is "is it non-zero",
   not "is it under 252".
3. **Match the reference's confidence.** PKHeX disables its own Let's Go EV rule, so PKSE reports
   Warning rather than Invalid.

## Layer 3 — encounter matching

`include/Legality/EncounterMatch.h`. This asks a different question from layers 1 and 2: *is there a
real encounter that could have produced this Pokémon?* This species, in this form, at this met
location, at this met level, with this shiny state, ball, gender and nature.

Three design decisions make that answerable without porting PKHeX's RNG machinery:

**1. Tables are keyed by the mon's ORIGIN version, not by the save it currently lives in.** A met
location only means anything in its own game's namespace. Z-A and Scarlet/Violet are both Gen 9 and
share none of theirs, so a Sword Pokémon in a Violet box is checked against **Sword's** tables —
which is also what PKSE's location display already does.

**2. Evolution is walked backwards.** A Charizard was caught as a Charmander, so the match runs over
the whole pre-evolution chain (`Pokemon::getPreEvolutionChain`).

**3. Matching is ANY-of.** A row only has to *exist*. A constraint is reported violated only when
**every** otherwise-matching row violates it.

### Where it deliberately stays silent

- **A fateful Pokémon that matches nothing** is a **Warning**, not Invalid. Mystery Gift distributions
  ARE in the tables for Gens 3-9, but only the ones PKHeX archived, and Gens 1 and 2 have none (see
  below) — so a missing card is a gap in the data rather than evidence about the Pokémon.
- **An origin game PKSE has no tables for** (Pokémon GO, an unknown version byte) produces one
  **Info** note and no findings.
- **Cross-game transfers are NOT judged.** PKSE's own bank moves Pokémon along routes no official
  transfer offers, and flagging its output would be flagging the feature. Where a transfer matters to
  the checker, the *met location* is the concern, not the origin marker.

### The matching machinery

```
checkEncounter(pk, report)
  ├─ resolve the origin version -> getEncounterTable(version)      nullptr -> one Info note, return
  ├─ build the pre-evolution chain (Candidate[])
  ├─ if the mon has an egg location, or IS an egg -> checkEgg(...) / matchingGiftEgg(...)
  ├─ sweep(table, chain, versionBit, metLocation, metLevel, …)
  │     └─ collects a bitmask of VIOLATIONS across all otherwise-matching rows
  └─ reportViolations(violations, pk, report)
```

Helpers worth knowing before you touch this file:

| Helper | Purpose |
|---|---|
| `isHomeRemappedLocation(loc)` | a location HOME rewrote on transfer |
| `isExternalTransferLocation(group, loc)` | the "came from another game" marker |
| `hasEggLocation(loc)` | non-zero, and not BDSP's `0xFFFF` sentinel |
| `breedable(table, species)` | the per-game breedable bitset |
| `canHatchAt(egg, loc, versionBit)` | an egg hatches wherever the player is walking, so this is a permitted **set** |
| `levelMatches(row, metLevel)` | resolves `ENCOUNTER_FLAG_BOOST60` etc. |
| `encounterLocationMatches(rowLoc, metLoc)` | resolves the Wild Area and roamer region sentinels |
| `getTimeCapsuleScope(group, hasMetData, chain, …)` | which Gen 1/2 games a record can come from, and whether the other generation is asked |
| `mergeMatchState(state, other)` | folds a second sweep into the first, keeping the further explanation |
| `eggExplainsWithoutMetData(table, pk, chain, …)` | an egg as the story for a record with no met data — only where it holds |

`EggRule::eggLocation == 0` means the **format** has no such field — Gen 3 alone — which is why an
egg there has to be inferred instead of read.

### Which failure gets reported — and the order is a DECLARATION

`Stage` ranks explanations, and "furthest" is simply the larger enumerator. **The evolution floor
outranks the level band** — an ancestor's row that fits the met level says more about the record
than the Pokémon's own row missing it — so `STAGE_EVOLUTION` is declared AFTER `STAGE_LEVEL`. It was
first placed between `STAGE_LOCATION` and `STAGE_LEVEL`, which ranked it above the location (the
Seadra fix) and silently BELOW the level band — the enum contradicted the comment written above it.
Nothing on a real save exposed that; it was found by reading the two against each other.

The case that pins it is FireRed's **Route 16**: wild Rattata at 18-22, wild Raticate at
23-25, and Raticate evolves at 20. A Raticate met at 18 and now level 19 fits the Rattata row and
misses its own; what it cannot have done is evolve. With the old order it was told *"no encounter for
Raticate at Route 16 is met at level 18 (nearest is 23-25)"*. Reordering changed no finding on any of
36 real saves — it only changes which true sentence the user reads.

### Gen 1 and Gen 2: two generations, one question

**The Time Capsule carried Pokémon both ways**, and neither format records which game a Pokémon came
from, so PKHeX asks BOTH generations' encounters of any Gen 1 or Gen 2 record that could have crossed
(`EncounterGenerator12`). Asking only the format's own group reported Crystal's Articuno — a Red one
that crossed over — as a species Johto never had. `getTimeCapsuleScope` answers two narrowing facts,
both PKHeX's (`EncounterEnumerator2`, `GBRestrictions`):

- **Crystal's caught data pins the game.** A record carrying it was caught or hatched in Crystal, so
  only Crystal's rows are asked — Gold and Silver never write the word, and a Gen 1 game has nowhere to
  keep it.
- **A Gen 2 record WITHOUT caught data came from Crystal only if it could have visited Gen 1**,
  because crossing the Time Capsule is the one way to clear the word: its chain has to reach a Gen 1
  species (`CanVisitGen1`). That is what keeps a caught-data-less Celebi off Crystal's GS Ball template.

When the record could have crossed, the other generation's table is swept as well, and the two
states merge (`mergeMatchState`): a match in either explains the Pokémon (*"Matches a static encounter
in Red/Blue/Yellow, traded through the Time Capsule"*), and failures name both generations.

**Without met data an egg is only POSSIBLE.** Gold and Silver write nothing when an egg hatches, so
PKHeX's `VerifyEncounterEgg2` accepts a Gen 2 egg without caught data outright — which is what Elm's
Togepi from Gold needed. But nothing in such a record evidences an egg the way met level 0 at a hatch
location does, so `eggExplainsWithoutMetData` explains a Pokémon only when the story holds (a breedable
base, at least the hatch level, past every evolution level) and otherwise says nothing, leaving the
sweep's finding in place. Routing every breedable species to the egg path instead turned *"Butterfree
at level 9 would have to evolve from Metapod at 10"* into a sentence about a Caterpie egg.

Crystal's own rules, each a finding the console reported on a Pokémon PKHeX calls legal:

- **An in-game trade is met at `LinkTrade2NPC` (126) at met level 0** (`EncounterTrade2`), and its
  level is the LOWEST the received Pokémon can have, with no ceiling. The generator emitted every Gen 2
  trade at location 0 with a single level, so Kyle's Onix (as a Steelix) and Kirk's Shuckle both read
  as "not a place". The rows now carry `ENCOUNTER_FLAG_MET_LEVEL_ZERO`: the met level must be 0 and the
  span bounds the CURRENT level.
- **Raikou, Entei and Gold/Silver's Suicune roam**, so their template's location 2 is only where they
  start. `EncounterStatic2.IsRoaming` is COMPUTED (those species, anywhere but Tin Tower), not a
  property the generator could read, which is why it was missed. `ENCOUNTER_LOCATION_ROAMER2` carries
  PKHeX's sixteen Johto routes.
- **A Crystal egg hatches anywhere up to location 95.** `HatchLocationC` (16) is only what PKHeX's
  `EncounterEgg2` STAMPS when it builds one; its verifier accepts any met location up to 95 ("Any met
  location is fine"). Taking the stamp for the rule flagged every egg hatched elsewhere — a Cleffa at
  19, a Togetic at 8.
- **The Virtual Console GS Ball Celebi is ONE template, not an array** (`Encounters2.CelebiVC`, Ilex
  Forest, level 30), and `assert_all_arrays_used` only knew arrays. It now also asserts single
  `static readonly Encounter… X = new(…)` declarations, which is how the omission would have been caught.

**Gen 1's events are deliberately NOT rows.** Every one fixes a trainer: the Virtual Console Mew is OT
"GF" with trainer id 22796, the Stadium gifts 1999/2000, the tours fixed OT names (`EncounterGift1`).
`EncounterRow` has no trainer fields, so as a plain row the VC Mew would make every **glitch Mew** read
legal — and the level 7 Mews in real Red, Blue, Silver and Crystal saves are exactly that. They stay
Invalid, as PKHeX calls them; the cost is that a genuine GF Mew is flagged too until rows can carry a
trainer id.

## Where the report surfaces

**The details modal.** A one-line summary pinned below the scrolling info column; `R` or a tap
(touch id `95`) opens the full issue list overlay. When the report is `empty()` there is nothing to
show, so the button is greyed and does nothing.

```cpp
const Legality::Report legalityRep = Legality::analyze(*p, p->getGameGroup());
```

Note it passes the **entity's** group, not the save's — a bank slot holds a foreign mon and must be
judged as what it is.

**The box summary panel**, bottom-left: *"Legality: no problems found"* (green) or *"Legality: N
issue(s)"* (red), answered as the cursor moves rather than one Pokémon at a time. It says **"no
problems found", never "Legal"** — Layer 3 errs toward silence, so a clean report means nothing was
found, not that HOME will accept it. It is not a tap target of its own (the whole panel already is
one); the issue list stays on `R` in the editor.

**Reading findings across a whole box.** When you run the checker over real saves to judge a change,
treat findings as **warnings, not failures**, and the reason is worth internalising:

> PKSE can edit saves, so a user's box may genuinely hold something illegal. **The value is the
> rate**: one Invalid in a box of thirty is a Pokémon; thirty is a bug.

That framing is what would have caught the Gen 1 saturated-EV misread immediately.

## Adding a check

1. **Decide the layer.** Field range → L1. Cross-field or species-data consistency → L2. "Could this
   have been obtained" → L3.

2. **Decide the severity, honestly.**

   | Severity | Means |
   |---|---|
   | `Invalid` | The games cannot produce this. You are certain. |
   | `Warning` | Suspicious, or the reference itself is not certain (the Let's Go EV case). |
   | `Info` | A fact worth having: which encounter matched, or *a check could not run*. |

   **An unrunnable check must produce an Info note, never silence.** Silence is indistinguishable
   from a pass.

3. **Gate on capability, not on a magic zero.**

   ```cpp
   if (!pk.hasBall()) { /* nothing to check */ }
   else if (pk.ball() > MAX_BALL) add(r, Severity::Invalid, …);
   ```

4. **Ask the format for its limits.** `maxIV()`, `maxEV()`, `getMaxNicknameLength()` — never a
   hardcoded 31 / 252 / 12.

5. **Write the message so it names the value.**

   ```cpp
   add(r, Severity::Invalid, std::string(statName(i)) + " EV over 252 (" + std::to_string(ev[i]) + ")");
   ```

   "EV out of range" sends the user hunting. "Atk EV over 252 (255)" does not.

6. **Verify against real saves before shipping it.** Run the analyzer over every Pokémon in real
   saves, one per generation at least, and look at the rate (above). The sharpest version of that test
   is a Pokémon genuinely caught on real hardware: the analyzer must produce **no** Invalid finding on
   it, and a new check that false-flags shows up immediately.

## The failure mode to avoid

Every legality bug in this codebase's history has the same shape:

> **A missing table entry read as a definitive negative.**

`getLearnableBits()` returns `nullptr` for a species+form absent from a game. That means *"PKSE has
no data"*, not *"it can learn nothing"* — and the difference matters twice over, because the bank's
transfer sanitizer **deletes** whatever `isLearnable()` denies.

When you add a lookup to a check, write down what `nullptr` / `0` / `""` means from that lookup, and
branch on it separately from the real negative.

---

## Rules: Layer 3 and the encounter tables

The sections above describe the machinery; these are the rules it has to obey, with the measurement
or the defect each one came from.

### Scope

Three layers, all **informational** — PKSE never refuses an edit because a Pokémon looks illegal.
Layer 3 covers **all nineteen groups**, keyed by the mon's ORIGIN version rather than the save it sits
in, walks the pre-evolution chain, and matches ANY-of.

### Gen 1 and Gen 2 are checked on different terms, and the record decides which

A PK1 stores no met location and no met level at all, and Gold/Silver leave Gen 2's caught-data word
zero — only Crystal writes it. So `hasMetData()` is the switch: where it is true the ordinary check
runs, and where it is false the sweep does not compare the location (there is nothing to compare, and
the 0 those accessors return would reject every row) and compares the row's level FLOOR against the
Pokémon's CURRENT level instead of its met level. A Pokémon only ever grows, so "caught at or above
this row's minimum" is the strongest true statement available — it catches a level-2 Mewtwo and
little else. **That is deliberately weaker than the check every other generation gets**, and it is
worded differently for that reason; a record that cannot say where or when it was caught cannot be
held to either. The Time Capsule scope, Crystal's traps and Gen 1's events are in "Gen 1 and Gen 2:
two generations, one question" above.

### Eggs

**An egg's met level is not the level it hatches at**, and conflating them is what made every bred
Gen 3 Pokémon read as impossible. PKHeX keeps the two apart on purpose: `EggLevel23 = 5` is the
CURRENT level an egg hatches at in Gens 2 and 3, while `EggStateLegality.GetEggLevelMet` gives the MET
level as **0 for Gens 3 and 4** and 1 from Gen 5 on (Crystal 1; Gold and Silver record none). PKSE's
`EggRule` once carried 5 for both Gen 3 groups and Gen 2, and 1 for Gen 4. Consequences, all silent: a
genuinely bred Gen 3 Pokémon never reached the egg path and was reported as having no encounter at
all, and the Gen 3 rebuild stamped a met level the games never write — which the checker then
ACCEPTED, because both halves read the same wrong constant. **A bug that is self-consistent across a
writer and its reader produces no finding anywhere**; the only thing that catches it is a real save (a
Ruby Wynaut, met level 0 on Route 111) or the source it was derived from.

**Met level 0 is what marks a Gen 3 egg**, which is why the format needs no egg-location field to be
readable: no Gen 3 encounter is met at level 0, so met level 0 at a place an egg may hatch is a pair
only breeding produces. Gen 2 is the same shape one level up (Crystal: met level 1 at
`HatchLocationC`).

**An `EggRule` carries three legal egg locations, not two.** Gen 5 is why: PKHeX's
`IsEggLocationBred5` accepts the daycare, the ordinary link trade AND the id Spin Trade writes
(*"incorrectly used ... two legal values!"*, its own comment). Everywhere else the second alternate is
0. **Widening that row is not a safe edit** — the `EGG_RULES` tuples are positional, and adding the
third entry turned an `EGG_RULES[game][2]` that meant "hatch level" into one that meant "second
alternate", quietly moving BDSP's two gift eggs to met level 0. `hatch_level()` exists so the next one
cannot happen, and the generated diff is what caught this one.

**Every generation has a hatch-location set and PKHeX ships all of them** — `EggHatchLocation3`
through `9`, not just 8/8b/9 as this chapter once said. Acting on that wrong reading cost two things:
Gens 4 through 7 got no hatch check at all, and Gen 3 got a mask holding exactly ONE location, on the
belief that a Gen 3 egg's met location is stamped where it was RECEIVED. It is not. **An egg is met
WHERE IT HATCHED in every generation, Gen 3 included** — the one hatched Pokémon in a real Ruby save
is a Wynaut met on Route 111, several routes from the Route 117 day care — and `LocationPermitted3`
accordingly permits 103 places for FR/LG and 100 across R/S/E. `canHatchAt` still reads an ABSENT mask
as "not checked" rather than "nowhere", because that is the only safe reading of missing data, but
nothing in PKSE relies on it any more. Four things the normalisation has to get right, none of them
guessable:

- **PKHeX's version bits are not PKSE's**, and the mapping is written out per game in
  `HATCH_MASK_SOURCES` rather than inferred: PKHeX groups R/S and keeps Emerald apart, numbers White
  before Black, and indexes Gen 4 as DP/Pt/HGSS.
- **Gen 6 is an arithmetic RULE, not a table** — every EVEN location in a range, minus one unused id —
  so it is transcribed as the rule. A 185-entry hand-typed span nobody can proofread is the shape that
  goes wrong silently.
- **Poké Pelago (30016) is a hatch location no mask can hold.** `EggHatchLocation7` accepts it
  alongside its byte table, and a byte-per-location mask reaching 30016 would be thirty kilobytes of
  zeros, so it rides along as `EggRule::hatchLocationExtra`.
- **Gen 2 is the one genuinely fixed case, and it is Crystal-only.** Gold and Silver write no met data
  at all; PKHeX's `EncounterEgg2` stamps `HatchLocationC` and met level 1 for Crystal and nothing for
  G/S.

### Which failure is reported: the evolution floor outranks the met location

A Pokémon is swept against every row of every ancestor and only the furthest STAGE any of them reached
is described, so that ranking decides the sentence the user reads. A level 15 Seadra met on Route 24
was told *"Route 24 is not a place Seadra can be encountered in FireRed"* — true of Seadra, irrelevant
to this Pokémon, and it named the one field the record had RIGHT. Horsea is fished on Route 24; what a
level 15 Seadra cannot have done is evolve, which takes 32. Seadra's own rows, at seventeen other
places, failed on LOCATION and outranked Horsea's row failing on the floor.

Two halves, and both are needed: the floor is asked **after** the location test, so it is only ever
claimed about a row matching where the Pokémon says it was met; and `STAGE_EVOLUTION` ranks **above**
both `STAGE_LOCATION` and `STAGE_LEVEL`, since the ancestor's row may cover the met level perfectly
well (the declaration order and the Route 16 case are in "Which failure gets reported" above). Neither
half changes which rows MATCH — a row failing the floor was skipped before and is skipped now, so no
finding is created or destroyed, only worded correctly. It was wrong in every generation with a
level-gated evolution, not only Gen 3.

**An evolution level is a claim about the CURRENT level, and it has to be chain-aware.** Layer 3
matches an evolved Pokémon against its pre-evolution's slots, so it also has to ask whether the
Pokémon could have made the trip: a Hoothoot row explains a Noctowl only from level 20. `EvoEdge`
carries that level (0 when the evolution is a stone, a trade or friendship, which is most of them),
taken as the MINIMUM across every method reaching one destination — a species with both a level-up
and a stone route is not level-gated at all, and taking the first method would invent a floor the
games do not impose. **Flat "Noctowl must be level 20" would be wrong**: Gold and Silver put wild
Noctowl on Route 2 at level 7, alongside Butterfree, Beedrill, Pidgeotto, Ledian and Ariados, every one
of them below its own evolution level. So the floor belongs to a CHAIN POSITION, not to a species —
position 0 always answers 0, and only an ancestor's row has to pay. The egg path asks the same
question of the chain's base, which is what finally makes a level 12 Noctowl impossible in FireRed:
the egg is a Hoothoot either way.

**`buildChain` asked for form 0 twice.** Its own comment described trying the Pokémon's form and
falling back to form 0; both calls passed 0, so the fallback was the only branch that ever ran and an
Alolan Raichu resolved to a KANTONIAN Pichu. Fixing it removed a real false positive on a Z-A Vivillon,
whose form-6 lineage had been resolving to form 0's. Which form answered is now remembered, because the
evolution level has to be read off the same edge.

### Origins

**A format that records no origin falls back to its own group**, which is the same rule
`checkRelearnMoves` already applies: a PK1 and a PK2 store no version byte anywhere, so the group
whose format it is in is the only answer available. And because such a record cannot say WHICH of that
group's games it came from, every one of them counts — the version mask is all bits, so a row for any
of Red, Green, Blue or Yellow is a row this Pokémon could have come from. Red/Green/Blue/Yellow is the
only group of FOUR; PKHeX tags the international Blue's slot data as Green, because they are the same
game, and keeps the Japanese Blue apart.

**An origin from a later generation than the record is impossible, and has to be caught before the
table lookup.** That lookup keys on the version byte alone, so a PK3 stamped with origin 7 resolves to
HeartGold's table and gets measured in a namespace it never used — which shipped as *"location253 is
not a place Bulbasaur can be encountered in HeartGold"*, said confidently about a game the Pokémon was
never in. It is Invalid rather than a skip because nothing legitimate makes one: every PKSE conversion
clamps the origin to a game the destination format has. **Nor is a met location portable in the other
direction**: Pal Park (Gen 3 → 4) and Poké Transfer (Gen 4 → 5) REPLACE it, in the destination's
namespace, while the origin byte still names the game it was caught in — so an ordinary transferred
Geodude read as *"Granite Cave is not a place Geodude can be encountered in FireRed"*, Granite Cave
being what 55 means in Gen 3 and Pal Park what it means in Gen 4. Everything from Gen 5 on carries the
original location across, which is why a Sword Pokémon in a Z-A box still matches its Galar encounter.

### Layer 3 errs toward silence — and what a correct table must still do

**Silence is a design rule, not a limitation.** An origin PKSE has no tables for produces one Info
note and no findings; cross-game transfers are not judged at all (PKSE's own bank makes them, and
flagging its output would be flagging the feature). So a sweep of real saves — every title, not one
sample per generation, which never opens half of them — should be **near-silent**: a wall of findings
means the tables are wrong, not the saves.

**But a clean sweep proves nothing on its own**: a table that matches nothing and a table that matches
everything both look quiet next to a box of ordinary Pokémon. A correct set of tables must therefore
also satisfy six properties, each of which a broken table fails:

1. **A Pokémon moved to a met location no game has draws a complaint.** The negative case: without it,
   a table that accepts anything passes.
2. **A Pokémon posed as the game's own first distribution row matches.** Proves the event rows were
   generated and are reachable.
3. **A Pokémon posed at one of the game's own WILD slots matches.** This is the one that catches a
   **mis-parsed slot table** — the failure mode every new generation risks, since a wrong header
   length or stride still builds, still produces plausible rows, and simply never matches anything,
   invisibly behind a sweep of a save full of foreign Pokémon. Two things make the pose meaningful: the
   donor must not be an unhatched EGG (an egg is routed down the egg path before the sweep is ever
   reached, so it reports nothing and reads as a table failure), and the row must belong to the
   donor's own VERSION — half of every paired generation is version-exclusive (Caterpie is
   HeartGold's, Weedle SoulSilver's), so posing a SoulSilver Pokémon at a HeartGold-only slot fails for
   a reason that has nothing to do with the table.
4. **A Pokémon posed as an EVOLUTION of a wild slot's species, one level below the level that
   evolution needs, draws a complaint.** This exists because real saves never exercise it: the
   evolution floor is the one Layer 3 constraint about the CURRENT level rather than the met level, and
   a box of ordinary Pokémon clears every one of its own evolution levels comfortably. The evolution
   must be one the game cannot produce on its own, or the sweep matches the evolution's own row at
   chain position 0, where no floor applies — correct behaviour, but the property would then hold for
   the wrong reason.
5. **The same pose where that evolution IS catchable elsewhere in the game draws the SAME
   complaint** — the evolution-floor-outranks-location rule above.
6. **The reported Seadra case**, built from the tables with no save at all (Horsea fished on FireRed's
   Route 24, Seadra not), reads correctly in both directions: a level 15 Seadra met there at level 10
   is told about the evolution and not the location, and a level 40 one — an ordinary evolved catch —
   draws nothing at all. Only the pair is worth anything.

**A format with no met location cannot be given a bogus one**, so property 1 does not apply to Gen 1
or Gold/Silver — `setMetLocation` is a no-op there and the sweep does not compare locations at all, so
the Pokémon stays clean because the property is meaningless, not because the table failed to bite. Any
check of it should say how many it skipped. Property 3 poses such a donor on its own terms too: it
raises the donor's LEVEL to the row's floor rather than its met level, because the floor is what gets
compared. Likewise **every Gen 3 distribution FR/LG can receive is an egg** (below), so property 2 has
no plain event row to pose in Kanto, and that is the data being right.

### Generating `EncounterTable.cpp`

`EncounterTable.cpp` is generated by `tools/gen_encounters.py` and holds wild / static / gift / trade
/ raid / **event** rows in one shape. **New groups are APPENDED to its `GAMES` list, never inserted**
— the tables are emitted in that order, so inserting one rewrites every table after it and buries the
real change in a diff that is the only review gate the generated data has.

The readers, each of which silently produces a table that matches nothing when got wrong:

- **Gen 3 and Gen 4 share one wild-slot reader.** Both store 10-byte slots of `species u16, form,
  slotNumber, levelMin, levelMax, …`; only the area header differs, 4 bytes in Gen 3 and 6 in Gen 4
  (PKHeX's `EncounterArea4` slices from `data[6..]`). Gen 4's trailing four bytes say which slot a
  swarm REPLACES, which is a question about that day's game state and not about the Pokémon in hand.
- **Gens 1 and 2 share one too**: a 4-byte area header then 4-byte slots of `species, slotNumber,
  levelMin, levelMax`, the species a single BYTE. **Gen 2 puts a rate table in front of its slots** for
  every slot type past Surf — the rods, Rock Smash, Headbutt, the Bug Contest — one byte per slot,
  ahead of the slots themselves. Read them as slot data and rates become species.
- **Gens 5, 6 and 7 share ONE wild reader**, and it is not Gen 3/4's: a 4-byte area header (location
  u16, slot type) then 4-byte slots of `species | form << 11` u16, levelMin, levelMax, with the species
  masked to `0x3FF`. `EncounterArea5`, `EncounterArea6XY`, `EncounterArea6AO` and `EncounterArea7` are
  the same twenty lines four times over. **A zero slot is padding**: Gen 5 areas carry a fixed slot
  count per type and leave the unused ones zeroed — 574 of Black's 2974 — which would otherwise become
  rows claiming species 0 is catchable.
- **Ruby/Sapphire/Emerald is the only group of THREE, so `versions` is not a two-bit field.** Every
  other group in every generation is a pair or a single title. PKHeX also spells its Gen 3 games
  `R`/`S`/`E` and their pairing `RS` where PKSE's enum says `RU`/`SA`/`EM`, so the tokens are mapped by
  an explicit table — `"RS"` contains neither `"RU"` nor `"SA"`, and anything clever would silently
  hand Emerald every Ruby-only legendary.
- **One PKHeX file can be two PKSE groups.** Diamond/Pearl and Platinum share `Encounters4DPPt.cs`
  while PKSE splits them (different save layouts), so a version token has to be able to say "belongs
  to no game in this group" — distinct from "unrecognised", which keeps the row. Without that
  distinction Platinum's table inherits Dialga.
- **The version is not always the third argument.** B2W2's Join Avenue trades take only an OT-name
  array and the version, so `version_from_args` scans for the argument that IS a version token rather
  than indexing a position. It cannot mistake a PID for a game, because a PID is not one of the group's
  version names — and a template with no version token at all (N's Pokémon) belongs to the whole group,
  which is what PKHeX fixes on those records anyway.
- **Two things are not `new(...)` at all.** ORAS writes its six Cosplay Pikachu as C# record copies
  (`BaseCosplay with {Form = 1, …}`), and without resolving them the scanner walks past to the `new(`
  inside the override's own move list and parses THAT as the entry — an entry with no Species, which is
  how it announced itself. `parse_template` + `parse_array(bases=…)` resolve them, and the count is
  asserted. X/Y's **Friend Safari is not in the binary either**: PKHeX appends it as a hand-written area
  at location 148, so reading only the `.pkl` leaves it empty and every Friend Safari Pokémon — a great
  many of them — reads as impossible.
- **`Location` is OPTIONAL in the C# record.** Sun/Moon has a gift EGG whose only placement is its
  `EggLocation`, because a gift egg is met wherever the player hatches it.
- **A single template must be checked like an array.** `assert_all_arrays_used` once knew only arrays,
  which is how Crystal's standalone VC GS Ball Celebi went missing; it now also asserts single
  `static readonly Encounter… X = new(…)` declarations.

**A roamer is met where it was cornered**, not where its template starts it, so Latias/Latios and the
six Gen 4 roamers carry a region SENTINEL rather than a location (Gen 2's is
`ENCOUNTER_LOCATION_ROAMER2`). Matching their template's Location literally would flag every roamer
anyone has ever caught. Gen 4 additionally narrows the set by the GroundTile the Pokémon was standing
on; PKSE takes the union of grass and water, which can only widen what is accepted.

### Mystery Gift distributions

**Distributions cover every generation PKSE has tables for** — Gen 3 through Gen 9. **A Mystery Gift
IS an encounter template**: distributions are emitted as ordinary `EncounterRow`s of kind
`ENCOUNTER_KIND_EVENT` rather than into a table of their own — a parallel table would have duplicated
the species index, the pre-evolution walk and every constraint check, and the two would have drifted.
For Gens 5–9 PKHeX ships the database as flat arrays of fixed-size records
(`Resources/legality/mgdb/*.pkl`), one per format, so they need no container decoding. Gens 3 and 4
need their own readers because neither is that shape:

- **Gen 3's are a hand-written C# array**, not a database. `EncountersWC3.cs` spans BOTH Gen 3 groups
  (tokens run R/S/RS for Hoenn, FRLG for Kanto, EFL and Gen3 across them), so each group filters the
  same list. Every one is met at **location 255** in a Poké Ball; only species, level and version vary.
  `Encounter_WC3` is a spread of the four component arrays — reading it as well doubles every row.
  Pokémon Center New York and Japan are deliberately absent: PKHeX builds those from their own
  resources and they are a different shape again.

  **Each group has to recognise the other's version tokens**, and the reason is the generator's own
  fallback: an unrecognised token means "every game in this group", which is right for a template
  carrying no version at all and catastrophic here. Without those aliases FireRed and LeafGreen
  carried **59** distributions only Ruby and Sapphire ever received, Hoenn carried 8 of Kanto's, and
  Ruby and Sapphire took the 15 `EFL` eggs that went to Emerald alone — `EFL` is the whole of FR/LG but
  only Emerald of the trio. PKHeX refuses each of those outright: `EncounterGift3.IsMatchExact` opens
  with *"Gen3 Version MUST match"*. The failure is a false NEGATIVE, so it never flagged a legal
  Pokémon; it quietly accepted ones no cartridge could produce.

  **Every Gen 3 distribution FR/LG can receive is an EGG.** PKHeX names FR/LG, EFL or Gen3 only inside
  its `Eggs` array — `Common`, `International` and `Japan` are Ruby/Sapphire throughout — so Kanto
  having no plain event row is the data being right, not a parse that came up empty.
- **A Gen 4 card wraps a whole PK4, and its met location is BIASED BY 3000 in the egg-location
  field.** `PCD.IsMatchExact` compares `wc.EggLocation + 3000` against the Pokémon's met location, so
  the raw values read 60 and 1 rather than anything that looks like a place; the game adds the bias on
  redemption. The PK4 is stored DECRYPTED (PKHeX only re-encrypts on write), so the ordinary PK4
  offsets read straight through. **Nothing stamps Platinum**: a Gen 4 card carries the origin version
  itself rather than leaving it to the redeeming game, and every one says Diamond or HeartGold — so
  Platinum's table has no distributions and a Platinum-origin event Pokémon is not judged.
- **`pgf.pkl` is not a flat array.** Its records are packed at the FRONT and one "receivability" byte
  per record follows them all (PKHeX `PGF.GetArray`: count = length / (Size + 1)), so reading it as
  fixed-size records leaves a remainder and refuses the whole file.
- **That trailing byte is what separates the two Gen 5 pairs.** One database serves Black/White and
  Black 2/White 2 both, and its low nibble is PKHeX's `RestrictVersion`, one bit per Gen 5 game in
  GameVersion order (White, Black, White 2, Black 2); zero means no restriction. Without it each
  group's table carries the other's distributions — 143 rows for BW and 89 for B2W2 become the same
  232 for each.
- **Gens 6 and 7 keep their database in two files.** `wc6.pkl` holds bare cards and `wc6full.pkl`
  holds full ones with the card at a fixed offset inside (`Size - WC6.Size`), and PKHeX unions them
  (`WC6Full.GetArray`). Reading only the bare file loses every distribution archived with its metadata,
  which is most of them.
- **The Pokémon card type is spelled differently in every generation.** It is 1 in PGT/PCD (2 is an
  egg, 3 an item), 1 in PGF, 0 in WC6 and WC7, 0 in WB7, and 1 in WC8/WA8/WB8/WC9/WA9. Reading the
  wrong value does not fail; it silently swaps items for Pokémon. **WB7 inverts it**: Let's Go predates
  PKHeX's `GiftType` enum, so its `IsEntity` is `CardType == 0` and its `IsItem` is `CardType == 1`.
  Assuming one convention kept every Let's Go *item* card and threw away all fifteen of its Pokémon —
  silently, because the result was simply an empty table. PGF's shiny byte is its own vocabulary too:
  0 means NEVER there and Random in Gen 6's `ShinyType6`.
- **WC9 and WA9 store Gen 9 INTERNAL species numbering**, not National Dex. The generator converts
  through PKHeX's `SpeciesConverter.Table9InternalToNational`; reading the field raw gives a plausible
  wrong species. Its deltas are written zero-padded (`065`, `-07`), so parsing them with `int(v, 0)`
  reads `010` as octal 8.
- **Gen 3 has no mgdb pickle.** Its distributions live in the hand-written arrays above, so a FR/LG
  fateful Pokémon that matches none of them still takes the softened "event Pokémon are not checked"
  path. That is a gap in the data, not a verdict.

**A distribution egg never reaches the sweep** — every EGG row is skipped there, so `matchingGiftEgg`
on the egg path is the only thing that can match one, and it has to do it in two shapes. Where the
format carries an egg-location field, that field IS the distribution's signature and a hatched Pokémon
still has it, so the row is found by comparing it (and an egg location of 0 is "none", never a value to
compare, or every ordinary record would pair with a row that records none either). **Gen 3 has no such
field**, so the only record that can name a distribution is an egg that has NOT hatched: it sits at the
event location (255) carrying the row's met level, which is exactly what PKHeX compares before it gives
up on a hatched one — `EncounterGift3.IsMatchExact` answers `if (hatchedEgg) return true; // defer egg
specific checks to later`. A hatched Gen 3 gift egg is therefore indistinguishable from a bred one here
for the same reason it is there. Without the second shape, Gen 3's **66** distribution-egg rows could
never match anything at all, which looks exactly like never having generated them.

**Only an EVENT row may be named, and that distinction cost a false positive.** The same matcher
reaches the games' own in-game gift eggs — that is what it was written for, so that Riolu, which BDSP
cannot breed, is not accused of being an impossible egg. Several of those rows carry the **day care's
own egg location** (SM/USUM's Eevee gift is 60002), so a note attached to every match told a player who
had simply bred an Eevee that they had been handed a distribution. The suppression still accepts any
gift row; only `kind == ENCOUNTER_KIND_EVENT` earns the note.

**A HOME gift is met at 30018 by definition.** The GO/HOME abstain (`isExternalTransferLocation`)
used to `return` before the sweep on the grounds that no row could carry those ids. Adding the
distributions made that false, and it threw away every HOME-gift row unread. It is a **fallback** now:
the sweep runs first and the note is only emitted when nothing matched. On one real Violet save that
turned three "not modelled" Pokémon into three positively identified distributions.

### The HOME tracker

**The HOME tracker is the one transfer fact a save carries that the games cannot forge.** HOME stamps a
per-Pokémon u64 on anything that passes through it, so a Gen 4 Pokémon sitting in a Violet box must
have one whatever its origin byte says. `hasHomeTracker()` gates it for the same reason every other
capability predicate exists — 0 is the real "never tracked" value. **The offset differs per format**:
`0x135` for PK8/PB8 (G8PKM), `0x14D` for PA8, `0x127` for PK9/PA9. The finding is a **Warning, never
Invalid**, because PKSE's own bank produces trackerless transfers legitimately.
