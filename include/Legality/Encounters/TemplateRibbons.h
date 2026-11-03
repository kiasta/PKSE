/**
 * The ribbons an encounter template comes with. PKHeX RibbonIndex (one id per ribbon and mark, shared
 * by every format from Gen 3 on) and the gift templates' ribbon sets that PKHeX reads through
 * IRibbonSetEvent3, IRibbonSetEvent4 and IRibbonSetCommon7. Ported from PKHeX
 * PKHeX.Core/Ribbons/RibbonIndex.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_TEMPLATERIBBONS_H
#define LEGALITY_ENCOUNTERS_TEMPLATERIBBONS_H

#include <array>
#include <cstdint>
#include <string_view>

namespace Legality
{
    namespace Encounters
    {
        class Encounterable;

        /// PKHeX RibbonIndex, with its values (the Gen 8+ cards store them).
        enum class RibbonIndex : uint8_t
        {
            ChampionKalos, // 0
            ChampionG3,
            ChampionSinnoh,
            BestFriends,
            Training,
            BattlerSkillful,
            BattlerExpert,
            Effort,
            Alert,
            Shock,
            Downcast,
            Careless,
            Relax,
            Snooze,
            Smile,
            Gorgeous,
            Royal,
            GorgeousRoyal,
            Artist,
            Footprint,
            Record,
            Legend,
            Country,
            National,
            Earth,
            World,
            Classic,
            Premier,
            Event,
            Birthday,
            Special,
            Souvenir,
            Wishing,
            ChampionBattle,
            ChampionRegional,
            ChampionNational,
            ChampionWorld,
            CountMemoryContest,
            CountMemoryBattle,
            ChampionG6Hoenn,
            ContestStar,
            MasterCoolness,
            MasterBeauty,
            MasterCuteness,
            MasterCleverness,
            MasterToughness,
            ChampionAlola,
            BattleRoyale,
            BattleTreeGreat,
            BattleTreeMaster,
            ChampionGalar,
            TowerMaster,
            MasterRank,
            MarkLunchtime,
            MarkSleepyTime,
            MarkDusk,
            MarkDawn,
            MarkCloudy,
            MarkRainy,
            MarkStormy,
            MarkSnowy,
            MarkBlizzard,
            MarkDry,
            MarkSandstorm,
            MarkMisty,
            MarkDestiny,
            MarkFishing,
            MarkCurry,
            MarkUncommon,
            MarkRare,
            MarkRowdy,
            MarkAbsentMinded,
            MarkJittery,
            MarkExcited,
            MarkCharismatic,
            MarkCalmness,
            MarkIntense,
            MarkZonedOut,
            MarkJoyful,
            MarkAngry,
            MarkSmiley,
            MarkTeary,
            MarkUpbeat,
            MarkPeeved,
            MarkIntellectual,
            MarkFerocious,
            MarkCrafty,
            MarkScowling,
            MarkKindly,
            MarkFlustered,
            MarkPumpedUp,
            MarkZeroEnergy,
            MarkPrideful,
            MarkUnsure,
            MarkHumble,
            MarkThorny,
            MarkVigor,
            MarkSlump,
            Hisui,
            TwinklingStar,
            ChampionPaldea,
            MarkJumbo,
            MarkMini,
            MarkItemfinder,
            MarkPartner,
            MarkGourmand,
            OnceInALifetime,
            MarkAlpha,
            MarkMightiest,
            MarkTitan,
            Partner,
            MaxCount,
        };

        /// A set of RibbonIndex values, as a template's generated data stores it.
        struct RibbonIndexSet
        {
            std::array<uint8_t, 16> bits{};

            constexpr bool has(RibbonIndex ribbon) const noexcept
            {
                const uint8_t ribbonValue = static_cast<uint8_t>(ribbon);
                return (bits[ribbonValue >> 3] & (1u << (ribbonValue & 7))) != 0;
            }
        };

        /// The PKSE ribbon table's sprite stem for a RibbonIndex: PKHeX's property name in lower case.
        std::string_view getRibbonIndexSpriteStem(RibbonIndex ribbon) noexcept;
        /// The RibbonIndex whose sprite stem this is; MaxCount when none.
        RibbonIndex getRibbonIndexFromSpriteStem(std::string_view spriteStem) noexcept;

        /// Which of PKHeX's ribbon interfaces a template implements, and the ribbons it sets.
        struct TemplateRibbons
        {
            /// PKHeX `enc is IRibbonSetEvent3`.
            bool hasEvent3 = false;
            /// PKHeX `enc is IRibbonSetEvent4`.
            bool hasEvent4 = false;
            /// PKHeX `enc is IRibbonSetCommon7`.
            bool hasCommon7 = false;
            RibbonIndexSet ribbons;

            bool has(RibbonIndex ribbon) const noexcept { return ribbons.has(ribbon); }
        };

        TemplateRibbons getTemplateRibbons(const Encounterable &encounter) noexcept;

        /// PKHeX IEncounterServerDate.IsDateRestricted: the met date was set by a server the console
        /// cannot change (Pokemon GO, HOME gifts, and every Legends: Arceus, Scarlet/Violet and
        /// Legends: Z-A card).
        bool isEncounterDateRestricted(const Encounterable &encounter) noexcept;
    }
}

#endif
