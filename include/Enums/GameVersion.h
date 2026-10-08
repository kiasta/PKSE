#ifndef ENUMS_GAME_VERSION_H
#define ENUMS_GAME_VERSION_H

#include <cstdint>
#include <string>

namespace Enums
{
    // Game Version ID enum shared between actual Version IDs and lumped version groupings
    enum class GameVersion
    {
        // Indicators for method empty arguments & result indication. Not stored values.
        Any = 0,
        Invalid = 255,

        // The following values are IDs stored within PKM data, and can also identify individual games.

        // Gen 3 (GBA) -- values match the Origins version field in PK3
        SA = 1, // Pokemon Sapphire
        RU = 2, // Pokemon Ruby
        EM = 3, // Pokemon Emerald
        FR = 4, // Pokemon FireRed
        LG = 5, // Pokemon LeafGreen

        // Gen 4 (Nintendo DS). Values match PKHeX. Unlike Gen 1, a PK4 DOES store its origin
        // in the Version field at 0x5F, so these are both read and written.
        HG = 7,  // Pokemon HeartGold
        SS = 8,  // Pokemon SoulSilver
        D = 10,  // Pokemon Diamond
        P = 11,  // Pokemon Pearl
        Pt = 12, // Pokemon Platinum

        // Gen 5 (Nintendo DS). NOTE the pairing order is PKHeX's and is not the way the games
        // are usually written: White is 20 and Black 21, White 2 is 22 and Black 2 23.
        W = 20,  // Pokemon White
        B = 21,  // Pokemon Black
        W2 = 22, // Pokemon White 2
        B2 = 23, // Pokemon Black 2

        // Gen 6 (Nintendo 3DS)
        X = 24,  // Pokemon X
        Y = 25,  // Pokemon Y
        AS = 26, // Pokemon Alpha Sapphire
        OR = 27, // Pokemon Omega Ruby

        // Gen 7 (Nintendo 3DS). Let's Go is also generation 7 but a DIFFERENT storage format
        // (PB7 vs PK7) and has its own group, GG -- the two must never be conflated.
        SN = 30, // Pokemon Sun
        MN = 31, // Pokemon Moon
        US = 32, // Pokemon Ultra Sun
        UM = 33, // Pokemon Ultra Moon

        // Gen 1 (Game Boy, distributed on the 3DS Virtual Console). Values match PKHeX.
        // A PK1 stores NO version field -- the format has nowhere to put one -- so these are
        // only ever WRITTEN, by the transfer path that sends a Gen 1 Pokemon forward. Nothing
        // reads one back out of a PK1; see Pokemon1RBY::originGame(), which reports absence.
        RD = 35, // Pokemon Red
        GN = 36, // Pokemon Green (JP)
        BU = 37, // Pokemon Blue  (JP; the international Blue is RD's pair and shares its id)
        YW = 38, // Pokemon Yellow

        // Gen 2 (Game Boy Color, and the 3DS Virtual Console re-releases). Like a PK1, a PK2
        // stores NO version field, so these are only ever WRITTEN, by a transfer path.
        GD = 39, // Pokemon Gold
        SI = 40, // Pokemon Silver
        C = 41,  // Pokemon Crystal

        // Nintendo Switch
        GP = 42, // Pokemon: Let's Go, Pikachu!
        GE = 43, // Pokemon: Let's Go, Eevee!
        SW = 44, // Pokemon Sword
        SH = 45, // Pokemon Shield
                 // HOME = 46, // Not used (?)
        PLA = 47, // Pokemon Legends: Arceus
        BD = 48,  // Pokemon Brilliant Diamond
        SP = 49,  // Pokemon Shining Pearl
        SL = 50,  // Pokemon Scarlet
        VL = 51,  // Pokemon Violet
        ZA = 52,  // Pokemon Legends: Z-A

        // The following values are not actually stored values in pk data,
        // These values are assigned as properties for various logic branching.

        FRLG = 72, // Pokemon FireRed & LeafGreen group
        GG = 73,   // Pokemon Let's Go Pikachu & Eevee group
        SWSH = 74, // Pokemon Sword & Shield group
        BDSP = 75, // Pokemon Brilliant Diamond & Shining Pearl group
        SV = 76,   // Pokemon Scarlet & Violet group
        RBY = 77,  // Pokemon Red/Blue/Yellow group -- the only Gen 1 STORAGE format (PK1)
        GSC = 78,  // Pokemon Gold/Silver/Crystal group -- the only Gen 2 STORAGE format (PK2)

