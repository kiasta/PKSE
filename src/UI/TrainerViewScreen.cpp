#include <cstring>
#include <ctime>     // std::time / std::localtime -> a created mon's met date = today
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>

#include "Names/ItemPouches.h"   // getPouchItems -> the add-item picker's per-pouch list
#include "Names/MoveInfo.h"      // getMoveBasePP -> a created/picked move gets real PP, not 0
#include "Names/MoveNames.h"     // getMoveCount -> scan for a created mon's first legal move
#include "Names/MovePresence.h"  // isMovePresent -> hide moves a game doesn't have (PKHeX-style filter)
#include "Names/LocationNames.h" // getLocationTable -> the Met Location picker's per-game list
#include "Names/FormNames.h"     // getDisplayName -> variant prefix in the "can't go in this game" toast
#include "Enums/Ball.h"          // getBallList -> the per-game Ball picker
#include "Legality/Legality.h"   // analyze() -> gate the details-page Legality (R) button when clean

#include "Globals.h"
#include "Save/GetSaveFileContents.h"
#include "UI/TrainerViewScreen.h"
#include "UI/TouchInput.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"
#include "UI/Panels/PartyPokemonPanel.h"
#include "UI/Panels/BoxPokemonPanel.h"
#include "UI/Panels/ItemsPanel.h"
#include "UI/Panels/StoragePanel.h"
#include "UI/Panels/HomeMenuPanel.h"
#include "UI/Dialogs/ItemEditDialog.h"
#include "UI/Dialogs/SaveConfirmDialog.h"
#include "UI/Dialogs/StatEditDialog.h"
#include "UI/Modals/PokemonDetailsModal.h"
#include "Utils/HelperUtilities.h"
#include "Utils/Keyboard.h"
#include "Utils/Logger.h"
#include "Utils/EventLog.h"
#include "Utils/FileUtilities.h"
#include "Utils/Settings.h"
#include "Trainer/Trainer.h"
#include "Trainer/Inventory.h"
#include "Trainer/Inventory9LZA.h"
#include "Trainer/Inventory9SV.h"
#include "Trainer/Inventory8LA.h"
#include "Trainer/Inventory8BDSP.h"
#include "Trainer/Inventory7LGPE.h"
#include "Trainer/Inventory3FRLG.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h"       // getAbilitySlots -> the per-game legal ability list
#include "Pokemon/FormInfo.h"          // isBattleOnlyForm -> filters the Form picker
#include "Pokemon/LearnsetTable.h"
#include "Conversion/Convert.h"
#include "Utils/StringHelpers.h"

namespace UI {
    static std::string leafName(const std::string& path);   // defined below; used by the ctor's trace

    // UI Layout constants
    constexpr int LEFT_PANEL_X = 12;
    constexpr int LEFT_PANEL_Y = 80;
    constexpr int LEFT_TRAINER_INFO_PANEL_WIDTH = 220;
    constexpr int LEFT_TRAINER_INFO_PANEL_HEIGHT = 210;
    constexpr int LEFT_VIEW_MODE_PANEL_WIDTH = 220;
    constexpr int LEFT_VIEW_MODE_PANEL_HEIGHT = 205;  // fits 4 modes (Party/Boxes/Items/Storage)
    constexpr int LEFT_PANEL_SPACING = 12;
    constexpr int CONTENT_PANEL_X = LEFT_PANEL_X + LEFT_VIEW_MODE_PANEL_WIDTH + 12;
    constexpr int CONTENT_PANEL_Y = LEFT_PANEL_Y;
    constexpr int CONTENT_PANEL_HEIGHT = 560;

    // Creator: build a valid, game-accepted default Pokemon in the current save's format. Species-
    // correct ability / gender / friendship come from the personal table; the user refines the rest
    // in the details editor. See scratchpad/creator_plan.md.
    // Randomize IVs: roll the six IVs to fresh 0-31 values and NOTHING else. Deliberately IVs
    // only -- nature, ability, and everything else are kept, so it can never silently change the
    // mon's identity. (A broader encounter-consistent randomizer -- PID/nature/ability from a real
    // encounter -- is a future version; see docs/FUTURE_VERSIONS.md.)
    static void randomizeIVs(Pokemon::Pokemon* p) {
        if (!p) return;
        for (int i = 0; i < 6; ++i)
            p->setIV(i, static_cast<uint8_t>(Utils::rand32() % 32));
        p->recalculateStats();
        p->refreshChecksum();
    }

    // Origin version byte for the save that is open. A game GROUP deliberately collapses a version pair
    // into one value -- it exists to say "these games share a save format" -- so picking the origin from
    // it cannot tell Violet from Scarlet, and whatever it stamps claims the FIRST game of the pair
    // (Violet -> Scarlet, Shield -> Sword, Shining Pearl -> Brilliant Diamond, Let's Go Eevee -> Let's Go
    // Pikachu, LeafGreen -> FireRed). The title id knows exactly which game is open, and the enum's
    // per-game values ARE the stored origin bytes, so it is used directly; the group only supplies a
    // fallback for an id we don't recognise. Gen 3 stores origin in a 4-bit field, and both its values
    // (FR = 4, LG = 5) fit, so distinguishing them is safe.
    //
    // Used by BOTH places that stamp an origin: the creator, and the bank's down-convert into Gen 3.
    // They had the same bug and only the creator's was fixed; one resolver is the point.
    static uint8_t saveOriginVersion(Trainer::Trainer& tr, u64 titleId) {
        const Enums::GameVersion v = Enums::getGameVersion(titleId);
        if (v != Enums::GameVersion::Invalid && Enums::getGameGroup(v) == tr.getGameGroup())
            return static_cast<uint8_t>(v);
        return Enums::getGroupRepVersion(tr.getGameGroup());
    }

    // A game group's bit in PersonalInfo::presence. FireRed/LeafGreen has no bit (Gen 3 predates the
    // bitmask), so it returns 0 -- callers must treat that as "no data", never as "present nowhere".
    static uint8_t personalPresenceBit(Enums::GameVersion group) {
        switch (group) {
            case Enums::GameVersion::GG:   return Pokemon::PERSONAL_GAME_GG;
            case Enums::GameVersion::SWSH: return Pokemon::PERSONAL_GAME_SWSH;
            case Enums::GameVersion::BDSP: return Pokemon::PERSONAL_GAME_BDSP;
            case Enums::GameVersion::PLA:  return Pokemon::PERSONAL_GAME_PLA;
            case Enums::GameVersion::SV:   return Pokemon::PERSONAL_GAME_SV;
            case Enums::GameVersion::ZA:   return Pokemon::PERSONAL_GAME_ZA;
            default: return 0;
        }
    }

    // True when ANY form of the species is flagged present in this game.
    //
    // Form 0 is not the question, and asking it is a bug in its own right: Legends: Arceus has no
    // Unovan Braviary, so #628's form-0 row has the PLA bit clear while its Hisuian form-1 row has it
    // set. A form-0-only test therefore reads "Braviary is not in this game" and drops the species
    // whole -- which is how the creator's list came to be 226 species instead of 242. Sixteen
    // Hisuian-only species are in that position (Growlithe, Arcanine, Voltorb, Electrode, Typhlosion,
    // Qwilfish, Samurott, Lilligant, Basculin, Zorua, Zoroark, Braviary, Sliggoo, Goodra, Avalugg,
    // Decidueye); PLA is the only game where any species is, but the test is wrong everywhere.
    static bool speciesPresentIn(uint16_t species, uint8_t bit) {
        const Pokemon::PersonalInfo& base = Pokemon::getPersonalInfo(species, 0);
        if ((base.presence & bit) != 0) return true;
        int count = base.formCount;
        if (count < 1) count = 1;
        for (int f = 1; f < count; ++f)
            if ((Pokemon::getPersonalInfo(species, static_cast<uint8_t>(f)).presence & bit) != 0)
                return true;
        return false;
    }

    // The forms of `species` that `group` can actually hold, ascending. Three filters, all asking the
    // same question -- can a save legitimately contain this? -- of three different obstacles:
    //   * battle-only : the game overwrites it (Megas, Zen, ride builds)
    //   * not present : the game has no such form (Unovan Braviary in Legends: Arceus)
    //   * Lord/Lady   : the game has it and keeps it, but the player never catches a noble
    //
    // formCount is the UNION across every supported game, so it over-reports per game -- Braviary has
    // two rows but Legends: Arceus only ever had the Hisuian one. Without the presence half of this
    // filter the Form picker offered a base Braviary that game has never had.
    //
    // Two callers, and they MUST agree: the Form picker, and the form a newly created mon starts on.
    // A created mon sitting on a form its own picker won't offer looks exactly like the bug above.
    //
    // Presence is skipped in two cases, both of which mean "the table can't answer", never "no forms":
    // Gen 3 has no presence bit at all, and a species that is off-dex HERE has the bit clear on every
    // one of its forms -- filtering on that would strand an already-present mon on a one-row picker.
    static std::vector<int> selectableForms(uint16_t species, Enums::GameVersion group) {
        int count = Pokemon::getPersonalInfo(species, 0).formCount;
        if (count < 1) count = 1;
        const uint8_t bit = personalPresenceBit(group);
        const bool byPresence = (bit != 0) && speciesPresentIn(species, bit);
        std::vector<int> out;
        for (int f = 0; f < count; ++f) {
            if (Pokemon::isBattleOnlyForm(species, static_cast<uint8_t>(f))) continue;
            if (Pokemon::isLordForm(species, static_cast<uint8_t>(f))) continue;
            if (byPresence &&
                (Pokemon::getPersonalInfo(species, static_cast<uint8_t>(f)).presence & bit) == 0) continue;
            out.push_back(f);
        }
        return out;
    }

    // The genders a species can actually be, from its personal-table gender ratio (0 = Male,
    // 1 = Female, 2 = Genderless -- the values the entity byte stores).
    //
    // A fixed-gender species has exactly ONE: Braviary is male-only, Miltank female-only, Magnemite
    // genderless. Like the form filters this is a HARD rule that "Allow illegal edits" does not lift.
    // A 255 EV is a real number in a field that holds numbers; a female Braviary is not a thing any of
    // these games has -- the species' gender is a property of the species, not a value with a legal
    // range, so there is no unusual-but-storable version of it to permit.
    //
    // A dual-gender species likewise never includes Genderless: that value means "this species has no
    // gender", which is false for it, and the games render it as a blank where a symbol should be.
    //
    // The ratio is read per FORM, not per species -- Bloodmoon Ursaluna and the cap Pikachu are
    // male-only forms of dual-gender species, so the form must be passed in rather than defaulted
    // to 0. But per-form is not the whole rule: for Meowstic, Indeedee, Basculegion and Oinkologne
    // the form IS the gender, so form 0's male-only ratio is a fact about that form and not about
    // the species. Reading it as a fixed-gender species is what locked a Meowstic to whichever
    // gender it happened to be. Those four always offer both; the form follows the pick.
    //
    // Offering both unconditionally is safe because the two gendered forms carry identical presence
    // bits -- no game has one gender of these four without the other (checked against the table).
    // Worth re-checking if the personal table is ever regenerated for a new game.
    static std::vector<int> selectableGenders(uint16_t species, uint8_t form) {
        if (Pokemon::isFormGenderSpecific(species)) return { 0, 1 };
        switch (Pokemon::getPersonalInfo(species, form).genderRatio) {
            case 255: return { 2 };      // genderless
            case 254: return { 1 };      // always female
            case 0:   return { 0 };      // always male
            default:  return { 0, 1 };   // a threshold -> either, but never genderless
        }
    }

