// Port of PKHeX PKHeX.Core/Legality/Verifiers/FormVerifier.cs and FormArgumentVerifier.cs: form and
// form argument.
//
// Not checked, for want of PKHeX's move-learn engine: Legends: Z-A Hangry Morpeko's level, Primeape's
// Rage Fist, and the mega stone a Z-A party Mega must hold. Koraidon/Miraidon's ride form argument
// needs PKHeX's StarterBoxLegend flag and the Ride slot, neither of which PKSE has.
#include "Legality/Verifiers/Verifiers.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/RNG/ToxtricityUtil.h"
#include "Legality/Encounters/EncounterTemplateType.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Verifiers/BreedingRules.h"
#include "Legality/Verifiers/PersonalTableGroup.h"
#include "Legality/Verifiers/VivillonPatternTable.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t FARFETCHD_SPECIES = 83;
        constexpr uint16_t EEVEE_SPECIES = 133;
        constexpr uint16_t PRIMEAPE_SPECIES = 57;
        constexpr uint16_t QWILFISH_SPECIES = 211;
        constexpr uint16_t UNOWN_SPECIES = 201;
        constexpr uint16_t STANTLER_SPECIES = 234;
        constexpr uint16_t MOTHIM_SPECIES = 414;
        constexpr uint16_t ROTOM_SPECIES = 479;
        constexpr uint16_t DIALGA_SPECIES = 483;
        constexpr uint16_t PALKIA_SPECIES = 484;
        constexpr uint16_t GIRATINA_SPECIES = 487;
        constexpr uint16_t SHAYMIN_SPECIES = 492;
        constexpr uint16_t ARCEUS_SPECIES = 493;
        constexpr uint16_t BASCULIN_SPECIES = 550;
        constexpr uint16_t YAMASK_SPECIES = 562;
        constexpr uint16_t BISHARP_SPECIES = 625;
        constexpr uint16_t KELDEO_SPECIES = 647;
        constexpr uint16_t GENESECT_SPECIES = 649;
        constexpr uint16_t SCATTERBUG_SPECIES = 664;
        constexpr uint16_t SPEWPA_SPECIES = 665;
        constexpr uint16_t VIVILLON_SPECIES = 666;
        constexpr uint16_t FURFROU_SPECIES = 676;
        constexpr uint16_t MEOWSTIC_SPECIES = 678;
        constexpr uint16_t HOOPA_SPECIES = 720;
        constexpr uint16_t ROCKRUFF_SPECIES = 744;
        constexpr uint16_t LYCANROC_SPECIES = 745;
        constexpr uint16_t SILVALLY_SPECIES = 773;
        constexpr uint16_t TOXTRICITY_SPECIES = 849;
        constexpr uint16_t RUNERIGUS_SPECIES = 867;
        constexpr uint16_t ALCREMIE_SPECIES = 869;
        constexpr uint16_t SIRFETCHD_SPECIES = 865;
        constexpr uint16_t WYRDEER_SPECIES = 899;
        constexpr uint16_t OVERQWIL_SPECIES = 904;
        constexpr uint16_t BASCULEGION_SPECIES = 902;
        constexpr uint16_t ANNIHILAPE_SPECIES = 979;
        constexpr uint16_t KINGAMBIT_SPECIES = 983;
        constexpr uint16_t GIMMIGHOUL_SPECIES = 999;
        constexpr uint16_t GHOLDENGO_SPECIES = 1000;
        constexpr uint16_t KORAIDON_SPECIES = 1007;
        constexpr uint16_t MIRAIDON_SPECIES = 1008;
        constexpr uint16_t OGERPON_SPECIES = 1017;

        constexpr uint16_t SECRET_SWORD_MOVE = 548;
        constexpr uint16_t BARB_BARRAGE_MOVE = 839;
        constexpr uint16_t GRISEOUS_ORB_ITEM = 112;
        constexpr uint16_t ADAMANT_CRYSTAL_ITEM = 1777;
        constexpr uint16_t LUSTROUS_GLOBE_ITEM = 1778;
        constexpr uint16_t GRISEOUS_CORE_ITEM = 1779;
        constexpr uint16_t WELLSPRING_MASK_ITEM = 2407;
        constexpr uint16_t HEARTHFLAME_MASK_ITEM = 2408;
        constexpr uint16_t CORNERSTONE_MASK_ITEM = 2406;
        constexpr uint8_t VIVILLON_FANCY_FORM = 18;
        constexpr uint8_t ALCREMIE_DECORATION_RIBBON = 6;
        constexpr uint8_t VERSION_SUN = 30;
        constexpr uint8_t VERSION_MOON = 31;
        constexpr uint8_t VERSION_POKEMON_GO = 34;
        constexpr uint16_t LOCATION_GO_HOME = 30012;

        // PKHeX FormItem.Arceus_PlateIDs / Arceus_ZCrystal, in form order.
        constexpr uint16_t ARCEUS_PLATES[] = {303, 306, 304, 305, 309, 308, 310, 313, 298, 299, 301, 300, 307, 302, 311, 312, 644};
        constexpr uint16_t ARCEUS_Z_CRYSTALS[] = {782, 785, 783, 784, 788, 787, 789, 792, 777, 778, 780, 779, 786, 781, 790, 791, 793};

        int findIndex(const uint16_t *values, size_t valueCount, uint16_t searchedValue)
        {
            for (size_t valueIndex = 0; valueIndex < valueCount; valueIndex++)
            {
                if (values[valueIndex] == searchedValue)
                {
                    return static_cast<int>(valueIndex);
                }
            }
            return -1;
        }

        /// PKHeX FormItem.GetFormArceus. Gen 4 numbers the ??? type as form 9, after which every plate shifts up.
        uint8_t getArceusForm(uint16_t itemId, int formatGeneration)
        {
            if (itemId >= 777 && itemId <= 793)
            {
                return static_cast<uint8_t>(findIndex(ARCEUS_Z_CRYSTALS, std::size(ARCEUS_Z_CRYSTALS), itemId) + 1);
            }
            if ((itemId >= 298 && itemId <= 313) || itemId == 644)
            {
                const uint8_t plateForm = static_cast<uint8_t>(findIndex(ARCEUS_PLATES, std::size(ARCEUS_PLATES), itemId) + 1);
                return (formatGeneration == 4 && plateForm >= 9) ? plateForm + 1 : plateForm;
            }
            return 0;
        }

        uint8_t getSilvallyForm(uint16_t itemId)
        {
            return (itemId >= 904 && itemId <= 920) ? static_cast<uint8_t>(itemId - 903) : 0;
        }

        uint8_t getGenesectForm(uint16_t itemId)
        {
            return (itemId >= 116 && itemId <= 119) ? static_cast<uint8_t>(itemId - 115) : 0;
        }

        uint8_t getOgerponForm(uint16_t itemId)
        {
            switch (itemId)
            {
            case WELLSPRING_MASK_ITEM:
                return 1;
            case HEARTHFLAME_MASK_ITEM:
                return 2;
            case CORNERSTONE_MASK_ITEM:
                return 3;
            default:
                return 0;
            }
        }

        /// PKHeX EntityPID.GetUnownForm3.
        uint8_t getUnownForm3(uint32_t PID)
        {
            const uint32_t letterValue = ((PID & 0x3000000) >> 18) | ((PID & 0x30000) >> 12) | ((PID & 0x300) >> 6) | (PID & 0x3);
            return static_cast<uint8_t>(letterValue % 28);
        }

        /// PKHeX Vivillon3DS.IsPatternValid.
        bool isVivillonPatternValid(uint8_t formId, uint8_t consoleRegion)
        {
            if (formId >= VIVILLON_PATTERN_REGIONS_LENGTH || consoleRegion >= 8)
            {
                return false;
            }
            return (VIVILLON_PATTERN_REGIONS[formId] & (1 << consoleRegion)) != 0;
        }

        /// PKHeX Vivillon3DS.IsPatternNative: the pattern the save's country and region produce.
        bool isVivillonPatternNative(uint8_t formId, uint8_t country, uint8_t region)
        {
            if (formId > VIVILLON_MAX_WILD_FORM || country >= VIVILLON_COUNTRY_PATTERN_LENGTH)
            {
                return false;
            }
            const uint8_t countryPattern = VIVILLON_COUNTRY_PATTERN[country];
            if (countryPattern != 0)
            {
                return formId == countryPattern;
            }
            const uint16_t key = static_cast<uint16_t>(country << 8 | region);
            const uint16_t *keysEnd = VIVILLON_COUNTRY_REGION_KEYS + VIVILLON_COUNTRY_REGION_KEYS_LENGTH;
            const uint16_t *found = std::lower_bound(VIVILLON_COUNTRY_REGION_KEYS, keysEnd, key);
            if (found == keysEnd || *found != key)
            {
                return false;
            }
            return formId == VIVILLON_COUNTRY_REGION_PATTERN[found - VIVILLON_COUNTRY_REGION_KEYS];
        }

        /// PKHeX FormInfo.IsValidOutOfBoundsForm: forms a species keeps that its personal entry does not list.
        bool isValidOutOfBoundsForm(uint16_t speciesId, uint8_t formId, int generation)
        {
            switch (speciesId)
            {
            case UNOWN_SPECIES:
                return formId < (generation == 2 ? 26 : 28);
            case MOTHIM_SPECIES:
                return formId < 3;
            case SCATTERBUG_SPECIES:
            case SPEWPA_SPECIES:
                return formId <= VIVILLON_MAX_WILD_FORM;
            default:
                return false;
            }
        }

        /// PKHeX FormInfo.IsBattleOnlyForm: BattleMegas and BattleForms.
        bool isBattleOnlyForm(uint16_t speciesId, uint8_t formId)
        {
            return Pokemon::isMegaForm(speciesId, formId) || Pokemon::isBattleForm(speciesId, formId);
        }

        bool hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId)
        {
            for (int moveIndex = 0; moveIndex < 4; moveIndex++)
            {
                if (pokemon.move(moveIndex) == moveId)
                {
                    return true;
                }
            }
            return false;
        }

        bool hasRelearnMove(const Pokemon::Pokemon &pokemon, uint16_t moveId)
        {
            for (int moveIndex = 0; moveIndex < 4; moveIndex++)
            {
                if (pokemon.relearnMove(moveIndex) == moveId)
                {
                    return true;
                }
            }
            return false;
        }

        struct FormAnalysis
        {
            const AnalysisContext &context;
            const Evolutions::EvolutionHistory &history;
            const int encounterGeneration;
            const uint16_t encounterSpecies;

            void addInvalid(ResultCode code, const std::string &firstArgument = "") const { context.add(Severity::Invalid, CheckIdentifier::Form, code, firstArgument); }

            /// PKHeX LegalityAnalysis.IsStoredSlot(Party): a Pokemon file or an unknown slot counts.
            bool isInParty() const { return context.options.slotType != StorageSlotType::Box; }

            void verifyVivillonRegion(uint8_t formId) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (!pokemon.hasConsoleRegion())
                {
                    return;
                }
                if (!isVivillonPatternValid(formId, pokemon.consoleRegion()))
                {
                    addInvalid(ResultCode::FormVivillonInvalid);
                    return;
                }
                if (!isVivillonPatternNative(formId, pokemon.consoleCountry(), pokemon.consoleSubregion()))
                {
                    context.add(Severity::Warning, CheckIdentifier::Form, ResultCode::FormVivillonNonNative);
                }
            }

            /// PKHeX FormVerifier.VerifyForm. True when the form was decided before the battle-form check.
            bool verifyFormSpecies() const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const uint16_t speciesId = pokemon.speciesID();
                const uint8_t formId = pokemon.form();
                const uint16_t heldItem = pokemon.heldItem();
                const int formatGeneration = context.formatGeneration;
                const ActiveTrainer *activeTrainer = context.options.activeTrainer;
                switch (speciesId)
                {
                case PIKACHU_SPECIES:
                    if (encounterGeneration == 6)
                    {
                        // A Cosplay Pikachu cannot leave Omega Ruby/Alpha Sapphire.
                        if (formId != 0 && formatGeneration != 6)
                        {
                            addInvalid(ResultCode::TransferBad);
                            return true;
                        }
                        return false;
                    }
                    // Let's Go cannot obtain the other game's partner.
                    if (formId != 0 && activeTrainer != nullptr && activeTrainer->gameVersion == Enums::GameVersion::GE)
                    {
                        addInvalid(ResultCode::FormBattle);
                        return true;
                    }
                    return false;
                case EEVEE_SPECIES:
                    if (formId != 0 && activeTrainer != nullptr && activeTrainer->gameVersion == Enums::GameVersion::GP)
                    {
                        addInvalid(ResultCode::FormBattle);
                        return true;
                    }
                    return false;
                case UNOWN_SPECIES:
                    if (encounterGeneration == 2 && formId >= 26)
                    {
                        addInvalid(ResultCode::FormInvalidRangeAtMost, "25");
                        return true;
                    }
                    if (encounterGeneration == 3)
                    {
                        const uint8_t expectedForm = getUnownForm3(pokemon.encryptionConstant());
                        if (expectedForm != formId)
                        {
                            addInvalid(ResultCode::FormInvalidExpect, std::to_string(expectedForm));
                            return true;
                        }
                    }
                    return false;
                case DIALGA_SPECIES:
                case PALKIA_SPECIES:
                case GIRATINA_SPECIES:
                {
                    // Legends: Arceus changes these forms with key items.
                    if (formId > 0 && context.formatGroup == Enums::GameVersion::PLA)
                    {
                        return false;
                    }
                    uint16_t formItem = speciesId == DIALGA_SPECIES ? ADAMANT_CRYSTAL_ITEM : (speciesId == PALKIA_SPECIES ? LUSTROUS_GLOBE_ITEM : GRISEOUS_CORE_ITEM);
                    if (formatGeneration < 9)
                    {
                        if (speciesId != GIRATINA_SPECIES)
                        {
                            return false;
                        }
                        formItem = GRISEOUS_ORB_ITEM;
                    }
                    if ((formId == 1) != (heldItem == formItem))
                    {
                        addInvalid(ResultCode::FormItemInvalid);
                        return true;
                    }
                    return false;
                }
                case ARCEUS_SPECIES:
                    if (formId > 0 && context.formatGroup == Enums::GameVersion::PLA)
                    {
                        return false;
                    }
                    if (getArceusForm(heldItem, formatGeneration) != formId)
                    {
                        addInvalid(ResultCode::FormItemInvalid);
                    }
                    return true;
                case KELDEO_SPECIES:
                    // Gen 5's tutor and transfer could leave the form and move apart; Gen 8 forces them together.
                    if (encounterGeneration != 5 || formatGeneration >= 8)
                    {
                        if ((formId == 1) != hasMove(pokemon, SECRET_SWORD_MOVE))
                        {
                            context.add(Severity::Invalid, CheckIdentifier::Form, ResultCode::MoveKeldeoMismatch);
                            return true;
                        }
                    }
                    return false;
                case GENESECT_SPECIES:
                    if (getGenesectForm(heldItem) != formId)
                    {
                        addInvalid(ResultCode::FormItemInvalid);
                    }
                    return true;
                case FURFROU_SPECIES:
                    if (formatGeneration == 6 && formId != 0 && !isInParty())
                    {
                        addInvalid(ResultCode::FormParty);
                        return true;
                    }
                    if (pokemon.isEgg() && formId != 0)
                    {
                        addInvalid(ResultCode::EggSpecies);
                        return true;
                    }
                    break;
                case SCATTERBUG_SPECIES:
                case SPEWPA_SPECIES:
                case VIVILLON_SPECIES:
                {
                    const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                    if (encounterMatch != nullptr && encounterMatch->contextGroup() == Enums::GameVersion::SV)
                    {
                        // PKHeX: a Poke Ball pattern is the encounter's own form, and only Fancy hatches.
                        if (formId > VIVILLON_FANCY_FORM && encounterMatch->formId() != formId)
                        {
                            addInvalid(ResultCode::FormVivillonEventPre);
                            return true;
                        }
                        if (formId != VIVILLON_FANCY_FORM && encounterMatch->templateType() == Encounters::EncounterTemplateType::Egg9)
                        {
                            addInvalid(ResultCode::FormVivillonNonNative);
                            return true;
                        }
                        return false;
                    }
                    if (formId > VIVILLON_MAX_WILD_FORM)
                    {
                        if (speciesId != VIVILLON_SPECIES)
                        {
                            addInvalid(ResultCode::FormVivillonEventPre);
                            return true;
                        }
                        if (encounterMatch != nullptr && !Encounters::isMysteryGift(encounterMatch->templateType()))
                        {
                            addInvalid(ResultCode::FormVivillonInvalid);
                        }
                        return true;
                    }
                    verifyVivillonRegion(formId);
                    return false;
                }
                case MEOWSTIC_SPECIES:
                    if ((formId & 1) != pokemon.gender())
                    {
                        addInvalid(ResultCode::GenderInvalidNone);
                        return true;
                    }
                    return false;
                case SILVALLY_SPECIES:
                    if (getSilvallyForm(heldItem) != formId)
                    {
                        addInvalid(ResultCode::FormItemInvalid);
                    }
                    return true;
                case ROCKRUFF_SPECIES:
                case LYCANROC_SPECIES:
                {
                    // Own Tempo Rockruff and Dusk Lycanroc do not exist in Sun/Moon.
                    const uint8_t ultraOnlyForm = speciesId == ROCKRUFF_SPECIES ? 1 : 2;
                    const bool isSunMoon = pokemon.originGame() == VERSION_SUN || pokemon.originGame() == VERSION_MOON;
                    if (encounterGeneration == 7 && formId == ultraOnlyForm && isSunMoon)
                    {
                        addInvalid(ResultCode::FormInvalidGame);
                        return true;
                    }
                    return false;
                }
                case TOXTRICITY_SPECIES:
                    if (encounterSpecies == TOXTRICITY_SPECIES && formId != RNG::ToxtricityUtil::getAmpLowKeyResult(pokemon.nature()))
                    {
                        addInvalid(ResultCode::FormInvalidNature);
                        return true;
                    }
                    return false;
                case OGERPON_SPECIES:
                    if ((formId & 3) != getOgerponForm(heldItem))
                    {
                        addInvalid(ResultCode::FormItemInvalid);
                        return true;
                    }
                    return false;
                case ROTOM_SPECIES:
                    if (pokemon.isEgg() && formId != 0)
                    {
                        addInvalid(ResultCode::EggSpecies);
                        return true;
                    }
                    return false;
                default:
                    break;
                }
                // Forms that exist only in the party.
                const bool isPartyOnlySpecies = speciesId == SHAYMIN_SPECIES || speciesId == FURFROU_SPECIES || speciesId == HOOPA_SPECIES;
                if (isPartyOnlySpecies && formId != 0 && !isInParty() && formatGeneration <= 6)
                {
                    addInvalid(ResultCode::FormParty);
                    return true;
                }
                return false;
            }

            void verifyForm() const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const uint16_t speciesId = pokemon.speciesID();
                const uint8_t formId = pokemon.form();
                const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(getPersonalTableGroup(context.formatGroup), speciesId, formId);
                const int formCount = record.formCount;
                if (formCount <= 1 && formId == 0)
                {
                    return;
                }
                const bool isFormWithinRange = formId == 0 || formId < formCount;
                if (!isFormWithinRange && !isValidOutOfBoundsForm(speciesId, formId, encounterGeneration))
                {
                    addInvalid(ResultCode::FormInvalidRangeAtMost, std::to_string(formCount - 1));
                    return;
                }
                if (verifyFormSpecies())
                {
                    return;
                }
                if (!isBattleOnlyForm(speciesId, formId))
                {
                    return;
                }
                if (context.formatGroup != Enums::GameVersion::ZA)
                {
                    addInvalid(ResultCode::FormBattle);
                    return;
                }
                // PKHeX VerifyBattleForms9a: Legends: Z-A keeps a battle form in the party.
                if (!isInParty())
                {
                    addInvalid(ResultCode::FormBattle);
                }
            }

            /// PKHeX FormArgumentVerifier.IsFormArgumentDayCounterValid.
            bool isFormArgumentDayCounterValid(uint32_t maxSeed, bool canRefresh) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const uint32_t remain = pokemon.formArgumentRemain();
                const uint32_t elapsed = pokemon.formArgumentElapsed();
                const uint32_t maximumElapsed = pokemon.formArgumentMaximum();
                if (canRefresh)
                {
                    if (maximumElapsed < elapsed || remain + elapsed < maxSeed)
                    {
                        return false;
                    }
                }
                else if (maximumElapsed != 0 || remain + elapsed != maxSeed)
                {
                    return false;
                }
                return remain <= maxSeed && remain != 0;
            }

            void checkHoopa(uint32_t formArgument) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (formArgument == 0)
                {
                    // Neither Legends: Z-A nor Scarlet/Violet sets it on a form change.
                    if (history.hasVisited(Enums::GameVersion::ZA) || history.hasVisited(Enums::GameVersion::SV))
                    {
                        return;
                    }
                }
                else if (history.hasVisited(Enums::GameVersion::SM) && isFormArgumentDayCounterValid(3, false))
                {
                    return;
                }
                if (context.formatGeneration == 6)
                {
                    if (formArgument != 0)
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                        return;
                    }
                    if (pokemon.formArgumentElapsed() + pokemon.formArgumentRemain() != 3)
                    {
                        addInvalid(ResultCode::FormArgumentInvalid);
                    }
                    return;
                }
                addInvalid(ResultCode::FormArgumentInvalid);
            }

            void checkFurfrou(uint32_t formArgument) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const uint8_t formId = pokemon.form();
                if (context.formatGeneration == 6)
                {
                    // Depositing drops the form but forgets the argument.
                    if (formArgument > 0xFF)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "255");
                        return;
                    }
                    const uint32_t remain = pokemon.formArgumentRemain();
                    const uint32_t elapsed = pokemon.formArgumentElapsed();
                    if (formId != 0)
                    {
                        if (remain + elapsed < 5 || elapsed != formArgument)
                        {
                            addInvalid(ResultCode::FormArgumentInvalid);
                        }
                        return;
                    }
                    if (remain != 0 || elapsed != 0)
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    return;
                }
                const bool isUSUMFormat = context.formatGroup == Enums::GameVersion::SM || context.formatGroup == Enums::GameVersion::USUM;
                if (isUSUMFormat)
                {
                    // Bank reverts the form but not the argument of a Gen 6 Furfrou.
                    if (encounterGeneration == 6 && formId == 0 && formArgument <= 0xFF)
                    {
                        return;
                    }
                    if (formId == 0 && formArgument == 0)
                    {
                        return;
                    }
                    if (!isFormArgumentDayCounterValid(5, true))
                    {
                        addInvalid(ResultCode::FormArgumentInvalid);
                    }
                    return;
                }
                const bool isGoHome = pokemon.originGame() == VERSION_POKEMON_GO && pokemon.metLocation() == LOCATION_GO_HOME;
                if (isGoHome && formArgument == 0)
                {
                    return; // GO transfers forget to set it
                }
                if (context.formatGroup == Enums::GameVersion::ZA)
                {
                    if (encounterGeneration == 6 && formId == 0 && formArgument <= 0xFF)
                    {
                        return;
                    }
                    // Legends: Z-A sets a trim to 5 days and never counts it down.
                    if ((formArgument == 5) == (formId != 0))
                    {
                        return;
                    }
                    if (formId == 0 && encounterGeneration <= 7 && isFormArgumentDayCounterValid(5, true))
                    {
                        return;
                    }
                }
                addInvalid(ResultCode::FormArgumentInvalid);
            }

            /// PKHeX VerifyFormArgumentRange: zero unless it evolved from the species that counts it.
            void verifyFormArgumentRange(uint16_t countingSpecies, uint32_t formArgument, uint32_t minimum, uint32_t maximum) const
            {
                // PKHeX reports FormArgumentNotAllowed when the encounter is the evolved species itself.
                // PKSE picks the first matching row, which can be a wild evolved one where PKHeX
                // matched the pre-evolution, so a non-zero argument there stays unreported.
                if (encounterSpecies == countingSpecies)
                {
                    return;
                }
                if (formArgument < minimum)
                {
                    addInvalid(ResultCode::FormArgumentAtLeast, std::to_string(minimum));
                    return;
                }
                if (formArgument > maximum)
                {
                    addInvalid(ResultCode::FormArgumentAtMost, std::to_string(maximum));
                }
            }

            /// A counter that a Pokemon still in its counting form may hold up to 9999.
            void verifyCounter(uint32_t formArgument) const
            {
                if (formArgument != 0 && context.pokemon.isEgg())
                {
                    addInvalid(ResultCode::FormArgumentNotAllowed);
                    return;
                }
                if (formArgument > 9999)
                {
                    addInvalid(ResultCode::FormArgumentAtMost, "9999");
                }
            }

            void checkGimmighoulCoins(uint32_t formArgument, bool isEvolved) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                // Only Scarlet/Violet copies the coin count in, on a level up; 999 triggers the evolution.
                if (formArgument > 999)
                {
                    addInvalid(ResultCode::FormArgumentAtMost, "999");
                    return;
                }
                if (Evolutions::getEntityCurrentLevel(pokemon) == pokemon.metLevel())
                {
                    addInvalid(ResultCode::FormArgumentNotAllowed);
                    return;
                }
                if (isEvolved && !history.hasVisited(Enums::GameVersion::ZA) && formArgument != 999)
                {
                    addInvalid(ResultCode::FormArgumentAtLeast, "999");
                }
            }

            void checkQwilfish(uint32_t formArgument) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (formArgument == 0)
                {
                    return;
                }
                if (formArgument > 9999)
                {
                    addInvalid(ResultCode::FormArgumentAtMost, "9999");
                    return;
                }
                if (pokemon.isEgg())
                {
                    addInvalid(ResultCode::FormArgumentNotAllowed);
                    return;
                }
                const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
                if (history.hasVisited(Enums::GameVersion::PLA))
                {
                    if (currentLevel < 25)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "0");
                    }
                    return;
                }
                if (history.hasVisited(Enums::GameVersion::ZA))
                {
                    const bool hasTracker = pokemon.hasHomeTracker() && pokemon.homeTracker() != 0;
                    if (currentLevel < (hasTracker ? 15 : 28))
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "0");
                    }
                    return;
                }
                addInvalid(ResultCode::FormArgumentAtMost, "0");
            }

            void checkOverqwil(uint32_t formArgument) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (formArgument == 0 && encounterSpecies == OVERQWIL_SPECIES)
                {
                    return;
                }
                if (formArgument > 9999)
                {
                    addInvalid(ResultCode::FormArgumentAtMost, "9999");
                    return;
                }
                const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
                const bool hasVisitedLegendsArceus = history.hasVisited(Enums::GameVersion::PLA);
                if (history.hasVisited(Enums::GameVersion::SV))
                {
                    // Scarlet/Violet evolves it by knowing Barb Barrage alone.
                    const bool knowsBarbBarrage = hasMove(pokemon, BARB_BARRAGE_MOVE) || hasRelearnMove(pokemon, BARB_BARRAGE_MOVE);
                    if (knowsBarbBarrage || currentLevel >= (hasVisitedLegendsArceus ? 15 : 28))
                    {
                        if (formArgument == 0 || hasVisitedLegendsArceus || history.hasVisited(Enums::GameVersion::ZA))
                        {
                            return;
                        }
                    }
                }
                if (hasVisitedLegendsArceus && formArgument >= 20 && currentLevel >= 25)
                {
                    return;
                }
                if (history.hasVisited(Enums::GameVersion::ZA) && formArgument >= 20 && currentLevel >= 28)
                {
                    return;
                }
                context.add(Severity::Invalid, CheckIdentifier::Form, ResultCode::EvolutionInvalid);
            }

            /// PKHeX FormArgumentVerifier.VerifyFormArgumentNone.
            void verifyFormArgumentNone(uint32_t formArgument) const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                const bool isBaseFurfrou = pokemon.speciesID() == FURFROU_SPECIES && pokemon.form() == 0;
                if (context.formatGeneration != 6)
                {
                    if (formArgument != 0 && !(isBaseFurfrou && (formArgument & ~0xFF0000u) == 0))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    return;
                }
                if (formArgument != 0)
                {
                    if (!(isBaseFurfrou && (formArgument & ~0xFFu) == 0))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    return;
                }
                if (pokemon.formArgumentRemain() != 0 || pokemon.formArgumentElapsed() != 0)
                {
                    addInvalid(ResultCode::FormArgumentNotAllowed);
                }
            }

            /// PKHeX FormArgumentVerifier.VerifyFormArgument.
            void verifyFormArgument() const
            {
                const Pokemon::Pokemon &pokemon = context.pokemon;
                if (!pokemon.hasFormArgument())
                {
                    return;
                }
                const uint32_t formArgument = pokemon.formArgument();
                const uint32_t unusedMask = context.formatGeneration == 6 ? 0xFFFFFF00u : 0xFF000000u;
                if ((formArgument & unusedMask) != 0)
                {
                    addInvalid(ResultCode::FormArgumentAtMost);
                    return;
                }
                const uint8_t formId = pokemon.form();
                switch (pokemon.speciesID())
                {
                case FURFROU_SPECIES:
                    checkFurfrou(formArgument);
                    return;
                case HOOPA_SPECIES:
                    if (formId == 1)
                    {
                        checkHoopa(formArgument);
                        return;
                    }
                    break;
                case YAMASK_SPECIES:
                case BASCULIN_SPECIES:
                    if ((pokemon.speciesID() == YAMASK_SPECIES && formId == 1) || (pokemon.speciesID() == BASCULIN_SPECIES && formId == 2))
                    {
                        verifyCounter(formArgument);
                        return;
                    }
                    break;
                case FARFETCHD_SPECIES:
                    if (formId == 1)
                    {
                        if (formArgument == 0)
                        {
                            return;
                        }
                        if (formArgument > 9999)
                        {
                            addInvalid(ResultCode::FormArgumentAtMost, "9999");
                            return;
                        }
                        if (pokemon.isEgg())
                        {
                            addInvalid(ResultCode::FormArgumentNotAllowed);
                            return;
                        }
                        if (!history.hasVisited(Enums::GameVersion::ZA))
                        {
                            addInvalid(ResultCode::FormArgumentAtMost, "0");
                        }
                        return;
                    }
                    break;
                case SIRFETCHD_SPECIES:
                    if (formArgument == 0)
                    {
                        if (encounterSpecies == SIRFETCHD_SPECIES || history.hasVisited(Enums::GameVersion::SV) || history.hasVisited(Enums::GameVersion::SWSH))
                        {
                            return;
                        }
                    }
                    else if (formArgument > 9999)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "9999");
                        return;
                    }
                    if (history.hasVisited(Enums::GameVersion::ZA) && formArgument >= 3)
                    {
                        return;
                    }
                    addInvalid(ResultCode::FormArgumentAtMost, "0");
                    return;
                case QWILFISH_SPECIES:
                    if (formId == 1)
                    {
                        checkQwilfish(formArgument);
                        return;
                    }
                    break;
                case OVERQWIL_SPECIES:
                    checkOverqwil(formArgument);
                    return;
                case STANTLER_SPECIES:
                    if (formArgument != 0 && pokemon.isEgg())
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    else if (formArgument != 0 && Evolutions::getEntityCurrentLevel(pokemon) < 31)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "0");
                    }
                    else if (formArgument > 9999)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "9999");
                    }
                    else if (formArgument != 0 && !history.hasVisitedAs(Enums::GameVersion::PLA, STANTLER_SPECIES))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    return;
                case PRIMEAPE_SPECIES:
                    if (formArgument > 9999)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "9999");
                        return;
                    }
                    // Whether it could have learned Rage Fist needs PKHeX's move sources; a visit to
                    // a game that counts Rage Fist uses is the most PKSE can ask.
                    if (formArgument != 0 && !history.hasVisitedAs(Enums::GameVersion::SV, PRIMEAPE_SPECIES) && !history.hasVisitedAs(Enums::GameVersion::ZA, PRIMEAPE_SPECIES))
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "0");
                    }
                    return;
                case BISHARP_SPECIES:
                    if (formArgument > 9999)
                    {
                        addInvalid(ResultCode::FormArgumentAtMost, "9999");
                    }
                    else if (formArgument != 0 && !history.hasVisitedAs(Enums::GameVersion::SV, BISHARP_SPECIES))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                    }
                    return;
                case GIMMIGHOUL_SPECIES:
                    if (formArgument == 0)
                    {
                        return;
                    }
                    if (!history.hasVisited(Enums::GameVersion::SV))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                        return;
                    }
                    checkGimmighoulCoins(formArgument, false);
                    return;
                case GHOLDENGO_SPECIES:
                    if (encounterSpecies == GHOLDENGO_SPECIES)
                    {
                        if (formArgument != 0)
                        {
                            addInvalid(ResultCode::FormArgumentNotAllowed);
                        }
                        return;
                    }
                    if (history.hasVisited(Enums::GameVersion::ZA) && formArgument == 0)
                    {
                        return;
                    }
                    if (!history.hasVisited(Enums::GameVersion::SV))
                    {
                        addInvalid(ResultCode::FormArgumentNotAllowed);
                        return;
                    }
                    checkGimmighoulCoins(formArgument, true);
                    return;
                case RUNERIGUS_SPECIES:
                    verifyFormArgumentRange(RUNERIGUS_SPECIES, formArgument, 49, 9999);
                    return;
                case ALCREMIE_SPECIES:
                    verifyFormArgumentRange(ALCREMIE_SPECIES, formArgument, 0, ALCREMIE_DECORATION_RIBBON);
                    return;
                case WYRDEER_SPECIES:
                    if (encounterSpecies != WYRDEER_SPECIES && Evolutions::getEntityCurrentLevel(pokemon) < 31)
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Form, ResultCode::EvolutionInvalid);
                        return;
                    }
                    verifyFormArgumentRange(WYRDEER_SPECIES, formArgument, 20, 9999);
                    return;
                case BASCULEGION_SPECIES:
                    verifyFormArgumentRange(BASCULEGION_SPECIES, formArgument, 294, 9999);
                    return;
                case ANNIHILAPE_SPECIES:
                    verifyFormArgumentRange(ANNIHILAPE_SPECIES, formArgument, 20, 9999);
                    return;
                case KINGAMBIT_SPECIES:
                    verifyFormArgumentRange(KINGAMBIT_SPECIES, formArgument, 3, 9999);
                    return;
                case KORAIDON_SPECIES:
                case MIRAIDON_SPECIES:
                    return;
                default:
                    break;
                }
                verifyFormArgumentNone(formArgument);
            }
        };
    }

    void verifyForm(const AnalysisContext &context)
    {
        if (context.formatGeneration < 4)
        {
            return;
        }
        const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
        const uint16_t encounterSpecies = encounterMatch != nullptr ? encounterMatch->speciesId() : 0;
        // Range-checked templates need the encounter's species; without a match those checks are skipped.
        const FormAnalysis analysis{context, context.evolutionHistory(), getEncounterGeneration(context), encounterSpecies};
        analysis.verifyForm();
        if (encounterSpecies != 0)
        {
            analysis.verifyFormArgument();
        }
    }
}
