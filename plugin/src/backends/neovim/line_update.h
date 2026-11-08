#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct NeovimLineUpdate
{
    size_t                   nFirstLine = 0;
    size_t                   nLastLine  = 0;
    std::vector<std::string> aLines;
};

struct NeovimTextReplacement
{
    size_t      nStart = 0;
    size_t      nEnd   = 0;
    std::string sText;
};

// Line-array edits need special treatment at EOF: there is no separator after
// the last logical line. Byte boundaries are supplied by Scintilla, so CRLF,
// LF and CR documents use the editor's actual line positions.
inline NeovimTextReplacement BuildLineReplacement(
    const NeovimLineUpdate &sUpdate, size_t nLineCount, size_t nStart, size_t nEnd, size_t nPreviousLineEnd,
    std::string_view sEol)
{
    NeovimTextReplacement sResult{nStart, nEnd, {}};
    if (sUpdate.nFirstLine == sUpdate.nLastLine && sUpdate.aLines.empty())
        return {nStart, nStart, {}};
    if (sUpdate.nFirstLine == nLineCount && !sUpdate.aLines.empty())
        sResult.sText += sEol;
    for (size_t nIndex = 0; nIndex < sUpdate.aLines.size(); ++nIndex)
    {
        if (nIndex)
            sResult.sText += sEol;
        sResult.sText += sUpdate.aLines[nIndex];
    }
    if (sUpdate.nLastLine < nLineCount && !sUpdate.aLines.empty())
        sResult.sText += sEol;
    if (sUpdate.nLastLine == nLineCount && sUpdate.aLines.empty() && sUpdate.nFirstLine > 0)
        sResult.nStart = nPreviousLineEnd;
    return sResult;
}
