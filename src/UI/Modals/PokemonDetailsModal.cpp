#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "UI/Modals/PokemonDetailsModal.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "UI/SpriteManager.h"
#include "Trainer/Trainer.h"
#include "Utils/HelperUtilities.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/PokemonTypes.h"
#include "Pokemon/Experience.h"
#include "Pokemon/BaseStatsGen89.h"
#include "Names/FormNames.h"
#include "Pokemon/PersonalInfoTable.h"   // getPersonalInfo -> formCount (Form row/picker)
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

namespace UI {
namespace Modals {

    // HOME "Check Summary"-style editor page. Full-screen, three columns: the render + details
    // (left), the editable stat table + shiny/nature/gender (center — the navigable "Values" column),
    // and the moveset editor (right — moves + held item). Editing is handled in TrainerViewScreen; this
    // only draws + captures touch.
    void drawPokemonDetailsModal(TrainerViewScreen& screen, PKSEFramebuffer& fb) {
        const Pokemon::Pokemon* p = screen.detailsTargetPokemon();
        if (!p || p->speciesID() == 0) return;

        const int W = fb.getWidth(), H = fb.getHeight();
        const bool isShiny = p->isShiny(p->id32(), p->species());
        const bool av = p->hasAwakeningValues();
        const int sel = screen.details.selectedField;   // 0-5 stats, 6 shiny, 7 nature, 8 gender, 9 level, 10-13 moves, 14 item
        const Legality::Report legalityRep = Legality::analyze(*p, p->getGameGroup());

        screen.touchButtons.clear();

        // Full-screen page over a dimmed box screen.
        fb.drawFilledRect(0, 0, W, H, Color(0, 0, 0, 130));
        fb.drawVerticalGradient(0, 0, W, H, Color(Colors::Background.r, Colors::Background.g, Colors::Background.b, 250),
                                Color(Colors::Background.r, Colors::Background.g, Colors::Background.b, 255));

        // ---- Top bar: name + gender + shiny + level + dex + close ----
        std::string name = utf16ToUtf8(p->nickname());
        if (name.empty()) name = std::string(p->species());
        // Regional/variant label ABOVE the name ("Combat Breed", "Alolan", "Hisuian", ...), so a
        // variant reads as what it is even when the mon is nicknamed. Only drawn when the form has a
        // name; the name drops a row to make space for it.
        const char* variant = Names::getFormName(p->speciesID(), p->form());
        int nameY = 16;
        if (variant[0] != '\0') { fb.drawText(28, 3, variant, Colors::Accent, TextStyle::Caption); nameY = 25; }
        fb.drawText(28, nameY, name, Colors::Text, TextStyle::Heading);
        int nW, nH; fb.measureText(name, nW, nH, TextStyle::Heading);
        int mx = 28 + nW + 12;
        const char* g = p->genderSymbol();
        if (g[0] != '\0') { fb.drawSymbol(mx, nameY + 6, g, (std::string(g) == "\xE2\x99\x82") ? Colors::Blue : Colors::Magenta); mx += 24; }
        if (isShiny) fb.drawShinyMark(mx, nameY + 4, 18, Colors::ShinyStar);

        std::string dex = std::to_string(p->speciesID());
        while (dex.size() < 3) dex = "0" + dex;
        // Box mons carry no party-stat block, so level() reads 0 for them in the packed formats
        // (SwSh/BDSP/SV/Z-A); fall back to the EXP-derived level (authoritative) for display.
        uint8_t subLvl = p->level();
        if (subLvl == 0) subLvl = Pokemon::getLevelFromExp(p->exp(), Pokemon::getGrowthRate(p->speciesID()));
        std::string sub = "Lv. " + std::to_string(subLvl) + "     No. " + dex;
        int sW, sH; fb.measureText(sub, sW, sH);
        fb.drawText(W - 90 - sW, 24, sub, Colors::TextDim);
        fb.drawFilledRect(0, 60, W, 2, Colors::Accent);

        // Close button (top-right).
        fb.drawFilledCircle(W - 40, 30, 20, Colors::PanelAlt);
        fb.drawCircle(W - 40, 30, 20, Colors::Border, 1);
        fb.drawText(W - 47, 18, "\xC3\x97", Colors::Text, TextStyle::Heading);  // ×
        screen.touchButtons.push_back({ 99, W - 64, 6, 52, 52 });

        // "Unsaved changes" marker (edits are applied live; X commits them and clears this). Centered
        // at the top so it crowds neither the name nor the Lv/No. A being-CREATED mon is unsaved by
        // nature -- every field is -- so the marker is just noise there and is hidden.
        if (screen.pokemonEditDirty() && !screen.creator.editing) {
            const char* u = "Unsaved changes";
            int uw, uh; fb.measureText(u, uw, uh, TextStyle::Caption);
            fb.drawText((W - uw) / 2, 8, u, Colors::Warning, TextStyle::Caption);
        }

        const int colY = 72, colH = H - colY - kNavBarH - 6;   // leave room for the bottom nav bar

        // =========================== LEFT: render + details ===========================
        const int Lx = 24, Lw = 430;
        fb.drawFilledRoundedRect(Lx, colY, Lw, colH, 16, Colors::Panel);
        fb.drawRoundedRect(Lx, colY, Lw, colH, 16, Colors::Border, 1);

        // Render sprite (top of the details panel).
        const int renderSz = 150;
        Sprite* sprite = SpriteManager::getSprite(p->speciesID(), p->form(), isShiny);
        if (sprite && sprite->data) {
            fb.drawSpriteIdle(Lx + (Lw - renderSz) / 2, colY + 14, renderSz, renderSz,
                              sprite->width, sprite->height, sprite->data, sprite->channels, 0.0f);
        }
        // Type badges under the render (icons, centered).
        {
            Pokemon::TypePair types = Pokemon::getPokemonTypes(p->speciesID(), p->form(), p->getGameGroup());
            Sprite* t1 = SpriteManager::getTypeSprite(types.type1);
            Sprite* t2 = Pokemon::hasSecondType(types) ? SpriteManager::getTypeSprite(types.type2) : nullptr;
            const int th = 22;
            int w1 = (t1 && t1->data) ? (t1->width * th) / t1->height : 0;
            int w2 = (t2 && t2->data) ? (t2->width * th) / t2->height : 0;
            int gap = (w2 > 0) ? 8 : 0;
            int tx = Lx + (Lw - (w1 + gap + w2)) / 2, tyy = colY + 14 + renderSz + 6;
            if (t1 && t1->data) { fb.drawImageScaled(tx, tyy, t1->width, t1->height, w1, th, t1->data, t1->channels); tx += w1 + gap; }
            if (t2 && t2->data) { fb.drawImageScaled(tx, tyy, t2->width, t2->height, w2, th, t2->data, t2->channels); }
        }

        // ----- Scrollable info column (below the sprite). Rows are TALL for easy touch; the column
        // clips + scrolls and auto-follows the selected field so it never runs off screen. -----
        char buf[160];
        const int RH = 40;                                     // row pitch -- big, touch-friendly targets
        const int contentTop = colY + 14 + renderSz + 40;
        const int legalityH = 34;                              // legality line is pinned below the scroll
        const int contentBottom = colY + colH - legalityH - 6;
        const int scroll = screen.details.leftScroll;
        int iy = contentTop;
        int selRowY = -1;                                      // absolute content-Y of the selected row
        std::vector<int> leftOrder;                            // editable field ids in draw order (nav list)
        auto rowVisible = [&](int ry) { return (ry + RH) > contentTop && ry < contentBottom; };

        fb.setClipRect(Lx + 1, contentTop, Lw - 2, contentBottom - contentTop);
        auto row = [&](const char* label, const std::string& value) {
            const int ry = iy - scroll;
            if (rowVisible(ry)) {
                fb.drawText(Lx + 18, ry + 11, label, Colors::TextDim, TextStyle::Body);
                int vw, vh; fb.measureText(value, vw, vh, TextStyle::Body);
                fb.drawText(Lx + Lw - 18 - vw, ry + 11, value, Colors::Text, TextStyle::Body);
            }
            iy += RH;
        };
        // Editable row: selection highlight + touch target, registered only while visible.
        auto editRow = [&](const char* label, const std::string& value, int fieldIdx) {
            leftOrder.push_back(fieldIdx);   // nav list: every editable row, in draw order (even off-screen)
            const int ry = iy - scroll;
            const bool es = (sel == fieldIdx);
            if (es) selRowY = iy;
            if (rowVisible(ry)) {
                if (es) { fb.drawFilledRoundedRect(Lx + 8, ry + 2, Lw - 16, RH - 6, 8, Colors::Selected);
                          fb.drawRoundedRect(Lx + 8, ry + 2, Lw - 16, RH - 6, 8, Colors::Accent, 2); }
                fb.drawText(Lx + 18, ry + 11, label, es ? Colors::Text : Colors::TextDim, TextStyle::Body);
                int vw, vh; fb.measureText(value, vw, vh, TextStyle::Body);
                fb.drawText(Lx + Lw - 18 - vw, ry + 11, value, es ? Colors::Accent : Colors::Text, TextStyle::Body);
                screen.touchButtons.push_back({ fieldIdx, Lx + 8, ry, Lw - 16, RH });
            }
            iy += RH;
        };
        // FireRed/LeafGreen (Gen 3) wires none of exp / form / met date / fateful, so those rows are
        // editable everywhere EXCEPT there (notGen3). Stat Nature (mints) and the egg-met conditions
        // are Gen 8+ / breeding-only -> the tighter modernFmt gate.
        const bool notGen3   = (p->getGameGroup() != Enums::GameVersion::FRLG);
        const bool modernFmt = (notGen3 && p->getGameGroup() != Enums::GameVersion::GG);
        // Nickname leads the column -- it's the name shown in-game. Gen 3 is NOT excluded: its field is
        // shorter (10) and its character set narrower, but it is as editable as any other format's.
        editRow("Nickname", utf16ToUtf8(p->nickname()), 23);
        editRow("Ability",    getAbilityName(p->ability()), 15);
        editRow("Friendship", std::to_string(p->friendship()), 16);
        // Form -- when the species has alternate forms AND the format can set them (Gen 3 forms are
        // PID-derived, so setForm is a no-op there -> exclude only Gen 3).
        if (notGen3 && Pokemon::getPersonalInfo(p->speciesID(), 0).formCount > 1) {
            const char* fn = Names::getFormName(p->speciesID(), p->form());
            editRow("Form", (fn[0] != '\0') ? std::string(fn) : std::string("Base"), 26);
        }
        // Stat Nature (the mint / effective-stat nature) -- modern formats only.
        if (modernFmt) editRow("Stat Nature", getNatureName(p->statNature()), 27);
        snprintf(buf, sizeof(buf), "%08X", p->pid());   row("PID", buf);
        snprintf(buf, sizeof(buf), "%u", p->exp());
        if (notGen3) editRow("EXP", buf, 24); else row("EXP", buf);
        editRow("Egg", p->isEgg() ? "Yes" : "No", 17);

        // ---- Original Trainer / met info ----
        std::string ot = utf16ToUtf8(p->otName());
        if (!ot.empty()) { ot += (p->otGender() == 0) ? " (M)" : " (F)"; row("OT", ot); }
        // Trainer ID in the format the game shows (six digits for Gen 7+ origins, 16-bit TID otherwise;
        // a mon with no origin falls back to its own format -- only Gen 3 is 16-bit among these games).
        {
            const uint8_t ver = p->originGame();
            const bool sixDigit = (ver != 0) ? Enums::usesSixDigitTrainerID(ver)
                                             : (p->getGameGroup() != Enums::GameVersion::FRLG);
            if (sixDigit) snprintf(buf, sizeof(buf), "%06u", p->id32() % 1000000u);
            else          snprintf(buf, sizeof(buf), "%05u", p->id32() & 0xFFFFu);
            row("OT ID", buf);
        }
        // Handling Trainer (Gen 7+): shown only once a mon has actually been handled by someone (htName
        // non-empty), like the OT row above. This makes the OT/HT re-stamp verifiable on-device --
        // change trainer gender/name, reopen a traded-in mon, and HT should track you. FireRed/LeafGreen
        // has no handler concept, so htName() is empty there and this row stays hidden.
        {
            std::string ht = utf16ToUtf8(p->htName());
            if (!ht.empty()) { ht += (p->htGender() == 0) ? " (M)" : " (F)"; row("HT", ht); }
        }
        { snprintf(buf, sizeof(buf), "Lv. %u", p->metLevel()); editRow("Met Lv", buf, 18); }
        // Origin-generation location routing: a Gen 3/4 mon's MET id is remapped into the current
        // format's numbering when it is transferred up (Gen 5+ keep their own table), so a Gen 3/4 met
        // must be named with the format's table -- else a Platinum starter link-traded to SV reads "(none)".
        const uint8_t fmtVer = Enums::getGroupRepVersion(p->getGameGroup());
        { const char* loc = Names::getMetLocationName(Enums::locationTableVersion(p->originGame(), fmtVer, false), p->metLocation());
          editRow("Met", (loc[0] != '\0') ? std::string(loc) : std::string("(none)"), 25); }
        // Met date -- every format records one except Gen 3 (FireRed/LeafGreen). Year byte is +2000.
        if (notGen3) {
            // A transferred mon can carry no met date (00/00) -- show "(none)" instead of "00/00/2000",
            // matching the egg-date row and how PKHeX blanks an unset date.
            if (p->metMonth() == 0 || p->metDay() == 0) snprintf(buf, sizeof(buf), "(none)");
            else snprintf(buf, sizeof(buf), "%02u/%02u/%04u", p->metDay(), p->metMonth(), 2000 + p->metYear());
            editRow("Met Date", buf, 28);
        }
        // Egg-met conditions -- the breeding formats only.
        if (modernFmt) {
            // "From an egg" means a real egg location. BDSP's "none" sentinel is 65535 (Gen 4 numbering,
            // where 0 is a real place), not 0 -- treat it as no-egg too, so the display matches the game.
            const bool fromEgg = p->eggLocation() != 0
                              && !(p->getGameGroup() == Enums::GameVersion::BDSP && p->eggLocation() == 0xFFFF);
            { const char* el = Names::getMetLocationName(Enums::locationTableVersion(p->originGame(), fmtVer, true), p->eggLocation());
              editRow("Egg Loc", (fromEgg && el[0] != '\0') ? std::string(el) : std::string("(none)"), 29); }
            if (fromEgg)
                 snprintf(buf, sizeof(buf), "%02u/%02u/%04u", p->eggDay(), p->eggMonth(), 2000 + p->eggYear());
            else snprintf(buf, sizeof(buf), "(none)");
            editRow("Egg Date", buf, 30);
        }
        editRow("Ball", Enums::getBallName(p->ball()), 19);
        editRow("Language", Enums::getLanguageName(p->language()), 20);
        { std::string og = Enums::getOriginGameName(p->originGame());
          editRow("Origin", og, 21); }
        if (notGen3) editRow("Fateful", p->isFatefulEncounter() ? "Yes" : "No", 31);
        {
            // Pokerus: editable (None -> Infected -> Cured) where the game has it; read-only otherwise.
            const char* pkrs = p->isPokerusInfected() ? "Infected" : p->isPokerusCured() ? "Cured" : "None";
            if (p->hasPokerus()) editRow("Pokerus", pkrs, 22);
            else                 row("Pokerus", pkrs);
        }
        // Ribbons: count only (there can be dozens); Y / tap opens the full list.
        {
            auto rb = Names::getMonRibbons(reinterpret_cast<const uint8_t*>(p->getData().data()), p->getGameGroup());
            if (!rb.empty()) {
                const int ry = iy - scroll;
                row("Ribbons", std::to_string(rb.size()) + "  (Y)");   // advances iy
                if (rowVisible(ry)) screen.touchButtons.push_back({ 94, Lx + 8, ry, Lw - 16, RH });
            }
        }
        fb.clearClip();
        screen.details.leftOrder = leftOrder;   // hand the nav its draw-order field list

        // Auto-scroll so the selected field stays visible (applied next frame), plus a faint scrollbar.
        {
            const int contentH = iy - contentTop;
            const int viewH = contentBottom - contentTop;
            int s = scroll;
            if (selRowY >= 0) {
                if (selRowY - s < contentTop)                s = selRowY - contentTop;
                else if ((selRowY + RH) - s > contentBottom) s = (selRowY + RH) - contentBottom;
            }
            const int maxS = (contentH > viewH) ? (contentH - viewH) : 0;
            if (s < 0) s = 0;
            if (s > maxS) s = maxS;
            screen.details.leftScroll = s;
            drawScrollbar(fb, Lx + Lw - 7, contentTop, viewH, contentH, s);
        }

        // Legality summary pinned at the bottom of the left pane (R / tap opens the full issue list).
        // A clean mon is still worth opening: Layer 3 records which encounter it matched, or why it
        // could not be checked, as notes — so the row stays tappable whenever there is anything to read.
        {
            const int ly = colY + colH - legalityH + 6;
            const bool clean = legalityRep.ok();
            std::string label = clean ? "Legality: no problems found" : "Legality: " + std::to_string(legalityRep.problemCount()) + " issue(s)";
            if (!legalityRep.empty()) label += "  -  R / tap to view";
            fb.drawText(Lx + 18, ly, label, clean ? Color(120, 205, 140) : Color(235, 100, 100), TextStyle::Caption);
            if (!legalityRep.empty()) {
                int lw, lh; fb.measureText(label, lw, lh, TextStyle::Caption);
                screen.touchButtons.push_back({ 95, Lx + 14, ly - 4, lw + 8, lh + 8 });  // id 95: open legality overlay
            }
        }

        // =========================== CENTER: editable stat table + shiny ===========================
        const int Cx = 474, Cw = 340;
        fb.drawFilledRoundedRect(Cx, colY, Cw, colH, 16, Colors::Panel);
        fb.drawRoundedRect(Cx, colY, Cw, colH, 16, Colors::Border, 1);
        fb.drawText(Cx + 18, colY + 16, "Values", Colors::Text, TextStyle::Heading);

        const int nameX = Cx + 18, cIV = Cx + 120, cEV = Cx + 200, cStat = Cx + 280;
        int ty = colY + 62;
        fb.drawText(cIV,   ty, "IV",            Colors::TextDim, TextStyle::Caption);
        fb.drawText(cEV,   ty, av ? "AV" : "EV", Colors::TextDim, TextStyle::Caption);
        fb.drawText(cStat, ty, "Stat",          Colors::TextDim, TextStyle::Caption);
        ty += 24;

        // Party stats (the 0x14A+ block) read 0 for box mons in the packed formats, so compute the
        // battle stat from base + IV + EV + EXP-level + (stat)nature when the stored value is 0. Party
        // mons keep their exact stored stats; LGPE (bstat == null -> its AV formula) is left as-is.
        const bool statIsGG = (p->getGameGroup() == Enums::GameVersion::GG);
        const Pokemon::BaseStatsGen89* bstat = statIsGG ? nullptr : Pokemon::getBaseStatsGen89(p->speciesID(), p->form());
        const uint8_t dispLevel = (p->level() != 0) ? p->level()
                                : Pokemon::getLevelFromExp(p->exp(), Pokemon::getGrowthRate(p->speciesID()));
        auto dispStat = [&](int idx, uint16_t stored) -> int {
            if (stored != 0 || !bstat) return stored;  // party mon / LGPE / no base data -> stored value
            const int base = (idx == 0) ? bstat->hp : (idx == 1) ? bstat->atk : (idx == 2) ? bstat->def
                           : (idx == 3) ? bstat->spa : (idx == 4) ? bstat->spd : bstat->spe;
            int iv = 0, ev = 0;
            switch (idx) {
                case 0: iv = p->ivHP();  ev = p->evHP();  break;
                case 1: iv = p->ivATK(); ev = p->evATK(); break;
                case 2: iv = p->ivDEF(); ev = p->evDEF(); break;
                case 3: iv = p->ivSPA(); ev = p->evSPA(); break;
                case 4: iv = p->ivSPD(); ev = p->evSPD(); break;
                default: iv = p->ivSPE(); ev = p->evSPE(); break;
            }
            int val = ((2 * base + iv + ev / 4) * dispLevel) / 100;
            if (idx == 0) return val + dispLevel + 10;  // HP uses a distinct formula
            val += 5;
            // Nature (mint-aware) modifier. natIdx maps a display row to its nature stat index
            // (nature order is Atk, Def, Spe, SpA, SpD); HP (idx 0) is never nature-affected.
            static const int natIdx[6] = { -1, 0, 1, 3, 4, 2 };
            const int up = p->statNature() / 5, down = p->statNature() % 5;
            if (up != down) {
                if (natIdx[idx] == up)        val = val * 110 / 100;
                else if (natIdx[idx] == down) val = val * 90 / 100;
            }
            return val;
        };

        struct S { const char* nm; int iv, evav, stat; };
        const S rows[6] = {
            { "HP",  p->ivHP(),  av ? p->avHP()  : p->evHP(),  dispStat(0, p->statHPMax()) },
            { "Atk", p->ivATK(), av ? p->avATK() : p->evATK(), dispStat(1, p->statATK()) },
            { "Def", p->ivDEF(), av ? p->avDEF() : p->evDEF(), dispStat(2, p->statDEF()) },
            { "SpA", p->ivSPA(), av ? p->avSPA() : p->evSPA(), dispStat(3, p->statSPA()) },
            { "SpD", p->ivSPD(), av ? p->avSPD() : p->evSPD(), dispStat(4, p->statSPD()) },
            { "Spe", p->ivSPE(), av ? p->avSPE() : p->evSPE(), dispStat(5, p->statSPE()) },
        };
        const int statRowH = 44;
        for (int i = 0; i < 6; ++i) {
            const bool s = (sel == i);
            if (s) { fb.drawFilledRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                     fb.drawRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2); }
            fb.drawText(nameX, ty, rows[i].nm, s ? Colors::Text : Colors::TextDim);
            fb.drawText(cIV,   ty, std::to_string(rows[i].iv),   Colors::Text);
            fb.drawText(cEV,   ty, std::to_string(rows[i].evav), Colors::Text);
            fb.drawText(cStat, ty, std::to_string(rows[i].stat), Colors::Accent);
            screen.touchButtons.push_back({ i, Cx + 8, ty - 6, Cw - 16, statRowH - 6 });
            ty += statRowH;
        }

