#pragma once

#include "msgpack_limits.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MsgPack
{
struct Value
{
    int                nType    = 0; // 0 nil, 1 integer, 2 string, 3 array, 4 other, 5 map (key/value pairs)
    int64_t            nInteger = 0;
    std::string        sString;
    std::vector<Value> aItems;
};

inline bool AppendArrayHeader(std::string &sData, size_t nSize)
{
    if (nSize > UINT32_MAX)
        return false;
    const int nBytes = nSize <= UINT16_MAX ? 2 : 4;
    sData.push_back(static_cast<char>(nBytes == 2 ? 0xdc : 0xdd));
    for (int nShift = (nBytes - 1) * 8; nShift >= 0; nShift -= 8)
        sData.push_back(static_cast<char>(nSize >> nShift));
    return true;
}

// Decode only complete, bounded frames. Container storage and string payloads
// share one budget, including the actual sizeof(Value) on the target platform.
class Decoder
{
  public:
    explicit Decoder(std::string_view sBytes, size_t nBudget = MaxDecodedBytes)
        : m_sBytes(sBytes), m_nBytesRemaining(std::min(nBudget, MaxDecodedBytes))
    {
    }
    bool Decode(Value &sResult)
    {
        if (m_sBytes.size() > MaxFrameBytes)
            return false;
        try
        {
            Value sValue;
            if (!ReserveBytes(sizeof(Value)) || !DecodeValue(sValue, 0) || m_nOffset != m_sBytes.size())
                return false;
            sResult = std::move(sValue);
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        catch (const std::length_error &)
        {
            return false;
        }
    }

  private:
    std::string_view m_sBytes;
    size_t           m_nOffset = 0;
    size_t           m_nBytesRemaining;
    size_t           m_nValuesRemaining = MaxValues - 1;

    bool ReserveBytes(uint64_t nBytes)
    {
        if (nBytes > m_nBytesRemaining)
            return false;
        m_nBytesRemaining -= static_cast<size_t>(nBytes);
        return true;
    }
    bool ReserveValues(uint64_t nCount)
    {
        if (nCount > m_nValuesRemaining || nCount > m_sBytes.size() - m_nOffset ||
            !ReserveBytes(nCount * sizeof(Value)))
            return false;
        m_nValuesRemaining -= static_cast<size_t>(nCount);
        return true;
    }
    bool ReadBytes(void *pData, size_t nSize)
    {
        if (nSize > m_sBytes.size() - m_nOffset)
            return false;
        std::memcpy(pData, m_sBytes.data() + m_nOffset, nSize);
        m_nOffset += nSize;
        return true;
    }
    bool ReadBigEndianInteger(int nBytes, uint64_t &nValue)
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
    bool DecodeValue(Value &sValue, int nDepth)
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
            sValue.nType = 5;
            if ((nTag & 0xf0) == 0x80)
                nLength = nTag & 15;
            else if (!ReadBigEndianInteger(nTag == 0xde ? 2 : 4, nLength))
                return false;
            if (!ReserveValues(nLength * 2))
                return false;
            sValue.aItems.resize(static_cast<size_t>(nLength * 2));
            for (auto &sItem : sValue.aItems)
            {
                if (!DecodeValue(sItem, nDepth + 1))
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
            if (nLength > MaxPayloadBytes)
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
        if (nLength > MaxPayloadBytes)
            return false;
        if (sValue.nType == 2)
        {
            if (nLength > m_sBytes.size() - m_nOffset || !ReserveBytes(nLength + 1))
                return false;
            sValue.sString.resize(static_cast<size_t>(nLength));
            return !nLength || ReadBytes(sValue.sString.data(), static_cast<size_t>(nLength));
        }
        if (!ReserveValues(nLength))
            return false;
        sValue.aItems.resize(static_cast<size_t>(nLength));
        for (auto &sItem : sValue.aItems)
            if (!DecodeValue(sItem, nDepth + 1))
                return false;
        return true;
    }
};
} // namespace MsgPack
