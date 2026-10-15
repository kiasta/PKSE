/**
 * See Legality.h. Every check reads the base Pokemon interface polymorphically;
 * per-generation behavior branches on the game group / capability flags, never on type
 * (RTTI is disabled). Optional fields that are unwired for a format read 0 and are
 * treated as "not applicable -> skip" so unsupported formats produce no false flags.
 */

#include "Legality/Legality.h"

#include <string>

#include "Legality/EncounterMatch.h" // Layer 3 -- encounter templates
#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h" // getAbilitySlots -> per-generation ability slots
#include "Pokemon/FormInfo.h"    // isFormGenderSpecific -> gender stored as the form index
#include "Pokemon/LearnsetTable.h"
#include "Trainer/Trainer.h" // Trainer::getSpeciesName / getItemName (name-table sentinels)
#include "Names/MoveNames.h" // Names::getMoveName
#include "Names/ItemNames.h" // Names::getItemNameG3 (Gen 3 item id space)

namespace Legality
{

    namespace
    {
        void add(Report &report, Severity severity, std::string text)
        {
            report.issues.push_back(Issue{severity, std::move(text)});
        }
        const char *statName(int statIndex)
        {
            static const char *const statNames[6] = {"HP", "Atk", "Def", "Spe", "SpA", "SpD"};
            return statNames[statIndex];
        }
        // Personal-table presence bit for a game group (0 if the group has no bit).
        uint8_t presenceBit(Enums::GameVersion gameVersion)
        {
            switch (gameVersion)
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
    }

    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup)
    {
        Report report;
        const uint16_t species = pokemon.speciesID();
        // empty slot — nothing to validate
        if (species == 0) return report;

        // LGPE (group GG) is the only pre-Gen8 supported group; the rest have mint/stat nature.
        const bool gen8plus = (originGroup != Enums::GameVersion::GG);

        // Species/form personal data (abilities / gender ratio / per-game presence) for the L2 checks.
        const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(species, pokemon.form());

        if (std::string(Trainer::getSpeciesName(species)) == "Unknown")
            add(report, Severity::Invalid, "Unknown species id " + std::to_string(species));

        if (pokemon.nature() > 24)
            add(report, Severity::Invalid, "Nature out of range (" + std::to_string(pokemon.nature()) + ")");
        if (gen8plus && pokemon.statNature() > 24)
            add(report, Severity::Invalid, "Stat nature out of range (" + std::to_string(pokemon.statNature()) + ")");

        const uint8_t evValue[6] = {pokemon.evHP(),  pokemon.evATK(), pokemon.evDEF(),
                                    pokemon.evSPE(), pokemon.evSPA(), pokemon.evSPD()};
        int evTotal = 0;
        for (int index = 0; index < 6; ++index)
            evTotal += evValue[index];

        if (pokemon.hasAwakeningValues())
        {
            // Let's Go has no EV training -- it replaced it with Awakening Values. Nothing in the game
            // writes these bytes and the stat formula has no EV term (PKHeX PB7.LoadStats), so on a
            // legitimate Pokemon they are all 0.
            //
            // The 252/510 caps are the wrong question here, not merely a differently-worded one: they
            // would pass a fully trained 510 spread as perfectly legal while a lone stray 4 slipped by
            // in silence. What matters is non-zero at all, so that is what is checked.
            //
            // Reported as a WARNING, not Invalid. PKHeX has this exact rule ("Cannot receive EVs.") but
            // deliberately leaves it disabled, so calling it illegal outright would be a stronger claim
            // than the reference is willing to make. It is still worth surfacing -- the value cannot come
            // from playing the game -- and it is harmless in itself, since no stat reads it.
            if (evTotal != 0)
            {
                std::string which;
                for (int index = 0; index < 6; ++index)
                {
                    if (evValue[index] == 0)
                        continue;
                    if (!which.empty())
                        which += ", ";
                    which += std::string(statName(index)) + " " + std::to_string(evValue[index]);
                }
                add(report, Severity::Warning, "Let's Go has no EVs, but this Pokemon carries " + which + " -- Awakening Values are its stat training, and these bytes affect nothing");
            }
        }
        else if (pokemon.maxEV() > 255)
        {
            // Gen 1/2 have no EVs. They have Stat Experience: a 16-bit counter per stat that caps
            // at 65535 with NO combined limit, so every value the field can physically hold is
            // legal and there is nothing here to check. That is a real answer, not a gap.
            //
            // Applying the 252/510 rule anyway would not merely be pedantic, it would be loudly
            // wrong: the ev[] array above comes from the 8-bit accessors, which SATURATE for these
            // formats, so any well-trained Red/Blue Pokemon reads 255 in all six slots and gets
            // reported six times for "EV over 252" plus once for "EV total over 510" -- seven
            // Invalid findings on a Pokemon that was legitimately trained on a Game Boy.
        }
        else
        {
            // Everywhere else: <=252 per stat, <=510 total.
            for (int index = 0; index < 6; ++index)
            {
                if (evValue[index] > 252)
                    add(report, Severity::Invalid, std::string(statName(index)) + " EV over 252 (" + std::to_string(evValue[index]) + ")");
            }
            if (evTotal > 510)
                add(report, Severity::Invalid, "EV total over 510 (" + std::to_string(evTotal) + ")");
        }

        if (pokemon.hasAwakeningValues())
        {
            const uint8_t awakeningValues[6] = {pokemon.avHP(),  pokemon.avATK(), pokemon.avDEF(),
                                                pokemon.avSPE(), pokemon.avSPA(), pokemon.avSPD()};
            for (int index = 0; index < 6; ++index)
                if (awakeningValues[index] > 200)
                    add(report, Severity::Invalid, std::string(statName(index)) + " AV over 200 (" + std::to_string(awakeningValues[index]) + ")");
        }

        // Skipped entirely for a format with no abilities. Gen 1 predates them: ability() and
        // abilityNumber() both read 0, which this check would report as an unusual slot AND as an
        // ability the species cannot have -- two Invalid findings about a mechanic the game does
        // not have. Same reasoning as the Stat Experience branch above.
        if (pokemon.hasAbility())
        {
            const uint8_t abilityNumberValue = pokemon.abilityNumber();
            if (abilityNumberValue != 1 && abilityNumberValue != 2 && abilityNumberValue != 4)
                add(report, Severity::Warning, "Unusual ability slot (" + std::to_string(abilityNumberValue) + ")");
            // Slots are per-generation: Gen 3 has two of them and its pair differs from the modern
            // table for 101 species, so checking a Gen 3 pokemon against the modern one both misses real
            // problems and invents fake ones. PKHeX encodes "single ability" as ability2 == ability1,
            // so that set collapses naturally. In Legends: Z-A the slot may still hold what the
            // Pokemon had BEFORE it evolved -- see Pokemon::isAbilityLegal.
            if (!Pokemon::isAbilityLegal(pokemon))
                add(report, Severity::Invalid, "Ability not legal for this species");
        }

        {
            const uint16_t moveIds[4] = {pokemon.move(0), pokemon.move(1), pokemon.move(2), pokemon.move(3)};
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                if (moveIds[moveIndex] != 0 && std::string(Names::getMoveName(moveIds[moveIndex])) == "-")
                    add(report, Severity::Invalid, "Unknown move in slot " + std::to_string(moveIndex + 1));
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                for (int otherMoveIndex = moveIndex + 1; otherMoveIndex < 4; ++otherMoveIndex)
                    if (moveIds[moveIndex] != 0 && moveIds[moveIndex] == moveIds[otherMoveIndex])
                    {
                        add(report, Severity::Invalid, "Duplicate move: " + std::string(Names::getMoveName(moveIds[moveIndex])));
                        break; // report each duplicated move once
                    }
        }

        // The pool is that game's own learn methods unioned with the species' whole pre-evolution chain,
        // so an inherited move no longer false-flags. Still a Warning rather than Invalid: the pool can't
        // express *when* a move was legal (move tutors that came and went, event moves, trade-backs).
        // All seven games have a table now, but keep the nullptr guard honest -- if a group ever lacks one,
        // getLearnableBits() returns nullptr meaning "unknown", which must not be reported as illegal.
        //
        // A Pokemon that came through Poke Transporter learned its moves in Gen 1 or Gen 2, and no
        // later learn pool records that -- a Yellow Pikachu really does know Surf. So a transferred
        // pokemon is checked against the UNION of the two pools: the one it learned in and the one it
        // lives in now. Both halves are needed. Checking only the modern pool flags every legitimate
        // Virtual Console move, which PKSE's own transfer produces; checking only the old one flags
        // every TM taught after it arrived. The origin byte is what says so, and it is trustworthy
        // here -- nothing but a transfer writes a Gen 1/2 version into a modern record.
        const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
        const Enums::GameVersion transferGroup = (originVersion >= 35 && originVersion <= 38) ? Enums::GameVersion::RBY : (originVersion >= 39 && originVersion <= 41) ? Enums::GameVersion::GSC : originGroup;
        const bool transferPool = transferGroup != originGroup && Pokemon::getLearnableBits(species, pokemon.form(), transferGroup) != nullptr;
        if (Pokemon::getLearnableBits(species, pokemon.form(), originGroup) != nullptr)
        {
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = pokemon.move(moveIndex);
                if (moveId == 0 || Pokemon::isLearnable(species, pokemon.form(), originGroup, moveId))
                    continue;
                if (transferPool && Pokemon::isLearnable(species, pokemon.form(), transferGroup, moveId))
                    continue;
                add(report, Severity::Warning, "Move may not be learnable: " + std::string(Names::getMoveName(moveId)));
            }
        }

