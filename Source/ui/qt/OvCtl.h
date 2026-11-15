#pragma once
// ============================================================
//  OvCtl — drive the UI's parameters from outside, through shared memory.
//
//  Why this exists: verifying a drag by moving a real pointer means guessing
//  window coordinates (the 1040x772 design box is scaled by fitScale and the
//  node's own position depends on the preset), and every guess that misses
//  produces a "nothing changed" that looks exactly like a rendering bug.  A
//  control block removes the guesswork: a parameter is named, a target and a
//  ramp time are given, and the preview moves it the way a hand would — a
//  continuous stream of small changes over several frames.
//
//  MIT shared memory, no sockets, no protocol.  The writer sets a slot and bumps
//  its sequence number; the reader (the preview) notices the bump and starts a
//  ramp from the parameter's current value to the new target, approaching it
//  exponentially:
//
//      v += (target - v) * (1 - exp (-dt / tau))
//
//  which is what a human drag looks like to the code under test: never a jump,
//  always a decaying step, and it holds at intermediate values for as long as the
//  drag would have taken.
// ============================================================
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <cmath>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ovctl
{

inline constexpr std::uint64_t kMagic   = 0x4F564552'43544C42ull;  // "OVER CTLB"
inline constexpr int           kSlots   = 256;
inline constexpr int           kIdChars = 40;
inline constexpr double        kDefaultTau = 0.35;   // seconds; a human-ish drag

struct Entry
{
    char     id[kIdChars] {};
    /// Target value in the parameter's SCALED units (Hz, dB, index).
    float    target = 0.0f;
    /// Time constant of the approach.  0 means "jump".
    float    tau = float (kDefaultTau);
    /// Bumped by the writer on every change.  The reader keeps the last value
    /// it acted on, so a slot is applied exactly once per set.
    std::atomic<std::uint32_t> seq { 0 };
    std::atomic<std::uint32_t> used { 0 };
};

struct Block
{
    std::atomic<std::uint64_t> magic { 0 };
    std::atomic<std::uint32_t> version { 1 };
    /// Bumped on every write, so a reader can cheaply skip an idle block.
    std::atomic<std::uint32_t> seq { 0 };
    /// Seconds since the block was created, for the reader to detect a stale
    /// mapping left by a crashed writer.
    std::atomic<std::uint64_t> heartbeatNs { 0 };
    Entry entries[kSlots];   // NOT "slots": Qt defines that as a macro
};

inline std::size_t blockBytes() { return sizeof (Block); }

inline std::string shmName (const std::string& bare)
{
    if (! bare.empty() && bare[0] == '/') return bare;
    return "/" + bare;
}

/// Map the block.  `create` makes it (and truncates an existing one).
inline Block* map (const std::string& bareName, bool create)
{
    const std::string name = shmName (bareName);
    const int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    const int fd = ::shm_open (name.c_str(), flags, 0600);
    if (fd < 0) return nullptr;

    if (create)
    {
        if (::ftruncate (fd, off_t (blockBytes())) != 0)
        {
            ::close (fd);
            return nullptr;
        }
    }

    void* p = ::mmap (nullptr, blockBytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close (fd);
    if (p == MAP_FAILED) return nullptr;

    auto* b = static_cast<Block*> (p);
    if (create)
    {
        std::memset (static_cast<void*> (b), 0, blockBytes());
        b->magic.store (kMagic, std::memory_order_release);
        b->version.store (1, std::memory_order_release);
    }
    else if (b->magic.load (std::memory_order_acquire) != kMagic)
    {
        // Someone else's segment, or an unfinished create: refuse rather than
        // reinterpret memory we do not own.
        ::munmap (p, blockBytes());
        return nullptr;
    }
    return b;
}

/// Attach to an EXISTING block, creating one only if there is none.
///
/// The writer must not truncate: `set` used to open with create=true every time,
/// which ftruncate()s and memsets the whole block, so each command erased the
/// one before it.  Measured: `ovctl list` always reported 0 slots and the preview
/// never saw a value, while every individual `set` looked successful.
inline Block* attach (const std::string& bareName)
{
    if (Block* b = map (bareName, false))
        return b;                    // already there: keep its contents
    return map (bareName, true);     // first one wins and creates it
}

inline void unmap (Block* b)
{
    if (b != nullptr) ::munmap (static_cast<void*> (b), blockBytes());
}

/// Writer side: set (or create) an entry by id and bump its sequence.
inline bool setParam (Block* b, const std::string& id, double target, double tau,
                 bool* created = nullptr)
{
    if (b == nullptr || id.size() + 1 > std::size_t (kIdChars)) return false;

    for (int i = 0; i < kSlots; ++i)
    {
        Entry& s = b->entries[i];
        if (s.used.load (std::memory_order_acquire) == 0) continue;
        if (std::strncmp (s.id, id.c_str(), kIdChars) != 0) continue;

        s.target = float (target);
        s.tau    = float (std::max (0.0, tau));
        s.seq.fetch_add (1, std::memory_order_release);
        b->seq.fetch_add (1, std::memory_order_release);
        if (created) *created = false;
        return true;
    }

    for (int i = 0; i < kSlots; ++i)
    {
        Entry& s = b->entries[i];
        if (s.used.load (std::memory_order_acquire) != 0) continue;

        std::memset (s.id, 0, kIdChars);
        std::strncpy (s.id, id.c_str(), kIdChars - 1);
        s.target = float (target);
        s.tau    = float (std::max (0.0, tau));
        s.seq.store (1, std::memory_order_release);
        s.used.store (1, std::memory_order_release);
        b->seq.fetch_add (1, std::memory_order_release);
        if (created) *created = true;
        return true;
    }
    return false;
}

/// The value a ramp should have moved to after `dt`, starting from `from`.
///
/// Exponential approach, not a linear one: a hand on a mouse moves fast and then
/// settles, and this is the same shape with a single time constant.  `tau <= 0`
/// means "jump", which is what a preset load should do.
inline double approach (double from, double target, double dt, double tau)
{
    if (tau <= 1e-6 || dt <= 0.0) return target;
    const double k = 1.0 - std::exp (-dt / tau);
    return from + (target - from) * k;
}

}  // namespace ovctl
