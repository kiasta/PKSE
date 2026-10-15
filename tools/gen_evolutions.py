#!/usr/bin/env python3
"""Generate include/Pokemon/EvolutionTable.h + src/Pokemon/EvolutionTable.cpp -- the
per-game REVERSE evolution lineage (what a species+form evolved FROM), plus the per-game
FORWARD table of trade evolutions (what a species+form becomes when it is traded).

Layer 3 legality needs this at runtime. An encounter template records the species that
was actually caught, so a Charizard met on Route 3 has to be matched against the
*Charmander* slot there; without the lineage every evolved Pokemon reads as "no
encounter produces this species". gen_learnsets.py already walks the same lineage, but
it folds the result into its move bitsets at generation time and never emits the edges,
so there is nothing to reuse at runtime.

Source: PKHeX's evos_<game>.pkl, read exactly as gen_learnsets.read_evolutions does
(EvolutionReversePersonal.GetLineage -- walk every source species+form, register the
reverse edge for each destination it leads to, first source wins). This script imports
those helpers rather than restating them, so the two tables can never disagree about
what an ancestor is.

The lineage is PER GAME because the games disagree: Pichu is absent from Let's Go, so a
Pikachu there did not evolve from one, and a regional form's chain differs from its
Kantonian counterpart's. Edges whose source is not present in that game are dropped for
the same reason gen_learnsets skips them.

Regenerate:  python tools/gen_evolutions.py
Pulls the PKHeX resources it reads from GitHub on demand (tools/pkhex_source.py);
no local PKHeX checkout required.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# gen_learnsets owns the PKHeX personal/evolution binary readers; importing keeps the
# two generators reading the lineage through one implementation.
from gen_learnsets import (  # noqa: E402
    BYTE, EVO_ANY_FORM, EVO_INDEX_GAME, EVO_RESOURCE, EVO_SIZE, EVO_SPECIES_INDEXED, GAMES_TO_EMIT,
    GAME_VERSION_IDS, MAX_EVO_DEPTH, Personal, Personal1, Personal2, Personal3, _load,
    bin_entries, read_evolutions,
)
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Pokemon", "EvolutionTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Pokemon", "EvolutionTable.cpp")


def build(game, pers, index_pers):
    """Reverse lineage for one game -> sorted [(dstSp, dstForm, srcSp, srcForm, level)].

    An edge is kept only when BOTH ends are present in the game: an ancestor the game
    does not have is not an ancestor you could have evolved from there.

    `level` is the lowest CURRENT level the destination can exist at, having come up this
    edge -- 0 when the evolution is not gated on a level (a stone, a trade, friendship).

    `index_pers` is the table the binary is ADDRESSED by, which is the game's own for every
    game but Sun/Moon -- see EVO_INDEX_GAME.
    """
    rev = read_evolutions(EVO_RESOURCE[game], pers, index_pers)
    out = []
    for (dsp, dform), (ssp, sform) in rev.items():
        if not pers.present(dsp, dform) or not pers.present(ssp, sform):
            continue
        out.append((dsp, dform, ssp, sform, min(rev.levels.get((dsp, dform), 0), 100)))
    out.sort()
    return out


# PKHeX's three trade EvolutionTypes (Legality/Evolutions/Methods/EvolutionType.cs).
PKHEX_TRADE = 5
PKHEX_TRADE_HELD_ITEM = 6
PKHEX_TRADE_SHELMET_KARRABLAST = 7
# ...mapped onto PKSE's TradeEvolutionTrigger. PKHeX is a LOGIC source, not a naming source.
TRIGGER_FOR_PKHEX_TYPE = {PKHEX_TRADE: 0, PKHEX_TRADE_HELD_ITEM: 1, PKHEX_TRADE_SHELMET_KARRABLAST: 2}
TRIGGER_NAMES = ["Trade", "HeldItem", "PartnerSpecies"]

# Gen 3's item ids for the six evolutions Gen 2 introduced. Gen 2's own binary records them as a
# plain Trade with no argument, so the ITEM the games require is not in the data -- see
# derive_gen2_items().
GEN2_ITEM_TRADE_COUNT = 6


def load_item_names(relative_path):
    """PKHeX item text -> [name], DECODED BY ITS BOM.

    The resources are not uniform -- the Gen 3 list is UTF-16 while the modern one is UTF-8 -- and
    almost any even-length byte sequence is valid UTF-16, so trying encodings in order decodes one
    as the other instead of raising.
    """
    with open(pkhex_path(relative_path), "rb") as handle:
        raw = handle.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16").splitlines()
    return raw.decode("utf-8-sig").splitlines()


def comparable_item_name(name):
    """An item name reduced so two generations' spellings of the same item compare equal."""
    return "".join(character for character in name.lower() if character.isalnum())


