// Port of PKHeX PKHeX.Core/Legality/Verifiers/MiscVerifier.cs: per-format miscellaneous fields, and
// the checks every format shares from Misc/ (MiscVerifierHelpers, FullnessRules, MiscScaleVerifier,
// MiscPokerusVerifier, MiscDateVerifier, MiscFatefulEncounterVerifier, MiscEvolutionVerifier).
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <optional>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGift3.h"
#include "Legality/Encounters/EncounterGift9a.h"
#include "Legality/Encounters/EncounterSlot8.h"
#include "Legality/Encounters/EncounterSlot8a.h"
#include "Legality/Encounters/EncounterSlot9a.h"
#include "Legality/Encounters/EncounterStatic8.h"
#include "Legality/Encounters/EncounterStatic8a.h"
#include "Legality/Encounters/EncounterStatic8Nest.h"
#include "Legality/Encounters/EncounterStatic8b.h"
#include "Legality/Encounters/EncounterStatic9a.h"
#include "Legality/Encounters/EncounterTrade9a.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftWA9.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWA8.h"
#include "Legality/Encounters/MysteryGiftWB8.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/RNG/MethodFinder.h"
#include "Legality/Verifiers/MiscVerifier.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t MEW_SPECIES = 151;
        constexpr uint16_t CELEBI_SPECIES = 251;
        constexpr uint16_t LYCANROC_SPECIES = 745;
        constexpr uint16_t SOLGALEO_SPECIES = 791;
        constexpr uint16_t LUNALA_SPECIES = 792;
        constexpr uint16_t METAPOD_SPECIES = 11;
        constexpr uint16_t KAKUNA_SPECIES = 14;
        constexpr uint16_t PINECO_SPECIES = 204;
        constexpr uint16_t SILCOON_SPECIES = 266;
        constexpr uint16_t CASCOON_SPECIES = 268;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint16_t SPEWPA_SPECIES = 665;

        constexpr uint8_t NATURE_COUNT = 25;
        constexpr uint8_t SERIOUS_NATURE = 12;
        constexpr uint8_t FULLNESS_MAXIMUM_SWITCH = 245;

        constexpr uint8_t POKEMON_GO_VERSION = 34;
        constexpr uint8_t X_VERSION = 24;
        constexpr uint8_t Y_VERSION = 25;
        constexpr uint8_t SWORD_VERSION = 44;
        constexpr uint8_t SHIELD_VERSION = 45;
        constexpr uint8_t LEGENDS_ARCEUS_VERSION = 47;
        constexpr uint8_t BRILLIANT_DIAMOND_VERSION = 48;
        constexpr uint8_t SHINING_PEARL_VERSION = 49;
        constexpr uint8_t SCARLET_VERSION = 50;
        constexpr uint8_t VIOLET_VERSION = 51;
        constexpr uint8_t FIRERED_VIRTUAL_CONSOLE_VERSION = 56;
        constexpr uint8_t LEAFGREEN_VIRTUAL_CONSOLE_VERSION = 57;

        /// PKHeX Locations.Default8bNone: Brilliant Diamond/Shining Pearl's "no location".
        constexpr uint16_t LOCATION_NONE_8BDSP = 65535;
        /// PKHeX Locations.IsMetLocation3RS: Ruby/Sapphire/Emerald's shared met locations end at the ferry.
        constexpr uint16_t LAST_MET_LOCATION_3RS = 87;
        constexpr int DATE_EPOCH_YEAR = 2000;
        /// PKHeX Locations.Transfer1 / Transfer2: the met locations of a Virtual Console transfer.
        constexpr uint16_t TRANSFER_LOCATION_VC1 = 30013;
        constexpr uint16_t TRANSFER_LOCATION_VC2 = 30017;

        /// PKHeX LocationsHOME.GetVersionSWSH: the Sword/Shield version HOME files a Switch game's Pokemon under.
        uint8_t getVersionSWSH(uint8_t version)
        {
            switch (version)
            {
                case LEGENDS_ARCEUS_VERSION:
                case BRILLIANT_DIAMOND_VERSION:
                case SCARLET_VERSION:
                case FIRERED_VIRTUAL_CONSOLE_VERSION:
                    return SWORD_VERSION;
                case SHINING_PEARL_VERSION:
                case VIOLET_VERSION:
                case LEAFGREEN_VIRTUAL_CONSOLE_VERSION:
                    return SHIELD_VERSION;
                default:
                    return version;
            }
        }

        /// PKHeX FullnessRules.IsUnfeedable6.
        bool isUnfeedable6(uint16_t speciesId)
        {
            switch (speciesId)
            {
                case METAPOD_SPECIES:
                case KAKUNA_SPECIES:
                case PINECO_SPECIES:
                case SILCOON_SPECIES:
                case CASCOON_SPECIES:
                case SHEDINJA_SPECIES:
                case SPEWPA_SPECIES:
                    return true;
                default:
                    return false;
            }
        }

        bool isEncounterKnown(const AnalysisContext &context)
        {
            return context.encounter.type != EncounterType::Unchecked;
        }

        /// The encounter's generation (PKHeX IEncounterTemplate.Generation); the origin's when PKSE did not match one.
        int getEncounterGeneration(const AnalysisContext &context)
        {
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                return encounterMatch->generation();
            }
            return getOriginGeneration(context);
        }

        bool isHomeTracked(const Pokemon::Pokemon &pokemon)
        {
            return pokemon.hasHomeTracker() && pokemon.homeTracker() != 0;
        }

        /// PKHeX MiscScaleVerifier.VerifyHeightWeight.
        void verifyHeightWeight(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t heightScalar = pokemon.heightScalar();
            const uint8_t weightScalar = pokemon.weightScalar();
            const int encounterGeneration = getEncounterGeneration(context);
            const Enums::GameVersion encounterContext = getOriginContextGroup(context);
            if (encounterGeneration < 8 && context.formatGeneration >= 9)
            {
                const bool isPokemonGo = pokemon.hasOriginGame() && pokemon.originGame() == POKEMON_GO_VERSION;
                const bool hasVisitedLegendsArceus = context.evolutionHistory().hasVisited(Enums::GameVersion::PLA);
                if (heightScalar == 0 && weightScalar == 0 && !hasVisitedLegendsArceus && !isPokemonGo)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatInvalidHeightWeight);
                }
            }
            else if (encounterContext == Enums::GameVersion::ZA)
            {
                if (heightScalar != weightScalar)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectWeightValue, std::to_string(heightScalar));
                }
            }
            else if (encounterGeneration >= 8)
            {
                // A HOME gift (WC8) and every Scarlet/Violet gift (WC9) fix the scalars, which PKSE's
                // event rows do not tell apart; an event or an unmatched origin is skipped.
                const bool mayBeFixedGift = context.encounter.type == EncounterType::Event || !isEncounterKnown(context);
                if (!mayBeFixedGift && heightScalar == 0 && weightScalar == 0)
                {
                    context.add(Severity::Warning, CheckIdentifier::Encounter, ResultCode::StatInvalidHeightWeight);
                }
            }
        }

        /// PKHeX MiscScaleVerifier.VerifyScale.
        void verifyScale(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t heightScalar = pokemon.heightScalar();
            const uint8_t scale = pokemon.scale();
            const Enums::GameVersion encounterContext = getOriginContextGroup(context);
            if (encounterContext == Enums::GameVersion::PLA)
            {
                const std::optional<bool> encounterAlpha = isEncounterAlpha(context);
                if (!encounterAlpha.has_value())
                {
                    return;
                }
                if (*encounterAlpha)
                {
                    // A static Alpha generated at 127 that HOME has not yet rewritten keeps 127 (EncounterStatic8a.IsAlpha127),
                    // which PKSE's rows do not mark, so a 127 scale on 127 scalars is accepted for any Alpha.
                    const bool isAlpha127 = heightScalar == 127 && pokemon.weightScalar() == 127 && scale == 127;
                    if (scale != 255 && !isAlpha127)
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatIncorrectScaleValue, "255");
                    }
                    return;
                }
            }
            if (encounterContext == Enums::GameVersion::ZA)
            {
                if (scale == 0)
                {
                    if (heightScalar != 0)
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatIncorrectHeightValue, "0");
                    }
                }
                else if ((heightScalar != 0 || isHomeTracked(pokemon)) && heightScalar != scale)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatIncorrectHeightValue, std::to_string(heightScalar));
                }
            }
            else if (isHomeTracked(pokemon) && heightScalar != scale)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatIncorrectScaleValue, std::to_string(heightScalar));
            }
        }

        bool hasSizeScalars(Enums::GameVersion group)
        {
            return group == Enums::GameVersion::SWSH || group == Enums::GameVersion::BDSP || group == Enums::GameVersion::PLA || group == Enums::GameVersion::SV || group == Enums::GameVersion::ZA;
        }

        /// PKHeX MiscScaleVerifier.
        void verifyScaleValues(const AnalysisContext &context)
        {
            if (!hasSizeScalars(context.formatGroup))
            {
                return;
            }
            verifyHeightWeight(context);
            if (context.pokemon.hasScale())
            {
                verifyScale(context);
            }
        }

        /// PKHeX Pokerus.HasVisitedAnother: a Legends: Arceus Pokemon can only carry Pokerus brought from another game.
        bool hasVisitedPokerusGame(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (isUntraded(context) || !isHomeTracked(pokemon))
            {
                return false;
            }
            uint16_t encounterSpeciesId = pokemon.speciesID();
            uint8_t encounterFormId = pokemon.form();
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                encounterSpeciesId = encounterMatch->speciesId();
            }
            else
            {
                const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Evolutions::getEvolutionContextGroup(context.formatGroup));
                const int stageCount = static_cast<int>(stages.size());
                if (stageCount != 0)
                {
                    encounterSpeciesId = stages[stageCount - 1].speciesId;
                    encounterFormId = stages[stageCount - 1].formId;
                }
            }
            const uint8_t presence = Pokemon::getPersonalInfo(encounterSpeciesId, encounterFormId).presence;
            if ((presence & (Pokemon::PERSONAL_GAME_BDSP | Pokemon::PERSONAL_GAME_SWSH)) != 0)
            {
                return true;
            }
            const int originGeneration = getOriginGeneration(context);
            return originGeneration >= 1 && originGeneration < 8;
        }

        /// PKHeX Pokerus.IsObtainable.
        bool isPokerusObtainable(const AnalysisContext &context)
        {
            switch (context.formatGroup)
            {
                case Enums::GameVersion::PLA:
                    return hasVisitedPokerusGame(context);
                case Enums::GameVersion::GG:
                case Enums::GameVersion::SV:
                case Enums::GameVersion::ZA:
                    return false;
                default:
                    return true;
            }
        }

        /// PKHeX MiscPokerusVerifier.
        void verifyPokerus(const AnalysisContext &context)
        {
            if (context.formatGeneration == 1)
            {
                return;
            }
            const uint8_t pokerusState = context.pokemon.pokerus();
            const int strain = pokerusState >> 4;
            const int days = pokerusState & 0x0F;
            // The Nintendo Switch FireRed/LeafGreen has no Pokerus.
            if (context.formatGeneration == 3 && !allowsGameBoyAdvanceCrossTransfer(context))
            {
                if (strain != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::PokerusStrainUnobtainable, std::to_string(strain));
                }
                if (days != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::PokerusDaysAtMost, "0");
                }
                return;
            }
            bool isStrainValid = true;
            if (!isPokerusObtainable(context))
            {
                isStrainValid = strain == 0 && days == 0;
            }
            else if (context.formatGeneration <= 2)
            {
                isStrainValid = strain <= 8;
            }
            if (!isStrainValid)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::PokerusStrainUnobtainable, std::to_string(strain));
            }
            const int maximumDays = (strain & 3) + 1;
            if (days > maximumDays)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::PokerusDaysAtMost, std::to_string(maximumDays));
            }
        }

        /// PKHeX MiscDateVerifier.
        void verifyDates(const AnalysisContext &context)
        {
            if (context.formatGeneration <= 3)
            {
                return;
            }
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t noLocation = context.formatGroup == Enums::GameVersion::BDSP ? LOCATION_NONE_8BDSP : 0;
            if (pokemon.metLocation() != noLocation)
            {
                if (!isValidDate(DATE_EPOCH_YEAR + pokemon.metYear(), pokemon.metMonth(), pokemon.metDay()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::DateCalendarInvalidMet);
                }
            }
            else if (context.encounter.type != EncounterType::Unmatched)
            {
                if (pokemon.metMonth() != 0 || pokemon.metDay() != 0 || pokemon.metYear() != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::DateCalendarInvalidMet);
                }
            }

            if (pokemon.eggLocation() != noLocation)
            {
                if (!isValidDate(DATE_EPOCH_YEAR + pokemon.eggYear(), pokemon.eggMonth(), pokemon.eggDay()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::DateCalendarInvalidEgg);
                }
            }
            else if (pokemon.eggMonth() != 0 || pokemon.eggDay() != 0 || pokemon.eggYear() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::DateCalendarInvalidEgg);
            }
        }

        /// PKHeX MiscFatefulEncounterVerifier.VerifyReceivability.
        void verifyGiftReceivability(const AnalysisContext &context, const Encounters::Encounterable &gift)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(Encounters::getOriginVersion(pokemon));
            bool isVersionNotDistributed = false;
            uint8_t restrictLanguage = 0;
            if (const Encounters::MysteryGiftPCD *card = Encounters::encounterAs<Encounters::MysteryGiftPCD>(gift))
            {
                isVersionNotDistributed = !card->canBeReceivedByVersion(originVersion) && card->giftData().version == 0;
            }
            else if (const Encounters::MysteryGiftWC6 *card = Encounters::encounterAs<Encounters::MysteryGiftWC6>(gift))
            {
                isVersionNotDistributed = !card->canBeReceivedByVersion(originVersion) && !wasTradedEgg(context);
                restrictLanguage = card->card().restrictLanguage;
            }
            else if (const Encounters::MysteryGiftWC7 *card = Encounters::encounterAs<Encounters::MysteryGiftWC7>(gift))
            {
                isVersionNotDistributed = !card->canBeReceivedByVersion(originVersion) && !wasTradedEgg(context);
                restrictLanguage = card->card().restrictLanguage;
            }
            else if (const Encounters::MysteryGiftWC8 *card = Encounters::encounterAs<Encounters::MysteryGiftWC8>(gift))
            {
                isVersionNotDistributed = !card->canBeReceivedByVersion(originVersion);
            }
            else if (const Encounters::MysteryGiftWB8 *card = Encounters::encounterAs<Encounters::MysteryGiftWB8>(gift))
            {
                isVersionNotDistributed = !card->canRecordBeReceivedByVersion(originVersion, pokemon);
            }
            else if (const Encounters::MysteryGiftWA8 *card = Encounters::encounterAs<Encounters::MysteryGiftWA8>(gift))
            {
                isVersionNotDistributed = !card->canRecordBeReceivedByVersion(originVersion, pokemon);
            }
            else if (const Encounters::MysteryGiftPGF *card = Encounters::encounterAs<Encounters::MysteryGiftPGF>(gift))
            {
                restrictLanguage = card->restrictLanguage();
            }
            if (isVersionNotDistributed)
            {
                context.add(Severity::Invalid, CheckIdentifier::GameOrigin, ResultCode::EncounterGiftVersionNotDistributed);
                return;
            }
            if (restrictLanguage != 0 && pokemon.language() != restrictLanguage)
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::EncounterGiftLanguageNotDistributed, std::to_string(restrictLanguage));
            }
        }

        /// PKHeX MiscFatefulEncounterVerifier.VerifyFatefulMysteryGift.
        void verifyFatefulMysteryGift(const AnalysisContext &context, const Encounters::Encounterable &gift)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Encounters::MysteryGiftPGF *card = Encounters::encounterAs<Encounters::MysteryGiftPGF>(gift);
            if (card != nullptr && card->isShinyGift())
            {
                if (RNG::MethodFinder::analyze(pokemon).type != RNG::PIDType::Gen5MysteryGiftShiny)
                {
                    const uint16_t locationToCheck = pokemon.isEgg() ? pokemon.metLocation() : pokemon.eggLocation();
                    if (!Encounters::isLinkTradeLocation5(locationToCheck))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::PIDTypeMismatch);
                    }
                }
            }
            const bool shouldBeFateful = gift.isFatefulEncounter();
            if (pokemon.isFatefulEncounter() != shouldBeFateful)
            {
                context.add(Severity::Invalid, CheckIdentifier::Fateful, shouldBeFateful ? ResultCode::FatefulMysteryMissing : ResultCode::FatefulInvalid);
            }
        }

        /// PKHeX MiscFatefulEncounterVerifier.Verify, against the encounter engine's template.
        void verifyFatefulEncounterTemplate(const AnalysisContext &context, const Encounters::Encounterable &encounter)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const bool isFateful = pokemon.isFatefulEncounter();
            const Encounters::EncounterGift3 *gift3 = Encounters::encounterAs<Encounters::EncounterGift3>(encounter);
            if (gift3 != nullptr && gift3->isFatefulEncounter())
            {
                // Eggs hatched in Ruby/Sapphire clear the obedience flag.
                if (gift3->isEgg() && (context.formatGeneration != 3 || pokemon.metLocation() <= LAST_MET_LOCATION_3RS))
                {
                    return;
                }
                if (!isFateful)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Fateful, ResultCode::FatefulMissing);
                }
                return;
            }
            if (Encounters::isMysteryGift(encounter.templateType()))
            {
                verifyGiftReceivability(context, encounter);
                verifyFatefulMysteryGift(context, encounter);
                return;
            }
            if (encounter.isFatefulEncounter())
            {
                if (!isFateful)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Fateful, ResultCode::FatefulMissing);
                }
                return;
            }
            if (isFateful)
            {
                context.add(Severity::Invalid, CheckIdentifier::Fateful, ResultCode::FatefulInvalid);
            }
        }

        /// PKHeX MiscFatefulEncounterVerifier; silent without a matched encounter.
        void verifyFatefulEncounter(const AnalysisContext &context)
        {
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                verifyFatefulEncounterTemplate(context, *encounterMatch);
            }
        }

        /// PKHeX MiscEvolutionVerifier: Sun/Moon's version-specific evolutions need a trade to cross versions.
        // PKHeX MiscEncounterDetailsVerifier.VerifyCorrelation8: an overworld correlation the template
        // requires, or one it forbids.
        bool isOverworldCorrelationValid8(Encounters::OverworldCorrelation8Requirement requirement, bool isMatch) noexcept
        {
            switch (requirement)
            {
            case Encounters::OverworldCorrelation8Requirement::MustHave:
                return isMatch;
            case Encounters::OverworldCorrelation8Requirement::MustNotHave:
                return !isMatch;
            default:
                return true;
            }
        }

        /// The ISeedCorrelation64 result of a Legends: Z-A template (IEncounter9a); none for any other
        /// template or a HOME Wonder Card, which PKHeX leaves out.
        std::optional<Encounters::SeedCorrelationResult> getLumioseSeedResult(const Pokemon::Pokemon &pokemon, const Encounters::Encounterable &encounter)
        {
            if (const Encounters::EncounterSlot9a *slot = Encounters::encounterAs<Encounters::EncounterSlot9a>(encounter))
            {
                return slot->tryGetSeed(pokemon);
            }
            if (const Encounters::EncounterStatic9a *staticEncounter = Encounters::encounterAs<Encounters::EncounterStatic9a>(encounter))
            {
                return staticEncounter->tryGetSeed(pokemon);
            }
            if (const Encounters::EncounterGift9a *gift = Encounters::encounterAs<Encounters::EncounterGift9a>(encounter))
            {
                return gift->tryGetSeed(pokemon);
            }
            if (const Encounters::EncounterTrade9a *trade = Encounters::encounterAs<Encounters::EncounterTrade9a>(encounter))
            {
                return trade->tryGetSeed(pokemon);
            }
            if (const Encounters::MysteryGiftWA9 *card = Encounters::encounterAs<Encounters::MysteryGiftWA9>(encounter))
            {
                if (card->isHomeGift())
                {
                    return std::nullopt;
                }
                return card->tryGetSeed(pokemon);
            }
            return std::nullopt;
        }

        // PKHeX MiscEncounterDetailsVerifier.VerifyRandomCorrelationSwitch, for the templates the encounter
        // engine has ported: Sword/Shield overworld slots and statics (IOverworldCorrelation8), their raids and
        // the Legends: Z-A templates (ISeedCorrelation64), the Legends: Arceus templates' initial mastery
        // flags (IMasteryInitialMoveShop8), and Brilliant Diamond/Shining Pearl statics
        // (IStaticCorrelation8b). The
        // PIDIV each one would record is not kept: no later check reads it. The server date window
        // (VerifyServerDate2000) needs the distribution dates, which PKSE's gift tables do not carry.
        void verifyEncounterDetails(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr || !context.legalityInfo->encounterMatch())
            {
                return;
            }
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Encounters::Encounterable &encounter = *context.legalityInfo->encounterMatch();
            bool isValid = true;
            if (const Encounters::EncounterSlot8 *slot = Encounters::encounterAs<Encounters::EncounterSlot8>(encounter))
            {
                isValid = isOverworldCorrelationValid8(slot->getRequirement(pokemon), slot->isOverworldCorrelationCorrect(pokemon));
            }
            else if (const Encounters::EncounterStatic8 *overworldStatic = Encounters::encounterAs<Encounters::EncounterStatic8>(encounter))
            {
                isValid = isOverworldCorrelationValid8(overworldStatic->getRequirement(), overworldStatic->isOverworldCorrelationCorrect(pokemon));
            }
            else if (const Encounters::EncounterStatic8b *roamerStatic = Encounters::encounterAs<Encounters::EncounterStatic8b>(encounter))
            {
                const bool isMatch = roamerStatic->isStaticCorrelationCorrect(pokemon);
                switch (roamerStatic->getRequirement(pokemon))
                {
                case Encounters::StaticCorrelation8bRequirement::MustHave:
                    isValid = isMatch;
                    break;
                case Encounters::StaticCorrelation8bRequirement::MustNotHave:
                    isValid = !isMatch;
                    break;
                default:
                    break;
                }
            }
            else if (const Encounters::EncounterStatic8Nest *raid = Encounters::encounterAs<Encounters::EncounterStatic8Nest>(encounter))
            {
                uint64_t seed = 0;
                isValid = raid->tryGetSeed(pokemon, seed);
            }
            else if (const Encounters::EncounterSlot8a *legendsSlot = Encounters::encounterAs<Encounters::EncounterSlot8a>(encounter))
            {
                // Its seed result is never Invalid (a multi-roll seed cannot be ruled out); only the
                // initial mastery flags (IMasteryInitialMoveShop8) can fail.
                if (!legendsSlot->isForcedMasteryCorrect(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::EncounterMasteryInitial);
                }
            }
            else if (const Encounters::EncounterStatic8a *legendsStatic = Encounters::encounterAs<Encounters::EncounterStatic8a>(encounter))
            {
                if (!legendsStatic->isForcedMasteryCorrect(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::EncounterMasteryInitial);
                }
            }
            else if (const std::optional<Encounters::SeedCorrelationResult> lumioseResult = getLumioseSeedResult(pokemon, encounter))
            {
                isValid = *lumioseResult != Encounters::SeedCorrelationResult::Invalid;
            }
            if (!isValid)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::PIDTypeMismatch);
            }
        }

        void verifyVersionEvolution(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGeneration < 7 || !pokemon.hasOriginGame())
            {
                return;
            }
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr || encounterMatch->speciesId() == pokemon.speciesID())
            {
                return;
            }
            const bool isMoonVersion = (pokemon.originGame() & 1) == 1;
            const bool isSunVersion = !isMoonVersion;
            const uint16_t speciesId = pokemon.speciesID();
            const bool isLycanrocMismatch = speciesId == LYCANROC_SPECIES && context.formatGeneration == 7 && ((pokemon.form() == 0 && isMoonVersion) || (pokemon.form() == 1 && isSunVersion));
            const bool isCosmogMismatch = (speciesId == SOLGALEO_SPECIES && isMoonVersion) || (speciesId == LUNALA_SPECIES && isSunVersion);
            if ((isLycanrocMismatch || isCosmogMismatch) && isUntraded(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Evolution, ResultCode::EvolutionTradeRequired);
            }
        }
    }

    static bool isLeapYear(int year)
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    /// PKHeX DateUtil.IsValidDate.
    bool isValidDate(int year, int month, int day)
    {
        if (year <= 0 || year > 9999 || month < 1 || month > 12)
        {
            return false;
        }
        constexpr int daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const int monthDayCount = (month == 2 && isLeapYear(year)) ? 29 : daysInMonth[month - 1];
        return day >= 1 && day <= monthDayCount;
    }

    std::optional<bool> isEncounterAlpha(const AnalysisContext &context)
    {
        if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
        {
            // PKHeX `enc is IAlphaReadOnly { IsAlpha: true }`.
            if (const Encounters::EncounterSlot8a *slot = Encounters::encounterAs<Encounters::EncounterSlot8a>(*encounterMatch))
            {
                return slot->isAlpha();
            }
            if (const Encounters::EncounterStatic8a *fixed = Encounters::encounterAs<Encounters::EncounterStatic8a>(*encounterMatch))
            {
                return fixed->isAlpha();
            }
            if (const Encounters::EncounterSlot9a *slot = Encounters::encounterAs<Encounters::EncounterSlot9a>(*encounterMatch))
            {
                return slot->isAlpha();
            }
            if (const Encounters::EncounterStatic9a *fixed = Encounters::encounterAs<Encounters::EncounterStatic9a>(*encounterMatch))
            {
                return fixed->isAlpha();
            }
            if (const Encounters::EncounterGift9a *gift = Encounters::encounterAs<Encounters::EncounterGift9a>(*encounterMatch))
            {
                return gift->isAlpha();
            }
            if (const Encounters::MysteryGiftWA9 *card = Encounters::encounterAs<Encounters::MysteryGiftWA9>(*encounterMatch))
            {
                return card->isAlpha();
            }
            return false;
        }
        return std::nullopt;
    }

    bool isStoredSlot(const AnalysisContext &context, StorageSlotType slotType)
    {
        return context.options.slotType == slotType || context.options.slotType == StorageSlotType::Unknown;
    }

    void verifyStatAlignment(const AnalysisContext &context)
    {
        const uint8_t statNature = context.pokemon.statNature();
        if (statNature == context.pokemon.nature())
        {
            return;
        }
        const bool isMint = statNature < NATURE_COUNT && (statNature % 6 != 0 || statNature == SERIOUS_NATURE);
        if (!isMint)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatAlignmentInvalid);
        }
    }

    bool isObedienceLevelValid(const AnalysisContext &context, uint8_t obedienceLevel)
    {
        const uint8_t metLevel = context.pokemon.metLevel();
        if (isUntraded(context))
        {
            return obedienceLevel == metLevel;
        }
        if (obedienceLevel < metLevel)
        {
            return false;
        }
        return Evolutions::getEntityCurrentLevel(context.pokemon) >= obedienceLevel;
    }

    bool isBattleVersionValid(const AnalysisContext &context, uint8_t battleVersion)
    {
        if (battleVersion == 0)
        {
            return true;
        }
        if (battleVersion != SWORD_VERSION && battleVersion != SHIELD_VERSION)
        {
            return false;
        }
        if (!context.evolutionHistory().hasVisited(Enums::GameVersion::SWSH))
        {
            return false;
        }
        const uint8_t homeVersion = getVersionSWSH(context.pokemon.originGame());
        return homeVersion != SWORD_VERSION && homeVersion != SHIELD_VERSION;
    }

    void verifyFullness(const AnalysisContext &context, uint8_t fullness, uint8_t enjoyment)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (pokemon.isEgg())
        {
            if (fullness != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatFullness, "0");
            }
            if (enjoyment != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatEnjoyment, "0");
            }
            return;
        }
        if (context.formatGeneration >= 8)
        {
            // Exiting camp costs 10, so 245 is the highest a Sword/Shield Pokemon keeps; Brilliant
            // Diamond/Shining Pearl and Legends: Arceus never set it, even through HOME.
            if (fullness > FULLNESS_MAXIMUM_SWITCH)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatFullnessAtMost, std::to_string(FULLNESS_MAXIMUM_SWITCH));
            }
            else if (fullness != 0 && context.formatGroup != Enums::GameVersion::SWSH)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatFullness, "0");
            }
            if (enjoyment != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatEnjoyment, "0");
            }
            return;
        }
        if (context.formatGeneration != 6)
        {
            return;
        }
        // Omega Ruby/Alpha Sapphire has no Pokemon-Amie to feed in; only an X/Y-fed, unevolved Pokemon can be full.
        const bool isXY = pokemon.originGame() == X_VERSION || pokemon.originGame() == Y_VERSION;
        if (!isUntraded(context) || isXY || fullness == 0)
        {
            return;
        }
        const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
        if (encounterMatch == nullptr || encounterMatch->speciesId() != pokemon.speciesID())
        {
            return;
        }
        if (isUnfeedable6(pokemon.speciesID()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatFullness, "0");
        }
    }

    void verifyMisc(const AnalysisContext &context)
    {
        verifyEggs(context);
        if (context.formatGeneration < 3)
        {
            verifyMiscGameBoy(context);
            verifyPokerus(context);
            return;
        }

        switch (context.formatGroup)
        {
            case Enums::GameVersion::RSE:
            case Enums::GameVersion::FRLG:
                verifyMiscGeneration3(context);
                break;
            case Enums::GameVersion::DP:
            case Enums::GameVersion::PT:
            case Enums::GameVersion::HGSS:
                verifyMiscGeneration4(context);
                break;
            case Enums::GameVersion::BW:
            case Enums::GameVersion::B2W2:
                verifyMiscGeneration5(context);
                break;
            case Enums::GameVersion::XY:
            case Enums::GameVersion::ORAS:
                verifyMiscGeneration6(context);
                break;
            case Enums::GameVersion::SM:
            case Enums::GameVersion::USUM:
                verifyMiscGeneration7(context);
                break;
            case Enums::GameVersion::GG:
                verifyMisc7LGPE(context);
                break;
            case Enums::GameVersion::SWSH:
                verifyMisc8SWSH(context);
                break;
            case Enums::GameVersion::BDSP:
                verifyMisc8BDSP(context);
                break;
            case Enums::GameVersion::PLA:
                verifyMisc8LA(context);
                break;
            case Enums::GameVersion::SV:
                verifyMisc9SV(context);
                break;
            case Enums::GameVersion::ZA:
                verifyMisc9LZA(context);
                break;
            default:
                break;
        }

        verifyEncounterDetails(context);
        verifyFatefulEncounter(context);
        verifyScaleValues(context);
        verifyPokerus(context);
        verifyDates(context);
        verifyVersionEvolution(context);
    }
}
