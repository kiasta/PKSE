#include "Trainer/PokemonFile.h"

#include <utility>

#include "Trainer/Bank.h"
#include "Utils/Logger.h"

namespace Trainer::PokemonFile
{
    bool importIntoBank(Bank &bank, const std::string &path, size_t *outBox, size_t *outSlot, std::string *error)
    {
        // load() already hands back a Bank-ready, round-trip-verified record.
        LoadResult loaded = load(path);
        if (!loaded)
        {
            if (error)
                *error = loaded.error;
            return false;
        }

        for (size_t boxIndex = 0; boxIndex < bank.boxes.size(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < Bank::BANK_SLOTS_PER_BOX; ++slotIndex)
            {
                if (bank.boxes[boxIndex][slotIndex])
                    continue;

                bank.boxes[boxIndex][slotIndex] = std::move(loaded.pokemon);
                if (outBox)
                    *outBox = boxIndex;
                if (outSlot)
                    *outSlot = slotIndex;
                if (error)
                    error->clear();
                return true;
            }
        }

        Utils::logErrorToFile("PokemonFile: Bank is full");
        if (error)
            *error = "Bank is full";
        return false;
    }
}
