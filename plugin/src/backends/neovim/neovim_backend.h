#pragma once

#include "../common/backend_common.h"

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class Backend_NVIM : public Backend
{
  public:
    BOOL Install(HWND hScintillaWindow) override;
    BOOL Uninstall(HWND hScintillaWindow) override;

    BOOL bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) override;

  private:
    static constexpr UINT_PTR NeovimResponseTimerId = 0x4E56494D;
    struct Value
    {
        int                nType    = 0; // 0 nil, 1 integer, 2 string, 3 array, 4 other
        int64_t            nInteger = 0;
        std::string        sString;
        std::vector<Value> aItems;
    };
    struct Session
    {
        HWND        hWindow   = nullptr;
        LRESULT     nDocument = 0;
        HANDLE      hProcess  = nullptr;
        HANDLE      hInput    = nullptr;
        HANDLE      hOutput   = nullptr;
        unsigned    nId       = 0;
        std::string sText;
        bool        bFailed = false;
        std::string sIncoming;
        size_t      nReadOffset    = 0;
        bool        bBufferedRead  = false;
        unsigned    nStateId       = 0;
        unsigned    nRevision      = 0;
        unsigned    nStateRevision = 0;
        UINT_PTR    nTimer         = 0;
    };
    Session m_sSession;
    void    Close(HANDLE &hHandle);
    void    Stop();
    void    Number(std::string &sData, uint64_t nValue, int nBytes);
    void    String(std::string &sData, const std::string &sValue);
    bool    Read(void *pData, size_t nSize);
    bool    ReadNumber(int nBytes, uint64_t &nValue);
    bool    Decode(Value &sValue, int nDepth = 0);
    void    Array(std::string &sData, size_t nSize);
    void    Integer(std::string &sData, int64_t nValue);
    void    Boolean(std::string &sData, bool bValue);
    bool SendRpc(const std::string &sMethod, const std::string &sArguments, unsigned &nId, bool bNotification = false);
    bool Rpc(const std::string &sMethod, const std::string &sArguments, Value &sResult);
    std::string StateArguments();
    bool        RequestState();
    bool        SyncDocument(const std::string &sText, LRESULT nLine, LRESULT nColumn);
    std::string Text(HWND hWindow);
    bool        Supported(const std::string &sText);
    std::string Normalize(const std::string &sText);
    std::string RestoreEol(HWND hWindow, const std::string &sText);
    bool        Start();
    void        Fail(const wchar_t *pMessage);
    bool        ApplyState(const Value &sResult);
    bool        PollResponses();
};