/**
 * When a banked Pokemon is withdrawn into a save of a DIFFERENT game, its native bytes must be
 * re-formatted into the destination game's entity format. Following PKHeX/HOME: the origin identity
 * (Version, OT, ID, met/egg, HOME tracker, EC/PID/IVs/nature/gender/language/ribbons) is preserved
 * verbatim; only game-specific container fields are rewritten, and the checksum is refreshed.
 *
 * PKSE preserves origin identity + ability but SANITIZES the moveset for the destination: any move (or
 * relearn move) the destination species can't legally learn -- illegal for the species, OR not present in
 * that game at all -- is cleared to empty (see convert()). This is REQUIRED: Let's Go turns a pokemon carrying
 * an impossible move into a Bad Egg. The held item is sanitized the same way, against a per-game holdable-
 * item table: it is cleared if the destination cannot hold that id, which includes EVERY id for Let's Go and
 * Legends: Arceus (neither game has a held-item mechanic at all).
 * (PKHeX instead regenerates the whole moveset from the destination learnset via HOME; we keep the pokemon's
 * own legal moves and only drop the illegal ones.) Same-generation sibling pairs (SwSh<->BDSP, S/V<->Z-A) share
 * a near-identical layout (copy + null a couple of divergent fields). Cross-generation pairs (PK8<->PK9) are a
 * field remap: gender bit-position, Version/Language/FormArg/AffixedRibbon offsets, height/weight/scale,
 * HOME-tracker offset, and the record-flag / Tera / ObedienceLevel regions are re-laid-out; the Tera type
 * is imported from the species' primary type (PK8 has none). Legends: Arceus (PA8) has its whole Block
 * B/C/D at different offsets, so it is a full field relocation routed through the PK8 layout (PA8<->PK8,
 * then the PK8<->PK9 transforms for a Gen 9 endpoint); PA8-only data (GVs, Alpha, move-mastery) is dropped
 * and LA-only balls collapse to a Poke Ball. Let's Go (PB7, 260-byte Gen-7 record) likewise relocates
 * through the PK8 layout, but stat-training is NOT cross-converted: AVs/EVs are dropped so a pokemon entering
 * LGPE gets AVs=0 and one leaving gets EVs=0 (the UI asks the user to acknowledge this); its Level/stats/CP
 * tail is recomputed on convert. FireRed/LeafGreen (PK3, 80/100-byte GBA record) is the most divergent:
 * no EC (EC:=PID), nature/gender/shiny are all PID-derived, ability is a slot BIT resolved via the personal
 * table, species/items/text use Gen-3 id spaces, and IVs pack the ability bit at 31. Its remap converts
 * item ids (Item3to4), transcodes Gen-3<->Unicode names, and sanitizes the moveset against the real Gen-3
 * learnset like every other game; Level/stats are recomputed on convert. Going TO Gen 3, nature/gender become
 * PID-derived (Gen 3 stores neither) and the LANGUAGE is clamped to one Gen 3 shipped a game in -- which
 * decides the character table both names are written through, so a Korean or Chinese Pokemon becomes an
 * English one and its nickname falls back to the English species name. A nickname is otherwise carried,
 * and an un-nicknamed Pokemon arrives holding its own language's species name (Gen 3 has no is-nicknamed
 * flag, so that string is the only way to say "not nicknamed"). The OT name is the one field with no
 * fallback: it is carried when the destination's alphabet can spell it and left BLANK when it cannot --
 * see canStoreOriginalTrainerName, which is what the confirm dialog warns from. Sun/Moon and Ultra
 * Sun/Ultra Moon (PK7, the 232/260-byte 3DS record) are siblings of each other and reach the hub by a
 * remap that is the PB7 one plus everything Let's Go dropped -- held item, EVs, contest stats, both
 * trainers' memories -- while the Gen-7-only regions (Super Training, Poke Pelago, the 3DS geolocation
 * block, affection) have nowhere to go in Gen 8 and are dropped. Every conversion is thus a PK8-layout
 * hub: any game <-> any other (full two-way, beyond HOME's one-way Let's Go limit), bounded only by
 * destination dex-presence.
 *
 * GEN 1 AND GEN 2 JOIN THE HUB ONE WAY, THROUGH POKE TRANSPORTER. A PK1/PK2 is not remapped onto a
 * modern layout; it is run through the transfer the Virtual Console games actually had -- which builds
 * a PK7 -- and the result then travels the ordinary hub. That step INVENTS most of a modern record,
 * because a PK1 has no nature, ability, PID, ball, met data or origin game to carry: see the block
 * comment above transportVirtualConsoleToPK7() in Convert.cpp for the full recipe and for the three
 * things PKSE cannot reproduce (the origin title is a guess; names are written in English; the 3DS
 * locale block stays blank). Nothing converts BACK: a PK1 has no room for what it would be handed.
 */
