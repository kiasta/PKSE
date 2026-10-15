#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <cstdint>
#include <string>

namespace UI
{

    /// A Pokedex number as this UI spells it: zero-padded to three digits, no "No." prefix.
    ///
    /// THREE views show it -- the details page, the box Summary panel and the Storage detail strip
    /// -- and each had grown its own copy of the pad loop. Three copies of a format is three
    /// chances for the next view to be given two of them, which is the same reason boxGridColumns
    /// below exists. The "No." label itself stays at the call site, because the spacing in front of
    /// it differs per view and that is layout rather than format.
    ///
    /// Species above 999 are left alone rather than widened: #1025 reads "1025", which is what the
    /// modern games show, and padding is only ever needed to reach three.
    inline std::string dexNumberLabel(uint16_t speciesId)
    {
        std::string text = std::to_string(speciesId);
        while (text.size() < 3)
            text = "0" + text;
        return text;
    }

    /// A rectangular tap target a screen or dialog captured while drawing, hit-tested on the next
    /// update. Lives here rather than inside TrainerViewScreen because dialogs are drawn by more
    /// than one screen now -- the file browser is opened from the title picker as well -- and a
    /// dialog cannot name a screen type without depending on it.
    struct TouchButton
    {
        int buttonId, hitX, hitY, hitWidth, hitHeight;
    };

    /// Number of COLUMNS in a box grid holding `slotsPerBox` Pokemon.
    ///
    /// Every game PKSE handles lays a box out in FIVE ROWS and varies the width: 6x5 = 30 for
    /// most, 5x5 = 25 in Let's Go, 4x5 = 20 for an international Gen 1 save (a Japanese one is 30
    /// and comes out 6x5 again). Derived from the slot count rather than listed per game, and it
    /// lives here because the rule had FOUR independent copies -- the box panel, the HOME menu
    /// panel, the storage view and the box-tap hit test -- each spelling out "25 ? 5 : 6". Four
    /// copies of a rule is four chances for the next box size to be added to three of them.
    ///
    /// A slot count that is not a multiple of five falls back to 6, which is wrong but bounded;
    /// no such game exists, and guessing a width is better than dividing by zero.
    constexpr int boxGridColumns(int slotsPerBox)
    {
        const int cols = slotsPerBox / 5;
        return (cols >= 1 && cols * 5 == slotsPerBox) ? cols : 6;
    }
    inline constexpr int BOX_GRID_ROWS = 5;

    /// Width every box grid is SPACED to, whatever its own column count. This is the bank's width
    /// (30 slots -> 6), and it is the reference because the bank is the grid a save pane sits
    /// beside in the storage view.
    inline constexpr int BOX_GRID_REFERENCE_COLUMNS = 6;

    /// Left offset that centres `cols` columns of `colPitch` inside a `BOX_GRID_REFERENCE_COLUMNS`-wide area.
    ///
    /// A narrow box is drawn at the SAME cell size as a wide one and centred in the leftover
    /// space, rather than stretched to fill the panel. Stretching is the obvious implementation
    /// and it is wrong to use: a 20-slot Gen 1 box spread over six columns' worth of width gives
    /// cells half again as large as the bank's, so the cursor visibly changes scale as it crosses
    /// from the save pane to the bank pane, and the two grids no longer line up row for row.
    /// Centring makes a 4-wide box look exactly like a 6-wide one with its outer columns removed.
    constexpr int boxGridCenterOffset(int cols, int colPitch)
    {
        const int slack = BOX_GRID_REFERENCE_COLUMNS - cols;
        return slack > 0 ? (slack * colPitch) / 2 : 0;
    }

    // Pinned at compile time: these are the four box sizes PKSE actually stores, and the layout
    // each one has to produce. A change to the rule that broke one of them would otherwise only
    // show up as a misdrawn grid on hardware.
    static_assert(boxGridColumns(30) == 6, "a 30-slot box is 6x5"); // most games, and the bank
    static_assert(boxGridColumns(25) == 5, "a 25-slot box is 5x5"); // Let's Go
    static_assert(boxGridColumns(20) == 4, "a 20-slot box is 4x5"); // Gen 1 international
    static_assert(boxGridColumns(30) * BOX_GRID_ROWS == 30, "columns x rows must cover the box");
    static_assert(boxGridColumns(25) * BOX_GRID_ROWS == 25, "columns x rows must cover the box");
    static_assert(boxGridColumns(20) * BOX_GRID_ROWS == 20, "columns x rows must cover the box");

