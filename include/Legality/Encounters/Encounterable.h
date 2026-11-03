/**
 * An encounter template: one way a Pokemon can come into existence (a wild slot, a static
 * encounter, an in-game trade, a Mystery Gift, an egg). PKSE's counterpart of PKHeX's
 * IEncounterable, IEncounterTemplate and IEncounterMatch, ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Interfaces.
 *
 * PKHeX tests a template with `enc is EncounterSlot3` and `enc is IFixedTrainer`. With RTTI off,
 * the concrete type is answered by templateType() (encounterAs<T>() does the checked cast) and each
 * optional PKHeX interface by an as...() query that returns the interface or nullptr.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERABLE_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERABLE_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "Enums/Ball.h"
#include "Enums/GameVersion.h"
#include "Legality/AnalysisContext.h"
#include "Legality/Encounters/EncounterTemplateType.h"
#include "Legality/Encounters/Moveset.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/RNG/GenerateParameters.h"
#include "Legality/RNG/PIDIV.h"
#include "Legality/RNG/PIDType.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EncounterMatchRating: how well a template that passed isMatchExact() explains the
        /// Pokemon. Lower is better; the enumerator keeps the best non-Match as its fallback.
        enum class EncounterMatchRating : uint8_t
        {
            Match,
            Deferred,
            DeferredErrors,
            PartialMatch,
            MaxNotMatch,
        };

        /// PKHeX RandomCorrelationRating.
        enum class RandomCorrelationRating : uint8_t
        {
            Match,
            NotIdeal,
            Mismatch,
        };

        /// PKHeX FixedGenderUtil.GenderRandom.
        constexpr uint8_t GENDER_RANDOM = 0xFF;
        /// PKHeX Nature.Random.
        constexpr uint8_t NATURE_RANDOM = 25;

        class EncounterMatchTemplate;
        class FixedTrainerTemplate;
        class FixedNicknameTemplate;
        class MovesetTemplate;
        class RelearnTemplate;
        class RandomCorrelationTemplate;
        class EncounterSlot34Template;
        class EggTemplate;
        class SingleMoveBonusTemplate;
        class GroundTypeTileTemplate;

        class Encounterable
        {
        public:
            virtual ~Encounterable() = default;

            /// The concrete PKHeX class this template is (see encounterAs()).
            virtual EncounterTemplateType templateType() const noexcept = 0;
            /// PKSE's broad category (EncounterInfo::type), which some verifiers and the UI read.
            virtual EncounterType encounterType() const noexcept = 0;
            /// PKHeX IEncounterable.Name and LongName, in English, as PKHeX formats them: the parity
            /// harness compares them with PKHeX's matched template.
            virtual std::string name() const = 0;
            virtual std::string longName() const { return name(); }

            // PKHeX IEncounterTemplate.
            virtual uint16_t speciesId() const noexcept = 0;
            virtual uint8_t formId() const noexcept = 0;
            virtual uint8_t levelMin() const noexcept = 0;
            virtual uint8_t levelMax() const noexcept = 0;
            /// A single game or a PKSE group value when the template is shared (PKHeX's RSE, FRLG...).
            virtual Enums::GameVersion version() const noexcept = 0;
            virtual uint8_t generation() const noexcept = 0;
            /// PKHeX IContext, as a context group (Legality::Evolutions::EVOLUTION_CONTEXT_GROUPS).
            virtual Enums::GameVersion contextGroup() const noexcept = 0;
            virtual uint16_t location() const noexcept = 0;
            virtual uint16_t eggLocation() const noexcept = 0;
            virtual bool isEgg() const noexcept = 0;
            virtual Enums::Ball fixedBall() const noexcept { return Enums::Ball::None; }
            virtual RNG::Shiny shiny() const noexcept { return RNG::Shiny::Random; }
            virtual RNG::AbilityPermission ability() const noexcept { return RNG::AbilityPermission::Any12; }

            // Single-value PKHeX interfaces, folded into the base with PKHeX's "not fixed" value.
            /// PKHeX IFixedGender.Gender; GENDER_RANDOM when the template does not fix it.
            virtual uint8_t fixedGender() const noexcept { return GENDER_RANDOM; }
            /// PKHeX IFixedNature.Nature; NATURE_RANDOM when the template does not fix it.
            virtual uint8_t fixedNature() const noexcept { return NATURE_RANDOM; }
            /// PKHeX IFixedIVSet.IVs; isSpecified false when the template does not fix them.
            virtual RNG::IndividualValueSet fixedIVs() const noexcept { return RNG::IndividualValueSet{}; }
            /// PKHeX IFlawlessIVCount.FlawlessIVCount; 0 when the template guarantees none.
            virtual uint8_t flawlessIVCount() const noexcept { return 0; }
            /// PKHeX IFatefulEncounterReadOnly.FatefulEncounter.
            virtual bool isFatefulEncounter() const noexcept { return false; }
            /// PKHeX IMetLevel: true when the template states its own met level (metLevel()).
            virtual bool hasMetLevel() const noexcept { return false; }
            virtual uint8_t metLevel() const noexcept { return 0; }
            /// PKHeX IEncounterFormRandom.IsRandomUnspecificForm.
            virtual bool isRandomUnspecificForm() const noexcept { return false; }
            /// PKHeX IHatchCycle.EggCycles; 0 when the template does not state one.
            virtual uint8_t eggCycles() const noexcept { return 0; }
            /// PKHeX IRestrictVersion.CanBeReceivedByVersion; true for templates without the interface.
            virtual bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept
            {
                (void)version;
                return true;
            }

            // Multi-member PKHeX interfaces: `enc is IFoo f` is `const auto *f = enc.asFoo()`.
            virtual const EncounterMatchTemplate *asEncounterMatch() const noexcept { return nullptr; }
            virtual const FixedTrainerTemplate *asFixedTrainer() const noexcept { return nullptr; }
            virtual const FixedNicknameTemplate *asFixedNickname() const noexcept { return nullptr; }
            virtual const MovesetTemplate *asMoveset() const noexcept { return nullptr; }
            virtual const RelearnTemplate *asRelearn() const noexcept { return nullptr; }
            virtual const RandomCorrelationTemplate *asRandomCorrelation() const noexcept { return nullptr; }
            virtual const EncounterSlot34Template *asEncounterSlot34() const noexcept { return nullptr; }
            virtual const EggTemplate *asEgg() const noexcept { return nullptr; }
            virtual const SingleMoveBonusTemplate *asSingleMoveBonus() const noexcept { return nullptr; }
            virtual const GroundTypeTileTemplate *asGroundTypeTile() const noexcept { return nullptr; }

            /// Copies a template the finder keeps after the enumerator that produced it is gone.
            virtual std::unique_ptr<Encounterable> clone() const = 0;

            // PKHeX ILevelRange extensions.
            bool isFixedLevel() const noexcept { return levelMin() == levelMax(); }
            bool isLevelWithinRange(uint8_t level) const noexcept { return levelMin() <= level && level <= levelMax(); }
            bool isLevelWithinRange(uint8_t rangeMin, uint8_t rangeMax) const noexcept { return levelMin() <= rangeMax && rangeMin <= levelMax(); }
            bool isLevelWithinRange(uint8_t level, uint8_t minDecrease, uint8_t maxIncrease) const noexcept { return levelMin() - minDecrease <= level && level <= levelMax() + maxIncrease; }
            /// PKHeX ILocation.GetLocation.
            uint16_t getLocation() const noexcept { return location() != 0 ? location() : eggLocation(); }
            /// PKHeX IEncounterTemplate.IsWithinEncounterRange.
            bool isWithinEncounterRange(const Pokemon::Pokemon &pokemon) const;
            /// PKHeX IFixedGender.IsFixedGender, IFixedNature.IsFixedNature.
            bool isFixedGender() const noexcept { return fixedGender() != GENDER_RANDOM; }
            bool isFixedNature() const noexcept { return fixedNature() < NATURE_RANDOM; }
        };

        /// PKHeX IEncounterMatch. Every table template implements it; eggs and EncounterInvalid do not.
        class EncounterMatchTemplate
        {
        public:
            virtual ~EncounterMatchTemplate() = default;
            virtual bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const = 0;
            virtual EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const = 0;
        };

        /// PKHeX IFixedTrainer.
        class FixedTrainerTemplate
        {
        public:
            virtual ~FixedTrainerTemplate() = default;
            virtual bool isFixedTrainer() const noexcept = 0;
            virtual bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const = 0;
        };

        /// PKHeX IFixedNickname.
        class FixedNicknameTemplate
        {
        public:
            virtual ~FixedNicknameTemplate() = default;
            virtual bool isFixedNickname() const noexcept = 0;
            virtual bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const = 0;
            virtual std::u16string getNickname(int language) const = 0;
        };

        /// PKHeX IMoveset.
        class MovesetTemplate
        {
        public:
            virtual ~MovesetTemplate() = default;
            virtual Moveset moves() const noexcept = 0;
        };

        /// PKHeX IRelearn.
        class RelearnTemplate
        {
        public:
            virtual ~RelearnTemplate() = default;
            virtual Moveset relearnMoves() const noexcept = 0;
        };

        /// PKHeX IRandomCorrelation, and IRandomCorrelationEvent3 when hasReviseReset() is true.
        class RandomCorrelationTemplate
        {
        public:
            virtual ~RandomCorrelationTemplate() = default;
            virtual RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const = 0;
            virtual RNG::PIDType getSuggestedCorrelation() const noexcept = 0;
            virtual bool hasReviseReset() const noexcept { return false; }
            /// PKHeX IRandomCorrelationEvent3.IsCompatibleReviseReset.
            virtual RandomCorrelationRating isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const
            {
                return isCompatible(pidiv.type, pokemon);
            }
        };

        /// PKHeX IEncounterSlot34 (with IEncounterSlot3 / IEncounterSlot4's Type).
        class EncounterSlot34Template
        {
        public:
            virtual ~EncounterSlot34Template() = default;
            virtual uint8_t slotNumber() const noexcept = 0;
            virtual uint8_t magnetPullIndex() const noexcept = 0;
            virtual uint8_t magnetPullCount() const noexcept = 0;
            virtual uint8_t staticIndex() const noexcept = 0;
            virtual uint8_t staticCount() const noexcept = 0;
            virtual uint8_t pressureLevel() const noexcept = 0;
            virtual uint8_t areaRate() const noexcept = 0;
            /// The SlotType3 / SlotType4 value.
            virtual uint8_t slotType() const noexcept = 0;
        };

        /// PKHeX IEncounterEgg. The learnset is reached through the move port's own lookup for
        /// (generation, version, species, form); canHaveVoltTackle is PKHeX's.
        class EggTemplate
        {
        public:
            virtual ~EggTemplate() = default;
            virtual bool canHaveVoltTackle() const noexcept = 0;
        };

        /// PKHeX ISingleMoveBonus.
        class SingleMoveBonusTemplate
        {
        public:
            virtual ~SingleMoveBonusTemplate() = default;
            virtual bool isMoveBonusPossible() const noexcept = 0;
            virtual bool isMoveBonusRequired() const noexcept = 0;
            virtual std::span<const uint16_t> getMoveBonusPossible() const noexcept = 0;
            virtual bool isMoveBonus(uint16_t moveId) const noexcept = 0;
        };

        /// PKHeX IGroundTypeTile (Gen 4 templates).
        class GroundTypeTileTemplate
        {
        public:
            virtual ~GroundTypeTileTemplate() = default;
            /// GROUND_TILE_* bits (Legality/Encounters/GroundTileAllowed.h).
            virtual uint32_t groundTile() const noexcept = 0;
            /// PKHeX HasGroundTile: the formats that still store the tile.
            static constexpr bool hasGroundTile(uint8_t formatGeneration) noexcept { return formatGeneration >= 4 && formatGeneration <= 6; }
        };

        /// PKHeX `enc as T`: the template as its concrete class, or nullptr. A class lists the
        /// template types it covers in a static isTemplateType() (EncounterSlot3 also covers the
        /// swarm slot that derives from it).
        template <typename TemplateClass>
        const TemplateClass *encounterAs(const Encounterable &encounter) noexcept
        {
            if (!TemplateClass::isTemplateType(encounter.templateType()))
            {
                return nullptr;
            }
            return static_cast<const TemplateClass *>(&encounter);
        }

        template <typename TemplateClass>
        const TemplateClass *encounterAs(const Encounterable *encounter) noexcept
        {
            if (!encounter)
            {
                return nullptr;
            }
            return encounterAs<TemplateClass>(*encounter);
        }
    }
}

#endif
