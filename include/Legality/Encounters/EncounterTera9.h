/**
 * A Scarlet/Violet stock Tera Raid den encounter (Paldea, Kitakami or Blueberry Academy). Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterTera9.cs and ITeraRaid9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTERA9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTERA9_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX TeraRaidMapParent.
        enum class TeraRaidMapParent : uint8_t
        {
            Paldea,
            Kitakami,
            Blueberry,
        };

        class EncounterTera9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint8_t level = 0;
                Moveset moves{};
                RNG::GemType teraType = RNG::GemType::Default;
                /// The distribution index; 0 for a stock den.
                uint8_t raidIndex = 0;
                uint8_t stars = 0;
                /// The weight this encounter has among its star rank's encounters.
                uint8_t randomRate = 0;
                /// The total weight of the lower-index encounters per game; -1 when the game cannot host it.
                int16_t randomRateMinimumScarlet = 0;
                int16_t randomRateMinimumViolet = 0;
                TeraRaidMapParent map = TeraRaidMapParent::Paldea;
            };

            explicit constexpr EncounterTera9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Tera9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Tera9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Raid; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return LOCATION_TERA_CAVERN9; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isRandomUnspecificForm() const noexcept override { return properties.formId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTera9>(*this); }

            RNG::GemType teraType() const noexcept { return properties.teraType; }
            uint8_t stars() const noexcept { return properties.stars; }
            uint8_t raidIndex() const noexcept { return properties.raidIndex; }
            /// PKHeX IsDistribution.
            bool isDistribution() const noexcept { return properties.raidIndex != 0; }
            TeraRaidMapParent map() const noexcept { return properties.map; }
            /// PKHeX CanBeEncountered: the raid seed's star and opponent rolls select this encounter.
            bool canBeEncountered(uint32_t seed) const noexcept;

            /// PKHeX GetRateTotalSL / GetRateTotalVL: the weight total of a star rank's encounters.
            static int16_t getRateTotalScarlet(int stars, TeraRaidMapParent map) noexcept;
            static int16_t getRateTotalViolet(int stars, TeraRaidMapParent map) noexcept;

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };

        /// PKHeX `pk is ITeraType t && !Tera9RNG.IsMatchTeraType(...)` and the other raid matchers'
        /// shared tail: the record is not the raid seed's Pokemon.
        bool isRaidSeedMismatch9(const Pokemon::Pokemon &pokemon, uint32_t seed, RNG::GemType teraType, uint16_t speciesId, uint8_t formId);
    }
}

#endif
