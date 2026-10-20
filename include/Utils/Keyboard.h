#ifndef UTILS_KEYBOARD_H
#define UTILS_KEYBOARD_H

#include <functional>
#include <string>

namespace Utils {
    /**
     * Text and number prompts.
     *
     * PKSE draws its own keyboard: UIManager registers a KeyboardPresenter, which shows a themed
     * keyboard over the screen that raised the prompt and keeps that screen drawing underneath it,
     * so a search can filter its list while the query is still being typed. The console's own
     * keyboard (swkbd) is kept as promptSystemText() -- what the on-screen one hands off to for
     * scripts it has no keys for -- and as the fallback when no presenter is registered.
     *
     * Either way the call BLOCKS until the prompt is answered. Three rules follow, and all are
     * load-bearing:
     *
     *  - **Call it only from a screen's update(), never from draw().** The UI loop runs
     *    update() -> draw() -> flush(), so at update() time no NanoVG frame is open. The presenter
     *    draws whole frames of its own, and swkbd suspends the app; either one started mid-draw
     *    would leave a half-built frame open underneath it.
     *  - **Never launch swkbd twice in one frame.** Launching a second system keyboard immediately
     *    after the first returns (both inside one update()) crashes on hardware: a library applet
     *    can't be torn down and relaunched back-to-back. The on-screen keyboard is not an applet and
     *    has no such limit, but promptSystemText() still is one.
     *  - **Treat a cancel as "no change", not as an empty string.** Returning "" for a cancel would
     *    blank whatever the user was editing, which is the opposite of what they asked for.
     */
    struct KeyboardResult {
        bool accepted = false;   // false = cancelled or the keyboard failed; `text` is then empty
        std::string text;        // UTF-8, as typed (NOT yet validated against a game's encoding)
    };

    /// Everything one prompt asks of whichever keyboard shows it.
    struct KeyboardRequest {
        std::string header;
        std::string guide;          ///< shown in the empty field, in place of text
        std::string initialText;
        int maxChars = 0;           ///< characters, not bytes -- one character is up to 4 UTF-8 bytes
        bool digitsOnly = false;    ///< a number pad rather than a keyboard
        bool allowNegative = false; ///< digitsOnly only: offer a sign key
        /// Runs after every edit while the keyboard is open, with the text as it now stands -- how a
        /// search re-filters under the keyboard. Empty when the caller only wants the final answer.
        std::function<void(const std::string &)> textChanged;
    };

    /// Shows a keyboard for `request` and blocks until it is answered. Registered by the UI, which
    /// is the layer that can draw one; cleared again when the UI goes away.
    using KeyboardPresenter = std::function<KeyboardResult(const KeyboardRequest &request)>;
    void setKeyboardPresenter(KeyboardPresenter presenter);

    KeyboardResult promptText(const std::string& header, const std::string& guide, const std::string& initial, int maxChars, const std::function<void(const std::string &)> &textChanged = nullptr);

    /**
     * Show the numeric keypad, seeded with `initial`. Returns the parsed value clamped to
     * [minValue, maxValue]; `accepted` is false on cancel or if the text wasn't a number.
     */
    struct NumberResult {
        bool accepted = false;
        int value = 0;
    };
    NumberResult promptNumber(const std::string& header, int initial, int minValue, int maxValue);

    /// The console's own keyboard, whatever presenter is registered. It is the only way to type
    /// Japanese, Chinese or Korean -- PKSE's keyboard carries no input method -- and it is a library
    /// applet, so the frame rule above applies to it in full.
    KeyboardResult promptSystemText(const std::string& header, const std::string& guide, const std::string& initial, int maxChars);

    /// How many prompts have been REFUSED because `g_suppressKeyboard` was set (see Globals.h).
    ///
    /// A suppressed prompt is not a neutral event: it means something reached a keyboard that, on a
    /// console with a human in front of it, would have stopped and waited. During the self test that
    /// is a journey doing something it did not intend, so the count is read per step and reported.
    /// Monotonic within a run; `resetKeyboardSuppressionCount()` zeroes it.
    int keyboardPromptsSuppressed();
    void resetKeyboardSuppressionCount();
}

#endif
