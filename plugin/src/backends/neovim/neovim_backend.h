#pragma once

#include "../common/backend_common.h"
#include "../common/editor_text.h"

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class Backend_NVIM : public Backend
{
  public:
    explicit Backend_NVIM(HWND hWindow) : Backend(hWindow), m_sEditor(hWindow) {}
    BOOL Install() override;
    BOOL Uninstall() override;
    void OnCaretMoved(LRESULT nPosition) override;

    BOOL bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) override;

  private:
    static constexpr UINT_PTR NeovimResponseTimerId = 0x4E56494D;
    enum class Mode : char
    {
        Normal          = 'n',
        Insert          = 'i',
        Replace         = 'R',
        VisualCharacter = 'v',
        VisualLine      = 'V',
        VisualBlock     = '\x16'
    };
    struct Value
    {
        int                nType    = 0; // 0 nil, 1 integer, 2 string, 3 array, 4 other
        int64_t            nInteger = 0;
        std::string        sString;
        std::vector<Value> aItems;
    };
    struct Session
    {
        LRESULT                  nDocument = 0;
        HANDLE                   hProcess  = nullptr;
        HANDLE                   hInput    = nullptr;
        HANDLE                   hOutput   = nullptr;
        unsigned                 nId       = 0;
        std::string              sText;
        bool                     bFailed = false;
        std::string              sIncoming;
        size_t                   nReadOffset     = 0;
        bool                     bBufferedRead   = false;
        unsigned                 nStateId        = 0;
        unsigned                 nRevision       = 0;
        unsigned                 nStateRevision  = 0;
        unsigned                 nCaretId        = 0;
        UINT_PTR                 nTimer          = 0;
        LRESULT                  nKnownCaret     = 0;
        LRESULT                  nRequestedCaret = 0;
        bool                     bApplyingState  = false;
        Mode                     eMode           = Mode::Normal;
        std::vector<std::string> aPendingKeys;
    };
    Session    m_sSession;
    EditorText m_sEditor;
    int        m_nSuppressedChar      = -1;
    LRESULT    m_nOriginalCaretStyle  = 0;
    LRESULT    m_nOriginalCaretPeriod = 0;
    void       CloseHandleAndReset(HANDLE &hHandle);
    void       StopNeovimProcess();
    void       AppendBigEndianBytes(std::string &sData, uint64_t nValue, int nBytes);
    void       AppendMsgPackString(std::string &sData, const std::string &sValue);
    bool       ReadBytes(void *pData, size_t nSize);
    bool       ReadBigEndianInteger(int nBytes, uint64_t &nValue);
    bool       DecodeMsgPackValue(Value &sValue, int nDepth = 0);
    void       AppendMsgPackArrayHeader(std::string &sData, size_t nSize);
    void       AppendMsgPackInteger(std::string &sData, int64_t nValue);
    void       AppendMsgPackBoolean(std::string &sData, bool bValue);
    bool SendRpc(const std::string &sMethod, const std::string &sArguments, unsigned &nId, bool bNotification = false);
    bool CallRpcAndWait(const std::string &sMethod, const std::string &sArguments, Value &sResult);
    std::string BuildStateArguments();
    bool        RequestNeovimState();
    bool        SyncNeovim(const std::string &sText, LRESULT nLine, LRESULT nColumn);
    bool        RequestCaretSync();
    bool        SendKey(const std::string &sKey);
    std::string GetEditorText();
    bool        ContainsNoNulBytes(const std::string &sText);
    std::string NormalizeLineEndingsToLf(const std::string &sText);
    std::string RestoreDocumentLineEndings(const std::string &sText);
    bool        StartNeovimProcess();
    void        StopAndReportError(const wchar_t *pMessage);
    bool        SyncDocument(const Value &sResult);
    bool        PollResponses();
};
