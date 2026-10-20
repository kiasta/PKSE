#ifndef UI_PANELS_BOX_POKEMON_PANEL_H
#define UI_PANELS_BOX_POKEMON_PANEL_H

namespace UI
{
    class PKSEFramebuffer;
    class TrainerViewScreen;
}

namespace UI
{
    namespace Panels
    {
        void drawBoxPokemon(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer, int boxPokemonX, int boxPokemonY, int boxPokemonWidth, int boxPokemonHeight);
        // HOME summary side-panel (render + hexagon + info) shown right of the box when entered.
        void drawBoxSummaryPanel(UI::TrainerViewScreen &screen, UI::PKSEFramebuffer &framebuffer, int boxSummaryPanelX, int boxSummaryPanelY, int boxSummaryPanelWidth, int boxSummaryPanelHeight);
    }
}

#endif
