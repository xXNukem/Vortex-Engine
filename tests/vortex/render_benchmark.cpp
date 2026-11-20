#include "vortex/render/first_person/render_workers.h"
#include "vortex/render/first_person/sprites/sprite_renderer.h"
#include "render_profile.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef VORTEX_RENDER_PROFILE
namespace vortex::render_profile {
std::uint64_t times[Count];
std::uint64_t work[WorkCount];
ProbeReading probes[ProbeCount];
bool probeEnabled = false;
}

namespace {
struct alignas(std::max_align_t) Allocation {
    std::size_t size;
};

std::size_t liveBytes = 0, peakBytes = 0, allocationCount = 0;
}

void* operator new(std::size_t size)
{
    if (size > std::size_t(-1) - sizeof(Allocation)) throw std::bad_alloc();
    auto* allocation = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!allocation) throw std::bad_alloc();
    new (allocation) Allocation{size};
    liveBytes += size;
    peakBytes = std::max(peakBytes, liveBytes);
    ++allocationCount;
    return allocation + 1;
}

void operator delete(void* pointer) noexcept
{
    if (!pointer) return;
    auto* allocation = static_cast<Allocation*>(pointer) - 1;
    liveBytes -= allocation->size;
    std::free(allocation);
}

void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
#endif

namespace {
using namespace vortex;
using namespace vortex::first_person;
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

std::uint64_t nanos(Clock::time_point start)
{
    return std::chrono::duration_cast<Nanoseconds>(Clock::now() - start).count();
}

Texture pattern(int width, int height, unsigned seed)
{
    Texture result{width, height, std::vector<Rgb>(std::size_t(width) * height)};
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        result.pixels[i] = {std::uint8_t(i * 13 + seed), std::uint8_t(i / width * 29 + seed),
                            std::uint8_t(i * 37 + i / width * 11 + seed)};
    }
    return result;
}

struct Geometry {
    Point a, b;
    std::uint16_t material;
};

enum class SpriteSet {
    Default, Large, Transparent, Small, Overlap, OpaqueOverlap, Partial, Hidden, Offscreen, Few, Layers
};

struct Case {
    const char* name;
    double nearWall, farWall, height, strength;
    bool dense, scalar, sprites, partial, grid, effect;
    SpriteSet spriteSet = SpriteSet::Default;
    bool planes = true;
    int wallRows = 74;
};

constexpr Case cases[] = {
    {"dense", 29, 1800, 96, 0, true, false, false, false, true, false},
    {"open", 1600, 2400, 96, 0, false, false, false, false, false, true},
    {"short", 95, 1500, 28, 0, false, false, false, true, true, false},
    {"scalar", 1600, 2400, 80, 0, false, true, false, false, false, false},
    {"sprites", 300, 1800, 100, 0, false, false, true, true, true, false},
    {"mixed", 95, 1000, 96, .38, true, false, true, true, true, true},
    {"warp", 1600, 2400, 96, .72, false, false, false, false, false, true},
    {"large-opaque", 300, 1800, 100, 0, false, false, true, true, true, false, SpriteSet::Large},
    {"large-transparent", 300, 1800, 100, 0, false, false, true, true, true, false,
     SpriteSet::Transparent},
    {"small", 300, 1800, 100, 0, false, false, true, true, true, false, SpriteSet::Small},
    {"overlap", 300, 1800, 100, 0, false, false, true, true, true, false, SpriteSet::Overlap},
    {"opaque-overlap", 300, 1800, 100, 0, false, false, true, true, true, false,
     SpriteSet::OpaqueOverlap, false},
    {"partial", 95, 1800, 100, 0, false, false, true, true, true, false, SpriteSet::Partial},
    {"hidden", 29, 1800, 100, 0, false, false, true, false, true, false, SpriteSet::Hidden},
    {"offscreen", 300, 1800, 100, 0, false, false, true, true, true, false, SpriteSet::Offscreen},
    {"w02-walls", 110, 900, 96, 0, false, false, false, false, true, false,
     SpriteSet::Default, false, 74},
    {"w02-few", 160, 900, 96, 0, false, false, true, false, true, false,
     SpriteSet::Few, false, 128},
    {"w02-overlap", 300, 900, 96, 0, false, false, true, false, true, false,
     SpriteSet::Overlap, false, 74},
    {"w02-tall", 27, 900, 120, 0, false, false, false, false, true, false,
     SpriteSet::Default, false, 128},
    {"w02-far", 980, 1750, 96, 0, false, false, false, false, false, false,
     SpriteSet::Default, false, 57},
    {"w02-mixed", 95, 1000, 96, .38, true, false, true, true, true, true,
     SpriteSet::Default, true, 73},
    {"w02-hidden", 29, 900, 100, 0, false, false, true, false, true, false,
     SpriteSet::Hidden, false, 74},
    {"w02-layers", 240, 900, 96, 0, false, false, true, true, true, true,
     SpriteSet::Layers, true, 128},
};

