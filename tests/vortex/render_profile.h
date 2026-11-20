#pragma once

#ifdef VORTEX_RENDER_PROFILE
#include <atomic>
#include <chrono>
#include <cstdint>

namespace vortex::render_profile {
enum Phase { Setup, Rays, Projection, Material, Walls, Planes, Depth, Sprites, Fisheye, SpriteProject, SpriteClip, SpriteRuns, SpriteRaster,
             DepthInit, DepthExpand, DepthColumns, Count };
using Clock = std::chrono::steady_clock;
extern std::uint64_t times[Count];
enum Work { Projected, EmptyRect, WallColumns, HiddenRects, TransparentPixels,
            DepthTests, DepthRejects, Stores, WorkCount };
extern std::uint64_t work[WorkCount];

enum Probe { SpriteGate, SpriteTexel, SpriteWrite, SpriteColumnGate, ProbeCount };
struct ProbeReading {
    std::uint64_t calls = 0, samples = 0, elapsed = 0, clockPair = 0;
};
extern ProbeReading probes[ProbeCount];
extern bool probeEnabled;

template<Probe phase, class Operation>
auto probe(Operation operation)
{
    if (!probeEnabled) return operation();
    auto& reading = probes[phase];
    if (++reading.calls % 257 != 1) return operation();

    // Sample infrequently and record an empty interval for the clock overhead.
    // These probes are diagnostic, since timing changes locality and scheduling.
    const auto start = Clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const volatile auto result = operation();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto end = Clock::now();
    const volatile auto empty = decltype(operation()){};
    (void)empty;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto calibration = Clock::now();

    ++reading.samples;
    reading.elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    reading.clockPair += std::chrono::duration_cast<std::chrono::nanoseconds>(calibration - end).count();
    return static_cast<decltype(operation())>(result);
}

class Sample {
public:
    explicit Sample(Phase phase) : phase_(phase), start_(Clock::now()) {}
    ~Sample()
    {
        times[phase_] += std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start_).count();
    }

private:
    Phase phase_;
    Clock::time_point start_;
};
}
#define VORTEX_MEASURE(phase) const ::vortex::render_profile::Sample measure_##phase(::vortex::render_profile::phase)
#endif
