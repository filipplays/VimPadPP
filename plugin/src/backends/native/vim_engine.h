#pragma once

#include "editor_text.h"

#include <windows.h>

#include <optional>
#include <string>
#include <unordered_map>

class VimEngine
{
  public:
    enum class Mode
    {
        Normal,
        Insert,
        Visual,
    };

    explicit VimEngine(HWND hEditor);

    [[nodiscard]] Mode GetMode() const;
    bool               HandleCharacter(unsigned char cKey, bool bControl, bool bAlt);
    void               Cancel();

  private:
    enum class ParserState
    {
        Normal,
        OperatorPending,
        AwaitingCharacter,
    };
    enum class CommandKind
    {
        Action,
        Motion,
        Operator,
        NeedsCharacter,
        Prefix,
    };
    enum class Command
    {
        Left,
        Down,
        Up,
        Right,
        WordForward,
        WordBackward,
        WordEnd,
        WORDForward,
        WORDBackward,
        WORDEnd,
        LineStart,
        LineEnd,
        DocumentStart,
        DocumentEnd,
        InsertBefore,
        InsertAfter,
        InsertLineStart,
        InsertLineEnd,
        OpenBelow,
        OpenAbove,
        DeleteCharacter,
        PasteAfter,
        PasteBefore,
        FindForward,
        FindBackward,
        TillForward,
        TillBackward,
        ReplaceCharacter,
        Delete,
        Change,
        Yank,
        Go,
    };
    struct CommandMetadata
    {
        CommandKind eKind;
        Command     eCommand;
    };
    struct MotionResult
    {
        LRESULT nDestination;
        LRESULT nStart;
        LRESULT nEnd;
        bool    bLinewise;
    };

    void                        RegisterCommands();
    void                        ResetParser();
    bool                        HandleNormalCharacter(unsigned char cKey);
    bool                        HandleOperatorPendingCharacter(unsigned char cKey);
    bool                        HandleAwaitedCharacter(unsigned char cKey);
    bool                        HandlePrefixedCharacter(unsigned char cKey);
    void                        DispatchCommand(const CommandMetadata &sCommand, int nCount);
    void                        HandleActionCommand(Command eCommand, int nCount);
    void                        HandleMotionCommand(Command eCommand, int nCount);
    void                        HandleOperatorCommand(Command eCommand, int nCount);
    void                        HandleCharacterCommand(Command eCommand);
    void                        HandlePrefixCommand(Command eCommand);
    [[nodiscard]] bool          IsCountCharacter(unsigned char cKey) const;
    void                        AppendCountDigit(unsigned char cKey);
    void                        ExecuteAction(Command eCommand, int nCount);
    std::optional<MotionResult> ResolveMotion(Command eCommand, int nCount) const;
    void                        ApplyOperator(MotionResult sMotion);
    void                        ExecuteAwaitedCharacter(unsigned char cKey);
    void                        PasteUnnamedRegister(bool bAfter, int nCount);
    [[nodiscard]] std::string   ReadRange(LRESULT nStart, LRESULT nEnd) const;
    void                        SetMode(Mode eMode);

    [[nodiscard]] LRESULT       Caret() const;
    [[nodiscard]] LRESULT       DocumentLength() const;
    [[nodiscard]] LRESULT       LineStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       LineEnd(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       NextWordStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       PreviousWordStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       WordEnd(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       NextWORDStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       PreviousWORDStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       WORDEnd(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       FindOnLine(unsigned char cTarget, bool bForward) const;
    [[nodiscard]] unsigned char CharacterAt(LRESULT nPosition) const;
    [[nodiscard]] int           TakeCount();
    static bool                 IsWordCharacter(unsigned char cCharacter);
    static bool                 IsWORDCharacter(unsigned char cCharacter);

    EditorText                                         m_hEditor;
    Mode                                               m_eMode        = Mode::Normal;
    ParserState                                        m_eParserState = ParserState::Normal;
    std::unordered_map<unsigned char, CommandMetadata> m_aCommands;
    Command                                            m_ePendingOperator = Command::Delete;
    Command                                            m_eAwaitingCommand = Command::FindForward;
    unsigned int                                       m_nCount           = 0;
    unsigned int                                       m_nOperatorCount   = 0;
    std::optional<Command>                             m_ePendingPrefix;
    bool                                               m_bAwaitingForOperator = false;
    LRESULT                                            m_nInsertCaretPeriod   = 0;
    std::optional<LRESULT>                             m_nPreferredColumn;
    std::string                                        m_sUnnamedRegister;
    bool                                               m_bUnnamedRegisterLinewise = false;
};
