#include "vim_engine.h"

#include "ScintillaMessages.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
LRESULT Send(const ScintillaEditor &sEditor, UINT nMessage, WPARAM wParam = 0, LPARAM lParam = 0)
{
    return sEditor.Send(nMessage, wParam, lParam);
}
} // namespace

VimEngine::VimEngine(HWND hEditor) : m_hEditor(hEditor)
{
    RegisterCommands();
    m_nInsertCaretPeriod = Send(m_hEditor, Scintilla::GETCARETPERIOD);
    SetMode(Mode::Normal);
}

VimEngine::Mode VimEngine::GetMode() const { return m_eMode; }

bool VimEngine::HandleCharacter(unsigned char cKey, bool bControl, bool bAlt)
{
    if (bAlt)
    {
        return true;
    }
    if (bControl && cKey == 'c')
    {
        Cancel();
        return true;
    }
    if (m_eMode != Mode::Normal)
    {
        return false;
    }

    bool bResult = true;

    switch (m_eParserState)
    {
    case ParserState::Normal:
        bResult = HandleNormalCharacter(cKey);
        break;
    case ParserState::OperatorPending:
        bResult = HandleOperatorPendingCharacter(cKey);
        break;
    case ParserState::AwaitingCharacter:
        bResult = HandleAwaitedCharacter(cKey);
        break;
    }

    std::cout << "Caret: " << Caret() << " Character ASCII: " << static_cast<unsigned int>(CharacterAt(Caret()))
              << std::endl;

    return bResult;
}

bool VimEngine::HandleNormalCharacter(unsigned char cKey)
{
    if (m_ePendingPrefix.has_value())
    {
        return HandlePrefixedCharacter(cKey);
    }
    if (IsCountCharacter(cKey))
    {
        AppendCountDigit(cKey);
        return true;
    }
    const auto itCommand = m_aCommands.find(cKey);
    if (itCommand == m_aCommands.end())
    {
        ResetParser();
        return true;
    }
    const auto &sCommand = itCommand->second;
    // A prefix owns the count until its complete command is known.
    const int   nCount   = sCommand.eKind == CommandKind::Prefix ? 0 : TakeCount();
    DispatchCommand(sCommand, nCount);
    return true;
}

bool VimEngine::HandleOperatorPendingCharacter(unsigned char cKey)
{
    if (m_ePendingPrefix.has_value())
        return HandlePrefixedCharacter(cKey);
    if (IsCountCharacter(cKey))
    {
        AppendCountDigit(cKey);
        return true;
    }
    const auto itCommand = m_aCommands.find(cKey);
    if (itCommand != m_aCommands.end() && itCommand->second.eKind == CommandKind::Prefix)
    {
        HandlePrefixCommand(itCommand->second.eCommand);
        return true;
    }
    const int nCount = TakeCount();
    if ((m_ePendingOperator == Command::Delete && cKey == 'd') ||
        (m_ePendingOperator == Command::Change && cKey == 'c') || (m_ePendingOperator == Command::Yank && cKey == 'y'))
    {
        const LRESULT nStart     = LineStart(Caret());
        const LRESULT nLine      = Send(m_hEditor, Scintilla::LINEFROMPOSITION, Caret());
        const LRESULT nLineCount = Send(m_hEditor, Scintilla::GETLINECOUNT);
        const LRESULT nEnd       = nLine + std::max(1, nCount) < nLineCount
                                       ? Send(m_hEditor, Scintilla::POSITIONFROMLINE, nLine + std::max(1, nCount))
                                       : DocumentLength();
        ApplyOperator(MotionResult{nStart, nStart, nEnd, true});
    }
    else if (itCommand != m_aCommands.end() && itCommand->second.eKind == CommandKind::Motion)
    {
        HandleMotionCommand(itCommand->second.eCommand, nCount);
    }
    else if (
        itCommand != m_aCommands.end() && itCommand->second.eKind == CommandKind::NeedsCharacter &&
        itCommand->second.eCommand != Command::ReplaceCharacter)
    {
        m_eAwaitingCommand     = itCommand->second.eCommand;
        m_bAwaitingForOperator = true;
        m_eParserState         = ParserState::AwaitingCharacter;
        return true;
    }
    ResetParser();
    return true;
}

bool VimEngine::HandleAwaitedCharacter(unsigned char cKey)
{
    ExecuteAwaitedCharacter(cKey);
    ResetParser();
    return true;
}

bool VimEngine::HandlePrefixedCharacter(unsigned char cKey)
{
    // Registering 'g' records Command::Go rather than a generic flag. The
    // next character is therefore interpreted by the handler for that exact
    // prefix: `gg` reaches the document start. To add `g_`, add a new motion
    // command and a `case Command::Go` branch below. A distinct prefix such as
    // `z` follows the same pattern: register it as Prefix with Command::Zoom,
    // then add a Command::Zoom case that handles `zt`, `zz`, and `zb`.
    switch (*m_ePendingPrefix)
    {
    case Command::Go:
        if (cKey == 'g')
        {
            HandleMotionCommand(Command::DocumentStart, TakeCount());
        }
        break;
    default:
        break;
    }
    ResetParser();
    return true;
}

