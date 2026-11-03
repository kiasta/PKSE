/**
 * A Gen 3 event distribution (a Wonder Card era gift, or an event egg). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Gifts/EncounterGift3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// The Gen 3 games a template can be received in, one bit each: PKHeX's single versions and
        /// its RS, FRLG, EFL and Gen3 lumps (GameVersion.ContainsFromLumped).
        enum Version3Mask : uint8_t
        {
            VERSION3_SAPPHIRE = 1 << 0,
            VERSION3_RUBY = 1 << 1,
            VERSION3_EMERALD = 1 << 2,
            VERSION3_FIRERED = 1 << 3,
            VERSION3_LEAFGREEN = 1 << 4,
            VERSION3_COLOSSEUM_XD = 1 << 5,
            VERSION3_RUBY_SAPPHIRE = VERSION3_RUBY | VERSION3_SAPPHIRE,
            VERSION3_FIRERED_LEAFGREEN = VERSION3_FIRERED | VERSION3_LEAFGREEN,
            VERSION3_EMERALD_FIRERED_LEAFGREEN = VERSION3_EMERALD | VERSION3_FIRERED_LEAFGREEN,
            VERSION3_ALL = VERSION3_RUBY_SAPPHIRE | VERSION3_EMERALD | VERSION3_FIRERED_LEAFGREEN | VERSION3_COLOSSEUM_XD,
        };

        /// The Version3Mask bit of a stored version id; 0 for anything else.
        uint8_t getVersion3Bit(uint8_t version) noexcept;

        /// PKHeX EncounterGift3NY / EncounterGift3JPN IsMatchExact, the shared tail: the Poke Ball, then
        /// the met level and location a Gen 3 record keeps, or the egg location a transferred one clears.
        bool isMatchDistributionMetData3(const Pokemon::Pokemon &pokemon, uint8_t level, uint16_t location);

        /// PKHeX GiftGender3: how the gift's OT gender is chosen.
        enum class GiftGender3 : uint8_t
        {
            Recipient,
            Only0,
            Only1,
            RandAlgo,
            RandD3,
            RandS3,
            RandS7,
            RandSG15,
            RandD3_0,
            RandD3_1,
        };

        class EncounterGift3 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RandomCorrelationTemplate, public FixedTrainerTemplate
        {
        public:
            /// PKHeX's UnspecifiedID: the gift takes the recipient's id.
            static constexpr uint16_t UNSPECIFIED_ID = 0xFFFF;

            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t versionMask = 0;
                bool isEgg = false;
                uint8_t metLevel = 0;
                RNG::PIDType method = RNG::PIDType::None;
                Moveset moves{};
                /// Empty when the gift carries the recipient's name.
                const char16_t *originalTrainerName = u"";
                GiftGender3 originalTrainerGender = GiftGender3::Recipient;
                uint16_t TID16 = UNSPECIFIED_ID;
                uint16_t SID16 = UNSPECIFIED_ID;
                uint8_t language = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                bool isFatefulEncounter = false;
                bool hasNationalRibbon = false;
            };

            explicit constexpr EncounterGift3(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift3; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift3; }
            EncounterType encounterType() const noexcept override { return properties.isEgg ? EncounterType::Egg : EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override;
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return 255; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }
            bool hasMetLevel() const noexcept override { return true; }
            uint8_t metLevel() const noexcept override { return properties.metLevel; }
            bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept override { return (properties.versionMask & getVersion3Bit(static_cast<uint8_t>(version))) != 0; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == properties.method ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return properties.method; }
            bool hasReviseReset() const noexcept override { return true; }
            RandomCorrelationRating isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const override;
            bool isFixedTrainer() const noexcept override;
            bool isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &, int) const override { return true; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift3>(*this); }

            const Properties &giftProperties() const noexcept { return properties; }

        private:
            uint32_t getGender(uint32_t seed) const noexcept;
            bool isGenderSpecificMatch(uint8_t gender) const noexcept;
            bool isMatchGender(const Pokemon::Pokemon &pokemon, uint32_t seed) const noexcept;

            Properties properties;
        };
    }
}

#endif