    struct Color
    {
        uint8_t red, green, blue, alpha;

        constexpr Color(uint8_t redValue, uint8_t greenValue, uint8_t blueValue, uint8_t alphaValue = 255) : red(redValue), green(greenValue), blue(blueValue), alpha(alphaValue) {}

        uint32_t toRGBA8() const
        {
            return (red << 0) | (green << 8) | (blue << 16) | (alpha << 24);
        }
    };

    namespace Colors
    {
        // Fixed literal colors (theme-independent, always constant)
        constexpr Color Black(0, 0, 0);
        constexpr Color White(255, 255, 255);
        constexpr Color Gray(128, 128, 128);
        constexpr Color LightGray(192, 192, 192);
        constexpr Color DarkGray(64, 64, 64);
        constexpr Color Red(255, 0, 0);
        constexpr Color Green(0, 255, 0);
        constexpr Color Blue(0, 0, 255);
        constexpr Color Yellow(255, 255, 0);
        constexpr Color Cyan(0, 255, 255);
        constexpr Color Magenta(255, 0, 255);
        constexpr Color Orange(255, 165, 0);
        // Fill for a Settings toggle that can damage real data (the illegal-values override, and
        // writing a backup over the live game save). Deliberately in the fixed block rather than
        // the theme-aware one below: a mid red reads against both the dark panel and the light
        // one, which is why the two call sites already used this same value in either theme.
        constexpr Color Danger(200, 80, 80);
        // The legality verdict, wherever it is shown -- the editor page's row, its issue-list
        // overlay, and the box Summary panel's bottom-left corner. Named because those are three
        // files saying one thing, and a green that drifted in one of them would read as a different
        // verdict. Fixed rather than theme-aware: both are mid tones chosen to carry on the dark
        // panel and the light one, which is why all three sites already used them unchanged in
        // either theme. Clean is deliberately not the same green as anything else here -- it means
        // "nothing was found against this Pokemon", not "PKSE approves".
        // The editor page's one ACTION button: Trade Evolve. A filled green pill, because it does
        // something to the Pokemon rather than showing a value, and every other row on that column
        // is a value. Deliberately NOT LegalityClean, which means "nothing was found against this
        // Pokemon" rather than "press this" -- overloading that green would make a verdict and a
        // button the same colour. Fixed rather than theme-aware for the reason Danger is: a mid
        // green carries on the dark panel and the light one alike. EvolveText is the dark text drawn
        // on top of it, the same arrangement Primary/PrimaryText use for amber.
        constexpr Color Evolve(88, 180, 112);
        constexpr Color EvolveText(10, 36, 18);
        constexpr Color LegalityClean(120, 205, 140);
        constexpr Color LegalityProblem(235, 100, 100);
        // Laid over a box slot that does not match the storage search. Black at partial alpha darkens
        // either theme's disc and sprite alike.
        constexpr Color SearchMissShade(0, 0, 0, 140);

        // Semantic theme colors (runtime-swappable by applyTheme())
        // These are mutable so a dark/light toggle can restyle the whole UI without
        // changing the 15+ screens that reference Colors::Text etc. Defaults = dark theme.
        inline Color Background = Color(28, 27, 38); // deep indigo-tinted charcoal
        inline Color Panel = Color(40, 39, 54);      // elevated card surface
        inline Color PanelAlt = Color(48, 47, 64);   // secondary surface / hover
        inline Color Selected = Color(78, 70, 130);  // selection highlight (indigo)
        inline Color Border = Color(60, 58, 78);
        inline Color Text = Color(234, 233, 242);
        inline Color TextDim = Color(150, 148, 168);
        inline Color Accent = Color(139, 122, 255);  // indigo/violet accent
        inline Color AccentDim = Color(96, 84, 178); // muted accent (bars/underlines)
        // Warm secondary (HOME's "warm-on-cool" pop): selected pill / primary action / Save.
        // Indigo stays the hero; amber is used sparingly for the single primary/selected element.
        inline Color Primary = Color(255, 184, 77);   // amber
        inline Color PrimaryText = Color(43, 32, 10); // dark text drawn on top of amber
        // Attention accent for warning dialog titles ("Unsaved Changes", "Delete Backup?"). Theme-aware
        // because a bright amber that reads on the dark UI is nearly invisible on light-mode white.
        inline Color Warning = Color(255, 199, 66);
        // Shiny marker (star / "Yes"), HOME-style red rather than yellow. Theme-aware: yellow washed
        // out on light-mode white exactly like Warning did, and red reads on both sprites and panels.
        inline Color ShinyStar = Color(255, 96, 86);
        // Storage cursor-mode colors, one per CursorMode — a red/blue/green scheme across the three
        // pointer arrows (Menu / Move / Multi). Theme-aware: the light variants are deepened so
        // the arrow, the selection wash and the carried-block backing still read against a white panel.
        inline Color CursorMenu = Color(232, 92, 92);
        inline Color CursorMove = Color(86, 148, 244);
        inline Color CursorMulti = Color(96, 205, 128);