def read_trade_evolutions(game, pers, index_pers):
    """Forward TRADE evolutions for one game -> sorted rows.

    Each row is (speciesId, formId, destinationSpeciesId, destinationFormId, trigger, argument).

    An 8-byte EvolutionMethod is `type, pad, argument u16, species u16, form, level` (PKHeX
    EvolutionSet.GetMethod). Only the three trade types are kept. THE LEVEL BYTE IS IGNORED, and
    deliberately: PKHeX never reads it for a trade method (EvolutionMethod.Check returns through
    ValidNotLevelUp before the level comparison), and it is not always 0 -- Slowpoke -> Slowking
    carries 37 from Gen 7 on, inherited from the Slowbro row beside it. Gating on it would refuse
    a level-20 Slowpoke the games evolve happily.

    A destination form of 0xFF means "keep the source form" (PKHeX's AnyForm), resolved here
    against the form actually being walked, so every emitted row names a concrete form.
    """
    entries = bin_entries(_load(BYTE("evolve", "evos_%s.pkl" % EVO_RESOURCE[game])))
    speciesIndexed = EVO_RESOURCE[game] in EVO_SPECIES_INDEXED
    rows = []
    last = min(pers.maxsp, pers.count - 1)
    for speciesId in range(1, last + 1):
        for formId in range(pers.form_count(speciesId)):
            entryIndex = speciesId if speciesIndexed else index_pers.form_index(speciesId, formId)
            if entryIndex >= len(entries):
                continue
            entry = entries[entryIndex]
            for offset in range(0, len(entry) - (EVO_SIZE - 1), EVO_SIZE):
                destinationSpeciesId = struct.unpack_from("<H", entry, offset + 4)[0]
                if destinationSpeciesId == 0:
                    break
                trigger = TRIGGER_FOR_PKHEX_TYPE.get(entry[offset])
                if trigger is None:
                    continue
                destinationFormId = entry[offset + 6]
                if destinationFormId == EVO_ANY_FORM:
                    destinationFormId = formId
                # Both ends must exist in THIS game, exactly as the reverse table requires: an
                # evolution into a species the game does not have is not one you can perform here.
                if not pers.present(speciesId, formId) or not pers.present(destinationSpeciesId, destinationFormId):
                    continue
                argument = struct.unpack_from("<H", entry, offset + 2)[0]
                rows.append((speciesId, formId, destinationSpeciesId, destinationFormId, trigger, argument))
    rows.sort()
    return rows


def resolve_partner_species(game, rows):
    """Fill in the partner for the PartnerSpecies rows, which the binary does not carry.

    PKHeX spells the pairing into the method type itself (TradeShelmetKarrablast) and leaves the
    argument 0, so the partner is derived rather than read: a game with this trigger has exactly
    two such rows, and each one's partner is the other's source species. The count is asserted
    rather than assumed -- a third row would mean the pairing is no longer a pair, and silently
    pointing two of them at each other would be worse than failing here.
    """
    partnerRows = [row for row in rows if row[4] == TRIGGER_FOR_PKHEX_TYPE[PKHEX_TRADE_SHELMET_KARRABLAST]]
    if not partnerRows:
        return rows
    if len(partnerRows) != 2:
        raise SystemExit("%s: expected 2 partner-species trade rows, found %d" % (game, len(partnerRows)))
    partnerFor = {partnerRows[0][0]: partnerRows[1][0], partnerRows[1][0]: partnerRows[0][0]}
    return [row[:5] + (partnerFor[row[0]],) if row[4] == 2 else row for row in rows]


