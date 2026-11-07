#pragma once
// ============================================================
//  OvTick — a MIDI-clock style tick reporter.
//
//  Prints one line per named series with the numbers that make a rate claim
//  checkable: how many, how often, over how long, and how evenly.
//
//      [tick] sample        n=  142  hz= 11.83  over= 12.00s  dt= 84.5ms
//                           min= 71.2  max= 96.4  jitter= 4.3 (sd)
//                           drift= +0.0ms/interval
//      [tick] frame         n= 1682  hz=140.17  over= 12.00s  dt=  7.1ms
//                           min=  6.8  max= 17.9  jitter= 0.9 (sd)
//
//  Why so much detail for a counter: "it runs at 60 fps" and "it stutters" are
//  the same number (the mean) and completely different experiences.  The
//  spread is what separates them, so `jitter` is the standard deviation of the
//  intervals and `min`/`max` are the extremes.  `drift` is the mean interval
//  minus the interval the series was DECLARED to run at — a series that claims
//  8.33 ms and averages 7.10 ms has drifted 1.2 ms per interval, i.e. it is
//  running 17 % fast.
//
//  Everything is measured against steady_clock, never against wall-clock time
//  or a frame counter, because both of those are what is being questioned.
// ============================================================
#include "OvShmRing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <functional>
#include <thread>
#include <vector>

namespace ovtick
{

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
//  The wire format.
//
//  Ticks cross from the producer (audio callback, and the GUI thread for paint
//  events) to the consumer (the reporter) through a lock-free shared-memory
//  ring, not through the same thread.  That is deliberate: the audio callback
//  must not print, allocate or take a lock, and the reporter must be able to run
//  even when the GUI thread is stuck — which is exactly the state that needed
//  diagnosing when the meters stopped.
//
//  The record is POD and fixed-size so a slot copy is a memcpy and the layout is
//  identical in both processes.
// ---------------------------------------------------------------------------
enum class TickKind : std::uint32_t
{
    Sample = 1,      // one poll of the analyser/meters
    FftBlock,        // a new FFT block arrived
    MetersChanged,   // the meter values moved
    SpectrumChanged, // the spectrum table moved
    ParamsChanged,   // a parameter moved
    FrameSwapped,    // a frame was presented
    PaintSc,         // the SC plot repainted
    PaintPq,         // the PQ plot repainted
    PaintMeter,      // the meter pod repainted
};

struct TickRecord
{
    std::uint64_t ns = 0;        // steady_clock nanoseconds, absolute
    TickKind      kind {};
    std::uint32_t threadId = 0;  // diagnostic: which thread actually produced it
};

constexpr std::size_t kRingRecords = 8192;    // power of two
using Ring = ovshm::Ring<TickRecord, kRingRecords>;

/// Which producer a tick came from.
///
/// The ring is SINGLE-producer by design (see OvShmRing.h), so there has to be
/// one ring per thread that pushes.  There are exactly two:
///
///   Gui     OvEngine's poll, the parameter scan, and every paint — all on the
///           Qt GUI thread.
///   Render  QQuickWindow::frameSwapped, which Qt delivers on its RENDER thread.
///
/// Sharing one ring between those two produced negative intervals in the very
/// first run — `min=-96.02ms` — because two threads advanced the write index
/// concurrently.  The timestamp maths was fine; the buffer was being corrupted.
enum class Producer { Gui = 0, Render = 1, kCount = 2 };

constexpr std::size_t kProducers = (std::size_t) Producer::kCount;

inline const char* producerName (Producer p)
{
    return p == Producer::Gui ? "gui" : "render";
}

inline const char* kindName (TickKind k)
{
    switch (k)
    {
        case TickKind::Sample:          return "sample";
        case TickKind::FftBlock:        return "fft-block";
        case TickKind::MetersChanged:   return "meters-new";
        case TickKind::SpectrumChanged: return "spectrum-new";
        case TickKind::ParamsChanged:   return "params-new";
        case TickKind::FrameSwapped:    return "frame";
        case TickKind::PaintSc:         return "paint-sc";
        case TickKind::PaintPq:         return "paint-pq";
        case TickKind::PaintMeter:      return "paint-meter";
    }
    return "?";
}

struct Series
{
    const char* name = "";
    double expectedHz = 0.0;      // 0 = the series is not paced, drift is n/a
    long long count = 0;
    Clock::time_point first {};
    Clock::time_point last {};

    // interval statistics, in milliseconds
    double sumDt = 0.0, sumDt2 = 0.0;
    double minDt = 0.0, maxDt = 0.0;