std::vector<Geometry> makeGeometry(const Case& scene)
{
    const double first = scene.partial ? -195 : -1000;
    const double last = scene.partial ? -14 : 1000;
    std::vector<Geometry> walls{
        {{scene.farWall, -4000}, {scene.farWall, 4000}, 1},
        {{-100, -1100}, {scene.farWall, -1100}, 2},
        {{-100, 1100}, {scene.farWall, 1100}, 2},
    };
    const int segments = scene.dense ? 96 : 16;
    for (int i = 0; i < segments; ++i) {
        const auto y = [&](int index) { return first + (last - first) * index / segments; };
        const auto x = [&](int index) { return scene.nearWall + (index % 3) * 3; };
        walls.push_back({{x(i), y(i)}, {x(i + 1), y(i + 1)}, 2});
    }
    if (scene.grid) {
        for (int i = 0; i < 160; ++i) {
            const double x = 350 + (i % 19) * 54;
            const double y = -1200 + (i / 19) * 280 + (i % 4) * 11;
            walls.push_back({{x, y}, {x + 23, y + 36}, 1});
        }
    }
    return walls;
}

struct Frames {
    explicit Frames(std::string_view mode, const char* path)
    {
        if (mode == "write") output.open(path, std::ios::binary);
        if (mode == "verify") input.open(path, std::ios::binary);
        if (!output.is_open() && !input.is_open()) throw std::runtime_error("frame file");
    }
    Frames() = default;

    void bytes(const void* data, std::size_t count)
    {
        const auto* first = static_cast<const char*>(data);
        if (output.is_open() && !output.write(first, count)) throw std::runtime_error("frame write");
        if (input.is_open()) {
            compared += count;
            while (count) {
                const auto chunk = std::min(count, scratch.size());
                if (!input.read(scratch.data(), chunk) ||
                    !std::equal(first, first + chunk, scratch.data()))
                    throw std::runtime_error("frame bytes differ");
                first += chunk;
                count -= chunk;
            }
        }
    }

    template<class T>
    void value(const T& object) { bytes(&object, sizeof(T)); }

    template<class T>
    void vector(const std::vector<T>& objects)
    {
        value(objects.size());
        if (!objects.empty()) bytes(objects.data(), objects.size() * sizeof(T));
    }

    std::ofstream output;
    std::ifstream input;
    std::uint64_t compared = 0;
    std::array<char, 65536> scratch{};
};

struct Materials {
    Texture tall = pattern(48, 74, 17);
    Texture other = pattern(33, 57, 42);
    Frames* frames = nullptr;
};

WallSample wallMaterial(const void* context, const RayHit& hit)
{
    const auto& materials = *static_cast<const Materials*>(context);
    const auto& texture = hit.type == 2 ? materials.tall : materials.other;
    const int x = std::min(int(hit.segmentPosition * hit.textureSpan * texture.width),
                           texture.width - 1);
    if (auto* frames = materials.frames) {
        frames->value('W');
        frames->value(hit.distance);
        frames->value(hit.type);
        frames->value(hit.segmentPosition);
        frames->value(hit.textureSpan);
        frames->value(x);
    }
    if (hit.type == 3) return {nullptr, 0, {117, 157, 234}};
    return {&texture, x, {}};
}

sprites::Bank makeSprites()
{
    sprites::Bank result;
    for (const auto [width, height] : {std::pair{29, 43}, {61, 22}}) {
        auto texture = pattern(width, height, 39);
        std::vector<std::uint8_t> indices(texture.pixels.size());
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto x = i % width, y = i / width;
            indices[i] = ((x + y) % 7 != 0 && x / 5 % 3 != 0) ? 1 : 0;
        }
        result.images.push_back({std::move(texture), width / 2, height - 3, std::move(indices)});
    }
    auto opaque = result.images.front();
    std::fill(opaque.indices.begin(), opaque.indices.end(), 1);
    result.images.push_back(std::move(opaque));
    auto transparent = result.images.front();
    std::fill(transparent.indices.begin(), transparent.indices.end(), 0);
    result.images.push_back(std::move(transparent));
    return result;
}

