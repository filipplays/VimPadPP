#include "test_runner.h"

#include "ScintillaMessages.h"
#include "VimMotions.h"

#include <windows.h>

#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr UINT SCI_SETTEXT = 2181;
constexpr UINT SCI_GETTEXT = 2182;

std::string EscapeForDisplay(const std::string &sText)
{
    std::ostringstream sStream;
    for (const unsigned char cCharacter : sText)
    {
        switch (cCharacter)
        {
        case '\\':
            sStream << "\\\\";
            break;
        case '\n':
            sStream << "\\n";
            break;
        case '\r':
            sStream << "\\r";
            break;
        case '\t':
            sStream << "\\t";
            break;
        default:
            if (std::isprint(cCharacter))
            {
                sStream << static_cast<char>(cCharacter);
            }
            else
            {
                sStream << "\\x" << std::hex << std::uppercase << static_cast<int>(cCharacter) << std::dec;
            }
            break;
        }
    }
    return sStream.str();
}

std::string EscapeForJson(const std::string &sText)
{
    std::ostringstream sStream;
    for (const unsigned char cCharacter : sText)
    {
        switch (cCharacter)
        {
        case '"':
            sStream << "\\\"";
            break;
        case '\\':
            sStream << "\\\\";
            break;
        case '\b':
            sStream << "\\b";
            break;
        case '\f':
            sStream << "\\f";
            break;
        case '\n':
            sStream << "\\n";
            break;
        case '\r':
            sStream << "\\r";
            break;
        case '\t':
            sStream << "\\t";
            break;
        default:
            if (cCharacter < 0x20)
            {
                sStream << "\\u00" << std::hex << std::uppercase << static_cast<int>(cCharacter) << std::dec;
            }
            else
            {
                sStream << static_cast<char>(cCharacter);
            }
            break;
        }
    }
    return sStream.str();
}

class EditorHarness
{
  public:
    ~EditorHarness() { Shutdown(); }

