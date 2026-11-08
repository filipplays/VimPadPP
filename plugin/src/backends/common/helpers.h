#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Uncomment a ScopedTimer at a call site to print the time spent in that scope.
class ScopedTimer
{
  public:
    explicit ScopedTimer(const char *pName);
    ~ScopedTimer();
    ScopedTimer(const ScopedTimer &)            = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

  private:
    const char                           *m_pName;
    std::chrono::steady_clock::time_point m_tStart;
};

// Host-side input lifetime. Durations are inclusive; timestamps share a steady clock.
class InputTraceRecorder
{
  public:
    using Clock   = std::chrono::steady_clock;
    using TraceId = uint64_t;
    TraceId              Begin(const std::string &sKey, Clock::time_point tReceived);
    void                 SetRevision(TraceId nId, unsigned nRevision);
    void                 MarkSent(TraceId nId);
    void                 Event(TraceId nId, const std::string &sName, Clock::time_point tStart = {});
    std::vector<TraceId> Targets(unsigned nRevision) const;
    void                 Finish(const std::vector<TraceId> &aTargets, const std::string &sOutcome);
    void                 FinishAll(const std::string &sOutcome);
    void                 Poll(Clock::time_point tStart, bool bHadData);
    void                 PrintCompleted();

  private:
    struct EventRecord
    {
        std::string sName;
        int64_t     nOffsetUs   = 0;
        int64_t     nDurationUs = -1;
    };
    struct Trace
    {
        std::string              sKey;
        Clock::time_point        tReceived;
        Clock::time_point        tFinished{};
        unsigned                 nRevision = 0;
        bool                     bSent     = false;
        std::vector<EventRecord> aEvents;
        std::string              sOutcome;
        int64_t                  nTotalUs    = 0;
        unsigned                 nEmptyPolls = 0;
        unsigned                 nDataPolls  = 0;
        int64_t                  nPollUs     = 0;
    };
    std::map<TraceId, Trace> m_aTraces;
};

class InputTraceScope
{
  public:
    InputTraceScope(
        InputTraceRecorder &sRecorder, std::vector<InputTraceRecorder::TraceId> aTargets, const char *pName);
    ~InputTraceScope();
    InputTraceScope(const InputTraceScope &)            = delete;
    InputTraceScope &operator=(const InputTraceScope &) = delete;

  private:
    InputTraceRecorder                      &m_sRecorder;
    std::vector<InputTraceRecorder::TraceId> m_aTargets;
    std::string                              m_sName;
    InputTraceRecorder::Clock::time_point    m_tStart;
};
