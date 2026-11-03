// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ball/BallVerifier.cs, BallUseLegality.cs and
// BallContext6/7/HOME.cs: ball.
//
// The checks read the encounter engine's original template (EncounterOriginal), as PKHeX's do.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <optional>
#include <string>

#include "Enums/Ball.h"
#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg7.h"
#include "Legality/Encounters/EncounterInvalid.h"
#include "Legality/Encounters/EncounterSlot3.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/EncounterSlot8.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/EncounterStatic5Entree.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/Verifiers/BreedingPermissionTable.h"
#include "Legality/Verifiers/BreedingRules.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Names/RibbonNames.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        using Enums::Ball;

        constexpr uint16_t NINCADA_SPECIES = 290;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint16_t SPINDA_SPECIES = 327;
        constexpr uint16_t BELDUM_SPECIES = 374;
        constexpr uint16_t TAPU_KOKO_SPECIES = 785;
        constexpr uint16_t TAPU_FINI_SPECIES = 788;
        // Species the Gen 6/7 breeding tables single out.
        constexpr uint16_t CHIKORITA_SPECIES = 152;
        constexpr uint16_t CYNDAQUIL_SPECIES = 155;
        constexpr uint16_t TOTODILE_SPECIES = 158;
        constexpr uint16_t NIDORAN_FEMALE_SPECIES = 29;
        constexpr uint16_t NIDORAN_MALE_SPECIES = 32;
        constexpr uint16_t MAGNEMITE_SPECIES = 81;
        constexpr uint16_t VOLTORB_SPECIES = 100;
        constexpr uint16_t KANGASKHAN_SPECIES = 115;
        constexpr uint16_t STARYU_SPECIES = 120;
        constexpr uint16_t TAUROS_SPECIES = 128;
        constexpr uint16_t DITTO_SPECIES = 132;
        constexpr uint16_t TYROGUE_SPECIES = 236;
        constexpr uint16_t MILTANK_SPECIES = 241;
        constexpr uint16_t TREECKO_SPECIES = 252;
        constexpr uint16_t TORCHIC_SPECIES = 255;
        constexpr uint16_t MUDKIP_SPECIES = 258;
        constexpr uint16_t PLUSLE_SPECIES = 311;
        constexpr uint16_t MINUN_SPECIES = 312;
        constexpr uint16_t VOLBEAT_SPECIES = 313;
        constexpr uint16_t ILLUMISE_SPECIES = 314;
        constexpr uint16_t LUNATONE_SPECIES = 337;
        constexpr uint16_t SOLROCK_SPECIES = 338;
        constexpr uint16_t KECLEON_SPECIES = 352;
        constexpr uint16_t DUSKULL_SPECIES = 355;
        constexpr uint16_t TURTWIG_SPECIES = 387;
        constexpr uint16_t CHIMCHAR_SPECIES = 390;
        constexpr uint16_t PIPLUP_SPECIES = 393;
        constexpr uint16_t BRONZOR_SPECIES = 436;
        constexpr uint16_t HAPPINY_SPECIES = 440;
        constexpr uint16_t ROTOM_SPECIES = 479;
        constexpr uint16_t SNIVY_SPECIES = 495;
        constexpr uint16_t TEPIG_SPECIES = 498;
        constexpr uint16_t OSHAWOTT_SPECIES = 501;
        constexpr uint16_t DEERLING_SPECIES = 585;
        constexpr uint16_t ARCHEN_SPECIES = 566;
        constexpr uint16_t KLINK_SPECIES = 599;
        constexpr uint16_t GOLETT_SPECIES = 622;
        constexpr uint16_t FLABEBE_SPECIES = 669;
        constexpr uint16_t TYRUNT_SPECIES = 696;
        constexpr uint16_t AMAURA_SPECIES = 698;
        constexpr uint16_t PUMPKABOO_SPECIES = 710;

        constexpr uint8_t VERSION_SUN = 30;
        constexpr uint8_t VERSION_MOON = 31;
        constexpr uint8_t VERSION_FIRERED = 4;
        constexpr uint8_t VERSION_LEAFGREEN = 5;
        constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
        constexpr uint8_t VERSION_LEAFGREEN_SWITCH = 57;
        constexpr uint8_t VERSION_LEGENDS_ARCEUS = 47;
        // PKHeX Locations: Entree Forest, where the Dream World's visitors are met.
        constexpr uint16_t LOCATION_ENTREE_FOREST = 75;
        // PKHeX Locations.SafariLocation_RSE / _FRLG, Locations4.MarshLocation_DPPt / SafariLocation_HGSS,
        // Locations8b.IsMarsh, EncounterStatic5Radar.Location and Locations.PokeWalker4.
        constexpr uint16_t LOCATION_SAFARI_ZONE_HOENN = 57;
        constexpr uint16_t LOCATION_SAFARI_ZONE_KANTO = 136;
        constexpr uint16_t LOCATION_GREAT_MARSH = 52;
        constexpr uint16_t LOCATION_SAFARI_ZONE_JOHTO = 202;
        constexpr uint16_t LOCATION_GREAT_MARSH_BDSP_FIRST = 219;
        constexpr uint16_t LOCATION_GREAT_MARSH_BDSP_LAST = 224;
        constexpr uint16_t LOCATION_DREAM_RADAR = 30015;
        constexpr uint16_t LOCATION_POKEWALKER = 233;
        constexpr int ABILITY_NUMBER_HIDDEN = 4;

        constexpr uint64_t ballBit(Ball ball)
        {
            return 1ull << static_cast<int>(ball);
        }

        // PKHeX BallUseLegality's permit masks.
        constexpr uint64_t WILD_REGULAR = ballBit(Ball::Master) | ballBit(Ball::Ultra) | ballBit(Ball::Great) | ballBit(Ball::Poke);
        constexpr uint64_t WILD_KURT_4 = ballBit(Ball::Fast) | ballBit(Ball::Level) | ballBit(Ball::Lure) | ballBit(Ball::Heavy) | ballBit(Ball::Love) | ballBit(Ball::Friend) | ballBit(Ball::Moon);
        constexpr uint64_t WILD_ENHANCE_3 = ballBit(Ball::Net) | ballBit(Ball::Dive) | ballBit(Ball::Nest) | ballBit(Ball::Repeat) | ballBit(Ball::Timer) | ballBit(Ball::Luxury) | ballBit(Ball::Premier);
        constexpr uint64_t WILD_ENHANCE_4 = ballBit(Ball::Dusk) | ballBit(Ball::Heal) | ballBit(Ball::Quick);
        constexpr uint64_t WILD_ENHANCE_5 = ballBit(Ball::Dream);
        constexpr uint64_t WILD_ENHANCE_7 = ballBit(Ball::Beast);
        constexpr uint64_t WILD_ENHANCE_8 = ballBit(Ball::Dream) | ballBit(Ball::Safari) | ballBit(Ball::Sport);
        constexpr uint64_t WILD_BALLS_8A = ballBit(Ball::LAPoke) | ballBit(Ball::LAGreat) | ballBit(Ball::LAUltra) | ballBit(Ball::LAFeather) | ballBit(Ball::LAWing) | ballBit(Ball::LAJet) | ballBit(Ball::LAHeavy) | ballBit(Ball::LALeaden) | ballBit(Ball::LAGigaton);
        constexpr uint64_t WILD_BALLS_1 = ballBit(Ball::Poke);
        constexpr uint64_t WILD_BALLS_3 = WILD_REGULAR | WILD_ENHANCE_3;
        constexpr uint64_t WILD_BALLS_3_SWITCH = WILD_BALLS_3 & ~ballBit(Ball::Premier) & ~ballBit(Ball::Dive);
        constexpr uint64_t WILD_BALLS_4_DPPT = WILD_BALLS_3 | WILD_ENHANCE_4;
        constexpr uint64_t WILD_BALLS_4_HGSS = WILD_BALLS_4_DPPT | WILD_KURT_4;
        constexpr uint64_t WILD_BALLS_5 = WILD_BALLS_4_DPPT;
        constexpr uint64_t DREAM_WORLD_BALLS = WILD_BALLS_5 | WILD_ENHANCE_5;
        constexpr uint64_t WILD_BALLS_6 = WILD_BALLS_5;
        constexpr uint64_t WILD_BALLS_7 = WILD_BALLS_4_HGSS | WILD_ENHANCE_7;
        constexpr uint64_t WILD_BALLS_8 = WILD_BALLS_7 | WILD_ENHANCE_8;
        constexpr uint64_t WILD_BALLS_7B = WILD_REGULAR | ballBit(Ball::Premier);
        constexpr uint64_t WILD_BALLS_9 = WILD_BALLS_8;
        constexpr uint64_t CURRY_BALLS = ballBit(Ball::Poke) | ballBit(Ball::Great) | ballBit(Ball::Ultra);

        /// PKHeX BallVerificationResult, the failures only.
        enum class BallFailure : uint8_t
        {
            None,
            Encounter,
            CaptureHeavy,
            InheritAbility,
            InheritSpecies,
            InheritCherish,
            InheritMaster,
            OutOfRange,
        };

        ResultCode getFailureCode(BallFailure failure)
        {
            switch (failure)
            {
            case BallFailure::CaptureHeavy:
                return ResultCode::BallHeavy;
            case BallFailure::InheritAbility:
                return ResultCode::BallAbility;
            case BallFailure::InheritSpecies:
                return ResultCode::BallSpecies;
            case BallFailure::InheritCherish:
                return ResultCode::BallEggCherish;
            case BallFailure::InheritMaster:
                return ResultCode::BallEggMaster;
            case BallFailure::OutOfRange:
                return ResultCode::BallUnavailable;
            default:
                return ResultCode::BallEncounterMismatch;
            }
        }

        bool isBallPermitted(uint64_t permit, uint8_t ballId)
        {
            return ballId < 64 && (permit & (1ull << ballId)) != 0;
        }

        BallFailure verifyBallEquals(uint8_t ballId, uint64_t permit)
        {
            return isBallPermitted(permit, ballId) ? BallFailure::None : BallFailure::Encounter;
        }

        BallFailure verifyBallEquals(uint8_t ballId, Ball permit)
        {
            return ballId == static_cast<uint8_t>(permit) ? BallFailure::None : BallFailure::Encounter;
        }

        /// PKHeX BallUseLegality.GetWildBalls.
        uint64_t getWildBalls(int generation, Enums::GameVersion originGroup, uint8_t originVersion, const AnalysisOptions &options)
        {
            // PKHeX ParseSettings.IsExclusivelyNintendoSwitchGBA: a FireRed/LeafGreen Pokemon in a Switch save
            // can only have come from the Switch release.
            const bool isExclusivelySwitchGameBoyAdvance = allowsSwitchGameBoyAdvanceEra(options) && !allowsGameBoyAdvanceCartridgeEra(options);
            const bool isFireRedLeafGreenOrigin = originVersion == VERSION_FIRERED || originVersion == VERSION_LEAFGREEN;
            switch (generation)
            {
            case 1:
            case 2:
                return WILD_BALLS_1;
            case 3:
                if (isFireRedLeafGreenOrigin && isExclusivelySwitchGameBoyAdvance)
                {
                    return WILD_BALLS_3_SWITCH;
                }
                return (originVersion == VERSION_FIRERED_SWITCH || originVersion == VERSION_LEAFGREEN_SWITCH) ? WILD_BALLS_3_SWITCH : WILD_BALLS_3;
            case 4:
                return originGroup == Enums::GameVersion::HGSS ? WILD_BALLS_4_HGSS : WILD_BALLS_4_DPPT;
            case 5:
                return WILD_BALLS_5;
            case 6:
                return WILD_BALLS_6;
            case 7:
                return originGroup == Enums::GameVersion::GG ? WILD_BALLS_7B : WILD_BALLS_7;
            case 8:
                if (originGroup == Enums::GameVersion::BDSP)
                {
                    return WILD_BALLS_4_HGSS;
                }
                return originGroup == Enums::GameVersion::PLA ? WILD_BALLS_8A : WILD_BALLS_8;
            case 9:
                return WILD_BALLS_9;
            default:
                return 0;
            }
        }

        /// The BallType bit each Gen 6+ breeding table keys a ball by (PKHeX BallContext*.GetPermitBit).
        enum class BallFamily : uint8_t
        {
            Gen3 = 0,
            Gen4 = 1,
            Safari = 2,
            Apricorn = 3,
            Sport = 4,
            Dream = 5,
            Beast = 6,
            None = 9,
        };

        BallFamily getBallFamily(uint8_t ballId)
        {
            switch (static_cast<Ball>(ballId))
            {
            case Ball::Ultra:
            case Ball::Great:
            case Ball::Poke:
            case Ball::Net:
            case Ball::Dive:
            case Ball::Nest:
            case Ball::Repeat:
            case Ball::Timer:
            case Ball::Luxury:
            case Ball::Premier:
                return BallFamily::Gen3;
            case Ball::Safari:
                return BallFamily::Safari;
            case Ball::Dusk:
            case Ball::Heal:
            case Ball::Quick:
                return BallFamily::Gen4;
            case Ball::Fast:
            case Ball::Level:
            case Ball::Lure:
            case Ball::Heavy:
            case Ball::Love:
            case Ball::Friend:
            case Ball::Moon:
                return BallFamily::Apricorn;
            case Ball::Sport:
                return BallFamily::Sport;
            case Ball::Dream:
                return BallFamily::Dream;
            case Ball::Beast:
                return BallFamily::Beast;
            default:
                return BallFamily::None;
            }
        }

        /// PKHeX BallContext*.CanBreedWithBall's table lookup. Gen 6 has no Beast Ball family.
        bool isFamilyPermitted(const uint8_t *permitTable, size_t permitLength, uint16_t speciesId, BallFamily family, bool hasBeastFamily)
        {
            if (speciesId >= permitLength || family == BallFamily::None || (family == BallFamily::Beast && !hasBeastFamily))
            {
                return false;
            }
            return (permitTable[speciesId] & (1 << static_cast<int>(family))) != 0;
        }

        // PKHeX BallContext6/7: the Hidden Ability combinations each ball family cannot carry.
        bool isBannedHiddenGen6(BallFamily family, uint16_t speciesId, uint8_t formId)
        {
            switch (family)
            {
            case BallFamily::Gen3:
                switch (speciesId)
                {
                case CHIKORITA_SPECIES:
                case CYNDAQUIL_SPECIES:
                case TOTODILE_SPECIES:
                    return true;
                case DEERLING_SPECIES:
                    return formId != 0;
                case PUMPKABOO_SPECIES:
                    return formId == 3;
                default:
                    return false;
                }
            case BallFamily::Gen4:
                switch (speciesId)
                {
                case CHIKORITA_SPECIES:
                case CYNDAQUIL_SPECIES:
                case TOTODILE_SPECIES:
                case TREECKO_SPECIES:
                case TORCHIC_SPECIES:
                case MUDKIP_SPECIES:
                case TURTWIG_SPECIES:
                case CHIMCHAR_SPECIES:
                case PIPLUP_SPECIES:
                case SNIVY_SPECIES:
                case TEPIG_SPECIES:
                case OSHAWOTT_SPECIES:
                case ARCHEN_SPECIES:
                case TYRUNT_SPECIES:
                case AMAURA_SPECIES:
                    return true;
                case DEERLING_SPECIES:
                    return formId != 0;
                default:
                    return false;
                }
            case BallFamily::Safari:
            case BallFamily::Apricorn:
            case BallFamily::Sport:
                return true;
            case BallFamily::Dream:
                return speciesId == PLUSLE_SPECIES || speciesId == MINUN_SPECIES || speciesId == KECLEON_SPECIES || speciesId == DUSKULL_SPECIES;
            default:
                return false;
            }
        }

        bool isBannedHiddenGen7(BallFamily family, uint16_t speciesId, uint8_t formId)
        {
            const bool isBlueFlabebe = speciesId == FLABEBE_SPECIES && formId == 3;
            switch (family)
            {
            case BallFamily::Gen3:
            case BallFamily::Gen4:
                return speciesId == ARCHEN_SPECIES || speciesId == TYRUNT_SPECIES || speciesId == AMAURA_SPECIES;
            case BallFamily::Safari:
                switch (speciesId)
                {
                case NIDORAN_FEMALE_SPECIES:
                case NIDORAN_MALE_SPECIES:
                case VOLBEAT_SPECIES:
                case ILLUMISE_SPECIES:
                case MAGNEMITE_SPECIES:
                case VOLTORB_SPECIES:
                case KANGASKHAN_SPECIES:
                case TAUROS_SPECIES:
                case DITTO_SPECIES:
                case MILTANK_SPECIES:
                case BELDUM_SPECIES:
                case BRONZOR_SPECIES:
                case HAPPINY_SPECIES:
                case TYROGUE_SPECIES:
                case STARYU_SPECIES:
                case LUNATONE_SPECIES:
                case SOLROCK_SPECIES:
                case ROTOM_SPECIES:
                case KLINK_SPECIES:
                case GOLETT_SPECIES:
                    return true;
                default:
                    return false;
                }
            case BallFamily::Apricorn:
                return speciesId == NIDORAN_FEMALE_SPECIES || speciesId == NIDORAN_MALE_SPECIES || speciesId == VOLTORB_SPECIES || speciesId == BRONZOR_SPECIES || isBlueFlabebe;
            case BallFamily::Beast:
                return speciesId == VOLTORB_SPECIES || isBlueFlabebe;
            default:
                return false;
            }
        }

        /// PKHeX BallContextHOME.IsAbilityPatchPossible: from Gen 8 on an Ability Patch could have
        /// given the Hidden Ability, except to the species whose Hidden Ability is not distinct.
        bool isAbilityPatchPossible(int formatGeneration, uint16_t speciesId)
        {
            if (formatGeneration <= 7)
            {
                return false;
            }
            return speciesId != LUNATONE_SPECIES && speciesId != SOLROCK_SPECIES && speciesId != ROTOM_SPECIES && speciesId != ARCHEN_SPECIES;
        }

        /// PKHeX BallVerifier.VerifyBallEgg and VerifyBallInherited.
        BallFailure verifyBallEgg(const AnalysisContext &context, int generation, uint8_t ballId)
        {
            if (generation < 6)
            {
                return verifyBallEquals(ballId, Ball::Poke);
            }
            if (ballId == static_cast<uint8_t>(Ball::Master))
            {
                return BallFailure::InheritMaster;
            }
            if (ballId == static_cast<uint8_t>(Ball::Cherish))
            {
                return BallFailure::InheritCherish;
            }
            const uint8_t lastBall = static_cast<uint8_t>(generation == 6 ? Ball::Dream : Ball::Beast);
            if (ballId > lastBall)
            {
                return BallFailure::OutOfRange;
            }
            const std::optional<Evolutions::EvolutionCriteria> eggStage = getEggStage(context);
            if (!eggStage)
            {
                return BallFailure::None;
            }
            if (ballId == static_cast<uint8_t>(Ball::Poke))
            {
                return BallFailure::None;
            }
            const uint16_t speciesId = eggStage->speciesId;
            // An empty permit row is a species that cannot breed: PKHeX would have no egg encounter,
            // and an unmatched encounter accepts any ball.
            const uint8_t *permitTable = generation == 6 ? BALL_PERMIT_GENERATION_6 : (generation == 7 ? BALL_PERMIT_GENERATION_7 : BALL_PERMIT_HOME);
            const size_t permitLength = generation == 6 ? BALL_PERMIT_GENERATION_6_LENGTH : (generation == 7 ? BALL_PERMIT_GENERATION_7_LENGTH : BALL_PERMIT_HOME_LENGTH);
            if (speciesId < permitLength && permitTable[speciesId] == 0)
            {
                return BallFailure::None;
            }
            const BallFamily family = getBallFamily(ballId);
            if (generation == 6 || generation == 7)
            {
                const bool isGenerationSix = generation == 6;
                const bool isPermitted = isGenerationSix ? isFamilyPermitted(BALL_PERMIT_GENERATION_6, BALL_PERMIT_GENERATION_6_LENGTH, speciesId, family, false) : isFamilyPermitted(BALL_PERMIT_GENERATION_7, BALL_PERMIT_GENERATION_7_LENGTH, speciesId, family, true);
                if (!isPermitted)
                {
                    return BallFailure::InheritSpecies;
                }
                const bool isHidden = context.pokemon.abilityNumber() == ABILITY_NUMBER_HIDDEN;
                const bool isBanned = isGenerationSix ? isBannedHiddenGen6(family, speciesId, eggStage->formId) : isBannedHiddenGen7(family, speciesId, eggStage->formId);
                if (!isAbilityPatchPossible(context.formatGeneration, speciesId) && isHidden && isBanned)
                {
                    return BallFailure::InheritAbility;
                }
                return BallFailure::None;
            }
            if (context.encounter.originGroup == Enums::GameVersion::BDSP && speciesId == SPINDA_SPECIES)
            {
                return verifyBallEquals(ballId, WILD_BALLS_4_HGSS); // a Brilliant Diamond/Shining Pearl Spinda cannot go to HOME
            }
            return isFamilyPermitted(BALL_PERMIT_HOME, BALL_PERMIT_HOME_LENGTH, speciesId, family, true) ? BallFailure::None : BallFailure::InheritSpecies;
        }

        bool hasCurryMark(const Pokemon::Pokemon &pokemon)
        {
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
            for (const Names::RibbonDefinition &ribbon : Names::getRibbonDefinitions(pokemon.getGameGroup()))
            {
                if (ribbon.spriteStem != nullptr && std::string(ribbon.spriteStem) == "ribbonmarkcurry")
                {
                    return Names::getRibbonValue(entityData, ribbon) != 0;
                }
            }
            return false;
        }
    }

    namespace
    {
        /// PKHeX BallVerifier.VerifyBall(enc, current, pk), reading the engine's template.
        BallFailure verifyBallTemplate(const AnalysisContext &context, const Encounters::Encounterable &encounter, uint8_t ballId)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t originVersion = pokemon.originGame();
            const Enums::Ball fixedBall = encounter.fixedBall();

            // PKHeX IsReplacedBall: Sword/Shield replaces a Legends: Arceus ball with a Poke Ball,
            // except a fixed one (Enamorus's Cherish Ball).
            const bool hasStandardFixedBall = fixedBall != Ball::None && fixedBall < Ball::Strange;
            if (context.formatGroup == Enums::GameVersion::SWSH && encounter.version() == Enums::GameVersion::PLA && !hasStandardFixedBall)
            {
                return verifyBallEquals(ballId, Ball::Poke);
            }

            // Sun/Moon's catch rate makes these species impossible to catch in a Heavy Ball.
            const uint16_t encounterSpecies = encounter.speciesId();
            const bool isNoHeavyBallSpecies = encounterSpecies == BELDUM_SPECIES || (encounterSpecies >= TAPU_KOKO_SPECIES && encounterSpecies <= TAPU_FINI_SPECIES);
            if (ballId == static_cast<uint8_t>(Ball::Heavy) && !Encounters::encounterAs<Encounters::EncounterEgg7>(encounter) && (originVersion == VERSION_SUN || originVersion == VERSION_MOON) && isNoHeavyBallSpecies)
            {
                return BallFailure::CaptureHeavy;
            }

            if (Encounters::encounterAs<Encounters::EncounterInvalid>(encounter))
            {
                return BallFailure::None; // PKHeX EncounterInvalid accepts any ball
            }
            if (const Encounters::EncounterSlot8GO *pogo = Encounters::encounterAs<Encounters::EncounterSlot8GO>(encounter))
            {
                return pogo->isBallValid(static_cast<Ball>(ballId), pokemon.speciesID(), pokemon) ? BallFailure::None : BallFailure::Encounter;
            }
            if (fixedBall != Ball::None)
            {
                return verifyBallEquals(ballId, fixedBall);
            }
            if (Encounters::encounterAs<Encounters::EncounterSlot8>(encounter) && hasCurryMark(pokemon))
            {
                return verifyBallEquals(ballId, CURRY_BALLS);
            }
            if (encounter.asEgg() != nullptr)
            {
                return verifyBallEgg(context, encounter.generation(), ballId);
            }
            if (Encounters::encounterAs<Encounters::EncounterStatic5Entree>(encounter))
            {
                return verifyBallEquals(ballId, DREAM_WORLD_BALLS);
            }
            // PKHeX GetWildBalls(enc.Generation, enc.Version): GO Park counts as Let's Go.
            // A shared template's version is already a group.
            const bool isGoPark = encounter.version() == Encounters::VERSION_POKEMON_GO && encounter.generation() == 7;
            const Enums::GameVersion versionGroup = Enums::getGameGroup(encounter.version());
            const Enums::GameVersion originGroup = isGoPark ? Enums::GameVersion::GG : (versionGroup == Enums::GameVersion::Invalid ? encounter.version() : versionGroup);
            return verifyBallEquals(ballId, getWildBalls(encounter.generation(), originGroup, originVersion, context.options));
        }

        /// PKHeX BallVerifier.VerifyEvolvedShedinja, reading the engine's template.
        BallFailure verifyEvolvedShedinjaTemplate(const AnalysisContext &context, const Encounters::Encounterable &encounter, uint8_t ballId)
        {
            const Evolutions::EvolutionHistory &history = context.evolutionHistory();
            // Gen 3 evolution copies the current ball.
            if (Encounters::encounterAs<Encounters::EncounterSlot3>(encounter) && history.getChain(Enums::GameVersion::RSE).size() == 2)
            {
                return verifyBallTemplate(context, encounter, ballId);
            }
            // D/P/Pt keeps the HG/SS ball apart; only the Bug-Catching Contest uses the Sport Ball.
            const Encounters::EncounterSlot4 *slot4 = Encounters::encounterAs<Encounters::EncounterSlot4>(encounter);
            if (slot4 != nullptr && slot4->slotType() == static_cast<uint8_t>(RNG::SlotType4::BugContest) && history.getChain(Enums::GameVersion::DP).size() == 2)
            {
                return (ballId == static_cast<uint8_t>(Ball::Sport) || ballId == static_cast<uint8_t>(Ball::Poke)) ? BallFailure::None : BallFailure::Encounter;
            }
            return verifyBallEquals(ballId, Ball::Poke);
        }
    }

    void verifyBall(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGeneration <= 2 || !pokemon.hasBall())
        {
            return;
        }
        if (const Encounters::Encounterable *encounterOriginal = Encounters::getEncounterOriginal(context))
        {
            const uint8_t ballId = pokemon.ball();
            const bool isEvolvedShedinja = encounterOriginal->speciesId() == NINCADA_SPECIES && pokemon.speciesID() == SHEDINJA_SPECIES;
            const BallFailure failure = isEvolvedShedinja ? verifyEvolvedShedinjaTemplate(context, *encounterOriginal, ballId) : verifyBallTemplate(context, *encounterOriginal, ballId);
            if (failure != BallFailure::None)
            {
                context.add(Severity::Invalid, CheckIdentifier::Ball, getFailureCode(failure));
            }
        }
    }
}