void VimEngine::DispatchCommand(const CommandMetadata &sCommand, int nCount)
{
    switch (sCommand.eKind)
    {
    case CommandKind::Action:
        HandleActionCommand(sCommand.eCommand, nCount);
        break;
    case CommandKind::Motion:
        HandleMotionCommand(sCommand.eCommand, nCount);
        break;
    case CommandKind::Operator:
        HandleOperatorCommand(sCommand.eCommand, nCount);
        break;
    case CommandKind::NeedsCharacter:
        HandleCharacterCommand(sCommand.eCommand);
        break;
    case CommandKind::Prefix:
        HandlePrefixCommand(sCommand.eCommand);
        break;
    }
}

void VimEngine::HandleActionCommand(Command eCommand, int nCount)
{
    m_nPreferredColumn.reset();
    ExecuteAction(eCommand, std::max(1, nCount));
    ResetParser();
}

void VimEngine::HandleOperatorCommand(Command eCommand, int nCount)
{
    m_nPreferredColumn.reset();
    m_ePendingOperator = eCommand;
    m_nOperatorCount   = static_cast<unsigned int>(nCount);
    m_eParserState     = ParserState::OperatorPending;
}

void VimEngine::HandleCharacterCommand(Command eCommand)
{
    m_nPreferredColumn.reset();
    m_eAwaitingCommand = eCommand;
    m_eParserState     = ParserState::AwaitingCharacter;
}

void VimEngine::HandlePrefixCommand(Command eCommand)
{
    // Keep the preferred column until the prefixed motion is resolved.
    m_ePendingPrefix = eCommand;
}

bool VimEngine::IsCountCharacter(unsigned char cKey) const
{
    return std::isdigit(cKey) && (cKey != '0' || m_nCount != 0);
}

void VimEngine::AppendCountDigit(unsigned char cKey)
{
    // Counts arrive one decimal digit at a time: multiplying by ten shifts
    // the existing digits left before the newest digit is appended (2 then 5
    // becomes 2 * 10 + 5, or 25). Clamp instead of allowing an overflow.
    m_nCount = std::min(
        m_nCount * 10 + static_cast<unsigned int>(cKey - '0'),
        static_cast<unsigned int>(std::numeric_limits<int>::max()));
}

void VimEngine::Cancel()
{
    ResetParser();
    SetMode(Mode::Normal);
}

void VimEngine::ResetParser()
{
    m_eParserState   = ParserState::Normal;
    m_nCount         = 0;
    m_nOperatorCount = 0;
    m_ePendingPrefix.reset();
    m_bAwaitingForOperator = false;
}

void VimEngine::SetMode(Mode eMode)
{
    m_eMode = eMode;
    Send(
        m_hEditor, Scintilla::SETCARETSTYLE,
        eMode == Mode::Normal ? Scintilla::CARETSTYLE_BLOCK : Scintilla::CARETSTYLE_LINE);
    Send(m_hEditor, Scintilla::SETCARETPERIOD, eMode == Mode::Normal ? 0 : m_nInsertCaretPeriod);
}

void VimEngine::ExecuteAwaitedCharacter(unsigned char cKey)
{
    if (m_eAwaitingCommand == Command::ReplaceCharacter)
    {
        if (Caret() < DocumentLength() && CharacterAt(Caret()) != '\n')
        {
            Send(m_hEditor, Scintilla::BEGINUNDOACTION);
            Send(
                m_hEditor, Scintilla::DELETERANGE, Caret(),
                Send(m_hEditor, Scintilla::POSITIONAFTER, Caret()) - Caret());
            Send(m_hEditor, Scintilla::ADDTEXT, 1, reinterpret_cast<LPARAM>(&cKey));
            Send(m_hEditor, Scintilla::ENDUNDOACTION);
        }
        return;
    }
    const bool    bForward = m_eAwaitingCommand == Command::FindForward || m_eAwaitingCommand == Command::TillForward;
    const LRESULT nMatch   = FindOnLine(cKey, bForward);
    if (nMatch < 0)
    {
        return;
    }
    const LRESULT nDestination =
        (m_eAwaitingCommand == Command::TillForward)    ? Send(m_hEditor, Scintilla::POSITIONBEFORE, nMatch)
        : (m_eAwaitingCommand == Command::TillBackward) ? Send(m_hEditor, Scintilla::POSITIONAFTER, nMatch)
                                                        : nMatch;
    if (!m_bAwaitingForOperator)
    {
        Send(m_hEditor, Scintilla::GOTOPOS, nDestination);
        return;
    }

    const LRESULT nCaret = Caret();
    const LRESULT nStart = std::min(nCaret, nDestination);
    const LRESULT nEnd   = bForward ? Send(m_hEditor, Scintilla::POSITIONAFTER, nDestination)
                                    : Send(m_hEditor, Scintilla::POSITIONAFTER, nCaret);
    ApplyOperator(MotionResult{nDestination, nStart, nEnd, false});
}

