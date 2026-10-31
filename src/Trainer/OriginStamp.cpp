#include "Trainer/OriginStamp.h"

#include "Enums/GameVersion.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer1RBY.h" // gameTitle() -- a PK1 has no version byte of its own
#include "Trainer/Trainer2GSC.h" // gameTitle() -- and neither has a PK2

namespace Trainer
{
    std::string originStampLabel(const Pokemon::Pokemon &entity, const Trainer *openSave)
    {
        if (entity.hasOriginGame())
            return Enums::getOriginGameName(entity.originGame());

        // ASK THE ENTITY FOR ITS GROUP -- not the open save, and not a constant. Gen 1 and Gen 2
        // BOTH lack the version byte, so the hardcoded "Gen 1" this replaced stamped every Gold,
        // Silver and Crystal Pokemon as Gen 1: a wrong answer that reads exactly like a right one.
        const Enums::GameVersion group = entity.getGameGroup();

        // Borrowing the open save's title is only honest when the Pokemon actually belongs to it.
        // A Gen 1 record viewed from a Gen 2 save, or from the bank, has no title context to take.
        if (openSave != nullptr && openSave->getGameGroup() == group)
        {
            if (group == Enums::GameVersion::RBY)
                return static_cast<const Trainer1RBY *>(openSave)->gameTitle();
            if (group == Enums::GameVersion::GSC)
                return static_cast<const Trainer2GSC *>(openSave)->gameTitle();
        }
        // No save context: stop at the generation rather than naming a title we cannot know.
        return (group == Enums::GameVersion::GSC) ? "Gen 2" : "Gen 1";
    }

    OriginMark originMarkFor(const Pokemon::Pokemon &entity)
    {
        // A format with no version byte: Gen 1 and Gen 2. The TITLE is unknowable, but the console
        // is not, which is exactly why Bank and HOME mark these with a Game Boy instead of a name.
        if (!entity.hasOriginGame())
        {
            const Enums::GameVersion group = entity.getGameGroup();
            return (group == Enums::GameVersion::RBY || group == Enums::GameVersion::GSC) ? OriginMark::GameBoy : OriginMark::None;
        }

        // PKHeX OriginMarkUtil.GetOriginMark, and THE ORDER IS LOAD-BEARING. A Virtual Console, GO
        // or Let's Go Pokemon sits in a Gen 7 FORMAT while carrying its own origin version, so the
        // three specific marks must be tested before the generation lumps -- checking Gen 7 first
        // would stamp an Alola clover on every transferred Red/Blue Pokemon.
        const uint8_t version = entity.originGame();
        if (version >= 35 && version <= 41)
            return OriginMark::GameBoy; // Red, Blue, Blue (JP), Yellow, Gold, Silver, Crystal
        if (version == 56 || version == 57)
            return OriginMark::GameBoyAdvance; // FireRed / LeafGreen (Switch)
        if (version == 34)
            return OriginMark::GO;
        if (version == 42 || version == 43)
            return OriginMark::LetsGo; // Let's Go Pikachu / Eevee
        if (version >= 24 && version <= 27)
            return OriginMark::Gen6Pentagon; // X, Y, Alpha Sapphire, Omega Ruby
        if (version >= 30 && version <= 33)
            return OriginMark::Gen7Clover; // Sun, Moon, Ultra Sun, Ultra Moon
        if (version == 44 || version == 45)
            return OriginMark::Gen8Galar; // Sword / Shield
        if (version == 48 || version == 49)
            return OriginMark::Gen8Trio; // Brilliant Diamond / Shining Pearl
        if (version == 47)
            return OriginMark::Gen8Arc; // Legends: Arceus
        if (version == 50 || version == 51)
            return OriginMark::Gen9Paldea; // Scarlet / Violet
        if (version == 52)
            return OriginMark::Gen9ZA; // Legends: Z-A

        // Gen 3 cartridges, Gen 4, Gen 5, the GameCube titles and anything newer than PKHeX's mark
        // set. The games stamp nothing, so neither does PKSE -- the text label carries those.
        return OriginMark::None;
    }

    const char *originMarkFileStem(OriginMark mark)
    {
        switch (mark)
        {
        case OriginMark::GameBoy:      return "vc";
        case OriginMark::Gen6Pentagon: return "6";
        case OriginMark::Gen7Clover:   return "7";
        case OriginMark::Gen8Galar:    return "8";
        case OriginMark::LetsGo:       return "gg";
        case OriginMark::Gen8Trio:     return "bs";
        case OriginMark::Gen8Arc:      return "la";
        case OriginMark::Gen9Paldea:   return "sv";
        case OriginMark::Gen9ZA:       return "za";
        case OriginMark::GO:           return "go";
        case OriginMark::GameBoyAdvance: return "gba";
        default:                       return nullptr;
        }
    }
}
