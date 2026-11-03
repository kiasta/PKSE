/**
 * The RNG library under Legality/RNG, checked against PKHeX's own unit tests for it
 * (PKHeX.Core.Tests: Entity/LCRNGTest.cs, Entity/PIDIVTests.cs, General/Xoroshiro128Tests.cs,
 * General/Xoroshiro128bTests.cs, Legality/RNG/MethodJChecks.cs, Legality/RNG/RaidTests.cs).
 *
 * Every vector here is one PKHeX asserts. A seed reversal that loses a candidate does not crash or
 * look wrong; it just stops matching a correlation, and the legality checker then calls a real
 * Pokemon's PID unrelated to its IVs. These fixed vectors are where that shows up first.
 *
 * PKHeX's Wild8a/Wild8b tests and its shadow team tests are not here: the first two drive PKHeX's
 * entity generators, which PKSE does not port, and the third needs the Colosseum/XD team data,
 * which belongs to the encounter tables rather than to the RNG.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Test/TestSuite.h"
#include "Trainer/Bank.h"
#include "Legality/RNG/ChannelJirachi.h"
#include "Legality/RNG/ClassicEraRNG.h"
#include "Legality/RNG/CommonEvent3.h"
#include "Legality/RNG/EncounterSlotRNG.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"
#include "Legality/RNG/LCRNGReversalSkip.h"
#include "Legality/RNG/MRNG.h"
#include "Legality/RNG/MRNGReversal.h"
#include "Legality/RNG/MethodCXD.h"
#include "Legality/RNG/MethodFinder.h"
#include "Legality/RNG/MethodJ.h"
#include "Legality/RNG/MethodPokeSpot.h"
#include "Legality/RNG/PIDIV.h"
#include "Legality/RNG/PIDType.h"
#include "Legality/RNG/PokewalkerRNG.h"
#include "Legality/RNG/XDRNG.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Legality/RNG/Xoroshiro128Plus8b.h"
#include "Legality/RNG/Xoroshiro128PlusReversal.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        namespace RNG = Legality::RNG;

        enum Chunk
        {
            SequenceChunk = 0,
            XoroshiroReversalChunk,
            MethodFinderGen3Chunk,
            MethodFinderGen4And5Chunk,
            SeedReversalChunk,
            MethodJChunk,
            CHUNK_COUNT,
        };

        std::string hexText(uint64_t printedNumber)
        {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "0x%llX", (unsigned long long)printedNumber);
            return std::string(buffer);
        }

        struct PIDIVFixture
        {
            uint16_t species;
            uint32_t PID;
            uint8_t ivs[6]; // PKHeX's IVs order: HP, ATK, DEF, SPE, SPA, SPD
            uint16_t TID16;
            uint16_t SID16;
            uint8_t gender;
            uint8_t otGender;
            uint8_t originGame;
            RNG::PIDType expected;
        };

        /// A blank entity of `group`'s format carrying the fixture's fields, the way PKHeX's tests
        /// build `new PK3 { ... }`. Null when the group has no entity class.
        std::unique_ptr<Pokemon::Pokemon> makeFixturePokemon(Enums::GameVersion group, const PIDIVFixture &fixture)
        {
            std::vector<std::byte> record(Trainer::Bank::recordSizeFor(group), std::byte{0});
            std::unique_ptr<Pokemon::Pokemon> pokemon = Trainer::Bank::makePokemon(group, record);
            if (!pokemon)
            {
                return pokemon;
            }
            // Species before PID: the gender setter reads the species' ratio, and a gender that the PID
            // already gives only writes the stored bits.
            if (fixture.species != 0)
            {
                pokemon->setSpecies(fixture.species);
            }
            pokemon->setPID(fixture.PID);
            for (int statIndex = 0; statIndex < 6; statIndex++)
            {
                pokemon->setIV(statIndex, fixture.ivs[statIndex]);
            }
            pokemon->setTID16(fixture.TID16);
            pokemon->setSID16(fixture.SID16);
            pokemon->setOTGender(fixture.otGender);
            pokemon->setOriginGame(fixture.originGame);
            // Gen 3 derives gender from the PID, and its setter rerolls the PID. Later formats store
            // gender, and setSpecies() seeded it from the blank PID, so it must be written even for male.
            if (!Enums::isGen3Group(group))
            {
                pokemon->setGender(fixture.gender);
            }
            return pokemon;
        }

        uint32_t packIVs(const Pokemon::Pokemon &pokemon)
        {
            return (uint32_t)pokemon.ivHP() | ((uint32_t)pokemon.ivATK() << 5) | ((uint32_t)pokemon.ivDEF() << 10) | ((uint32_t)pokemon.ivSPE() << 15) | ((uint32_t)pokemon.ivSPA() << 20) | ((uint32_t)pokemon.ivSPD() << 25);
        }

        void checkFixtures(Context &context, Enums::GameVersion group, std::span<const PIDIVFixture> fixtures)
        {
            for (const PIDIVFixture &fixture : fixtures)
            {
                const std::string where = std::string("PID ") + hexText(fixture.PID);
                std::unique_ptr<Pokemon::Pokemon> pokemon = makeFixturePokemon(group, fixture);
                if (!context.check(pokemon != nullptr, "no entity class for a PIDIV fixture's format", where))
                {
                    continue;
                }
                if (!context.check(pokemon->pid() == fixture.PID, "building a PIDIV fixture changed its PID", where + ", now " + hexText(pokemon->pid())))
                {
                    continue;
                }
                const RNG::PIDIV result = RNG::MethodFinder::analyze(*pokemon);
                context.check(result.type == fixture.expected, "MethodFinder classified a PKHeX test vector differently", where + ": expected " + RNG::getPIDTypeSourceName(fixture.expected) + ", got " + RNG::getPIDTypeSourceName(result.type));
            }
        }

        // PKHeX LCRNGTest, Xoroshiro128Tests.Forward/Reverse and Xoroshiro128bTests.
        void checkSequences(Context &context)
        {
            struct DistanceVector
            {
                uint32_t start;
                uint32_t end;
                uint32_t expected;
            };
            static constexpr DistanceVector LCRNG_DISTANCES[] = {
                {0x12345u, 0xAEA0DF8Cu, 12345u},
                {0xBADC0DEDu, 0xBADC0DEDu, 0u},
                {0u, 0x0A3561A1u, UINT32_MAX},
                {0x0A3561A1u, 0u, 1u},
            };
            static constexpr DistanceVector XDRNG_DISTANCES[] = {
                {8675309u, 0x75C29428u, 8675309u},
                {0xBADC0DEDu, 0xBADC0DEDu, 0u},
                {0u, 0xA170F641u, UINT32_MAX},
                {0xA170F641u, 0u, 1u},
            };
            for (const DistanceVector &vector : LCRNG_DISTANCES)
            {
                const uint32_t distance = RNG::LCRNG::getDistance(vector.start, vector.end);
                context.check(distance == vector.expected, "LCRNG distance", hexText(vector.start) + " -> " + hexText(vector.end) + " gave " + std::to_string(distance));
            }
            for (const DistanceVector &vector : XDRNG_DISTANCES)
            {
                const uint32_t distance = RNG::XDRNG::getDistance(vector.start, vector.end);
                context.check(distance == vector.expected, "XDRNG distance", hexText(vector.start) + " -> " + hexText(vector.end) + " gave " + std::to_string(distance));
            }

            constexpr uint64_t TARGET_FIRST_STATE = 0x339C61939607D435;
            constexpr uint64_t TARGET_SECOND_STATE = 0xF4DD6E2E9698D1B0;
            constexpr int EXPECTED_ADVANCES = 98;
            constexpr int ADVANCE_LIMIT = 1000;

            int forwardAdvances = -1;
            RNG::Xoroshiro128Plus forward(1, 1);
            for (int i = 0; i < ADVANCE_LIMIT; i++)
            {
                (void)forward.next();
                if (forward.equals(TARGET_FIRST_STATE, TARGET_SECOND_STATE))
                {
                    forwardAdvances = i;
                    break;
                }
            }
            context.check(forwardAdvances == EXPECTED_ADVANCES, "Xoroshiro128Plus forward advance count", std::to_string(forwardAdvances));

            int reverseAdvances = -1;
            RNG::Xoroshiro128Plus reverse(TARGET_FIRST_STATE, TARGET_SECOND_STATE);
            for (int i = 0; i < ADVANCE_LIMIT; i++)
            {
                (void)reverse.previous();
                if (reverse.equals(1, 1))
                {
                    reverseAdvances = i;
                    break;
                }
            }
            context.check(reverseAdvances == EXPECTED_ADVANCES, "Xoroshiro128Plus reverse advance count", std::to_string(reverseAdvances));

            int forward8bAdvances = -1;
            RNG::Xoroshiro128Plus8b forward8b(1, 1);
            for (int i = 0; i < ADVANCE_LIMIT; i++)
            {
                (void)forward8b.next();
                if (forward8b.equals(TARGET_FIRST_STATE, TARGET_SECOND_STATE))
                {
                    forward8bAdvances = i;
                    break;
                }
            }
            context.check(forward8bAdvances == EXPECTED_ADVANCES, "Xoroshiro128Plus8b forward advance count", std::to_string(forward8bAdvances));

            int reverse8bAdvances = -1;
            RNG::Xoroshiro128Plus8b reverse8b(TARGET_FIRST_STATE, TARGET_SECOND_STATE);
            for (int i = 0; i < ADVANCE_LIMIT; i++)
            {
                (void)reverse8b.previous();
                if (reverse8b.equals(1, 1))
                {
                    reverse8bAdvances = i;
                    break;
                }
            }
            context.check(reverse8bAdvances == EXPECTED_ADVANCES, "Xoroshiro128Plus8b reverse advance count", std::to_string(reverse8bAdvances));
        }

        // PKHeX Xoroshiro128Tests.TestConsecutive/TestSkip and RaidTests.CheckDynamaxRaidCharizardPK8.
        void checkXoroshiroReversal(Context &context)
        {
            static constexpr uint64_t SEEDS[] = {
                0x5a62b550aa55aa55, 0x5a63b450aa55aa55, 0x5a6cb550aa55aa55, 0x5a6db450aa55aa55,
                0x9a508f54aa55aa55, 0x9a518e54aa55aa55, 0x9a5a8754aa55aa55, 0x9a5b8654aa55aa55,
                0x9a70a754aa55aa55, 0x9a71a654aa55aa55, 0x9a7aaf54aa55aa55, 0x9a7bae54aa55aa55,
                0xaa54ab55aa55aa55, 0xaa55aa55aa55aa55, 0xaa7e8b55aa55aa55, 0xaa7f8a55aa55aa55,
            };
            static constexpr uint32_t SECOND_OUTPUTS[] = {
                0x359D276C, 0x35BD486B, 0x375D316C, 0x377D526B,
                0x3BA454B2, 0x3BC475B1, 0x3C635EBA, 0x3C837FB9,
                0x379F74DA, 0x37BF95D9, 0x38607ED2, 0x38809FD1,
                0x3C2FD8B6, 0x3C4FF9B5, 0x38F40296, 0x39142395,
            };
            constexpr uint32_t FIRST_OUTPUT = 0xCCF314B0;
            constexpr uint32_t THIRD_OUTPUT = 0x43157D53;

            for (size_t seedIndex = 0; seedIndex < std::size(SEEDS); seedIndex++)
            {
                const uint64_t seed = SEEDS[seedIndex];
                const uint32_t secondOutput = SECOND_OUTPUTS[seedIndex];

                RNG::Xoroshiro128Plus forward(seed);
                const uint32_t firstActual = (uint32_t)forward.next();
                const uint32_t secondActual = (uint32_t)forward.next();
                const uint32_t thirdActual = (uint32_t)forward.next();
                context.check(firstActual == FIRST_OUTPUT && secondActual == secondOutput && thirdActual == THIRD_OUTPUT, "Xoroshiro128Plus outputs from a PKHeX test seed", hexText(seed));

                bool consecutiveFound = false;
                bool consecutiveAllMatch = true;
                RNG::XoroMachineConsecutive consecutive(FIRST_OUTPUT, secondOutput);
                while (consecutive.moveNext())
                {
                    RNG::Xoroshiro128Plus candidate(consecutive.current());
                    const uint32_t candidateFirst = (uint32_t)candidate.next();
                    const uint32_t candidateSecond = (uint32_t)candidate.next();
                    consecutiveAllMatch = consecutiveAllMatch && candidateFirst == FIRST_OUTPUT && candidateSecond == secondOutput;
                    consecutiveFound = consecutiveFound || consecutive.current() == seed;
                }
                context.check(consecutiveFound, "XoroMachineConsecutive missed a seed", hexText(seed));
                context.check(consecutiveAllMatch, "XoroMachineConsecutive yielded a seed with other outputs", hexText(seed));

                bool skipFound = false;
                bool skipAllMatch = true;
                RNG::XoroMachineSkip skip(FIRST_OUTPUT, THIRD_OUTPUT);
                while (skip.moveNext())
                {
                    RNG::Xoroshiro128Plus candidate(skip.current());
                    const uint32_t candidateFirst = (uint32_t)candidate.next();
                    (void)candidate.next();
                    const uint32_t candidateThird = (uint32_t)candidate.next();
                    skipAllMatch = skipAllMatch && candidateFirst == FIRST_OUTPUT && candidateThird == THIRD_OUTPUT;
                    skipFound = skipFound || skip.current() == seed;
                }
                context.check(skipFound, "XoroMachineSkip missed a seed", hexText(seed));
                context.check(skipAllMatch, "XoroMachineSkip yielded a seed with other outputs", hexText(seed));
            }

            // The raid Charizard's encryption constant and PID, from the PK8 bytes in RaidTests.
            constexpr uint32_t CHARIZARD_ENCRYPTION_CONSTANT = 0xC09FE765;
            constexpr uint32_t CHARIZARD_PID = 0xE20155EE;
            constexpr uint64_t CHARIZARD_RAID_SEED = 0xbefd08cf9e027d0a;
            bool raidSeedFound = false;
            RNG::XoroMachineSkip raidSeeds(CHARIZARD_ENCRYPTION_CONSTANT, CHARIZARD_PID);
            while (raidSeeds.moveNext())
            {
                raidSeedFound = raidSeedFound || raidSeeds.current() == CHARIZARD_RAID_SEED;
            }
            context.check(raidSeedFound, "XoroMachineSkip missed the raid Charizard's seed", hexText(CHARIZARD_RAID_SEED));
        }

        // PKHeX PIDIVTest.PIDIVMatchingTest3, Test3Unown, Test3Method3, Test3MiscCXD,
        // Test3MiscChannel, Test3Event and PIDIVMethod4IVs.
        void checkMethodFinderGen3(Context &context)
        {
            static constexpr uint8_t RUBY = 2;
            static constexpr PIDIVFixture FIXTURES[] = {
                {0, 0xE97E0000, {17, 19, 20, 16, 13, 12}, 0, 0, 0, 0, 0, RNG::PIDType::Method1},
                {0, 0x5271E97E, {2, 18, 3, 12, 22, 24}, 0, 0, 0, 0, 0, RNG::PIDType::Method2},
                {0, 0x31B05271, {2, 18, 3, 5, 30, 11}, 0, 0, 0, 0, 0, RNG::PIDType::Method4},
                {201, 0x815549A2, {2, 26, 30, 30, 11, 26}, 0, 0, 0, 0, 0, RNG::PIDType::Method1Unown},
                {201, 0x8A7B5190, {14, 2, 21, 30, 29, 15}, 0, 0, 0, 0, 0, RNG::PIDType::Method2Unown},
                {201, 0x5FA80D70, {2, 6, 3, 26, 4, 19}, 0, 0, 0, 0, 0, RNG::PIDType::Method4Unown},
                {1, 0x3DD1BB49, {23, 12, 31, 9, 3, 3}, 0, 0, 0, 0, 0, RNG::PIDType::Method3},
                {201, 0xBB493DD1, {23, 12, 31, 9, 3, 3}, 0, 0, 0, 0, 0, RNG::PIDType::Method3Unown},
                {0, 0x0985A297, {6, 1, 0, 7, 17, 7}, 0, 0, 0, 0, 0, RNG::PIDType::ColosseumXD},
                {0, 0x264750D9, {6, 31, 14, 27, 5, 27}, 0, 45819, 0, 1, RUBY, RNG::PIDType::Channel},
                {0, 0x0000E97E, {17, 19, 20, 16, 13, 12}, 0, 0, 0, 0, 0, RNG::PIDType::BACD},
                {0, 0x0000E980, {17, 19, 20, 16, 13, 12}, 1337, 60486, 0, 0, 0, RNG::PIDType::BACDAntishiny},
                {0, 0x67DBFC33, {12, 25, 27, 30, 2, 31}, 0, 0, 0, 0, 0, RNG::PIDType::BACD},
                {0, 0x67DBFC38, {12, 25, 27, 30, 2, 31}, 1337, 40657, 0, 0, 0, RNG::PIDType::BACDAntishiny},
                {0, 0x38CA4EA0, {0, 20, 28, 11, 19, 0}, 30317, 0, 0, 0, 0, RNG::PIDType::BACDShiny},
                {0, 0xBD3DF676, {0, 15, 5, 4, 21, 5}, 80, 0, 0, 0, 0, RNG::PIDType::BACDAntishinyXor},
                {0, 0xFEE73213, {3, 29, 23, 30, 28, 24}, 0, 0, 0, 0, 0, RNG::PIDType::Method4},
            };
            checkFixtures(context, Enums::GameVersion::RSE, FIXTURES);

            // The two restricted events also pin the origin seed to 16 bits.
            for (size_t fixtureIndex : {(size_t)10, (size_t)11})
            {
                std::unique_ptr<Pokemon::Pokemon> pokemon = makeFixturePokemon(Enums::GameVersion::RSE, FIXTURES[fixtureIndex]);
                if (pokemon)
                {
                    const RNG::PIDIV result = RNG::MethodFinder::analyze(*pokemon);
                    context.check(result.originSeed <= UINT16_MAX, "a restricted Gen 3 event's origin seed is wider than 16 bits", hexText(result.originSeed));
                }
            }

            // Method 1: the origin seed must regenerate the same PID and IVs.
            std::unique_ptr<Pokemon::Pokemon> methodOne = makeFixturePokemon(Enums::GameVersion::RSE, FIXTURES[0]);
            if (methodOne)
            {
                uint32_t seed = RNG::MethodFinder::analyze(*methodOne).originSeed;
                const uint32_t regeneratedPID = RNG::ClassicEraRNG::getSequentialPID(seed);
                const uint32_t regeneratedIVs = RNG::ClassicEraRNG::getSequentialIVs(seed);
                context.check(regeneratedPID == methodOne->pid() && regeneratedIVs == packIVs(*methodOne), "a Method 1 origin seed does not regenerate its PID and IVs", hexText(regeneratedPID) + " " + hexText(regeneratedIVs));
            }

            // Colosseum/XD: IVs, an ability call, then the PID halves.
            std::unique_ptr<Pokemon::Pokemon> colosseum = makeFixturePokemon(Enums::GameVersion::RSE, FIXTURES[8]);
            if (colosseum)
            {
                uint32_t seed = RNG::MethodFinder::analyze(*colosseum).originSeed;
                const uint32_t firstIVs = RNG::XDRNG::next15(seed);
                const uint32_t secondIVs = RNG::XDRNG::next15(seed);
                (void)RNG::XDRNG::next16(seed);
                const uint32_t highPID = RNG::XDRNG::next16(seed);
                const uint32_t lowPID = RNG::XDRNG::next16(seed);
                context.check(((highPID << 16) | lowPID) == colosseum->pid() && ((secondIVs << 15) | firstIVs) == packIVs(*colosseum), "a Colosseum/XD origin seed does not regenerate its PID and IVs");
            }

            // Berry Fix Zigzagoon: seed 0x20, forced shiny for TID 30317 / SID 0.
            constexpr uint32_t BERRY_FIX_SEED = 0x20;
            std::unique_ptr<Pokemon::Pokemon> berryFix = makeFixturePokemon(Enums::GameVersion::RSE, FIXTURES[14]);
            if (berryFix)
            {
                context.check(RNG::MethodFinder::analyze(*berryFix).originSeed == BERRY_FIX_SEED, "the Berry Fix Zigzagoon's origin seed is not 0x20");
                uint32_t seed = BERRY_FIX_SEED;
                const uint32_t regeneratedPID = RNG::CommonEvent3::getForceShiny(seed, 30317);
                const uint32_t regeneratedIVs = RNG::ClassicEraRNG::getSequentialIVs(seed);
                context.check(regeneratedPID == berryFix->pid() && regeneratedIVs == packIVs(*berryFix), "the Berry Fix seed does not regenerate the Zigzagoon", hexText(regeneratedPID) + " " + hexText(regeneratedIVs));
            }

            // PIDIVMethod4IVs: one IV seed for the Method 4 spread is two calls after 0x48FBAA42.
            std::unique_ptr<Pokemon::Pokemon> methodFour = makeFixturePokemon(Enums::GameVersion::RSE, FIXTURES[16]);
            if (methodFour)
            {
                const uint32_t packedIVs = packIVs(*methodFour);
                uint32_t seeds[RNG::LCRNG::MAX_IV_SEED_COUNT];
                const int seedCount = RNG::LCRNGReversalSkip::getSeedsIVs(seeds, (packedIVs & 0x7FFF) << 16, ((packedIVs >> 15) & 0x7FFF) << 16);
                const uint32_t expected = RNG::LCRNG::next2(0x48FBAA42u);
                bool found = false;
                for (int i = 0; i < seedCount; i++)
                {
                    found = found || seeds[i] == expected;
                }
                context.check(found, "LCRNGReversalSkip missed the Method 4 IV seed", hexText(expected));
            }
        }

        // PKHeX PIDIVTest.PIDIVMatchingTest4, PIDIVMatchingTest5 and PIDIVPokewalkerTest.
        void checkMethodFinderGen4And5(Context &context)
        {
            static constexpr uint8_t MALE = 0;
            static constexpr uint8_t FEMALE = 1;
            static constexpr PIDIVFixture GEN4_FIXTURES[] = {
                {1, 0x00000037, {16, 13, 12, 2, 18, 3}, 0, 0, MALE, 0, 0, RNG::PIDType::CuteCharm},
                {0, 0x07578CB7, {16, 13, 12, 2, 18, 3}, 0, 0, 0, 0, 0, RNG::PIDType::Gen4MysteryGiftAntishiny},
                {0, 0xA9C1A9C6, {22, 14, 23, 24, 11, 4}, 0, 0, 0, 0, 0, RNG::PIDType::ChainShiny},
                {25, 0x34000089, {0, 0, 0, 0, 0, 0}, 20790, 39664, MALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x7DFFFF60, {0, 0, 0, 0, 0, 0}, 30859, 63760, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x7DFFFF65, {0, 0, 0, 0, 0, 0}, 30859, 63760, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x7E000003, {0, 0, 0, 0, 0, 0}, 30859, 63760, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x2100008F, {0, 0, 0, 0, 0, 0}, 31526, 42406, MALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x71FFFF5A, {0, 0, 0, 0, 0, 0}, 49017, 12807, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0xC0000001, {0, 0, 0, 0, 0, 0}, 17398, 31936, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x2FFFFF5E, {0, 0, 0, 0, 0, 0}, 27008, 42726, FEMALE, 0, 0, RNG::PIDType::Pokewalker},
                {25, 0x59FFFFFE, {0, 0, 0, 0, 0, 0}, 51223, 28044, MALE, 0, 0, RNG::PIDType::Pokewalker},
            };
            checkFixtures(context, Enums::GameVersion::DP, GEN4_FIXTURES);

            static constexpr PIDIVFixture GEN5_FIXTURES[] = {
                {0, 0xBEEF0037, {0, 0, 0, 0, 0, 0}, 1337, 48097, 0, 0, 0, RNG::PIDType::Gen5MysteryGiftShiny},
            };
            checkFixtures(context, Enums::GameVersion::BW, GEN5_FIXTURES);
        }

        bool containsSeed(std::span<const uint32_t> seeds, uint32_t wanted)
        {
            for (uint32_t seed : seeds)
            {
                if (seed == wanted)
                {
                    return true;
                }
            }
            return false;
        }

        void unpackIVs(uint32_t packedIVs, uint32_t &hpIV, uint32_t &attackIV, uint32_t &defenseIV, uint32_t &speedIV, uint32_t &specialAttackIV, uint32_t &specialDefenseIV)
        {
            hpIV = packedIVs & 31;
            attackIV = (packedIVs >> 5) & 31;
            defenseIV = (packedIVs >> 10) & 31;
            speedIV = (packedIVs >> 15) & 31;
            specialAttackIV = (packedIVs >> 20) & 31;
            specialDefenseIV = (packedIVs >> 25) & 31;
        }

        // PKHeX PIDIVTest: Method1, PIDIVSearchEuclid, LCRNGReversalLatticeRecovery,
        // MRNGReversalRanchRecovery, ChannelLatticeRecovery, PokewalkerIVTest, CXDTrainerTest and
        // PIDIVPokeSpotTest.
        void checkSeedReversals(Context &context)
        {
            struct MethodOneVector
            {
                uint32_t seed;
                uint16_t firstRandom;
                uint16_t secondRandom;
            };
            static constexpr MethodOneVector METHOD_ONE[] = {
                {0x00001234, 0x4DCB, 0xE161},
                {0x00005678, 0x734D, 0xC596},
            };
            for (const MethodOneVector &vector : METHOD_ONE)
            {
                uint32_t seeds[RNG::LCRNG::MAX_PID_SEED_COUNT];
                const int seedCount = RNG::LCRNGReversal::getSeeds(seeds, (uint32_t)vector.firstRandom << 16, (uint32_t)vector.secondRandom << 16);
                context.check(seedCount != 0 && containsSeed(std::span<const uint32_t>(seeds, (size_t)seedCount), vector.seed), "LCRNGReversal missed a Method 1 seed", hexText(vector.seed));
            }

            {
                constexpr uint32_t SEED = 0x2E15555E;
                constexpr uint32_t FIRST_RANDOM = 0x20AD96A9;
                constexpr uint32_t SECOND_RANDOM = 0x7E1DBEC8;
                context.check(RNG::XDRNG::MAX_IV_SEED_COUNT > RNG::XDRNG::MAX_PID_SEED_COUNT, "XDRNG IV seed buffer is not larger than the PID one");
                uint32_t seeds[RNG::XDRNG::MAX_IV_SEED_COUNT];
                int seedCount = RNG::XDRNG::getSeeds(seeds, FIRST_RANDOM & 0xFFFF0000, SECOND_RANDOM & 0xFFFF0000);
                context.check(containsSeed(std::span<const uint32_t>(seeds, (size_t)seedCount), SEED), "XDRNG PID reversal missed a seed", hexText(SEED));
                seedCount = RNG::XDRNG::getSeedsIVs(seeds, FIRST_RANDOM & 0x7FFF0000, SECOND_RANDOM & 0x7FFF0000);
                context.check(containsSeed(std::span<const uint32_t>(seeds, (size_t)seedCount), SEED), "XDRNG IV reversal missed a seed", hexText(SEED));
            }

            {
                constexpr uint32_t SEED = 0x12345678;
                const uint32_t first = RNG::LCRNG::next(SEED);
                const uint32_t second = RNG::LCRNG::next(first);
                const uint32_t third = RNG::LCRNG::next(second);
                uint32_t seeds[RNG::LCRNG::MAX_IV_SEED_COUNT];
                int seedCount = RNG::LCRNGReversal::getSeedsIVs(seeds, first & 0x7FFF0000, second & 0x7FFF0000);
                context.check(containsSeed(std::span<const uint32_t>(seeds, (size_t)seedCount), SEED), "LCRNGReversal IV lattice missed a seed", hexText(SEED));
                seedCount = RNG::LCRNGReversalSkip::getSeedsIVs(seeds, first & 0x7FFF0000, third & 0x7FFF0000);
                context.check(containsSeed(std::span<const uint32_t>(seeds, (size_t)seedCount), SEED), "LCRNGReversalSkip IV lattice missed a seed", hexText(SEED));
            }

            struct RecoveryVector
            {
                uint32_t packedIVs;
                int recovered;
                bool isObtainable;
            };
            static constexpr RecoveryVector RANCH[] = {
                {0x00000007, 0, true}, {0x00000065, 0, true}, {0x04206967, 2, true}, {0x0BADB015, 2, true},
                {0x3FF07C1F, 4, true}, {0x3FFFFFFF, 4, true}, {0x3FFFFC1F, 6, true}, {0x00042069, 6, true},
            };
            for (const RecoveryVector &vector : RANCH)
            {
                uint32_t hpIV, attackIV, defenseIV, speedIV, specialAttackIV, specialDefenseIV;
                unpackIVs(vector.packedIVs, hpIV, attackIV, defenseIV, speedIV, specialAttackIV, specialDefenseIV);
                uint32_t seeds[RNG::LCRNG::MAX_IV_SEED_COUNT];
                const int seedCount = RNG::MRNGReversal::getSeedsIVs(seeds, hpIV, attackIV, defenseIV, specialAttackIV, specialDefenseIV, speedIV);
                context.check(seedCount == vector.recovered, "MRNGReversal recovered a different seed count", hexText(vector.packedIVs) + ": " + std::to_string(seedCount));
                for (int i = 0; i < seedCount; i++)
                {
                    context.check(RNG::MRNG::getSequentialIVs(seeds[i]) == vector.packedIVs, "an MRNGReversal seed does not regenerate its IVs", hexText(seeds[i]));
                }
            }

            static constexpr RecoveryVector CHANNEL[] = {
                {0x00000000, 1, false}, {0x00000001, 7, true}, {0x00000087, 9, true},
                {0x00000888, 10, true}, {0x0000F525, 11, true}, {0x00019994, 12, true},
            };
            for (const RecoveryVector &vector : CHANNEL)
            {
                uint32_t hpIV, attackIV, defenseIV, speedIV, specialAttackIV, specialDefenseIV;
                unpackIVs(vector.packedIVs, hpIV, attackIV, defenseIV, speedIV, specialAttackIV, specialDefenseIV);
                uint32_t seeds[RNG::XDRNG::MAX_CHANNEL_SEED_COUNT];
                const int seedCount = RNG::XDRNG::getSeedsChannel(seeds, hpIV, attackIV, defenseIV, specialAttackIV, specialDefenseIV, speedIV);
                context.check(seedCount == vector.recovered, "XDRNG Channel lattice recovered a different seed count", hexText(vector.packedIVs) + ": " + std::to_string(seedCount));
                bool possible = false;
                for (int i = 0; i < seedCount; i++)
                {
                    const uint32_t ivSeed = RNG::ChannelJirachi::skipToIVs(seeds[i]);
                    context.check(RNG::XDRNG::getSequentialIV32(ivSeed) == vector.packedIVs, "a Channel lattice seed does not regenerate its IVs", hexText(seeds[i]));
                    possible = possible || RNG::ChannelJirachi::getPossible(seeds[i]).pattern != RNG::ChannelJirachiRandomResult::None;
                }
                context.check(possible == vector.isObtainable, "Channel Jirachi menu reachability differs from PKHeX", hexText(vector.packedIVs));
            }

            struct PokewalkerVector
            {
                uint32_t hpIV, attackIV, defenseIV, specialAttackIV, specialDefenseIV, speedIV;
                uint32_t seed;
                uint32_t priorPokemonCount;
                RNG::PokewalkerSeedType type;
            };
            static constexpr PokewalkerVector POKEWALKER[] = {
                {30, 31, 31, 14, 31, 31, 0x28070031, 24, RNG::PokewalkerSeedType::NoStroll},
                {0, 0, 0, 5, 3, 9, 0x00011434, 539, RNG::PokewalkerSeedType::Stroll},
            };
            for (const PokewalkerVector &vector : POKEWALKER)
            {
                const RNG::PokewalkerSeedResult result = RNG::PokewalkerRNG::getLeastEffortSeed(vector.hpIV, vector.attackIV, vector.defenseIV, vector.specialAttackIV, vector.specialDefenseIV, vector.speedIV);
                context.check(result.type == vector.type && result.priorPokemonCount == vector.priorPokemonCount && result.seed == vector.seed, "Pokewalker least-effort seed differs from PKHeX", hexText(result.seed) + " count " + std::to_string(result.priorPokemonCount));
            }

            struct TrainerVector
            {
                uint16_t TID16;
                uint16_t SID16;
                bool expected;
            };
            static constexpr TrainerVector TRAINERS[] = {
                {62714, 62938, true}, {5724, 31840, true}, {31827, 52374, true}, {1337, 1337, true},
                {34952, 34952, true}, {1337, 1, false}, {42069, 13370, false},
            };
            for (const TrainerVector &vector : TRAINERS)
            {
                uint32_t origin = 0;
                const bool found = RNG::MethodCXD::tryGetSeedTrainerID(vector.TID16, vector.SID16, origin);
                context.check(found == vector.expected, "Colosseum/XD trainer id seed search differs from PKHeX", std::to_string(vector.TID16) + "/" + std::to_string(vector.SID16));
            }

            struct PokeSpotVector
            {
                uint32_t PID;
                uint8_t slot;
            };
            static constexpr PokeSpotVector POKESPOT[] = {
                {0x7B2D9DA7, 0}, // Zubat (Cave), common
                {0x3EE9AF66, 1}, // Gligar (Rock), uncommon
                {0x9B667F3C, 2}, // Surskit (Oasis), rare
            };
            for (const PokeSpotVector &vector : POKESPOT)
            {
                uint32_t origin = 0;
                context.check(RNG::MethodPokeSpot::tryGetOriginSeedPID(vector.PID, vector.slot, origin), "PokeSpot found no origin seed for a PKHeX test PID", hexText(vector.PID));
            }
        }

        /// PKHeX MockSlot4: a level 5 grass slot, pressure level equal to the maximum.
        RNG::EncounterSlot4Parameters mockSlot4(uint8_t slotNumber)
        {
            RNG::EncounterSlot4Parameters slot;
            slot.slotNumber = slotNumber;
            slot.levelMin = 5;
            slot.levelMax = 5;
            slot.pressureLevel = slot.levelMax;
            slot.type = RNG::SlotType4::Grass;
            return slot;
        }

        // PKHeX MethodJChecks.CheckRangeJ, CheckPossibleJ and CheckImpossibleJ.
        void checkMethodJ(Context &context)
        {
            struct RangeVector
            {
                uint32_t prePID;
                uint32_t originSeed;
                int frames;
            };
            static constexpr RangeVector RANGES[] = {
                {0x00000000, 0x64e86d5e, 5},
                {0x00000001, 0x1250e055, 14},
                {0x00000002, 0x746d2fae, 10},
            };
            for (const RangeVector &vector : RANGES)
            {
                const uint32_t PID = RNG::ClassicEraRNG::getSequentialPIDFrom(vector.prePID);
                const int windowFrames = RNG::MethodJ::getReversalWindow(vector.prePID, (uint8_t)(PID % 25));
                const uint32_t origin = RNG::LCRNG::reverse(vector.prePID, windowFrames * 2);
                context.check(windowFrames == vector.frames && origin == vector.originSeed, "Method J reversal window differs from PKHeX", hexText(vector.prePID) + ": " + std::to_string(windowFrames) + " frames to " + hexText(origin));
            }

            // Seeds that generate well-known flawless PIDs.
            constexpr uint32_t TIMID_FLAWLESS = 0xC69FB838;
            constexpr uint32_t MODEST_FLAWLESS = TIMID_FLAWLESS ^ 0x80000000u;
            struct PossibleVector
            {
                uint32_t prePID;
                uint8_t slotNumber;
                RNG::LeadRequired expectedLead;
            };
            static constexpr PossibleVector POSSIBLE[] = {
                {TIMID_FLAWLESS, 0, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 1, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 2, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 3, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 4, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 5, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 7, RNG::LeadRequired::Synchronize},
                {TIMID_FLAWLESS, 8, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 0, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 1, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 2, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 4, RNG::LeadRequired::SynchronizeFail},
                {MODEST_FLAWLESS, 5, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 8, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 9, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 10, RNG::LeadRequired::Synchronize},
                {MODEST_FLAWLESS, 6, RNG::LeadRequired::None},
            };
            for (const PossibleVector &vector : POSSIBLE)
            {
                const RNG::LeadSeed result = RNG::MethodJ::getSeed(mockSlot4(vector.slotNumber), vector.prePID);
                context.check(result.lead == vector.expectedLead && result.seed != 0, "Method J lead for a possible slot differs from PKHeX", hexText(vector.prePID) + " slot " + std::to_string(vector.slotNumber) + ": lead " + std::to_string((int)result.lead));
            }

            struct ImpossibleVector
            {
                uint32_t prePID;
                uint8_t slotNumber;
            };
            static constexpr ImpossibleVector IMPOSSIBLE[] = {
                {TIMID_FLAWLESS, 6}, {TIMID_FLAWLESS, 9}, {TIMID_FLAWLESS, 10}, {TIMID_FLAWLESS, 11},
                {MODEST_FLAWLESS, 3}, {MODEST_FLAWLESS, 7}, {MODEST_FLAWLESS, 11},
            };
            for (const ImpossibleVector &vector : IMPOSSIBLE)
            {
                const RNG::LeadSeed result = RNG::MethodJ::getSeed(mockSlot4(vector.slotNumber), vector.prePID);
                context.check(!result.isValid(), "Method J found a lead for an impossible slot", hexText(vector.prePID) + " slot " + std::to_string(vector.slotNumber));
            }
        }
    }

    int rngPlan(Context &)
    {
        return CHUNK_COUNT;
    }

    void rngRun(Context &context, int chunk)
    {
        switch (chunk)
        {
        case SequenceChunk:
            checkSequences(context);
            break;
        case XoroshiroReversalChunk:
            checkXoroshiroReversal(context);
            break;
        case MethodFinderGen3Chunk:
            checkMethodFinderGen3(context);
            break;
        case MethodFinderGen4And5Chunk:
            checkMethodFinderGen4And5(context);
            break;
        case SeedReversalChunk:
            checkSeedReversals(context);
            break;
        case MethodJChunk:
            checkMethodJ(context);
            break;
        default:
            break;
        }
    }
}
