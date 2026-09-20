#ifndef UTILS_KEYBOARD_H
#define UTILS_KEYBOARD_H

#include <string>

namespace Utils {
    /**
     * Switch software-keyboard (swkbd) wrapper.
     *
     * promptText() and promptNumber() use the all-foreground applet: the call blocks and PKSE is
     * suspended for as long as the keyboard owns the screen. Three rules follow for those helpers:
     *
     *  - **Call it only from a screen's update(), never from draw().** The UI loop runs
     *    update() -> draw() -> flush(), so at update() time no NanoVG frame is open. Calling it
     *    mid-draw would leave a half-built frame open across the suspend.
     *  - **At most ONE prompt per frame -- never chain them.** Launching a second prompt immediately
     *    after the first returns (both inside one update()) crashes on hardware: a library applet
     *    can't be torn down and relaunched back-to-back. For multi-field input (e.g. a date), use a
     *    single combined field, or spread the prompts across frames.
     *  - **Treat a cancel as "no change", not as an empty string.** Returning "" for a cancel would
     *    blank whatever the user was editing, which is the opposite of what they asked for.
     */
    struct KeyboardResult {
        bool accepted = false;   ///< false = cancelled or the applet failed; `text` is then empty
        std::string text;        ///< UTF-8, as typed (NOT yet validated against a game's encoding)
    };

    enum class InlineKeyboardEvent {
        None,
        Changed,
        Accepted,
        Cancelled,
        Failed
    };

    struct InlineKeyboardUpdate {
        InlineKeyboardEvent event = InlineKeyboardEvent::None;
        std::string text;
    };

    /**
     * Launch the native keyboard as a background applet so the application keeps rendering.
     * Call updateInlineText() once per frame until it reports Accepted, Cancelled, or Failed.
     */
    bool beginInlineText(const std::string& initial, int maxChars);
    InlineKeyboardUpdate updateInlineText();
    void closeInlineText();

    /**
     * Show the text keyboard, seeded with `initial`.
     *
     * @param header    Title line shown above the field (e.g. "Rename Box").
     * @param guide     Greyed-out hint shown in the empty field (e.g. "Box name").
     * @param initial   Text the field starts with, so editing beats retyping.
     * @param maxChars  Character limit, which is what the games specify -- the UTF-8 byte buffer is
     *                  sized from it internally, since one character can take up to 4 bytes.
     *
     * The returned text is whatever the user typed. It is NOT checked against the destination
     * game's character set -- Gen 3 in particular can represent only a subset -- so the caller must
     * still encode it and handle the characters that don't map.
     */
    KeyboardResult promptText(const std::string& header, const std::string& guide,
                              const std::string& initial, int maxChars);

    /**
     * Show the numeric keypad, seeded with `initial`. Returns the parsed value clamped to
     * [minValue, maxValue]; `accepted` is false on cancel or if the text wasn't a number.
     */
    struct NumberResult {
        bool accepted = false;
        int value = 0;
    };
    NumberResult promptNumber(const std::string& header, int initial, int minValue, int maxValue);
}

#endif
