// Port of PKHeX PKHeX.Core/Legality/Verifiers/Misc/ for the formats before the Switch's own games plus Let's
// Go: MiscG1Verifier.cs, MiscVerifierG3.cs, MiscVerifierG4.cs, MiscVerifierPK5.cs, MiscVerifierPK6.cs,
// MiscVerifierPK7.cs and MiscVerifierPB7.cs.
#include "Legality/Verifiers/MiscVerifier.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "Enums/Ball.h"
#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Legality/Verifiers/ForeignSpecies.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Gen1Tables.h"
#include "Pokemon/PersonalInfo1RBY.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon4DP.h"
#include "Pokemon/Pokemon4HGSS.h"
#include "Pokemon/Pokemon4PT.h"
#include "Pokemon/Pokemon5B2W2.h"
#include "Pokemon/Pokemon5BW.h"
#include "Pokemon/Pokemon6ORAS.h"
#include "Pokemon/Pokemon6XY.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Pokemon7SM.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t MAGNEMITE_SPECIES = 81;
        constexpr uint16_t MAGNETON_SPECIES = 82;
        constexpr uint16_t DITTO_SPECIES = 132;
        constexpr uint16_t EEVEE_SPECIES = 133;
        constexpr uint16_t PORYGON_SPECIES = 137;
        constexpr uint16_t GEN1_SPECIES_MAXIMUM = 151;

        /// Stadium 2 can give Magnemite and Magneton the Steel type, stored as the Gen 2 id.
        constexpr uint8_t STADIUM2_STEEL_TYPE = 9;

        constexpr uint8_t HEARTGOLD_VERSION = 7;
        constexpr uint8_t SOULSILVER_VERSION = 8;
        constexpr uint8_t POKEMON_GO_VERSION = 34;
        /// PKHeX Locations.Transfer3: Pal Park.
        constexpr uint16_t PAL_PARK_LOCATION = 0x37;
        /// PKHeX Locations.LinkTrade4: the egg location a traded Gen 4 egg carries.
        constexpr uint16_t LINK_TRADE_EGG_LOCATION_4 = 2002;
        constexpr uint8_t SHINY_LEAF_ALL_LEAVES = 0b11111;
        constexpr uint8_t SHINY_LEAF_CROWN = 0b111111;

        constexpr uint8_t POKESTAR_FAME_STEP = 25;
        /// PKHeX ResortEventState.MAX.
        constexpr uint8_t RESORT_EVENT_STATE_MAXIMUM = 20;
        constexpr int CP_MAXIMUM = 10000;
        constexpr int SWITCH_YEAR_MAXIMUM = 60;

        /// PKHeX PersonalTable1.TypeIDExists: the Gen 1 ROM type ids its personal table uses.
        bool isGen1TypeId(uint8_t romType)
        {
            return romType < 32 && ((0b111111100000000000110111111u >> romType) & 1) != 0;
        }

        /// The Gen 1 personal table's two types in ROM numbering, the second repeating the first when single-typed.
        void getGen1PersonalTypes(uint16_t speciesId, uint8_t &firstType, uint8_t &secondType)
        {
            const Pokemon::PersonalRecord &personalRecord = Pokemon::getPersonalInfo1RBY(speciesId, 0);
            firstType = Pokemon::pkseTypeToG1(personalRecord.type1);
            secondType = Pokemon::pkseTypeToG1(personalRecord.type2 == Pokemon::PERSONAL_TYPE_NONE ? personalRecord.type1 : personalRecord.type2);
        }

        /// PKHeX MiscG1Verifier.VerifyMiscG1Types.
        void verifyGen1Types(const AnalysisContext &context, const Pokemon::Pokemon1RBY &pokemon)
        {
            const uint8_t storedFirstType = pokemon.storedRomType1();
            const uint8_t storedSecondType = pokemon.storedRomType2();
            const uint16_t speciesId = pokemon.speciesID();
            if (speciesId == PORYGON_SPECIES)
            {
                // Conversion gives Porygon any type combination a Gen 1 species has.
                if (!isGen1TypeId(storedFirstType))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen1TypePorygonFail1);
                }
                if (!isGen1TypeId(storedSecondType))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen1TypePorygonFail2);
                    return;
                }
                for (uint16_t candidateSpeciesId = 1; candidateSpeciesId <= GEN1_SPECIES_MAXIMUM; ++candidateSpeciesId)
                {
                    uint8_t firstType = 0;
                    uint8_t secondType = 0;
                    getGen1PersonalTypes(candidateSpeciesId, firstType, secondType);
                    if (firstType == storedFirstType && secondType == storedSecondType)
                    {
                        return;
                    }
                }
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen1TypePorygonFail);
                return;
            }
            if (speciesId == 0 || speciesId > GEN1_SPECIES_MAXIMUM)
            {
                return;
            }
            uint8_t firstType = 0;
            uint8_t secondType = 0;
            getGen1PersonalTypes(speciesId, firstType, secondType);
            const bool isStadiumSteelType = allowsGameBoyCartridgeEra(context.options) && (speciesId == MAGNEMITE_SPECIES || speciesId == MAGNETON_SPECIES) && storedSecondType == STADIUM2_STEEL_TYPE;
            const bool isSecondMatch = storedSecondType == secondType || isStadiumSteelType;
            if (storedFirstType != firstType)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen1Type1Fail);
            }
            if (!isSecondMatch)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen1Type2Fail);
            }
        }

        /// PKHeX MiscG1Verifier.VerifyAsFormat2: Crystal's caught data records the time of day.
        void verifyGen2TimeOfDay(const AnalysisContext &context, const Pokemon::Pokemon2GSC &pokemon)
        {
            const uint16_t caughtData = pokemon.caughtData();
            if (caughtData == 0)
            {
                return;
            }
            const EncounterInfo &encounter = context.encounter;
            if (!encounter.isValid())
            {
                return;
            }
            const uint8_t timeOfDay = static_cast<uint8_t>(caughtData >> 14);
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            const bool isEggGift = encounterMatch != nullptr && encounterMatch->isEgg();
            const bool isFixedTime = encounter.type == EncounterType::Trade || (encounter.type == EncounterType::Gift && (!isEggGift || pokemon.isEgg()));
            const bool isValid = isFixedTime ? timeOfDay == 0 : (timeOfDay >= 1 && timeOfDay <= 3);
            if (!isValid)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MetDetailTimeOfDay);
            }
        }

        /// PKHeX MiscVerifierG4.IsValidBallDP: a Diamond/Pearl/Platinum ball byte ends at the Cherish Ball.
        bool isBallValidDP(uint8_t ball)
        {
            return ball <= static_cast<uint8_t>(Enums::Ball::Cherish);
        }

        enum PalParkOrigin : uint8_t
        {
            PAL_PARK_NONE = 0,
            PAL_PARK_DP = 1 << 0,
            PAL_PARK_PT = 1 << 1,
            PAL_PARK_HGSS = 1 << 2,
        };

        /**
         * PKHeX MiscVerifierG4.Verify. PKHeX's Pal Park trash checks (G4PKM.IsTrashPalPark*) are all still
         * "todo" returning true, so the Pal Park origin follows from the extended met location alone, and
         * EggHatchLocation4.IsValidMet4HGSS is taken as true for a traded egg.
         */
        template <typename PokemonType>
        void verifyGeneration4Format(const AnalysisContext &context, const PokemonType &pokemon)
        {
            const int8_t walkingMood = pokemon.walkingMood();
            if (walkingMood != 0)
            {
                const bool isHeartGoldSoulSilverSave = context.options.activeTrainer != nullptr && context.options.activeTrainer->gameVersion == Enums::GameVersion::HGSS;
                if (pokemon.isEgg())
                {
                    context.add(Severity::Warning, CheckIdentifier::Egg, ResultCode::Gen4PartnerMoodEgg);
                }
                else if (context.options.slotType == StorageSlotType::Box && isHeartGoldSoulSilverSave)
                {
                    context.add(Severity::Warning, CheckIdentifier::Misc, ResultCode::Gen4PartnerMoodZero);
                }
            }

            const uint8_t shinyLeaf = pokemon.shinyLeaf();
            if ((shinyLeaf > SHINY_LEAF_ALL_LEAVES && shinyLeaf != SHINY_LEAF_CROWN) || (shinyLeaf != 0 && pokemon.isEgg()))
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen4ShinyLeafBitsInvalid);
            }

            uint8_t palParkOrigin = PAL_PARK_NONE;
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            const bool isFromGeneration3 = encounterMatch != nullptr && encounterMatch->generation() == 3;
            const uint16_t metLocationExtended = pokemon.metLocationExtended();
            if (isFromGeneration3)
            {
                palParkOrigin = metLocationExtended == 0 ? PAL_PARK_DP : (PAL_PARK_PT | PAL_PARK_HGSS);
                const bool isMetLocationValid = (metLocationExtended == 0 && (palParkOrigin & PAL_PARK_DP) != 0) || (metLocationExtended == pokemon.metLocationDP() && (palParkOrigin & (PAL_PARK_PT | PAL_PARK_HGSS)) != 0);
                if (!isMetLocationValid)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferMetLocation, std::to_string(PAL_PARK_LOCATION));
                }
            }

            const uint8_t ballDPPt = pokemon.ballDPPt();
            const uint8_t ballHGSS = pokemon.ballHGSS();
            if (!isBallValidDP(ballDPPt))
            {
                context.add(Severity::Invalid, CheckIdentifier::Ball, ResultCode::BallGen4Sinnoh);
            }

            bool isCreatedInHGSS = false;
            if ((palParkOrigin & PAL_PARK_HGSS) != 0)
            {
                isCreatedInHGSS = true;
            }
            else if (context.encounter.type == EncounterType::Event && getOriginGeneration(context) == 4 && pokemon.eggLocation() == 0)
            {
                // A PCD wonder card injects a finished Pokemon, which never fills the HG/SS byte.
                isCreatedInHGSS = false;
            }
            else if (pokemon.eggLocation() == LINK_TRADE_EGG_LOCATION_4)
            {
                isCreatedInHGSS = ballHGSS != 0;
            }
            else
            {
                isCreatedInHGSS = pokemon.originGame() == HEARTGOLD_VERSION || pokemon.originGame() == SOULSILVER_VERSION;
            }

            bool isBallValidHGSS = false;
            if (!isCreatedInHGSS)
            {
                isBallValidHGSS = ballHGSS == 0;
            }
            else if ((palParkOrigin & (PAL_PARK_DP | PAL_PARK_PT)) != 0 && ballHGSS == 0)
            {
                isBallValidHGSS = true;
            }
            else
            {
                isBallValidHGSS = ballHGSS == ballDPPt || (ballDPPt == static_cast<uint8_t>(Enums::Ball::Poke) && !isBallValidDP(ballHGSS));
            }
            if (!isBallValidHGSS)
            {
                context.add(Severity::Invalid, CheckIdentifier::Ball, ResultCode::BallGen4Johto);
            }
        }

        /// PKHeX MiscVerifierPK5. N's Sparkle needs EncounterStatic5N, which PKSE's rows do not mark, so only an
        /// Egg or a wild Pokemon is held to not having it.
        template <typename PokemonType>
        void verifyGeneration5Format(const AnalysisContext &context, const PokemonType &pokemon)
        {
            const uint8_t fame = pokemon.pokeStarFame();
            if (fame != 0)
            {
                const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                if (pokemon.isEgg())
                {
                    context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::EggShinyPokeStar);
                }
                else if (encounterMatch != nullptr && encounterMatch->speciesId() == DITTO_SPECIES)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen5PokeStarMustBeZero);
                }
                else if (fame % POKESTAR_FAME_STEP != 0 && fame % POKESTAR_FAME_STEP != 5)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen5PokeStarImpossibleValue);
                }
            }
            const EncounterType encounterType = context.encounter.type;
            const bool cannotBeFromN = encounterType == EncounterType::Wild || encounterType == EncounterType::Egg || encounterType == EncounterType::Unmatched;
            if (pokemon.hasNSparkle() && cannotBeFromN)
            {
                context.add(Severity::Invalid, CheckIdentifier::Fateful, ResultCode::Gen5SparkleInvalid);
            }
        }

        /// PKHeX NatureAmp.AmplifyStat; `statIndex` is the visual order Attack, Defense, Sp. Atk, Sp. Def, Speed.
        int amplifyStat(uint8_t nature, int statIndex, int initialStat)
        {
            if (nature >= 25)
            {
                return 90 * initialStat / 100;
            }
            // A nature counts its stats Attack, Defense, Speed, Sp. Atk, Sp. Def; the visual order puts Speed last.
            constexpr int NATURE_ORDER_BY_VISUAL_INDEX[5] = { 0, 1, 3, 4, 2 };
            const int raisedIndex = nature / 5;
            const int loweredIndex = nature % 5;
            const int natureIndex = NATURE_ORDER_BY_VISUAL_INDEX[statIndex];
            if (raisedIndex == loweredIndex)
            {
                return initialStat;
            }
            if (natureIndex == raisedIndex)
            {
                return 110 * initialStat / 100;
            }
            if (natureIndex == loweredIndex)
            {
                return 90 * initialStat / 100;
            }
            return initialStat;
        }

        /// PKHeX PB7.CalcCP: BaseCP plus AwakeCP, capped.
        int calculateCombatPower(const Pokemon::Pokemon7LGPE &pokemon)
        {
            const int level = Evolutions::getEntityCurrentLevel(pokemon);
            const uint8_t nature = pokemon.nature();
            const volatile float friendshipPercent = pokemon.friendship() / 255.0f / 10.0f + 1.0f;
            const int friendshipScalar = static_cast<int>(friendshipPercent * 100.0f);
            auto getStat = [&](int baseStat, int individualValue)
            {
                return (individualValue + (2 * baseStat)) * level / 100;
            };
            auto getScaledStat = [&](int baseStat, int individualValue, int visualIndex)
            {
                const int amplifiedStat = amplifyStat(nature, visualIndex, getStat(baseStat, individualValue) + 5);
                return static_cast<uint16_t>(amplifiedStat * friendshipScalar / 100);
            };
            const int statSum = static_cast<uint16_t>(getStat(pokemon.baseHP(), pokemon.effectiveIV(Pokemon::Pokemon::HT_HP))) + 10 + level
                + getScaledStat(pokemon.baseATK(), pokemon.effectiveIV(Pokemon::Pokemon::HT_ATK), 0)
                + getScaledStat(pokemon.baseDEF(), pokemon.effectiveIV(Pokemon::Pokemon::HT_DEF), 1)
                + getScaledStat(pokemon.baseSPE(), pokemon.effectiveIV(Pokemon::Pokemon::HT_SPE), 4)
                + getScaledStat(pokemon.baseSPA(), pokemon.effectiveIV(Pokemon::Pokemon::HT_SPA), 2)
                + getScaledStat(pokemon.baseSPD(), pokemon.effectiveIV(Pokemon::Pokemon::HT_SPD), 3);
            volatile float baseCombatPower = statSum * 6.0f;
            baseCombatPower = baseCombatPower * level;
            baseCombatPower = baseCombatPower / 100.0f;
            int combatPower = static_cast<int>(baseCombatPower);

            const int awakeningSum = pokemon.avHP() + pokemon.avATK() + pokemon.avDEF() + pokemon.avSPE() + pokemon.avSPA() + pokemon.avSPD();
            if (awakeningSum != 0)
            {
                volatile float awakeningScalar = level * 4.0f;
                awakeningScalar = awakeningScalar / 100.0f;
                awakeningScalar = awakeningScalar + 2.0f;
                const volatile float awakeningCombatPower = awakeningSum * awakeningScalar;
                combatPower += static_cast<int>(awakeningCombatPower);
            }
            return combatPower < CP_MAXIMUM ? combatPower : CP_MAXIMUM;
        }

        uint32_t getFloatBits(float floatValue)
        {
            uint32_t floatBits = 0;
            std::memcpy(&floatBits, &floatValue, sizeof floatBits);
            return floatBits;
        }

        /// PKHeX MiscVerifierPB7.VerifyAbsoluteSizes. The fixed-size WB7 (Meltan) needs IsHeightWeightFixed,
        /// which PKSE's rows do not carry, so an event is not checked.
        void verifyLetsGoSizes(const AnalysisContext &context, const Pokemon::Pokemon7LGPE &pokemon)
        {
            if (context.encounter.type == EncounterType::Event)
            {
                return;
            }
            const float expectedHeight = Pokemon::Pokemon7LGPE::heightAbsoluteFor(pokemon.speciesID(), pokemon.form(), pokemon.heightScalar());
            const float expectedWeight = Pokemon::Pokemon7LGPE::weightAbsoluteFor(pokemon.speciesID(), pokemon.form(), pokemon.heightScalar(), pokemon.weightScalar());
            if (pokemon.heightAbsolute() != expectedHeight)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectHeight, std::to_string(getFloatBits(expectedHeight)));
            }
            if (pokemon.weightAbsolute() != expectedWeight)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectWeight, std::to_string(getFloatBits(expectedWeight)));
            }
        }

        /// PKHeX MiscVerifierPB7.IsReceivedDateValid.
        bool isReceivedDateValid(const AnalysisContext &context, const Pokemon::Pokemon7LGPE &pokemon)
        {
            const uint8_t receivedYear = pokemon.receivedYear();
            if (!isValidDate(2000 + receivedYear, pokemon.receivedMonth(), pokemon.receivedDay()))
            {
                return false;
            }
            if (receivedYear > SWITCH_YEAR_MAXIMUM)
            {
                return false;
            }
            // A GO Park capture keeps GO's met date, so its received date may differ.
            const bool isFromGo = pokemon.originGame() == POKEMON_GO_VERSION;
            const bool isSameAsMet = receivedYear == pokemon.metYear() && pokemon.receivedMonth() == pokemon.metMonth() && pokemon.receivedDay() == pokemon.metDay();
            return !isUntraded(context) || isFromGo || isSameAsMet;
        }

        // ---- PKHeX MiscVerifierG3.cs and TrashByteRules3 ----

        constexpr uint8_t FIRERED_VERSION = 4;
        constexpr uint8_t LEAFGREEN_VERSION = 5;
        constexpr std::byte GENERATION_3_TERMINATOR{0xFF};
        constexpr size_t JAPANESE_TRAINER_PREFILL_LENGTH = 6;

        /// PKHeX ItemStorage3FRLG_VC.Unreleased: held items the Nintendo Switch FireRed/LeafGreen cannot give.
        // clang-format off
        constexpr uint16_t UNRELEASED_SWITCH_FIRERED_LEAFGREEN_HELD_ITEMS[] = {
            5, 7, 12, 39, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51, 81,
            121, 122, 123, 124, 125, 126, 127, 128, 129,
            168, 169, 170, 171, 172, 173, 174, 175,
            179, 180, 185, 186, 191, 192, 193, 198, 202, 219,
            254, 255, 256, 257, 258, 259,
        };
        // clang-format on

        bool isUnreleasedSwitchFireRedLeafGreenHeldItem(uint16_t itemId)
        {
            for (const uint16_t unreleasedItemId : UNRELEASED_SWITCH_FIRERED_LEAFGREEN_HELD_ITEMS)
            {
                if (unreleasedItemId == itemId)
                {
                    return true;
                }
            }
            return false;
        }

        /// TrashBytes8.GetTerminatorIndex: the index of the first 0xFF, or -1.
        int getGeneration3TerminatorIndex(std::span<const std::byte> field)
        {
            for (size_t byteIndex = 0; byteIndex < field.size(); ++byteIndex)
            {
                if (field[byteIndex] == GENERATION_3_TERMINATOR)
                {
                    return static_cast<int>(byteIndex);
                }
            }
            return -1;
        }

        bool isAllEqualTo(std::span<const std::byte> field, std::byte expectedByte)
        {
            for (const std::byte fieldByte : field)
            {
                if (fieldByte != expectedByte)
                {
                    return false;
                }
            }
            return true;
        }

        /// TrashByteRules3.IsTerminatedZero: everything after the terminator is zero.
        bool isTerminatedZero(std::span<const std::byte> field)
        {
            const int terminatorIndex = getGeneration3TerminatorIndex(field);
            if (terminatorIndex == -1 || terminatorIndex >= static_cast<int>(field.size()) - 1)
            {
                return true;
            }
            return isAllEqualTo(field.subspan(static_cast<size_t>(terminatorIndex) + 1), std::byte{0});
        }

        /// TrashByteRules3.IsTerminatedFFZero: terminators up to `prefillLength`, then zeros.
        bool isTerminatedPrefillZero(std::span<const std::byte> field, size_t prefillLength)
        {
            const int terminatorIndex = getGeneration3TerminatorIndex(field);
            if (terminatorIndex == -1 || terminatorIndex >= static_cast<int>(field.size()) - 1)
            {
                return true;
            }
            size_t firstTrashIndex = static_cast<size_t>(terminatorIndex) + 1;
            if (firstTrashIndex < prefillLength)
            {
                if (!isAllEqualTo(field.subspan(firstTrashIndex, prefillLength - firstTrashIndex), GENERATION_3_TERMINATOR))
                {
                    return false;
                }
                firstTrashIndex = prefillLength;
                if (firstTrashIndex >= field.size())
                {
                    return true;
                }
            }
            return isAllEqualTo(field.subspan(firstTrashIndex), std::byte{0});
        }

        /// TrashByteRules3.IsResetTrash: a trip through Colosseum/XD rewrites both names with clean buffers.
        bool isResetTrash(const AnalysisContext &context)
        {
            if (!allowsGameBoyAdvanceCrossTransfer(context))
            {
                return false;
            }
            if (!isTerminatedZero(context.pokemon.originalTrainerNameBytes()))
            {
                return false;
            }
            if (isNicknamedAsStored(context))
            {
                return true;
            }
            return isTerminatedZero(context.pokemon.nicknameBytes());
        }

        /// PKHeX MiscVerifierG3.VerifyTrash for a PK3. The international rules need PKHeX's table of
        /// default trainer names (TrashTrainer3) and the nickname rule its Gen 3 evolution chains, which
        /// PKSE does not have, so only the in-game trade and Japanese trainer-name rules run.
        void verifyGeneration3Trash(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const std::span<const std::byte> trainerField = pokemon.originalTrainerNameBytes();
            const std::span<const std::byte> nicknameField = pokemon.nicknameBytes();
            if (trainerField.empty() || nicknameField.empty())
            {
                return;
            }
            if (!pokemon.isEgg() && isResetTrash(context))
            {
                return;
            }
            const EncounterInfo &encounter = context.encounter;
            if (!encounter.isValid() || !Enums::isGen3Group(encounter.originGroup))
            {
                return;
            }
            if (encounter.type == EncounterType::Trade)
            {
                if (!isTerminatedZero(trainerField))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::TrashBytesShouldBeEmpty);
                }
                if (!isTerminatedZero(nicknameField))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::TrashBytesShouldBeEmpty);
                }
                return;
            }
            if (encounter.type == EncounterType::Event)
            {
                return;
            }
            const bool isJapanese = pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese);
            if (isJapanese && !(pokemon.isEgg() && trainerField.back() == GENERATION_3_TERMINATOR))
            {
                if (!isTerminatedPrefillZero(trainerField, JAPANESE_TRAINER_PREFILL_LENGTH))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::TrashBytesMissingTerminatorFinal);
                }
            }
        }
    }

    void verifyMiscGameBoy(const AnalysisContext &context)
    {
        if (context.formatGroup == Enums::GameVersion::RBY)
        {
            verifyGen1Types(context, static_cast<const Pokemon::Pokemon1RBY &>(context.pokemon));
        }
        else if (context.formatGroup == Enums::GameVersion::GSC)
        {
            verifyGen2TimeOfDay(context, static_cast<const Pokemon::Pokemon2GSC &>(context.pokemon));
        }
    }

    void verifyMiscGeneration3(const AnalysisContext &context)
    {
        verifyGeneration3Trash(context);
        if (allowsGameBoyAdvanceCrossTransfer(context))
        {
            return;
        }

        // Only FireRed/LeafGreen were released on Nintendo Switch, so a Pokemon there can only come from them.
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (pokemon.originGame() != FIRERED_VERSION && pokemon.originGame() != LEAFGREEN_VERSION)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TradeNotAvailable);
        }
        else if (isForeignToFireRedLeafGreen(pokemon.speciesID()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TradeNotAvailable);
        }

        if (isUnreleasedSwitchFireRedLeafGreenHeldItem(pokemon.heldItem()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::ItemUnreleased);
        }
    }

    void verifyMiscGeneration4(const AnalysisContext &context)
    {
        switch (context.formatGroup)
        {
            case Enums::GameVersion::DP:
                verifyGeneration4Format(context, static_cast<const Pokemon::Pokemon4DP &>(context.pokemon));
                break;
            case Enums::GameVersion::PT:
                verifyGeneration4Format(context, static_cast<const Pokemon::Pokemon4PT &>(context.pokemon));
                break;
            case Enums::GameVersion::HGSS:
                verifyGeneration4Format(context, static_cast<const Pokemon::Pokemon4HGSS &>(context.pokemon));
                break;
            default:
                break;
        }
    }

    void verifyMiscGeneration5(const AnalysisContext &context)
    {
        if (context.formatGroup == Enums::GameVersion::BW)
        {
            verifyGeneration5Format(context, static_cast<const Pokemon::Pokemon5BW &>(context.pokemon));
        }
        else if (context.formatGroup == Enums::GameVersion::B2W2)
        {
            verifyGeneration5Format(context, static_cast<const Pokemon::Pokemon5B2W2 &>(context.pokemon));
        }
    }

    void verifyMiscGeneration6(const AnalysisContext &context)
    {
        if (context.formatGroup == Enums::GameVersion::XY)
        {
            const auto &pokemon = static_cast<const Pokemon::Pokemon6XY &>(context.pokemon);
            verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());
        }
        else if (context.formatGroup == Enums::GameVersion::ORAS)
        {
            const auto &pokemon = static_cast<const Pokemon::Pokemon6ORAS &>(context.pokemon);
            verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());
        }
    }

    void verifyMiscGeneration7(const AnalysisContext &context)
    {
        uint8_t resortEventStatus = 0;
        if (context.formatGroup == Enums::GameVersion::SM)
        {
            const auto &pokemon = static_cast<const Pokemon::Pokemon7SM &>(context.pokemon);
            verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());
            resortEventStatus = pokemon.resortEventStatus();
        }
        else if (context.formatGroup == Enums::GameVersion::USUM)
        {
            const auto &pokemon = static_cast<const Pokemon::Pokemon7USUM &>(context.pokemon);
            verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());
            resortEventStatus = pokemon.resortEventStatus();
        }
        if (resortEventStatus >= RESORT_EVENT_STATE_MAXIMUM)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferBad);
        }
    }

    void verifyMisc7LGPE(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon7LGPE &>(context.pokemon);
        verifyLetsGoSizes(context, pokemon);

        // A partner starter keeps its own CP, mood and spirit.
        const bool isStarter = (pokemon.speciesID() == PIKACHU_SPECIES && pokemon.form() == 8) || (pokemon.speciesID() == EEVEE_SPECIES && pokemon.form() == 1);
        const int expectedCombatPower = calculateCombatPower(pokemon);
        if (pokemon.cp() != expectedCombatPower && !isStarter)
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectCP, std::to_string(expectedCombatPower));
        }

        if (pokemon.receivedHour() >= 24 || pokemon.receivedMinute() >= 60 || pokemon.receivedSecond() >= 60)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::DateLocalInvalidTime);
        }
        if (!isReceivedDateValid(context, pokemon))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::DateLocalInvalidDate);
        }

        if (isStoredSlot(context, StorageSlotType::Party) || isStarter)
        {
            return;
        }
        const std::string initialValue = std::to_string(Pokemon::Pokemon7LGPE::INITIAL_SPIRIT_MOOD);
        if (pokemon.spirit() != Pokemon::Pokemon7LGPE::INITIAL_SPIRIT_MOOD)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen7BSocialShouldBe100Spirit, initialValue);
        }
        if (pokemon.mood() != Pokemon::Pokemon7LGPE::INITIAL_SPIRIT_MOOD)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::Gen7BSocialShouldBe100Mood, initialValue);
        }
    }
}
