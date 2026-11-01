#include "json_test_cases.h"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
class JsonParser
{
  public:
    explicit JsonParser(std::string_view sInput) : m_sInput(sInput) {}

    std::vector<TestCase> ParseTestCases()
    {
        std::vector<TestCase> aTestCases;
        Expect('[');
        if (Consume(']'))
        {
            return aTestCases;
        }
        do
        {
            aTestCases.push_back(ParseTestCase());
        } while (Consume(','));
        Expect(']');
        SkipWhitespace();
        if (m_nPosition != m_sInput.size())
        {
            Fail("unexpected content after the test-case array");
        }
        return aTestCases;
    }

  private:
    [[noreturn]] void Fail(const std::string &sMessage) const
    {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(m_nPosition) + ": " + sMessage);
    }

    void SkipWhitespace()
    {
        while (m_nPosition < m_sInput.size() && std::isspace(static_cast<unsigned char>(m_sInput[m_nPosition])))
        {
            ++m_nPosition;
        }
    }

    bool Consume(char cExpected)
    {
        SkipWhitespace();
        if (m_nPosition < m_sInput.size() && m_sInput[m_nPosition] == cExpected)
        {
            ++m_nPosition;
            return true;
        }
        return false;
    }

    void Expect(char cExpected)
    {
        if (!Consume(cExpected))
        {
            Fail(std::string("expected '") + cExpected + "'");
        }
    }

    std::string ParseString()
    {
        Expect('"');
        std::string sResult;
        while (m_nPosition < m_sInput.size())
        {
            const char cCharacter = m_sInput[m_nPosition++];
            if (cCharacter == '"')
            {
                return sResult;
            }
            if (cCharacter != '\\')
            {
                sResult.push_back(cCharacter);
                continue;
            }
            if (m_nPosition == m_sInput.size())
            {
                Fail("unfinished escape sequence");
            }
            switch (m_sInput[m_nPosition++])
            {
            case '"':
                sResult.push_back('"');
                break;
            case '\\':
                sResult.push_back('\\');
                break;
            case '/':
                sResult.push_back('/');
                break;
            case 'b':
                sResult.push_back('\b');
                break;
            case 'f':
                sResult.push_back('\f');
                break;
            case 'n':
                sResult.push_back('\n');
                break;
            case 'r':
                sResult.push_back('\r');
                break;
            case 't':
                sResult.push_back('\t');
                break;
            default:
                Fail("unsupported string escape sequence");
            }
        }
        Fail("unterminated string");
    }

    LRESULT ParseCaret()
    {
        SkipWhitespace();
        if (m_nPosition == m_sInput.size() || !std::isdigit(static_cast<unsigned char>(m_sInput[m_nPosition])))
        {
            Fail("expected a non-negative integer");
        }
        unsigned long long nValue = 0;
        while (m_nPosition < m_sInput.size() && std::isdigit(static_cast<unsigned char>(m_sInput[m_nPosition])))
        {
            const unsigned int nDigit = static_cast<unsigned int>(m_sInput[m_nPosition++] - '0');
            if (nValue > (static_cast<unsigned long long>(std::numeric_limits<LRESULT>::max()) - nDigit) / 10)
            {
                Fail("integer is out of range");
            }
            nValue = nValue * 10 + nDigit;
        }
        return static_cast<LRESULT>(nValue);
    }

    std::vector<std::string> ParseKeyArray()
    {
        std::vector<std::string> aKeys;
        Expect('[');
        if (Consume(']'))
        {
            return aKeys;
        }
        do
        {
            aKeys.push_back(ParseString());
        } while (Consume(','));
        Expect(']');
        return aKeys;
    }

    void SkipValue()
    {
        SkipWhitespace();
        if (m_nPosition == m_sInput.size())
        {
            Fail("expected a value");
        }
        const char cCharacter = m_sInput[m_nPosition];
        if (cCharacter == '"')
        {
            ParseString();
            return;
        }
        if (cCharacter == '{')
        {
            Expect('{');
            if (!Consume('}'))
            {
                do
                {
                    ParseString();
                    Expect(':');
                    SkipValue();
                } while (Consume(','));
                Expect('}');
            }
            return;
        }
        if (cCharacter == '[')
        {
            Expect('[');
            if (!Consume(']'))
            {
                do
                {
                    SkipValue();
                } while (Consume(','));
                Expect(']');
            }
            return;
        }
        while (m_nPosition < m_sInput.size() && !std::isspace(static_cast<unsigned char>(m_sInput[m_nPosition])) &&
               m_sInput[m_nPosition] != ',' && m_sInput[m_nPosition] != ']' && m_sInput[m_nPosition] != '}')
        {
            ++m_nPosition;
        }
    }

    TestCase ParseTestCase()
    {
        TestCase sTestCase;
        bool     bIdSet            = false;
        bool     bInitialTextSet   = false;
        bool     bInitialCaretSet  = false;
        bool     bKeysSet          = false;
        bool     bExpectedTextSet  = false;
        bool     bExpectedCaretSet = false;

        Expect('{');
        if (!Consume('}'))
        {
            do
            {
                const std::string sName = ParseString();
                Expect(':');
                if (sName == "id")
                {
                    sTestCase.sId = ParseString();
                    bIdSet        = true;
                }
                else if (sName == "initial_text")
                {
                    sTestCase.sInitialText = ParseString();
                    bInitialTextSet        = true;
                }
                else if (sName == "initial_caret")
                {
                    sTestCase.nInitialCaret = ParseCaret();
                    bInitialCaretSet        = true;
                }
                else if (sName == "keys")
                {
                    sTestCase.aKeys = ParseKeyArray();
                    bKeysSet        = true;
                }
                else if (sName == "expected_text")
                {
                    sTestCase.sExpectedText = ParseString();
                    bExpectedTextSet        = true;
                }
                else if (sName == "expected_caret")
                {
                    sTestCase.nExpectedCaret = ParseCaret();
                    bExpectedCaretSet        = true;
                }
                else
                {
                    SkipValue();
                }
            } while (Consume(','));
            Expect('}');
        }

        if (!bIdSet || !bInitialTextSet || !bInitialCaretSet || !bKeysSet || !bExpectedTextSet || !bExpectedCaretSet ||
            sTestCase.sId.empty())
        {
            Fail(
                "each test case requires id, initial state, keys, and expected "
                "state");
        }
        return sTestCase;
    }

    std::string_view m_sInput;
    std::size_t      m_nPosition = 0;
};
} // namespace

std::vector<TestCase> ParseTestCasesJson(std::string_view sInput) { return JsonParser(sInput).ParseTestCases(); }
