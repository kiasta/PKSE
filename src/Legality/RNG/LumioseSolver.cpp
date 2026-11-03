#include "Legality/RNG/LumioseSolver.h"

#include "Legality/RNG/ShinyUtil.h"
#include "Legality/RNG/Xoroshiro128PlusReversal.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            template <typename SolverClass>
            bool tryGetSeedWithSolver(const GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon, SolverClass solver, uint64_t &seed) noexcept
            {
                while (solver.moveNext())
                {
                    seed = solver.current();
                    if (LumioseRNG::isMatch(pokemon, parameters, seed))
                    {
                        return true;
                    }
                }
                seed = 0;
                return false;
            }

            // PKHeX LumioseSolver.TryGetSeedSkip: the fake trainer id sits between the EC and the PID.
            bool tryGetSeedSkip(const GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon, uint32_t encryptionConstant, uint32_t PID, uint64_t &seed) noexcept
            {
                if (tryGetSeedWithSolver(parameters, pokemon, XoroMachineSkip(encryptionConstant, PID), seed))
                {
                    return true;
                }
                // The fake trainer may have flipped the PID anti-shiny.
                return tryGetSeedWithSolver(parameters, pokemon, XoroMachineSkip(encryptionConstant, LumioseRNG::antiShiny(PID)), seed);
            }
        }

        bool LumioseSolver::tryGetSeed(const GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon, uint64_t &seed) noexcept
        {
            seed = 0;
            if (parameters.shiny == Shiny::Random && ShinyUtil::isShiny(pokemon))
            {
                // The shiny PID's high bits were forced; recovering them needs the brute-force searches.
                return false;
            }
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            const uint32_t PID = pokemon.pid();
            if (parameters.correlation == LumioseCorrelation::SkipTrainer)
            {
                return tryGetSeedWithSolver(parameters, pokemon, XoroMachineConsecutive(encryptionConstant, PID), seed);
            }
            return tryGetSeedSkip(parameters, pokemon, encryptionConstant, PID, seed);
        }
    }
}
