#include "UI/Dialogs/KeyboardDialog.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "UI/TouchInput.h"

namespace UI
{
    namespace Dialogs
    {
        namespace
        {
            // Widths are counted in HALF keys so the wide keys on the bottom row stay integers. A key
            // is TouchTargetMin tall; HALF_KEY_PITCH is half of one standard key plus the gap after it.
            constexpr int KEY_HEIGHT = TouchTargetMin;
            constexpr int KEY_GAP = 6;
            constexpr int HALF_KEY_PITCH = 47;
            constexpr int STANDARD_KEY_HALVES = 2;
            constexpr int KEY_ROW_COUNT = 4;
            constexpr int KEY_CORNER_RADIUS = 10;
            constexpr int KEY_ICON_SIZE = 26;
            constexpr int CARD_PADDING = 16;
            constexpr int CARD_NAV_BAR_GAP = 8;
            constexpr int FIELD_HEIGHT = 48;
            constexpr int FIELD_KEY_GAP = 10;
            constexpr int FIELD_CORNER_RADIUS = 10;
            constexpr int FIELD_INSET = 16;

            constexpr int PRESSED_FRAMES = 6;
            constexpr int REFUSED_FRAMES = 30;
            constexpr int CARET_BLINK_FRAMES = 30;
            constexpr int REPEAT_DELAY_FRAMES = 24;
            constexpr int REPEAT_INTERVAL_FRAMES = 4;

            constexpr u64 DIRECTION_BUTTONS = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right;
            constexpr u64 REPEATABLE_BUTTONS = HidNpadButton_A | HidNpadButton_B | HidNpadButton_L | HidNpadButton_R | DIRECTION_BUTTONS;
            constexpr u64 ANSWER_BUTTONS = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus | HidNpadButton_Minus | DIRECTION_BUTTONS;

            const KeyboardState *g_activeKeyboard = nullptr;

            size_t nextCharacterEnd(const std::string &text, size_t characterStart)
            {
                size_t characterEnd = std::min(characterStart + 1, text.size());
                while (characterEnd < text.size() && (static_cast<unsigned char>(text[characterEnd]) & 0xC0) == 0x80)
                    ++characterEnd;
                return characterEnd;
            }

            size_t previousCharacterStart(const std::string &text, size_t characterEnd)
            {
                if (characterEnd == 0)
                    return 0;
                size_t characterStart = std::min(characterEnd, text.size()) - 1;
                while (characterStart > 0 && (static_cast<unsigned char>(text[characterStart]) & 0xC0) == 0x80)
                    --characterStart;
                return characterStart;
            }

            /// Characters, not bytes: the cap a prompt asks for is the one the games count in.
            int characterCount(const std::string &text)
            {
                int leadByteCount = 0;
                for (const char byte : text)
                    if ((static_cast<unsigned char>(byte) & 0xC0) != 0x80)
                        ++leadByteCount;
                return leadByteCount;
            }

            struct KeyPlan
            {
                KeyboardKeyAction action;
                std::string lowerLabel;
                std::string upperLabel;
                int rowIndex;
                int widthInHalfKeys;
            };

            void addKey(std::vector<KeyPlan> &plans, int rowIndex, KeyboardKeyAction action, const std::string &label, int widthInHalfKeys = STANDARD_KEY_HALVES)
            {
                plans.push_back({action, label, label, rowIndex, widthInHalfKeys});
            }

            /// One Type key per character, pairing each with the character at the same position in
            /// `upperCharacters` -- which is the lower-case string again for a row with no case.
            void addTypeKeys(std::vector<KeyPlan> &plans, int rowIndex, const std::string &lowerCharacters, const std::string &upperCharacters, int widthInHalfKeys = STANDARD_KEY_HALVES)
            {
                size_t lowerStart = 0;
                size_t upperStart = 0;
                while (lowerStart < lowerCharacters.size())
                {
                    const size_t lowerEnd = nextCharacterEnd(lowerCharacters, lowerStart);
                    const size_t upperEnd = nextCharacterEnd(upperCharacters, upperStart);
                    plans.push_back({KeyboardKeyAction::Type, lowerCharacters.substr(lowerStart, lowerEnd - lowerStart),
                                     upperCharacters.substr(upperStart, upperEnd - upperStart), rowIndex,
                                     widthInHalfKeys});
                    lowerStart = lowerEnd;
                    upperStart = upperEnd;
                }
            }

