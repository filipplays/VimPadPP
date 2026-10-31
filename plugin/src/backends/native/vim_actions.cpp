#include "VimEngine.h"

#include "ScintillaMessages.h"

namespace
{
LRESULT Send(
    const ScintillaEditor &sEditor, UINT nMessage, WPARAM wParam = 0,
    LPARAM lParam = 0)
{
    return sEditor.Send(nMessage, wParam, lParam);
}
} // namespace

void VimEngine::ExecuteAction(Command eCommand, int nCount)
{
    const LRESULT nPosition = Caret();
    switch (eCommand)
    {
    case Command::InsertBefore:
        SetMode(Mode::Insert);
        break;
    case Command::InsertAfter:
        Send(
            m_hEditor, Scintilla::GOTOPOS,
            CharacterAt(nPosition) == '\n'
                ? nPosition
                : Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition));
        SetMode(Mode::Insert);
        break;
    case Command::InsertLineStart:
        Send(m_hEditor, Scintilla::GOTOPOS, LineStart(nPosition));
        SetMode(Mode::Insert);
        break;
    case Command::InsertLineEnd:
        Send(m_hEditor, Scintilla::GOTOPOS, LineEnd(nPosition));
        SetMode(Mode::Insert);
        break;
    case Command::OpenBelow:
        Send(m_hEditor, Scintilla::GOTOPOS, LineEnd(nPosition));
        Send(m_hEditor, Scintilla::NEWLINE);
        SetMode(Mode::Insert);
        break;
    case Command::OpenAbove:
        Send(m_hEditor, Scintilla::GOTOPOS, LineStart(nPosition));
        Send(m_hEditor, Scintilla::NEWLINE);
        Send(m_hEditor, Scintilla::GOTOPOS, LineStart(nPosition));
        SetMode(Mode::Insert);
        break;
    case Command::DeleteCharacter:
        for (int nIndex = 0; nIndex < nCount && Caret() < DocumentLength();
             ++nIndex)
        {
            Send(
                m_hEditor, Scintilla::DELETERANGE, Caret(),
                Send(m_hEditor, Scintilla::POSITIONAFTER, Caret()) - Caret());
        }
        break;
    case Command::PasteAfter:
        PasteUnnamedRegister(true, nCount);
        break;
    case Command::PasteBefore:
        PasteUnnamedRegister(false, nCount);
        break;
    default:
        break;
    }
}