def derive_gen2_items(gen2Rows, gen3Rows):
    """Gen 2's held-item trade evolutions, which PKHeX's Gen 2 binary does not record.

    evos_g2.pkl spells all ten of Gold/Silver/Crystal's trade evolutions as a plain Trade with a
    zero argument, so the item the games require -- King's Rock for Politoed and Slowking, Metal
    Coat for Steelix and Scizor, Dragon Scale for Kingdra, Up-Grade for Porygon2 -- is simply
    absent. Following the data would put the button on a Poliwhirl holding nothing and evolve it,
    which the game will not do.

    The six are recovered from PKHeX alone rather than transcribed: Gen 3 keeps the same six
    evolutions WITH their items, so each Gen 2 row is matched to the Gen 3 row for the same
    (source, destination) pair, and that row's Gen 3 item id is carried across by NAME into Gen 2's
    own id space. Both name tables are PKHeX's. All six must resolve; a miss raises rather than
    quietly leaving a gate open.
    """
    gen3ItemNames = load_item_names("Resources/text/items/gen3/text_ItemsG3_en.txt")
    gen2ItemNames = load_item_names("Resources/text/items/gen2/text_ItemsG2_en.txt")
    gen2IdForName = {}
    for itemId, itemName in enumerate(gen2ItemNames):
        gen2IdForName.setdefault(comparable_item_name(itemName), itemId)

    gen3ItemForPair = {}
    for speciesId, _formId, destinationSpeciesId, _destinationFormId, trigger, argument in gen3Rows:
        if trigger == TRIGGER_FOR_PKHEX_TYPE[PKHEX_TRADE_HELD_ITEM]:
            gen3ItemForPair[(speciesId, destinationSpeciesId)] = argument

    resolved = []
    resolvedCount = 0
    for row in gen2Rows:
        speciesId, formId, destinationSpeciesId, destinationFormId, trigger, argument = row
        gen3ItemId = gen3ItemForPair.get((speciesId, destinationSpeciesId))
        if trigger != TRIGGER_FOR_PKHEX_TYPE[PKHEX_TRADE] or gen3ItemId is None:
            resolved.append(row)
            continue
        itemName = gen3ItemNames[gen3ItemId] if gen3ItemId < len(gen3ItemNames) else ""
        gen2ItemId = gen2IdForName.get(comparable_item_name(itemName))
        if gen2ItemId is None:
            raise SystemExit("GSC: no Gen 2 item matches Gen 3 item %d (%r)" % (gen3ItemId, itemName))
        resolved.append((speciesId, formId, destinationSpeciesId, destinationFormId,
                         TRIGGER_FOR_PKHEX_TYPE[PKHEX_TRADE_HELD_ITEM], gen2ItemId))
        resolvedCount += 1
    if resolvedCount != GEN2_ITEM_TRADE_COUNT:
        raise SystemExit("GSC: expected %d held-item trade evolutions, resolved %d"
                         % (GEN2_ITEM_TRADE_COUNT, resolvedCount))
    return resolved