            /// The text pages share their bottom row: the two OTHER pages, the console keyboard,
            /// space and OK. Twenty-two halves, the width of every row above it.
            void addTextBottomRow(std::vector<KeyPlan> &plans, KeyboardKeyAction firstPage, const std::string &firstPageLabel, KeyboardKeyAction secondPage, const std::string &secondPageLabel)
            {
                constexpr int bottomRow = KEY_ROW_COUNT - 1;
                addKey(plans, bottomRow, firstPage, firstPageLabel, 3);
                addKey(plans, bottomRow, secondPage, secondPageLabel, 3);
                // Hiragana, kanji and hangul: the scripts this keyboard has no keys for, and the
                // reason the console's own keyboard is one press away.
                addKey(plans, bottomRow, KeyboardKeyAction::SystemKeyboard, "あ漢한", 3);
                addKey(plans, bottomRow, KeyboardKeyAction::Space, "Space", 8);
                addKey(plans, bottomRow, KeyboardKeyAction::Accept, "OK", 5);
            }

            std::vector<KeyPlan> planPage(KeyboardPage page, bool allowNegative)
            {
                std::vector<KeyPlan> plans;
                switch (page)
                {
                case KeyboardPage::Letters:
                    // The punctuation is what Pokemon names are made of: Farfetch'd, Ho-Oh, Mr. Mime.
                    addTypeKeys(plans, 0, "qwertyuiop", "QWERTYUIOP");
                    addKey(plans, 0, KeyboardKeyAction::Backspace, "");
                    addTypeKeys(plans, 1, "asdfghjkl'-", "ASDFGHJKL'-");
                    addKey(plans, 2, KeyboardKeyAction::Shift, "");
                    addTypeKeys(plans, 2, "zxcvbnm,.?", "ZXCVBNM,.?");
                    addTextBottomRow(plans, KeyboardKeyAction::ShowSymbols, "?123", KeyboardKeyAction::ShowAccents, "àé");
                    break;
                case KeyboardPage::Symbols:
                    addTypeKeys(plans, 0, "1234567890", "1234567890");
                    addKey(plans, 0, KeyboardKeyAction::Backspace, "");
                    addTypeKeys(plans, 1, "!?&/:;()\"#@", "!?&/:;()\"#@");
                    // The gender signs, the ellipsis and the multiplication sign are in the games'
                    // own character sets, which is why they are here rather than further out.
                    addTypeKeys(plans, 2, "♂♀…×~*+=%_$", "♂♀…×~*+=%_$");
                    addTextBottomRow(plans, KeyboardKeyAction::ShowLetters, "ABC", KeyboardKeyAction::ShowAccents, "àé");
                    break;
                case KeyboardPage::Accents:
                    addTypeKeys(plans, 0, "àáâäãåæçèé", "ÀÁÂÄÃÅÆÇÈÉ");
                    addKey(plans, 0, KeyboardKeyAction::Backspace, "");
                    addTypeKeys(plans, 1, "êëìíîïñòóôö", "ÊËÌÍÎÏÑÒÓÔÖ");
                    addKey(plans, 2, KeyboardKeyAction::Shift, "");
                    addTypeKeys(plans, 2, "õøœùúûüýÿß", "ÕØŒÙÚÛÜÝŸß");
                    addTextBottomRow(plans, KeyboardKeyAction::ShowLetters, "ABC", KeyboardKeyAction::ShowSymbols, "?123");
                    break;
                case KeyboardPage::Digits:
                    addTypeKeys(plans, 0, "123", "123", 3);
                    addKey(plans, 0, KeyboardKeyAction::Backspace, "", 3);
                    addTypeKeys(plans, 1, "456", "456", 3);
                    addKey(plans, 1, KeyboardKeyAction::ClearText, "Clear", 3);
                    addTypeKeys(plans, 2, "789", "789", 3);
                    if (allowNegative)
                        addKey(plans, 2, KeyboardKeyAction::ToggleSign, "+/-", 3);
                    addTypeKeys(plans, 3, "0", "0", 6);
                    addKey(plans, 3, KeyboardKeyAction::Accept, "OK", 6);
                    break;
                }
                return plans;
            }

            bool isInside(int pointX, int pointY, int rectX, int rectY, int rectWidth, int rectHeight)
            {
                return pointX >= rectX && pointX < rectX + rectWidth && pointY >= rectY && pointY < rectY + rectHeight;
            }

            /// A tapped key that ends the prompt fires only once its press has been drawn, the deferral
            /// every tapped confirm in PKSE gets. A key that types fires at once: the character
            /// appearing in the field is the feedback.
            bool deferredWhenTapped(KeyboardKeyAction action)
            {
                return action == KeyboardKeyAction::Accept || action == KeyboardKeyAction::SystemKeyboard;
            }

            /// Keys that make sense to hold A on. Holding A on Shift or a page key would flicker it.
            bool repeatsWhenHeld(KeyboardKeyAction action)
            {
                return action == KeyboardKeyAction::Type || action == KeyboardKeyAction::Backspace || action == KeyboardKeyAction::Space;
            }
        }

        const KeyboardState *activeKeyboard() { return g_activeKeyboard; }
        void setActiveKeyboard(const KeyboardState *keyboard) { g_activeKeyboard = keyboard; }

