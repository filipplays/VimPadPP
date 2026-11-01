#include "vim_engine.h"

#include "ScintillaMessages.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace
{
LRESULT Send(const ScintillaEditor &sEditor, UINT nMessage, WPARAM wParam = 0, LPARAM lParam = 0)
{
    return sEditor.Send(nMessage, wParam, lParam);
}
} // namespace

void VimEngine::HandleMotionCommand(Command eCommand, int nCount)
{
    if (m_eParserState == ParserState::OperatorPending)
    {
        if (const auto sMotion = ResolveMotion(eCommand, nCount))
            ApplyOperator(*sMotion);
        ResetParser();
        return;
    }
    const bool bPreserveColumn = eCommand == Command::Down || eCommand == Command::Up ||
                                 eCommand == Command::DocumentStart || eCommand == Command::DocumentEnd;
    if (bPreserveColumn)
    {
        if (!m_nPreferredColumn.has_value())
            m_nPreferredColumn = Caret() - LineStart(Caret());
    }
    if (const auto sMotion = ResolveMotion(eCommand, nCount))
    {
        if (eCommand == Command::LineEnd)
            // End-of-line is a column goal, even across differently sized
            // lines.
            m_nPreferredColumn = std::numeric_limits<LRESULT>::max();
        else if (!bPreserveColumn)
            m_nPreferredColumn.reset();
        LRESULT       nDestination = sMotion->nDestination;
        const LRESULT nLineEnd     = LineEnd(nDestination);
        if (eCommand != Command::WordBackward && nDestination == nLineEnd && nDestination > LineStart(nDestination))
            nDestination = Send(m_hEditor, Scintilla::POSITIONBEFORE, nDestination);
        Send(m_hEditor, Scintilla::GOTOPOS, nDestination);
    }
    ResetParser();
}

std::optional<VimEngine::MotionResult> VimEngine::ResolveMotion(Command eCommand, int nCount) const
{
    if (nCount == 0)
        nCount = eCommand == Command::DocumentEnd ? static_cast<int>(Send(m_hEditor, Scintilla::GETLINECOUNT)) : 1;
    const LRESULT nStart       = Caret();
    LRESULT       nDestination = nStart;
    bool          bLinewise    = false;
    switch (eCommand)
    {
    case Command::Left:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
        {
            if (nDestination == LineStart(nDestination))
                break;
            nDestination = Send(m_hEditor, Scintilla::POSITIONBEFORE, nDestination);
        }
        break;
    case Command::Right:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = Send(m_hEditor, Scintilla::POSITIONAFTER, nDestination);
        break;
    case Command::WordForward:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = NextWordStart(nDestination);
        break;
    case Command::WordBackward:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = PreviousWordStart(nDestination);
        break;
    case Command::WordEnd:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = WordEnd(nDestination);
        break;
    case Command::WORDForward:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = NextWORDStart(nDestination);
        if (nDestination == DocumentLength() && nDestination > 0 &&
            !std::isspace(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nDestination))))
            nDestination = Send(m_hEditor, Scintilla::POSITIONBEFORE, nDestination);
        break;
    case Command::WORDBackward:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = PreviousWORDStart(nDestination);
        break;
    case Command::WORDEnd:
        for (int nIndex = 0; nIndex < nCount; ++nIndex)
            nDestination = WORDEnd(nDestination);
        break;
    case Command::LineStart:
        nDestination = LineStart(nStart);
        break;
    case Command::LineEnd:
    {
        const LRESULT nLine     = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nStart);
        const LRESULT nLastLine = Send(m_hEditor, Scintilla::GETLINECOUNT) - 1;
        // A counted $ needs a following line before it can advance. Once
        // advancing, it stops at EOF when fewer than count - 1 lines remain.
        if (nCount > 1 && nLine == nLastLine)
            return std::nullopt;
        nDestination = Send(m_hEditor, Scintilla::GETLINEENDPOSITION, std::min(nLine + nCount - 1, nLastLine));
        break;
    }
    case Command::DocumentStart:
    case Command::DocumentEnd:
    case Command::Down:
    case Command::Up:
    {
        const LRESULT nLine      = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nStart);
        const LRESULT nLineCount = Send(m_hEditor, Scintilla::GETLINECOUNT);
        const LRESULT nTargetLine =
            eCommand == Command::DocumentStart || eCommand == Command::DocumentEnd
                ? std::min(static_cast<LRESULT>(nCount) - 1, nLineCount - 1)
                : std::clamp(
                      nLine + (eCommand == Command::Down ? nCount : -nCount), static_cast<LRESULT>(0), nLineCount - 1);
        const LRESULT nColumn      = m_nPreferredColumn.value_or(nStart - LineStart(nStart));
        const LRESULT nTargetStart = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nTargetLine);
        const LRESULT nTargetEnd   = Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nTargetLine);
        nDestination               = nTargetStart + std::min(nColumn, nTargetEnd - nTargetStart);
        bLinewise                  = true;
        break;
    }
    default:
        return std::nullopt;
    }
    LRESULT nRangeStart = std::min(nStart, nDestination);
    LRESULT nRangeEnd   = std::max(nStart, nDestination);
    if (bLinewise)
    {
        const LRESULT nFirstLine = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nRangeStart);
        const LRESULT nLastLine  = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nRangeEnd);
        nRangeStart              = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nFirstLine);
        nRangeEnd                = nLastLine + 1 < Send(m_hEditor, Scintilla::GETLINECOUNT)
                                       ? Send(m_hEditor, Scintilla::POSITIONFROMLINE, nLastLine + 1)
                                       : DocumentLength();
    }
    if (eCommand == Command::Right || eCommand == Command::WordEnd || eCommand == Command::WORDEnd)
        nRangeEnd = Send(m_hEditor, Scintilla::POSITIONAFTER, nRangeEnd);
    return MotionResult{nDestination, nRangeStart, std::min(nRangeEnd, DocumentLength()), bLinewise};
}