def emit_header():
    return '''/**
 *
 * Auto-generated by tools/gen_evolutions.py from PKHeX's evos_*.pkl.
 * DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * Per game, because the chains differ: Pichu is absent from Let's Go, so a Pikachu
 * there has no ancestor, and a Hisuian form's ancestor is not its Kantonian one.
 * An edge is present only when both ends exist in that game.
 *
 * Layer 3 legality is the caller: an encounter template names the species that was
 * caught, so an evolved Pokemon has to be matched against its pre-evolution's slots.
 */
#ifndef PKM_EVOLUTION_TABLE_H
#define PKM_EVOLUTION_TABLE_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Pokemon {

    /// Longest pre-evolution chain the tables can hold (real max is 2 -- Bulbasaur ->
    /// Ivysaur -> Venusaur gives Venusaur two ancestors; the extra slot is slack).
    constexpr int EVO_MAX_CHAIN = %d;

    /**
     * Immediate pre-evolution of (species, form) in `group`.
     *
     * Returns false when the species is the base of its chain, when the group has no
     * table, or when either end is absent from that game -- all of which mean "nothing
     * evolved into this here", never "unknown".
     */
    bool getPreEvolution(Enums::GameVersion group, uint16_t species, uint8_t form, uint16_t& outSpecies, uint8_t& outForm);

    /**
     * Full pre-evolution chain for (species, form), nearest ancestor first.
     *
     * Writes at most `max` entries and returns how many were written. The species
     * itself is NOT included.
     */
    int getPreEvolutionChain(Enums::GameVersion group, uint16_t species, uint8_t form, uint16_t* outSpecies, uint8_t* outForm, int max);

    /**
     * Lowest CURRENT level at which (species, form) can exist in `group`, given that it was
     * OBTAINED as its ancestor `stepsBack` links down the chain.
     *
     * `stepsBack` 0 is the species itself and always answers 0: a Pokemon caught as what it is
     * has no evolution to have satisfied. That distinction is the whole point -- Gold/Silver put
     * wild Noctowl on Route 2 at level 7, thirteen levels below the level a Hoothoot evolves at,
     * so "Noctowl must be level 20" is true of an evolved one and false of a caught one.
     *
     * Every link is asked, not just the last: a three-stage chain has two floors and the binding
     * one is the higher. Returns 0 for a group with no table, which reads as no constraint.
     */
    uint8_t getEvolutionLevelFloor(Enums::GameVersion group, uint16_t species, uint8_t form, int stepsBack);

    /** How a trade evolution is triggered. */
    enum class TradeEvolutionTrigger : uint8_t
    {
        Trade = 0,          ///< trading alone
        HeldItem = 1,       ///< trading while holding one specific item, which the evolution consumes
        PartnerSpecies = 2, ///< trading FOR one specific other species (Karrablast / Shelmet)
    };

    /** One trade evolution a (species, form) can undergo in one game. */
    struct TradeEvolution
    {
        uint16_t speciesId;
        uint8_t  formId;
        uint16_t destinationSpeciesId;
        uint8_t  destinationFormId;
        TradeEvolutionTrigger trigger;
        /// HeldItem only, in THE GAME'S OWN item id space -- Metal Coat is 199 in Gen 3 and 233
        /// from Gen 4 on, so this is read through getItemNameFor(group, ...). 0 otherwise.
        uint16_t requiredItemId;
        /// PartnerSpecies only: the species that has to be on the other end of the trade.
        uint16_t partnerSpeciesId;
    };

    /// Clamperl is the only species with two, one per held item (Huntail / Gorebyss).
    constexpr int TRADE_EVOLUTIONS_MAX_PER_SPECIES = 2;

    /**
     * Every trade evolution (speciesId, formId) has in `group`, written to `outEvolutions`.
     *
     * Returns how many were written, at most `maxEvolutions`. Zero means this species+form has no
     * trade evolution in this game -- either it has none anywhere, or one end of it is absent from
     * this game (Let's Go carries rows for Pokemon it does not have; they are filtered out here).
     *
     * The rows describe what the GAME does, not what the Pokemon can do right now: a HeldItem row
     * is returned whether or not the Pokemon is holding the item. Deciding that is the caller's,
     * so that a button can be shown and disabled rather than hidden.
     */
    int getTradeEvolutions(Enums::GameVersion group, uint16_t speciesId, uint8_t formId, TradeEvolution *outEvolutions, int maxEvolutions);
}

#endif  // PKM_EVOLUTION_TABLE_H
''' % MAX_EVO_DEPTH


