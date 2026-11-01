#include "vim_engine.h"

void VimEngine::RegisterCommands()
{
    const auto Register = [this](unsigned char cKey, CommandKind eKind, Command eCommand)
    { m_aCommands.emplace(cKey, CommandMetadata{eKind, eCommand}); };
    // Fuzzed 24. 10. 26
    Register('h', CommandKind::Motion, Command::Left);
    Register('j', CommandKind::Motion, Command::Down);
    Register('k', CommandKind::Motion, Command::Up);
    Register('l', CommandKind::Motion, Command::Right);
    // -----
    // Fuzzed 24. 10. 26
    Register('w', CommandKind::Motion, Command::WordForward);
    Register('W', CommandKind::Motion, Command::WORDForward);
    Register('b', CommandKind::Motion, Command::WordBackward);
    Register('B', CommandKind::Motion, Command::WORDBackward);
    Register('e', CommandKind::Motion, Command::WordEnd);
    Register('E', CommandKind::Motion, Command::WORDEnd);
    // -----
    // Fuzzed
    Register('0', CommandKind::Motion, Command::LineStart);
    Register('$', CommandKind::Motion, Command::LineEnd);
    Register('G', CommandKind::Motion, Command::DocumentEnd);
    Register('g', CommandKind::Prefix, Command::Go);
    // -----
    Register('i', CommandKind::Action, Command::InsertBefore);
    Register('a', CommandKind::Action, Command::InsertAfter);
    Register('I', CommandKind::Action, Command::InsertLineStart);
    Register('A', CommandKind::Action, Command::InsertLineEnd);
    Register('o', CommandKind::Action, Command::OpenBelow);
    Register('O', CommandKind::Action, Command::OpenAbove);
    Register('x', CommandKind::Action, Command::DeleteCharacter);
    // -----
    Register('d', CommandKind::Operator, Command::Delete);
    Register('c', CommandKind::Operator, Command::Change);
    Register('y', CommandKind::Operator, Command::Yank);
    Register('p', CommandKind::Action, Command::PasteAfter);
    Register('P', CommandKind::Action, Command::PasteBefore);
    // -----
    Register('f', CommandKind::NeedsCharacter, Command::FindForward);
    Register('F', CommandKind::NeedsCharacter, Command::FindBackward);
    Register('t', CommandKind::NeedsCharacter, Command::TillForward);
    Register('T', CommandKind::NeedsCharacter, Command::TillBackward);
    Register('r', CommandKind::NeedsCharacter, Command::ReplaceCharacter);
}
