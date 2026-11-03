#include "Legality/RNG/UnownRuins4.h"

#include <iterator>

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/MethodFinder.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr int MAX_DEPTH = 4;
            constexpr int UNOWN_LETTER_COUNT = 26;

            struct FormSet
            {
                uint8_t start;
                uint8_t end;
            };

            // The letter sets the Ruins of Alph unlock, in the order the player is most likely to have them.
            constexpr FormSet RUINS_OF_ALPH_FORM_SETS[MAX_DEPTH] = {{0, 9}, {17, 21}, {10, 16}, {22, 25}};

            constexpr uint8_t SOLACEON_FORMS[] = {0, 1, 2, 6, 7, 9, 10, 11, 12, 14, 15, 16, 18, 19, 20, 21, 22, 23, 24, 25};

            int addForms(uint8_t *forms, int count, int depth) noexcept
            {
                const FormSet set = RUINS_OF_ALPH_FORM_SETS[depth];
                for (int letter = set.start; letter <= set.end; letter++)
                {
                    forms[count++] = static_cast<uint8_t>(letter);
                }
                return count;
            }

            // Permutes which letter sets are unlocked and checks the letter the roll would pick from each.
            bool recurse(uint8_t *forms, uint8_t form, uint32_t random, int count, int depth) noexcept
            {
                if (depth == MAX_DEPTH)
                {
                    return false;
                }

                // Most players unlock every set, so the all-added case goes first.
                const int newCount = addForms(forms, count, depth);
                if (recurse(forms, form, random, newCount, depth + 1))
                {
                    return true;
                }

                // Earlier depths cover the not-added case for this set.
                const uint32_t roll = random % static_cast<uint32_t>(newCount);
                if (forms[roll] == form)
                {
                    return true;
                }

                // The letters just added need not be cleared: the shorter count ignores them.
                return recurse(forms, form, random, count, depth + 1);
            }

            bool isFormValidInterior(uint8_t form, uint32_t random) noexcept
            {
                uint8_t forms[UNOWN_LETTER_COUNT] = {};
                return recurse(forms, form, random, 0, 0);
            }

            uint8_t getSolaceonForm(uint32_t random, uint8_t prefer) noexcept
            {
                // The Maniac Tunnel area gives ! or ?.
                if (prefer >= 26)
                {
                    return static_cast<uint8_t>(26 + (random & 1));
                }
                return SOLACEON_FORMS[random % std::size(SOLACEON_FORMS)];
            }
        }

        bool RuinsOfAlph4::isFormValid(uint32_t seed, uint8_t form) noexcept
        {
            // Calls A, B, C, D are the PID and IVs, E the held item, F the form.
            return isFormValidFrame(LCRNG::next6(seed), form);
        }

        bool RuinsOfAlph4::isFormValid(const Pokemon::Pokemon &pokemon, uint8_t form) noexcept
        {
            uint32_t seed = 0;
            if (!MethodFinder::getLCRNGMethod1Match(pokemon, seed))
            {
                return true;
            }
            return isFormValid(seed, form);
        }

        // PKHeX RuinsOfAlph4.IsFormValidFrame
        bool RuinsOfAlph4::isFormValidFrame(uint32_t seed, uint8_t form) noexcept
        {
            if (form >= 26)
            {
                return form == getEntranceForm(seed);
            }

            const uint32_t random = seed >> 16;
            const bool radio = (random % 100) < 50;
            if (radio)
            {
                // The radio gives a not-yet-seen letter, which depends on the save's progress: any letter.
                return true;
            }
            if (isFormValidInterior(form, random))
            {
                return true;
            }
            const uint32_t next = LCRNG::next16(seed);
            return isFormValidInterior(form, next);
        }

        bool SolaceonRuins4::isFormValid(uint32_t seed, uint8_t form) noexcept
        {
            if (isSingleFormRoomUnown(form))
            {
                return true;
            }
            const uint32_t random = LCRNG::next6(seed) >> 16;
            return getSolaceonForm(random, form) == form;
        }

        bool SolaceonRuins4::isFormValid(const Pokemon::Pokemon &pokemon, uint8_t form) noexcept
        {
            if (isSingleFormRoomUnown(form))
            {
                return true;
            }
            uint32_t seed = 0;
            if (!MethodFinder::getLCRNGMethod1Match(pokemon, seed))
            {
                return true;
            }
            const uint32_t random = LCRNG::next6(seed) >> 16;
            return getSolaceonForm(random, form) == form;
        }
    }
}