struct LayerObservation {
    Frames* frames = nullptr;
    Clock::time_point start;
    std::uint64_t spriteTime = 0;
};

void layerObservation(void* context, std::size_t index, bool after,
                      const std::vector<std::uint32_t>& pixels)
{
    auto& observation = *static_cast<LayerObservation*>(context);
    auto* frames = observation.frames;
    if (!frames) {
        if (after) observation.spriteTime += nanos(observation.start);
        else observation.start = Clock::now();
        return;
    }
    frames->value('L');
    frames->value(index);
    frames->value(after);
    frames->vector(pixels);
}

void verifyDirectSprites(Frames& frames)
{
    constexpr int width = 319, height = 177;
    const auto bank = makeSprites();
    const std::vector<sprites::Instance> objects{
        {55, 15, -2, 135, 0, true, false},
        {55, 15, -2, 135, 2},
        {55 + 1e-9, 15, -2, 135, 1, false, true, false, true},
        {25, -15, 9, 48, 1, true, true, true},
        {2, 180, 1, 20, 1},
        {-1, 0, 0, 40, 0},
    };
    std::vector<double> columns(width);
    for (int x = 0; x < width; ++x) columns[x] = x % 5 < 3 ? 25.0 : 130.0;
    std::fill_n(columns.begin() + 259, 3, 25.0);
    for (auto input : {sprites::DepthInput::MayContainNaN, sprites::DepthInput::NoNaN}) {
        std::vector<std::uint32_t> pixels(width * height, 0x12345678);
        std::vector<double> depth(pixels.size());
        for (std::size_t i = 0; i < depth.size(); ++i) {
            depth[i] = columns[i % width];
            if (input == sprites::DepthInput::MayContainNaN && i % 17 == 0)
                depth[i] = std::numeric_limits<double>::quiet_NaN();
        }
        sprites::render(pixels, depth, bank, objects, {0, 0, 0},
                        {width, height, 168.5, 25, columns.data()}, input);
        frames.vector(pixels);
        frames.vector(depth);
    }

    // Opaque coverage with live depth, wall-gated columns, and later equal-depth overlays.
    const std::vector<sprites::Instance> overlapping{
        {55, 0, 4, 85, 2},
        {55, 0, 4, 85, 2, true},
        {65, 0, 4, 85, 0},
        {65, 0, 39, 85, 1},
        {65, 25, 4, 85, 0},
        {55, 0, 4, 85, 1, true, true, false, true},
        {39, 15, 9, 48, 1, true, true},
    };
    std::vector<std::uint32_t> pixels(width * height, 0x12345678);
    std::vector<double> depth(pixels.size());
    for (std::size_t i = 0; i < depth.size(); ++i) depth[i] = columns[i % width];
    sprites::render(pixels, depth, bank, overlapping, {0, 0, 0},
                    {width, height, 168.5, 25, columns.data()}, sprites::DepthInput::NoNaN);
    frames.vector(pixels);
    frames.vector(depth);
}

