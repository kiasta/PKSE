#include "Legality/RNG/Tera9RNG.h"

#include "Enums/GameVersion.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/PokemonTypes.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t TERA_TYPE_COUNT = 18;
            constexpr uint16_t BURMY_SPECIES = 412;
            constexpr uint16_t FURFROU_SPECIES = 676;
            constexpr uint16_t ORICORIO_SPECIES = 741;

            const Pokemon::PersonalRecord &getScarletVioletRecord(uint16_t species, uint8_t form) noexcept
            {
                return Pokemon::getPersonalRecord(Enums::GameVersion::SV, species, form);
            }

            /// PKHeX stores a single-typed species' second type as its first.
            uint8_t getSecondType(const Pokemon::PersonalRecord &record) noexcept
            {
                return record.type2 == Pokemon::PERSONAL_TYPE_NONE ? record.type1 : record.type2;
            }

            bool isPresentInScarletViolet(uint16_t species, uint8_t form) noexcept
            {
                return (Pokemon::getPersonalInfo(species, form).presence & Pokemon::PERSONAL_GAME_SV) != 0;
            }

            bool isMatchType(const Pokemon::PersonalRecord &record, uint8_t original) noexcept
            {
                return original == record.type1 || original == getSecondType(record);
            }

            // PKHeX TeraTypeUtil.GetTeraTypeImport: HOME picks the first type unless it is Normal.
            bool isMatchTeraTypeImport(const Pokemon::PersonalRecord &record, uint8_t original) noexcept
            {
                const uint8_t importType = record.type1 != Pokemon::TYPE_NORMAL ? record.type1 : getSecondType(record);
                return original == importType;
            }

            // PKHeX FormInfo.IsFormChangeEggTypes
            bool isFormChangeEggTypes(uint16_t species) noexcept
            {
                return species == BURMY_SPECIES || species == FURFROU_SPECIES || species == ORICORIO_SPECIES;
            }

            template <typename Predicate>
            bool isMatchAnyPresentForm(uint16_t species, Predicate predicate) noexcept
            {
                const uint8_t formCount = getScarletVioletRecord(species, 0).formCount;
                if (isPresentInScarletViolet(species, 0) && predicate(getScarletVioletRecord(species, 0)))
                {
                    return true;
                }
                for (uint8_t form = 1; form < formCount; form++)
                {
                    if (isPresentInScarletViolet(species, form) && predicate(getScarletVioletRecord(species, form)))
                    {
                        return true;
                    }
                }
                return false;
            }

            // Story progress sets the star spread; PKHeX's <= check makes each range's top edge inclusive.
            bool isValidStarRand(uint32_t random, uint8_t stars) noexcept
            {
                switch (stars)
                {
                case 1:
                    return random <= 80;
                case 2:
                    return (random <= 70 && random > 20) || random > 80;
                case 3:
                    return true;
                case 4:
                    return random > 30;
                case 5:
                    return random > 70;
                default:
                    return true;
                }
            }
        }

        bool Tera9RNG::isMatchTeraType(uint32_t seed, GemType gem, uint16_t species, uint8_t form, uint8_t original) noexcept
        {
            uint8_t type = 0;
            if (isSpecified(gem, type))
            {
                return original == type;
            }

            Xoroshiro128Plus random(seed);
            if (gem == GemType::Default)
            {
                const uint64_t pivot = random.nextInt(2);
                return original == getTeraTypeFromPersonal(species, form, pivot);
            }
            if (gem == GemType::Random)
            {
                return original == random.nextInt(TERA_TYPE_COUNT);
            }
            return false;
        }

        bool Tera9RNG::isMatchTeraType(GemType gem, uint16_t species, uint8_t form, uint8_t original) noexcept
        {
            if (gem == GemType::Random)
            {
                return true;
            }
            uint8_t type = 0;
            if (isSpecified(gem, type))
            {
                return original == type;
            }
            if (gem == GemType::Default)
            {
                return isMatchTeraTypePersonal(species, form, original);
            }
            return false;
        }

        uint8_t Tera9RNG::getTeraType(uint64_t seed, GemType gem, uint16_t species, uint8_t form) noexcept
        {
            uint8_t type = 0;
            if (isSpecified(gem, type))
            {
                return type;
            }
            Xoroshiro128Plus random(seed);
            if (gem == GemType::Random)
            {
                return static_cast<uint8_t>(random.nextInt(TERA_TYPE_COUNT));
            }
            const uint64_t pivot = random.nextInt(2);
            return getTeraTypeFromPersonal(species, form, pivot);
        }

        bool Tera9RNG::isMatchTeraTypePersonalEgg(uint16_t species, uint8_t form, uint8_t original) noexcept
        {
            if (isFormChangeEggTypes(species))
            {
                return isMatchTeraTypePersonalAnyForm(species, original);
            }
            return isMatchTeraTypePersonal(species, form, original);
        }

        bool Tera9RNG::isMatchTeraTypePersonal(uint16_t species, uint8_t form, uint8_t original) noexcept
        {
            return isMatchType(getScarletVioletRecord(species, form), original);
        }

        bool Tera9RNG::isMatchTeraTypePersonalAnyForm(uint16_t species, uint8_t original) noexcept
        {
            return isMatchAnyPresentForm(species, [original](const Pokemon::PersonalRecord &record) { return isMatchType(record, original); });
        }

        bool Tera9RNG::isMatchTeraTypePersonalImport(uint16_t species, uint8_t form, uint8_t original) noexcept
        {
            return isMatchTeraTypeImport(getScarletVioletRecord(species, form), original);
        }

        bool Tera9RNG::isMatchTeraTypePersonalAnyFormImport(uint16_t species, uint8_t original) noexcept
        {
            return isMatchAnyPresentForm(species, [original](const Pokemon::PersonalRecord &record) { return isMatchTeraTypeImport(record, original); });
        }

        uint8_t Tera9RNG::getTeraTypeFromPersonal(uint16_t species, uint8_t form, uint64_t pivot) noexcept
        {
            const Pokemon::PersonalRecord &record = getScarletVioletRecord(species, form);
            return pivot == 0 ? record.type1 : getSecondType(record);
        }

        // PKHeX Tera9RNG.IsMatchStarChoice: the first roll is the star count, the second the opponent from
        // the total of every raid's rate, which differs between Scarlet and Violet.
        bool Tera9RNG::isMatchStarChoice(uint32_t seed, uint8_t stars, uint8_t raidRate, int16_t randomRateMinimumScarlet, int16_t randomRateMinimumViolet, uint16_t rateTotalScarlet, uint16_t rateTotalViolet) noexcept
        {
            Xoroshiro128Plus random(seed);
            if (stars < 6)
            {
                const uint32_t starRandom = static_cast<uint32_t>(random.nextInt(100));
                if (!isValidStarRand(starRandom, stars))
                {
                    return false;
                }
            }

            {
                Xoroshiro128Plus copy = random;
                const int rateRandom = static_cast<int>(copy.nextInt(rateTotalScarlet));
                if (randomRateMinimumScarlet >= 0 && static_cast<uint32_t>(rateRandom - randomRateMinimumScarlet) < raidRate)
                {
                    return true;
                }
            }
            {
                Xoroshiro128Plus copy = random;
                const int rateRandom = static_cast<int>(copy.nextInt(rateTotalViolet));
                if (randomRateMinimumViolet >= 0 && static_cast<uint32_t>(rateRandom - randomRateMinimumViolet) < raidRate)
                {
                    return true;
                }
            }
            return false;
        }

        // PKHeX Tera9RNG.GetOriginalSeed: as Sword/Shield, the EC is the first output, here with a fake trainer roll before the PID.
        uint32_t Tera9RNG::getOriginalSeed(const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            constexpr uint32_t TWO_SEEDS_ENCRYPTION_CONSTANT = 0xF8572EBE;
            if (encryptionConstant != TWO_SEEDS_ENCRYPTION_CONSTANT)
            {
                return encryptionConstant - static_cast<uint32_t>(Xoroshiro128Plus::XOROSHIRO_CONSTANT);
            }
            constexpr uint32_t FIRST_SEED = 0xD5B9C463;
            constexpr uint32_t SECOND_SEED = 0xDD6295A4;
            constexpr uint32_t FIRST_PID = 0xC9E69326;
            return pokemon.pid() == FIRST_PID ? FIRST_SEED : SECOND_SEED;
        }
    }
}