        // Resolve through the id space the pokemon's own game uses -- a Gen 3 held item checked against
        // the modern table would either name the wrong item or be reported "unknown" when it is fine.
        if (pokemon.heldItem() != 0)
        {
            const char *heldName = Enums::isGen3Group(originGroup) ? Names::getItemNameG3(pokemon.heldItem()) : Trainer::getItemName(pokemon.heldItem());
            if (std::string(heldName) == "???")
                add(report, Severity::Warning, "Unknown held item id " + std::to_string(pokemon.heldItem()));
        }

        if (pokemon.ball() != 0 && pokemon.ball() > 37)
            add(report, Severity::Invalid, "Ball id out of range (" + std::to_string(pokemon.ball()) + ")");
        if (pokemon.language() != 0 && (pokemon.language() > 10 || pokemon.language() == 6))
            add(report, Severity::Invalid, "Invalid language id (" + std::to_string(pokemon.language()) + ")");

        const uint8_t expLevel = Pokemon::getLevelFromExp(pokemon.exp(), Pokemon::getGrowthRate(species));
        if (expLevel < 1 || expLevel > 100)
            add(report, Severity::Invalid, "EXP maps to invalid level (" + std::to_string(expLevel) + ")");
        if (pokemon.level() != 0 && pokemon.level() != expLevel)
            add(report, Severity::Invalid, "Level " + std::to_string(pokemon.level()) + " does not match EXP (expected " + std::to_string(expLevel) + ")");

