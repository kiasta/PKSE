#ifndef ENUMS_BALL_H
#define ENUMS_BALL_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include "Enums/GameVersion.h"

namespace Enums
{
    /// Ball IDs for the corresponding English ball name.
    enum class Ball
    {
        None,
        Master,
        Ultra,
        Great,
        Poke,
        Safari,
        Net,
        Dive,
        Nest,
        Repeat,
        Timer,
        Luxury,
        Premier,
        Dusk,
        Heal,
        Quick,
        Cherish,
        Fast,
        Level,
        Lure,
        Heavy,
        Love,
        Friend,
        Moon,
        Sport,
        Dream,
        Beast,
        // Legends: Arceus
        Strange,
        LAPoke,
        LAGreat,
        LAUltra,
        LAFeather,
        LAWing,
        LAJet,
        LAHeavy,
        LALeaden,
        LAGigaton,
        LAOrigin
    };

    /// English ball name for a stored ball id (indices match the Ball enum above).
    inline const char *getBallName(uint8_t ballId)
    {
        static const char *const names[] = {
            "(none)", "Master Ball", "Ultra Ball", "Great Ball", "Poké Ball", "Safari Ball",
            "Net Ball", "Dive Ball", "Nest Ball", "Repeat Ball", "Timer Ball", "Luxury Ball",
            "Premier Ball", "Dusk Ball", "Heal Ball", "Quick Ball", "Cherish Ball", "Fast Ball",
            "Level Ball", "Lure Ball", "Heavy Ball", "Love Ball", "Friend Ball", "Moon Ball",
            "Sport Ball", "Dream Ball", "Beast Ball", "Strange Ball", "Poké Ball", "Great Ball",
            "Ultra Ball", "Feather Ball", "Wing Ball", "Jet Ball", "Heavy Ball", "Leaden Ball",
            "Gigaton Ball", "Origin Ball"};
        return ballId < (sizeof(names) / sizeof(names[0])) ? names[ballId] : "(unknown)";
    }

    /// Highest ball id each save format can hold.
    ///
    /// Through Gen 7 this is arithmetic on introductions: a generation keeps every ball the ones
    /// before it had and adds its own, and nothing carries a ball backward, because Bank and
    /// Transporter only ever move forward.
    ///
    /// FROM GEN 8 ON, HOME DECIDES, and it does not hold one ball per Pokemon -- it holds ONE PER
    /// GAME SIDE, and each side decides for itself what to do with a neighbouring side's. Only
    /// Sword/Shield converts: PKHeX's GameDataPK8 adopts a ball through `ball > Beast ? 4 : ball`,
    /// so a Hisui ball becomes a Poke Ball entering it. GameDataPB8, GameDataPK9 and GameDataPA9
    /// each do a plain `Ball = side.Ball` and take it unchanged, which is why BDSP, Scarlet/Violet
    /// and Legends: Z-A reach Origin while Sword/Shield stops at Beast. PKHeX's per-format
    /// Legal.MaxBallID_* agrees exactly, and is the corroboration rather than the source.
    ///
    /// Deliberately NOT BallUseLegality.GetWildBalls, which answers the much narrower "what can a
    /// WILD capture be in" and so omits Safari, Cherish and every fixed-ball encounter -- all of
    /// which a save holds perfectly legitimately.
    inline constexpr uint8_t MAX_BALL_GEN3 = 12;      // Premier -- the last of the twelve GBA balls
    inline constexpr uint8_t MAX_BALL_GEN4 = 24;      // Sport, from HeartGold/SoulSilver
    inline constexpr uint8_t MAX_BALL_GEN5 = 25;      // Dream, from the Dream World
    inline constexpr uint8_t MAX_BALL_GEN6 = 25;      // Gen 6 introduced none of its own
    inline constexpr uint8_t MAX_BALL_GEN7 = 26;      // Beast, completing the standard set
    inline constexpr uint8_t MAX_BALL_GEN8_SWSH = 26; // Beast again -- HOME converts anything above
    inline constexpr uint8_t MAX_BALL_HISUI = 37;     // Origin -- the top of the enum

    /**
     * Every ball id from Master (1) up to and including `maxBallId`.
     *
     * The standard sets are cumulative -- a generation keeps every ball the ones before it had and
     * adds its own -- so a contiguous run is the whole rule, and the only thing that varies per
     * group is where it stops. Legends: Arceus is spelled out below instead, because its set
     * REPLACES the standard one rather than extending it.
     */
    inline std::vector<uint8_t> getBallListUpTo(uint8_t maxBallId)
    {
        std::vector<uint8_t> ballIds;
        ballIds.reserve(maxBallId);
        for (uint8_t ballId = 1; ballId <= maxBallId; ++ballId)
        {
            ballIds.push_back(ballId);
        }
        return ballIds;
    }

