/**
 * Pokemon files (.pk1 ... .pa9): export, read back, refuse what is not one, and import into the bank.
 *
 * The feature writes to the SD card and into the bank, and a file PKSE exports is meant to open in
 * PKHeX and the other way round, so it is held to expectations stated here rather than read from the
 * code under test:
 *
 *   - the extension is PKHeX's for the format (`.pk3` for every Gen 3 game, `.pb7` for Let's Go ...);
 *   - the file written is exactly what serialize() produced, and reading it back gives the same
 *     Pokemon -- species, form, PID, trainer ids, nickname, IVs, moves, level and EXP;
 *   - a file renamed to another format of a different size is refused, and so is one with a byte
 *     changed under its checksum (Gen 3 on; Gens 1 and 2 have no checksum to break);
 *   - the same record encrypted, under PKHeX's encrypted name (.ek3 ... .ea9), opens as the same
 *     Pokemon (Gen 3 on);
 *   - importing into an empty bank lands in box 1, slot 1, as that same Pokemon.
 *
 * It runs on a few real Pokemon from each save, against a scratch bank in the run's work directory.
 * Nothing the user owns is read or written: the files go in the work directory and are deleted.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "Test/TestSuite.h"
#include "Trainer/Bank.h"
#include "Trainer/PokemonFile.h"
#include "Trainer/Trainer.h"
#include "Utils/StringHelpers.h"

namespace Test
{

    namespace
    {
        /// How many Pokemon from each save are put through the file round trip.
        constexpr int SAMPLES_PER_SAVE = 3;

        /// PKHeX's file extension for each entity format.
        const char *pkhexExtensionFor(Enums::GameVersion group)
        {
            using Enums::GameVersion;
            switch (group)
            {
            case GameVersion::RBY:
                return ".pk1";
            case GameVersion::GSC:
                return ".pk2";
            case GameVersion::RSE:
            case GameVersion::FRLG:
                return ".pk3";
            case GameVersion::DP:
            case GameVersion::PT:
            case GameVersion::HGSS:
                return ".pk4";
            case GameVersion::BW:
            case GameVersion::B2W2:
                return ".pk5";
            case GameVersion::XY:
            case GameVersion::ORAS:
                return ".pk6";
            case GameVersion::SM:
            case GameVersion::USUM:
                return ".pk7";
            case GameVersion::GG:
                return ".pb7";
            case GameVersion::SWSH:
                return ".pk8";
            case GameVersion::BDSP:
                return ".pb8";
            case GameVersion::PLA:
                return ".pa8";
            case GameVersion::SV:
                return ".pk9";
            case GameVersion::ZA:
                return ".pa9";
            default:
                return "";
            }
        }

        bool writeBytes(const std::string &path, const std::vector<std::byte> &bytes)
        {
            FILE *file = fopen(path.c_str(), "wb");
            if (!file)
                return false;
            const bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
            return fclose(file) == 0 && written;
        }

        std::vector<std::byte> readBytes(const std::string &path)
        {
            std::vector<std::byte> bytes;
            FILE *file = fopen(path.c_str(), "rb");
            if (!file)
                return bytes;
            std::byte buffer[4096];
            size_t count;
            while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
                bytes.insert(bytes.end(), buffer, buffer + count);
            fclose(file);
            return bytes;
        }

        /// The fields a user would call "the same Pokemon". Each one that differs is named.
        std::string identityDifference(const Pokemon::Pokemon &expected, const Pokemon::Pokemon &actual)
        {
            if (actual.speciesID() != expected.speciesID())
                return "species";
            if (actual.form() != expected.form())
                return "form";
            if (actual.pid() != expected.pid())
                return "pid";
            if (actual.id32() != expected.id32())
                return "id32";
            if (actual.nickname() != expected.nickname())
                return "nickname";
            if (actual.otName() != expected.otName())
                return "otName";
            if (actual.exp() != expected.exp())
                return "exp";
            if (actual.level() != expected.level())
                return "level";
            for (int statIndex = 0; statIndex < 6; ++statIndex)
                if (actual.effectiveIV(statIndex) != expected.effectiveIV(statIndex))
                    return "iv" + std::to_string(statIndex);
            for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                if (actual.move(moveIndex) != expected.move(moveIndex))
                    return "move" + std::to_string(moveIndex + 1);
            return std::string();
        }

        /// The first few occupied slots, party first: real Pokemon this save actually holds.
        std::vector<const Pokemon::Pokemon *> pickSamples(const Trainer::Trainer &tr)
        {
            std::vector<const Pokemon::Pokemon *> picked;
            for (const auto &member : tr.party)
                if ((int)picked.size() < SAMPLES_PER_SAVE && !isEmptySlot(member.get()))
                    picked.push_back(member.get());
            for (size_t boxIndex = 0; boxIndex < tr.boxes.size(); ++boxIndex)
                for (size_t slotIndex = 0; slotIndex < tr.getSlotsPerBox() && slotIndex < Trainer::BOX_SLOTS; ++slotIndex)
                    if ((int)picked.size() < SAMPLES_PER_SAVE && !isEmptySlot(tr.boxes[boxIndex][slotIndex].get()))
                        picked.push_back(tr.boxes[boxIndex][slotIndex].get());
            return picked;
        }

        void checkOne(Context &context, const Pokemon::Pokemon &pokemon, int sampleIndex)
        {
            const std::string where = "sample=" + std::to_string(sampleIndex + 1) + " " + monLabel(pokemon);
            const Enums::GameVersion group = pokemon.getGameGroup();

            // The extension.
            const std::string extension = Trainer::PokemonFile::extensionFor(pokemon);
            const std::string expectedExtension = pkhexExtensionFor(group);
            context.check(extension == expectedExtension, "an exported Pokemon file has the wrong extension",
                          where + " got=\"" + extension + "\" expected=\"" + expectedExtension + "\"");

            // Export, and the file is exactly what serialize() produced.
            std::string error;
            const std::vector<std::byte> serialized = Trainer::PokemonFile::serialize(pokemon, &error);
            if (!context.check(!serialized.empty(), "a Pokemon could not be serialized for export",
                               where + " error=\"" + error + "\""))
                return;
            const std::string path = context.workDir + "/pokemonfile-test" + expectedExtension;
            if (!context.check(Trainer::PokemonFile::write(pokemon, path, &error), "exporting a Pokemon file failed",
                               where + " error=\"" + error + "\""))
                return;
            context.check(readBytes(path) == serialized, "the exported file is not what serialize() produced", where);

            // Read back as the same Pokemon.
            Trainer::PokemonFile::LoadResult loaded = Trainer::PokemonFile::load(path);
            if (context.check(static_cast<bool>(loaded), "an exported Pokemon file did not open again",
                              where + " error=\"" + loaded.error + "\""))
            {
                const std::string difference = identityDifference(pokemon, *loaded.pokemon);
                context.check(difference.empty(), "an exported Pokemon file opened as a different Pokemon",
                              where + " field=" + difference);
                context.check(loaded.pokemon->isStructurallyValid(), "an exported Pokemon file opened invalid", where);
            }

            // Another format of a different size, by name only: refused. Gen 3's record is the
            // smallest from Gen 3 on, so a Gen 3 file pretends to be a Gen 9 one and everything else
            // a Gen 3 one; Gens 1 and 2 pretend to be Gen 3 too.
            const std::string wrongExtension = Enums::isGen3Group(group) ? ".pk9" : ".pk3";
            const std::string renamedPath = context.workDir + "/pokemonfile-renamed" + wrongExtension;
            if (writeBytes(renamedPath, serialized))
            {
                Trainer::PokemonFile::LoadResult renamed = Trainer::PokemonFile::load(renamedPath);
                context.check(!renamed, "a Pokemon file renamed to another format's extension was accepted",
                              where + " as=\"" + wrongExtension + "\"");
                remove(renamedPath.c_str());
            }

            // The same record ENCRYPTED, under PKHeX's encrypted name (.ek3, .eb7, .ea9 ...): the same
            // Pokemon. Gens 1 and 2 have no encryption and no such name.
            if (group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC && serialized.size() >= 4)
            {
                std::string encryptedExtension = expectedExtension; // .pk3 -> .ek3, .pb7 -> .eb7, .pa9 -> .ea9
                encryptedExtension[1] = 'e';
                const uint32_t encryptionKey = static_cast<uint32_t>(serialized[0]) | (static_cast<uint32_t>(serialized[1]) << 8) |
                                               (static_cast<uint32_t>(serialized[2]) << 16) |
                                               (static_cast<uint32_t>(serialized[3]) << 24);
                std::byte *encrypted =
                    Trainer::Bank::encryptRecord(Trainer::Bank::groupAsBanked(group), serialized, encryptionKey);
                if (context.check(encrypted != nullptr, "a Pokemon could not be encrypted for the .ekX test", where))
                {
                    const std::vector<std::byte> encryptedBytes(encrypted, encrypted + serialized.size());
                    delete[] encrypted;
                    context.check(encryptedBytes != serialized, "the encrypted form of a Pokemon file equals its plain form",
                                  where);
                    const std::string encryptedPath = context.workDir + "/pokemonfile-encrypted" + encryptedExtension;
                    if (writeBytes(encryptedPath, encryptedBytes))
                    {
                        Trainer::PokemonFile::LoadResult opened = Trainer::PokemonFile::load(encryptedPath);
                        if (context.check(static_cast<bool>(opened), "an encrypted Pokemon file did not open",
                                          where + " as=\"" + encryptedExtension + "\" error=\"" + opened.error + "\""))
                        {
                            const std::string difference = identityDifference(pokemon, *opened.pokemon);
                            context.check(difference.empty(), "an encrypted Pokemon file opened as a different Pokemon",
                                          where + " as=\"" + encryptedExtension + "\" field=" + difference);
                        }
                        remove(encryptedPath.c_str());
                    }
                }
            }

            // A byte changed under the checksum: refused. Gens 1 and 2 have no checksum to break.
            if (group != Enums::GameVersion::RBY && group != Enums::GameVersion::GSC && serialized.size() > 0x40)
            {
                std::vector<std::byte> corrupted = serialized;
                corrupted[0x30] ^= std::byte{0x5A};
                if (writeBytes(path, corrupted))
                {
                    Trainer::PokemonFile::LoadResult broken = Trainer::PokemonFile::load(path);
                    context.check(!broken, "a Pokemon file with a byte changed under its checksum was accepted", where);
                }
            }
            remove(path.c_str());
        }

        /// Import into an EMPTY scratch bank: box 1, slot 1, the same Pokemon.
        void checkImport(Context &context, const Pokemon::Pokemon &pokemon)
        {
            const std::string where = monLabel(pokemon);
            const std::string path =
                context.workDir + "/pokemonfile-import" + std::string(pkhexExtensionFor(pokemon.getGameGroup()));
            std::string error;
            if (!context.check(Trainer::PokemonFile::write(pokemon, path, &error),
                               "exporting a Pokemon file for the import test failed", where + " error=\"" + error + "\""))
                return;

            // A scratch bank, never the user's; cleared afterwards exactly as the bank suite does.
            Trainer::Bank::setRootOverride(context.workDir + "/pokemonfile-bank");
            {
                Trainer::Bank bank;
                bool emptyAtStart = true;
                for (const auto &box : bank.boxes)
                    for (const auto &slotPokemon : box)
                        emptyAtStart = emptyAtStart && !slotPokemon;
                if (context.check(emptyAtStart, "the scratch bank for the import test was not empty", where))
                {
                    size_t box = 99, slotIndex = 99;
                    const bool imported = Trainer::PokemonFile::importIntoBank(bank, path, &box, &slotIndex, &error);
                    if (context.check(imported, "importing a Pokemon file into an empty bank failed",
                                      where + " error=\"" + error + "\""))
                    {
                        context.check(box == 0 && slotIndex == 0, "an import into an empty bank did not land in box 1, slot 1",
                                      where + " " + slot((int)box, (int)slotIndex));
                        const Pokemon::Pokemon *placed = bank.boxes[0][0].get();
                        if (context.check(placed != nullptr, "an import reported a slot it did not fill", where))
                        {
                            const std::string difference = identityDifference(pokemon, *placed);
                            context.check(difference.empty(), "an imported Pokemon is not the Pokemon in the file",
                                          where + " field=" + difference);
                        }
                    }
                }
            }
            Trainer::Bank::setRootOverride("");
            remove(path.c_str());
        }
    }

    int pokemonFilePlan(Context &context) { return context.trainer ? 1 : 0; }

    void pokemonFileRun(Context &context, int)
    {
        if (!context.trainer)
            return;
        const std::vector<const Pokemon::Pokemon *> samples = pickSamples(*context.trainer);
        if (samples.empty())
        {
            context.skip("this save holds no Pokemon, so Pokemon files were not tested");
            return;
        }
        for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
            checkOne(context, *samples[sampleIndex], (int)sampleIndex);
        checkImport(context, *samples.front());
    }
}