        void KeyboardState::open(const Utils::KeyboardRequest &request, int screenWidth, int screenHeight)
        {
            this->request = request;
            this->screenWidth = screenWidth;
            this->screenHeight = screenHeight;
            text = request.initialText;
            caretOffset = text.size();
            textSelected = request.digitsOnly && !text.empty();
            shift = ShiftMode::Off;
            finished = false;
            accepted = false;
            caretStopX.clear();
            caretStopOffsets.clear();
            measuredText.clear();
            textScroll = 0;
            caretBlinkFrames = 0;
            pressedKeyIndex = -1;
            pressedFrames = 0;
            pendingKeyIndex = -1;
            pendingFrames = 0;
            refusedFrames = 0;
            repeatingButton = 0;
            repeatHeldFrames = 0;
            analogNavigation.reset();
            waitingForRelease = false;
            keys.clear();
            selectedKeyIndex = 0;
            buildPage(request.digitsOnly ? KeyboardPage::Digits : KeyboardPage::Letters);
        }

        void KeyboardState::buildPage(KeyboardPage newPage)
        {
            // A page change keeps the cursor on the same row, as near as the new keys allow.
            const bool keepSelection = !keys.empty() && selectedKeyIndex >= 0 && selectedKeyIndex < static_cast<int>(keys.size());
            const int selectedRow = keepSelection ? keys[selectedKeyIndex].rowIndex : 0;
            const int selectedCenterX = keepSelection ? keys[selectedKeyIndex].keyX + keys[selectedKeyIndex].keyWidth / 2 : 0;

            page = newPage;
            const std::vector<KeyPlan> plans = planPage(newPage, request.allowNegative);

            // Rows start at the widest row's left edge rather than each being centred, so a short row
            // -- the number pad's third, without a sign key -- keeps its columns under the ones above.
            int widestRowHalves = 0;
            for (int rowIndex = 0; rowIndex < KEY_ROW_COUNT; ++rowIndex)
            {
                int rowHalves = 0;
                for (const KeyPlan &plan : plans)
                    if (plan.rowIndex == rowIndex)
                        rowHalves += plan.widthInHalfKeys;
                widestRowHalves = std::max(widestRowHalves, rowHalves);
            }
            const int keyBlockWidth = widestRowHalves * HALF_KEY_PITCH - KEY_GAP;

            cardWidth = keyBlockWidth + 2 * CARD_PADDING;
            cardHeight = CARD_PADDING + FIELD_HEIGHT + FIELD_KEY_GAP + KEY_ROW_COUNT * KEY_HEIGHT + (KEY_ROW_COUNT - 1) * KEY_GAP + CARD_PADDING;
            cardX = (screenWidth - cardWidth) / 2;
            cardY = screenHeight - NAV_BAR_HEIGHT - CARD_NAV_BAR_GAP - cardHeight;
            fieldX = cardX + CARD_PADDING;
            fieldY = cardY + CARD_PADDING;
            fieldWidth = keyBlockWidth;
            fieldHeight = FIELD_HEIGHT;

            const int keyRowsTop = fieldY + FIELD_HEIGHT + FIELD_KEY_GAP;
            keys.clear();
            keys.reserve(plans.size());
            int rowCursorX = fieldX;
            int previousRow = -1;
            for (const KeyPlan &plan : plans)
            {
                if (plan.rowIndex != previousRow)
                {
                    rowCursorX = fieldX;
                    previousRow = plan.rowIndex;
                }
                KeyboardKey key;
                key.action = plan.action;
                key.lowerLabel = plan.lowerLabel;
                key.upperLabel = plan.upperLabel;
                key.rowIndex = plan.rowIndex;
                key.keyX = rowCursorX;
                key.keyY = keyRowsTop + plan.rowIndex * (KEY_HEIGHT + KEY_GAP);
                key.keyWidth = plan.widthInHalfKeys * HALF_KEY_PITCH - KEY_GAP;
                key.keyHeight = KEY_HEIGHT;
                keys.push_back(std::move(key));
                rowCursorX += plan.widthInHalfKeys * HALF_KEY_PITCH;
            }

            selectedKeyIndex = 0;
            if (keepSelection)
            {
                int nearestDistance = INT_MAX;
                for (int keyIndex = 0; keyIndex < static_cast<int>(keys.size()); ++keyIndex)
                {
                    if (keys[keyIndex].rowIndex != selectedRow)
                        continue;
                    const int distance = std::abs(keys[keyIndex].keyX + keys[keyIndex].keyWidth / 2 - selectedCenterX);
                    if (distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        selectedKeyIndex = keyIndex;
                    }
                }
            }
            pressedKeyIndex = -1;
            pressedFrames = 0;
        }

