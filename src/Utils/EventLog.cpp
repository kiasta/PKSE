#include "Utils/EventLog.h"

#include <cstdio>

#include "Enums/Ball.h"
#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Names/FormNames.h"
#include "Names/ItemNames.h"
#include "Names/LocationNames.h"
#include "Names/MoveNames.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "Utils/StringHelpers.h"

namespace Utils
{
    namespace
    {
        // Quote and escape. A nickname is user-controlled text that reaches this log verbatim, so a
        // pokemon called `He said "hi"` must not be able to split one field into three.
        std::string quoted(const std::string &value)
        {
            std::string out;
            out.reserve(value.size() + 2);
            out += '"';
            for (const char character : value)
            {
                if (character == '"' || character == '\\')
                    out += '\\';
                out += character;
            }
            out += '"';
            return out;
        }

        std::string numberText(uint32_t value)
        {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%u", value);
            return std::string(buffer);
        }

        std::string hex32(uint32_t value)
        {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "0x%08X", value);
            return std::string(buffer);
        }

        // Empty rather than "(none)" so an absent name is absent from the line entirely -- a field
        // that is present only when it means something is easier to grep than one that is always
        // there and usually blank.
        std::string safeName(const char *text)
        {
            return (text != nullptr && text[0] != '\0') ? std::string(text) : std::string();
        }

    }

    std::string itemName(uint16_t itemId, Enums::GameVersion gameGroup)
    {
        if (itemId == 0)
            return std::string();
        return Enums::isGen3Group(gameGroup) ? safeName(Names::getItemNameG3(itemId)) : safeName(Names::getItemName(itemId));
    }

    std::string logField(const char *key, const std::string &value)
    {
        return std::string(key) + "=" + quoted(value);
    }

    std::string logSlot(const char *key, int pane, int box, int slot)
    {
        // 1-based box/slot to match the UI. Pane is named, not numbered: "1" would be meaningless in
        // a log read six months later, and the save/bank distinction is the whole point of the field.
        return std::string(key) + "=" + (pane == 0 ? "save" : "bank") + "/box" + numberText(static_cast<uint32_t>(box + 1)) + "/slot" + numberText(static_cast<uint32_t>(slot + 1));
    }

    PokemonOrigin classifyMonOrigin(const Pokemon::Pokemon &pokemon, const Trainer::Trainer &save)
    {
        const std::string ot = utf16ToUtf8(pokemon.otName());
        if (ot.empty())
            return PokemonOrigin::NoOT;

        // All three must agree. Name alone is not enough (two trainers can share one), and ID alone
        // is not either -- a renamed trainer keeps their ID. PKHeX uses the same triple to decide
        // whether the handler slot should be filled, which is exactly the behaviour being diagnosed
        // when someone reports an OT/HT problem.
        const bool sameId = (pokemon.id32() == save.ID32);
        const bool sameName = (ot == save.trainerName);
        const bool sameGender = (pokemon.otGender() == save.trainerGender);
        return (sameId && sameName && sameGender) ? PokemonOrigin::OwnCaught : PokemonOrigin::Traded;
    }

    const char *monOriginName(PokemonOrigin origin)
    {
        switch (origin)
        {
        case PokemonOrigin::OwnCaught:
            return "OWN";
        case PokemonOrigin::Traded:
            return "TRADED";
        case PokemonOrigin::NoOT:
            return "NO_OT";
        }
        return "?";
    }

    std::string briefPokemon(const Pokemon::Pokemon &pokemon)
    {
        const std::string species = Names::getDisplayName(pokemon.speciesID(), pokemon.form(), Trainer::getSpeciesName(pokemon.speciesID()), pokemon.getGameGroup());
        std::string out = logField("species", species) + " form=" + numberText(pokemon.formID()) + " lv=" + numberText(pokemon.level());

        const std::string nick = utf16ToUtf8(pokemon.nickname());
        if (!nick.empty() && nick != species)
            out += " " + logField("nick", nick);
        if (pokemon.isShiny(pokemon.id32(), pokemon.species()))
            out += " shiny=Y";
        if (pokemon.isEgg())
            out += " egg=Y";
        return out;
    }

    std::string describePokemon(const Pokemon::Pokemon &pokemon, const Trainer::Trainer &save)
    {
        std::string out = briefPokemon(pokemon);

        out += std::string(" gender=") + pokemon.genderSymbol();
        out += " fmt=" + Enums::getGameVersionName(pokemon.getGameGroup());

        const PokemonOrigin origin = classifyMonOrigin(pokemon, save);
        out += std::string(" own=") + monOriginName(origin);

        const std::string ot = utf16ToUtf8(pokemon.otName());
        if (!ot.empty())
        {
            out += " " + logField("ot", ot);
            out += std::string(" otgender=") + (pokemon.otGender() == 0 ? "M" : "F");
            // Both spellings of the ID. The six-digit form is what the game and the user see; the
            // raw id32 is what the code compares, and a report where those disagree is the bug.
            const uint8_t ver = pokemon.originGame();
            const bool sixDigit = (ver != 0) ? Enums::usesSixDigitTrainerID(ver) : Enums::getVersionGeneration(Enums::getGroupRepVersion(pokemon.getGameGroup())) >= 7;
            out += " otid=" + numberText(sixDigit ? (pokemon.id32() % 1000000u) : (pokemon.id32() & 0xFFFFu));
            out += " otid32=" + hex32(pokemon.id32());
        }
        // Logged for every format that HAS a handler, set or not -- matching the details view, and
        // for the same reason. Gating on a non-empty name made two different facts produce the same
        // line: "this Pokemon has never been traded" and "this game has no handler field". An empty
        // HT on a pokemon the save did not originate is itself the finding, and a log that omits the
        // field cannot show it. Gens 1-5 have no handler, so they print nothing here and that
        // absence now means exactly one thing.
        if (pokemon.hasHandler())
        {
            const std::string ht = utf16ToUtf8(pokemon.htName());
            out += " " + logField("ht", ht.empty() ? "(none)" : ht);
            if (!ht.empty())
                out += std::string(" htgender=") + (pokemon.htGender() == 0 ? "M" : "F");
            out += std::string(" handler=") + (pokemon.currentHandler() == 0 ? "OT" : "HT");
        }
        // The save's own trainer, so a log read in isolation can verify the OWN/TRADED call above
        // instead of asking the reporter who they are.
        out += " " + logField("saveot", save.trainerName) + " saveid32=" + hex32(save.ID32);

        // Gated on the format's own predicates, for the reason those predicates exist: 0 is a real
        // version and a real location, so a format that stores NEITHER field still reads back as
        // both. Ungated, a Gen 1 pokemon logged "origin=Unknown" beside a met location resolved through
        // the Scarlet table -- invented data in the one record a bug report gets read from.
        if (pokemon.hasOriginGame())
        {
            const std::string og = Enums::getOriginGameName(pokemon.originGame());
            if (!og.empty())
                out += " " + logField("origin", og);
        }
        if (pokemon.hasMetData())
        {
            const uint8_t fmtVer = Enums::getGroupRepVersion(pokemon.getGameGroup());
            const std::string met = safeName(Names::getMetLocationName(Enums::locationTableVersion(pokemon.originGame(), fmtVer, false), pokemon.metLocation()));
            if (!met.empty())
                out += " " + logField("met", met);
            out += " metloc=" + numberText(pokemon.metLocation()) + " metlv=" + numberText(pokemon.metLevel());
            if (pokemon.metMonth() != 0 && pokemon.metDay() != 0)
            {
                char date[16];
                snprintf(date, sizeof(date), "%04u-%02u-%02u", 2000 + pokemon.metYear(), pokemon.metMonth(), pokemon.metDay());
                out += std::string(" metdate=") + date;
            }
        }
        if (pokemon.eggLocation() != 0)
        {
            out += " eggLocation=" + numberText(pokemon.eggLocation());
            if (pokemon.isFatefulEncounter())
                out += " fateful=Y";
        }
        else if (pokemon.isFatefulEncounter())
        {
            out += " fateful=Y";
        }

        {
            const std::string ball = safeName(Enums::getBallName(pokemon.ball()));
            if (!ball.empty())
                out += " " + logField("ball", ball);
        }
        {
            const std::string lang = safeName(Enums::getLanguageName(pokemon.language()));
            if (!lang.empty())
                out += " " + logField("lang", lang);
        }
        {
            const std::string ability = safeName(Trainer::getAbilityName(pokemon.ability()));
            if (!ability.empty())
                out += " " + logField("ability", ability);
        }
        out += " abilityid=" + numberText(pokemon.ability());
        {
            const std::string nature = safeName(Trainer::getNatureName(pokemon.nature()));
            if (!nature.empty())
                out += " " + logField("nature", nature);
        }
        {
            const std::string held = itemName(pokemon.heldItem(), pokemon.getGameGroup());
            if (!held.empty())
                out += " " + logField("held", held);
        }
        out += " friendship=" + numberText(pokemon.friendship());
        out += " pid=" + hex32(pokemon.pid()) + " ec=" + hex32(pokemon.encryptionConstant());
        out += " exp=" + numberText(pokemon.exp());

        out += " ivs=" + numberText(pokemon.ivHP()) + "/" + numberText(pokemon.ivATK()) + "/" + numberText(pokemon.ivDEF()) + "/" + numberText(pokemon.ivSPA()) + "/" + numberText(pokemon.ivSPD()) + "/" + numberText(pokemon.ivSPE());
        // Which stats are Hyper Trained, in the same order as the IVs above, so the line explains
        // itself: "ivs=20/31/20/20/20/31 ht=HP/Def/SpD" is why a recalculation moved a stat and the
        // stored IV did not. Only printed when the format has the field and something is set.
        if (pokemon.isHyperTrained())
        {
            static const char *statNames[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
            std::string ht;
            for (int index = 0; index < 6; ++index)
                if (pokemon.isHyperTrained(index))
                    ht += (ht.empty() ? "" : "/") + std::string(statNames[index]);
            out += " ht=" + ht;
        }
        out += " evs=" + numberText(pokemon.evHP()) + "/" + numberText(pokemon.evATK()) + "/" + numberText(pokemon.evDEF()) + "/" + numberText(pokemon.evSPA()) + "/" + numberText(pokemon.evSPD()) + "/" + numberText(pokemon.evSPE());
        if (pokemon.hasAwakeningValues())
        {
            out += " avs=" + numberText(pokemon.avHP()) + "/" + numberText(pokemon.avATK()) + "/" + numberText(pokemon.avDEF()) + "/" + numberText(pokemon.avSPA()) + "/" + numberText(pokemon.avSPD()) + "/" + numberText(pokemon.avSPE());
        }

        // Moves are logged by NAME as well as id: a move that is legal in one game and dummied in
        // another is a known way to produce a Bad Egg, and chasing that from bare numbers is slow.
        std::string moves;
        for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
        {
            const uint16_t moveId = pokemon.move(moveIndex);
            if (moveId == 0)
                continue;
            if (!moves.empty())
                moves += ",";
            const std::string name = safeName(Names::getMoveName(moveId));
            moves += name.empty() ? ("#" + numberText(moveId)) : name;
        }
        if (!moves.empty())
            out += " " + logField("moves", moves);

        return out;
    }
} // namespace Utils
