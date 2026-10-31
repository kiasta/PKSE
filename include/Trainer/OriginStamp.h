/**
 * OriginStamp.h - which game a Pokemon came from, as a label.
 *
 * Lives beside the Trainer rather than in the UI layer for two reasons: the fallback needs a
 * SAVE to name a versionless record, and the interesting half of it has to be answerable
 * without a screen -- the save layer is pure byte work and must not reach into the UI for it.
 */

#ifndef TRAINER_ORIGIN_STAMP_H
#define TRAINER_ORIGIN_STAMP_H

#include <cstdint>
#include <string>

namespace Pokemon
{
    class Pokemon;
}

namespace Trainer
{
    class Trainer;

    /**
     * THE ORIGIN STAMP. Three views show it, and the interesting half is the FALLBACK.
     *
     * Gen 1 and Gen 2 write no version byte at all -- a PK1 is identical whether it came from Red,
     * Green, Blue or Yellow, and a PK2 the same across Gold, Silver and Crystal -- so
     * hasOriginGame() is false and originGame() has nothing to report. The only thing nameable then
     * is the SAVE the Pokemon is sitting in, and only while it is sitting in one.
     *
     * @param entity    The Pokemon to stamp.
     * @param openSave  The save it is loaded from, or nullptr for a bank record or one in hand --
     *                  the bank's tag records a locale, not a title, so a banked PK1 may well have
     *                  come from a different cartridge than the one open right now.
     * @return A game or group name; never empty.
     */
    std::string originStampLabel(const Pokemon::Pokemon &entity, const Trainer *openSave);

    /**
     * The games' own ORIGIN MARKING for a Pokemon -- the Kalos pentagon, the Alola clover, the
     * Galar mark, a Game Boy for a Virtual Console transfer, and so on. Bank, HOME and PKSM all
     * mark origin this way, and the artwork is PKHeX's Markings resources (fetched by `make
     * marks`), so these are the marks the games themselves stamp rather than PKSE's invention.
     *
     * GEN 3, 4 AND 5 RETURN None, AND THAT IS THE CORRECT ANSWER. No mark existed before the
     * Kalos pentagon, so there is nothing to draw and nothing to invent; those origins are named
     * in text instead, which they can be because they carry a version byte. Gen 1 and Gen 2 are
     * the opposite case -- no version byte, so the console is the only thing actually known.
     */
    enum class OriginMark : uint8_t
    {
        None = 0,
        GameBoy,      // Virtual Console Red .. Crystal, and any PK1/PK2 in its own save
        Gen6Pentagon, // X / Y / Omega Ruby / Alpha Sapphire
        Gen7Clover,   // Sun / Moon / Ultra Sun / Ultra Moon
        Gen8Galar,    // Sword / Shield
        LetsGo,       // Let's Go Pikachu / Eevee
        Gen8Trio,     // Brilliant Diamond / Shining Pearl
        Gen8Arc,      // Legends: Arceus
        Gen9Paldea,   // Scarlet / Violet
        Gen9ZA,       // Legends: Z-A
        GO,           // Pokemon GO
        GameBoyAdvance, // FireRed / LeafGreen on Nintendo Switch, sent through HOME
        Count
    };

    /// Which marking a Pokemon carries. PKHeX's OriginMarkUtil.GetOriginMark.
    OriginMark originMarkFor(const Pokemon::Pokemon &entity);

    /**
     * The romfs file stem for a marking (`romfs:/sprites/marks/<stem>.png`), or nullptr for None.
     * Lives beside the enum so the two cannot drift; the names are PKHeX's own.
     */
    const char *originMarkFileStem(OriginMark mark);
}

#endif
