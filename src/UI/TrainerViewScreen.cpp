#include <cstdlib> // std::free -> readAllBytes returns a malloc'd buffer (PKSM bank import)
#include <cstring>
#include <ctime> // std::time / std::localtime -> a created pokemon's met date = today
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>

#include "Names/NameLanguage.h" // display language for every generated name table
#include "Names/ItemNames.h"    // getItemNameFor -> the items view searches on the drawn name
#include "Names/ItemPouches.h"   // getPouchItems -> the add-item picker's per-pouch list
#include "Names/MoveInfo.h"      // getMoveBasePP -> a created/picked move gets real PP, not 0
#include "Names/MoveNames.h"     // getMoveCount -> scan for a created pokemon's first legal move
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
#include "UI/Dialogs/FileBrowserDialog.h"
#include "UI/Dialogs/PKSMImportDialog.h"
#include "UI/Dialogs/KeyboardDialog.h"
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
#include "Trainer/Inventory3RSE.h"
#include "Trainer/Trainer3RSE.h" // hoennLayout(): Ruby/Sapphire and Emerald size their pouches differently
#include "Trainer/PokemonFile.h" // standalone native Pokemon import/export
#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h" // getAbilitySlots -> the per-game legal ability list
#include "Pokemon/FormInfo.h"    // isBattleOnlyForm -> filters the Form picker
#include "Pokemon/Evolution.h"  // getTradeEvolutionOffer / applyTradeEvolution -> the Trade Evolve row
#include "Pokemon/Trade.h"      // applyTradeRoundTrip -> the handler block a trade leaves
#include "Trainer/TradeDonor.h" // buildTradeDonor -> may this save stand in as the partner?
#include "Save/SaveFileName.h"  // the same filters the save picker browses with
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/Gen1Tables.h"    // MAX_SPECIES_GEN1 -- the Gen 1 dex bound
#include "Pokemon/Pokemon2GSC.h"   // MAX_SPECIES_GEN2
#include "Pokemon/PersonalInfo3FRLG.h"  // the per-group species ceilings this picker filters by
#include "Pokemon/PersonalInfo4HGSS.h"
#include "Pokemon/PersonalInfo5B2W2.h"
#include "Pokemon/PersonalInfo6ORAS.h"
#include "Pokemon/PersonalInfo7USUM.h"
#include "Conversion/Convert.h"
#include "Utils/StringHelpers.h"

namespace UI
{
    static std::string leafName(const std::string &path); // defined below; used by the ctor's trace

    // UI Layout constants
    constexpr int LEFT_PANEL_X = 12;
    constexpr int LEFT_PANEL_Y = 80;
    constexpr int LEFT_TRAINER_INFO_PANEL_WIDTH = 220;
    constexpr int LEFT_TRAINER_INFO_PANEL_HEIGHT = 210;
    constexpr int LEFT_VIEW_MODE_PANEL_WIDTH = 220;
    constexpr int LEFT_VIEW_MODE_PANEL_HEIGHT = 205; // fits 4 modes (Party/Boxes/Items/Storage)
    constexpr int LEFT_PANEL_SPACING = 12;
    constexpr int CONTENT_PANEL_X = LEFT_PANEL_X + LEFT_VIEW_MODE_PANEL_WIDTH + 12;
    constexpr int CONTENT_PANEL_Y = LEFT_PANEL_Y;
    constexpr int CONTENT_PANEL_HEIGHT = 560;

    // The Settings rows, by index. ONE list, read by both drawSettingsView (which draws the labels, values
    // and badges) and the A-button handler in update() (which toggles them). Private copies of these
    // numbers -- a chain of `index ==` tests in one, an if/else ladder in the other -- do not all move when
    // a row is inserted, and the badges then light for their neighbour's setting. Add a row here and in
    // drawSettingsView's four arrays together.
    constexpr int SETTINGS_ROW_AUTO_BACKUP = 0;
    constexpr int SETTINGS_ROW_THEME = 1;
    constexpr int SETTINGS_ROW_ALLOW_ILLEGAL = 2;
    constexpr int SETTINGS_ROW_AUTO_LEGALIZE = 3;
    constexpr int SETTINGS_ROW_MOVE_WARNING = 4;
    constexpr int SETTINGS_ROW_INJECT_TO_GAME = 5;
    constexpr int SETTINGS_ROW_DEBUG_LOGGING = 6;
    constexpr int SETTINGS_ROW_LANGUAGE = 7;
    constexpr int SETTINGS_ROW_COUNT = 8; // rows DEFINED, which is what the row tables are sized by

    // Rows actually SHOWN. The Language row is defined but not selectable yet (see
    // Names::DISPLAY_LANGUAGE_SELECTABLE), and a row that does nothing does not belong on screen at
    // all -- a greyed-out control still reads as something the user ought to be able to reach. It is
    // trimmed off the END of the table, which is why the count is the whole mechanism and there is
    // no gap to skip over in the drawing loop, the touch targets or the Up/Down wrap.
    constexpr int SETTINGS_ROW_VISIBLE_COUNT =
        Names::DISPLAY_LANGUAGE_SELECTABLE ? SETTINGS_ROW_COUNT : SETTINGS_ROW_LANGUAGE;
    static_assert(SETTINGS_ROW_LANGUAGE == SETTINGS_ROW_COUNT - 1,
                  "Hiding the Language row works by trimming the LAST row -- a row added after it "
                  "would be hidden instead. Move Language back to the end, or hide it by index.");

    // Creator: build a valid, game-accepted default Pokemon in the current save's format. Species-
    // correct ability / gender / friendship come from the personal table; the user refines the rest
    // in the details editor. See scratchpad/creator_plan.md.
    // Randomize IVs: roll the six IVs to fresh 0-31 values and NOTHING else. Deliberately IVs
    // only -- nature, ability, and everything else are kept, so it can never silently change the
    // pokemon's identity. (A broader encounter-consistent randomizer -- PID/nature/ability from a real
    // encounter -- is a future version.)
    static void randomizeIVs(Pokemon::Pokemon *p)
    {
        if (!p)
            return;
        for (int index = 0; index < 6; ++index)
            p->setIV(index, static_cast<uint8_t>(Utils::rand32() % 32));
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
    static uint8_t saveOriginVersion(Trainer::Trainer &tr, u64 titleId)
    {
        const Enums::GameVersion value = Enums::getGameVersion(titleId);
        if (value != Enums::GameVersion::Invalid && Enums::getGameGroup(value) == tr.getGameGroup())
            return static_cast<uint8_t>(value);
        return Enums::getGroupRepVersion(tr.getGameGroup());
    }

    // A game group's bit in PersonalInfo::presence. FireRed/LeafGreen has no bit (Gen 3 predates the
    // bitmask), so it returns 0 -- callers must treat that as "no data", never as "present nowhere".
    /**
     * Highest National Dex id a group's games can store, or 0 when the answer is unknown.
     *
     * The per-game presence bitmask in the personal table only covers the Switch titles, so
     * personalPresenceBit() returns 0 for every pre-Switch group -- and a caller that reads 0 as
     * "do not filter" then offers a Red/Blue save all 1025 species. Every pre-Switch dex is a
     * contiguous run from 1, so a ceiling is the whole answer for them.
     *
     * Sun/Moon is the one approximation: its dex stops at 802 while Ultra Sun/Ultra Moon reaches
     * 807, and the shared Gen 7 constant is the latter. Over-offering six species in a Sun save is
     * the safe direction -- filtering to nothing is the failure this function exists to prevent --
     * and the legality checker is what reports the difference.
     */
    static uint16_t highestSpeciesIdForGroup(Enums::GameVersion group)
    {
        switch (group)
        {
        case Enums::GameVersion::RBY:
            return Pokemon::MAX_SPECIES_GEN1;
        case Enums::GameVersion::GSC:
            return Pokemon::MAX_SPECIES_GEN2;
        case Enums::GameVersion::FRLG:
        case Enums::GameVersion::RSE:
            return Pokemon::PERSONAL_MAX_SPECIES_3FRLG;
        case Enums::GameVersion::DP:
        case Enums::GameVersion::PT:
        case Enums::GameVersion::HGSS:
            return Pokemon::PERSONAL_MAX_SPECIES_4HGSS;
        case Enums::GameVersion::BW:
        case Enums::GameVersion::B2W2:
            return Pokemon::PERSONAL_MAX_SPECIES_5B2W2;
        case Enums::GameVersion::XY:
        case Enums::GameVersion::ORAS:
            return Pokemon::PERSONAL_MAX_SPECIES_6ORAS;
        case Enums::GameVersion::SM:
        case Enums::GameVersion::USUM:
            return Pokemon::PERSONAL_MAX_SPECIES_7USUM;
        default:
            return 0;
        }
    }

    static uint8_t personalPresenceBit(Enums::GameVersion group)
    {
        switch (group)
        {
        case Enums::GameVersion::GG:
            return Pokemon::PERSONAL_GAME_GG;
        case Enums::GameVersion::SWSH:
            return Pokemon::PERSONAL_GAME_SWSH;
        case Enums::GameVersion::BDSP:
            return Pokemon::PERSONAL_GAME_BDSP;
        case Enums::GameVersion::PLA:
            return Pokemon::PERSONAL_GAME_PLA;
        case Enums::GameVersion::SV:
            return Pokemon::PERSONAL_GAME_SV;
        case Enums::GameVersion::ZA:
            return Pokemon::PERSONAL_GAME_ZA;
        default:
            return 0;
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
    static bool speciesPresentIn(uint16_t species, uint8_t bit)
    {
        const Pokemon::PersonalInfo &base = Pokemon::getPersonalInfo(species, 0);
        if ((base.presence & bit) != 0)
            return true;
        int count = base.formCount;
        if (count < 1)
            count = 1;
        for (int fIndex = 1; fIndex < count; ++fIndex)
            if ((Pokemon::getPersonalInfo(species, static_cast<uint8_t>(fIndex)).presence & bit) != 0)
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
    // Two callers, and they MUST agree: the Form picker, and the form a newly created pokemon starts on.
    // A created pokemon sitting on a form its own picker won't offer looks exactly like the bug above.
    //
    // Presence is skipped in two cases, both of which mean "the table can't answer", never "no forms":
    // Gen 3 has no presence bit at all, and a species that is off-dex HERE has the bit clear on every
    // one of its forms -- filtering on that would strand an already-present pokemon on a one-row picker.
    static std::vector<int> selectableForms(uint16_t species, Enums::GameVersion group)
    {
        int count = Pokemon::getPersonalInfo(species, 0).formCount;
        if (count < 1)
            count = 1;
        const uint8_t bit = personalPresenceBit(group);
        const bool byPresence = (bit != 0) && speciesPresentIn(species, bit);
        std::vector<int> out;
        for (int fIndex = 0; fIndex < count; ++fIndex)
        {
            if (Pokemon::isBattleOnlyForm(species, static_cast<uint8_t>(fIndex)))
                continue;
            if (Pokemon::isLordForm(species, static_cast<uint8_t>(fIndex)))
                continue;
            if (byPresence &&
                (Pokemon::getPersonalInfo(species, static_cast<uint8_t>(fIndex)).presence & bit) == 0)
                continue;
            out.push_back(fIndex);
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
    static std::vector<int> selectableGenders(uint16_t species, uint8_t form)
    {
        if (Pokemon::isFormGenderSpecific(species))
            return {0, 1};
        switch (Pokemon::getPersonalInfo(species, form).genderRatio)
        {
        case 255:
            return {2};
        case 254:
            return {1}; // always female
        case 0:
            return {0}; // always male
        default:
            return {0, 1}; // a threshold -> either, but never genderless
        }
    }

    // The name a pokemon shows when it has NO custom nickname. The games store the DISPLAY string in the
    // nickname field itself, so "not nicknamed" still means writing the species name there -- leaving
    // the field blank shows a blank name in-game. Gen 3 stores that default UPPERCASE, as FRLG shows it.
    //
    // Uppercasing the UTF-8 bytes is safe: no byte of a multi-byte sequence falls in 'a'-'z' (lead
    // bytes are >= 0xC0, continuations 0x80-0xBF), so NIDORAN♀ and FARFETCH'D keep their sign and
    // apostrophe intact.
    static std::string defaultNicknameFor(const Pokemon::Pokemon &p)
    {
        std::string text = p.species();
        if (Enums::isGen3Group(p.getGameGroup()))
        {
            for (char &character : text)
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return text;
    }

    /**
     * Re-points an existing Pokemon at a different species, reconciling everything the species
     * DECIDES. Changing the id alone is not enough -- four other fields are species-derived, and
     * leaving any of them behind produces a pokemon the games reject rather than an unusual one:
     *
     *   FORM     the new species may have fewer forms, and a form past its formCount is a Bad Egg
     *            in-game. Clamped to a form this game actually has (which is not always 0 -- see
     *            selectableForms).
     *   GENDER   the new species may be genderless, female-only, or tie gender to the form.
     *   ABILITY  the old ability almost certainly is not one of the new species' slots. The SLOT
     *            is preserved rather than the id, so a hidden-ability pokemon stays hidden.
     *   EXP      total EXP is a function of level AND growth rate, and growth rate is per species.
     *            The LEVEL is what the player means to keep, so it is re-derived into EXP.
     *
     * Everything else -- IVs, EVs, nickname, OT, met data, ball, moves -- is the player's and is
     * deliberately left alone. Moves in particular are NOT sanitized: an illegal move on a swapped
     * species is exactly what the legality checker exists to report, and silently deleting it
     * would repair the evidence out from under it.
     */
    static void applySpeciesChange(Pokemon::Pokemon &pokemon, uint16_t species, Enums::GameVersion group)
    {
        // Read the level BEFORE the species changes -- exp() is interpreted through the old
        // species' growth rate, and a box record may carry no level byte at all.
        uint8_t levelValue = pokemon.level();
        if (levelValue == 0)
            levelValue = Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(pokemon.speciesID()));
        if (levelValue == 0)
            levelValue = 1;

        const uint8_t oldAbilityNumber = pokemon.abilityNumber();
        pokemon.setSpecies(species);

        const std::vector<int> forms = selectableForms(species, group);
        uint8_t form = pokemon.form();
        if (std::find(forms.begin(), forms.end(), static_cast<int>(form)) == forms.end())
            form = forms.empty() ? 0 : static_cast<uint8_t>(forms.front());
        pokemon.setForm(form);
        // Maushold and Dudunsparce read their form from EC % 100, so the form has to be re-tied to
        // the encryption constant or the game overrides what was just set.
        pokemon.setEncryptionConstant(
            Pokemon::correctEncryptionConstantForForm(species, form, pokemon.encryptionConstant()));

        const std::vector<int> genders = selectableGenders(species, form);
        if (!genders.empty() &&
            std::find(genders.begin(), genders.end(), static_cast<int>(pokemon.gender())) == genders.end())
            pokemon.setGender(static_cast<uint8_t>(genders.front()));

        if (pokemon.hasAbility())
        {
            const Pokemon::AbilitySlots slots = Pokemon::getAbilitySlots(species, form, group);
            // abilityNumber is a BITMASK in the modern formats: 1 = slot 1, 2 = slot 2, 4 = hidden.
            int want = (oldAbilityNumber == 4) ? 2 : (oldAbilityNumber == 2) ? 1
                                                                             : 0;
            if (want >= slots.count || slots.slot[want] == 0)
                want = 0;
            if (slots.slot[want] != 0)
            {
                pokemon.setAbility(slots.slot[want]);
                if (!Enums::isGen3Group(group))
                    pokemon.setAbilityNumber(static_cast<uint8_t>(want == 2 ? 4 : want + 1));
            }
        }

        pokemon.setLevel(levelValue); // rewrites EXP from the NEW growth rate and recalculates stats
        pokemon.refreshChecksum();
    }

    static std::unique_ptr<Pokemon::Pokemon> buildDefaultMon(Trainer::Trainer &tr, uint16_t species,
                                                             uint8_t version)
    {
        auto pokemon = tr.createBlankPokemon();
        if (!pokemon)
            return pokemon;
        // Start on the lowest form this game HAS, which is not always form 0: Legends: Arceus never had
        // a base Braviary or a Kanto Growlithe, so creating one there has to begin on the Hisuian form.
        // Everything below keys off this form, not off 0 -- a regional variant has its own abilities,
        // gender ratio and friendship, and reading form 0's would quietly stamp the wrong ones.
        const std::vector<int> forms = selectableForms(species, tr.getGameGroup());
        const uint8_t form = forms.empty() ? 0 : static_cast<uint8_t>(forms.front());
        const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(species, form);
        pokemon->setSpecies(species); // first: drives the EXP growth rate + base-stat lookup
        pokemon->setForm(form);
        pokemon->setEncryptionConstant(Utils::rand32());
        pokemon->setPID(Utils::rand32());
        // ...and re-apply the form's EC correlation, because the random EC above lands AFTER setForm
        // and would otherwise undo it. Maushold and Dudunsparce read their form from EC % 100, so a
        // freshly rolled EC has a 99-in-100 chance of contradicting the form just set. A no-op for
        // every other species; see FormInfo.
        pokemon->setEncryptionConstant(
            Pokemon::correctEncryptionConstantForForm(species, form, pokemon->encryptionConstant()));
        // Starting level. Gen 3 has NO level-1 Pokemon: its eggs hatch at 5 and its lowest wild
        // encounters are 2, so level 1 is unobtainable there -- and it is the one level whose total EXP
        // is **0**, the degenerate input to the game's level-from-EXP lookup. Gen 8+ eggs really do hatch
        // at level 1, so every other format keeps it.
        const uint8_t startLevel = Enums::isGen3Group(tr.getGameGroup()) ? 5 : 1;
        pokemon->setLevel(startLevel); // after species; writes EXP from growth rate + recalcs stats
        // Random nature at birth (the real and stat/mint nature start matched; the mint stays editable).
        {
            uint8_t natureValue = static_cast<uint8_t>(Utils::rand32() % 25);
            pokemon->setNature(natureValue);
            pokemon->setStatNature(natureValue);
        }
        // Random gender, constrained by the species ratio: fixed for genderless (255) / female-only (254);
        // otherwise rolled against the female threshold (genderRatio 0 -> always male).
        {
            uint8_t genderValue = (pi.genderRatio == 255)   ? 2
                        : (pi.genderRatio == 254) ? 1
                                                  : (((Utils::rand32() & 0xFF) < pi.genderRatio) ? 1 : 0);
            pokemon->setGender(genderValue);
        }
        // Slot-1 ability, from the table for the game being created into (Gen 3's slot pair is
        // its own; pi is the modern one). setAbility resolves the slot; Gen 3 also re-rolls the PID.
        {
            const Pokemon::AbilitySlots slots =
                Pokemon::getAbilitySlots(species, form, tr.getGameGroup());
            pokemon->setAbility(slots.slot[0]);
            if (!Enums::isGen3Group(tr.getGameGroup()))
                pokemon->setAbilityNumber(1);
        }
        pokemon->setFriendship(pi.baseFriendship);
        // Poké Ball -- but Legends: Arceus uses its own ball set, where the Poké Ball is id 28 (the
        // standard Poké Ball id 4 isn't one of its balls and reads as the wrong ball in-game).
        pokemon->setBall(tr.getGameGroup() == Enums::GameVersion::PLA ? 28 : 4);
        // The SAVE's language, not a fixed English. It decides which character table a Gen 3 name is
        // written through, and the OT name stamped below is the save's own trainer name -- so on a
        // Japanese cartridge an English stamp would leave a Pokemon whose OT cannot be stored at all.
        // Every format from Gen 4 on reports English here, which is what this always was.
        pokemon->setLanguage(tr.language());
        pokemon->setOriginGame(version);
        pokemon->setMetLevel(startLevel); // met AT the level it is, not at 1 -- Gen 3 has no met level 1 either
        // Met "here, today": a valid caught date + a real location, so the pokemon doesn't read as met on
        // 00/00/2000 at location 0 ("nothing" -- which BDSP renders as "hatched from an egg at Jubilife
        // City"). Date components are years-since-2000 / 1-based month / day; formats without a met date
        // (Gen 3) no-op these setters.
        std::time_t nowT = std::time(nullptr);
        if (const std::tm *localTime = std::localtime(&nowT))
        {
            pokemon->setMetYear(static_cast<uint8_t>(((localTime->tm_year + 1900) - 2000) & 0xFF));
            pokemon->setMetMonth(static_cast<uint8_t>(localTime->tm_mon + 1));
            pokemon->setMetDay(static_cast<uint8_t>(localTime->tm_mday));
        }
        // A real, recognizable in-game met location for every game group, so a created pokemon is never met
        // at location 0 / "(none)". Ids are per-game (each game has its own location table + numbering).
        switch (tr.getGameGroup())
        {
        case Enums::GameVersion::SV:
            pokemon->setMetLocation(8);
            break; // Mesagoza
        case Enums::GameVersion::ZA:
            pokemon->setMetLocation(11);
            break; // Centrico Plaza
        case Enums::GameVersion::BDSP:
            pokemon->setMetLocation(38);
            break; // Oreburgh City
        case Enums::GameVersion::PLA:
            pokemon->setMetLocation(7);
            break; // Obsidian Fieldlands
        case Enums::GameVersion::SWSH:
            pokemon->setMetLocation(12);
            break; // Route 1 (Galar)
        case Enums::GameVersion::GG:
            pokemon->setMetLocation(3);
            break; // Route 1 (Kanto)
        case Enums::GameVersion::FRLG:
            pokemon->setMetLocation(101);
            break; // Route 1 (Kanto, Gen 3 numbering)
        case Enums::GameVersion::RSE:
            pokemon->setMetLocation(16);
            break; // Route 101 (Hoenn) -- same Gen 3 table as FR/LG, a different entry in it
        default:
            break;
        }
        // Not from an egg (a caught pokemon): clear egg origin. The "no egg location" value is game-specific.
        // BDSP uses Gen 4-style numbering where 0 is a REAL place (Jubilife City) and 65535 is "none"
        // (PKHeX Locations.Default8bNone) -- writing 0 there made a created pokemon read as "hatched from an
        // Egg" received at Jubilife City. Every other format uses 0 for "none".
        pokemon->setEgg(false);
        pokemon->setEggLocation(tr.getGameGroup() == Enums::GameVersion::BDSP ? 0xFFFF : 0x0000);
        pokemon->setEggYear(0);
        pokemon->setEggMonth(0);
        pokemon->setEggDay(0);
        // Clear the HOME ribbon+mark block (0x34-0x45) so a created pokemon owns no stray ribbon, AND reset
        // AffixedRibbon -- the byte that selects which ribbon the game DISPLAYS. On an all-zero blank it
        // is 0, and ribbon index 0 is the Kalos Champion ribbon, so a fresh pokemon SHOWED it even with no
        // ribbon owned (the SV report). "None" is 0xFF. A zero offset means the format has no such field
        // (FRLG / GG), which also gates the ribbon-ownership clear to the formats that carry the block.
        // Both fields sit in the checksummed region, covered by the refreshChecksum() below.
        //
        // The offset table is Conversion::affixedRibbonOffset, shared with the cross-generation converter --
        // the same fact bit twice, once here and once through transfer, so it is defined once.
        if (const size_t affix = Conversion::affixedRibbonOffset(tr.getGameGroup()); affix != 0)
        {
            auto dataBytes = pokemon->getData();
            if (dataBytes.size() >= 0x46)
                std::memset(dataBytes.data() + 0x34, 0, 0x46 - 0x34);
            if (dataBytes.size() > affix)
                dataBytes.data()[affix] = std::byte{Conversion::AFFIXED_RIBBON_NONE};
        }
        // First move = the species' first legal move for this game, so the created pokemon is legal. PLA
        // rejects an unlearnable move as a Bad Egg in-game (Pound isn't a Vulpix move there); the other
        // games merely flag it.
        //
        // The FORM belongs in this lookup as much as it does in the stat and ability ones above.
        // Learnsets are keyed on (species, form) and a regional form has its own: asking for form 0
        // got the Unovan Braviary row, which in Legends: Arceus does not exist at all. getLearnableBits
        // returns nullptr there, every move reads "not learnable", and the old `defMove = 1` fallback
        // turned that into POUND -- an illegal move, on a pokemon the creator had just built to be legal,
        // in the one game that Bad-Eggs it. A missing table is not an empty movepool.
        uint16_t defMove = 0;
        for (uint16_t moveIndex = 1; moveIndex < Names::getMoveCount(); ++moveIndex)
            if (Pokemon::isLearnable(species, form, tr.getGameGroup(), moveIndex) &&
                Names::isMovePresent(moveIndex, tr.getGameGroup()))
            {
                defMove = moveIndex;
                break;
            }
        if (defMove == 0)
        {
            // No learnset row for this (species, form) in this game. Every move is now a guess, so
            // leave the slot EMPTY rather than write a plausible-looking wrong one: an empty slot is
            // visible in the editor and harmless in-game, where a wrong move is neither. Log it --
            // reaching here means the learnset table is missing a row the creator can offer.
            Utils::logErrorToFile("buildDefaultMon: no learnable move for species " +
                                  std::to_string(species) + " form " + std::to_string(form) +
                                  " -- leaving move 1 empty");
        }
        pokemon->setMove(0, defMove);
        pokemon->setMovePP(0, Names::getMoveBasePP(defMove, tr.getGameGroup()));
        pokemon->setMovePPUps(0, 0);
        pokemon->setId32(tr.ID32);
        pokemon->setOTName(Utils::utf8ToUtf16(tr.trainerName));
        pokemon->setOTGender(tr.trainerGender); // match the trainer -- else Gen 3 reads it as "Apparently met"
        // Default nickname = the species name (see defaultNicknameFor). isNicknamed is left false --
        // this is the species default, not a custom name.
        pokemon->setNickname(Utils::utf8ToUtf16(defaultNicknameFor(*pokemon)));
        // Let's Go stores + DISPLAYS absolute height/weight (floats at PB7 0x2C / 0xE4); PK8/PK9 keep
        // only the 0-255 scalar and derive the size on the fly. A created LGPE pokemon left those floats at
        // 0, so it showed 0'00" / 0.0 lbs in-game. Roll random size scalars and compute the absolutes
        // from the species base size -- same formula as the bank convert (PKHeX PB7.Get*Absolute).
        if (tr.getGameGroup() == Enums::GameVersion::GG)
        {
            auto dataBytes = pokemon->getData();
            if (dataBytes.size() >= 0xE8)
            {
                const uint8_t heightScalar = static_cast<uint8_t>(Utils::rand32() & 0xFF);
                const uint8_t weightScalar = static_cast<uint8_t>(Utils::rand32() & 0xFF);
                dataBytes.data()[0x3A] = static_cast<std::byte>(heightScalar);
                dataBytes.data()[0x3B] = static_cast<std::byte>(weightScalar);
                const float heightRatio = (heightScalar / 255.0f) * 0.79999995f + 0.6f; // height ratio (-20% .. +40%)
                const float weightRatio = (weightScalar / 255.0f) * 0.40000004f + 0.8f; // weight ratio (+/- 20%)
                const float hAbs = heightRatio * static_cast<float>(pi.height);
                const float wAbs = heightRatio * weightRatio * static_cast<float>(pi.weight);
                std::memcpy(dataBytes.data() + 0x2C, &hAbs, sizeof(float));
                std::memcpy(dataBytes.data() + 0xE4, &wAbs, sizeof(float));
            }
        }
        // Roll a random IV spread at birth (same routine as the L-button randomizer, which is unchanged);
        // this recalculates stats and refreshes the checksum, so it stands in for the final pass.
        randomizeIVs(pokemon.get());
        pokemon->setStatHPCurrent(pokemon->statHPMax());
        return pokemon;
    }

    // Settings view (the toggle rows), reached from the HOME menu's Settings icon.
    static void drawSettingsView(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int settingsViewX,
                                 int settingsViewY, int settingsViewWidth, int settingsViewHeight)
    {
        framebuffer.drawFilledRoundedRect(settingsViewX, settingsViewY, settingsViewWidth, settingsViewHeight, 16,
                                          Colors::Panel);
        framebuffer.drawRoundedRect(settingsViewX, settingsViewY, settingsViewWidth, settingsViewHeight, 16,
                                    Colors::Border, 1);
        constexpr int headerHeight = 46;
        framebuffer.drawFilledRoundedRect(settingsViewX, settingsViewY, settingsViewWidth, headerHeight, 16,
                                          Colors::AccentDim);
        framebuffer.drawFilledRect(settingsViewX, settingsViewY + headerHeight - 16, settingsViewWidth, 16,
                                   Colors::AccentDim);
        framebuffer.drawText(settingsViewX + 22,
                             settingsViewY + (headerHeight - framebuffer.lineHeight(TextStyle::Heading)) / 2,
                             "Settings", Colors::Text, TextStyle::Heading);

        screen.touchButtons.clear();
        // Every row DEFINED, including the Language row the loop below stops short of. Sizing the
        // tables by the defined count rather than the visible one is what keeps every row's label,
        // value and badge on the same line of the same table whether it is on screen or not.
        constexpr int rowCount = SETTINGS_ROW_COUNT;
        // The injection row names its actual scope. It does NOT govern saving a cart-loaded session
        // back to the cart -- that is always allowed. It only unlocks writing an OLDER BACKUP over
        // the live save, which is the case that can roll a game backwards.
        const char *labels[rowCount] = {
            "Auto-Backup on Load",
            "Theme",
            "Allow Illegal Values",
            "Auto-Legalize Unsupported Transfers",
            "Bank Storage Move Warning",
            "Allow Inject Backups to Game Save",
            "Enable Debug Logging",
            "Language"};
        std::string values[rowCount] = {
            g_autoBackupEnabled ? "On" : "Off",
            (g_themeMode == ThemeMode::Dark) ? "Dark" : "Light",
            g_allowIllegalEdits ? "On" : "Off",
            g_autoLegalizeTransfers ? "On" : "Off",
            g_moveWarn ? "On" : "Off",
            g_injectToGameSave ? "On" : "Off",
            g_debugLogging ? "On" : "Off",
            Names::nameLanguageLabel(Names::displayLanguageIndex()),
        };
        // Whether each row reads "On" (so its pill lights up), and which of those are the two that can
        // damage real data. Both are columns of the SAME table as labels and values. A separate chain of
        // `index ==` tests is a second, independent list of row numbers: inserting a row moves the labels
        // and values and leaves the chain where it is, so from that row down every badge lights for its
        // NEIGHBOUR's setting. A row's label, value and colour travel together.
        const bool rowIsOn[rowCount] = {
            g_autoBackupEnabled,
            false, // Theme names its mode (Dark/Light) rather than toggling, so it never lights up
            g_allowIllegalEdits,
            g_autoLegalizeTransfers,
            g_moveWarn,
            g_injectToGameSave,
            g_debugLogging,
            false, // Language: never lights up, and is not drawn at all -- SETTINGS_ROW_VISIBLE_COUNT
        };
        // Amber "On" for the benign toggles; RED for the two that can damage real data -- the
        // illegal-values override and game-save injection. Colour carries the risk, not just text.
        const bool rowIsDestructive[rowCount] = {false, false, true, false, false, true, false, false};
        // The panel is a fixed height, so the rows have to fit inside it -- there is no scrolling
        // here. The assert is the point, and it has earned its keep twice: the Language row as a
        // seventh and the auto-legalize row as an EIGHTH each broke the build rather than quietly
        // drawing past the panel edge. Eight rows need rowH + rowGap <= 58, so the pitch came down
        // again, 56+10 -> 50+8 (558 of 560 used). rowH is also the touch target, and 50 is still
        // above the ~44px minimum.
        //
        // Deliberately asserted on the DEFINED count, not the seven currently drawn: it has to keep
        // holding on the day the Language row comes back, and an assert that only covers what is on
        // screen today would pass right up until then.
        constexpr int rowW = 720, rowH = 50, rowGap = 8, rowsTop = 22, footerH = 26;
        static_assert(headerHeight + rowsTop + rowCount * (rowH + rowGap) + footerH <= CONTENT_PANEL_HEIGHT,
                      "Settings rows no longer fit the content panel -- shrink the rows or add scrolling");
        const int rowX = settingsViewX + (settingsViewWidth - rowW) / 2;
        int rectY = settingsViewY + headerHeight + rowsTop;
        for (int index = 0; index < SETTINGS_ROW_VISIBLE_COUNT; ++index)
        {
            const bool selectedIndex = (screen.settingsSelectedRow == index);
            framebuffer.drawSoftShadow(rowX, rectY, rowW, rowH, 14);
            framebuffer.drawFilledRoundedRect(rowX, rectY, rowW, rowH, 14,
                                              selectedIndex ? Colors::Selected : Colors::PanelAlt);
            if (selectedIndex)
                framebuffer.drawRoundedRect(rowX, rectY, rowW, rowH, 14, Colors::Accent, 2);
            framebuffer.drawText(rowX + 24, rectY + (rowH - framebuffer.lineHeight(TextStyle::Body)) / 2, labels[index],
                                 Colors::Text, TextStyle::Body);
            int valueWidth, vh;
            framebuffer.measureText(values[index], valueWidth, vh, TextStyle::Body);
            const int pillW = valueWidth + 48, pillH = 40;
            const int panelX = rowX + rowW - pillW - 20, py = rectY + (rowH - pillH) / 2;
            Color pillFill = Colors::Background, pillText = Colors::Text;
            if (rowIsOn[index])
            {
                pillFill = rowIsDestructive[index] ? Colors::Danger : Colors::Primary;
                pillText = rowIsDestructive[index] ? Colors::White : Colors::PrimaryText;
            }
            framebuffer.drawPill(panelX, py, pillW, pillH, pillFill);
            framebuffer.drawText(panelX + (pillW - valueWidth) / 2, py + (pillH - vh) / 2, values[index], pillText,
                                 TextStyle::Body);
            screen.touchButtons.push_back({index, rowX, rectY, rowW, rowH});
            rectY += rowH + rowGap;
        }
        framebuffer.drawText(rowX, rectY + 8, "A: toggle     B: back", Colors::TextDim, TextStyle::Caption);
    }

    // Trainer view (HOME-style ID card), reached from the HOME menu's Trainer icon. The first two
    // rows (Name / Money) are editable; the rows below are informational -- editing TID/SID would
    // re-own every Pokemon already in the save, so it is deliberately left out.
    static void drawTrainerView(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int trainerViewX,
                                int trainerViewY, int trainerViewWidth, int trainerViewHeight)
    {
        Trainer::Trainer &t = screen.trainer;
        framebuffer.drawFilledRoundedRect(trainerViewX, trainerViewY, trainerViewWidth, trainerViewHeight, 16,
                                          Colors::Panel);
        framebuffer.drawRoundedRect(trainerViewX, trainerViewY, trainerViewWidth, trainerViewHeight, 16, Colors::Border,
                                    1);
        constexpr int headerHeight = 46;
        framebuffer.drawFilledRoundedRect(trainerViewX, trainerViewY, trainerViewWidth, headerHeight, 16,
                                          Colors::AccentDim);
        framebuffer.drawFilledRect(trainerViewX, trainerViewY + headerHeight - 16, trainerViewWidth, 16,
                                   Colors::AccentDim);
        framebuffer.drawText(trainerViewX + 22,
                             trainerViewY + (headerHeight - framebuffer.lineHeight(TextStyle::Heading)) / 2, "Trainer",
                             Colors::Text, TextStyle::Heading);

        screen.touchButtons.clear();

        const int cardW = 680, cardX = trainerViewX + (trainerViewWidth - cardW) / 2;
        int centerY = trainerViewY + headerHeight + 26;

        // --- Editable rows (Name / Money): selectable via cursor + touch, styled like Settings.
        // Gender is display-only, so it sits with the identity rows below rather than leaving a hole
        // the cursor has to jump over. ---
        constexpr int editRowCount = 2;
        const char *labels[editRowCount] = {"Name", "Money"};
        const std::string values[editRowCount] = {
            t.trainerName.empty() ? "(none)" : t.trainerName,
            "$" + std::to_string(t.money),
        };
        const int rowH = 60;
        for (int index = 0; index < editRowCount; ++index)
        {
            const bool selectedIndex = (screen.trainerSelectedRow == index);
            framebuffer.drawSoftShadow(cardX, centerY, cardW, rowH, 14);
            framebuffer.drawFilledRoundedRect(cardX, centerY, cardW, rowH, 14,
                                              selectedIndex ? Colors::Selected : Colors::PanelAlt);
            if (selectedIndex)
                framebuffer.drawRoundedRect(cardX, centerY, cardW, rowH, 14, Colors::Accent, 2);
            framebuffer.drawText(cardX + 24, centerY + (rowH - framebuffer.lineHeight(TextStyle::Body)) / 2,
                                 labels[index], Colors::TextDim, TextStyle::Body);
            int valueWidth, vh;
            framebuffer.measureText(values[index], valueWidth, vh, TextStyle::Body);
            const int pillW = valueWidth + 44, pillH = 38;
            const int panelX = cardX + cardW - pillW - 20, py = centerY + (rowH - pillH) / 2;
            framebuffer.drawPill(panelX, py, pillW, pillH, selectedIndex ? Colors::Primary : Colors::Background);
            framebuffer.drawText(panelX + (pillW - valueWidth) / 2, py + (pillH - vh) / 2, values[index],
                                 selectedIndex ? Colors::PrimaryText : Colors::Text, TextStyle::Body);
            screen.touchButtons.push_back({index, cardX, centerY, cardW, rowH});
            centerY += rowH + 14;
        }

        centerY += 6;
        framebuffer.drawHDivider(cardX + 20, centerY, cardW - 40);
        centerY += 20;

        auto infoRow = [&](const char *label, const std::string &value)
        {
            framebuffer.drawFilledRoundedRect(cardX, centerY, cardW, 46, 12, Colors::PanelAlt);
            framebuffer.drawText(cardX + 24, centerY + (46 - framebuffer.lineHeight(TextStyle::Body)) / 2, label,
                                 Colors::TextDim, TextStyle::Body);
            int valueWidth, vh;
            framebuffer.measureText(value, valueWidth, vh, TextStyle::Body);
            framebuffer.drawText(cardX + cardW - 24 - valueWidth, centerY + (46 - vh) / 2, value, Colors::Text,
                                 TextStyle::Body);
            centerY += 54;
        };
        infoRow("Gender", t.trainerGender == 0 ? "Male" : "Female");
        infoRow("Trainer ID", std::to_string(t.TID16) + " / " + std::to_string(t.SID16));
        infoRow("Full TID", std::to_string(t.TID));
        infoRow("Full SID", std::to_string(t.SID));
    }

    TrainerViewScreen::TrainerViewScreen(Trainer::Trainer &trainer, const std::string &titleName,
                                         const std::string &backupDir, u64 titleId, AccountUid userUid,
                                         bool loadedFromCart)
        : trainer(trainer), titleName(titleName), backupDir(backupDir), gameVersion(Utils::getTitleVersion(titleId)),
          titleId(titleId), userUid(userUid)
    {
        // No title id means nobody installed this save -- the user browsed to a file. `backupDir`
        // then holds that file's PATH rather than a directory, which is what the Gen 1 write-back
        // expects, and the save picker has only one destination to offer.
        externalFileSession = (titleId == 0);
        // Assigned in the body rather than the init list: it is declared far below these members, and
        // C++ initialises in DECLARATION order, so listing it here would only earn a -Wreorder.
        this->loadedFromCart = loadedFromCart;
        saveDestinationIndex = defaultSaveDestinationRow();

        // Open on the box the game was last left on (persisted per-game as the "current box"), so
        // the editor lands where the player was -- in the Boxes view and the Storage view's save
        // pane alike, since both read selectedBoxIndex. Clamp in case a save holds a stale index.
        {
            const uint8_t currentBoxIndex = trainer.getCurrentBox();
            if (currentBoxIndex < trainer.getBoxCount())
                selectedBoxIndex = static_cast<int>(currentBoxIndex);
        }

        // Persistent cross-game bank for the Storage view (unified; loads any existing on-SD contents).
        bank = std::make_unique<Trainer::Bank>();
        // Open the bank pane on the box it was last SAVED on. That value rides in the bank's own
        // image rather than in settings.cfg, which is what lets moving it count as an unsaved bank
        // change -- see Bank::currentBox. Has to follow the construction above, since loading the
        // file is what fills it in.
        if (bank && bank->currentBox < Trainer::Bank::BANK_BOX_COUNT)
            stBankBox = static_cast<int>(bank->currentBox);
        // A damaged bank file drops slots silently otherwise -- the user would just find Pokemon
        // missing with no explanation.
        if (bank && bank->lastLoadRejects() > 0)
        {
            postStatus(std::to_string(bank->lastLoadRejects()) +
                           " damaged bank slot(s) could not be read and were skipped.",
                       480);
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
        if (bank)
        {
            Utils::logTest("BANKLOAD rejects=" + std::to_string(bank->lastLoadRejects()));
            Utils::logEventToFile("BANK action=LOAD rejects=" + std::to_string(bank->lastLoadRejects()));
        }
    }

    // Non-null cells in the carried block (a block can contain holes -- see moveMon).
    int TrainerViewScreen::carriedCount() const
    {
        int count = 0;
        for (const auto &p : moveMon)
            if (p)
                ++count;
        return count;
    }

    const Pokemon::Pokemon *TrainerViewScreen::firstCarried() const
    {
        for (const auto &p : moveMon)
            if (p)
                return p.get();
        return nullptr;
    }

    // Put the whole carried block back where it was lifted from. Each cell prefers its own original
    // slot; if something has since filled that slot, it falls back to any empty slot in the origin
    // pane, because a carried Pokemon must never be dropped on the floor.
    void TrainerViewScreen::returnHeldToOrigin()
    {
        if (!carrying())
            return;
        const int pane = heldPane;
        if (pane == 1 && !bank)
            return;
        const int slots = (pane == 0) ? static_cast<int>(trainer.getSlotsPerBox())
                                      : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
        const int boxCount = (pane == 0) ? static_cast<int>(trainer.getBoxCount())
                                         : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
        const int cols = boxGridColumns(slots);
        auto place = [&](int box, int slot, std::unique_ptr<Pokemon::Pokemon> &pokemon) -> bool
        {
            if (box < 0 || box >= boxCount || slot < 0 || slot >= slots)
                return false;
            if (storageSlotLocked(pane, box, slot))
                return false;
            auto &destination = storageSlot(pane, box, slot);
            if (!destination || destination->speciesID() == 0)
            {
                destination = std::move(pokemon);
                return true;
            } // species-0 = empty (S/V ghost)
            return false;
        };
        const int carriedColumns = selectDimensions.first > 0 ? selectDimensions.first : 1;
        for (size_t moveMonIndex = 0; moveMonIndex < moveMon.size(); ++moveMonIndex)
        {
            if (!moveMon[moveMonIndex])
                continue;
            const int columnOffset = static_cast<int>(moveMonIndex) % carriedColumns,
                      rowOffset = static_cast<int>(moveMonIndex) / carriedColumns;
            int box = heldFromBox, slot = heldFromSlot + columnOffset + rowOffset * cols;
            while (slot >= slots)
            {
                slot -= slots;
                ++box;
            } // defensive: a grab is bounds-checked to one box
            if (place(box, slot, moveMon[moveMonIndex]))
                continue;
            bool placed = false;
            for (int boxIndex = 0; boxIndex < boxCount && !placed; ++boxIndex)
                for (int slotIndex = 0; slotIndex < slots && !placed; ++slotIndex)
                    placed = place(boxIndex, slotIndex, moveMon[moveMonIndex]);
        }
        moveMon.clear();
        selectDimensions = {0, 0};
    }

    // Reference to a storage slot's unique_ptr (pane 0 = save boxes, 1 = bank). Callers must
    // ensure `bank` exists for pane 1.
    std::unique_ptr<Pokemon::Pokemon> &TrainerViewScreen::storageSlot(int pane, int box, int slot)
    {
        return pane == 0 ? trainer.boxes[box][slot] : bank->boxes[box][slot];
    }

    // A save-pane slot that is a party member (LGPE) is locked: removing/displacing it would
    // orphan the party pointer, and LGPE keeps a separate party copy that wins on save. No-op for
    // SWSH/LZA (party is a separate structure -> getPartyPosition returns 0).
    bool TrainerViewScreen::storageSlotLocked(int pane, int box, int slot)
    {
        return pane == 0 && trainer.getPartyPosition(box, slot) > 0;
    }

    // The tail of the + handler: prompt about unsaved GAME-save changes, else leave. Split out so the
    // bank's Save/Discard prompt can resume the exit once the user has answered it.
    void TrainerViewScreen::beginAppExit()
    {
        if (hasUnsavedChanges && !saveConfirmActive)
        {
            exitingWithUnsavedChanges = true;
            exitingViaPlus = true; // remember we're exiting via the + button
            saveConfirmActive = true;
            return;
        }
        exitRequested = true;
    }

    // The nav-bar hints for whichever OVERLAY owns input, or "" when none does. See the header for
    // why this is shared rather than written twice.
    //
    // The ORDER here is the input handler's order, not the drawing order: the bar has to name the
    // dialog that will actually answer the next button press. A picker opened from the details page
    // is the case that made this necessary -- it is drawn over that page, so the page's own bar was
    // still advertising Edit / Ribbons / Save while none of them did anything.
    std::string TrainerViewScreen::overlayNavHint() const
    {
        // The keyboard is drawn over whatever raised it and takes every press until it closes.
        if (const Dialogs::KeyboardState *keyboard = Dialogs::activeKeyboard())
            return keyboard->navHint();
        if (pksmImport.resultActive)
            return "A: OK";
        if (pksmImport.previewActive)
            return "A: Import  |  B: Cancel";
        if (fileBrowser.active) // the PKSM bank picker -- no search here, unlike the save picker's
            return "Up/Down: Move  |  L/R: Page  |  A: Open  |  X: All Files  |  B: Up / Close";
        if (pickerActive)
            return "Up/Down: Move  |  L/R: Page  |  A: Select  |  Y: Search  |  B: Cancel";
        if (statEdit.dialogActive)
            return statEdit.mode != Dialogs::StatEditMode::IV
                       ? "Up/Down: IV/AV-EV  |  Arrows: +/- Value  |  ZL/ZR: +/-100  |  A: Confirm  |  B: Cancel"
                       : "Up/Down: IV/AV-EV  |  Arrows: +/- Value  |  A: Confirm  |  B: Cancel";
        if (itemEditDialogActive)
            return "Left/Right: +/-1  |  Up/Down: +/-10  |  ZL/ZR: +/-100  |  A: Confirm  |  B: Cancel";
        if (itemRemoveConfirmActive)
            return "A: Remove  |  B: Cancel";
        if (saveConfirmActive)
            return "Up/Down: Choose  |  A: Save  |  B: Cancel";
        if (releaseConfirmActive)
            return "A: Release  |  B: Cancel";
        // The lossy-transfer notices. These had no entry at all, so the bar behind them kept
        // advertising the storage controls while a Cancel/Continue prompt held input.
        if (moveConfirmActive())
            return "A: Continue  |  B: Cancel";
        if (creator.keepConfirmActive)
            return "A: Keep  |  Y: Discard  |  B: Back";
        if (details.discardConfirmActive)
            return "A: Save  |  Y: Discard  |  B: Back";
        if (tradeEvolveChoiceActive)
            return "A: Choose  |  B: Cancel";
        if (tradeEvolveConfirmActive)
            return "A: Use a save  |  Y: Generate  |  B: Cancel";
        if (tradePartnerTitles.active)
            return "A: Choose  |  B: Cancel";
        // The legality and ribbon lists are deliberately NOT here. They dim the whole page, nav bar
        // included, and carry their own "B / tap: close" line inside the card -- naming it on a bar
        // the overlay has just dimmed would be the second hint this function exists to remove.
        return "";
    }

    // Convert `pk` in place into the format needed to live in `destPane`. The bank (pane 1) accepts
    // anything as-is; a save slot (pane 0) requires the open game's format, so a foreign pokemon is CONVERTED
    // in place if a supported route exists (M5 Phase B). Returns false + posts a status when it can't go
    // there (out-of-dex / unsupported generation pair). Shared by the single-pokemon and bulk placement paths.
    bool TrainerViewScreen::convertForPane(std::unique_ptr<Pokemon::Pokemon> &pokemon, int destPane)
    {
        // bank: store as-is
        if (destPane != 0 || !pokemon) return true;
        if (pokemon->getGameGroup() == trainer.getGameGroup())
        {
            // Same game, so no conversion -- but repair an invalid AffixedRibbon and re-checksum
            // before it enters the save. The re-checksum SHOULD be a no-op writing back the identical
            // value, since the bank stores native bytes untouched; that is exactly why it is cheap
            // insurance, because a stale checksum reaching the game's box writer is a Bad Egg in-game.
            //
            // The ribbon repair is NOT a no-op for legacy stock: mons banked before the creator learned
            // to set AffixedRibbon carry 0, which the game reads as "display ribbon index 0" -- the
            // Kalos Champion ribbon. Cross-generation withdrawals get this inside convert(); a same-group one
            // never calls convert() at all, so without this the 0 rides straight back into the save.
            // Repairing here rather than in the bank keeps the bank's never-mutate rule intact
            // (see Appendix C) -- the fix lands on the way INTO a save, where it belongs.
            Conversion::normalizeAffixedRibbon(*pokemon);
            pokemon->refreshChecksum();
            return true;
        }
        Conversion::Result conversionResult;
        const std::string species = pokemon->species();
        // Who is receiving it. Only the Gen 1/2 route reads this: a Poke Transporter transfer arrives
        // through Bank, which is a trade, so the player opening this save becomes the handling
        // trainer. Every other conversion carries the source's own OT/HT and ignores it.
        const Conversion::ReceivingTrainer receiving{Utils::utf8ToUtf16(trainer.trainerName), trainer.trainerGender};
        // The exact destination game, not just its group: a pokemon transferred down into Gen 3 cannot keep a
        // modern origin (4-bit field) and gets restamped, and "FRLG" alone can't say which half it is.
        auto converted = Conversion::convert(*pokemon, trainer.getGameGroup(), conversionResult,
                                             saveOriginVersion(trainer, titleId), &receiving);
        if (converted)
        {
            // Cross-generation conversion is where the subtle transfer bugs have historically lived
            // (fainted arrivals, deleted moves, garbage levels), so every one gets a line.
            Utils::logTest("CONVERT  species=\"" + species + "\" -> " +
                           std::to_string(static_cast<int>(trainer.getGameGroup())) +
                           " lvl=" + std::to_string(converted->level()) +
                           " hp=" + std::to_string(converted->statHPMax()) + " result=OK");
            pokemon = std::move(converted);
            return true; // convert in place
        }
        Utils::logTest("CONVERT  species=\"" + species + "\" result=REFUSED msg=\"" +
                       Conversion::resultMessage(conversionResult) + "\"");
        storageStatus = Conversion::resultMessage(conversionResult);
        storageStatusFrames = 150; // ~2.5s at 60fps
        return false;
    }

    // True if placing `pk` into `pane` would run a Let's Go conversion (exactly one side is LGPE), which
    // resets AVs/EVs -> the user is asked to acknowledge it, unless the Move warning setting is off.
    // Only save-pane (0) placements convert; the
    // bank (1) stores native bytes, so a deposit never resets anything.
    bool TrainerViewScreen::lgpeConversionInvolved(int pane, const Pokemon::Pokemon *pokemon) const
    {
        if (pane != 0 || !pokemon)
            return false;
        const bool srcGG = pokemon->getGameGroup() == Enums::GameVersion::GG;
        const bool dstGG = trainer.getGameGroup() == Enums::GameVersion::GG;
        return srcGG != dstGG;
    }

    // True if any pokemon in the carried block would run an LGPE conversion when dropped into destPane.
    bool TrainerViewScreen::blockInvolvesLgpe(int destPane) const
    {
        if (destPane != 0)
            return false;
        for (const auto &p : moveMon)
            if (p && lgpeConversionInvolved(destPane, p.get()))
                return true;
        return false;
    }

    // True if placing `pk` into `pane` converts a non-Gen3 pokemon DOWN into Gen 3 (FR/LG or RSE). That path
    // rebuilds the PID to preserve the nature (Gen 3 derives nature FROM the PID), which is destructive and
    // can read as illegal -- so it is the first warning offered when a move matches more than one. Like the
    // other two it obeys the Move warning setting. Only save-pane (0) placements convert; the bank stores
    // native bytes.
    bool TrainerViewScreen::gen3DowngradeInvolved(int pane, const Pokemon::Pokemon *pokemon) const
    {
        if (pane != 0 || !pokemon)
            return false;
        return Enums::isGen3Group(trainer.getGameGroup()) && !Enums::isGen3Group(pokemon->getGameGroup());
    }

    // True if any pokemon in the carried block would run a Gen 3 downgrade when dropped into destPane.
    bool TrainerViewScreen::blockInvolvesGen3Downgrade(int destPane) const
    {
        if (destPane != 0 || !Enums::isGen3Group(trainer.getGameGroup()))
            return false;
        for (const auto &p : moveMon)
            if (p && !Enums::isGen3Group(p->getGameGroup()))
                return true;
        return false;
    }

    // True if placing `pk` into `pane` would run it through Poke Transporter -- i.e. it is a Gen 1 or
    // Gen 2 Pokemon leaving for a later generation. That transfer rewrites more of the Pokemon than
    // any other conversion (IVs, PID, EXP, ability, sometimes the nickname) and cannot be undone,
    // so it outranks the Let's Go notice when a move matches both. Like the other two it obeys the
    // Move warning setting. Only save-pane (0) placements convert; the bank stores native bytes, so
    // a deposit transfers nothing.
    bool TrainerViewScreen::virtualConsoleTransferInvolved(int pane, const Pokemon::Pokemon *pokemon) const
    {
        if (pane != 0 || !pokemon)
            return false;
        return Conversion::virtualConsoleTransferInvolved(*pokemon, trainer.getGameGroup());
    }

    // True if any pokemon in the carried block would run a Poke Transporter transfer into destPane.
    bool TrainerViewScreen::blockInvolvesVirtualConsoleTransfer(int destPane) const
    {
        if (destPane != 0)
            return false;
        for (const auto &p : moveMon)
            if (p && virtualConsoleTransferInvolved(destPane, p.get()))
                return true;
        return false;
    }

    // The Gen 1/2 title a pending Transporter transfer will stamp, and whether that is a guess.
    // Read by the confirm dialog, which is the only place the user is told.
    Enums::GameVersion TrainerViewScreen::pendingTransferOriginVersion() const
    {
        for (const auto &p : moveMon)
            if (p && virtualConsoleTransferInvolved(0, p.get()))
                return Conversion::virtualConsoleOriginVersion(*p);
        return Enums::GameVersion::Invalid;
    }

    // True when the pending Gen 3 move would land at least one Pokemon with a BLANK OT name.
    //
    // Only Gen 3 can lose a trainer name at all -- it is the last generation whose text is a
    // per-language font map -- and it only loses one whose alphabet that map has no room for, so
    // this is the Korean and Chinese case (and a Japanese name going to a save Gen 3 clamps to
    // English). It rides the Gen 3 confirm dialog rather than raising a second one: the Move
    // warning setting gates every bank transfer warning, and a notice outside it would quietly be
    // a warning the user cannot turn off.
    bool TrainerViewScreen::pendingMoveDropsOriginalTrainerName() const
    {
        for (const auto &p : moveMon)
        {
            if (p && gen3DowngradeInvolved(0, p.get()) &&
                !Conversion::canStoreOriginalTrainerName(*p, trainer.getGameGroup()))
                return true;
        }
        return false;
    }

    bool TrainerViewScreen::pendingTransferOriginIsGuess() const
    {
        for (const auto &p : moveMon)
            if (p && virtualConsoleTransferInvolved(0, p.get()))
                return Conversion::virtualConsoleOriginIsGuess(*p);
        return false;
    }

    // build the Ability picker's option list. A species holds one of its ABILITY SLOTS (slot 1,
    // slot 2, hidden), and most species have slot 2 == slot 1, so the deduped list is usually one
    // or two entries -- those are the only legal picks and they render green at the top.
    // "Allow illegal values" appends every remaining ability id after them, EXCEPT on Gen 3: a PK3
    // stores a selector bit rather than an ability id, so nothing outside the two slots is
    // expressible there and offering more would just be a no-op the user can't see.
    // The pokemon's current ability is always listed even when it is not legal, so an existing bad
    // value stays visible and reversible instead of vanishing from its own picker.
    void TrainerViewScreen::buildAbilityPickerOrder(uint16_t species, uint8_t form,
                                                    Enums::GameVersion group, uint16_t current)
    {
        pickerOrder.clear();
        const Pokemon::AbilitySlots slots = Pokemon::getAbilitySlots(species, form, group);
        uint16_t legal[3];
        const int nLegal = slots.distinct(legal);
        for (int index = 0; index < nLegal; ++index)
            pickerOrder.push_back(legal[index]);
        pickerLegalCount = static_cast<int>(pickerOrder.size());

        const bool gen3 = Enums::isGen3Group(group);
        if (g_allowIllegalEdits && !gen3)
        {
            const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Ability);
            for (int aIndex = 0; aIndex < total; ++aIndex)
            {
                bool isLegal = false;
                for (int legalIndex = 0; legalIndex < pickerLegalCount; ++legalIndex)
                    if (pickerOrder[legalIndex] == aIndex)
                    {
                        isLegal = true;
                        break;
                    }
                if (!isLegal)
                    pickerOrder.push_back(aIndex);
            }
        }
        else
        {
            bool alreadyListed = false;
            for (int x : pickerOrder)
                if (x == static_cast<int>(current))
                {
                    alreadyListed = true;
                    break;
                }
            // keep an already-illegal value selectable
            if (!alreadyListed) pickerOrder.push_back(current);
        }

        pickerSel = 0;
        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size()); ++pickerOrderIndex)
            if (pickerOrder[pickerOrderIndex] == static_cast<int>(current))
            {
                pickerSel = pickerOrderIndex;
                break;
            }
    }

    // Creator: fill the species picker with only the species obtainable in the open game (via the
    // personal presence bitmask). Reuses pickerOrder (row -> species id); pickerLegalCount stays 0
    // (no green highlight for species).
    //
    // This is a HARD rule -- "Allow illegal edits" does not offer the rest of the dex. A game that has
    // no entry for a species has no stats, no learnset and no name for it, so what would be created is
    // not an unusual pokemon but a broken one; that toggle is for values a game can hold but shouldn't.
    void TrainerViewScreen::buildCreatorSpeciesOrder()
    {
        buildSpeciesPickerOrder(trainer.getGameGroup(), 0);
    }

    // Shared by the creator (which species may be MADE here) and the details editor (which species
    // a pokemon may BECOME). `current` is pre-selected when it is in the list; pass 0 for none.
    void TrainerViewScreen::buildSpeciesPickerOrder(Enums::GameVersion group, uint16_t current)
    {
        pickerOrder.clear();
        pickerLegalCount = 0;
        const uint8_t bit = personalPresenceBit(group);
        const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Species);
        // Only the Switch titles are in the presence bitmask, so `bit` is 0 for every pre-Switch
        // group -- and reading that as "no data, do not filter" offered a Red/Blue save all 1025
        // species, Mewtwo through Pecharunt. Those dexes are contiguous runs from 1, so a ceiling
        // answers for them; see highestSpeciesIdForGroup. A group with neither a bit nor a ceiling
        // is genuinely unknown and must NOT filter down to nothing.
        const uint16_t highestSpeciesId = highestSpeciesIdForGroup(group);
        // DLC species are deliberately NOT filtered out here, and there is no warning either.
        // Owning a DLC gates the AREAS, not the Pokemon: the patch ships the data to every copy,
        // so a player without the Expansion Pass can be traded a Crown Tundra species (or receive
        // one from HOME) and use it normally. PKHeX agrees -- its legality caps are the full-DLC
        // ones unconditionally. Filtering here would deny content that is perfectly valid.
        for (int sIndex = 1; sIndex < total; ++sIndex)
        { // skip 0 = None
            // ANY form present, not just form 0 -- see speciesPresentIn. Creating one of these picks
            // up the right form automatically: buildDefaultMon starts a pokemon on the first form the
            // game has, so a Braviary made in Legends: Arceus is Hisuian.
            const bool passed = (bit != 0)             ? speciesPresentIn(static_cast<uint16_t>(sIndex), bit)
                                : (highestSpeciesId != 0) ? (sIndex <= static_cast<int>(highestSpeciesId))
                                                          : true;
            if (passed)
                pickerOrder.push_back(sIndex);
        }
        pickerSel = 0;
        // Land on the species the pokemon already is, so the list opens where the user is looking.
        // A pokemon whose species this game has no entry for is simply not in the list -- the picker
        // opens at the top rather than pretending it found it.
        if (current != 0)
        {
            for (size_t pickerOrderIndex = 0; pickerOrderIndex < pickerOrder.size(); ++pickerOrderIndex)
            {
                if (pickerOrder[pickerOrderIndex] == static_cast<int>(current))
                {
                    pickerSel = static_cast<int>(pickerOrderIndex);
                    break;
                }
            }
        }
    }

    // (move half): fill the move picker with the pokemon's LEARNABLE moves first (green + top), then
    // every other move. Learnability is the per-game single-stage pool from the learnset table.
    void TrainerViewScreen::buildMovePickerOrder(uint16_t species, uint8_t form, Enums::GameVersion group,
                                                 uint16_t current)
    {
        pickerOrder.clear();
        const int total = Dialogs::pickerOptionCount(Dialogs::PickerKind::Move);
        // Per GAME only -- DLC moves stay on offer for the same reason DLC species do; the save
        // dialog warns rather than the picker hiding them.
        const auto present = [&](int moveId)
        {
            return Names::isMovePresent(static_cast<uint16_t>(moveId), group);
        };
        std::vector<bool> legal(total, false);
        for (int mIndex = 1; mIndex < total; ++mIndex)
            if (Pokemon::isLearnable(species, form, group, static_cast<uint16_t>(mIndex)) && present(mIndex))
            {
                legal[mIndex] = true;
                pickerOrder.push_back(mIndex);
            }
        pickerLegalCount = static_cast<int>(pickerOrder.size());
        // Then the present-but-illegal moves (not green): still selectable, flagged only by the legality
        // check -- PKHeX-style. Moves that DON'T EXIST in this game (dummied / out of range, e.g. Pound in
        // Legends: Arceus, which the game turns into a Bad Egg) are dropped, matching PKHeX's editor.
        pickerOrder.push_back(0); // None first in the non-legal section, to clear a slot
        for (int mIndex = 1; mIndex < total; ++mIndex)
            if (!legal[mIndex] && present(mIndex))
                pickerOrder.push_back(mIndex);
        pickerSel = 0;
        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size()); ++pickerOrderIndex)
            if (pickerOrder[pickerOrderIndex] == static_cast<int>(current))
            {
                pickerSel = pickerOrderIndex;
                break;
            }
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
    void TrainerViewScreen::buildFormPickerOrder(uint16_t species, uint8_t current, Enums::GameVersion group)
    {
        pickerOrder.clear();
        pickerLegalCount = 0;
        int count = Pokemon::getPersonalInfo(species, 0).formCount;
        if (count < 1)
            count = 1;

        const std::vector<int> offered = selectableForms(species, group);
        for (int fIndex = 0; fIndex < count; ++fIndex)
        {
            bool offer = (fIndex == static_cast<int>(current)); // the current form is always offered
            for (int offset : offered)
                if (offset == fIndex)
                {
                    offer = true;
                    break;
                }
            // The pokemon's CURRENT form is always listed even when temporary or absent from this game -- a
            // save can arrive holding one, and hiding it would make the form both invisible and
            // unfixable. This does not let such a form be applied to anything that isn't already in it.
            if (offer)
                pickerOrder.push_back(fIndex);
        }
        // Guard a form id past the table's formCount (corrupt buffer): keep it selectable so the
        // row still renders and the pick is a no-op rather than an empty list.
        if (pickerOrder.empty())
            pickerOrder.push_back(current);
        pickerSel = 0;
        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size()); ++pickerOrderIndex)
            if (pickerOrder[pickerOrderIndex] == static_cast<int>(current))
            {
                pickerSel = pickerOrderIndex;
                break;
            }
    }