        // Gen 4 gets THREE groups because it has three save LAYOUTS: the block sizes, the box
        // storage layout and the bag offsets all differ. The PK4 entity is common to all three.
        DP = 79,   // Diamond/Pearl
        PT = 80,   // Platinum
        HGSS = 81, // HeartGold/SoulSilver

        // Gen 5-7: one group per save layout, same reasoning.
        BW = 82,   // Black/White
        B2W2 = 83, // Black 2/White 2

        Gen7B = 84,
        Gen8 = 85,
        Gen9 = 86,

        // Continued from the block above -- 84-86 were already taken by the generational
        // groupings, so the Gen 6/7 save-format groups pick up after them.
        XY = 87,   // X/Y
        ORAS = 88, // Omega Ruby/Alpha Sapphire
        SM = 89,   // Sun/Moon
        USUM = 90, // Ultra Sun/Ultra Moon

        // Gen 3's Hoenn trio. ONE group, not two: Ruby, Sapphire and Emerald share the PK3 entity
        // and the same 128 KiB container, and differ in only two things PKSE reads -- the bag is
        // 0x360 bytes in RS and 0x3B0 in Emerald, and Emerald has a security key where RS has none.
        // That is the GSC shape (three games, one storage format, a layout table for the offsets),
        // not the Gen 4 shape (three genuinely different containers). FireRed/LeafGreen stay their
        // own group: their bag and party live at completely different offsets.
        RSE = 91
    };

    /**
     * A title that ships ONE APPLICATION PER LANGUAGE: its id, its game, and which language it is.
     *
     * Gen 3's Switch release (2026-02-27) is the only one. The GBA originals had no in-game
     * language option, so the eShop ships a separate SKU per localisation rather than one
     * multi-language build -- unlike Sword/Shield, BDSP, Legends: Arceus, Scarlet/Violet and
     * Legends: Z-A, which are a single worldwide title each and need exactly one id.
     *
     * ALL TWELVE RESOLVE TO THE SAME GameVersion, because the save CONTAINER is identical: same
     * 128 KiB image, same fourteen rotated sectors, same PK3. The five Western SKUs share a charset
     * too (Gen3Text's table covers EN/FR/DE/IT/ES alike); the JAPANESE one does not, and does not
     * need to -- nothing about a Gen 3 save records its language, so Trainer3FRLG::detectLanguage()
     * measures it from the trainer-name field width exactly as PKHeX does, whatever title id it
     * arrived under. The SKU is not the authority on that and must not become one. That is correct for everything that
     * reads a save and WRONG for everything that has to tell two installed titles apart -- and both of those existed.
     * Omitting the ids entirely made a French LeafGreen owner see "0 saves found", because
     * SaveSelectScreen drops an id getGameVersion() does not know. Adding them without a language
     * replaced that with something quieter and worse: the picker drew two tiles both labelled
     * "LeafGreen", and the backup directory is built from that label, so all three SKUs shared one
     * folder -- and a French save loaded whatever English bytes were already sitting in it.
     *
     * ONE TABLE, BOTH FACTS. The id -> version mapping and the id -> language mapping are the same
     * fact about the same SKU, so a seventh localisation is one row here and cannot land with a
     * version but no language.
     */
    struct PerLanguageTitle
    {
        uint64_t titleId;
        GameVersion version;
        uint8_t language; // an Enums::LanguageID value; see getTitleLanguage
    };