        {
            const uint8_t effLevel = pokemon.level() != 0 ? pokemon.level() : expLevel;
            if (pokemon.metLevel() != 0 && pokemon.metLevel() > effLevel)
                add(report, Severity::Invalid, "Met level " + std::to_string(pokemon.metLevel()) + " above current level " + std::to_string(effLevel));
        }

        {
            const uint8_t genderValue = pokemon.gender();
            if (genderValue > 2)
                add(report, Severity::Invalid, "Gender value out of range (" + std::to_string(genderValue) + ")");
            // Meowstic, Indeedee, Basculegion and Oinkologne keep the gender in the form index, so
            // the per-form ratio read above describes the FORM: reporting "male-only species" about
            // a Meowstic would be a false statement about a species that is plainly dual-gender.
            // What can actually be wrong is the pairing -- PKHeX checks the same invariant, from the
            // form side, as `(form & 1) != gender`.
            else if (Pokemon::isFormGenderSpecific(species))
            {
                if (genderValue > 1)
                    add(report, Severity::Invalid, "Gendered species marked genderless");
                else if ((pokemon.form() & 1) != genderValue)
                    add(report, Severity::Invalid, "Form " + std::to_string(pokemon.form()) + " is the " + ((pokemon.form() & 1) ? "female" : "male") + " form but the gender is " + (genderValue ? "female" : "male"));
            }
            else if (pi.genderRatio == 255)
            {
                if (genderValue != 2)
                    add(report, Severity::Invalid, "Genderless species but a gender is set");
            }
            else if (genderValue == 2)
                add(report, Severity::Invalid, "Gendered species marked genderless");
            else if (pi.genderRatio == 254 && genderValue != 1)
                add(report, Severity::Invalid, "Female-only species set to male");
            else if (pi.genderRatio == 0 && genderValue != 0)
                add(report, Severity::Invalid, "Male-only species set to female");
        }

        {
            const uint8_t bit = presenceBit(originGroup);
            if (bit != 0 && (pi.presence & bit) == 0)
                add(report, Severity::Invalid, "Species/form not obtainable in this game");
        }

        const std::u16string otNameStr = pokemon.otName();
        if (!pokemon.isEgg())
        {
            if (otNameStr.empty())
                add(report, Severity::Warning, "Empty OT name");
            if (pokemon.id32() == 0)
                add(report, Severity::Warning, "Trainer ID is zero");
        }

        if (pokemon.nickname().size() > 12)
            add(report, Severity::Warning, "Nickname longer than 12 characters");
        if (otNameStr.size() > 12)
            add(report, Severity::Warning, "OT name longer than 12 characters");

        if (!pokemon.checksumValid())
            add(report, Severity::Warning, "Stored checksum is invalid");

        // Runs last so its findings read as the conclusion after the field checks, and
        // it takes no game argument: the tables follow the pokemon's origin version, not the
        // save it is sitting in. See EncounterMatch.h.
        checkEncounter(pokemon, report);

        return report;
    }
}
