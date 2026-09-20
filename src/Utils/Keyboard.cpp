#include "Utils/Keyboard.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <switch.h>

#include "Utils/Logger.h"

namespace Utils {
    namespace {
        // swkbd's length cap is in characters, but the output buffer it fills is UTF-8, where one
        // character costs up to 4 bytes. Sizing the buffer from the character cap alone would
        // truncate any non-ASCII name mid-sequence.
        constexpr int kBytesPerChar = 4;

        struct InlineState {
            SwkbdInline keyboard{};
            bool created = false;
            bool active = false;
            bool initialized = false;
            bool changed = false;
            bool accepted = false;
            bool cancelled = false;
            std::string text;
        };

        InlineState inlineState;

        void inlineInitialized() {
            inlineState.initialized = true;
        }

        void inlineChanged(const char* text, SwkbdChangedStringArg*) {
            if (!inlineState.active) return;
            inlineState.text = text ? text : "";
            inlineState.changed = true;
        }

        void inlineAccepted(const char* text, SwkbdDecidedEnterArg*) {
            if (!inlineState.active) return;
            inlineState.text = text ? text : "";
            inlineState.accepted = true;
        }

        void inlineCancelled() {
            if (inlineState.active) inlineState.cancelled = true;
        }

        s32 utf8CharacterCount(const std::string& text) {
            s32 count = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80) ++count;
            return count;
        }