#ifndef CONVERSION_CONVERT_H
#define CONVERSION_CONVERT_H

#include <memory>
#include <string>

#include "Pokemon/Pokemon.h"
#include "Enums/GameVersion.h"

namespace Conversion {
    /// Who the Pokemon is being withdrawn INTO. Every real route into another game -- Poke
    /// Transporter, Bank, HOME -- makes the receiving player its active handler, so a conversion
    /// into a format with a handler stamps this trainer there (Pokemon::applyReceivingTrainer).
    ///
    /// Pass nullptr (or an empty name) when the caller has no save behind it. The Pokemon then keeps
    /// the handler it arrived with -- none at all for a Gen 1-3 source, which is a readable state --
    /// because an invented name is not.
    struct ReceivingTrainer
    {
        std::u16string name;
        uint8_t gender = 0;   // 0 = male, 1 = female, as every format here stores it
        uint8_t language = 0; // Enums::LanguageID of the receiving save; Gen 8+ records the handler's
    };

    enum class Result {
        Ok,           // converted successfully
        SameGroup,    // no conversion needed -- the caller should move the original as-is
        Unsupported,  // not one of the seven supported mainline games (all of which now interconvert)
        NotInDex,     // species/form does not exist in the destination game's dex
        Blocked,      // destination refuses this species (e.g. BDSP Spinda / Nincada)
    };

    /// Converts `source` into `destGroup`'s entity format, preserving origin identity and refreshing the
    /// checksum. Returns a NEW entity, or nullptr with `result` set to the reason. A same-format pair
    /// yields SameGroup + nullptr (the caller should just move the original -- no conversion needed).
    ///
    /// `destOriginVersion` is the exact destination GAME's origin byte, used only where an origin cannot
    /// be carried across and has to be restamped -- today that is the Gen 3 down-convert, whose origin
    /// field is 4 bits wide and cannot hold a modern game's id. It must be passed because a game *group*
    /// deliberately collapses a version pair into one value: `FRLG` cannot say FireRed from LeafGreen,
    /// and stamping a fixed member of the pair is wrong half the time. 0 = unknown, which falls back to
    /// the group's representative version.
    ///
    /// `receivingTrainer` names the trainer whose save the Pokemon is landing in, who becomes its
    /// active handler. The one pairing that leaves the handler alone is Sun/Moon <-> Ultra Sun/Ultra
    /// Moon: one record format, so the move is not a transfer into another game. nullptr means "not
    /// known" and keeps the handler the source had. See ReceivingTrainer above.
    std::unique_ptr<Pokemon::Pokemon> convert(const Pokemon::Pokemon& source, Enums::GameVersion destGroup, Result& result, uint8_t destOriginVersion = 0, const ReceivingTrainer* receivingTrainer = nullptr);

    /// True if `destGroup` can accept `source` (same group, or a supported+allowed conversion). Pure check
    /// (no allocation) for gating UI without performing the conversion.
    bool canConvert(const Pokemon::Pokemon& source, Enums::GameVersion destGroup, Result& result);

    /// Short human-facing reason for a non-Ok/SameGroup result, for on-screen feedback.
    const char* resultMessage(Result result);

