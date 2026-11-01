#pragma once

#include "scintilla_editor.h"

class EditorText : public ScintillaEditor
{
  public:
    explicit EditorText(HWND hWindow);

    [[nodiscard]] LRESULT       Caret() const;
    [[nodiscard]] LRESULT       DocumentLength() const;
    [[nodiscard]] LRESULT       LineStart(LRESULT nPosition) const;
    [[nodiscard]] LRESULT       LineEnd(LRESULT nPosition) const;
    [[nodiscard]] unsigned char CharacterAt(LRESULT nPosition) const;
};