    bool Initialize(std::string &sError, bool bVisible)
    {
        m_hScintillaModule = LoadLibraryW(L"Scintilla.dll");
        if (m_hScintillaModule == nullptr)
        {
            sError = "could not load Scintilla.dll";
            return false;
        }

        constexpr wchar_t kClassName[] = L"VimMotionsFunctionalTestHost";
        WNDCLASSEXW       sWindowClass{};
        sWindowClass.cbSize        = sizeof(sWindowClass);
        sWindowClass.hInstance     = GetModuleHandleW(nullptr);
        sWindowClass.lpszClassName = kClassName;
        sWindowClass.lpfnWndProc   = DefWindowProcW;
        if (RegisterClassExW(&sWindowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            sError = "could not register the test host window class";
            return false;
        }

        m_hParent = CreateWindowExW(
            0, kClassName, L"VimMotions Functional Tests", WS_OVERLAPPED, 0, 0, bVisible ? 800 : 100,
            bVisible ? 600 : 100, nullptr, nullptr, sWindowClass.hInstance, nullptr);
        if (m_hParent == nullptr)
        {
            sError = "could not create the test host window";
            return false;
        }
        m_hEditor = CreateWindowExW(
            0, L"Scintilla", L"", WS_CHILD, 0, 0, bVisible ? 800 : 100, bVisible ? 600 : 100, m_hParent, nullptr,
            sWindowClass.hInstance, nullptr);
        if (m_hEditor == nullptr)
        {
            sError = "could not create the Scintilla test control";
            return false;
        }

        m_hVimMotionsModule = LoadLibraryW(L"VimMotions.dll");
        if (m_hVimMotionsModule == nullptr)
        {
            sError = "could not load VimMotions.dll";
            return false;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        m_pInstallVim   = reinterpret_cast<InstallVimFn>(GetProcAddress(m_hVimMotionsModule, "InstallVim"));
        m_pUninstallVim = reinterpret_cast<UninstallVimFn>(GetProcAddress(m_hVimMotionsModule, "UninstallVim"));
#pragma GCC diagnostic pop
        if (m_pInstallVim == nullptr || m_pUninstallVim == nullptr || !m_pInstallVim(m_hEditor))
        {
            sError = "could not install VimMotions.dll into the Scintilla control";
            return false;
        }
        m_bInstalled = true;
        m_bVisible   = bVisible;
        if (bVisible)
        {
            ShowWindow(m_hEditor, SW_SHOW);
            ShowWindow(m_hParent, SW_SHOW);
            UpdateWindow(m_hParent);
        }
        return true;
    }

    bool SetFixture(const TestCase &sTestCase, std::string &sError) const
    {
        // Each fixture needs fresh parser, register, and preferred-column
        // state.
        if (!m_pUninstallVim(m_hEditor) || !m_pInstallVim(m_hEditor))
        {
            sError = "could not reset VimMotions.dll for the test case";
            return false;
        }
        SendMessageW(m_hEditor, WM_KEYDOWN, VK_ESCAPE, 0);
        SendMessageW(m_hEditor, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(sTestCase.sInitialText.c_str()));
        SendMessageW(m_hEditor, Scintilla::GOTOPOS, sTestCase.nInitialCaret, 0);
        if (m_bVisible)
        {
            UpdateWindow(m_hEditor);
        }
        if (sTestCase.nInitialCaret < 0 ||
            sTestCase.nInitialCaret > SendMessageW(m_hEditor, Scintilla::GETLENGTH, 0, 0))
        {
            sError = "initial_caret is outside the fixture text";
            return false;
        }
        return true;
    }

    bool SendKeys(const std::vector<std::string> &aKeys, std::string &sError) const
    {
        for (const std::string &sKey : aKeys)
        {
            if (sKey == "Esc")
            {
                SendMessageW(m_hEditor, WM_KEYDOWN, VK_ESCAPE, 0);
            }
            else if (sKey.size() == 1)
            {
                // The Vim subclass consumes WM_CHAR in normal mode; in insert
                // mode it calls DefSubclassProc, exactly as it does for input
                // produced by TranslateMessage in the production message loop.
                SendMessageW(m_hEditor, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(sKey.front())), 0);
            }
            else
            {
                sError = "unsupported key token: " + sKey;
                return false;
            }
            if (m_bVisible)
            {
                UpdateWindow(m_hEditor);
            }
        }
        return true;
    }

    [[nodiscard]] std::string ReadText() const
    {
        const LRESULT     nLength = SendMessageW(m_hEditor, Scintilla::GETLENGTH, 0, 0);
        std::vector<char> aText(static_cast<std::size_t>(nLength) + 1, '\0');
        SendMessageW(m_hEditor, SCI_GETTEXT, static_cast<WPARAM>(aText.size()), reinterpret_cast<LPARAM>(aText.data()));
        return std::string(aText.data(), static_cast<std::size_t>(nLength));
    }

    [[nodiscard]] LRESULT ReadCaret() const { return SendMessageW(m_hEditor, Scintilla::GETCURRENTPOS, 0, 0); }

  private:
    void Shutdown()
    {
        if (m_bInstalled && m_pUninstallVim != nullptr && m_hEditor != nullptr)
        {
            m_pUninstallVim(m_hEditor);
        }
        m_bInstalled = false;
        if (m_hParent != nullptr)
        {
            DestroyWindow(m_hParent);
        }
        m_hEditor = nullptr;
        m_hParent = nullptr;
        if (m_hVimMotionsModule != nullptr)
        {
            FreeLibrary(m_hVimMotionsModule);
        }
        m_hVimMotionsModule = nullptr;
        if (m_hScintillaModule != nullptr)
        {
            FreeLibrary(m_hScintillaModule);
        }
        m_hScintillaModule = nullptr;
    }

    HMODULE        m_hScintillaModule  = nullptr;
    HMODULE        m_hVimMotionsModule = nullptr;
    HWND           m_hParent           = nullptr;
    HWND           m_hEditor           = nullptr;
    InstallVimFn   m_pInstallVim       = nullptr;
    UninstallVimFn m_pUninstallVim     = nullptr;
    bool           m_bInstalled        = false;
    bool           m_bVisible          = false;
};

void WaitForMessages(std::uint32_t nWaitMilliseconds)
{
    const auto sDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(nWaitMilliseconds);
    while (std::chrono::steady_clock::now() < sDeadline)
    {
        MSG sMessage{};
        while (std::chrono::steady_clock::now() < sDeadline && PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&sMessage);
            DispatchMessageW(&sMessage);
        }
        const auto nRemaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(sDeadline - std::chrono::steady_clock::now()).count();
        if (nRemaining > 0)
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(nRemaining), QS_ALLINPUT);
    }
}

