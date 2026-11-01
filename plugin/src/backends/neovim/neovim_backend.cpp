#include "neovim_backend.h"
#include "ScintillaMessages.h"

#include <commctrl.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
void ModuleAnchor() {}
} // namespace

void Backend_NVIM::Close(HANDLE &hHandle)
{
    if (hHandle && hHandle != INVALID_HANDLE_VALUE)
        CloseHandle(hHandle);
    hHandle = nullptr;
}
void Backend_NVIM::Stop()
{
    if (m_sSession.nTimer)
        KillTimer(m_sSession.hWindow, m_sSession.nTimer);
    m_sSession.nTimer = 0;
    Close(m_sSession.hInput);
    if (m_sSession.hProcess)
    {
        if (WaitForSingleObject(m_sSession.hProcess, 1000) != WAIT_OBJECT_0)
        {
            TerminateProcess(m_sSession.hProcess, 1);
            WaitForSingleObject(m_sSession.hProcess, 1000);
        }
    }
    Close(m_sSession.hOutput);
    Close(m_sSession.hProcess);
}
void Backend_NVIM::Number(std::string &sData, uint64_t nValue, int nBytes)
{
    for (int nShift = (nBytes - 1) * 8; nShift >= 0; nShift -= 8)
        sData.push_back(static_cast<char>(nValue >> nShift));
}
void Backend_NVIM::String(std::string &sData, const std::string &sValue)
{
    sData.push_back(static_cast<char>(0xdb));
    Number(sData, sValue.size(), 4);
    sData += sValue;
}
bool Backend_NVIM::Read(void *pData, size_t nSize)
{
    if (m_sSession.bBufferedRead)
    {
        if (nSize > m_sSession.sIncoming.size() - m_sSession.nReadOffset)
            return false;
        std::memcpy(pData, m_sSession.sIncoming.data() + m_sSession.nReadOffset, nSize);
        m_sSession.nReadOffset += nSize;
        return true;
    }
    auto     *pBytes    = static_cast<char *>(pData);
    ULONGLONG nDeadline = GetTickCount64() + 5000;
    while (nSize)
    {
        DWORD nAvailable = 0, nRead = 0;
        if (!PeekNamedPipe(m_sSession.hOutput, nullptr, 0, nullptr, &nAvailable, nullptr))
            return false;
        if (!nAvailable)
        {
            if (WaitForSingleObject(m_sSession.hProcess, 0) == WAIT_OBJECT_0 || GetTickCount64() >= nDeadline)
                return false;
            Sleep(1);
            continue;
        }
        if (!ReadFile(
                m_sSession.hOutput, pBytes, static_cast<DWORD>(nSize < nAvailable ? nSize : nAvailable), &nRead,
                nullptr) ||
            !nRead)
            return false;
        pBytes += nRead;
        nSize -= nRead;
    }
    return true;
}
bool Backend_NVIM::ReadNumber(int nBytes, uint64_t &nValue)
{
    nValue = 0;
    for (int nIndex = 0; nIndex < nBytes; ++nIndex)
    {
        unsigned char nByte;
        if (!Read(&nByte, 1))
            return false;
        nValue = (nValue << 8) | nByte;
    }
    return true;
}
bool Backend_NVIM::Decode(Value &sValue, int nDepth)
{
    if (nDepth > 32)
        return false;
    unsigned char nTag;
    if (!Read(&nTag, 1))
        return false;
    uint64_t nLength = 0;
    if (nTag <= 0x7f)
    {
        sValue.nType    = 1;
        sValue.nInteger = nTag;
        return true;
    }
    if (nTag >= 0xe0)
    {
        sValue.nType    = 1;
        sValue.nInteger = static_cast<int8_t>(nTag);
        return true;
    }
    if ((nTag & 0xe0) == 0xa0)
    {
        sValue.nType = 2;
        nLength      = nTag & 31;
    }
    else if ((nTag & 0xf0) == 0x90)
    {
        sValue.nType = 3;
        nLength      = nTag & 15;
    }
    else if (nTag == 0xc0)
        return true;
    else if (nTag == 0xc2 || nTag == 0xc3)
    {
        sValue.nType = 4;
        return true;
    }
    else if (nTag >= 0xcc && nTag <= 0xcf)
    {
        sValue.nType = 1;
        if (!ReadNumber(1 << (nTag - 0xcc), nLength))
            return false;
        sValue.nInteger = static_cast<int64_t>(nLength);
        return true;
    }
    else if (nTag >= 0xd0 && nTag <= 0xd3)
    {
        int nBytes = 1 << (nTag - 0xd0);
        if (!ReadNumber(nBytes, nLength))
            return false;
        sValue.nType    = 1;
        sValue.nInteger = nBytes == 8 ? static_cast<int64_t>(nLength)
                                      : static_cast<int64_t>(nLength << (64 - nBytes * 8)) >> (64 - nBytes * 8);
        return true;
    }
    else if (nTag == 0xd9 || nTag == 0xda || nTag == 0xdb || nTag == 0xc4 || nTag == 0xc5 || nTag == 0xc6)
    {
        sValue.nType = 2;
        if (!ReadNumber(nTag == 0xd9 || nTag == 0xc4 ? 1 : nTag == 0xda || nTag == 0xc5 ? 2 : 4, nLength))
            return false;
    }
    else if (nTag == 0xdc || nTag == 0xdd)
    {
        sValue.nType = 3;
        if (!ReadNumber(nTag == 0xdc ? 2 : 4, nLength))
            return false;
    }
    else
        return false;
    if (nLength > 16 * 1024 * 1024)
        return false;
    if (sValue.nType == 2)
    {
        sValue.sString.resize(static_cast<size_t>(nLength));
        return !nLength || Read(sValue.sString.data(), static_cast<size_t>(nLength));
    }
    sValue.aItems.resize(static_cast<size_t>(nLength));
    for (auto &sItem : sValue.aItems)
        if (!Decode(sItem, nDepth + 1))
            return false;
    return true;
}
void Backend_NVIM::Array(std::string &sData, size_t nSize)
{
    sData.push_back(static_cast<char>(0xdc));
    Number(sData, nSize, 2);
}
void Backend_NVIM::Integer(std::string &sData, int64_t nValue)
{
    sData.push_back(static_cast<char>(0xd3));
    Number(sData, static_cast<uint64_t>(nValue), 8);
}
void Backend_NVIM::Boolean(std::string &sData, bool bValue)
{
    sData.push_back(static_cast<char>(bValue ? 0xc3 : 0xc2));
}
bool Backend_NVIM::SendRpc(const std::string &sMethod, const std::string &sArguments, unsigned &nId, bool bNotification)
{
    std::string sRequest;
    sRequest.push_back(static_cast<char>(bNotification ? 0x93 : 0x94));
    sRequest.push_back(static_cast<char>(bNotification ? 2 : 0));
    if (!bNotification)
    {
        nId = ++m_sSession.nId;
        sRequest.push_back(static_cast<char>(0xce));
        Number(sRequest, nId, 4);
    }
    String(sRequest, sMethod);
    sRequest += sArguments;
    DWORD nWritten = 0;
    std::cout << "---Request: " << sRequest << std::endl;
    return WriteFile(m_sSession.hInput, sRequest.data(), static_cast<DWORD>(sRequest.size()), &nWritten, nullptr) &&
           nWritten == sRequest.size();
}
bool Backend_NVIM::Rpc(const std::string &sMethod, const std::string &sArguments, Value &sResult)
{
    unsigned nId = 0;
    if (!SendRpc(sMethod, sArguments, nId))
        return false;
    Value sResponse;
    if (!Decode(sResponse) || sResponse.nType != 3 || sResponse.aItems.size() != 4 ||
        sResponse.aItems[0].nInteger != 1 || sResponse.aItems[1].nInteger != nId || sResponse.aItems[2].nType != 0)
        return false;
    sResult = std::move(sResponse.aItems[3]);
    return true;
}
std::string Backend_NVIM::StateArguments()
{
    std::string sArgs;
    Array(sArgs, 1);
    Array(sArgs, 2);
    Array(sArgs, 2);
    String(sArgs, "nvim_buf_get_lines");
    Array(sArgs, 4);
    Integer(sArgs, 0);
    Integer(sArgs, 0);
    Integer(sArgs, -1);
    Boolean(sArgs, true);
    Array(sArgs, 2);
    String(sArgs, "nvim_win_get_cursor");
    Array(sArgs, 1);
    Integer(sArgs, 0);
    return sArgs;
}
bool Backend_NVIM::RequestState()
{
    std::cout << "Request state" << std::endl;
    if (m_sSession.nStateId)
        return true;
    m_sSession.nStateRevision = m_sSession.nRevision;
    return SendRpc("nvim_call_atomic", StateArguments(), m_sSession.nStateId);
}
bool Backend_NVIM::SyncDocument(const std::string &sText, LRESULT nLine, LRESULT nColumn)
{
    std::cout << "Document SYNC" << std::endl;
    std::string sArgs;
    Array(sArgs, 1);
    Array(sArgs, 2);
    Array(sArgs, 2);
    String(sArgs, "nvim_buf_set_lines");
    Array(sArgs, 5);
    Integer(sArgs, 0);
    Integer(sArgs, 0);
    Integer(sArgs, -1);
    Boolean(sArgs, true);
    std::string sNormalized = Normalize(sText);
    size_t      nLines      = 1;
    for (char nChar : sNormalized)
        if (nChar == '\n')
            ++nLines;
    Array(sArgs, nLines);
    size_t nStart = 0;
    for (size_t nIndex = 0; nIndex <= sNormalized.size(); ++nIndex)
        if (nIndex == sNormalized.size() || sNormalized[nIndex] == '\n')
        {
            String(sArgs, sNormalized.substr(nStart, nIndex - nStart));
            nStart = nIndex + 1;
        }
    Array(sArgs, 2);
    String(sArgs, "nvim_win_set_cursor");
    Array(sArgs, 2);
    Integer(sArgs, 0);
    Array(sArgs, 2);
    Integer(sArgs, nLine + 1);
    Integer(sArgs, nColumn);
    Value sResult;
    return Rpc("nvim_call_atomic", sArgs, sResult) && sResult.nType == 3 && sResult.aItems.size() == 2 &&
           sResult.aItems[1].nType == 0;
}
std::string Backend_NVIM::Text(HWND hWindow)
{
    size_t      nLength = static_cast<size_t>(SendMessageW(hWindow, Scintilla::GETLENGTH, 0, 0));
    std::string sText(nLength + 1, '\0');
    SendMessageW(hWindow, Scintilla::GETTEXT, nLength + 1, reinterpret_cast<LPARAM>(sText.data()));
    sText.resize(nLength);
    return sText;
}
bool Backend_NVIM::Supported(const std::string &sText)
{
    for (unsigned char nChar : sText)
        if (nChar == 0)
            return false;
    return true;
}
std::string Backend_NVIM::Normalize(const std::string &sText)
{
    std::string sResult;
    for (size_t nIndex = 0; nIndex < sText.size(); ++nIndex)
    {
        if (sText[nIndex] == '\r')
        {
            sResult += '\n';
            if (nIndex + 1 < sText.size() && sText[nIndex + 1] == '\n')
                ++nIndex;
        }
        else
            sResult += sText[nIndex];
    }
    return sResult;
}
std::string Backend_NVIM::RestoreEol(HWND hWindow, const std::string &sText)
{
    LRESULT nEol = SendMessageW(hWindow, Scintilla::GETEOLMODE, 0, 0);
    if (m_sSession.sText.find('\n') != std::string::npos)
        nEol = m_sSession.sText.find("\r\n") != std::string::npos ? Scintilla::EOL_CRLF : Scintilla::EOL_LF;
    else if (m_sSession.sText.find('\r') != std::string::npos)
        nEol = Scintilla::EOL_CR;
    if (nEol == Scintilla::EOL_LF)
        return sText;
    std::string sResult;
    for (char nChar : sText)
        if (nChar == '\n')
            sResult += nEol == Scintilla::EOL_CRLF ? "\r\n" : "\r";
        else
            sResult += nChar;
    return sResult;
}
bool Backend_NVIM::Start()
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModuleAnchor), &hModule))
        return false;
    wchar_t aPath[32768];
    DWORD   nLength = GetModuleFileNameW(hModule, aPath, 32768);
    if (!nLength || nLength == 32768)
        return false;
    std::wstring sPath(aPath, nLength);
    size_t       nSlash = sPath.find_last_of(L"\\/");
    if (nSlash == std::wstring::npos)
        return false;
    sPath = sPath.substr(0, nSlash) + L"\\neovim\\bin\\nvim.exe";
    SECURITY_ATTRIBUTES sSecurity{sizeof(sSecurity), nullptr, TRUE};
    HANDLE              hChildInput = nullptr, hChildOutput = nullptr;
    if (!CreatePipe(&hChildInput, &m_sSession.hInput, &sSecurity, 0) ||
        !CreatePipe(&m_sSession.hOutput, &hChildOutput, &sSecurity, 0))
    {
        Close(hChildInput);
        Close(hChildOutput);
        return false;
    }
    SetHandleInformation(m_sSession.hInput, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_sSession.hOutput, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW sStartup{};
    sStartup.cb         = sizeof(sStartup);
    sStartup.dwFlags    = STARTF_USESTDHANDLES;
    sStartup.hStdInput  = hChildInput;
    sStartup.hStdOutput = hChildOutput;
    sStartup.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION sProcess{};
    std::wstring        sCommand = L"\"" + sPath + L"\" --embed --headless -u NONE -i NONE -n";

    BOOL bStarted = CreateProcessW(
        sPath.c_str(), sCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &sStartup,
        &sProcess);
    Close(hChildInput);
    Close(hChildOutput);
    if (!bStarted)
        return false;
    CloseHandle(sProcess.hThread);
    m_sSession.hProcess = sProcess.hProcess;
    return true;
}
void Backend_NVIM::Fail(const wchar_t *pMessage)
{
    m_sSession.bFailed = true;
    Stop();
    MessageBoxW(m_sSession.hWindow, pMessage, L"Neovim prototype", MB_OK | MB_ICONERROR);
}
bool Backend_NVIM::ApplyState(const Value &sResult)
{
    std::cout << "APPLY STATE" << std::endl;
    if (sResult.nType != 3 || sResult.aItems.size() != 2 || sResult.aItems[1].nType != 0 ||
        sResult.aItems[0].nType != 3 || sResult.aItems[0].aItems.size() != 2)
        return false;
    const Value &sLines  = sResult.aItems[0].aItems[0];
    const Value &sCursor = sResult.aItems[0].aItems[1];
    if (sLines.nType != 3 || sCursor.nType != 3 || sCursor.aItems.size() != 2 || sCursor.aItems[0].nType != 1 ||
        sCursor.aItems[1].nType != 1)
        return false;
    std::string sText;
    for (size_t nIndex = 0; nIndex < sLines.aItems.size(); ++nIndex)
    {
        if (sLines.aItems[nIndex].nType != 2)
            return false;
        if (nIndex)
            sText += '\n';
        sText += sLines.aItems[nIndex].sString;
    }
    HWND hWindow = m_sSession.hWindow;
    std::cout << "Got text: " << sText << std::endl;
    if (SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0) != m_sSession.nDocument ||
        Text(hWindow) != m_sSession.sText)
        return true;
    const std::string sNewText = RestoreEol(hWindow, sText);
    if (sNewText != m_sSession.sText)
    {
        size_t nStart = 0;
        while (nStart < sNewText.size() && nStart < m_sSession.sText.size() &&
               sNewText[nStart] == m_sSession.sText[nStart])
            ++nStart;
        size_t nOldEnd = m_sSession.sText.size(), nNewEnd = sNewText.size();
        while (nOldEnd > nStart && nNewEnd > nStart && m_sSession.sText[nOldEnd - 1] == sNewText[nNewEnd - 1])
        {
            --nOldEnd;
            --nNewEnd;
        }
        SendMessageW(hWindow, Scintilla::SETTARGETSTART, nStart, 0);
        SendMessageW(hWindow, Scintilla::SETTARGETEND, nOldEnd, 0);
        SendMessageW(
            hWindow, Scintilla::REPLACETARGET, nNewEnd - nStart, reinterpret_cast<LPARAM>(sNewText.data() + nStart));
        m_sSession.sText = sNewText;
    }
    LRESULT nStart =
        SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, static_cast<WPARAM>(sCursor.aItems[0].nInteger - 1), 0);
    SendMessageW(hWindow, Scintilla::GOTOPOS, static_cast<WPARAM>(nStart + sCursor.aItems[1].nInteger), 0);
    return true;
}
bool Backend_NVIM::PollResponses()
{
    std::cout << "--POLL RESPONSE--" << std::endl;
    DWORD nAvailable = 0;
    if (!PeekNamedPipe(m_sSession.hOutput, nullptr, 0, nullptr, &nAvailable, nullptr))
        return false;
    while (nAvailable)
    {
        char  aBuffer[4096];
        DWORD nRead = 0;
        if (!ReadFile(
                m_sSession.hOutput, aBuffer, nAvailable < sizeof(aBuffer) ? nAvailable : sizeof(aBuffer), &nRead,
                nullptr) ||
            !nRead)
            return false;
        m_sSession.sIncoming.append(aBuffer, nRead);
        nAvailable -= nRead;
    }
    if (m_sSession.sIncoming.size() > 32 * 1024 * 1024)
        return false;
    for (;;)
    {
        m_sSession.nReadOffset   = 0;
        m_sSession.bBufferedRead = true;
        Value sResponse;
        bool  bComplete          = Decode(sResponse);
        m_sSession.bBufferedRead = false;
        if (!bComplete)
            break;
        m_sSession.sIncoming.erase(0, m_sSession.nReadOffset);
        if (sResponse.nType != 3 || sResponse.aItems.size() != 4 || sResponse.aItems[0].nInteger != 1 ||
            sResponse.aItems[1].nInteger != m_sSession.nStateId || sResponse.aItems[2].nType != 0)
            return false;
        m_sSession.nStateId = 0;
        if (m_sSession.nStateRevision != m_sSession.nRevision)
        {
            if (!RequestState())
                return false;
        }
        else if (!ApplyState(sResponse.aItems[3]))
            return false;
    }
    if (!m_sSession.nStateId)
    {
        KillTimer(m_sSession.hWindow, m_sSession.nTimer);
        m_sSession.nTimer = 0;
    }
    return true;
}
BOOL Backend_NVIM::bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM, UINT_PTR, DWORD_PTR)
{
    if (nMessage == WM_TIMER && wParam == m_sSession.nTimer && m_sSession.nTimer)
    {
        if (!PollResponses())
            Fail(L"Neovim communication failed.");
        return TRUE;
    }
    else if ((nMessage == WM_CHAR || nMessage == WM_KEYDOWN) && !m_sSession.bFailed)
    {
        std::string sKey;
        if (nMessage == WM_KEYDOWN)
        {
            switch (wParam)
            {
            case VK_LEFT:
                sKey = "<Left>";
                break;
            case VK_RIGHT:
                sKey = "<Right>";
                break;
            case VK_UP:
                sKey = "<Up>";
                break;
            case VK_DOWN:
                sKey = "<Down>";
                break;
            case VK_DELETE:
                sKey = "<Del>";
                break;
            default:
                return FALSE;
            }
        }
        else if (wParam == 27)
            sKey = "<Esc>";
        else if (wParam == 13)
            sKey = "<CR>";
        else if (wParam == 8)
            sKey = "<BS>";
        else if (wParam == 9)
            sKey = "<Tab>";
        else if (wParam >= 1 && wParam <= 26)
            sKey = std::string("<C-") + static_cast<char>('a' + wParam - 1) + ">";
        else if (wParam == '<')
            sKey = "<lt>";
        else if (wParam < 128)
            sKey.assign(1, static_cast<char>(wParam));
        else
        {
            wchar_t nChar = static_cast<wchar_t>(wParam);
            char    aUtf8[4];
            int     nBytes = WideCharToMultiByte(CP_UTF8, 0, &nChar, 1, aUtf8, sizeof(aUtf8), nullptr, nullptr);
            if (nBytes <= 0)
                return TRUE;
            sKey.assign(aUtf8, nBytes);
        }
        if (SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0) != m_sSession.nDocument ||
            Text(hWindow) != m_sSession.sText)
        {
            std::string sText = Text(hWindow);
            if (!Supported(sText))
            {
                Fail(
                    L"The Neovim prototype supports ASCII text with LF line "
                    L"endings only.");
                return TRUE;
            }
            LRESULT nPosition = SendMessageW(hWindow, Scintilla::GETCURRENTPOS, 0, 0);
            LRESULT nLine     = SendMessageW(hWindow, Scintilla::LINEFROMPOSITION, nPosition, 0);
            LRESULT nColumn   = nPosition - SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, nLine, 0);
            if (m_sSession.nStateId || !SyncDocument(sText, nLine, nColumn))
            {
                Fail(L"Could not synchronize the document with Neovim.");
                return TRUE;
            }
            m_sSession.sText     = sText;
            m_sSession.nDocument = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
        }
        std::string sArgs;
        Array(sArgs, 1);
        String(sArgs, sKey);
        unsigned nUnused = 0;
        ++m_sSession.nRevision;
        if (!SendRpc("nvim_input", sArgs, nUnused, true) || !RequestState())
        {
            Fail(L"Neovim communication failed.");
            return TRUE;
        }
        if (!m_sSession.nTimer)
        {
            if (SetTimer(hWindow, NeovimResponseTimerId, 10, nullptr))
                m_sSession.nTimer = NeovimResponseTimerId;
        }
        if (!m_sSession.nTimer)
            Fail(L"Could not start the Neovim response timer.");
        return TRUE;
    }
    return FALSE;
}
BOOL Backend_NVIM::Install(HWND hWindow)
{
    m_sSession.hWindow   = hWindow;
    m_sSession.nDocument = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
    m_sSession.sText     = Text(hWindow);
    if (!Supported(m_sSession.sText))
    {
        Fail(
            L"The Neovim prototype does not support NUL bytes "
            L"only.");
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!Start())
    {
        Fail(
            L"Could not start bundled Neovim. Check that neovim\\bin\\nvim.exe "
            L"is installed beside the plugin.");
        SetLastError(ERROR_PROCESS_ABORTED);
        return FALSE;
    }
    LRESULT nPosition = SendMessageW(hWindow, Scintilla::GETCURRENTPOS, 0, 0);
    LRESULT nLine     = SendMessageW(hWindow, Scintilla::LINEFROMPOSITION, nPosition, 0);
    LRESULT nColumn   = nPosition - SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, nLine, 0);
    if (!SyncDocument(m_sSession.sText, nLine, nColumn))
    {
        Fail(L"Neovim initialization failed or timed out.");
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}
BOOL Backend_NVIM::Uninstall(HWND)
{
    Stop();
    return TRUE;
}
