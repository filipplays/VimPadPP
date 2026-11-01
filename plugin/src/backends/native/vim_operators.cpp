#include "vim_engine.h"

#include "ScintillaMessages.h"

#include <algorithm>

namespace
{
LRESULT Send(const ScintillaEditor &sEditor, UINT nMessage, WPARAM wParam = 0, LPARAM lParam = 0)
{
    return sEditor.Send(nMessage, wParam, lParam);
}
} // namespace

void VimEngine::ApplyOperator(MotionResult sMotion)
{
    m_sUnnamedRegister         = ReadRange(sMotion.nStart, sMotion.nEnd);
    m_bUnnamedRegisterLinewise = sMotion.bLinewise;
    if (m_ePendingOperator == Command::Yank || sMotion.nEnd <= sMotion.nStart)
        return;

    const LRESULT nColumn = Caret() - LineStart(Caret());
    Send(m_hEditor, Scintilla::BEGINUNDOACTION);
    Send(m_hEditor, Scintilla::DELETERANGE, sMotion.nStart, sMotion.nEnd - sMotion.nStart);
    Send(m_hEditor, Scintilla::ENDUNDOACTION);
    LRESULT nDestination = std::min(sMotion.nStart, DocumentLength());
    if (sMotion.bLinewise && m_ePendingOperator == Command::Delete)
    {
        const LRESULT nLineEnd = LineEnd(nDestination);
        const LRESULT nLastCharacter =
            nLineEnd > LineStart(nDestination) ? Send(m_hEditor, Scintilla::POSITIONBEFORE, nLineEnd) : nLineEnd;
        nDestination = std::min(nDestination + nColumn, nLastCharacter);
    }
    Send(m_hEditor, Scintilla::GOTOPOS, nDestination);
    if (m_ePendingOperator == Command::Change)
        SetMode(Mode::Insert);
}

void VimEngine::PasteUnnamedRegister(bool bAfter, int nCount)
{
    if (m_sUnnamedRegister.empty())
        return;

    std::string sText     = m_sUnnamedRegister;
    LRESULT     nPosition = Caret();
    if (m_bUnnamedRegisterLinewise)
    {
        if (sText.back() != '\n')
            sText.push_back('\n');
        const LRESULT nLine      = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nPosition);
        const LRESULT nLineCount = Send(m_hEditor, Scintilla::GETLINECOUNT);
        if (bAfter && nLine + 1 < nLineCount)
            nPosition = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nLine + 1);
        else if (bAfter)
        {
            nPosition = DocumentLength();
            if (nPosition > 0 && CharacterAt(nPosition - 1) != '\n')
            {
                static constexpr char kNewline[] = "\n";
                Send(m_hEditor, Scintilla::INSERTTEXT, nPosition, reinterpret_cast<LPARAM>(kNewline));
                ++nPosition;
            }
        }
        else
            nPosition = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nLine);
    }
    else if (bAfter)
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);

    Send(m_hEditor, Scintilla::BEGINUNDOACTION);
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        Send(m_hEditor, Scintilla::INSERTTEXT, nPosition, reinterpret_cast<LPARAM>(sText.c_str()));
        nPosition += static_cast<LRESULT>(sText.size());
    }
    Send(m_hEditor, Scintilla::ENDUNDOACTION);
    Send(
        m_hEditor, Scintilla::GOTOPOS,
        m_bUnnamedRegisterLinewise ? nPosition - static_cast<LRESULT>(sText.size()) : nPosition - 1);
}

std::string VimEngine::ReadRange(LRESULT nStart, LRESULT nEnd) const
{
    std::string sText;
    // Registers store bytes, including every byte of CRLF and UTF-8 sequences.
    for (LRESULT nPosition = nStart; nPosition < nEnd; ++nPosition)
        sText.push_back(static_cast<char>(CharacterAt(nPosition)));
    return sText;
}
