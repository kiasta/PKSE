#include "Pokemon/Evolution.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Names/NameLanguage.h"
#include "Pokemon/AbilityInfo.h"
#include "Pokemon/Pokemon.h"
#include "Utils/StringHelpers.h"

namespace Names
{
    // The species table read in a NAMED language rather than the display one, forward-declared the
    // way Conversion/Convert.cpp declares it: the table is declared in Trainer/Trainer.h, and a
    // Pokemon-layer source must not include the trainer layer to reach it.
    const char *getSpeciesNameLocalized(uint16_t speciesId, size_t languageIndex);
}

namespace Pokemon
{

    namespace
    {

        /**
         * True where the game stores a species name in UPPER CASE, which every game before
         * Generation 5 does -- an un-nicknamed Kadabra is KADABRA on a Game Boy, a GBA and a DS.
         * Japanese and Korean are excluded because their scripts have no case, exactly as PKHeX's
         * SpeciesName.GetSpeciesName1234 excludes them.
         */
        bool storesUpperCaseSpeciesNames(Enums::GameVersion gameGroup, uint8_t languageId)
        {
            if (Enums::getVersionGeneration(Enums::getGroupRepVersion(gameGroup)) > 4)
            {
                return false;
            }
            return languageId != static_cast<uint8_t>(Enums::LanguageID::Japanese) && languageId != static_cast<uint8_t>(Enums::LanguageID::Korean);
        }

    }

