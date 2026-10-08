#ifndef TRAINER_POKEMONFILE_H
#define TRAINER_POKEMONFILE_H

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Pokemon/Pokemon.h"

namespace Trainer
{
    class Bank;

    namespace PokemonFile
    {
        struct LoadResult
        {
            std::unique_ptr<Pokemon::Pokemon> pokemon;
            std::string error;

            explicit operator bool() const noexcept { return pokemon != nullptr; }
        };

        /// Native Pokemon entity extensions supported by PKSE's existing entity classes.
        /// Deliberately excludes side-game formats PKSE does not currently model as entities
        /// (Stadium, Colosseum/XD, Battle Revolution and Ranch).
        const std::vector<std::string> &extensions();

        /// True when `fileName` ends in one of the native extensions above (case-insensitive).
        bool supportsFileName(const std::string &fileName);

        /// Parse one native Pokemon entity. The extension selects the entity format and the size is
        /// validated before any format parser sees the bytes. The result is also checked for structural
        /// validity and native encrypt/decrypt round-trip fidelity.
        LoadResult parse(std::span<const std::byte> bytes, const std::string &fileName);

        /// Read and parse one native Pokemon entity from disk.
        LoadResult load(const std::string &path);

        /// Return an entity safe for PKSE's unified Bank representation. Native box-sized files
        /// are promoted to the Bank's party-sized record where required, with the party tail
        /// recalculated from the stored data. Gen 1/2 locale-sized records and Gen 3's existing
        /// short-record Bank path remain byte-preserving.
        std::unique_ptr<Pokemon::Pokemon> prepareForBank(const Pokemon::Pokemon &pokemon,
                                                         std::string *error = nullptr);

        /// Read, validate and place one native Pokemon entity into the first free Bank slot.
        /// Nothing is persisted here: the existing Bank Save/Discard flow owns that decision.
        bool importIntoBank(Bank &bank, const std::string &path, size_t *outBox = nullptr,
                            size_t *outSlot = nullptr, std::string *error = nullptr);

        /// Serialize a Pokemon to PKHeX-compatible decrypted native bytes. The source object
        /// is never mutated: checksum refresh happens on a clone, then the emitted bytes are reparsed and
        /// compared with that clone before they are returned.
        std::vector<std::byte> serialize(const Pokemon::Pokemon &pokemon, std::string *error = nullptr);

        /// Canonical PKHeX-compatible extension for this PKSE entity class (including PB/PA variants).
        std::string extensionFor(const Pokemon::Pokemon &pokemon);

        /// A unique default export path under sdmc:/PKSE/exports/.
        std::string defaultExportPath(const Pokemon::Pokemon &pokemon);

        /// Serialize, verify and write the entity. Parent directories are created as needed for PKSE's
        /// own default export directory. Existing files are never partially accepted as success.
        bool write(const Pokemon::Pokemon &pokemon, const std::string &path, std::string *error = nullptr);
    }
}

#endif
