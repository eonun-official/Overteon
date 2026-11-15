#pragma once
// ============================================================
//  OvShmRing — a lock-free single-producer / single-consumer ring buffer in
//  POSIX shared memory.
//
//  Why this and not a mutex: the producer is the JACK audio callback.  A lock
//  there means the audio thread can be made to wait by a UI thread that was
//  preempted, which is how a plug-in produces dropouts at 0 % CPU.  The ring
//  below never blocks: the producer stores to a slot then advances a write
//  index, the consumer does the same with a read index, and the only shared
//  mutable state is two atomic counters.
//
//  Why SHARED MEMORY rather than plain memory: the same ring can be opened by
//  another process.  That is what makes the tick data inspectable from outside
//  a hung UI — `bench.py` attaches to the name and reads the last records
//  without asking the audio thread anything.  It also means the preview can be
//  restarted while a reader keeps its mapping.
//
//  Correctness notes, because lock-free code is easy to get subtly wrong:
//
//   * exactly one producer and one consumer.  Two producers would need a CAS
//     loop and would stop being wait-free; nothing here needs that.
//
//   * indices are FREE-RUNNING (they never wrap), and the slot is
//     `index % capacity`.  Wrapping the counters would need a modulo on every
//     read and would make "full" and "empty" indistinguishable when the buffer
//     holds a multiple of the capacity; free-running counters only disagree
//     once every 2^64 records.
//
//   * the producer reads `read` with acquire and writes `write` with release,
//     so the slot's payload is visible before the index that publishes it.  The
//     consumer mirrors this.  This is the whole memory-ordering argument and it
//     is why the atomics are not relaxed.
//
//   * capacity must be a power of two so `%` becomes `&`.  The audio callback
//     runs 48000/64 times a second; a division there is measurable.
//
//   * if the consumer is too slow the OLDEST record is dropped (the producer
//     overwrites and moves on).  For metering data that is correct: a stale
//     level matters less than the current one, and the alternative — blocking —
//     is the thing we are avoiding.  Drops are counted so they are visible
//     rather than silent.
//
//  Layout in shared memory:
//      [ Header (atomics + counters) ] [ capacity * T ]
// ============================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
// POSIX shared memory (shm_open/mmap) does not exist on Windows.  This header
// backs OVTICK's cross-process sampling ring, which is a development
// instrument: an external reader attaches to the block and prints the rate
// statistics.  On Windows the ring keeps the same interface and reports itself
// as not-open, so push() is a no-op and pop() finds nothing.  The per-interval
// timing that OVTICK computes in-process is unaffected -- only the
// cross-process tap is missing.
namespace ovshm
{

constexpr std::size_t kCacheLine = 64;

template <typename T, std::size_t Capacity>
class Ring
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0,
                   "Capacity must be a power of two");

    Ring() = default;
    Ring (const Ring&) = delete;
    Ring& operator= (const Ring&) = delete;

    bool open (const std::string&, bool) { return false; }
    void close() {}
    bool ok() const { return false; }
    bool push (const T&) { return false; }
    bool pop (T&) { return false; }
    template <typename Fn> int drain (Fn&&) { return 0; }
    std::uint64_t available() const { return 0; }
    std::uint64_t dropped() const { return 0; }
    std::uint64_t produced() const { return 0; }
    std::uint64_t consumed() const { return 0; }
};

} // namespace ovshm

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ovshm
{

/// Cache-line size for padding.  64 is right on x86-64 and arm64; being wrong
/// only costs a little false sharing, never correctness.
constexpr std::size_t kCacheLine = 64;

struct Header
{
    // Producer writes `write`; consumer writes `read`.  Padding keeps them off
    // each other's cache line: without it, every store by one side invalidates
    // the other side's line and the buffer is "lock-free" but slow.
    alignas (kCacheLine) std::atomic<std::uint64_t> write { 0 };
    alignas (kCacheLine) std::atomic<std::uint64_t> read  { 0 };

    alignas (kCacheLine) std::atomic<std::uint64_t> capacity { 0 };
    alignas (kCacheLine) std::atomic<std::uint64_t> dropped  { 0 };   // producer side
    alignas (kCacheLine) std::atomic<std::uint64_t> produced { 0 };   // total ever written
    alignas (kCacheLine) std::atomic<std::uint64_t> consumed { 0 };   // total ever read

    /// Set once by the creator so a reader can tell a live ring from a stale
    /// mapping left by a crashed instance.
    alignas (kCacheLine) std::atomic<std::uint64_t> magic { 0 };
    alignas (kCacheLine) std::atomic<std::uint64_t> timestampNs { 0 };
};

inline constexpr std::uint64_t kMagic = 0x4F564552'52494E47ull;   // "OVER RING"

/// A mapped ring of T.  Create or attach; both sides get the same object.
template <typename T, std::size_t Capacity>
class Ring
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0,
                   "Capacity must be a power of two so the index wrap is a mask");

    Ring() = default;
    ~Ring() { close(); }

    Ring (const Ring&) = delete;
    Ring& operator= (const Ring&) = delete;

    /// Create (or open) the ring and map it.  `name` must start with '/'.
    /// Both the producer and the consumer call this with the same name.
    bool open (const std::string& name, bool create)
    {
        close();
        m_name = name;
        m_creator = create;

        const int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
        m_fd = ::shm_open (name.c_str(), flags, 0600);
        if (m_fd < 0)
        {
            std::fprintf (stderr, "[shm] shm_open(%s) failed\n", name.c_str());
            return false;
        }

        const std::size_t bytes = sizeof (Header) + Capacity * sizeof (T);
        if (create)
        {
            if (::ftruncate (m_fd, (off_t) bytes) != 0)
            {
                std::fprintf (stderr, "[shm] ftruncate(%s) failed\n", name.c_str());
                close();
                return false;
            }
        }

        void* p = ::mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (p == MAP_FAILED)
        {
            std::fprintf (stderr, "[shm] mmap(%s) failed\n", name.c_str());
            close();
            return false;
        }

        m_base  = static_cast<std::uint8_t*> (p);
        m_bytes = bytes;
        m_header = reinterpret_cast<Header*> (m_base);
        m_slots  = reinterpret_cast<T*> (m_base + sizeof (Header));

        if (create)
        {
            // The mapping of a fresh segment arrives zeroed, so the counters are
            // already 0; only the identity and the capacity need writing, and
            // the magic LAST so a racing reader cannot see a half-built ring.
            m_header->capacity.store (Capacity, std::memory_order_relaxed);
            m_header->write.store (0, std::memory_order_relaxed);
            m_header->read.store (0, std::memory_order_relaxed);
            m_header->dropped.store (0, std::memory_order_relaxed);
            m_header->produced.store (0, std::memory_order_relaxed);
            m_header->consumed.store (0, std::memory_order_relaxed);
            m_header->magic.store (kMagic, std::memory_order_release);
        }
        else if (m_header->magic.load (std::memory_order_acquire) != kMagic)
        {
            std::fprintf (stderr, "[shm] %s is not our ring (magic mismatch)\n",
                          name.c_str());
            close();
            return false;
        }

        m_ok = true;
        return true;
    }

    void close()
    {
        if (m_base != nullptr && m_base != MAP_FAILED)
            ::munmap (m_base, m_bytes);
        m_base = nullptr;
        m_header = nullptr;
        m_slots = nullptr;
        if (m_fd >= 0)
        {
            ::close (m_fd);
            m_fd = -1;
            if (m_creator) ::shm_unlink (m_name.c_str());
        }
        m_ok = false;
    }

    bool ok() const { return m_ok; }
    const std::string& name() const { return m_name; }

    // ---- producer side --------------------------------------------------
    /// Write one record.  Never blocks.  Returns false if it had to drop the
    /// oldest record to make room (which is still a successful write).
    bool push (const T& v)
    {
        if (! m_ok) return false;

        const std::uint64_t w = m_header->write.load (std::memory_order_relaxed);
        const std::uint64_t r = m_header->read.load (std::memory_order_acquire);

        bool dropped = false;
        if (w - r >= Capacity)
        {
            // Full: the consumer is behind.  Move the read index forward so the
            // oldest slot is reused.  Only the producer may do this here, and it
            // is safe because the consumer's own `read` store is the only other
            // writer and it can only ever move this forward too.
            m_header->read.store (w - Capacity + 1, std::memory_order_release);
            m_header->dropped.fetch_add (1, std::memory_order_relaxed);
            dropped = true;
        }

        m_slots[(std::size_t) (w & (Capacity - 1))] = v;
        // Release: the slot's contents must be visible before the index moves.
        m_header->write.store (w + 1, std::memory_order_release);
        m_header->produced.fetch_add (1, std::memory_order_relaxed);
        return ! dropped;
    }

    // ---- consumer side --------------------------------------------------
    /// Read one record if there is one.
    bool pop (T& out)
    {
        if (! m_ok) return false;

        const std::uint64_t r = m_header->read.load (std::memory_order_relaxed);
        const std::uint64_t w = m_header->write.load (std::memory_order_acquire);
        if (r >= w) return false;

        out = m_slots[(std::size_t) (r & (Capacity - 1))];
        // Release so the producer cannot overwrite this slot before we are done
        // reading it.  (It reads `read` with acquire.)
        m_header->read.store (r + 1, std::memory_order_release);
        m_header->consumed.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    /// Drain everything available, calling fn(T) for each record.  Returns how
    /// many were read.
    template <typename Fn>
    int drain (Fn&& fn)
    {
        T v;
        int n = 0;
        while (pop (v)) { fn (v); ++n; }
        return n;
    }

    std::uint64_t available() const
    {
        if (! m_ok) return 0;
        const std::uint64_t r = m_header->read.load (std::memory_order_acquire);
        const std::uint64_t w = m_header->write.load (std::memory_order_acquire);
        return w > r ? w - r : 0;
    }

    std::uint64_t dropped()  const { return m_ok ? m_header->dropped.load (std::memory_order_relaxed) : 0; }
    std::uint64_t produced() const { return m_ok ? m_header->produced.load (std::memory_order_relaxed) : 0; }
    std::uint64_t consumed() const { return m_ok ? m_header->consumed.load (std::memory_order_relaxed) : 0; }

private:
    std::string    m_name;
    int            m_fd = -1;
    std::uint8_t*  m_base = nullptr;
    std::size_t    m_bytes = 0;
    Header*        m_header = nullptr;
    T*             m_slots = nullptr;
    bool           m_ok = false;
    bool           m_creator = false;
};

} // namespace ovshm

#endif   // !_WIN32

