#include "line_update.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::string Join(const std::vector<std::string> &aLines, const std::string &sEol)
{
    std::string sText;
    for (size_t nIndex = 0; nIndex < aLines.size(); ++nIndex)
    {
        if (nIndex)
            sText += sEol;
        sText += aLines[nIndex];
    }
    return sText;
}
void Check(const std::vector<std::string> &aBefore, const NeovimLineUpdate &sUpdate, const std::string &sEol)
{
    std::string         sActual = Join(aBefore, sEol);
    std::vector<size_t> aStarts;
    size_t              nPosition = 0;
    for (const auto &sLine : aBefore)
    {
        aStarts.push_back(nPosition);
        nPosition += sLine.size() + sEol.size();
    }
    const size_t nStart = sUpdate.nFirstLine == aBefore.size() ? sActual.size() : aStarts[sUpdate.nFirstLine];
    const size_t nEnd   = sUpdate.nLastLine == aBefore.size() ? sActual.size() : aStarts[sUpdate.nLastLine];
    const size_t nPreviousEnd =
        sUpdate.nFirstLine ? aStarts[sUpdate.nFirstLine - 1] + aBefore[sUpdate.nFirstLine - 1].size() : 0;
    const auto sReplacement = BuildLineReplacement(sUpdate, aBefore.size(), nStart, nEnd, nPreviousEnd, sEol);
    assert(sReplacement.nStart <= sReplacement.nEnd && sReplacement.nEnd <= sActual.size());
    sActual.replace(sReplacement.nStart, sReplacement.nEnd - sReplacement.nStart, sReplacement.sText);

    // Independent line-array splice, compared with the editor's byte-range splice.
    auto aExpected = aBefore;
    aExpected.erase(aExpected.begin() + sUpdate.nFirstLine, aExpected.begin() + sUpdate.nLastLine);
    aExpected.insert(aExpected.begin() + sUpdate.nFirstLine, sUpdate.aLines.begin(), sUpdate.aLines.end());
    if (aExpected.empty())
        aExpected.emplace_back();
    assert(sActual == Join(aExpected, sEol));
}
} // namespace

int main()
{
    const std::vector<std::vector<std::string>> aDocuments{
        {""}, {"one"}, {"one", ""}, {"", "", ""}, {"alpha", "beta", "last"}, {"é", "🙂", ""}};
    const std::vector<std::vector<std::string>> aReplacements{{}, {""}, {"x"}, {"x", "y"}, {"", ""}, {"é", "", "🙂"}};
    for (const std::string &sEol : {std::string("\n"), std::string("\r\n"), std::string("\r")})
        for (const auto &aBefore : aDocuments)
            for (size_t nFirst = 0; nFirst <= aBefore.size(); ++nFirst)
                for (size_t nLast = nFirst; nLast <= aBefore.size(); ++nLast)
                    for (const auto &aLines : aReplacements)
                        Check(aBefore, {nFirst, nLast, aLines}, sEol);
    std::cout << "Line update checks passed\n";
}