void measure(const Case& scene, int width, int height, int samples, Frames* frames,
             RenderWorkers& workers, bool recordWalls)
{
    const auto walls = makeGeometry(scene);
    StaticRayGrid grid([&](auto visit) {
        if (scene.grid) for (const auto& wall : walls) visit(wall.a, wall.b);
    });
    PreparedRayScene prepared;
    prepared.walls.reserve(walls.size());
    prepared.dynamicSurfaces.reserve(1);
    Materials materials;
    if (scene.wallRows != materials.tall.height)
        materials.tall = pattern(48, scene.wallRows, 17);
    const PlaneBank planes{scene.scalar ? pattern(37, 65, 25) : pattern(128, 128, 25),
                           scene.scalar ? pattern(8, 9, 107) : pattern(128, 128, 107)};
    const auto bank = makeSprites();
    std::vector<sprites::Instance> objects;
    std::vector<sprites::Instance> overlays;
    if (scene.sprites && scene.spriteSet == SpriteSet::Default) {
        for (int i = 0; i < 48; ++i)
            objects.push_back({45.0 + i * 8, (i % 8 - 4) * 12.0, i % 5 * 6.0,
                               45.0 + i % 6 * 9, std::size_t(i % 2), i % 3 == 0, i % 7 == 0});
        overlays.push_back({45, -48, 0, 45, 0, true, false, false, true});
        overlays.push_back({scene.nearWall + 1, -45, 30, 34, 1, false, false, true});
    }
    switch (scene.spriteSet) {
    case SpriteSet::Default:
        break;
    case SpriteSet::Few:
        objects.push_back({74, -19, 9, 32, 0});
        objects.push_back({78, 26, 8, 38, 2});
        break;
    case SpriteSet::Layers:
        break;
    case SpriteSet::Large:
        objects.push_back({36, 0, 2, 98, 2});
        break;
    case SpriteSet::Transparent:
        objects.push_back({36, 0, 2, 98, 3});
        break;
    case SpriteSet::Small:
        for (int i = 0; i < 768; ++i)
            objects.push_back({500.0 + i % 19 * 43, (i % 51 - 25) * 18.0,
                               (i % 9) * 12.0, 8.0 + i % 7, std::size_t(i % 3)});
        break;
    case SpriteSet::OpaqueOverlap:
        for (int i = 0; i < 96; ++i)
            objects.push_back({55.0 + i, 0, 12, 70, 2});
        break;
    case SpriteSet::Overlap:
        for (int i = 0; i < 96; ++i)
            objects.push_back({65.0 + i / 2, 3, 6, 46, std::size_t(i % 3)});
        break;
    case SpriteSet::Partial:
        for (int i = 0; i < 24; ++i)
            objects.push_back({155.0 + i * 4, -18, 6, 77, std::size_t(i % 3)});
        break;
    case SpriteSet::Hidden:
    case SpriteSet::Offscreen:
        for (int i = 0; i < 128; ++i)
            objects.push_back({160.0 + i, scene.spriteSet == SpriteSet::Hidden ? 0.0 : 1500.0,
                               6, 68, std::size_t(i % 3)});
        break;
    }
    std::vector<SpriteLayer> layers{{&bank, &objects}, {&bank, &overlays}};
    std::array<std::vector<sprites::Instance>, 8> stacked;
    if (scene.spriteSet == SpriteSet::Layers) {
        layers.clear();
        for (std::size_t i = 0; i < stacked.size(); ++i) {
            const double distance = 95.0 - 7.0 * i;
            stacked[i].push_back({distance, 3, 5, 60, i % 3});
            stacked[i].push_back({distance, 3, 5, 60, 1, false, false, false, true});
            layers.push_back({&bank, &stacked[i]});
        }
    }
    FisheyeEffect effect({1.17, scene.strength});
    LayerObservation observation;
    SceneStyle style{wallMaterial, &materials, 0x5072a4ff, 0x17161cff,
                     scene.planes ? &planes : nullptr, scene.scalar ? 193.5 : 384.0,
                     layers.data(), scene.sprites ? layers.size() : 0, scene.effect ? &effect : nullptr,
                     layerObservation, &observation};
    std::vector<std::uint32_t> pixels;
    RenderBuffers buffers;
    const double focal = width * .69;
#ifdef VORTEX_RENDER_PROFILE
    const auto emptyFrameBytes = liveBytes;
#endif
    for (int sample = -6; sample < (frames ? 0 : samples); ++sample) {
        const int pose = (sample + 6) % 3;
        const CameraPose camera{pose * 2.125, -3.5 + pose * 2, .023 + pose * .071};
        const Point origin{camera.x, camera.y};
        const auto prepareStart = Clock::now();
        prepared.walls.clear();
        for (const auto& wall : walls)
            prepared.walls.push_back(prepareRaySurface(wall.a, wall.b, origin, wall.material, 1));
        prepared.dynamicSurfaces.assign(1, prepareRaySurface({189, 43}, {215, 73 + pose * 3.0},
                                                            origin, 3, .72));
        const View view{camera, {width, height, std::cos(camera.angle), std::sin(camera.angle),
                                 width * .5 / focal, focal, scene.height * .43},
                        scene.height, focal, prepared, scene.grid ? &grid : nullptr};
        const auto prepare = nanos(prepareStart);
        observation.frames = sample >= -3 ? frames : nullptr;
        materials.frames = recordWalls ? observation.frames : nullptr;
        observation.spriteTime = 0;
#ifdef VORTEX_RENDER_PROFILE
        std::fill_n(render_profile::times, render_profile::Count, 0);
        std::fill_n(render_profile::work, render_profile::WorkCount, 0);
        std::fill_n(render_profile::probes, render_profile::ProbeCount, render_profile::ProbeReading{});
        const auto startBytes = liveBytes;
        const auto startAllocations = allocationCount;
        peakBytes = liveBytes;
#endif
        const auto cpuStart = std::clock();
        const auto start = Clock::now();
        if (!workers.render(view, style, pixels, buffers)) throw std::runtime_error("render failed");
        const auto total = nanos(start);
        const auto cpuTotal = std::clock() - cpuStart;
#ifdef VORTEX_RENDER_PROFILE
        const auto retained = liveBytes - emptyFrameBytes;
        const auto transient = peakBytes - std::max(startBytes, liveBytes);
        const auto allocations = allocationCount - startAllocations;
#endif
        if (frames && sample >= -3) {
            frames->value('F');
            frames->vector(pixels);
            frames->vector(buffers.depth);
            frames->value(buffers.wallCoverage.size());
            for (const auto& [begin, end] : buffers.wallCoverage) {
                frames->value(begin);
                frames->value(end);
            }
            frames->vector(buffers.wallDepthColumns);
        }
        if (sample < 0 && sample != -6) continue;
        if (frames) continue;
        std::cout << scene.name << ',' << width << ',' << height << ',' << sample << ','
                  << prepare << ',' << total << ',' << observation.spriteTime
                  << ',' << std::uint64_t(double(cpuTotal) * 1e9 / CLOCKS_PER_SEC);
#ifdef VORTEX_RENDER_PROFILE
        for (auto time : render_profile::times) std::cout << ',' << time;
        for (auto count : render_profile::work) std::cout << ',' << count;
        for (const auto& probe : render_profile::probes)
            std::cout << ',' << probe.calls << ',' << probe.samples << ','
                      << probe.elapsed << ',' << probe.clockPair;
        std::cout << ',' << retained << ',' << transient << ',' << allocations << ',' << grid.bytes();
#endif
        std::cout << '\n';
    }
}
}

