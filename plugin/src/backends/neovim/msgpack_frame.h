#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

// Finds one MessagePack value without allocating its decoded contents. The caller
// keeps the same buffer prefix until COMPLETE, then consumes Size() bytes and resets.
class MsgPackFrame
{
  public:
    enum class Result
    {
        NEED_MORE,
        COMPLETE,
        INVALID
    };

    Result Scan(std::string_view sBytes)
    {
        if (m_bInvalid || m_nOffset > sBytes.size())
            return Result::INVALID;
        while (!m_aRemaining.empty())
        {
            if (m_ePhase == Phase::TAG)
            {
                if (m_nOffset == sBytes.size())
                    return Result::NEED_MORE;
                if (m_aRemaining.size() > 33)
                    return Fail();
                const auto nTag = static_cast<unsigned char>(sBytes[m_nOffset++]);
                m_eKind         = Kind::PAYLOAD;
                m_nLength       = 0;
                m_nLengthBytes  = 0;
                if (nTag <= 0x7f || nTag >= 0xe0 || nTag == 0xc0 || nTag == 0xc2 || nTag == 0xc3)
                    CompleteValue();
                else
                {
                    if ((nTag & 0xe0) == 0xa0)
                        m_nLength = nTag & 31;
                    else if ((nTag & 0xf0) == 0x90 || (nTag & 0xf0) == 0x80)
                    {
                        m_eKind   = nTag < 0x90 ? Kind::MAP : Kind::ARRAY;
                        m_nLength = nTag & 15;
                    }
                    else if (nTag >= 0xc4 && nTag <= 0xc9)
                    {
                        m_eKind        = nTag >= 0xc7 ? Kind::EXT : Kind::PAYLOAD;
                        m_nLengthBytes = 1 << ((nTag - 0xc4) % 3);
                    }
                    else if (nTag == 0xca || nTag == 0xcb)
                        m_nLength = nTag == 0xca ? 4 : 8;
                    else if (nTag >= 0xcc && nTag <= 0xd3)
                        m_nLength = uint64_t{1} << ((nTag - 0xcc) % 4);
                    else if (nTag >= 0xd4 && nTag <= 0xd8)
                    {
                        m_eKind   = Kind::EXT;
                        m_nLength = uint64_t{1} << (nTag - 0xd4);
                    }
                    else if (nTag >= 0xd9 && nTag <= 0xdf)
                    {
                        if (nTag >= 0xde)
                            m_eKind = Kind::MAP;
                        else if (nTag >= 0xdc)
                            m_eKind = Kind::ARRAY;
                        m_nLengthBytes = nTag <= 0xdb ? 1 << (nTag - 0xd9) : (nTag % 2 == 0 ? 2 : 4);
                    }
                    else
                        return Fail(); // Reserved tag 0xc1.
                    m_ePhase = Phase::LENGTH;
                }
            }
            if (m_ePhase == Phase::LENGTH)
            {
                while (m_nLengthBytes)
                {
                    if (m_nOffset == sBytes.size())
                        return Result::NEED_MORE;
                    m_nLength = (m_nLength << 8) | static_cast<unsigned char>(sBytes[m_nOffset++]);
                    --m_nLengthBytes;
                }
                // Match the decoder's element, payload and map limits.
                if (m_nLength > (m_eKind == Kind::MAP ? 1024 * 1024 : 16 * 1024 * 1024))
                    return Fail();
                if (m_eKind == Kind::ARRAY || m_eKind == Kind::MAP)
                {
                    const uint64_t nChildren = m_nLength * (m_eKind == Kind::MAP ? 2 : 1);
                    m_ePhase                 = Phase::TAG;
                    if (nChildren)
                        m_aRemaining.push_back(nChildren);
                    else
                        CompleteValue();
                }
                else
                {
                    m_nPayloadRemaining = m_nLength + (m_eKind == Kind::EXT ? 1 : 0);
                    if (m_nPayloadRemaining > 16 * 1024 * 1024)
                        return Fail();
                    m_ePhase = Phase::PAYLOAD;
                }
            }
            if (m_ePhase == Phase::PAYLOAD)
            {
                const auto nChunk = std::min<uint64_t>(m_nPayloadRemaining, sBytes.size() - m_nOffset);
                m_nOffset += static_cast<size_t>(nChunk);
                m_nPayloadRemaining -= nChunk;
                if (m_nPayloadRemaining)
                    return Result::NEED_MORE;
                m_ePhase = Phase::TAG;
                CompleteValue();
            }
        }
        return Result::COMPLETE;
    }

    size_t Size() const { return m_nOffset; }
    void   Reset()
    {
        m_aRemaining.assign(1, 1);
        m_nOffset           = 0;
        m_nLength           = 0;
        m_nLengthBytes      = 0;
        m_nPayloadRemaining = 0;
        m_ePhase            = Phase::TAG;
        m_bInvalid          = false;
    }

  private:
    enum class Phase
    {
        TAG,
        LENGTH,
        PAYLOAD
    };
    enum class Kind
    {
        PAYLOAD,
        ARRAY,
        MAP,
        EXT
    };
    std::vector<uint64_t> m_aRemaining{1};
    size_t                m_nOffset           = 0;
    uint64_t              m_nLength           = 0;
    unsigned              m_nLengthBytes      = 0;
    uint64_t              m_nPayloadRemaining = 0;
    Phase                 m_ePhase            = Phase::TAG;
    Kind                  m_eKind             = Kind::PAYLOAD;
    bool                  m_bInvalid          = false;

    Result Fail()
    {
        m_bInvalid = true;
        return Result::INVALID;
    }
    void CompleteValue()
    {
        while (!m_aRemaining.empty() && --m_aRemaining.back() == 0)
            m_aRemaining.pop_back();
    }
};
