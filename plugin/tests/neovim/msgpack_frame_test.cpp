#include "msgpack_frame.h"
#include "msgpack_value.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::string Bytes(std::initializer_list<unsigned char> aBytes) { return std::string(aBytes.begin(), aBytes.end()); }
void        CheckFragmentation(const std::string &sMessage)
{
    using Result = MsgPackFrame::Result;
    MsgPack::Value sDecoded;
    assert(MsgPack::Decoder(sMessage).Decode(sDecoded));
    for (size_t nSplit = 0; nSplit < sMessage.size(); ++nSplit)
    {
        MsgPackFrame sFrame;
        assert(sFrame.Scan(std::string_view(sMessage).substr(0, nSplit)) == Result::NEED_MORE);
        const size_t nScanned = sFrame.Size();
        assert(sFrame.Scan(std::string_view(sMessage).substr(0, nSplit)) == Result::NEED_MORE);
        assert(sFrame.Size() == nScanned);
        assert(sFrame.Scan(sMessage) == Result::COMPLETE);
        assert(sFrame.Size() == sMessage.size());
    }
    MsgPackFrame sFrame;
    for (size_t nSize = 0; nSize <= sMessage.size(); ++nSize)
        assert(
            sFrame.Scan(std::string_view(sMessage).substr(0, nSize)) ==
            (nSize == sMessage.size() ? Result::COMPLETE : Result::NEED_MORE));
    // A following frame must not become part of this one.
    assert(sFrame.Scan(sMessage + Bytes({0xc0})) == Result::COMPLETE);
    assert(sFrame.Size() == sMessage.size());
    sFrame.Reset();
    assert(sFrame.Scan(Bytes({0xc0})) == Result::COMPLETE);
    assert(sFrame.Size() == 1);
}
} // namespace