    void mark (Clock::time_point now)
    {
        if (count > 0)
        {
            const double dt = std::chrono::duration<double, std::milli> (now - last).count();
            if (count == 1 || dt < minDt) minDt = dt;
            if (count == 1 || dt > maxDt) maxDt = dt;
            sumDt += dt;
            sumDt2 += dt * dt;
        }
        else
        {
            first = now;
        }
        last = now;
        ++count;
    }

    double elapsedSeconds() const
    {
        if (count < 2) return 0.0;
        return std::chrono::duration<double> (last - first).count();
    }

    void report (std::FILE* f) const
    {
        if (count == 0)
        {
            std::fprintf (f, "  [tick] %-12s no ticks\n", name);
            return;
        }

        const double secs = elapsedSeconds();
        const double intervals = count > 1 ? double (count - 1) : 0.0;
        const double mean = intervals > 0.0 ? sumDt / intervals : 0.0;

        // Sample standard deviation of the intervals: the "does it stutter"
        // number.  Population form (divide by n) would understate it for the
        // short windows this is used over.
        double sd = 0.0;
        if (intervals > 1.0)
        {
            const double var = (sumDt2 - intervals * mean * mean) / (intervals - 1.0);
            sd = var > 0.0 ? std::sqrt (var) : 0.0;
        }

        std::fprintf (f, "  [tick] %-12s n=%5lld  hz=%7.2f  over=%6.2fs  dt=%6.2fms",
                      name, count, secs > 0.0 ? intervals / secs : 0.0, secs, mean);
        if (count > 2)
            std::fprintf (f, "  min=%6.2f  max=%6.2f  jitter=%5.2f", minDt, maxDt, sd);

        if (expectedHz > 0.0)
        {
            const double want = 1000.0 / expectedHz;
            // Per-interval difference, which is what accumulates: 1 ms of drift
            // per interval is nothing over a second and everything over a minute.
            std::fprintf (f, "  drift=%+6.2fms (want %5.2f, %+5.1f%%)",
                          mean - want, want,
                          want > 0.0 ? (mean - want) / want * 100.0 : 0.0);
        }
        std::fprintf (f, "\n");
    }
};

class Registry
{
public:
    static Registry& instance()
    {
        static Registry r;
        return r;
    }

    /// Master switch.  Nothing is recorded or printed when this is false, so the
    /// counters cost one branch when `--tick` is absent.
    bool enabled = false;

    /// How often the summary is printed, in seconds.
    double reportEvery = 5.0;

    /// Open (or attach to) the ring for one producer.  Called once per thread.
    bool openRing (Producer p, const std::string& name, bool create)
    {
        ringName[(int) p] = name;
        return rings[(int) p].open (name, create);
    }

    Ring& shm (Producer p) { return rings[(int) p]; }

