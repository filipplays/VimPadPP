#include "neovim_backend.h"
#include "ScintillaMessages.h"
// #include "../common/helpers.h" // Uncomment for the timing hooks below.

#include <algorithm>
#include <commctrl.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
void ModuleAnchor() {}
} // namespace

void Backend_NVIM::CloseHandleAndReset(HANDLE &hHandle)
{
    if (hHandle && hHandle != INVALID_HANDLE_VALUE)
        CloseHandle(hHandle);
    hHandle = nullptr;
}
void Backend_NVIM::StopNeovimProcess()
{
    if (m_bScrollPastEnd)
    {
        (void)m_sEditor.Send(Scintilla::SETENDATLASTLINE, m_bOriginalEndAtLastLine);
        m_bScrollPastEnd = false;
    }
    if (m_sSession.nTimer)
        KillTimer(m_hWindow, m_sSession.nTimer);
    m_sSession.nTimer = 0;
    if (m_hCommandBar)
        DestroyWindow(m_hCommandBar);
    m_hCommandBar = nullptr;
    CloseHandleAndReset(m_sSession.hInput);
    if (m_sSession.hProcess)
    {
        if (WaitForSingleObject(m_sSession.hProcess, 1000) != WAIT_OBJECT_0)
        {
            TerminateProcess(m_sSession.hProcess, 1);
            WaitForSingleObject(m_sSession.hProcess, 1000);
        }
    }
    CloseHandleAndReset(m_sSession.hOutput);
    CloseHandleAndReset(m_sSession.hProcess);
}
void Backend_NVIM::AppendBigEndianBytes(std::string &sData, uint64_t nValue, int nBytes)
{
    for (int nShift = (nBytes - 1) * 8; nShift >= 0; nShift -= 8)
        sData.push_back(static_cast<char>(nValue >> nShift));
}
void Backend_NVIM::AppendMsgPackString(std::string &sData, const std::string &sValue)
{
    sData.push_back(static_cast<char>(0xdb));
    AppendBigEndianBytes(sData, sValue.size(), 4);
    sData += sValue;
}
bool Backend_NVIM::ReadBytes(void *pData, size_t nSize)
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
bool Backend_NVIM::ReadBigEndianInteger(int nBytes, uint64_t &nValue)
{
    nValue = 0;
    for (int nIndex = 0; nIndex < nBytes; ++nIndex)
    {
        unsigned char nByte;
        if (!ReadBytes(&nByte, 1))
            return false;
        nValue = (nValue << 8) | nByte;
    }
    return true;
}
bool Backend_NVIM::DecodeMsgPackValue(Value &sValue, int nDepth)
{
    if (nDepth > 32)
        return false;
    unsigned char nTag;
    if (!ReadBytes(&nTag, 1))
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
        sValue.nType    = 4;
        sValue.nInteger = nTag == 0xc3;
        return true;
    }
    else if ((nTag & 0xf0) == 0x80 || nTag == 0xde || nTag == 0xdf)
    {
        sValue.nType = 4;
        if ((nTag & 0xf0) == 0x80)
            nLength = nTag & 15;
        else if (!ReadBigEndianInteger(nTag == 0xde ? 2 : 4, nLength))
            return false;
        if (nLength > 1024 * 1024)
            return false;
        for (uint64_t nIndex = 0; nIndex < nLength * 2; ++nIndex)
        {
            Value sItem;
            if (!DecodeMsgPackValue(sItem, nDepth + 1))
                return false;
        }
        return true;
    }
    else if (nTag == 0xca || nTag == 0xcb || (nTag >= 0xd4 && nTag <= 0xd8) || (nTag >= 0xc7 && nTag <= 0xc9))
    {
        sValue.nType = 4;
        if (nTag == 0xca || nTag == 0xcb)
            nLength = nTag == 0xca ? 4 : 8;
        else
        {
            if (nTag >= 0xd4)
                nLength = uint64_t{1} << (nTag - 0xd4);
            else if (!ReadBigEndianInteger(nTag == 0xc7 ? 1 : nTag == 0xc8 ? 2 : 4, nLength))
                return false;
            ++nLength; // Extension type byte.
        }
        if (nLength > 16 * 1024 * 1024)
            return false;
        char aBytes[256];
        while (nLength)
        {
            size_t nChunk = static_cast<size_t>(std::min<uint64_t>(nLength, sizeof(aBytes)));
            if (!ReadBytes(aBytes, nChunk))
                return false;
            nLength -= nChunk;
        }
        return true;
    }
    else if (nTag >= 0xcc && nTag <= 0xcf)
    {
        sValue.nType = 1;
        if (!ReadBigEndianInteger(1 << (nTag - 0xcc), nLength))
            return false;
        sValue.nInteger = static_cast<int64_t>(nLength);
        return true;
    }
    else if (nTag >= 0xd0 && nTag <= 0xd3)
    {
        int nBytes = 1 << (nTag - 0xd0);
        if (!ReadBigEndianInteger(nBytes, nLength))
            return false;
        sValue.nType    = 1;
        sValue.nInteger = nBytes == 8 ? static_cast<int64_t>(nLength)
                                      : static_cast<int64_t>(nLength << (64 - nBytes * 8)) >> (64 - nBytes * 8);
        return true;
    }
    else if (nTag == 0xd9 || nTag == 0xda || nTag == 0xdb || nTag == 0xc4 || nTag == 0xc5 || nTag == 0xc6)
    {
        sValue.nType = 2;
        if (!ReadBigEndianInteger(nTag == 0xd9 || nTag == 0xc4 ? 1 : nTag == 0xda || nTag == 0xc5 ? 2 : 4, nLength))
            return false;
    }
    else if (nTag == 0xdc || nTag == 0xdd)
    {
        sValue.nType = 3;
        if (!ReadBigEndianInteger(nTag == 0xdc ? 2 : 4, nLength))
            return false;
    }
    else
        return false;
    if (nLength > 16 * 1024 * 1024)
        return false;
    if (sValue.nType == 2)
    {
        sValue.sString.resize(static_cast<size_t>(nLength));
        return !nLength || ReadBytes(sValue.sString.data(), static_cast<size_t>(nLength));
    }
    sValue.aItems.resize(static_cast<size_t>(nLength));
    for (auto &sItem : sValue.aItems)
        if (!DecodeMsgPackValue(sItem, nDepth + 1))
            return false;
    return true;
}
void Backend_NVIM::AppendMsgPackArrayHeader(std::string &sData, size_t nSize)
{
    sData.push_back(static_cast<char>(0xdc));
    AppendBigEndianBytes(sData, nSize, 2);
}
void Backend_NVIM::AppendMsgPackInteger(std::string &sData, int64_t nValue)
{
    sData.push_back(static_cast<char>(0xd3));
    AppendBigEndianBytes(sData, static_cast<uint64_t>(nValue), 8);
}
void Backend_NVIM::AppendMsgPackBoolean(std::string &sData, bool bValue)
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
        AppendBigEndianBytes(sRequest, nId, 4);
    }
    AppendMsgPackString(sRequest, sMethod);
    sRequest += sArguments;
    DWORD nWritten = 0;
    return WriteFile(m_sSession.hInput, sRequest.data(), static_cast<DWORD>(sRequest.size()), &nWritten, nullptr) &&
           nWritten == sRequest.size();
}
bool Backend_NVIM::CallRpcAndWait(const std::string &sMethod, const std::string &sArguments, Value &sResult)
{
    unsigned nId = 0;
    if (!SendRpc(sMethod, sArguments, nId))
        return false;
    for (;;)
    {
        Value sResponse;
        if (!DecodeMsgPackValue(sResponse) || sResponse.nType != 3 || sResponse.aItems.empty())
            return false;
        if (sResponse.aItems[0].nInteger == 2)
        {
            if (!HandleNotification(sResponse))
                return false;
            continue;
        }
        if (sResponse.aItems.size() != 4 || sResponse.aItems[0].nInteger != 1 || sResponse.aItems[1].nInteger != nId ||
            sResponse.aItems[2].nType != 0)
            return false;
        sResult = std::move(sResponse.aItems[3]);
        return true;
    }
}
bool Backend_NVIM::AttachUi()
{
    std::string sArgs;
    AppendMsgPackArrayHeader(sArgs, 3);
    AppendMsgPackInteger(sArgs, 80);
    AppendMsgPackInteger(sArgs, 24);
    sArgs.push_back(static_cast<char>(0x81));
    AppendMsgPackString(sArgs, "ext_messages");
    AppendMsgPackBoolean(sArgs, true);
    Value sResult;
    return CallRpcAndWait("nvim_ui_attach", sArgs, sResult);
}
void Backend_NVIM::PositionCommandBar()
{
    if (!m_hCommandBar)
        return;
    RECT sRect{};
    if (GetClientRect(m_hWindow, &sRect))
    {
        int nHeight = 24;
        SetWindowPos(
            m_hCommandBar, HWND_TOP, 0, std::max(0L, sRect.bottom - nHeight), sRect.right, nHeight, SWP_NOACTIVATE);
    }
}
void Backend_NVIM::UpdateCommandBar()
{
    if (!m_hCommandBar)
        return;
    std::string sText;
    if (m_bCommandVisible)
    {
        size_t nPosition = std::min(m_nCommandPosition, m_sCommandText.size());
        sText = m_sCommandPrefix + m_sCommandText.substr(0, nPosition) + "|" + m_sCommandText.substr(nPosition);
    }
    else
        sText = m_sMessage;
    if (sText.empty())
    {
        ShowWindow(m_hCommandBar, SW_HIDE);
        if (m_bScrollPastEnd)
        {
            (void)m_sEditor.Send(Scintilla::SETENDATLASTLINE, m_bOriginalEndAtLastLine);
            m_bScrollPastEnd = false;
        }
        EnsureCaretVisible();
        return;
    }
    std::replace(sText.begin(), sText.end(), '\n', ' ');
    int nLength = MultiByteToWideChar(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), nullptr, 0);
    if (nLength <= 0)
        return;
    std::wstring sWide(static_cast<size_t>(nLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), sWide.data(), nLength);
    SetWindowTextW(m_hCommandBar, sWide.c_str());
    PositionCommandBar();
    ShowWindow(m_hCommandBar, SW_SHOWNOACTIVATE);
    EnsureCaretVisible();
}
void Backend_NVIM::EnsureCaretVisible()
{
    if (!m_hCommandBar || m_sEditor.Send(Scintilla::GETDOCPOINTER) != m_sSession.nDocument ||
        !IsWindowVisible(m_hWindow))
        return;
    RECT sUsable{};
    if (!GetClientRect(m_hWindow, &sUsable))
        return;
    if (IsWindowVisible(m_hCommandBar))
    {
        RECT sBar{};
        RECT sOverlap{};
        if (GetWindowRect(m_hCommandBar, &sBar))
        {
            MapWindowPoints(HWND_DESKTOP, m_hWindow, reinterpret_cast<POINT *>(&sBar), 2);
            if (IntersectRect(&sOverlap, &sUsable, &sBar))
            {
                sUsable.bottom = sOverlap.top;
                // Allow the last document row to move above the overlay too.
                if (!m_bScrollPastEnd)
                {
                    m_bOriginalEndAtLastLine = m_sEditor.Send(Scintilla::GETENDATLASTLINE) != 0;
                    (void)m_sEditor.Send(Scintilla::SETENDATLASTLINE, false);
                    m_bScrollPastEnd = true;
                }
            }
        }
    }
    const LRESULT nCaret  = m_sEditor.Caret();
    const LRESULT nLine   = m_sEditor.Send(Scintilla::LINEFROMPOSITION, nCaret);
    const LRESULT nHeight = m_sEditor.Send(Scintilla::TEXTHEIGHT, nLine);
    // No scroll position can fit a complete row in a smaller text area.
    if (nHeight <= 0 || sUsable.bottom - sUsable.top < nHeight)
        return;
    if (!m_sEditor.Send(Scintilla::GETLINEVISIBLE, nLine))
        (void)m_sEditor.Send(Scintilla::ENSUREVISIBLE, nLine);
    // Query the caret itself, rather than the start of its document line:
    // a wrapped line can occupy several display rows.
    const LRESULT nY    = m_sEditor.Send(Scintilla::POINTYFROMPOSITION, 0, nCaret);
    LRESULT       nRows = 0;
    if (nY < sUsable.top)
        nRows = -((sUsable.top - nY + nHeight - 1) / nHeight);
    else if (nY + nHeight > sUsable.bottom)
        nRows = (nY + nHeight - sUsable.bottom + nHeight - 1) / nHeight;
    if (nRows)
        (void)m_sEditor.Send(Scintilla::LINESCROLL, 0, nRows);
}
bool Backend_NVIM::HandleNotification(const Value &sNotification)
{
    if (sNotification.aItems.size() != 3 || sNotification.aItems[1].nType != 2 || sNotification.aItems[2].nType != 3)
        return false;
    if (sNotification.aItems[1].sString == "vim_motions_save")
    {
        m_sSession.bSavePending = true;
        return true;
    }
    if (sNotification.aItems[1].sString != "redraw")
        return true;
    auto ChunksToText = [](const Value &sChunks)
    {
        std::string sText;
        if (sChunks.nType == 3)
            for (const Value &sChunk : sChunks.aItems)
                if (sChunk.nType == 3 && sChunk.aItems.size() >= 2 && sChunk.aItems[1].nType == 2)
                    sText += sChunk.aItems[1].sString;
        return sText;
    };
    for (const Value &sEvent : sNotification.aItems[2].aItems)
    {
        if (sEvent.nType != 3 || sEvent.aItems.empty() || sEvent.aItems[0].nType != 2)
            continue;
        const std::string &sName = sEvent.aItems[0].sString;
        for (size_t nIndex = 1; nIndex < sEvent.aItems.size(); ++nIndex)
        {
            const Value &sArgs = sEvent.aItems[nIndex];
            if (sArgs.nType != 3)
                continue;
            if (sName == "cmdline_show" && sArgs.aItems.size() >= 4 && sArgs.aItems[1].nType == 1 &&
                sArgs.aItems[2].nType == 2 && sArgs.aItems[3].nType == 2)
            {
                m_sCommandText     = ChunksToText(sArgs.aItems[0]);
                m_nCommandPosition = static_cast<size_t>(std::max<int64_t>(0, sArgs.aItems[1].nInteger));
                m_sCommandPrefix   = sArgs.aItems[2].sString + sArgs.aItems[3].sString;
                m_bCommandVisible  = true;
            }
            else if (sName == "cmdline_pos" && !sArgs.aItems.empty() && sArgs.aItems[0].nType == 1)
                m_nCommandPosition = static_cast<size_t>(std::max<int64_t>(0, sArgs.aItems[0].nInteger));
            else if (sName == "cmdline_hide")
                m_bCommandVisible = false;
            else if (sName == "msg_show" && sArgs.aItems.size() >= 2)
                m_sMessage = ChunksToText(sArgs.aItems[1]);
            else if (sName == "msg_clear")
                m_sMessage.clear();
            else if (sName == "flush")
                UpdateCommandBar();
        }
    }
    return true;
}
std::string Backend_NVIM::BuildStateArguments(bool bAcceptCurrent)
{
    std::string           sArgs;
    // Run the comparison and snapshot together, before serializing any document text.
    static constexpr char StateSnapshotLua[] = R"lua(
local function snapshot(nKnownBuffer, nKnownTick, nKnownEpoch, nKnownSequence, bAcceptCurrent)
  local nBuffer = vim.api.nvim_get_current_buf()
  local nTick = vim.api.nvim_buf_get_changedtick(nBuffer)
  local sStore = rawget(_G, '__vim_motions_sync')
  if not sStore then
    sStore = {nEpoch = 0}
    rawset(_G, '__vim_motions_sync', sStore)
  end
  local sJournal = sStore.sJournal
  -- A reset follows an authoritative host-to-Neovim text push. Reloads, detach,
  -- buffer switches and untracked tick changes also require a fresh baseline.
  if bAcceptCurrent or not sJournal or not sJournal.bValid or sJournal.nBuffer ~= nBuffer
      or sJournal.nObservedTick ~= nTick then
    if sJournal then
      sJournal.bValid = false
      sJournal.aChanges = {}
      sJournal.aCheckpoints = {}
    end
    sStore.nEpoch = sStore.nEpoch + 1
    sJournal = {nBuffer = nBuffer, nEpoch = sStore.nEpoch, nSequence = 0,
                nFirstSequence = 1, nBytes = 0, nObservedTick = nTick,
                aChanges = {}, aCheckpoints = {}, bValid = true}
    sStore.sJournal = sJournal
    local function record(nChangedTick, nFirst, nLast, aLines)
      if sStore.sJournal ~= sJournal or not sJournal.bValid then return true end
      sJournal.nSequence = sJournal.nSequence + 1
      sJournal.nObservedTick = nChangedTick
      local nBytes = 64
      for _, sLine in ipairs(aLines) do nBytes = nBytes + #sLine + 1 end
      sJournal.aChanges[sJournal.nSequence] = {sJournal.nSequence, nFirst, nLast, aLines, nBytes}
      sJournal.nBytes = sJournal.nBytes + nBytes
      -- Bounded retention. An older client receives a full snapshot instead.
      while sJournal.nSequence - sJournal.nFirstSequence + 1 > 1024 or sJournal.nBytes > 4 * 1024 * 1024 do
        local sOld = sJournal.aChanges[sJournal.nFirstSequence]
        sJournal.nBytes = sJournal.nBytes - sOld[5]
        sJournal.aChanges[sJournal.nFirstSequence] = nil
        sJournal.nFirstSequence = sJournal.nFirstSequence + 1
      end
    end
    local function invalidate()
      sJournal.bValid = false
      sJournal.aChanges = {}
      sJournal.aCheckpoints = {}
    end
    assert(vim.api.nvim_buf_attach(nBuffer, false, {
      on_lines = function(_, nBuf, nChangedTick, nFirst, nLast, nNewLast)
        if sStore.sJournal ~= sJournal or not sJournal.bValid then return true end
        return record(nChangedTick, nFirst, nLast, vim.api.nvim_buf_get_lines(nBuf, nFirst, nNewLast, true))
      end,
      on_changedtick = function(_, _, nChangedTick)
        return record(nChangedTick, -1, -1, {})
      end,
      on_reload = invalidate,
      on_detach = invalidate,
    }))
  end
  local bCanReplay = nKnownBuffer == nBuffer and nKnownEpoch == sJournal.nEpoch
      and nKnownSequence >= sJournal.nFirstSequence - 1 and nKnownSequence <= sJournal.nSequence
      and sJournal.aCheckpoints[nKnownSequence] == nKnownTick
  local nKind, aPayload, nBaseSequence = 0, vim.NIL, -1
  if bCanReplay then
    -- Only a later request acknowledging an issued checkpoint releases history.
    while sJournal.nFirstSequence <= nKnownSequence do
      local sOld = sJournal.aChanges[sJournal.nFirstSequence]
      sJournal.nBytes = sJournal.nBytes - sOld[5]
      sJournal.aChanges[sJournal.nFirstSequence] = nil
      sJournal.nFirstSequence = sJournal.nFirstSequence + 1
    end
    nBaseSequence = nKnownSequence
    if nKnownSequence < sJournal.nSequence then
      nKind, aPayload = 2, {}
      for nSequence = nKnownSequence + 1, sJournal.nSequence do
        local sChange = sJournal.aChanges[nSequence]
        aPayload[#aPayload + 1] = {sChange[1], sChange[2], sChange[3], sChange[4]}
      end
    end
  elseif not bAcceptCurrent then
    nKind = 1
    aPayload = vim.api.nvim_buf_get_lines(nBuffer, 0, -1, true)
  end
  for nSequence in pairs(sJournal.aCheckpoints) do
    if nSequence < sJournal.nFirstSequence - 1 then sJournal.aCheckpoints[nSequence] = nil end
  end
  sJournal.aCheckpoints[sJournal.nSequence] = nTick
  return {nBuffer, nTick, sJournal.nEpoch, nBaseSequence, sJournal.nSequence, nKind, aPayload,
          vim.api.nvim_win_get_cursor(0), {vim.fn.mode(1), vim.fn.getpos('v'), vim.o.selection}}
end
_G.__vim_motions_state = snapshot
return snapshot(...)
)lua";
    static constexpr char StateRequestLua[]  = "return _G.__vim_motions_state(...)";
    AppendMsgPackArrayHeader(sArgs, 2);
    // Install the collector once during host synchronization. Per-key RPCs stay small.
    AppendMsgPackString(sArgs, bAcceptCurrent ? StateSnapshotLua : StateRequestLua);
    AppendMsgPackArrayHeader(sArgs, 5);
    AppendMsgPackInteger(sArgs, m_sSession.nSyncedBuffer);
    AppendMsgPackInteger(sArgs, m_sSession.nSyncedChangedTick);
    AppendMsgPackInteger(sArgs, m_sSession.nSyncedEpoch);
    AppendMsgPackInteger(sArgs, m_sSession.nSyncedSequence);
    AppendMsgPackBoolean(sArgs, bAcceptCurrent);
    return sArgs;
}
bool Backend_NVIM::RequestNeovimState()
{
    if (m_sSession.nStateId || m_sSession.nCaretId || m_bMouseSelecting)
        return true;
    m_sSession.nStateRevision = m_sSession.nRevision;
    if (!SendRpc("nvim_exec_lua", BuildStateArguments(), m_sSession.nStateId))
        return false;
    return true;
}
bool Backend_NVIM::SendKey(const std::string &sKey)
{
    std::string sArgs;
    AppendMsgPackArrayHeader(sArgs, 1);
    AppendMsgPackString(sArgs, sKey);
    unsigned nUnused = 0;
    return SendRpc("nvim_input", sArgs, nUnused, true);
}
bool Backend_NVIM::RequestCaretSync()
{
    if (m_bSyncSelection)
    {
        // Scintilla uses a half-open byte range. Supply both the upper boundary
        // and the previous character so Lua can honor Neovim's selection option.
        const LRESULT         nCaret             = m_sSession.nKnownCaret;
        const LRESULT         nUpper             = std::max(nCaret, m_nSelectionAnchor);
        const LRESULT         nPrevious          = m_sEditor.Send(Scintilla::POSITIONBEFORE, nUpper);
        static constexpr char SelectionSyncLua[] = R"lua(
local nAnchorLine, nAnchorColumn, nCaretLine, nCaretColumn, nPreviousLine, nPreviousColumn = ...
local bEmpty = nAnchorLine == nCaretLine and nAnchorColumn == nCaretColumn
local sMode = vim.fn.mode(1):sub(1, 1)
if not bEmpty or sMode == 'n' or sMode == 'v' or sMode == 'V' or sMode == '\22' then
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes('<C-\\><C-n>', true, false, true), 'nx', false)
end
if nAnchorLine == nCaretLine and nAnchorColumn == nCaretColumn then
  vim.api.nvim_win_set_cursor(0, {nCaretLine, nCaretColumn})
  return
end
if vim.o.selection ~= 'exclusive' then
  if nAnchorLine < nCaretLine or (nAnchorLine == nCaretLine and nAnchorColumn < nCaretColumn) then
    nCaretLine, nCaretColumn = nPreviousLine, nPreviousColumn
  else
    nAnchorLine, nAnchorColumn = nPreviousLine, nPreviousColumn
  end
end
vim.cmd('normal! v')
vim.api.nvim_win_set_cursor(0, {nAnchorLine, nAnchorColumn})
vim.cmd('normal! o')
vim.api.nvim_win_set_cursor(0, {nCaretLine, nCaretColumn})
)lua";
        std::string           sArgs;
        AppendMsgPackArrayHeader(sArgs, 2);
        AppendMsgPackString(sArgs, SelectionSyncLua);
        AppendMsgPackArrayHeader(sArgs, 6);
        for (const LRESULT nPosition : {m_nSelectionAnchor, nCaret, nPrevious})
        {
            AppendMsgPackInteger(sArgs, m_sEditor.Send(Scintilla::LINEFROMPOSITION, nPosition) + 1);
            AppendMsgPackInteger(sArgs, nPosition - m_sEditor.LineStart(nPosition));
        }
        m_nSelectionRevision       = m_sSession.nRevision;
        m_sSession.nRequestedCaret = nCaret;
        return SendRpc("nvim_exec_lua", sArgs, m_sSession.nCaretId);
    }
    const LRESULT nPosition = m_sSession.nKnownCaret;
    const LRESULT nLine     = m_sEditor.Send(Scintilla::LINEFROMPOSITION, nPosition);
    const LRESULT nColumn   = nPosition - m_sEditor.LineStart(nPosition);
    std::string   sArgs;
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackInteger(sArgs, 0);
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackInteger(sArgs, nLine + 1);
    AppendMsgPackInteger(sArgs, nColumn);
    m_sSession.nRequestedCaret = nPosition;
    return SendRpc("nvim_win_set_cursor", sArgs, m_sSession.nCaretId);
}
void Backend_NVIM::OnCaretMoved(LRESULT nPosition)
{
    if (m_bMouseSelecting || m_bSyncSelection || m_sSession.bFailed || m_sSession.bApplyingState ||
        nPosition == m_sSession.nKnownCaret || m_sEditor.Send(Scintilla::GETDOCPOINTER) != m_sSession.nDocument ||
        GetEditorText() != m_sSession.sText)
        return;
    m_sSession.nKnownCaret = nPosition;
    m_nViewAnchor          = m_sEditor.Send(Scintilla::GETANCHOR);
    ++m_sSession.nRevision;
    const Mode eMode = m_sSession.eMode;
    if ((eMode == Mode::Normal || eMode == Mode::VisualCharacter || eMode == Mode::VisualLine ||
         eMode == Mode::VisualBlock) &&
        !SendKey("<Esc>"))
    {
        StopAndReportError(L"Could not cancel the pending Neovim command.");
        return;
    }
    if (!m_sSession.nCaretId && !RequestCaretSync())
    {
        StopAndReportError(L"Could not synchronize the caret with Neovim.");
        return;
    }
    if (!m_sSession.nTimer && SetTimer(m_hWindow, NeovimResponseTimerId, 10, nullptr))
        m_sSession.nTimer = NeovimResponseTimerId;
    if (!m_sSession.nTimer)
        StopAndReportError(L"Could not start the Neovim response timer.");
}
void Backend_NVIM::FinishMouseSelection()
{
    m_bMouseSelecting = false;
    m_bMouseFinishing = false;
    if (m_sSession.bFailed || m_sEditor.Send(Scintilla::GETDOCPOINTER) != m_sSession.nDocument ||
        GetEditorText() != m_sSession.sText)
        return;
    // Rectangular and multiple selections are not representable by this path.
    if (m_sEditor.Send(Scintilla::GETSELECTIONMODE) != 0 || m_sEditor.Send(Scintilla::GETSELECTIONS) != 1)
        return;
    m_nSelectionAnchor     = m_sEditor.Send(Scintilla::GETANCHOR);
    m_sSession.nKnownCaret = m_sEditor.Caret();
    m_bSyncSelection       = true;
    ++m_sSession.nRevision;
    if (!m_sSession.nCaretId && !RequestCaretSync())
        StopAndReportError(L"Could not synchronize the mouse selection with Neovim.");
}
bool Backend_NVIM::SyncNeovim(const std::string &sText, LRESULT nLine, LRESULT nColumn)
{
    InvalidateSyncedRevision();
    std::string sArgs;
    AppendMsgPackArrayHeader(sArgs, 1);
    AppendMsgPackArrayHeader(sArgs, 3);
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackString(sArgs, "nvim_buf_set_lines");
    AppendMsgPackArrayHeader(sArgs, 5);
    AppendMsgPackInteger(sArgs, 0);
    AppendMsgPackInteger(sArgs, 0);
    AppendMsgPackInteger(sArgs, -1);
    AppendMsgPackBoolean(sArgs, true);
    std::string sNormalized = NormalizeLineEndingsToLf(sText);
    size_t      nLines      = 1;
    for (char nChar : sNormalized)
        if (nChar == '\n')
            ++nLines;
    AppendMsgPackArrayHeader(sArgs, nLines);
    size_t nStart = 0;
    for (size_t nIndex = 0; nIndex <= sNormalized.size(); ++nIndex)
        if (nIndex == sNormalized.size() || sNormalized[nIndex] == '\n')
        {
            AppendMsgPackString(sArgs, sNormalized.substr(nStart, nIndex - nStart));
            nStart = nIndex + 1;
        }
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackString(sArgs, "nvim_win_set_cursor");
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackInteger(sArgs, 0);
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackInteger(sArgs, nLine + 1);
    AppendMsgPackInteger(sArgs, nColumn);
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackString(sArgs, "nvim_exec_lua");
    sArgs += BuildStateArguments(true);
    Value sResult;
    if (!CallRpcAndWait("nvim_call_atomic", sArgs, sResult) || sResult.nType != 3 || sResult.aItems.size() != 2 ||
        sResult.aItems[1].nType != 0 || sResult.aItems[0].nType != 3 || sResult.aItems[0].aItems.size() != 3)
        return false;
    const Value &sRevision = sResult.aItems[0].aItems[2];
    if (sRevision.nType != 3 || sRevision.aItems.size() != 9 || sRevision.aItems[0].nType != 1 ||
        sRevision.aItems[0].nInteger <= 0 || sRevision.aItems[1].nType != 1 || sRevision.aItems[1].nInteger < 0 ||
        sRevision.aItems[2].nType != 1 || sRevision.aItems[2].nInteger <= 0 || sRevision.aItems[4].nType != 1 ||
        sRevision.aItems[4].nInteger < 0 || sRevision.aItems[5].nType != 1 || sRevision.aItems[5].nInteger != 0)
        return false;
    m_sSession.nSyncedBuffer      = sRevision.aItems[0].nInteger;
    m_sSession.nSyncedChangedTick = sRevision.aItems[1].nInteger;
    m_sSession.nSyncedEpoch       = sRevision.aItems[2].nInteger;
    m_sSession.nSyncedSequence    = sRevision.aItems[4].nInteger;
    return true;
}
std::string Backend_NVIM::GetEditorText()
{
    size_t      nLength = static_cast<size_t>(m_sEditor.DocumentLength());
    std::string sText(nLength + 1, '\0');
    (void)m_sEditor.Send(Scintilla::GETTEXT, nLength + 1, reinterpret_cast<LPARAM>(sText.data()));
    sText.resize(nLength);
    return sText;
}
bool Backend_NVIM::ContainsNoNulBytes(const std::string &sText)
{
    for (unsigned char nChar : sText)
        if (nChar == 0)
            return false;
    return true;
}
std::string Backend_NVIM::NormalizeLineEndingsToLf(const std::string &sText)
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
std::string Backend_NVIM::DocumentLineEnding()
{
    LRESULT nEol = m_sEditor.Send(Scintilla::GETEOLMODE);
    if (m_sSession.sText.find('\n') != std::string::npos)
        nEol = m_sSession.sText.find("\r\n") != std::string::npos ? Scintilla::EOL_CRLF : Scintilla::EOL_LF;
    else if (m_sSession.sText.find('\r') != std::string::npos)
        nEol = Scintilla::EOL_CR;
    return nEol == Scintilla::EOL_CRLF ? "\r\n" : nEol == Scintilla::EOL_CR ? "\r" : "\n";
}
std::string Backend_NVIM::RestoreDocumentLineEndings(const std::string &sText)
{
    const std::string sEol = DocumentLineEnding();
    if (sEol == "\n")
        return sText;
    std::string sResult;
    for (char nChar : sText)
        if (nChar == '\n')
            sResult += sEol;
        else
            sResult += nChar;
    return sResult;
}
void Backend_NVIM::InvalidateSyncedRevision()
{
    m_sSession.nSyncedBuffer      = -1;
    m_sSession.nSyncedChangedTick = -1;
    m_sSession.nSyncedEpoch       = -1;
    m_sSession.nSyncedSequence    = -1;
}
bool Backend_NVIM::ApplyTextReplacement(size_t nStart, size_t nEnd, const std::string &sText)
{
    if (nStart > nEnd || nEnd > m_sSession.sText.size())
        return false;
    // Limit the comparison to the supplied replacement range, including on the
    // full-snapshot recovery path. Small line updates never scan the whole file.
    size_t nPrefix = 0;
    while (nStart < nEnd && nPrefix < sText.size() && m_sSession.sText[nStart] == sText[nPrefix])
    {
        ++nStart;
        ++nPrefix;
    }
    size_t nNewEnd = sText.size();
    while (nEnd > nStart && nNewEnd > nPrefix && m_sSession.sText[nEnd - 1] == sText[nNewEnd - 1])
    {
        --nEnd;
        --nNewEnd;
    }
    if (nStart == nEnd && nPrefix == nNewEnd)
        return true;
    (void)m_sEditor.Send(Scintilla::SETTARGETSTART, nStart);
    (void)m_sEditor.Send(Scintilla::SETTARGETEND, nEnd);
    const size_t  nNewLength = nNewEnd - nPrefix;
    const LRESULT nReplaced =
        m_sEditor.Send(Scintilla::REPLACETARGET, nNewLength, reinterpret_cast<LPARAM>(sText.data() + nPrefix));
    if (nReplaced != static_cast<LRESULT>(nNewLength))
        return false;
    m_sSession.sText.replace(nStart, nEnd - nStart, sText.data() + nPrefix, nNewLength);
    return true;
}
bool Backend_NVIM::StartNeovimProcess()
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
    std::wstring sConfigPath  = sPath.substr(0, nSlash) + L"\\config\\vimpad.lua";
    int          nConfigBytes = WideCharToMultiByte(
        CP_UTF8, 0, sConfigPath.data(), static_cast<int>(sConfigPath.size()), nullptr, 0, nullptr, nullptr);
    if (nConfigBytes <= 0)
        return false;
    m_sPluginConfigPath.resize(nConfigBytes);
    WideCharToMultiByte(
        CP_UTF8, 0, sConfigPath.data(), static_cast<int>(sConfigPath.size()), m_sPluginConfigPath.data(), nConfigBytes,
        nullptr, nullptr);
    std::wstring sNeovimDir = sPath.substr(0, nSlash) + L"\\neovim";
    sPath                   = sNeovimDir + L"\\bin\\nvim.exe";
    SECURITY_ATTRIBUTES sSecurity{sizeof(sSecurity), nullptr, TRUE};
    HANDLE              hChildInput = nullptr, hChildOutput = nullptr;
    if (!CreatePipe(&hChildInput, &m_sSession.hInput, &sSecurity, 0) ||
        !CreatePipe(&m_sSession.hOutput, &hChildOutput, &sSecurity, 0))
    {
        CloseHandleAndReset(hChildInput);
        CloseHandleAndReset(hChildOutput);
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
    std::wstring sCommand = L"\"" + sPath + L"\" --embed -u \"" + sNeovimDir + L"\\vim-motions-init.lua\" -i NONE -n";

    BOOL bStarted = CreateProcessW(
        sPath.c_str(), sCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &sStartup,
        &sProcess);
    CloseHandleAndReset(hChildInput);
    CloseHandleAndReset(hChildOutput);
    if (!bStarted)
        return false;
    CloseHandle(sProcess.hThread);
    m_sSession.hProcess = sProcess.hProcess;
    return true;
}
void Backend_NVIM::StopAndReportError(const wchar_t *pMessage)
{
    if (m_sSession.bFailed)
        return;
    m_sSession.bFailed = true;
    if (m_pFailureHandler)
        m_pFailureHandler(m_pFailureContext);
    StopNeovimProcess();
    MessageBoxW(m_hWindow, pMessage, L"Neovim prototype", MB_OK | MB_ICONERROR);
}
Backend_NVIM::SyncResult Backend_NVIM::SyncDocument(const Value &sResult)
{
    // ScopedTimer sTimer("Apply state to Scintilla");
    if (sResult.nType != 3 || sResult.aItems.size() != 9)
        return SyncResult::FAILED;
    for (size_t nIndex = 0; nIndex < 6; ++nIndex)
        if (sResult.aItems[nIndex].nType != 1)
            return SyncResult::FAILED;
    const int64_t nBuffer       = sResult.aItems[0].nInteger;
    const int64_t nChangedTick  = sResult.aItems[1].nInteger;
    const int64_t nEpoch        = sResult.aItems[2].nInteger;
    const int64_t nBaseSequence = sResult.aItems[3].nInteger;
    const int64_t nSequence     = sResult.aItems[4].nInteger;
    const int64_t nKind         = sResult.aItems[5].nInteger;
    const Value  &sPayload      = sResult.aItems[6];
    const Value  &sCursor       = sResult.aItems[7];
    const Value  &sModeState    = sResult.aItems[8];
    if (nBuffer <= 0 || nChangedTick < 0 || nEpoch <= 0 || nSequence < 0 || nKind < 0 || nKind > 2 ||
        (nKind == 0 ? sPayload.nType != 0 : sPayload.nType != 3) || sCursor.nType != 3 || sCursor.aItems.size() != 2 ||
        sCursor.aItems[0].nType != 1 || sCursor.aItems[0].nInteger < 1 || sCursor.aItems[1].nType != 1 ||
        sCursor.aItems[1].nInteger < 0 || sModeState.nType != 3 || sModeState.aItems.size() != 3 ||
        sModeState.aItems[0].nType != 2 || sModeState.aItems[0].sString.empty() || sModeState.aItems[1].nType != 3 ||
        sModeState.aItems[1].aItems.size() != 4 || sModeState.aItems[1].aItems[1].nType != 1 ||
        sModeState.aItems[1].aItems[2].nType != 1 || sModeState.aItems[2].nType != 2)
        return SyncResult::FAILED;
    if (nKind != 1 && (nBuffer != m_sSession.nSyncedBuffer || nEpoch != m_sSession.nSyncedEpoch ||
                       nBaseSequence != m_sSession.nSyncedSequence || nSequence < nBaseSequence ||
                       (nKind == 0 && (nSequence != nBaseSequence || nChangedTick != m_sSession.nSyncedChangedTick))))
        return SyncResult::RESYNC;
    const bool bDocumentChanged =
        m_sEditor.Send(Scintilla::GETDOCPOINTER) != m_sSession.nDocument || GetEditorText() != m_sSession.sText;
    if (bDocumentChanged)
        return SyncResult::SKIPPED;
    std::vector<NeovimLineUpdate> aUpdates;
    if (nKind == 2)
    {
        int64_t nExpectedSequence = nBaseSequence;
        int64_t nLineCount        = m_sEditor.Send(Scintilla::GETLINECOUNT);
        for (const Value &sUpdate : sPayload.aItems)
        {
            if (sUpdate.nType != 3 || sUpdate.aItems.size() != 4 || sUpdate.aItems[0].nType != 1 ||
                sUpdate.aItems[1].nType != 1 || sUpdate.aItems[2].nType != 1 || sUpdate.aItems[3].nType != 3)
                return SyncResult::FAILED;
            if (nExpectedSequence == INT64_MAX || sUpdate.aItems[0].nInteger != ++nExpectedSequence)
                return SyncResult::RESYNC;
            const int64_t nFirst = sUpdate.aItems[1].nInteger;
            const int64_t nLast  = sUpdate.aItems[2].nInteger;
            const auto   &aLines = sUpdate.aItems[3].aItems;
            if (nFirst == -1 && nLast == -1 && aLines.empty())
                continue; // changedtick advanced without changing text.
            if (nFirst < 0 || nLast < nFirst || nLast > nLineCount)
                return SyncResult::RESYNC;
            NeovimLineUpdate sLineUpdate{static_cast<size_t>(nFirst), static_cast<size_t>(nLast), {}};
            for (const Value &sLine : aLines)
            {
                if (sLine.nType != 2 || sLine.sString.find('\n') != std::string::npos ||
                    !ContainsNoNulBytes(sLine.sString))
                    return SyncResult::FAILED;
                sLineUpdate.aLines.push_back(sLine.sString);
            }
            // Neovim reports deleting every line as an empty replacement; both
            // editors retain one implicit empty logical line afterward.
            nLineCount = std::max<int64_t>(1, nLineCount + static_cast<int64_t>(aLines.size()) - (nLast - nFirst));
            aUpdates.push_back(std::move(sLineUpdate));
        }
        if (nExpectedSequence != nSequence)
            return SyncResult::RESYNC;
    }
    struct StateUpdateGuard
    {
        bool &bApplying;
        explicit StateUpdateGuard(bool &bFlag) : bApplying(bFlag) { bApplying = true; }
        ~StateUpdateGuard() { bApplying = false; }
    } sGuard(m_sSession.bApplyingState);
    if (nKind == 1)
    {
        std::string sText;
        for (size_t nIndex = 0; nIndex < sPayload.aItems.size(); ++nIndex)
        {
            if (sPayload.aItems[nIndex].nType != 2)
                return SyncResult::FAILED;
            if (nIndex)
                sText += '\n';
            sText += sPayload.aItems[nIndex].sString;
        }
        const std::string sNewText = RestoreDocumentLineEndings(sText);
        if (sPayload.aItems.empty() || !ApplyTextReplacement(0, m_sSession.sText.size(), sNewText))
            return SyncResult::FAILED;
    }
    else if (nKind == 2)
    {
        const std::string sEol = DocumentLineEnding();
        for (const auto &sUpdate : aUpdates)
        {
            const size_t nLineCount = static_cast<size_t>(m_sEditor.Send(Scintilla::GETLINECOUNT));
            const size_t nLength    = m_sSession.sText.size();
            const size_t nStart =
                sUpdate.nFirstLine == nLineCount
                    ? nLength
                    : static_cast<size_t>(m_sEditor.Send(Scintilla::POSITIONFROMLINE, sUpdate.nFirstLine));
            const size_t nEnd =
                sUpdate.nLastLine == nLineCount
                    ? nLength
                    : static_cast<size_t>(m_sEditor.Send(Scintilla::POSITIONFROMLINE, sUpdate.nLastLine));
            const size_t nPreviousEnd =
                sUpdate.nFirstLine
                    ? static_cast<size_t>(m_sEditor.Send(Scintilla::GETLINEENDPOSITION, sUpdate.nFirstLine - 1))
                    : 0;
            const auto sReplacement = BuildLineReplacement(sUpdate, nLineCount, nStart, nEnd, nPreviousEnd, sEol);
            if (!ApplyTextReplacement(sReplacement.nStart, sReplacement.nEnd, sReplacement.sText))
                return SyncResult::FAILED;
        }
    }
    const Mode eMode = static_cast<Mode>(sModeState.aItems[0].sString[0]);
    if (eMode != m_sSession.eMode)
    {
        const bool bInsert = eMode == Mode::Insert || eMode == Mode::Replace;
        (void)m_sEditor.Send(
            Scintilla::SETCARETSTYLE, bInsert ? Scintilla::CARETSTYLE_LINE : Scintilla::CARETSTYLE_BLOCK);
        (void)m_sEditor.Send(Scintilla::SETCARETPERIOD, bInsert ? m_nOriginalCaretPeriod : 0);
        m_sSession.eMode = eMode;
    }
    const LRESULT nLine   = static_cast<LRESULT>(sCursor.aItems[0].nInteger - 1);
    const LRESULT nStart  = m_sEditor.Send(Scintilla::POSITIONFROMLINE, nLine);
    const LRESULT nCaret  = nStart + static_cast<LRESULT>(sCursor.aItems[1].nInteger);
    const bool    bVisual = eMode == Mode::VisualCharacter || eMode == Mode::VisualLine || eMode == Mode::VisualBlock;
    if (!bVisual)
        (void)m_sEditor.Send(Scintilla::GOTOPOS, nCaret);
    else
    {
        const Value  &sAnchor      = sModeState.aItems[1];
        const LRESULT nAnchorLine  = static_cast<LRESULT>(sAnchor.aItems[1].nInteger - 1);
        const LRESULT nAnchorStart = m_sEditor.Send(Scintilla::POSITIONFROMLINE, nAnchorLine);
        if (nAnchorLine < 0 || nAnchorStart < 0 || sAnchor.aItems[2].nInteger < 1)
            return SyncResult::FAILED;
        const LRESULT nAnchor = nAnchorStart + static_cast<LRESULT>(sAnchor.aItems[2].nInteger - 1);
        if (eMode == Mode::VisualLine)
        {
            const LRESULT nFirst    = std::min(nLine, nAnchorLine);
            const LRESULT nLast     = std::max(nLine, nAnchorLine);
            const LRESULT nFirstPos = m_sEditor.Send(Scintilla::POSITIONFROMLINE, nFirst);
            LRESULT       nEnd      = m_sEditor.Send(Scintilla::POSITIONFROMLINE, nLast + 1);
            if (nEnd < 0)
                nEnd = m_sEditor.DocumentLength();
            (void)m_sEditor.Send(
                Scintilla::SETSEL, nLine < nAnchorLine ? nEnd : nFirstPos, nLine < nAnchorLine ? nFirstPos : nEnd);
        }
        else if (eMode == Mode::VisualCharacter)
        {
            const LRESULT nEnd = sModeState.aItems[2].sString == "exclusive" && nAnchor != nCaret
                                     ? std::max(nAnchor, nCaret)
                                     : m_sEditor.Send(Scintilla::POSITIONAFTER, std::max(nAnchor, nCaret));
            (void)m_sEditor.Send(
                Scintilla::SETSEL, nCaret < nAnchor ? nEnd : nAnchor, nCaret < nAnchor ? nCaret : nEnd);
        }
        else
        {
            const LRESULT nCaretColumn  = m_sEditor.Send(Scintilla::GETCOLUMN, nCaret);
            const LRESULT nAnchorColumn = m_sEditor.Send(Scintilla::GETCOLUMN, nAnchor);
            const LRESULT nLeft         = std::min(nCaretColumn, nAnchorColumn);
            const LRESULT nRight        = std::max(nCaretColumn, nAnchorColumn);
            (void)m_sEditor.Send(Scintilla::CLEARSELECTIONS);
            for (LRESULT nRow = std::min(nLine, nAnchorLine); nRow <= std::max(nLine, nAnchorLine); ++nRow)
            {
                const LRESULT nLeftPos  = m_sEditor.Send(Scintilla::FINDCOLUMN, nRow, nLeft);
                const LRESULT nRightPos = m_sEditor.Send(Scintilla::FINDCOLUMN, nRow, nRight);
                const LRESULT nLineEnd  = m_sEditor.Send(Scintilla::GETLINEENDPOSITION, nRow);
                const LRESULT nEnd =
                    nRightPos < nLineEnd ? m_sEditor.Send(Scintilla::POSITIONAFTER, nRightPos) : nLineEnd;
                (void)m_sEditor.Send(
                    nRow == std::min(nLine, nAnchorLine) ? Scintilla::SETSELECTION : Scintilla::ADDSELECTION, nEnd,
                    nLeftPos);
            }
        }
    }
    EnsureCaretVisible();
    m_sSession.nKnownCaret        = m_sEditor.Caret();
    m_nViewAnchor                 = m_sEditor.Send(Scintilla::GETANCHOR);
    // Never acknowledge stale, skipped or partially applied snapshots.
    m_sSession.nSyncedBuffer      = nBuffer;
    m_sSession.nSyncedChangedTick = nChangedTick;
    m_sSession.nSyncedEpoch       = nEpoch;
    m_sSession.nSyncedSequence    = nSequence;
    return SyncResult::APPLIED;
}
bool Backend_NVIM::PollResponses()
{
    // ScopedTimer sTimer("Poll Neovim responses");
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
        const auto eFrame = m_sSession.sIncomingFrame.Scan(m_sSession.sIncoming);
        if (eFrame == MsgPackFrame::Result::INVALID)
            return false;
        if (eFrame == MsgPackFrame::Result::NEED_MORE)
        {
            break;
        }
        m_sSession.nReadOffset   = 0;
        m_sSession.bBufferedRead = true;
        Value      sResponse;
        const bool bComplete     = DecodeMsgPackValue(sResponse);
        m_sSession.bBufferedRead = false;
        if (!bComplete || m_sSession.nReadOffset != m_sSession.sIncomingFrame.Size())
            return false;
        m_sSession.sIncoming.erase(0, m_sSession.nReadOffset);
        m_sSession.sIncomingFrame.Reset();
        if (sResponse.nType == 3 && !sResponse.aItems.empty() && sResponse.aItems[0].nInteger == 2)
        {
            if (!HandleNotification(sResponse))
                return false;
            continue;
        }
        if (sResponse.nType != 3 || sResponse.aItems.size() != 4 || sResponse.aItems[0].nInteger != 1 ||
            sResponse.aItems[2].nType != 0)
            return false;
        if (m_sSession.nCaretId && sResponse.aItems[1].nInteger == m_sSession.nCaretId)
        {
            m_sSession.nCaretId = 0;
            if (m_sSession.nKnownCaret != m_sSession.nRequestedCaret ||
                (m_bSyncSelection && m_nSelectionRevision != m_sSession.nRevision))
            {
                if (!RequestCaretSync())
                    return false;
            }
            else
            {
                m_bSyncSelection = false;
                for (const auto &sKey : m_sSession.aPendingKeys)
                    if (!SendKey(sKey))
                        return false;
                m_sSession.aPendingKeys.clear();
                if (!RequestNeovimState())
                    return false;
            }
            continue;
        }
        if (sResponse.aItems[1].nInteger != m_sSession.nStateId)
            return false;
        m_sSession.nStateId = 0;
        if (m_bMouseSelecting || m_sSession.nCaretId || m_sSession.nStateRevision != m_sSession.nRevision)
        {
            if (!RequestNeovimState())
                return false;
            continue;
        }
        const auto eResult = SyncDocument(sResponse.aItems[3]);
        if (eResult == SyncResult::FAILED)
            return false;
        if (eResult == SyncResult::RESYNC)
        {
            InvalidateSyncedRevision();
            if (!RequestNeovimState())
                return false;
            continue;
        }
        if (m_sSession.bSavePending && !m_sSession.nCaretId && m_sSession.aPendingKeys.empty())
        {
            m_sSession.bSavePending = false;
            if (m_pSaveHandler && m_sEditor.Send(Scintilla::GETDOCPOINTER) == m_sSession.nDocument &&
                GetEditorText() == m_sSession.sText)
                m_pSaveHandler(m_hWindow);
        }
    }
    return true;
}
bool Backend_NVIM::PassesKeyToHost(WPARAM nKey) const
{
    if (m_sSession.bFailed)
        return true;
    return std::any_of(
        m_aPassThroughKeys.begin(), m_aPassThroughKeys.end(),
        [nKey](const HostKey &sKey) { return sKey.Matches(nKey); });
}