    inline constexpr PerLanguageTitle PER_LANGUAGE_TITLES[] = {
        // LanguageID: Japanese = 1, English = 2, French = 3, Italian = 4, German = 5, Spanish = 7.
        // Spelled as numbers because LanguageID.h includes THIS header, so the enum cannot be named
        // from here. Ordered by game, then in LanguageID order.
        //
        // TWELVE, WHICH IS ALL OF THEM: the six GBA languages across the two games, confirmed
        // against the US, GB, DE, IT and JP eShop indexes -- twelve distinct ids and no more. Korea
        // has none, which is right: the GBA originals never had a Korean release. The US and EU
        // share one English SKU rather than having an id each.
        {0x01006FA0233F8000, GameVersion::FR, 1}, // FireRed   (Japanese) ポケットモンスター ファイアレッド
        {0x0100554023408000, GameVersion::FR, 2}, // FireRed   (English)  Pokémon FireRed Version
        {0x01004B3023412000, GameVersion::FR, 3}, // FireRed   (French)   Pokémon Version Rouge Feu
        {0x010092302342A000, GameVersion::FR, 4}, // FireRed   (Italian)  Pokémon Versione Rosso Fuoco
        {0x01007F8023416000, GameVersion::FR, 5}, // FireRed   (German)   Pokémon Feuerrote Edition
        {0x0100EB702342C000, GameVersion::FR, 7}, // FireRed   (Spanish)  Pokémon Edición Rojo Fuego
        {0x0100F1E0233FA000, GameVersion::LG, 1}, // LeafGreen (Japanese) ポケットモンスター リーフグリーン
        {0x010034D02340E000, GameVersion::LG, 2}, // LeafGreen (English)  Pokémon LeafGreen Version
        {0x010087C02342E000, GameVersion::LG, 3}, // LeafGreen (French)   Pokémon Version Vert Feuille
        {0x01005C7023432000, GameVersion::LG, 4}, // LeafGreen (Italian)  Pokémon Versione Verde Foglia
        {0x0100FD6023430000, GameVersion::LG, 5}, // LeafGreen (German)   Pokémon Blattgrüne Edition
        {0x01002B5023434000, GameVersion::LG, 7}, // LeafGreen (Spanish)  Pokémon Edición Verde Hoja
    };

    /// An Enums::LanguageID, or 0 for every other title -- one game, one id, nothing to
    /// disambiguate. 0 here is not LanguageID::Hacked; it means "not a per-language title", so
    /// callers test it rather than naming it.
    inline uint8_t getTitleLanguage(uint64_t titleId)
    {
        for (const PerLanguageTitle &title : PER_LANGUAGE_TITLES)
        {
            if (title.titleId == titleId)
                return title.language;
        }
        return 0;
    }

    inline GameVersion getGameVersion(uint64_t titleId)
    {
        // Gen 3's per-language SKUs answer from their own table -- see PER_LANGUAGE_TITLES.
        for (const PerLanguageTitle &title : PER_LANGUAGE_TITLES)
        {
            if (title.titleId == titleId)
                return title.version;
        }
        switch (titleId)
        {
        // // Gen 7 - Let's Go
        case 0x010003F003A34000:
            return GameVersion::GP; // Let's Go Pikachu
        case 0x0100187003A36000:
            return GameVersion::GE; // Let's Go Eevee

        // Gen 8 - Sword/Shield
        case 0x0100ABF008968000:
            return GameVersion::SW;
        case 0x01008DB008C2C000:
            return GameVersion::SH; // Shield

        // Gen 8 - BDSP
        case 0x0100000011D90000:
            return GameVersion::BD; // Brilliant Diamond
        case 0x010018E011D92000:
            return GameVersion::SP; // Shining Pearl

        // Gen 8 - Legends Arceus
        case 0x01001F5010DFA000:
            return GameVersion::PLA;

        // Gen 9 - Scarlet/Violet (reuse the Gen 9 save path with packed slots)
        case 0x0100A3D008C5C000:
            return GameVersion::SL;
        case 0x01008F6008C5E000:
            return GameVersion::VL; // Violet

        // Gen 9 - Legends: Z-A
        case 0x0100F43008C44000:
            return GameVersion::ZA;

        default:
            return GameVersion::Invalid;
        }
    }

