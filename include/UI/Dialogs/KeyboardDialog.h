/**
 * PKSE's own keyboard: every text and number prompt, drawn in the app's theme instead of handing the
 * screen to the console's keyboard applet.
 *
 * The console's keyboard owns the whole display while it is up, so nothing PKSE draws can react to
 * what is being typed. This one is a card over the bottom of the screen and the screen above it keeps
 * drawing -- which is what lets a search filter its list while the query is still being typed.
 *
 * UIManager runs it (see UI.cpp): Utils::promptText hands it a request, and it pumps frames of its
 * own -- the screen that asked, then this card -- until the prompt is answered. State lives in
 * KeyboardState; this file owns the layout, the input and the drawing.
 */
#ifndef UI_DIALOGS_KEYBOARD_DIALOG_H
#define UI_DIALOGS_KEYBOARD_DIALOG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <switch.h>

#include "UI/NavigationRepeat.h"
#include "Utils/Keyboard.h"

namespace UI
{
    class PKSEFramebuffer;
    class TouchInput;

    namespace Dialogs
    {
        /// Which keys the card is showing. Digits is a number prompt's pad; the other three are the
        /// text pages ZL/ZR cycle through.
        enum class KeyboardPage : uint8_t
        {
            Letters,
            Symbols,
            Accents,
            Digits
        };

        enum class KeyboardKeyAction : uint8_t
        {
            Type, // inserts its label
            Backspace,
            Shift,
            Space,
            Accept,
            ClearText,
            ToggleSign,
            ShowLetters,
            ShowSymbols,
            ShowAccents,
            SystemKeyboard // hands the text to the console's keyboard, for scripts with no keys here
        };

        /// Shift is one-shot unless pressed twice: Once applies to the next letter only, Locked
        /// until pressed again.
        enum class ShiftMode : uint8_t
        {
            Off,
            Once,
            Locked
        };

        /// One key of the current page, laid out in screen pixels.
        struct KeyboardKey
        {
            KeyboardKeyAction action = KeyboardKeyAction::Type;
            std::string lowerLabel;
            std::string upperLabel; // what a shifted key types; the same as lowerLabel for most keys
            int rowIndex = 0;
            int keyX = 0, keyY = 0, keyWidth = 0, keyHeight = 0;
        };

        struct KeyboardState
        {
            /// Same as TrainerViewScreen::TAP_PRESS_FRAMES -- a tap must feel the same on every screen.
            static constexpr int TAP_PRESS_FRAMES = 4;

            Utils::KeyboardRequest request;
            std::string text;
            /// Byte offset of the caret into `text`, always on a character boundary.
            size_t caretOffset = 0;
            /// The whole text is selected, so the next character typed replaces it. A number prompt
            /// opens this way: the old value is almost never the start of the new one.
            bool textSelected = false;

            KeyboardPage page = KeyboardPage::Letters;
            ShiftMode shift = ShiftMode::Off;
            std::vector<KeyboardKey> keys; // the current page, row by row
            int selectedKeyIndex = 0;

            bool finished = false;
            bool accepted = false;

            /// Lays out the card for a screen of this size and seeds it from `request`.
            void open(const Utils::KeyboardRequest &request, int screenWidth, int screenHeight);
            /// One frame of input. Does nothing once `finished` is set.
            void update(const PadState &pad, const TouchInput &touch);
            Utils::KeyboardResult result() const;

            /// The controls, for whichever nav bar is on screen while the card owns input.
            std::string navHint() const;
            /// The card's top edge. A list that must stay readable while the user types keeps its
            /// rows above this.
            int cardTop() const noexcept { return cardY; }

            // Geometry, set by open() -- update() hit-tests against it, so it cannot wait for a draw.
            int screenWidth = 0, screenHeight = 0;
            int cardX = 0, cardY = 0, cardWidth = 0, cardHeight = 0;
            int fieldX = 0, fieldY = 0, fieldWidth = 0, fieldHeight = 0;

            // Captured by drawKeyboard() and read by the NEXT update(), the one-frame-late contract
            // every touch target here keeps: where each caret position falls, measured from the start
            // of the text, and where on screen that start is drawn.
            std::vector<int> caretStopX;
            std::vector<size_t> caretStopOffsets;
            std::string measuredText; // the text caretStopX was measured for
            int textScroll = 0;       // pixels the field's text is shifted left to keep the caret in view
            int textOriginX = 0;

            // Frame counters.
            int caretBlinkFrames = 0; // since the last edit or caret move; the caret holds solid after one
            int pressedKeyIndex = -1; // the key drawn held, and for how long
            int pressedFrames = 0;
            int pendingKeyIndex = -1; // a tapped key that closes the card, waiting for its press to show
            int pendingFrames = 0;
            int refusedFrames = 0;    // the character counter flashes after a character did not fit
            u64 repeatingButton = 0;  // the held button that auto-repeats, and for how long it has been held
            int repeatHeldFrames = 0;
            /// The left stick, read as one D-pad direction at a time so it steps the selection like the
            /// D-pad and repeats on the same timing.
            AnalogNavigation analogNavigation;
            /// Set when the console's keyboard hands back control: the buttons used to answer it may
            /// still be held, and must not arrive here as fresh presses.
            bool waitingForRelease = false;

        private:
            void buildPage(KeyboardPage newPage);
            void activateKey(int keyIndex);
            void moveSelection(int rowStep, int columnStep);
            u64 repeatedPresses(u64 pressedButtons, u64 heldButtons);
            bool insertText(const std::string &characters);
            void deleteBeforeCaret();
            void moveCaret(int characterStep);
            void textEdited();
            void toggleSign();
            void handOffToSystemKeyboard();
            void finish(bool acceptText);
        };

        /// Draws the card. Captures the field's caret positions for the next update().
        void drawKeyboard(KeyboardState &keyboard, PKSEFramebuffer &framebuffer);

        /// The keyboard on screen right now, or nullptr. Set by UIManager for as long as its loop
        /// runs, so a screen drawn underneath can name the keyboard's controls in its nav bar and keep
        /// a list clear of the card.
        const KeyboardState *activeKeyboard();
        void setActiveKeyboard(const KeyboardState *keyboard);
    }
}

#endif
