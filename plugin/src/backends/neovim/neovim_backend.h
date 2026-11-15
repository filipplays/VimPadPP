#pragma once

#include "../common/backend_common.h"
#include "../common/editor_text.h"
#include "../common/host_key.h"
#include "line_update.h"
#include "msgpack_frame.h"

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class Backend_NVIM : public Backend
{
  public:
    explicit Backend_NVIM(HWND hWindow, void (*pSaveHandler)(HWND))
        : Backend(hWindow), m_sEditor(hWindow), m_pSaveHandler(pSaveHandler)
    {
    }
    BOOL Install() override;
    BOOL Uninstall() override;
    void OnCaretMoved(LRESULT nPosition) override;
    bool PassesKeyToHost(WPARAM nKey) const override;
    bool PrepareHostKey() override;

    BOOL bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) override;

  private:
    static constexpr UINT_PTR NeovimResponseTimerId  = 0x4E56494D;
    static constexpr UINT     CaretVisibilityMessage = WM_APP + 0x4E56;
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
        MsgPackFrame             sIncomingFrame;
        int64_t                  nSyncedBuffer      = -1;
        int64_t                  nSyncedChangedTick = -1;
        int64_t                  nSyncedEpoch       = -1;
        int64_t                  nSyncedSequence    = -1;
        size_t                   nReadOffset        = 0;
        bool                     bBufferedRead      = false;
        unsigned                 nStateId           = 0;
        unsigned                 nRevision          = 0;
        unsigned                 nStateRevision     = 0;
        unsigned                 nCaretId           = 0;
        UINT_PTR                 nTimer             = 0;
        LRESULT                  nKnownCaret        = 0;
        LRESULT                  nRequestedCaret    = 0;
        bool                     bApplyingState     = false;
        bool                     bSavePending       = false;
        Mode                     eMode              = Mode::Normal;
        std::vector<std::string> aPendingKeys;
    };
    enum class SyncResult
    {
        APPLIED,
        SKIPPED,
        RESYNC,
        FAILED
    };
    Session    m_sSession;
    EditorText m_sEditor;
    void (*m_pSaveHandler)(HWND)       = nullptr;
    HWND                 m_hCommandBar = nullptr;
    std::string          m_sCommandPrefix;
    std::string          m_sCommandText;
    std::string          m_sMessage;
    size_t               m_nCommandPosition   = 0;
    bool                 m_bCommandVisible    = false;
    bool                 m_bMouseSelecting    = false;
    bool                 m_bMouseFinishing    = false;
    unsigned             m_nMouseDepth        = 0;
    bool                 m_bSyncSelection     = false;
    unsigned             m_nSelectionRevision = 0;
    LRESULT              m_nSelectionAnchor   = 0;
    int                  m_nSuppressedChar    = -1;
    bool                 m_bPassThroughKey    = false;
    LPARAM               m_nPassThroughScan   = 0;
    std::string          m_sPluginConfigPath;
    std::vector<HostKey> m_aPassThroughKeys;
    LRESULT              m_nOriginalCaretStyle    = 0;
    LRESULT              m_nOriginalCaretPeriod   = 0;
    bool                 m_bScrollPastEnd         = false;
    bool                 m_bOriginalEndAtLastLine = true;
    void                 CloseHandleAndReset(HANDLE &hHandle);
    void                 StopNeovimProcess();
    void                 AppendBigEndianBytes(std::string &sData, uint64_t nValue, int nBytes);
    void                 AppendMsgPackString(std::string &sData, const std::string &sValue);
    bool                 ReadBytes(void *pData, size_t nSize);
    bool                 ReadBigEndianInteger(int nBytes, uint64_t &nValue);
    bool                 DecodeMsgPackValue(Value &sValue, int nDepth = 0);
    void                 AppendMsgPackArrayHeader(std::string &sData, size_t nSize);
    void                 AppendMsgPackInteger(std::string &sData, int64_t nValue);
    void                 AppendMsgPackBoolean(std::string &sData, bool bValue);
    bool SendRpc(const std::string &sMethod, const std::string &sArguments, unsigned &nId, bool bNotification = false);
    bool CallRpcAndWait(const std::string &sMethod, const std::string &sArguments, Value &sResult);
    bool AttachUi();
    bool HandleNotification(const Value &sNotification);
    void UpdateCommandBar();
    void PositionCommandBar();
    void EnsureCaretVisible();
    std::string BuildStateArguments(bool bAcceptCurrent = false);
    bool        RequestNeovimState();
    bool        SyncNeovim(const std::string &sText, LRESULT nLine, LRESULT nColumn);
    bool        RequestCaretSync();
    void        FinishMouseSelection();
    bool        SendKey(const std::string &sKey);
    std::string GetEditorText();
    bool        ContainsNoNulBytes(const std::string &sText);
    std::string NormalizeLineEndingsToLf(const std::string &sText);
    std::string RestoreDocumentLineEndings(const std::string &sText);
    std::string DocumentLineEnding();
    void        InvalidateSyncedRevision();
    bool        ApplyTextReplacement(size_t nStart, size_t nEnd, const std::string &sText);
    bool        StartNeovimProcess();
    bool        LoadPluginConfig();
    void        StopAndReportError(const wchar_t *pMessage);
    SyncResult  SyncDocument(const Value &sResult);
    bool        PollResponses();
};