    inline GameVersion getGameGroup(GameVersion version)
    {
        switch (version)
        {
        case GameVersion::FR:
        case GameVersion::LG:
            return GameVersion::FRLG; // FireRed/LeafGreen group

        case GameVersion::RU:
        case GameVersion::SA:
        case GameVersion::EM:
            return GameVersion::RSE; // Ruby/Sapphire/Emerald group

        case GameVersion::GP:
        case GameVersion::GE:
            return GameVersion::GG; // Let's Go group

        case GameVersion::SW:
        case GameVersion::SH:
            return GameVersion::SWSH; // Sword/Shield group

        case GameVersion::BD:
        case GameVersion::SP:
            return GameVersion::BDSP; // BDSP group

        case GameVersion::SL:
        case GameVersion::VL:
            return GameVersion::SV; // Scarlet/Violet group

        case GameVersion::PLA:
            return GameVersion::PLA; // Legends Arceus (its own group)

        case GameVersion::ZA:
            return GameVersion::ZA; // Legends ZA (its own group)

        case GameVersion::RD:
        case GameVersion::GN:
        case GameVersion::BU:
        case GameVersion::YW:
            return GameVersion::RBY; // Red/Green/Blue/Yellow share one save layout

        case GameVersion::GD:
        case GameVersion::SI:
        case GameVersion::C:
            return GameVersion::GSC; // Gold/Silver/Crystal share one save layout

        case GameVersion::D:
        case GameVersion::P:
            return GameVersion::DP;
        case GameVersion::Pt:
            return GameVersion::PT; // Platinum is its own layout
        case GameVersion::HG:
        case GameVersion::SS:
            return GameVersion::HGSS;

        case GameVersion::B:
        case GameVersion::W:
            return GameVersion::BW;
        case GameVersion::B2:
        case GameVersion::W2:
            return GameVersion::B2W2;

        case GameVersion::X:
        case GameVersion::Y:
            return GameVersion::XY;
        case GameVersion::OR:
        case GameVersion::AS:
            return GameVersion::ORAS;

        case GameVersion::SN:
        case GameVersion::MN:
            return GameVersion::SM;
        case GameVersion::US:
        case GameVersion::UM:
            return GameVersion::USUM;

        default:
            return GameVersion::Invalid;
        }
    }

    inline std::string getGameVersionName(GameVersion version)
    {
        switch (version)
        {
        // Gen 1. RBY names the PAIR because a Gen 1 save cannot say which of Red/Blue it is --
        // there is no version byte anywhere in the 32 KiB -- and Trainer1RBY::gameTitle() is the
        // place that makes that call for a loaded save. Without these the group answered
        // "Unknown", which reached the [EVENT] log's `fmt=` field and any UI that names a group.
        case GameVersion::RD:
            return "Red";
        case GameVersion::GN:
            return "Green";
        case GameVersion::BU:
            return "Blue";
        case GameVersion::YW:
            return "Yellow";
        case GameVersion::RBY:
            return "Red/Blue/Yellow";
        // Gen 2. Gold and Silver are indistinguishable from the save alone (no version byte
        // anywhere), exactly like Red/Blue -- so the GROUP names the pair and gameTitle()
        // makes the finer call where it can (Crystal is detectable).
        case GameVersion::GD:
            return "Gold";
        case GameVersion::SI:
            return "Silver";
        case GameVersion::C:
            return "Crystal";
        case GameVersion::GSC:
            return "Gold/Silver/Crystal";
        // Gen 3 (Hoenn). Ruby and Sapphire cannot be told apart -- section 0's 0xAC word is 0 for
        // both, exactly as Red/Blue carry no version byte -- so RSE names the trio and the pair is
        // separated only by where the file came from, never by its bytes. Emerald names itself.
        case GameVersion::RU:
            return "Ruby";
        case GameVersion::SA:
            return "Sapphire";
        case GameVersion::EM:
            return "Emerald";
        case GameVersion::RSE:
            return "Ruby/Sapphire/Emerald";
        // Gen 4
        case GameVersion::D:
            return "Diamond";
        case GameVersion::P:
            return "Pearl";
        case GameVersion::Pt:
            return "Platinum";
        case GameVersion::HG:
            return "HeartGold";
        case GameVersion::SS:
            return "SoulSilver";
        case GameVersion::DP:
            return "Diamond/Pearl";
        case GameVersion::PT:
            return "Platinum";
        case GameVersion::HGSS:
            return "HeartGold/SoulSilver";
        // Gen 5
        case GameVersion::B:
            return "Black";
        case GameVersion::W:
            return "White";
        case GameVersion::B2:
            return "Black 2";
        case GameVersion::W2:
            return "White 2";
        case GameVersion::BW:
            return "Black/White";
        case GameVersion::B2W2:
            return "Black 2/White 2";
        // Gen 6
        case GameVersion::X:
            return "X";
        case GameVersion::Y:
            return "Y";
        case GameVersion::OR:
            return "Omega Ruby";
        case GameVersion::AS:
            return "Alpha Sapphire";
        case GameVersion::XY:
            return "X/Y";
        case GameVersion::ORAS:
            return "Omega Ruby/Alpha Sapphire";
        // Gen 7 (3DS)
        case GameVersion::SN:
            return "Sun";
        case GameVersion::MN:
            return "Moon";
        case GameVersion::US:
            return "Ultra Sun";
        case GameVersion::UM:
            return "Ultra Moon";
        case GameVersion::SM:
            return "Sun/Moon";
        case GameVersion::USUM:
            return "Ultra Sun/Ultra Moon";
        case GameVersion::FR:
            return "FireRed";
        case GameVersion::LG:
            return "LeafGreen";
        case GameVersion::FRLG:
            return "FireRed/LeafGreen";
        case GameVersion::GP:
            return "Let's Go Pikachu";
        case GameVersion::GE:
            return "Let's Go Eevee";
        case GameVersion::SW:
            return "Sword";
        case GameVersion::SH:
            return "Shield";
        case GameVersion::BD:
            return "Brilliant Diamond";
        case GameVersion::SP:
            return "Shining Pearl";
        case GameVersion::PLA:
            return "Legends Arceus";
        case GameVersion::SL:
            return "Scarlet";
        case GameVersion::VL:
            return "Violet";
        case GameVersion::ZA:
            return "Legends Z-A";
        case GameVersion::GG:
            return "Let's Go Pikachu/Eevee";
        case GameVersion::SWSH:
            return "Sword/Shield";
        case GameVersion::BDSP:
            return "Brilliant Diamond/Shining Pearl";
        case GameVersion::SV:
            return "Scarlet/Violet";
        default:
            return "Unknown";
        }
    }