    /// Push one tick into the ring of the given producer.  Safe from the audio
    /// callback and the render thread: no allocation, no lock, no printf.
    void push (TickKind kind, Producer p = Producer::Gui)
    {
        if (! enabled) return;
        Ring& ring = rings[(int) p];
        if (! ring.ok()) return;

        TickRecord r;
        r.ns = (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (
                   Clock::now().time_since_epoch()).count();
        r.kind = kind;
        // Which thread really produced it.  The producer is declared at the
        // call site and was wrong twice; recording the truth costs one add.
        static thread_local std::uint32_t tid = [] {
            return (std::uint32_t) (std::hash<std::thread::id> {} (std::this_thread::get_id()) & 0xffffu);
        }();
        r.threadId = tid;
        ring.push (r);                    // overwrites the oldest when full
    }

    void markKind (const char* name, TickKind kind, double expectedHz = 0.0,
                   Producer p = Producer::Gui)
    {
        // When the ring is live the CONSUMER does all the counting — it folds
        // every record as it drains.  Counting here as well counted each tick
        // twice, once with the producer's timestamp and once with the consumer's,
        // and the two interleave: that is what produced `min=-96.63ms`, an
        // interval that cannot exist.  The buffer was never corrupt; the
        // statistics were double-entered.
        if (ringReady())
        {
            push (kind, p);
            return;
        }
        mark (name, expectedHz, false);
    }

    /// True when at least one producer ring is open, i.e. the consumer is the
    /// one keeping the books.
    bool ringReady() const
    {
        for (const auto& ring : rings)
            if (ring.ok()) return true;
        return false;
    }

    void mark (const char* name, double expectedHz = 0.0, bool alsoPush = false)
    {
        if (! enabled) return;
        if (alsoPush) push (TickKind::Sample);

        const auto now = Clock::now();

        Series* s = find (name);
        if (s == nullptr)
        {
            stats.emplace_back();
            s = &stats.back();
            s->name = name;
            s->expectedHz = expectedHz;
        }
        s->mark (now);

        // Print on a wall of elapsed time, not on a tick count: a series that
        // runs 150 times a second must not decide how often the report appears.
        if (nextReport == Clock::time_point())
            nextReport = now + std::chrono::milliseconds (static_cast<long long> (reportEvery * 1000.0));
        else if (now >= nextReport)
        {
            nextReport = now + std::chrono::milliseconds (static_cast<long long> (reportEvery * 1000.0));
            reportAll (stdout);
        }
    }

    void reportAll (std::FILE* f)
    {
        if (stats.empty()) return;
        std::fprintf (f, "[tick] ---- %zu series ----\n", stats.size());
        for (auto& s : stats) s.report (f);
        std::fflush (f);
    }

    /// Zero the counters but keep the series, so the next window is clean.
    void resetCounters()
    {
        for (auto& s : stats)
        {
            s.count = 0;
            s.sumDt = s.sumDt2 = 0.0;
            s.minDt = s.maxDt = 0.0;
            s.first = s.last = Clock::time_point();
        }
    }

    void resetWindow() { nextReport = Clock::time_point(); }

    Series* find (const char* name)
    {
        for (auto& s : stats)
            if (s.name == name || (s.name != nullptr && std::string (s.name) == name))
                return &s;
        return nullptr;
    }

    /// The name a ring is published under, for a reader process.
    static std::string ringNameFor (int pid, Producer p)
    {
        return "/onyxteon-tick-" + std::to_string (pid) + "-" + producerName (p);
    }

    /// ---- consumer ------------------------------------------------------
    /// Drain the ring and fold every record into its series.  Returns the
    /// number of records consumed.
    ///
    /// This is what makes the counters measurements rather than impressions:
    /// the paint ticks are produced by the GUI thread but COUNTED here, so the
    /// rate is not affected by how often the GUI thread felt like reporting.
    int drainRing()
    {
        int n = 0;
        for (auto& ring : rings)
            if (ring.ok())
                n += ring.drain ([this] (const TickRecord& r) { fold (r); });

        // The periodic report belongs HERE, not in mark(): with the ring live,
        // mark() is never called (the consumer counts), so scheduling the print
        // from there meant it never happened at all.
        const auto now = Clock::now();
        if (nextReport == Clock::time_point())
            nextReport = now + std::chrono::milliseconds (
                             static_cast<long long> (reportEvery * 1000.0));
        else if (now >= nextReport)
        {
            nextReport = now + std::chrono::milliseconds (
                             static_cast<long long> (reportEvery * 1000.0));
            reportAll (stdout);
        }
        return n;
    }

    void fold (const TickRecord& r)
    {
        const char* n = kindName (r.kind);
        double want = 0.0;
        if (r.kind == TickKind::Sample) want = 1000.0 / double (1000);   // see below
        Series* ser = find (n);
        if (ser == nullptr)
        {
            stats.emplace_back();
            ser = &stats.back();
            ser->name = n;
            ser->expectedHz = want;
        }
        // A negative interval means the timestamps arrived out of order, which
        // can only happen when two threads write one ring.  Name it loudly
        // rather than letting the statistics absorb it.
        if (ser->count > 0 && r.ns < lastNs[(int) r.kind])
        {
            static int warned = 0;
            if (warned < 3)
            {
                ++warned;
                std::fprintf (stderr, "[tick] OUT OF ORDER: %s went backwards by %.2f ms "
                                      "(thread %u) — two producers are sharing a ring\n",
                              n, double (lastNs[(int) r.kind] - r.ns) / 1e6, r.threadId);
            }
        }
        lastNs[(int) r.kind] = r.ns;

        // Convert the record's absolute timestamp back into a time_point so the
        // intervals are computed from WHEN THE TICK HAPPENED, not when it was
        // read.  Reading a batch of twelve records "at once" would otherwise
        // show twelve zero-length intervals and a completely wrong rate.
        ser->mark (Clock::time_point (std::chrono::nanoseconds (r.ns)));
    }

private:
    std::vector<Series> stats;
    Clock::time_point nextReport {};
    Ring        rings[kProducers];
    std::string ringName[kProducers];
    std::uint64_t lastNs[16] {};
};

/// Record one tick.  `expectedHz` is only read the first time a name is seen.
inline void mark (const char* name, double expectedHz = 0.0)
{
    Registry::instance().mark (name, expectedHz);
}

/// Record one tick of a known kind: counted here AND pushed to the shared ring.
inline void markKind (const char* name, TickKind kind, double expectedHz = 0.0,
                      Producer p = Producer::Gui)
{
    Registry::instance().markKind (name, kind, expectedHz, p);
}

/// Pusher only — for the audio callback, where nothing may allocate or print.
inline void pushOnly (TickKind kind, Producer p = Producer::Gui)
{
    Registry::instance().push (kind, p);
}

} // namespace ovtick