        // Let's Go specific colors (matching in-game UI) — theme-independent
        constexpr Color PartnerHeart(255, 105, 180); // Hot pink heart for Partner Pokemon
        constexpr Color PartyNumber(255, 200, 50);   // Yellow/amber for party position numbers
        // Party-position badge: a gold disc with a dark digit, drawn behind the number so it stays
        // legible on any sprite in either theme (a bare amber digit washed out on light-mode tiles).
        constexpr Color PartyBadge(255, 193, 68);
        constexpr Color PartyBadgeText(38, 28, 8);
    }

    // Height of the arrowhead on the storage/box grid cursor, in framebuffer pixels. Absolute
    // rather than a fraction of the disc, because the two grids size their discs differently (the
    // Boxes view has more room per cell than the bank's paired panes) -- scaling off the disc gave
    // a visibly bigger cursor in one view than the other.
    constexpr int GRID_CURSOR_HEIGHT = 32;

    // Minimum comfortable touch-target size in framebuffer pixels. The UI renders at 1280x720 on
    // the ~6" handheld screen, so tappable controls (menu rows, dialog buttons, tab/nav hit areas)
    // should be at least this tall/wide for a fingertip. Grid slots are already larger than this.
    constexpr int TouchTargetMin = 56;

    enum class ThemeMode
    {
        Dark,
        Light
    };

    inline ThemeMode g_themeMode = ThemeMode::Dark;

    // Swap the semantic palette. Screens keep using Colors::Text/Panel/... and pick up
    // the change automatically on the next frame (everything redraws each frame).
    inline void applyTheme(ThemeMode mode)
    {
        using namespace Colors;
        g_themeMode = mode;
        if (mode == ThemeMode::Dark)
        {
            Background = Color(28, 27, 38);
            Panel = Color(40, 39, 54);
            PanelAlt = Color(48, 47, 64);
            Selected = Color(78, 70, 130);
            Border = Color(60, 58, 78);
            Text = Color(234, 233, 242);
            TextDim = Color(150, 148, 168);
            Accent = Color(139, 122, 255);
            AccentDim = Color(96, 84, 178);
            Primary = Color(255, 184, 77);
            PrimaryText = Color(43, 32, 10);
            Warning = Color(255, 199, 66);  // bright gold, reads on the dark UI
            ShinyStar = Color(255, 96, 86); // warm coral-red on the dark UI
            CursorMenu = Color(232, 92, 92);
            CursorMove = Color(86, 148, 244);
            CursorMulti = Color(96, 205, 128);
        }
        else
        { // Light
            Background = Color(241, 241, 246);
            Panel = Color(255, 255, 255);
            PanelAlt = Color(232, 231, 242);
            Selected = Color(223, 218, 250);
            Border = Color(214, 214, 224);
            Text = Color(28, 28, 38);
            TextDim = Color(110, 110, 128);
            Accent = Color(109, 90, 230);
            AccentDim = Color(150, 134, 240);
            Primary = Color(245, 166, 45);
            PrimaryText = Color(43, 32, 10);
            Warning = Color(176, 98, 0);    // deep amber, reads on light-mode white
            ShinyStar = Color(202, 44, 38); // deep red, reads on light-mode white
            CursorMenu = Color(206, 58, 58);
            CursorMove = Color(38, 106, 214);
            CursorMulti = Color(38, 150, 84);
        }
    }
}

#endif