    std::u16string speciesNameAsStored(uint16_t speciesId, Enums::GameVersion gameGroup, uint8_t languageId)
    {
        const size_t languageIndex = Names::languageIndexFor(static_cast<Enums::LanguageID>(languageId));
        const std::u16string speciesName = Utils::utf8ToUtf16(std::string(Names::getSpeciesNameLocalized(speciesId, languageIndex)));
        if (!storesUpperCaseSpeciesNames(gameGroup, languageId))
        {
            return speciesName;
        }
        // PKHeX upper-cases invariantly, so an accented letter is upper-cased too, and then French
        // strips the accent off E and I (StringConverter4Util.StripDiacriticsFR4).
        constexpr char16_t latinSmallAGrave = u'\u00E0';
        constexpr char16_t latinSmallThorn = u'\u00FE';
        constexpr char16_t divisionSign = u'\u00F7';
        constexpr char16_t latinCapitalEGrave = u'\u00C8';
        constexpr char16_t latinCapitalIDiaeresis = u'\u00CF';
        std::u16string storedName = Utils::upperCaseLatinLetters(speciesName);
        const bool isFrench = languageId == static_cast<uint8_t>(Enums::LanguageID::French);
        for (char16_t &character : storedName)
        {
            if (character >= latinSmallAGrave && character <= latinSmallThorn && character != divisionSign)
            {
                character = static_cast<char16_t>(character - (latinSmallAGrave - u'\u00C0'));
            }
            const int frenchAccentOffset = character - latinCapitalEGrave;
            if (isFrench && frenchAccentOffset >= 0 && frenchAccentOffset < 3)
            {
                character = u'E';
            }
            else if (isFrench && character == latinCapitalIDiaeresis)
            {
                character = u'I';
            }
        }
        const int generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(gameGroup));
        if (generation >= 3)
        {
            // The DS and GBA games spell Farfetch'd with a straight apostrophe.
            for (char16_t &character : storedName)
            {
                if (character == u'\u2019')
                {
                    character = u'\'';
                }
            }
            return storedName;
        }
        // Gen 1/2 store MR.MIME with no space and the one-dot leader, not the full stop a user types.
        constexpr uint16_t mrMimeSpecies = 122;
        if (speciesId == mrMimeSpecies)
        {
            const size_t spaceIndex = storedName.find(u' ');
            if (spaceIndex != std::u16string::npos && spaceIndex > 0)
            {
                storedName[spaceIndex - 1] = u'\u2024';
                storedName.erase(spaceIndex, 1);
            }
        }
        return storedName;
    }

    namespace
    {

        /**
         * True when the nickname field is holding the species' own name rather than a name the
         * player chose -- the only case in which an evolution may rewrite it.
         *
         * TWO TESTS, AND BOTH ARE NEEDED. The flag alone is wrong for the three formats that do not
         * have one: Pokemon3FRLG and Pokemon3RSE never override isNicknamed(), so it answers false
         * for every Gen 3 record, and renaming on that alone would destroy a real Gen 3 nickname.
         * The string test alone is wrong for the formats that DO have a flag, where a Pokemon
         * deliberately nicknamed "Kadabra" keeps that name through an evolution.
         *
         * The comparison uppercases and compares, rather than going through normalizeGameBoyName():
         * that helper also strips every character it cannot uppercase, so two different Japanese
         * names both reduce to "" and compare equal. Gen 1 and Gen 2 need no such care here anyway
         * -- their isNicknamed() already does this comparison, and already answers true for a
         * Japanese record it has no table to check.
         */
        bool nicknameIsSpeciesName(const Pokemon &pokemon)
        {
            if (pokemon.isNicknamed())
            {
                return false;
            }
            const std::u16string storedNickname = Utils::upperCaseLatinLetters(pokemon.nickname());
            if (storedNickname.empty())
            {
                return false;
            }
            const std::u16string currentSpeciesName = Utils::upperCaseLatinLetters(speciesNameAsStored(pokemon.speciesID(), pokemon.getGameGroup(), pokemon.language()));
            return storedNickname == currentSpeciesName;
        }

        /**
         * Moves the ability to the same SLOT of the new species, so a hidden-ability Pokemon stays
         * hidden. The slot index, not the ability id, is what survives an evolution -- the ids are
         * per species and almost never match. Mirrors what the species picker does.
         */
        void carryAbilitySlot(Pokemon &pokemon, uint8_t previousAbilityNumber, uint16_t destinationSpeciesId, uint8_t destinationFormId, Enums::GameVersion gameGroup)
        {
            if (!pokemon.hasAbility())
            {
                return;
            }
            const AbilitySlots slots = getAbilitySlots(destinationSpeciesId, destinationFormId, gameGroup);
            // abilityNumber is a BITMASK in the modern formats: 1 = slot 1, 2 = slot 2, 4 = hidden.
            int wantedSlot = (previousAbilityNumber == 4) ? 2 : (previousAbilityNumber == 2) ? 1 : 0;
            if (wantedSlot >= slots.count || slots.slot[wantedSlot] == 0)
            {
                wantedSlot = 0;
            }
            if (slots.slot[wantedSlot] == 0)
            {
                return;
            }
            pokemon.setAbility(slots.slot[wantedSlot]);
            if (!Enums::isGen3Group(gameGroup))
            {
                pokemon.setAbilityNumber(static_cast<uint8_t>(wantedSlot == 2 ? 4 : wantedSlot + 1));
            }
        }
    }

    TradeEvolutionOffer getTradeEvolutionOffer(const Pokemon &pokemon)
    {
        TradeEvolutionOffer offer;
        if (pokemon.speciesID() == 0)
        {
            return offer;
        }
        offer.candidateCount = getTradeEvolutions(pokemon.getGameGroup(), pokemon.speciesID(), pokemon.form(), offer.candidates, TRADE_EVOLUTIONS_MAX_PER_SPECIES);
        // WHICH ROW THE ITEM NAMES, not whether the evolution may happen. A held item of 0 is
        // "holding nothing", so it must never match a row -- a HeldItem row with no item id would
        // be a table bug, and this is what stops it becoming a wrong evolution.
        const uint16_t heldItemId = pokemon.hasHeldItem() ? pokemon.heldItem() : 0;
        for (int candidateIndex = 0; heldItemId != 0 && candidateIndex < offer.candidateCount; ++candidateIndex)
        {
            const TradeEvolution &candidate = offer.candidates[candidateIndex];
            if (candidate.trigger == TradeEvolutionTrigger::HeldItem && candidate.requiredItemId == heldItemId)
            {
                offer.heldItemIndex = candidateIndex;
                break;
            }
        }
        return offer;
    }

    bool applyTradeEvolution(Pokemon &pokemon, int candidateIndex)
    {
        const TradeEvolutionOffer offer = getTradeEvolutionOffer(pokemon);
        if (candidateIndex < 0 || candidateIndex >= offer.candidateCount)
        {
            return false;
        }
        const TradeEvolution &evolution = offer.candidates[candidateIndex];
        const Enums::GameVersion gameGroup = pokemon.getGameGroup();

        // Asked BEFORE the species changes: the question is whether the nickname is the name of
        // what this Pokemon is now, and a moment later it will not be.
        const bool renameNickname = nicknameIsSpeciesName(pokemon);
        const uint8_t previousAbilityNumber = pokemon.abilityNumber();

        pokemon.setSpecies(evolution.destinationSpeciesId); // first: drives the base-stat lookup
        pokemon.setForm(evolution.destinationFormId);
        carryAbilitySlot(pokemon, previousAbilityNumber, evolution.destinationSpeciesId, evolution.destinationFormId, gameGroup);

        // The item is destroyed by the evolution in every game that has one of these; it does not
        // go back to the bag. ONLY THE ITEM THE ROW ASKS FOR: the evolution runs whether or not it
        // is held, so clearing the field unconditionally would destroy an unrelated item a Scyther
        // happened to be carrying.
        if (evolution.trigger == TradeEvolutionTrigger::HeldItem && pokemon.hasHeldItem() && pokemon.heldItem() == evolution.requiredItemId)
        {
            pokemon.setHeldItem(0);
        }

        if (renameNickname)
        {
            pokemon.setNickname(speciesNameAsStored(evolution.destinationSpeciesId, gameGroup, pokemon.language()));
        }

        // EXP and level are deliberately untouched -- see the header. recalculateStats() reads the
        // level that is already there and refills the battle-stat tail from the new base stats.
        pokemon.recalculateStats();
        pokemon.refreshChecksum();
        return true;
    }
}
