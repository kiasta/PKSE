#ifndef GLOBALS_H
#define GLOBALS_H

#include <cstdint>
#include <string>

/// Derived from the Makefile's APP_VERSION_FULL via -DPKSE_VERSION. That is the long spelling; the
/// .nacp separately carries APP_VERSION, an abbreviation of it, because its display_version field
/// holds only 15 characters. Both are set side by side in the Makefile, so the two can differ in
/// how much of the version they show but not in which version it is.
///
/// The fallback is deliberately loud rather than a plausible-looking number. This header is also
/// parsed by the Visual Studio projects (IntelliSense only, never the real build), which don't pass
/// the define; a fallback like "0.0.3" would silently look correct while being a guess.
///
/// Deliberately `const` and not `constexpr`. A constant-evaluated std::string has to fit inside
/// libstdc++'s small-string buffer -- 15 characters -- because anything longer heap-allocates, and
/// an allocation cannot escape constant evaluation into the runtime. Any pre-release tag blows past
/// that, and the fallback below is already sitting exactly on 15, so `constexpr` here was one
/// character away from breaking on its own. Nothing evaluates this at compile time anyway.
#ifndef PKSE_VERSION
#define PKSE_VERSION "0.0.0-NOVERSION"
#endif
inline const std::string VERSION_STRING = PKSE_VERSION;

inline constexpr std::string BASE_SAVE_DIRECTORY = "sdmc:/PKSE";
/// Where Storage's Export writes Pokemon files, and the folder the import browser offers. Not
/// constexpr: a constant-evaluated std::string must fit the 15-character small-string buffer.
inline const std::string POKEMON_EXPORT_DIRECTORY = BASE_SAVE_DIRECTORY + "/exports";

inline constexpr uint32_t SIZE_HASH_IN_BYTES = 32;

/// Settings lock for writing an OLDER BACKUP over the live game save, persisted. Default OFF.
///
/// Scope is deliberately narrow. Saving a session that was loaded FROM the live save back TO it is
/// always allowed and needs no toggle — that is simply what a save editor does, and the data being
/// overwritten is the data that was just read. What this gates is the other case: opening a backup
/// from some earlier point and writing it over the current save, which rolls the game backwards and
/// loses everything played since. That is the only thing worth a lock, and it also raises a separate
/// confirmation at save time.
///
/// (Replaces a hard-coded `constexpr bool SAVE_TO_TITLE = true`, under which every save overwrote
/// the live save with no way to opt out.)
inline bool g_injectToGameSave = false;

/// Runtime setting (toggled in the in-app Settings screen, persisted to settings.cfg): when true,
/// "Load from Title" creates a new timestamped backup each time (kept indefinitely — backups are
/// never auto-pruned; deleting them is the user's decision). When false, it reuses a single
/// "Working" copy so backups don't pile up.
inline bool g_autoBackupEnabled = true;
inline bool g_allowIllegalEdits = false; // Settings toggle: lift the legal EV/AV caps (0-252 / 0-200 -> 0-255) so
                                         // illegal values can be set (e.g. to test the legality checker).

// Settings toggle: when a transfer has NO officially supported route (Gen 1/2 -> Gen 3, say),
// rebuild the Pokemon as a legal native of the destination -- derive its met location from a real
// encounter there, walk the pre-evolution chain, stamp the version that can actually produce it,
// fall back to "hatched from an egg" for a species the game never has, and drop moves that
// encounter could not have taught. OFF by default, because it REWRITES a Pokemon the user may
// prefer to fix by hand; off, PKSE only does the plain lookup and leaves an obvious placeholder.
//
// It never alters a Pokemon's LEVEL, and it never touches an officially supported transfer --
// Ruby/Sapphire/Emerald and FireRed/LeafGreen trade with each other on real hardware, so PKSE
// does there exactly what the cartridges do and leaves the record alone.
inline bool g_autoLegalizeTransfers = false;
/// Settings toggle ("Bank Storage Move Warning"): confirm before a bank move that loses data. It
/// covers EVERY bank transfer warning, in every generation -- the Gen 3 down-convert (rebuilds the
/// PID), the Poke Transporter run out of Gen 1/2 (rewrites IVs, PID, EXP and ability) and the Let's
/// Go transfer (resets AV/EV training to 0).
///
/// One switch over all three. Exempting the destructive ones would make the setting quietly
/// narrower than its label -- a user who had turned warnings off would still be stopped on every
/// FireRed drop, with nothing on the settings screen to say why. How destructive a transfer is
/// decides what each DIALOG says, not whether a dialog appears.
inline bool g_moveWarn = true;

/// Settings toggle ("Enable Debug Logging"), persisted. Default OFF, and every log sink checks it:
/// with it off PKSE writes no `sdmc:/PKSE/logs/debug_*.log` and no `sdmc:/PKSE/trace.log`, so a
/// normal run leaves nothing on the user's card. Turn it on, reproduce the problem, send the log.
///
/// Replaces a compile-time switch (`-DPKSE_PROD`, via `make ... prod`), which had three problems a
/// runtime flag does not: the shipped binary was not the binary that was tested, switching modes
/// needed a `make clean`, and a user who hit a bug on a release could not produce a log at all
/// without building their own. Being a normal setting, it is also readable by anything that wants
/// to know whether logging is on, which the preprocessor define was not.
inline bool g_debugLogging = false;

/// True while the on-console self test is driving the UI. Set by Test::Runner for the length of a
/// run and by nothing else.
///
/// It exists for ONE reason: `Utils::promptText` / `promptNumber` BLOCK until a human answers --
/// PKSE's own keyboard runs modally until OK or Cancel, and the console's keyboard it can hand off to
/// is a library applet. An automated pass that reaches one stops dead and waits for hands on the
/// console -- which is the opposite of what the pass is for, and it happened.
/// While this is set, both prompts return "not accepted" immediately (a cancel, which every call
/// site already handles as "no change") and count the attempt, so a run can neither hang nor
/// silently pretend the user typed something.
///
/// The journeys are written to avoid the prompts, and this is deliberately NOT a substitute for
/// that: `Utils::keyboardPromptsSuppressed()` is checked per step, and a journey that reaches a
/// keyboard is reported. This is the guarantee; the journeys' care is the intent. The same
/// reasoning as `injectToTitle(false)` and the scratch bank -- a test pass gets a hard floor under
/// it, not a promise that every step is well behaved.
inline bool g_suppressKeyboard = false;

/// Whether this build carries the on-console self test. Only the development build does
/// (`make BUILD_TYPE=development`): it defines PKSE_DEVELOPMENT_BUILD and compiles src/Test/. The
/// default production build has neither, so a release carries no self test. The hooks the test
/// drives -- g_suppressKeyboard, Bank::setRootOverride -- are in both builds and inert when nothing
/// sets them. UI.cpp asks the same #ifdef, because it must not include the header otherwise.
#ifdef PKSE_DEVELOPMENT_BUILD
inline constexpr bool SELF_TEST_AVAILABLE = true;
#else
inline constexpr bool SELF_TEST_AVAILABLE = false;
#endif

/// Number of days to retain debug log files
inline constexpr uint8_t LOG_RETENTION_DAYS = 30;

#endif