    // The name a mon shows when it has NO custom nickname. The games store the DISPLAY string in the
    // nickname field itself, so "not nicknamed" still means writing the species name there -- leaving
    // the field blank shows a blank name in-game. Gen 3 stores that default UPPERCASE, as FRLG shows it.
    //
    // Uppercasing the UTF-8 bytes is safe: no byte of a multi-byte sequence falls in 'a'-'z' (lead
    // bytes are >= 0xC0, continuations 0x80-0xBF), so NIDORAN♀ and FARFETCH'D keep their sign and
    // apostrophe intact.
    static std::string defaultNicknameFor(const Pokemon::Pokemon& p) {
        std::string s = p.species();
        if (p.getGameGroup() == Enums::GameVersion::FRLG) {
            for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return s;
    }

    static std::unique_ptr<Pokemon::Pokemon> buildDefaultMon(Trainer::Trainer& tr, uint16_t species,
                                                            uint8_t version) {
        auto p = tr.createBlankPokemon();
        if (!p) return p;
        // Start on the lowest form this game HAS, which is not always form 0: Legends: Arceus never had
        // a base Braviary or a Kanto Growlithe, so creating one there has to begin on the Hisuian form.
        // Everything below keys off this form, not off 0 -- a regional variant has its own abilities,
        // gender ratio and friendship, and reading form 0's would quietly stamp the wrong ones.
        const std::vector<int> forms = selectableForms(species, tr.getGameGroup());
        const uint8_t form = forms.empty() ? 0 : static_cast<uint8_t>(forms.front());
        const Pokemon::PersonalInfo& pi = Pokemon::getPersonalInfo(species, form);
        p->setSpecies(species);          // first: drives the EXP growth rate + base-stat lookup
        p->setForm(form);
        p->setEncryptionConstant(Utils::rand32());
        p->setPID(Utils::rand32());
        // ...and re-apply the form's EC correlation, because the random EC above lands AFTER setForm
        // and would otherwise undo it. Maushold and Dudunsparce read their form from EC % 100, so a
        // freshly rolled EC has a 99-in-100 chance of contradicting the form just set. A no-op for
        // every other species; see FormInfo.
        p->setEncryptionConstant(
            Pokemon::correctEncryptionConstantForForm(species, form, p->encryptionConstant()));
        // Starting level. Gen 3 has NO level-1 Pokemon: its eggs hatch at 5 and its lowest wild
        // encounters are 2, so level 1 is unobtainable there -- and it is the one level whose total EXP
        // is **0**, the degenerate input to the game's level-from-EXP lookup. Gen 8+ eggs really do hatch
        // at level 1, so every other format keeps it.
        const uint8_t startLevel = (tr.getGameGroup() == Enums::GameVersion::FRLG) ? 5 : 1;
        p->setLevel(startLevel);         // after species; writes EXP from growth rate + recalcs stats
        // Random nature at birth (the real and stat/mint nature start matched; the mint stays editable).
        { uint8_t nat = static_cast<uint8_t>(Utils::rand32() % 25); p->setNature(nat); p->setStatNature(nat); }
        // Random gender, constrained by the species ratio: fixed for genderless (255) / female-only (254);
        // otherwise rolled against the female threshold (genderRatio 0 -> always male).
        { uint8_t g = (pi.genderRatio == 255) ? 2
                    : (pi.genderRatio == 254) ? 1
                    : (((Utils::rand32() & 0xFF) < pi.genderRatio) ? 1 : 0);
          p->setGender(g); }
        // Slot-1 ability, from the table for the game being created into (Gen 3's slot pair is
        // its own; pi is the modern one). setAbility resolves the slot; Gen 3 also re-rolls the PID.
        { const Pokemon::AbilitySlots slots =
              Pokemon::getAbilitySlots(species, form, tr.getGameGroup());
          p->setAbility(slots.slot[0]);
          if (tr.getGameGroup() != Enums::GameVersion::FRLG) p->setAbilityNumber(1); }
        p->setFriendship(pi.baseFriendship);
        // Poké Ball -- but Legends: Arceus uses its own ball set, where the Poké Ball is id 28 (the
        // standard Poké Ball id 4 isn't one of its balls and reads as the wrong ball in-game).
        p->setBall(tr.getGameGroup() == Enums::GameVersion::PLA ? 28 : 4);
        p->setLanguage(2);               // English
        p->setOriginGame(version);
        p->setMetLevel(startLevel);      // met AT the level it is, not at 1 -- Gen 3 has no met level 1 either
        // Met "here, today": a valid caught date + a real location, so the mon doesn't read as met on
        // 00/00/2000 at location 0 ("nothing" -- which BDSP renders as "hatched from an egg at Jubilife
        // City"). Date components are years-since-2000 / 1-based month / day; formats without a met date
        // (Gen 3) no-op these setters.
        std::time_t nowT = std::time(nullptr);
        if (const std::tm* lt = std::localtime(&nowT)) {
            p->setMetYear(static_cast<uint8_t>(((lt->tm_year + 1900) - 2000) & 0xFF));
            p->setMetMonth(static_cast<uint8_t>(lt->tm_mon + 1));
            p->setMetDay(static_cast<uint8_t>(lt->tm_mday));
        }
        // A real, recognizable in-game met location for every game group, so a created mon is never met
        // at location 0 / "(none)". Ids are per-game (each game has its own location table + numbering).
        switch (tr.getGameGroup()) {
            case Enums::GameVersion::SV:   p->setMetLocation(8);   break;  // Mesagoza
            case Enums::GameVersion::ZA:   p->setMetLocation(11);  break;  // Centrico Plaza
            case Enums::GameVersion::BDSP: p->setMetLocation(38);  break;  // Oreburgh City
            case Enums::GameVersion::PLA:  p->setMetLocation(7);   break;  // Obsidian Fieldlands
            case Enums::GameVersion::SWSH: p->setMetLocation(12);  break;  // Route 1 (Galar)
            case Enums::GameVersion::GG:   p->setMetLocation(3);   break;  // Route 1 (Kanto)
            case Enums::GameVersion::FRLG: p->setMetLocation(101); break;  // Route 1 (Kanto, Gen 3 numbering)
            default: break;
        }
        // Not from an egg (a caught mon): clear egg origin. The "no egg location" value is game-specific.
        // BDSP uses Gen 4-style numbering where 0 is a REAL place (Jubilife City) and 65535 is "none"
        // (PKHeX Locations.Default8bNone) -- writing 0 there made a created mon read as "hatched from an
        // Egg" received at Jubilife City. Every other format uses 0 for "none".
        p->setEgg(false);
        p->setEggLocation(tr.getGameGroup() == Enums::GameVersion::BDSP ? 0xFFFF : 0x0000);
        p->setEggYear(0); p->setEggMonth(0); p->setEggDay(0);
        // Clear the HOME ribbon+mark block (0x34-0x45) so a created mon owns no stray ribbon, AND reset
        // AffixedRibbon -- the byte that selects which ribbon the game DISPLAYS. On an all-zero blank it
        // is 0, and ribbon index 0 is the Kalos Champion ribbon, so a fresh mon SHOWED it even with no
        // ribbon owned (the SV report). "None" is 0xFF. A zero offset means the format has no such field
        // (FRLG / GG), which also gates the ribbon-ownership clear to the formats that carry the block.
        // Both fields sit in the checksummed region, covered by the refreshChecksum() below.
        //
        // The offset table is Conversion::affixedRibbonOffset, shared with the cross-gen converter --
        // the same fact bit twice, once here and once through transfer, so it is defined once.
        if (const size_t affix = Conversion::affixedRibbonOffset(tr.getGameGroup()); affix != 0) {
            auto d = p->getData();
            if (d.size() >= 0x46) std::memset(d.data() + 0x34, 0, 0x46 - 0x34);
            if (d.size() > affix) d.data()[affix] = std::byte{Conversion::AFFIXED_RIBBON_NONE};
        }
        // First move = the species' first legal move for this game, so the created mon is legal. PLA
        // rejects an unlearnable move as a Bad Egg in-game (Pound isn't a Vulpix move there); the other
        // games merely flag it.
        //
        // The FORM belongs in this lookup as much as it does in the stat and ability ones above.
        // Learnsets are keyed on (species, form) and a regional form has its own: asking for form 0
        // got the Unovan Braviary row, which in Legends: Arceus does not exist at all. getLearnableBits
        // returns nullptr there, every move reads "not learnable", and the old `defMove = 1` fallback
        // turned that into POUND -- an illegal move, on a mon the creator had just built to be legal,
        // in the one game that Bad-Eggs it. A missing table is not an empty movepool.
        uint16_t defMove = 0;
        for (uint16_t m = 1; m < Names::getMoveCount(); ++m)
            if (Pokemon::isLearnable(species, form, tr.getGameGroup(), m)
                && Names::isMovePresent(m, tr.getGameGroup())) { defMove = m; break; }
        if (defMove == 0) {
            // No learnset row for this (species, form) in this game. Every move is now a guess, so
            // leave the slot EMPTY rather than write a plausible-looking wrong one: an empty slot is
            // visible in the editor and harmless in-game, where a wrong move is neither. Log it --
            // reaching here means the learnset table is missing a row the creator can offer.
            Utils::logErrorToFile("buildDefaultMon: no learnable move for species " +
                                  std::to_string(species) + " form " + std::to_string(form) +
                                  " -- leaving move 1 empty");
        }
        p->setMove(0, defMove);
        p->setMovePP(0, Names::getMoveBasePP(defMove, tr.getGameGroup()));
        p->setMovePPUps(0, 0);
        p->setId32(tr.ID32);
        p->setOTName(Utils::utf8ToUtf16(tr.trainerName));
        p->setOTGender(tr.trainerGender);   // match the trainer -- else Gen 3 reads it as "Apparently met"
        // Default nickname = the species name (see defaultNicknameFor). isNicknamed is left false --
        // this is the species default, not a custom name.
        p->setNickname(Utils::utf8ToUtf16(defaultNicknameFor(*p)));
        // Let's Go stores + DISPLAYS absolute height/weight (floats at PB7 0x2C / 0xE4); PK8/PK9 keep
        // only the 0-255 scalar and derive the size on the fly. A created LGPE mon left those floats at
        // 0, so it showed 0'00" / 0.0 lbs in-game. Roll random size scalars and compute the absolutes
        // from the species base size -- same formula as the bank convert (PKHeX PB7.Get*Absolute).
        if (tr.getGameGroup() == Enums::GameVersion::GG) {
            auto d = p->getData();
            if (d.size() >= 0xE8) {
                const uint8_t hs = static_cast<uint8_t>(Utils::rand32() & 0xFF);
                const uint8_t ws = static_cast<uint8_t>(Utils::rand32() & 0xFF);
                d.data()[0x3A] = static_cast<std::byte>(hs);
                d.data()[0x3B] = static_cast<std::byte>(ws);
                const float hr = (hs / 255.0f) * 0.79999995f + 0.6f;   // height ratio (-20% .. +40%)
                const float wr = (ws / 255.0f) * 0.40000004f + 0.8f;   // weight ratio (+/- 20%)
                const float hAbs = hr * static_cast<float>(pi.height);
                const float wAbs = hr * wr * static_cast<float>(pi.weight);
                std::memcpy(d.data() + 0x2C, &hAbs, sizeof(float));
                std::memcpy(d.data() + 0xE4, &wAbs, sizeof(float));
            }
        }
        // Roll a random IV spread at birth (same routine as the L-button randomizer, which is unchanged);
        // this recalculates stats and refreshes the checksum, so it stands in for the final pass.
        randomizeIVs(p.get());
        p->setStatHPCurrent(p->statHPMax());
        return p;
    }

    // Settings view (the toggle rows), reached from the HOME menu's Settings icon.
    static void drawSettingsView(TrainerViewScreen& screen, PKSEFramebuffer& fb, int x, int y, int w, int h) {
        fb.drawFilledRoundedRect(x, y, w, h, 16, Colors::Panel);
        fb.drawRoundedRect(x, y, w, h, 16, Colors::Border, 1);
        constexpr int hH = 46;
        fb.drawFilledRoundedRect(x, y, w, hH, 16, Colors::AccentDim);
        fb.drawFilledRect(x, y + hH - 16, w, 16, Colors::AccentDim);
        fb.drawText(x + 22, y + (hH - fb.lineHeight(TextStyle::Heading)) / 2, "Settings", Colors::Text, TextStyle::Heading);

        screen.touchButtons.clear();
        constexpr int kRows = 6;
        // The injection row names its actual scope. It does NOT govern saving a cart-loaded session
        // back to the cart -- that is always allowed. It only unlocks writing an OLDER BACKUP over
        // the live save, which is the case that can roll a game backwards.
        const char* labels[kRows] = {
            "Auto-Backup on Load",
            "Theme",
            "Allow Illegal Values",
            "Bank Storage LGPE Move Warning",
            "Allow Inject Backups to Game Save",
            "Enable Debug Logging"
        };
        std::string values[kRows] = {
            g_autoBackupEnabled ? "On" : "Off",
            (g_themeMode == ThemeMode::Dark) ? "Dark" : "Light",
            g_allowIllegalEdits ? "On" : "Off",
            g_moveWarn ? "On" : "Off",
            g_injectToGameSave ? "On" : "Off",
            g_debugLogging ? "On" : "Off",
        };
        // The panel is a fixed height, so the rows have to fit inside it -- there is no scrolling
        // here. Six rows leave 10px spare; rowH stays at 64 because it is also the touch target.
        // The assert is the point: add a seventh row and the build stops rather than quietly
        // drawing it past the panel edge.
        constexpr int rowW = 720, rowH = 64, rowGap = 12, rowsTop = 22, footerH = 26;
        static_assert(hH + rowsTop + kRows * (rowH + rowGap) + footerH <= CONTENT_PANEL_HEIGHT,
                      "Settings rows no longer fit the content panel -- shrink the rows or add scrolling");
        const int rx = x + (w - rowW) / 2;
        int ry = y + hH + rowsTop;
        for (int i = 0; i < kRows; ++i) {
            const bool sel = (screen.settingsSelectedRow == i);
            fb.drawSoftShadow(rx, ry, rowW, rowH, 14);
            fb.drawFilledRoundedRect(rx, ry, rowW, rowH, 14, sel ? Colors::Selected : Colors::PanelAlt);
            if (sel) fb.drawRoundedRect(rx, ry, rowW, rowH, 14, Colors::Accent, 2);
            fb.drawText(rx + 24, ry + (rowH - fb.lineHeight(TextStyle::Body)) / 2, labels[i], Colors::Text, TextStyle::Body);
            int vw, vh; fb.measureText(values[i], vw, vh, TextStyle::Body);
            const int pillW = vw + 48, pillH = 40;
            const int px = rx + rowW - pillW - 20, py = ry + (rowH - pillH) / 2;
            // Amber "On" for the benign toggles; RED for the two that can damage real data -- the
            // illegal-values override and game-save injection. Colour carries the risk, not just text.
            Color pillFill = Colors::Background, pillText = Colors::Text;
            if (i == 0 && g_autoBackupEnabled)      { pillFill = Colors::Primary;    pillText = Colors::PrimaryText; }
            else if (i == 2 && g_allowIllegalEdits) { pillFill = Color(200, 80, 80); pillText = Colors::White; }
            else if (i == 3 && g_moveWarn)      { pillFill = Colors::Primary;    pillText = Colors::PrimaryText; }
            else if (i == 4 && g_injectToGameSave)  { pillFill = Color(200, 80, 80); pillText = Colors::White; }
            else if (i == 5 && g_debugLogging)      { pillFill = Colors::Primary;    pillText = Colors::PrimaryText; }
            fb.drawPill(px, py, pillW, pillH, pillFill);
            fb.drawText(px + (pillW - vw) / 2, py + (pillH - vh) / 2, values[i], pillText, TextStyle::Body);
            screen.touchButtons.push_back({ i, rx, ry, rowW, rowH });
            ry += rowH + rowGap;
        }
        fb.drawText(rx, ry + 8, "A: toggle     B: back", Colors::TextDim, TextStyle::Caption);
    }

    // Trainer view (HOME-style ID card), reached from the HOME menu's Trainer icon. The first two
    // rows (Name / Money) are editable; the rows below are informational -- editing TID/SID would
    // re-own every Pokemon already in the save, so it is deliberately left out.
    static void drawTrainerView(TrainerViewScreen& screen, PKSEFramebuffer& fb, int x, int y, int w, int h) {
        Trainer::Trainer& t = screen.trainer;
        fb.drawFilledRoundedRect(x, y, w, h, 16, Colors::Panel);
        fb.drawRoundedRect(x, y, w, h, 16, Colors::Border, 1);
        constexpr int hH = 46;
        fb.drawFilledRoundedRect(x, y, w, hH, 16, Colors::AccentDim);
        fb.drawFilledRect(x, y + hH - 16, w, 16, Colors::AccentDim);
        fb.drawText(x + 22, y + (hH - fb.lineHeight(TextStyle::Heading)) / 2, "Trainer", Colors::Text, TextStyle::Heading);

        screen.touchButtons.clear();

        const int cardW = 680, cardX = x + (w - cardW) / 2;
        int cy = y + hH + 26;

        // --- Editable rows (Name / Money): selectable via cursor + touch, styled like Settings.
        // Gender is display-only, so it sits with the identity rows below rather than leaving a hole
        // the cursor has to jump over. ---
        constexpr int kEditRows = 2;
        const char* labels[kEditRows] = { "Name", "Money" };
        const std::string values[kEditRows] = {
            t.trainerName.empty() ? "(none)" : t.trainerName,
            "$" + std::to_string(t.money),
        };
        const int rowH = 60;
        for (int i = 0; i < kEditRows; ++i) {
            const bool sel = (screen.trainerSelectedRow == i);
            fb.drawSoftShadow(cardX, cy, cardW, rowH, 14);
            fb.drawFilledRoundedRect(cardX, cy, cardW, rowH, 14, sel ? Colors::Selected : Colors::PanelAlt);
            if (sel) fb.drawRoundedRect(cardX, cy, cardW, rowH, 14, Colors::Accent, 2);
            fb.drawText(cardX + 24, cy + (rowH - fb.lineHeight(TextStyle::Body)) / 2, labels[i], Colors::TextDim, TextStyle::Body);
            int vw, vh; fb.measureText(values[i], vw, vh, TextStyle::Body);
            const int pillW = vw + 44, pillH = 38;
            const int px = cardX + cardW - pillW - 20, py = cy + (rowH - pillH) / 2;
            fb.drawPill(px, py, pillW, pillH, sel ? Colors::Primary : Colors::Background);
            fb.drawText(px + (pillW - vw) / 2, py + (pillH - vh) / 2, values[i], sel ? Colors::PrimaryText : Colors::Text, TextStyle::Body);
            screen.touchButtons.push_back({ i, cardX, cy, cardW, rowH });
            cy += rowH + 14;
        }

        cy += 6;
        fb.drawHDivider(cardX + 20, cy, cardW - 40);
        cy += 20;

        // --- Read-only rows (not selectable). ---
        auto infoRow = [&](const char* label, const std::string& value) {
            fb.drawFilledRoundedRect(cardX, cy, cardW, 46, 12, Colors::PanelAlt);
            fb.drawText(cardX + 24, cy + (46 - fb.lineHeight(TextStyle::Body)) / 2, label, Colors::TextDim, TextStyle::Body);
            int vw, vh; fb.measureText(value, vw, vh, TextStyle::Body);
            fb.drawText(cardX + cardW - 24 - vw, cy + (46 - vh) / 2, value, Colors::Text, TextStyle::Body);
            cy += 54;
        };
        infoRow("Gender", t.trainerGender == 0 ? "Male" : "Female");
        infoRow("Trainer ID", std::to_string(t.TID16) + " / " + std::to_string(t.SID16));
        infoRow("Full TID", std::to_string(t.TID));
        infoRow("Full SID", std::to_string(t.SID));
    }

    TrainerViewScreen::TrainerViewScreen(Trainer::Trainer& trainer, const std::string& titleName, const std::string& backupDir, u64 titleId, AccountUid userUid, bool loadedFromCart)
        : trainer(trainer), titleName(titleName), backupDir(backupDir), gameVersion(Utils::getTitleVersion(titleId)), titleId(titleId), userUid(userUid) {
        // Assigned in the body rather than the init list: it is declared far below these members, and
        // C++ initialises in DECLARATION order, so listing it here would only earn a -Wreorder.
        this->loadedFromCart = loadedFromCart;
        saveDestIndex = defaultSaveDestRow();

        // Open on the box the game was last left on (persisted per-game as the "current box"),
        // so the editor lands where the player was. Clamp in case a save holds a stale index.
        {
            const uint8_t cb = trainer.getCurrentBox();
            if (cb < trainer.getBoxCount()) { selectedBoxIndex = static_cast<int>(cb); stSaveBox = static_cast<int>(cb); }
        }

        // Persistent cross-game bank for the Storage view (unified; loads any existing on-SD contents).
        bank = std::make_unique<Trainer::Bank>();
        // A damaged bank file drops slots silently otherwise -- the user would just find Pokemon
        // missing with no explanation.
        if (bank && bank->lastLoadRejects() > 0) {
            postStatus(std::to_string(bank->lastLoadRejects()) +
                       " damaged bank slot(s) could not be read and were skipped.", 480);
        }

        // Open the test trace with everything needed to interpret the rest of the run: which build,
        // which game, where the save came from, and the state of every setting that changes
        // behaviour. Without this a trace is a list of actions with no way to judge them.
        const std::string sessionInfo =
            "pkse=" + VERSION_STRING +
            " game=\"" + titleName + "\" gamever=" + (gameVersion.empty() ? "?" : gameVersion) +
            " src=" + (this->loadedFromCart ? "CART" : "BACKUP") +
            " backup=\"" + leafName(backupDir) + "\"" +
            " rev=\"" + (trainer.saveRevisionString.empty() ? "Base" : trainer.saveRevisionString) + "\"" +
            " " + Utils::logField("ot", trainer.trainerName) +
            " otid32=" + std::to_string(trainer.ID32) +
            " tid=" + std::to_string(trainer.TID16) + " sid=" + std::to_string(trainer.SID16) +
            " party=" + std::to_string(trainer.getPartySize()) +
            " boxes=" + std::to_string(trainer.getBoxCount()) +
            " inject=" + (g_injectToGameSave ? "ON" : "OFF") +
            " illegal=" + (g_allowIllegalEdits ? "ON" : "OFF") +
            " autobackup=" + (g_autoBackupEnabled ? "ON" : "OFF") +
            " movewarn=" + (g_moveWarn ? "ON" : "OFF") +
            " theme=" + (g_themeMode == ThemeMode::Dark ? "dark" : "light");
        Utils::logTestSession(sessionInfo);
        Utils::logEventToFile("SESSION " + sessionInfo);
        if (bank) {
            Utils::logTest("BANKLOAD rejects=" + std::to_string(bank->lastLoadRejects()));
            Utils::logEventToFile("BANK action=LOAD rejects=" + std::to_string(bank->lastLoadRejects()));
        }
    }

    // Non-null cells in the carried block (a block can contain holes -- see moveMon).
    int TrainerViewScreen::carriedCount() const {
        int n = 0;
        for (const auto& p : moveMon) if (p) ++n;
        return n;
    }

    const Pokemon::Pokemon* TrainerViewScreen::firstCarried() const {
        for (const auto& p : moveMon) if (p) return p.get();
        return nullptr;
    }

    // Put the whole carried block back where it was lifted from. Each cell prefers its own original
    // slot; if something has since filled that slot, it falls back to any empty slot in the origin
    // pane, because a carried Pokemon must never be dropped on the floor.
    void TrainerViewScreen::returnHeldToOrigin() {
        if (!carrying()) return;
        const int pane = heldPane;
        if (pane == 1 && !bank) return;
        const int slots = (pane == 0) ? static_cast<int>(trainer.getSlotsPerBox())
                                      : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
        const int boxCount = (pane == 0) ? static_cast<int>(trainer.getBoxCount())
                                         : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
        const int cols = (pane == 0) ? ((slots == 25) ? 5 : 6) : 6;
        auto place = [&](int box, int slot, std::unique_ptr<Pokemon::Pokemon>& pk) -> bool {
            if (box < 0 || box >= boxCount || slot < 0 || slot >= slots) return false;
            if (storageSlotLocked(pane, box, slot)) return false;
            auto& dst = storageSlot(pane, box, slot);
            if (!dst || dst->speciesID() == 0) { dst = std::move(pk); return true; }   // species-0 = empty (S/V ghost)
            return false;
        };
        const int w = selectDimensions.first > 0 ? selectDimensions.first : 1;
        for (size_t i = 0; i < moveMon.size(); ++i) {
            if (!moveMon[i]) continue;
            const int x = static_cast<int>(i) % w, y = static_cast<int>(i) / w;
            int box = heldFromBox, slot = heldFromSlot + x + y * cols;
            while (slot >= slots) { slot -= slots; ++box; }        // defensive: a grab is bounds-checked to one box
            if (place(box, slot, moveMon[i])) continue;
            bool placed = false;
            for (int b = 0; b < boxCount && !placed; ++b)
                for (int s = 0; s < slots && !placed; ++s)
                    placed = place(b, s, moveMon[i]);
        }
        moveMon.clear();
        selectDimensions = {0, 0};
    }

    // Reference to a storage slot's unique_ptr (pane 0 = save boxes, 1 = bank). Callers must
    // ensure `bank` exists for pane 1.
    std::unique_ptr<Pokemon::Pokemon>& TrainerViewScreen::storageSlot(int pane, int box, int slot) {
        return pane == 0 ? trainer.boxes[box][slot] : bank->boxes[box][slot];
    }

    // A save-pane slot that is a party member (LGPE) is locked: removing/displacing it would
    // orphan the party pointer, and LGPE keeps a separate party copy that wins on save. No-op for
    // SWSH/LZA (party is a separate structure -> getPartyPosition returns 0).
    bool TrainerViewScreen::storageSlotLocked(int pane, int box, int slot) {
        return pane == 0 && trainer.getPartyPosition(box, slot) > 0;
    }

    // The tail of the + handler: prompt about unsaved GAME-save changes, else leave. Split out so the
    // bank's Save/Discard prompt can resume the exit once the user has answered it.
    void TrainerViewScreen::beginAppExit() {
        if (hasUnsavedChanges && !saveConfirmActive) {
            exitingWithUnsavedChanges = true;
            exitingViaPlus = true;   // remember we're exiting via the + button
            saveConfirmActive = true;
            return;
        }
        exitRequested = true;
    }

    // Convert `pk` in place into the format needed to live in `destPane`. The bank (pane 1) accepts
    // anything as-is; a save slot (pane 0) requires the open game's format, so a foreign mon is CONVERTED
    // in place if a supported route exists (M5 Phase B). Returns false + posts a status when it can't go
    // there (out-of-dex / unsupported gen pair). Shared by the single-mon and bulk placement paths.
    bool TrainerViewScreen::convertForPane(std::unique_ptr<Pokemon::Pokemon>& pk, int destPane) {
        if (destPane != 0 || !pk) return true;                          // bank: store as-is
        if (pk->getGameGroup() == trainer.getGameGroup()) {
            // Same game, so no conversion -- but repair an invalid AffixedRibbon and re-checksum
            // before it enters the save. The re-checksum SHOULD be a no-op writing back the identical
            // value, since the bank stores native bytes untouched; that is exactly why it is cheap
            // insurance, because a stale checksum reaching the game's box writer is a Bad Egg in-game.
            //
            // The ribbon repair is NOT a no-op for legacy stock: mons banked before the creator learned
            // to set AffixedRibbon carry 0, which the game reads as "display ribbon index 0" -- the
            // Kalos Champion ribbon. Cross-gen withdrawals get this inside convert(); a same-group one
            // never calls convert() at all, so without this the 0 rides straight back into the save.
            // Repairing here rather than in the bank keeps the bank's never-mutate rule intact
            // (see Appendix C) -- the fix lands on the way INTO a save, where it belongs.
            Conversion::normalizeAffixedRibbon(*pk);
            pk->refreshChecksum();
            return true;
        }
        Conversion::Result res;
        const std::string species = pk->species();
        // The exact destination game, not just its group: a mon transferred down into Gen 3 cannot keep a
        // modern origin (4-bit field) and gets restamped, and "FRLG" alone can't say which half it is.
        auto converted = Conversion::convert(*pk, trainer.getGameGroup(), res,
                                             saveOriginVersion(trainer, titleId));
        if (converted) {
            // Cross-gen conversion is where the subtle transfer bugs have historically lived
            // (fainted arrivals, deleted moves, garbage levels), so every one gets a line.
            Utils::logTest("CONVERT  species=\"" + species + "\" -> " +
                           std::to_string(static_cast<int>(trainer.getGameGroup())) +
                           " lvl=" + std::to_string(converted->level()) +
                           " hp=" + std::to_string(converted->statHPMax()) + " result=OK");
            pk = std::move(converted); return true;      // convert in place
        }
        Utils::logTest("CONVERT  species=\"" + species + "\" result=REFUSED msg=\"" +
                       Conversion::resultMessage(res) + "\"");
        storageStatus = Conversion::resultMessage(res);
        storageStatusFrames = 150;   // ~2.5s at 60fps
        return false;
    }

    // True if placing `pk` into `pane` would run a Let's Go conversion (exactly one side is LGPE), which
    // resets AVs/EVs -> the user is asked to acknowledge it. Only save-pane (0) placements convert; the
    // bank (1) stores native bytes, so a deposit never resets anything.
    bool TrainerViewScreen::lgpeConversionInvolved(int pane, const Pokemon::Pokemon* pk) const {
        if (pane != 0 || !pk) return false;
        const bool srcGG = pk->getGameGroup() == Enums::GameVersion::GG;
        const bool dstGG = trainer.getGameGroup() == Enums::GameVersion::GG;
        return srcGG != dstGG;
    }

    // True if any mon in the carried block would run an LGPE conversion when dropped into destPane.
    bool TrainerViewScreen::blockInvolvesLgpe(int destPane) const {
        if (destPane != 0) return false;
        for (const auto& p : moveMon)
            if (p && lgpeConversionInvolved(destPane, p.get())) return true;
        return false;
    }

    // True if placing `pk` into `pane` converts a non-Gen3 mon DOWN into Gen 3 (FR/LG). That path rebuilds
    // the PID to preserve the nature (Gen 3 derives nature FROM the PID), which is destructive and can read
    // as illegal -- so it is always confirmed, independent of the LGPE-warn setting. Only save-pane (0)
    // placements convert; the bank stores native bytes.
    bool TrainerViewScreen::gen3DowngradeInvolved(int pane, const Pokemon::Pokemon* pk) const {
        if (pane != 0 || !pk) return false;
        return trainer.getGameGroup() == Enums::GameVersion::FRLG
            && pk->getGameGroup() != Enums::GameVersion::FRLG;
    }

    // True if any mon in the carried block would run a Gen 3 downgrade when dropped into destPane.
    bool TrainerViewScreen::blockInvolvesGen3Downgrade(int destPane) const {
        if (destPane != 0 || trainer.getGameGroup() != Enums::GameVersion::FRLG) return false;
        for (const auto& p : moveMon)
            if (p && p->getGameGroup() != Enums::GameVersion::FRLG) return true;
        return false;
    }

    // build the Ability picker's option list. A species holds one of its ABILITY SLOTS (slot 1,
    // slot 2, hidden), and most species have slot 2 == slot 1, so the deduped list is usually one
    // or two entries -- those are the only legal picks and they render green at the top.
    // "Allow illegal values" appends every remaining ability id after them, EXCEPT on Gen 3: a PK3
    // stores a selector bit rather than an ability id, so nothing outside the two slots is
    // expressible there and offering more would just be a no-op the user can't see.
    // The mon's current ability is always listed even when it is not legal, so an existing bad
    // value stays visible and reversible instead of vanishing from its own picker.
    void TrainerViewScreen::buildAbilityPickerOrder(uint16_t species, uint8_t form,
                                                   Enums::GameVersion group, uint16_t current) {
        pickerOrder.clear();
        const Pokemon::AbilitySlots slots = Pokemon::getAbilitySlots(species, form, group);
        uint16_t legal[3];
        const int nLegal = slots.distinct(legal);
        for (int i = 0; i < nLegal; ++i) pickerOrder.push_back(legal[i]);
        pickerLegalCount = static_cast<int>(pickerOrder.size());

        const bool gen3 = (group == Enums::GameVersion::FRLG);
        if (g_allowIllegalEdits && !gen3) {
            const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Ability);
            for (int a = 0; a < total; ++a) {
                bool isLegal = false;
                for (int i = 0; i < pickerLegalCount; ++i) if (pickerOrder[i] == a) { isLegal = true; break; }
                if (!isLegal) pickerOrder.push_back(a);
            }
        } else {
            bool has = false;
            for (int x : pickerOrder) if (x == static_cast<int>(current)) { has = true; break; }
            if (!has) pickerOrder.push_back(current);   // keep an already-illegal value selectable
        }

        pickerSel = 0;
        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
            if (pickerOrder[i] == static_cast<int>(current)) { pickerSel = i; break; }
    }