LRESULT       VimEngine::Caret() const { return m_hEditor.Caret(); }
LRESULT       VimEngine::DocumentLength() const { return m_hEditor.DocumentLength(); }
LRESULT       VimEngine::LineStart(LRESULT nPosition) const { return m_hEditor.LineStart(nPosition); }
LRESULT       VimEngine::LineEnd(LRESULT nPosition) const { return m_hEditor.LineEnd(nPosition); }
unsigned char VimEngine::CharacterAt(LRESULT nPosition) const { return m_hEditor.CharacterAt(nPosition); }

LRESULT VimEngine::NextWordStart(LRESULT nPosition) const
{
    const LRESULT nLength = DocumentLength();
    const bool    bWord   = nPosition < nLength && IsWordCharacter(CharacterAt(nPosition));
    while (nPosition < nLength && !std::isspace(CharacterAt(nPosition)) &&
           IsWordCharacter(CharacterAt(nPosition)) == bWord)
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    while (nPosition < nLength && !IsWordCharacter(CharacterAt(nPosition)))
    {
        if (std::isspace(CharacterAt(nPosition)))
        {
            if (CharacterAt(nPosition) == '\r' || CharacterAt(nPosition) == '\n')
            {
                const LRESULT nCurrentLine   = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nPosition);
                const LRESULT nNextLineStart = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nCurrentLine + 1);
                const LRESULT nNextLineEnd   = Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nCurrentLine + 1);
                if (nNextLineStart >= 0 && nNextLineStart == nNextLineEnd)
                    return nNextLineStart;
            }
            nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
            continue;
        }
        break;
    }
    return nPosition;
}

LRESULT VimEngine::PreviousWordStart(LRESULT nPosition) const
{
    while (nPosition > 0 && std::isspace(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition))))
    {
        const LRESULT nPreviousPosition = Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition);
        if (CharacterAt(nPreviousPosition) == '\r' || CharacterAt(nPreviousPosition) == '\n')
        {
            const LRESULT nPreviousLine = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nPreviousPosition);
            if (nPreviousLine > 0 && Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine) ==
                                         Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nPreviousLine))
            {
                const LRESULT nNextLineStart = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine + 1);
                return nPosition == nNextLineStart ? Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine)
                                                   : Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nPreviousLine - 1);
            }
        }
        nPosition = nPreviousPosition;
    }
    const bool bWord = IsWordCharacter(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition)));
    while (nPosition > 0 && !std::isspace(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition))) &&
           IsWordCharacter(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition))) == bWord)
        nPosition = Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition);
    return nPosition;
}

LRESULT VimEngine::WordEnd(LRESULT nPosition) const
{
    const LRESULT nLength              = DocumentLength();
    const bool    bStartedOnWhitespace = nPosition < nLength && std::isspace(CharacterAt(nPosition));
    while (nPosition < nLength && std::isspace(CharacterAt(nPosition)))
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    if (nPosition >= nLength)
        return nLength > 0 ? Send(m_hEditor, Scintilla::POSITIONBEFORE, nLength) : nPosition;

    const bool bWord     = IsWordCharacter(CharacterAt(nPosition));
    LRESULT    nRunEnd   = nPosition;
    LRESULT    nAfterRun = nPosition;
    while (nAfterRun < nLength && !std::isspace(CharacterAt(nAfterRun)) &&
           IsWordCharacter(CharacterAt(nAfterRun)) == bWord)
    {
        nRunEnd   = nAfterRun;
        nAfterRun = Send(m_hEditor, Scintilla::POSITIONAFTER, nAfterRun);
    }

    // A motion that crossed whitespace must stop at the first following run,
    // even when that run is one punctuation character long. Otherwise, `e`
    // from a punctuation character at the end of its run advances to the next
    // word as expected.
    if (bStartedOnWhitespace || nRunEnd > nPosition)
        return nRunEnd;

    nPosition = nAfterRun;
    while (nPosition < nLength && std::isspace(CharacterAt(nPosition)))
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    if (nPosition >= nLength)
        return nRunEnd;

    const bool bNextWord  = IsWordCharacter(CharacterAt(nPosition));
    nRunEnd               = nPosition;
    LRESULT nAfterNextRun = nPosition;
    while (nAfterNextRun < nLength && !std::isspace(CharacterAt(nAfterNextRun)) &&
           IsWordCharacter(CharacterAt(nAfterNextRun)) == bNextWord)
    {
        nRunEnd       = nAfterNextRun;
        nAfterNextRun = Send(m_hEditor, Scintilla::POSITIONAFTER, nAfterNextRun);
    }
    return nRunEnd;
}

