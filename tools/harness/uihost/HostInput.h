#ifndef PKSE_UIHOST_INPUT_H
#define PKSE_UIHOST_INPUT_H

#include <string>

/**
 * The two input channels the harness has to supply itself.
 *
 * BUTTONS are not here: a PadState is a plain struct and libnx's readers are constexpr reads over
 * it, so the driver builds one directly and the screens cannot tell the difference. Touch and the
 * software keyboard are different -- both are console services with no struct to fill in -- so each
 * gets a host implementation of the same wrapper the .nro calls, fed from a script.
 */
namespace UIHost
{

    /// What TouchInput::update() will observe on the next frame. The driver sets it; the host
    /// TouchInput.cpp reads it and derives the same press/hold/release edges the real one does.
    void setTouch(bool touchDown, int touchX, int touchY);

    /// Queue one answer for the next Utils::promptText() call -- the equivalent of cy.type().
    /// swkbd BLOCKS the app on console, so a queued answer models exactly that: the call returns
    /// immediately with this text, as though the user had typed it and pressed OK.
    void queueText(const std::string &text);
    /// Queue one answer for the next Utils::promptNumber().
    void queueNumber(int enteredValue);
    /// Make the next prompt come back cancelled. Worth testing on its own: a cancel must mean
    /// "no change", and treating it as an empty string would blank the field being edited.
    void queueCancel();

    /// How many queued keyboard answers were never consumed -- a test that queued input the UI
    /// never asked for did not do what it thought it did.
    int pendingPromptCount();
    void clearPrompts();
    /// How many prompts the UI has raised so far this run.
    int raisedPromptCount();

    /// What the next save commit reports back. The write itself is console-only, but the UI's
    /// handling of both outcomes is not -- see HostStubs.cpp.
    void setSaveResult(bool saveSucceeds);
    int saveCallCount();
    void resetSaveCallCount();
}

#endif
