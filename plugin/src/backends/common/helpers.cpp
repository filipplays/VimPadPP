#include "helpers.h"

#include <atomic>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

ScopedTimer::ScopedTimer(const char *pName) : m_pName(pName), m_tStart(std::chrono::steady_clock::now()) {}
ScopedTimer::~ScopedTimer()
{
    const auto nUs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_tStart).count();
    std::clog << m_pName << ": " << nUs << " us\n";
}

InputTraceRecorder::TraceId InputTraceRecorder::Begin(const std::string &sKey, Clock::time_point tReceived)
{
    static std::atomic<TraceId> nNextId{0};
    const TraceId               nId = ++nNextId;
    Trace                       sTrace;
    sTrace.sKey      = sKey;
    sTrace.tReceived = tReceived;
    sTrace.aEvents.push_back({"Input received", 0, -1});
    m_aTraces.emplace(nId, std::move(sTrace));
    return nId;
}
void InputTraceRecorder::SetRevision(TraceId nId, unsigned nRevision) { m_aTraces.at(nId).nRevision = nRevision; }
void InputTraceRecorder::MarkSent(TraceId nId) { m_aTraces.at(nId).bSent = true; }
void InputTraceRecorder::Event(TraceId nId, const std::string &sName, Clock::time_point tStart)
{
    const auto tNow    = Clock::now();
    auto       itTrace = m_aTraces.find(nId);
    if (itTrace == m_aTraces.end())
        return;
    auto      &sTrace    = itTrace->second;
    const auto nOffset   = std::chrono::duration_cast<std::chrono::microseconds>(tNow - sTrace.tReceived).count();
    const auto nDuration = tStart == Clock::time_point{}
                               ? -1
                               : std::chrono::duration_cast<std::chrono::microseconds>(tNow - tStart).count();
    sTrace.aEvents.push_back({sName, nOffset, nDuration});
}
std::vector<InputTraceRecorder::TraceId> InputTraceRecorder::Targets(unsigned nRevision) const
{
    std::vector<TraceId> aTargets;
    for (const auto &[nId, sTrace] : m_aTraces)
        if (sTrace.sOutcome.empty() && sTrace.bSent && sTrace.nRevision <= nRevision)
            aTargets.push_back(nId);
    return aTargets;
}
void InputTraceRecorder::Finish(const std::vector<TraceId> &aTargets, const std::string &sOutcome)
{
    const auto tNow = Clock::now();
    for (const auto nId : aTargets)
    {
        auto itTrace = m_aTraces.find(nId);
        if (itTrace == m_aTraces.end() || !itTrace->second.sOutcome.empty())
            continue;
        auto &sTrace     = m_aTraces.at(nId);
        sTrace.sOutcome  = sOutcome;
        sTrace.tFinished = tNow;
        sTrace.nTotalUs  = std::chrono::duration_cast<std::chrono::microseconds>(tNow - sTrace.tReceived).count();
    }
}
void InputTraceRecorder::FinishAll(const std::string &sOutcome)
{
    const auto tNow = Clock::now();
    for (auto &[nId, sTrace] : m_aTraces)
        if (sTrace.sOutcome.empty())
        {
            sTrace.sOutcome  = sOutcome;
            sTrace.tFinished = tNow;
            sTrace.nTotalUs  = std::chrono::duration_cast<std::chrono::microseconds>(tNow - sTrace.tReceived).count();
        }
}
void InputTraceRecorder::Poll(Clock::time_point tStart, bool bHadData)
{
    const auto tNow = Clock::now();
    for (auto &[nId, sTrace] : m_aTraces)
    {
        // Include the poll that completed this trace, but not earlier/later unrelated polls.
        if (sTrace.tReceived > tStart || (!sTrace.sOutcome.empty() && sTrace.tFinished < tStart))
            continue;
        if (bHadData)
            ++sTrace.nDataPolls;
        else
            ++sTrace.nEmptyPolls;
        sTrace.nPollUs += std::chrono::duration_cast<std::chrono::microseconds>(tNow - tStart).count();
    }
}
void InputTraceRecorder::PrintCompleted()
{
    std::ostringstream sOutput;
    for (auto itTrace = m_aTraces.begin(); itTrace != m_aTraces.end();)
    {
        const auto &[nId, sTrace] = *itTrace;
        if (sTrace.sOutcome.empty())
        {
            ++itTrace;
            continue;
        }
        sOutput << "Input #" << nId << ": " << std::quoted(sTrace.sKey) << " (revision " << sTrace.nRevision << ")\n";
        for (const auto &sEvent : sTrace.aEvents)
        {
            sOutput << "  +" << sEvent.nOffsetUs << " us  " << sEvent.sName;
            if (sEvent.nDurationUs >= 0)
                sOutput << "  duration: " << sEvent.nDurationUs << " us";
            sOutput << '\n';
        }
        sOutput << "  Outcome: " << sTrace.sOutcome << "\n  Input-to-completion: " << sTrace.nTotalUs
                << " us\n  Polls: " << sTrace.nEmptyPolls << " empty, " << sTrace.nDataPolls
                << " with data; total poll work: " << sTrace.nPollUs << " us (inclusive)\n-----------------------\n";
        itTrace = m_aTraces.erase(itTrace);
    }
    if (!sOutput.str().empty())
        std::cout << sOutput.str();
}
InputTraceScope::InputTraceScope(
    InputTraceRecorder &sRecorder, std::vector<InputTraceRecorder::TraceId> aTargets, const char *pName)
    : m_sRecorder(sRecorder), m_aTargets(std::move(aTargets)), m_sName(pName),
      m_tStart(InputTraceRecorder::Clock::now())
{
}
InputTraceScope::~InputTraceScope()
{
    for (const auto nId : m_aTargets)
        m_sRecorder.Event(nId, m_sName, m_tStart);
}
