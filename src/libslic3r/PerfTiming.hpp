///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_PerfTiming_hpp_
#define slic3r_PerfTiming_hpp_

#include <chrono>
#include <cstdio>
#include <atomic>

namespace Slic3r
{

// Set to true to enable pipeline stage timing output to stderr.
// When false, all timing code is eliminated by the compiler.
static constexpr bool PERF_TIMING = false;

// Wall-clock stage timer. Reports elapsed time since last reset/stage call.
class PerfStageTimer
{
public:
    void reset()
    {
        if constexpr (PERF_TIMING)
            m_t0 = std::chrono::steady_clock::now();
    }
    void stage(const char *name)
    {
        if constexpr (PERF_TIMING)
        {
            auto now = std::chrono::steady_clock::now();
            fprintf(stderr, "[TIMING] %-40s %7.1fms\n", name,
                    std::chrono::duration<double, std::milli>(now - m_t0).count());
            m_t0 = now;
        }
    }

private:
    std::chrono::steady_clock::time_point m_t0;
};

// Accumulator for timing across parallel threads. Use atomic counters
// that sum microseconds, then print a summary after the parallel region.
class PerfAccumTimer
{
public:
    void reset()
    {
        if constexpr (PERF_TIMING)
            m_us.store(0, std::memory_order_relaxed);
    }
    void add(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
    {
        if constexpr (PERF_TIMING)
            m_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(),
                           std::memory_order_relaxed);
    }
    double ms() const
    {
        if constexpr (PERF_TIMING)
            return m_us.load(std::memory_order_relaxed) / 1000.0;
        return 0.0;
    }

private:
    std::atomic<int64_t> m_us{0};
};

// Scoped timer that adds elapsed time to an accumulator on destruction.
class PerfScopedTimer
{
public:
    explicit PerfScopedTimer(PerfAccumTimer &accum) : m_accum(accum)
    {
        if constexpr (PERF_TIMING)
            m_start = std::chrono::steady_clock::now();
    }
    ~PerfScopedTimer()
    {
        if constexpr (PERF_TIMING)
            m_accum.add(m_start, std::chrono::steady_clock::now());
    }
    PerfScopedTimer(const PerfScopedTimer &) = delete;
    PerfScopedTimer &operator=(const PerfScopedTimer &) = delete;

private:
    PerfAccumTimer &m_accum;
    std::chrono::steady_clock::time_point m_start;
};

// Global slice-start timestamp for total elapsed measurement.
inline std::chrono::steady_clock::time_point &perf_slice_start()
{
    static std::chrono::steady_clock::time_point t;
    return t;
}

inline void perf_print(const char *fmt, ...)
{
    if constexpr (!PERF_TIMING)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

} // namespace Slic3r

#endif // slic3r_PerfTiming_hpp_