        // Shiny toggle row (selectable index 6).
        ty += 10;
        {
            const bool s = (sel == 6);
            if (s) { fb.drawFilledRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                     fb.drawRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2); }
            fb.drawText(nameX, ty, "Shiny", s ? Colors::Text : Colors::TextDim);
            // Right-aligned to the panel edge, flush with Nature/Gender/Level below it.
            { const char* sv = isShiny ? "Yes" : "No"; int sw, sh; fb.measureText(sv, sw, sh);
              fb.drawText(Cx + Cw - 18 - sw, ty, sv, isShiny ? Colors::ShinyStar : Colors::Text); }
            screen.touchButtons.push_back({ 6, Cx + 8, ty - 6, Cw - 16, statRowH - 6 });
            ty += statRowH;
        }

        // Nature row (selectable index 7) — cycled with Left/Right (or A).
        ty += 6;
        {
            const bool s = (sel == 7);
            if (s) { fb.drawFilledRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                     fb.drawRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2); }
            fb.drawText(nameX, ty, "Nature", s ? Colors::Text : Colors::TextDim);
            std::string nat = getNatureName(p->nature());
            int nw, nh; fb.measureText(nat, nw, nh);
            fb.drawText(Cx + Cw - 18 - nw, ty, nat, Colors::Accent);
            screen.touchButtons.push_back({ 7, Cx + 8, ty - 6, Cw - 16, statRowH - 6 });
            ty += statRowH;
        }

        // Gender row (selectable index 8) — A opens the picker. READ-ONLY for a fixed-gender species
        // (male-only Braviary, female-only Miltank, genderless Magnemite): the value still shows, but
        // there is no highlight and no touch target, and the cursor steps over it — the same way the
        // left column handles a read-only row by not listing it. See TrainerViewScreen::genderEditable.
        ty += 6;
        {
            const bool editable = screen.genderEditable(*p);
            const bool s = editable && (sel == 8);
            if (s) { fb.drawFilledRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                     fb.drawRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2); }
            fb.drawText(nameX, ty, "Gender", s ? Colors::Text : Colors::TextDim);
            const uint8_t gv = p->gender();
            const char* gname = (gv == 0) ? "Male" : (gv == 1) ? "Female" : "Genderless";
            const Color gc = (gv == 0) ? Colors::Blue : (gv == 1) ? Colors::Magenta : Colors::Text;
            int gw, gh; fb.measureText(gname, gw, gh);
            fb.drawText(Cx + Cw - 18 - gw, ty, gname, gc);
            if (editable) screen.touchButtons.push_back({ 8, Cx + 8, ty - 6, Cw - 16, statRowH - 6 });
            ty += statRowH;
        }

        // Level row (selectable index 9) — A opens the 1-100 level picker (changing level recomputes stats).
        ty += 6;
        {
            const bool s = (sel == 9);
            if (s) { fb.drawFilledRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Selected);
                     fb.drawRoundedRect(Cx + 8, ty - 6, Cw - 16, statRowH - 6, 10, Colors::Accent, 2); }
            fb.drawText(nameX, ty, "Level", s ? Colors::Text : Colors::TextDim);
            const std::string lvlStr = std::to_string(dispLevel);
            int lw, lh; fb.measureText(lvlStr, lw, lh);
            fb.drawText(Cx + Cw - 18 - lw, ty, lvlStr, Colors::Accent);
            screen.touchButtons.push_back({ 9, Cx + 8, ty - 6, Cw - 16, statRowH - 6 });
            ty += statRowH;
        }
        // (The old on-panel "Randomize" row was removed -- it's L / the bottom nav bar's "Randomize
        // IVs", so it no longer needs a highlighted box taking space in the Values column.)

        // =========================== RIGHT: moves + held item ===========================
        const int Rx = 838, Rw = W - Rx - 24;
        fb.drawFilledRoundedRect(Rx, colY, Rw, colH, 16, Colors::Panel);
        fb.drawRoundedRect(Rx, colY, Rw, colH, 16, Colors::Border, 1);

        // This panel is exclusively the moveset — 4 move slots (rows 10-13) + held item (row 14).
        iy = colY + 22;
        fb.drawText(Rx + 18, iy, "Moves", Colors::Text, TextStyle::Heading);
        iy += 46;
        for (int i = 0; i < 4; ++i) {
            const uint16_t mv = p->move(i);
            const bool s = (sel == 10 + i);
            if (s) { fb.drawFilledRoundedRect(Rx + 14, iy - 8, Rw - 28, 38, 10, Colors::Selected);
                     fb.drawRoundedRect(Rx + 14, iy - 8, Rw - 28, 38, 10, Colors::Accent, 2); }
            const std::string mn = mv ? std::string(Names::getMoveName(mv)) : std::string("-");
            fb.drawText(Rx + 24, iy, mn, (mv || s) ? Colors::Text : Colors::TextDim, TextStyle::Body);
            if (mv) {
                const std::string pp = "PP " + std::to_string(p->movePP(i));
                int vw, vh; fb.measureText(pp, vw, vh, TextStyle::Caption);
                fb.drawText(Rx + Rw - 20 - vw, iy + 3, pp, Colors::TextDim, TextStyle::Caption);
            }
            screen.touchButtons.push_back({ 10 + i, Rx + 14, iy - 8, Rw - 28, 38 });
            iy += 52;
        }

        // Held item (selectable index 14; A opens the item picker).
        iy += 14;
        fb.drawText(Rx + 18, iy, "Held Item", Colors::TextDim, TextStyle::Caption);
        iy += 30;
        {
            const bool s = (sel == 14);
            if (s) { fb.drawFilledRoundedRect(Rx + 14, iy - 8, Rw - 28, 38, 10, Colors::Selected);
                     fb.drawRoundedRect(Rx + 14, iy - 8, Rw - 28, 38, 10, Colors::Accent, 2); }
            const uint16_t it = p->heldItem();
            const std::string iname = it
                ? std::string(p->getGameGroup() == Enums::GameVersion::FRLG
                    ? Names::getItemNameG3(it) : getItemName(it))
                : std::string("None");
            fb.drawText(Rx + 24, iy, iname, it ? Colors::Text : Colors::TextDim, TextStyle::Body);
            screen.touchButtons.push_back({ 14, Rx + 14, iy - 8, Rw - 28, 38 });
        }

        // Bottom nav bar -- the SAME badge guide as the rest of the app. This is a full-screen page,
        // so it gets a real nav bar rather than the old cramped inline hint. The context depends on
        // which column is focused (values / details / moves). Drawn before the overlays so an open
        // legality/ribbon popup dims it like everything else behind them.
        {
            std::string navHint;
            // Y opens ribbons; R opens the legality list -- but only when there ARE issues, so a clean
            // mon simply omits R: Legality (the button is disabled / nothing to view).
            const std::string legalSeg = legalityRep.ok() ? "" : "R: Legality  |  ";
            // A freshly-created mon has no "save" -- every field is an unsaved edit until committed, so
            // X reads KEEP (commit + close); Discard still lives on the B Keep/Discard prompt.
            const std::string saveSeg = screen.creator.editing ? "X: Keep" : "X: Save";
            // B always reads "Close" now: with unsaved edits it raises the Save/Discard/Back prompt
            // rather than discarding on the spot, so promising "Discard" would describe the old
            // behaviour. Dirtiness is already signalled by the top-bar "Unsaved changes" marker.
            const std::string backSeg = "B: Close";
            if (sel >= 15)      navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Right: Values  |  " + saveSeg + "  |  " + backSeg;
            else if (sel >= 10) navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Left: Values  |  " + saveSeg + "  |  " + backSeg;
            else                navHint = "A: Edit  |  Y: Ribbons  |  L: Randomize IVs  |  " + legalSeg + "Left: Details  |  Right: Moves  |  " + saveSeg + "  |  " + backSeg;
            drawNavBar(fb, navHint);
        }

        // Legality issue overlay — opened via Y or by tapping the legality summary; any tap / B closes.
        if (screen.details.legalityOverlay) {
            fb.drawFilledRect(0, 0, W, H, Color(0, 0, 0, 170));
            const int rowsN = static_cast<int>(legalityRep.issues.size());
            const int ow = 760, oh = std::min(H - 60, 96 + std::max(1, rowsN) * 30);
            const int ox = (W - ow) / 2, oy = (H - oh) / 2;
            fb.drawFilledRoundedRect(ox, oy, ow, oh, 16, Colors::Panel);
            fb.drawRoundedRect(ox, oy, ow, oh, 16, Colors::Border, 1);
            fb.drawText(ox + 24, oy + 20, "Legality", Colors::Text, TextStyle::Heading);
            { const char* h = "B / tap: close"; int hw, hh; fb.measureText(h, hw, hh, TextStyle::Caption);
              fb.drawText(ox + ow - 24 - hw, oy + 28, h, Colors::TextDim, TextStyle::Caption); }
            int ly = oy + 66;
            if (legalityRep.ok()) {
                fb.drawText(ox + 28, ly, "No problems found.", Color(120, 205, 140), TextStyle::Body);
            } else {
                for (const auto& is : legalityRep.issues) {
                    if (is.severity == Legality::Severity::Info) continue;
                    if (ly > oy + oh - 30) break;
                    const Color c = (is.severity == Legality::Severity::Invalid) ? Color(235, 100, 100) : Colors::Orange;
                    const char* tag = (is.severity == Legality::Severity::Invalid) ? "[illegal]  " : "[warning]  ";
                    fb.drawText(ox + 28, ly, std::string(tag) + is.text, c, TextStyle::Caption);
                    ly += 30;
                }
            }
            // id 96: tap anywhere closes -- but NOT over the nav bar, whose badges are themselves
            // tappable. Overlapping them would fire both the badge's button and this close.
            screen.touchButtons.push_back({ 96, 0, 0, W, H - kNavBarH });
        }

        // Ribbon list overlay — opened by tapping the Ribbons row; any tap / B closes.
        // Two columns, because a fully-decorated Gen 8/9 mon can carry dozens of ribbons and marks.
        if (screen.details.ribbonOverlay) {
            const auto rb = Names::getMonRibbons(reinterpret_cast<const uint8_t*>(p->getData().data()),
                                                 p->getGameGroup());
            fb.drawFilledRect(0, 0, W, H, Color(0, 0, 0, 170));
            const int cols = (rb.size() > 12) ? 2 : 1;
            const int perCol = (static_cast<int>(rb.size()) + cols - 1) / std::max(1, cols);
            const int ow = (cols == 2) ? 860 : 560;
            const int oh = std::min(H - 60, 96 + std::max(1, perCol) * 28);
            const int ox = (W - ow) / 2, oy = (H - oh) / 2;
            fb.drawFilledRoundedRect(ox, oy, ow, oh, 16, Colors::Panel);
            fb.drawRoundedRect(ox, oy, ow, oh, 16, Colors::Border, 1);
            fb.drawText(ox + 24, oy + 20, "Ribbons & Marks", Colors::Text, TextStyle::Heading);
            { const char* h = "B / tap: close"; int hw, hh; fb.measureText(h, hw, hh, TextStyle::Caption);
              fb.drawText(ox + ow - 24 - hw, oy + 28, h, Colors::TextDim, TextStyle::Caption); }
            const int colW = (ow - 56) / std::max(1, cols);
            for (size_t i = 0; i < rb.size(); ++i) {
                const int c = static_cast<int>(i) / std::max(1, perCol);
                const int r = static_cast<int>(i) % std::max(1, perCol);
                const int tx = ox + 28 + c * colW;
                const int ty = oy + 66 + r * 28;
                if (ty > oy + oh - 26) continue;
                fb.drawText(tx, ty, rb[i], Colors::Text, TextStyle::Caption);
            }
            // id 96: tap anywhere closes -- but NOT over the nav bar, whose badges are themselves
            // tappable. Overlapping them would fire both the badge's button and this close.
            screen.touchButtons.push_back({ 96, 0, 0, W, H - kNavBarH });
        }
    }
}
}
