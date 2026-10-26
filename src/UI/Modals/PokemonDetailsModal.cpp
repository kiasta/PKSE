#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "UI/Modals/PokemonDetailsModal.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "UI/SpriteManager.h"
#include "Trainer/Trainer.h"
#include "Trainer/OriginStamp.h" // originStampLabel -- the Origin row
#include "Utils/HelperUtilities.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/PokemonTypes.h"
#include "Pokemon/Experience.h"
#include "Pokemon/Evolution.h" // getTradeEvolutionOffer -- the Trade Evolve row
#include "Pokemon/PersonalRecord.h"
#include "Names/FormNames.h"
#include "Names/TypeNames.h"
#include "Pokemon/PersonalInfoTable.h" // getPersonalInfo -> formCount (Form row/picker)
#include "Names/MoveNames.h"
#include "Names/LocationNames.h"
#include "Names/RibbonNames.h"
#include "Names/ItemNames.h"
#include "Enums/Ball.h"
#include "Enums/LanguageID.h"
#include "Enums/GameVersion.h"
#include "Legality/Legality.h"

using namespace Trainer;
using namespace Utils;

namespace UI
{
    namespace Modals
    {

        // HOME "Check Summary"-style editor page. Full-screen, three columns: the render + details
        // (left), the editable stat table + shiny/nature/gender (center — the navigable "Values" column),
        // and the moveset editor (right — moves + held item). Editing is handled in TrainerViewScreen; this
        // only draws + captures touch.
        void drawPokemonDetailsModal(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            const Pokemon::Pokemon *p = screen.detailsTargetPokemon();
            if (!p || p->speciesID() == 0)
                return;

            const int screenWidth = framebuffer.getWidth(), H = framebuffer.getHeight();
            const bool isShiny = p->isShiny(p->id32(), p->species());
            const bool usesAwakeningValues = p->hasAwakeningValues();
            const int selectedIndex = screen.details.selectedField; // 0-5 stats, 6 shiny, 7 nature, 8 gender, 9 level, 10-13 moves, 14 item
            const Legality::Report legalityRep = Legality::analyze(*p, p->getGameGroup());

            screen.touchButtons.clear();

            // Full-screen page over a dimmed box screen.
            framebuffer.drawFilledRect(0, 0, screenWidth, H, Color(0, 0, 0, 130));
            framebuffer.drawVerticalGradient(0, 0, screenWidth, H, Color(Colors::Background.red, Colors::Background.green, Colors::Background.blue, 250), Color(Colors::Background.red, Colors::Background.green, Colors::Background.blue, 255));

            // AN EGG HAS NO NAME, and what a Gen 3 egg stores instead is the Japanese word for egg --
            // stamped on every egg whatever the cartridge, so reading it out loud tells the player
            // their English save is Japanese. The games show "EGG"; the species is on its own row.
            std::string name = p->eggTextIsPlaceholder() ? std::string("Egg") : utf16ToUtf8(p->nickname());
            if (name.empty())
                name = std::string(p->species());
            // Regional/variant label ABOVE the name ("Combat Breed", "Alolan", "Hisuian", ...), so a
            // variant reads as what it is even when the pokemon is nicknamed. Only drawn when the form has a
            // name; the name drops a row to make space for it.
            const char *variant = Names::getFormName(p->speciesID(), p->form());
            int nameY = 16;
            if (variant[0] != '\0')
            {
                framebuffer.drawText(28, 3, variant, Colors::Accent, TextStyle::Caption);
                nameY = 25;
            }
            framebuffer.drawText(28, nameY, name, Colors::Text, TextStyle::Heading);
            int nameWidth, nH;
            framebuffer.measureText(name, nameWidth, nH, TextStyle::Heading);
            int markX = 28 + nameWidth + 12;
            const char *g = p->genderSymbol();
            if (g[0] != '\0')
            {
                framebuffer.drawSymbol(markX, nameY + 6, g, (std::string(g) == "\xE2\x99\x82") ? Colors::Blue : Colors::Magenta);
                markX += 24;
            }
            if (isShiny)
                framebuffer.drawShinyMark(markX, nameY + 4, 18, Colors::ShinyStar);

            const std::string dexNumberText = dexNumberLabel(p->speciesID());
            // Box mons carry no party-stat block, so level() reads 0 for them in the packed formats
            // (SwSh/BDSP/SV/Z-A); fall back to the EXP-derived level (authoritative) for display.
            uint8_t subLvl = p->level();
            if (subLvl == 0)
                subLvl = Pokemon::getLevelFromExp(p->exp(), Pokemon::getGrowthRate(p->speciesID()));
            std::string subtitleText = "Lv. " + std::to_string(subLvl) + "     No. " + dexNumberText;
            int subtitleWidth, sH;
            framebuffer.measureText(subtitleText, subtitleWidth, sH);
            framebuffer.drawText(screenWidth - 90 - subtitleWidth, 24, subtitleText, Colors::TextDim);
            framebuffer.drawFilledRect(0, 60, screenWidth, 2, Colors::Accent);

            // Close button (top-right).
            framebuffer.drawFilledCircle(screenWidth - 40, 30, 20, Colors::PanelAlt);
            framebuffer.drawCircle(screenWidth - 40, 30, 20, Colors::Border, 1);
            framebuffer.drawText(screenWidth - 47, 18, "\xC3\x97", Colors::Text, TextStyle::Heading); // ×
            screen.touchButtons.push_back({99, screenWidth - 64, 6, 52, 52});

            // "Unsaved changes" marker (edits are applied live; X commits them and clears this). Centered
            // at the top so it crowds neither the name nor the Lv/No. A being-CREATED pokemon is unsaved by
            // nature -- every field is -- so the marker is just noise there and is hidden.
            if (screen.pokemonEditDirty() && !screen.creator.editing)
            {
                const char *u = "Unsaved changes";
                int unitWidth, uh;
                framebuffer.measureText(u, unitWidth, uh, TextStyle::Caption);
                framebuffer.drawText((screenWidth - unitWidth) / 2, 8, u, Colors::Warning, TextStyle::Caption);
            }

            const int colY = 72, colH = H - colY - NAV_BAR_HEIGHT - 6; // leave room for the bottom nav bar

            const int leftColumnX = 24, Lw = 430;
            framebuffer.drawFilledRoundedRect(leftColumnX, colY, Lw, colH, 16, Colors::Panel);
            framebuffer.drawRoundedRect(leftColumnX, colY, Lw, colH, 16, Colors::Border, 1);

            const int renderSz = 150;
            Sprite *sprite = SpriteManager::getSprite(p->speciesID(), p->form(), isShiny);
            if (sprite && sprite->data)
            {
                framebuffer.drawSpriteIdle(leftColumnX + (Lw - renderSz) / 2, colY + 14, renderSz, renderSz, sprite->width, sprite->height, sprite->data, sprite->channels, 0.0f);
            }
            // A Tera badge takes its own line, so the scroll region below has to start lower.
            const int teraBadgeHeight = p->hasTeraType() ? 26 : 0;
            // Type badges under the render (icons, centered).
            {
                Pokemon::TypePair types = Pokemon::getPokemonTypes(p->speciesID(), p->form(), p->getGameGroup());
                Sprite *firstTypeIcon = SpriteManager::getTypeSprite(types.type1);
                Sprite *secondTypeIcon = Pokemon::hasSecondType(types) ? SpriteManager::getTypeSprite(types.type2) : nullptr;
                const int textHeight = 22;
                int firstTypeWidth = (firstTypeIcon && firstTypeIcon->data) ? (firstTypeIcon->width * textHeight) / firstTypeIcon->height : 0;
                int secondTypeWidth = (secondTypeIcon && secondTypeIcon->data) ? (secondTypeIcon->width * textHeight) / secondTypeIcon->height : 0;
                int typeIconGap = (secondTypeWidth > 0) ? 8 : 0;
                int ribbonX = leftColumnX + (Lw - (firstTypeWidth + typeIconGap + secondTypeWidth)) / 2, tyy = colY + 14 + renderSz + 6;
                if (firstTypeIcon && firstTypeIcon->data)
                {
                    framebuffer.drawImageScaled(ribbonX, tyy, firstTypeIcon->width, firstTypeIcon->height, firstTypeWidth, textHeight, firstTypeIcon->data, firstTypeIcon->channels);
                    ribbonX += firstTypeWidth + typeIconGap;
                }
                if (secondTypeIcon && secondTypeIcon->data)
                {
                    framebuffer.drawImageScaled(ribbonX, tyy, secondTypeIcon->width, secondTypeIcon->height, secondTypeWidth, textHeight, secondTypeIcon->data, secondTypeIcon->channels);
                }
            }
            // TERA BADGE, ON ITS OWN LINE AND LABELLED. Deliberately not beside the type badges: a
            // Tera type is not a third type, and the common case is a Pokemon whose Tera type MATCHES
            // one of its types, which sitting in the same row would render as a duplicate badge.
            // Gated on the capability rather than the value -- Tera type 0 is Normal, a real answer,
            // so a format without the field would otherwise draw a Normal badge it never had.
            if (teraBadgeHeight > 0)
            {
                const int teraIconHeight = 22;
                Sprite *teraIcon = SpriteManager::getTypeSprite(Pokemon::teraTypeNameIndex(p->teraType()));
                int teraLabelWidth = 0, teraLabelHeight = 0;
                framebuffer.measureText("Tera", teraLabelWidth, teraLabelHeight, TextStyle::Caption);
                const int teraIconWidth = (teraIcon && teraIcon->data) ? (teraIcon->width * teraIconHeight) / teraIcon->height : 0;
                const int teraGap = (teraIconWidth > 0) ? 8 : 0;
                int teraX = leftColumnX + (Lw - (teraLabelWidth + teraGap + teraIconWidth)) / 2;
                const int teraY = colY + 14 + renderSz + 6 + 22 + 4;
                framebuffer.drawText(teraX, teraY + (teraIconHeight - teraLabelHeight) / 2, "Tera", Colors::TextDim, TextStyle::Caption);
                teraX += teraLabelWidth + teraGap;
                if (teraIcon && teraIcon->data)
                {
                    framebuffer.drawImageScaled(teraX, teraY, teraIcon->width, teraIcon->height, teraIconWidth, teraIconHeight, teraIcon->data, teraIcon->channels);
                }
            }

            // clips + scrolls and auto-follows the selected field so it never runs off screen. -----
            char buffer[160];
            const int editRowHeight = 40; // row pitch -- big, touch-friendly targets
            const int contentTop = colY + 14 + renderSz + 40 + teraBadgeHeight;
            const int legalityH = 34; // legality line is pinned below the scroll
            const int contentBottom = colY + colH - legalityH - 6;
            const int scroll = screen.details.leftScroll;
            int infoRowY = contentTop;
            int selRowY = -1;           // absolute content-Y of the selected row
            std::vector<int> leftOrder; // editable field ids in draw order (nav list)
            auto rowVisible = [&](int rectY)
            { return (rectY + editRowHeight) > contentTop && rectY < contentBottom; };

            framebuffer.setClipRect(leftColumnX + 1, contentTop, Lw - 2, contentBottom - contentTop);
            auto row = [&](const char *label, const std::string &value)
            {
                const int rectY = infoRowY - scroll;
                if (rowVisible(rectY))
                {
                    framebuffer.drawText(leftColumnX + 18, rectY + 11, label, Colors::TextDim, TextStyle::Body);
                    int valueWidth, vh;
                    framebuffer.measureText(value, valueWidth, vh, TextStyle::Body);
                    framebuffer.drawText(leftColumnX + Lw - 18 - valueWidth, rectY + 11, value, Colors::Text, TextStyle::Body);
                }
                infoRowY += editRowHeight;
            };
            // Editable row: selection highlight + touch target, registered only while visible.
            auto editRow = [&](const char *label, const std::string &value, int fieldIdx)
            {
                leftOrder.push_back(fieldIdx); // nav list: every editable row, in draw order (even off-screen)
                const int rectY = infoRowY - scroll;
                const bool isFieldSelected = (selectedIndex == fieldIdx);
                if (isFieldSelected)
                    selRowY = infoRowY;
                if (rowVisible(rectY))
                {
                    if (isFieldSelected)
                    {
                        framebuffer.drawFilledRoundedRect(leftColumnX + 8, rectY + 2, Lw - 16, editRowHeight - 6, 8, Colors::Selected);
                        framebuffer.drawRoundedRect(leftColumnX + 8, rectY + 2, Lw - 16, editRowHeight - 6, 8, Colors::Accent, 2);
                    }
                    framebuffer.drawText(leftColumnX + 18, rectY + 11, label, isFieldSelected ? Colors::Text : Colors::TextDim, TextStyle::Body);
                    int valueWidth, vh;
                    framebuffer.measureText(value, valueWidth, vh, TextStyle::Body);
                    framebuffer.drawText(leftColumnX + Lw - 18 - valueWidth, rectY + 11, value, isFieldSelected ? Colors::Accent : Colors::Text, TextStyle::Body);
                    screen.touchButtons.push_back({fieldIdx, leftColumnX + 8, rectY, Lw - 16, editRowHeight});
                }
                infoRowY += editRowHeight;
            };
            // The one ACTION in this column, drawn as a filled pill with an up arrow instead of a
            // label-and-value row. It still joins `leftOrder` and still registers a touch target, so
            // it navigates exactly like the rows below it -- only the paint differs. It has no
            // disabled form: it is drawn only where there is a trade evolution, and every trade
            // evolution can be performed.
            auto evolveButtonRow = [&](const char *label, const std::string &value, int fieldIdx)
            {
                leftOrder.push_back(fieldIdx);
                const int rectY = infoRowY - scroll;
                const bool isFieldSelected = (selectedIndex == fieldIdx);
                if (isFieldSelected)
                    selRowY = infoRowY;
                if (rowVisible(rectY))
                {
                    const int buttonX = leftColumnX + 8;
                    const int buttonWidth = Lw - 16;
                    const int buttonHeight = editRowHeight - 6;
                    framebuffer.drawFilledRoundedRect(buttonX, rectY + 2, buttonWidth, buttonHeight, 10, Colors::Evolve);
                    // The selection ring is the page's own, so a selected button reads the same way
                    // a selected row does.
                    if (isFieldSelected)
                        framebuffer.drawRoundedRect(buttonX, rectY + 2, buttonWidth, buttonHeight, 10, Colors::Accent, 2);

                    const Color faceColor = Colors::EvolveText;
                    const int iconSize = 20;
                    framebuffer.drawEvolveArrow(buttonX + 14, rectY + 2 + (buttonHeight - iconSize) / 2, iconSize, faceColor);
                    framebuffer.drawText(buttonX + 14 + iconSize + 10, rectY + 11, label, faceColor, TextStyle::Body);
                    int valueWidth, valueHeight;
                    framebuffer.measureText(value, valueWidth, valueHeight, TextStyle::Body);
                    framebuffer.drawText(buttonX + buttonWidth - 14 - valueWidth, rectY + 11, value, faceColor, TextStyle::Body);
                    screen.touchButtons.push_back({fieldIdx, buttonX, rectY, buttonWidth, editRowHeight});
                }
                infoRowY += editRowHeight;
            };
            // FireRed/LeafGreen (Gen 3) wires none of exp / form / fateful, so those rows are editable
            // everywhere EXCEPT there (notGen3). Stat Nature (mints) is Gen 8+ -> the tighter modernFmt
            // gate.
            //
            // THE MET-DATE AND EGG ROWS ASK THE ENTITY INSTEAD, because modernFmt was the wrong
            // question for them: it names Gen 3 and Let's Go specifically, so Gens 1 and 2 fell through
            // it and drew an "Egg Loc" and an "Egg Date" for formats with no such field, and a Crystal
            // record drew a "Met Date" it cannot store either. Every one read "(none)", which is the
            // same thing the row says when the field exists and is unset -- exactly the ambiguity the
            // capability predicates exist to remove.
            const bool notGen3 = !Enums::isGen3Group(p->getGameGroup());
            const bool modernFmt = (notGen3 && p->getGameGroup() != Enums::GameVersion::GG);
            // TRADE EVOLVE LEADS THE COLUMN, and is drawn as a BUTTON rather than a row. Everything
            // below it is a value you edit; this one changes what the Pokemon IS, and while it
            // looked like its neighbours it read as another line of data rather than something to
            // press. Shown for every pokemon whose species+form trade-evolves in ITS OWN game -- a
            // bank slot holds a foreign pokemon.
            //
            // The value NAMES WHAT IT BECOMES, which is the whole of the question for every species
            // but Clamperl: its two rows lead to different Pokemon, so holding neither item both
            // are named and the press asks which. A held item is never a condition on pressing --
            // see TradeEvolutionOffer.
            //
            // Legends: Arceus has no trading at all; the same four evolutions happen there through
            // the Linking Cord, a BAG item the player uses rather than one the pokemon holds. So
            // the label says "Evolve" there.
            {
                const Pokemon::TradeEvolutionOffer tradeOffer = Pokemon::getTradeEvolutionOffer(*p);
                if (tradeOffer.hasCandidate())
                {
                    const bool tradesInThisGame = (p->getGameGroup() != Enums::GameVersion::PLA);
                    std::string tradeValue;
                    if (!screen.tradePartnerPick.refusalText.empty())
                    {
                        // A refused or failed trade-partner pick reports itself HERE, on the row
                        // that started it -- a pick that silently does nothing is indistinguishable
                        // from one that never registered. Tested FIRST because the button is always
                        // naming a destination otherwise, and that would bury the refusal.
                        tradeValue = screen.tradePartnerPick.refusalText;
                    }
                    else
                    {
                        const int resolvedIndex = tradeOffer.resolvedIndex();
                        for (int candidateIndex = 0; candidateIndex < tradeOffer.candidateCount; ++candidateIndex)
                        {
                            if (resolvedIndex >= 0 && candidateIndex != resolvedIndex)
                                continue;
                            const Pokemon::TradeEvolution &becomes = tradeOffer.candidates[candidateIndex];
                            if (!tradeValue.empty())
                                tradeValue += " or ";
                            tradeValue += Names::getDisplayName(becomes.destinationSpeciesId, becomes.destinationFormId, Trainer::getSpeciesName(becomes.destinationSpeciesId));
                        }
                    }
                    evolveButtonRow(tradesInThisGame ? "Trade Evolve" : "Evolve", tradeValue, 33);
                }
            }
            // Species heads the fields -- it is the pokemon's identity, and everything under it is read
            // through it (base stats, forms, abilities, the learnset). Editing it re-points the whole
            // record: see applySpeciesChange, which reconciles form, gender, ability and EXP.
            editRow("Species", Trainer::getSpeciesName(p->speciesID()), 32);
            // Nickname next -- it's the name shown in-game. Gen 3 is NOT excluded: its field is
            // shorter (10) and its character set narrower, but it is as editable as any other format's.
            // An egg is not nameable -- not in the games, and not here: its nickname field holds the
            // placeholder the hatch replaces, and writing a real name into it makes the record one no
            // save can hold. So the row reports and does not offer an edit.
            if (p->eggTextIsPlaceholder())
                row("Nickname", "Egg");
            else
                editRow("Nickname", utf16ToUtf8(p->nickname()), 23);
            // The hasXXX() gates below are not cosmetic. Every one of these fields reads 0 in a format
            // that lacks it, and 0 is a REAL value for all of them -- ability 0, nature 0 (Hardy),
            // friendship 0, ball 0 -- so an ungated row does not look empty, it looks like data. Gen 1
            // has none of them, and "Ability: none / Nature: Hardy" on a Red/Blue Bulbasaur is invented
            // information sitting in the same column as the real fields.
            if (p->hasAbility())
                editRow("Ability", getAbilityName(p->ability()), 15);
            // Read-only: changing a Tera type is a Tera Shard mechanic with its own rules (Ogerpon
            // and Terapagos cannot change theirs at all), so it is reported rather than edited.
            if (p->hasTeraType())
                row("Tera Type", Names::getTypeName(Pokemon::teraTypeNameIndex(p->teraType())));
            if (p->hasFriendship())
                editRow("Friendship", std::to_string(p->friendship()), 16);
            // Form -- when the species has alternate forms AND the format can set them (Gen 3 forms are
            // PID-derived, so setForm is a no-op there -> exclude only Gen 3).
            if (notGen3 && Pokemon::getPersonalInfo(p->speciesID(), 0).formCount > 1)
            {
                const char *fn = Names::getFormName(p->speciesID(), p->form());
                editRow("Form", (fn[0] != '\0') ? std::string(fn) : std::string("Base"), 26);
            }
            // Stat Nature (the mint / effective-stat nature) -- modern formats only.
            if (modernFmt)
                editRow("Stat Nature", getNatureName(p->statNature()), 27);
            if (p->hasPID())
            {
                snprintf(buffer, sizeof(buffer), "%08X", p->pid());
                row("PID", buffer);
            }
            snprintf(buffer, sizeof(buffer), "%u", p->exp());
            if (notGen3)
                editRow("EXP", buffer, 24);
            else
                row("EXP", buffer);
            // Gen 1 has no egg flag to edit
            if (p->hasMetData()) editRow("Egg", p->isEgg() ? "Yes" : "No", 17);

            // Shown whether or not it is set, for the same reason the HT row below is: an empty OT
            // is a real, legality-relevant state (a created or hacked pokemon that nobody caught), and
            // hiding the row turned that into a gap the reader had to notice was missing. Every
            // format PKSE handles records an OT, so there is no "this game has no such field" case
            // to distinguish here -- unlike the handler.
            std::string originalTrainerName = utf16ToUtf8(p->otName());
            if (originalTrainerName.empty())
                originalTrainerName = "(none)";
            else
                originalTrainerName += (p->otGender() == 0) ? " (M)" : " (F)";
            row("OT", originalTrainerName);
            // Trainer ID in the format the game shows (six digits for Gen 7+ origins, 16-bit TID otherwise;
            // a pokemon with no origin falls back to its own format -- only Gen 3 is 16-bit among these games).
            {
                const uint8_t originVersion = p->originGame();
                const bool sixDigit = (originVersion != 0) ? Enums::usesSixDigitTrainerID(originVersion) : !Enums::isGen3Group(p->getGameGroup());
                if (sixDigit)
                    snprintf(buffer, sizeof(buffer), "%06u", p->id32() % 1000000u);
                else
                    snprintf(buffer, sizeof(buffer), "%05u", p->id32() & 0xFFFFu);
                row("OT ID", buffer);
            }
            // Handling Trainer -- shown for every format that HAS one, whether or not it is set.
            //
            // Hiding it when the name is empty makes two different facts look identical: "this Pokemon has
            // never been traded" and "this game has no handler". A Sword/Shield pokemon moved between saves
            // has an empty HT, so the row would vanish with no way to tell that the field even existed.
            // hasHandler() answers the format question, so an unset handler can say so.
            //
            // The label carries WHICH trainer is currently in charge, because that is what the field means:
            // the game hands friendship, affection and memories to whichever of OT/HT is active.
            //
            // READ-ONLY. The handler is written by the Trade Evolve action, which is where the choice between
            // a real save and a generated trainer belongs -- a trade is something you DO to a Pokemon, not a
            // field you type into.
            if (p->hasHandler())
            {
                std::string handlingTrainerName = utf16ToUtf8(p->htName());
                if (handlingTrainerName.empty())
                    handlingTrainerName = "(none)";
                else
                    handlingTrainerName += (p->htGender() == 0) ? " (M)" : " (F)";
                row(p->currentHandler() == 1 ? "HT (active)" : "HT", handlingTrainerName);
            }
            if (p->hasMetData())
            {
                snprintf(buffer, sizeof(buffer), "Lv. %u", p->metLevel());
                editRow("Met Lv", buffer, 18);
            }
            // Origin-generation location routing: a Gen 3/4 pokemon's MET id is remapped into the current
            // format's numbering when it is transferred up (Gen 5+ keep their own table), so a Gen 3/4 met
            // must be named with the format's table -- else a Platinum starter link-traded to SV reads "(none)".
            const uint8_t fmtVer = Enums::getGroupRepVersion(p->getGameGroup());
            if (p->hasMetData())
            {
                const char *loc = Names::getMetLocationName(Enums::locationTableVersion(p->originGame(), fmtVer, false), p->metLocation());
                editRow("Met", (loc[0] != '\0') ? std::string(loc) : std::string("(none)"), 25);
            }
            // Met date -- Gens 1, 2 and 3 record none. Year byte is +2000.
            if (p->hasMetDate() && p->hasMetData())
            {
                // A transferred pokemon can carry no met date (00/00) -- show "(none)" instead of "00/00/2000",
                // matching the egg-date row and how PKHeX blanks an unset date.
                if (p->metMonth() == 0 || p->metDay() == 0)
                    snprintf(buffer, sizeof(buffer), "(none)");
                else
                    snprintf(buffer, sizeof(buffer), "%02u/%02u/%04u", p->metDay(), p->metMonth(), 2000 + p->metYear());
                editRow("Met Date", buffer, 28);
            }
            // Egg-met conditions -- the formats that have the field at all.
            if (p->hasEggData())
            {
                // "From an egg" means a real egg location. BDSP's "none" sentinel is 65535 (Gen 4 numbering,
                // where 0 is a real place), not 0 -- treat it as no-egg too, so the display matches the game.
                const bool fromEgg = p->eggLocation() != 0 && !(p->getGameGroup() == Enums::GameVersion::BDSP && p->eggLocation() == 0xFFFF);
                {
                    const char *eggLocationName = Names::getMetLocationName(Enums::locationTableVersion(p->originGame(), fmtVer, true), p->eggLocation());
                    editRow("Egg Loc", (fromEgg && eggLocationName[0] != '\0') ? std::string(eggLocationName) : std::string("(none)"), 29);
                }
                if (fromEgg)
                    snprintf(buffer, sizeof(buffer), "%02u/%02u/%04u", p->eggDay(), p->eggMonth(), 2000 + p->eggYear());
                else
                    snprintf(buffer, sizeof(buffer), "(none)");
                editRow("Egg Date", buffer, 30);
            }
            if (p->hasBall())
                editRow("Ball", Enums::getBallName(p->ball()), 19);
            // The same placeholder: an egg's language byte says Japanese on every cartridge, and the
            // hatch overwrites it with the game's own (the hatched eggs in a real English FireRed save
            // read English). Reporting "Japanese" here was the visible half of that.
            if (p->eggTextIsPlaceholder())
                row("Language", "(set when it hatches)");
            else
                editRow("Language", Enums::getLanguageName(p->language()), 20);
            {
                // One implementation, three views -- see UI::originStampLabel for why the fallback
                // is the interesting half. A bank record passes no save: the bank's tag records a
                // locale, not a title, so it must not borrow the open save's name.
                const Trainer::Trainer *openSave = (screen.details.source == TrainerViewScreen::EditSource::Bank) ? nullptr : &screen.trainer;
                if (p->hasOriginGame())
                {
                    editRow("Origin", Trainer::originStampLabel(*p, openSave), 21);
                }
                else
                {
                    // Gen 1/2: no version byte, so THE MARK IS THE ANSWER and there is no name to
                    // print beside it -- any title PKSE could show is inferred from the save the
                    // pokemon happens to be in. This is what Bank, HOME and PKSM do too.
                    const int markRowY = infoRowY - scroll;
                    if (rowVisible(markRowY))
                    {
                        framebuffer.drawText(leftColumnX + 18, markRowY + 11, "Origin", Colors::TextDim, TextStyle::Body);
                        Sprite *markSprite = SpriteManager::getOriginMarkSprite(Trainer::originMarkFor(*p), Colors::Text);
                        if (markSprite != nullptr && markSprite->data != nullptr)
                        {
                            const int markSize = 20;
                            framebuffer.drawImageScaled(leftColumnX + Lw - 18 - markSize, markRowY + 8, markSprite->width, markSprite->height, markSize, markSize, markSprite->data, markSprite->channels);
                        }
                    }
                    infoRowY += editRowHeight;
                }
            }
            if (notGen3)
                editRow("Fateful", p->isFatefulEncounter() ? "Yes" : "No", 31);
            if (p->hasAlphaFlag())
                editRow("Alpha", p->isAlpha() ? "Yes" : "No", 34);
            {
                // Pokerus: editable (None -> Infected -> Cured) where the game has it; read-only otherwise.
                const char *pkrs = p->isPokerusInfected() ? "Infected" : p->isPokerusCured() ? "Cured" : "None";
                if (p->hasPokerus())
                    editRow("Pokerus", pkrs, 22);
                else
                    row("Pokerus", pkrs);
            }
            // Ribbons: count only (there can be dozens); Y / tap opens the full list.
            {
                auto ribbonList = Names::getMonRibbons(reinterpret_cast<const uint8_t *>(p->getData().data()), p->getGameGroup());
                if (!ribbonList.empty())
                {
                    const int rectY = infoRowY - scroll;
                    row("Ribbons", std::to_string(ribbonList.size()) + "  (Y)"); // advances iy
                    if (rowVisible(rectY))
                        screen.touchButtons.push_back({94, leftColumnX + 8, rectY, Lw - 16, editRowHeight});
                }
            }
            framebuffer.clearClip();
            screen.details.leftOrder = leftOrder; // hand the nav its draw-order field list

            // Auto-scroll so the selected field stays visible (applied next frame), plus a faint scrollbar.
            {
                const int contentH = infoRowY - contentTop;
                const int viewH = contentBottom - contentTop;
                int scrollOffset = scroll;
                if (selRowY >= 0)
                {
                    if (selRowY - scrollOffset < contentTop)
                        scrollOffset = selRowY - contentTop;
                    else if ((selRowY + editRowHeight) - scrollOffset > contentBottom)
                        scrollOffset = (selRowY + editRowHeight) - contentBottom;
                }
                const int maxS = (contentH > viewH) ? (contentH - viewH) : 0;
                if (scrollOffset < 0)
                    scrollOffset = 0;
                if (scrollOffset > maxS)
                    scrollOffset = maxS;
                screen.details.leftScroll = scrollOffset;
                drawScrollbar(framebuffer, leftColumnX + Lw - 7, contentTop, viewH, contentH, scrollOffset);
            }

            // Legality summary pinned at the bottom of the left pane (R / tap opens the full issue list).
            // A clean pokemon is still worth opening: Layer 3 records which encounter it matched, or why it
            // could not be checked, as notes -- so the row stays tappable whenever there is anything to read.
            {
                const int legalityRowY = colY + colH - legalityH + 6;
                const bool clean = legalityRep.ok();
                std::string label = clean ? "Legality: no problems found" : "Legality: " + std::to_string(legalityRep.problemCount()) + " issue(s)";
                if (!legalityRep.empty())
                    label += "  -  R / tap to view";
                framebuffer.drawText(leftColumnX + 18, legalityRowY, label, clean ? Colors::LegalityClean : Colors::LegalityProblem, TextStyle::Caption);
                if (!legalityRep.empty())
                {
                    int labelWidth, lh;
                    framebuffer.measureText(label, labelWidth, lh, TextStyle::Caption);
                    // id 95: open legality overlay
                    screen.touchButtons.push_back({95, leftColumnX + 14, legalityRowY - 4, labelWidth + 8, lh + 8});
                }
            }

            const int centreColumnX = 474, Cw = 340;
            framebuffer.drawFilledRoundedRect(centreColumnX, colY, Cw, colH, 16, Colors::Panel);
            framebuffer.drawRoundedRect(centreColumnX, colY, Cw, colH, 16, Colors::Border, 1);
            framebuffer.drawText(centreColumnX + 18, colY + 16, "Values", Colors::Text, TextStyle::Heading);

            const int nameX = centreColumnX + 18, cIV = centreColumnX + 120, cEV = centreColumnX + 200, cStat = centreColumnX + 280;
            int statRowY = colY + 62;
            // Gen 1/2 store DVs and Stat Experience, not IVs and EVs. Different ranges, different
            // maths, different names -- so the headers ask the format rather than assuming.
            const bool gbStats = (p->maxEV() > 255);
            framebuffer.drawText(cIV, statRowY, (p->maxIV() < 31) ? "DV" : "IV", Colors::TextDim, TextStyle::Caption);
            framebuffer.drawText(cEV, statRowY, usesAwakeningValues ? "AV" : (gbStats ? "Exp" : "EV"), Colors::TextDim, TextStyle::Caption);
            framebuffer.drawText(cStat, statRowY, "Stat", Colors::TextDim, TextStyle::Caption);
            statRowY += 24;

            // Party stats (the 0x14A+ block) read 0 for box mons in the packed formats, so compute the
            // battle stat from base + IV + EV + EXP-level + (stat)nature when the stored value is 0. Party
            // mons keep their exact stored stats; LGPE (bstat == null -> its AV formula) is left as-is.
            // Gen 1 joins Let's Go in opting out of the fallback: its records ALWAYS carry real battle
            // stats, and its formula is a different one anyway (single Special, a square-root Stat Exp
            // term, no nature). Computing a modern stat for a Red/Blue Pokemon would be wrong in three
            // ways at once, so the stored value is the only answer used.
            const bool statIsGG = (p->getGameGroup() == Enums::GameVersion::GG) || (p->getGameGroup() == Enums::GameVersion::RBY);
            // THE MON'S OWN GROUP, not a modern table. Computing a fallback stat out of the Gen 8/9 base
            // stats shows a Gen 2 through Gen 7 Pokemon with an unfilled stat tail numbers derived from a
            // table its game never had.
            const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(p->getGameGroup(), p->speciesID(), p->form());
            const Pokemon::PersonalRecord *bstat = (statIsGG || record.hp == 0) ? nullptr : &record;
            const uint8_t dispLevel = (p->level() != 0) ? p->level() : Pokemon::getLevelFromExp(p->exp(), Pokemon::getGrowthRate(p->speciesID()));
            auto dispStat = [&](int statIndex, uint16_t stored) -> int
            {
                // party pokemon / LGPE / no base data -> stored value
                if (stored != 0 || !bstat) return stored;
                const int base = (statIndex == 0) ? bstat->hp : (statIndex == 1) ? bstat->atk : (statIndex == 2) ? bstat->def : (statIndex == 3) ? bstat->spa : (statIndex == 4) ? bstat->spd : bstat->spe;
                // effectiveIV, not ivXXX: a Hyper Trained stat is played at a maximal IV while the
                // stored IV stays put, so computing this row from the raw value shows a box Pokemon a
                // stat lower than the one the game gives it. This row order (HP, Atk, Def, SpA, SpD,
                // Spe) happens to match the flag bit order exactly, which is why index passes straight
                // through -- it does NOT elsewhere, so do not copy this call shape without checking.
                int ivValue = 0, ev = 0;
                switch (statIndex)
                {
                case 0:
                    ivValue = p->effectiveIV(0);
                    ev = p->evHP();
                    break;
                case 1:
                    ivValue = p->effectiveIV(1);
                    ev = p->evATK();
                    break;
                case 2:
                    ivValue = p->effectiveIV(2);
                    ev = p->evDEF();
                    break;
                case 3:
                    ivValue = p->effectiveIV(3);
                    ev = p->evSPA();
                    break;
                case 4:
                    ivValue = p->effectiveIV(4);
                    ev = p->evSPD();
                    break;
                default:
                    ivValue = p->effectiveIV(5);
                    ev = p->evSPE();
                    break;
                }
                int value = ((2 * base + ivValue + ev / 4) * dispLevel) / 100;
                // HP uses a distinct formula
                if (statIndex == 0) return value + dispLevel + 10;
                value += 5;
                // Nature (mint-aware) modifier. natIdx maps a display row to its nature stat index
                // (nature order is Atk, Def, Spe, SpA, SpD); HP (index 0) is never nature-affected.
                static const int natIdx[6] = {-1, 0, 1, 3, 4, 2};
                const int raisedStat = p->statNature() / 5, down = p->statNature() % 5;
                if (raisedStat != down)
                {
                    if (natIdx[statIndex] == raisedStat)
                        value = value * 110 / 100;
                    else if (natIdx[statIndex] == down)
                        value = value * 90 / 100;
                }
                return value;
            };

            struct StatRow
            {
                const char *label;
                int ivValue, evOrAvValue, statValue;
            };
            const StatRow rows[6] = {
                {"HP", p->ivHP(), usesAwakeningValues ? p->avHP() : p->evHP(), dispStat(0, p->statHPMax())},
                {"Atk", p->ivATK(), usesAwakeningValues ? p->avATK() : p->evATK(), dispStat(1, p->statATK())},
                {"Def", p->ivDEF(), usesAwakeningValues ? p->avDEF() : p->evDEF(), dispStat(2, p->statDEF())},
                {"SpA", p->ivSPA(), usesAwakeningValues ? p->avSPA() : p->evSPA(), dispStat(3, p->statSPA())},
                {"SpD", p->ivSPD(), usesAwakeningValues ? p->avSPD() : p->evSPD(), dispStat(4, p->statSPD())},
                {"Spe", p->ivSPE(), usesAwakeningValues ? p->avSPE() : p->evSPE(), dispStat(5, p->statSPE())},
            };
            const int statRowH = 44;
            for (int index = 0; index < 6; ++index)
            {
                const bool scrollOffset = (selectedIndex == index);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2);
                }
                framebuffer.drawText(nameX, statRowY, rows[index].label, scrollOffset ? Colors::Text : Colors::TextDim);
                framebuffer.drawText(cIV, statRowY, std::to_string(rows[index].ivValue), Colors::Text);
                // The stored IV is what is shown -- it is what the editor writes and what legality
                // matches -- so a Hyper Trained stat would otherwise read as a low IV next to a high
                // Stat with nothing to explain the gap. "HT" is what PKHeX calls it.
                if (p->isHyperTrained(index))
                    framebuffer.drawText(cIV + 34, statRowY, "HT", Colors::Accent, TextStyle::Caption);
                framebuffer.drawText(cEV, statRowY, std::to_string(rows[index].evOrAvValue), Colors::Text);
                framebuffer.drawText(cStat, statRowY, std::to_string(rows[index].statValue), Colors::Accent);
                screen.touchButtons.push_back({index, centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6});
                statRowY += statRowH;
            }
            if (p->isHyperTrained())
            {
                framebuffer.drawText(nameX, statRowY - 8, "HT: Hyper Trained, plays as " + std::to_string(p->maxIV()), Colors::TextDim, TextStyle::Caption);
                statRowY += 14;
            }