    inline const char *getSaveFileName(GameVersion group)
    {
        switch (group)
        {
        case GameVersion::GG:
            return "savedata.bin"; // Let's Go
        case GameVersion::SWSH:
            return "main"; // Sword/Shield
        case GameVersion::BDSP:
            return "SaveData.bin"; // BDSP (verified via Checkpoint backup)
        case GameVersion::PLA:
            return "main"; // Legends: Arceus
        case GameVersion::SV:
            return "main"; // Scarlet/Violet
        default:
            return "main";
        }
    }

    /**
     * True if a Pokemon originating from `version` displays its trainer ID as the Gen 7+
     * SIX-DIGIT id, rather than the classic 16-bit TID.
     *
     * The games pick this from the Pokemon's ORIGIN generation, not from the save it currently
     * lives in -- so a FireRed Rattata sitting in Shining Pearl still shows its 16-bit TID while
     * a native BDSP pokemon beside it shows six digits. Mirrors PKHeX
     * (GameDataCore.TrainerIDDisplayFormat: `Version.Generation >= 7 ? SixDigit : SixteenBit`).
     *
     * Takes a raw version byte rather than a GameVersion, because a banked pokemon can carry an origin
     * from a game PKSE does not itself support (a HOME-transferred Sun/Moon or Gen 4/5/6 pokemon), and
     * those ids are absent from the enum above. Gen 7+ is versions 30-33 (SM/USUM) plus 42 and up
     * (Let's Go, Gen 8, Gen 9). Note 35-41 are the Gen 1/2 Virtual Console games, which sit ABOVE
     * the Gen 7 SM/USUM ids numerically but are of course 16-bit.
     */
    inline bool usesSixDigitTrainerID(uint8_t version)
    {
        return (version >= 30 && version <= 33) || version >= 42;
    }

