// Port of PKHeX PKHeX.Core/Legality/Verifiers/AwakenedValueVerifier.cs: Let's Go awakening values.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        /// Stats in PKHeX's display order: HP, Attack, Defense, Special Attack, Special Defense, Speed.
        constexpr int DISPLAY_STAT_COUNT = 6;
        constexpr uint8_t AWAKENING_VALUE_MAXIMUM = 200;
        /// PKHeX GP1.InitialAV: what a Pokemon from GO Park starts with in every stat.
        constexpr uint8_t GO_PARK_INITIAL_AWAKENING_VALUE = 2;
        constexpr uint8_t POKEMON_GO_VERSION = 34;

        const char *getDisplayStatName(int displayStatIndex)
        {
            static const char *const displayStatNames[DISPLAY_STAT_COUNT] = {"HP", "Attack", "Defense", "Special Attack", "Special Defense", "Speed"};
            return displayStatNames[displayStatIndex];
        }

        /// PKHeX EntityCharacteristic.GetCharacteristicInit0: the highest IV, searched from EC % 6, as
        /// stat index * 5 + IV % 5. Stats in storage order: HP, Attack, Defense, Speed, Special Attack,
        /// Special Defense. A blank IV word (no IVs, not nicknamed, not an egg) is characteristic 0.
        int getCharacteristic(const Pokemon::Pokemon &pokemon)
        {
            constexpr int storageStatCount = 6;
            const uint8_t individualValues[storageStatCount] = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD()};
            bool isIndividualValueWordBlank = !pokemon.isNicknamed() && !pokemon.isEgg();
            for (const uint8_t individualValue : individualValues)
            {
                isIndividualValueWordBlank = isIndividualValueWordBlank && individualValue == 0;
            }
            if (isIndividualValueWordBlank)
            {
                return 0;
            }
            int statIndex = static_cast<int>(pokemon.encryptionConstant() % storageStatCount);
            int maximumStatIndex = statIndex;
            int maximumStatValue = 0;
            do
            {
                const int individualValue = individualValues[statIndex];
                if (individualValue > maximumStatValue)
                {
                    maximumStatIndex = statIndex;
                    maximumStatValue = individualValue;
                }
                statIndex = statIndex != storageStatCount - 1 ? statIndex + 1 : 0;
            } while (maximumStatIndex != statIndex);
            return (maximumStatIndex * 5) + (maximumStatValue % 5);
        }

        /// PKHeX PB7.GetRandomIndex: the display-order stat a level-up awakens. `bits` 1-5 name Attack,
        /// Defense, Special Attack, Special Defense or Speed, a stat the nature lowers is swapped for the
        /// one it raises, and 6-7 defer to the characteristic.
        int getLevelUpStatIndex(int bits, int characteristic, uint8_t natureId)
        {
            if (bits == 6 || bits == 7)
            {
                // The characteristic's storage-order stat, mapped to display order.
                constexpr int displayIndexByStorageIndex[DISPLAY_STAT_COUNT] = {0, 1, 2, 5, 3, 4};
                return displayIndexByStorageIndex[characteristic / 5];
            }
            if (bits == 0)
            {
                return 0;
            }
            // Pokemon::natureStatPercent takes ATK, DEF, SPE, SPA, SPD; bits 1-5 are ATK, DEF, SPA, SPD, SPE.
            constexpr int natureStatIndexByBits[DISPLAY_STAT_COUNT] = {0, 0, 1, 3, 4, 2};
            if (Pokemon::Pokemon::natureStatPercent(natureId, natureStatIndexByBits[bits]) >= 100)
            {
                return bits;
            }
            for (int candidateBits = 1; candidateBits < DISPLAY_STAT_COUNT; ++candidateBits)
            {
                if (Pokemon::Pokemon::natureStatPercent(natureId, natureStatIndexByBits[candidateBits]) > 100)
                {
                    return candidateBits;
                }
            }
            return 0;
        }

        /// PKHeX AwakeningUtil.SetExpectedMinimumAVs: every level gained in Let's Go awakens one stat,
        /// chosen from the encryption constant.
        void getExpectedMinimumAwakeningValues(const Pokemon::Pokemon &pokemon, uint8_t *requiredValues)
        {
            const uint8_t initialValue = (pokemon.hasOriginGame() && pokemon.originGame() == POKEMON_GO_VERSION) ? GO_PARK_INITIAL_AWAKENING_VALUE : 0;
            for (int displayStatIndex = 0; displayStatIndex < DISPLAY_STAT_COUNT; ++displayStatIndex)
            {
                requiredValues[displayStatIndex] = initialValue;
            }
            const int startLevel = pokemon.metLevel();
            const int endLevel = Evolutions::getEntityCurrentLevel(pokemon);
            if (startLevel == endLevel)
            {
                return;
            }
            const int characteristic = getCharacteristic(pokemon);
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            for (int level = startLevel + 1; level <= endLevel; ++level)
            {
                const int levelDigit = level % 10;
                const int bits = static_cast<int>((encryptionConstant >> (3 * levelDigit)) & 7);
                ++requiredValues[getLevelUpStatIndex(bits, characteristic, pokemon.nature())];
            }
        }
    }

    void verifyAwakeningValues(const AnalysisContext &context)
    {
        if (context.formatGroup != Enums::GameVersion::GG)
        {
            return;
        }
        const Pokemon::Pokemon &pokemon = context.pokemon;
        // Let's Go has no effort values: nothing in the game can give them.
        const int effortValueTotal = pokemon.evHP() + pokemon.evATK() + pokemon.evDEF() + pokemon.evSPE() + pokemon.evSPA() + pokemon.evSPD();
        if (effortValueTotal != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::AwakeningValues, ResultCode::EffortShouldBeZero);
        }

        const uint8_t currentValues[DISPLAY_STAT_COUNT] = {pokemon.avHP(), pokemon.avATK(), pokemon.avDEF(), pokemon.avSPA(), pokemon.avSPD(), pokemon.avSPE()};
        for (const uint8_t awakeningValue : currentValues)
        {
            if (awakeningValue > AWAKENING_VALUE_MAXIMUM)
            {
                context.add(Severity::Invalid, CheckIdentifier::AwakeningValues, ResultCode::AwakenedCap);
                break;
            }
        }

        uint8_t requiredValues[DISPLAY_STAT_COUNT];
        getExpectedMinimumAwakeningValues(pokemon, requiredValues);
        // PKHeX names the stat by an index into its display-order names that is off for the last three
        // (it passes 4, 5, 3 for Special Attack, Special Defense, Speed); the stat being judged is named here.
        for (int displayStatIndex = 0; displayStatIndex < DISPLAY_STAT_COUNT; ++displayStatIndex)
        {
            if (currentValues[displayStatIndex] < requiredValues[displayStatIndex])
            {
                context.add(Severity::Invalid, CheckIdentifier::AwakeningValues, ResultCode::AwakenedStatAtLeast, std::to_string(requiredValues[displayStatIndex]), getDisplayStatName(displayStatIndex));
            }
        }
    }
}
