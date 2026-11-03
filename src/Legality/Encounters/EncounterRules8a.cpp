#include "Legality/Encounters/EncounterRules8a.h"

#include <algorithm>
#include <span>
#include <string_view>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Names/RibbonNames.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon8LA.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_BASCULEGION = 902;
            constexpr uint16_t SPECIES_OVERQWIL = 904;
            constexpr uint16_t SPECIES_WYRDEER = 899;

            // PKHeX IRibbonSetMark9 is on PA8, PK9 and PA9 only; PK8 and PB8 keep the byte but not the mark.
            bool hasGen9Marks(const Pokemon::Pokemon &pokemon) noexcept
            {
                const Enums::GameVersion group = pokemon.getGameGroup();
                return group == Enums::GameVersion::PLA || group == Enums::GameVersion::SV || group == Enums::GameVersion::ZA;
            }

            bool hasRibbonMarkAlpha(const Pokemon::Pokemon &pokemon)
            {
                const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(pokemon.getGameGroup());
                const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
                for (const Names::RibbonDefinition &ribbon : ribbons)
                {
                    if (ribbon.spriteStem != nullptr && std::string_view(ribbon.spriteStem) == "ribbonmarkalpha")
                    {
                        return Names::getRibbonValue(entityData, ribbon) != 0;
                    }
                }
                return false;
            }
        }

        const char *getSlotTypeName8a(SlotType8a type) noexcept
        {
            switch (type)
            {
            case SlotType8a::Standard:
                return "Standard";
            case SlotType8a::Distortion:
                return "Distortion";
            case SlotType8a::Landmark:
                return "Landmark";
            case SlotType8a::MassOutbreakRegular:
                return "MassOutbreakRegular";
            case SlotType8a::MassOutbreakMassive:
                return "MassOutbreakMassive";
            }
            return "";
        }

        uint8_t getGenderRatio8a(uint8_t fixedGender, uint16_t speciesId, uint8_t formId) noexcept
        {
            switch (fixedGender)
            {
            case 0:
                return GENDER_RATIO_MAGIC_MALE;
            case 1:
                return GENDER_RATIO_MAGIC_FEMALE;
            default:
                return ::Pokemon::getGenderRatio(speciesId, formId, Enums::GameVersion::PLA);
            }
        }

        // PKHeX MarkRules.IsMarkValidAlpha
        bool isMarkValidAlpha(const Pokemon::Pokemon &pokemon, bool wasAlpha)
        {
            if (!hasGen9Marks(pokemon))
            {
                return true;
            }
            const bool hasMark = hasRibbonMarkAlpha(pokemon);
            if (!wasAlpha)
            {
                return !hasMark;
            }
            if (!RNG::HomeQuirks::hasEnteredSetAlphaMark(pokemon))
            {
                // Only HOME sets the mark, so either state is possible.
                return true;
            }
            return hasMark;
        }

        bool isMissingAlphaMark(const Pokemon::Pokemon &pokemon)
        {
            return hasGen9Marks(pokemon) && !hasRibbonMarkAlpha(pokemon);
        }

        bool isAlphaMismatch(const Pokemon::Pokemon &pokemon, bool isAlpha) noexcept
        {
            return pokemon.hasAlphaFlag() && pokemon.isAlpha() != isAlpha;
        }

        // PKHeX EncounterSlot8a.IsFormArgMismatch
        bool isFormArgumentMismatch8a(const Pokemon::Pokemon &pokemon, uint16_t templateSpeciesId) noexcept
        {
            if (!pokemon.hasFormArgument() || pokemon.formArgument() != 0)
            {
                return false;
            }
            const uint16_t speciesId = pokemon.speciesID();
            if (speciesId == templateSpeciesId)
            {
                return false;
            }
            if (speciesId == SPECIES_WYRDEER || speciesId == SPECIES_BASCULEGION)
            {
                return true;
            }
            // A tracked Overqwil may have evolved in Scarlet/Violet, which keeps no count; only an untracked one defers.
            return speciesId == SPECIES_OVERQWIL && pokemon.hasHomeTracker() && pokemon.homeTracker() == 0;
        }

        // PKHeX EncounterEnumerator8a.InitializeWildLocationInfo
        bool hasOriginalMetLocation8a(const Pokemon::Pokemon &pokemon)
        {
            const LocationRemapState state = getRemapState(Enums::GameVersion::PLA, getEntityContextGroup(pokemon));
            const bool canBeRemapped = state == LocationRemapState::Remapped || state == LocationRemapState::Either;
            if (!canBeRemapped)
            {
                return true;
            }
            return pokemon.metLocation() != LOCATION_HOME_SWLA;
        }

        int getMoveShopIndex8a(uint16_t moveId) noexcept
        {
            const uint16_t *const shopEnd = Pokemon::MOVE_SHOP_MOVES_8LA + Pokemon::MOVE_SHOP_COUNT_8LA;
            const uint16_t *const found = std::find(Pokemon::MOVE_SHOP_MOVES_8LA, shopEnd, moveId);
            return found == shopEnd ? -1 : static_cast<int>(found - Pokemon::MOVE_SHOP_MOVES_8LA);
        }

        // PKHeX PKM/Interfaces/IMoveShop8.cs MoveShop8MasteryExtensions.IsValidPurchasedEncounter
        bool isValidPurchasedEncounter8a(const Pokemon::Pokemon8LA &pokemon, const LearnSource::Learnset &learnset, uint8_t level, uint16_t alphaMove, bool allowPurchasedAlpha) noexcept
        {
            // PKHeX `shop.Permit`: the record's current species and form.
            const uint64_t permissions = Pokemon::getMoveShopPermissions8LA(pokemon.speciesID(), pokemon.form());
            for (int shopIndex = 0; shopIndex < Pokemon::MOVE_SHOP_COUNT_8LA; ++shopIndex)
            {
                if (((permissions >> shopIndex) & 1) == 0 || !pokemon.isMoveShopPurchased(shopIndex))
                {
                    continue;
                }
                const uint16_t moveId = Pokemon::MOVE_SHOP_MOVES_8LA[shopIndex];
                // A move the available learnset already offers cannot be bought.
                uint8_t learnLevel = 0;
                if (learnset.tryGetLevelLearnMove(moveId, learnLevel) && learnLevel <= level)
                {
                    return false;
                }
                // Only a capture from before the 1.1 patch could buy its Alpha move.
                if (moveId == alphaMove && !allowPurchasedAlpha)
                {
                    return false;
                }
            }
            return true;
        }

        // PKHeX PKM/Interfaces/IMoveShop8.cs MoveShop8MasteryExtensions.IsValidMasteredEncounter
        bool isValidMasteredEncounter8a(const Pokemon::Pokemon8LA &pokemon, std::span<const uint16_t, 4> encounterMoves, const LearnSource::Learnset &learnset, std::span<const Pokemon::LevelUpMove> masteryLevels, uint8_t metLevel, uint16_t alphaMove, bool allowPurchasedAlpha) noexcept
        {
            for (const uint16_t moveId : encounterMoves)
            {
                if (moveId == 0)
                {
                    continue;
                }
                const int shopIndex = getMoveShopIndex8a(moveId);
                // Not a move-shop move: mastered by hand for the encounter.
                if (shopIndex < 0)
                {
                    continue;
                }
                const bool isPurchased = pokemon.isMoveShopPurchased(shopIndex);
                const bool isMastered = pokemon.isMoveShopMastered(shopIndex);
                // PKHeX Learnset.TryGetLevelLearnMove: the move's first entry.
                const auto masteryEntry = std::find_if(masteryLevels.begin(), masteryLevels.end(), [moveId](const Pokemon::LevelUpMove &entry) { return entry.moveId == moveId; });
                const bool isMasteredLater = masteryEntry != masteryLevels.end() && masteryEntry->level > metLevel;
                if (isMasteredLater && moveId != alphaMove)
                {
                    if (!isMastered || isPurchased)
                    {
                        continue;
                    }
                    // Mastered early only by a Seed of Mastery, which needs the move learnable by then.
                    uint8_t learnLevel = 0;
                    if (learnset.tryGetLevelLearnMove(moveId, learnLevel) && learnLevel > metLevel)
                    {
                        return false;
                    }
                }
                else
                {
                    // Since the 1.1 patch the Alpha move counts as purchased without the flag; a capture
                    // from before it may still carry the flag.
                    if (!isMastered)
                    {
                        return false;
                    }
                    if (isPurchased && (moveId != alphaMove || !allowPurchasedAlpha))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }
}
