#include "EditorText.h"

#include "ScintillaMessages.h"

EditorText::EditorText(HWND hWindow) : ScintillaEditor(hWindow) {}

LRESULT EditorText::Caret() const { return Send(Scintilla::GETCURRENTPOS); }

LRESULT EditorText::DocumentLength() const
{
    return Send(Scintilla::GETLENGTH);
}

LRESULT EditorText::LineStart(LRESULT nPosition) const
{
    return Send(
        Scintilla::POSITIONFROMLINE,
        Send(Scintilla::LINEFROMPOSITION, nPosition));
}

LRESULT EditorText::LineEnd(LRESULT nPosition) const
{
    return Send(
        Scintilla::GETLINEENDPOSITION,
        Send(Scintilla::LINEFROMPOSITION, nPosition));
}

unsigned char EditorText::CharacterAt(LRESULT nPosition) const
{
    return nPosition < DocumentLength() ? static_cast<unsigned char>(Send(
                                              Scintilla::GETCHARAT, nPosition))
                                        : 0;
}