TestResult RunTest(const TestCase &sTestCase, const EditorHarness &sHarness, std::uint32_t nWaitMilliseconds)
{
    TestResult sResult;
    sResult.pTestCase = &sTestCase;
    const auto nStart = std::chrono::steady_clock::now();
    if (!sHarness.SetFixture(sTestCase, sResult.sError) || !sHarness.SendKeys(sTestCase.aKeys, sResult.sError))
    {
        sResult.nElapsedMicroseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - nStart).count();
        return sResult;
    }
    WaitForMessages(nWaitMilliseconds);
    sResult.sActualText  = sHarness.ReadText();
    sResult.nActualCaret = sHarness.ReadCaret();
    sResult.bPassed =
        sResult.sActualText == sTestCase.sExpectedText && sResult.nActualCaret == sTestCase.nExpectedCaret;
    sResult.nElapsedMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - nStart).count();
    return sResult;
}
} // namespace

bool RunFunctionalTests(
    const std::vector<TestCase> &aTestCases, std::vector<TestResult> &aResults, std::string &sError, bool bVisible,
    std::uint32_t nWaitMilliseconds)
{
    EditorHarness sHarness;
    if (!sHarness.Initialize(sError, bVisible))
    {
        return false;
    }

    aResults.clear();
    aResults.reserve(aTestCases.size());
    for (const TestCase &sTestCase : aTestCases)
    {
        aResults.push_back(RunTest(sTestCase, sHarness, nWaitMilliseconds));
    }
    return true;
}

std::string BuildTextReport(const std::vector<TestResult> &aResults)
{
    std::size_t nPassed = 0;
    for (const TestResult &sResult : aResults)
    {
        nPassed += sResult.bPassed ? 1 : 0;
    }

    std::ostringstream sReport;
    sReport << "VimMotions functional test report\n";
    for (const TestResult &sResult : aResults)
    {
        sReport << (sResult.bPassed ? "[PASS] " : "[FAIL] ") << sResult.pTestCase->sId << " ("
                << sResult.nElapsedMicroseconds << " us)\n";
        if (!sResult.bPassed)
        {
            if (!sResult.sError.empty())
            {
                sReport << "  Error: " << sResult.sError << "\n";
            }
            else
            {
                sReport << "  Expected text: \"" << EscapeForDisplay(sResult.pTestCase->sExpectedText)
                        << "\", caret: " << sResult.pTestCase->nExpectedCaret << "\n  Actual text:   \""
                        << EscapeForDisplay(sResult.sActualText) << "\", caret: " << sResult.nActualCaret << "\n";
            }
        }
    }
    sReport << "Total: " << aResults.size() << ", passed: " << nPassed << ", failed: " << aResults.size() - nPassed
            << "\n\n";
    return sReport.str();
}

std::string BuildJsonReport(const std::vector<TestResult> &aResults)
{
    std::size_t nPassed = 0;
    for (const TestResult &sResult : aResults)
    {
        nPassed += sResult.bPassed ? 1 : 0;
    }

    std::ostringstream sReport;
    sReport << "{\n  \"summary\": {\"total\": " << aResults.size() << ", \"passed\": " << nPassed
            << ", \"failed\": " << aResults.size() - nPassed << "},\n  \"results\": [\n";
    for (std::size_t nIndex = 0; nIndex < aResults.size(); ++nIndex)
    {
        const TestResult &sResult = aResults[nIndex];
        sReport << "    {\"id\": \"" << EscapeForJson(sResult.pTestCase->sId) << "\", \"status\": \""
                << (sResult.bPassed ? "passed" : "failed") << "\", \"duration_us\": " << sResult.nElapsedMicroseconds
                << ", \"expected\": {\"text\": \"" << EscapeForJson(sResult.pTestCase->sExpectedText)
                << "\", \"caret\": " << sResult.pTestCase->nExpectedCaret << "}, \"actual\": {\"text\": \""
                << EscapeForJson(sResult.sActualText) << "\", \"caret\": " << sResult.nActualCaret << "}, \"error\": \""
                << EscapeForJson(sResult.sError) << "\"}";
        if (nIndex + 1 < aResults.size())
        {
            sReport << ',';
        }
        sReport << '\n';
    }
    sReport << "  ]\n}\n";
    return sReport.str();
}