    /**
     * The highest ball id a save-format group's record can hold.
     *
     * THE CRITERION IS WHAT A RECORD CAN HOLD, NOT WHAT THE GAME CAN CATCH. Diamond cannot produce
     * an Apricorn ball, but it trades with HeartGold, which can -- so 17-24 sit inside Diamond's
     * ceiling even though Diamond can never make one. What sits outside it is a ball introduced
     * AFTER the group: nothing has ever carried a ball backward, so a Dream Ball in a Gen 4 save
     * is not rare, it is impossible.
     *
     * THIS IS THE ONE CEILING TABLE. getBallList() below builds its picker rows from it and the
     * conversion layer clamps to it, so what an editor offers and what a transfer accepts cannot
     * drift apart.
     *
     * EVERY GROUP IS LISTED, and that is the point. The picker's version of this was a switch over
     * four groups -- Gen 3, Let's Go and Legends: Arceus -- with everything else falling into
     * `default`, which was right while the only other things reaching it were the four Switch
     * groups. Eleven pre-Switch groups joined later and inherited the modern 26 in silence: two
     * harmlessly (Gens 1 and 2 have no ball field) and two correctly by luck (Gen 7 really does
     * stop at Beast), but seven wrongly -- Diamond/Pearl, Platinum and HeartGold/SoulSilver were
     * offered the Dream and Beast Balls, and Black/White through ORAS the Beast Ball. Nothing
     * downstream caught it: the picker draws no legal marker for balls, and the legality checker of the time only
     * range-checked the id against the enum.
     */
    inline uint8_t maxBallForGroup(GameVersion group)
    {
        switch (group)
        {
        // Gens 1 and 2 have no ball field at all: Pokemon1RBY::ball() returns 0, Pokemon2GSC
        // inherits the base's, and both answer hasBall() false, which is what the details modal
        // gates the Ball row on. 0 is the honest ceiling, and it makes getBallList() hand back an
        // empty picker rather than balls the format cannot store.
        case GameVersion::RBY:
        case GameVersion::GSC:
            return 0;

        // Gen 3 balls (Master..Premier) -- the same twelve in all five GBA games, Safari and Dive
        // included.
        case GameVersion::FRLG:
        case GameVersion::RSE:
            return MAX_BALL_GEN3;

        // Gen 4 adds Dusk/Heal/Quick/Cherish, and HeartGold/SoulSilver adds Kurt's seven Apricorn
        // balls plus Sport. Diamond/Pearl and Platinum can hold those eight without being able to
        // make one, since all three games trade, so the whole generation shares one ceiling.
        case GameVersion::DP:
        case GameVersion::PT:
        case GameVersion::HGSS:
            return MAX_BALL_GEN4;

        // Gen 5 adds the Dream Ball.
        case GameVersion::BW:
        case GameVersion::B2W2:
            return MAX_BALL_GEN5;

        // Gen 6 adds nothing: PKHeX's own line is `WildPokeballs6 = WildPokeBalls5; // Same as Gen5`.
        case GameVersion::XY:
        case GameVersion::ORAS:
            return MAX_BALL_GEN6;

        // Gen 7 adds the Beast Ball, completing the standard set. Let's Go shares that ceiling --
        // the five balls its games actually have are a SET inside it, which is getBallList()'s
        // business rather than this function's.
        case GameVersion::SM:
        case GameVersion::USUM:
        case GameVersion::GG:
            return MAX_BALL_GEN7;

        // BDSP, Scarlet/Violet and Legends: Z-A adopt a neighbouring HOME side's ball VERBATIM --
        // `Ball = side.Ball`, in GameDataPB8, GameDataPK9 and GameDataPA9 alike -- so a Hisui ball
        // travels into all three unchanged, and so does the Strange Ball that a HOME transfer INTO
        // Legends: Arceus is stamped with. Legends: Arceus reaches Origin by owning those balls.
        case GameVersion::BDSP:
        case GameVersion::SV:
        case GameVersion::ZA:
        case GameVersion::PLA:
            return MAX_BALL_HISUI;

        // Sword/Shield is the one Switch group that stops at the standard set, and it stops there
        // because it is the one HOME converts for: GameDataPK8 adopts a neighbour's ball through
        // `ball > Beast ? 4 : ball`, so a PK8 cannot hold a Hisui ball however it arrived. This is
        // also where an unrecognised group lands -- ADD A CASE RATHER THAN LANDING HERE.
        case GameVersion::SWSH:
        default:
            return MAX_BALL_GEN8_SWSH;
        }
    }

    /**
     * The ball ids to OFFER for a group, which is not always everything it can hold.
     *
     * Two groups carry a set rather than a run, and in both the gap is real rather than caution.
     * Let's Go's format reaches Beast but its games have five balls. Legends: Arceus REPLACES the
     * standard numbering instead of extending it, so its eleven start at Strange and none of
     * 1-26 belongs. Every other group offers its whole range, since each id below the ceiling can
     * genuinely be caught in, traded into, or transferred in.
     */
    inline std::vector<uint8_t> getBallList(GameVersion group)
    {
        if (group == GameVersion::GG)
        {
            return {1, 2, 3, 4, 12}; // Master, Ultra, Great, Poke, Premier
        }
        if (group == GameVersion::PLA)
        {
            return {27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37}; // Strange, then Poke..Origin
        }
        return getBallListUpTo(maxBallForGroup(group)); // 0 for Gens 1 and 2 -> no rows at all
    }
}

#endif