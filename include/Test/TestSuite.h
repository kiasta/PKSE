/**
 * A suite is two plain functions and a scope. `plan()` says how many CHUNKS of work the suite needs
 * for the context it has been handed; `run(chunk)` does one of them. The split exists for one
 * reason: the self test runs inside the app's own frame loop, so a chunk must finish inside a frame
 * or the console stops pumping appletMainLoop and the HOME button goes dead. Sweeping twelve boxes
 * is twelve chunks, not one long call.
 *
 * Function pointers rather than std::function, and a static table rather than a registration list:
 * the codebase builds with -fno-rtti -fno-exceptions, and a suite table that needs no allocation and
 * no dynamic initialisation is one less thing that can differ between the host harness and the .nro.
 *
 * ADDING A SUITE is: write `int fooPlan(Context&)` and `void fooRun(Context&, int)` in a new
 * src/Test/SuiteFoo.cpp, declare them below, and add one row to the table in TestRunner.cpp.
 */
#ifndef TEST_TEST_SUITE_H
#define TEST_TEST_SUITE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Test/TestReport.h"
#include "Pokemon/Pokemon.h"

namespace Test
{

    enum class Scope : uint8_t
    {
        Global,    // once per run, no save needed (data tables, ROMFS assets)
        PerTarget, // once per target title, with that title's save loaded
        Cross,     // once per run, after every target, against the samples they left behind
    };

    struct Suite
    {
        const char *suiteName; // short tag used in the report ("entity", "tables")
        Scope scope;
        /// Chunks this suite needs for `context`. 0 means "nothing to do here" and the suite is skipped
        /// silently -- a suite that cannot run for a REASON should return 1 and report a skip from
        /// run(), so the gap lands in the report instead of vanishing.
        int (*plan)(Context &context);
        void (*run)(Context &context, int chunk);
    };

    const Suite *suites() noexcept;
    int suiteCount() noexcept;

    /// Every save-format GROUP PKSE handles, in generation order.
    ///
    /// One list, used by both runs. The host harness sweeps the per-group tables for all of them
    /// because nothing limits it; on console the per-target suites still run once per generation,
    /// but a suite that needs no save (the table checks, the entity capability sweep) covers
    /// everything here regardless of what the console has installed. That is the point: the groups
    /// with no save on this machine are exactly the ones a per-save suite can say nothing about.
    ///
    /// Adding a game means adding its group here, and the suites that iterate it pick it up.
    inline constexpr Enums::GameVersion ALL_SAVE_GROUPS[] = {
        Enums::GameVersion::RBY,
        Enums::GameVersion::GSC,
        Enums::GameVersion::RSE,
        Enums::GameVersion::FRLG,
        Enums::GameVersion::DP,
        Enums::GameVersion::PT,
        Enums::GameVersion::HGSS,
        Enums::GameVersion::BW,
        Enums::GameVersion::B2W2,
        Enums::GameVersion::XY,
        Enums::GameVersion::ORAS,
        Enums::GameVersion::SM,
        Enums::GameVersion::USUM,
        Enums::GameVersion::GG,
        Enums::GameVersion::SWSH,
        Enums::GameVersion::BDSP,
        Enums::GameVersion::PLA,
        Enums::GameVersion::SV,
        Enums::GameVersion::ZA,
    };

    /// Every title the self test covers, in release order -- one plan row and one report
    /// line each. This is the list the user sees, so it names GAMES, not generations: Scarlet
    /// and Violet are one save format but two titles, and so are Sword and Shield.
    ///
    /// A title with no save on the console is reported as a skip, loudly. A pass that quietly
    /// covered five titles out of thirty-eight is worse than no pass at all.
    ///
    /// It lives in this header rather than in TestRunner.cpp because SuiteTables checks that every
    /// row here resolves to a save-format group, and the host harness runs that suite without
    /// linking the runner (which reaches dirent and the Switch filesystem).
    inline constexpr Enums::GameVersion TESTED_TITLES[] = {
        Enums::GameVersion::RD, Enums::GameVersion::BU, Enums::GameVersion::YW,
        Enums::GameVersion::GD, Enums::GameVersion::SI, Enums::GameVersion::C,
        // Release order puts Emerald AFTER FireRed/LeafGreen -- Ruby/Sapphire are 2002 and
        // both FR/LG (Jan) and Emerald (Sep) are 2004. It reads like a mistake next to the
        // other groupings and is not one; the three Hoenn games are one save-format group
        // wherever that matters, and this table only decides run order and row order.
        Enums::GameVersion::RU, Enums::GameVersion::SA,
        Enums::GameVersion::FR, Enums::GameVersion::LG,
        Enums::GameVersion::EM,
        Enums::GameVersion::D, Enums::GameVersion::P, Enums::GameVersion::Pt,
        Enums::GameVersion::HG, Enums::GameVersion::SS,
        Enums::GameVersion::B, Enums::GameVersion::W,
        Enums::GameVersion::B2, Enums::GameVersion::W2,
        Enums::GameVersion::X, Enums::GameVersion::Y,
        Enums::GameVersion::OR, Enums::GameVersion::AS,
        Enums::GameVersion::SN, Enums::GameVersion::MN,
        Enums::GameVersion::US, Enums::GameVersion::UM,
        Enums::GameVersion::GP, Enums::GameVersion::GE,
        Enums::GameVersion::SW, Enums::GameVersion::SH,
        Enums::GameVersion::BD, Enums::GameVersion::SP,
        Enums::GameVersion::PLA,
        Enums::GameVersion::SL, Enums::GameVersion::VL,
        Enums::GameVersion::ZA,
    };

