/**
 * Facts the history and memory verifiers share: whether a Pokemon has left its original trainer, which
 * context its encounter belongs to, and the trainer whose save is open. Ported from the PKHeX files named
 * at each declaration.
 */
#ifndef LEGALITY_VERIFIERS_HANDLERRULES_H
#define LEGALITY_VERIFIERS_HANDLERRULES_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/AnalysisContext.h"

namespace Legality
{
    // PKHeX Locations
    constexpr uint16_t LOCATION_LINK_TRADE_6 = 30002;
    constexpr uint16_t LOCATION_LINK_TRADE_6_NPC = 30001;
    constexpr uint16_t LOCATION_GO_HOME = 30012;
    constexpr uint16_t LOCATION_HOME_GIFT = 30018;
    constexpr uint16_t LOCATION_GO_LETS_GO = 50;

    constexpr uint8_t VERSION_POKEMON_GO = 34;

    /// The group standing for the PKHeX EntityContext `group` belongs to: the groups that share one
    /// entity format (X/Y and Omega Ruby/Alpha Sapphire, Sun/Moon and Ultra Sun/Ultra Moon, ...) answer
    /// the same group. Invalid stays Invalid.
    Enums::GameVersion getContextGroup(Enums::GameVersion group);

    /// PKHeX PKM.Generation: the generation of the version byte, 0 when it names no game.
    int getEntityGeneration(const AnalysisContext &context);

    /// PKHeX PKM.GO_HOME: a Pokemon GO Pokemon brought into HOME.
    bool isGoHome(const Pokemon::Pokemon &pokemon);

    /// PKHeX PKM.GO_LGPE: a Pokemon GO Pokemon brought into the GO Park.
    bool isGoLetsGo(const Pokemon::Pokemon &pokemon);

    /// PKHeX PKM.IsUntraded: no handling trainer, and never moved to a later format.
    bool isUntraded(const AnalysisContext &context);

    /// PKHeX IEncounterTemplate.Context of LegalInfo.EncounterOriginal, as getContextGroup's group.
    /// Invalid when PKSE cannot tell (a Pokemon GO Pokemon in HOME, whose context depends on the format
    /// GO exported it as).
    Enums::GameVersion getEncounterContextGroup(const AnalysisContext &context);
}

#endif
