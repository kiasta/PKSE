// AUTO-GENERATED ribbon tables, one per save format's ribbon layout, with PKSE's English names.
// Regenerate with tools/gen_ribbonnames.py (see tools/pkhex_source.py). Sources: PKHeX.Core's
// PKM/PA8.cs, PKM/PA9.cs, PKM/PB7.cs, PKM/PK3.cs, PKM/PK4.cs, PKM/PK5.cs, PKM/PK6.cs, PKM/PK7.cs, PKM/PK9.cs, PKM/Shared/G8PKM.cs, Ribbons/RibbonInfo.cs and Ribbons/RibbonIndex.cs.
//
// A flag is bit `bit` of the byte at `byteOffset` in the decrypted entity buffer, exactly like
// PKHeX's FlagUtil.GetFlag(Data, byteOffset, bit). Names are the games' own, with the word
// "Ribbon" dropped where the games append it; marks keep a trailing " Mark". The sprite stem is
// the PKHeX property name in lower case, which is how PKHeX.Drawing.Misc names the image.
#include "Names/RibbonNames.h"
#include "Names/NameLanguage.h"

#include <cstddef>
#include <string_view>

namespace Names
{
    namespace
    {
        constexpr RibbonDefinition flag(uint8_t byteOffset, uint8_t bit, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::Flag, byteOffset, bit, 0, 1, englishName, spriteStem};
        }