def emit_cpp(tables, tradeTables):
    L = []
    L.append('/**')
    L.append(' * Auto-generated by tools/gen_evolutions.py. DO NOT EDIT BY HAND.')
    L.append(' */')
    L.append('#include "Pokemon/EvolutionTable.h"')
    L.append('')
    L.append('namespace Pokemon {')
    L.append('')
    L.append('    namespace {')
    L.append('        // One reverse edge: (species, form) evolved from (fromSpecies, fromForm).')
    L.append('        // Rows are sorted by (species, form) so a lookup can binary-search.')
    L.append('        struct EvoEdge {')
    L.append('            uint16_t species;')
    L.append('            uint8_t  form;')
    L.append('            uint16_t fromSpecies;')
    L.append('            uint8_t  fromForm;')
    L.append('            // Lowest CURRENT level this species can exist at having come up this edge.')
    L.append('            // 0 when the evolution is not gated on a level at all -- a stone, a trade,')
    L.append('            // friendship -- which is most of them, and is the permissive answer.')
    L.append('            uint8_t  level;')
    L.append('        };')
    L.append('')

    for game in GAMES_TO_EMIT:
        rows = tables[game]
        L.append('        const EvoEdge EVO_%s[%d] = {' % (game, len(rows)))
        for i in range(0, len(rows), 3):
            chunk = rows[i:i + 3]
            L.append('            ' + ' '.join(
                '{%4d,%2d,%4d,%2d,%3d},' % r for r in chunk))
        L.append('        };')
        L.append('')

    L.append('        struct EvoTable {')
    L.append('            const EvoEdge* rows;')
    L.append('            uint32_t count;')
    L.append('        };')
    L.append('')
    L.append('        // Resolve a game (or game group) to its lineage. Individual version ids map')
    L.append('        // to their group\'s table -- Sword and Shield share one evolution graph.')
    L.append('        bool lookupTable(Enums::GameVersion group, EvoTable& out) {')
    L.append('            switch (group) {')
    for game in GAMES_TO_EMIT:
        for vid in GAME_VERSION_IDS[game]:
            L.append('                case Enums::GameVersion::%s:' % vid)
        L.append('                    out = EvoTable{ EVO_%s, %d }; return true;' % (game, len(tables[game])))
    L.append('                default:')
    L.append('                    return false;')
    L.append('            }')
    L.append('        }')
    L.append('')
    L.append('        // The edge INTO (species, form), or false when nothing evolves into it here.')
    L.append('        bool lookupEdge(Enums::GameVersion group, uint16_t species, uint8_t form, EvoEdge& out) {')
    L.append('            EvoTable t{};')
    L.append('            if (!lookupTable(group, t)) return false;')
    L.append('            uint32_t lo = 0, hi = t.count;')
    L.append('            while (lo < hi) {')
    L.append('                const uint32_t mid = lo + ((hi - lo) >> 1);')
    L.append('                const EvoEdge& e = t.rows[mid];')
    L.append('                if (e.species < species || (e.species == species && e.form < form)) lo = mid + 1;')
    L.append('                else hi = mid;')
    L.append('            }')
    L.append('            if (lo >= t.count) return false;')
    L.append('            const EvoEdge& e = t.rows[lo];')
    L.append('            if (e.species != species || e.form != form) return false;')
    L.append('            out = e;')
    L.append('            return true;')
    L.append('        }')
    L.append('    }')
    L.append('')
    L.append('    bool getPreEvolution(Enums::GameVersion group, uint16_t species, uint8_t form, uint16_t& outSpecies, uint8_t& outForm) {')
    L.append('        EvoEdge edge{};')
    L.append('        if (!lookupEdge(group, species, form, edge)) return false;')
    L.append('        outSpecies = edge.fromSpecies;')
    L.append('        outForm = edge.fromForm;')
    L.append('        return true;')
    L.append('    }')
    L.append('')
    L.append('    int getPreEvolutionChain(Enums::GameVersion group, uint16_t species, uint8_t form, uint16_t* outSpecies, uint8_t* outForm, int max) {')
    L.append('        int writtenCount = 0;')
    L.append('        uint16_t curSp = species;')
    L.append('        uint8_t curForm = form;')
    L.append('        while (writtenCount < max) {')
    L.append('            uint16_t pSp; uint8_t pForm;')
    L.append('            if (!getPreEvolution(group, curSp, curForm, pSp, pForm)) break;')
    L.append('            // Guard a cyclic data error: a chain that revisits a node would spin here.')
    L.append('            bool seen = (pSp == species && pForm == form);')
    L.append('            for (int chainIndex = 0; chainIndex < writtenCount && !seen; ++chainIndex)')
    L.append('                seen = (outSpecies[chainIndex] == pSp && outForm[chainIndex] == pForm);')
    L.append('            if (seen) break;')
    L.append('            outSpecies[writtenCount] = pSp;')
    L.append('            outForm[writtenCount] = pForm;')
    L.append('            ++writtenCount;')
    L.append('            curSp = pSp;')
    L.append('            curForm = pForm;')
    L.append('        }')
    L.append('        return writtenCount;')
    L.append('    }')
    L.append('')
    L.append('    uint8_t getEvolutionLevelFloor(Enums::GameVersion group, uint16_t species, uint8_t form, int stepsBack) {')
    L.append('        uint8_t floorLevel = 0;')
    L.append('        uint16_t currentSpecies = species;')
    L.append('        uint8_t currentForm = form;')
    L.append('        for (int step = 0; step < stepsBack; ++step) {')
    L.append('            EvoEdge edge{};')
    L.append('            if (!lookupEdge(group, currentSpecies, currentForm, edge)) break;')
    L.append('            if (edge.level > floorLevel) floorLevel = edge.level;')
    L.append('            currentSpecies = edge.fromSpecies;')
    L.append('            currentForm = edge.fromForm;')
    L.append('        }')
    L.append('        return floorLevel;')
    L.append('    }')
    L.append('')

    # ---- forward trade evolutions ---------------------------------------------------
    L.append('    namespace {')
    L.append('        // Trade evolutions, sorted by (speciesId, formId) so a lookup can binary-search to')
    L.append('        // the first row and walk the rest -- Clamperl has two, one per held item.')
    for game in GAMES_TO_EMIT:
        rows = tradeTables[game]
        if not rows:
            L.append('        // %s has none.' % game)
            continue
        L.append('        const TradeEvolution TRADE_%s[%d] = {' % (game, len(rows)))
        for speciesId, formId, destinationSpeciesId, destinationFormId, trigger, argument in rows:
            requiredItemId = argument if trigger == 1 else 0
            partnerSpeciesId = argument if trigger == 2 else 0
            L.append('            {%4d, %d, %4d, %d, TradeEvolutionTrigger::%-14s %4d, %4d},'
                     % (speciesId, formId, destinationSpeciesId, destinationFormId,
                        TRIGGER_NAMES[trigger] + ',', requiredItemId, partnerSpeciesId))
        L.append('        };')
        L.append('')

    L.append('        struct TradeEvolutionTable {')
    L.append('            const TradeEvolution* rows;')
    L.append('            uint32_t count;')
    L.append('        };')
    L.append('')
    L.append('        bool lookupTradeTable(Enums::GameVersion group, TradeEvolutionTable& out) {')
    L.append('            switch (group) {')
    for game in GAMES_TO_EMIT:
        rows = tradeTables[game]
        if not rows:
            continue
        for versionId in GAME_VERSION_IDS[game]:
            L.append('                case Enums::GameVersion::%s:' % versionId)
        L.append('                    out = TradeEvolutionTable{ TRADE_%s, %d }; return true;' % (game, len(rows)))
    L.append('                default:')
    L.append('                    return false;')
    L.append('            }')
    L.append('        }')
    L.append('    }')
    L.append('')
    L.append('    int getTradeEvolutions(Enums::GameVersion group, uint16_t speciesId, uint8_t formId, TradeEvolution* outEvolutions, int maxEvolutions) {')
    L.append('        TradeEvolutionTable table{};')
    L.append('        if (outEvolutions == nullptr || maxEvolutions <= 0 || !lookupTradeTable(group, table)) {')
    L.append('            return 0;')
    L.append('        }')
    L.append('        uint32_t low = 0, high = table.count;')
    L.append('        while (low < high) {')
    L.append('            const uint32_t middle = low + ((high - low) >> 1);')
    L.append('            const TradeEvolution& row = table.rows[middle];')
    L.append('            if (row.speciesId < speciesId || (row.speciesId == speciesId && row.formId < formId)) {')
    L.append('                low = middle + 1;')
    L.append('            } else {')
    L.append('                high = middle;')
    L.append('            }')
    L.append('        }')
    L.append('        int writtenCount = 0;')
    L.append('        for (uint32_t rowIndex = low; rowIndex < table.count && writtenCount < maxEvolutions; ++rowIndex) {')
    L.append('            const TradeEvolution& row = table.rows[rowIndex];')
    L.append('            if (row.speciesId != speciesId || row.formId != formId) {')
    L.append('                break;')
    L.append('            }')
    L.append('            outEvolutions[writtenCount] = row;')
    L.append('            ++writtenCount;')
    L.append('        }')
    L.append('        return writtenCount;')
    L.append('    }')
    L.append('}')
    L.append('')
    return "\n".join(L)


