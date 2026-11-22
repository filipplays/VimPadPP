#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <windows.h>

namespace RpcPipe
{
// The parent's write handle must support overlapped I/O; anonymous pipes do not.
// The inherited child end remains synchronous for Neovim's standard input.
inline bool CreateInput(HANDLE &hParent, HANDLE &hChild)
{
    static LONG        nSequence = 0;
    const std::wstring sName     = L"\\\\.\\pipe\\VimPadPP-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(GetTickCount64()) + L"-" +
                                   std::to_wstring(InterlockedIncrement(&nSequence));
    hParent                      = CreateNamedPipeW(
        sName.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
    if (hParent == INVALID_HANDLE_VALUE)
    {
        hParent = nullptr;
        return false;
    }
    SECURITY_ATTRIBUTES sSecurity{sizeof(sSecurity), nullptr, TRUE};
    hChild = CreateFileW(sName.c_str(), GENERIC_READ, 0, &sSecurity, OPEN_EXISTING, 0, nullptr);
    if (hChild == INVALID_HANDLE_VALUE)
    {
        hChild = nullptr;
        CloseHandle(hParent);
        hParent = nullptr;
        return false;
    }
    // The local client has already connected. This completes the server side
    // without leaving an outstanding connect operation or inheritable server end.
    OVERLAPPED sConnect{};
    if (!ConnectNamedPipe(hParent, &sConnect) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        CancelIoEx(hParent, &sConnect);
        DWORD nUnused = 0;
        GetOverlappedResult(hParent, &sConnect, &nUnused, TRUE);
        CloseHandle(hChild);
        CloseHandle(hParent);
        hChild = hParent = nullptr;
        return false;
    }
    return true;
}

inline bool Write(HANDLE hPipe, HANDLE hProcess, std::string_view sBytes, ULONGLONG nDeadline)
{
    OVERLAPPED sWrite{};
    sWrite.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!sWrite.hEvent)
        return false;
    bool bOk = true;
    while (!sBytes.empty())
    {
        const ULONGLONG nNow = GetTickCount64();
        if (nNow >= nDeadline)
        {
            bOk = false;
            break;
        }
        ResetEvent(sWrite.hEvent);
        const DWORD nChunk   = static_cast<DWORD>(std::min<size_t>(sBytes.size(), 65536));
        DWORD       nWritten = 0;
        if (!WriteFile(hPipe, sBytes.data(), nChunk, &nWritten, &sWrite))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                bOk = false;
                break;
            }
            HANDLE      aWait[] = {sWrite.hEvent, hProcess};
            const DWORD nWait =
                WaitForMultipleObjects(hProcess ? 2 : 1, aWait, FALSE, static_cast<DWORD>(nDeadline - nNow));
            if (nWait != WAIT_OBJECT_0)
            {
                // Keep OVERLAPPED and the caller's buffer alive until cancellation
                // completes. No worker thread or pending write survives a timeout.
                CancelIoEx(hPipe, &sWrite);
                GetOverlappedResult(hPipe, &sWrite, &nWritten, TRUE);
                bOk = false;
                break;
            }
            if (!GetOverlappedResult(hPipe, &sWrite, &nWritten, FALSE))
            {
                bOk = false;
                break;
            }
        }
        if (!nWritten || nWritten > nChunk)
        {
            bOk = false;
            break;
        }
        sBytes.remove_prefix(nWritten);
    }
    CloseHandle(sWrite.hEvent);
    return bOk;
}
} // namespace RpcPipe