    // Creator: fill the species picker with only the species obtainable in the open game (via the
    // personal presence bitmask). Reuses pickerOrder (row -> species id); pickerLegalCount stays 0
    // (no green highlight for species).
    //
    // This is a HARD rule -- "Allow illegal edits" does not offer the rest of the dex. A game that has
    // no entry for a species has no stats, no learnset and no name for it, so what would be created is
    // not an unusual mon but a broken one; that toggle is for values a game can hold but shouldn't.
    void TrainerViewScreen::buildCreatorSpeciesOrder() {
        pickerOrder.clear();
        pickerLegalCount = 0;
        const uint8_t bit = personalPresenceBit(trainer.getGameGroup());
        const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Species);
        // FireRed/LeafGreen (Gen 3) isn't in the presence bitmask, so its bit is 0 -- filtering by it
        // would leave the Select Species list EMPTY. Offer the Gen 3 National Dex (1-386) instead, the
        // honest bound there. An unrecognised group (bit 0, not Gen 3) means "no data" for the same
        // reason and likewise must not filter down to nothing.
        const bool isFRLG = (trainer.getGameGroup() == Enums::GameVersion::FRLG);
        // DLC species are deliberately NOT filtered out here, and there is no warning either.
        // Owning a DLC gates the AREAS, not the Pokemon: the patch ships the data to every copy,
        // so a player without the Expansion Pass can be traded a Crown Tundra species (or receive
        // one from HOME) and use it normally. PKHeX agrees -- its legality caps are the full-DLC
        // ones unconditionally. Filtering here would deny content that is perfectly valid.
        for (int s = 1; s < total; ++s) {  // skip 0 = None
            // ANY form present, not just form 0 -- see speciesPresentIn. Creating one of these picks
            // up the right form automatically: buildDefaultMon starts a mon on the first form the
            // game has, so a Braviary made in Legends: Arceus is Hisuian.
            const bool ok = isFRLG    ? (s <= 386)
                          : (bit == 0) ? true
                          : speciesPresentIn(static_cast<uint16_t>(s), bit);
            if (ok) pickerOrder.push_back(s);
        }
        pickerSel = 0;
    }

    // (move half): fill the move picker with the mon's LEARNABLE moves first (green + top), then
    // every other move. Learnability is the per-game single-stage pool from the learnset table.
    void TrainerViewScreen::buildMovePickerOrder(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t current) {
        pickerOrder.clear();
        const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Move);
        // Per GAME only -- DLC moves stay on offer for the same reason DLC species do; the save
        // dialog warns rather than the picker hiding them.
        const auto present = [&](int m) {
            return Names::isMovePresent(static_cast<uint16_t>(m), group);
        };
        std::vector<bool> legal(total, false);
        for (int m = 1; m < total; ++m)
            if (Pokemon::isLearnable(species, form, group, static_cast<uint16_t>(m)) && present(m)) {
                legal[m] = true; pickerOrder.push_back(m);
            }
        pickerLegalCount = static_cast<int>(pickerOrder.size());
        // Then the present-but-illegal moves (not green): still selectable, flagged only by the legality
        // check -- PKHeX-style. Moves that DON'T EXIST in this game (dummied / out of range, e.g. Pound in
        // Legends: Arceus, which the game turns into a Bad Egg) are dropped, matching PKHeX's editor.
        pickerOrder.push_back(0);   // None first in the non-legal section, to clear a slot
        for (int m = 1; m < total; ++m)
            if (!legal[m] && present(m)) pickerOrder.push_back(m);
        pickerSel = 0;
        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
            if (pickerOrder[i] == static_cast<int>(current)) { pickerSel = i; break; }
    }

    // Fill the form picker with the forms this game can actually hold (row -> form id) -- see
    // selectableForms above for the two filters and why each one is there.
    //
    // NEITHER filter is gated on "Allow illegal edits", for the same reason. That setting is for values
    // the games can genuinely hold but shouldn't -- a 255 EV, an off-dex species. A form is not one of
    // those: a temporary form gets overwritten on load, and a form the game never had is not a value
    // that game's form byte has any meaning for. Offering either would misrepresent the save rather
    // than permit an unusual one.
    //
    // pickerLegalCount stays 0 -- no green highlight, every offered row is equally valid.
    void TrainerViewScreen::buildFormPickerOrder(uint16_t species, uint8_t current, Enums::GameVersion group) {
        pickerOrder.clear();
        pickerLegalCount = 0;
        int count = Pokemon::getPersonalInfo(species, 0).formCount;
        if (count < 1) count = 1;

        const std::vector<int> offered = selectableForms(species, group);
        for (int f = 0; f < count; ++f) {
            bool offer = (f == static_cast<int>(current));   // see below
            for (int o : offered) if (o == f) { offer = true; break; }
            // The mon's CURRENT form is always listed even when temporary or absent from this game -- a
            // save can arrive holding one, and hiding it would make the form both invisible and
            // unfixable. This does not let such a form be applied to anything that isn't already in it.
            if (offer) pickerOrder.push_back(f);
        }
        // Guard a form id past the table's formCount (corrupt buffer): keep it selectable so the
        // row still renders and the pick is a no-op rather than an empty list.
        if (pickerOrder.empty()) pickerOrder.push_back(current);
        pickerSel = 0;
        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
            if (pickerOrder[i] == static_cast<int>(current)) { pickerSel = i; break; }
    }

    // Fill the gender picker with the genders this species can be (row -> gender value 0/1/2). For a
    // fixed-gender species that is a single row, which is the point: the row still opens and still says
    // what the mon is, there is simply nothing else to pick. See selectableGenders for the rule.
    void TrainerViewScreen::buildGenderPickerOrder(uint16_t species, uint8_t form, uint8_t current) {
        pickerOrder.clear();
        pickerLegalCount = 0;
        pickerOrder = selectableGenders(species, form);
        // The mon's CURRENT gender is always listed, the same rule the form picker uses: a save can
        // arrive holding an impossible one (an older PKSE wrote them, and other tools still do), and
        // hiding it would leave it both invisible and unfixable. Listing it is what makes it fixable.
        bool has = false;
        for (int g : pickerOrder) if (g == static_cast<int>(current)) { has = true; break; }
        if (!has && current <= 2) pickerOrder.push_back(current);
        pickerSel = 0;
        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
            if (pickerOrder[i] == static_cast<int>(current)) { pickerSel = i; break; }
    }

    // Whether the Gender row does anything. It is read-only when there is nothing to change it TO:
    // a male-only Braviary, a female-only Miltank, a genderless Magnemite. Offering a one-row picker
    // there was just a dead end, so the row now shows the gender and the cursor skips it.
    //
    // The one exception keeps it editable: a mon that already holds a gender its species cannot have.
    // Older PKSE builds wrote those and other tools still do, and the picker listing both the
    // impossible current value and the legal one is the only way to correct it -- locking the row
    // would make a female Braviary permanent.
    bool TrainerViewScreen::genderEditable(const Pokemon::Pokemon& p) const {
        const std::vector<int> g = selectableGenders(p.speciesID(), p.form());
        if (g.size() > 1) return true;                                   // a real choice
        return g.empty() || g[0] != static_cast<int>(p.gender());        // wrong gender -> fixable
    }

    // Resolve the Pokemon the details editor is currently targeting.
    Pokemon::Pokemon* TrainerViewScreen::detailsTargetPokemon() {
        switch (details.source) {
            case EditSource::Party:
                if (details.partyIndex >= 0 && details.partyIndex < static_cast<int>(trainer.party.size()))
                    return trainer.party[details.partyIndex].get();
                return nullptr;
            case EditSource::Bank:
                if (bank && details.bankBox >= 0 && details.bankBox < static_cast<int>(bank->boxes.size()) &&
                    details.bankSlot >= 0 && details.bankSlot < static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX))
                    return bank->boxes[details.bankBox][details.bankSlot].get();
                return nullptr;
            case EditSource::Box:
            default:
                if (selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                    selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS))
                    return trainer.boxes[selectedBoxIndex][selectedItemIndex].get();
                return nullptr;
        }
    }

    // After editing detailsTargetPokemon(), keep an LGPE party member's two representations (its box
    // slot + its independent party copy) in sync. Otherwise the save's party overlay clobbers a box
    // edit (or the box display goes stale after a party edit). No-op for gens without that duplication.
    void TrainerViewScreen::mirrorEditedPartyMember() {
        if (details.source == EditSource::Box) {
            trainer.mirrorPartyMemberFromBox(static_cast<size_t>(selectedBoxIndex), static_cast<size_t>(selectedItemIndex));
        } else if (details.source == EditSource::Party) {
            trainer.mirrorPartyMemberFromParty(static_cast<size_t>(details.partyIndex));
        }
        // EditSource::Bank has no party duplication.
    }

    // ---- Details-modal edit baseline / "unsaved changes" marker ------------------------------------
    // Modal edits mutate the live mon in place. We snapshot the target's decrypted bytes on open;
    // pokemonEditDirty() compares against it to drive the top-bar "Unsaved changes" marker. X (Save)
    // re-snapshots (commit -> marker clears). Closing the page WITHOUT Save calls restoreEditTarget(),
    // which rolls the mon back to the snapshot, so unsaved IV/EV/etc. edits are DISCARDED -- the
    // individual Save button is the only commit point.

    void TrainerViewScreen::snapshotEditTarget() {
        details.editSnapshot.clear();
        if (const Pokemon::Pokemon* t = detailsTargetPokemon()) {
            const auto d = t->getData();
            details.editSnapshot.assign(d.begin(), d.end());
        }
    }

    bool TrainerViewScreen::pokemonEditDirty() {
        if (details.editSnapshot.empty()) return false;
        const Pokemon::Pokemon* t = detailsTargetPokemon();
        if (!t) return false;
        const auto d = t->getData();
        return d.size() != details.editSnapshot.size()
            || !std::equal(d.begin(), d.end(), details.editSnapshot.begin());
    }

    // Roll the details target back to the snapshot: copy the baseline bytes over the live mon, then
    // re-mirror so an LGPE box/party twin reverts too. Because edits mutate the live buffer in place,
    // undoing them just means restoring the captured bytes (checksum + stats included -- the snapshot
    // is the whole serialized record). The snapshot is re-taken on X = Save, so this discards only the
    // edits made SINCE the last save. Called on close-without-Save; a no-op when nothing changed.
    void TrainerViewScreen::restoreEditTarget() {
        if (details.editSnapshot.empty()) return;
        Pokemon::Pokemon* t = detailsTargetPokemon();
        if (!t) return;
        auto d = t->getData();
        if (d.size() != details.editSnapshot.size()) return;   // fixed PK buffer -> always equal; guard anyway
        std::copy(details.editSnapshot.begin(), details.editSnapshot.end(), d.begin());
        mirrorEditedPartyMember();
    }

    void TrainerViewScreen::closeDetailsModal() {
        details.active = false;
        details.source = EditSource::Box;
        details.selectedField = 0;
        details.legalityOverlay = false;
        details.ribbonOverlay = false;
        details.discardConfirmActive = false;   // never let it survive to overlay the next page
        details.editSnapshot.clear();
    }

    // The bag keeps "owned but empty" slots (count 0 — e.g. used-up story key items) that we must
    // preserve on save (PKHeX keeps them positionally), but they shouldn't clutter the UI. This
    // returns the raw indices of the current pouch's items worth showing/editing (count > 0), and
    // is the single source of truth so the panel, navigation, and edit all stay in lockstep.
    // Returns the id of the touch button under a fresh tap this frame, or -1 if none. Buttons are
    // captured during the previous frame's draw (only the active overlay populates touchButtons).
    int TrainerViewScreen::touchedButtonId(const TouchInput& touch) const {
        if (!touch.justPressed()) return -1;
        for (const auto& b : touchButtons) {
            if (touch.x() >= b.x && touch.x() < b.x + b.w &&
                touch.y() >= b.y && touch.y() < b.y + b.h)
                return b.id;
        }
        return -1;
    }

    // Rename the focused box via the Switch keyboard.
    //
    // Safe to call from update(): the UI loop is update() -> draw() -> flush(), so no NanoVG frame
    // is open when swkbd suspends the app. Calling this from draw() would strand a half-built frame.
    void TrainerViewScreen::renameBox(int boxIndex) {
        const size_t maxChars = trainer.getMaxBoxNameLength();
        if (maxChars == 0) return;
        if (boxIndex < 0 || boxIndex >= static_cast<int>(trainer.boxNames.size())) return;

        const std::string current = trainer.boxNames[boxIndex];
        const Utils::KeyboardResult res =
            Utils::promptText("Rename Box", "Box name", current, static_cast<int>(maxChars));
        if (!res.accepted) return;          // cancel means "leave it alone", NOT "clear it"
        if (res.text == current) return;

        // Refuse rather than mangle: Gen 3's character set is fixed and predates Unicode, so a name
        // the keyboard was happy to produce may be unwritable there.
        if (!trainer.canStoreBoxName(res.text)) {
            Utils::logTest("BOXNAME  box=" + std::to_string(boxIndex + 1) +
                           " new=\"" + res.text + "\" result=REFUSED_CHARSET");
            storageStatus = "This game can't store one of those characters.";
            storageStatusFrames = 240;
            return;
        }

        // An empty name is legitimate: the games treat a blank as "use the default", and
        // parseBoxNames already turns a blank back into "Box N" on reload.
        Utils::logTest("BOXNAME  box=" + std::to_string(boxIndex + 1) +
                       " old=\"" + current + "\" new=\"" + res.text + "\" result=OK");
        trainer.boxNames[boxIndex] = res.text;
        // Only a box marked here is written on save. boxNames also holds display defaults for boxes
        // the save leaves unnamed, and persisting those would invent names the player never set.
        trainer.markBoxNameDirty(static_cast<size_t>(boxIndex));
        hasUnsavedChanges = true;
        storageStatus = res.text.empty() ? "Box name cleared." : ("Box renamed to \"" + res.text + "\".");
        storageStatusFrames = 180;
    }

    // Rename a BANK box. Unlike save boxes, bank names are PKSE-internal (stored UTF-8 in bank.dat,
    // no game charset limit), and the default label is "Bank N" rather than a stored string -- so an
    // empty name means "use the default", and the keyboard starts from the raw stored name (blank by
    // default) rather than from the "Bank N" label.
    void TrainerViewScreen::renameBankBox(int box) {
        if (!bank || box < 0 || box >= static_cast<int>(Trainer::Bank::BANK_BOX_COUNT)) return;
        const std::string current = bank->boxNames[box];
        const Utils::KeyboardResult res =
            Utils::promptText("Rename Bank Box", "Bank box name", current,
                              static_cast<int>(Trainer::Bank::MAX_BOX_NAME_LEN));
        if (!res.accepted) return;              // cancel = leave it alone
        if (res.text == current) return;

        bank->boxNames[box] = res.text;
        // A bank rename is an unsaved BANK change: it feeds serialize()/hasChanged(), so leaving the
        // storage view now raises the Save/Discard prompt just like a deposit does. (Not tied to the
        // trainer's hasUnsavedChanges, which is the save file's dirty flag.)
        Utils::logTest("BANKBOXNAME  box=" + std::to_string(box + 1) +
                       " new=\"" + res.text + "\" result=OK");
        storageStatus = res.text.empty() ? "Bank box name reset to default."
                                         : ("Bank box renamed to \"" + res.text + "\".");
        storageStatusFrames = 180;
    }

    // Re-stamp the trainer identity your Pokemon store after a name edit, so they stay
    // recognized as yours (see the header). Two independent matches per mon:
    //   (1) OT  -- you caught it: match OT ID32 + the carried OT name.
    //   (2) HT  -- it was traded to you (Gen 7+): match the carried HT name (HT has no TID/SID).
    // Genuinely foreign stamps (someone else's OT/HT) are left untouched. Walks party + boxes only --
    // the cross-game bank is deliberately excluded. Returns the count of mons actually changed.
    int TrainerViewScreen::restampCaughtPokemonIdentity(const std::u16string& caughtName) {
        const std::u16string newName = Utils::utf8ToUtf16(trainer.trainerName);
        const uint8_t newGender = trainer.trainerGender;
        const uint32_t id32 = trainer.ID32;
        int changed = 0;
        auto restamp = [&](Pokemon::Pokemon* pk) {
            if (!pk || pk->speciesID() == 0) return;
            bool did = false;
            // (1) You are the ORIGINAL TRAINER.
            if (pk->id32() == id32 && pk->otName() == caughtName) {
                if (pk->otName()   != newName)   { pk->setOTName(newName);     did = true; }
                if (pk->otGender() != newGender) { pk->setOTGender(newGender); did = true; }
            }
            // (2) You are the HANDLING TRAINER of a traded-in mon. FRLG has no HT (htName() is empty),
            // so it never matches; the empty guard also stops untraded mons (empty HT) matching a
            // cleared trainer name.
            if (!caughtName.empty() && pk->htName() == caughtName) {
                if (pk->htName()   != newName)   { pk->setHTName(newName);     did = true; }
                if (pk->htGender() != newGender) { pk->setHTGender(newGender); did = true; }
            }
            // Trainer fields don't affect stats, so refresh the checksum only (no recalculateStats).
            if (did) { pk->refreshChecksum(); ++changed; }
        };
        for (auto& pk : trainer.party) restamp(pk.get());
        for (auto& box : trainer.boxes)
            for (auto& pk : box) restamp(pk.get());
        return changed;
    }

    // Append " Updated OT on N of your Pokemon." to a status line when a re-stamp touched any.
    static std::string withOtRestampNote(std::string msg, int n) {
        if (n > 0) msg += " Updated OT on " + std::to_string(n) + " of your Pokemon.";
        return msg;
    }

    // Edit the trainer's OT name via the Switch keyboard. Mirrors renameBox: a cancel leaves the
    // name untouched, an unchanged result is a no-op, and a name the game's glyph table can't store is
    // refused rather than silently mangled (Gen 3 has ~70 glyphs; canStoreBoxName is the shared check).
    // Mirrors PKHeX TrainerNameVerifier.ContainsTooManyNumbers. The games cap how many digits a
    // trainer name may hold, separately from its length, and a name no longer than the cap is
    // exempt -- so a five-digit cap accepts "12345" but not "123456". Counts characters rather than
    // bytes (the name is UTF-8 here) and treats full-width digits as digits, matching .NET's
    // char.IsNumber, since a Japanese keyboard produces those.
    static bool nameHasTooManyDigits(const std::u16string& name, int maxDigits) {
        if (maxDigits < 0) return false;                                  // generation with no cap
        if (name.size() <= static_cast<size_t>(maxDigits)) return false;  // short enough to be exempt
        int digits = 0;
        for (char16_t c : name) {
            if ((c >= u'0' && c <= u'9') || (c >= 0xFF10 && c <= 0xFF19))
                ++digits;
        }
        return digits > maxDigits;
    }

    void TrainerViewScreen::editTrainerName() {
        const std::string current = trainer.trainerName;
        const Utils::KeyboardResult res =
            Utils::promptText("Trainer Name", "OT name", current,
                              static_cast<int>(trainer.getMaxTrainerNameLength()));
        if (!res.accepted) return;          // cancel means "leave it alone", not "clear it"
        if (res.text == current) return;
        if (!trainer.canStoreBoxName(res.text)) {
            Utils::logTest("TRAINERNAME new=\"" + res.text + "\" result=REFUSED_CHARSET");
            postStatus("This game can't store those characters (A-Z, 0-9, and ! ? . - only).", 240);
            return;
        }
        const int maxDigits = trainer.getMaxTrainerNameDigits();
        if (nameHasTooManyDigits(Utils::utf8ToUtf16(res.text), maxDigits)) {
            Utils::logTest("TRAINERNAME new=\"" + res.text + "\" result=REFUSED_DIGITS");
            postStatus("This game allows at most " + std::to_string(maxDigits) +
                       " numbers in a trainer name.", 240);
            return;
        }
        Utils::logTest("TRAINERNAME old=\"" + current + "\" new=\"" + res.text + "\" result=OK");
        // Match owned mons by the name they currently carry (the pre-rename name) BEFORE we change it.
        const std::u16string caughtName = Utils::utf8ToUtf16(current);
        trainer.trainerName = res.text;
        const int n = restampCaughtPokemonIdentity(caughtName);
        hasUnsavedChanges = true;
        Utils::logTest("TRAINERNAME restamped=" + std::to_string(n));
        postStatus(withOtRestampNote(res.text.empty() ? "Trainer name cleared." : "Trainer name updated.", n), 200);
    }

    // Edit the trainer's money via the number pad, clamped to this game's cap. promptNumber both
    // widths the keypad to the max and clamps the returned value, so an out-of-range entry can't slip in.
    void TrainerViewScreen::editTrainerMoney() {
        const Utils::NumberResult res =
            Utils::promptNumber("Money", static_cast<int>(trainer.money), 0,
                                static_cast<int>(trainer.getMaxMoney()));
        if (!res.accepted) return;
        const uint32_t newMoney = static_cast<uint32_t>(res.value);
        if (newMoney == trainer.money) return;
        Utils::logTest("TRAINERMONEY old=" + std::to_string(trainer.money) +
                       " new=" + std::to_string(newMoney) + " result=OK");
        trainer.money = newMoney;
        hasUnsavedChanges = true;
        postStatus("Money set to $" + std::to_string(newMoney) + ".", 150);
    }

    // NOTE: there was once a scanForDlcContent() here, warning at save time when a save held
    // content above its base game's id ceiling. It was removed because its premise was wrong.
    // Owning a DLC gates the AREAS, not the Pokemon: a player without the Expansion Pass can be
    // traded a Crown Tundra species, or receive one from HOME, and use it normally -- the patch
    // ships the data to every copy of the game. So there is nothing to warn about, and the warning
    // said something false ("it will not appear in-game"). Do not reintroduce it.

    // A backup's leaf folder name IS its name everywhere in the UI, so this is
    // what the user recognises when we report where a save went.
    static std::string leafName(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    void TrainerViewScreen::performSave(const std::string& destDir, bool injectToTitle) {
        // The game's "current box" is kept in sync live while a box view is open (see update()), so
        // it already reflects wherever the user last was -- including a box change made in Storage
        // before backing out (Storage's own X sorts; this game save happens later). Nothing to do here.
        const bool ok = Save::saveTrainerInfo(trainer, destDir.c_str(), titleId, userUid, injectToTitle);
        saveConfirmActive = false;

        // The single most important trace line: what was written, where, and whether it worked.
        const std::string saveInfo =
            std::string("dest=") + (injectToTitle ? "GAME" : "BACKUP") +
            " folder=\"" + leafName(destDir) + "\"" +
            " inject=" + (injectToTitle ? "1" : "0") +
            " illegaldata=" + (illegalDataWritten ? "1" : "0") +
            " result=" + (ok ? "OK" : "FAILED");
        Utils::logTest("SAVE     " + saveInfo);
        Utils::logEventToFile("SAVE " + saveInfo + " party=" + std::to_string(trainer.getPartySize()));

        if (ok) {
            hasUnsavedChanges = false;
            statEdit.dialogActive = false;
            details.active = false;
            details.editing = false;
            creator.editing = false; creator.keepConfirmActive = false;   // committed by the save
            // Name the destination back to the user: with three of them, "saved" alone is ambiguous
            // and the whole point of the picker is knowing WHERE it went.
            postStatus(injectToTitle ? "Written to the game save."
                                     : ("Written to backup \"" + leafName(destDir) + "\"."), 200);
            // A new backup becomes the session's working copy, so a second save goes to the same
            // place instead of silently forking another folder off the original.
            backupDir = destDir;
        } else {
            // A failed save used to be INDISTINGUISHABLE from a successful one.
            postStatus("SAVE FAILED - your changes are still unsaved. Nothing was written.", 480);
            hasUnsavedChanges = true;
        }
    }

    // Create a new named backup folder, seeded with a copy of the one currently open.
    //
    // The copy matters: several games' save paths RE-READ the destination's existing save file to
    // recover the blocks they don't touch (Let's Go most obviously), so writing into an empty
    // directory would produce a truncated save. "New backup" therefore means "a copy of this save,
    // plus my edits" -- which is also what the user means by it.
    std::string TrainerViewScreen::createNamedBackupDir(const std::string& name) {
        // Never let a typed name reach the filesystem unfiltered: a '/' or ".." would escape the
        // PKSE directory entirely. Keep it to characters that are safe on FAT32 as well.
        std::string leaf;
        for (const char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_')
                leaf += c;
        }
        while (!leaf.empty() && leaf.back() == ' ') leaf.pop_back();     // FAT32 dislikes trailing spaces
        while (!leaf.empty() && leaf.front() == ' ') leaf.erase(leaf.begin());
        if (leaf.empty()) return "";

        const std::string gameDir = BASE_SAVE_DIRECTORY + "/" + titleName;
        auto exists = [](const std::string& p) { struct stat st{}; return stat(p.c_str(), &st) == 0; };

        // Suffix rather than overwrite -- silently replacing a backup the user made earlier would be
        // the worst possible reading of "create a new save".
        std::string unique = leaf;
        for (int n = 2; n < 1000 && exists(gameDir + "/" + unique); ++n)
            unique = leaf + "-" + std::to_string(n);

        const std::string destDir = gameDir + "/" + unique;
        if (mkdir(destDir.c_str(), 0777) != 0 && errno != EEXIST) {
            Utils::logErrorToFile("Failed to create named backup directory", destDir.c_str());
            return "";
        }
        if (!Utils::copyDirectory(backupDir.c_str(), destDir.c_str())) {
            Utils::logErrorToFile("Failed to seed named backup from", backupDir.c_str());
            return "";
        }
        return destDir;
    }

    std::vector<int> TrainerViewScreen::visibleItemIndices() const {
        std::vector<int> vis;
        if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
            const auto& pouch = trainer.items[selectedCategory];
            vis.reserve(pouch.size());
            for (int i = 0; i < static_cast<int>(pouch.size()); ++i) {
                if (pouch[i].count > 0) vis.push_back(i);
            }
        }
        return vis;
    }

    // How many item slots the current pouch can hold, for the add-item flow.
    //
    // Two storage models. The id-indexed games (BDSP / S-V / Z-A) build their pouch vector by
    // walking every legal id, so an entry already exists for anything addable and nothing is ever
    // appended -- they report a bound large enough to never block. The slot-based games store only
    // what the bag holds, so a new item really does append and must respect the pouch's capacity.
    // Legends: Arceus reports 0 (appending unsupported): its PouchInfo8LA carries no capacity and
    // its item block is resized on write, so appending without a documented bound could overflow it.
    int TrainerViewScreen::currentPouchCapacity() const {
        using namespace Trainer;
        const int c = selectedCategory;
        if (c < 0) return 0;
        switch (trainer.getGameGroup()) {
            case Enums::GameVersion::FRLG:
                return (c < static_cast<int>(POUCH_COUNT3_FRLG))
                     ? getPouchInfo3FRLG(static_cast<PouchType3FRLG>(c)).maxSlots : 0;
            case Enums::GameVersion::GG:
                return (c < static_cast<int>(PouchType7LGPE::Count))
                     ? getPouchInfo7LGPE(static_cast<PouchType7LGPE>(c)).maxSlots : 0;
            case Enums::GameVersion::SWSH:
                return (c < static_cast<int>(PouchType8SWSH::Count))
                     ? getPouchInfo8SWSH(static_cast<PouchType8SWSH>(c)).maxCount : 0;
            case Enums::GameVersion::PLA:
                // Fixed-capacity packed pouches; the general bag grows with Satchel Upgrades.
                return static_cast<int>(trainer.getItemPouchCapacity(c));
            default:
                return 1 << 20;   // id-indexed: the entry is already there, so this never binds
        }
    }

    // Largest stack the current pouch allows for one item.
    //
    // This is a SAVE-SAFETY limit, not cosmetics. Writing a count the game never produces can
    // overflow the pouch and corrupt whatever follows it -- in Gen 3 that means spilling into the
    // KEY ITEMS pocket, which the user has hit for real. The editor previously offered 999 for
    // every game and every pouch, which is wrong in both directions.
    //
    // Values are PKHeX's per-pouch InventoryPouch maxima (PlayerBag*.cs / ItemStorage*.GetMax):
    //   * KEY ITEMS are 1 in EVERY game -- they are possession flags, not stacks.
    //   * Let's Go caps TMs at 1, and Z-A caps TMs AND Mega Stones at 1.
    //
    // Gen 3 uses PKHeX's 999. This was investigated properly and is SETTLED -- don't re-tighten it:
    //   * A stricter 99 was first considered on a report of an oversized stack overflowing a bag
    //     into the key-items pocket. That overflow is real, but it is **Gen 2 (Crystal)**, whose
    //     bag caps stacks at 99 and spills into fresh slots -- not Gen 3's model.
    //   * PKHeX applies no Gen 3 special-casing anywhere: PlayerBag3FRLG declares 999 like every
    //     other game, and InventoryPouch3 adds no count logic at all.
    //   * Other on-hardware Gen 3 editors permit the full u16 (65535), confirming Gen 3 does not
    //     enforce a low cap. We stay at PKHeX's 999 anyway: it is the legality-aware bound, and
    //     nothing is gained by allowing counts no game will ever show.
    int TrainerViewScreen::currentItemMaxCount() const {
        using GV = Enums::GameVersion;
        const int c = selectedCategory;
        switch (trainer.getGameGroup()) {
            case GV::FRLG: return (c == static_cast<int>(Trainer::PouchType3FRLG::KeyItems)) ? 1 : 999;
            // Let's Go caps TMs at 1. Its "KeyItems" pouch is NOT key-items-only -- it is PKHeX's
            // Items pouch with regular and key items MIXED (max 999), so it must not be capped at 1.
            case GV::GG:   return (c == static_cast<int>(Trainer::PouchType7LGPE::TMs)) ? 1 : 999;
            case GV::SWSH: return (c == static_cast<int>(Trainer::PouchType8SWSH::KeyItems)) ? 1 : 999;
            // PLA caps Key Items AND Recipes at 1 (possession flags), like PKHeX PlayerBag8a.
            case GV::PLA:  return (c == static_cast<int>(Trainer::PouchType8LA::KeyItems)
                                || c == static_cast<int>(Trainer::PouchType8LA::Recipes)) ? 1 : 999;
            case GV::BDSP: return (c == static_cast<int>(Trainer::PouchType8BDSP::KeyItems)) ? 1 : 999;
            case GV::SV:   return (c == static_cast<int>(Trainer::PouchType9SV::KeyItems)) ? 1 : 999;
            case GV::ZA:   return (c == static_cast<int>(Trainer::PouchType9LZA::KeyItems)
                                || c == static_cast<int>(Trainer::PouchType9LZA::TMs)
                                || c == static_cast<int>(Trainer::PouchType9LZA::MegaStones)) ? 1 : 999;
            default:       return 999;
        }
    }

    // Open the details modal on a storage slot (reuses the Box path for the save pane).
    void TrainerViewScreen::openStorageEditor(int pane, int box, int slot) {
        if (pane == 1) {
            details.source = EditSource::Bank;
            details.bankBox = box;
            details.bankSlot = slot;
        } else {
            details.source = EditSource::Box;
            selectedBoxIndex = box;
            selectedItemIndex = slot;
        }
        details.active = true;
        details.leftScroll = 0;   // start the info column at the top
        details.editing = false;
        details.category = 0;
        details.selectedStat = 0;
        details.selectedField = 0;
        details.hexMode = 0;
        creator.editing = false;  // default; the creator flow sets it true right after this call
        snapshotEditTarget();    // dirty-check baseline (unused for the creator: it has Keep/Discard)
    }

    // ---------------------------------------------------------------------------------------------
    // HOME-style rectangle select + block carry.
    //
    // The model, in full: A anchors a corner, moving the cursor rubber-bands a rectangle, A again
    // lifts every slot inside it into `moveMon` (row-major, holes kept as nulls) and snaps the cursor
    // to the rectangle's top-left. From then on the block rides the cursor -- across boxes and across
    // panes -- and A drops it so that cell (x, y) lands in slot `cursor + x + y*cols`. Landing is
    // POSITIONAL, not "fill the next free slot": the arrangement you picked up is the arrangement you
    // put down, and whatever occupied those slots comes back up into your hands (a block swap).
    //
    // Let's Go is the documented exception. Its storage is one gapless 1000-slot list, so a hole is
    // not representable; the block still lands at the exact cursor slot, then compactStorage() closes
    // any gap on the next frame, which slides the mons forward. That is the game's rule, not ours.
    // ---------------------------------------------------------------------------------------------

    // Column count of a pane's grid. LGPE's save boxes are 5x5 (25 slots); everything else is 6x5.
    int TrainerViewScreen::paneCols(int pane) const {
        if (pane != 0) return 6;
        return static_cast<int>(trainer.getSlotsPerBox()) == 25 ? 5 : 6;
    }

    int TrainerViewScreen::paneSlots(int pane) const {
        return pane == 0 ? static_cast<int>(trainer.getSlotsPerBox())
                         : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
    }

    int TrainerViewScreen::paneBoxes(int pane) const {
        return pane == 0 ? static_cast<int>(trainer.getBoxCount())
                         : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
    }

    void TrainerViewScreen::cancelSelection() {
        currentlySelecting = false;
        if (!carrying()) selectDimensions = {0, 0};
    }

    // Move mode: lift the slot under the cursor as a 1x1 block. Single and multi carry share one
    // representation, so every later step (drawing, bounds, put-down, B-to-return) is written once.
    void TrainerViewScreen::pickupSingle() {
        if (!bank) return;
        const int pane = storageFocusPane;
        const int box = (pane == 0) ? stSaveBox : stBankBox;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= paneSlots(pane)) return;
        if (storageSlotLocked(pane, box, slot)) return;      // LGPE party member: its slot is pinned
        auto& src = storageSlot(pane, box, slot);
        if (!src || src->speciesID() == 0) return;           // species-0 = empty (S/V ghost)

        moveMon.clear();
        moveMon.push_back(std::move(src));
        selectDimensions = {1, 1};
        heldPane = pane; heldFromBox = box; heldFromSlot = slot;
    }

    // Multi mode: the first A anchors the rectangle at the cursor, the second grabs it.
    void TrainerViewScreen::pickupMulti() {
        const int pane = storageFocusPane;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= paneSlots(pane)) return;
        if (currentlySelecting) {
            grabSelection(true);
            return;
        }
        const int cols = paneCols(pane);
        selectDimensions   = {slot % cols, slot / cols};     // anchor cell, NOT dimensions yet
        selectPane         = pane;
        selectBox          = (pane == 0) ? stSaveBox : stBankBox;
        currentlySelecting = true;
    }

    // Lift (remove = true) or copy (remove = false) every slot inside the anchored rectangle into
    // moveMon, then snap the cursor to the rectangle's top-left so the block sits under the pointer.
    void TrainerViewScreen::grabSelection(bool remove) {
        if (!currentlySelecting || !bank) return;
        const int pane = selectPane;
        const int box = selectBox;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= slots) { cancelSelection(); return; }

        const int curX = slot % cols, curY = slot / cols;
        const int ax = selectDimensions.first, ay = selectDimensions.second;
        const int baseX = std::min(ax, curX), baseY = std::min(ay, curY);
        const int w = std::abs(ax - curX) + 1, h = std::abs(ay - curY) + 1;

        moveMon.clear();
        moveMon.reserve(static_cast<size_t>(w) * static_cast<size_t>(h));
        int lockedLeft = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int s = (baseY + y) * cols + (baseX + x);
                if (s >= slots) { moveMon.push_back(nullptr); continue; }
                // A party-linked slot stays put: Let's Go's party points at box slots by INDEX, so
                // carrying one away would leave that pointer aimed at whatever slid in behind it.
                // It becomes a hole in the block rather than blocking the whole grab.
                if (storageSlotLocked(pane, box, s)) { moveMon.push_back(nullptr); ++lockedLeft; continue; }
                auto& src = storageSlot(pane, box, s);
                if (!src || src->speciesID() == 0) { moveMon.push_back(nullptr); continue; }
                if (remove) {
                    moveMon.push_back(std::move(src));
                } else {
                    moveMon.push_back(src->clone());        // duplicate: leave the original in place
                }
            }
        }

        selectDimensions   = {w, h};
        heldPane = pane; heldFromBox = box; heldFromSlot = baseY * cols + baseX;
        currentlySelecting = false;

        if (remove && carriedCount() > 0) hasUnsavedChanges = true;
        if (lockedLeft > 0)
            postStatus(std::to_string(lockedLeft) + " party-linked slot(s) stayed behind.", 180);

        // Trim first, THEN put the cursor on the block's top-left cell -- scrunching moves that
        // corner, and a cursor left on the untrimmed corner would draw the block offset from the
        // Pokemon that were just lifted. A rectangle that caught nothing leaves the cursor alone.
        postPickup();
        if (carrying()) {
            storageFocusPane = pane;
            if (pane == 0) { stSaveBox = box; stSaveSlot = heldFromSlot; }
            else           { stBankBox = box; stBankSlot = heldFromSlot; }
        }
    }

    // Drop a block whose every cell is null -- otherwise the hands stay "full" of nothing and the
    // next A would put empties down over real Pokemon.
    void TrainerViewScreen::postPickup() {
        if (currentlySelecting) return;
        if (carriedCount() == 0) {
            moveMon.clear();
            selectDimensions = {0, 0};
            return;
        }
        scrunchSelection();
    }

    // Trim fully-empty rows and columns off the block's edges. Selecting a loose
    // rectangle around three Pokemon should hand you a block sized to those three, not to the box
    // area you swept -- otherwise the bounds check refuses drops that would obviously have fitted.
    void TrainerViewScreen::scrunchSelection() {
        int w = selectDimensions.first, h = selectDimensions.second;
        if (w <= 1 && h <= 1) return;
        if (w <= 0 || h <= 0 || static_cast<int>(moveMon.size()) != w * h) return;

        auto colEmpty = [&](int c) {
            for (int r = 0; r < h; ++r) if (moveMon[r * w + c]) return false;
            return true;
        };
        auto rowEmpty = [&](int r) {
            for (int c = 0; c < w; ++c) if (moveMon[r * w + c]) return false;
            return true;
        };
        int first = 0, last = w - 1;
        while (first <= last && colEmpty(first)) ++first;
        while (last > first && colEmpty(last)) --last;
        int top = 0, bottom = h - 1;
        while (top <= bottom && rowEmpty(top)) ++top;
        while (bottom > top && rowEmpty(bottom)) --bottom;
        if (first > last || top > bottom) return;            // all empty; postPickup already handled it
        if (first == 0 && last == w - 1 && top == 0 && bottom == h - 1) return;

        std::vector<std::unique_ptr<Pokemon::Pokemon>> packed;
        packed.reserve(static_cast<size_t>(last - first + 1) * static_cast<size_t>(bottom - top + 1));
        for (int r = top; r <= bottom; ++r)
            for (int c = first; c <= last; ++c)
                packed.push_back(std::move(moveMon[r * w + c]));
        moveMon = std::move(packed);
        selectDimensions = {last - first + 1, bottom - top + 1};
        // The origin moves with the trim, so B still returns each cell to the slot it came from.
        const int cols = paneCols(heldPane);
        heldFromSlot += top * cols + first;
    }

    // Does the block fit in the focused pane starting at the cursor cell? A block may not hang off
    // the right edge or past the bottom row -- that is what makes "lands in the exact slot" a rule
    // rather than a hope.
    bool TrainerViewScreen::checkPutDownBounds() const {
        if (!carrying()) return false;
        const int pane = storageFocusPane;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0) return false;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        const int rows = (slots + cols - 1) / cols;
        const int c = slot % cols, r = slot / cols;
        return c + selectDimensions.first <= cols
            && r + selectDimensions.second <= rows
            && slot + (selectDimensions.first - 1) + (selectDimensions.second - 1) * cols < slots;
    }

    // Put the block down at the cursor: cell (x, y) -> slot + x + y*cols, exactly. Each cell SWAPS
    // with whatever is in its destination, so the displaced Pokemon come back up into your hands as
    // a block of the same shape (HOME's behaviour, and what makes a positional move reversible).
    //
    // Cells that cannot land -- a locked (party-linked) destination, or a cross-game mon with no
    // conversion route into this save -- stay in the block and stay untouched, so one blocker never
    // strands the rest of the group.
    void TrainerViewScreen::putDownBlock() {
        if (!carrying() || !bank) return;
        const int pane = storageFocusPane;
        const int box = (pane == 0) ? stSaveBox : stBankBox;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        if (!checkPutDownBounds()) {
            postStatus("That group doesn't fit here - move to a slot with room for it.", 180);
            return;
        }

        const int w = selectDimensions.first, h = selectDimensions.second;
        int placed = 0, lockedHit = 0, blocked = 0;
        bool converted = false;      // any cell crossed a game boundary -> noted on each MOVE event
        std::string firstName;
        const char* firstWhy = nullptr;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                const int dSlot = slot + x + y * cols;
                if (dSlot < 0 || dSlot >= slots) continue;
                // Never disturb a party-linked slot -- not even with an empty cell, which would
                // silently delete a party member out from under its pointer.
                if (storageSlotLocked(pane, box, dSlot)) {
                    if (moveMon[idx]) ++lockedHit;
                    continue;
                }
                if (moveMon[idx]) {
                    // Ask whether there is a route (for a species-named message), then honour what
                    // the conversion ACTUALLY returns. Writing an unconverted foreign Pokemon into a
                    // save is what produces a Bad Egg in game, so a failure here must not fall through.
                    const bool foreign = (pane == 0 && moveMon[idx]->getGameGroup() != trainer.getGameGroup());
                    Conversion::Result res{};
                    const bool routed = !foreign || Conversion::canConvert(*moveMon[idx], trainer.getGameGroup(), res);
                    if (!routed || !convertForPane(moveMon[idx], pane)) {
                        ++blocked;
                        if (!firstWhy) {
                            firstName = Names::getDisplayName(moveMon[idx]->speciesID(), moveMon[idx]->form(),
                                                              Trainer::getSpeciesName(moveMon[idx]->speciesID()));
                            firstWhy  = routed ? "conversion failed" : Conversion::resultMessage(res);
                        }
                        if (g_debugLogging) {
                            Utils::logEventToFile(
                                "MOVE result=BLOCKED " + Utils::logSlot("to", pane, box, dSlot)
                                + " " + Utils::logField("reason", routed ? "conversion failed"
                                                                         : Conversion::resultMessage(res))
                                + " " + Utils::describeMon(*moveMon[idx], trainer));
                        }
                        continue;                                   // leave it in hand, untouched
                    }
                    ++placed;
                    converted = converted || foreign;
                }
                auto& dst = storageSlot(pane, box, dSlot);
                std::swap(dst, moveMon[idx]);                       // block swap: the occupant comes up
                if (g_debugLogging && dst) {
                    std::string line = "MOVE result=OK " + Utils::logSlot("from", heldPane, heldFromBox, heldFromSlot)
                                     + " " + Utils::logSlot("to", pane, box, dSlot)
                                     + (converted ? " converted=Y" : "")
                                     + " " + Utils::describeMon(*dst, trainer);
                    if (moveMon[idx]) line += " displaced=[" + Utils::briefMon(*moveMon[idx]) + "]";
                    Utils::logEventToFile(line);
                }
                if (moveMon[idx] && moveMon[idx]->speciesID() == 0) moveMon[idx] = nullptr;  // ghost -> hole
            }
        }

        if (placed > 0) hasUnsavedChanges = true;
        // The block's origin is now this drop site, so B returns the swapped-out Pokemon here.
        heldPane = pane; heldFromBox = box; heldFromSlot = slot;
        postPickup();                       // may trim the swapped-out leftovers, moving heldFromSlot
        if (carrying()) {                   // keep the cursor on the leftovers' top-left corner
            if (pane == 0) stSaveSlot = heldFromSlot; else stBankSlot = heldFromSlot;
        }

        if (blocked > 0) {
            postStatus(firstName + ": " + (firstWhy ? firstWhy : "no transfer route")
                       + (blocked > 1 ? "  (+" + std::to_string(blocked - 1) + " more)" : "")
                       + " - still in hand", 240);
        } else if (lockedHit > 0) {
            postStatus(std::to_string(lockedHit) + " couldn't land on a party-linked slot - still in hand", 240);
        }
    }

    // The A press in the storage grid: put the block down if hands are full,
    // otherwise pick up according to the active mode.
    void TrainerViewScreen::storagePickup() {
        if (carrying()) {
            putDownBlock();
            return;
        }
        switch (cursorMode) {
            case CursorMode::Multi: pickupMulti(); break;
            case CursorMode::Move:  pickupSingle(); postPickup(); break;
            case CursorMode::Menu:
            default: break;   // handled by the caller (opens the per-Pokemon menu)
        }
    }

    // Sort one box: order its Pokemon by National Dex no., then form, then level (highest first),
    // and pack them to the front. Empty slots fall to the end.
    //
    // LOCKED SLOTS ARE PINNED and never participate. A save-side slot is locked when a party member
    // points at it (getPartyPosition > 0) -- Let's Go stores the party as INDICES into box storage, so
    // relocating such a slot would silently repoint a party member at a different Pokemon. The
    // occupant stays exactly where it is and the sort flows around it.
    void TrainerViewScreen::sortStorageBox(int pane, int box) {
        const int slots = (pane == 0) ? static_cast<int>(trainer.getSlotsPerBox())
                                      : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
        std::vector<int> freeIdx;                                   // slots the sort may write to
        std::vector<std::unique_ptr<Pokemon::Pokemon>> mons;
        for (int s = 0; s < slots; ++s) {
            if (storageSlotLocked(pane, box, s)) continue;          // party-linked: leave untouched
            freeIdx.push_back(s);
            auto& slot = storageSlot(pane, box, s);
            if (slot && slot->speciesID() != 0)                     // species-0 = empty (S/V ghost)
                mons.push_back(std::move(slot));
            slot.reset();
        }
        if (mons.empty()) {
            storageStatus = "Nothing to sort in this box";
            storageStatusFrames = 120;
            return;
        }
        std::sort(mons.begin(), mons.end(),
            [](const std::unique_ptr<Pokemon::Pokemon>& a, const std::unique_ptr<Pokemon::Pokemon>& b) {
                if (a->speciesID() != b->speciesID()) return a->speciesID() < b->speciesID();
                if (a->form() != b->form())           return a->form() < b->form();
                return a->level() > b->level();
            });
        for (size_t i = 0; i < mons.size() && i < freeIdx.size(); ++i)
            storageSlot(pane, box, freeIdx[i]) = std::move(mons[i]);

        hasUnsavedChanges = true;
        storageStatus = "Box sorted by Dex No. (" + std::to_string(mons.size()) + ")";
        storageStatusFrames = 120;
    }

    void TrainerViewScreen::handleStorageInput(u64 kDown) {
        if (!bank) return;

        // Accessors that always target the currently-focused pane.
        auto curBox  = [&]() -> int& { return storageFocusPane == 0 ? stSaveBox : stBankBox; };
        auto curSlot = [&]() -> int& { return storageFocusPane == 0 ? stSaveSlot : stBankSlot; };
        // The focused pane's box name is renamable when it's the bank (always) or a save that stores
        // box names (LGPE does not). Gates whether Up/Down can land on the box-name header.
        auto headerRenamable = [&]() { return storageFocusPane == 1 || trainer.supportsBoxNames(); };
        auto isLocked = [&](int pane, int box, int slot) { return storageSlotLocked(pane, box, slot); };
        auto occupied = [&](int pane, int box, int slot) {
            const auto& p = storageSlot(pane, box, slot);
            return p && p->speciesID() != 0;
        };

        const bool holding = carrying();

        // Y: cycle mode Menu -> Move -> Multi. Only with empty hands and no rectangle in progress,
        // so a mode switch can never orphan a carried block or a half-drawn selection.
        if ((kDown & HidNpadButton_Y) && !holding && !currentlySelecting) {
            cursorMode = cursorMode == CursorMode::Menu ? CursorMode::Move
                       : cursorMode == CursorMode::Move ? CursorMode::Multi : CursorMode::Menu;
        }

        // L/R: change the focused pane's box (wrap); keep the cursor slot in range. Changing box
        // abandons an in-progress rectangle -- it was anchored in the box you just left.
        if (kDown & (HidNpadButton_L | HidNpadButton_R)) {
            const int bc = paneBoxes(storageFocusPane);
            if (kDown & HidNpadButton_L) curBox() = (curBox() - 1 + bc) % bc;
            if (kDown & HidNpadButton_R) curBox() = (curBox() + 1) % bc;
            if (currentlySelecting) cancelSelection();
        }
        if (curSlot() >= paneSlots(storageFocusPane)) curSlot() = paneSlots(storageFocusPane) - 1;

        // D-pad: move within the focused pane; Left/Right cross panes at the horizontal edges.
        const int cols = paneCols(storageFocusPane);
        const int slots = paneSlots(storageFocusPane);
        const int rows = (slots + cols - 1) / cols;
        // curSlot() == -1 is the "box-name header focused" state (rename via A or a tap). Up from the
        // top row lands on it and Down leaves it -- but only where the header is renamable and nothing
        // is in hand or being selected, so LGPE (no box names) and mid-carry keep the plain wrap.
        const bool headerReachable = headerRenamable() && !holding && !currentlySelecting;
        if (kDown & HidNpadButton_Up) {
            if (curSlot() == -1) {
                curSlot() = (rows - 1) * cols;                        // header -> bottom row (wrap)
                if (curSlot() >= slots) curSlot() = slots - 1;
            } else {
                int r = curSlot() / cols, c = curSlot() % cols;
                if (r == 0 && headerReachable) {
                    curSlot() = -1;                                  // top row -> header
                } else {
                    r = (r - 1 + rows) % rows;
                    curSlot() = r * cols + c;
                    if (curSlot() >= slots) curSlot() = slots - 1;
                }
            }
        }
        if (kDown & HidNpadButton_Down) {
            if (curSlot() == -1) {
                curSlot() = 0;                                        // header -> top-left
            } else {
                int r = curSlot() / cols, c = curSlot() % cols;
                if (r == rows - 1 && headerReachable) {
                    curSlot() = -1;                                  // bottom row -> header (wrap)
                } else {
                    r = (r + 1) % rows;
                    curSlot() = r * cols + c;
                    if (curSlot() >= slots) curSlot() = slots - 1;
                }
            }
        }
        if (kDown & HidNpadButton_Left) {
            if (curSlot() == -1) {                                    // on the header: the box arrow cycles the box
                curBox() = (curBox() - 1 + paneBoxes(storageFocusPane)) % paneBoxes(storageFocusPane);
            } else {
                int c = curSlot() % cols;
                if (c > 0) {
                    curSlot() -= 1;
                } else if (storageFocusPane == 1 && !currentlySelecting) {
                    // A rectangle lives in one box of one pane, so crossing panes is only allowed
                    // when nothing is being selected. A carried block may cross freely.
                    const int r = curSlot() / cols;
                    storageFocusPane = 0;
                    const int scols = paneCols(0);
                    stSaveSlot = r * scols + (scols - 1);
                    if (stSaveSlot >= paneSlots(0)) stSaveSlot = paneSlots(0) - 1;
                }
            }
        }
        if (kDown & HidNpadButton_Right) {
            if (curSlot() == -1) {                                    // on the header: the box arrow cycles the box
                curBox() = (curBox() + 1) % paneBoxes(storageFocusPane);
            } else {
                int c = curSlot() % cols;
                if (c < cols - 1 && curSlot() + 1 < slots) {
                    curSlot() += 1;
                } else if (storageFocusPane == 0 && !currentlySelecting) {
                    const int r = curSlot() / cols;
                    storageFocusPane = 1;
                    stBankSlot = r * paneCols(1);  // col 0 of the bank pane
                }
            }
        }

        const int pane = storageFocusPane, box = curBox(), slot = curSlot();

        // Minus: options for the block in hand (Release all / Return to origin / Cancel).
        if ((kDown & HidNpadButton_Minus) && holding) {
            groupMenuActive = true;
            groupMenuIndex = 0;
            return;
        }

        // X: while drawing a rectangle it DUPLICATES the selection (grab a copy
        // and leave the originals in place). Otherwise it sorts the focused box. Suppressed while
        // carrying, so a sort can never run with Pokemon held out of the grid and lose them.
        if (kDown & HidNpadButton_X) {
            if (currentlySelecting) {
                grabSelection(false);
                if (carrying()) postStatus("Copied " + std::to_string(carriedCount()) + " - originals left in place.", 180);
                return;
            }
            if (!holding) {
                sortStorageBox(storageFocusPane, storageFocusPane == 0 ? stSaveBox : stBankBox);
                return;
            }
        }

        // A: act. A full hand overrides the mode -- it always means "put the block down here".
        if (kDown & HidNpadButton_A) {
            // Box-name header focused: A renames this pane's box (save box, or bank box). Guard first
            // so the placement/menu code below never indexes storage with slot == -1.
            if (slot == -1) {
                if (!holding) { if (pane == 0) renameBox(box); else renameBankBox(box); }
                return;
            }
            if (holding) {
                // Confirm first for a lossy conversion. Two separate warnings (see the header): the
                // Gen 3 down-convert is destructive and always warns; the Let's Go transfer is
                // recoverable and obeys the Move warning setting. Gen 3 wins if the move is both.
                const bool g3warn = blockInvolvesGen3Downgrade(pane);
                const bool lgwarn = g_moveWarn && blockInvolvesLgpe(pane);
                if (g3warn || lgwarn) {
                    pendingMove = PendingMove::PlaceHeld;
                    pendingMovePane = pane; pendingMoveBox = box; pendingMoveSlot = slot;
                    if (g3warn) gen3ConvertConfirmActive = true;
                    else        lgpeTransferConfirmActive = true;
                    moveConfirmIndex = 1;
                    return;
                }
                putDownBlock();
                return;
            }
            switch (cursorMode) {
                case CursorMode::Menu:
                    if (occupied(pane, box, slot)) {
                        // Party-linked (locked) slots still open the menu -- Edit and Clone are valid
                        // there; only Move and Release are disabled (see the action handler + the
                        // greyed rows). Start on Edit when Move is unavailable so the cursor doesn't
                        // land on a greyed row.
                        storageMenuActive = true;
                        storageMenuIndex = isLocked(pane, box, slot) ? 1 : 0;
                        menuPane = pane; menuBox = box; menuSlot = slot;
                    } else if (pane == 0 && !isLocked(pane, box, slot)) {
                        // Empty save-pane slot: create a new Pokemon here (pick species, then edit).
                        creator.active = true; creator.pane = pane; creator.box = box; creator.slot = slot;
                        pickerKind = Dialogs::PickerKind::Species;
                        buildCreatorSpeciesOrder();  // only species obtainable in this game (unless illegal-values on)
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerActive = true;
                    }
                    break;
                case CursorMode::Move:
                case CursorMode::Multi:
                    storagePickup();
                    break;
            }
        }
    }

    void TrainerViewScreen::update(const PadState& pad, const TouchInput& touch) {
        u64 kDown = padGetButtonsDown(&pad) | navTouchButton(touch);   // nav-bar badges are tappable

        // Let's Go stores its boxes as a GAPLESS list, so anything that vacated a slot last frame
        // left a hole the game can't represent. Re-pack it here rather than at each of the
        // several sites that can vacate a slot: this function has many early returns -- the release
        // path returns before ever reaching the bottom -- so an end-of-update hook would silently
        // skip them, and per-site hooks are exactly the coverage gap this codebase keeps hitting.
        // A no-op on every positional game. Excluded mid-operation so the board can't shift under a
        // Pokemon the user is currently carrying, swapping, selecting or creating.
        //
        // This IS the "exception for LGPE" in exact-slot placement: a block still lands on the exact
        // slots it was dropped onto, and then this closes any gap those slots left behind, sliding
        // the box forward. Nothing here can invalidate a selection any more -- a carried block owns
        // its Pokemon outright rather than pointing at slots -- but an in-progress rectangle is
        // anchored to slot indices, so it is excluded too.
        //
        // An OPEN DETAILS EDITOR is anchored to slot indices in exactly the same way -- it re-resolves
        // its target through selectedBoxIndex/selectedItemIndex every frame. The creator is how that
        // bit: it drops a new mon on whatever slot the cursor is on, which in a gapless game is usually
        // a gap, then clears creator.active and opens the editor. Compaction fired on the very next
        // frame, slid the mon down to the packed position, and left the editor pointing at an empty
        // slot -- so it drew nothing (drawPokemonDetailsModal bails on a null target) while still
        // swallowing input, and the app looked frozen with only B and + alive.
        //
        // Deferring is the whole fix: the mon still compacts, just once the editor closes.
        if (!carrying() && !currentlySelecting && !swapActive && !creator.active && !details.active) {
            trainer.compactStorage();
        }

        // Keep the game's persisted "current box" in step with whichever box view is open, so it
        // survives leaving Storage before the game save runs. In Storage X sorts, so the save happens
        // after backing out -- by which point the mode has changed; capturing the box here (not at
        // save time) is what makes the Storage box persist, matching the Boxes view. The save-pane box
        // (stSaveBox) and the Boxes-view box (selectedBoxIndex) are two cursors on the same current
        // box; gated on detailViewActive so the HOME menu (no box view) can't overwrite it.
        if (detailViewActive) {
            const int bc = static_cast<int>(trainer.getBoxCount());
            if (selectedMode == ViewMode::Storage && stSaveBox >= 0 && stSaveBox < bc)
                trainer.setCurrentBox(static_cast<uint8_t>(stSaveBox));
            else if (selectedMode == ViewMode::Boxes && selectedBoxIndex >= 0 && selectedBoxIndex < bc)
                trainer.setCurrentBox(static_cast<uint8_t>(selectedBoxIndex));
        }

        // Expire the transient status line. This lives HERE, not in handleStorageInput, because that
        // function only runs in the storage view and returns early without a bank -- so a message
        // posted from anywhere else (a save result, a box rename) would have stayed on screen
        // forever. update() runs every frame in every view, which is what a timer needs.
        if (storageStatusFrames > 0) --storageStatusFrames;

        // A box-name pill was tapped last frame: the header highlight has now been drawn once, so it
        // is safe to open the (blocking) rename keyboard. Doing this inline with the tap suspended the
        // app before the highlight ever rendered, so the selection only lit up AFTER the dialog closed
        // -- the deferral is what makes the tap feel immediate. One frame's delay is imperceptible.
        if (pendingHeaderRename) {
            pendingHeaderRename = false;
            if (selectedMode == ViewMode::Storage) {
                if (storageFocusPane == 0) renameBox(stSaveBox);
                else                       renameBankBox(stBankBox);
            } else {
                renameBox(selectedBoxIndex);
            }
            return;
        }

        // A trainer-info row (Name/Money) was activated last frame; open its blocking keyboard now that
        // the row highlight has had a frame to draw.
        if (pendingTrainerEdit >= 0) {
            const int row = pendingTrainerEdit;
            pendingTrainerEdit = -1;
            if (row == 0)      editTrainerName();
            else if (row == 1) editTrainerMoney();
            return;
        }

        // Handle + button (exits application)
        if (kDown & HidNpadButton_Plus) {
            // The bank question is already on screen -- answer that first rather than stacking a
            // second exit on top of it.
            if (storageExitConfirmActive) return;
            // A carried Pokemon goes home before anything is weighed up.
            returnHeldToOrigin();
            // The bank is its own save file and gets its own decision. Closing the app is NOT that
            // decision, so it must not write the bank on the way out: doing that committed every
            // transfer even when the user went on to DECLINE the game save, and the two files then
            // disagreed. A deposit became a permanent CLONE (the bank has it, the game save still
            // has it), a withdrawal a permanent LOSS (the bank no longer has it, the game save was
            // never written). Ask instead, and let Discard rewind both sides and write nothing.
            if (bank && bank->hasChanged()) {
                storageExitConfirmActive = true;
                storageExitConfirmIndex = 0;
                exitAfterBankChoice = true;   // resume this exit once they've answered
                return;
            }
            beginAppExit();
            return;
        }

        // Reusable value picker (nature / gender / move) — owns all input while open.
        if (pickerActive) {
            Pokemon::Pokemon* pkPick = detailsTargetPokemon();
            const int count = (pickerCount > 0) ? pickerCount : 1;
            const int page = 12;

            // Touch: tap an option row selects it immediately (button id = option index).
            int ptb = touchedButtonId(touch);
            if (ptb >= 0 && ptb < count) { pickerSel = ptb; kDown |= HidNpadButton_A; }

            if (kDown & HidNpadButton_Up)                        pickerSel = (pickerSel - 1 + count) % count;
            if (kDown & HidNpadButton_Down)                      pickerSel = (pickerSel + 1) % count;
            if (kDown & (HidNpadButton_L | HidNpadButton_Left))  pickerSel = std::max(0, pickerSel - page);
            if (kDown & (HidNpadButton_R | HidNpadButton_Right)) pickerSel = std::min(count - 1, pickerSel + page);
            if (pickerSel < 0) pickerSel = 0;
            if (pickerSel >= count) pickerSel = count - 1;

            if (kDown & HidNpadButton_B) {
                pickerActive = false; creator.active = false;
                // A change-type picker was launched from the Edit Item dialog -> cancelling it
                // returns THERE, not to the item list (the dialog is still "open" underneath).
                if (itemPickerReplace) { itemPickerReplace = false; itemEditDialogActive = true; }
                return;
            }

            // Creator: a Species pick in create mode builds a new mon into the target empty slot,
            // then hands off to the details editor (there is no live target Pokemon yet, so this
            // intercepts before the normal pkPick apply-switch below).
            if ((kDown & HidNpadButton_A) && creator.active && pickerKind == Dialogs::PickerKind::Species) {
                const int sp = (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size()))
                                   ? pickerOrder[pickerSel] : pickerSel;
                if (sp > 0) {
                    storageSlot(creator.pane, creator.box, creator.slot) =
                        buildDefaultMon(trainer, static_cast<uint16_t>(sp),
                                        saveOriginVersion(trainer, titleId));
                    hasUnsavedChanges = true;
                    pickerActive = false; creator.active = false;
                    openStorageEditor(creator.pane, creator.box, creator.slot);
                    creator.editing = true;  // modal now edits a fresh mon -> Keep/Discard prompt on exit
                } else {
                    pickerActive = false; creator.active = false;  // "None" cancels
                }
                return;
            }

            // Change item type: a pouch pick opened from the Edit Item dialog REPLACES the selected
            // item's type rather than adding a new one. Must come before the add handler (same kind).
            if ((kDown & HidNpadButton_A) && itemPickerReplace
             && (pickerKind == Dialogs::PickerKind::PouchItem
              || pickerKind == Dialogs::PickerKind::PouchItemG3)) {
                itemPickerReplace = false;
                if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size())
                    && selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    const uint16_t newId = static_cast<uint16_t>(pickerOrder[pickerSel]);
                    auto& pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size())) {
                        const int rawIdx = visible[selectedItemIndex];
                        const uint16_t keepCount = pouch[rawIdx].count;
                        // Before the swap -- the old id is about to be overwritten or zeroed. Both
                        // ids are logged because a retype is one of the two ways a pouch entry can
                        // change identity, and "my Master Ball became a Potion" reads very
                        // differently from an add followed by a remove.
                        if (g_debugLogging) {
                            Utils::logEventToFile(
                                std::string("ITEM action=RETYPE ")
                                + Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory))
                                + " " + Utils::logField("from", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup()))
                                + " fromid=" + std::to_string(pouch[rawIdx].itemId)
                                + " " + Utils::logField("to", Utils::itemName(newId, trainer.getGameGroup()))
                                + " toid=" + std::to_string(newId)
                                + " count=" + std::to_string(keepCount)
                                + " mode=" + (trainer.itemsAreIdIndexed() ? "idIndexed" : "slotBased"));
                        }
                        // The dialog we return to must show the NEW item's amount (its count carried
                        // over unchanged), so re-sync both the live value and the change-baseline.
                        itemEditDialogValue = keepCount;
                        itemEditDialogOriginalValue = keepCount;
                        if (trainer.itemsAreIdIndexed()) {
                            // Zero the old id (KEEP the entry so its slot is written to 0), then set the
                            // new id -- reassigning itemId in place would ghost the old id's count.
                            pouch[rawIdx].count = 0;
                            const auto e = std::find_if(pouch.begin(), pouch.end(),
                                [newId](const Trainer::InventoryItem& v) { return v.itemId == newId; });
                            if (e != pouch.end()) { e->count = keepCount; e->isNew = true; }
                            else pouch.push_back(Trainer::InventoryItem{ newId, keepCount, true, false });
                        } else {
                            pouch[rawIdx].itemId = newId;   // slot-based: reassign in place, region rewritten
                            pouch[rawIdx].isNew = true;     // the swapped-in item shows as new
                        }
                        hasUnsavedChanges = true;
                        // Land the cursor on the changed item (it may have re-sorted onto another page).
                        const std::vector<int> vis = visibleItemIndices();
                        int perPage = (CONTENT_PANEL_HEIGHT - 106) / 52;
                        if (perPage < 1) perPage = 1;
                        for (int i = 0; i < static_cast<int>(vis.size()); ++i) {
                            if (pouch[vis[i]].itemId == newId) { selectedItemIndex = i; currentPage = i / perPage; break; }
                        }
                    }
                }
                pickerActive = false;
                itemEditDialogActive = true;   // return to the Edit Item dialog, now showing the new type
                return;
            }

            // Item creation: a pouch pick edits the trainer's BAG, not a Pokemon, so it has to
            // intercept before the apply-switch below -- pkPick is null in the items view.
            if ((kDown & HidNpadButton_A)
             && (pickerKind == Dialogs::PickerKind::PouchItem
              || pickerKind == Dialogs::PickerKind::PouchItemG3)) {
                if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size())
                    && selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    const uint16_t id = static_cast<uint16_t>(pickerOrder[pickerSel]);
                    auto& pouch = trainer.items[selectedCategory];
                    const auto e = std::find_if(pouch.begin(), pouch.end(),
                        [id](const Trainer::InventoryItem& x) { return x.itemId == id; });
                    const char* addVia = nullptr;
                    if (e != pouch.end()) {
                        // Present but empty -> re-activate it AND flag it new (a freshly-added
                        // item always shows the bag's "new" marker in the games that have one).
                        if (e->count == 0) { e->count = 1; e->isNew = true; hasUnsavedChanges = true; addVia = "revived"; }
                    } else if (static_cast<int>(pouch.size()) < currentPouchCapacity()) {
                        pouch.push_back(Trainer::InventoryItem{ id, 1, true, false });
                        hasUnsavedChanges = true;
                        addVia = "appended";
                    } else {
                        addVia = "REFUSED_POUCH_FULL";
                    }
                    if (g_debugLogging && addVia) {
                        Utils::logEventToFile(
                            std::string("ITEM action=ADD ")
                            + Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory))
                            + " " + Utils::logField("item", Utils::itemName(id, trainer.getGameGroup()))
                            + " id=" + std::to_string(id)
                            + " count=1 via=" + addVia
                            + " pouchsize=" + std::to_string(pouch.size())
                            + "/" + std::to_string(currentPouchCapacity()));
                    }
                    // Land the cursor on the row that was just added so A edits its amount next --
                    // and FOLLOW IT TO ITS PAGE. The list is paged, and a new item usually sorts
                    // onto a later page, so selecting it without moving the page left the user
                    // staring at an unchanged screen having to go find it themselves.
                    const std::vector<int> vis = visibleItemIndices();
                    int perPage = (CONTENT_PANEL_HEIGHT - 106) / 52;   // mirrors ItemsPanel's row fit
                    if (perPage < 1) perPage = 1;
                    for (int i = 0; i < static_cast<int>(vis.size()); ++i) {
                        if (pouch[vis[i]].itemId == id) {
                            selectedItemIndex = i;
                            currentPage = i / perPage;
                            break;
                        }
                    }
                }
                pickerActive = false;
                return;
            }

            if ((kDown & HidNpadButton_A) && pkPick) {
                switch (pickerKind) {
                    case Dialogs::PickerKind::Nature:
                        pkPick->setNature(static_cast<uint8_t>(pickerSel));
                        pkPick->setStatNature(static_cast<uint8_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::Gender:
                        // rows are the species' possible genders, so the row index is NOT the value
                        if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size())) {
                            const uint8_t g = static_cast<uint8_t>(pickerOrder[pickerSel]);
                            pkPick->setGender(g);
                            // Meowstic, Indeedee, Basculegion and Oinkologne store the gender AS the
                            // form, so the form has to move with it -- leaving a female Meowstic on
                            // the male form is the one combination the form picker will not produce.
                            const uint8_t nf =
                                Pokemon::genderLinkedForm(pkPick->speciesID(), pkPick->form(), g);
                            if (nf != pkPick->form()) pkPick->setForm(nf);
                        }
                        break;
                    case Dialogs::PickerKind::Move: {
                        const int mv = (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size()))
                                           ? pickerOrder[pickerSel] : pickerSel;
                        pkPick->setMove(pickerSlot, static_cast<uint16_t>(mv));
                        pkPick->setMovePP(pickerSlot,
                                          Names::getMoveBasePP(static_cast<uint16_t>(mv),
                                                               pkPick->getGameGroup()));
                        pkPick->setMovePPUps(pickerSlot, 0);
                        break;
                    }
                    case Dialogs::PickerKind::Item:
                    case Dialogs::PickerKind::ItemG3:   // same write; only the id space differs
                        pkPick->setHeldItem(static_cast<uint16_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::Level:
                        pkPick->setLevel(static_cast<uint8_t>(pickerSel + 1));  // options 0-99 -> level 1-100
                        break;
                    case Dialogs::PickerKind::Friendship:
                        pkPick->setFriendship(static_cast<uint8_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::Ball:
                        if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size()))
                            pkPick->setBall(static_cast<uint8_t>(pickerOrder[pickerSel]));
                        break;
                    case Dialogs::PickerKind::Ability: {
                        const int v = (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size()))
                                          ? pickerOrder[pickerSel] : pickerSel;
                        pkPick->setAbility(static_cast<uint16_t>(v));
                        // Align the ability slot when the pick is one of the species' legal abilities.
                        // Gen 3 has no ability id field -- setAbility() already resolved the slot and
                        // re-rolled the PID there, so re-deriving the number would undo that work.
                        if (pkPick->getGameGroup() != GameVersion::FRLG) {
                            const Pokemon::AbilitySlots slots =
                                Pokemon::getAbilitySlots(pkPick->speciesID(), pkPick->form(), pkPick->getGameGroup());
                            const uint8_t an = Pokemon::getAbilityNumberForId(slots, static_cast<uint16_t>(v));
                            if (an != 0) pkPick->setAbilityNumber(an);
                        }
                        break;
                    }
                    case Dialogs::PickerKind::Language:
                        pkPick->setLanguage(static_cast<uint8_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::Origin:
                        pkPick->setOriginGame(static_cast<uint8_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::MetLevel:
                        pkPick->setMetLevel(static_cast<uint8_t>(pickerSel + 1));  // options 0-99 -> met level 1-100
                        break;
                    case Dialogs::PickerKind::MetLocation:
                        if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size())) {
                            uint16_t loc = static_cast<uint16_t>(pickerOrder[pickerSel]);
                            if (pickerMetIsEgg) pkPick->setEggLocation(loc);   // egg-met vs met location
                            else                pkPick->setMetLocation(loc);
                        }
                        break;
                    case Dialogs::PickerKind::Form:
                        // rows are storable forms, so the row index is NOT the form id
                        if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size())) {
                            const uint8_t nf = static_cast<uint8_t>(pickerOrder[pickerSel]);
                            pkPick->setForm(nf);   // recalculates stats + checksum internally
                            // Gender follows the form for the species that tie the two together,
                            // otherwise the form picker is a second door into exactly the combination
                            // the gender picker refuses to offer. Two different ties, both real:
                            //   - Meowstic/Indeedee/Basculegion/Oinkologne -- the form IS the gender,
                            //     both ways, so the low bit of the new form is the new gender.
                            //   - the cap Pikachu, Ash Greninja, Bloodmoon Ursaluna -- a single-gender
                            //     form of a dual-gender species. One-way only: the form pins the
                            //     gender, but there is no counterpart form to flip to, which is why
                            //     this reads the per-form ratio instead of the low bit.
                            if (Pokemon::isFormGenderSpecific(pkPick->speciesID())) {
                                const uint8_t g = static_cast<uint8_t>(nf & 1);
                                if (g != pkPick->gender()) pkPick->setGender(g);
                            } else {
                                const std::vector<int> g = selectableGenders(pkPick->speciesID(), nf);
                                if (g.size() == 1 && g[0] != static_cast<int>(pkPick->gender()))
                                    pkPick->setGender(static_cast<uint8_t>(g[0]));
                            }
                        }
                        break;
                    case Dialogs::PickerKind::StatNature:
                        pkPick->setStatNature(static_cast<uint8_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::Species:
                        pkPick->setSpecies(static_cast<uint16_t>(pickerSel));
                        break;
                    case Dialogs::PickerKind::PouchItem:
                    case Dialogs::PickerKind::PouchItemG3:
                        break;   // handled above -- these edit the bag, not a Pokemon
                }
                hasUnsavedChanges = true;
                mirrorEditedPartyMember();
                pickerActive = false;
            }
            return;
        }

        // X saves the game -- but ONLY from the HOME main menu (where you pick Pokemon / Party /
        // Storage / Items / ...). detailViewActive means "entered a view", and inside any entered view
        // X is a view-local action (Items: remove an item; Storage: sort box; the details page: save the
        // mon), so the global game-save must never fire there. Back out to the menu, then press X.
        if (kDown & HidNpadButton_X) {
            const bool onHomeMenu = !detailViewActive;
            if (onHomeMenu && !saveConfirmActive && !itemEditDialogActive && !statEdit.dialogActive &&
                !releaseConfirmActive && !storageMenuActive && !groupMenuActive &&
                !storageExitConfirmActive && !details.active) {
                exitingWithUnsavedChanges = false;  // Regular save, not exiting
                exitingViaPlus = false;
                saveDestIndex = defaultSaveDestRow();   // first row: game save for a title session,
                                                        // the open backup for a backup session
                saveConfirmActive = true;
                return;
            }
        }

        // Injecting into the real game save — the last gate, and the only thing PKSE does that
        // overwrites data the user cannot recover from inside the app. Handled BEFORE the save
        // dialog because saveConfirmActive is still true underneath it.
        if (saveInjectConfirmActive) {
            const int tb = touchedButtonId(touch);
            if (tb == 1)      kDown |= HidNpadButton_A;
            else if (tb == 0) kDown |= HidNpadButton_B;
            if (kDown & HidNpadButton_A) {
                saveInjectConfirmActive = false;
                performSave(backupDir, true);
                return;
            }
            if (kDown & HidNpadButton_B) { saveInjectConfirmActive = false; return; }  // back to the picker
            return;
        }

        // Handle save confirmation dialog
        if (saveConfirmActive) {
            if (exitingWithUnsavedChanges) {
                // Exiting with unsaved changes - different button logic
                // A: Discard changes and exit
                // B: Cancel exit (stay on screen)
                // Tappable buttons: id 0 = Cancel (B), id 1 = Discard & Exit (A).
                const int td = touchedButtonId(touch);
                if (td == 0) kDown |= HidNpadButton_B;
                else if (td == 1) kDown |= HidNpadButton_A;
                if (kDown & HidNpadButton_A) {
                    // User chose to discard changes and exit
                    saveConfirmActive = false;
                    if (exitingViaPlus) {
                        // Exiting via + button - exit the app
                        exitRequested = true;
                    }
                    // Always set goBack for consistency (B button case)
                    goBack = true;
                    exitingWithUnsavedChanges = false;
                    exitingViaPlus = false;
                    return;
                }
                if (kDown & HidNpadButton_B) {
                    // User cancelled exit - stay on screen
                    saveConfirmActive = false;
                    exitingWithUnsavedChanges = false;
                    exitingViaPlus = false;
                    return;
                }
            } else {
                // Regular save dialog (triggered by X button)
                // A: Save changes
                // B: Cancel
                // Destination picker: Up/Down choose, A writes there.
                const int nDest = saveDestCount();
                if (saveDestIndex >= nDest) saveDestIndex = 0;   // lock may have gone off
                if (kDown & HidNpadButton_Up)   saveDestIndex = (saveDestIndex - 1 + nDest) % nDest;
                if (kDown & HidNpadButton_Down) saveDestIndex = (saveDestIndex + 1) % nDest;
                const int td = touchedButtonId(touch);
                if (td >= 0 && td < nDest) { saveDestIndex = td; kDown |= HidNpadButton_A; }
                // The cursor is a ROW; which destination that row means depends on the session.
                const SaveDest chosenDest = saveDestAt(saveDestIndex);

                if (kDown & HidNpadButton_A) {
                    // A carried Pokemon lives outside both containers — return it before serializing
                    // so it isn't dropped from the save/bank.
                    returnHeldToOrigin();

                    // (The bank has its OWN persistence — it saves on storage-view exit / app exit,
                    // separate from this game-save, since it's a separate entity from the save file.)
                    if (chosenDest == DestGameSave) {
                        // Writing your OWN save back is the ordinary thing a save editor does -- the
                        // data being overwritten is the data we just read -- so it goes straight
                        // through. Only a backup-sourced session needs the extra gate, because that
                        // is the one that rolls the game backwards.
                        if (loadedFromCart) { performSave(backupDir, true); return; }
                        saveInjectConfirmActive = true;
                        return;
                    }

                    std::string destDir = backupDir;
                    if (chosenDest == DestNewBackup) {
                        const Utils::KeyboardResult res =
                            Utils::promptText("New Backup", "Backup name", titleName, 40);
                        if (!res.accepted) return;      // cancelled: leave the dialog up, nothing written
                        destDir = createNamedBackupDir(res.text);
                        if (destDir.empty()) {
                            postStatus("Couldn't create that backup. Use letters, numbers, spaces or -.", 480);
                            return;
                        }
                    }
                    performSave(destDir, false);
                    return;
                }
                if (kDown & HidNpadButton_B) {
                    // User cancelled save - just close the dialog
                    saveConfirmActive = false;
                    return;
                }
            }
            return;  // Don't process other inputs while save confirm is active
        }

        // Handle stat edit dialog
        if (statEdit.dialogActive) {
            // Touch: map on-screen buttons to their equivalent presses (IV/EV rows -> Up/Down mode
            // switch, the step buttons -> ZL/L/Left/Right/R/ZR, Save -> A, Cancel -> B), then reuse
            // the button logic below.
            switch (touchedButtonId(touch)) {
                case 10: kDown |= HidNpadButton_Up;    break;
                case 11: kDown |= HidNpadButton_Down;  break;
                case 34: kDown |= HidNpadButton_ZL;    break;
                case 30: kDown |= HidNpadButton_L;     break;
                case 31: kDown |= HidNpadButton_Left;  break;
                case 32: kDown |= HidNpadButton_Right; break;
                case 33: kDown |= HidNpadButton_R;     break;
                case 35: kDown |= HidNpadButton_ZR;    break;
                case 1:  kDown |= HidNpadButton_A;     break;
                case 0:  kDown |= HidNpadButton_B;     break;
                default: break;
            }

            // Resolve the Pokemon being edited (party / box / bank).
            Pokemon::Pokemon* pokemon = detailsTargetPokemon();

            if (pokemon) {
                    // Let's Go uses Awakening Values (AVs), not EVs — EVs are inert there. So the
                    // second editable stat is AV for LGPE Pokemon, EV for everything else.
                    const bool usesAV = pokemon->hasAwakeningValues();
                    const Dialogs::StatEditMode secondMode = usesAV ? Dialogs::StatEditMode::AV
                                                                    : Dialogs::StatEditMode::EV;

                    // Up/Down to switch between IV and the second stat (EV / AV)
                    if (kDown & HidNpadButton_Up) {
                        // Save current value before switching
                        if (statEdit.mode == secondMode) {
                            if (usesAV) statEdit.currentAV = statEdit.value;
                            else        statEdit.currentEV = statEdit.value;
                        }
                        statEdit.mode = Dialogs::StatEditMode::IV;
                        statEdit.value = statEdit.currentIV;  // Load IV value
                    }
                    if (kDown & HidNpadButton_Down) {
                        // Save current value before switching
                        if (statEdit.mode == Dialogs::StatEditMode::IV) {
                            statEdit.currentIV = statEdit.value;
                        }
                        statEdit.mode = secondMode;
                        statEdit.value = usesAV ? statEdit.currentAV : statEdit.currentEV;  // Load AV/EV value
                    }

                    // Get max value based on mode (IV 0-31, AV 0-200, EV 0-252)
                    int minValue = 0;
                    // Illegal-values override (Settings) lifts the EV/AV per-stat cap to 255. IV stays 31
                    // (the 5-bit format ceiling — there is no "illegal" IV value to reach).
                    int maxValue = (statEdit.mode == Dialogs::StatEditMode::IV) ? 31
                                 : (statEdit.mode == Dialogs::StatEditMode::AV) ? (g_allowIllegalEdits ? 255 : 200)
                                 : (g_allowIllegalEdits ? 255 : 252);

                    // Adjust value: Left/Right = -/+1, L/R = -/+10, ZL/ZR = -/+100. On a 0-31 IV the
                    // +/-100 steps just clamp, which is harmless.
                    if (kDown & HidNpadButton_Left)  statEdit.value = std::max(minValue, statEdit.value - 1);
                    if (kDown & HidNpadButton_Right) statEdit.value = std::min(maxValue, statEdit.value + 1);
                    if (kDown & HidNpadButton_L)     statEdit.value = std::max(minValue, statEdit.value - 10);
                    if (kDown & HidNpadButton_R)     statEdit.value = std::min(maxValue, statEdit.value + 10);
                    if (kDown & HidNpadButton_ZL)    statEdit.value = std::max(minValue, statEdit.value - 100);
                    if (kDown & HidNpadButton_ZR)    statEdit.value = std::min(maxValue, statEdit.value + 100);

                    // Store back into whichever value the active mode edits.
                    if (statEdit.mode == Dialogs::StatEditMode::IV)      statEdit.currentIV = statEdit.value;
                    else if (statEdit.mode == Dialogs::StatEditMode::AV) statEdit.currentAV = statEdit.value;
                    else                                                statEdit.currentEV = statEdit.value;

                    // For EVs, enforce the 510 legal total — unless the illegal-values override is on
                    // (510 is a game rule, not a byte limit; the illegal ceiling is 6 x 255 = 1530).
                    if (statEdit.mode == Dialogs::StatEditMode::EV && !g_allowIllegalEdits) {
                        int totalEVs = pokemon->evHP() + pokemon->evATK() + pokemon->evDEF() +
                            pokemon->evSPE() + pokemon->evSPA() + pokemon->evSPD();
                        int projectedTotal = totalEVs - statEdit.originalEV + statEdit.value;
                        if (projectedTotal > 510) {
                            // Adjust value to not exceed total
                            statEdit.value = std::max(0, 510 - (totalEVs - statEdit.originalEV));
                        }
                    }

                    // Confirm edit
                    if (kDown & HidNpadButton_A) {
                        // Remember that something outside the games' own limits is being committed,
                        // so the save can warn ONCE rather than the editor nagging per field. Only
                        // reachable with "Allow illegal values" on; the caps clamp otherwise.
                        const int evTotalAfter = pokemon->evHP() + pokemon->evATK() + pokemon->evDEF() +
                                                 pokemon->evSPE() + pokemon->evSPA() + pokemon->evSPD()
                                               - statEdit.originalEV + statEdit.currentEV;
                        if (statEdit.currentEV > 252 || statEdit.currentAV > 200 || evTotalAfter > 510)
                            illegalDataWritten = true;

                        // Map UI stat index to Pokemon data stat index
                        // UI order: HP, ATK, DEF, SPA, SPD, SPE (indices 0-5)
                        // Data order: HP, ATK, DEF, SPE, SPA, SPD (indices 0-5)
                        int statIndexMap[] = {0, 1, 2, 4, 5, 3}; // UI index -> Data index
                        int dataStatIndex = statIndexMap[statEdit.selectedStat];

                        // Apply IV always; apply AV for Let's Go, EV otherwise.
                        bool ivEvModified = false;  // IV/EV changes want a PID refresh; AV doesn't
                        bool anyModified = false;
                        if (statEdit.currentIV != statEdit.originalIV) {
                            pokemon->setIV(dataStatIndex, statEdit.currentIV);
                            hasUnsavedChanges = true;
                            ivEvModified = true; anyModified = true;
                        }
                        if (usesAV) {
                            if (statEdit.currentAV != statEdit.originalAV) {
                                pokemon->setAV(dataStatIndex, static_cast<uint8_t>(statEdit.currentAV));
                                hasUnsavedChanges = true;
                                anyModified = true;
                            }
                        } else if (statEdit.currentEV != statEdit.originalEV) {
                            pokemon->setEV(dataStatIndex, static_cast<uint8_t>(statEdit.currentEV));
                            hasUnsavedChanges = true;
                            ivEvModified = true; anyModified = true;
                        }

                        // Regenerate PID to maintain legality after IV/EV changes (AVs don't affect it).
                        if (ivEvModified) {
                            pokemon->regeneratePID(pokemon->id32());
                        }
                        // If this is an LGPE party member, mirror the edit to its party copy so the
                        // save's party overlay doesn't clobber it.
                        if (anyModified) {
                            mirrorEditedPartyMember();
                        }

                        statEdit.dialogActive = false;
                        return;
                    }

                    // Cancel edit
                    if (kDown & HidNpadButton_B) {
                        statEdit.dialogActive = false;
                        return;
                    }
            }

            return;  // Don't process other inputs while stat edit dialog is active
        }

        // Handle Pokemon details modal (HOME Check-Summary editor page).
        if (details.active) {
            Pokemon::Pokemon* pokemon = detailsTargetPokemon();
            int tb = touchedButtonId(touch);

            // Creator: a freshly-made mon isn't committed until accepted. Closing raises this prompt --
            // Keep (A / right button) accepts it; Discard (B / left button) removes it from the box.
            if (creator.keepConfirmActive) {
                // Three glyph buttons: id 0 = Back (B), id 2 = Discard (Y), id 1 = Keep (A).
                if (tb == 0)      kDown |= HidNpadButton_B;
                else if (tb == 1) kDown |= HidNpadButton_A;
                else if (tb == 2) kDown |= HidNpadButton_Y;
                if (kDown & HidNpadButton_B) { creator.keepConfirmActive = false; return; }  // back to editing the new mon
                const bool discard = (kDown & HidNpadButton_Y);
                if ((kDown & HidNpadButton_A) || discard) {
                    // Both outcomes are logged. A kept mon is a new record entering the save and is
                    // the thing a later "this Pokemon is illegal/corrupt" report is about, so it is
                    // described in full; a discarded one explains why a slot the user remembers
                    // filling is empty.
                    if (g_debugLogging) {
                        const auto& made = storageSlot(creator.pane, creator.box, creator.slot);
                        if (made) {
                            Utils::logEventToFile(
                                std::string("CREATE result=") + (discard ? "DISCARDED" : "KEPT") + " "
                                + Utils::logSlot("at", creator.pane, creator.box, creator.slot)
                                + " " + Utils::describeMon(*made, trainer));
                        }
                    }
                    if (discard) {  // remove the just-created mon from the box
                        storageSlot(creator.pane, creator.box, creator.slot).reset();
                        hasUnsavedChanges = true;
                    }
                    creator.keepConfirmActive = false; creator.editing = false;
                    details.active = false; details.source = EditSource::Box;
                    details.selectedField = 0; details.legalityOverlay = false; details.ribbonOverlay = false;
                }
                return;
            }

            // Unsaved edits on an EXISTING mon. Closing the page discards them, so say so first
            // rather than silently rolling back -- Save commits (same as X), Discard throws the
            // edits away, Back returns to the page. Mirrors the creator's Keep/Discard prompt,
            // which already covered the not-yet-accepted case.
            if (details.discardConfirmActive) {
                if (tb == 0)      kDown |= HidNpadButton_B;
                else if (tb == 1) kDown |= HidNpadButton_A;
                else if (tb == 2) kDown |= HidNpadButton_Y;
                if (kDown & HidNpadButton_B) { details.discardConfirmActive = false; return; }  // keep editing
                if (kDown & HidNpadButton_A) {          // Save: exactly what X does, then leave
                    details.discardConfirmActive = false;
                    if (g_debugLogging && pokemonEditDirty()) {
                        if (const Pokemon::Pokemon* t = detailsTargetPokemon())
                            Utils::logEventToFile("MODIFY via=save-on-close " + Utils::describeMon(*t, trainer));
                    }
                    snapshotEditTarget();               // baseline := current, so nothing rolls back
                    closeDetailsModal();                // hasUnsavedChanges + party mirror were
                    return;                             // already handled as each field was edited
                }
                if (kDown & HidNpadButton_Y) {          // Discard: roll back and leave
                    details.discardConfirmActive = false;
                    if (g_debugLogging) {
                        if (const Pokemon::Pokemon* t = detailsTargetPokemon())
                            Utils::logEventToFile("MODIFY result=DISCARDED via=close-prompt " + Utils::briefMon(*t));
                    }
                    restoreEditTarget();
                    closeDetailsModal();
                    return;
                }
                return;
            }

            // Legality overlay (full issue list) intercepts input while open: B or any tap closes it.
            if (details.legalityOverlay) {
                if ((kDown & HidNpadButton_B) || tb >= 0) details.legalityOverlay = false;
                return;
            }
            // Ribbon overlay (full ribbon/mark list) does the same.
            if (details.ribbonOverlay) {
                if ((kDown & HidNpadButton_B) || tb >= 0) details.ribbonOverlay = false;
                return;
            }
            // Open the ribbon list: Y, or tapping the Ribbons row (id 94).
            if ((kDown & HidNpadButton_Y) || tb == 94) {
                details.ribbonOverlay = true;
                return;
            }
            // Open the legality issue list: R, or tapping the legality summary (id 95) -- but ONLY
            // when the report has something in it. That includes a clean mon with layer 3 notes
            // (which encounter it matched or why the check could not run); a truly empty report
            // has nothing to show, so the button is disabled (greyed in the guide) and this does nothing.
            if ((kDown & HidNpadButton_R) || tb == 95) {
                if (pokemon && !Legality::analyze(*pokemon, pokemon->getGameGroup()).empty())
                    details.legalityOverlay = true;
                return;
            }
            // X: for an EXISTING mon, SAVE the edits -- commit the current state as the new baseline
            // (the "Unsaved changes" marker clears) and STAY on the page. This is the ONLY commit:
            // closing with B/close rolls back to this baseline, so any later edits are discarded. For a
            // freshly-CREATED mon there is nothing to "save": every field is an unsaved edit until
            // committed, so X is KEEP -- finalize the mon (already in the box) and close. Discard still
            // lives on the B Keep/Discard prompt.
            if (kDown & HidNpadButton_X) {
                if (creator.editing) { creator.editing = false; closeDetailsModal(); return; }
                if (g_debugLogging && pokemonEditDirty()) {
                    if (const Pokemon::Pokemon* t = detailsTargetPokemon())
                        Utils::logEventToFile("MODIFY via=save-button " + Utils::describeMon(*t, trainer));
                }
                snapshotEditTarget();   // baseline := current -> pokemonEditDirty() now false
                return;
            }
            // Randomize IVs: L only. (The on-panel Randomize row was removed; L is the trigger,
            // and the bottom nav bar advertises it.)
            if (kDown & HidNpadButton_L) {
                if (Pokemon::Pokemon* target = detailsTargetPokemon()) {
                    randomizeIVs(target);
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                }
                return;
            }

            // Touch: close button (99), or a stat row (0-5) / shiny (6) -> select + act (like A).
            // Close DISCARDS unsaved edits -- restoreEditTarget() rolls the mon back to the last
            // Save/open snapshot; the individual Save button (X) is the only commit. The creator is the
            // exception: its not-yet-committed mon asks Keep/Discard instead.
            if (tb == 99) {
                if (creator.editing) { creator.keepConfirmActive = true; creator.keepConfirmIndex = 1; return; }
                if (pokemonEditDirty()) { details.discardConfirmActive = true; return; }
                restoreEditTarget();
                closeDetailsModal();
                return;
            }
            if (tb >= 0 && tb <= 31) { details.selectedField = tb; kDown |= HidNpadButton_A; }

            // B closes the page. Unsaved edits would be rolled back to the last Save/open snapshot,
            // so ask before throwing them away.
            if (kDown & HidNpadButton_B) {
                if (creator.editing) { creator.keepConfirmActive = true; creator.keepConfirmIndex = 1; return; }
                if (pokemonEditDirty()) { details.discardConfirmActive = true; return; }
                restoreEditTarget();
                closeDetailsModal();
                return;
            }

            // Three-column navigation: Up/Down cycle within the current column; Left/Right hop between the
            // center column (0-9: stats/shiny/nature/gender/level), the moves column (10-14, right panel),
            // and the editable detail column (left panel; ids 15-25, navigated in draw order).
            {
                int& f = details.selectedField;
                // Left-pane editable field ids in DRAW (top-to-bottom) order, built by the modal draw
                // (PokemonDetailsModal.cpp) so conditional rows (Form, Stat Nature, Pokerus, ...) appear
                // only when actually shown and the cursor visits exactly what's on screen. Fall back to
                // the always-present rows on the very first frame, before the draw has run once.
                std::vector<int> L = details.leftOrder;
                if (L.empty()) L = { 15, 16, 17, 18, 25, 19, 20, 21 };
                const bool inRight = (f >= 10 && f <= 14);
                const bool inLeft  = (f >= 15);
                auto leftMove = [&](int dir) -> int {
                    int pos = 0;
                    for (int i = 0; i < static_cast<int>(L.size()); ++i) if (L[i] == f) { pos = i; break; }
                    return L[(pos + dir + static_cast<int>(L.size())) % static_cast<int>(L.size())];
                };
                // Center column: 0-9 in a fixed cycle, except that a read-only Gender row (8) is
                // stepped over -- the same treatment the left column gives a read-only row by simply
                // not listing it. See genderEditable: locked means there is nothing to change it to.
                const bool skipGender = pokemon && !genderEditable(*pokemon);
                auto centerMove = [&](int dir) -> int {
                    int nf = f;
                    for (int i = 0; i < 10; ++i) {
                        nf = (nf + dir + 10) % 10;
                        if (!(nf == 8 && skipGender)) break;
                    }
                    return nf;
                };
                if (kDown & HidNpadButton_Up)
                    f = inRight ? 10 + ((f - 10 - 1 + 5) % 5)
                      : inLeft  ? leftMove(-1)
                      :           centerMove(-1);
                if (kDown & HidNpadButton_Down)
                    f = inRight ? 10 + ((f - 10 + 1) % 5)
                      : inLeft  ? leftMove(+1)
                      :           centerMove(+1);
                if (kDown & HidNpadButton_Right) {
                    if (inLeft)        f = details.lastCenterField;               // left  -> center
                    else if (!inRight) { details.lastCenterField = f; f = 10; }   // center -> right
                }
                if (kDown & HidNpadButton_Left) {
                    if (inRight)       f = details.lastCenterField;               // right -> center
                    else if (!inLeft)  { details.lastCenterField = f; f = L.front(); }  // center -> left
                }
                // Catch every other way the cursor can land on a locked Gender row -- a remembered
                // lastCenterField, or a mon swapped out from under the cursor -- in one place.
                if (f == 8 && skipGender) f = 7;
            }

            // A on an editable row opens the right editor: stat dialog (0-5), shiny toggle (6), or the
            // selection picker (7 nature, 8 gender, 9-12 the four move slots).
            if ((kDown & HidNpadButton_A) && pokemon) {
                const int f = details.selectedField;
                if (f == 6) {
                    bool currentShiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
                    pokemon->setShiny(!currentShiny, pokemon->id32());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();  // keep an LGPE party member's box/party copies in sync
                } else if (f == 7) {          // Nature
                    pickerKind = Dialogs::PickerKind::Nature;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->nature();
                    pickerActive = true;
                } else if (f == 8 && genderEditable(*pokemon)) {   // Gender (read-only when fixed)
                    pickerKind = Dialogs::PickerKind::Gender;
                    // only the genders this species/form can be; a fixed-gender species never gets
                    // here at all, so every picker opened from this row has a real choice in it
                    buildGenderPickerOrder(pokemon->speciesID(), pokemon->form(), pokemon->gender());
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                } else if (f == 9) {          // Level (1-100 picker; box mons derive the level from EXP)
                    pickerKind = Dialogs::PickerKind::Level;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    uint8_t lvl = pokemon->level();
                    if (lvl == 0) lvl = Pokemon::getLevelFromExp(pokemon->exp(), Pokemon::getGrowthRate(pokemon->speciesID()));
                    pickerSel = (lvl > 0 ? lvl : 1) - 1;
                    pickerActive = true;
                } else if (f >= 10 && f <= 13) {  // Move slots 0-3
                    pickerKind = Dialogs::PickerKind::Move;
                    pickerSlot = f - 10;
                    // learnable moves first (green + top), for the mon's own game. Illegal moves are
                    // still offered (after the legal ones) and flagged by the legality check, PKHeX-style.
                    buildMovePickerOrder(pokemon->speciesID(), pokemon->form(), pokemon->getGameGroup(), pokemon->move(pickerSlot));
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                } else if (f == 14) {            // Held item
                    // Gen 3 held items are a separate, much smaller id space -- offering the modern
                    // list would both misname every entry and let an id be set that Gen 3 has no
                    // item for.
                    pickerKind = (pokemon->getGameGroup() == GameVersion::FRLG)
                               ? Dialogs::PickerKind::ItemG3 : Dialogs::PickerKind::Item;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->heldItem();
                    pickerActive = true;
                } else if (f == 15) {            // Ability
                    pickerKind = Dialogs::PickerKind::Ability;
                    // the species' legal ability slots for the mon's own game (green + top); sets
                    // pickerOrder, pickerLegalCount, pickerSel.
                    buildAbilityPickerOrder(pokemon->speciesID(), pokemon->form(),
                                            pokemon->getGameGroup(), pokemon->ability());
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                } else if (f == 16) {            // Friendship
                    pickerKind = Dialogs::PickerKind::Friendship;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->friendship();
                    pickerActive = true;
                } else if (f == 26) {            // Form (species alternate forms)
                    pickerKind = Dialogs::PickerKind::Form;
                    pickerFormSpecies = pokemon->speciesID();
                    // The MON's game group, not the save's: a bank slot holds a foreign mon, and the
                    // forms it can have are its own game's, not the open save's.
                    buildFormPickerOrder(pokemon->speciesID(), pokemon->form(),
                                         pokemon->getGameGroup());  // sets pickerOrder + pickerSel
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                } else if (f == 27) {            // Stat Nature (mint / effective-stat nature)
                    pickerKind = Dialogs::PickerKind::StatNature;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->statNature();
                    if (pickerSel >= 25) pickerSel = pokemon->nature();
                    pickerActive = true;
                } else if (f == 23) {            // Nickname (blocking text keyboard)
                    // Field width is the format's, not a constant: Gen 3 holds 10 characters, the rest 12.
                    std::string cur = Utils::utf16ToUtf8(pokemon->nickname());
                    Utils::KeyboardResult kr = Utils::promptText("Nickname", "Nickname", cur,
                                                                 pokemon->getMaxNicknameLength());
                    if (kr.accepted) {
                        if (!kr.text.empty()) {
                            const std::u16string want = Utils::utf8ToUtf16(kr.text);
                            // Refuse rather than mangle -- Gen 3's character set predates Unicode, and
                            // its encoder would end the name at the first character it can't map.
                            if (!pokemon->canStoreNickname(want)) {
                                postStatus("This game can't store one of those characters.");
                            } else {
                                pokemon->setNickname(want);
                                pokemon->setIsNicknamed(true);   // deliberate custom name
                                hasUnsavedChanges = true;
                                mirrorEditedPartyMember();
                            }
                        } else {
                            // An empty entry resets to the species default name (not nicknamed).
                            pokemon->setNickname(Utils::utf8ToUtf16(defaultNicknameFor(*pokemon)));
                            pokemon->setIsNicknamed(false);
                            hasUnsavedChanges = true;
                            mirrorEditedPartyMember();
                        }
                    }
                } else if (f == 24) {            // EXP (blocking numeric keypad)
                    uint32_t maxExp = Pokemon::getExpForLevel(100, Pokemon::getGrowthRate(pokemon->speciesID()));
                    Utils::NumberResult nr = Utils::promptNumber("EXP", static_cast<int>(pokemon->exp()),
                                                                 0, static_cast<int>(maxExp));
                    if (nr.accepted) {
                        pokemon->setExp(static_cast<uint32_t>(nr.value));
                        hasUnsavedChanges = true;
                        mirrorEditedPartyMember();
                    }
                } else if (f == 28) {            // Met Date -- ONE numeric keypad (YYYYMMDD).
                    // Chaining several swkbd launches inside a single frame crashes on hardware (a library
                    // applet can't be relaunched back-to-back), so the whole date is one field.
                    int cur = (2000 + pokemon->metYear()) * 10000
                            + (pokemon->metMonth() ? pokemon->metMonth() : 1) * 100
                            + (pokemon->metDay() ? pokemon->metDay() : 1);
                    Utils::NumberResult r = Utils::promptNumber("Met Date (YYYYMMDD)", cur, 20000101, 20991231);
                    if (r.accepted) {
                        int yy = r.value / 10000, mm = (r.value / 100) % 100, dd = r.value % 100;
                        if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) {
                            pokemon->setMetYear(static_cast<uint8_t>((yy - 2000) & 0xFF));
                            pokemon->setMetMonth(static_cast<uint8_t>(mm));
                            pokemon->setMetDay(static_cast<uint8_t>(dd));
                            hasUnsavedChanges = true; mirrorEditedPartyMember();
                        } else {
                            postStatus("Invalid date -- use YYYYMMDD.", 200);
                        }
                    }
                } else if (f == 29) {            // Egg Location (Met-location picker routed to the egg field)
                    const uint8_t ver = pokemon->originGame();
                    Names::LocationTable lt = Names::getLocationTable(ver);
                    pickerOrder.clear();
                    for (uint16_t id = 0; id < lt.count; ++id)
                        if (lt.names[id][0] != '\0') pickerOrder.push_back(id);
                    if (!pickerOrder.empty()) {
                        pickerMetVersion = ver;
                        pickerMetIsEgg = true;   // this picker targets the egg-met location
                        pickerKind = Dialogs::PickerKind::MetLocation;
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0;
                        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
                            if (pickerOrder[i] == pokemon->eggLocation()) { pickerSel = i; break; }
                        pickerActive = true;
                    } else {
                        postStatus("No met-location list for this game.", 200);
                    }
                } else if (f == 30) {            // Egg Date -- ONE numeric keypad (YYYYMMDD), see Met Date.
                    int cur = (2000 + pokemon->eggYear()) * 10000
                            + (pokemon->eggMonth() ? pokemon->eggMonth() : 1) * 100
                            + (pokemon->eggDay() ? pokemon->eggDay() : 1);
                    Utils::NumberResult r = Utils::promptNumber("Egg Date (YYYYMMDD)", cur, 20000101, 20991231);
                    if (r.accepted) {
                        int yy = r.value / 10000, mm = (r.value / 100) % 100, dd = r.value % 100;
                        if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) {
                            pokemon->setEggYear(static_cast<uint8_t>((yy - 2000) & 0xFF));
                            pokemon->setEggMonth(static_cast<uint8_t>(mm));
                            pokemon->setEggDay(static_cast<uint8_t>(dd));
                            hasUnsavedChanges = true; mirrorEditedPartyMember();
                        } else {
                            postStatus("Invalid date -- use YYYYMMDD.", 200);
                        }
                    }
                } else if (f == 31) {            // Fateful Encounter (toggle)
                    pokemon->setFatefulEncounter(!pokemon->isFatefulEncounter());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                } else if (f == 17) {            // Egg (toggle not-egg <-> egg)
                    pokemon->setEgg(!pokemon->isEgg());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                } else if (f == 18) {            // Met Level (1-100 picker)
                    pickerKind = Dialogs::PickerKind::MetLevel;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = (pokemon->metLevel() > 0 ? pokemon->metLevel() : 1) - 1;
                    pickerActive = true;
                } else if (f == 19) {            // Ball (per-game list -- PLA uses the Hisui balls)
                    pickerKind = Dialogs::PickerKind::Ball;
                    pickerOrder.clear();
                    for (uint8_t b : Enums::getBallList(pokemon->getGameGroup())) pickerOrder.push_back(b);
                    pickerLegalCount = 0;   // no green "legal" prefix for balls
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerSel = 0;          // land on the mon's current ball if it's in the list
                    for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
                        if (pickerOrder[i] == pokemon->ball()) { pickerSel = i; break; }
                    pickerActive = true;
                } else if (f == 20) {            // Language
                    pickerKind = Dialogs::PickerKind::Language;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->language();
                    pickerActive = true;
                } else if (f == 21) {            // Origin game
                    pickerKind = Dialogs::PickerKind::Origin;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->originGame();
                    pickerActive = true;
                } else if (f == 22 && pokemon->hasPokerus()) {   // Pokerus: cycle None -> Infected -> Cured
                    uint8_t next;
                    if (pokemon->isPokerusInfected())   next = 0x10;  // Infected -> Cured (strain 1, 0 days)
                    else if (pokemon->isPokerusCured()) next = 0x00;  // Cured -> None
                    else                                next = 0x12;  // None -> Infected (strain 1, 2 days)
                    pokemon->setPokerus(next);
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                } else if (f == 25) {            // Met Location (picker of the origin game's places)
                    const uint8_t ver = pokemon->originGame();
                    Names::LocationTable lt = Names::getLocationTable(ver);
                    pickerOrder.clear();
                    for (uint16_t id = 0; id < lt.count; ++id)
                        if (lt.names[id][0] != '\0') pickerOrder.push_back(id);   // real, non-blank places only
                    if (!pickerOrder.empty()) {
                        pickerMetVersion = ver;
                        pickerMetIsEgg = false;   // this picker targets the met location
                        pickerKind = Dialogs::PickerKind::MetLocation;
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0;   // land on the current location if it's in the list
                        for (int i = 0; i < static_cast<int>(pickerOrder.size()); ++i)
                            if (pickerOrder[i] == pokemon->metLocation()) { pickerSel = i; break; }
                        pickerActive = true;
                    } else {
                        postStatus("No met-location list for this game.", 200);
                    }
                } else if (f >= 15) {
                    // A left-column detail row with no editor (e.g. Pokerus on a game without the
                    // mechanic) -- do nothing rather than fall through to the stat editor below.
                } else {
                    const int st = f;  // 0-5 (HP, Atk, Def, SpA, SpD, Spe)
                    statEdit.selectedStat = st;
                    switch (st) {
                        case 0: statEdit.originalIV = pokemon->ivHP();  statEdit.originalEV = pokemon->evHP();  statEdit.originalAV = pokemon->avHP();  break;
                        case 1: statEdit.originalIV = pokemon->ivATK(); statEdit.originalEV = pokemon->evATK(); statEdit.originalAV = pokemon->avATK(); break;
                        case 2: statEdit.originalIV = pokemon->ivDEF(); statEdit.originalEV = pokemon->evDEF(); statEdit.originalAV = pokemon->avDEF(); break;
                        case 3: statEdit.originalIV = pokemon->ivSPA(); statEdit.originalEV = pokemon->evSPA(); statEdit.originalAV = pokemon->avSPA(); break;
                        case 4: statEdit.originalIV = pokemon->ivSPD(); statEdit.originalEV = pokemon->evSPD(); statEdit.originalAV = pokemon->avSPD(); break;
                        case 5: statEdit.originalIV = pokemon->ivSPE(); statEdit.originalEV = pokemon->evSPE(); statEdit.originalAV = pokemon->avSPE(); break;
                    }
                    statEdit.currentIV = statEdit.originalIV;
                    statEdit.currentEV = statEdit.originalEV;
                    statEdit.currentAV = statEdit.originalAV;
                    statEdit.mode = Dialogs::StatEditMode::IV;
                    statEdit.value = statEdit.currentIV;
                    statEdit.dialogActive = true;
                }
            }

            return;  // Don't process other inputs while the page is active
        }

        // Handle edit dialog
        if (itemEditDialogActive) {
            // Touch: map the step buttons to the SAME presses as the stat editor
            // (ZL/L/Left/Right/R/ZR = -/+100, -/+10, -/+1), Confirm -> A, Cancel -> B, then reuse the
            // button logic below.
            switch (touchedButtonId(touch)) {
                case 34: kDown |= HidNpadButton_ZL;    break;
                case 30: kDown |= HidNpadButton_L;     break;
                case 31: kDown |= HidNpadButton_Left;  break;
                case 32: kDown |= HidNpadButton_Right; break;
                case 33: kDown |= HidNpadButton_R;     break;
                case 35: kDown |= HidNpadButton_ZR;    break;
                case 1:  kDown |= HidNpadButton_A;     break;
                case 0:  kDown |= HidNpadButton_B;     break;
                case 40: kDown |= HidNpadButton_X;     break;  // item drop-down -> change type
                case 2:  kDown |= HidNpadButton_Y;     break;  // Remove
                default: break;
            }
            // Adjust value, bounded by what the GAME accepts for this pouch. Over-cap counts are
            // a save-corruption vector, not cosmetic: an oversized Gen 3 stack overflows the bag into
            // the key-items pocket. Left/Right = -/+1, L/R = -/+10, ZL/ZR = -/+100 -- matching the stat
            // editor (these used to be Up/Down, inconsistent between the two dialogs).
            const int itemMax = currentItemMaxCount();
            if (kDown & HidNpadButton_Left)  itemEditDialogValue = std::max(0, itemEditDialogValue - 1);
            if (kDown & HidNpadButton_Right) itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 1);
            if (kDown & HidNpadButton_L)     itemEditDialogValue = std::max(0, itemEditDialogValue - 10);
            if (kDown & HidNpadButton_R)     itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 10);
            if (kDown & HidNpadButton_ZL)    itemEditDialogValue = std::max(0, itemEditDialogValue - 100);
            if (kDown & HidNpadButton_ZR)    itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 100);
            // A save edited elsewhere (or by an older PKSE) can hold an over-cap count; clamp on
            // entry so opening the dialog can only ever move it back into range, never past it.
            itemEditDialogValue = std::clamp(itemEditDialogValue, 0, itemMax);

            // X: change this item's TYPE. Open the pouch picker filtered to legal ids the bag doesn't
            // already hold; the pick reassigns this slot (see the picker's replace-confirm handler).
            if (kDown & HidNpadButton_X) {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    const auto& pouch = trainer.items[selectedCategory];
                    const auto legal = Names::getPouchItems(trainer.getGameGroup(), selectedCategory);
                    pickerOrder.clear();
                    for (uint16_t id : legal) {
                        const auto e = std::find_if(pouch.begin(), pouch.end(),
                            [id](const Trainer::InventoryItem& v) { return v.itemId == id; });
                        if (e == pouch.end() || e->count == 0) pickerOrder.push_back(id);   // not currently held
                    }
                    if (!pickerOrder.empty()) {
                        itemPickerReplace = true;
                        pickerKind = (trainer.getGameGroup() == GameVersion::FRLG)
                                   ? Dialogs::PickerKind::PouchItemG3 : Dialogs::PickerKind::PouchItem;
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0;
                        pickerActive = true;
                        itemEditDialogActive = false;   // picker takes over; the change applies on pick
                    } else {
                        postStatus("No other item types available in this pocket.", 240);
                    }
                }
                return;
            }

            // Y: remove this item from the pouch.
            if (kDown & HidNpadButton_Y) {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    auto& pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size())) {
                        const int rawIdx = visible[selectedItemIndex];
                        if (g_debugLogging) {
                            Utils::logEventToFile(
                                std::string("ITEM action=REMOVE ")
                                + Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory))
                                + " " + Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup()))
                                + " id=" + std::to_string(pouch[rawIdx].itemId)
                                + " count=" + std::to_string(pouch[rawIdx].count)
                                + " mode=" + (trainer.itemsAreIdIndexed() ? "zeroed" : "erased")
                                + " via=dialog");
                        }
                        if (trainer.itemsAreIdIndexed()) {
                            pouch[rawIdx].count = 0;   // keep the entry so its id-slot is written to 0
                        } else {
                            pouch.erase(pouch.begin() + rawIdx);   // slot-based: erase; region rewritten
                        }
                        hasUnsavedChanges = true;
                        const int vis = static_cast<int>(visibleItemIndices().size());
                        if (selectedItemIndex >= vis) selectedItemIndex = std::max(0, vis - 1);
                        postStatus("Item removed.", 200);
                    }
                }
                itemEditDialogActive = false;
                return;
            }

            // Confirm edit
            if (kDown & HidNpadButton_A) {
                // Save the new value to the item (map the visible selection to the raw pouch slot).
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    auto& pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size())) {
                        const int rawIdx = visible[selectedItemIndex];
                        // Confirming a count of 0 IS a removal. On a slot-based game erase the entry, or
                        // updateItemBlock would write a {itemId, 0} ghost slot; on an id-indexed game
                        // keep the entry at count 0 so its id-slot is written to 0.
                        if (g_debugLogging && itemEditDialogValue != itemEditDialogOriginalValue) {
                            const bool removing = (itemEditDialogValue == 0);
                            Utils::logEventToFile(
                                std::string("ITEM action=") + (removing ? "REMOVE" : "SETCOUNT") + " "
                                + Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory))
                                + " " + Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup()))
                                + " id=" + std::to_string(pouch[rawIdx].itemId)
                                + " from=" + std::to_string(itemEditDialogOriginalValue)
                                + " to=" + std::to_string(itemEditDialogValue)
                                + (removing ? (trainer.itemsAreIdIndexed() ? " mode=zeroed" : " mode=erased") : ""));
                        }
                        if (itemEditDialogValue == 0 && !trainer.itemsAreIdIndexed()) {
                            pouch.erase(pouch.begin() + rawIdx);
                        } else {
                            pouch[rawIdx].count = static_cast<uint16_t>(itemEditDialogValue);
                        }
                        if (itemEditDialogValue != itemEditDialogOriginalValue) hasUnsavedChanges = true;
                    }
                }
                itemEditDialogActive = false;
                return;
            }

            // Cancel edit
            if (kDown & HidNpadButton_B) {
                itemEditDialogActive = false;
                return;
            }

            return;  // Don't process other inputs while edit dialog is active
        }

        // Handle storage release confirmation (single slot or the whole multi-selection).
        if (releaseConfirmActive) {
            int tb = touchedButtonId(touch);
            if (tb == 1) kDown |= HidNpadButton_A;       // tap Release
            else if (tb == 0) kDown |= HidNpadButton_B;  // tap Cancel
            if (kDown & HidNpadButton_A) {
                if (releaseGroup) {
                    if (g_debugLogging) {
                        for (const auto& m : moveMon) {
                            if (m) Utils::logEventToFile("RELEASE source=hand " + Utils::describeMon(*m, trainer));
                        }
                    }
                    // The group being released is the block in hand, so releasing it is simply
                    // dropping what we are carrying -- nothing is left pointing at a stale slot.
                    moveMon.clear();
                    selectDimensions = {0, 0};
                } else {
                    auto& slot = storageSlot(releasePane, releaseBox, releaseSlot);
                    if (g_debugLogging && slot) {
                        Utils::logEventToFile("RELEASE " + Utils::logSlot("from", releasePane, releaseBox, releaseSlot)
                                              + " " + Utils::describeMon(*slot, trainer));
                    }
                    slot.reset();
                }
                hasUnsavedChanges = true;
                releaseConfirmActive = false;
                return;
            }
            if (kDown & HidNpadButton_B) { releaseConfirmActive = false; return; }
            return;
        }

        // Item removal confirm: X in the Items list asks before deleting. A = Remove, B = Cancel.
        // Mirrors the storage release confirm; the delete matches the Edit Item dialog's Y-remove.
        if (itemRemoveConfirmActive) {
            int tb = touchedButtonId(touch);
            if (tb == 1) kDown |= HidNpadButton_A;       // tap Remove
            else if (tb == 0) kDown |= HidNpadButton_B;  // tap Cancel
            if (kDown & HidNpadButton_A) {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    auto& pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size())) {
                        const int rawIdx = visible[selectedItemIndex];
                        if (g_debugLogging) {
                            Utils::logEventToFile(
                                std::string("ITEM action=REMOVE ")
                                + Utils::logField("pouch", Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory))
                                + " " + Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup()))
                                + " id=" + std::to_string(pouch[rawIdx].itemId)
                                + " count=" + std::to_string(pouch[rawIdx].count)
                                + " mode=" + (trainer.itemsAreIdIndexed() ? "zeroed" : "erased"));
                        }
                        if (trainer.itemsAreIdIndexed()) pouch[rawIdx].count = 0;   // keep entry; id-slot written to 0
                        else pouch.erase(pouch.begin() + rawIdx);                   // slot-based: erase; region rewritten
                        hasUnsavedChanges = true;
                        const int vis = static_cast<int>(visibleItemIndices().size());
                        if (selectedItemIndex >= vis) selectedItemIndex = std::max(0, vis - 1);
                        postStatus("Item removed.", 200);
                    }
                }
                itemRemoveConfirmActive = false;
                return;
            }
            if (kDown & HidNpadButton_B) { itemRemoveConfirmActive = false; return; }
            return;
        }

        // Lossy-move acknowledgement. The two warnings differ only in what they SAY -- both guard the
        // same pending action and answer the same question -- so the input handling is shared here
        // deliberately, while the notices themselves are separate dialogs. Continue runs the stashed
        // action; Cancel leaves everything as it was.
        if (moveConfirmActive()) {
            int tb = touchedButtonId(touch);
            if (tb == 1) kDown |= HidNpadButton_A;        // tap Continue
            else if (tb == 0) kDown |= HidNpadButton_B;   // tap Cancel
            if (kDown & (HidNpadButton_A | HidNpadButton_B)) {
                const bool go = (kDown & HidNpadButton_A) != 0;   // A always continues
                gen3ConvertConfirmActive = false;
                lgpeTransferConfirmActive = false;
                if (go && pendingMove == PendingMove::PlaceHeld) {
                    // Re-point the cursor at the slot the drop was aimed at, then run the ordinary
                    // put-down -- one placement path, so the confirmed move behaves identically to
                    // the unconfirmed one (bounds, block swap, per-cell conversion and all).
                    storageFocusPane = pendingMovePane;
                    if (pendingMovePane == 0) { stSaveBox = pendingMoveBox; stSaveSlot = pendingMoveSlot; }
                    else                      { stBankBox = pendingMoveBox; stBankSlot = pendingMoveSlot; }
                    putDownBlock();
                }
                pendingMove = PendingMove::None;
            }
            return;
        }

        // Handle the red-mode per-Pokemon action menu (Move / Edit / Release / Cancel).
        if (storageMenuActive) {
            constexpr int N = 5;
            int tb = touchedButtonId(touch);
            if (tb >= 0) { storageMenuIndex = tb; kDown |= HidNpadButton_A; }  // tap a row = select + confirm
            // On a party-linked (locked) slot, Move (0) and Release (3) are disabled -- skip them so
            // Up/Down never land on a greyed row, and guard the actions below in case a tap slips in.
            const bool menuLocked = storageSlotLocked(menuPane, menuBox, menuSlot);
            auto menuDisabled = [&](int i) { return menuLocked && (i == 0 || i == 3); };
            if (kDown & HidNpadButton_Up)   do { storageMenuIndex = (storageMenuIndex - 1 + N) % N; } while (menuDisabled(storageMenuIndex));
            if (kDown & HidNpadButton_Down) do { storageMenuIndex = (storageMenuIndex + 1) % N; } while (menuDisabled(storageMenuIndex));
            if (kDown & HidNpadButton_B) { storageMenuActive = false; return; }
            if (kDown & HidNpadButton_A) {
                storageMenuActive = false;
                switch (storageMenuIndex) {
                    case 0:  // Move -> pick up as a 1x1 block (blocked on a party-linked slot)
                        if (menuLocked) break;
                        storageFocusPane = menuPane;
                        if (menuPane == 0) { stSaveBox = menuBox; stSaveSlot = menuSlot; }
                        else               { stBankBox = menuBox; stBankSlot = menuSlot; }
                        pickupSingle();
                        postPickup();
                        break;
                    case 1:  // Edit -> open the details modal
                        openStorageEditor(menuPane, menuBox, menuSlot);
                        break;
                    case 2:  // Clone -> duplicate into the first empty slot (this box first, then ANY box)
                        if (const Pokemon::Pokemon* src = storageSlot(menuPane, menuBox, menuSlot).get()) {
                            if (auto copy = src->clone()) {
                                const int slots = menuPane == 0 ? static_cast<int>(trainer.getSlotsPerBox())
                                                                : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
                                const int boxes = menuPane == 0 ? static_cast<int>(trainer.getBoxCount())
                                                                : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
                                // Search the mon's own box first, then spill into any other box. A full
                                // box must NOT eat the clone -- on Let's Go's gapless list every box but
                                // the last is full, so a single-box search silently dropped the copy (a
                                // party mon in box 0 could never be cloned).
                                bool placed = false;
                                for (int bo = 0; bo < boxes && !placed; ++bo) {
                                    const int box = (menuBox + bo) % boxes;
                                    for (int s = 0; s < slots && !placed; ++s) {
                                        auto& dst = storageSlot(menuPane, box, s);
                                        if ((!dst || dst->speciesID() == 0) && !storageSlotLocked(menuPane, box, s)) {
                                            dst = std::move(copy);
                                            hasUnsavedChanges = true;
                                            placed = true;
                                            // A clone puts a SECOND record with the same PID/EC into
                                            // the save, which is exactly what a later "duplicate
                                            // Pokemon" or failed-legality report is about -- so log
                                            // where it came from and where it landed.
                                            if (g_debugLogging) {
                                                Utils::logEventToFile(
                                                    "CLONE result=OK " + Utils::logSlot("from", menuPane, menuBox, menuSlot)
                                                    + " " + Utils::logSlot("to", menuPane, box, s)
                                                    + " " + Utils::describeMon(*dst, trainer));
                                            }
                                        }
                                    }
                                }
                                if (!placed) {
                                    postStatus("No free slot to clone into.", 240);
                                    if (g_debugLogging) {
                                        Utils::logEventToFile("CLONE result=REFUSED reason=no-free-slot "
                                                              + Utils::logSlot("from", menuPane, menuBox, menuSlot)
                                                              + " " + Utils::briefMon(*src));
                                    }
                                }
                            }
                        }
                        break;
                    case 3:  // Release -> confirm (blocked on a party-linked slot)
                        if (menuLocked) break;
                        releaseGroup = false;
                        releasePane = menuPane; releaseBox = menuBox; releaseSlot = menuSlot;
                        releaseConfirmActive = true;
                        break;
                    default: break;  // Cancel
                }
                return;
            }
            return;
        }

        // Handle the carried-block menu, opened with Minus (Release all / Return / Cancel). Moving a
        // block no longer needs a menu entry -- you carry it to where you want it and press A.
        if (groupMenuActive) {
            constexpr int N = 3;
            int tb = touchedButtonId(touch);
            if (tb >= 0) { groupMenuIndex = tb; kDown |= HidNpadButton_A; }  // tap a row = select + confirm
            if (kDown & HidNpadButton_Up)   groupMenuIndex = (groupMenuIndex - 1 + N) % N;
            if (kDown & HidNpadButton_Down) groupMenuIndex = (groupMenuIndex + 1) % N;
            if (kDown & HidNpadButton_B) { groupMenuActive = false; return; }
            if (kDown & HidNpadButton_A) {
                groupMenuActive = false;
                switch (groupMenuIndex) {
                    case 0: releaseGroup = true; releaseConfirmActive = true; break;  // Release all
                    case 1: returnHeldToOrigin(); break;                              // Put it all back
                    default: break;  // Cancel
                }
                return;
            }
            return;
        }

        // Handle the storage-exit Save / Discard / Cancel prompt (bank has unsaved changes).
        if (storageExitConfirmActive) {
            constexpr int N = 3;
            int tb = touchedButtonId(touch);
            if (tb >= 0) { storageExitConfirmIndex = tb; kDown |= HidNpadButton_A; }  // tap a row = select + confirm
            if (kDown & HidNpadButton_Up)   storageExitConfirmIndex = (storageExitConfirmIndex - 1 + N) % N;
            if (kDown & HidNpadButton_Down) storageExitConfirmIndex = (storageExitConfirmIndex + 1) % N;
            // Cancel: stay, and call off any app exit that raised this (they backed out of leaving too).
            if (kDown & HidNpadButton_B) {
                storageExitConfirmActive = false;
                exitAfterBankChoice = false;
                return;
            }
            if (kDown & HidNpadButton_A) {
                storageExitConfirmActive = false;
                // Claimed up front, so a branch that bails out (a failed bank write) can't leave a
                // stale "and then exit" hanging over the next visit to this prompt.
                const bool resumeExit = exitAfterBankChoice;
                exitAfterBankChoice = false;
                bool answered = false;   // Save or Discard actually went through (Cancel did not)
                switch (storageExitConfirmIndex) {
                    case 0:  // Save & Exit
                        // Same rule as the app-exit path: a failed bank write must not be followed by
                        // leaving the view, or the deposits vanish with no indication.
                        if (bank && !bank->save()) {
                            postStatus("Couldn't save the bank - staying in storage so nothing is lost.", 480);
                            Utils::logEventToFile("BANK action=SAVE result=FAILED");
                            return;
                        }
                        Utils::logTest("BANKSAVE result=OK verifyfail=" +
                                       std::to_string(bank ? bank->lastVerifyFailures() : 0));
                        Utils::logEventToFile("BANK action=SAVE result=OK verifyfail="
                                              + std::to_string(bank ? bank->lastVerifyFailures() : 0));
                        // Written, but the round-trip check found slots that don't reproduce. That
                        // is a PKSE bug rather than a write failure, so it warns instead of blocking.
                        if (bank && bank->lastVerifyFailures() > 0) {
                            postStatus(std::to_string(bank->lastVerifyFailures()) +
                                       " bank slot(s) failed the integrity check - see the PKSE log.", 480);
                        }
                        detailViewActive = false;
                        answered = true;
                        break;
                    case 1:  // Discard & Exit -> revert the in-memory bank to its on-disk state.
                             // ONLY the bank: it owns what lives in the bank, not what lives in the
                             // save. Pulling a Pokemon out and then discarding therefore leaves the
                             // copy in the save box -- intended, and how a HOME-style box works. Undoing that half
                             // is the GAME save's own discard, which is a separate decision.
                        Utils::logEventToFile("BANK action=DISCARD result=OK scope=bank-only"
                                              " note=withdrawn-copies-remain-in-save");
                        if (bank) bank->load();
                        detailViewActive = false;
                        answered = true;
                        break;
                    default: break;  // Cancel: stay in the storage view
                }
                // Raised by + rather than by B: the bank has had its answer, so carry on out of the
                // app. Cancel leaves `answered` false and simply stays.
                if (resumeExit && answered) beginAppExit();
                return;
            }
            return;
        }

        // Handle detail view exit
        if (detailViewActive) {
            if (kDown & HidNpadButton_B) {
                if (selectedMode == ViewMode::Storage) {
                    // B unwinds one step: abandon a rectangle being drawn, then put a carried block
                    // back where it came from, then leave the view.
                    if (currentlySelecting) { cancelSelection(); return; }
                    if (carrying()) { returnHeldToOrigin(); return; }
                    // Leaving the storage view: if the bank has unsaved changes, prompt Save / Discard /
                    // Cancel. No changes -> just exit.
                    // The bank is its own entity, saved here rather than with the game (X) save.
                    if (bank && bank->hasChanged()) {
                        storageExitConfirmActive = true;
                        storageExitConfirmIndex = 0;
                        return;
                    }
                    detailViewActive = false;
                    return;
                }
                if (swapActive) {
                    swapActive = false;  // cancel an in-progress grab rather than leaving the grid
                    return;
                }
                // Exit detail view (B button only)
                detailViewActive = false;
                selectedItemIndex = 0;
                currentPage = 0;
                return;
            }

            // Storage view: dual-pane bank navigation + deposit/withdraw.
            if (selectedMode == ViewMode::Storage) {
                // Touch drag-select (Multi mode): press anchors the rectangle, sliding a finger over
                // the grid rubber-bands it, lifting grabs the block. The same gesture the D-pad does
                // with A / move / A -- so a rectangle can be swept out in one motion.
                //
                // Only the anchor's own pane and box take part: a rectangle is a region of ONE box,
                // and the cursor slot is what the highlight and the grab both read.
                auto slotUnderFinger = [&](int& outPane, int& outSlot) {
                    for (const auto& t : storageTouchTargets) {
                        if (t.slot < 0) continue;                      // header arrows / name pill
                        if (touch.x() >= t.x && touch.x() < t.x + t.w &&
                            touch.y() >= t.y && touch.y() < t.y + t.h) {
                            outPane = t.pane; outSlot = t.slot; return true;
                        }
                    }
                    return false;
                };
                if (currentlySelecting && touch.isDown() && !touch.justPressed()) {
                    int tp = 0, ts = 0;
                    if (slotUnderFinger(tp, ts) && tp == selectPane) {
                        if (tp == 0) stSaveSlot = ts; else stBankSlot = ts;
                    }
                }
                if (currentlySelecting && touch.justReleased() && touch.dragged()) {
                    grabSelection(true);     // a swept-out rectangle grabs on lift
                    return;
                }

                // A tap on a slot moves the cursor there and acts like pressing A, so it works in
                // every cursor mode (Menu opens the popup, Move picks up/places, Multi anchors then
                // grabs). Rects were captured during the previous frame's draw.
                if (touch.justPressed()) {
                    for (const auto& t : storageTouchTargets) {
                        if (touch.x() >= t.x && touch.x() < t.x + t.w &&
                            touch.y() >= t.y && touch.y() < t.y + t.h) {
                            storageFocusPane = t.pane;
                            if (t.slot == -2)      kDown |= HidNpadButton_L;  // ◀ prev box
                            else if (t.slot == -3) kDown |= HidNpadButton_R;  // ▶ next box
                            else if (t.slot == -4) {                          // tapped the name pill -> rename
                                if (t.pane == 1 || trainer.supportsBoxNames()) {
                                    if (t.pane == 0) stSaveSlot = -1; else stBankSlot = -1;  // focus header now
                                    pendingHeaderRename = true;               // open the rename next frame
                                }
                            }
                            else {
                                if (t.pane == 0) stSaveSlot = t.slot; else stBankSlot = t.slot;
                                kDown |= HidNpadButton_A;
                            }
                            break;
                        }
                    }
                }
                handleStorageInput(kDown);
                return;
            }

            // Handle detail view navigation
            if (selectedMode == ViewMode::Items) {
                // Get current pouch size for bounds checking
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size())) {
                    const auto& pouch = trainer.items[selectedCategory];
                    // Navigate/edit only the visible items (count > 0); selectedItemIndex indexes
                    // this filtered view, mapped back to the raw pouch via visible[...] on edit.
                    std::vector<int> visible = visibleItemIndices();
                    // Calculate items per column based on panel height
                    int itemsPerPage = (CONTENT_PANEL_HEIGHT - 106) / 52;  // rows that fit (mirror ItemsPanel)
                    if (itemsPerPage < 1) itemsPerPage = 1;
                    int totalItems = static_cast<int>(visible.size());
                    int totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
                    // Clamp BOTH the selection and the page to the current item set. Removing every
                    // item on the last page shrinks totalItems/totalPages; without re-clamping here the
                    // cursor is stranded on an out-of-range page ("6/5") until the pouch/tab changes or
                    // the view is re-entered (E8). Re-derive currentPage from the clamped selection so
                    // the two stay consistent no matter how the item left (list remove, dialog remove,
                    // or an edit to count 0).
                    if (totalItems > 0) {
                        if (selectedItemIndex >= totalItems) selectedItemIndex = totalItems - 1;
                        if (selectedItemIndex < 0) selectedItemIndex = 0;
                        currentPage = selectedItemIndex / itemsPerPage;
                    } else {
                        selectedItemIndex = 0;
                        currentPage = 0;
                    }

                    // Touch: tap a row to select + edit it.
                    int itemTap = touchedButtonId(touch);
                    if (itemTap >= 0 && itemTap < totalItems) {
                        selectedItemIndex = itemTap;
                        currentPage = selectedItemIndex / itemsPerPage;
                        kDown |= HidNpadButton_A;
                    }

                    // Up/Down to select items
                    if ((kDown & HidNpadButton_Up) && totalItems > 0) {
                        selectedItemIndex = (selectedItemIndex - 1 + totalItems) % totalItems;
                        // Adjust page if needed
                        currentPage = selectedItemIndex / itemsPerPage;
                    }
                    if ((kDown & HidNpadButton_Down) && totalItems > 0) {
                        selectedItemIndex = (selectedItemIndex + 1) % totalItems;
                        // Adjust page if needed
                        currentPage = selectedItemIndex / itemsPerPage;
                    }

                    // Left/Right page through the single-column list.
                    if ((kDown & HidNpadButton_Right) && totalPages > 1) {
                        currentPage = (currentPage + 1) % totalPages;
                        selectedItemIndex = std::min(currentPage * itemsPerPage, totalItems - 1);
                    }
                    if ((kDown & HidNpadButton_Left) && totalPages > 1) {
                        currentPage = (currentPage - 1 + totalPages) % totalPages;
                        selectedItemIndex = std::min(currentPage * itemsPerPage, totalItems - 1);
                    }

                    // A button to edit item amount (map the visible selection back to the raw pouch slot)
                    if (kDown & HidNpadButton_A) {
                        if (selectedItemIndex >= 0 && selectedItemIndex < totalItems) {
                            int rawIdx = visible[selectedItemIndex];
                            // Open edit dialog for selected item
                            itemEditDialogActive = true;
                            itemEditDialogValue = pouch[rawIdx].count;
                            itemEditDialogOriginalValue = pouch[rawIdx].count;
                        }
                    }

                    // Y opens the add-item picker: the ids that legally belong in THIS pouch,
                    // minus what the bag already holds. Nothing is offered for a pouch the game
                    // doesn't have, or when appending isn't supported and every legal id is present.
                    if (kDown & HidNpadButton_Y) {
                        const auto legal = Names::getPouchItems(trainer.getGameGroup(), selectedCategory);
                        const bool canAppend = static_cast<int>(pouch.size()) < currentPouchCapacity();
                        pickerOrder.clear();
                        bool blockedByCapacity = false;   // a new item exists to add, but the pouch can't take it
                        for (uint16_t id : legal) {
                            const auto e = std::find_if(pouch.begin(), pouch.end(),
                                [id](const Trainer::InventoryItem& x) { return x.itemId == id; });
                            if (e != pouch.end()) {
                                if (e->count == 0) pickerOrder.push_back(id);   // present but empty -> settable
                            } else if (canAppend) {
                                pickerOrder.push_back(id);                      // genuinely new -> needs a slot
                            } else {
                                blockedByCapacity = true;
                            }
                        }
                        if (!pickerOrder.empty()) {
                            pickerKind = (trainer.getGameGroup() == GameVersion::FRLG)
                                       ? Dialogs::PickerKind::PouchItemG3
                                       : Dialogs::PickerKind::PouchItem;
                            pickerCount = static_cast<int>(pickerOrder.size());
                            pickerSel = 0;
                            pickerActive = true;
                        } else if (blockedByCapacity) {
                            // A slot-based pocket (FRLG / GG / SWSH / PLA) that's full -- say so rather
                            // than a silent no-op. The id-indexed games never hit this (entries preexist).
                            postStatus("This pocket is full.", 300);
                        } else {
                            postStatus("You already have every item this pocket can hold.", 300);
                        }
                        return;
                    }

                    // X asks to remove the selected item, behind a confirm dialog. The delete
                    // runs in the itemRemoveConfirmActive handler on A; here we just open the prompt.
                    if ((kDown & HidNpadButton_X) && totalItems > 0
                        && selectedItemIndex >= 0 && selectedItemIndex < totalItems) {
                        itemRemoveConfirmActive = true;
                        return;
                    }

                    // L/R to change categories (but not when in edit dialog)
                    if (kDown & HidNpadButton_L) {
                        switch(trainer.getGameGroup()) {
                            case GameVersion::ZA: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_LZA) % POUCH_COUNT9_LZA;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::SV: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_SV) % POUCH_COUNT9_SV;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::PLA: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT8_LA) % POUCH_COUNT8_LA;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::BDSP: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT8BDSP) % POUCH_COUNT8BDSP;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::SWSH: {
                                selectedCategory = (selectedCategory - 1 + static_cast<int>(Trainer::PouchType8SWSH::Count)) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::GG: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT7_LGPE) % POUCH_COUNT7_LGPE;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::FRLG: {
                                selectedCategory = (selectedCategory - 1 + POUCH_COUNT3_FRLG) % POUCH_COUNT3_FRLG;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            default: break;
                        }
                    }
                    if (kDown & HidNpadButton_R) {
                        switch(trainer.getGameGroup()) {
                            case GameVersion::ZA: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_LZA;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::SV: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_SV;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::PLA: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT8_LA;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::BDSP: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT8BDSP;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::SWSH: {
                                selectedCategory = (selectedCategory + 1) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::GG: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT7_LGPE;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            case GameVersion::FRLG: {
                                selectedCategory = (selectedCategory + 1) % POUCH_COUNT3_FRLG;
                                currentPage = 0;
                                selectedItemIndex = 0;
                                break;
                            }
                            default: break;
                        }
                    }
                }
            }

            // Handle detail view navigation for Boxes mode
            if (selectedMode == ViewMode::Boxes) {
                // Calculate grid layout based on game (LGPE: 5x5=25, others: 6x5=30)
                const int slotsPerBox = static_cast<int>(trainer.getSlotsPerBox());
                const int GRID_COLS = (slotsPerBox == 25) ? 5 : 6;  // LGPE uses 5x5, others use 6x5
                const int GRID_ROWS = 5;

                // selectedItemIndex == -1 is the "box-name header focused" state: navigate up to it
                // (or tap it) and press A to rename the box. Only where the game stores box names
                // (LGPE does not) and no swap is in progress; otherwise it keeps the top<->bottom wrap.
                const bool canFocusHeader = trainer.supportsBoxNames() && !swapActive;

                // Up/Down/Left/Right to navigate the grid (and to/from the header)
                if (kDown & HidNpadButton_Up) {
                    if (selectedItemIndex == -1) {
                        selectedItemIndex = (GRID_ROWS - 1) * GRID_COLS;          // header -> bottom row
                    } else {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        int currentCol = selectedItemIndex % GRID_COLS;
                        if (currentRow > 0)      selectedItemIndex -= GRID_COLS;
                        else if (canFocusHeader) selectedItemIndex = -1;          // top row -> header
                        else                     selectedItemIndex = (GRID_ROWS - 1) * GRID_COLS + currentCol;
                    }
                }
                if (kDown & HidNpadButton_Down) {
                    if (selectedItemIndex == -1) {
                        selectedItemIndex = 0;                                     // header -> top-left
                    } else {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        int currentCol = selectedItemIndex % GRID_COLS;
                        if (currentRow < GRID_ROWS - 1) selectedItemIndex += GRID_COLS;
                        else if (canFocusHeader)        selectedItemIndex = -1;    // bottom row -> header
                        else                            selectedItemIndex = currentCol;
                    }
                }
                if (kDown & HidNpadButton_Left) {
                    if (selectedItemIndex == -1) {                                // on the header: ◀ cycles box
                        int boxCount = static_cast<int>(trainer.getBoxCount());
                        selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                    } else if (selectedItemIndex % GRID_COLS > 0) {
                        selectedItemIndex--;
                    } else {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        selectedItemIndex = currentRow * GRID_COLS + (GRID_COLS - 1);
                    }
                }
                if (kDown & HidNpadButton_Right) {
                    if (selectedItemIndex == -1) {                                // on the header: ▶ cycles box
                        int boxCount = static_cast<int>(trainer.getBoxCount());
                        selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                    } else if (selectedItemIndex % GRID_COLS < GRID_COLS - 1) {
                        selectedItemIndex++;
                    } else {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        selectedItemIndex = currentRow * GRID_COLS;
                    }
                }

                // L/R to change boxes
                if (kDown & HidNpadButton_L) {
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                }
                if (kDown & HidNpadButton_R) {
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                }

                // Touch: tap the ‹/› arrows to change box; tap a slot to select it, tap the
                // already-selected slot again to open its details (maps to A). Arrow/slot rects
                // are captured during the box draw (ids 1000/1001 for arrows, 0..slots-1 for cells).
                {
                    int tapped = touchedButtonId(touch);
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    if (tapped == 1000) {
                        selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                    } else if (tapped == 1001) {
                        selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                    } else if (tapped == 2000) {
                        kDown |= HidNpadButton_A;  // tapped the summary side-panel -> edit selected mon
                    } else if (tapped == 1002) {   // tapped the box-name pill -> focus header, rename next frame
                        if (trainer.supportsBoxNames() && !swapActive) {
                            selectedItemIndex = -1;         // highlight this frame; open the rename next frame
                            pendingHeaderRename = true;
                        }
                    } else if (tapped >= 0 && tapped < static_cast<int>(trainer.getSlotsPerBox())) {
                        if (selectedItemIndex == tapped) kDown |= HidNpadButton_A;  // second tap -> details
                        else selectedItemIndex = tapped;
                    }
                }

                // A button: box-name header -> rename; occupied slot -> details; empty slot -> create.
                if (kDown & HidNpadButton_A) {
                    if (selectedItemIndex == -1) {
                        renameBox(selectedBoxIndex);
                    } else if (selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                        selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS)) {
                        const auto& pokemon = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        if (pokemon && pokemon->speciesID() != 0) {   // skip empty / species-0 ghost slots
                            details.active = true;
                            details.leftScroll = 0;
                            details.source = EditSource::Box;
                            details.category = 0;
                            details.selectedStat = 0;
                            details.selectedField = 0;
                            details.hexMode = 0;
                            details.editing = false;
                            snapshotEditTarget();   // dirty-check baseline for Save/Discard on close
                        } else if (!storageSlotLocked(0, selectedBoxIndex, selectedItemIndex) && !swapActive) {
                            // Empty slot -> create a new Pokemon here, the same flow as the Storage view:
                            // pick a species, edit it in the details modal, Keep/Discard on exit. The
                            // creator's species-pick handler drops it into boxes[box][slot] via
                            // storageSlot(0,...) and opens the editor with EditSource::Box.
                            creator.active = true;
                            creator.pane = 0; creator.box = selectedBoxIndex; creator.slot = selectedItemIndex;
                            pickerKind = Dialogs::PickerKind::Species;
                            buildCreatorSpeciesOrder();   // only species this game can hold (unless illegal-values on)
                            pickerCount = static_cast<int>(pickerOrder.size());
                            pickerSel = 0;
                            pickerActive = true;
                        }
                    }
                }

                // X button: release the mon under the cursor, after a confirm. Blocked on a party-linked
                // slot (LGPE) -- releasing it would orphan the party pointer, the same rule the Storage
                // release menu enforces (D8). Reuses the global release-confirm flow (pane 0 = save box).
                // Skipped mid-swap -- while holding a grabbed mon, B cancels and Y drops.
                if ((kDown & HidNpadButton_X) && !swapActive) {
                    const bool inRange = selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                                         selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS);
                    if (inRange) {
                        const auto& pk = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        if (pk && pk->speciesID() != 0) {
                            if (storageSlotLocked(0, selectedBoxIndex, selectedItemIndex)) {
                                postStatus("That Pokémon is a party member — release it from the party first.", 240);
                            } else {
                                releaseGroup = false;
                                releasePane = 0; releaseBox = selectedBoxIndex; releaseSlot = selectedItemIndex;
                                releaseConfirmActive = true;
                            }
                        }
                    }
                }

                // Y button: grab the slot under the cursor, then Y on another occupied slot
                // to swap them (Phase 3.1). Same-slot Y cancels the grab.
                if (kDown & HidNpadButton_Y) {
                    bool inRange = selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                                   selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS);
                    bool cursorOccupied = inRange && trainer.boxes[selectedBoxIndex][selectedItemIndex] != nullptr;

                    if (!swapActive) {
                        if (cursorOccupied) {
                            swapActive = true;
                            swapSourceBox = selectedBoxIndex;
                            swapSourceSlot = selectedItemIndex;
                        }
                    } else if (swapSourceBox == selectedBoxIndex && swapSourceSlot == selectedItemIndex) {
                        swapActive = false;  // grabbed same slot again -> cancel
                    } else {
                        // Occupied target -> swap; empty target -> move (swapping with an empty
                        // slot IS a move). LGPE storage is compacted on save so any gap left by a
                        // move is removed and the party/starter pointers are remapped.
                        (void)cursorOccupied;
                        trainer.swapBoxSlots(swapSourceBox, swapSourceSlot, selectedBoxIndex, selectedItemIndex);
                        hasUnsavedChanges = true;
                        swapActive = false;
                    }
                }
            }

            // Handle detail view navigation for Party mode
            if (selectedMode == ViewMode::Party) {
                constexpr int COLUMN_SIZE = 3;

                // Determine current column (0 = left, 1 = right)
                int currentColumn = (selectedPartyIndex >= COLUMN_SIZE) ? 1 : 0;
                int rowInColumn = selectedPartyIndex % COLUMN_SIZE;

                // Up/Down to navigate within current column
                if (kDown & HidNpadButton_Up) {
                    rowInColumn = (rowInColumn - 1 + COLUMN_SIZE) % COLUMN_SIZE;
                    selectedPartyIndex = currentColumn * COLUMN_SIZE + rowInColumn;
                }
                if (kDown & HidNpadButton_Down) {
                    rowInColumn = (rowInColumn + 1) % COLUMN_SIZE;
                    selectedPartyIndex = currentColumn * COLUMN_SIZE + rowInColumn;
                }

                // Left/Right to move between columns
                if (kDown & HidNpadButton_Left) {
                    if (currentColumn == 1) {
                        // Move from right column to left column, same row
                        selectedPartyIndex = rowInColumn;
                    }
                }
                if (kDown & HidNpadButton_Right) {
                    if (currentColumn == 0) {
                        // Move from left column to right column, same row
                        selectedPartyIndex = COLUMN_SIZE + rowInColumn;
                    }
                }

                // A button to view details
                if (kDown & HidNpadButton_A) {
                    // Only open if there's a pokemon in the selected slot
                    if (selectedPartyIndex >= 0 && selectedPartyIndex < static_cast<int>(trainer.party.size())) {
                        const Pokemon::Pokemon* pokemon = trainer.party[selectedPartyIndex].get();
                        if (pokemon && pokemon->speciesID() != 0) {  // Not empty
                            details.active = true;
                            details.leftScroll = 0;
                            details.source = EditSource::Party;
                            details.partyIndex = selectedPartyIndex;
                            details.category = 0;
                            details.selectedStat = 0;
                            details.selectedField = 0;
                            details.hexMode = 0;
                            details.editing = false;
                            snapshotEditTarget();   // dirty-check baseline for Save/Discard on close
                        }
                    }
                }
            }

            // Trainer info view: Name (0) / Money (1). Gender is read-only and lives with the
            // identity rows below, so it is not in this list at all. Both rows defer one frame so
            // the row highlight draws before the blocking swkbd opens.
            if (selectedMode == ViewMode::Trainer) {
                constexpr int kEditRows = 2;
                int tb = touchedButtonId(touch);
                if (tb >= 0 && tb < kEditRows) { trainerSelectedRow = tb; kDown |= HidNpadButton_A; }
                if (kDown & HidNpadButton_Up)   trainerSelectedRow = (trainerSelectedRow - 1 + kEditRows) % kEditRows;
                if (kDown & HidNpadButton_Down) trainerSelectedRow = (trainerSelectedRow + 1) % kEditRows;
                if (kDown & HidNpadButton_A) {
                    pendingTrainerEdit = trainerSelectedRow;   // Name (0) / Money (1): open swkbd next frame
                }
            }

            // Settings view: Up/Down select a row, A toggles it (0 = auto-backup, 1 = theme,
            // 2 = allow illegal values, 3 = Let's Go move warning, 4 = inject backups to game save,
            // 5 = debug logging). Keep this in step with drawSettingsView's labels/values.
            if (selectedMode == ViewMode::Settings) {
                constexpr int kSettingsRows = 6;
                if (kDown & HidNpadButton_Up)   settingsSelectedRow = (settingsSelectedRow - 1 + kSettingsRows) % kSettingsRows;
                if (kDown & HidNpadButton_Down) settingsSelectedRow = (settingsSelectedRow + 1) % kSettingsRows;
                int st = touchedButtonId(touch);
                if (st >= 0 && st < kSettingsRows) { settingsSelectedRow = st; kDown |= HidNpadButton_A; }
                if (kDown & HidNpadButton_A) {
                    if (settingsSelectedRow == 0)      g_autoBackupEnabled = !g_autoBackupEnabled;
                    else if (settingsSelectedRow == 1) applyTheme(g_themeMode == ThemeMode::Dark ? ThemeMode::Light : ThemeMode::Dark);
                    else if (settingsSelectedRow == 2) g_allowIllegalEdits = !g_allowIllegalEdits;
                    else if (settingsSelectedRow == 3) g_moveWarn = !g_moveWarn;
                    else if (settingsSelectedRow == 4) {
                        // The master lock for writing into the real game save. Turning it ON only
                        // makes the "Game save" destination available in the save dialog -- it never
                        // makes a save destructive on its own, so no confirmation is needed here.
                        g_injectToGameSave = !g_injectToGameSave;
                        postStatus(g_injectToGameSave
                            ? "An older backup can now be written over your live game save."
                            : "Backups can no longer be written over your live game save.", 300);
                    }
                    else {
                        // Off by default, so a normal run leaves nothing on the card. The status
                        // names the path because the whole point of the toggle is handing that file
                        // to someone else: turn it on, reproduce the problem, send the log.
                        g_debugLogging = !g_debugLogging;
                        postStatus(g_debugLogging
                            ? "Debug logging on. Logs are written to sdmc:/PKSE/logs."
                            : "Debug logging off. No new log files will be written.", 300);
                    }
                    Utils::saveSettings();   // persist the change to settings.cfg
                }
            }

            return;  // Don't process other inputs while in detail view
        }

        // Normal mode navigation (not in detail view)
        if (kDown & HidNpadButton_B) {
            // Check for unsaved changes
            if (hasUnsavedChanges && !saveConfirmActive) {
                // Prompt to save changes before going back
                exitingWithUnsavedChanges = true;
                exitingViaPlus = false;  // Exiting via B button (go back)
                saveConfirmActive = true;
                return;
            }
            // No unsaved changes or already handled, go back immediately
            goBack = true;
        }

        // HOME main menu navigation. Pills (0 Pokemon, 1 Party, 2 Storage) are a vertical column;
        // the icons (3 Items, 4 Trainer, 5 Settings) are a horizontal row below. Up/Down move the
        // column and step into/out of the row; Left/Right move within the row (matching the layout).
        if (kDown & HidNpadButton_Up) {
            if (homeMenuIndex >= 3)      homeMenuIndex = 2;   // icon row -> Storage pill
            else if (homeMenuIndex > 0)  homeMenuIndex--;     // up the pill column
        }
        if (kDown & HidNpadButton_Down) {
            if (homeMenuIndex < 2)       homeMenuIndex++;     // down the pill column
            else if (homeMenuIndex == 2) homeMenuIndex = 3;   // Storage pill -> icon row
        }
        if (homeMenuIndex >= 3) {                             // horizontal icon row
            if (kDown & HidNpadButton_Left)  homeMenuIndex = (homeMenuIndex == 3) ? 5 : homeMenuIndex - 1;
            if (kDown & HidNpadButton_Right) homeMenuIndex = (homeMenuIndex == 5) ? 3 : homeMenuIndex + 1;
            kDown &= ~(HidNpadButton_Left | HidNpadButton_Right);  // consume L/R (don't also scroll the box preview)
        }
        switch (homeMenuIndex) {
            case 0: selectedMode = ViewMode::Boxes;    break;
            case 1: selectedMode = ViewMode::Party;    break;
            case 2: selectedMode = ViewMode::Storage;  break;
            case 3: selectedMode = ViewMode::Items;    break;
            case 4: selectedMode = ViewMode::Trainer;  break;
            case 5: selectedMode = ViewMode::Settings; break;
        }

        // Touch: tapping a menu pill/icon (ids 100-105) focuses it and acts like A.
        int homeTap = touchedButtonId(touch);
        if (homeTap >= 100 && homeTap <= 105) {
            homeMenuIndex = homeTap - 100;
            switch (homeMenuIndex) {
                case 0: selectedMode = ViewMode::Boxes;    break;
                case 1: selectedMode = ViewMode::Party;    break;
                case 2: selectedMode = ViewMode::Storage;  break;
                case 3: selectedMode = ViewMode::Items;    break;
                case 4: selectedMode = ViewMode::Trainer;  break;
                case 5: selectedMode = ViewMode::Settings; break;
            }
            kDown |= HidNpadButton_A;
        }

        // A / tap: activate the focused destination (enter the mode, or toggle theme for Settings).
        if (kDown & HidNpadButton_A) {
            switch (homeMenuIndex) {
                case 0:  // Pokemon (Boxes)
                    detailViewActive = true; selectedItemIndex = 0; currentPage = 0;
                    break;
                case 1:  // Party
                    detailViewActive = true; selectedPartyIndex = 0;
                    break;
                case 2:  // Storage (bank) — start on the save pane, Menu mode, nothing held/selected.
                    detailViewActive = true; storageFocusPane = 0; stSaveSlot = 0; stBankSlot = 0;
                    moveMon.clear(); selectDimensions = {0, 0}; currentlySelecting = false;
                    storageMenuActive = false; groupMenuActive = false;
                    cursorMode = CursorMode::Menu;
                    break;
                case 3:  // Items
                    detailViewActive = true; selectedItemIndex = 0; currentPage = 0;
                    break;
                case 4:  // Trainer info
                    detailViewActive = true;
                    break;
                case 5:  // Settings screen (auto-backup + theme)
                    detailViewActive = true; settingsSelectedRow = 0;
                    break;
            }
        }

        // L/R to navigate categories (Items mode only, when not in detail view)
        if (selectedMode == ViewMode::Items) {
            if (kDown & HidNpadButton_L) {
                switch(trainer.getGameGroup()) {
                    case GameVersion::ZA: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_LZA) % POUCH_COUNT9_LZA;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::SV: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_SV) % POUCH_COUNT9_SV;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::PLA: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT8_LA) % POUCH_COUNT8_LA;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::BDSP: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT8BDSP) % POUCH_COUNT8BDSP;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::SWSH: {
                        selectedCategory = (selectedCategory - 1 + static_cast<int>(Trainer::PouchType8SWSH::Count)) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::GG: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT7_LGPE) % POUCH_COUNT7_LGPE;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::FRLG: {
                        selectedCategory = (selectedCategory - 1 + POUCH_COUNT3_FRLG) % POUCH_COUNT3_FRLG;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    default: break;
                }
            }
            if (kDown & HidNpadButton_R) {
                switch(trainer.getGameGroup()) {
                    case GameVersion::ZA: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_LZA;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::SV: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_SV;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::PLA: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT8_LA;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::BDSP: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT8BDSP;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::SWSH: {
                        selectedCategory = (selectedCategory + 1) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::GG: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT7_LGPE;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    case GameVersion::FRLG: {
                        selectedCategory = (selectedCategory + 1) % POUCH_COUNT3_FRLG;
                        currentPage = 0;
                        selectedItemIndex = 0;
                        break;
                    }
                    default: break;
                }
            }
        }

        // L/R to navigate boxes (Boxes mode only, when not in detail view)
        if (selectedMode == ViewMode::Boxes) {
            if (kDown & HidNpadButton_L) {
                int boxCount = static_cast<int>(trainer.getBoxCount());
                selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
            }
            if (kDown & HidNpadButton_R) {
                int boxCount = static_cast<int>(trainer.getBoxCount());
                selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
            }
        }
    }

    void TrainerViewScreen::draw(PKSEFramebuffer& fb) {
        fb.clear(Colors::Background);

        // --- Title bar: the shared chrome, with game name + version + DLC as the subtitle ---
        std::string subtitle = titleName;
        if (!gameVersion.empty()) {
            subtitle += "  v" + gameVersion;
        }
        if (!trainer.saveRevisionString.empty() && trainer.saveRevisionString != "Base") {
            subtitle += "  (" + trainer.saveRevisionString + ")";
        }
        drawTitleBar(fb, subtitle);

        // Not entered -> the HOME main menu (replaces the old left trainer-info + mode-selector
        // chrome). Entered -> the selected mode's content spans the full width.
        const bool entered = detailViewActive;
        if (!entered) {
            Panels::drawHomeMenu(*this, fb);
        } else {
            const int contentX = LEFT_PANEL_X;
            const int contentPanelWidth = fb.getWidth() - contentX;
            switch (selectedMode) {
                case ViewMode::Party:
                    Panels::drawPartyPokemon(fb, trainer, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT, selectedPartyIndex);
                    break;
                case ViewMode::Boxes: {
                    // HOME layout: box on the left, the summary side-panel (render + hexagon) on the right.
                    constexpr int gap = 12, summaryW = 452;
                    const int boxW = contentPanelWidth - gap - summaryW;
                    Panels::drawBoxPokemon(*this, fb, contentX, CONTENT_PANEL_Y, boxW, CONTENT_PANEL_HEIGHT);
                    Panels::drawBoxSummaryPanel(*this, fb, contentX + boxW + gap, CONTENT_PANEL_Y, summaryW, CONTENT_PANEL_HEIGHT);
                    break;
                }
                case ViewMode::Items:
                    Panels::drawItems(*this, fb, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
                    break;
                case ViewMode::Storage:
                    Panels::drawStorageView(*this, fb, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
                    break;
                case ViewMode::Trainer:
                    drawTrainerView(*this, fb, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
                    break;
                case ViewMode::Settings:
                    drawSettingsView(*this, fb, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
                    break;
            }
        }

        // Draw instructions
        std::string instructions;
        if (swapActive) {
            instructions = "HOLDING  |  Arrows: Move Cursor  |  L/R: Change Box  |  Y: Drop Here  |  B: Cancel";
        } else if (statEdit.dialogActive) {
            if (statEdit.mode != Dialogs::StatEditMode::IV) {
                instructions = "Up/Down: IV/AV-EV  |  Arrows: +/- Value  |  ZL/ZR: +/-100  |  A: Confirm  |  B: Cancel";
            } else {
                instructions = "Up/Down: IV/AV-EV  |  Arrows: +/- Value  |  A: Confirm  |  B: Cancel";
            }
        } else if (itemEditDialogActive) {
            instructions = "Left/Right: +/-1  |  Up/Down: +/-10  |  ZL/ZR: +/-100  |  A: Confirm  |  B: Cancel";
        } else if (itemRemoveConfirmActive) {
            instructions = "A: Remove  |  B: Cancel";
        } else if (saveConfirmActive) {
            instructions = "A: Save Changes  |  B: Cancel";
        } else if (releaseConfirmActive) {
            instructions = "A: Release  |  B: Cancel";
        } else if (details.active) {
            instructions = "Up/Down: Select  |  A: Edit  |  Y: Change View  |  B: Close  |  X: Save";
        } else if (detailViewActive && selectedMode == ViewMode::Settings) {
            instructions = "Up/Down: Select  |  A: Toggle  |  B: Back";
        } else if (detailViewActive) {
            if (selectedMode == ViewMode::Items) {
                instructions = "Up/Down: Select  |  A: Edit Amount  |  Y: Add Item  |  X: Remove Item  |  Left/Right: Page  |  L/R: Category  |  B: Back";
            } else if (selectedMode == ViewMode::Boxes) {
                if (details.active) {
                    if (details.category == 0) { // Main
                        instructions = details.editing
                            ? details.selectedField == 3
                                ? "Up/Down: Select Field | A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Field | B: Back  |  X: Save  |  +: Exit App"
                            : "Up/Down: Select Category | A: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                    else if (details.category == 2) { // Stats
                        instructions = details.editing
                            ? "Up/Down: Select Field  |  A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                            : "Up/Down: Select Category  |  A: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                    else {
                        instructions = "Up/Down: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                }
                else {
                    // Only advertise Details / Grab-Move when the cursor is on an occupied slot.
                    bool occ = false;
                    if (selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                        selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS)) {
                        const auto& bpk = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        occ = bpk && bpk->speciesID() != 0;
                    }
                    // On the box-name header, A renames; on a slot, the usual actions. No "navigate up
                    // to rename" hint -- it's discoverable (the pill highlights) and just adds clutter.
                    if (selectedItemIndex == -1) {
                        instructions = "L/R: Box  |  A: Rename  |  B: Back";
                    } else {
                        instructions = occ
                            ? "Arrows: Navigate  |  L/R: Box  |  A: Details  |  X: Release  |  Y: Grab/Move  |  B: Back"
                            : "Arrows: Navigate  |  L/R: Box  |  A: Create  |  B: Back";
                    }
                }
            } else if(selectedMode == ViewMode::Party) { // TODO: There HAS to be a better way of doing this without all of the if/else conditionals... probably will look into this at some point.
                if (details.active) {
                    if (details.category == 0) { // Main
                        instructions = details.editing
                            ? details.selectedField == 3
                                ? "Up/Down: Select Field | A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Field | B: Back  |  X: Save  |  +: Exit App"
                            : "Up/Down: Select Category | A: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                    else if (details.category == 2) { // Stats
                        instructions = details.editing
                            ? "Up/Down: Select Field  |  A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                            : "Up/Down: Select Category  |  A: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                    else {
                        instructions = "Up/Down: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                }
                else {
                    instructions = "Arrows: Navigate Grid  |  A: View Details  |  B: Back  |  +: Exit App";
                }
            } else if (selectedMode == ViewMode::Storage) {
                if (details.active) {
                    instructions = "Up/Down: Category  |  A: Edit  |  B: Close";
                } else if (storageExitConfirmActive) {
                    instructions = "Up/Down: Choose  |  A: Confirm  |  B: Stay";
                } else if (storageMenuActive || groupMenuActive) {
                    instructions = "Up/Down: Choose  |  A: Select  |  B: Cancel";
                } else if (releaseConfirmActive) {
                    instructions = "A: Release  |  B: Cancel";
                } else if (currentlySelecting) {
                    instructions = "Arrows: Size Selection  |  A: Grab Group  |  X: Copy Group  |  B: Cancel";
                } else if (carrying()) {
                    instructions = carriedCount() > 1
                        ? "Arrows: Move Group (cross panes at edge)  |  L/R: Box  |  A: Place Here  |  Minus: Options  |  B: Put Back"
                        : "Arrows: Move (cross panes at edge)  |  L/R: Box  |  A: Drop / Swap  |  Minus: Options  |  B: Put Back";
                } else if (cursorMode == CursorMode::Menu) {
                    instructions = "Arrows: Move  |  L/R: Box  |  Y: Mode (Menu)  |  A: Menu  |  X: Sort Box  |  B: Back";
                } else if (cursorMode == CursorMode::Move) {
                    instructions = "Arrows: Move  |  L/R: Box  |  Y: Mode (Move)  |  A: Pick Up  |  X: Sort Box  |  B: Back";
                } else {
                    instructions = "Arrows: Move  |  L/R: Box  |  Y: Mode (Multi)  |  A: Start Selection  |  X: Sort Box  |  B: Back";
                }
            } else if (selectedMode == ViewMode::Trainer) {
                // X saves only from the HOME menu (see the X handler), so the flow is edit -> B -> X.
                instructions = "Up/Down: Select  |  A: Edit  |  B: Back  |  +: Exit App";
            }
        } else {
            // HOME main menu.
            instructions = "Arrows: Navigate  |  A: Open  |  B: Go Back  |  X: Save  |  +: Exit App";
        }
        // --- Nav bar: the contextual controls, drawn as controller badges ---
        drawNavBar(fb, instructions);
        const int footerY = fb.getHeight() - kNavBarH;

        // Draw dialogs on top of everything (Modals first, then dialogs)
        if (details.active) {
            Modals::drawPokemonDetailsModal(*this, fb);
        }
        if (pickerActive) {   // overlays the modal; registers its own touch buttons last
            Dialogs::drawPickerDialog(*this, fb);
        }
        if (itemEditDialogActive) {
            Dialogs::drawItemEditDialog(*this, fb);
        }
        if (itemRemoveConfirmActive) {
            Dialogs::drawItemRemoveConfirm(*this, fb);
        }
        if (statEdit.dialogActive) {
            Dialogs::drawStatEditDialog(*this, fb);
        }
        if (saveConfirmActive) {
            Dialogs::drawSaveConfirmDialog(*this, fb);
        }
        if (saveInjectConfirmActive) {   // overlays the picker
            Dialogs::drawSaveInjectConfirm(*this, fb);
        }
        if (storageMenuActive) {
            Panels::drawStorageActionMenu(*this, fb);
        }
        if (groupMenuActive) {
            Panels::drawStorageGroupMenu(*this, fb);
        }
        if (releaseConfirmActive) {
            Panels::drawStorageReleaseConfirm(*this, fb);
        }
        if (storageExitConfirmActive) {
            Panels::drawStorageExitConfirm(*this, fb);
        }
        if (creator.keepConfirmActive) {   // "Keep this new Pokemon?" overlays the creator's editor
            Panels::drawCreatorKeepConfirm(*this, fb);
        }
        if (details.discardConfirmActive) {   // "Unsaved changes" on closing an existing mon's editor
            Panels::drawDetailsDiscardConfirm(*this, fb);
        }
        if (gen3ConvertConfirmActive) {    // "Convert to Gen 3?" -- PID rebuild, never gated
            Panels::drawGen3ConvertConfirm(*this, fb);
        }
        if (lgpeTransferConfirmActive) {   // "Moving to/from Let's Go resets AVs/EVs" -- gated by g_moveWarn
            Panels::drawLgpeTransferConfirm(*this, fb);
        }

        // Transient status line (a refused cross-game drop, a rejected name), centered above the footer.
        // Drawn LAST so it is not painted over: a message can be raised from inside the details modal
        // (a nickname Gen 3 can't store), and the modal is drawn after the main body.
        if (storageStatusFrames > 0 && !storageStatus.empty()) {
            int tw, th; fb.measureText(storageStatus, tw, th);
            const int padX = 18, bw = tw + padX * 2, bh = th + 14;
            const int bx = (fb.getWidth() - bw) / 2, by = footerY - bh - 12;
            fb.drawFilledRoundedRect(bx, by, bw, bh, 8, Colors::Panel);
            fb.drawRoundedRect(bx, by, bw, bh, 8, Colors::Accent, 2);
            fb.drawText(bx + padX, by + 7, storageStatus, Colors::Text);
        }
    }
}