def main():
    # The four pre-Gen-4 readers are shaped differently from Personal and each other, exactly
    # as in gen_learnsets.main() -- this mirrors it deliberately, so the two generators walk the
    # same lineage through the same code. Constructing Personal("GSC") instead throws a KeyError,
    # which makes the whole generator unrunnable rather than dropping one table.
    special = {"FRLG": lambda: Personal3(),
               "RSE": lambda: Personal3("personal_rs"),
               "RBY": Personal1,
               "GSC": Personal2}
    personals = {g: (special[g]() if g in special else Personal(g)) for g in GAMES_TO_EMIT}

    tables = {}
    for game in GAMES_TO_EMIT:
        tables[game] = build(game, personals[game], personals[EVO_INDEX_GAME.get(game, game)])

    # Forward trade evolutions. The INDEX personal is the one the binary is addressed by, which is
    # not always the game's own (EVO_INDEX_GAME); the PRESENCE personal always is.
    tradeTables = {}
    for game in GAMES_TO_EMIT:
        indexGame = EVO_INDEX_GAME.get(game, game)
        tradeTables[game] = read_trade_evolutions(game, personals[game], personals[indexGame])
    tradeTables["GSC"] = derive_gen2_items(tradeTables["GSC"], tradeTables["FRLG"])
    for game in GAMES_TO_EMIT:
        tradeTables[game] = resolve_partner_species(game, tradeTables[game])

    for game in GAMES_TO_EMIT:
        print("  %-5s reverse edges: %4d   trade evolutions: %3d"
              % (game, len(tables[game]), len(tradeTables[game])))

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(emit_header())
    print("Wrote", OUT_H)
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(emit_cpp(tables, tradeTables))
    print("Wrote", OUT_CPP)

    total = sum(len(t) for t in tables.values())
    tradeTotal = sum(len(t) for t in tradeTables.values())
    print("  TABLE TOTAL: %d reverse edges, %d trade evolutions" % (total, tradeTotal))


if __name__ == "__main__":
    main()