int main()
{
    // Cover every tag, including binary/extension payloads containing reserved tags.
    for (unsigned nTag = 0; nTag <= 255; ++nTag)
    {
        if (nTag == 0xc1)
            continue;
        std::string sMessage(1, static_cast<char>(nTag));
        if (nTag >= 0x80 && nTag <= 0x8f)
            sMessage.append(2 * (nTag & 15), '\0');
        else if (nTag >= 0x90 && nTag <= 0x9f)
            sMessage.append(nTag & 15, '\0');
        else if (nTag >= 0xa0 && nTag <= 0xbf)
            sMessage.append(nTag & 31, static_cast<char>(0xc1));
        else if (nTag >= 0xc4 && nTag <= 0xc9)
        {
            sMessage.append(1u << ((nTag - 0xc4) % 3), '\0'); // Empty bin/ext payload.
            if (nTag >= 0xc7)
                sMessage.push_back(0); // Extension type.
        }
        else if (nTag == 0xca || nTag == 0xcb)
            sMessage.append(nTag == 0xca ? 4 : 8, '\0');
        else if (nTag >= 0xcc && nTag <= 0xd3)
            sMessage.append(1u << ((nTag - 0xcc) % 4), '\0');
        else if (nTag >= 0xd4 && nTag <= 0xd8)
            sMessage.append((1u << (nTag - 0xd4)) + 1, '\0');
        else if (nTag >= 0xd9 && nTag <= 0xdb)
            sMessage.append(1u << (nTag - 0xd9), '\0');
        else if (nTag >= 0xdc && nTag <= 0xdf)
            sMessage.append(nTag == 0xdc || nTag == 0xde ? 2 : 4, '\0');
        CheckFragmentation(sMessage);
    }
    CheckFragmentation(Bytes({0x94, 1, 42, 0xc0, 0x95, 1, 2, 0xc0, 0x92, 3, 4, 0x92, 0xa1, 'n', 0x94, 0, 1, 1, 0}));
    CheckFragmentation(Bytes({0x81, 0xa1, 'x', 0x92, 0xc7, 2, 0, 0xc1, 0x91, 0xc4, 1, 0xc1}));
    CheckFragmentation(Bytes({0xda, 1, 0}) + std::string(256, 'x'));
    CheckFragmentation(Bytes({0xdd, 0, 0, 0, 2, 0xc0, 0xc0}));
    CheckFragmentation(Bytes({0xdf, 0, 0, 0, 1, 0, 1}));

    // A large payload is skipped once as bytes arrive, with no decoded string allocation.
    const std::string sLarge = Bytes({0xdb, 0, 0x10, 0, 0}) + std::string(1024 * 1024, 'x');
    MsgPackFrame      sLargeFrame;
    for (size_t nSize = 0; nSize < sLarge.size(); nSize += 4096)
    {
        assert(sLargeFrame.Scan(std::string_view(sLarge).substr(0, nSize)) == MsgPackFrame::Result::NEED_MORE);
        assert(sLargeFrame.Size() == nSize);
    }
    assert(sLargeFrame.Scan(sLarge) == MsgPackFrame::Result::COMPLETE);

    for (const auto &sInvalid : std::vector<std::string>{
             Bytes({0xc1}), Bytes({0xdb, 1, 0, 0, 1}), Bytes({0xdf, 0, 0x10, 0, 1}),
             std::string(33, static_cast<char>(0x91)) + Bytes({0xc0})})
    {
        MsgPackFrame sFrame;
        assert(sFrame.Scan(sInvalid) == MsgPackFrame::Result::INVALID);
        assert(sFrame.Scan(sInvalid) == MsgPackFrame::Result::INVALID);
        sFrame.Reset();
        assert(sFrame.Scan(Bytes({0xc0})) == MsgPackFrame::Result::COMPLETE);
    }
    CheckFragmentation(std::string(32, static_cast<char>(0x91)) + Bytes({0xc0}));
    // Decode the outgoing array length independently of the production decoder.
    for (const size_t nCount : {65535u, 65536u, 65537u})
    {
        std::string sEncoded;
        assert(MsgPack::AppendArrayHeader(sEncoded, nCount));
        const size_t nBytes = nCount <= 65535 ? 2 : 4;
        assert(static_cast<unsigned char>(sEncoded[0]) == (nBytes == 2 ? 0xdc : 0xdd));
        size_t nDecodedCount = 0;
        for (size_t nIndex = 1; nIndex <= nBytes; ++nIndex)
            nDecodedCount = (nDecodedCount << 8) | static_cast<unsigned char>(sEncoded[nIndex]);
        assert(nDecodedCount == nCount);
        sEncoded.append(nCount, static_cast<char>(0xa0)); // Empty line strings.
        MsgPackFrame sFrame;
        assert(sFrame.Scan(sEncoded) == MsgPackFrame::Result::COMPLETE);
        MsgPack::Value sValue;
        assert(MsgPack::Decoder(sEncoded).Decode(sValue));
        assert(sValue.nType == 3 && sValue.aItems.size() == nCount);
        assert(sValue.aItems.front().nType == 2 && sValue.aItems.back().nType == 2);
    }
    std::string sUnchanged = "prefix";
    assert(!MsgPack::AppendArrayHeader(sUnchanged, uint64_t{UINT32_MAX} + 1));
    assert(sUnchanged == "prefix");
    for (const auto &sHeader : {Bytes({0xdd, 1, 0, 0, 0}), Bytes({0xdf, 0, 0x10, 0, 0})})
    {
        MsgPackFrame sFrame;
        assert(sFrame.Scan(sHeader) == MsgPackFrame::Result::INVALID);
        MsgPack::Value sValue;
        assert(!MsgPack::Decoder(sHeader).Decode(sValue));
    }
    // Individually acceptable nested arrays must share the frame's value limit.
    std::string sNested = Bytes({0x92});
    assert(MsgPack::AppendArrayHeader(sNested, MsgPack::MaxValues / 2));
    sNested.append(MsgPack::MaxValues / 2, static_cast<char>(0xc0));
    assert(MsgPack::AppendArrayHeader(sNested, MsgPack::MaxValues / 2));
    MsgPackFrame sNestedFrame;
    assert(sNestedFrame.Scan(sNested) == MsgPackFrame::Result::INVALID);
    // Charge nested containers and string storage against the same budget.
    const std::string sBudget = Bytes({0x92, 0x91, 0xa1, 'x', 0x91, 0xa1, 'y'});
    MsgPack::Value    sBudgetValue;
    assert(!MsgPack::Decoder(sBudget, 5 * sizeof(MsgPack::Value)).Decode(sBudgetValue));
    assert(MsgPack::Decoder(sBudget, 5 * sizeof(MsgPack::Value) + 4).Decode(sBudgetValue));
    // Decoding never consumes a following frame or accepts trailing garbage.
    assert(!MsgPack::Decoder(Bytes({0xc0, 0xc0})).Decode(sBudgetValue));
    assert(!MsgPack::Decoder(Bytes({0x91})).Decode(sBudgetValue));
    std::cout << "MessagePack framing checks passed\n";
}