        u64 KeyboardState::repeatedPresses(u64 pressedButtons, u64 heldButtons)
        {
            const u64 freshButtons = pressedButtons & REPEATABLE_BUTTONS;
            if (freshButtons != 0)
            {
                repeatingButton = freshButtons & (~freshButtons + 1); // the lowest one, if several
                repeatHeldFrames = 0;
                return 0;
            }
            if ((heldButtons & repeatingButton) == 0)
            {
                repeatingButton = 0;
                return 0;
            }
            ++repeatHeldFrames;
            if (repeatHeldFrames < REPEAT_DELAY_FRAMES)
                return 0;
            return (repeatHeldFrames - REPEAT_DELAY_FRAMES) % REPEAT_INTERVAL_FRAMES == 0 ? repeatingButton : 0;
        }

        void KeyboardState::update(const PadState &pad, const TouchInput &touch)
        {
            if (finished)
                return;
            ++caretBlinkFrames;
            if (pressedFrames > 0 && --pressedFrames == 0)
                pressedKeyIndex = -1;
            if (refusedFrames > 0)
                --refusedFrames;

            // Sampled every frame, so a stick push made while a wait below is running is seen as held
            // rather than arriving later as a fresh press.
            const HidAnalogStickState stick = padGetStickPos(&pad, 0);
            const AnalogNavigationSample analog = analogNavigation.sample(stick.x, stick.y, HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right);
            const u64 pressedButtons = padGetButtonsDown(&pad) | analog.down;
            const u64 heldButtons = padGetButtons(&pad) | analog.held;

            if (waitingForRelease)
            {
                if ((heldButtons & ANSWER_BUTTONS) != 0 || touch.isDown())
                    return;
                waitingForRelease = false;
            }

            if (pendingKeyIndex >= 0)
            {
                if (--pendingFrames > 0)
                    return;
                const int keyIndex = pendingKeyIndex;
                pendingKeyIndex = -1;
                activateKey(keyIndex);
                return;
            }

            const u64 buttonsDown = pressedButtons | navTouchButton(touch);
            const u64 buttonsActing = buttonsDown | repeatedPresses(pressedButtons, heldButtons);

            if (buttonsDown & HidNpadButton_Plus)
            {
                finish(true);
                return;
            }
            if (buttonsDown & HidNpadButton_Minus)
            {
                finish(false);
                return;
            }

            if (touch.justPressed())
            {
                for (int keyIndex = 0; keyIndex < static_cast<int>(keys.size()); ++keyIndex)
                {
                    const KeyboardKey &key = keys[keyIndex];
                    if (!isInside(touch.x(), touch.y(), key.keyX, key.keyY, key.keyWidth, key.keyHeight))
                        continue;
                    selectedKeyIndex = keyIndex;
                    pressedKeyIndex = keyIndex;
                    pressedFrames = PRESSED_FRAMES;
                    if (deferredWhenTapped(key.action))
                    {
                        pendingKeyIndex = keyIndex;
                        pendingFrames = TAP_PRESS_FRAMES;
                    }
                    else
                    {
                        activateKey(keyIndex);
                    }
                    return;
                }
                // A tap in the field puts the caret at the nearest character boundary.
                if (isInside(touch.x(), touch.y(), fieldX, fieldY, fieldWidth, fieldHeight) && !caretStopX.empty())
                {
                    const int tapTextX = touch.x() - textOriginX;
                    size_t nearestStop = 0;
                    for (size_t stopIndex = 1; stopIndex < caretStopX.size(); ++stopIndex)
                        if (std::abs(caretStopX[stopIndex] - tapTextX) < std::abs(caretStopX[nearestStop] - tapTextX))
                            nearestStop = stopIndex;
                    caretOffset = caretStopOffsets[nearestStop];
                    textSelected = false;
                    caretBlinkFrames = 0;
                    return;
                }
            }

            if (buttonsActing & HidNpadButton_A)
            {
                const bool freshPress = (buttonsDown & HidNpadButton_A) != 0;
                if (freshPress || repeatsWhenHeld(keys[selectedKeyIndex].action))
                {
                    pressedKeyIndex = selectedKeyIndex;
                    pressedFrames = PRESSED_FRAMES;
                    activateKey(selectedKeyIndex);
                    if (finished)
                        return;
                }
            }
            if (buttonsActing & HidNpadButton_B)
            {
                // Holding B deletes back to an empty field and stops there. Only a fresh press on an
                // empty field leaves, so a held B cannot run on through the text and out of the prompt.
                if (!text.empty())
                {
                    deleteBeforeCaret();
                }
                else if (buttonsDown & HidNpadButton_B)
                {
                    finish(false);
                    return;
                }
            }
            if (page != KeyboardPage::Digits)
            {
                if (buttonsDown & HidNpadButton_Y)
                    insertText(" ");
                if ((buttonsDown & HidNpadButton_X) && page != KeyboardPage::Symbols)
                    shift = (shift == ShiftMode::Off) ? ShiftMode::Once : ShiftMode::Off;
                if (buttonsDown & (HidNpadButton_ZL | HidNpadButton_ZR))
                {
                    constexpr KeyboardPage textPages[] = {KeyboardPage::Letters, KeyboardPage::Symbols,
                                                          KeyboardPage::Accents};
                    constexpr int textPageCount = 3;
                    const int pageStep = (buttonsDown & HidNpadButton_ZR) ? 1 : textPageCount - 1;
                    buildPage(textPages[(static_cast<int>(page) + pageStep) % textPageCount]);
                }
            }
            if (buttonsActing & HidNpadButton_L)
                moveCaret(-1);
            if (buttonsActing & HidNpadButton_R)
                moveCaret(1);
            if (buttonsActing & HidNpadButton_Up)
                moveSelection(-1, 0);
            if (buttonsActing & HidNpadButton_Down)
                moveSelection(1, 0);
            if (buttonsActing & HidNpadButton_Left)
                moveSelection(0, -1);
            if (buttonsActing & HidNpadButton_Right)
                moveSelection(0, 1);
        }