    inline constexpr int TESTED_TITLE_COUNT = (int)(sizeof(TESTED_TITLES) / sizeof(TESTED_TITLES[0]));

    /// An EMPTY slot. Several games keep a live entity in every slot rather than a null pointer --
    /// SwSh's box writer deliberately stamps a canonical encrypted blank over each unoccupied slot,
    /// because leftover bytes that decrypt to species 0 re-encrypt into a Bad Egg -- so a non-null
    /// pointer does not mean "occupied". Species 0 does, and that is the test the whole UI uses
    /// (`!pokemon || pokemon->speciesID() == 0`). Shared rather than per-suite: getting it wrong once produced
    /// 1849 phantom findings in the entity sweep and 936 more in the save round trip, independently.
    inline bool isEmptySlot(const Pokemon::Pokemon *pokemon) noexcept
    {
        return pokemon == nullptr || pokemon->speciesID() == 0;
    }

    struct Sample
    {
        /// The TITLE this pokemon was taken from -- Sword, Violet, Yellow -- not its generation. The
        /// run tests one save per title, so this is also what caps the pool: SAMPLES_PER_TARGET
        /// Pokemon per title.
        Enums::GameVersion version = Enums::GameVersion::Invalid;
        Enums::GameVersion group = Enums::GameVersion::Invalid;
        std::string origin; // "Sword", for finding detail lines
        std::unique_ptr<Pokemon::Pokemon> pokemon;
    };

    /// How many Pokemon each target contributes. Small on purpose: the conversion matrix is quadratic
    /// in the pool, and four representative Pokemon per title already covers the field remaps.
    inline constexpr int SAMPLES_PER_TARGET = 4;

    /// The pool lives in an inline function-local static rather than in a .cpp, so a caller can
    /// link the suites it wants WITHOUT linking the suite table -- which references every suite,
    /// including the ones that need the Switch SDK. That is what lets the host harness run the
    /// entity / setters / legality / conversion suites against real saves on a PC.
    inline std::vector<Sample> &sampleStore() noexcept
    {
        static std::vector<Sample> pool;
        return pool;
    }

    inline void clearSamples() { sampleStore().clear(); }

    inline void addSample(Enums::GameVersion version, Enums::GameVersion group, const std::string &origin, std::unique_ptr<Pokemon::Pokemon> pokemon)
    {
        if (!pokemon) return;
        sampleStore().push_back(Sample{version, group, origin, std::move(pokemon)});
    }

    inline const std::vector<Sample> &samples() noexcept { return sampleStore(); }

    int tablesPlan(Context &);
    void tablesRun(Context &, int); // generation-independent data tables
    int assetsPlan(Context &);
    void assetsRun(Context &, int); // ROMFS: fonts, type icons, every sprite
    int generationTablesPlan(Context &);
    void generationTablesRun(Context &, int); // per-generation tables (items, learnsets, dex)
    int savePlan(Context &);
    void saveRun(Context &, int); // save structure, round trip, edit persistence
    int entityPlan(Context &);
    void entityRun(Context &, int); // every Pokemon in the save
    int settersPlan(Context &);
    void settersRun(Context &, int); // set -> get on a blank entity, field by field
    int legalityPlan(Context &);
    void legalityRun(Context &, int); // the legality analyzer over the save
    int conversionPlan(Context &);
    void conversionRun(Context &, int); // the cross-generation conversion matrix
    int pokemonFilePlan(Context &);
    void pokemonFileRun(Context &, int); // .pkX export, read back, refusal, bank import
    int bankPlan(Context &);
    void bankRun(Context &, int); // bank deposit / save / reload / verify
    int uiPlan(Context &);
    void uiRun(Context &, int); // DRIVES the app: real screens, synthetic input
    int pickerPlan(Context &);
    void pickerRun(Context &, int); // DRIVES the save picker (console only)
    int fileBrowserPlan(Context &);
    void fileBrowserRun(Context &, int); // DRIVES the file browser over a fixture it builds
}

#endif