bool Backend_NVIM::PrepareHostKey()
{
    if (m_sSession.bFailed)
        return true;
    const ULONGLONG nDeadline = GetTickCount64() + 5000;
    while (m_sSession.nStateId || m_sSession.nCaretId)
    {
        if (GetTickCount64() >= nDeadline || !PollResponses())
        {
            StopAndReportError(L"Could not apply pending Neovim edits before passing a shortcut to the host.");
            return false;
        }
        if (m_sSession.nStateId || m_sSession.nCaretId)
            Sleep(1);
    }
    return true;
}

bool Backend_NVIM::LoadPluginConfig()
{
    // Evaluate a separate returned table; no plugin settings are put in init.lua.
    static constexpr char PluginConfigLua[] = R"lua(
local sPath = ...
local bOk, aResult = xpcall(function()
  local _, sStatError, nStatError = vim.uv.fs_stat(sPath)
  if nStatError == 'ENOENT' then return {} end
  if sStatError then error(sStatError) end
  local tConfig = dofile(sPath)
  if type(tConfig) ~= 'table' then error('vimpad.lua must return a table') end
  for sName in pairs(tConfig) do
    if sName ~= 'passthrough_keys' then error('Unknown setting: ' .. tostring(sName)) end
  end
  local aKeys = tConfig.passthrough_keys
  if aKeys == nil then return {} end
  if type(aKeys) ~= 'table' then error('passthrough_keys must be a list of key strings') end
  local nCount = 0
  for nIndex, sKey in pairs(aKeys) do
    if type(nIndex) ~= 'number' or nIndex < 1 or nIndex % 1 ~= 0 or type(sKey) ~= 'string' then
      error('passthrough_keys must be a list of key strings')
    end
    nCount = nCount + 1
  end
  for nIndex = 1, nCount do
    if aKeys[nIndex] == nil then error('passthrough_keys must not have gaps') end
  end
  return aKeys
end, debug.traceback)
return {bOk and '' or tostring(aResult), bOk and aResult or {}}
)lua";
    std::string           sArgs;
    AppendMsgPackArrayHeader(sArgs, 2);
    AppendMsgPackString(sArgs, PluginConfigLua);
    AppendMsgPackArrayHeader(sArgs, 1);
    AppendMsgPackString(sArgs, m_sPluginConfigPath);
    Value sConfig;
    if (!CallRpcAndWait("nvim_exec_lua", sArgs, sConfig) || sConfig.nType != 3 || sConfig.aItems.size() != 2 ||
        sConfig.aItems[0].nType != 2 || sConfig.aItems[1].nType != 3)
    {
        StopAndReportError(L"Could not read config\\vimpad.lua.");
        return false;
    }
    std::string          sError = sConfig.aItems[0].sString;
    std::vector<HostKey> aKeys;
    if (sError.empty())
        for (const auto &sValue : sConfig.aItems[1].aItems)
        {
            HostKey sKey;
            if (sValue.nType != 2 || !HostKey::Parse(sValue.sString, sKey))
            {
                sError = "Invalid passthrough key: " + sValue.sString;
                break;
            }
            aKeys.push_back(sKey);
        }
    if (!sError.empty())
    {
        int nSize = MultiByteToWideChar(CP_UTF8, 0, sError.data(), static_cast<int>(sError.size()), nullptr, 0);
        std::wstring sDetail(nSize, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, sError.data(), static_cast<int>(sError.size()), sDetail.data(), nSize);
        StopAndReportError((L"VimPad++ configuration failed (config\\vimpad.lua):\n\n" + sDetail).c_str());
        return false;
    }
    m_aPassThroughKeys = std::move(aKeys);
    return true;
}

