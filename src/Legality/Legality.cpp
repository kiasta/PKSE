/**
 * See Legality.h. Every check reads the base Pokemon interface polymorphically;
 * per-generation behavior branches on the game group / capability flags, never on type
 * (RTTI is disabled). Optional fields that are unwired for a format read 0 and are
 * treated as "not applicable -> skip" so unsupported formats produce no false flags.
 */

#include "Legality/Legality.h"

#include <string>

#include "Legality/EncounterMatch.h" // Layer 3 -- encounter templates
#include "Enums/LanguageID.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Experience.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/AbilityInfo.h" // getAbilitySlots -> per-generation ability slots
#include "Pokemon/FormInfo.h"    // isFormGenderSpecific, getGenderRatio
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

        /// Generation of a save-format group (1-9), 0 when it is not one.
        int groupGeneration(Enums::GameVersion group)
        {
            return Enums::getVersionGeneration(Enums::getGroupRepVersion(group));
        }

        constexpr int MAX_LEARN_GROUPS = 24;

        struct LearnGroups
        {
            Enums::GameVersion groups[MAX_LEARN_GROUPS];
            int groupCount = 0;

            void add(Enums::GameVersion group)
            {
                for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex)
                {
                    if (groups[groupIndex] == group)
                        return;
                }
                if (groupCount < MAX_LEARN_GROUPS)
                    groups[groupCount++] = group;
            }

            /// Every save-format group of one 3DS-or-older generation. A Pokemon that lived in a
            /// generation may have been traded between its games, so their pools all count
            /// (PKHeX LearnGroup3-7 each check every game of their generation).
            void addGeneration(int generation)
            {
                switch (generation)
                {
                case 1:
                    add(Enums::GameVersion::RBY);
                    break;
                case 2:
                    add(Enums::GameVersion::GSC);
                    break;
                case 3:
                    add(Enums::GameVersion::RSE);
                    add(Enums::GameVersion::FRLG);
                    break;
                case 4:
                    add(Enums::GameVersion::DP);
                    add(Enums::GameVersion::PT);
                    add(Enums::GameVersion::HGSS);
                    break;
                case 5:
                    add(Enums::GameVersion::BW);
                    add(Enums::GameVersion::B2W2);
                    break;
                case 6:
                    add(Enums::GameVersion::XY);
                    add(Enums::GameVersion::ORAS);
                    break;
                case 7:
                    add(Enums::GameVersion::SM);
                    add(Enums::GameVersion::USUM);
                    break;
                default:
                    break;
                }
            }

            /// The generations a Pokemon of `originGeneration` passed through on its way up to
            /// `lastGeneration`, inclusive. Gen 1/2 Virtual Console Pokemon skip straight to Gen 7
            /// through Poke Transporter, and the Game Boy games trade with each other through the
            /// Time Capsule (PKHeX LearnGroup7.GetPrevious, LearnGroup1/2.GetPrevious).
            void addPath(int originGeneration, int lastGeneration)
            {
                if (originGeneration == 1 || originGeneration == 2)
                {
                    addGeneration(1);
                    addGeneration(2);
                    if (lastGeneration >= 7)
                        addGeneration(7);
                    return;
                }
                if (originGeneration < 3)
                    return;
                for (int generation = originGeneration; generation <= lastGeneration && generation <= 7; ++generation)
                    addGeneration(generation);
            }
        };

        /// Every learn pool a Pokemon in `formatGroup` can have taken its moves from -- PKHeX walks
        /// each generation the Pokemon visited (LearnGroup*.GetPrevious), and a pool of one game
        /// alone called every move it learned before a transfer unlearnable: an X/Y Chespin's Tackle
        /// in Sun, a Johto Articuno's Red/Blue Peck, a White Pokemon's Black 2/White 2 tutor move.
        LearnGroups getLearnGroups(const Pokemon::Pokemon &pokemon, Enums::GameVersion formatGroup)
        {
            constexpr uint8_t koreanLanguage = 8;
            constexpr uint8_t pokemonGoVersion = 34;
            LearnGroups learnGroups;
            learnGroups.add(formatGroup);
            const int formatGeneration = groupGeneration(formatGroup);
            const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            const int originGeneration = (originVersion == 0 || originVersion == pokemonGoVersion) ? 0 : Enums::getVersionGeneration(originVersion);

            if (formatGroup == Enums::GameVersion::GG)
                return learnGroups; // Let's Go has no earlier game to have learned anything in (LearnGroup7b)
            if (formatGeneration == 1 || formatGeneration == 2)
            {
                // The Time Capsule; Korean Gold/Silver has none, so a Korean Gen 2 Pokemon never
                // saw Red/Blue/Yellow (LearnGroup2.GetPrevious).
                learnGroups.addGeneration(formatGeneration);
                if (formatGeneration == 1 || pokemon.language() != koreanLanguage)
                    learnGroups.addPath(1, formatGeneration);
                return learnGroups;
            }
            if (formatGeneration >= 3 && formatGeneration <= 7)
            {
                learnGroups.addGeneration(formatGeneration);
                if (originGeneration != 0 && originGeneration < formatGeneration)
                    learnGroups.addPath(originGeneration, formatGeneration);
                return learnGroups;
            }

            // The HOME era. Sword/Shield is the one Switch game a 3DS Pokemon reaches with its
            // earlier moves still judged (LearnGroup8.GoBackwards); every HOME game reaches the
            // others only through HOME, so only a Pokemon carrying a HOME tracker has visited them
            // (LearnGroupHOME; PKHeX's default HOMETransferTrackerNotPresent is Invalid).
            const bool hasVisitedHome = pokemon.hasHomeTracker();
            if (originGeneration != 0 && originGeneration <= 7 && (formatGroup == Enums::GameVersion::SWSH || hasVisitedHome))
            {
                if (originVersion == static_cast<uint8_t>(Enums::GameVersion::GP) || originVersion == static_cast<uint8_t>(Enums::GameVersion::GE))
                    learnGroups.add(Enums::GameVersion::GG);
                else
                    learnGroups.addPath(originGeneration, 7);
            }
            if (hasVisitedHome)
            {
                learnGroups.add(Enums::GameVersion::SWSH);
                learnGroups.add(Enums::GameVersion::BDSP);
                learnGroups.add(Enums::GameVersion::PLA);
                learnGroups.add(Enums::GameVersion::SV);
                learnGroups.add(Enums::GameVersion::ZA);
            }
            return learnGroups;
        }

        bool isLearnableInAny(const LearnGroups &learnGroups, uint16_t species, uint8_t form, uint16_t moveId)
        {
            for (int groupIndex = 0; groupIndex < learnGroups.groupCount; ++groupIndex)
            {
                if (Pokemon::isLearnable(species, form, learnGroups.groups[groupIndex], moveId))
                    return true;
            }
            return false;
        }

        /// Gen 5's Blue-Striped Basculin is the one ability PKHeX lets disagree with the personal
        /// table (AbilityVerifier.IsEdgeCaseBasculin): Black/White read the wrong personal entry
        /// and gave wild and bred ones Reckless, and Black 2/White 2 did the same to bred ones.
        /// Rock Head is right for the rest. Which encounter it came from is Layer 3's question, so
        /// both are accepted here.
        bool isGenerationFiveBasculinReckless(const Pokemon::Pokemon &pokemon, Enums::GameVersion formatGroup)
        {
            constexpr uint16_t basculinSpecies = 550;
            constexpr uint8_t blueStripedForm = 1;
            constexpr uint16_t recklessAbility = 120;
            constexpr uint8_t hiddenAbilityNumber = 4;
            return groupGeneration(formatGroup) == 5 && pokemon.speciesID() == basculinSpecies && pokemon.form() == blueStripedForm && pokemon.ability() == recklessAbility && pokemon.abilityNumber() != hiddenAbilityNumber;
        }
    }

    Report analyze(const Pokemon::Pokemon &pokemon, Enums::GameVersion originGroup)
    {
        Report report;
        const uint16_t species = pokemon.speciesID();
        // empty slot — nothing to validate
        if (species == 0) return report;

        // Species/form personal data (per-game presence) for the L2 checks.
        const Pokemon::PersonalInfo &pi = Pokemon::getPersonalInfo(species, pokemon.form());
        const int formatGeneration = groupGeneration(originGroup);

        if (std::string(Trainer::getSpeciesName(species)) == "Unknown")
            add(report, Severity::Invalid, "Unknown species id " + std::to_string(species));

        if (pokemon.nature() > 24)
            add(report, Severity::Invalid, "Nature out of range (" + std::to_string(pokemon.nature()) + ")");
        // The stat nature (mints) is its own field from Gen 8 on; every older format answers it with
        // the nature itself, already checked above.
        if (formatGeneration >= 8 && pokemon.statNature() > 24)
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
            // Everywhere else: <=510 total, and <=252 per stat from Gen 6 on. Gens 3-5 cap a stat
            // at 255, which is the ordinary result of vitamins and training there, not an edit
            // (PKHeX EffortValueVerifier applies the 252 cap only for format >= 6).
            for (int statIndex = 0; statIndex < 6 && formatGeneration >= 6; ++statIndex)
            {
                if (evValue[statIndex] > 252)
                    add(report, Severity::Invalid, std::string(statName(statIndex)) + " EV over 252 (" + std::to_string(evValue[statIndex]) + ")");
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
            if (!Pokemon::isAbilityLegal(pokemon) && !isGenerationFiveBasculinReckless(pokemon, originGroup))
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

        // The pool is each game's own learn methods unioned with the species' whole pre-evolution
        // chain, so an inherited move does not false-flag, and then unioned across every game the
        // Pokemon can have visited -- see getLearnGroups. Still a Warning rather than Invalid: the
        // pools cannot express *when* a move was legal (tutors that came and went), and they hold no
        // move a specific encounter handed out (an event's, the Odd Egg's Dizzy Punch).
        //
        // A missing pool for the save's own game means "unknown", not "cannot learn", so the check
        // only runs when that one exists.
        if (Pokemon::getLearnableBits(species, pokemon.form(), originGroup) != nullptr)
        {
            const LearnGroups learnGroups = getLearnGroups(pokemon, originGroup);
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = pokemon.move(moveIndex);
                if (moveId == 0 || isLearnableInAny(learnGroups, species, pokemon.form(), moveId))
                    continue;
                const std::string moveName = Names::getMoveName(moveId);
                if (pokemon.isFatefulEncounter())
                    add(report, Severity::Warning, "Move may not be learnable: " + moveName + " (event moves are not modelled)");
                else
                    add(report, Severity::Warning, "Move may not be learnable: " + moveName);
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
        // Judged against the save's own format: Korean arrived in Gens 2 and 4, the Chinese scripts
        // in Gen 7 and Latin American Spanish with Legends: Z-A (PKHeX Legal.GetMaxLanguageID).
        if (pokemon.language() != 0 && !Enums::groupHasLanguage(originGroup, pokemon.language()))
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
            const uint8_t genderRatio = Pokemon::getGenderRatio(species, pokemon.form(), originGroup);
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
            else if (genderRatio == 255)
            {
                if (genderValue != 2)
                    add(report, Severity::Invalid, "Genderless species but a gender is set");
            }
            else if (genderValue == 2)
                add(report, Severity::Invalid, "Gendered species marked genderless");
            else if (genderRatio == 254 && genderValue != 1)
                add(report, Severity::Invalid, "Female-only species set to male");
            else if (genderRatio == 0 && genderValue != 0)
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