    /**
     * Human-readable name for a raw ORIGIN version byte (the PKM Version field). Covers EVERY
     * game a HOME-transferred pokemon can carry -- Gen 1 through 9 -- not just the Switch titles
     * PKSE itself edits, so the details modal's "Origin" row shows a transferred Sun/Moon or
     * Gen 4/5/6 pokemon's real game instead of "Unknown". Ids + names match PKHeX's GameVersion enum.
     */
    inline std::string getOriginGameName(uint8_t version)
    {
        switch (version)
        {
        // Gen 3 (GBA) + GameCube
        case 1:
            return "Sapphire";
        case 2:
            return "Ruby";
        case 3:
            return "Emerald";
        case 4:
            return "FireRed";
        case 5:
            return "LeafGreen";
        case 15:
            return "Colosseum/XD";
        // Gen 4 (NDS)
        case 7:
            return "HeartGold";
        case 8:
            return "SoulSilver";
        case 10:
            return "Diamond";
        case 11:
            return "Pearl";
        case 12:
            return "Platinum";
        case 16:
            return "Battle Revolution";
        // Gen 5 (NDS)
        case 20:
            return "White";
        case 21:
            return "Black";
        case 22:
            return "White 2";
        case 23:
            return "Black 2";
        // Gen 6 (3DS)
        case 24:
            return "X";
        case 25:
            return "Y";
        case 26:
            return "Alpha Sapphire";
        case 27:
            return "Omega Ruby";
        // Gen 7 (3DS)
        case 30:
            return "Sun";
        case 31:
            return "Moon";
        case 32:
            return "Ultra Sun";
        case 33:
            return "Ultra Moon";
        case 34:
            return "Pokemon GO";
        // Virtual Console (Gen 1/2)
        case 35:
            return "Red";
        case 36:
            return "Blue";
        case 37:
            return "Blue (JP)";
        case 38:
            return "Yellow";
        case 39:
            return "Gold";
        case 40:
            return "Silver";
        case 41:
            return "Crystal";
        // Nintendo Switch
        case 42:
            return "Let's Go Pikachu";
        case 43:
            return "Let's Go Eevee";
        case 44:
            return "Sword";
        case 45:
            return "Shield";
        case 47:
            return "Legends Arceus";
        case 48:
            return "Brilliant Diamond";
        case 49:
            return "Shining Pearl";
        case 50:
            return "Scarlet";
        case 51:
            return "Violet";
        case 52:
            return "Legends Z-A";
        case 53:
            return "Champions";
        default:
            return "Unknown";
        }
    }

    /** Generation (1-9) of a raw origin/format version byte; 0 if unknown. Matches PKHeX's GameVersion buckets. */
    inline int getVersionGeneration(uint8_t version)
    {
        // 35-41 are the Virtual Console re-releases. They were absent here while
        // getOriginGameName() already named all seven of them, so a transferred Gen 1/2 pokemon
        // showed a real game beside generation 0 -- and 0 is this function's "unknown", which
        // callers read as a definitive negative. Same failure shape as a missing data table.
        if (version >= 35 && version <= 38)
            return 1; // Red / Green / Blue / Yellow
        // Gold / Silver / Crystal
        if (version >= 39 && version <= 41) return 2;
        if ((version >= 1 && version <= 5) || version == 15)
            return 3;
        if (version == 7 || version == 8 || (version >= 10 && version <= 12) || version == 16)
            return 4;
        if (version >= 20 && version <= 23)
            return 5;
        if (version >= 24 && version <= 27)
            return 6;
        // SM/USUM/GO + Let's Go
        if ((version >= 30 && version <= 34) || version == 42 || version == 43) return 7;
        if (version >= 44 && version <= 49)
            return 8;
        if (version >= 50 && version <= 52)
            return 9;
        return 0;
    }

    /**
     * Is this save-format GROUP one of the GBA games?
     *
     * There are two of them -- FRLG for FireRed/LeafGreen and RSE for Ruby/Sapphire/Emerald --
     * and everything Gen 3 shares is shared by both: the PK3 record, the item id space, the
     * five-digit trainer id, the absence of an ability-number field, and having no general form
     * byte. All five games are one generation and differ only in save-container offsets.
     *
     * It exists because the question kept being written as `group == FRLG`, which was true while
     * FireRed/LeafGreen were the only Gen 3 group PKSE opened and became a silent bug the moment
     * they were not -- a Ruby Pokemon would have had its held item named out of the modern item
     * table, its trainer id printed with six digits, and an ability number written into a format
     * that has no field for one. Ask this instead of naming a group, and a third Gen 3 group
     * cannot be missed.
     *
     * Note getVersionGeneration() cannot answer this: it takes a stored VERSION byte, and a group
     * value is not one.
     */
    inline constexpr bool isGen3Group(GameVersion group) noexcept
    {
        return group == GameVersion::FRLG || group == GameVersion::RSE;
    }

