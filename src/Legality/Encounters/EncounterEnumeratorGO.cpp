#include "Legality/Encounters/EncounterEnumeratorGO.h"

#include <span>

#include "Legality/Encounters/EncounterDataGO.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_SILCOON = 266;
            constexpr uint16_t SPECIES_DUSTOX = 269;
            constexpr uint16_t SPECIES_RUNERIGUS = 867;
            constexpr uint16_t SPECIES_WYRDEER = 899;
            constexpr uint16_t SPECIES_BASCULEGION = 902;
            constexpr uint16_t SPECIES_OVERQWIL = 904;
            constexpr uint16_t SPECIES_MAUSHOLD = 925;
            constexpr uint16_t SPECIES_ANNIHILAPE = 979;
            constexpr uint16_t SPECIES_DUDUNSPARCE = 982;
            constexpr uint16_t SPECIES_KINGAMBIT = 983;
            /// PKHeX EvolutionRestrictions.IsEvolvedSpeciesFormRare: one encryption constant in 100.
            constexpr uint32_t RARE_FORM_MODULUS = 100;

            enum class SeekMode : uint8_t
            {
                Forward,
                Reverse,
            };

            /// PKHeX EvolutionRestrictions.IsFormArgEvolution.
            bool isFormArgumentEvolution(uint16_t speciesId) noexcept
            {
                return speciesId == SPECIES_RUNERIGUS || speciesId == SPECIES_WYRDEER || speciesId == SPECIES_ANNIHILAPE || speciesId == SPECIES_BASCULEGION || speciesId == SPECIES_KINGAMBIT || speciesId == SPECIES_OVERQWIL;
            }

            /// PKHeX EvolutionRestrictions.GetIsExpectedEvolveFormEC100.
            bool isExpectedEvolveFormEncryptionConstant100(uint16_t speciesId, uint8_t formId, bool isRare) noexcept
            {
                if (speciesId == SPECIES_MAUSHOLD)
                {
                    return formId == (isRare ? 0 : 1);
                }
                return formId == (isRare ? 1 : 0);
            }

            // PKHeX EncounterEnumerator8GO.GetSeekMode: an evolution whose record contradicts its
            // first-listed template is searched from the end, so the other branch is found first.
            SeekMode getSeekMode(const Pokemon::Pokemon &pokemon) noexcept
            {
                const uint16_t speciesId = pokemon.speciesID();
                if (speciesId == SPECIES_DUDUNSPARCE || speciesId == SPECIES_MAUSHOLD)
                {
                    const bool isRare = pokemon.encryptionConstant() % RARE_FORM_MODULUS == 0;
                    if (!isExpectedEvolveFormEncryptionConstant100(speciesId, pokemon.form(), isRare))
                    {
                        return SeekMode::Reverse;
                    }
                }
                else if (isFormArgumentEvolution(speciesId))
                {
                    if (pokemon.hasFormArgument() && pokemon.formArgument() == 0)
                    {
                        return SeekMode::Reverse;
                    }
                }
                else if (speciesId >= SPECIES_SILCOON && speciesId <= SPECIES_DUSTOX && !WurmpleUtil::isWurmpleEvolutionValid(pokemon))
                {
                    return SeekMode::Reverse;
                }
                return SeekMode::Forward;
            }

            template <typename Area>
            bool isCompatible(const Area &area, const Evolutions::EvolutionCriteria &evolution, const Pokemon::Pokemon &pokemon) noexcept
            {
                if (area.speciesId != evolution.speciesId)
                {
                    return false;
                }
                return area.formId == evolution.formId || isFormChangeable(area.speciesId, area.formId, evolution.formId, Enums::GameVersion::SWSH, getEntityContextGroup(pokemon));
            }

            // PKHeX EncounterEnumerator7GO/8GO MoveNext: seek the next area whose species-form one
            // chain stage reaches, walk its slots for that stage, then seek on. A Reverse seek stops
            // before area 0, as PKHeX's `Index > 0` loop does.
            template <typename Area>
            MatchedEncounters enumerateAreas(std::span<const Area> areas, const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, SeekMode mode)
            {
                const std::span<const Evolutions::EvolutionCriteria> stages = chain.view();
                EncounterReference deferred;
                EncounterMatchRating deferredRating = EncounterMatchRating::MaxNotMatch;
                int areaIndex = mode == SeekMode::Reverse ? static_cast<int>(areas.size()) - 1 : 0;
                size_t stageIndex = 0;
                while (true)
                {
                    bool isFound = false;
                    for (; mode == SeekMode::Forward ? areaIndex < static_cast<int>(areas.size()) : areaIndex > 0; areaIndex += mode == SeekMode::Forward ? 1 : -1, stageIndex = 0)
                    {
                        const Area &area = areas[static_cast<size_t>(areaIndex)];
                        do
                        {
                            if (isCompatible(area, stages[stageIndex], pokemon))
                            {
                                isFound = true;
                                break;
                            }
                        } while (++stageIndex < stages.size());
                        if (isFound)
                        {
                            break;
                        }
                    }
                    if (!isFound)
                    {
                        break;
                    }

                    const Area &area = areas[static_cast<size_t>(areaIndex)];
                    const Evolutions::EvolutionCriteria &evolution = stages[stageIndex];
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount; ++slotIndex)
                    {
                        const auto &slot = area.slots[slotIndex];
                        if (slot.speciesId() != evolution.speciesId)
                        {
                            continue;
                        }
                        if (!slot.isMatchExact(pokemon, evolution))
                        {
                            continue;
                        }
                        const EncounterMatchRating rating = slot.getMatchRating(pokemon);
                        if (rating == EncounterMatchRating::Match)
                        {
                            co_yield MatchedEncounter{borrowEncounter(slot), rating};
                            continue;
                        }
                        if (rating < deferredRating)
                        {
                            deferred = borrowEncounter(slot);
                            deferredRating = rating;
                        }
                    }
                    stageIndex = 0;
                    areaIndex += mode == SeekMode::Forward ? 1 : -1;
                }
                if (deferred)
                {
                    co_yield MatchedEncounter{deferred, deferredRating};
                }
            }
        }

        MatchedEncounters EncounterEnumeratorGO::enumerateLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            if (chain.isEmpty() || !EncounterStateUtil::canBeWildEncounter(pokemon))
            {
                return MatchedEncounters();
            }
            return enumerateAreas(getAreasGoLetsGo(), pokemon, chain, SeekMode::Forward);
        }

        MatchedEncounters EncounterEnumeratorGO::enumerateHome(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            if (chain.isEmpty() || !EncounterStateUtil::canBeWildEncounter(pokemon))
            {
                return MatchedEncounters();
            }
            return enumerateAreas(getAreasGoHome(), pokemon, chain, getSeekMode(pokemon));
        }
    }
}