        /**
         * Common swkbd plumbing. Returns false on cancel OR on applet failure -- the caller treats
         * both as "no change", which is right either way: a failed applet must not be allowed to
         * overwrite the user's existing text with an empty string.
         */
        bool runKeyboard(SwkbdType type, const std::string& header, const std::string& guide,
                         const std::string& initial, int maxChars, std::string& out) {
            if (maxChars <= 0) return false;

            SwkbdConfig kbd;
            Result rc = swkbdCreate(&kbd, 0);
            if (R_FAILED(rc)) {
                logErrorToFile("swkbdCreate failed");
                return false;
            }

            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetType(&kbd, type);
            swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(maxChars));
            swkbdConfigSetStringLenMin(&kbd, 0);           // allow clearing the field entirely
            if (!header.empty()) swkbdConfigSetHeaderText(&kbd, header.c_str());
            if (!guide.empty())  swkbdConfigSetGuideText(&kbd, guide.c_str());
            if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());

            // +1 for the NUL the applet writes.
            std::vector<char> buf(static_cast<size_t>(maxChars) * kBytesPerChar + 1, '\0');
            rc = swkbdShow(&kbd, buf.data(), buf.size());
            swkbdClose(&kbd);

            if (R_FAILED(rc)) return false;               // cancelled (the common case) or failed
            buf.back() = '\0';                            // belt and braces before constructing
            out.assign(buf.data());
            return true;
        }
    }

    bool beginInlineText(const std::string& initial, int maxChars) {
        if (maxChars <= 0) return false;
        closeInlineText();

        Result rc = swkbdInlineCreate(&inlineState.keyboard);
        if (R_FAILED(rc)) {
            logErrorToFile("swkbdInlineCreate failed");
            return false;
        }
        inlineState.created = true;
        swkbdInlineSetFinishedInitializeCallback(&inlineState.keyboard, inlineInitialized);

        rc = swkbdInlineLaunchForLibraryApplet(
            &inlineState.keyboard, SwkbdInlineMode_AppletDisplay, 0);
        if (R_FAILED(rc)) {
            logErrorToFile("swkbdInlineLaunchForLibraryApplet failed");
            closeInlineText();
            return false;
        }
        inlineState.active = true;
        inlineState.text = initial;
        inlineState.text = initial;
        swkbdInlineSetChangedStringCallback(&inlineState.keyboard, inlineChanged);
        swkbdInlineSetDecidedEnterCallback(&inlineState.keyboard, inlineAccepted);
        swkbdInlineSetDecidedCancelCallback(&inlineState.keyboard, inlineCancelled);

        SwkbdAppearArg appearArg;
        swkbdInlineMakeAppearArg(&appearArg, SwkbdType_QWERTY);
        swkbdInlineAppearArgSetOkButtonText(&appearArg, "Done");
        swkbdInlineAppearArgSetStringLenMax(&appearArg, maxChars);
        swkbdInlineAppearArgSetStringLenMin(&appearArg, 0);
        swkbdInlineSetInputText(&inlineState.keyboard, initial.c_str());
        swkbdInlineSetCursorPos(&inlineState.keyboard, utf8CharacterCount(initial));
        swkbdInlineAppear(&inlineState.keyboard, &appearArg);

        rc = swkbdInlineUpdate(&inlineState.keyboard, nullptr);
        if (R_FAILED(rc)) {
            logErrorToFile("initial swkbdInlineUpdate failed");
            closeInlineText();
            return false;
        }
        return true;
    }

    InlineKeyboardUpdate updateInlineText() {
        InlineKeyboardUpdate update;
        if (!inlineState.active) return update;

        SwkbdState state = SwkbdState_Inactive;
        const Result rc = swkbdInlineUpdate(&inlineState.keyboard, &state);
        if (R_FAILED(rc)) {
            logErrorToFile("swkbdInlineUpdate failed");
            update.event = InlineKeyboardEvent::Failed;
            update.text = inlineState.text;
            closeInlineText();
            return update;
        }

        update.text = inlineState.text;
        if (inlineState.cancelled) {
            update.event = InlineKeyboardEvent::Cancelled;
            closeInlineText();
        } else if (inlineState.accepted) {
            update.event = InlineKeyboardEvent::Accepted;
            closeInlineText();
        } else if (inlineState.changed) {
            inlineState.changed = false;
            update.event = InlineKeyboardEvent::Changed;
        } else if (inlineState.initialized && state == SwkbdState_Inactive) {
            logErrorToFile("inline keyboard exited unexpectedly");
            update.event = InlineKeyboardEvent::Failed;
            closeInlineText();
        }
        return update;
    }

    void closeInlineText() {
        if (inlineState.created) {
            const Result rc = swkbdInlineClose(&inlineState.keyboard);
            if (R_FAILED(rc)) logErrorToFile("swkbdInlineClose failed");
        }
        inlineState.created = false;
        inlineState.active = false;
        inlineState.initialized = false;
        inlineState.changed = false;
        inlineState.accepted = false;
        inlineState.cancelled = false;
        inlineState.text.clear();
    }

    KeyboardResult promptText(const std::string& header, const std::string& guide,
                              const std::string& initial, int maxChars) {
        KeyboardResult r;
        r.accepted = runKeyboard(SwkbdType_QWERTY, header, guide, initial, maxChars, r.text);
        if (!r.accepted) r.text.clear();
        return r;
    }

    NumberResult promptNumber(const std::string& header, int initial, int minValue, int maxValue) {
        NumberResult r;
        if (minValue > maxValue) return r;

        // Width the field to the largest value that is actually allowed, so the keypad can't be
        // used to type a number the caller would only have to reject afterwards.
        const int digits = static_cast<int>(std::to_string(std::max(std::abs(minValue), std::abs(maxValue))).size());

        std::string text;
        if (!runKeyboard(SwkbdType_NumPad, header, std::to_string(minValue) + " - " + std::to_string(maxValue),
                         std::to_string(initial), digits, text))
            return r;

        // An empty field is a deliberate "no change" rather than 0 -- otherwise cancelling by
        // clearing the field would silently zero an item count.
        if (text.empty()) return r;

        errno = 0;
        char* end = nullptr;
        const long v = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || errno == ERANGE) return r;   // not a number at all

        r.value = static_cast<int>(std::clamp<long>(v, minValue, maxValue));
        r.accepted = true;
        return r;
    }
}