    // Fill the gender picker with the genders this species can be (row -> gender value 0/1/2). For a
    // fixed-gender species that is a single row, which is the point: the row still opens and still says
    // what the pokemon is, there is simply nothing else to pick. See selectableGenders for the rule.
    void TrainerViewScreen::buildGenderPickerOrder(uint16_t species, uint8_t form, uint8_t current)
    {
        pickerOrder.clear();
        pickerLegalCount = 0;
        pickerOrder = selectableGenders(species, form);
        // The pokemon's CURRENT gender is always listed, the same rule the form picker uses: a save can
        // arrive holding an impossible one (an older PKSE wrote them, and other tools still do), and
        // hiding it would leave it both invisible and unfixable. Listing it is what makes it fixable.
        bool alreadyListed = false;
        for (int g : pickerOrder)
            if (g == static_cast<int>(current))
            {
                alreadyListed = true;
                break;
            }
        if (!alreadyListed && current <= 2)
            pickerOrder.push_back(current);
        pickerSel = 0;
        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size()); ++pickerOrderIndex)
            if (pickerOrder[pickerOrderIndex] == static_cast<int>(current))
            {
                pickerSel = pickerOrderIndex;
                break;
            }
    }

    // Whether the Gender row does anything. It is read-only when there is nothing to change it TO:
    // a male-only Braviary, a female-only Miltank, a genderless Magnemite. Offering a one-row picker
    // there was just a dead end, so the row now shows the gender and the cursor skips it.
    //
    // The one exception keeps it editable: a pokemon that already holds a gender its species cannot have.
    // Older PKSE builds wrote those and other tools still do, and the picker listing both the
    // impossible current value and the legal one is the only way to correct it -- locking the row
    // would make a female Braviary permanent.
    namespace
    {
        /// Which of the two Hoenn bag layouts the loaded save is. Only Trainer3RSE knows -- Emerald
        /// carries a security key at Small+0xAC and Ruby/Sapphire write 0 there -- and RTTI is off
        /// project-wide, so this casts on the group the caller has already checked, exactly as the
        /// rest of PKSE dispatches. Calling it for any other group is a bug, not a fallback.
        Trainer::Gen3HoennLayout hoennLayout(const Trainer::Trainer &loadedSave)
        {
            return static_cast<const Trainer::Trainer3RSE &>(loadedSave).layout();
        }
    }

    Dialogs::PickerKind TrainerViewScreen::pouchPickerKind() const
    {
        switch (trainer.getGameGroup())
        {
        case Enums::GameVersion::RBY:
            return Dialogs::PickerKind::PouchItemG1;
        case Enums::GameVersion::FRLG:
        case Enums::GameVersion::RSE:
            return Dialogs::PickerKind::PouchItemG3; // one Gen 3 item id space, all five GBA games
        default:
            return Dialogs::PickerKind::PouchItem;
        }
    }

    bool TrainerViewScreen::genderEditable(const Pokemon::Pokemon &p) const
    {
        // A format with no gender field has nothing to write, whatever the species table says the
        // species could be. selectableGenders() answers from the MODERN table, so without this a
        // Gen 1 Bulbasaur offers a Male/Female picker whose result setGender() throws away.
        if (!p.hasStoredGender())
            return false;
        const std::vector<int> selectableGenderList = selectableGenders(p.speciesID(), p.form());
        // a real choice
        if (selectableGenderList.size() > 1) return true;
        return selectableGenderList.empty() ||
               selectableGenderList[0] != static_cast<int>(p.gender()); // wrong gender -> fixable
    }

    // Resolve the Pokemon the details editor is currently targeting.
    Pokemon::Pokemon *TrainerViewScreen::detailsTargetPokemon()
    {
        switch (details.source)
        {
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
    void TrainerViewScreen::mirrorEditedPartyMember()
    {
        if (details.source == EditSource::Box)
        {
            trainer.mirrorPartyMemberFromBox(static_cast<size_t>(selectedBoxIndex),
                                             static_cast<size_t>(selectedItemIndex));
        }
        else if (details.source == EditSource::Party)
        {
            trainer.mirrorPartyMemberFromParty(static_cast<size_t>(details.partyIndex));
        }
        // EditSource::Bank has no party duplication.
    }

    // Modal edits mutate the live pokemon in place. We snapshot the target's decrypted bytes on open;
    // pokemonEditDirty() compares against it to drive the top-bar "Unsaved changes" marker. X (Save)
    // re-snapshots (commit -> marker clears). Closing the page WITHOUT Save calls restoreEditTarget(),
    // which rolls the pokemon back to the snapshot, so unsaved IV/EV/etc. edits are DISCARDED -- the
    // individual Save button is the only commit point.

    void TrainerViewScreen::snapshotEditTarget()
    {
        details.editSnapshot.clear();
        if (const Pokemon::Pokemon *t = detailsTargetPokemon())
        {
            const auto dataBytes = t->getData();
            details.editSnapshot.assign(dataBytes.begin(), dataBytes.end());
        }
    }

    bool TrainerViewScreen::pokemonEditDirty()
    {
        if (details.editSnapshot.empty())
            return false;
        const Pokemon::Pokemon *t = detailsTargetPokemon();
        if (!t)
            return false;
        const auto dataBytes = t->getData();
        return dataBytes.size() != details.editSnapshot.size() ||
               !std::equal(dataBytes.begin(), dataBytes.end(), details.editSnapshot.begin());
    }

    // Roll the details target back to the snapshot: copy the baseline bytes over the live pokemon, then
    // re-mirror so an LGPE box/party twin reverts too. Because edits mutate the live buffer in place,
    // undoing them just means restoring the captured bytes (checksum + stats included -- the snapshot
    // is the whole serialized record). The snapshot is re-taken on X = Save, so this discards only the
    // edits made SINCE the last save. Called on close-without-Save; a no-op when nothing changed.
    void TrainerViewScreen::restoreEditTarget()
    {
        if (details.editSnapshot.empty())
            return;
        Pokemon::Pokemon *t = detailsTargetPokemon();
        if (!t)
            return;
        auto dataBytes = t->getData();
        // fixed PK buffer -> always equal; guard anyway
        if (dataBytes.size() != details.editSnapshot.size()) return;
        std::copy(details.editSnapshot.begin(), details.editSnapshot.end(), dataBytes.begin());
        mirrorEditedPartyMember();
    }

    void TrainerViewScreen::closeDetailsModal()
    {
        details.active = false;
        details.source = EditSource::Box;
        details.selectedField = 0;
        details.legalityOverlay = false;
        details.ribbonOverlay = false;
        details.discardConfirmActive = false; // never let it survive to overlay the next page
        tradeEvolveConfirmActive = false;     // ...and neither may the trade-evolve choices
        tradeEvolveChoiceActive = false;
        tradePartnerTitles.active = false;    // ...nor the donor title picker
        details.editSnapshot.clear();
    }

    // The bag keeps "owned but empty" slots (count 0 — e.g. used-up story key items) that we must
    // preserve on save (PKHeX keeps them positionally), but they shouldn't clutter the UI. This
    // returns the raw indices of the current pouch's items worth showing/editing (count > 0), and
    // is the single source of truth so the panel, navigation, and edit all stay in lockstep.
    // Returns the id of the touch button under a fresh tap this frame, or -1 if none. Buttons are
    // captured during the previous frame's draw (only the active overlay populates touchButtons).
    int TrainerViewScreen::touchedButtonId(const TouchInput &touch) const
    {
        if (!touch.justPressed())
            return -1;
        for (const auto &b : touchButtons)
        {
            if (touch.x() >= b.hitX && touch.x() < b.hitX + b.hitWidth &&
                touch.y() >= b.hitY && touch.y() < b.hitY + b.hitHeight)
                return b.buttonId;
        }
        return -1;
    }

    // Rename the focused box via the Switch keyboard.
    //
    // Safe to call from update(): the UI loop is update() -> draw() -> flush(), so no NanoVG frame
    // is open when swkbd suspends the app. Calling this from draw() would strand a half-built frame.
    void TrainerViewScreen::renameBox(int boxIndex)
    {
        const size_t maxChars = trainer.getMaxBoxNameLength();
        if (maxChars == 0)
            return;
        if (boxIndex < 0 || boxIndex >= static_cast<int>(trainer.boxNames.size()))
            return;

        const std::string current = trainer.boxNames[boxIndex];
        const Utils::KeyboardResult promptResult =
            Utils::promptText("Rename Box", "Box name", current, static_cast<int>(maxChars));
        // cancel means "leave it alone", NOT "clear it"
        if (!promptResult.accepted) return;
        if (promptResult.text == current)
            return;

        // Refuse rather than mangle: Gen 3's character set is fixed and predates Unicode, so a name
        // the keyboard was happy to produce may be unwritable there.
        if (!trainer.canStoreBoxName(promptResult.text))
        {
            Utils::logTest("BOXNAME  box=" + std::to_string(boxIndex + 1) +
                           " new=\"" + promptResult.text + "\" result=REFUSED_CHARSET");
            storageStatus = "This game can't store one of those characters.";
            storageStatusFrames = 240;
            return;
        }

        // An empty name is legitimate: the games treat a blank as "use the default", and
        // parseBoxNames already turns a blank back into "Box N" on reload.
        Utils::logTest("BOXNAME  box=" + std::to_string(boxIndex + 1) +
                       " old=\"" + current + "\" new=\"" + promptResult.text + "\" result=OK");
        trainer.boxNames[boxIndex] = promptResult.text;
        // Only a box marked here is written on save. boxNames also holds display defaults for boxes
        // the save leaves unnamed, and persisting those would invent names the player never set.
        trainer.markBoxNameDirty(static_cast<size_t>(boxIndex));
        hasUnsavedChanges = true;
        storageStatus =
            promptResult.text.empty() ? "Box name cleared." : ("Box renamed to \"" + promptResult.text + "\".");
        storageStatusFrames = 180;
    }

    // Rename a BANK box. Unlike save boxes, bank names are PKSE-internal (stored UTF-8 in bank.dat,
    // no game charset limit), and the default label is "Bank N" rather than a stored string -- so an
    // empty name means "use the default", and the keyboard starts from the raw stored name (blank by
    // default) rather than from the "Bank N" label.
    void TrainerViewScreen::renameBankBox(int box)
    {
        if (!bank || box < 0 || box >= static_cast<int>(Trainer::Bank::BANK_BOX_COUNT))
            return;
        const std::string current = bank->boxNames[box];
        const Utils::KeyboardResult promptResult =
            Utils::promptText("Rename Bank Box", "Bank box name", current,
                              static_cast<int>(Trainer::Bank::MAX_BOX_NAME_LEN));
        // cancel = leave it alone
        if (!promptResult.accepted) return;
        if (promptResult.text == current)
            return;

        bank->boxNames[box] = promptResult.text;
        // A bank rename is an unsaved BANK change: it feeds serialize()/hasChanged(), so leaving the
        // storage view now raises the Save/Discard prompt just like a deposit does. (Not tied to the
        // trainer's hasUnsavedChanges, which is the save file's dirty flag.)
        Utils::logTest("BANKBOXNAME  box=" + std::to_string(box + 1) +
                       " new=\"" + promptResult.text + "\" result=OK");
        storageStatus = promptResult.text.empty() ? "Bank box name reset to default."
                                         : ("Bank box renamed to \"" + promptResult.text + "\".");
        storageStatusFrames = 180;
    }

    size_t TrainerViewScreen::emptyBankBoxCount() const
    {
        if (!bank)
            return 0;
        size_t byteCount = 0;
        for (size_t bANK_BOXIndex = 0; bANK_BOXIndex < Trainer::Bank::BANK_BOX_COUNT; ++bANK_BOXIndex)
        {
            bool empty = true;
            for (size_t slotIndex = 0; slotIndex < Trainer::Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
                if (bank->boxes[bANK_BOXIndex][slotIndex])
                {
                    empty = false;
                    break;
                }
            if (empty)
                ++byteCount;
        }
        return byteCount;
    }

    void TrainerViewScreen::openPKSMImportBrowser()
    {
        if (!bank)
            return;
        pksmImport.reset();
        // PKSM folders come first so existing PKSM users still land on their bank files.
        // Native imports use the same browser, but always stage into the in-memory Bank
        // and leave persistence to Storage's existing Save/Discard prompt.
        fileBrowserPurpose = FileBrowserPurpose::PKSMBank;
        std::vector<std::string> importExtensions = Trainer::PokemonFile::extensions();
        importExtensions.push_back(".bnk");
        importExtensions.push_back(".bin"); // hand-copied pre-2019 PKSM bank.bin
        fileBrowser.open("Import Pokemon / PKSM Bank",
                         {"sdmc:/3ds/PKSM/banks",
                          "sdmc:/3ds/PKSM/extDataBackup/banks",
                          "sdmc:/3ds/PKSM",
                          "sdmc:/PKSM/banks",
                          "sdmc:/PKSE/exports",
                          "sdmc:/PKSE",
                          "sdmc:/"},
                         importExtensions);
        Utils::logEventToFile("PKSMIMPORT action=BROWSE dir=\"" + fileBrowser.directory + "\"");
    }

    void TrainerViewScreen::openTradePartnerBrowser()
    {
        tradePartnerPick = TradePartnerPickState{};
        fileBrowserPurpose = FileBrowserPurpose::TradePartner;
        // The same filters the save picker uses, so the list is exactly what openExternalSave can
        // open -- including a 3DS `main`, which has no extension and is matched by whole name.
        fileBrowser.open("Select a Save to Trade With",
                         {"sdmc:/PKSE/saves/", "sdmc:/PKSE/", "sdmc:/roms/", "sdmc:/retroarch/saves/",
                          "sdmc:/emulators/", "sdmc:/"},
                         Save::saveFileExtensions(), Save::saveFileExactNames());
        Utils::logEventToFile("TRADEPARTNER action=BROWSE dir=\"" + fileBrowser.directory + "\"");
    }

    void TrainerViewScreen::tradeEvolveWithChosenSave(const std::string &path)
    {
        fileBrowser.close();
        tradePartnerPick = TradePartnerPickState{};
        Pokemon::Pokemon *pokemon = detailsTargetPokemon();
        if (!pokemon)
            return;

        size_t size = 0;
        // readAllBytes hands back a malloc'd buffer, so it is freed with free().
        uint8_t *file = Utils::readAllBytes(path.c_str(), &size);
        if (!file || size == 0)
        {
            if (file)
                std::free(file);
            tradePartnerPick.refusalText = "Could not read";
            Utils::logErrorToFile("trade partner: could not read file", path.c_str());
            return;
        }
        std::vector<uint8_t> bytes(file, file + size);
        std::free(file);

        std::string label;
        std::unique_ptr<Trainer::Trainer> donorSave = Save::openExternalSave(std::move(bytes), path, &label);
        if (!donorSave)
        {
            tradePartnerPick.refusalText = "Not a save PKSE opens";
            return;
        }

        const Trainer::TradeDonor donor = Trainer::buildTradeDonor(*donorSave, *pokemon);
        if (!donor.isAccepted())
        {
            // REFUSED IS AN ANSWER, NOT A FAILURE -- the row says which one, because a pick that
            // silently does nothing is indistinguishable from one that did not register.
            tradePartnerPick.refusalText = Trainer::tradeDonorRefusalText(donor.refusal);
            if (g_debugLogging)
            {
                Utils::logEventToFile(std::string("TRADEPARTNER action=REFUSED ") +
                                      Utils::logField("save", label) + " " +
                                      Utils::logField("reason", tradePartnerPick.refusalText));
            }
            return;
        }

        tradePartnerPick.acceptedName = Utils::utf16ToUtf8(donor.partner.trainerName);
        if (g_debugLogging)
            Utils::logEventToFile(std::string("TRADEPARTNER action=SAVE ") + Utils::logField("save", label));
        finishTradeEvolve(*pokemon, donor.partner);
    }

    void TrainerViewScreen::openTradePartnerTitlePicker()
    {
        tradePartnerTitles = TradePartnerTitleState{};
        const Pokemon::Pokemon *pokemon = detailsTargetPokemon();
        if (!pokemon)
            return;
        for (const ConsoleSaveEntry &entry : consoleSaves)
        {
            // THE SAVE ALREADY OPEN IS NEVER OFFERED. Its trainer is the OT of anything caught in
            // it, so buildTradeDonor would refuse it -- listing it would be a row that exists only
            // to say no. A DIFFERENT USER'S copy of the same title is still a real partner, which
            // is the only donor a single-title game like Legends: Z-A can have.
            const bool isTheOpenSave = entry.titleId == titleId &&
                                       std::memcmp(&entry.accountUid, &userUid, sizeof(AccountUid)) == 0;
            if (isTheOpenSave)
                continue;
            if (Trainer::titleCanTradeWith(entry.gameVersion, *pokemon))
                tradePartnerTitles.candidates.push_back(entry);
        }
        tradePartnerTitles.active = true;
        Utils::logEventToFile("TRADEPARTNER action=TITLES offered=" +
                              std::to_string(tradePartnerTitles.candidates.size()));
    }

    void TrainerViewScreen::tradeEvolveWithChosenTitle(const ConsoleSaveEntry &entry)
    {
        tradePartnerTitles.active = false;
        tradePartnerPick = TradePartnerPickState{};
        Pokemon::Pokemon *pokemon = detailsTargetPokemon();
        if (!pokemon)
            return;

        // READ THE DONOR'S LIVE SAVE INTO A SCRATCH DIRECTORY, never into the user's backup list.
        // copySaveDataTo exists for exactly this -- its own comment says putting a transient
        // working copy among the user's backups would make the backup screen unusable in a week --
        // and the directory is removed again below whatever the outcome.
        const std::string scratchDirectory = BASE_SAVE_DIRECTORY + "/tradepartner";
        mkdir(BASE_SAVE_DIRECTORY.c_str(), 0777);
        Utils::deleteDirectoryRecursive(scratchDirectory.c_str()); // never read a previous attempt
        if (mkdir(scratchDirectory.c_str(), 0777) != 0 && errno != EEXIST)
        {
            tradePartnerPick.refusalText = "Could not read that save";
            Utils::logErrorToFile("trade partner: could not create the scratch directory",
                                  scratchDirectory.c_str());
            return;
        }
        if (!Utils::copySaveDataTo(entry.accountUid, entry.titleId, scratchDirectory.c_str()))
        {
            tradePartnerPick.refusalText = "Could not read that save";
            Utils::deleteDirectoryRecursive(scratchDirectory.c_str());
            return;
        }

        std::unique_ptr<Trainer::Trainer> donorSave =
            Save::readTitleSaveAsTrainer(scratchDirectory.c_str(), entry.titleId);
        if (!donorSave)
        {
            tradePartnerPick.refusalText = "Could not read that save";
        }
        else
        {
            const Trainer::TradeDonor donor = Trainer::buildTradeDonor(*donorSave, *pokemon);
            if (!donor.isAccepted())
            {
                tradePartnerPick.refusalText = Trainer::tradeDonorRefusalText(donor.refusal);
                if (g_debugLogging)
                {
                    Utils::logEventToFile(std::string("TRADEPARTNER action=REFUSED ") +
                                          Utils::logField("title", entry.label) + " " +
                                          Utils::logField("reason", tradePartnerPick.refusalText));
                }
            }
            else
            {
                if (g_debugLogging)
                    Utils::logEventToFile(std::string("TRADEPARTNER action=TITLE ") +
                                          Utils::logField("title", entry.label) + " " +
                                          Utils::logField("user", entry.userName));
                finishTradeEvolve(*pokemon, donor.partner);
            }
        }

        // The donor's bytes were only ever wanted for its trainer block.
        Utils::deleteDirectoryRecursive(scratchDirectory.c_str());
    }

    void TrainerViewScreen::beginTradeEvolve(int candidateIndex)
    {
        tradeEvolveChoiceActive = false;
        tradeEvolveCandidateIndex = candidateIndex;
        Pokemon::Pokemon *pokemon = detailsTargetPokemon();
        if (!pokemon)
            return;
        if (pokemon->hasHandler())
        {
            // FROM GEN 6 ON THE RECORD KEEPS THE OTHER TRAINER, and that residue is the only thing
            // that says the trade happened -- so who it was is a question only the user can answer.
            // Gens 1-5 are never asked, because a trade writes nothing to those formats at all.
            tradeEvolveConfirmActive = true;
            return;
        }
        const uint16_t previousSpeciesId = pokemon->speciesID();
        if (!Pokemon::applyTradeEvolution(*pokemon, candidateIndex))
            return;
        // Logged AFTER, so the line describes the pokemon as it landed.
        if (g_debugLogging)
        {
            Utils::logEventToFile(std::string("MON action=TRADE_EVOLVE ") +
                                  Utils::logField("from", Trainer::getSpeciesName(previousSpeciesId)) + " " +
                                  Utils::logField("to", Trainer::getSpeciesName(pokemon->speciesID())) + " " +
                                  Utils::briefPokemon(*pokemon));
        }
        hasUnsavedChanges = true;
        mirrorEditedPartyMember();
    }

    void TrainerViewScreen::tradeEvolveWithGeneratedTrainer()
    {
        tradeEvolveConfirmActive = false;
        tradePartnerPick = TradePartnerPickState{};
        Pokemon::Pokemon *pokemon = detailsTargetPokemon();
        if (!pokemon)
            return;
        finishTradeEvolve(*pokemon, Pokemon::generateTradePartner(*pokemon));
    }

    void TrainerViewScreen::finishTradeEvolve(Pokemon::Pokemon &pokemon, const Pokemon::TradePartner &partner)
    {
        // TRADE FIRST, THEN EVOLVE -- the order the games use, and it is not cosmetic: the new
        // handler's friendship is set from the species' base, and at that moment the species is
        // still the PRE-evolution. Evolving first would start the handler on the evolved species'
        // base instead, which is not what arriving in the other game does.
        const uint16_t previousSpeciesId = pokemon.speciesID();
        const bool traded = Pokemon::applyTradeRoundTrip(pokemon, partner);
        const bool evolved = Pokemon::applyTradeEvolution(pokemon, tradeEvolveCandidateIndex);
        if (!evolved)
        {
            tradePartnerPick.refusalText = traded ? "Traded, but did not evolve" : "Could not apply";
            return;
        }
        hasUnsavedChanges = true;
        mirrorEditedPartyMember();
        if (g_debugLogging)
        {
            Utils::logEventToFile(std::string("MON action=TRADE_EVOLVE ") +
                                  Utils::logField("from", Trainer::getSpeciesName(previousSpeciesId)) + " " +
                                  Utils::logField("to", Trainer::getSpeciesName(pokemon.speciesID())) + " " +
                                  Utils::logField("handler", Utils::utf16ToUtf8(partner.trainerName)) + " " +
                                  Utils::logField("handlerStamped", traded ? "Y" : "N") + " " +
                                  Utils::briefPokemon(pokemon));
        }
    }

    void TrainerViewScreen::scanChosenPKSMBank(const std::string &path)
    {
        pksmImport.reset();
        pksmImport.fileName = leafName(path);

        size_t size = 0;
        // readAllBytes hands back a malloc'd buffer, so it is freed with free() rather than the
        // delete[] most of the older call sites use.
        uint8_t *file = Utils::readAllBytes(path.c_str(), &size);
        if (!file || size == 0)
        {
            if (file)
                std::free(file);
            pksmImport.importer.fail("Couldn't read that file from the SD card.");
            pksmImport.resultActive = true;
            Utils::logErrorToFile("PKSM import: could not read file", path.c_str());
            return;
        }

        const bool passed = pksmImport.importer.scan(std::span<const uint8_t>(file, size));
        std::free(file);

        // Box names live in a `<name>.json` sidecar beside the .bnk. Optional -- a missing or
        // unreadable one just means the imported boxes keep their default "Bank N" labels.
        if (passed)
        {
            const size_t dotPosition = path.find_last_of('.');
            const size_t slash = path.find_last_of('/');
            if (dotPosition != std::string::npos && (slash == std::string::npos || dotPosition > slash))
            {
                const std::string sidecar = path.substr(0, dotPosition) + ".json";
                size_t jsonSize = 0;
                uint8_t *json = Utils::readAllBytes(sidecar.c_str(), &jsonSize);
                if (json)
                {
                    pksmImport.importer.setBoxNames(
                        Trainer::parsePKSMBoxNames(std::span<const uint8_t>(json, jsonSize)));
                    std::free(json);
                }
            }
        }

        const auto &r = pksmImport.importer.report();
        Utils::logEventToFile("PKSMIMPORT action=SCAN file=\"" + pksmImport.fileName + "\"" +
                              " result=" + (passed ? "OK" : "REJECTED") +
                              " boxes=" + std::to_string(r.sourceBoxes) +
                              " occupied=" + std::to_string(r.occupied) +
                              " importable=" + std::to_string(r.importable) +
                              " unsupported=" + std::to_string(r.unsupported) +
                              " damaged=" + std::to_string(r.damaged));

        // A file we cannot use, and a file with nothing PKSE can store, both go straight to the
        // result screen -- there is no decision left to offer.
        if (!passed || r.importable == 0)
            pksmImport.resultActive = true;
        else
            pksmImport.previewActive = true;
    }

    // Owns every button while an import step is on screen -- including +, because closing the app is
    // not an answer to "import this file?". Same reasoning as the bank Save/Discard prompt.
    // Layered back-to-front: result over preview over browser, so the topmost one answers first.
    void TrainerViewScreen::handlePKSMImportInput(u64 buttonsDown, const TouchInput &touch)
    {
        const int tapped = touchedButtonId(touch);

        if (pksmImport.resultActive)
        {
            if ((buttonsDown & (HidNpadButton_A | HidNpadButton_B)) || tapped == 0)
            {
                const bool committed = pksmImport.committed;
                const size_t placed = pksmImport.importer.report().placed;
                pksmImport.reset();
                fileBrowser.close();
                // The bank is not on disk yet, and the exit prompt is easy to dismiss out of habit,
                // so say plainly that saving is still required.
                if (committed && placed > 0)
                    postStatus(std::to_string(placed) + " Pokemon imported - choose Save when you leave Storage.", 480);
            }
            return;
        }

        // --- Preview: the decision. Cancel drops BACK to the browser (still open underneath), so
        //     opening the wrong file costs one press rather than restarting the flow. ---
        if (pksmImport.previewActive)
        {
            if ((buttonsDown & HidNpadButton_A) || tapped == 1)
            {
                pksmImport.previewActive = false;
                commitPKSMImport();
                pksmImport.resultActive = true;
            }
            else if ((buttonsDown & HidNpadButton_B) || tapped == 0)
            {
                Utils::logEventToFile("PKSMIMPORT action=CANCEL file=\"" + pksmImport.fileName + "\"");
                pksmImport.reset(); // frees the staged Pokemon; the browser is left up underneath
            }
            return;
        }

        if (!fileBrowser.active)
            return;

        if (buttonsDown & HidNpadButton_Up)
            fileBrowser.move(-1);
        if (buttonsDown & HidNpadButton_Down)
            fileBrowser.move(1);
        if (buttonsDown & (HidNpadButton_L | HidNpadButton_Left))
            fileBrowser.move(-Dialogs::FileBrowserState::VISIBLE_ROWS);
        if (buttonsDown & (HidNpadButton_R | HidNpadButton_Right))
            fileBrowser.move(Dialogs::FileBrowserState::VISIBLE_ROWS);

        if (buttonsDown & HidNpadButton_X)
        {
            // The extension filter is a convenience, not a rule -- a bank copied off with a different
            // name would otherwise be invisible with no way to reach it.
            fileBrowser.showAllFiles = !fileBrowser.showAllFiles;
            fileBrowser.refresh();
            return;
        }
        if (buttonsDown & HidNpadButton_B)
        {
            if (!fileBrowser.goUp())
            {
                fileBrowser.close();
                pksmImport.reset();
            } // at the root: leave
            return;
        }

        bool activate = (buttonsDown & HidNpadButton_A) != 0;
        if (tapped >= 0)
        {
            fileBrowser.selectedIndex = tapped;
            fileBrowser.move(0);
            activate = true;
        }
        if (activate && fileBrowser.activate())
        {
            const std::string picked = fileBrowser.chosenPath;
            fileBrowser.chosenPath.clear();
            if (fileBrowserPurpose == FileBrowserPurpose::TradePartner)
            {
                tradeEvolveWithChosenSave(picked);
            }
            else if (Trainer::PokemonFile::supportsFileName(picked))
            {
                size_t importBox = 0, importSlot = 0;
                std::string error;
                if (bank && Trainer::PokemonFile::importIntoBank(*bank, picked, &importBox, &importSlot, &error))
                {
                    stBankBox = static_cast<int>(importBox);
                    stBankSlot = static_cast<int>(importSlot);
                    storageFocusPane = 1;
                    fileBrowser.close();
                    pksmImport.reset();
                    postStatus("Pokemon imported - choose Save when you leave Storage.", 480);
                    Utils::logEventToFile("POKEMONFILE action=IMPORT result=OK file=\"" + leafName(picked) +
                                          "\" box=" + std::to_string(importBox + 1) +
                                          " slot=" + std::to_string(importSlot + 1));
                }
                else
                {
                    const std::string reason = error.empty() ? "could not read Pokemon file" : error;
                    postStatus("Import failed: " + reason, 480);
                    Utils::logEventToFile("POKEMONFILE action=IMPORT result=FAILED file=\"" + leafName(picked) +
                                          "\" reason=\"" + reason + "\"");
                }
            }
            else
            {
                scanChosenPKSMBank(picked);
            }
        }
    }

    void TrainerViewScreen::commitPKSMImport()
    {
        if (!bank)
            return;
        const size_t placed = pksmImport.importer.commit(*bank);
        pksmImport.committed = true;

        const auto &r = pksmImport.importer.report();
        Utils::logEventToFile("PKSMIMPORT action=COMMIT file=\"" + pksmImport.fileName + "\"" +
                              " placed=" + std::to_string(placed) +
                              " boxes=" + std::to_string(r.boxesUsed) +
                              " scattered=" + std::to_string(r.scattered) +
                              " overflow=" + std::to_string(r.overflow) +
                              " names=" + std::to_string(r.namesCarried));
        Utils::logTest("PKSMIMPORT placed=" + std::to_string(placed) +
                       " overflow=" + std::to_string(r.overflow));

        // Land the cursor on the first imported box so the result is visible the moment the dialog
        // closes -- an import that appears to do nothing is indistinguishable from one that failed.
        if (placed > 0)
        {
            for (size_t bANK_BOXIndex = 0; bANK_BOXIndex < Trainer::Bank::BANK_BOX_COUNT; ++bANK_BOXIndex)
            {
                bool movedAny = false;
                for (size_t slotIndex = 0; slotIndex < Trainer::Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
                    if (bank->boxes[bANK_BOXIndex][slotIndex])
                    {
                        movedAny = true;
                        break;
                    }
                if (movedAny)
                {
                    stBankBox = static_cast<int>(bANK_BOXIndex);
                    stBankSlot = 0;
                    storageFocusPane = 1;
                    break;
                }
            }
        }
    }

    // Re-stamp the trainer identity your Pokemon store after a name edit, so they stay
    // recognized as yours (see the header). Two independent matches per pokemon:
    //   (1) OT  -- you caught it: match OT ID32 + the carried OT name.
    //   (2) HT  -- it was traded to you (Gen 7+): match the carried HT name (HT has no TID/SID).
    // Genuinely foreign stamps (someone else's OT/HT) are left untouched. Walks party + boxes only --
    // the cross-game bank is deliberately excluded. Returns the count of mons actually changed.
    int TrainerViewScreen::restampCaughtPokemonIdentity(const std::u16string &caughtName)
    {
        const std::u16string newName = Utils::utf8ToUtf16(trainer.trainerName);
        const uint8_t newGender = trainer.trainerGender;
        const uint32_t id32 = trainer.ID32;
        int changed = 0;
        auto restamp = [&](Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->speciesID() == 0)
                return;
            bool changedAny = false;
            // (1) You are the ORIGINAL TRAINER.
            if (pokemon->id32() == id32 && pokemon->otName() == caughtName)
            {
                if (pokemon->otName() != newName)
                {
                    pokemon->setOTName(newName);
                    changedAny = true;
                }
                if (pokemon->otGender() != newGender)
                {
                    pokemon->setOTGender(newGender);
                    changedAny = true;
                }
            }
            // (2) You are the HANDLING TRAINER of a traded-in pokemon. FRLG has no HT (htName() is empty),
            // so it never matches; the empty guard also stops untraded mons (empty HT) matching a
            // cleared trainer name.
            if (!caughtName.empty() && pokemon->htName() == caughtName)
            {
                if (pokemon->htName() != newName)
                {
                    pokemon->setHTName(newName);
                    changedAny = true;
                }
                if (pokemon->htGender() != newGender)
                {
                    pokemon->setHTGender(newGender);
                    changedAny = true;
                }
            }
            // Trainer fields don't affect stats, so refresh the checksum only (no recalculateStats).
            if (changedAny)
            {
                pokemon->refreshChecksum();
                ++changed;
            }
        };
        for (auto &pokemon : trainer.party)
            restamp(pokemon.get());
        for (auto &box : trainer.boxes)
            for (auto &pokemon : box)
                restamp(pokemon.get());
        return changed;
    }

    // Append " Updated OT on N of your Pokemon." to a status line when a re-stamp touched any.
    static std::string withOtRestampNote(std::string message, int count)
    {
        if (count > 0)
            message += " Updated OT on " + std::to_string(count) + " of your Pokemon.";
        return message;
    }

    // Edit the trainer's OT name via the Switch keyboard. Mirrors renameBox: a cancel leaves the
    // name untouched, an unchanged result is a no-op, and a name the game's glyph table can't store is
    // refused rather than silently mangled (Gen 3 has ~70 glyphs; canStoreBoxName is the shared check).
    // Mirrors PKHeX TrainerNameVerifier.ContainsTooManyNumbers. The games cap how many digits a
    // trainer name may hold, separately from its length, and a name no longer than the cap is
    // exempt -- so a five-digit cap accepts "12345" but not "123456". Counts characters rather than
    // bytes (the name is UTF-8 here) and treats full-width digits as digits, matching .NET's
    // char.IsNumber, since a Japanese keyboard produces those.
    static bool nameHasTooManyDigits(const std::u16string &name, int maxDigits)
    {
        // generation with no cap
        if (maxDigits < 0) return false;
        // short enough to be exempt
        if (name.size() <= static_cast<size_t>(maxDigits)) return false;
        int digits = 0;
        for (char16_t character : name)
        {
            if ((character >= u'0' && character <= u'9') || (character >= 0xFF10 && character <= 0xFF19))
                ++digits;
        }
        return digits > maxDigits;
    }

    void TrainerViewScreen::editTrainerName()
    {
        const std::string current = trainer.trainerName;
        const Utils::KeyboardResult promptResult =
            Utils::promptText("Trainer Name", "OT name", current,
                              static_cast<int>(trainer.getMaxTrainerNameLength()));
        // cancel means "leave it alone", not "clear it"
        if (!promptResult.accepted) return;
        if (promptResult.text == current)
            return;
        if (!trainer.canStoreBoxName(promptResult.text))
        {
            Utils::logTest("TRAINERNAME new=\"" + promptResult.text + "\" result=REFUSED_CHARSET");
            postStatus("This game can't store those characters (A-Z, 0-9, and ! ? . - only).", 240);
            return;
        }
        const int maxDigits = trainer.getMaxTrainerNameDigits();
        if (nameHasTooManyDigits(Utils::utf8ToUtf16(promptResult.text), maxDigits))
        {
            Utils::logTest("TRAINERNAME new=\"" + promptResult.text + "\" result=REFUSED_DIGITS");
            postStatus("This game allows at most " + std::to_string(maxDigits) +
                           " numbers in a trainer name.",
                       240);
            return;
        }
        Utils::logTest("TRAINERNAME old=\"" + current + "\" new=\"" + promptResult.text + "\" result=OK");
        // Match owned mons by the name they currently carry (the pre-rename name) BEFORE we change it.
        const std::u16string caughtName = Utils::utf8ToUtf16(current);
        trainer.trainerName = promptResult.text;
        const int count = restampCaughtPokemonIdentity(caughtName);
        hasUnsavedChanges = true;
        Utils::logTest("TRAINERNAME restamped=" + std::to_string(count));
        postStatus(
            withOtRestampNote(promptResult.text.empty() ? "Trainer name cleared." : "Trainer name updated.", count),
            200);
    }

    // Edit the trainer's money via the number pad, clamped to this game's cap. promptNumber both
    // widths the keypad to the max and clamps the returned value, so an out-of-range entry can't slip in.
    void TrainerViewScreen::editTrainerMoney()
    {
        const Utils::NumberResult promptResult =
            Utils::promptNumber("Money", static_cast<int>(trainer.money), 0,
                                static_cast<int>(trainer.getMaxMoney()));
        if (!promptResult.accepted)
            return;
        const uint32_t newMoney = static_cast<uint32_t>(promptResult.value);
        if (newMoney == trainer.money)
            return;
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
    static std::string leafName(const std::string &path)
    {
        const size_t slash = path.find_last_of('/');
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    void TrainerViewScreen::performSave(const std::string &destDir, bool injectToTitle)
    {
        // The game's "current box" is kept in sync live while a box view is open (see update()), so
        // it already reflects wherever the user last was -- including a box change made in Storage
        // before backing out (Storage's own X sorts; this game save happens later). Nothing to do here.
        const bool passed = Save::saveTrainerInfo(trainer, destDir.c_str(), titleId, userUid, injectToTitle);
        saveConfirmActive = false;

        // The single most important trace line: what was written, where, and whether it worked.
        const std::string saveInfo =
            std::string("dest=") + (injectToTitle ? "GAME" : "BACKUP") +
            " folder=\"" + leafName(destDir) + "\"" +
            " inject=" + (injectToTitle ? "1" : "0") +
            " illegaldata=" + (illegalDataWritten ? "1" : "0") +
            " result=" + (passed ? "OK" : "FAILED");
        Utils::logTest("SAVE     " + saveInfo);
        Utils::logEventToFile("SAVE " + saveInfo + " party=" + std::to_string(trainer.getPartySize()));

        if (passed)
        {
            hasUnsavedChanges = false;
            statEdit.dialogActive = false;
            details.active = false;
            details.editing = false;
            creator.editing = false;
            creator.keepConfirmActive = false; // committed by the save
            // Name the destination back to the user: with three of them, "saved" alone is ambiguous
            // and the whole point of the picker is knowing WHERE it went.
            postStatus(injectToTitle ? "Written to the game save."
                                     : ("Written to backup \"" + leafName(destDir) + "\"."),
                       200);
            // A new backup becomes the session's working copy, so a second save goes to the same
            // place instead of silently forking another folder off the original.
            backupDir = destDir;
        }
        else
        {
            // Without this the failure is INDISTINGUISHABLE from a successful save.
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
    std::string TrainerViewScreen::createNamedBackupDir(const std::string &name)
    {
        // Never let a typed name reach the filesystem unfiltered: a '/' or ".." would escape the
        // PKSE directory entirely. Keep it to characters that are safe on FAT32 as well.
        std::string leaf;
        for (const char character : name)
        {
            if (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' || character == '-' ||
                character == '_')
                leaf += character;
        }
        // FAT32 dislikes trailing spaces
        while (!leaf.empty() && leaf.back() == ' ') leaf.pop_back();
        while (!leaf.empty() && leaf.front() == ' ')
            leaf.erase(leaf.begin());
        if (leaf.empty())
            return "";

        const std::string gameDir = BASE_SAVE_DIRECTORY + "/" + titleName;
        auto exists = [](const std::string &p)
        { struct stat fileStatus{}; return stat(p.c_str(), &fileStatus) == 0; };

        // Suffix rather than overwrite -- silently replacing a backup the user made earlier would be
        // the worst possible reading of "create a new save".
        std::string unique = leaf;
        for (int nIndex = 2; nIndex < 1000 && exists(gameDir + "/" + unique); ++nIndex)
            unique = leaf + "-" + std::to_string(nIndex);

        const std::string destDir = gameDir + "/" + unique;
        if (mkdir(destDir.c_str(), 0777) != 0 && errno != EEXIST)
        {
            Utils::logErrorToFile("Failed to create named backup directory", destDir.c_str());
            return "";
        }
        if (!Utils::copyDirectory(backupDir.c_str(), destDir.c_str()))
        {
            Utils::logErrorToFile("Failed to seed named backup from", backupDir.c_str());
            return "";
        }
        return destDir;
    }

    //
    // The search request. The list the user moves through is the FILTERED one; the list every
    // apply site writes from is the unfiltered one. Keeping those two straight is the whole job
    // here, and the three helpers below are the only places allowed to convert between them.

    //
    // Matched against what the box grid can actually show a name for: the species and, when it has
    // one, the nickname. Searching a field the grid never draws would leave the user staring at a
    // ringed slot with no idea why it matched.

    bool TrainerViewScreen::storageSlotMatchesSearch(int pane, int box, int slot)
    {
        if (!storageSearch.isFiltering())
            return true;
        const Pokemon::Pokemon *pokemon = storageSlot(pane, box, slot).get();
        // an empty slot matches nothing, however the query reads
        if (pokemon == nullptr || pokemon->speciesID() == 0) return false;
        if (storageSearch.matches(Trainer::getSpeciesName(pokemon->speciesID())))
            return true;
        const std::string nickname = Utils::utf16ToUtf8(pokemon->nickname());
        return !nickname.empty() && storageSearch.matches(nickname.c_str());
    }

    bool TrainerViewScreen::jumpToFirstStorageMatch()
    {
        if (!storageSearch.isFiltering())
            return false;
        // The focused pane first, from the box the user is already looking at, then wrapping, then
        // the other pane -- so a match near the cursor wins over one at the start of the bank.
        for (int paneStep = 0; paneStep < 2; ++paneStep)
        {
            const int pane = (storageFocusPane + paneStep) % 2;
            const int boxCount = paneBoxes(pane);
            const int slotCount = paneSlots(pane);
            const int startBox = (paneStep == 0) ? (pane == 0 ? selectedBoxIndex : stBankBox) : 0;
            for (int boxStep = 0; boxStep < boxCount; ++boxStep)
            {
                const int box = (startBox + boxStep) % boxCount;
                for (int slot = 0; slot < slotCount; ++slot)
                {
                    if (!storageSlotMatchesSearch(pane, box, slot))
                        continue;
                    storageFocusPane = pane;
                    if (pane == 0)
                    {
                        selectedBoxIndex = box;
                        stSaveSlot = slot;
                    }
                    else
                    {
                        stBankBox = box;
                        stBankSlot = slot;
                    }
                    return true;
                }
            }
        }
        return false;
    }

    bool TrainerViewScreen::armTap(int buttonId, u64 button)
    {
        pendingTapButtonId = buttonId;
        pendingTapButton = button;
        pendingTapFrames = TAP_PRESS_FRAMES;
        return true;
    }

    void TrainerViewScreen::clearPickerSearch()
    {
        pickerSearch.clear();
        pickerFilteredRows.clear();
    }

    bool TrainerViewScreen::pickerUsesOrder() const
    {
        using Dialogs::PickerKind;
        // Exactly the kinds drawPickerDialog() treats as reordered. A kind that fills pickerOrder
        // and is missing here would be labelled by row while being written by value -- naming one
        // thing on screen and applying another.
        switch (pickerKind)
        {
        case PickerKind::Ability:
        case PickerKind::Species:
        case PickerKind::Move:
        case PickerKind::PouchItem:
        case PickerKind::PouchItemG3:
        case PickerKind::PouchItemG1:
        case PickerKind::MetLocation:
        case PickerKind::Ball:
        case PickerKind::Form:
        case PickerKind::Gender:
            return !pickerOrder.empty();
        default:
            return false;
        }
    }

    int TrainerViewScreen::pickerVisibleCount() const
    {
        if (pickerSearch.isFiltering())
            return static_cast<int>(pickerFilteredRows.size());
        return pickerCount;
    }

    int TrainerViewScreen::pickerRowForVisibleIndex(int visibleIndex) const
    {
        if (!pickerSearch.isFiltering())
            return visibleIndex;
        if (visibleIndex < 0 || visibleIndex >= static_cast<int>(pickerFilteredRows.size()))
            return -1;
        return pickerFilteredRows[visibleIndex];
    }

    int TrainerViewScreen::pickerSelectedValue() const
    {
        const int row = pickerRowForVisibleIndex(pickerSel);
        if (row < 0 || row >= pickerCount)
            return -1;
        if (pickerUsesOrder() && row < static_cast<int>(pickerOrder.size()))
            return pickerOrder[row];
        return row;
    }

    void TrainerViewScreen::rebuildPickerFilter(int previouslySelectedValue)
    {
        pickerFilteredRows.clear();
        if (pickerSearch.isFiltering())
        {
            pickerFilteredRows.reserve(static_cast<size_t>(pickerCount));
            for (int row = 0; row < pickerCount; ++row)
            {
                const int value = (pickerUsesOrder() && row < static_cast<int>(pickerOrder.size()))
                                      ? pickerOrder[row]
                                      : row;
                // Matched against the label the row DRAWS -- see pickerRowLabel().
                if (pickerSearch.matches(Dialogs::pickerRowLabel(*this, pickerKind, value).c_str()))
                    pickerFilteredRows.push_back(row);
            }
        }

        // Keep the cursor on the same VALUE where the filter still contains it, so typing a query
        // that does not exclude the current pick leaves the selection where the user left it.
        const int visibleCount = pickerVisibleCount();
        if (visibleCount <= 0)
        {
            pickerSel = 0;
            return;
        }
        if (previouslySelectedValue >= 0)
        {
            for (int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex)
            {
                const int row = pickerRowForVisibleIndex(visibleIndex);
                const int value = (pickerUsesOrder() && row >= 0 && row < static_cast<int>(pickerOrder.size()))
                                      ? pickerOrder[row]
                                      : row;
                if (value == previouslySelectedValue)
                {
                    pickerSel = visibleIndex;
                    return;
                }
            }
        }
        if (pickerSel >= visibleCount)
            pickerSel = visibleCount - 1;
        if (pickerSel < 0)
            pickerSel = 0;
    }

    std::vector<int> TrainerViewScreen::visibleItemIndices() const
    {
        std::vector<int> visibleIndices;
        if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
        {
            const auto &pouch = trainer.items[selectedCategory];
            visibleIndices.reserve(pouch.size());
            for (int pouchIndex = 0; pouchIndex < static_cast<int>(pouch.size()); ++pouchIndex)
            {
                // "owned but empty" slots stay in the data and out of the list
                if (pouch[pouchIndex].count <= 0) continue;
                // The search is applied HERE because every consumer of the item list --
                // the panel, the cursor, the edit dialog, the remove flow -- goes through this one
                // function. Filtering at the source means they cannot disagree about which row is
                // which, which is the failure a second index space would invite.
                if (itemSearch.isFiltering())
                {
                    const char *itemName =
                        Names::getItemNameFor(trainer.getGameGroup(), pouch[pouchIndex].itemId);
                    if (!itemSearch.matches(itemName))
                        continue;
                }
                visibleIndices.push_back(pouchIndex);
            }
        }
        return visibleIndices;
    }

    // How many item slots the current pouch can hold, for the add-item flow.
    //
    // Two storage models. The id-indexed games (BDSP / S-V / Z-A) build their pouch vector by
    // walking every legal id, so an entry already exists for anything addable and nothing is ever
    // appended -- they report a bound large enough to never block. The slot-based games store only
    // what the bag holds, so a new item really does append and must respect the pouch's capacity.
    // Legends: Arceus reports 0 (appending unsupported): its PouchInfo8LA carries no capacity and
    // its item block is resized on write, so appending without a documented bound could overflow it.
    int TrainerViewScreen::currentPouchCapacity() const
    {
        using namespace Trainer;
        const int categoryIndex = selectedCategory;
        if (categoryIndex < 0)
            return 0;
        switch (trainer.getGameGroup())
        {
        case Enums::GameVersion::FRLG:
            return (categoryIndex < static_cast<int>(POUCH_COUNT3_FRLG))
                       ? getPouchInfo3FRLG(static_cast<PouchType3FRLG>(categoryIndex)).maxSlots
                       : 0;
        case Enums::GameVersion::RSE:
            // The one place the Hoenn layout has to be asked for: Emerald's Items and Key Items
            // pouches hold 30 where Ruby/Sapphire's hold 20, so a shared number would either
            // truncate an Emerald bag or offer ten slots a Ruby bag does not have.
            return (categoryIndex < static_cast<int>(POUCH_COUNT3_RSE))
                       ? getPouchInfo3RSE(static_cast<PouchType3RSE>(categoryIndex), hoennLayout(trainer)).maxSlots
                       : 0;
        case Enums::GameVersion::GG:
            return (categoryIndex < static_cast<int>(PouchType7LGPE::Count))
                       ? getPouchInfo7LGPE(static_cast<PouchType7LGPE>(categoryIndex)).maxSlots
                       : 0;
        case Enums::GameVersion::SWSH:
            return (categoryIndex < static_cast<int>(PouchType8SWSH::Count))
                       ? getPouchInfo8SWSH(static_cast<PouchType8SWSH>(categoryIndex)).maxCount
                       : 0;
        case Enums::GameVersion::PLA:
            // Fixed-capacity packed pouches; the general bag grows with Satchel Upgrades.
            return static_cast<int>(trainer.getItemPouchCapacity(categoryIndex));
        default:
            return 1 << 20; // id-indexed: the entry is already there, so this never binds
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
    int TrainerViewScreen::currentItemMaxCount() const
    {
        using Version = Enums::GameVersion;
        const int categoryIndex = selectedCategory;
        switch (trainer.getGameGroup())
        {
        case Version::FRLG:
            return (categoryIndex == static_cast<int>(Trainer::PouchType3FRLG::KeyItems)) ? 1 : 999;
        // Let's Go caps TMs at 1. Its "KeyItems" pouch is NOT key-items-only -- it is PKHeX's
        // Items pouch with regular and key items MIXED (max 999), so it must not be capped at 1.
        case Version::GG:
            return (categoryIndex == static_cast<int>(Trainer::PouchType7LGPE::TMs)) ? 1 : 999;
        case Version::SWSH:
            return (categoryIndex == static_cast<int>(Trainer::PouchType8SWSH::KeyItems)) ? 1 : 999;
        // PLA caps Key Items AND Recipes at 1 (possession flags), like PKHeX PlayerBag8a.
        case Version::PLA:
            return (categoryIndex == static_cast<int>(Trainer::PouchType8LA::KeyItems) ||
                    categoryIndex == static_cast<int>(Trainer::PouchType8LA::Recipes))
                       ? 1
                       : 999;
        case Version::BDSP:
            return (categoryIndex == static_cast<int>(Trainer::PouchType8BDSP::KeyItems)) ? 1 : 999;
        case Version::SV:
            return (categoryIndex == static_cast<int>(Trainer::PouchType9SV::KeyItems)) ? 1 : 999;
        case Version::ZA:
            return (categoryIndex == static_cast<int>(Trainer::PouchType9LZA::KeyItems) ||
                    categoryIndex == static_cast<int>(Trainer::PouchType9LZA::TMs) ||
                    categoryIndex == static_cast<int>(Trainer::PouchType9LZA::MegaStones))
                       ? 1
                       : 999;
        default:
            return 999;
        }
    }

    // Open the details modal on a storage slot (reuses the Box path for the save pane).
    void TrainerViewScreen::openStorageEditor(int pane, int box, int slot)
    {
        if (pane == 1)
        {
            details.source = EditSource::Bank;
            details.bankBox = box;
            details.bankSlot = slot;
        }
        else
        {
            details.source = EditSource::Box;
            selectedBoxIndex = box;
            selectedItemIndex = slot;
        }
        details.active = true;
        details.leftScroll = 0; // start the info column at the top
        details.editing = false;
        details.category = 0;
        details.selectedStat = 0;
        details.selectedField = 0;
        details.hexMode = 0;
        creator.editing = false; // default; the creator flow sets it true right after this call
        snapshotEditTarget();    // dirty-check baseline (unused for the creator: it has Keep/Discard)
    }

    // Column count of a pane's grid. The bank is always 30 slots; a save pane follows its own box
    // size -- 25 in Let's Go, 20 in an international Gen 1 save, 30 elsewhere.
    int TrainerViewScreen::paneCols(int pane) const
    {
        if (pane != 0)
            return boxGridColumns(static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX));
        return boxGridColumns(static_cast<int>(trainer.getSlotsPerBox()));
    }

    int TrainerViewScreen::paneSlots(int pane) const
    {
        return pane == 0 ? static_cast<int>(trainer.getSlotsPerBox())
                         : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
    }

    int TrainerViewScreen::paneBoxes(int pane) const
    {
        return pane == 0 ? static_cast<int>(trainer.getBoxCount())
                         : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
    }

    void TrainerViewScreen::cancelSelection()
    {
        currentlySelecting = false;
        if (!carrying())
            selectDimensions = {0, 0};
    }

    // Move mode: lift the slot under the cursor as a 1x1 block. Single and multi carry share one
    // representation, so every later step (drawing, bounds, put-down, B-to-return) is written once.
    void TrainerViewScreen::pickupSingle()
    {
        if (!bank)
            return;
        const int pane = storageFocusPane;
        const int box = (pane == 0) ? selectedBoxIndex : stBankBox;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= paneSlots(pane))
            return;
        // LGPE party member: its slot is pinned
        if (storageSlotLocked(pane, box, slot)) return;
        auto &source = storageSlot(pane, box, slot);
        // species-0 = empty (S/V ghost)
        if (!source || source->speciesID() == 0) return;

        moveMon.clear();
        moveMon.push_back(std::move(source));
        selectDimensions = {1, 1};
        heldPane = pane;
        heldFromBox = box;
        heldFromSlot = slot;
    }

    // Multi mode: the first A anchors the rectangle at the cursor, the second grabs it.
    void TrainerViewScreen::pickupMulti()
    {
        const int pane = storageFocusPane;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= paneSlots(pane))
            return;
        if (currentlySelecting)
        {
            grabSelection(true);
            return;
        }
        const int cols = paneCols(pane);
        selectDimensions = {slot % cols, slot / cols}; // anchor cell, NOT dimensions yet
        selectPane = pane;
        selectBox = (pane == 0) ? selectedBoxIndex : stBankBox;
        currentlySelecting = true;
    }

    // Lift (remove = true) or copy (remove = false) every slot inside the anchored rectangle into
    // moveMon, then snap the cursor to the rectangle's top-left so the block sits under the pointer.
    void TrainerViewScreen::grabSelection(bool remove)
    {
        if (!currentlySelecting || !bank)
            return;
        const int pane = selectPane;
        const int box = selectBox;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0 || slot >= slots)
        {
            cancelSelection();
            return;
        }

        const int curX = slot % cols, curY = slot / cols;
        const int anchorX = selectDimensions.first, anchorY = selectDimensions.second;
        const int baseX = std::min(anchorX, curX), baseY = std::min(anchorY, curY);
        const int selectionColumns = std::abs(anchorX - curX) + 1, selectionRows = std::abs(anchorY - curY) + 1;

        moveMon.clear();
        moveMon.reserve(static_cast<size_t>(selectionColumns) * static_cast<size_t>(selectionRows));
        int lockedLeft = 0;
        for (int yIndex = 0; yIndex < selectionRows; ++yIndex)
        {
            for (int columnOffset = 0; columnOffset < selectionColumns; ++columnOffset)
            {
                const int sourceSlot = (baseY + yIndex) * cols + (baseX + columnOffset);
                if (sourceSlot >= slots)
                {
                    moveMon.push_back(nullptr);
                    continue;
                }
                // A party-linked slot stays put: Let's Go's party points at box slots by INDEX, so
                // carrying one away would leave that pointer aimed at whatever slid in behind it.
                // It becomes a hole in the block rather than blocking the whole grab.
                if (storageSlotLocked(pane, box, sourceSlot))
                {
                    moveMon.push_back(nullptr);
                    ++lockedLeft;
                    continue;
                }
                auto &source = storageSlot(pane, box, sourceSlot);
                if (!source || source->speciesID() == 0)
                {
                    moveMon.push_back(nullptr);
                    continue;
                }
                if (remove)
                {
                    moveMon.push_back(std::move(source));
                }
                else
                {
                    moveMon.push_back(source->clone()); // duplicate: leave the original in place
                }
            }
        }

        selectDimensions = {selectionColumns, selectionRows};
        heldPane = pane;
        heldFromBox = box;
        heldFromSlot = baseY * cols + baseX;
        currentlySelecting = false;

        if (remove && carriedCount() > 0)
            hasUnsavedChanges = true;
        if (lockedLeft > 0)
            postStatus(std::to_string(lockedLeft) + " party-linked slot(s) stayed behind.", 180);

        // Trim first, THEN put the cursor on the block's top-left cell -- scrunching moves that
        // corner, and a cursor left on the untrimmed corner would draw the block offset from the
        // Pokemon that were just lifted. A rectangle that caught nothing leaves the cursor alone.
        postPickup();
        if (carrying())
        {
            storageFocusPane = pane;
            if (pane == 0)
            {
                selectedBoxIndex = box;
                stSaveSlot = heldFromSlot;
            }
            else
            {
                stBankBox = box;
                stBankSlot = heldFromSlot;
            }
        }
    }

    // Drop a block whose every cell is null -- otherwise the hands stay "full" of nothing and the
    // next A would put empties down over real Pokemon.
    void TrainerViewScreen::postPickup()
    {
        if (currentlySelecting)
            return;
        if (carriedCount() == 0)
        {
            moveMon.clear();
            selectDimensions = {0, 0};
            return;
        }
        scrunchSelection();
    }

    // Trim fully-empty rows and columns off the block's edges. Selecting a loose
    // rectangle around three Pokemon should hand you a block sized to those three, not to the box
    // area you swept -- otherwise the bounds check refuses drops that would obviously have fitted.
    void TrainerViewScreen::scrunchSelection()
    {
        int selectionColumns = selectDimensions.first, selectionRows = selectDimensions.second;
        if (selectionColumns <= 1 && selectionRows <= 1)
            return;
        if (selectionColumns <= 0 || selectionRows <= 0 ||
            static_cast<int>(moveMon.size()) != selectionColumns * selectionRows)
            return;

        auto colEmpty = [&](int columnIndex)
        {
            for (int rIndex = 0; rIndex < selectionRows; ++rIndex)
                if (moveMon[rIndex * selectionColumns + columnIndex])
                    return false;
            return true;
        };
        auto rowEmpty = [&](int rowIndex)
        {
            for (int cIndex = 0; cIndex < selectionColumns; ++cIndex)
                if (moveMon[rowIndex * selectionColumns + cIndex])
                    return false;
            return true;
        };
        int first = 0, last = selectionColumns - 1;
        while (first <= last && colEmpty(first))
            ++first;
        while (last > first && colEmpty(last))
            --last;
        int top = 0, bottom = selectionRows - 1;
        while (top <= bottom && rowEmpty(top))
            ++top;
        while (bottom > top && rowEmpty(bottom))
            --bottom;
        // all empty; postPickup already handled it
        if (first > last || top > bottom) return;
        if (first == 0 && last == selectionColumns - 1 && top == 0 && bottom == selectionRows - 1)
            return;

        std::vector<std::unique_ptr<Pokemon::Pokemon>> packed;
        packed.reserve(static_cast<size_t>(last - first + 1) * static_cast<size_t>(bottom - top + 1));
        for (int rIndex = top; rIndex <= bottom; ++rIndex)
            for (int columnIndex = first; columnIndex <= last; ++columnIndex)
                packed.push_back(std::move(moveMon[rIndex * selectionColumns + columnIndex]));
        moveMon = std::move(packed);
        selectDimensions = {last - first + 1, bottom - top + 1};
        // The origin moves with the trim, so B still returns each cell to the slot it came from.
        const int cols = paneCols(heldPane);
        heldFromSlot += top * cols + first;
    }

    // Does the block fit in the focused pane starting at the cursor cell? A block may not hang off
    // the right edge or past the bottom row -- that is what makes "lands in the exact slot" a rule
    // rather than a hope.
    bool TrainerViewScreen::checkPutDownBounds() const
    {
        if (!carrying())
            return false;
        const int pane = storageFocusPane;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        if (slot < 0)
            return false;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        const int rows = (slots + cols - 1) / cols;
        const int columnIndex = slot % cols, r = slot / cols;
        return columnIndex + selectDimensions.first <= cols && r + selectDimensions.second <= rows &&
               slot + (selectDimensions.first - 1) + (selectDimensions.second - 1) * cols < slots;
    }

    // Put the block down at the cursor: cell (x, y) -> slot + x + y*cols, exactly. Each cell SWAPS
    // with whatever is in its destination, so the displaced Pokemon come back up into your hands as
    // a block of the same shape (HOME's behaviour, and what makes a positional move reversible).
    //
    // Cells that cannot land -- a locked (party-linked) destination, or a cross-game pokemon with no
    // conversion route into this save -- stay in the block and stay untouched, so one blocker never
    // strands the rest of the group.
    void TrainerViewScreen::putDownBlock()
    {
        if (!carrying() || !bank)
            return;
        const int pane = storageFocusPane;
        const int box = (pane == 0) ? selectedBoxIndex : stBankBox;
        const int slot = (pane == 0) ? stSaveSlot : stBankSlot;
        const int cols = paneCols(pane);
        const int slots = paneSlots(pane);
        if (!checkPutDownBounds())
        {
            postStatus("That group doesn't fit here - move to a slot with room for it.", 180);
            return;
        }

        const int selectionColumns = selectDimensions.first, selectionRows = selectDimensions.second;
        int placed = 0, lockedHit = 0, blocked = 0;
        bool converted = false; // any cell crossed a game boundary -> noted on each MOVE event
        std::string firstName;
        const char *firstWhy = nullptr;

        for (int yIndex = 0; yIndex < selectionRows; ++yIndex)
        {
            for (int columnOffset = 0; columnOffset < selectionColumns; ++columnOffset)
            {
                const size_t index = static_cast<size_t>(yIndex) * static_cast<size_t>(selectionColumns) +
                                     static_cast<size_t>(columnOffset);
                const int dSlot = slot + columnOffset + yIndex * cols;
                if (dSlot < 0 || dSlot >= slots)
                    continue;
                // Never disturb a party-linked slot -- not even with an empty cell, which would
                // silently delete a party member out from under its pointer.
                if (storageSlotLocked(pane, box, dSlot))
                {
                    if (moveMon[index])
                        ++lockedHit;
                    continue;
                }
                if (moveMon[index])
                {
                    // Ask whether there is a route (for a species-named message), then honour what
                    // the conversion ACTUALLY returns. Writing an unconverted foreign Pokemon into a
                    // save is what produces a Bad Egg in game, so a failure here must not fall through.
                    const bool foreign = (pane == 0 && moveMon[index]->getGameGroup() != trainer.getGameGroup());
                    Conversion::Result conversionResult{};
                    const bool routed =
                        !foreign || Conversion::canConvert(*moveMon[index], trainer.getGameGroup(), conversionResult);
                    if (!routed || !convertForPane(moveMon[index], pane))
                    {
                        ++blocked;
                        if (!firstWhy)
                        {
                            firstName = Names::getDisplayName(moveMon[index]->speciesID(), moveMon[index]->form(),
                                                              Trainer::getSpeciesName(moveMon[index]->speciesID()));
                            firstWhy = routed ? "conversion failed" : Conversion::resultMessage(conversionResult);
                        }
                        if (g_debugLogging)
                        {
                            Utils::logEventToFile(
                                "MOVE result=BLOCKED " + Utils::logSlot("to", pane, box, dSlot) + " " +
                                Utils::logField("reason", routed ? "conversion failed"
                                                                 : Conversion::resultMessage(conversionResult)) +
                                " " + Utils::describePokemon(*moveMon[index], trainer));
                        }
                        continue; // leave it in hand, untouched
                    }
                    ++placed;
                    converted = converted || foreign;
                }
                auto &destination = storageSlot(pane, box, dSlot);
                std::swap(destination, moveMon[index]); // block swap: the occupant comes up
                if (g_debugLogging && destination)
                {
                    std::string line = "MOVE result=OK " + Utils::logSlot("from", heldPane, heldFromBox, heldFromSlot) +
                                       " " + Utils::logSlot("to", pane, box, dSlot) +
                                       (converted ? " converted=Y" : "") + " " +
                                       Utils::describePokemon(*destination, trainer);
                    if (moveMon[index])
                        line += " displaced=[" + Utils::briefPokemon(*moveMon[index]) + "]";
                    Utils::logEventToFile(line);
                }
                // ghost -> hole
                if (moveMon[index] && moveMon[index]->speciesID() == 0) moveMon[index] = nullptr;
            }
        }

        if (placed > 0)
            hasUnsavedChanges = true;
        // The block's origin is now this drop site, so B returns the swapped-out Pokemon here.
        heldPane = pane;
        heldFromBox = box;
        heldFromSlot = slot;
        postPickup(); // may trim the swapped-out leftovers, moving heldFromSlot
        if (carrying())
        { // keep the cursor on the leftovers' top-left corner
            if (pane == 0)
                stSaveSlot = heldFromSlot;
            else
                stBankSlot = heldFromSlot;
        }

        if (blocked > 0)
        {
            postStatus(firstName + ": " + (firstWhy ? firstWhy : "no transfer route") +
                           (blocked > 1 ? "  (+" + std::to_string(blocked - 1) + " more)" : "") + " - still in hand",
                       240);
        }
        else if (lockedHit > 0)
        {
            postStatus(std::to_string(lockedHit) + " couldn't land on a party-linked slot - still in hand", 240);
        }
    }

    // The A press in the storage grid: put the block down if hands are full,
    // otherwise pick up according to the active mode.
    void TrainerViewScreen::storagePickup()
    {
        if (carrying())
        {
            putDownBlock();
            return;
        }
        switch (cursorMode)
        {
        case CursorMode::Multi:
            pickupMulti();
            break;
        case CursorMode::Move:
            pickupSingle();
            postPickup();
            break;
        case CursorMode::Menu:
        default:
            break; // handled by the caller (opens the per-Pokemon menu)
        }
    }

    // Sort one box: order its Pokemon by National Dex no., then form, then level (highest first),
    // and pack them to the front. Empty slots fall to the end.
    //
    // LOCKED SLOTS ARE PINNED and never participate. A save-side slot is locked when a party member
    // points at it (getPartyPosition > 0) -- Let's Go stores the party as INDICES into box storage, so
    // relocating such a slot would silently repoint a party member at a different Pokemon. The
    // occupant stays exactly where it is and the sort flows around it.
    void TrainerViewScreen::sortStorageBox(int pane, int box)
    {
        const int slots = (pane == 0) ? static_cast<int>(trainer.getSlotsPerBox())
                                      : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
        std::vector<int> freeIdx; // slots the sort may write to
        std::vector<std::unique_ptr<Pokemon::Pokemon>> mons;
        for (int slotIndex = 0; slotIndex < slots; ++slotIndex)
        {
            // party-linked: leave untouched
            if (storageSlotLocked(pane, box, slotIndex)) continue;
            freeIdx.push_back(slotIndex);
            auto &slot = storageSlot(pane, box, slotIndex);
            if (slot && slot->speciesID() != 0) // species-0 = empty (S/V ghost)
                mons.push_back(std::move(slot));
            slot.reset();
        }
        if (mons.empty())
        {
            storageStatus = "Nothing to sort in this box";
            storageStatusFrames = 120;
            return;
        }
        std::sort(mons.begin(), mons.end(),
                  [](const std::unique_ptr<Pokemon::Pokemon> &a, const std::unique_ptr<Pokemon::Pokemon> &b)
                  {
                      if (a->speciesID() != b->speciesID())
                          return a->speciesID() < b->speciesID();
                      if (a->form() != b->form())
                          return a->form() < b->form();
                      return a->level() > b->level();
                  });
        for (size_t monIndex = 0; monIndex < mons.size() && monIndex < freeIdx.size(); ++monIndex)
            storageSlot(pane, box, freeIdx[monIndex]) = std::move(mons[monIndex]);

        hasUnsavedChanges = true;
        storageStatus = "Box sorted by Dex No. (" + std::to_string(mons.size()) + ")";
        storageStatusFrames = 120;
    }

    void TrainerViewScreen::handleStorageInput(u64 buttonsDown)
    {
        if (!bank)
            return;

        // Accessors that always target the currently-focused pane.
        auto curBox = [&]() -> int &
        { return storageFocusPane == 0 ? selectedBoxIndex : stBankBox; };
        auto curSlot = [&]() -> int &
        { return storageFocusPane == 0 ? stSaveSlot : stBankSlot; };
        // The focused pane's box name is renamable when it's the bank (always) or a save that stores
        // box names (LGPE does not). Gates whether Up/Down can land on the box-name header.
        auto headerRenamable = [&]()
        { return storageFocusPane == 1 || trainer.supportsBoxNames(); };
        auto isLocked = [&](int pane, int box, int slot)
        { return storageSlotLocked(pane, box, slot); };
        auto occupied = [&](int pane, int box, int slot)
        {
            const auto &p = storageSlot(pane, box, slot);
            return p && p->speciesID() != 0;
        };

        const bool holding = carrying();

        // Y: cycle mode Menu -> Move -> Multi. Only with empty hands and no rectangle in progress,
        // so a mode switch can never orphan a carried block or a half-drawn selection.
        if ((buttonsDown & HidNpadButton_Y) && !holding && !currentlySelecting)
        {
            cursorMode = cursorMode == CursorMode::Menu   ? CursorMode::Move
                         : cursorMode == CursorMode::Move ? CursorMode::Multi
                                                          : CursorMode::Menu;
        }

        // L/R: change the focused pane's box (wrap); keep the cursor slot in range. Changing box
        // abandons an in-progress rectangle -- it was anchored in the box you just left.
        if (buttonsDown & (HidNpadButton_L | HidNpadButton_R))
        {
            const int paneBoxCount = paneBoxes(storageFocusPane);
            if (buttonsDown & HidNpadButton_L)
                curBox() = (curBox() - 1 + paneBoxCount) % paneBoxCount;
            if (buttonsDown & HidNpadButton_R)
                curBox() = (curBox() + 1) % paneBoxCount;
            if (currentlySelecting)
                cancelSelection();
        }
        if (curSlot() >= paneSlots(storageFocusPane))
            curSlot() = paneSlots(storageFocusPane) - 1;

        // D-pad: move within the focused pane; Left/Right cross panes at the horizontal edges.
        const int cols = paneCols(storageFocusPane);
        const int slots = paneSlots(storageFocusPane);
        const int rows = (slots + cols - 1) / cols;
        // curSlot() == -1 is the "box-name header focused" state (rename via A or a tap). Up from the
        // top row lands on it and Down leaves it -- but only where the header is renamable and nothing
        // is in hand or being selected, so LGPE (no box names) and mid-carry keep the plain wrap.
        const bool headerReachable = headerRenamable() && !holding && !currentlySelecting;
        if (buttonsDown & HidNpadButton_Up)
        {
            if (curSlot() == -1)
            {
                curSlot() = (rows - 1) * cols; // header -> bottom row (wrap)
                if (curSlot() >= slots)
                    curSlot() = slots - 1;
            }
            else
            {
                int rowIndex = curSlot() / cols, columnIndex = curSlot() % cols;
                if (rowIndex == 0 && headerReachable)
                {
                    curSlot() = -1; // top row -> header
                }
                else
                {
                    rowIndex = (rowIndex - 1 + rows) % rows;
                    curSlot() = rowIndex * cols + columnIndex;
                    if (curSlot() >= slots)
                        curSlot() = slots - 1;
                }
            }
        }
        if (buttonsDown & HidNpadButton_Down)
        {
            if (curSlot() == -1)
            {
                curSlot() = 0; // header -> top-left
            }
            else
            {
                int rowIndex = curSlot() / cols, columnIndex = curSlot() % cols;
                if (rowIndex == rows - 1 && headerReachable)
                {
                    curSlot() = -1; // bottom row -> header (wrap)
                }
                else
                {
                    rowIndex = (rowIndex + 1) % rows;
                    curSlot() = rowIndex * cols + columnIndex;
                    if (curSlot() >= slots)
                        curSlot() = slots - 1;
                }
            }
        }
        if (buttonsDown & HidNpadButton_Left)
        {
            if (curSlot() == -1)
            { // on the header: the box arrow cycles the box
                curBox() = (curBox() - 1 + paneBoxes(storageFocusPane)) % paneBoxes(storageFocusPane);
            }
            else
            {
                int columnIndex = curSlot() % cols;
                if (columnIndex > 0)
                {
                    curSlot() -= 1;
                }
                else if (storageFocusPane == 1 && !currentlySelecting)
                {
                    // A rectangle lives in one box of one pane, so crossing panes is only allowed
                    // when nothing is being selected. A carried block may cross freely.
                    const int rowIndex = curSlot() / cols;
                    storageFocusPane = 0;
                    const int scols = paneCols(0);
                    stSaveSlot = rowIndex * scols + (scols - 1);
                    if (stSaveSlot >= paneSlots(0))
                        stSaveSlot = paneSlots(0) - 1;
                }
            }
        }
        if (buttonsDown & HidNpadButton_Right)
        {
            if (curSlot() == -1)
            { // on the header: the box arrow cycles the box
                curBox() = (curBox() + 1) % paneBoxes(storageFocusPane);
            }
            else
            {
                int columnIndex = curSlot() % cols;
                if (columnIndex < cols - 1 && curSlot() + 1 < slots)
                {
                    curSlot() += 1;
                }
                else if (storageFocusPane == 0 && !currentlySelecting)
                {
                    const int rowIndex = curSlot() / cols;
                    storageFocusPane = 1;
                    stBankSlot = rowIndex * paneCols(1); // col 0 of the bank pane
                }
            }
        }

        const int pane = storageFocusPane, box = curBox(), slot = curSlot();

        // Minus: options for the block in hand (Release all / Return to origin / Cancel).
        if ((buttonsDown & HidNpadButton_Minus) && holding)
        {
            groupMenuActive = true;
            groupMenuIndex = 0;
            return;
        }

        // Minus with empty hands: import a PKSM bank. Lives here, in the storage view, rather than
        // on the HOME menu, because that is what puts it inside the bank's existing Save/Discard
        // contract -- an import is unsaved bank state exactly like a deposit, and leaving the view
        // asks about it. Guarded on empty hands so an import can never run with Pokemon held out
        // of the grid.
        if ((buttonsDown & HidNpadButton_Minus) && !currentlySelecting)
        {
            openPKSMImportBrowser();
            return;
        }

        // ZL/ZR: jump ten boxes. The bank is 200 boxes deep, and L/R one at a time is 200 presses
        // to reach the end of it.
        if (buttonsDown & (HidNpadButton_ZL | HidNpadButton_ZR))
        {
            const int paneBoxCount = paneBoxes(storageFocusPane);
            const int step = 10;
            if (buttonsDown & HidNpadButton_ZL)
                curBox() = ((curBox() - step) % paneBoxCount + paneBoxCount) % paneBoxCount;
            if (buttonsDown & HidNpadButton_ZR)
                curBox() = (curBox() + step) % paneBoxCount;
            if (currentlySelecting)
                cancelSelection();
            if (curSlot() >= paneSlots(storageFocusPane))
                curSlot() = paneSlots(storageFocusPane) - 1;
            return;
        }

        // X: while drawing a rectangle it DUPLICATES the selection (grab a copy
        // and leave the originals in place). Otherwise it sorts the focused box. Suppressed while
        // carrying, so a sort can never run with Pokemon held out of the grid and lose them.
        if (buttonsDown & HidNpadButton_X)
        {
            if (currentlySelecting)
            {
                grabSelection(false);
                if (carrying())
                    postStatus("Copied " + std::to_string(carriedCount()) + " - originals left in place.", 180);
                return;
            }
            if (!holding)
            {
                sortStorageBox(storageFocusPane, storageFocusPane == 0 ? selectedBoxIndex : stBankBox);
                return;
            }
        }

        // A: act. A full hand overrides the mode -- it always means "put the block down here".
        if (buttonsDown & HidNpadButton_A)
        {
            // Box-name header focused: A renames this pane's box (save box, or bank box). Guard first
            // so the placement/menu code below never indexes storage with slot == -1.
            if (slot == -1)
            {
                if (!holding)
                {
                    if (pane == 0)
                        renameBox(box);
                    else
                        renameBankBox(box);
                }
                return;
            }
            if (holding)
            {
                // Confirm first for a lossy conversion. Three separate warnings (see the header),
                // each with its own wording, and ALL of them gated by the one Move warning setting
                // -- it covers every bank transfer warning in every generation, not the Let's Go
                // one alone. They are mutually exclusive here, most destructive first: the Gen 3
                // down-convert rebuilds the PID, the Transporter run rewrites most of the record,
                // and the Let's Go transfer only resets training.
                const bool gen3DowngradeWarning = g_moveWarn && blockInvolvesGen3Downgrade(pane);
                const bool virtualConsoleWarning = g_moveWarn && blockInvolvesVirtualConsoleTransfer(pane);
                const bool lgpeTransferWarning = g_moveWarn && blockInvolvesLgpe(pane);
                if (gen3DowngradeWarning || virtualConsoleWarning || lgpeTransferWarning)
                {
                    pendingMove = PendingMove::PlaceHeld;
                    pendingMovePane = pane;
                    pendingMoveBox = box;
                    pendingMoveSlot = slot;
                    if (gen3DowngradeWarning)
                        gen3ConvertConfirmActive = true;
                    else if (virtualConsoleWarning)
                        virtualConsoleTransferConfirmActive = true;
                    else
                        lgpeTransferConfirmActive = true;
                    moveConfirmIndex = 1;
                    return;
                }
                putDownBlock();
                return;
            }
            switch (cursorMode)
            {
            case CursorMode::Menu:
                if (occupied(pane, box, slot))
                {
                    // Party-linked (locked) slots still open the menu -- Edit and Clone are valid
                    // there; only Move and Release are disabled (see the action handler + the
                    // greyed rows). Start on Edit when Move is unavailable so the cursor doesn't
                    // land on a greyed row.
                    storageMenuActive = true;
                    storageMenuIndex = isLocked(pane, box, slot) ? 1 : 0;
                    menuPane = pane;
                    menuBox = box;
                    menuSlot = slot;
                }
                else if (pane == 0 && !isLocked(pane, box, slot))
                {
                    // Empty save-pane slot: create a new Pokemon here (pick species, then edit).
                    creator.active = true;
                    creator.pane = pane;
                    creator.box = box;
                    creator.slot = slot;
                    pickerKind = Dialogs::PickerKind::Species;
                    buildCreatorSpeciesOrder(); // only species obtainable in this game (unless illegal-values on)
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                break;
            case CursorMode::Move:
            case CursorMode::Multi:
                storagePickup();
                break;
            }
        }
    }

    void TrainerViewScreen::update(const PadState &pad, const TouchInput &touch)
    {
        const HidAnalogStickState stick = padGetStickPos(&pad, 0);

        // L/R repeat only while they are navigation. On the Pokemon details page L randomizes IVs
        // and R opens legality details, so those action bindings deliberately remain edge-triggered.
        const bool fileBrowserNavigation =
            fileBrowser.active && !pksmImport.previewActive && !pksmImport.resultActive;
        const bool viewShoulderNavigation =
            !pksmImportActive() && !details.active &&
            (selectedMode == ViewMode::Storage || selectedMode == ViewMode::Items ||
             selectedMode == ViewMode::Boxes);
        const u64 repeatableShoulders =
            (pickerActive || fileBrowserNavigation || statEdit.dialogActive ||
             itemEditDialogActive || viewShoulderNavigation)
                ? (HidNpadButton_L | HidNpadButton_R)
                : 0;

        u64 buttonsDown = controllerNavigation.apply(
            padGetButtonsDown(&pad), padGetButtons(&pad), stick.x, stick.y,
            HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right,
            repeatableShoulders)
            | navTouchButton(touch); // nav-bar badges are tappable

        // A dialog tap recorded earlier fires once its press has actually been on screen. See
        // pendingTapButton: acting in the tap's own frame is why a tapped option never once showed
        // as selected. This deliberately does NOT return -- the frames in between still run their
        // ordinary bookkeeping, and a physical button pressed meanwhile is still honoured.
        if (pendingTapButton != 0 && --pendingTapFrames <= 0)
        {
            buttonsDown |= pendingTapButton;
            pendingTapButton = 0;
            pendingTapButtonId = -1;
        }

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
        // bit: it drops a new pokemon on whatever slot the cursor is on, which in a gapless game is usually
        // a gap, then clears creator.active and opens the editor. Compaction fired on the very next
        // frame, slid the pokemon down to the packed position, and left the editor pointing at an empty
        // slot -- so it drew nothing (drawPokemonDetailsModal bails on a null target) while still
        // swallowing input, and the app looked frozen with only B and + alive.
        //
        // Deferring is the whole fix: the pokemon still compacts, just once the editor closes.
        if (!carrying() && !currentlySelecting && !swapActive && !creator.active && !details.active)
        {
            trainer.compactStorage();
        }

        // Keep the game's persisted "current box" in step with the box view that is open, so it
        // survives leaving Storage before the game save runs. In Storage X sorts, so the save happens
        // after backing out -- by which point the mode has changed; capturing the box here rather
        // than at save time is what makes it stick. The Boxes view and the Storage save pane read
        // the one selectedBoxIndex, so there is nothing to reconcile between them; gated on
        // detailViewActive so the HOME menu (which shows no box) can't overwrite it.
        if (detailViewActive && (selectedMode == ViewMode::Storage || selectedMode == ViewMode::Boxes) &&
            selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.getBoxCount()))
        {
            trainer.setCurrentBox(static_cast<uint8_t>(selectedBoxIndex));
        }


        // MIRROR THE BANK PANE'S BOX INTO THE BANK ITSELF, every frame, so that moving it registers
        // as an unsaved bank change and the Save/Discard prompt appears on the way out. Here rather
        // than at each of the half-dozen places that move that cursor (L/R, the search jump, a
        // pick-up, a menu action), so none of them can be the one that forgets. Costs nothing --
        // it is an assignment, and the comparison that reads it only runs when the view is left.
        if (bank && stBankBox >= 0 && stBankBox < static_cast<int>(Trainer::Bank::BANK_BOX_COUNT))
            bank->currentBox = static_cast<uint16_t>(stBankBox);

        // Expire the transient status line. This lives HERE, not in handleStorageInput, because that
        // function only runs in the storage view and returns early without a bank -- so a message
        // posted from anywhere else (a save result, a box rename) would have stayed on screen
        // forever. update() runs every frame in every view, which is what a timer needs.
        if (storageStatusFrames > 0)
            --storageStatusFrames;

        // A box-name pill was tapped last frame: the header highlight has now been drawn once, so it
        // is safe to open the (blocking) rename keyboard. Doing this inline with the tap suspended the
        // app before the highlight ever rendered, so the selection only lit up AFTER the dialog closed
        // -- the deferral is what makes the tap feel immediate. One frame's delay is imperceptible.
        if (pendingHeaderRename)
        {
            pendingHeaderRename = false;
            if (selectedMode == ViewMode::Storage)
            {
                if (storageFocusPane == 0)
                    renameBox(selectedBoxIndex);
                else
                    renameBankBox(stBankBox);
            }
            else
            {
                renameBox(selectedBoxIndex);
            }
            return;
        }

        // A trainer-info row (Name/Money) was activated last frame; open its blocking keyboard now that
        // the row highlight has had a frame to draw.
        if (pendingTrainerEdit >= 0)
        {
            const int row = pendingTrainerEdit;
            pendingTrainerEdit = -1;
            if (row == 0)
                editTrainerName();
            else if (row == 1)
                editTrainerMoney();
            return;
        }

        // PKSM bank import (browser / preview / result). Placed ABOVE the + handler on purpose: it
        // owns every button while it is up, so + cannot close the app out from under a half-answered
        // import question -- the same rule the bank Save/Discard prompt already follows.
        if (pksmImportActive())
        {
            handlePKSMImportInput(buttonsDown, touch);
            return;
        }

        if (buttonsDown & HidNpadButton_Plus)
        {
            // The bank question is already on screen -- answer that first rather than stacking a
            // second exit on top of it.
            if (storageExitConfirmActive)
                return;
            // A carried Pokemon goes home before anything is weighed up.
            returnHeldToOrigin();
            // The bank is its own save file and gets its own decision. Closing the app is NOT that
            // decision, so it must not write the bank on the way out: doing that committed every
            // transfer even when the user went on to DECLINE the game save, and the two files then
            // disagreed. A deposit became a permanent CLONE (the bank has it, the game save still
            // has it), a withdrawal a permanent LOSS (the bank no longer has it, the game save was
            // never written). Ask instead, and let Discard rewind both sides and write nothing.
            if (bank && bank->hasChanged())
            {
                storageExitConfirmActive = true;
                storageExitConfirmIndex = 0;
                exitAfterBankChoice = true; // resume this exit once they've answered
                return;
            }
            beginAppExit();
            return;
        }

        // Reusable value picker (nature / gender / move) — owns all input while open.
        if (pickerActive)
        {
            Pokemon::Pokemon *pkPick = detailsTargetPokemon();
            const int page = 12;

            // Y opens the search box. Accepting an empty query clears the
            // filter, which is why no second key is needed to get out of one. The rows re-filter
            // with every key, and each pass starts from the value that was selected before typing,
            // so a cancel -- which puts the old query back -- lands on it again.
            if (buttonsDown & HidNpadButton_Y)
            {
                const int selectedValueBeforeSearch = pickerSelectedValue();
                const auto refilterRows = [this, selectedValueBeforeSearch]
                { rebuildPickerFilter(selectedValueBeforeSearch); };
                pickerSearch.promptForQuery(Dialogs::pickerTitle(pickerKind), ListSearch::FilterTiming::WhileTyping,
                                            refilterRows);
                return;
            }

            // Everything below moves through the VISIBLE rows, which are the filtered ones when a
            // query is set. `count` is deliberately not pickerCount.
            const int count = (pickerVisibleCount() > 0) ? pickerVisibleCount() : 1;

            // Touch: tap an option row selects it immediately (button id = visible row index).
            int touchedButton = touchedButtonId(touch);
            if (touchedButton >= 0 && touchedButton < count)
            {
                pickerSel = touchedButton;          // draws highlighted this frame
                // ...and is applied on the next one
                if (armTap(touchedButton, HidNpadButton_A)) return;
            }

            if (buttonsDown & HidNpadButton_Up)
                pickerSel = (pickerSel - 1 + count) % count;
            if (buttonsDown & HidNpadButton_Down)
                pickerSel = (pickerSel + 1) % count;
            if (buttonsDown & (HidNpadButton_L | HidNpadButton_Left))
                pickerSel = std::max(0, pickerSel - page);
            if (buttonsDown & (HidNpadButton_R | HidNpadButton_Right))
                pickerSel = std::min(count - 1, pickerSel + page);
            if (pickerSel < 0)
                pickerSel = 0;
            if (pickerSel >= count)
                pickerSel = count - 1;

            // A query that matches nothing leaves no row to apply. Swallow A rather than writing
            // whatever pickerSelectedValue() falls back to.
            if (pickerVisibleCount() == 0)
                buttonsDown &= ~static_cast<u64>(HidNpadButton_A);

            if (buttonsDown & HidNpadButton_B)
            {
                pickerActive = false;
                creator.active = false;
                // A change-type picker was launched from the Edit Item dialog -> cancelling it
                // returns THERE, not to the item list (the dialog is still "open" underneath).
                if (itemPickerReplace)
                {
                    itemPickerReplace = false;
                    itemEditDialogActive = true;
                }
                return;
            }

            // Creator: a Species pick in create mode builds a new pokemon into the target empty slot,
            // then hands off to the details editor (there is no live target Pokemon yet, so this
            // intercepts before the normal pkPick apply-switch below).
            if ((buttonsDown & HidNpadButton_A) && creator.active && pickerKind == Dialogs::PickerKind::Species)
            {
                const int pickedSpecies = pickerSelectedValue();
                if (pickedSpecies > 0)
                {
                    storageSlot(creator.pane, creator.box, creator.slot) =
                        buildDefaultMon(trainer, static_cast<uint16_t>(pickedSpecies),
                                        saveOriginVersion(trainer, titleId));
                    hasUnsavedChanges = true;
                    pickerActive = false;
                    creator.active = false;
                    openStorageEditor(creator.pane, creator.box, creator.slot);
                    creator.editing = true; // modal now edits a fresh pokemon -> Keep/Discard prompt on exit
                }
                else
                {
                    pickerActive = false;
                    creator.active = false; // "None" cancels
                }
                return;
            }

            // Change item type: a pouch pick opened from the Edit Item dialog REPLACES the selected
            // item's type rather than adding a new one. Must come before the add handler (same kind).
            if ((buttonsDown & HidNpadButton_A) && itemPickerReplace &&
                (pickerKind == Dialogs::PickerKind::PouchItem || pickerKind == Dialogs::PickerKind::PouchItemG1 ||
                 pickerKind == Dialogs::PickerKind::PouchItemG3))
            {
                itemPickerReplace = false;
                if (pickerSelectedValue() >= 0 && selectedCategory >= 0 &&
                    selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    const uint16_t newId = static_cast<uint16_t>(pickerSelectedValue());
                    auto &pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size()))
                    {
                        const int rawIdx = visible[selectedItemIndex];
                        const uint16_t keepCount = pouch[rawIdx].count;
                        // Before the swap -- the old id is about to be overwritten or zeroed. Both
                        // ids are logged because a retype is one of the two ways a pouch entry can
                        // change identity, and "my Master Ball became a Potion" reads very
                        // differently from an add followed by a remove.
                        if (g_debugLogging)
                        {
                            Utils::logEventToFile(
                                std::string("ITEM action=RETYPE ") +
                                Utils::logField("pouch",
                                                Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
                                " " +
                                Utils::logField("from", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup())) +
                                " fromid=" + std::to_string(pouch[rawIdx].itemId) + " " +
                                Utils::logField("to", Utils::itemName(newId, trainer.getGameGroup())) +
                                " toid=" + std::to_string(newId) + " count=" + std::to_string(keepCount) +
                                " mode=" + (trainer.itemsAreIdIndexed() ? "idIndexed" : "slotBased"));
                        }
                        // The dialog we return to must show the NEW item's amount (its count carried
                        // over unchanged), so re-sync both the live value and the change-baseline.
                        itemEditDialogValue = keepCount;
                        itemEditDialogOriginalValue = keepCount;
                        if (trainer.itemsAreIdIndexed())
                        {
                            // Zero the old id (KEEP the entry so its slot is written to 0), then set the
                            // new id -- reassigning itemId in place would ghost the old id's count.
                            pouch[rawIdx].count = 0;
                            const auto existingEntry = std::find_if(pouch.begin(), pouch.end(),
                                                        [newId](const Trainer::InventoryItem &newValue)
                                                        { return newValue.itemId == newId; });
                            if (existingEntry != pouch.end())
                            {
                                existingEntry->count = keepCount;
                                existingEntry->isNew = true;
                            }
                            else
                                pouch.push_back(Trainer::InventoryItem{newId, keepCount, true, false});
                        }
                        else
                        {
                            pouch[rawIdx].itemId = newId; // slot-based: reassign in place, region rewritten
                            pouch[rawIdx].isNew = true;   // the swapped-in item shows as new
                        }
                        hasUnsavedChanges = true;
                        // Land the cursor on the changed item (it may have re-sorted onto another page).
                        const std::vector<int> visibleIndices = visibleItemIndices();
                        int perPage = (CONTENT_PANEL_HEIGHT - 106 - listSearchBoxHeight()) / 52;
                        if (perPage < 1)
                            perPage = 1;
                        for (int viIndex = 0; viIndex < static_cast<int>(visibleIndices.size()); ++viIndex)
                        {
                            if (pouch[visibleIndices[viIndex]].itemId == newId)
                            {
                                selectedItemIndex = viIndex;
                                currentPage = viIndex / perPage;
                                break;
                            }
                        }
                    }
                }
                pickerActive = false;
                itemEditDialogActive = true; // return to the Edit Item dialog, now showing the new type
                return;
            }

            // Item creation: a pouch pick edits the trainer's BAG, not a Pokemon, so it has to
            // intercept before the apply-switch below -- pkPick is null in the items view.
            if ((buttonsDown & HidNpadButton_A) &&
                (pickerKind == Dialogs::PickerKind::PouchItem || pickerKind == Dialogs::PickerKind::PouchItemG1 ||
                 pickerKind == Dialogs::PickerKind::PouchItemG3))
            {
                if (pickerSelectedValue() >= 0 && selectedCategory >= 0 &&
                    selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    const uint16_t pickedItemId = static_cast<uint16_t>(pickerSelectedValue());
                    auto &pouch = trainer.items[selectedCategory];
                    const auto existingEntry = std::find_if(pouch.begin(), pouch.end(),
                                                [pickedItemId](const Trainer::InventoryItem &x)
                                                { return x.itemId == pickedItemId; });
                    const char *addVia = nullptr;
                    if (existingEntry != pouch.end())
                    {
                        // Present but empty -> re-activate it AND flag it new (a freshly-added
                        // item always shows the bag's "new" marker in the games that have one).
                        if (existingEntry->count == 0)
                        {
                            existingEntry->count = 1;
                            existingEntry->isNew = true;
                            hasUnsavedChanges = true;
                            addVia = "revived";
                        }
                    }
                    else if (static_cast<int>(pouch.size()) < currentPouchCapacity())
                    {
                        pouch.push_back(Trainer::InventoryItem{pickedItemId, 1, true, false});
                        hasUnsavedChanges = true;
                        addVia = "appended";
                    }
                    else
                    {
                        addVia = "REFUSED_POUCH_FULL";
                    }
                    if (g_debugLogging && addVia)
                    {
                        Utils::logEventToFile(
                            std::string("ITEM action=ADD ") +
                            Utils::logField("pouch",
                                            Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
                            " " + Utils::logField("item", Utils::itemName(pickedItemId, trainer.getGameGroup())) +
                            " id=" + std::to_string(pickedItemId) + " count=1 via=" + addVia + " pouchsize=" +
                            std::to_string(pouch.size()) + "/" + std::to_string(currentPouchCapacity()));
                    }
                    // Land the cursor on the row that was just added so A edits its amount next --
                    // and FOLLOW IT TO ITS PAGE. The list is paged, and a new item usually sorts
                    // onto a later page, so selecting it without moving the page left the user
                    // staring at an unchanged screen having to go find it themselves.
                    const std::vector<int> visibleIndices = visibleItemIndices();
                    int perPage =
                        (CONTENT_PANEL_HEIGHT - 106 - listSearchBoxHeight()) / 52; // mirrors ItemsPanel's row fit
                    if (perPage < 1)
                        perPage = 1;
                    for (int viIndex = 0; viIndex < static_cast<int>(visibleIndices.size()); ++viIndex)
                    {
                        if (pouch[visibleIndices[viIndex]].itemId == pickedItemId)
                        {
                            selectedItemIndex = viIndex;
                            currentPage = viIndex / perPage;
                            break;
                        }
                    }
                }
                pickerActive = false;
                return;
            }

            if ((buttonsDown & HidNpadButton_A) && pkPick)
            {
                switch (pickerKind)
                {
                case Dialogs::PickerKind::Nature:
                    pkPick->setNature(static_cast<uint8_t>(pickerSelectedValue()));
                    pkPick->setStatNature(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Gender:
                    // rows are the species' possible genders, so the row index is NOT the value
                    if (!pickerOrder.empty() && pickerSel < static_cast<int>(pickerOrder.size()))
                    {
                        const uint8_t genderValue = static_cast<uint8_t>(pickerSelectedValue());
                        pkPick->setGender(genderValue);
                        // Meowstic, Indeedee, Basculegion and Oinkologne store the gender AS the
                        // form, so the form has to move with it -- leaving a female Meowstic on
                        // the male form is the one combination the form picker will not produce.
                        const uint8_t newForm =
                            Pokemon::genderLinkedForm(pkPick->speciesID(), pkPick->form(), genderValue);
                        if (newForm != pkPick->form())
                            pkPick->setForm(newForm);
                    }
                    break;
                case Dialogs::PickerKind::Move:
                {
                    const int moveId = pickerSelectedValue();
                    pkPick->setMove(pickerSlot, static_cast<uint16_t>(moveId));
                    pkPick->setMovePP(pickerSlot,
                                      Names::getMoveBasePP(static_cast<uint16_t>(moveId),
                                                           pkPick->getGameGroup()));
                    pkPick->setMovePPUps(pickerSlot, 0);
                    break;
                }
                case Dialogs::PickerKind::Item:
                case Dialogs::PickerKind::ItemG1: // Gen 1 has no held items; unreachable, listed for the switch
                case Dialogs::PickerKind::ItemG3: // same write; only the id space differs
                    pkPick->setHeldItem(static_cast<uint16_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Level:
                    pkPick->setLevel(static_cast<uint8_t>(pickerSelectedValue() + 1)); // options 0-99 -> level 1-100
                    break;
                case Dialogs::PickerKind::Friendship:
                    pkPick->setFriendship(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Ball:
                    if (pickerSelectedValue() >= 0)
                        pkPick->setBall(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Ability:
                {
                    const int newValue = pickerSelectedValue();
                    pkPick->setAbility(static_cast<uint16_t>(newValue));
                    // Align the ability slot when the pick is one of the species' legal abilities.
                    // Gen 3 has no ability id field -- setAbility() already resolved the slot and
                    // re-rolled the PID there, so re-deriving the number would undo that work.
                    if (!Enums::isGen3Group(pkPick->getGameGroup()))
                    {
                        const Pokemon::AbilitySlots slots =
                            Pokemon::getAbilitySlots(pkPick->speciesID(), pkPick->form(), pkPick->getGameGroup());
                        const uint8_t abilityNumber =
                            Pokemon::getAbilityNumberForId(slots, static_cast<uint16_t>(newValue));
                        if (abilityNumber != 0)
                            pkPick->setAbilityNumber(abilityNumber);
                    }
                    break;
                }
                case Dialogs::PickerKind::Language:
                    pkPick->setLanguage(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Origin:
                    pkPick->setOriginGame(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::MetLevel:
                    // options 0-99 -> met level 1-100
                    pkPick->setMetLevel(static_cast<uint8_t>(pickerSelectedValue() + 1));
                    break;
                case Dialogs::PickerKind::MetLocation:
                    if (pickerSelectedValue() >= 0)
                    {
                        uint16_t locationId = static_cast<uint16_t>(pickerSelectedValue());
                        // egg-met vs met location
                        if (pickerMetIsEgg) pkPick->setEggLocation(locationId);
                        else
                            pkPick->setMetLocation(locationId);
                    }
                    break;
                case Dialogs::PickerKind::Form:
                    // rows are storable forms, so the row index is NOT the form id
                    if (pickerSelectedValue() >= 0)
                    {
                        const uint8_t newForm = static_cast<uint8_t>(pickerSelectedValue());
                        pkPick->setForm(newForm); // recalculates stats + checksum internally
                        // Gender follows the form for the species that tie the two together,
                        // otherwise the form picker is a second door into exactly the combination
                        // the gender picker refuses to offer. Two different ties, both real:
                        //   - Meowstic/Indeedee/Basculegion/Oinkologne -- the form IS the gender,
                        //     both ways, so the low bit of the new form is the new gender.
                        //   - the cap Pikachu, Ash Greninja, Bloodmoon Ursaluna -- a single-gender
                        //     form of a dual-gender species. One-way only: the form pins the
                        //     gender, but there is no counterpart form to flip to, which is why
                        //     this reads the per-form ratio instead of the low bit.
                        if (Pokemon::isFormGenderSpecific(pkPick->speciesID()))
                        {
                            const uint8_t genderValue = static_cast<uint8_t>(newForm & 1);
                            if (genderValue != pkPick->gender())
                                pkPick->setGender(genderValue);
                        }
                        else
                        {
                            const std::vector<int> genderValue = selectableGenders(pkPick->speciesID(), newForm);
                            if (genderValue.size() == 1 && genderValue[0] != static_cast<int>(pkPick->gender()))
                                pkPick->setGender(static_cast<uint8_t>(genderValue[0]));
                        }
                    }
                    break;
                case Dialogs::PickerKind::StatNature:
                    pkPick->setStatNature(static_cast<uint8_t>(pickerSelectedValue()));
                    break;
                case Dialogs::PickerKind::Species:
                    // Rows are the species this game HAS, so the row index is not the species
                    // id. (The creator intercepts its own Species picks earlier; this is the
                    // edit path, which re-points an existing Pokemon.)
                    if (pickerSelectedValue() >= 0)
                    {
                        const uint16_t pickedSpecies = static_cast<uint16_t>(pickerSelectedValue());
                        if (pickedSpecies != 0 && pickedSpecies != pkPick->speciesID())
                        {
                            if (g_debugLogging)
                            {
                                Utils::logEventToFile(
                                    std::string("MON action=SPECIES ") +
                                    Utils::logField("from", Trainer::getSpeciesName(pkPick->speciesID())) + " " +
                                    Utils::logField("to", Trainer::getSpeciesName(pickedSpecies)) + " " +
                                    Utils::briefPokemon(*pkPick));
                            }
                            applySpeciesChange(*pkPick, pickedSpecies, pkPick->getGameGroup());
                        }
                    }
                    break;
                case Dialogs::PickerKind::PouchItem:
                case Dialogs::PickerKind::PouchItemG1:
                case Dialogs::PickerKind::PouchItemG3:
                    break; // handled above -- these edit the bag, not a Pokemon
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
        // pokemon), so the global game-save must never fire there. Back out to the menu, then press X.
        if (buttonsDown & HidNpadButton_X)
        {
            const bool onHomeMenu = !detailViewActive;
            if (onHomeMenu && !saveConfirmActive && !itemEditDialogActive && !statEdit.dialogActive &&
                !releaseConfirmActive && !storageMenuActive && !groupMenuActive &&
                !storageExitConfirmActive && !details.active)
            {
                exitingWithUnsavedChanges = false; // Regular save, not exiting
                exitingViaPlus = false;
                // first row: the game save for a title session, the open backup for a backup session
                saveDestinationIndex = defaultSaveDestinationRow();
                saveConfirmActive = true;
                return;
            }
        }

        // Injecting into the real game save — the last gate, and the only thing PKSE does that
        // overwrites data the user cannot recover from inside the app. Handled BEFORE the save
        // dialog because saveConfirmActive is still true underneath it.
        if (saveInjectConfirmActive)
        {
            const int touchedButton = touchedButtonId(touch);
            if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A))
                return;
            if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B))
                return;
            if (buttonsDown & HidNpadButton_A)
            {
                saveInjectConfirmActive = false;
                performSave(backupDir, true);
                return;
            }
            if (buttonsDown & HidNpadButton_B)
            {
                saveInjectConfirmActive = false;
                return;
            } // back to the picker
            return;
        }

        if (saveConfirmActive)
        {
            if (exitingWithUnsavedChanges)
            {
                // Exiting with unsaved changes - different button logic
                // A: Discard changes and exit
                // B: Cancel exit (stay on screen)
                // Tappable buttons: id 0 = Cancel (B), id 1 = Discard & Exit (A).
                const int touchedButton = touchedButtonId(touch);
                if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B))
                    return;
                if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A))
                    return;
                if (buttonsDown & HidNpadButton_A)
                {
                    // User chose to discard changes and exit
                    saveConfirmActive = false;
                    if (exitingViaPlus)
                    {
                        // Exiting via + button - exit the app
                        exitRequested = true;
                    }
                    // Always set goBack for consistency (B button case)
                    goBack = true;
                    exitingWithUnsavedChanges = false;
                    exitingViaPlus = false;
                    return;
                }
                if (buttonsDown & HidNpadButton_B)
                {
                    // User cancelled exit - stay on screen
                    saveConfirmActive = false;
                    exitingWithUnsavedChanges = false;
                    exitingViaPlus = false;
                    return;
                }
            }
            else
            {
                // Regular save dialog (triggered by X button)
                // A: Save changes
                // B: Cancel
                // Destination picker: Up/Down choose, A writes there.
                const int nDest = saveDestinationCount();
                // lock may have gone off
                if (saveDestinationIndex >= nDest) saveDestinationIndex = 0;
                if (buttonsDown & HidNpadButton_Up)
                    saveDestinationIndex = (saveDestinationIndex - 1 + nDest) % nDest;
                if (buttonsDown & HidNpadButton_Down)
                    saveDestinationIndex = (saveDestinationIndex + 1) % nDest;
                const int touchedButton = touchedButtonId(touch);
                if (touchedButton >= 0 && touchedButton < nDest)
                {
                    saveDestinationIndex = touchedButton;
                    if (armTap(touchedButton, HidNpadButton_A))
                        return;
                }
                // The cursor is a ROW; which destination that row means depends on the session.
                const SaveDestination chosenDest = saveDestinationAt(saveDestinationIndex);

                if (buttonsDown & HidNpadButton_A)
                {
                    // A carried Pokemon lives outside both containers — return it before serializing
                    // so it isn't dropped from the save/bank.
                    returnHeldToOrigin();

                    // (The bank has its OWN persistence — it saves on storage-view exit / app exit,
                    // separate from this game-save, since it's a separate entity from the save file.)
                    if (chosenDest == DestinationExternalFile)
                    {
                        // Straight back to the file it came from. saveTrainerInfoRBY takes its own
                        // timestamped copy of the original bytes first, so there is no extra
                        // confirmation here -- the destructive step is already guarded.
                        performSave(backupDir, false);
                        return;
                    }
                    if (chosenDest == DestinationGameSave)
                    {
                        // Writing your OWN save back is the ordinary thing a save editor does -- the
                        // data being overwritten is the data we just read -- so it goes straight
                        // through. Only a backup-sourced session needs the extra gate, because that
                        // is the one that rolls the game backwards.
                        if (loadedFromCart)
                        {
                            performSave(backupDir, true);
                            return;
                        }
                        saveInjectConfirmActive = true;
                        return;
                    }

                    std::string destDir = backupDir;
                    if (chosenDest == DestinationNewBackup)
                    {
                        const Utils::KeyboardResult promptResult =
                            Utils::promptText("New Backup", "Backup name", titleName, 40);
                        // cancelled: leave the dialog up, nothing written
                        if (!promptResult.accepted) return;
                        destDir = createNamedBackupDir(promptResult.text);
                        if (destDir.empty())
                        {
                            postStatus("Couldn't create that backup. Use letters, numbers, spaces or -.", 480);
                            return;
                        }
                    }
                    performSave(destDir, false);
                    return;
                }
                if (buttonsDown & HidNpadButton_B)
                {
                    // User cancelled save - just close the dialog
                    saveConfirmActive = false;
                    return;
                }
            }
            return; // Don't process other inputs while save confirm is active
        }

        if (statEdit.dialogActive)
        {
            // Touch: map on-screen buttons to their equivalent presses (IV/EV rows -> Up/Down mode
            // switch, the step buttons -> ZL/L/Left/Right/R/ZR, Save -> A, Cancel -> B), then reuse
            // the button logic below.
            switch (touchedButtonId(touch))
            {
            case 10:
                buttonsDown |= HidNpadButton_Up;
                break;
            case 11:
                buttonsDown |= HidNpadButton_Down;
                break;
            case 34:
                buttonsDown |= HidNpadButton_ZL;
                break;
            case 30:
                buttonsDown |= HidNpadButton_L;
                break;
            case 31:
                buttonsDown |= HidNpadButton_Left;
                break;
            case 32:
                buttonsDown |= HidNpadButton_Right;
                break;
            case 33:
                buttonsDown |= HidNpadButton_R;
                break;
            case 35:
                buttonsDown |= HidNpadButton_ZR;
                break;
            // Save and Cancel CLOSE the dialog, so they are deferred a frame to be seen held --
            // the step buttons above are not, since the number changing is its own feedback and a
            // lagged nudge just reads as a sluggish control.
            case 1:
                if (armTap(1, HidNpadButton_A))
                    return;
                break;
            case 0:
                if (armTap(0, HidNpadButton_B))
                    return;
                break;
            default:
                break;
            }

            // Resolve the Pokemon being edited (party / box / bank).
            Pokemon::Pokemon *pokemon = detailsTargetPokemon();

            if (pokemon)
            {
                // Let's Go uses Awakening Values (AVs), not EVs — EVs are inert there. So the
                // second editable stat is AV for LGPE Pokemon, EV for everything else.
                const bool usesAV = pokemon->hasAwakeningValues();
                const Dialogs::StatEditMode secondMode = usesAV ? Dialogs::StatEditMode::AV
                                                                : Dialogs::StatEditMode::EV;

                // Up/Down to switch between IV and the second stat (EV / AV)
                if (buttonsDown & HidNpadButton_Up)
                {
                    if (statEdit.mode == secondMode)
                    {
                        if (usesAV)
                            statEdit.currentAV = statEdit.value;
                        else
                            statEdit.currentEV = statEdit.value;
                    }
                    statEdit.mode = Dialogs::StatEditMode::IV;
                    statEdit.value = statEdit.currentIV; // Load IV value
                }
                if (buttonsDown & HidNpadButton_Down)
                {
                    if (statEdit.mode == Dialogs::StatEditMode::IV)
                    {
                        statEdit.currentIV = statEdit.value;
                    }
                    statEdit.mode = secondMode;
                    statEdit.value = usesAV ? statEdit.currentAV : statEdit.currentEV; // Load AV/EV value
                }

                // Ranges come from the FORMAT, not from constants. Gen 1/2 store 4-bit DVs
                // (max 15) and 16-bit Stat Experience (max 65535); hardcoding 31/252 lets the
                // editor offer values those formats cannot hold, and clamps away ones they can.
                int minValue = 0;
                // Illegal-values override (Settings) lifts the EV/AV per-stat cap to the byte
                // ceiling. The IV cap is the format's own -- there is no "illegal" IV to reach.
                const int fmtMaxIV = pokemon->maxIV();
                const int fmtMaxEV = pokemon->maxEV();
                // A format whose EV field is wider than a byte has no separate legal cap below
                // it: 65535 Stat Exp is the GAME's own maximum, not an illegal edit.
                const int evCap = (fmtMaxEV > 255) ? fmtMaxEV : (g_allowIllegalEdits ? 255 : 252);
                int maxValue = (statEdit.mode == Dialogs::StatEditMode::IV)   ? fmtMaxIV
                               : (statEdit.mode == Dialogs::StatEditMode::AV) ? (g_allowIllegalEdits ? 255 : 200)
                                                                              : evCap;

                // Adjust value: Left/Right = -/+1, L/R = -/+10, ZL/ZR = -/+100. On a 0-31 IV the
                // +/-100 steps just clamp, which is harmless.
                if (buttonsDown & HidNpadButton_Left)
                    statEdit.value = std::max(minValue, statEdit.value - 1);
                if (buttonsDown & HidNpadButton_Right)
                    statEdit.value = std::min(maxValue, statEdit.value + 1);
                if (buttonsDown & HidNpadButton_L)
                    statEdit.value = std::max(minValue, statEdit.value - 10);
                if (buttonsDown & HidNpadButton_R)
                    statEdit.value = std::min(maxValue, statEdit.value + 10);
                if (buttonsDown & HidNpadButton_ZL)
                    statEdit.value = std::max(minValue, statEdit.value - 100);
                if (buttonsDown & HidNpadButton_ZR)
                    statEdit.value = std::min(maxValue, statEdit.value + 100);

                // Store back into whichever value the active mode edits.
                if (statEdit.mode == Dialogs::StatEditMode::IV)
                    statEdit.currentIV = statEdit.value;
                else if (statEdit.mode == Dialogs::StatEditMode::AV)
                    statEdit.currentAV = statEdit.value;
                else
                    statEdit.currentEV = statEdit.value;

                // For EVs, enforce the 510 legal total — unless the illegal-values override is on
                // (510 is a game rule, not a byte limit; the illegal ceiling is 6 x 255 = 1530).
                // The 510 total is a Gen 3+ rule. Gen 1/2 Stat Experience has no combined cap
                // at all -- each stat fills to 65535 independently -- so applying it there would
                // clamp perfectly legal training away, and the sum itself would be computed from
                // the saturating 8-bit accessors and be meaningless anyway.
                if (statEdit.mode == Dialogs::StatEditMode::EV && !g_allowIllegalEdits && fmtMaxEV <= 255)
                {
                    int totalEVs = pokemon->evHP() + pokemon->evATK() + pokemon->evDEF() +
                                   pokemon->evSPE() + pokemon->evSPA() + pokemon->evSPD();
                    int projectedTotal = totalEVs - statEdit.originalEV + statEdit.value;
                    if (projectedTotal > 510)
                    {
                        statEdit.value = std::max(0, 510 - (totalEVs - statEdit.originalEV));
                    }
                }

                // Confirm edit
                if (buttonsDown & HidNpadButton_A)
                {
                    // Remember that something outside the games' own limits is being committed,
                    // so the save can warn ONCE rather than the editor nagging per field. Only
                    // reachable with "Allow illegal values" on; the caps clamp otherwise.
                    // Only meaningful where EVs are the 0-252/510 quantity. A Gen 1 Stat Exp of
                    // 60000 is not an illegal edit, and the 8-bit sum below cannot represent it.
                    if (fmtMaxEV <= 255)
                    {
                        const int evTotalAfter = pokemon->evHP() + pokemon->evATK() + pokemon->evDEF() +
                                                 pokemon->evSPE() + pokemon->evSPA() + pokemon->evSPD() -
                                                 statEdit.originalEV + statEdit.currentEV;
                        if (statEdit.currentEV > 252 || statEdit.currentAV > 200 || evTotalAfter > 510)
                            illegalDataWritten = true;
                    }
                    else if (statEdit.currentAV > 200)
                    {
                        illegalDataWritten = true;
                    }

                    // Map UI stat index to Pokemon data stat index
                    // UI order: HP, ATK, DEF, SPA, SPD, SPE (indices 0-5)
                    // Data order: HP, ATK, DEF, SPE, SPA, SPD (indices 0-5)
                    int statIndexMap[] = {0, 1, 2, 4, 5, 3}; // UI index -> Data index
                    int dataStatIndex = statIndexMap[statEdit.selectedStat];

                    // Apply IV always; apply AV for Let's Go, EV otherwise.
                    bool ivEvModified = false; // IV/EV changes want a PID refresh; AV doesn't
                    bool anyModified = false;
                    if (statEdit.currentIV != statEdit.originalIV)
                    {
                        pokemon->setIV(dataStatIndex, statEdit.currentIV);
                        hasUnsavedChanges = true;
                        ivEvModified = true;
                        anyModified = true;
                    }
                    if (usesAV)
                    {
                        if (statEdit.currentAV != statEdit.originalAV)
                        {
                            pokemon->setAV(dataStatIndex, static_cast<uint8_t>(statEdit.currentAV));
                            hasUnsavedChanges = true;
                            anyModified = true;
                        }
                    }
                    else if (statEdit.currentEV != statEdit.originalEV)
                    {
                        // setEVWide, so a 16-bit Stat Experience value survives the write. It
                        // forwards to setEV() for every format where the two are the same thing.
                        pokemon->setEVWide(dataStatIndex, static_cast<uint16_t>(statEdit.currentEV));
                        hasUnsavedChanges = true;
                        ivEvModified = true;
                        anyModified = true;
                    }

                    // Regenerate PID to maintain legality after IV/EV changes (AVs don't affect it).
                    if (ivEvModified)
                    {
                        pokemon->regeneratePID(pokemon->id32());
                    }
                    // If this is an LGPE party member, mirror the edit to its party copy so the
                    // save's party overlay doesn't clobber it.
                    if (anyModified)
                    {
                        mirrorEditedPartyMember();
                    }

                    statEdit.dialogActive = false;
                    return;
                }

                // Cancel edit
                if (buttonsDown & HidNpadButton_B)
                {
                    statEdit.dialogActive = false;
                    return;
                }
            }

            return; // Don't process other inputs while stat edit dialog is active
        }

        // Handle Pokemon details modal (HOME Check-Summary editor page).
        if (details.active)
        {
            Pokemon::Pokemon *pokemon = detailsTargetPokemon();
            int touchedButton = touchedButtonId(touch);

            // Creator: a freshly-made pokemon isn't committed until accepted. Closing raises this prompt --
            // Keep (A / right button) accepts it; Discard (B / left button) removes it from the box.
            if (creator.keepConfirmActive)
            {
                // Three glyph buttons: id 0 = Back (B), id 2 = Discard (Y), id 1 = Keep (A).
                if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B))
                    return;
                if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A))
                    return;
                if (touchedButton == 2 && armTap(touchedButton, HidNpadButton_Y))
                    return;
                if (buttonsDown & HidNpadButton_B)
                {
                    creator.keepConfirmActive = false;
                    return;
                } // back to editing the new pokemon
                const bool discard = (buttonsDown & HidNpadButton_Y);
                if ((buttonsDown & HidNpadButton_A) || discard)
                {
                    // Both outcomes are logged. A kept pokemon is a new record entering the save and is
                    // the thing a later "this Pokemon is illegal/corrupt" report is about, so it is
                    // described in full; a discarded one explains why a slot the user remembers
                    // filling is empty.
                    if (g_debugLogging)
                    {
                        const auto &made = storageSlot(creator.pane, creator.box, creator.slot);
                        if (made)
                        {
                            Utils::logEventToFile(std::string("CREATE result=") + (discard ? "DISCARDED" : "KEPT") +
                                                  " " + Utils::logSlot("at", creator.pane, creator.box, creator.slot) +
                                                  " " + Utils::describePokemon(*made, trainer));
                        }
                    }
                    if (discard)
                    { // remove the just-created pokemon from the box
                        storageSlot(creator.pane, creator.box, creator.slot).reset();
                        hasUnsavedChanges = true;
                    }
                    creator.keepConfirmActive = false;
                    creator.editing = false;
                    details.active = false;
                    details.source = EditSource::Box;
                    details.selectedField = 0;
                    details.legalityOverlay = false;
                    details.ribbonOverlay = false;
                }
                return;
            }

            // Unsaved edits on an EXISTING pokemon. Closing the page discards them, so say so first
            // rather than silently rolling back -- Save commits (same as X), Discard throws the
            // edits away, Back returns to the page. Mirrors the creator's Keep/Discard prompt,
            // which already covered the not-yet-accepted case.
            if (tradePartnerTitles.active)
            {
                const int candidateCount = static_cast<int>(tradePartnerTitles.candidates.size());
                if (buttonsDown & HidNpadButton_B)
                {
                    tradePartnerTitles.active = false;
                    return;
                }
                if (candidateCount > 0)
                {
                    if (buttonsDown & HidNpadButton_Up)
                        tradePartnerTitles.selectedIndex =
                            (tradePartnerTitles.selectedIndex + candidateCount - 1) % candidateCount;
                    if (buttonsDown & HidNpadButton_Down)
                        tradePartnerTitles.selectedIndex = (tradePartnerTitles.selectedIndex + 1) % candidateCount;
                    // Keep the selection on screen; the list draws six rows at a time.
                    constexpr int visibleRows = 6;
                    if (tradePartnerTitles.selectedIndex < tradePartnerTitles.scroll)
                        tradePartnerTitles.scroll = tradePartnerTitles.selectedIndex;
                    else if (tradePartnerTitles.selectedIndex >= tradePartnerTitles.scroll + visibleRows)
                        tradePartnerTitles.scroll = tradePartnerTitles.selectedIndex - visibleRows + 1;

                    if (touchedButton >= 0 && touchedButton < candidateCount)
                    {
                        tradePartnerTitles.selectedIndex = touchedButton;
                        if (armTap(touchedButton, HidNpadButton_A))
                            return;
                    }
                    if (buttonsDown & HidNpadButton_A)
                    {
                        tradeEvolveWithChosenTitle(tradePartnerTitles.candidates[tradePartnerTitles.selectedIndex]);
                        return;
                    }
                }
                return; // nothing else reaches the page while the picker is up
            }
            if (tradeEvolveChoiceActive)
            {
                // Same list controls as the donor title picker above -- Up/Down/A/B -- because it
                // is the same kind of question, and a second scheme for a two-row list would be a
                // second thing to learn.
                Pokemon::Pokemon *choosing = detailsTargetPokemon();
                const int candidateCount =
                    choosing ? Pokemon::getTradeEvolutionOffer(*choosing).candidateCount : 0;
                if ((buttonsDown & HidNpadButton_B) || candidateCount <= 0)
                {
                    tradeEvolveChoiceActive = false;
                    return;
                }
                if (buttonsDown & HidNpadButton_Up)
                    tradeEvolveChoiceIndex = (tradeEvolveChoiceIndex + candidateCount - 1) % candidateCount;
                if (buttonsDown & HidNpadButton_Down)
                    tradeEvolveChoiceIndex = (tradeEvolveChoiceIndex + 1) % candidateCount;
                if (touchedButton >= 0 && touchedButton < candidateCount)
                {
                    tradeEvolveChoiceIndex = touchedButton;
                    if (armTap(touchedButton, HidNpadButton_A))
                        return;
                }
                if (buttonsDown & HidNpadButton_A)
                {
                    beginTradeEvolve(tradeEvolveChoiceIndex);
                    return;
                }
                return; // nothing else reaches the page while the choice is up
            }
            if (tradeEvolveConfirmActive)
            {
                if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B))
                    return;
                if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A))
                    return;
                if (touchedButton == 2 && armTap(touchedButton, HidNpadButton_Y))
                    return;
                if (buttonsDown & HidNpadButton_B)
                {
                    tradeEvolveConfirmActive = false;
                    return;
                }
                if (buttonsDown & HidNpadButton_A)
                { // pick a save you own -- a real trainer
                    tradeEvolveConfirmActive = false;
                    // A SWITCH TITLE'S SAVE IS NOT A FILE ON THE CARD. Which route a record's donor
                    // comes down is decided by asking whether its FORMAT opens from a file at all,
                    // read off the LOOSE_FORMATS table itself -- not by whether the console happens
                    // to have a compatible title installed. Those are different questions, and
                    // answering the first with the second sent a Sword owner with no Shield save to
                    // a file browser that could never open one.
                    if (Save::groupOpensFromFile(pokemon->getGameGroup()))
                        openTradePartnerBrowser();
                    else
                        openTradePartnerTitlePicker();
                    return;
                }
                if (buttonsDown & HidNpadButton_Y)
                { // invent one -- see generateTradePartner
                    tradeEvolveWithGeneratedTrainer();
                    return;
                }
                return; // nothing else reaches the page while the choice is open
            }
            if (details.discardConfirmActive)
            {
                if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B))
                    return;
                if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A))
                    return;
                if (touchedButton == 2 && armTap(touchedButton, HidNpadButton_Y))
                    return;
                if (buttonsDown & HidNpadButton_B)
                {
                    details.discardConfirmActive = false;
                    return;
                } // keep editing
                if (buttonsDown & HidNpadButton_A)
                { // Save: exactly what X does, then leave
                    details.discardConfirmActive = false;
                    if (g_debugLogging && pokemonEditDirty())
                    {
                        if (const Pokemon::Pokemon *t = detailsTargetPokemon())
                            Utils::logEventToFile("MODIFY via=save-on-close " + Utils::describePokemon(*t, trainer));
                    }
                    snapshotEditTarget(); // baseline := current, so nothing rolls back
                    closeDetailsModal();  // hasUnsavedChanges + party mirror were
                    return;               // already handled as each field was edited
                }
                if (buttonsDown & HidNpadButton_Y)
                { // Discard: roll back and leave
                    details.discardConfirmActive = false;
                    if (g_debugLogging)
                    {
                        if (const Pokemon::Pokemon *t = detailsTargetPokemon())
                            Utils::logEventToFile("MODIFY result=DISCARDED via=close-prompt " +
                                                  Utils::briefPokemon(*t));
                    }
                    restoreEditTarget();
                    closeDetailsModal();
                    return;
                }
                return;
            }

            // Legality overlay (full issue list) intercepts input while open: B or any tap closes it.
            if (details.legalityOverlay)
            {
                if ((buttonsDown & HidNpadButton_B) || touchedButton >= 0)
                    details.legalityOverlay = false;
                return;
            }
            // Ribbon overlay (full ribbon/mark list) does the same.
            if (details.ribbonOverlay)
            {
                if ((buttonsDown & HidNpadButton_B) || touchedButton >= 0)
                    details.ribbonOverlay = false;
                return;
            }
            // Open the ribbon list: Y, or tapping the Ribbons row (id 94).
            if ((buttonsDown & HidNpadButton_Y) || touchedButton == 94)
            {
                details.ribbonOverlay = true;
                return;
            }
            // Open the legality issue list: R, or tapping the legality summary (id 95) -- but ONLY
            // when the report has something in it. That includes a clean pokemon with Layer 3 notes
            // (which encounter it matched, or why the check could not run); a truly empty report
            // has nothing to show, so the button is disabled (greyed in the guide) and this does nothing.
            if ((buttonsDown & HidNpadButton_R) || touchedButton == 95)
            {
                if (pokemon && !Legality::analyze(*pokemon, pokemon->getGameGroup()).empty())
                    details.legalityOverlay = true;
                return;
            }
            // X: for an EXISTING pokemon, SAVE the edits -- commit the current state as the new baseline
            // (the "Unsaved changes" marker clears) and STAY on the page. This is the ONLY commit:
            // closing with B/close rolls back to this baseline, so any later edits are discarded. For a
            // freshly-CREATED pokemon there is nothing to "save": every field is an unsaved edit until
            // committed, so X is KEEP -- finalize the pokemon (already in the box) and close. Discard still
            // lives on the B Keep/Discard prompt.
            if (buttonsDown & HidNpadButton_X)
            {
                if (creator.editing)
                {
                    creator.editing = false;
                    closeDetailsModal();
                    return;
                }
                if (g_debugLogging && pokemonEditDirty())
                {
                    if (const Pokemon::Pokemon *t = detailsTargetPokemon())
                        Utils::logEventToFile("MODIFY via=save-button " + Utils::describePokemon(*t, trainer));
                }
                snapshotEditTarget(); // baseline := current -> pokemonEditDirty() now false
                return;
            }
            // Randomize IVs: L only. (The on-panel Randomize row was removed; L is the trigger,
            // and the bottom nav bar advertises it.)
            if (buttonsDown & HidNpadButton_L)
            {
                if (Pokemon::Pokemon *target = detailsTargetPokemon())
                {
                    randomizeIVs(target);
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                }
                return;
            }

            // Touch: close button (99), or a stat row (0-5) / shiny (6) -> select + act (like A).
            // Close DISCARDS unsaved edits -- restoreEditTarget() rolls the pokemon back to the last
            // Save/open snapshot; the individual Save button (X) is the only commit. The creator is the
            // exception: its not-yet-committed pokemon asks Keep/Discard instead.
            if (touchedButton == 99)
            {
                if (creator.editing)
                {
                    creator.keepConfirmActive = true;
                    creator.keepConfirmIndex = 1;
                    return;
                }
                if (pokemonEditDirty())
                {
                    details.discardConfirmActive = true;
                    return;
                }
                restoreEditTarget();
                closeDetailsModal();
                return;
            }
            if (touchedButton >= 0 && touchedButton <= 33)
            {
                details.selectedField = touchedButton;   // draws highlighted this frame
                // ...and opens on the next
                if (armTap(touchedButton, HidNpadButton_A)) return;
            }

            // B closes the page. Unsaved edits would be rolled back to the last Save/open snapshot,
            // so ask before throwing them away.
            if (buttonsDown & HidNpadButton_B)
            {
                if (creator.editing)
                {
                    creator.keepConfirmActive = true;
                    creator.keepConfirmIndex = 1;
                    return;
                }
                if (pokemonEditDirty())
                {
                    details.discardConfirmActive = true;
                    return;
                }
                restoreEditTarget();
                closeDetailsModal();
                return;
            }

            // Three-column navigation: Up/Down cycle within the current column; Left/Right hop between the
            // center column (0-9: stats/shiny/nature/gender/level), the moves column (10-14, right panel),
            // and the editable detail column (left panel; ids 15 and up, navigated in draw order).
            {
                int &fieldIndex = details.selectedField;
                // Left-pane editable field ids in DRAW (top-to-bottom) order, built by the modal draw
                // (PokemonDetailsModal.cpp) so conditional rows (Form, Stat Nature, Pokerus, ...) appear
                // only when actually shown and the cursor visits exactly what's on screen. Fall back to
                // the always-present rows on the very first frame, before the draw has run once.
                std::vector<int> leftColumnOrder = details.leftOrder;
                if (leftColumnOrder.empty())
                    leftColumnOrder = {32, 23, 15, 16, 17, 18, 25, 19, 20, 21};
                const bool inRight = (fieldIndex >= 10 && fieldIndex <= 14);
                const bool inLeft = (fieldIndex >= 15);
                auto leftMove = [&](int direction) -> int
                {
                    int position = 0;
                    for (int lIndex = 0; lIndex < static_cast<int>(leftColumnOrder.size()); ++lIndex)
                        if (leftColumnOrder[lIndex] == fieldIndex)
                        {
                            position = lIndex;
                            break;
                        }
                    return leftColumnOrder[(position + direction + static_cast<int>(leftColumnOrder.size())) %
                                           static_cast<int>(leftColumnOrder.size())];
                };
                // Center column: 0-9 in a fixed cycle, except that a read-only Gender row (8) is
                // stepped over -- the same treatment the left column gives a read-only row by simply
                // not listing it. See genderEditable: locked means there is nothing to change it to.
                const bool skipGender = pokemon && !genderEditable(*pokemon);
                // Nature (7) is skipped the same way for a format that has none -- the row is not
                // drawn at all there, so stopping on it would park the cursor on blank panel.
                const bool skipNature = pokemon && !pokemon->hasNature();
                auto centerMove = [&](int direction) -> int
                {
                    int newForm = fieldIndex;
                    for (int index = 0; index < 10; ++index)
                    {
                        newForm = (newForm + direction + 10) % 10;
                        if ((newForm == 8 && skipGender) || (newForm == 7 && skipNature))
                            continue;
                        break;
                    }
                    return newForm;
                };
                if (buttonsDown & HidNpadButton_Up)
                    fieldIndex = inRight  ? 10 + ((fieldIndex - 10 - 1 + 5) % 5)
                        : inLeft ? leftMove(-1)
                                 : centerMove(-1);
                if (buttonsDown & HidNpadButton_Down)
                    fieldIndex = inRight  ? 10 + ((fieldIndex - 10 + 1) % 5)
                        : inLeft ? leftMove(+1)
                                 : centerMove(+1);
                if (buttonsDown & HidNpadButton_Right)
                {
                    // left  -> center
                    if (inLeft) fieldIndex = details.lastCenterField;
                    else if (!inRight)
                    {
                        details.lastCenterField = fieldIndex;
                        fieldIndex = 10;
                    } // center -> right
                }
                if (buttonsDown & HidNpadButton_Left)
                {
                    // right -> center
                    if (inRight) fieldIndex = details.lastCenterField;
                    else if (!inLeft)
                    {
                        details.lastCenterField = fieldIndex;
                        fieldIndex = leftColumnOrder.front();
                    } // center -> left
                }
                // Catch every other way the cursor can land on a locked Gender row -- a remembered
                // lastCenterField, or a pokemon swapped out from under the cursor -- in one place.
                if (fieldIndex == 8 && skipGender)
                    fieldIndex = skipNature ? 6 : 7;
                if (fieldIndex == 7 && skipNature)
                    fieldIndex = 6;
            }

            // A on an editable row opens the right editor: stat dialog (0-5), shiny toggle (6), or the
            // selection picker (7 nature, 8 gender, 9-12 the four move slots).
            if ((buttonsDown & HidNpadButton_A) && pokemon)
            {
                const int fieldIndex = details.selectedField;
                if (fieldIndex == 6)
                {
                    bool currentShiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
                    pokemon->setShiny(!currentShiny, pokemon->id32());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember(); // keep an LGPE party member's box/party copies in sync
                }
                else if (fieldIndex == 7 && pokemon->hasNature())
                { // Nature
                    pickerKind = Dialogs::PickerKind::Nature;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->nature();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 8 && genderEditable(*pokemon))
                { // Gender (read-only when fixed)
                    pickerKind = Dialogs::PickerKind::Gender;
                    // only the genders this species/form can be; a fixed-gender species never gets
                    // here at all, so every picker opened from this row has a real choice in it
                    buildGenderPickerOrder(pokemon->speciesID(), pokemon->form(), pokemon->gender());
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 9)
                { // Level (1-100 picker; box mons derive the level from EXP)
                    pickerKind = Dialogs::PickerKind::Level;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    uint8_t levelValue = pokemon->level();
                    if (levelValue == 0)
                        levelValue =
                            Pokemon::getLevelFromExp(pokemon->exp(), Pokemon::getGrowthRate(pokemon->speciesID()));
                    pickerSel = (levelValue > 0 ? levelValue : 1) - 1;
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex >= 10 && fieldIndex <= 13)
                { // Move slots 0-3
                    pickerKind = Dialogs::PickerKind::Move;
                    pickerSlot = fieldIndex - 10;
                    // learnable moves first (green + top), for the pokemon's own game. Illegal moves are
                    // still offered (after the legal ones) and flagged by the legality check, PKHeX-style.
                    buildMovePickerOrder(pokemon->speciesID(), pokemon->form(), pokemon->getGameGroup(),
                                         pokemon->move(pickerSlot));
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 14)
                { // Held item
                    // Gen 3 held items are a separate, much smaller id space -- offering the modern
                    // list would both misname every entry and let an id be set that Gen 3 has no
                    // item for.
                    pickerKind = (pokemon->getGameGroup() == GameVersion::RBY)    ? Dialogs::PickerKind::ItemG1
                                 : Enums::isGen3Group(pokemon->getGameGroup())    ? Dialogs::PickerKind::ItemG3
                                                                                  : Dialogs::PickerKind::Item;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->heldItem();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 15)
                { // Ability
                    pickerKind = Dialogs::PickerKind::Ability;
                    // the species' legal ability slots for the pokemon's own game (green + top); sets
                    // pickerOrder, pickerLegalCount, pickerSel.
                    buildAbilityPickerOrder(pokemon->speciesID(), pokemon->form(),
                                            pokemon->getGameGroup(), pokemon->ability());
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 16)
                { // Friendship
                    pickerKind = Dialogs::PickerKind::Friendship;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->friendship();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 32)
                { // Species
                    pickerKind = Dialogs::PickerKind::Species;
                    // The MON's game group, not the save's: a bank slot holds a foreign pokemon, and
                    // the species it may become are its own game's.
                    buildSpeciesPickerOrder(pokemon->getGameGroup(), pokemon->speciesID());
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 33)
                { // Trade Evolve -- run the evolution a trade would have triggered, in place.
                    // No confirmation for the evolution itself: the page already holds a snapshot,
                    // so B -> Discard puts the pokemon back exactly as it was, which is a better
                    // undo than a dialog in front of every press.
                    const Pokemon::TradeEvolutionOffer tradeOffer = Pokemon::getTradeEvolutionOffer(*pokemon);
                    tradePartnerPick = TradePartnerPickState{};
                    const int resolvedIndex = tradeOffer.resolvedIndex();
                    if (!tradeOffer.hasCandidate())
                    {
                        // Unreachable from the UI -- the button is not drawn at all without one.
                    }
                    else if (resolvedIndex < 0)
                    {
                        // Two destinations and no held item naming either: Clamperl, and only
                        // Clamperl. The item is the games' way of choosing, so with none held the
                        // user is the only thing left that can.
                        tradeEvolveChoiceIndex = 0;
                        tradeEvolveChoiceActive = true;
                    }
                    else
                    {
                        beginTradeEvolve(resolvedIndex);
                    }
                }
                else if (fieldIndex == 26)
                { // Form (species alternate forms)
                    pickerKind = Dialogs::PickerKind::Form;
                    pickerFormSpecies = pokemon->speciesID();
                    // The MON's game group, not the save's: a bank slot holds a foreign pokemon, and the
                    // forms it can have are its own game's, not the open save's.
                    buildFormPickerOrder(pokemon->speciesID(), pokemon->form(),
                                         pokemon->getGameGroup()); // sets pickerOrder + pickerSel
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 27)
                { // Stat Nature (mint / effective-stat nature)
                    pickerKind = Dialogs::PickerKind::StatNature;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->statNature();
                    if (pickerSel >= 25)
                        pickerSel = pokemon->nature();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 23)
                { // Nickname (blocking text keyboard)
                    // Field width is the format's, not a constant: Gen 3 holds 10 characters, the rest 12.
                    std::string currentText = Utils::utf16ToUtf8(pokemon->nickname());
                    Utils::KeyboardResult keyboardResult = Utils::promptText("Nickname", "Nickname", currentText,
                                                                 pokemon->getMaxNicknameLength());
                    if (keyboardResult.accepted)
                    {
                        if (!keyboardResult.text.empty())
                        {
                            const std::u16string want = Utils::utf8ToUtf16(keyboardResult.text);
                            // Refuse rather than mangle -- Gen 3's character set predates Unicode, and
                            // its encoder would end the name at the first character it can't map.
                            if (!pokemon->canStoreNickname(want))
                            {
                                postStatus("This game can't store one of those characters.");
                            }
                            else
                            {
                                pokemon->setNickname(want);
                                pokemon->setIsNicknamed(true); // deliberate custom name
                                hasUnsavedChanges = true;
                                mirrorEditedPartyMember();
                            }
                        }
                        else
                        {
                            // An empty entry resets to the species default name (not nicknamed).
                            pokemon->setNickname(Utils::utf8ToUtf16(defaultNicknameFor(*pokemon)));
                            pokemon->setIsNicknamed(false);
                            hasUnsavedChanges = true;
                            mirrorEditedPartyMember();
                        }
                    }
                }
                else if (fieldIndex == 24)
                { // EXP (blocking numeric keypad)
                    uint32_t maxExp = Pokemon::getExpForLevel(100, Pokemon::getGrowthRate(pokemon->speciesID()));
                    Utils::NumberResult numberResult = Utils::promptNumber("EXP", static_cast<int>(pokemon->exp()),
                                                                 0, static_cast<int>(maxExp));
                    if (numberResult.accepted)
                    {
                        pokemon->setExp(static_cast<uint32_t>(numberResult.value));
                        hasUnsavedChanges = true;
                        mirrorEditedPartyMember();
                    }
                }
                else if (fieldIndex == 28)
                { // Met Date -- ONE numeric keypad (YYYYMMDD).
                    // Chaining several swkbd launches inside a single frame crashes on hardware (a library
                    // applet can't be relaunched back-to-back), so the whole date is one field.
                    int currentText = (2000 + pokemon->metYear()) * 10000 +
                                      (pokemon->metMonth() ? pokemon->metMonth() : 1) * 100 +
                                      (pokemon->metDay() ? pokemon->metDay() : 1);
                    Utils::NumberResult dateResult =
                        Utils::promptNumber("Met Date (YYYYMMDD)", currentText, 20000101, 20991231);
                    if (dateResult.accepted)
                    {
                        int year = dateResult.value / 10000, mm = (dateResult.value / 100) % 100,
                            dd = dateResult.value % 100;
                        if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31)
                        {
                            pokemon->setMetYear(static_cast<uint8_t>((year - 2000) & 0xFF));
                            pokemon->setMetMonth(static_cast<uint8_t>(mm));
                            pokemon->setMetDay(static_cast<uint8_t>(dd));
                            hasUnsavedChanges = true;
                            mirrorEditedPartyMember();
                        }
                        else
                        {
                            postStatus("Invalid date -- use YYYYMMDD.", 200);
                        }
                    }
                }
                else if (fieldIndex == 29)
                { // Egg Location -- the in-world list PLUS the egg SOURCES
                    const uint8_t originVersion = pokemon->originGame();
                    Names::LocationTable locationTable = Names::getLocationTable(originVersion);
                    pickerOrder.clear();
                    for (uint16_t pickedItemId = 0; pickedItemId < locationTable.count; ++pickedItemId)
                        if (locationTable.names[pickedItemId][0] != '\0')
                            pickerOrder.push_back(pickedItemId);
                    // Bank 6 is who the egg came FROM ("Nursery Couple"), and it is what a hatched
                    // egg's location field actually holds. It was reachable to DISPLAY all along
                    // and never offered here, which is what the report hit: the entry the reporter
                    // wanted was in the table, just not in the list. The met-location picker below
                    // deliberately still shows bank 0 only -- these are not places a Pokemon is met.
                    const Names::LocationTable eggSources = Names::getEggSourceLocationTable(originVersion);
                    for (uint16_t sourceIndex = 0; sourceIndex < eggSources.count; ++sourceIndex)
                        if (eggSources.names[sourceIndex][0] != '\0')
                            pickerOrder.push_back(
                                static_cast<int>(Names::EGG_SOURCE_LOCATION_BASE) + sourceIndex);
                    if (!pickerOrder.empty())
                    {
                        pickerMetVersion = originVersion;
                        pickerMetIsEgg = true; // this picker targets the egg-met location
                        pickerKind = Dialogs::PickerKind::MetLocation;
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0;
                        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size());
                             ++pickerOrderIndex)
                            if (pickerOrder[pickerOrderIndex] == pokemon->eggLocation())
                            {
                                pickerSel = pickerOrderIndex;
                                break;
                            }
                        pickerActive = true;
                        clearPickerSearch();
                    }
                    else
                    {
                        postStatus("No met-location list for this game.", 200);
                    }
                }
                else if (fieldIndex == 30)
                { // Egg Date -- ONE numeric keypad (YYYYMMDD), see Met Date.
                    int currentText = (2000 + pokemon->eggYear()) * 10000 +
                                      (pokemon->eggMonth() ? pokemon->eggMonth() : 1) * 100 +
                                      (pokemon->eggDay() ? pokemon->eggDay() : 1);
                    Utils::NumberResult dateResult =
                        Utils::promptNumber("Egg Date (YYYYMMDD)", currentText, 20000101, 20991231);
                    if (dateResult.accepted)
                    {
                        int year = dateResult.value / 10000, mm = (dateResult.value / 100) % 100,
                            dd = dateResult.value % 100;
                        if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31)
                        {
                            pokemon->setEggYear(static_cast<uint8_t>((year - 2000) & 0xFF));
                            pokemon->setEggMonth(static_cast<uint8_t>(mm));
                            pokemon->setEggDay(static_cast<uint8_t>(dd));
                            hasUnsavedChanges = true;
                            mirrorEditedPartyMember();
                        }
                        else
                        {
                            postStatus("Invalid date -- use YYYYMMDD.", 200);
                        }
                    }
                }
                else if (fieldIndex == 31)
                { // Fateful Encounter (toggle)
                    pokemon->setFatefulEncounter(!pokemon->isFatefulEncounter());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                }
                else if (fieldIndex == 17)
                { // Egg (toggle not-egg <-> egg)
                    pokemon->setEgg(!pokemon->isEgg());
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                }
                else if (fieldIndex == 18)
                { // Met Level (1-100 picker)
                    pickerKind = Dialogs::PickerKind::MetLevel;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = (pokemon->metLevel() > 0 ? pokemon->metLevel() : 1) - 1;
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 19)
                { // Ball (per-game list -- PLA uses the Hisui balls)
                    pickerKind = Dialogs::PickerKind::Ball;
                    pickerOrder.clear();
                    for (uint8_t b : Enums::getBallList(pokemon->getGameGroup()))
                        pickerOrder.push_back(b);
                    pickerLegalCount = 0; // no green "legal" prefix for balls
                    pickerCount = static_cast<int>(pickerOrder.size());
                    pickerSel = 0; // land on the pokemon's current ball if it's in the list
                    for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size());
                         ++pickerOrderIndex)
                        if (pickerOrder[pickerOrderIndex] == pokemon->ball())
                        {
                            pickerSel = pickerOrderIndex;
                            break;
                        }
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 20)
                { // Language
                    pickerKind = Dialogs::PickerKind::Language;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->language();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 21)
                { // Origin game
                    pickerKind = Dialogs::PickerKind::Origin;
                    pickerCount = Dialogs::pickerOptionCount(pickerKind);
                    pickerSel = pokemon->originGame();
                    pickerActive = true;
                    clearPickerSearch();
                }
                else if (fieldIndex == 22 && pokemon->hasPokerus())
                { // Pokerus: cycle None -> Infected -> Cured
                    uint8_t next;
                    // Infected -> Cured (strain 1, 0 days)
                    if (pokemon->isPokerusInfected()) next = 0x10;
                    // Cured -> None
                    else if (pokemon->isPokerusCured()) next = 0x00;
                    else
                        next = 0x12; // None -> Infected (strain 1, 2 days)
                    pokemon->setPokerus(next);
                    hasUnsavedChanges = true;
                    mirrorEditedPartyMember();
                }
                else if (fieldIndex == 25)
                { // Met Location (picker of the origin game's places)
                    const uint8_t originVersion = pokemon->originGame();
                    Names::LocationTable locationTable = Names::getLocationTable(originVersion);
                    pickerOrder.clear();
                    for (uint16_t pickedItemId = 0; pickedItemId < locationTable.count; ++pickedItemId)
                        // real, non-blank places only
                        if (locationTable.names[pickedItemId][0] != '\0') pickerOrder.push_back(pickedItemId);
                    if (!pickerOrder.empty())
                    {
                        pickerMetVersion = originVersion;
                        pickerMetIsEgg = false; // this picker targets the met location
                        pickerKind = Dialogs::PickerKind::MetLocation;
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0; // land on the current location if it's in the list
                        for (int pickerOrderIndex = 0; pickerOrderIndex < static_cast<int>(pickerOrder.size());
                             ++pickerOrderIndex)
                            if (pickerOrder[pickerOrderIndex] == pokemon->metLocation())
                            {
                                pickerSel = pickerOrderIndex;
                                break;
                            }
                        pickerActive = true;
                        clearPickerSearch();
                    }
                    else
                    {
                        postStatus("No met-location list for this game.", 200);
                    }
                }
                else if (fieldIndex >= 15)
                {
                    // A left-column detail row with no editor (e.g. Pokerus on a game without the
                    // mechanic) -- do nothing rather than fall through to the stat editor below.
                }
                else
                {
                    const int statIndex = fieldIndex; // 0-5 (HP, Atk, Def, SpA, SpD, Spe)
                    statEdit.selectedStat = statIndex;
                    switch (statIndex)
                    {
                    // evWide(), not evXXX(): Gen 1/2 Stat Experience is 16 bits and the 8-bit
                    // accessors saturate at 255, so loading through them would show 255 for any
                    // Stat Exp above it and then WRITE that back on confirm -- an edit to one
                    // stat would silently wipe the other five down to 255.
                    case 0:
                        statEdit.originalIV = pokemon->ivHP();
                        statEdit.originalEV = pokemon->evWide(0);
                        statEdit.originalAV = pokemon->avHP();
                        break;
                    case 1:
                        statEdit.originalIV = pokemon->ivATK();
                        statEdit.originalEV = pokemon->evWide(1);
                        statEdit.originalAV = pokemon->avATK();
                        break;
                    case 2:
                        statEdit.originalIV = pokemon->ivDEF();
                        statEdit.originalEV = pokemon->evWide(2);
                        statEdit.originalAV = pokemon->avDEF();
                        break;
                    case 3:
                        statEdit.originalIV = pokemon->ivSPA();
                        statEdit.originalEV = pokemon->evWide(4);
                        statEdit.originalAV = pokemon->avSPA();
                        break;
                    case 4:
                        statEdit.originalIV = pokemon->ivSPD();
                        statEdit.originalEV = pokemon->evWide(5);
                        statEdit.originalAV = pokemon->avSPD();
                        break;
                    case 5:
                        statEdit.originalIV = pokemon->ivSPE();
                        statEdit.originalEV = pokemon->evWide(3);
                        statEdit.originalAV = pokemon->avSPE();
                        break;
                    }
                    statEdit.currentIV = statEdit.originalIV;
                    statEdit.currentEV = statEdit.originalEV;
                    statEdit.currentAV = statEdit.originalAV;
                    statEdit.mode = Dialogs::StatEditMode::IV;
                    statEdit.value = statEdit.currentIV;
                    statEdit.dialogActive = true;
                }
            }

            return; // Don't process other inputs while the page is active
        }

        if (itemEditDialogActive)
        {
            // Touch: map the step buttons to the SAME presses as the stat editor
            // (ZL/L/Left/Right/R/ZR = -/+100, -/+10, -/+1), Confirm -> A, Cancel -> B, then reuse the
            // button logic below.
            switch (touchedButtonId(touch))
            {
            case 34:
                buttonsDown |= HidNpadButton_ZL;
                break;
            case 30:
                buttonsDown |= HidNpadButton_L;
                break;
            case 31:
                buttonsDown |= HidNpadButton_Left;
                break;
            case 32:
                buttonsDown |= HidNpadButton_Right;
                break;
            case 33:
                buttonsDown |= HidNpadButton_R;
                break;
            case 35:
                buttonsDown |= HidNpadButton_ZR;
                break;
            // Confirm, Cancel and Remove CLOSE the dialog, so they are deferred a frame to be seen
            // held; the step buttons above and the type drop-down are not, since each shows its own
            // result immediately.
            case 1:
                if (armTap(1, HidNpadButton_A))
                    return;
                break;
            case 0:
                if (armTap(0, HidNpadButton_B))
                    return;
                break;
            case 40:
                buttonsDown |= HidNpadButton_X;
                break; // item drop-down -> change type
            case 2:
                if (armTap(2, HidNpadButton_Y))
                    return;
                break; // Remove
            default:
                break;
            }
            // Adjust value, bounded by what the GAME accepts for this pouch. Over-cap counts are
            // a save-corruption vector, not cosmetic: an oversized Gen 3 stack overflows the bag into
            // the key-items pocket. Left/Right = -/+1, L/R = -/+10, ZL/ZR = -/+100 -- matching the stat
            // editor (these used to be Up/Down, inconsistent between the two dialogs).
            const int itemMax = currentItemMaxCount();
            if (buttonsDown & HidNpadButton_Left)
                itemEditDialogValue = std::max(0, itemEditDialogValue - 1);
            if (buttonsDown & HidNpadButton_Right)
                itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 1);
            if (buttonsDown & HidNpadButton_L)
                itemEditDialogValue = std::max(0, itemEditDialogValue - 10);
            if (buttonsDown & HidNpadButton_R)
                itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 10);
            if (buttonsDown & HidNpadButton_ZL)
                itemEditDialogValue = std::max(0, itemEditDialogValue - 100);
            if (buttonsDown & HidNpadButton_ZR)
                itemEditDialogValue = std::min(itemMax, itemEditDialogValue + 100);
            // A save edited elsewhere (or by an older PKSE) can hold an over-cap count; clamp on
            // entry so opening the dialog can only ever move it back into range, never past it.
            itemEditDialogValue = std::clamp(itemEditDialogValue, 0, itemMax);

            // X: change this item's TYPE. Open the pouch picker filtered to legal ids the bag doesn't
            // already hold; the pick reassigns this slot (see the picker's replace-confirm handler).
            if (buttonsDown & HidNpadButton_X)
            {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    const auto &pouch = trainer.items[selectedCategory];
                    const auto legal = Names::getPouchItems(trainer.getGameGroup(), selectedCategory);
                    pickerOrder.clear();
                    for (uint16_t pickedItemId : legal)
                    {
                        const auto existingEntry = std::find_if(pouch.begin(), pouch.end(),
                                                    [pickedItemId](const Trainer::InventoryItem &newValue)
                                                    { return newValue.itemId == pickedItemId; });
                        if (existingEntry == pouch.end() || existingEntry->count == 0)
                            pickerOrder.push_back(pickedItemId); // not currently held
                    }
                    if (!pickerOrder.empty())
                    {
                        itemPickerReplace = true;
                        pickerKind = pouchPickerKind();
                        pickerCount = static_cast<int>(pickerOrder.size());
                        pickerSel = 0;
                        pickerActive = true;
                        clearPickerSearch();
                        itemEditDialogActive = false; // picker takes over; the change applies on pick
                    }
                    else
                    {
                        postStatus("No other item types available in this pocket.", 240);
                    }
                }
                return;
            }

            // Y: remove this item from the pouch.
            if (buttonsDown & HidNpadButton_Y)
            {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    auto &pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size()))
                    {
                        const int rawIdx = visible[selectedItemIndex];
                        if (g_debugLogging)
                        {
                            Utils::logEventToFile(
                                std::string("ITEM action=REMOVE ") +
                                Utils::logField("pouch",
                                                Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
                                " " +
                                Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup())) +
                                " id=" + std::to_string(pouch[rawIdx].itemId) +
                                " count=" + std::to_string(pouch[rawIdx].count) +
                                " mode=" + (trainer.itemsAreIdIndexed() ? "zeroed" : "erased") + " via=dialog");
                        }
                        if (trainer.itemsAreIdIndexed())
                        {
                            pouch[rawIdx].count = 0; // keep the entry so its id-slot is written to 0
                        }
                        else
                        {
                            pouch.erase(pouch.begin() + rawIdx); // slot-based: erase; region rewritten
                        }
                        hasUnsavedChanges = true;
                        const int visibleIndices = static_cast<int>(visibleItemIndices().size());
                        if (selectedItemIndex >= visibleIndices)
                            selectedItemIndex = std::max(0, visibleIndices - 1);
                        postStatus("Item removed.", 200);
                    }
                }
                itemEditDialogActive = false;
                return;
            }

            // Confirm edit
            if (buttonsDown & HidNpadButton_A)
            {
                // Save the new value to the item (map the visible selection to the raw pouch slot).
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    auto &pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size()))
                    {
                        const int rawIdx = visible[selectedItemIndex];
                        // Confirming a count of 0 IS a removal. On a slot-based game erase the entry, or
                        // updateItemBlock would write a {itemId, 0} ghost slot; on an id-indexed game
                        // keep the entry at count 0 so its id-slot is written to 0.
                        if (g_debugLogging && itemEditDialogValue != itemEditDialogOriginalValue)
                        {
                            const bool removing = (itemEditDialogValue == 0);
                            Utils::logEventToFile(
                                std::string("ITEM action=") + (removing ? "REMOVE" : "SETCOUNT") + " " +
                                Utils::logField("pouch",
                                                Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
                                " " +
                                Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup())) +
                                " id=" + std::to_string(pouch[rawIdx].itemId) +
                                " from=" + std::to_string(itemEditDialogOriginalValue) +
                                " to=" + std::to_string(itemEditDialogValue) +
                                (removing ? (trainer.itemsAreIdIndexed() ? " mode=zeroed" : " mode=erased") : ""));
                        }
                        if (itemEditDialogValue == 0 && !trainer.itemsAreIdIndexed())
                        {
                            pouch.erase(pouch.begin() + rawIdx);
                        }
                        else
                        {
                            pouch[rawIdx].count = static_cast<uint16_t>(itemEditDialogValue);
                        }
                        if (itemEditDialogValue != itemEditDialogOriginalValue)
                            hasUnsavedChanges = true;
                    }
                }
                itemEditDialogActive = false;
                return;
            }

            // Cancel edit
            if (buttonsDown & HidNpadButton_B)
            {
                itemEditDialogActive = false;
                return;
            }

            return; // Don't process other inputs while edit dialog is active
        }

        // Handle storage release confirmation (single slot or the whole multi-selection).
        if (releaseConfirmActive)
        {
            int touchedButton = touchedButtonId(touch);
            // tap Release -- drawn held this frame, acted on the next
            if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A)) return;
            // tap Cancel
            if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B)) return;
            if (buttonsDown & HidNpadButton_A)
            {
                if (releaseGroup)
                {
                    if (g_debugLogging)
                    {
                        for (const auto &carriedPokemon : moveMon)
                        {
                            if (carriedPokemon)
                                Utils::logEventToFile("RELEASE source=hand " +
                                                      Utils::describePokemon(*carriedPokemon, trainer));
                        }
                    }
                    // The group being released is the block in hand, so releasing it is simply
                    // dropping what we are carrying -- nothing is left pointing at a stale slot.
                    moveMon.clear();
                    selectDimensions = {0, 0};
                }
                else
                {
                    auto &slot = storageSlot(releasePane, releaseBox, releaseSlot);
                    if (g_debugLogging && slot)
                    {
                        Utils::logEventToFile("RELEASE " +
                                              Utils::logSlot("from", releasePane, releaseBox, releaseSlot) + " " +
                                              Utils::describePokemon(*slot, trainer));
                    }
                    slot.reset();
                }
                hasUnsavedChanges = true;
                releaseConfirmActive = false;
                return;
            }
            if (buttonsDown & HidNpadButton_B)
            {
                releaseConfirmActive = false;
                return;
            }
            return;
        }

        // Item removal confirm: X in the Items list asks before deleting. A = Remove, B = Cancel.
        // Mirrors the storage release confirm; the delete matches the Edit Item dialog's Y-remove.
        if (itemRemoveConfirmActive)
        {
            int touchedButton = touchedButtonId(touch);
            // tap Remove -- drawn held this frame, acted on the next
            if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A)) return;
            // tap Cancel
            if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B)) return;
            if (buttonsDown & HidNpadButton_A)
            {
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    auto &pouch = trainer.items[selectedCategory];
                    std::vector<int> visible = visibleItemIndices();
                    if (selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(visible.size()))
                    {
                        const int rawIdx = visible[selectedItemIndex];
                        if (g_debugLogging)
                        {
                            Utils::logEventToFile(
                                std::string("ITEM action=REMOVE ") +
                                Utils::logField("pouch",
                                                Panels::pouchDisplayName(trainer.getGameGroup(), selectedCategory)) +
                                " " +
                                Utils::logField("item", Utils::itemName(pouch[rawIdx].itemId, trainer.getGameGroup())) +
                                " id=" + std::to_string(pouch[rawIdx].itemId) +
                                " count=" + std::to_string(pouch[rawIdx].count) +
                                " mode=" + (trainer.itemsAreIdIndexed() ? "zeroed" : "erased"));
                        }
                        // keep entry; id-slot written to 0
                        if (trainer.itemsAreIdIndexed()) pouch[rawIdx].count = 0;
                        else
                            pouch.erase(pouch.begin() + rawIdx); // slot-based: erase; region rewritten
                        hasUnsavedChanges = true;
                        const int visibleIndices = static_cast<int>(visibleItemIndices().size());
                        if (selectedItemIndex >= visibleIndices)
                            selectedItemIndex = std::max(0, visibleIndices - 1);
                        postStatus("Item removed.", 200);
                    }
                }
                itemRemoveConfirmActive = false;
                return;
            }
            if (buttonsDown & HidNpadButton_B)
            {
                itemRemoveConfirmActive = false;
                return;
            }
            return;
        }

        // Lossy-move acknowledgement. The two warnings differ only in what they SAY -- both guard the
        // same pending action and answer the same question -- so the input handling is shared here
        // deliberately, while the notices themselves are separate dialogs. Continue runs the stashed
        // action; Cancel leaves everything as it was.
        if (moveConfirmActive())
        {
            int touchedButton = touchedButtonId(touch);
            // tap Continue -- drawn held this frame, acted on the next
            if (touchedButton == 1 && armTap(touchedButton, HidNpadButton_A)) return;
            // tap Cancel
            if (touchedButton == 0 && armTap(touchedButton, HidNpadButton_B)) return;
            if (buttonsDown & (HidNpadButton_A | HidNpadButton_B))
            {
                const bool activatePressed = (buttonsDown & HidNpadButton_A) != 0; // A always continues
                gen3ConvertConfirmActive = false;
                virtualConsoleTransferConfirmActive = false;
                lgpeTransferConfirmActive = false;
                if (activatePressed && pendingMove == PendingMove::PlaceHeld)
                {
                    // Re-point the cursor at the slot the drop was aimed at, then run the ordinary
                    // put-down -- one placement path, so the confirmed move behaves identically to
                    // the unconfirmed one (bounds, block swap, per-cell conversion and all).
                    storageFocusPane = pendingMovePane;
                    if (pendingMovePane == 0)
                    {
                        selectedBoxIndex = pendingMoveBox;
                        stSaveSlot = pendingMoveSlot;
                    }
                    else
                    {
                        stBankBox = pendingMoveBox;
                        stBankSlot = pendingMoveSlot;
                    }
                    putDownBlock();
                }
                pendingMove = PendingMove::None;
            }
            return;
        }

        // Handle the per-Pokemon action menu (Move / Edit / Clone / Export / Release / Find / Cancel).
        if (storageMenuActive)
        {
            constexpr int optionCount = 7;
            int touchedButton = touchedButtonId(touch);
            if (touchedButton >= 0)
            {
                storageMenuIndex = touchedButton;        // draws highlighted this frame
                // ...and runs on the next
                if (armTap(touchedButton, HidNpadButton_A)) return;
            }
            // On a party-linked (locked) slot, only Move (0) and Release (4) are disabled.
            // Export is read-only and remains available even for a locked slot.
            const bool menuLocked = storageSlotLocked(menuPane, menuBox, menuSlot);
            auto menuDisabled = [&](int menuIndex)
            { return menuLocked && (menuIndex == 0 || menuIndex == 4); };
            // Export (3), Find (5) and Cancel (6) do not disturb slot ownership.
            if (buttonsDown & HidNpadButton_Up)
                do
                {
                    storageMenuIndex = (storageMenuIndex - 1 + optionCount) % optionCount;
                } while (menuDisabled(storageMenuIndex));
            if (buttonsDown & HidNpadButton_Down)
                do
                {
                    storageMenuIndex = (storageMenuIndex + 1) % optionCount;
                } while (menuDisabled(storageMenuIndex));
            if (buttonsDown & HidNpadButton_B)
            {
                storageMenuActive = false;
                return;
            }
            if (buttonsDown & HidNpadButton_A)
            {
                storageMenuActive = false;
                switch (storageMenuIndex)
                {
                case 0: // Move -> pick up as a 1x1 block (blocked on a party-linked slot)
                    if (menuLocked)
                        break;
                    storageFocusPane = menuPane;
                    if (menuPane == 0)
                    {
                        selectedBoxIndex = menuBox;
                        stSaveSlot = menuSlot;
                    }
                    else
                    {
                        stBankBox = menuBox;
                        stBankSlot = menuSlot;
                    }
                    pickupSingle();
                    postPickup();
                    break;
                case 1: // Edit -> open the details modal
                    openStorageEditor(menuPane, menuBox, menuSlot);
                    break;
                case 2: // Clone -> duplicate into the first empty slot (this box first, then ANY box)
                    if (const Pokemon::Pokemon *source = storageSlot(menuPane, menuBox, menuSlot).get())
                    {
                        if (auto copy = source->clone())
                        {
                            const int slots = menuPane == 0 ? static_cast<int>(trainer.getSlotsPerBox())
                                                            : static_cast<int>(Trainer::Bank::BANK_SLOTS_PER_BOX);
                            const int boxes = menuPane == 0 ? static_cast<int>(trainer.getBoxCount())
                                                            : static_cast<int>(Trainer::Bank::BANK_BOX_COUNT);
                            // Search the pokemon's own box first, then spill into any other box. A full
                            // box must NOT eat the clone -- on Let's Go's gapless list every box but
                            // the last is full, so a single-box search silently dropped the copy (a
                            // party pokemon in box 0 could never be cloned).
                            bool placed = false;
                            for (int boxIndex = 0; boxIndex < boxes && !placed; ++boxIndex)
                            {
                                const int box = (menuBox + boxIndex) % boxes;
                                for (int slotIndex = 0; slotIndex < slots && !placed; ++slotIndex)
                                {
                                    auto &destination = storageSlot(menuPane, box, slotIndex);
                                    if ((!destination || destination->speciesID() == 0) &&
                                        !storageSlotLocked(menuPane, box, slotIndex))
                                    {
                                        destination = std::move(copy);
                                        hasUnsavedChanges = true;
                                        placed = true;
                                        // A clone puts a SECOND record with the same PID/EC into
                                        // the save, which is exactly what a later "duplicate
                                        // Pokemon" or failed-legality report is about -- so log
                                        // where it came from and where it landed.
                                        if (g_debugLogging)
                                        {
                                            Utils::logEventToFile("CLONE result=OK " +
                                                                  Utils::logSlot("from", menuPane, menuBox, menuSlot) +
                                                                  " " + Utils::logSlot("to", menuPane, box, slotIndex) +
                                                                  " " + Utils::describePokemon(*destination, trainer));
                                        }
                                    }
                                }
                            }
                            if (!placed)
                            {
                                postStatus("No free slot to clone into.", 240);
                                if (g_debugLogging)
                                {
                                    Utils::logEventToFile("CLONE result=REFUSED reason=no-free-slot " +
                                                          Utils::logSlot("from", menuPane, menuBox, menuSlot) + " " +
                                                          Utils::briefPokemon(*source));
                                }
                            }
                        }
                    }
                    break;
                case 3: // Export -> verified native file; source stays untouched
                    if (const Pokemon::Pokemon *source = storageSlot(menuPane, menuBox, menuSlot).get())
                    {
                        std::string error;
                        const std::string path = Trainer::PokemonFile::defaultExportPath(*source);
                        if (!path.empty() && Trainer::PokemonFile::write(*source, path, &error))
                        {
                            postStatus("Exported " + leafName(path) + " to sdmc:/PKSE/exports.", 360);
                            Utils::logEventToFile("POKEMONFILE action=EXPORT result=OK file=\"" +
                                                  leafName(path) + "\" " + Utils::briefPokemon(*source));
                        }
                        else
                        {
                            const std::string reason = error.empty() ? "could not create export path" : error;
                            postStatus("Export failed: " + reason, 480);
                            Utils::logEventToFile("POKEMONFILE action=EXPORT result=FAILED reason=\"" + reason +
                                                  "\" " + Utils::briefPokemon(*source));
                        }
                    }
                    break;
                case 4: // Release -> confirm (blocked on a party-linked slot)
                    if (menuLocked)
                        break;
                    releaseGroup = false;
                    releasePane = menuPane;
                    releaseBox = menuBox;
                    releaseSlot = menuSlot;
                    releaseConfirmActive = true;
                    break;
                case 5: // Find -> highlight matching Pokemon and jump to the first
                    // The grids highlight straight from the query, so matches light up while it is
                    // typed. The cursor jumps only once it is accepted: jumping per key would move
                    // the bank's current box, which is an unsaved bank change.
                    if (storageSearch.promptForQuery("Box and Bank", ListSearch::FilterTiming::WhileTyping))
                    {
                        if (storageSearch.isFiltering() && !jumpToFirstStorageMatch())
                            postStatus("No Pokemon here match that search.", 240);
                    }
                    break;
                default:
                    break; // Cancel
                }
                return;
            }
            return;
        }

        // Handle the carried-block menu, opened with Minus (Release all / Return / Cancel). Moving a
        // block no longer needs a menu entry -- you carry it to where you want it and press A.
        if (groupMenuActive)
        {
            constexpr int optionCount = 3;
            int touchedButton = touchedButtonId(touch);
            if (touchedButton >= 0)
            {
                groupMenuIndex = touchedButton;          // draws highlighted this frame
                // ...and runs on the next
                if (armTap(touchedButton, HidNpadButton_A)) return;
            }
            if (buttonsDown & HidNpadButton_Up)
                groupMenuIndex = (groupMenuIndex - 1 + optionCount) % optionCount;
            if (buttonsDown & HidNpadButton_Down)
                groupMenuIndex = (groupMenuIndex + 1) % optionCount;
            if (buttonsDown & HidNpadButton_B)
            {
                groupMenuActive = false;
                return;
            }
            if (buttonsDown & HidNpadButton_A)
            {
                groupMenuActive = false;
                switch (groupMenuIndex)
                {
                case 0:
                    releaseGroup = true;
                    releaseConfirmActive = true;
                    break; // Release all
                case 1:
                    returnHeldToOrigin();
                    break; // Put it all back
                default:
                    break; // Cancel
                }
                return;
            }
            return;
        }

        // Handle the storage-exit Save / Discard / Cancel prompt (bank has unsaved changes).
        if (storageExitConfirmActive)
        {
            constexpr int optionCount = 3;
            int touchedButton = touchedButtonId(touch);
            if (touchedButton >= 0)
            {
                storageExitConfirmIndex = touchedButton;
                buttonsDown |= HidNpadButton_A;
            } // tap a row = select + confirm
            if (buttonsDown & HidNpadButton_Up)
                storageExitConfirmIndex = (storageExitConfirmIndex - 1 + optionCount) % optionCount;
            if (buttonsDown & HidNpadButton_Down)
                storageExitConfirmIndex = (storageExitConfirmIndex + 1) % optionCount;
            // Cancel: stay, and call off any app exit that raised this (they backed out of leaving too).
            if (buttonsDown & HidNpadButton_B)
            {
                storageExitConfirmActive = false;
                exitAfterBankChoice = false;
                return;
            }
            if (buttonsDown & HidNpadButton_A)
            {
                storageExitConfirmActive = false;
                // Claimed up front, so a branch that bails out (a failed bank write) can't leave a
                // stale "and then exit" hanging over the next visit to this prompt.
                const bool resumeExit = exitAfterBankChoice;
                exitAfterBankChoice = false;
                bool answered = false; // Save or Discard actually went through (Cancel did not)
                switch (storageExitConfirmIndex)
                {
                case 0: // Save & Exit
                    // Same rule as the app-exit path: a failed bank write must not be followed by
                    // leaving the view, or the deposits vanish with no indication.
                    if (bank && !bank->save())
                    {
                        postStatus("Couldn't save the bank - staying in storage so nothing is lost.", 480);
                        Utils::logEventToFile("BANK action=SAVE result=FAILED");
                        return;
                    }
                    // The open box went out with that write -- it is part of the bank's image (see
                    // Bank::currentBox), so it needs nothing here and is kept only because the user
                    // chose Save.
                    Utils::logTest("BANKSAVE result=OK verifyfail=" +
                                   std::to_string(bank ? bank->lastVerifyFailures() : 0));
                    Utils::logEventToFile("BANK action=SAVE result=OK verifyfail=" +
                                          std::to_string(bank ? bank->lastVerifyFailures() : 0));
                    // Written, but the round-trip check found slots that don't reproduce. That
                    // is a PKSE bug rather than a write failure, so it warns instead of blocking.
                    if (bank && bank->lastVerifyFailures() > 0)
                    {
                        postStatus(std::to_string(bank->lastVerifyFailures()) +
                                       " bank slot(s) failed the integrity check - see the PKSE log.",
                                   480);
                    }
                    detailViewActive = false;
                    answered = true;
                    break;
                case 1: // Discard & Exit -> revert the in-memory bank to its on-disk state.
                        // ONLY the bank: it owns what lives in the bank, not what lives in the
                        // save. Pulling a Pokemon out and then discarding therefore leaves the
                        // copy in the save box -- intended, and how a HOME-style box works. Undoing that half
                        // is the GAME save's own discard, which is a separate decision.
                    Utils::logEventToFile("BANK action=DISCARD result=OK scope=bank-only"
                                          " note=withdrawn-copies-remain-in-save");
                    if (bank)
                    {
                        bank->load();
                        // load() put the open box back to the last SAVED one; the pane's cursor has
                        // to follow it, or the next visit reopens the box the user just discarded
                        // and the bank would read as changed again the moment it was drawn.
                        if (bank->currentBox < Trainer::Bank::BANK_BOX_COUNT)
                        {
                            stBankBox = static_cast<int>(bank->currentBox);
                            stBankSlot = 0;
                        }
                    }
                    detailViewActive = false;
                    answered = true;
                    break;
                default:
                    break; // Cancel: stay in the storage view
                }
                // Raised by + rather than by B: the bank has had its answer, so carry on out of the
                // app. Cancel leaves `answered` false and simply stays.
                if (resumeExit && answered)
                    beginAppExit();
                return;
            }
            return;
        }

        if (detailViewActive)
        {
            if (buttonsDown & HidNpadButton_B)
            {
                if (selectedMode == ViewMode::Storage)
                {
                    // B unwinds one step: abandon a rectangle being drawn, then put a carried block
                    // back where it came from, then leave the view.
                    if (currentlySelecting)
                    {
                        cancelSelection();
                        return;
                    }
                    if (carrying())
                    {
                        returnHeldToOrigin();
                        return;
                    }
                    // Leaving the storage view: if the bank has unsaved changes, prompt Save / Discard /
                    // Cancel. No changes -> just exit.
                    // The bank is its own entity, saved here rather than with the game (X) save.
                    if (bank && bank->hasChanged())
                    {
                        storageExitConfirmActive = true;
                        storageExitConfirmIndex = 0;
                        return;
                    }
                    detailViewActive = false;
                    return;
                }
                if (swapActive)
                {
                    swapActive = false; // cancel an in-progress grab rather than leaving the grid
                    return;
                }
                // Exit detail view (B button only)
                detailViewActive = false;
                selectedItemIndex = 0;
                currentPage = 0;
                return;
            }

            // Storage view: dual-pane bank navigation + deposit/withdraw.
            if (selectedMode == ViewMode::Storage)
            {
                // Touch drag-select (Multi mode): press anchors the rectangle, sliding a finger over
                // the grid rubber-bands it, lifting grabs the block. The same gesture the D-pad does
                // with A / move / A -- so a rectangle can be swept out in one motion.
                //
                // Only the anchor's own pane and box take part: a rectangle is a region of ONE box,
                // and the cursor slot is what the highlight and the grab both read.
                auto slotUnderFinger = [&](int &outPane, int &outSlot)
                {
                    for (const auto &t : storageTouchTargets)
                    {
                        // header arrows / name pill
                        if (t.slot < 0) continue;
                        if (touch.x() >= t.hitX && touch.x() < t.hitX + t.hitWidth &&
                            touch.y() >= t.hitY && touch.y() < t.hitY + t.hitHeight)
                        {
                            outPane = t.pane;
                            outSlot = t.slot;
                            return true;
                        }
                    }
                    return false;
                };
                if (currentlySelecting && touch.isDown() && !touch.justPressed())
                {
                    int touchedPane = 0, touchedSlot = 0;
                    if (slotUnderFinger(touchedPane, touchedSlot) && touchedPane == selectPane)
                    {
                        if (touchedPane == 0)
                            stSaveSlot = touchedSlot;
                        else
                            stBankSlot = touchedSlot;
                    }
                }
                if (currentlySelecting && touch.justReleased() && touch.dragged())
                {
                    grabSelection(true); // a swept-out rectangle grabs on lift
                    return;
                }

                // A tap on a slot moves the cursor there and acts like pressing A, so it works in
                // every cursor mode (Menu opens the popup, Move picks up/places, Multi anchors then
                // grabs). Rects were captured during the previous frame's draw.
                if (touch.justPressed())
                {
                    for (const auto &t : storageTouchTargets)
                    {
                        if (touch.x() >= t.hitX && touch.x() < t.hitX + t.hitWidth &&
                            touch.y() >= t.hitY && touch.y() < t.hitY + t.hitHeight)
                        {
                            storageFocusPane = t.pane;
                            // ◀ previous box
                            if (t.slot == -2) buttonsDown |= HidNpadButton_L;
                            // ▶ next box
                            else if (t.slot == -3) buttonsDown |= HidNpadButton_R;
                            else if (t.slot == -4)
                            { // tapped the name pill -> rename
                                if (t.pane == 1 || trainer.supportsBoxNames())
                                {
                                    if (t.pane == 0)
                                        stSaveSlot = -1;
                                    else
                                        stBankSlot = -1;        // focus header now
                                    pendingHeaderRename = true; // open the rename next frame
                                }
                            }
                            else
                            {
                                if (t.pane == 0)
                                    stSaveSlot = t.slot;
                                else
                                    stBankSlot = t.slot;
                                buttonsDown |= HidNpadButton_A;
                            }
                            break;
                        }
                    }
                }
                handleStorageInput(buttonsDown);
                return;
            }

            if (selectedMode == ViewMode::Items)
            {
                // Search the pouch. visibleItemIndices() applies the query, so the
                // cursor, the tiles and every edit path all narrow together. It RETURNS, so nothing
                // below may also listen for Y -- Add Item did, and was unreachable until it moved.
                if (buttonsDown & HidNpadButton_Y)
                {
                    const int itemIndexBeforeSearch = selectedItemIndex;
                    const int pageBeforeSearch = currentPage;
                    const auto resetCursor = [this]
                    {
                        selectedItemIndex = 0;
                        currentPage = 0;
                    };
                    // Typing resets the cursor as it goes; a query that ends where it began puts it back.
                    if (!itemSearch.promptForQuery("Items", ListSearch::FilterTiming::WhileTyping, resetCursor))
                    {
                        selectedItemIndex = itemIndexBeforeSearch;
                        currentPage = pageBeforeSearch;
                    }
                    return;
                }
                if (selectedCategory >= 0 && selectedCategory < static_cast<int>(trainer.items.size()))
                {
                    const auto &pouch = trainer.items[selectedCategory];
                    // Navigate/edit only the visible items (count > 0); selectedItemIndex indexes
                    // this filtered view, mapped back to the raw pouch via visible[...] on edit.
                    std::vector<int> visible = visibleItemIndices();
                    int itemsPerPage =
                        (CONTENT_PANEL_HEIGHT - 106 - listSearchBoxHeight()) / 52; // rows that fit (mirror ItemsPanel)
                    if (itemsPerPage < 1)
                        itemsPerPage = 1;
                    int totalItems = static_cast<int>(visible.size());
                    int totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;
                    // Clamp BOTH the selection and the page to the current item set. Removing every
                    // item on the last page shrinks totalItems/totalPages; without re-clamping here the
                    // cursor is stranded on an out-of-range page ("6/5") until the pouch/tab changes or
                    // the view is re-entered (E8). Re-derive currentPage from the clamped selection so
                    // the two stay consistent no matter how the item left (list remove, dialog remove,
                    // or an edit to count 0).
                    if (totalItems > 0)
                    {
                        if (selectedItemIndex >= totalItems)
                            selectedItemIndex = totalItems - 1;
                        if (selectedItemIndex < 0)
                            selectedItemIndex = 0;
                        currentPage = selectedItemIndex / itemsPerPage;
                    }
                    else
                    {
                        selectedItemIndex = 0;
                        currentPage = 0;
                    }

                    // Touch: tap a row to select + edit it.
                    int itemTap = touchedButtonId(touch);
                    if (itemTap >= 0 && itemTap < totalItems)
                    {
                        selectedItemIndex = itemTap;
                        currentPage = selectedItemIndex / itemsPerPage;
                        buttonsDown |= HidNpadButton_A;
                    }

                    if ((buttonsDown & HidNpadButton_Up) && totalItems > 0)
                    {
                        selectedItemIndex = (selectedItemIndex - 1 + totalItems) % totalItems;
                        // Adjust page if needed
                        currentPage = selectedItemIndex / itemsPerPage;
                    }
                    if ((buttonsDown & HidNpadButton_Down) && totalItems > 0)
                    {
                        selectedItemIndex = (selectedItemIndex + 1) % totalItems;
                        // Adjust page if needed
                        currentPage = selectedItemIndex / itemsPerPage;
                    }

                    // Left/Right page through the single-column list.
                    if ((buttonsDown & HidNpadButton_Right) && totalPages > 1)
                    {
                        currentPage = (currentPage + 1) % totalPages;
                        selectedItemIndex = std::min(currentPage * itemsPerPage, totalItems - 1);
                    }
                    if ((buttonsDown & HidNpadButton_Left) && totalPages > 1)
                    {
                        currentPage = (currentPage - 1 + totalPages) % totalPages;
                        selectedItemIndex = std::min(currentPage * itemsPerPage, totalItems - 1);
                    }

                    // A button to edit item amount (map the visible selection back to the raw pouch slot)
                    if (buttonsDown & HidNpadButton_A)
                    {
                        if (selectedItemIndex >= 0 && selectedItemIndex < totalItems)
                        {
                            int rawIdx = visible[selectedItemIndex];
                            itemEditDialogActive = true;
                            itemEditDialogValue = pouch[rawIdx].count;
                            itemEditDialogOriginalValue = pouch[rawIdx].count;
                        }
                    }

                    // Minus opens the add-item picker: the ids that legally belong in THIS pouch,
                    // minus what the bag already holds. Nothing is offered for a pouch the game
                    // doesn't have, or when appending isn't supported and every legal id is present.
                    //
                    // NOT Y, which is what it was: Y opens search in every list that has one, and the
                    // search branch above returns first -- so while this sat on Y it never ran at all,
                    // with the nav bar still advertising it and no touch target to reach it any other
                    // way. Minus is what the trainer view already uses to bring something IN (the
                    // Storage view's PKSM import), and it is bound nowhere else on this screen.
                    if (buttonsDown & HidNpadButton_Minus)
                    {
                        const auto legal = Names::getPouchItems(trainer.getGameGroup(), selectedCategory);
                        const bool canAppend = static_cast<int>(pouch.size()) < currentPouchCapacity();
                        pickerOrder.clear();
                        bool blockedByCapacity = false; // a new item exists to add, but the pouch can't take it
                        for (uint16_t pickedItemId : legal)
                        {
                            const auto existingEntry = std::find_if(pouch.begin(), pouch.end(),
                                                        [pickedItemId](const Trainer::InventoryItem &x)
                                                        { return x.itemId == pickedItemId; });
                            if (existingEntry != pouch.end())
                            {
                                // present but empty -> settable
                                if (existingEntry->count == 0) pickerOrder.push_back(pickedItemId);
                            }
                            else if (canAppend)
                            {
                                pickerOrder.push_back(pickedItemId); // genuinely new -> needs a slot
                            }
                            else
                            {
                                blockedByCapacity = true;
                            }
                        }
                        if (!pickerOrder.empty())
                        {
                            pickerKind = pouchPickerKind();
                            pickerCount = static_cast<int>(pickerOrder.size());
                            pickerSel = 0;
                            pickerActive = true;
                            clearPickerSearch();
                        }
                        else if (blockedByCapacity)
                        {
                            // A slot-based pocket (FRLG / GG / SWSH / PLA) that's full -- say so rather
                            // than a silent no-op. The id-indexed games never hit this (entries preexist).
                            postStatus("This pocket is full.", 300);
                        }
                        else
                        {
                            postStatus("You already have every item this pocket can hold.", 300);
                        }
                        return;
                    }

                    // X asks to remove the selected item, behind a confirm dialog. The delete
                    // runs in the itemRemoveConfirmActive handler on A; here we just open the prompt.
                    if ((buttonsDown & HidNpadButton_X) && totalItems > 0 && selectedItemIndex >= 0 &&
                        selectedItemIndex < totalItems)
                    {
                        itemRemoveConfirmActive = true;
                        return;
                    }

                    // L/R to change categories (but not when in edit dialog)
                    if (buttonsDown & HidNpadButton_L)
                    {
                        switch (trainer.getGameGroup())
                        {
                        case GameVersion::ZA:
                        {
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_LZA) % POUCH_COUNT9_LZA;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::SV:
                        {
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_SV) % POUCH_COUNT9_SV;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::PLA:
                        {
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT8_LA) % POUCH_COUNT8_LA;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::BDSP:
                        {
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT8BDSP) % POUCH_COUNT8BDSP;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::SWSH:
                        {
                            selectedCategory =
                                (selectedCategory - 1 + static_cast<int>(Trainer::PouchType8SWSH::Count)) %
                                static_cast<int>(Trainer::PouchType8SWSH::Count);
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::GG:
                        {
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT7_LGPE) % POUCH_COUNT7_LGPE;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::FRLG:
                        case GameVersion::RSE:
                        {
                            static_assert(POUCH_COUNT3_FRLG == POUCH_COUNT3_RSE, "both Gen 3 groups have six pockets");
                            selectedCategory = (selectedCategory - 1 + POUCH_COUNT3_FRLG) % POUCH_COUNT3_FRLG;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                    if (buttonsDown & HidNpadButton_R)
                    {
                        switch (trainer.getGameGroup())
                        {
                        case GameVersion::ZA:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_LZA;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::SV:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_SV;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::PLA:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT8_LA;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::BDSP:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT8BDSP;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::SWSH:
                        {
                            selectedCategory =
                                (selectedCategory + 1) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::GG:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT7_LGPE;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        case GameVersion::FRLG:
                        case GameVersion::RSE:
                        {
                            selectedCategory = (selectedCategory + 1) % POUCH_COUNT3_FRLG;
                            currentPage = 0;
                            selectedItemIndex = 0;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }

            if (selectedMode == ViewMode::Boxes)
            {
                // Grid layout: five rows always, width from the box size -- 6x5=30 usually,
                // 5x5=25 in Let's Go, 4x5=20 for an international Gen 1 save.
                const int slotsPerBox = static_cast<int>(trainer.getSlotsPerBox());
                const int GRID_COLS = boxGridColumns(slotsPerBox);
                const int GRID_ROWS = BOX_GRID_ROWS;

                // selectedItemIndex == -1 is the "box-name header focused" state: navigate up to it
                // (or tap it) and press A to rename the box. Only where the game stores box names
                // (LGPE does not) and no swap is in progress; otherwise it keeps the top<->bottom wrap.
                const bool canFocusHeader = trainer.supportsBoxNames() && !swapActive;

                // Up/Down/Left/Right to navigate the grid (and to/from the header)
                if (buttonsDown & HidNpadButton_Up)
                {
                    if (selectedItemIndex == -1)
                    {
                        selectedItemIndex = (GRID_ROWS - 1) * GRID_COLS; // header -> bottom row
                    }
                    else
                    {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        int currentCol = selectedItemIndex % GRID_COLS;
                        if (currentRow > 0)
                            selectedItemIndex -= GRID_COLS;
                        // top row -> header
                        else if (canFocusHeader) selectedItemIndex = -1;
                        else
                            selectedItemIndex = (GRID_ROWS - 1) * GRID_COLS + currentCol;
                    }
                }
                if (buttonsDown & HidNpadButton_Down)
                {
                    if (selectedItemIndex == -1)
                    {
                        selectedItemIndex = 0; // header -> top-left
                    }
                    else
                    {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        int currentCol = selectedItemIndex % GRID_COLS;
                        if (currentRow < GRID_ROWS - 1)
                            selectedItemIndex += GRID_COLS;
                        // bottom row -> header
                        else if (canFocusHeader) selectedItemIndex = -1;
                        else
                            selectedItemIndex = currentCol;
                    }
                }
                if (buttonsDown & HidNpadButton_Left)
                {
                    if (selectedItemIndex == -1)
                    { // on the header: ◀ cycles box
                        int boxCount = static_cast<int>(trainer.getBoxCount());
                        selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                    }
                    else if (selectedItemIndex % GRID_COLS > 0)
                    {
                        selectedItemIndex--;
                    }
                    else
                    {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        selectedItemIndex = currentRow * GRID_COLS + (GRID_COLS - 1);
                    }
                }
                if (buttonsDown & HidNpadButton_Right)
                {
                    if (selectedItemIndex == -1)
                    { // on the header: ▶ cycles box
                        int boxCount = static_cast<int>(trainer.getBoxCount());
                        selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                    }
                    else if (selectedItemIndex % GRID_COLS < GRID_COLS - 1)
                    {
                        selectedItemIndex++;
                    }
                    else
                    {
                        int currentRow = selectedItemIndex / GRID_COLS;
                        selectedItemIndex = currentRow * GRID_COLS;
                    }
                }

                // L/R to change boxes
                if (buttonsDown & HidNpadButton_L)
                {
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                }
                if (buttonsDown & HidNpadButton_R)
                {
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                }

                // Touch: tap the ‹/› arrows to change box; tap a slot to select it, tap the
                // already-selected slot again to open its details (maps to A). Arrow/slot rects
                // are captured during the box draw (ids 1000/1001 for arrows, 0..slots-1 for cells).
                {
                    int tapped = touchedButtonId(touch);
                    int boxCount = static_cast<int>(trainer.getBoxCount());
                    if (tapped == 1000)
                    {
                        selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
                    }
                    else if (tapped == 1001)
                    {
                        selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
                    }
                    else if (tapped == 2000)
                    {
                        buttonsDown |= HidNpadButton_A; // tapped the summary side-panel -> edit selected pokemon
                    }
                    else if (tapped == 1002)
                    { // tapped the box-name pill -> focus header, rename next frame
                        if (trainer.supportsBoxNames() && !swapActive)
                        {
                            selectedItemIndex = -1; // highlight this frame; open the rename next frame
                            pendingHeaderRename = true;
                        }
                    }
                    else if (tapped >= 0 && tapped < static_cast<int>(trainer.getSlotsPerBox()))
                    {
                        // second tap -> details
                        if (selectedItemIndex == tapped) buttonsDown |= HidNpadButton_A;
                        else
                            selectedItemIndex = tapped;
                    }
                }

                // A button: box-name header -> rename; occupied slot -> details; empty slot -> create.
                if (buttonsDown & HidNpadButton_A)
                {
                    if (selectedItemIndex == -1)
                    {
                        renameBox(selectedBoxIndex);
                    }
                    else if (selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                             selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS))
                    {
                        const auto &pokemon = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        if (pokemon && pokemon->speciesID() != 0)
                        { // skip empty / species-0 ghost slots
                            details.active = true;
                            details.leftScroll = 0;
                            details.source = EditSource::Box;
                            details.category = 0;
                            details.selectedStat = 0;
                            details.selectedField = 0;
                            details.hexMode = 0;
                            details.editing = false;
                            snapshotEditTarget(); // dirty-check baseline for Save/Discard on close
                        }
                        else if (!storageSlotLocked(0, selectedBoxIndex, selectedItemIndex) && !swapActive)
                        {
                            // Empty slot -> create a new Pokemon here, the same flow as the Storage view:
                            // pick a species, edit it in the details modal, Keep/Discard on exit. The
                            // creator's species-pick handler drops it into boxes[box][slot] via
                            // storageSlot(0,...) and opens the editor with EditSource::Box.
                            creator.active = true;
                            creator.pane = 0;
                            creator.box = selectedBoxIndex;
                            creator.slot = selectedItemIndex;
                            pickerKind = Dialogs::PickerKind::Species;
                            buildCreatorSpeciesOrder(); // only species this game can hold (unless illegal-values on)
                            pickerCount = static_cast<int>(pickerOrder.size());
                            pickerSel = 0;
                            pickerActive = true;
                            clearPickerSearch();
                        }
                    }
                }

                // X button: release the pokemon under the cursor, after a confirm. Blocked on a party-linked
                // slot (LGPE) -- releasing it would orphan the party pointer, the same rule the Storage
                // release menu enforces (D8). Reuses the global release-confirm flow (pane 0 = save box).
                // Skipped mid-swap -- while holding a grabbed pokemon, B cancels and Y drops.
                if ((buttonsDown & HidNpadButton_X) && !swapActive)
                {
                    const bool inRange = selectedBoxIndex >= 0 &&
                                         selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                                         selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS);
                    if (inRange)
                    {
                        const auto &pk = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        if (pk && pk->speciesID() != 0)
                        {
                            if (storageSlotLocked(0, selectedBoxIndex, selectedItemIndex))
                            {
                                postStatus("That Pokémon is a party member — release it from the party first.", 240);
                            }
                            else
                            {
                                releaseGroup = false;
                                releasePane = 0;
                                releaseBox = selectedBoxIndex;
                                releaseSlot = selectedItemIndex;
                                releaseConfirmActive = true;
                            }
                        }
                    }
                }

                // Y button: grab the slot under the cursor, then Y on another occupied slot
                // to swap them (Phase 3.1). Same-slot Y cancels the grab.
                if (buttonsDown & HidNpadButton_Y)
                {
                    bool inRange = selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                                   selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS);
                    bool cursorOccupied = inRange && trainer.boxes[selectedBoxIndex][selectedItemIndex] != nullptr;

                    if (!swapActive)
                    {
                        if (cursorOccupied)
                        {
                            swapActive = true;
                            swapSourceBox = selectedBoxIndex;
                            swapSourceSlot = selectedItemIndex;
                        }
                    }
                    else if (swapSourceBox == selectedBoxIndex && swapSourceSlot == selectedItemIndex)
                    {
                        swapActive = false; // grabbed same slot again -> cancel
                    }
                    else
                    {
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

            if (selectedMode == ViewMode::Party)
            {
                constexpr int COLUMN_SIZE = 3;

                // Determine current column (0 = left, 1 = right)
                int currentColumn = (selectedPartyIndex >= COLUMN_SIZE) ? 1 : 0;
                int rowInColumn = selectedPartyIndex % COLUMN_SIZE;

                // Up/Down to navigate within current column
                if (buttonsDown & HidNpadButton_Up)
                {
                    rowInColumn = (rowInColumn - 1 + COLUMN_SIZE) % COLUMN_SIZE;
                    selectedPartyIndex = currentColumn * COLUMN_SIZE + rowInColumn;
                }
                if (buttonsDown & HidNpadButton_Down)
                {
                    rowInColumn = (rowInColumn + 1) % COLUMN_SIZE;
                    selectedPartyIndex = currentColumn * COLUMN_SIZE + rowInColumn;
                }

                // Left/Right to move between columns
                if (buttonsDown & HidNpadButton_Left)
                {
                    if (currentColumn == 1)
                    {
                        selectedPartyIndex = rowInColumn;
                    }
                }
                if (buttonsDown & HidNpadButton_Right)
                {
                    if (currentColumn == 0)
                    {
                        selectedPartyIndex = COLUMN_SIZE + rowInColumn;
                    }
                }

                // A button to view details
                if (buttonsDown & HidNpadButton_A)
                {
                    // Only open if there's a pokemon in the selected slot
                    if (selectedPartyIndex >= 0 && selectedPartyIndex < static_cast<int>(trainer.party.size()))
                    {
                        const Pokemon::Pokemon *pokemon = trainer.party[selectedPartyIndex].get();
                        if (pokemon && pokemon->speciesID() != 0)
                        { // Not empty
                            details.active = true;
                            details.leftScroll = 0;
                            details.source = EditSource::Party;
                            details.partyIndex = selectedPartyIndex;
                            details.category = 0;
                            details.selectedStat = 0;
                            details.selectedField = 0;
                            details.hexMode = 0;
                            details.editing = false;
                            snapshotEditTarget(); // dirty-check baseline for Save/Discard on close
                        }
                    }
                }
            }

            // Trainer info view: Name (0) / Money (1). Gender is read-only and lives with the
            // identity rows below, so it is not in this list at all. Both rows defer one frame so
            // the row highlight draws before the blocking swkbd opens.
            if (selectedMode == ViewMode::Trainer)
            {
                constexpr int editRowCount = 2;
                int touchedButton = touchedButtonId(touch);
                if (touchedButton >= 0 && touchedButton < editRowCount)
                {
                    trainerSelectedRow = touchedButton;
                    buttonsDown |= HidNpadButton_A;
                }
                if (buttonsDown & HidNpadButton_Up)
                    trainerSelectedRow = (trainerSelectedRow - 1 + editRowCount) % editRowCount;
                if (buttonsDown & HidNpadButton_Down)
                    trainerSelectedRow = (trainerSelectedRow + 1) % editRowCount;
                if (buttonsDown & HidNpadButton_A)
                {
                    pendingTrainerEdit = trainerSelectedRow; // Name (0) / Money (1): open swkbd next frame
                }
            }

            // Settings view: Up/Down select a row, A toggles it. The row numbers are the shared
            // SETTINGS_ROW_* constants at the top of this file, so this handler and
            // drawSettingsView's labels/values/badges cannot name different rows.
            if (selectedMode == ViewMode::Settings)
            {
                constexpr int settingsRowCount = SETTINGS_ROW_VISIBLE_COUNT;
                if (buttonsDown & HidNpadButton_Up)
                    settingsSelectedRow = (settingsSelectedRow - 1 + settingsRowCount) % settingsRowCount;
                if (buttonsDown & HidNpadButton_Down)
                    settingsSelectedRow = (settingsSelectedRow + 1) % settingsRowCount;
                int statIndex = touchedButtonId(touch);
                if (statIndex >= 0 && statIndex < settingsRowCount)
                {
                    settingsSelectedRow = statIndex;
                    buttonsDown |= HidNpadButton_A;
                }
                if (buttonsDown & HidNpadButton_A)
                {
                    if (settingsSelectedRow == SETTINGS_ROW_AUTO_BACKUP)
                        g_autoBackupEnabled = !g_autoBackupEnabled;
                    else if (settingsSelectedRow == SETTINGS_ROW_THEME)
                        applyTheme(g_themeMode == ThemeMode::Dark ? ThemeMode::Light : ThemeMode::Dark);
                    else if (settingsSelectedRow == SETTINGS_ROW_ALLOW_ILLEGAL)
                        g_allowIllegalEdits = !g_allowIllegalEdits;
                    else if (settingsSelectedRow == SETTINGS_ROW_AUTO_LEGALIZE)
                    {
                        // Rebuilding a pokemon the destination could never have produced REWRITES
                        // its met data and can drop moves, so it is opt-in: a user who would rather
                        // set those by hand gets an obvious placeholder instead. It never alters a
                        // level, and never touches a transfer the games themselves support.
                        g_autoLegalizeTransfers = !g_autoLegalizeTransfers;
                        postStatus(g_autoLegalizeTransfers
                                       ? "Transfers with no official route will be rebuilt as legal natives."
                                       : "Transfers will be left as-is for you to correct by hand.",
                                   300);
                    }
                    else if (settingsSelectedRow == SETTINGS_ROW_MOVE_WARNING)
                    {
                        // One switch for EVERY bank transfer warning, in every generation -- the
                        // Gen 3 down-convert and the Poke Transporter run out of Gen 1/2 as well as
                        // the Let's Go AV/EV reset. It used to gate the Let's Go one alone and the
                        // other two warned regardless, so a user who had turned warnings off was
                        // still stopped on every FireRed drop.
                        g_moveWarn = !g_moveWarn;
                        postStatus(g_moveWarn
                                       ? "Lossy bank transfers will ask before converting."
                                       : "Lossy bank transfers will convert without asking.",
                                   300);
                    }
                    else if (settingsSelectedRow == SETTINGS_ROW_INJECT_TO_GAME)
                    {
                        // The master lock for writing into the real game save. Turning it ON only
                        // makes the "Game save" destination available in the save dialog -- it never
                        // makes a save destructive on its own, so no confirmation is needed here.
                        g_injectToGameSave = !g_injectToGameSave;
                        postStatus(g_injectToGameSave
                                       ? "An older backup can now be written over your live game save."
                                       : "Backups can no longer be written over your live game save.",
                                   300);
                    }
                    else if (settingsSelectedRow == SETTINGS_ROW_DEBUG_LOGGING)
                    {
                        // Off by default, so a normal run leaves nothing on the card. The status
                        // names the path because the whole point of the toggle is handing that file
                        // to someone else: turn it on, reproduce the problem, send the log.
                        g_debugLogging = !g_debugLogging;
                        postStatus(g_debugLogging
                                       ? "Debug logging on. Logs are written to sdmc:/PKSE/logs."
                                       : "Debug logging off. No new log files will be written.",
                                   300);
                    }
                    else if (settingsSelectedRow == SETTINGS_ROW_LANGUAGE)
                    {
                        // UNREACHABLE while Names::DISPLAY_LANGUAGE_SELECTABLE is false: the row is
                        // not drawn, so settingsSelectedRow cannot reach it. Kept, rather than
                        // deleted, because it is the working feature and the constant is the only
                        // thing switched off -- flipping it true restores this screen exactly.
                        //
                        // Which language every NAME table is read in -- species, moves, items,
                        // abilities, natures, types, forms and locations. It does not touch stored
                        // text: a Pokemon's nickname and its trainer's name are bytes in the save
                        // and are shown as they were written, whatever this says.
                        //
                        // A plain cycle rather than a picker dialog: nine entries is shorter than
                        // several lists that already use A-to-cycle here, and it keeps the whole
                        // settings screen operable without opening anything.
                        const size_t nextLanguage =
                            (Names::displayLanguageIndex() + 1) % Names::LANGUAGE_COUNT;
                        Names::setDisplayLanguage(nextLanguage);
                        postStatus(std::string("Names now shown in ") +
                                       Names::nameLanguageLabel(nextLanguage) + ".",
                                   240);
                    }
                    Utils::saveSettings(); // persist the change to settings.cfg
                }
            }

            return; // Don't process other inputs while in detail view
        }

        // Normal mode navigation (not in detail view)
        if (buttonsDown & HidNpadButton_B)
        {
            if (hasUnsavedChanges && !saveConfirmActive)
            {
                // Prompt to save changes before going back
                exitingWithUnsavedChanges = true;
                exitingViaPlus = false; // Exiting via B button (go back)
                saveConfirmActive = true;
                return;
            }
            // No unsaved changes or already handled, go back immediately
            goBack = true;
        }

        // HOME main menu navigation. Pills (0 Pokemon, 1 Party, 2 Storage) are a vertical column;
        // the icons (3 Items, 4 Trainer, 5 Settings) are a horizontal row below. Up/Down move the
        // column and step into/out of the row; Left/Right move within the row (matching the layout).
        if (buttonsDown & HidNpadButton_Up)
        {
            // icon row -> Storage pill
            if (homeMenuIndex >= 3) homeMenuIndex = 2;
            // up the pill column
            else if (homeMenuIndex > 0) homeMenuIndex--;
        }
        if (buttonsDown & HidNpadButton_Down)
        {
            // down the pill column
            if (homeMenuIndex < 2) homeMenuIndex++;
            // Storage pill -> icon row
            else if (homeMenuIndex == 2) homeMenuIndex = 3;
        }
        if (homeMenuIndex >= 3)
        { // horizontal icon row
            if (buttonsDown & HidNpadButton_Left)
                homeMenuIndex = (homeMenuIndex == 3) ? 5 : homeMenuIndex - 1;
            if (buttonsDown & HidNpadButton_Right)
                homeMenuIndex = (homeMenuIndex == 5) ? 3 : homeMenuIndex + 1;
            buttonsDown &=
                ~(HidNpadButton_Left | HidNpadButton_Right); // consume L/R (don't also scroll the box preview)
        }
        switch (homeMenuIndex)
        {
        case 0:
            selectedMode = ViewMode::Boxes;
            break;
        case 1:
            selectedMode = ViewMode::Party;
            break;
        case 2:
            selectedMode = ViewMode::Storage;
            storageSearch.clear();
            break;
        case 3:
            selectedMode = ViewMode::Items;
            break;
        case 4:
            selectedMode = ViewMode::Trainer;
            break;
        case 5:
            selectedMode = ViewMode::Settings;
            break;
        }

        // Touch: tapping a menu pill/icon (ids 100-105) focuses it and acts like A.
        int homeTap = touchedButtonId(touch);
        if (homeTap >= 100 && homeTap <= 105)
        {
            homeMenuIndex = homeTap - 100;
            switch (homeMenuIndex)
            {
            case 0:
                selectedMode = ViewMode::Boxes;
                break;
            case 1:
                selectedMode = ViewMode::Party;
                break;
            case 2:
                selectedMode = ViewMode::Storage;
                storageSearch.clear();
                break;
            case 3:
                selectedMode = ViewMode::Items;
                break;
            case 4:
                selectedMode = ViewMode::Trainer;
                break;
            case 5:
                selectedMode = ViewMode::Settings;
                break;
            }
            buttonsDown |= HidNpadButton_A;
        }

        // A / tap: activate the focused destination (enter the mode, or toggle theme for Settings).
        if (buttonsDown & HidNpadButton_A)
        {
            switch (homeMenuIndex)
            {
            case 0: // Pokemon (Boxes)
                detailViewActive = true;
                selectedItemIndex = 0;
                currentPage = 0;
                break;
            case 1: // Party
                detailViewActive = true;
                selectedPartyIndex = 0;
                break;
            case 2: // Storage (bank) — start on the save pane, Menu mode, nothing held/selected.
                detailViewActive = true;
                storageFocusPane = 0;
                stSaveSlot = 0;
                stBankSlot = 0;
                moveMon.clear();
                selectDimensions = {0, 0};
                currentlySelecting = false;
                storageMenuActive = false;
                groupMenuActive = false;
                cursorMode = CursorMode::Menu;
                break;
            case 3:
                detailViewActive = true;
                selectedItemIndex = 0;
                currentPage = 0;
                break;
            case 4: // Trainer info
                detailViewActive = true;
                break;
            case 5: // Settings screen (auto-backup + theme)
                detailViewActive = true;
                settingsSelectedRow = 0;
                break;
            }
        }

        // L/R to navigate categories (Items mode only, when not in detail view)
        if (selectedMode == ViewMode::Items)
        {
            if (buttonsDown & HidNpadButton_L)
            {
                switch (trainer.getGameGroup())
                {
                case GameVersion::ZA:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_LZA) % POUCH_COUNT9_LZA;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::SV:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT9_SV) % POUCH_COUNT9_SV;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::PLA:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT8_LA) % POUCH_COUNT8_LA;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::BDSP:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT8BDSP) % POUCH_COUNT8BDSP;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::SWSH:
                {
                    selectedCategory = (selectedCategory - 1 + static_cast<int>(Trainer::PouchType8SWSH::Count)) %
                                       static_cast<int>(Trainer::PouchType8SWSH::Count);
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::GG:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT7_LGPE) % POUCH_COUNT7_LGPE;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::FRLG:
                case GameVersion::RSE:
                {
                    selectedCategory = (selectedCategory - 1 + POUCH_COUNT3_FRLG) % POUCH_COUNT3_FRLG;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                default:
                    break;
                }
            }
            if (buttonsDown & HidNpadButton_R)
            {
                switch (trainer.getGameGroup())
                {
                case GameVersion::ZA:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_LZA;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::SV:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT9_SV;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::PLA:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT8_LA;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::BDSP:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT8BDSP;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::SWSH:
                {
                    selectedCategory = (selectedCategory + 1) % static_cast<int>(Trainer::PouchType8SWSH::Count);
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::GG:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT7_LGPE;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                case GameVersion::FRLG:
                case GameVersion::RSE:
                {
                    selectedCategory = (selectedCategory + 1) % POUCH_COUNT3_FRLG;
                    currentPage = 0;
                    selectedItemIndex = 0;
                    break;
                }
                default:
                    break;
                }
            }
        }

        // L/R to navigate boxes (Boxes mode only, when not in detail view)
        if (selectedMode == ViewMode::Boxes)
        {
            if (buttonsDown & HidNpadButton_L)
            {
                int boxCount = static_cast<int>(trainer.getBoxCount());
                selectedBoxIndex = (selectedBoxIndex - 1 + boxCount) % boxCount;
            }
            if (buttonsDown & HidNpadButton_R)
            {
                int boxCount = static_cast<int>(trainer.getBoxCount());
                selectedBoxIndex = (selectedBoxIndex + 1) % boxCount;
            }
        }
    }

    void TrainerViewScreen::draw(PKSEFramebuffer &framebuffer)
    {
        framebuffer.clear(Colors::Background);

        std::string subtitle = titleName;
        if (!gameVersion.empty())
        {
            subtitle += "  v" + gameVersion;
        }
        if (!trainer.saveRevisionString.empty() && trainer.saveRevisionString != "Base")
        {
            subtitle += "  (" + trainer.saveRevisionString + ")";
        }
        drawTitleBar(framebuffer, subtitle);

        // Not entered -> the HOME main menu. Entered -> the selected mode's content spans the full width.
        const bool entered = detailViewActive;
        if (!entered)
        {
            Panels::drawHomeMenu(*this, framebuffer);
        }
        else
        {
            const int contentX = LEFT_PANEL_X;
            const int contentPanelWidth = framebuffer.getWidth() - contentX;
            switch (selectedMode)
            {
            case ViewMode::Party:
                Panels::drawPartyPokemon(framebuffer, trainer, contentX, CONTENT_PANEL_Y, contentPanelWidth,
                                         CONTENT_PANEL_HEIGHT, selectedPartyIndex);
                break;
            case ViewMode::Boxes:
            {
                // HOME layout: box on the left, the summary side-panel (render + hexagon) on the right.
                constexpr int columnGap = 12, summaryW = 452;
                const int boxWidth = contentPanelWidth - columnGap - summaryW;
                Panels::drawBoxPokemon(*this, framebuffer, contentX, CONTENT_PANEL_Y, boxWidth, CONTENT_PANEL_HEIGHT);
                Panels::drawBoxSummaryPanel(*this, framebuffer, contentX + boxWidth + columnGap, CONTENT_PANEL_Y,
                                            summaryW, CONTENT_PANEL_HEIGHT);
                break;
            }
            case ViewMode::Items:
                Panels::drawItems(*this, framebuffer, contentX, CONTENT_PANEL_Y, contentPanelWidth,
                                  CONTENT_PANEL_HEIGHT);
                break;
            case ViewMode::Storage:
                Panels::drawStorageView(*this, framebuffer, contentX, CONTENT_PANEL_Y, contentPanelWidth,
                                        CONTENT_PANEL_HEIGHT);
                break;
            case ViewMode::Trainer:
                drawTrainerView(*this, framebuffer, contentX, CONTENT_PANEL_Y, contentPanelWidth, CONTENT_PANEL_HEIGHT);
                break;
            case ViewMode::Settings:
                drawSettingsView(*this, framebuffer, contentX, CONTENT_PANEL_Y, contentPanelWidth,
                                 CONTENT_PANEL_HEIGHT);
                break;
            }
        }

        // Draw instructions. An overlay that owns input names its own keys wherever it is drawn --
        // including over the full-screen details page, which paints its own bar -- so that half of
        // the decision lives in overlayNavHint() and both bar-drawers ask it first.
        std::string instructions = overlayNavHint();
        if (!instructions.empty())
        {
            // named by the overlay
        }
        else if (swapActive)
        {
            instructions = "HOLDING  |  Arrows: Move Cursor  |  L/R: Change Box  |  Y: Drop Here  |  B: Cancel";
        }
        else if (details.active)
        {
            instructions = "Up/Down: Select  |  A: Edit  |  Y: Change View  |  B: Close  |  X: Save";
        }
        else if (detailViewActive && selectedMode == ViewMode::Settings)
        {
            instructions = "Up/Down: Select  |  A: Toggle  |  B: Back";
        }
        else if (detailViewActive)
        {
            if (selectedMode == ViewMode::Items)
            {
                instructions = "Up/Down: Select  |  A: Edit Amount  |  Y: Search  |  -: Add Item  |  X: Remove Item  | "
                               " Left/Right: Page  |  L/R: Category  |  B: Back";
            }
            else if (selectedMode == ViewMode::Boxes)
            {
                if (details.active)
                {
                    if (details.category == 0)
                    { // Main
                        instructions =
                            details.editing
                                ? details.selectedField == 3
                                      ? "Up/Down: Select Field | A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                      : "Up/Down: Select Field | B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Category | A: Select Category  |  B: Close  |  X: Save  |  +: Exit "
                                  "App";
                    }
                    else if (details.category == 2)
                    { // Stats
                        instructions =
                            details.editing
                                ? "Up/Down: Select Field  |  A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Category  |  A: Select Category  |  B: Close  |  X: Save  |  +: "
                                  "Exit App";
                    }
                    else
                    {
                        instructions = "Up/Down: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                }
                else
                {
                    // Only advertise Details / Grab-Move when the cursor is on an occupied slot.
                    bool anyOccupied = false;
                    if (selectedBoxIndex >= 0 && selectedBoxIndex < static_cast<int>(trainer.boxes.size()) &&
                        selectedItemIndex >= 0 && selectedItemIndex < static_cast<int>(BOX_SLOTS))
                    {
                        const auto &bpk = trainer.boxes[selectedBoxIndex][selectedItemIndex];
                        anyOccupied = bpk && bpk->speciesID() != 0;
                    }
                    // On the box-name header, A renames; on a slot, the usual actions. No "navigate up
                    // to rename" hint -- it's discoverable (the pill highlights) and just adds clutter.
                    if (selectedItemIndex == -1)
                    {
                        instructions = "L/R: Box  |  A: Rename  |  B: Back";
                    }
                    else
                    {
                        instructions = anyOccupied ? "Arrows: Navigate  |  L/R: Box  |  A: Details  |  X: Release  |  "
                                                     "Y: Grab/Move  |  B: Back"
                                                   : "Arrows: Navigate  |  L/R: Box  |  A: Create  |  B: Back";
                    }
                }
            }
            else if (selectedMode == ViewMode::Party)
            {
              // will look into this at some point.
                if (details.active)
                {
                    if (details.category == 0)
                    { // Main
                        instructions =
                            details.editing
                                ? details.selectedField == 3
                                      ? "Up/Down: Select Field | A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                      : "Up/Down: Select Field | B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Category | A: Select Category  |  B: Close  |  X: Save  |  +: Exit "
                                  "App";
                    }
                    else if (details.category == 2)
                    { // Stats
                        instructions =
                            details.editing
                                ? "Up/Down: Select Field  |  A: Edit Field |  B: Back  |  X: Save  |  +: Exit App"
                                : "Up/Down: Select Category  |  A: Select Category  |  B: Close  |  X: Save  |  +: "
                                  "Exit App";
                    }
                    else
                    {
                        instructions = "Up/Down: Select Category  |  B: Close  |  X: Save  |  +: Exit App";
                    }
                }
                else
                {
                    instructions = "Arrows: Navigate Grid  |  A: View Details  |  B: Back  |  +: Exit App";
                }
            }
            else if (selectedMode == ViewMode::Storage)
            {
                if (details.active)
                {
                    instructions = "Up/Down: Category  |  A: Edit  |  B: Close";
                }
                else if (storageExitConfirmActive)
                {
                    instructions = "Up/Down: Choose  |  A: Confirm  |  B: Stay";
                }
                else if (storageMenuActive || groupMenuActive)
                {
                    instructions = "Up/Down: Choose  |  A: Select  |  B: Cancel";
                }
                else if (releaseConfirmActive)
                {
                    instructions = "A: Release  |  B: Cancel";
                }
                else if (currentlySelecting)
                {
                    instructions = "Arrows: Size Selection  |  A: Grab Group  |  X: Copy Group  |  B: Cancel";
                }
                else if (carrying())
                {
                    instructions = carriedCount() > 1 ? "Arrows: Move Group (cross panes at edge)  |  L/R: Box  |  A: "
                                                        "Place Here  |  Minus: Options  |  B: Put Back"
                                                      : "Arrows: Move (cross panes at edge)  |  L/R: Box  |  A: Drop / "
                                                        "Swap  |  Minus: Options  |  B: Put Back";
                }
                else if (cursorMode == CursorMode::Menu)
                {
                    instructions = "Arrows: Move  |  L/R: Box  |  ZL/ZR: +/-10  |  Y: Mode (Menu)  |  A: Menu  |  X: "
                                   "Sort  |  Minus: Import  |  B: Back";
                }
                else if (cursorMode == CursorMode::Move)
                {
                    instructions = "Arrows: Move  |  L/R: Box  |  ZL/ZR: +/-10  |  Y: Mode (Move)  |  A: Pick Up  |  "
                                   "X: Sort  |  Minus: Import  |  B: Back";
                }
                else
                {
                    instructions = "Arrows: Move  |  L/R: Box  |  ZL/ZR: +/-10  |  Y: Mode (Multi)  |  A: Select  |  "
                                   "X: Sort  |  Minus: Import  |  B: Back";
                }
            }
            else if (selectedMode == ViewMode::Trainer)
            {
                // X saves only from the HOME menu (see the X handler), so the flow is edit -> B -> X.
                instructions = "Up/Down: Select  |  A: Edit  |  B: Back  |  +: Exit App";
            }
        }
        else
        {
            // HOME main menu.
            instructions = "Arrows: Navigate  |  A: Open  |  B: Go Back  |  X: Save  |  +: Exit App";
        }
        // Skipped when the details page is up: that page is full-screen, paints an opaque gradient
        // over everything including this strip, and draws its own bar (with these same hints when an
        // overlay owns input). Drawing one here as well put two bars in the frame -- invisible,
        // because the second covered the first exactly, but wasted work and indistinguishable from
        // a real double-bar bug -- one nav bar per frame, always.
        if (!details.active)
            drawNavBar(framebuffer, instructions);
        const int footerY = framebuffer.getHeight() - NAV_BAR_HEIGHT;

        // Draw dialogs on top of everything (Modals first, then dialogs)
        if (details.active)
        {
            Modals::drawPokemonDetailsModal(*this, framebuffer);
        }
        if (pickerActive)
        { // overlays the modal; registers its own touch buttons last
            Dialogs::drawPickerDialog(*this, framebuffer);
        }
        if (itemEditDialogActive)
        {
            Dialogs::drawItemEditDialog(*this, framebuffer);
        }
        if (itemRemoveConfirmActive)
        {
            Dialogs::drawItemRemoveConfirm(*this, framebuffer);
        }
        if (statEdit.dialogActive)
        {
            Dialogs::drawStatEditDialog(*this, framebuffer);
        }
        if (saveConfirmActive)
        {
            Dialogs::drawSaveConfirmDialog(*this, framebuffer);
        }
        if (saveInjectConfirmActive)
        { // overlays the picker
            Dialogs::drawSaveInjectConfirm(*this, framebuffer);
        }
        if (storageMenuActive)
        {
            Panels::drawStorageActionMenu(*this, framebuffer);
        }
        if (groupMenuActive)
        {
            Panels::drawStorageGroupMenu(*this, framebuffer);
        }
        if (releaseConfirmActive)
        {
            Panels::drawStorageReleaseConfirm(*this, framebuffer);
        }
        if (storageExitConfirmActive)
        {
            Panels::drawStorageExitConfirm(*this, framebuffer);
        }
        if (creator.keepConfirmActive)
        { // "Keep this new Pokemon?" overlays the creator's editor
            Panels::drawCreatorKeepConfirm(*this, framebuffer);
        }
        if (details.discardConfirmActive)
        { // "Unsaved changes" on closing an existing pokemon's editor
            Panels::drawDetailsDiscardConfirm(*this, framebuffer);
        }
        if (tradeEvolveChoiceActive)
        { // "Trade Evolve": which of two destinations -- Clamperl holding neither item
            Panels::drawTradeEvolveChoice(*this, framebuffer);
        }
        if (tradeEvolveConfirmActive)
        { // "Trade Evolve": a real save, a generated trainer, or cancel
            Panels::drawTradeEvolveConfirm(*this, framebuffer);
        }
        if (tradePartnerTitles.active)
        { // which of this console's saves is the other side of the trade
            Panels::drawTradePartnerTitlePicker(*this, framebuffer);
        }
        if (gen3ConvertConfirmActive)
        { // "Convert to Gen 3?" -- PID rebuild, never gated
            Panels::drawGen3ConvertConfirm(*this, framebuffer);
        }
        if (virtualConsoleTransferConfirmActive)
        {
            Panels::drawVirtualConsoleTransferConfirm(*this, framebuffer);
        }
        if (lgpeTransferConfirmActive)
        { // "Moving to/from Let's Go resets AVs/EVs" -- gated by g_moveWarn
            Panels::drawLgpeTransferConfirm(*this, framebuffer);
        }
        // PKSM import, back to front: the browser stays up underneath the preview so Cancel can drop
        // straight back onto the file list. Each one clears + repopulates touchButtons, so only the
        // topmost is tappable -- which matches which one owns input.
        if (fileBrowser.active)
        {
            // Signature changed since this feature was parked: the browser is shared with
            // SaveSelectScreen now, so it takes the state + tap vector rather than a screen.
            Dialogs::drawFileBrowser(fileBrowser, framebuffer, touchButtons);
        }
        if (pksmImport.previewActive)
        {
            Dialogs::drawPKSMImportPreview(*this, framebuffer);
        }
        if (pksmImport.resultActive)
        {
            Dialogs::drawPKSMImportResult(*this, framebuffer);
        }

        // Transient status line (a refused cross-game drop, a rejected name), centered above the footer.
        // Drawn LAST so it is not painted over: a message can be raised from inside the details modal
        // (a nickname Gen 3 can't store), and the modal is drawn after the main body.
        if (storageStatusFrames > 0 && !storageStatus.empty())
        {
            int textWidth, th;
            framebuffer.measureText(storageStatus, textWidth, th);
            const int padX = 18, bw = textWidth + padX * 2, bh = th + 14;
            const int boxX = (framebuffer.getWidth() - bw) / 2, by = footerY - bh - 12;
            framebuffer.drawFilledRoundedRect(boxX, by, bw, bh, 8, Colors::Panel);
            framebuffer.drawRoundedRect(boxX, by, bw, bh, 8, Colors::Accent, 2);
            framebuffer.drawText(boxX + padX, by + 7, storageStatus, Colors::Text);
        }
    }
}