int main(int argc, char** argv)
try {
    const std::string_view mode = argc > 1 ? argv[1] : "time";
    bool timing = mode == "time";
#ifdef VORTEX_RENDER_PROFILE
    render_profile::probeEnabled = mode == "probe";
    timing = timing || render_profile::probeEnabled;
#endif
    if ((!timing && mode != "write" && mode != "verify") || argc > 5 ||
        (!timing && argc < 3)) return 1;
    const int samples = timing && argc >= 3 ? std::stoi(argv[2]) : 31;
    if (samples < 1) return 1;
    const std::string_view filter = argc >= 4 ? argv[3] : "";
    // An explicit thread count selects frame/layer comparison without the ordered
    // wall trace, which is part of the original serial benchmark format.
    const bool recordWalls = argc < 5;
    const int threads = recordWalls ? 1 : std::stoi(argv[4]);
    if (threads < 0) return 1;
#ifdef VORTEX_RENDER_PROFILE
    if (threads != 1) return 1;
#endif
    RenderWorkers workers{static_cast<unsigned>(threads)};
    Frames frames = timing ? Frames{} : Frames{mode, argv[2]};
    if (timing) {
        std::cout << "scene,width,height,sample,prepare_ns,total_ns,layer_sprite_ns,process_cpu_ns";
#ifdef VORTEX_RENDER_PROFILE
        std::cout << ",setup_ns,rays_ns,projection_ns,material_ns,walls_ns,planes_ns,depth_ns,"
                     "sprites_ns,fisheye_ns,sprite_project_ns,sprite_clip_ns,sprite_runs_ns,"
                     "sprite_raster_ns,depth_init_ns,depth_expand_ns,depth_columns_ns,"
                     "projected,empty_rect,wall_columns,hidden_rects,"
                     "transparent_pixels,depth_tests,depth_rejects,stores,"
                     "gate_calls,gate_samples,gate_probe_ns,gate_clock_ns,"
                     "texel_calls,texel_samples,texel_probe_ns,texel_clock_ns,"
                     "write_calls,write_samples,write_probe_ns,write_clock_ns,"
                     "column_gate_calls,column_gate_samples,column_gate_probe_ns,column_gate_clock_ns,"
                     "retained_bytes,transient_bytes,allocations,grid_bytes";
#endif
        std::cout << '\n';
    }
    for (const auto [width, height] : {std::pair{320, 200}, {640, 400}, {1280, 720},
                                      {1920, 1080}, {2560, 1440}, {3840, 2160}, {641, 481}, {1279, 721}}) {
        for (const auto& scene : cases) {
            if (std::string_view(scene.name).substr(0, filter.size()) != filter)
                continue;
            measure(scene, width, height, samples, timing ? nullptr : &frames, workers, recordWalls);
        }
    }
    if (!timing) verifyDirectSprites(frames);
    if (frames.input.is_open()) {
        if (frames.input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("unexpected extra frame bytes");
        std::cerr << "Verified " << frames.compared << " bytes\n";
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