BOOL Backend_NVIM::bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (nMessage == WM_LBUTTONDOWN && !(wParam & MK_CONTROL) && !(GetKeyState(VK_MENU) & 0x8000) && !m_sSession.bFailed)
    {
        // Drain pre-gesture edits before Scintilla chooses positions in the text.
        if (!PrepareHostKey())
            return TRUE;
        m_bMouseSelecting = true;
        ++m_sSession.nRevision;
    }
    if (m_bMouseSelecting && (nMessage == WM_LBUTTONDOWN || nMessage == WM_MOUSEMOVE || nMessage == WM_LBUTTONUP ||
                              nMessage == WM_CAPTURECHANGED))
    {
        ++m_nMouseDepth;
        if (nMessage == WM_LBUTTONUP || nMessage == WM_CAPTURECHANGED)
            m_bMouseFinishing = true;
        // Sample after Scintilla handles the mouse, also in the standalone host
        // where SCN_UPDATEUI is not forwarded. ReleaseCapture can reenter here.
        DefSubclassProc(hWindow, nMessage, wParam, lParam);
        --m_nMouseDepth;
        if (!m_nMouseDepth && m_bMouseFinishing)
            FinishMouseSelection();
        return TRUE;
    }
    if (nMessage == WM_KILLFOCUS)
        m_bPassThroughKey = false;
    if (nMessage == WM_KEYDOWN || nMessage == WM_SYSKEYDOWN)
    {
        m_bPassThroughKey  = PassesKeyToHost(wParam);
        m_nPassThroughScan = lParam & 0x00ff0000;
        if (m_bPassThroughKey)
        {
            m_nSuppressedChar = -1;
            return FALSE;
        }
    }
    if ((nMessage == WM_CHAR || nMessage == WM_SYSCHAR) && m_bPassThroughKey &&
        (lParam & 0x00ff0000) == m_nPassThroughScan)
        return FALSE;
    if (nMessage == CaretVisibilityMessage)
    {
        PositionCommandBar();
        // Scintilla may defer rewrapping after a resize/zoom until painting.
        UpdateWindow(hWindow);
        EnsureCaretVisible();
        return TRUE;
    }
    // The subclass runs before Scintilla handles layout/zoom. Check the final
    // geometry on the next message, without interfering with ordinary scrolling.
    if (nMessage == WM_SIZE || nMessage == WM_SETFONT || nMessage == Scintilla::SETZOOM ||
        nMessage == Scintilla::ZOOMIN || nMessage == Scintilla::ZOOMOUT ||
        (nMessage == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)))
        PostMessageW(hWindow, CaretVisibilityMessage, 0, 0);
    if (nMessage == WM_TIMER && wParam == m_sSession.nTimer && m_sSession.nTimer)
    {
        if (!PollResponses())
            StopAndReportError(L"Neovim communication failed.");
        return TRUE;
    }
    else if ((nMessage == WM_CHAR || nMessage == WM_KEYDOWN) && !m_sSession.bFailed)
    {
        // ScopedTimer sTimer("Handle input message");
        std::string sKey;
        if (nMessage == WM_KEYDOWN)
        {
            m_nSuppressedChar   = -1;
            const bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool bShift   = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool bAlt     = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (wParam == VK_ESCAPE && !bControl && !bAlt && !bShift)
            {
                sKey              = "<Esc>";
                m_nSuppressedChar = 27;
            }
            else if (bControl && !bAlt && wParam >= 'A' && wParam <= 'Z')
            {
                sKey              = std::string("<C-") + static_cast<char>('a' + wParam - 'A') + ">";
                m_nSuppressedChar = static_cast<int>(wParam - 'A' + 1);
            }
            else if (
                bControl && !bAlt &&
                (wParam == VK_RETURN || wParam == VK_BACK || wParam == VK_TAB || wParam == VK_SPACE))
            {
                const char *pName = wParam == VK_RETURN ? "CR"
                                    : wParam == VK_BACK ? "BS"
                                    : wParam == VK_TAB  ? "Tab"
                                                        : "Space";
                sKey              = std::string("<C-") + pName + ">";
                m_nSuppressedChar = wParam == VK_RETURN ? 10 : wParam == VK_BACK ? 127 : wParam == VK_TAB ? 9 : 0;
            }
            else
            {
                const char *pName = nullptr;
                switch (wParam)
                {
                case VK_LEFT:
                    pName = "Left";
                    break;
                case VK_RIGHT:
                    pName = "Right";
                    break;
                case VK_UP:
                    pName = "Up";
                    break;
                case VK_DOWN:
                    pName = "Down";
                    break;
                case VK_DELETE:
                    pName = "Del";
                    break;
                case VK_HOME:
                    pName = "Home";
                    break;
                case VK_END:
                    pName = "End";
                    break;
                case VK_PRIOR:
                    pName = "PageUp";
                    break;
                case VK_NEXT:
                    pName = "PageDown";
                    break;
                case VK_INSERT:
                    pName = "Insert";
                    break;
                default:
                    break;
                }
                if (pName)
                {
                    sKey = "<";
                    if (bControl)
                        sKey += "C-";
                    if (bShift)
                        sKey += "S-";
                    sKey += pName;
                    sKey += ">";
                }
                else if (wParam >= VK_F1 && wParam <= VK_F12)
                {
                    sKey = "<";
                    if (bControl)
                        sKey += "C-";
                    if (bShift)
                        sKey += "S-";
                    sKey += "F" + std::to_string(wParam - VK_F1 + 1) + ">";
                }
                else
                {
                    // TranslateMessage has already queued any WM_CHAR. Consume the
                    // keydown so Scintilla cannot act before that character arrives.
                    return TRUE;
                }
            }
        }
        else if (m_nSuppressedChar == static_cast<int>(wParam))
        {
            m_nSuppressedChar = -1;
            return TRUE;
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
            {
                return TRUE;
            }
            sKey.assign(aUtf8, nBytes);
        }
        const bool bDocumentChanged =
            m_sEditor.Send(Scintilla::GETDOCPOINTER) != m_sSession.nDocument || GetEditorText() != m_sSession.sText;
        if (bDocumentChanged)
        {
            std::string sText = GetEditorText();
            if (!ContainsNoNulBytes(sText))
            {
                StopAndReportError(
                    L"The Neovim prototype supports ASCII text with LF line "
                    L"endings only.");
                return TRUE;
            }
            LRESULT nPosition = m_sEditor.Caret();
            LRESULT nLine     = m_sEditor.Send(Scintilla::LINEFROMPOSITION, nPosition);
            LRESULT nColumn   = nPosition - m_sEditor.LineStart(nPosition);
            if (m_sSession.nStateId || !SyncNeovim(sText, nLine, nColumn))
            {
                StopAndReportError(L"Could not synchronize the document with Neovim.");
                return TRUE;
            }
            m_sSession.sText       = sText;
            m_sSession.nDocument   = m_sEditor.Send(Scintilla::GETDOCPOINTER);
            m_sSession.nKnownCaret = nPosition;
        }
        ++m_sSession.nRevision;
        if (m_sSession.nCaretId)
        {
            m_sSession.aPendingKeys.push_back(sKey);
            return TRUE;
        }
        if (!SendKey(sKey))
        {
            StopAndReportError(L"Neovim communication failed.");
            return TRUE;
        }
        if (!RequestNeovimState())
        {
            StopAndReportError(L"Neovim communication failed.");
            return TRUE;
        }
        if (!m_sSession.nTimer)
        {
            if (SetTimer(hWindow, NeovimResponseTimerId, 10, nullptr))
                m_sSession.nTimer = NeovimResponseTimerId;
        }
        if (!m_sSession.nTimer)
            StopAndReportError(L"Could not start the Neovim response timer.");
        return TRUE;
    }
    return FALSE;
}
BOOL Backend_NVIM::Install()
{
    m_nOriginalCaretStyle  = m_sEditor.Send(Scintilla::GETCARETSTYLE);
    m_nOriginalCaretPeriod = m_sEditor.Send(Scintilla::GETCARETPERIOD);
    m_sSession.nDocument   = m_sEditor.Send(Scintilla::GETDOCPOINTER);
    m_sSession.sText       = GetEditorText();
    if (!ContainsNoNulBytes(m_sSession.sText))
    {
        StopAndReportError(
            L"The Neovim prototype does not support NUL bytes "
            L"only.");
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!StartNeovimProcess())
    {
        StopAndReportError(
            L"Could not start bundled Neovim. Check that neovim\\bin\\nvim.exe "
            L"is installed beside the plugin.");
        SetLastError(ERROR_PROCESS_ABORTED);
        return FALSE;
    }
    if (!CreateCommandBar())
    {
        StopAndReportError(L"Could not create the Neovim command bar.");
        return FALSE;
    }
    if (!AttachUi())
    {
        StopAndReportError(
            L"Could not attach the Neovim user interface. Check your config and that neovim\\vim-motions-init.lua "
            L"exists. Startup may have timed out.");
        return FALSE;
    }
    std::string sArgs;
    AppendMsgPackArrayHeader(sArgs, 1);
    AppendMsgPackString(sArgs, "get(g:, 'vim_motions_startup_error', 'Plugin startup script did not run')");
    Value sStartupError;
    if (!CallRpcAndWait("nvim_eval", sArgs, sStartupError) || sStartupError.nType != 2)
    {
        StopAndReportError(L"Could not check Neovim startup. Check your config for errors or a startup timeout.");
        return FALSE;
    }
    if (!sStartupError.sString.empty())
    {
        int          nSize    = MultiByteToWideChar(CP_UTF8, 0, sStartupError.sString.c_str(), -1, nullptr, 0);
        std::wstring sMessage = L"Neovim configuration failed:\n\n";
        if (nSize > 0)
        {
            std::wstring sDetail(static_cast<size_t>(nSize), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, sStartupError.sString.c_str(), -1, sDetail.data(), nSize);
            sDetail.pop_back();
            sMessage += sDetail;
        }
        StopAndReportError(sMessage.c_str());
        return FALSE;
    }
    if (!LoadPluginConfig())
        return FALSE;
    LRESULT nPosition      = m_sEditor.Caret();
    m_sSession.nKnownCaret = nPosition;
    m_nViewAnchor          = m_sEditor.Send(Scintilla::GETANCHOR);
    LRESULT nLine          = m_sEditor.Send(Scintilla::LINEFROMPOSITION, nPosition);
    LRESULT nColumn        = nPosition - m_sEditor.LineStart(nPosition);
    if (!SyncNeovim(m_sSession.sText, nLine, nColumn))
    {
        StopAndReportError(L"Neovim initialization failed or timed out.");
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    (void)m_sEditor.Send(Scintilla::SETCARETSTYLE, Scintilla::CARETSTYLE_BLOCK);
    (void)m_sEditor.Send(Scintilla::SETCARETPERIOD, 0);
    m_sSession.eMode = Mode::Normal;
    if (!SetTimer(m_hWindow, NeovimResponseTimerId, 10, nullptr))
    {
        StopAndReportError(L"Could not start the Neovim response timer.");
        return FALSE;
    }
    m_sSession.nTimer = NeovimResponseTimerId;
    return TRUE;
}
BOOL Backend_NVIM::Uninstall()
{
    StopNeovimProcess();
    (void)m_sEditor.Send(Scintilla::SETCARETSTYLE, m_nOriginalCaretStyle);
    (void)m_sEditor.Send(Scintilla::SETCARETPERIOD, m_nOriginalCaretPeriod);
    return TRUE;
}

void Backend_NVIM::SuspendView()
{
    if (!m_hWindow)
        return;
    if (m_sSession.nTimer)
        KillTimer(m_hWindow, m_sSession.nTimer);
    m_sSession.nTimer = 0;
    if (m_hCommandBar)
        DestroyWindow(m_hCommandBar);
    m_hCommandBar = nullptr;
    if (m_bScrollPastEnd)
        (void)m_sEditor.Send(Scintilla::SETENDATLASTLINE, m_bOriginalEndAtLastLine);
    m_bScrollPastEnd = false;
    (void)m_sEditor.Send(Scintilla::SETCARETSTYLE, m_nOriginalCaretStyle);
    (void)m_sEditor.Send(Scintilla::SETCARETPERIOD, m_nOriginalCaretPeriod);
    m_bPassThroughKey = false;
    m_nSuppressedChar = -1;
    m_bMouseSelecting = false;
    m_bMouseFinishing = false;
    m_hWindow         = nullptr;
    m_sEditor         = EditorText(nullptr);
}

bool Backend_NVIM::ResumeView(HWND hWindow)
{
    m_hWindow              = hWindow;
    m_sEditor              = EditorText(hWindow);
    m_nOriginalCaretStyle  = m_sEditor.Send(Scintilla::GETCARETSTYLE);
    m_nOriginalCaretPeriod = m_sEditor.Send(Scintilla::GETCARETPERIOD);
    if (m_sSession.bFailed)
        return true;
    if (!CreateCommandBar())
        return false;
    const bool bInsert = m_sSession.eMode == Mode::Insert || m_sSession.eMode == Mode::Replace;
    (void)m_sEditor.Send(Scintilla::SETCARETSTYLE, bInsert ? Scintilla::CARETSTYLE_LINE : Scintilla::CARETSTYLE_BLOCK);
    (void)m_sEditor.Send(Scintilla::SETCARETPERIOD, bInsert ? m_nOriginalCaretPeriod : 0);
    if (!SetTimer(hWindow, NeovimResponseTimerId, 10, nullptr))
        return false;
    m_sSession.nTimer         = NeovimResponseTimerId;
    m_sSession.bApplyingState = true;
    (void)m_sEditor.Send(Scintilla::SETSEL, m_nViewAnchor, m_sSession.nKnownCaret);
    UpdateCommandBar();
    m_sSession.bApplyingState = false;
    return true;
}

bool Backend_NVIM::CreateCommandBar()
{
    m_hCommandBar = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_BORDER | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, m_hWindow, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!m_hCommandBar)
        return false;
    SendMessageW(m_hCommandBar, WM_SETFONT, SendMessageW(m_hWindow, WM_GETFONT, 0, 0), FALSE);
    return true;
}