            // Shiny toggle row (selectable index 6).
            statRowY += 10;
            {
                const bool scrollOffset = (selectedIndex == 6);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2);
                }
                framebuffer.drawText(nameX, statRowY, "Shiny", scrollOffset ? Colors::Text : Colors::TextDim);
                // Right-aligned to the panel edge, flush with Nature/Gender/Level below it.
                {
                    const char *sv = isShiny ? "Yes" : "No";
                    int statTextWidth, sh;
                    framebuffer.measureText(sv, statTextWidth, sh);
                    framebuffer.drawText(centreColumnX + Cw - 18 - statTextWidth, statRowY, sv, isShiny ? Colors::ShinyStar : Colors::Text);
                }
                screen.touchButtons.push_back({6, centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6});
                statRowY += statRowH;
            }

            // Nature row (selectable index 7) — cycled with Left/Right (or A). Hidden for formats with
            // no nature at all: nature 0 is Hardy, a real nature, so an ungated row would claim one.
            statRowY += 6;
            if (p->hasNature())
            {
                const bool scrollOffset = (selectedIndex == 7);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2);
                }
                framebuffer.drawText(nameX, statRowY, "Nature", scrollOffset ? Colors::Text : Colors::TextDim);
                std::string natureName = getNatureName(p->nature());
                int natureTextWidth, nh;
                framebuffer.measureText(natureName, natureTextWidth, nh);
                framebuffer.drawText(centreColumnX + Cw - 18 - natureTextWidth, statRowY, natureName, Colors::Accent);
                screen.touchButtons.push_back({7, centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6});
                statRowY += statRowH;
            }

            // Gender row (selectable index 8) — A opens the picker. READ-ONLY for a fixed-gender species
            // (male-only Braviary, female-only Miltank, genderless Magnemite): the value still shows, but
            // there is no highlight and no touch target, and the cursor steps over it — the same way the
            // left column handles a read-only row by not listing it. See TrainerViewScreen::genderEditable.
            statRowY += 6;
            {
                const bool editable = screen.genderEditable(*p);
                const bool scrollOffset = editable && (selectedIndex == 8);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2);
                }
                framebuffer.drawText(nameX, statRowY, "Gender", scrollOffset ? Colors::Text : Colors::TextDim);
                const uint8_t genderValue = p->gender();
                const char *gname = (genderValue == 0) ? "Male" : (genderValue == 1) ? "Female" : "Genderless";
                const Color genderColor = (genderValue == 0) ? Colors::Blue : (genderValue == 1) ? Colors::Magenta : Colors::Text;
                int genderTextWidth, gh;
                framebuffer.measureText(gname, genderTextWidth, gh);
                framebuffer.drawText(centreColumnX + Cw - 18 - genderTextWidth, statRowY, gname, genderColor);
                if (editable)
                    screen.touchButtons.push_back({8, centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6});
                statRowY += statRowH;
            }

            // Level row (selectable index 9) — A opens the 1-100 level picker (changing level recomputes stats).
            statRowY += 6;
            {
                const bool scrollOffset = (selectedIndex == 9);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2);
                }
                framebuffer.drawText(nameX, statRowY, "Level", scrollOffset ? Colors::Text : Colors::TextDim);
                const std::string lvlStr = std::to_string(dispLevel);
                int labelWidth, lh;
                framebuffer.measureText(lvlStr, labelWidth, lh);
                framebuffer.drawText(centreColumnX + Cw - 18 - labelWidth, statRowY, lvlStr, Colors::Accent);
                screen.touchButtons.push_back({9, centreColumnX + 8, statRowY - 6, Cw - 16, statRowH - 6});
                statRowY += statRowH;
            }

            const int rightColumnX = 838, Rw = screenWidth - rightColumnX - 24;
            framebuffer.drawFilledRoundedRect(rightColumnX, colY, Rw, colH, 16, Colors::Panel);
            framebuffer.drawRoundedRect(rightColumnX, colY, Rw, colH, 16, Colors::Border, 1);

            // This panel is exclusively the moveset — 4 move slots (rows 10-13) + held item (row 14).
            infoRowY = colY + 22;
            framebuffer.drawText(rightColumnX + 18, infoRowY, "Moves", Colors::Text, TextStyle::Heading);
            infoRowY += 46;
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
            {
                const uint16_t moveId = p->move(moveIndex);
                const bool scrollOffset = (selectedIndex == 10 + moveIndex);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(rightColumnX + 14, infoRowY - 8, Rw - 28, 38, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(rightColumnX + 14, infoRowY - 8, Rw - 28, 38, 10, Colors::Accent, 2);
                }
                const std::string moveName = moveId ? std::string(Names::getMoveName(moveId)) : std::string("-");
                framebuffer.drawText(rightColumnX + 24, infoRowY, moveName, (moveId || scrollOffset) ? Colors::Text : Colors::TextDim, TextStyle::Body);
                if (moveId)
                {
                    const std::string ppFieldName = "PP " + std::to_string(p->movePP(moveIndex));
                    int valueWidth, vh;
                    framebuffer.measureText(ppFieldName, valueWidth, vh, TextStyle::Caption);
                    framebuffer.drawText(rightColumnX + Rw - 20 - valueWidth, infoRowY + 3, ppFieldName, Colors::TextDim, TextStyle::Caption);
                }
                screen.touchButtons.push_back({10 + moveIndex, rightColumnX + 14, infoRowY - 8, Rw - 28, 38});
                infoRowY += 52;
            }

            // Held item (selectable index 14; A opens the item picker).
            infoRowY += 14;
            framebuffer.drawText(rightColumnX + 18, infoRowY, "Held Item", Colors::TextDim, TextStyle::Caption);
            infoRowY += 30;
            {
                const bool scrollOffset = (selectedIndex == 14);
                if (scrollOffset)
                {
                    framebuffer.drawFilledRoundedRect(rightColumnX + 14, infoRowY - 8, Rw - 28, 38, 10, Colors::Selected);
                    framebuffer.drawRoundedRect(rightColumnX + 14, infoRowY - 8, Rw - 28, 38, 10, Colors::Accent, 2);
                }
                const uint16_t heldItemId = p->hasHeldItem() ? p->heldItem() : 0;
                const std::string iname = heldItemId ? std::string(Enums::isGen3Group(p->getGameGroup()) ? Names::getItemNameG3(heldItemId) : getItemName(heldItemId)) : std::string("None");
                framebuffer.drawText(rightColumnX + 24, infoRowY, p->hasHeldItem() ? iname : std::string("--"), heldItemId ? Colors::Text : Colors::TextDim, TextStyle::Body);
                // Only offer the row as a touch target where the format can actually hold an item;
                // Gen 1 has no held-item field, so tapping it would open an editor over nothing.
                if (p->hasHeldItem())
                    screen.touchButtons.push_back({14, rightColumnX + 14, infoRowY - 8, Rw - 28, 38});
            }

            // Bottom nav bar -- the SAME badge guide as the rest of the app. This is a full-screen page, so
            // it gets a real nav bar. The context depends on which column is focused (values / details /
            // moves). Drawn before the overlays so an open legality or ribbon popup dims it like everything
            // else behind them.
            {
                // An overlay drawn OVER this page owns input, so it names the keys -- this page's own
                // controls do nothing while a picker or a confirm is up, and printing them anyway is
                // what made the picker carry a second hint strip inside its card. The screen draws that
                // bar itself, after the overlay, so the overlay's scrim does not dim it.
                std::string navHint = screen.overlayNavHint();
                if (navHint.empty())
                {
                // Y opens ribbons; R opens the legality list -- but only when there is something to read,
                // so a pokemon with an empty report simply omits R: Legality (nothing to view).
                const std::string legalSeg = legalityRep.empty() ? "" : "R: Legality  |  ";
                // A freshly-created pokemon has no "save" -- every field is an unsaved edit until committed, so
                // X reads KEEP (commit + close); Discard still lives on the B Keep/Discard prompt.
                const std::string saveSeg = screen.creator.editing ? "X: Keep" : "X: Save";
                // B always reads "Close": with unsaved edits it raises the Save/Discard/Back prompt rather
                // than discarding on the spot, so "Discard" would be a promise it does not keep. Dirtiness
                // is already signalled by the top-bar "Unsaved changes" marker.
                const std::string backSeg = "B: Close";
                if (selectedIndex >= 15)
                    navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Right: Values  |  " + saveSeg + "  |  " + backSeg;
                else if (selectedIndex >= 10)
                    navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Left: Values  |  " + saveSeg + "  |  " + backSeg;
                else
                    navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Left: Details  |  Right: Moves  |  " + saveSeg + "  |  " + backSeg;
                drawNavBar(framebuffer, navHint);
                }
            }

            // Legality issue overlay — opened via Y or by tapping the legality summary; any tap / B closes.
            if (screen.details.legalityOverlay)
            {
                framebuffer.drawFilledRect(0, 0, screenWidth, H, Color(0, 0, 0, 170));
                constexpr int overlayWidth = 760;
                constexpr int overlayPadding = 28;
                constexpr int headerHeight = 66;    // card top down to the first line of the report
                constexpr int headlineHeight = 30;  // the "No problems found." row
                constexpr int issueGap = 8;         // between two issues, on top of the line pitch
                constexpr int footerPadding = 24;
                constexpr int tagGap = 12;
                const int overlayX = (screenWidth - overlayWidth) / 2;

                // EVERY MESSAGE IS WRAPPED TO THE CARD, AND THE CARD IS SIZED FROM THE WRAPPED LINES.
                // Layer 3's sentences name a game, a species, a place and a level, and drawn as one
                // line each they ran straight out of the card's right edge. The severity tag keeps a
                // column of its own, as wide as the widest tag, so every message starts at the same x
                // and a continuation line sits under the text it continues rather than under the tag.
                constexpr const char *noteTag = "[note]", *warningTag = "[warning]", *illegalTag = "[illegal]";
                int tagColumnWidth = 0;
                for (const char *tag : {noteTag, warningTag, illegalTag})
                {
                    int tagWidth = 0, tagHeight = 0;
                    framebuffer.measureText(tag, tagWidth, tagHeight, TextStyle::Caption);
                    tagColumnWidth = std::max(tagColumnWidth, tagWidth);
                }
                const int tagX = overlayX + overlayPadding;
                const int messageX = tagX + tagColumnWidth + tagGap;
                const int messageWidth = overlayX + overlayWidth - overlayPadding - messageX;
                const int captionLineHeight = framebuffer.lineHeight(TextStyle::Caption);

                // Info notes are listed too, dimmed: they carry Layer 3's verdict (which encounter
                // matched) and its abstentions (why a check could not run), which are exactly what a
                // clean-looking pokemon is worth reading.
                struct IssueLayout
                {
                    Color color;
                    const char *tag;
                    std::vector<std::string> lines;
                };
                std::vector<IssueLayout> issueLayouts;
                issueLayouts.reserve(legalityRep.issues.size());
                int contentHeight = legalityRep.ok() ? headlineHeight : 0;
                for (const auto &issue : legalityRep.issues)
                {
                    IssueLayout layout{Colors::TextDim, noteTag, {}};
                    if (issue.severity == Legality::Severity::Invalid)
                    {
                        layout.color = Colors::LegalityProblem;
                        layout.tag = illegalTag;
                    }
                    else if (issue.severity == Legality::Severity::Warning)
                    {
                        layout.color = Colors::Orange;
                        layout.tag = warningTag;
                    }
                    layout.lines = wrapTextToWidth(framebuffer, issue.text, messageWidth, TextStyle::Caption);
                    contentHeight += static_cast<int>(layout.lines.size()) * captionLineHeight + issueGap;
                    issueLayouts.push_back(std::move(layout));
                }

                const int overlayHeight = std::min(H - 60, headerHeight + std::max(contentHeight, headlineHeight) + footerPadding);
                const int overlayY = (H - overlayHeight) / 2;
                framebuffer.drawFilledRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 16, Colors::Panel);
                framebuffer.drawRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 16, Colors::Border, 1);
                framebuffer.drawText(overlayX + 24, overlayY + 20, "Legality", Colors::Text, TextStyle::Heading);
                {
                    const char *closeHint = "B / tap: close";
                    int closeHintWidth = 0, closeHintHeight = 0;
                    framebuffer.measureText(closeHint, closeHintWidth, closeHintHeight, TextStyle::Caption);
                    framebuffer.drawText(overlayX + overlayWidth - 24 - closeHintWidth, overlayY + 28, closeHint, Colors::TextDim, TextStyle::Caption);
                }

                int lineY = overlayY + headerHeight;
                if (legalityRep.ok())
                {
                    framebuffer.drawText(tagX, lineY, "No problems found.", Colors::LegalityClean, TextStyle::Body);
                    lineY += headlineHeight;
                }
                // A REPORT TALLER THAN THE SCREEN SAYS SO. Wrapping makes an issue taller, never
                // shorter, so the card fills sooner than it did -- and an issue cut off without a word
                // is one the user never learns exists. An issue is drawn whole or not at all, and the
                // row the first one left out would have taken names how many were left out.
                const int contentBottom = overlayY + overlayHeight - footerPadding;
                for (size_t issueIndex = 0; issueIndex < issueLayouts.size(); ++issueIndex)
                {
                    const IssueLayout &layout = issueLayouts[issueIndex];
                    const int issueHeight = static_cast<int>(layout.lines.size()) * captionLineHeight;
                    const bool isLastIssue = (issueIndex + 1 == issueLayouts.size());
                    const int roomForHiddenCount = isLastIssue ? 0 : issueGap + captionLineHeight;
                    if (lineY + issueHeight + roomForHiddenCount > contentBottom)
                    {
                        const std::string hiddenCount = "+" + std::to_string(issueLayouts.size() - issueIndex) + " more not shown";
                        framebuffer.drawText(messageX, lineY, hiddenCount, Colors::TextDim, TextStyle::Caption);
                        break;
                    }
                    framebuffer.drawText(tagX, lineY, layout.tag, layout.color, TextStyle::Caption);
                    for (const std::string &line : layout.lines)
                    {
                        framebuffer.drawText(messageX, lineY, line, layout.color, TextStyle::Caption);
                        lineY += captionLineHeight;
                    }
                    lineY += issueGap;
                }
                // id 96: tap anywhere closes -- but NOT over the nav bar, whose badges are themselves
                // tappable. Overlapping them would fire both the badge's button and this close.
                screen.touchButtons.push_back({96, 0, 0, screenWidth, H - NAV_BAR_HEIGHT});
            }

            // Ribbon list overlay — opened by tapping the Ribbons row; any tap / B closes.
            // Two columns, because a fully-decorated Gen 8/9 pokemon can carry dozens of ribbons and marks.
            if (screen.details.ribbonOverlay)
            {
                const auto ribbonList = Names::getMonRibbons(reinterpret_cast<const uint8_t *>(p->getData().data()), p->getGameGroup());
                framebuffer.drawFilledRect(0, 0, screenWidth, H, Color(0, 0, 0, 170));
                const int cols = (ribbonList.size() > 12) ? 2 : 1;
                const int perCol = (static_cast<int>(ribbonList.size()) + cols - 1) / std::max(1, cols);
                const int overlayWidth = (cols == 2) ? 860 : 560;
                const int overlayHeight = std::min(H - 60, 96 + std::max(1, perCol) * 28);
                const int overlayX = (screenWidth - overlayWidth) / 2, oy = (H - overlayHeight) / 2;
                framebuffer.drawFilledRoundedRect(overlayX, oy, overlayWidth, overlayHeight, 16, Colors::Panel);
                framebuffer.drawRoundedRect(overlayX, oy, overlayWidth, overlayHeight, 16, Colors::Border, 1);
                framebuffer.drawText(overlayX + 24, oy + 20, "Ribbons & Marks", Colors::Text, TextStyle::Heading);
                {
                    const char *h = "B / tap: close";
                    int headingWidth, hh;
                    framebuffer.measureText(h, headingWidth, hh, TextStyle::Caption);
                    framebuffer.drawText(overlayX + overlayWidth - 24 - headingWidth, oy + 28, h, Colors::TextDim, TextStyle::Caption);
                }
                const int colW = (overlayWidth - 56) / std::max(1, cols);
                for (size_t rbIndex = 0; rbIndex < ribbonList.size(); ++rbIndex)
                {
                    const int color = static_cast<int>(rbIndex) / std::max(1, perCol);
                    const int ribbonRowIndex = static_cast<int>(rbIndex) % std::max(1, perCol);
                    const int ribbonX = overlayX + 28 + color * colW;
                    const int statRowY = oy + 66 + ribbonRowIndex * 28;
                    if (statRowY > oy + overlayHeight - 26)
                        continue;
                    framebuffer.drawText(ribbonX, statRowY, ribbonList[rbIndex], Colors::Text, TextStyle::Caption);
                }
                // id 96: tap anywhere closes -- but NOT over the nav bar, whose badges are themselves
                // tappable. Overlapping them would fire both the badge's button and this close.
                screen.touchButtons.push_back({96, 0, 0, screenWidth, H - NAV_BAR_HEIGHT});
            }
        }
    }
}