        constexpr RibbonDefinition contestRank(uint8_t wordOffset, uint8_t rankShift, uint8_t maxCount, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::ContestRank, wordOffset, rankShift, 0, maxCount, englishName, spriteStem};
        }

        constexpr RibbonDefinition memoryCount(uint8_t flagByteOffset, uint8_t flagBit, uint8_t countByteOffset, uint8_t maxCount, const char *englishName, const char *spriteStem)
        {
            return RibbonDefinition{RibbonKind::MemoryCount, flagByteOffset, flagBit, countByteOffset, maxCount, englishName, spriteStem};
        }

        // PK3 (Ruby/Sapphire/Emerald, FireRed/LeafGreen): one u32 at 0x4C. The five contest ribbons are
        // 3-bit rank fields; bit 31 is FatefulEncounter, not a ribbon.
        const RibbonDefinition GEN3_RIBBONS[] = {
            contestRank(0x4C, 0, 4, "Cool Ribbon", "ribbong3cool"),
            contestRank(0x4C, 3, 4, "Beauty Ribbon", "ribbong3beauty"),
            contestRank(0x4C, 6, 4, "Cute Ribbon", "ribbong3cute"),
            contestRank(0x4C, 9, 4, "Smart Ribbon", "ribbong3smart"),
            contestRank(0x4C, 12, 4, "Tough Ribbon", "ribbong3tough"),
            flag(0x4D, 7, "Champion", "ribbonchampiong3"),
            flag(0x4E, 0, "Winning", "ribbonwinning"),
            flag(0x4E, 1, "Victory", "ribbonvictory"),
            flag(0x4E, 2, "Artist", "ribbonartist"),
            flag(0x4E, 3, "Effort", "ribboneffort"),
            flag(0x4E, 4, "Battle Champion", "ribbonchampionbattle"),
            flag(0x4E, 5, "Regional Champion", "ribbonchampionregional"),
            flag(0x4E, 6, "National Champion", "ribbonchampionnational"),
            flag(0x4E, 7, "Country", "ribboncountry"),
            flag(0x4F, 0, "National", "ribbonnational"),
            flag(0x4F, 1, "Earth", "ribbonearth"),
            flag(0x4F, 2, "World", "ribbonworld"),
        };

        // PK4 and PK5. The Hoenn contest ribbons are separate FLAGS here, one per rank.
        const RibbonDefinition GEN45_RIBBONS[] = {
            flag(0x24, 0, "Sinnoh Champion", "ribbonchampionsinnoh"),
            flag(0x24, 1, "Ability", "ribbonability"),
            flag(0x24, 2, "Great Ability", "ribbonabilitygreat"),
            flag(0x24, 3, "Double Ability", "ribbonabilitydouble"),
            flag(0x24, 4, "Multi Ability", "ribbonabilitymulti"),
            flag(0x24, 5, "Pair Ability", "ribbonabilitypair"),
            flag(0x24, 6, "World Ability", "ribbonabilityworld"),
            flag(0x24, 7, "Alert", "ribbonalert"),
            flag(0x25, 0, "Shock", "ribbonshock"),
            flag(0x25, 1, "Downcast", "ribbondowncast"),
            flag(0x25, 2, "Careless", "ribboncareless"),
            flag(0x25, 3, "Relax", "ribbonrelax"),
            flag(0x25, 4, "Snooze", "ribbonsnooze"),
            flag(0x25, 5, "Smile", "ribbonsmile"),
            flag(0x25, 6, "Gorgeous", "ribbongorgeous"),
            flag(0x25, 7, "Royal", "ribbonroyal"),
            flag(0x26, 0, "Gorgeous Royal", "ribbongorgeousroyal"),
            flag(0x26, 1, "Footprint", "ribbonfootprint"),
            flag(0x26, 2, "Record", "ribbonrecord"),
            flag(0x26, 3, "Event", "ribbonevent"),
            flag(0x26, 4, "Legend", "ribbonlegend"),
            flag(0x26, 5, "World Champion", "ribbonchampionworld"),
            flag(0x26, 6, "Birthday", "ribbonbirthday"),
            flag(0x26, 7, "Special", "ribbonspecial"),
            flag(0x27, 0, "Souvenir", "ribbonsouvenir"),
            flag(0x27, 1, "Wishing", "ribbonwishing"),
            flag(0x27, 2, "Classic", "ribbonclassic"),
            flag(0x27, 3, "Premier", "ribbonpremier"),
            flag(0x3C, 0, "Cool Ribbon", "ribbong3cool"),
            flag(0x3C, 1, "Cool Ribbon Super", "ribbong3coolsuper"),
            flag(0x3C, 2, "Cool Ribbon Hyper", "ribbong3coolhyper"),
            flag(0x3C, 3, "Cool Ribbon Master", "ribbong3coolmaster"),
            flag(0x3C, 4, "Beauty Ribbon", "ribbong3beauty"),
            flag(0x3C, 5, "Beauty Ribbon Super", "ribbong3beautysuper"),
            flag(0x3C, 6, "Beauty Ribbon Hyper", "ribbong3beautyhyper"),
            flag(0x3C, 7, "Beauty Ribbon Master", "ribbong3beautymaster"),
            flag(0x3D, 0, "Cute Ribbon", "ribbong3cute"),
            flag(0x3D, 1, "Cute Ribbon Super", "ribbong3cutesuper"),
            flag(0x3D, 2, "Cute Ribbon Hyper", "ribbong3cutehyper"),
            flag(0x3D, 3, "Cute Ribbon Master", "ribbong3cutemaster"),
            flag(0x3D, 4, "Smart Ribbon", "ribbong3smart"),
            flag(0x3D, 5, "Smart Ribbon Super", "ribbong3smartsuper"),
            flag(0x3D, 6, "Smart Ribbon Hyper", "ribbong3smarthyper"),
            flag(0x3D, 7, "Smart Ribbon Master", "ribbong3smartmaster"),
            flag(0x3E, 0, "Tough Ribbon", "ribbong3tough"),
            flag(0x3E, 1, "Tough Ribbon Super", "ribbong3toughsuper"),
            flag(0x3E, 2, "Tough Ribbon Hyper", "ribbong3toughhyper"),
            flag(0x3E, 3, "Tough Ribbon Master", "ribbong3toughmaster"),
            flag(0x3E, 4, "Champion", "ribbonchampiong3"),
            flag(0x3E, 5, "Winning", "ribbonwinning"),
            flag(0x3E, 6, "Victory", "ribbonvictory"),
            flag(0x3E, 7, "Artist", "ribbonartist"),
            flag(0x3F, 0, "Effort", "ribboneffort"),
            flag(0x3F, 1, "Battle Champion", "ribbonchampionbattle"),
            flag(0x3F, 2, "Regional Champion", "ribbonchampionregional"),
            flag(0x3F, 3, "National Champion", "ribbonchampionnational"),
            flag(0x3F, 4, "Country", "ribboncountry"),
            flag(0x3F, 5, "National", "ribbonnational"),
            flag(0x3F, 6, "Earth", "ribbonearth"),
            flag(0x3F, 7, "World", "ribbonworld"),
            flag(0x60, 0, "Cool Ribbon (Sinnoh)", "ribbong4cool"),
            flag(0x60, 1, "Cool Ribbon Great", "ribbong4coolgreat"),
            flag(0x60, 2, "Cool Ribbon Ultra", "ribbong4coolultra"),
            flag(0x60, 3, "Cool Ribbon Master (Sinnoh)", "ribbong4coolmaster"),
            flag(0x60, 4, "Beauty Ribbon (Sinnoh)", "ribbong4beauty"),
            flag(0x60, 5, "Beauty Ribbon Great", "ribbong4beautygreat"),
            flag(0x60, 6, "Beauty Ribbon Ultra", "ribbong4beautyultra"),
            flag(0x60, 7, "Beauty Ribbon Master (Sinnoh)", "ribbong4beautymaster"),
            flag(0x61, 0, "Cute Ribbon (Sinnoh)", "ribbong4cute"),
            flag(0x61, 1, "Cute Ribbon Great", "ribbong4cutegreat"),
            flag(0x61, 2, "Cute Ribbon Ultra", "ribbong4cuteultra"),
            flag(0x61, 3, "Cute Ribbon Master (Sinnoh)", "ribbong4cutemaster"),
            flag(0x61, 4, "Smart Ribbon (Sinnoh)", "ribbong4smart"),
            flag(0x61, 5, "Smart Ribbon Great", "ribbong4smartgreat"),
            flag(0x61, 6, "Smart Ribbon Ultra", "ribbong4smartultra"),
            flag(0x61, 7, "Smart Ribbon Master (Sinnoh)", "ribbong4smartmaster"),
            flag(0x62, 0, "Tough Ribbon (Sinnoh)", "ribbong4tough"),
            flag(0x62, 1, "Tough Ribbon Great", "ribbong4toughgreat"),
            flag(0x62, 2, "Tough Ribbon Ultra", "ribbong4toughultra"),
            flag(0x62, 3, "Tough Ribbon Master (Sinnoh)", "ribbong4toughmaster"),
        };

        // PK6.
        const RibbonDefinition GEN6_RIBBONS[] = {
            flag(0x30, 0, "Kalos Champion", "ribbonchampionkalos"),
            flag(0x30, 1, "Champion", "ribbonchampiong3"),
            flag(0x30, 2, "Sinnoh Champion", "ribbonchampionsinnoh"),
            flag(0x30, 3, "Best Friends", "ribbonbestfriends"),
            flag(0x30, 4, "Training", "ribbontraining"),
            flag(0x30, 5, "Skillful Battler", "ribbonbattlerskillful"),
            flag(0x30, 6, "Expert Battler", "ribbonbattlerexpert"),
            flag(0x30, 7, "Effort", "ribboneffort"),
            flag(0x31, 0, "Alert", "ribbonalert"),
            flag(0x31, 1, "Shock", "ribbonshock"),
            flag(0x31, 2, "Downcast", "ribbondowncast"),
            flag(0x31, 3, "Careless", "ribboncareless"),
            flag(0x31, 4, "Relax", "ribbonrelax"),
            flag(0x31, 5, "Snooze", "ribbonsnooze"),
            flag(0x31, 6, "Smile", "ribbonsmile"),
            flag(0x31, 7, "Gorgeous", "ribbongorgeous"),
            flag(0x32, 0, "Royal", "ribbonroyal"),
            flag(0x32, 1, "Gorgeous Royal", "ribbongorgeousroyal"),
            flag(0x32, 2, "Artist", "ribbonartist"),
            flag(0x32, 3, "Footprint", "ribbonfootprint"),
            flag(0x32, 4, "Record", "ribbonrecord"),
            flag(0x32, 5, "Legend", "ribbonlegend"),
            flag(0x32, 6, "Country", "ribboncountry"),
            flag(0x32, 7, "National", "ribbonnational"),
            flag(0x33, 0, "Earth", "ribbonearth"),
            flag(0x33, 1, "World", "ribbonworld"),
            flag(0x33, 2, "Classic", "ribbonclassic"),
            flag(0x33, 3, "Premier", "ribbonpremier"),
            flag(0x33, 4, "Event", "ribbonevent"),
            flag(0x33, 5, "Birthday", "ribbonbirthday"),
            flag(0x33, 6, "Special", "ribbonspecial"),
            flag(0x33, 7, "Souvenir", "ribbonsouvenir"),
            flag(0x34, 0, "Wishing", "ribbonwishing"),
            flag(0x34, 1, "Battle Champion", "ribbonchampionbattle"),
            flag(0x34, 2, "Regional Champion", "ribbonchampionregional"),
            flag(0x34, 3, "National Champion", "ribbonchampionnational"),
            flag(0x34, 4, "World Champion", "ribbonchampionworld"),
            memoryCount(0x34, 5, 0x38, 40, "Contest Memory", "ribboncountmemorycontest"),
            memoryCount(0x34, 6, 0x39, 8, "Battle Memory", "ribboncountmemorybattle"),
            flag(0x34, 7, "Hoenn Champion", "ribbonchampiong6hoenn"),
            flag(0x35, 0, "Contest Star", "ribbonconteststar"),
            flag(0x35, 1, "Coolness Master", "ribbonmastercoolness"),
            flag(0x35, 2, "Beauty Master", "ribbonmasterbeauty"),
            flag(0x35, 3, "Cuteness Master", "ribbonmastercuteness"),
            flag(0x35, 4, "Cleverness Master", "ribbonmastercleverness"),
            flag(0x35, 5, "Toughness Master", "ribbonmastertoughness"),
        };

        // PK7 and PB7: PK6's block plus four Gen 7 ribbons.
        const RibbonDefinition GEN7_RIBBONS[] = {
            flag(0x30, 0, "Kalos Champion", "ribbonchampionkalos"),
            flag(0x30, 1, "Champion", "ribbonchampiong3"),
            flag(0x30, 2, "Sinnoh Champion", "ribbonchampionsinnoh"),
            flag(0x30, 3, "Best Friends", "ribbonbestfriends"),
            flag(0x30, 4, "Training", "ribbontraining"),
            flag(0x30, 5, "Skillful Battler", "ribbonbattlerskillful"),
            flag(0x30, 6, "Expert Battler", "ribbonbattlerexpert"),
            flag(0x30, 7, "Effort", "ribboneffort"),
            flag(0x31, 0, "Alert", "ribbonalert"),
            flag(0x31, 1, "Shock", "ribbonshock"),
            flag(0x31, 2, "Downcast", "ribbondowncast"),
            flag(0x31, 3, "Careless", "ribboncareless"),
            flag(0x31, 4, "Relax", "ribbonrelax"),
            flag(0x31, 5, "Snooze", "ribbonsnooze"),
            flag(0x31, 6, "Smile", "ribbonsmile"),
            flag(0x31, 7, "Gorgeous", "ribbongorgeous"),
            flag(0x32, 0, "Royal", "ribbonroyal"),
            flag(0x32, 1, "Gorgeous Royal", "ribbongorgeousroyal"),
            flag(0x32, 2, "Artist", "ribbonartist"),
            flag(0x32, 3, "Footprint", "ribbonfootprint"),
            flag(0x32, 4, "Record", "ribbonrecord"),
            flag(0x32, 5, "Legend", "ribbonlegend"),
            flag(0x32, 6, "Country", "ribboncountry"),
            flag(0x32, 7, "National", "ribbonnational"),
            flag(0x33, 0, "Earth", "ribbonearth"),
            flag(0x33, 1, "World", "ribbonworld"),
            flag(0x33, 2, "Classic", "ribbonclassic"),
            flag(0x33, 3, "Premier", "ribbonpremier"),
            flag(0x33, 4, "Event", "ribbonevent"),
            flag(0x33, 5, "Birthday", "ribbonbirthday"),
            flag(0x33, 6, "Special", "ribbonspecial"),
            flag(0x33, 7, "Souvenir", "ribbonsouvenir"),
            flag(0x34, 0, "Wishing", "ribbonwishing"),
            flag(0x34, 1, "Battle Champion", "ribbonchampionbattle"),
            flag(0x34, 2, "Regional Champion", "ribbonchampionregional"),
            flag(0x34, 3, "National Champion", "ribbonchampionnational"),
            flag(0x34, 4, "World Champion", "ribbonchampionworld"),
            memoryCount(0x34, 5, 0x38, 40, "Contest Memory", "ribboncountmemorycontest"),
            memoryCount(0x34, 6, 0x39, 8, "Battle Memory", "ribboncountmemorybattle"),
            flag(0x34, 7, "Hoenn Champion", "ribbonchampiong6hoenn"),
            flag(0x35, 0, "Contest Star", "ribbonconteststar"),
            flag(0x35, 1, "Coolness Master", "ribbonmastercoolness"),
            flag(0x35, 2, "Beauty Master", "ribbonmasterbeauty"),
            flag(0x35, 3, "Cuteness Master", "ribbonmastercuteness"),
            flag(0x35, 4, "Cleverness Master", "ribbonmastercleverness"),
            flag(0x35, 5, "Toughness Master", "ribbonmastertoughness"),
            flag(0x35, 6, "Alola Champion", "ribbonchampionalola"),
            flag(0x35, 7, "Battle Royal Master", "ribbonbattleroyale"),
            flag(0x36, 0, "Battle Tree Great", "ribbonbattletreegreat"),
            flag(0x36, 1, "Battle Tree Master", "ribbonbattletreemaster"),
        };

        // The HOME block of every Gen 8/9 format, in RibbonIndex order: AffixedRibbon stores a row's
        // position here.
        const RibbonDefinition GEN89_RIBBONS[] = {
            flag(0x34, 0, "Kalos Champion", "ribbonchampionkalos"),
            flag(0x34, 1, "Champion", "ribbonchampiong3"),
            flag(0x34, 2, "Sinnoh Champion", "ribbonchampionsinnoh"),
            flag(0x34, 3, "Best Friends", "ribbonbestfriends"),
            flag(0x34, 4, "Training", "ribbontraining"),
            flag(0x34, 5, "Skillful Battler", "ribbonbattlerskillful"),
            flag(0x34, 6, "Expert Battler", "ribbonbattlerexpert"),
            flag(0x34, 7, "Effort", "ribboneffort"),
            flag(0x35, 0, "Alert", "ribbonalert"),
            flag(0x35, 1, "Shock", "ribbonshock"),
            flag(0x35, 2, "Downcast", "ribbondowncast"),
            flag(0x35, 3, "Careless", "ribboncareless"),
            flag(0x35, 4, "Relax", "ribbonrelax"),
            flag(0x35, 5, "Snooze", "ribbonsnooze"),
            flag(0x35, 6, "Smile", "ribbonsmile"),
            flag(0x35, 7, "Gorgeous", "ribbongorgeous"),
            flag(0x36, 0, "Royal", "ribbonroyal"),
            flag(0x36, 1, "Gorgeous Royal", "ribbongorgeousroyal"),
            flag(0x36, 2, "Artist", "ribbonartist"),
            flag(0x36, 3, "Footprint", "ribbonfootprint"),
            flag(0x36, 4, "Record", "ribbonrecord"),
            flag(0x36, 5, "Legend", "ribbonlegend"),
            flag(0x36, 6, "Country", "ribboncountry"),
            flag(0x36, 7, "National", "ribbonnational"),
            flag(0x37, 0, "Earth", "ribbonearth"),
            flag(0x37, 1, "World", "ribbonworld"),
            flag(0x37, 2, "Classic", "ribbonclassic"),
            flag(0x37, 3, "Premier", "ribbonpremier"),
            flag(0x37, 4, "Event", "ribbonevent"),
            flag(0x37, 5, "Birthday", "ribbonbirthday"),
            flag(0x37, 6, "Special", "ribbonspecial"),
            flag(0x37, 7, "Souvenir", "ribbonsouvenir"),
            flag(0x38, 0, "Wishing", "ribbonwishing"),
            flag(0x38, 1, "Battle Champion", "ribbonchampionbattle"),
            flag(0x38, 2, "Regional Champion", "ribbonchampionregional"),
            flag(0x38, 3, "National Champion", "ribbonchampionnational"),
            flag(0x38, 4, "World Champion", "ribbonchampionworld"),
            memoryCount(0x38, 5, 0x3C, 40, "Contest Memory", "ribboncountmemorycontest"),
            memoryCount(0x38, 6, 0x3D, 8, "Battle Memory", "ribboncountmemorybattle"),
            flag(0x38, 7, "Hoenn Champion", "ribbonchampiong6hoenn"),
            flag(0x39, 0, "Contest Star", "ribbonconteststar"),
            flag(0x39, 1, "Coolness Master", "ribbonmastercoolness"),
            flag(0x39, 2, "Beauty Master", "ribbonmasterbeauty"),
            flag(0x39, 3, "Cuteness Master", "ribbonmastercuteness"),
            flag(0x39, 4, "Cleverness Master", "ribbonmastercleverness"),
            flag(0x39, 5, "Toughness Master", "ribbonmastertoughness"),
            flag(0x39, 6, "Alola Champion", "ribbonchampionalola"),
            flag(0x39, 7, "Battle Royal Master", "ribbonbattleroyale"),
            flag(0x3A, 0, "Battle Tree Great", "ribbonbattletreegreat"),
            flag(0x3A, 1, "Battle Tree Master", "ribbonbattletreemaster"),
            flag(0x3A, 2, "Galar Champion", "ribbonchampiongalar"),
            flag(0x3A, 3, "Tower Master", "ribbontowermaster"),
            flag(0x3A, 4, "Master Rank", "ribbonmasterrank"),
            flag(0x3A, 5, "Lunchtime Mark", "ribbonmarklunchtime"),
            flag(0x3A, 6, "Sleepy-Time Mark", "ribbonmarksleepytime"),
            flag(0x3A, 7, "Dusk Mark", "ribbonmarkdusk"),
            flag(0x3B, 0, "Dawn Mark", "ribbonmarkdawn"),
            flag(0x3B, 1, "Cloudy Mark", "ribbonmarkcloudy"),
            flag(0x3B, 2, "Rainy Mark", "ribbonmarkrainy"),
            flag(0x3B, 3, "Stormy Mark", "ribbonmarkstormy"),
            flag(0x3B, 4, "Snowy Mark", "ribbonmarksnowy"),
            flag(0x3B, 5, "Blizzard Mark", "ribbonmarkblizzard"),
            flag(0x3B, 6, "Dry Mark", "ribbonmarkdry"),
            flag(0x3B, 7, "Sandstorm Mark", "ribbonmarksandstorm"),
            flag(0x40, 0, "Misty Mark", "ribbonmarkmisty"),
            flag(0x40, 1, "Destiny Mark", "ribbonmarkdestiny"),
            flag(0x40, 2, "Fishing Mark", "ribbonmarkfishing"),
            flag(0x40, 3, "Curry Mark", "ribbonmarkcurry"),
            flag(0x40, 4, "Uncommon Mark", "ribbonmarkuncommon"),
            flag(0x40, 5, "Rare Mark", "ribbonmarkrare"),
            flag(0x40, 6, "Rowdy Mark", "ribbonmarkrowdy"),
            flag(0x40, 7, "Absent-Minded Mark", "ribbonmarkabsentminded"),
            flag(0x41, 0, "Jittery Mark", "ribbonmarkjittery"),
            flag(0x41, 1, "Excited Mark", "ribbonmarkexcited"),
            flag(0x41, 2, "Charismatic Mark", "ribbonmarkcharismatic"),
            flag(0x41, 3, "Calmness Mark", "ribbonmarkcalmness"),
            flag(0x41, 4, "Intense Mark", "ribbonmarkintense"),
            flag(0x41, 5, "Zoned-Out Mark", "ribbonmarkzonedout"),
            flag(0x41, 6, "Joyful Mark", "ribbonmarkjoyful"),
            flag(0x41, 7, "Angry Mark", "ribbonmarkangry"),
            flag(0x42, 0, "Smiley Mark", "ribbonmarksmiley"),
            flag(0x42, 1, "Teary Mark", "ribbonmarkteary"),
            flag(0x42, 2, "Upbeat Mark", "ribbonmarkupbeat"),
            flag(0x42, 3, "Peeved Mark", "ribbonmarkpeeved"),
            flag(0x42, 4, "Intellectual Mark", "ribbonmarkintellectual"),
            flag(0x42, 5, "Ferocious Mark", "ribbonmarkferocious"),
            flag(0x42, 6, "Crafty Mark", "ribbonmarkcrafty"),
            flag(0x42, 7, "Scowling Mark", "ribbonmarkscowling"),
            flag(0x43, 0, "Kindly Mark", "ribbonmarkkindly"),
            flag(0x43, 1, "Flustered Mark", "ribbonmarkflustered"),
            flag(0x43, 2, "Pumped-Up Mark", "ribbonmarkpumpedup"),
            flag(0x43, 3, "Zero Energy Mark", "ribbonmarkzeroenergy"),
            flag(0x43, 4, "Prideful Mark", "ribbonmarkprideful"),
            flag(0x43, 5, "Unsure Mark", "ribbonmarkunsure"),
            flag(0x43, 6, "Humble Mark", "ribbonmarkhumble"),
            flag(0x43, 7, "Thorny Mark", "ribbonmarkthorny"),
            flag(0x44, 0, "Vigor Mark", "ribbonmarkvigor"),
            flag(0x44, 1, "Slump Mark", "ribbonmarkslump"),
            flag(0x44, 2, "Hisui", "ribbonhisui"),
            flag(0x44, 3, "Twinkling Star", "ribbontwinklingstar"),
            flag(0x44, 4, "Paldea Champion", "ribbonchampionpaldea"),
            flag(0x44, 5, "Jumbo Mark", "ribbonmarkjumbo"),
            flag(0x44, 6, "Mini Mark", "ribbonmarkmini"),
            flag(0x44, 7, "Itemfinder Mark", "ribbonmarkitemfinder"),
            flag(0x45, 0, "Partner Mark", "ribbonmarkpartner"),
            flag(0x45, 1, "Gourmand Mark", "ribbonmarkgourmand"),
            flag(0x45, 2, "Once-in-a-Lifetime", "ribbononceinalifetime"),
            flag(0x45, 3, "Alpha Mark", "ribbonmarkalpha"),
            flag(0x45, 4, "Mightiest Mark", "ribbonmarkmightiest"),
            flag(0x45, 5, "Titan Mark", "ribbonmarktitan"),
            flag(0x45, 6, "Partner", "ribbonpartner"),
        };

        // Indexed by a Gen 3 contest rank. The field stores only the HIGHEST rank reached.
        const char *const GEN3_CONTEST_RANK_NAMES[] = {"", "", " Super", " Hyper", " Master"};
        const char *const GEN3_CONTEST_RANK_SPRITE_SUFFIXES[] = {"", "", "super", "hyper", "master"};
        static_assert(sizeof(GEN3_CONTEST_RANK_NAMES) == sizeof(GEN3_CONTEST_RANK_SPRITE_SUFFIXES), "one image suffix per contest rank name");
        constexpr uint32_t GEN3_CONTEST_RANK_MASK = 7;

        /// The active language's wording for a ribbon PKSE named in English. One place, so every
        /// name the tables produce goes through the same lookup.
        std::string localize(const std::string &englishName)
        {
            const size_t languageIndex = displayLanguageIndex();
            if (languageIndex == LANGUAGE_INDEX_ENGLISH) return englishName;
            const char *localized = getRibbonNameLocalized(englishName.c_str(), languageIndex);
            return (localized != nullptr && localized[0] != '\0') ? std::string(localized) : englishName;
        }

        uint32_t readRibbonWord(const uint8_t *entityData, uint8_t wordOffset)
        {
            const uint8_t *wordBytes = entityData + wordOffset;
            return static_cast<uint32_t>(wordBytes[0]) | (static_cast<uint32_t>(wordBytes[1]) << 8) | (static_cast<uint32_t>(wordBytes[2]) << 16) | (static_cast<uint32_t>(wordBytes[3]) << 24);
        }

        void writeRibbonWord(uint8_t *entityData, uint8_t wordOffset, uint32_t ribbonWord)
        {
            uint8_t *wordBytes = entityData + wordOffset;
            for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
            {
                wordBytes[byteIndex] = static_cast<uint8_t>(ribbonWord >> (8 * byteIndex));
            }
        }

        void setFlag(uint8_t *entityData, uint8_t byteOffset, uint8_t bit, bool flagSet)
        {
            const uint8_t mask = static_cast<uint8_t>(1u << bit);
            entityData[byteOffset] = flagSet ? static_cast<uint8_t>(entityData[byteOffset] | mask) : static_cast<uint8_t>(entityData[byteOffset] & ~mask);
        }
    }

    std::span<const RibbonDefinition> getRibbonDefinitions(Enums::GameVersion group)
    {
        // A game id is taken to its group; a group id is not a game and getGameGroup() refuses it.
        const Enums::GameVersion gameGroup = Enums::getGameGroup(group) != Enums::GameVersion::Invalid ? Enums::getGameGroup(group) : group;
        switch (gameGroup)
        {
        case Enums::GameVersion::RSE:
        case Enums::GameVersion::FRLG:
            return GEN3_RIBBONS;
        case Enums::GameVersion::DP:
        case Enums::GameVersion::PT:
        case Enums::GameVersion::HGSS:
        case Enums::GameVersion::BW:
        case Enums::GameVersion::B2W2:
            return GEN45_RIBBONS;
        case Enums::GameVersion::XY:
        case Enums::GameVersion::ORAS:
            return GEN6_RIBBONS;
        case Enums::GameVersion::SM:
        case Enums::GameVersion::USUM:
        case Enums::GameVersion::GG:
            return GEN7_RIBBONS;
        case Enums::GameVersion::SWSH:
        case Enums::GameVersion::BDSP:
        case Enums::GameVersion::PLA:
        case Enums::GameVersion::SV:
        case Enums::GameVersion::ZA:
            return GEN89_RIBBONS;
        default:
            // Gen 1 and Gen 2 store no ribbons.
            return {};
        }
    }

    uint8_t getRibbonValue(const uint8_t *entityData, const RibbonDefinition &ribbon)
    {
        switch (ribbon.kind)
        {
        case RibbonKind::ContestRank:
        {
            const uint8_t contestRank = static_cast<uint8_t>((readRibbonWord(entityData, ribbon.byteOffset) >> ribbon.bit) & GEN3_CONTEST_RANK_MASK);
            // 5-7 are not ranks; the game treats them as never won
            return contestRank <= ribbon.maxCount ? contestRank : 0;
        }
        case RibbonKind::MemoryCount:
            return entityData[ribbon.countByteOffset];
        case RibbonKind::Flag:
        default:
            return static_cast<uint8_t>((entityData[ribbon.byteOffset] >> ribbon.bit) & 1u);
        }
    }

    void setRibbonValue(uint8_t *entityData, const RibbonDefinition &ribbon, uint8_t ribbonValue)
    {
        const uint8_t clampedValue = ribbonValue > ribbon.maxCount ? ribbon.maxCount : ribbonValue;
        switch (ribbon.kind)
        {
        case RibbonKind::ContestRank:
        {
            const uint32_t rankMask = GEN3_CONTEST_RANK_MASK << ribbon.bit;
            writeRibbonWord(entityData, ribbon.byteOffset, (readRibbonWord(entityData, ribbon.byteOffset) & ~rankMask) | (static_cast<uint32_t>(clampedValue) << ribbon.bit));
            break;
        }
        case RibbonKind::MemoryCount:
            entityData[ribbon.countByteOffset] = clampedValue;
            setFlag(entityData, ribbon.byteOffset, ribbon.bit, clampedValue != 0);
            break;
        case RibbonKind::Flag:
        default:
            setFlag(entityData, ribbon.byteOffset, ribbon.bit, clampedValue != 0);
            break;
        }
    }

    std::string getRibbonName(const RibbonDefinition &ribbon, uint8_t ribbonValue)
    {
        if (ribbon.kind == RibbonKind::ContestRank && ribbonValue <= ribbon.maxCount)
        {
            return localize(std::string(ribbon.englishName) + GEN3_CONTEST_RANK_NAMES[ribbonValue]);
        }
        return localize(ribbon.englishName);
    }

    std::string getRibbonSpriteStem(const RibbonDefinition &ribbon, uint8_t ribbonValue, Enums::GameVersion group)
    {
        if (ribbon.kind == RibbonKind::ContestRank && ribbonValue <= ribbon.maxCount)
        {
            return std::string(ribbon.spriteStem) + GEN3_CONTEST_RANK_SPRITE_SUFFIXES[ribbonValue];
        }
        if (ribbon.kind == RibbonKind::MemoryCount)
        {
            const Enums::GameVersion gameGroup = Enums::getGameGroup(group) != Enums::GameVersion::Invalid ? Enums::getGameGroup(group) : group;
            const bool isGeneration9 = Enums::getVersionGeneration(Enums::getGroupRepVersion(gameGroup)) >= 9;
            const bool isBattleMemory = std::string_view(ribbon.spriteStem) == "ribboncountmemorybattle";
            const uint8_t goldCount = (isGeneration9 && isBattleMemory) ? ribbon.maxCount - 1 : ribbon.maxCount;
            if (ribbonValue >= goldCount)
            {
                return std::string(ribbon.spriteStem) + "2";
            }
        }
        return ribbon.spriteStem;
    }

    bool isRibbonMark(const RibbonDefinition &ribbon)
    {
        const std::string_view englishName = ribbon.englishName;
        constexpr std::string_view markSuffix = " Mark";
        return englishName.size() > markSuffix.size() && englishName.substr(englishName.size() - markSuffix.size()) == markSuffix;
    }

    int getRibbonAffixIndex(const RibbonDefinition &ribbon)
    {
        // The HOME table is in RibbonIndex order (the generator checks it against PKHeX's enum), so
        // a row's position IS its RibbonIndex.
        const RibbonDefinition *homeRows = GEN89_RIBBONS;
        const size_t homeRowCount = sizeof(GEN89_RIBBONS) / sizeof(GEN89_RIBBONS[0]);
        if (&ribbon < homeRows || &ribbon >= homeRows + homeRowCount) return -1;
        return static_cast<int>(&ribbon - homeRows);
    }

    std::vector<std::string> getMonRibbons(const uint8_t *entityData, Enums::GameVersion group)
    {
        if (entityData == nullptr) return {};
        std::vector<std::string> ribbonNames;
        for (const RibbonDefinition &ribbon : getRibbonDefinitions(group))
        {
            const uint8_t ribbonValue = getRibbonValue(entityData, ribbon);
            if (ribbonValue != 0)
            {
                ribbonNames.push_back(getRibbonName(ribbon, ribbonValue));
            }
        }
        return ribbonNames;
    }
}