    /** A representative stored version byte for a storage-format game group, for location-table routing. */
    inline uint8_t getGroupRepVersion(GameVersion group)
    {
        switch (group)
        {
        case GameVersion::RBY:
            return 35; // Red -- see gameTitle(): a Gen 1 save cannot name
                       // which of the pair it is, so the rep claims the
                       // first, exactly as the other pairs below do.
        case GameVersion::GSC:
            return 39; // Gold -- see the RBY note: the pair cannot be
                       // told apart from the save, so the rep claims the first
        case GameVersion::DP:
            return 10; // Diamond
        case GameVersion::PT:
            return 12; // Platinum
        case GameVersion::HGSS:
            return 7; // HeartGold
        case GameVersion::BW:
            return 21; // Black   (PKHeX numbers White 20, Black 21)
        case GameVersion::B2W2:
            return 23; // Black 2
        case GameVersion::XY:
            return 24; // X
        case GameVersion::ORAS:
            return 27; // Omega Ruby
        case GameVersion::SM:
            return 30; // Sun
        case GameVersion::USUM:
            return 32; // Ultra Sun
        case GameVersion::FRLG:
            return 4; // FireRed
        case GameVersion::RSE:
            return 2; // Ruby -- Ruby and Sapphire carry no byte that separates them, so the rep
                      // claims the first of the pair, as RBY, GSC and DP do. Emerald IS detectable
                      // and never reaches this: it names itself.
        case GameVersion::GG:
            return 42; // Let's Go Pikachu
        case GameVersion::SWSH:
            return 44;
        case GameVersion::PLA:
            return 47;
        case GameVersion::BDSP:
            return 48;
        case GameVersion::SV:
            return 50;
        case GameVersion::ZA:
            return 52;
        default:
            return 50;
        }
    }

    /**
     * Which stored version's met-location table names a location, per PKHeX GameStrings.GetGeneration
     * (for a Gen 5+ format): a Gen 5+ origin keeps its OWN table; a Gen 4 EGG location stays on the Gen 4
     * table; but a Gen 3/4 MET location (and a Gen 3 egg) is remapped into the CURRENT format's numbering
     * when the pokemon is transferred up, so it must be named with the format's table -- not the origin's.
     * Without this a Platinum starter link-traded up to Scarlet/Violet reads its met as "(none)".
     */
    inline uint8_t locationTableVersion(uint8_t originVersion, uint8_t formatVersion, bool isEggLocation)
    {
        // Pokemon GO: its met marker uses the current format's numbering
        if (originVersion == 34) return formatVersion;
        const int originGeneration = getVersionGeneration(originVersion);
        if (originGeneration >= 5)
            return originVersion;
        if (originGeneration == 4 && isEggLocation)
            return originVersion;
        return formatVersion;
    }

    /**
     * Is this a game PKSE can open, edit and write back?
     *
     * DERIVED, NOT LISTED. A second list of supported games is a list that falls behind the
     * first one, and getGameGroup() IS the first one: a title is supported exactly when it
     * resolves to a save-format group, because the group is what every layer -- Trainer, Pokemon,
     * Encryption, Inventory -- dispatches on, and a title with no group has nothing to open it
     * with. Adding a game means adding its getGameGroup case, so this answer cannot drift.
     *
     * A save-format GROUP is answered too, for its games: a caller holding getGameGroup(...)
     * rather than a title must not be told "unsupported" for a format PKSE implements.
     *
     * THIS GATES NOTHING. The save picker offers whatever getGameVersion(titleId) recognises and
     * a loose file is admitted by Save::openExternalSave; neither consults this. What keeps a
     * half-finished format away from users is not writing its title id or its detect() entry yet
     * -- not leaving this false.
     */
    inline bool isGameSupported(GameVersion version)
    {
        switch (version)
        {
        // Every save-format group PKSE implements, which is all nineteen of them.
        case GameVersion::RBY:
        case GameVersion::GSC:
        case GameVersion::RSE:
        case GameVersion::FRLG:
        case GameVersion::DP:
        case GameVersion::PT:
        case GameVersion::HGSS:
        case GameVersion::BW:
        case GameVersion::B2W2:
        case GameVersion::XY:
        case GameVersion::ORAS:
        case GameVersion::SM:
        case GameVersion::USUM:
        case GameVersion::GG:
        case GameVersion::SWSH:
        case GameVersion::BDSP:
        case GameVersion::PLA:
        case GameVersion::SV:
        case GameVersion::ZA:
            return true;

        // The generational groupings are neither a game nor a save format -- no file is "a Gen 8
        // save" and nothing opens one. They exist for logic branching, so the question does not
        // apply to them and false is the honest answer rather than a gap.
        case GameVersion::Gen7B:
        case GameVersion::Gen8:
        case GameVersion::Gen9:
            return false;

        // Every individual title, answered by the table that already knows. Invalid resolves to
        // Invalid and so reports false, which is also correct.
        default:
            return getGameGroup(version) != GameVersion::Invalid;
        }
    }
}

#endif
