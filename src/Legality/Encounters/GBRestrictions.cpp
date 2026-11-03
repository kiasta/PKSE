#include "Legality/Encounters/GBRestrictions.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterData1.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGift1.h"
#include "Legality/Encounters/EncounterStatic1.h"
#include "Legality/Encounters/EncounterTrade1.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Utils/Gen1Text.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX ItemConverter.CatchRateIsHeldItem, one bit per catch-rate value.
            constexpr uint8_t CATCH_RATE_HELD_ITEM_FLAGS[32] = {
                0x3F, 0xFF, 0xFF, 0xFD, 0xFF, 0xDF, 0x3B, 0xD2,
                0x03, 0xFF, 0xFF, 0xFB, 0xEF, 0xFF, 0xE7, 0x7E,
                0x18, 0x9C, 0xC5, 0xF1, 0xFB, 0x77, 0xF0, 0xBF,
                0xF7, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0x07, 0x00,
            };

            constexpr uint16_t MAX_SPECIES_ID_1 = 151;
            constexpr uint8_t VERSION_RED = 35;
            constexpr uint8_t VERSION_YELLOW = 38;
            constexpr uint8_t VERSION_GOLD = 39;
            constexpr uint8_t VERSION_CRYSTAL = 41;
            /// The Pokemon Stadium gift boxes, whose catch-rate byte some gifts keep.
            constexpr uint8_t CATCH_RATE_NORMAL_BOX = 167;
            constexpr uint8_t CATCH_RATE_GORGEOUS_BOX = 168;

            bool isStadiumGiftSpecies(uint16_t speciesId) noexcept
            {
                switch (speciesId)
                {
                case 1: // Bulbasaur
                case 4: // Charmander
                case 7: // Squirtle
                case 54: // Psyduck
                case 106: // Hitmonlee
                case 107: // Hitmonchan
                case 133: // Eevee
                case 138: // Omanyte
                case 140: // Kabuto
                    return true;
                default:
                    return false;
                }
            }

            bool isEvolvedFromGen1Species(uint16_t speciesId) noexcept
            {
                switch (speciesId)
                {
                case 169: // Crobat
                case 182: // Bellossom
                case 186: // Politoed
                case 196: // Espeon
                case 197: // Umbreon
                case 199: // Slowking
                case 208: // Steelix
                case 212: // Scizor
                case 230: // Kingdra
                case 233: // Porygon2
                case 242: // Blissey
                case 462: // Magnezone
                case 463: // Lickilicky
                case 464: // Rhyperior
                case 465: // Tangrowth
                case 466: // Electivire
                case 467: // Magmortar
                case 470: // Leafeon
                case 471: // Glaceon
                case 474: // Porygon-Z
                case 700: // Sylveon
                case 900: // Kleavor
                    return true;
                default:
                    return false;
                }
            }

            bool rateMatchesEither(uint8_t rate, uint16_t speciesId) noexcept
            {
                return rate == GBRestrictions::getCatchRate1(Enums::GameVersion::RD, speciesId) || rate == getCatchRateYellow(speciesId);
            }

            // PKHeX GBRestrictions.GetCatchRateMatchesPreEvolution
            bool getCatchRateMatchesPreEvolution(const Pokemon::Pokemon &pokemon, uint8_t rate)
            {
                const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::RBY);
                const Evolutions::EvolutionEntity entity = Evolutions::getEvolutionEntity(pokemon);
                Evolutions::EvolutionCriteria head;
                head.speciesId = pokemon.speciesID();
                head.formId = pokemon.formID();
                head.levelMax = entity.currentLevel;
                while (true)
                {
                    if (!GBRestrictions::isSpeciesNotAvailableCatchRate(head.speciesId) && rateMatchesEither(rate, head.speciesId))
                    {
                        return true;
                    }
                    Evolutions::EvolutionCriteria previous;
                    if (tree == nullptr || !tree->tryDevolve(Evolutions::SpeciesForm{head.speciesId, head.formId}, entity, head.levelMax, 2, false, Evolutions::EvolutionRuleTweak{}, previous))
                    {
                        break;
                    }
                    head = previous;
                }
                return (rate == CATCH_RATE_NORMAL_BOX || rate == CATCH_RATE_GORGEOUS_BOX) && isStadiumGiftSpecies(head.speciesId);
            }

            // PKHeX GBRestrictions.CanOriginateGen1
            bool canOriginateGen1(const Pokemon::Pokemon &pokemon)
            {
                if (GameBoyEntity::isKorean(pokemon) || pokemon.isEgg())
                {
                    return false;
                }
                // A Gen 2 record with met data was not met in Gen 1 (Stadium 2 keeps it, but this is
                // only asked while choosing which generation to search).
                if (GameBoyEntity::getCaughtData(pokemon) != 0)
                {
                    return false;
                }
                return GBRestrictions::canVisitGen1(pokemon.speciesID());
            }

            // PKHeX GBRestrictions.GetTradebackStatusRBY
            PotentialGBOrigin getTradebackStatusRBY(const Pokemon::Pokemon &pokemon)
            {
                if (!ALLOW_GEN1_TRADEBACK)
                {
                    return PotentialGBOrigin::Gen1Only;
                }
                const uint8_t rate = GameBoyEntity::getCatchRate(pokemon);
                if (rate == 0)
                {
                    return PotentialGBOrigin::Either;
                }
                if (!getCatchRateMatchesPreEvolution(pokemon, rate))
                {
                    return PotentialGBOrigin::Either;
                }
                if (GBRestrictions::isCatchRateHeldItem(rate))
                {
                    return PotentialGBOrigin::Either;
                }
                return PotentialGBOrigin::Gen1Only;
            }

            // PKHeX GBRestrictions.IsCatchRateMatchEncounter
            bool isCatchRateMatchEncounter(const Encounterable &encounter, const Pokemon::Pokemon &pokemon)
            {
                if (const EncounterGift1 *gift = encounterAs<EncounterGift1>(encounter))
                {
                    if (gift->getMatchRating(pokemon) < EncounterMatchRating::PartialMatch)
                    {
                        return true;
                    }
                }
                else if (const EncounterStatic1 *staticEncounter = encounterAs<EncounterStatic1>(encounter))
                {
                    if (staticEncounter->getMatchRating(pokemon) < EncounterMatchRating::PartialMatch)
                    {
                        return true;
                    }
                }
                else if (EncounterTrade1::isTemplateType(encounter.templateType()))
                {
                    return true;
                }
                return GBRestrictions::rateMatchesEncounter(encounter.speciesId(), encounter.version(), GameBoyEntity::getCatchRate(pokemon));
            }
        }

        bool GBRestrictions::isCatchRateHeldItem(uint8_t rate) noexcept
        {
            return ((CATCH_RATE_HELD_ITEM_FLAGS[rate >> 3] >> (rate & 7)) & 1) != 0;
        }

        uint8_t GBRestrictions::getCatchRate1(Enums::GameVersion version, uint16_t speciesId) noexcept
        {
            if (version == Enums::GameVersion::YW)
            {
                return getCatchRateYellow(speciesId);
            }
            return Pokemon::getPersonalRecord(Enums::GameVersion::RBY, speciesId, 0).catchRate;
        }

        // PKHeX GBRestrictions.RateMatchesEncounter
        bool GBRestrictions::rateMatchesEncounter(uint16_t speciesId, Enums::GameVersion version, uint8_t rate) noexcept
        {
            // PKHeX version.Contains(YW): Yellow itself, or the Red/Green/Blue/Yellow lump.
            if (version == Enums::GameVersion::YW || version == Enums::GameVersion::RBY)
            {
                if (rate == getCatchRateYellow(speciesId))
                {
                    return true;
                }
                if (version == Enums::GameVersion::YW)
                {
                    return false;
                }
            }
            return rate == getCatchRate1(Enums::GameVersion::RD, speciesId);
        }

        bool GBRestrictions::isSpeciesNotAvailableCatchRate(uint16_t speciesId) noexcept
        {
            switch (speciesId)
            {
            case 12: // Butterfree
            case 18: // Pidgeot
            case 31: // Nidoqueen
            case 34: // Nidoking
            case 38: // Ninetales
            case 45: // Vileplume
            case 53: // Persian
            case 59: // Arcanine
            case 62: // Poliwrath
            case 65: // Alakazam
            case 68: // Machamp
            case 71: // Victreebel
            case 78: // Rapidash
            case 91: // Cloyster
            case 103: // Exeggutor
            case 121: // Starmie
            case 149: // Dragonite
                return true;
            default:
                return false;
            }
        }

        bool GBRestrictions::canVisitGen1(uint16_t speciesId) noexcept
        {
            return speciesId <= MAX_SPECIES_ID_1 || isEvolvedFromGen1Species(speciesId);
        }

        bool GBRestrictions::isTradeEvolution1(uint16_t speciesId) noexcept
        {
            // Kadabra, Machoke, Graveler, Haunter.
            return speciesId == 64 || speciesId == 67 || speciesId == 75 || speciesId == 93;
        }

        // PKHeX GBRestrictions.GetTradebackStatusInitial
        PotentialGBOrigin GBRestrictions::getTradebackStatusInitial(const Pokemon::Pokemon &pokemon)
        {
            if (pokemon.getGameGroup() == Enums::GameVersion::RBY)
            {
                return getTradebackStatusRBY(pokemon);
            }
            if (pokemon.getGameGroup() == Enums::GameVersion::GSC || GameBoyEntity::isVirtualConsole2(pokemon))
            {
                return !canOriginateGen1(pokemon) ? PotentialGBOrigin::Gen2Only : PotentialGBOrigin::Either;
            }
            // A Virtual Console Gen 1 Pokemon traded forward cannot be told from one that visited Gen 2.
            return PotentialGBOrigin::Either;
        }

        // PKHeX GBRestrictions.IsTimeCapsuleTransferred
        TimeCapsuleEvaluation GBRestrictions::isTimeCapsuleTransferred(const Pokemon::Pokemon &pokemon, const std::array<LearnSource::MoveResult, 4> &moves, const Encounterable &encounter)
        {
            if (encounter.generation() == 2)
            {
                return TimeCapsuleEvaluation::Transferred21;
            }
            const uint8_t rate = GameBoyEntity::getCatchRate(pokemon);
            if (rate == 0)
            {
                return TimeCapsuleEvaluation::Transferred12;
            }
            if (LearnSource::isAnyFromContext(Enums::GameVersion::GSC, moves))
            {
                if (!isCatchRateHeldItem(rate))
                {
                    return TimeCapsuleEvaluation::BadCatchRate;
                }
                return TimeCapsuleEvaluation::Transferred12;
            }
            const bool isTradebackItem = isCatchRateHeldItem(rate);
            if (isCatchRateMatchEncounter(encounter, pokemon))
            {
                return isTradebackItem ? TimeCapsuleEvaluation::Indeterminate : TimeCapsuleEvaluation::NotTransferred;
            }
            return isTradebackItem ? TimeCapsuleEvaluation::Transferred12 : TimeCapsuleEvaluation::BadCatchRate;
        }

        uint8_t GameBoyEntity::getCatchRate(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (pokemon.getGameGroup() != Enums::GameVersion::RBY)
            {
                return 0;
            }
            return static_cast<const Pokemon::Pokemon1RBY &>(pokemon).catchRate();
        }

        uint16_t GameBoyEntity::getCaughtData(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (!GameBoyEntity::hasCaughtDataField(pokemon))
            {
                return 0;
            }
            return static_cast<const Pokemon::Pokemon2GSC &>(pokemon).caughtData();
        }

        bool GameBoyEntity::hasCaughtDataField(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.getGameGroup() == Enums::GameVersion::GSC;
        }

        uint8_t GameBoyEntity::getMetTimeOfDay(const Pokemon::Pokemon &pokemon) noexcept
        {
            return static_cast<uint8_t>(GameBoyEntity::getCaughtData(pokemon) >> 14);
        }

        bool GameBoyEntity::isJapanese(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese);
        }

        bool GameBoyEntity::isKorean(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean);
        }

        bool GameBoyEntity::isVirtualConsole1(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (!pokemon.hasOriginGame())
            {
                return false;
            }
            const uint8_t version = pokemon.originGame();
            return version >= VERSION_RED && version <= VERSION_YELLOW;
        }

        bool GameBoyEntity::isVirtualConsole2(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (!pokemon.hasOriginGame())
            {
                return false;
            }
            const uint8_t version = pokemon.originGame();
            return version >= VERSION_GOLD && version <= VERSION_CRYSTAL;
        }

        bool GameBoyEntity::isTradeTrainerMarker(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (pokemon.getGameGroup() == Enums::GameVersion::RBY)
            {
                const auto &record = static_cast<const Pokemon::Pokemon1RBY &>(pokemon);
                return record.rd8(record.ofsOT()) == Utils::GEN1_TRADE_OT && record.rd8(record.ofsOT() + 1) == Utils::GEN1_TERMINATOR;
            }
            if (pokemon.getGameGroup() == Enums::GameVersion::GSC)
            {
                const auto &record = static_cast<const Pokemon::Pokemon2GSC &>(pokemon);
                return record.rd8(record.ofsOT()) == Utils::GEN1_TRADE_OT && record.rd8(record.ofsOT() + 1) == Utils::GEN1_TERMINATOR;
            }
            return false;
        }

        bool GameBoyEntity::isShiny(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (getFormatGeneration(pokemon) <= 2)
            {
                return pokemon.isShiny(0, "");
            }
            return Encounters::isShiny(pokemon);
        }

        bool GameBoyEntity::hasMove(const Pokemon::Pokemon &pokemon, uint16_t moveId) noexcept
        {
            for (int slotIndex = 0; slotIndex < 4; ++slotIndex)
            {
                if (pokemon.move(slotIndex) == moveId)
                {
                    return true;
                }
            }
            return false;
        }
    }
}