        void KeyboardState::activateKey(int keyIndex)
        {
            if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size()))
                return;
            // Copied rather than referenced: a page key rebuilds `keys` underneath it.
            const KeyboardKey key = keys[keyIndex];
            switch (key.action)
            {
            case KeyboardKeyAction::Type:
                if (insertText(shift != ShiftMode::Off ? key.upperLabel : key.lowerLabel) && shift == ShiftMode::Once)
                    shift = ShiftMode::Off;
                break;
            case KeyboardKeyAction::Backspace:
                deleteBeforeCaret();
                break;
            case KeyboardKeyAction::Shift:
                shift = shift == ShiftMode::Off ? ShiftMode::Once : shift == ShiftMode::Once ? ShiftMode::Locked : ShiftMode::Off;
                break;
            case KeyboardKeyAction::Space:
                insertText(" ");
                break;
            case KeyboardKeyAction::Accept:
                finish(true);
                break;
            case KeyboardKeyAction::ClearText:
                if (!text.empty())
                {
                    text.clear();
                    caretOffset = 0;
                    textSelected = false;
                    textEdited();
                }
                break;
            case KeyboardKeyAction::ToggleSign:
                toggleSign();
                break;
            case KeyboardKeyAction::ShowLetters:
                buildPage(KeyboardPage::Letters);
                break;
            case KeyboardKeyAction::ShowSymbols:
                buildPage(KeyboardPage::Symbols);
                break;
            case KeyboardKeyAction::ShowAccents:
                buildPage(KeyboardPage::Accents);
                break;
            case KeyboardKeyAction::SystemKeyboard:
                handOffToSystemKeyboard();
                break;
            }
        }

        void KeyboardState::moveSelection(int rowStep, int columnStep)
        {
            if (keys.empty())
                return;
            const KeyboardKey &selectedKey = keys[selectedKeyIndex];
            if (columnStep != 0)
            {
                // Keys are stored row by row, so a row is one contiguous run; the ends wrap.
                int rowFirstIndex = selectedKeyIndex;
                while (rowFirstIndex > 0 && keys[rowFirstIndex - 1].rowIndex == selectedKey.rowIndex)
                    --rowFirstIndex;
                int rowLastIndex = selectedKeyIndex;
                while (rowLastIndex + 1 < static_cast<int>(keys.size()) && keys[rowLastIndex + 1].rowIndex == selectedKey.rowIndex)
                    ++rowLastIndex;
                int targetIndex = selectedKeyIndex + columnStep;
                if (targetIndex < rowFirstIndex)
                    targetIndex = rowLastIndex;
                if (targetIndex > rowLastIndex)
                    targetIndex = rowFirstIndex;
                selectedKeyIndex = targetIndex;
                return;
            }
            // Up and down land on the key nearest the selected one's centre, and wrap top to bottom.
            const int rowCount = keys.back().rowIndex + 1;
            const int targetRow = (selectedKey.rowIndex + rowStep + rowCount) % rowCount;
            const int selectedCenterX = selectedKey.keyX + selectedKey.keyWidth / 2;
            int nearestIndex = selectedKeyIndex;
            int nearestDistance = INT_MAX;
            for (int keyIndex = 0; keyIndex < static_cast<int>(keys.size()); ++keyIndex)
            {
                if (keys[keyIndex].rowIndex != targetRow)
                    continue;
                const int distance = std::abs(keys[keyIndex].keyX + keys[keyIndex].keyWidth / 2 - selectedCenterX);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestIndex = keyIndex;
                }
            }
            selectedKeyIndex = nearestIndex;
        }

        bool KeyboardState::insertText(const std::string &characters)
        {
            if (textSelected)
            {
                text.clear();
                caretOffset = 0;
                textSelected = false;
            }
            // A digit typed in front of a minus sign would make the number unreadable.
            if (request.digitsOnly && caretOffset == 0 && !text.empty() && text[0] == '-')
                caretOffset = 1;
            if (characterCount(text) + characterCount(characters) > request.maxChars)
            {
                refusedFrames = REFUSED_FRAMES;
                return false;
            }
            text.insert(caretOffset, characters);
            caretOffset += characters.size();
            textEdited();
            return true;
        }

        void KeyboardState::deleteBeforeCaret()
        {
            if (textSelected)
            {
                text.clear();
                caretOffset = 0;
                textSelected = false;
                textEdited();
                return;
            }
            if (caretOffset == 0)
                return;
            const size_t characterStart = previousCharacterStart(text, caretOffset);
            text.erase(characterStart, caretOffset - characterStart);
            caretOffset = characterStart;
            textEdited();
        }

        void KeyboardState::moveCaret(int characterStep)
        {
            if (textSelected)
            {
                textSelected = false;
                caretOffset = characterStep < 0 ? 0 : text.size();
            }
            else
            {
                caretOffset = characterStep < 0 ? previousCharacterStart(text, caretOffset) : nextCharacterEnd(text, caretOffset);
            }
            caretBlinkFrames = 0;
        }

        void KeyboardState::textEdited()
        {
            caretBlinkFrames = 0;
            if (request.textChanged)
                request.textChanged(text);
        }

        void KeyboardState::toggleSign()
        {
            textSelected = false;
            if (!text.empty() && text[0] == '-')
            {
                text.erase(0, 1);
                if (caretOffset > 0)
                    --caretOffset;
            }
            else
            {
                if (characterCount(text) + 1 > request.maxChars)
                {
                    refusedFrames = REFUSED_FRAMES;
                    return;
                }
                text.insert(0, "-");
                ++caretOffset;
            }
            textEdited();
        }

        void KeyboardState::handOffToSystemKeyboard()
        {
            const Utils::KeyboardResult typed = Utils::promptSystemText(request.header, request.guide, text, request.maxChars);
            // The buttons that answered the console's keyboard can still be down when it returns.
            // They were meant for it, and must not arrive here as fresh presses.
            waitingForRelease = true;
            if (!typed.accepted)
                return;
            text = typed.text;
            caretOffset = text.size();
            textSelected = false;
            textEdited();
            // The console's keyboard was just answered with its own OK; asking for a second one here
            // would be two confirmations for one answer.
            finish(true);
        }

        void KeyboardState::finish(bool acceptText)
        {
            finished = true;
            accepted = acceptText;
        }

        Utils::KeyboardResult KeyboardState::result() const
        {
            Utils::KeyboardResult keyboardResult;
            keyboardResult.accepted = accepted;
            if (accepted)
                keyboardResult.text = text;
            return keyboardResult;
        }

        std::string KeyboardState::navHint() const
        {
            if (page == KeyboardPage::Digits)
                return "Arrows: Move  |  A: Type  |  B: Delete  |  L/R: Cursor  |  +: OK  |  -: Cancel";
            if (page == KeyboardPage::Symbols)
                return "Arrows: Move  |  A: Type  |  B: Delete  |  Y: Space  |  L/R: Cursor  |  ZL/ZR: Layout  |  +: OK  |  -: Cancel";
            return "Arrows: Move  |  A: Type  |  B: Delete  |  Y: Space  |  X: Shift  |  L/R: Cursor  |  ZL/ZR: Layout  |  +: OK  |  -: Cancel";
        }

        namespace
        {
            void drawField(KeyboardState &keyboard, PKSEFramebuffer &framebuffer)
            {
                framebuffer.drawFilledRoundedRect(keyboard.fieldX, keyboard.fieldY, keyboard.fieldWidth, keyboard.fieldHeight, FIELD_CORNER_RADIUS, Colors::PanelAlt);
                framebuffer.drawRoundedRect(keyboard.fieldX, keyboard.fieldY, keyboard.fieldWidth, keyboard.fieldHeight, FIELD_CORNER_RADIUS, Colors::Accent, 2);
                const int fieldCenterY = keyboard.fieldY + keyboard.fieldHeight / 2;

                int headerWidth = 0, headerHeight = 0;
                framebuffer.measureText(keyboard.request.header, headerWidth, headerHeight, TextStyle::Caption);
                const int headerX = keyboard.fieldX + FIELD_INSET;
                framebuffer.drawText(headerX, framebuffer.textYCenteredOn(fieldCenterY, TextStyle::Caption), keyboard.request.header, Colors::Accent, TextStyle::Caption);
                const int dividerX = headerX + headerWidth + 14;
                framebuffer.drawFilledRect(dividerX, keyboard.fieldY + 10, 1, keyboard.fieldHeight - 20, Colors::Border);

                // Right-aligned: how full the field is -- or, for a number, the range it must fall in,
                // since a count of digits says nothing useful there.
                const std::string tally = keyboard.request.digitsOnly ? keyboard.request.guide : std::to_string(characterCount(keyboard.text)) + " / " + std::to_string(keyboard.request.maxChars);
                int tallyWidth = 0, tallyHeight = 0;
                framebuffer.measureText(tally, tallyWidth, tallyHeight, TextStyle::Caption);
                const int tallyX = keyboard.fieldX + keyboard.fieldWidth - FIELD_INSET - tallyWidth;
                framebuffer.drawText(tallyX, framebuffer.textYCenteredOn(fieldCenterY, TextStyle::Caption), tally, keyboard.refusedFrames > 0 ? Colors::ShinyStar : Colors::TextDim, TextStyle::Caption);

                const int textLeft = dividerX + 14;
                const int textAreaWidth = std::max(0, tallyX - 16 - textLeft);

                // Every caret position's x, re-measured only when the text changes.
                if (keyboard.measuredText != keyboard.text || keyboard.caretStopX.empty())
                {
                    keyboard.caretStopX.clear();
                    keyboard.caretStopOffsets.clear();
                    size_t stopOffset = 0;
                    while (true)
                    {
                        int prefixWidth = 0, prefixHeight = 0;
                        if (stopOffset > 0)
                            framebuffer.measureText(keyboard.text.substr(0, stopOffset), prefixWidth, prefixHeight, TextStyle::Body);
                        keyboard.caretStopX.push_back(prefixWidth);
                        keyboard.caretStopOffsets.push_back(stopOffset);
                        if (stopOffset >= keyboard.text.size())
                            break;
                        stopOffset = nextCharacterEnd(keyboard.text, stopOffset);
                    }
                    keyboard.measuredText = keyboard.text;
                }
                int caretTextX = 0;
                for (size_t stopIndex = 0; stopIndex < keyboard.caretStopOffsets.size(); ++stopIndex)
                    if (keyboard.caretStopOffsets[stopIndex] == keyboard.caretOffset)
                        caretTextX = keyboard.caretStopX[stopIndex];
                const int textWidth = keyboard.caretStopX.back();

                // Scroll only as far as it takes to keep the caret in view.
                if (textWidth <= textAreaWidth)
                    keyboard.textScroll = 0;
                else if (caretTextX - keyboard.textScroll > textAreaWidth)
                    keyboard.textScroll = caretTextX - textAreaWidth;
                else if (caretTextX < keyboard.textScroll)
                    keyboard.textScroll = caretTextX;
                keyboard.textOriginX = textLeft - keyboard.textScroll;

                const int lineHeight = framebuffer.lineHeight(TextStyle::Body);
                const int textY = framebuffer.textYCenteredOn(fieldCenterY, TextStyle::Body);
                framebuffer.setClipRect(textLeft - 2, keyboard.fieldY, textAreaWidth + 4, keyboard.fieldHeight);
                if (keyboard.text.empty())
                {
                    if (!keyboard.request.digitsOnly)
                        framebuffer.drawText(textLeft, textY, keyboard.request.guide, Colors::TextDim);
                }
                else
                {
                    if (keyboard.textSelected)
                        framebuffer.drawFilledRoundedRect(keyboard.textOriginX - 4, fieldCenterY - lineHeight / 2 - 3, textWidth + 8, lineHeight + 6, 6, Colors::Selected);
                    framebuffer.drawText(keyboard.textOriginX, textY, keyboard.text, Colors::Text);
                }
                // Solid for a beat after every edit or move, then blinking -- so a caret that has just
                // moved is never caught invisible.
                const bool caretShown = (keyboard.caretBlinkFrames / CARET_BLINK_FRAMES) % 2 == 0;
                if (caretShown && !keyboard.textSelected)
                    framebuffer.drawFilledRect(keyboard.textOriginX + caretTextX - 1, fieldCenterY - lineHeight / 2 - 2, 2, lineHeight + 4, Colors::Accent);
                framebuffer.clearClip();
            }

            void drawKey(const KeyboardState &keyboard, PKSEFramebuffer &framebuffer, int keyIndex)
            {
                const KeyboardKey &key = keyboard.keys[keyIndex];
                const bool selected = keyIndex == keyboard.selectedKeyIndex;
                const bool pressed = keyIndex == keyboard.pressedKeyIndex;

                // Keys that type sit on the lighter surface; the function keys are set back on the
                // darker one, the same split the console's keyboard makes. OK is the one amber
                // element, as the primary action is everywhere else in PKSE.
                Color fillColor = Colors::Background;
                Color labelColor = Colors::Text;
                TextStyle labelStyle = TextStyle::Body;
                // A fill that says something -- OK, or Shift while it is on -- survives selection; every
                // other key takes the selection fill when the cursor is on it.
                bool fillShowsState = false;
                switch (key.action)
                {
                case KeyboardKeyAction::Type:
                    fillColor = Colors::PanelAlt;
                    labelStyle = TextStyle::Heading;
                    break;
                case KeyboardKeyAction::Space:
                    fillColor = Colors::PanelAlt;
                    labelColor = Colors::TextDim;
                    labelStyle = TextStyle::Caption;
                    break;
                case KeyboardKeyAction::Accept:
                    fillColor = Colors::Primary;
                    labelColor = Colors::PrimaryText;
                    labelStyle = TextStyle::Heading;
                    fillShowsState = true;
                    break;
                case KeyboardKeyAction::Shift:
                    if (keyboard.shift == ShiftMode::Locked)
                    {
                        fillColor = Colors::Accent;
                        labelColor = Colors::Panel;
                        fillShowsState = true;
                    }
                    else if (keyboard.shift == ShiftMode::Once)
                    {
                        fillColor = Colors::AccentDim;
                        fillShowsState = true;
                    }
                    break;
                default:
                    break;
                }
                if (selected && !fillShowsState)
                    fillColor = Colors::Selected;

                framebuffer.drawFilledRoundedRect(key.keyX, key.keyY, key.keyWidth, key.keyHeight, KEY_CORNER_RADIUS, fillColor);
                if (pressed)
                {
                    Color pressTint = Colors::Primary;
                    pressTint.alpha = 110;
                    framebuffer.drawFilledRoundedRect(key.keyX, key.keyY, key.keyWidth, key.keyHeight, KEY_CORNER_RADIUS, pressTint);
                    framebuffer.drawRoundedRect(key.keyX, key.keyY, key.keyWidth, key.keyHeight, KEY_CORNER_RADIUS, Colors::Primary, 3);
                }
                else if (selected)
                {
                    framebuffer.drawRoundedRect(key.keyX, key.keyY, key.keyWidth, key.keyHeight, KEY_CORNER_RADIUS, Colors::Accent, 3);
                }
                else
                {
                    framebuffer.drawRoundedRect(key.keyX, key.keyY, key.keyWidth, key.keyHeight, KEY_CORNER_RADIUS, Colors::Border, 1);
                }

                const int keyCenterX = key.keyX + key.keyWidth / 2;
                const int keyCenterY = key.keyY + key.keyHeight / 2;
                if (key.action == KeyboardKeyAction::Backspace)
                {
                    framebuffer.drawBackspaceIcon(keyCenterX - KEY_ICON_SIZE / 2, keyCenterY - KEY_ICON_SIZE / 2, KEY_ICON_SIZE, labelColor);
                    return;
                }
                if (key.action == KeyboardKeyAction::Shift)
                {
                    framebuffer.drawShiftIcon(keyCenterX - KEY_ICON_SIZE / 2, keyCenterY - KEY_ICON_SIZE / 2, KEY_ICON_SIZE, labelColor, keyboard.shift != ShiftMode::Off);
                    // Caps lock underlines the arrow, the mark the console's own keyboard uses.
                    if (keyboard.shift == ShiftMode::Locked)
                        framebuffer.drawFilledRoundedRect(keyCenterX - 8, keyCenterY + KEY_ICON_SIZE / 2 + 2, 16, 3, 1, labelColor);
                    return;
                }
                const bool shifted = keyboard.shift != ShiftMode::Off;
                const std::string &label = shifted ? key.upperLabel : key.lowerLabel;
                int labelWidth = 0, labelHeight = 0;
                framebuffer.measureText(label, labelWidth, labelHeight, labelStyle);
                framebuffer.drawText(keyCenterX - labelWidth / 2, framebuffer.textYCenteredOn(keyCenterY, labelStyle), label, labelColor, labelStyle);
            }
        }

        void drawKeyboard(KeyboardState &keyboard, PKSEFramebuffer &framebuffer)
        {
            framebuffer.drawSoftShadow(keyboard.cardX, keyboard.cardY, keyboard.cardWidth, keyboard.cardHeight, CHROME_CORNER_RADIUS);
            framebuffer.drawFilledRoundedRect(keyboard.cardX, keyboard.cardY, keyboard.cardWidth, keyboard.cardHeight, CHROME_CORNER_RADIUS, Colors::Panel);
            framebuffer.drawRoundedRect(keyboard.cardX, keyboard.cardY, keyboard.cardWidth, keyboard.cardHeight, CHROME_CORNER_RADIUS, Colors::Border, 1);
            drawField(keyboard, framebuffer);
            for (int keyIndex = 0; keyIndex < static_cast<int>(keyboard.keys.size()); ++keyIndex)
                drawKey(keyboard, framebuffer, keyIndex);
        }
    }
}