LRESULT VimEngine::NextWORDStart(LRESULT nPosition) const
{
    const LRESULT nLength = DocumentLength();
    while (nPosition < nLength && IsWORDCharacter(CharacterAt(nPosition)))
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    while (nPosition < nLength && std::isspace(CharacterAt(nPosition)))
    {
        const unsigned char cCharacter = CharacterAt(nPosition);
        nPosition                      = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
        if ((cCharacter == '\n' || cCharacter == '\r') && nPosition < nLength &&
            (CharacterAt(nPosition) == '\n' || CharacterAt(nPosition) == '\r'))
            return nPosition;
    }
    return nPosition;
}

LRESULT VimEngine::PreviousWORDStart(LRESULT nPosition) const
{
    while (nPosition > 0 && !IsWORDCharacter(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition))))
    {
        const LRESULT nPreviousPosition = Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition);
        if (CharacterAt(nPreviousPosition) == '\r' || CharacterAt(nPreviousPosition) == '\n')
        {
            const LRESULT nPreviousLine = Send(m_hEditor, Scintilla::LINEFROMPOSITION, nPreviousPosition);
            if (nPreviousLine > 0 && Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine) ==
                                         Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nPreviousLine))
            {
                const LRESULT nNextLineStart = Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine + 1);
                return nPosition == nNextLineStart ? Send(m_hEditor, Scintilla::POSITIONFROMLINE, nPreviousLine)
                                                   : Send(m_hEditor, Scintilla::GETLINEENDPOSITION, nPreviousLine - 1);
            }
        }
        nPosition = nPreviousPosition;
    }
    while (nPosition > 0 && IsWORDCharacter(CharacterAt(Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition))))
        nPosition = Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition);
    return nPosition;
}

LRESULT VimEngine::WORDEnd(LRESULT nPosition) const
{
    const LRESULT nLength              = DocumentLength();
    const bool    bStartedOnWhitespace = nPosition < nLength && std::isspace(CharacterAt(nPosition));
    while (nPosition < nLength && std::isspace(CharacterAt(nPosition)))
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    if (nPosition >= nLength)
        return nLength > 0 ? Send(m_hEditor, Scintilla::POSITIONBEFORE, nLength) : nPosition;

    LRESULT nRunEnd   = nPosition;
    LRESULT nAfterRun = nPosition;
    while (nAfterRun < nLength && IsWORDCharacter(CharacterAt(nAfterRun)))
    {
        nRunEnd   = nAfterRun;
        nAfterRun = Send(m_hEditor, Scintilla::POSITIONAFTER, nAfterRun);
    }

    if (bStartedOnWhitespace || nRunEnd > nPosition)
        return nRunEnd;

    nPosition = nAfterRun;
    while (nPosition < nLength && std::isspace(CharacterAt(nPosition)))
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    if (nPosition >= nLength)
        return nRunEnd;

    nRunEnd = nPosition;
    while (nPosition < nLength && IsWORDCharacter(CharacterAt(nPosition)))
    {
        nRunEnd   = nPosition;
        nPosition = Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition);
    }
    return nRunEnd;
}

LRESULT VimEngine::FindOnLine(unsigned char cTarget, bool bForward) const
{
    const LRESULT nCaret    = Caret();
    const LRESULT nBoundary = bForward ? LineEnd(nCaret) : LineStart(nCaret);
    LRESULT       nPosition = nCaret;
    while (bForward ? nPosition < nBoundary : nPosition > nBoundary)
    {
        nPosition = bForward ? Send(m_hEditor, Scintilla::POSITIONAFTER, nPosition)
                             : Send(m_hEditor, Scintilla::POSITIONBEFORE, nPosition);
        if (CharacterAt(nPosition) == cTarget)
            return nPosition;
    }
    return -1;
}

int VimEngine::TakeCount()
{
    // Zero means no count was supplied. Each command resolves its own default.
    const int nCount = static_cast<int>(m_nCount);
    m_nCount         = 0;
    // Consume operator and motion counts together only once the motion is
    // complete, including any prefix.
    if (m_eParserState == ParserState::OperatorPending && (m_nOperatorCount != 0 || nCount != 0))
        return static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(std::max(1u, m_nOperatorCount)) * std::max(1, nCount),
            std::numeric_limits<int>::max()));
    return nCount;
}

bool VimEngine::IsWordCharacter(unsigned char cCharacter) { return std::isalnum(cCharacter) || cCharacter == '_'; }

bool VimEngine::IsWORDCharacter(unsigned char cCharacter) { return !std::isspace(cCharacter); }
