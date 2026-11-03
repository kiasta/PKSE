#include "Legality/Encounters/EncounterVerifier.h"

#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EggHatchLocation.h"
#include "Legality/Encounters/EncounterEgg2.h"
#include "Legality/Encounters/EncounterEgg3.h"
#include "Legality/Encounters/EncounterEgg4.h"
#include "Legality/Encounters/EncounterEgg5.h"
#include "Legality/Encounters/EncounterEgg6.h"
#include "Legality/Encounters/EncounterEgg7.h"
#include "Legality/Encounters/EncounterEgg8.h"
#include "Legality/Encounters/EncounterEgg8b.h"
#include "Legality/Encounters/EncounterEgg9.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterInvalid.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterShadow3Colo.h"
#include "Legality/Encounters/EncounterSlot1.h"
#include "Legality/Encounters/EncounterSlot2.h"
#include "Legality/Encounters/EncounterStatic3.h"
#include "Legality/Encounters/EncounterStatic4.h"
#include "Legality/Encounters/EncounterTrade1.h"
#include "Legality/Encounters/EncounterTrade2.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Legality/Encounters/GroundTileAllowed.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWB7.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/MysteryGiftWA8.h"
#include "Legality/Encounters/MysteryGiftWB8.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/MysteryGiftWA9.h"
#include "Legality/Encounters/MysteryGiftWC9.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EggStateLegality: the met level a Gen 3/4 egg records, and the level a Gen 2/3
            /// egg is met at once transferred.
            constexpr uint8_t EGG_MET_LEVEL_GEN3_GEN4 = 0;
            constexpr uint8_t EGG_LEVEL_GEN2_GEN3 = 5;

            constexpr uint16_t SPECIES_MEW = 151;
            constexpr uint16_t SPECIES_DEOXYS = 386;
            /// Birth Island in FireRed/LeafGreen; also the Emerald location byte of Birth Island.
            constexpr uint16_t LOCATION_BIRTH_ISLAND3 = 200;
            constexpr uint8_t LOCATION_FARAWAY_ISLAND3 = 201;

            /// PKHeX LocationsHOME: where HOME puts a Switch FireRed/LeafGreen Pokemon.
            constexpr uint16_t LOCATION_HOME_VIRTUAL_CONSOLE3 = 30007;
            constexpr uint16_t LOCATION_HOME_LEAFGREEN_TO_SHIELD = 59994;
            constexpr uint16_t LOCATION_HOME_FIRERED_TO_SWORD = 59995;

            /// PKHeX GameVersion ids for the HOME FireRed/LeafGreen remap (SW = 44, SH = 45).
            constexpr uint8_t VERSION_SWORD = 44;
            constexpr uint8_t VERSION_SHIELD = 45;

            CheckResult getInvalid(ResultCode code, CheckIdentifier identifier = CheckIdentifier::Encounter)
            {
                return CheckResult::get(Judgement::Invalid, identifier, code);
            }

            CheckResult getInvalid(ResultCode code, uint8_t argumentValue)
            {
                return CheckResult::get(Judgement::Invalid, CheckIdentifier::Encounter, code, std::to_string(argumentValue));
            }

            CheckResult getValid(ResultCode code)
            {
                return CheckResult::get(Judgement::Valid, CheckIdentifier::Encounter, code);
            }

            bool isJapanese(const Pokemon::Pokemon &pokemon)
            {
                return pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese);
            }

            bool isFireRedLeafGreen(const Pokemon::Pokemon &pokemon)
            {
                return isVersion(pokemon, Enums::GameVersion::FR) || isVersion(pokemon, Enums::GameVersion::LG);
            }

            /// PKHeX ParseSettings.IsGen3HOMEOriginVirtual.
            bool isGen3HomeOriginVirtual(const Pokemon::Pokemon &pokemon, const LegalityInfo &info, bool state)
            {
                return getFormatGeneration(pokemon) >= 8 && Evolutions::isVersionVirtualConsoleGen3(info.entity) == state;
            }

            /// PKHeX ParseSettings.AllowGBACrossTransferRSE.
            bool allowsGameBoyAdvanceCrossTransfer(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                return info.allowEraCartGBA || isGen3HomeOriginVirtual(pokemon, info, false);
            }

            /// PKHeX ParseSettings.AllowGen3EventTicketsAll.
            bool allowsGen3EventTicketsAll(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                return info.allowEraSwitchGBA || !isGen3HomeOriginVirtual(pokemon, info, true);
            }

            /// PKHeX LocationsHOME.IsValidMetFRLG.
            bool isValidHomeMetFireRedLeafGreen(uint16_t location, uint8_t version)
            {
                return (location == LOCATION_HOME_LEAFGREEN_TO_SHIELD && version == VERSION_SHIELD) || (location == LOCATION_HOME_FIRERED_TO_SWORD && version == VERSION_SWORD);
            }

            CheckResult verifyUnhatchedEgg3(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.metLevel() != EGG_MET_LEVEL_GEN3_GEN4)
                {
                    return getInvalid(ResultCode::EggFMetLevel, EGG_MET_LEVEL_GEN3_GEN4);
                }
                const uint16_t expected = isFireRedLeafGreen(pokemon) ? LOCATION_HATCH_FRLG : LOCATION_HATCH_RSE;
                if (pokemon.metLocation() != expected)
                {
                    return getInvalid(ResultCode::EggMetLocationFail);
                }
                return getValid(ResultCode::EggLocation);
            }

            CheckResult verifyEncounterEgg3Transfer(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                if (pokemon.isEgg())
                {
                    return getInvalid(ResultCode::TransferEgg);
                }
                if (pokemon.metLevel() < EGG_LEVEL_GEN2_GEN3)
                {
                    return getInvalid(ResultCode::TransferEggMetLevel);
                }
                // PKHeX EncounterUtil.GetExpectedEggLocation(pk, 0): a Brilliant Diamond/Shining
                // Pearl record stores "none" as 65535.
                const uint16_t expectedEggLocation = pokemon.getGameGroup() == Enums::GameVersion::BDSP ? LOCATION_DEFAULT8B_NONE : 0;
                if (pokemon.eggLocation() != expectedEggLocation)
                {
                    return getInvalid(ResultCode::EggLocationNone);
                }

                const uint8_t formatGeneration = getFormatGeneration(pokemon);
                if (Evolutions::isVersionVirtualConsoleGen3(info.entity))
                {
                    if (pokemon.getGameGroup() == Enums::GameVersion::SWSH)
                    {
                        if (!isValidHomeMetFireRedLeafGreen(pokemon.metLocation(), getOriginVersion(pokemon)))
                        {
                            return getInvalid(ResultCode::TransferMetLocation);
                        }
                    }
                    else if (pokemon.metLocation() != LOCATION_HOME_VIRTUAL_CONSOLE3)
                    {
                        return getInvalid(ResultCode::TransferMetLocation);
                    }
                    // A Switch FireRed/LeafGreen transfer is always met at level 5.
                    if (pokemon.metLevel() != EGG_LEVEL_GEN2_GEN3)
                    {
                        return getInvalid(ResultCode::TransferEggMetLevel);
                    }
                }
                else if (formatGeneration != 4)
                {
                    if (pokemon.metLocation() != LOCATION_TRANSFER4)
                    {
                        return getInvalid(ResultCode::TransferEggLocationTransporter);
                    }
                }
                else if (pokemon.metLocation() != LOCATION_TRANSFER3)
                {
                    return getInvalid(ResultCode::EggLocationPalPark);
                }
                return getValid(ResultCode::EggLocation);
            }

            CheckResult verifyEncounterEgg3(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                if (getFormatGeneration(pokemon) != 3)
                {
                    return verifyEncounterEgg3Transfer(pokemon, info);
                }
                if (pokemon.metLevel() != EGG_MET_LEVEL_GEN3_GEN4)
                {
                    return getInvalid(ResultCode::EggFMetLevel, EGG_MET_LEVEL_GEN3_GEN4);
                }
                const uint8_t met = static_cast<uint8_t>(pokemon.metLocation());
                // Emerald: Birth Island is out of reach for Japanese players, Faraway Island for everyone else.
                const uint8_t inaccessible = isJapanese(pokemon) ? static_cast<uint8_t>(LOCATION_BIRTH_ISLAND3) : LOCATION_FARAWAY_ISLAND3;
                if (met == inaccessible)
                {
                    return getInvalid(ResultCode::EggLocationInvalid, met);
                }
                if (isValidMet3(met, getOriginVersion(pokemon)))
                {
                    return getValid(ResultCode::EggLocation);
                }
                // The version is not updated when an egg hatches in another game.
                if (!allowsGameBoyAdvanceCrossTransfer(pokemon, info))
                {
                    return getInvalid(ResultCode::EggLocationInvalid);
                }
                if (isValidMet3Any(met))
                {
                    return getValid(ResultCode::EggLocationTrade);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg4
            CheckResult verifyEncounterEgg4(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggLevel = 1;
                if (getFormatGeneration(pokemon) != 4)
                {
                    if (pokemon.isEgg())
                    {
                        return getInvalid(ResultCode::TransferEgg);
                    }
                    if (pokemon.metLevel() < eggLevel)
                    {
                        return getInvalid(ResultCode::TransferEggMetLevel);
                    }
                    if (pokemon.metLocation() != LOCATION_TRANSFER4)
                    {
                        return getInvalid(ResultCode::TransferEggLocationTransporter);
                    }
                    return getValid(ResultCode::EggLocation);
                }
                if (pokemon.metLevel() != EGG_MET_LEVEL_GEN3_GEN4)
                {
                    return getInvalid(ResultCode::EggFMetLevel, EGG_MET_LEVEL_GEN3_GEN4);
                }
                const uint16_t met = pokemon.metLocation();
                // The Shaymin event never reached Korea: Seabreak Path and Flower Paradise are out of reach.
                constexpr uint16_t locationFlowerParadise = 63;
                constexpr uint16_t locationSeabreakPath = 85;
                if (pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean) && (met == locationFlowerParadise || met == locationSeabreakPath))
                {
                    return getInvalid(ResultCode::EggLocationInvalid);
                }
                if (isValidMet4(met, getOriginVersion(pokemon)))
                {
                    return getValid(ResultCode::EggLocation);
                }
                // The version is not updated when an egg hatches in another game.
                if (pokemon.eggLocation() == LOCATION_LINK_TRADE4 && isValidMet4Any(met))
                {
                    return getValid(ResultCode::EggLocationTrade);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg5
            CheckResult verifyEncounterEgg5(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                if (isValidMet5(pokemon.metLocation(), getOriginVersion(pokemon)))
                {
                    return getValid(ResultCode::EggLocation);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyUnhatchedEgg5
            CheckResult verifyUnhatchedEgg5(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                if (isLinkTradeLocation5(pokemon.eggLocation()))
                {
                    return getInvalid(ResultCode::EggLocationTradeFail);
                }
                const uint16_t met = pokemon.metLocation();
                if (isLinkTradeLocation5(met))
                {
                    return getValid(ResultCode::EggLocationTrade);
                }
                return met == 0 ? getValid(ResultCode::EggUnhatched) : getInvalid(ResultCode::EggLocationNone);
            }

            // PKHeX EncounterVerifier.VerifyUnhatchedEgg
            CheckResult verifyUnhatchedEgg(const Pokemon::Pokemon &pokemon, uint16_t tradeLocation, uint16_t noneLocation = 0)
            {
                const uint8_t formatGeneration = getFormatGeneration(pokemon);
                const uint8_t eggLevel = formatGeneration == 3 || formatGeneration == 4 ? EGG_MET_LEVEL_GEN3_GEN4 : 1;
                if (pokemon.metLevel() != eggLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggLevel);
                }
                if (pokemon.eggLocation() == tradeLocation)
                {
                    return getInvalid(ResultCode::EggLocationTradeFail);
                }
                const uint16_t met = pokemon.metLocation();
                if (met == tradeLocation)
                {
                    return getValid(ResultCode::EggLocationTrade);
                }
                return met == noneLocation ? getValid(ResultCode::EggUnhatched) : getInvalid(ResultCode::EggLocationNone);
            }

            /// PKHeX's Gen 2 met level of a hatched egg (EggStateLegality.EggMetLevel).
            constexpr uint8_t EGG_MET_LEVEL_GEN2 = 1;
            /// PKHeX VerifyEncounterEgg2: the highest Gen 2 met location an egg can hatch at.
            constexpr uint16_t LOCATION_EGG_HATCH_LAST_GEN2 = 95;

            // PKHeX EncounterVerifier.VerifyEncounterEgg2
            CheckResult verifyEncounterEgg2(const Pokemon::Pokemon &pokemon)
            {
                if (GameBoyEntity::getCaughtData(pokemon) == 0)
                {
                    return getValid(ResultCode::EggLocation);
                }
                if (pokemon.metLevel() != EGG_MET_LEVEL_GEN2)
                {
                    return getInvalid(ResultCode::EggFMetLevel, EGG_MET_LEVEL_GEN2);
                }
                if (pokemon.metLocation() > LOCATION_EGG_HATCH_LAST_GEN2)
                {
                    return getInvalid(ResultCode::EggMetLocationFail);
                }
                return getValid(ResultCode::EggLocation);
            }

            // PKHeX EncounterVerifier.VerifyUnhatchedEgg2
            CheckResult verifyUnhatchedEgg2(const Pokemon::Pokemon &pokemon)
            {
                if (GameBoyEntity::getCaughtData(pokemon) == 0)
                {
                    return CheckResult::getValid(CheckIdentifier::Encounter);
                }
                if (pokemon.metLevel() != EGG_MET_LEVEL_GEN2)
                {
                    return getInvalid(ResultCode::EggFMetLevel, EGG_MET_LEVEL_GEN2);
                }
                if (pokemon.metLocation() != 0)
                {
                    return getInvalid(ResultCode::EggLocationInvalid);
                }
                return getValid(ResultCode::EggLocation);
            }

            /// PKHeX VerifyEncounterEggUnhatched.
            CheckResult verifyEncounterEggUnhatched(const Pokemon::Pokemon &pokemon, const Encounterable &egg)
            {
                if (encounterAs<EncounterEgg2>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg2(pokemon);
                }
                if (encounterAs<EncounterEgg3>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg3(pokemon);
                }
                if (encounterAs<EncounterEgg4>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg(pokemon, LOCATION_LINK_TRADE4);
                }
                if (encounterAs<EncounterEgg5>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg5(pokemon);
                }
                if (encounterAs<EncounterEgg6>(egg) != nullptr || encounterAs<EncounterEgg7>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg(pokemon, LOCATION_LINK_TRADE6);
                }
                if (encounterAs<EncounterEgg8>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg(pokemon, LOCATION_LINK_TRADE6);
                }
                if (encounterAs<EncounterEgg8b>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg(pokemon, LOCATION_LINK_TRADE6_NPC, LOCATION_DEFAULT8B_NONE);
                }
                if (encounterAs<EncounterEgg9>(egg) != nullptr)
                {
                    return verifyUnhatchedEgg(pokemon, LOCATION_LINK_TRADE6);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.IsValidMetForeignEggSWSH
            bool isValidMetForeignEggSwordShield(const Pokemon::Pokemon &pokemon, uint16_t met)
            {
                const uint8_t version = getOriginVersion(pokemon);
                if (version == static_cast<uint8_t>(Enums::GameVersion::BD) || version == static_cast<uint8_t>(Enums::GameVersion::SP))
                {
                    // PKHeX LocationsHOME.IsValidMetBDSP: Shining Pearl -> Shield, Brilliant Diamond -> Sword.
                    return (met == LOCATION_HOME_SHSP && version == static_cast<uint8_t>(Enums::GameVersion::SH)) || (met == LOCATION_HOME_SWBD && version == static_cast<uint8_t>(Enums::GameVersion::SW));
                }
                if (version == static_cast<uint8_t>(Enums::GameVersion::SL) || version == static_cast<uint8_t>(Enums::GameVersion::VL))
                {
                    // PKHeX LocationsHOME.IsValidMetSV: Violet -> Shield, Scarlet -> Sword.
                    return (met == LOCATION_HOME_SHVL && version == static_cast<uint8_t>(Enums::GameVersion::SH)) || (met == LOCATION_HOME_SWSL && version == static_cast<uint8_t>(Enums::GameVersion::SW));
                }
                return isValidMet8SWSH(met);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg6
            CheckResult verifyEncounterEgg6(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                const uint16_t met = pokemon.metLocation();
                const bool isXY = isVersion(pokemon, Enums::GameVersion::X) || isVersion(pokemon, Enums::GameVersion::Y);
                const bool isValid = isXY ? isValidMet6XY(met) : isValidMet6AO(met);
                return isValid ? getValid(ResultCode::EggLocation) : getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg7
            CheckResult verifyEncounterEgg7(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                const uint16_t met = pokemon.metLocation();
                const bool isSunMoon = isVersion(pokemon, Enums::GameVersion::SN) || isVersion(pokemon, Enums::GameVersion::MN);
                const bool isValid = isSunMoon ? isValidMet7SM(met) : isValidMet7USUM(met);
                return isValid ? getValid(ResultCode::EggLocation) : getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg8
            CheckResult verifyEncounterEgg8(const Pokemon::Pokemon &pokemon)
            {
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                if (isValidMetForeignEggSwordShield(pokemon, pokemon.metLocation()))
                {
                    return getValid(ResultCode::EggLocation);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg8BDSP
            CheckResult verifyEncounterEgg8BrilliantDiamondShiningPearl(const Pokemon::Pokemon &pokemon)
            {
                if (isFormatSwordShield(pokemon))
                {
                    return verifyEncounterEgg8(pokemon);
                }
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                uint16_t met = pokemon.metLocation();
                if (pokemon.getGameGroup() != Enums::GameVersion::BDSP)
                {
                    met = getLocationLocal8b(met);
                }
                const bool isBrilliantDiamond = getOriginVersion(pokemon) == static_cast<uint8_t>(Enums::GameVersion::BD);
                if (isBrilliantDiamond ? isValidMet8BD(met) : isValidMet8SP(met))
                {
                    return getValid(ResultCode::EggLocation);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterEgg9
            CheckResult verifyEncounterEgg9(const Pokemon::Pokemon &pokemon)
            {
                if (isFormatSwordShield(pokemon))
                {
                    return verifyEncounterEgg8(pokemon);
                }
                constexpr uint8_t eggMetLevel = 1;
                if (pokemon.metLevel() != eggMetLevel)
                {
                    return getInvalid(ResultCode::EggFMetLevel, eggMetLevel);
                }
                const uint16_t met = pokemon.metLocation();
                const bool isScarlet = getOriginVersion(pokemon) == static_cast<uint8_t>(Enums::GameVersion::SL);
                if (isScarlet ? isValidMet9SL(met) : isValidMet9VL(met))
                {
                    return getValid(ResultCode::EggLocation);
                }
                return getInvalid(ResultCode::EggLocationInvalid);
            }

            // PKHeX EncounterVerifier.VerifyEncounterG12
            CheckResult verifyEncounterG12(const Pokemon::Pokemon &pokemon, const Encounterable &encounter)
            {
                if (encounter.isEgg())
                {
                    return pokemon.isEgg() ? verifyUnhatchedEgg2(pokemon) : verifyEncounterEgg2(pokemon);
                }
                if (EncounterSlot1::isTemplateType(encounter.templateType()))
                {
                    return getValid(ResultCode::EncounterCondition);
                }
                if (const EncounterSlot2 *slot = encounterAs<EncounterSlot2>(encounter))
                {
                    // PKHeX VerifyWildEncounterGen2
                    if (!slot->isHeadbutt())
                    {
                        return getValid(ResultCode::EncounterCondition);
                    }
                    return slot->isTreeAvailable(pokemon.tid16()) ? getValid(ResultCode::Gen2TreeId) : getInvalid(ResultCode::Gen2InvalidTileTreeNotFound);
                }
                if (const EncounterTrade1 *trade = encounterAs<EncounterTrade1>(encounter))
                {
                    // PKHeX VerifyEncounterTrade: a trade that evolves on arrival cannot stay unevolved
                    // (an Everstone trade is a separate template without the flag).
                    if (trade->evolveOnTrade() && trade->speciesId() == pokemon.speciesID())
                    {
                        return getInvalid(ResultCode::EncounterTradeShouldHaveEvolvedToSpecies);
                    }
                    return getValid(ResultCode::EncounterTradeMatch);
                }
                if (EncounterTrade2::isTemplateType(encounter.templateType()))
                {
                    return getValid(ResultCode::EncounterTradeMatch);
                }
                return getValid(ResultCode::Valid);
            }
        }

        CheckResult EncounterVerifier::verifyEncounterEggHatched(const Pokemon::Pokemon &pokemon, Enums::GameVersion contextGroup, const LegalityInfo &info)
        {
            if (Enums::isGen3Group(contextGroup))
            {
                return verifyEncounterEgg3(pokemon, info);
            }
            if (contextGroup == Enums::GameVersion::DP)
            {
                return verifyEncounterEgg4(pokemon);
            }
            if (contextGroup == Enums::GameVersion::BW)
            {
                return verifyEncounterEgg5(pokemon);
            }
            if (contextGroup == Enums::GameVersion::XY)
            {
                return verifyEncounterEgg6(pokemon);
            }
            if (contextGroup == Enums::GameVersion::SM)
            {
                return verifyEncounterEgg7(pokemon);
            }
            if (contextGroup == Enums::GameVersion::SWSH)
            {
                return verifyEncounterEgg8(pokemon);
            }
            if (contextGroup == Enums::GameVersion::BDSP)
            {
                return verifyEncounterEgg8BrilliantDiamondShiningPearl(pokemon);
            }
            if (contextGroup == Enums::GameVersion::SV)
            {
                return verifyEncounterEgg9(pokemon);
            }
            if (contextGroup == Enums::GameVersion::GSC)
            {
                return verifyEncounterEgg2(pokemon);
            }
            return CheckResult::getValid(CheckIdentifier::Encounter);
        }

        CheckResult EncounterVerifier::verifyEncounter(uint8_t generation, const Pokemon::Pokemon &pokemon, const Encounterable &encounter, const LegalityInfo &info)
        {
            if (generation == 1 || generation == 2)
            {
                return verifyEncounterG12(pokemon, encounter);
            }

            // The Colosseum e-Reader cards were only released in Japan.
            if (const EncounterShadow3Colo *shadow = encounterAs<EncounterShadow3Colo>(encounter))
            {
                if (shadow->isEReader() && !isJapanese(pokemon))
                {
                    return getInvalid(ResultCode::Gen3EReader);
                }
            }

            if (const EncounterStatic3 *static3 = encounterAs<EncounterStatic3>(encounter))
            {
                // Mew at Faraway Island (Emerald) was only distributed in Japan.
                if (static3->speciesId() == SPECIES_MEW && !isJapanese(pokemon))
                {
                    return getInvalid(ResultCode::EncounterUnreleasedEMewJapan);
                }
                // Deoxys at Birth Island (FireRed/LeafGreen) was never distributed in Japan on
                // cartridge; the Switch release opened it to every language.
                if (static3->speciesId() == SPECIES_DEOXYS && static3->location() == LOCATION_BIRTH_ISLAND3 && isJapanese(pokemon) && !allowsGen3EventTicketsAll(pokemon, info))
                {
                    return getInvalid(ResultCode::EncounterUnreleased);
                }
            }

            if (const EncounterStatic4 *static4 = encounterAs<EncounterStatic4>(encounter))
            {
                // Shaymin at Flower Paradise was never distributed in Korea.
                constexpr uint16_t speciesShaymin = 492;
                if (static4->speciesId() == speciesShaymin && pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean))
                {
                    return getInvalid(ResultCode::EncounterUnreleased);
                }
                // Route 45 has no water a roamer can be surfed into.
                constexpr uint16_t locationRoute45 = 193;
                if (static4->isRoaming() && getFormatGeneration(pokemon) == 4 && pokemon.metLocation() == locationRoute45 && pokemon.groundTile() == GROUND_TILE_TYPE_WATER)
                {
                    return getInvalid(ResultCode::Gen4InvalidTileR45Surf);
                }
            }

            // PKHeX VerifyEncounterEvent for the Gen 4 gifts; the other generations' gifts belong to their ports.
            if (MysteryGiftPCD::isTemplateType(encounter.templateType()) || MysteryGiftPGT::isTemplateType(encounter.templateType()) || MysteryGiftPGF::isTemplateType(encounter.templateType()) || MysteryGiftWC6::isTemplateType(encounter.templateType()) || MysteryGiftWB7::isTemplateType(encounter.templateType()) || MysteryGiftWC7::isTemplateType(encounter.templateType()) || MysteryGiftWC8::isTemplateType(encounter.templateType()) || MysteryGiftWB8::isTemplateType(encounter.templateType()) || MysteryGiftWA8::isTemplateType(encounter.templateType()) || MysteryGiftWC9::isTemplateType(encounter.templateType()) || MysteryGiftWA9::isTemplateType(encounter.templateType()))
            {
                if (!pokemon.isEgg() && encounter.isEgg())
                {
                    const CheckResult hatchCheck = verifyEncounterEggHatched(pokemon, encounter.contextGroup(), info);
                    if (!hatchCheck.isValid())
                    {
                        return hatchCheck;
                    }
                }
                return getValid(ResultCode::Valid);
            }

            if (encounter.asEgg() != nullptr && pokemon.isEgg())
            {
                return verifyEncounterEggUnhatched(pokemon, encounter);
            }
            if (encounter.isEgg() && !pokemon.isEgg())
            {
                return verifyEncounterEggHatched(pokemon, encounter.contextGroup(), info);
            }
            if (EncounterInvalid::isTemplateType(encounter.templateType()))
            {
                return getInvalid(ResultCode::EncounterInvalid);
            }
            return getValid(ResultCode::Valid);
        }
    }
}
