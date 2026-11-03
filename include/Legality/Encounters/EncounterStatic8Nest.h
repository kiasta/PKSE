/**
 * The Sword/Shield Max Raid templates: a stock den raid (8N), a Watchtower crystal den (8NC), a
 * distribution den (8ND) and a Dynamax Adventure in the Max Lair (8U), with the raid seed check
 * they share. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen8/EncounterStatic8Nest.cs,
 * EncounterStatic8N.cs, EncounterStatic8NC.cs, EncounterStatic8ND.cs, EncounterStatic8U.cs and
 * PKHeX.Core/Legality/Encounters/Data/Gen8/Encounters8Nest.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8NEST_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8NEST_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncounterStatic8Nest<T>: the fields and matching every raid template shares.
        class EncounterStatic8Nest : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::SWSH;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                Moveset moves{};
                RNG::IndividualValueSet individualValues{};
                uint8_t dynamaxLevel = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = GENDER_RANDOM;
                uint8_t flawlessIVCount = 0;
                bool canGigantamax = false;
            };

            explicit constexpr EncounterStatic8Nest(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8N || type == EncounterTemplateType::Static8NC || type == EncounterTemplateType::Static8ND || type == EncounterTemplateType::Static8U; }
            EncounterType encounterType() const noexcept override { return EncounterType::Raid; }
            std::string longName() const override { return name(); }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SWSH; }
            uint16_t location() const noexcept override { return LOCATION_SHARED_NEST8; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }

            uint8_t dynamaxLevel() const noexcept { return properties.dynamaxLevel; }
            bool canGigantamax() const noexcept { return properties.canGigantamax; }
            /// PKHeX Verify: the raid RNG from `seed` reproduces the record.
            bool verify(const Pokemon::Pokemon &pokemon, uint64_t seed, bool forceNoShiny = false) const;
            /// PKHeX TryGetSeed: true with the seed when a raid seed explains the record.
            bool tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const;

        protected:
            virtual bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept;
            virtual bool isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept;
            virtual bool isMatchSeed(const Pokemon::Pokemon &pokemon, uint64_t seed) const { return verify(pokemon, seed); }
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;

            Properties properties;

        private:
            EncounterMatchRating isMatchDeferred(const Pokemon::Pokemon &pokemon) const;
            bool isMatchCorrelation(const Pokemon::Pokemon &pokemon) const;
        };

        /// PKHeX EncounterStatic8N: a stock raid den, by nest and star rank.
        class EncounterStatic8N final : public EncounterStatic8Nest
        {
        public:
            constexpr EncounterStatic8N(const Properties &properties, uint8_t nestIndex, uint8_t minRank, uint8_t maxRank) noexcept : EncounterStatic8Nest(properties), nestIndex(nestIndex), minRank(minRank), maxRank(maxRank) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8N; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8N; }
            std::string name() const override;
            uint8_t levelMin() const noexcept override;
            uint8_t levelMax() const noexcept override;
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8N>(*this); }

            /// PKHeX IsDownLeveled(pk).
            bool isDownLeveled(const Pokemon::Pokemon &pokemon) const noexcept;

        protected:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept override;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept override;
            bool isMatchSeed(const Pokemon::Pokemon &pokemon, uint64_t seed) const override;

        private:
            bool isDownLeveled(const Pokemon::Pokemon &pokemon, uint32_t metRank, uint8_t metLevel) const noexcept;
            /// PKHeX IsPossibleSeed: whether the seed rolls the met level, and whether it was down-leveled.
            bool isPossibleSeed(const Pokemon::Pokemon &pokemon, uint64_t seed, bool checkDynamaxLevel, bool &forceNoShiny) const noexcept;

            uint8_t nestIndex;
            uint8_t minRank;
            uint8_t maxRank;
        };

        /// PKHeX EncounterStatic8NC: a Watchtower Ruins crystal den.
        class EncounterStatic8NC final : public EncounterStatic8Nest
        {
        public:
            explicit constexpr EncounterStatic8NC(const Properties &properties) noexcept : EncounterStatic8Nest(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8NC; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8NC; }
            std::string name() const override { return "Watchtower Crystal Den Encounter"; }
            uint16_t location() const noexcept override { return LOCATION_WATCHTOWER8; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8NC>(*this); }

        protected:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept override;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept override;
        };

        /// PKHeX EncounterStatic8ND: a distribution raid den.
        class EncounterStatic8ND final : public EncounterStatic8Nest
        {
        public:
            constexpr EncounterStatic8ND(const Properties &properties, uint8_t distributionIndex) noexcept : EncounterStatic8Nest(properties), distributionIndex(distributionIndex) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8ND; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8ND; }
            std::string name() const override;
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8ND>(*this); }

        protected:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept override;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept override;
            bool isMatchSeed(const Pokemon::Pokemon &pokemon, uint64_t seed) const override;

        private:
            uint8_t distributionIndex;
        };

        /// PKHeX EncounterStatic8U: a Dynamax Adventure in the Max Lair.
        class EncounterStatic8U final : public EncounterStatic8Nest
        {
        public:
            /// PKHeX ShinyXor: a Max Lair encounter forced shiny always has XOR 1.
            static constexpr uint32_t SHINY_XOR = 1;

            explicit constexpr EncounterStatic8U(const Properties &properties) noexcept : EncounterStatic8Nest(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8U; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8U; }
            std::string name() const override { return "Max Lair Encounter"; }
            uint16_t location() const noexcept override { return LOCATION_MAX_LAIR8; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8U>(*this); }

            /// PKHeX IsShinyXorValid: not shiny, or shiny with XOR 1.
            static bool isShinyXorValid(uint32_t shinyXor) noexcept { return shinyXor > 15 || shinyXor == SHINY_XOR; }
            /// PKHeX ShouldHaveScientistTrash: a level 65 (non-legendary) catch.
            bool shouldHaveScientistTrash() const noexcept { return properties.level != 70; }

        protected:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept override;
        };
    }
}

#endif