    /// True when the original trainer's name survives a move of `source` into `destGroup`.
    ///
    /// False means the destination's alphabet cannot spell it and the OT field will arrive BLANK.
    /// Only a Gen 3 destination can answer false: every later format stores UTF-16 and holds any
    /// name at all, while Gen 3 is a per-language font map with no Hangul, no CJK and -- once the
    /// language has been clamped to one Gen 3 shipped in -- no way to write a Korean or Chinese
    /// trainer's name. A name merely too LONG for Gen 3's seven characters is not a loss and does
    /// not answer false; it is truncated, as every transfer into an older generation truncates.
    ///
    /// Pure check, for the confirm dialog to warn with before the move runs. Nothing is substituted
    /// in the blank field's place -- see the OT block in remapPK8toPK3 for why a placeholder would
    /// be worse than an empty field.
    bool canStoreOriginalTrainerName(const Pokemon::Pokemon& source, Enums::GameVersion destGroup);

    /// True when moving `source` into `destGroup` would run a Poke Transporter transfer -- i.e.
    /// `source` is a Gen 1 or Gen 2 record. That transfer is not reversible and rewrites more of
    /// the Pokemon than any other conversion does (IVs, PID, EXP, ability, nickname), so the UI
    /// asks before it runs.
    bool virtualConsoleTransferInvolved(const Pokemon::Pokemon& source, Enums::GameVersion destGroup);

    /// The origin GAME a Poke Transporter run will stamp on a Gen 1/2 record.
    ///
    /// Gen 2 names Crystal outright when the record carries Crystal's caught data -- that is the
    /// one thing about its origin a PK2 records. Everything else is the group's representative
    /// title, because neither the record nor the save it came from holds a version byte: Red,
    /// Green and Blue share an engine, Gold and Silver share a layout, and a banked record has no
    /// save behind it to ask. See virtualConsoleOriginIsGuess.
    Enums::GameVersion virtualConsoleOriginVersion(const Pokemon::Pokemon& source);

    /// True when virtualConsoleOriginVersion() returned a REPRESENTATIVE rather than a fact, so
    /// the caller can say so before the transfer runs.
    bool virtualConsoleOriginIsGuess(const Pokemon::Pokemon& source);

    /// Byte offset of **AffixedRibbon** in a game's entity format, or 0 for the formats that have no
    /// such field (Gen 3's PK3, Let's Go's PB7, and the Gen 7 3DS games' PK7 -- Gen 8 introduced the
    /// field along with the reordered ribbon block).
    ///
    /// AffixedRibbon names the single ribbon the game DISPLAYS on the summary screen. Its "none" value
    /// is 0xFF, **not** 0 -- 0 is a real index, and index 0 is the Kalos Champion ribbon. So any code
    /// path that leaves this byte at its zero-initialised default hands the pokemon a Kalos Champion ribbon
    /// it does not own. That has now bitten twice, from two different directions (the creator, then
    /// cross-generation conversion), so the offset lives here once and both call sites read it.
    size_t affixedRibbonOffset(Enums::GameVersion group) noexcept;

    /// The value meaning "display no ribbon". Not zero -- see affixedRibbonOffset().
    inline constexpr uint8_t AFFIXED_RIBBON_NONE = 0xFF;

    /// Repairs an invalid AffixedRibbon on `pk` **in place**, re-checksumming if it changed. Returns
    /// true when it changed something.
    ///
    /// Invalid means the byte reads 0 -- "display ribbon index 0", the Kalos Champion ribbon -- while
    /// the pokemon does not own that ribbon. Verified against real saves: every genuinely game-caught pokemon
    /// carries 0xFF, including ones that DO own ribbons, so a 0 here is never something the game wrote.
    /// The owned-ribbon guard means a deliberately affixed ribbon is never disturbed.
    ///
    /// Call on the way INTO a save (conversion, and the same-group path that skips conversion). Not on
    /// deposit: the bank stores native bytes untouched by design.
    bool normalizeAffixedRibbon(Pokemon::Pokemon& pokemon);
}

#endif
