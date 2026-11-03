/**
 * The names a game gives a species, as the legality checks compare stored text against them
 * (PKHeX PKHeX.Core/PKM/Util/SpeciesName.cs and Language.GetAvailableGameLanguages).
 *
 * A Pokemon that was never nicknamed stores its species name in the NICKNAME field, in the record's
 * own language and in the spelling of the game that wrote it: upper case before Generation 5
 * outside Japanese and Korean, and a handful of per-generation spellings. These answer "is this the
 * name the game would have written?" for the nickname, trash-byte and word-filter checks.
 *
 * `contextGroup` stands for PKHeX's EntityContext: the save-format group whose records the
 * question is about (any group of the same generation answers the same, except Legends: Z-A,
 * the one game with Latin American Spanish).
 */
#ifndef LEGALITY_VERIFIERS_SPECIESNAME_H
#define LEGALITY_VERIFIERS_SPECIESNAME_H

#include <cstdint>
#include <span>
#include <string>

#include "Enums/GameVersion.h"

namespace Legality
{
    /// Generation (1-9) of a save-format group.
    int getContextGeneration(Enums::GameVersion contextGroup);

    /// The language ids the games of `contextGroup` were released in (PKHeX Language.GetAvailableGameLanguages).
    std::span<const uint8_t> getContextLanguages(Enums::GameVersion contextGroup);

    /// The species name as `generation`'s games write it for `language`; species 0 is the egg name.
    /// Empty for a language or species with no name (PKHeX GetSpeciesNameGeneration).
    std::u16string getSpeciesNameForGeneration(uint16_t speciesId, uint8_t language, int generation);

    /// The name `generation`'s games give an egg in `language` (PKHeX SpeciesName.GetEggName).
    std::u16string getEggNameForGeneration(uint8_t language, int generation);

    /// English, Italian and Spanish spell Farfetch'd and Sirfetch'd with a curly apostrophe.
    bool isApostropheFarfetchdLanguage(uint8_t language);

    /// True when `nickname` is not the species name in `language` (PKHeX SpeciesName.IsNicknamed).
    bool isNicknamedInLanguage(uint16_t speciesId, const std::u16string &nickname, uint8_t language, int generation);

    /// True when `nickname` is the species name in none of the context's languages.
    bool isNicknamedAnyLanguage(uint16_t speciesId, const std::u16string &nickname, Enums::GameVersion contextGroup);

    /// The species whose plain name, in any of the context's languages, is `name`.
    bool tryGetSpeciesAnyLanguage(const std::u16string &name, Enums::GameVersion contextGroup, uint16_t &speciesId);

    /// As tryGetSpeciesAnyLanguage, comparing lower-cased (.NET ToLowerInvariant) text.
    bool tryGetSpeciesAnyLanguageIgnoringCase(const std::u16string &name, Enums::GameVersion contextGroup, uint16_t &speciesId);
}

#endif
