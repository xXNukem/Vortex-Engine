#include "vortex/render/first_person/render_workers.h"

#include "../worker_pool.h"
#include "planes/plane_pass.h"
#include "postprocess/fisheye_pass.h"
#include "raycast/grid_query.h"
#include "sprites/sprite_pass.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef VORTEX_RENDER_PROFILE
#include "render_profile.h"
#else
#define VORTEX_MEASURE(phase)
#endif

#ifdef __APPLE__
#include <cstring>
#endif

namespace vortex::first_person {
namespace {
// Select timing once per view, keeping clock checks out of the untimed column loop.
template <bool Enabled>
class Sample {
public:
    Sample(const Timing& timing, Stage stage)
        : timing_(timing), stage_(stage), start_(Enabled ? timing.now() : 0) {}
    ~Sample()
    {
        if constexpr (Enabled) timing_.sample(timing_.context, stage_, timing_.now() - start_);
    }

private:
    const Timing& timing_;
    Stage stage_;
    std::uint64_t start_;
};

template <bool Timed, bool ParallelGrid = false, typename Visitor>
bool walkColumns(const View& view, Visitor visit, const Timing& timing, int begin, int end)
{
    const auto& p = view.projection;
    const Point origin{view.camera.x, view.camera.y};
    double backtrack = 0;
    {
        VORTEX_MEASURE(Rays);
        const Sample<Timed> sample(timing, Stage::Rays);
        if (view.grid) backtrack = view.grid->backtrack(origin);
    }

    for (int column = begin; column < end; ++column) {
        const auto hit = [&] {
            VORTEX_MEASURE(Rays);
            const auto direction = columnRay(p, column);
            const Sample<Timed> sample(timing, Stage::Rays);
            if (!view.grid) return castPreparedSceneRay(view.scene, direction);
            if constexpr (ParallelGrid)
                return detail::GridQuery::cast(*view.grid, view.scene, origin, direction, backtrack);
            else
                return view.grid->cast(view.scene, origin, direction, backtrack);
        }();
        if (!std::isfinite(hit.distance)) continue;
        const auto projected = [&] {
            VORTEX_MEASURE(Projection);
            return projectWall(p.height, p.focal, p.eyeHeight, view.wallHeight, hit.distance);
        }();
        if (!visit(column, hit, projected)) return false;
    }
    return true;
}

template <bool Timed>
bool renderScene(const View& view, const SceneStyle& style, std::vector<std::uint32_t>& pixels,
                 RenderBuffers& buffers, const Timing& timing, detail::WorkerPool* pool = nullptr)
{
    const auto& p = view.projection;
    if (p.width <= 0 || p.height <= 0) return false;

    const bool requiresDepth = style.layerCount != 0;
    auto& depth = buffers.depth;
    {
        VORTEX_MEASURE(Setup);
        pixels.resize(std::size_t(p.width) * p.height);
        fillBackground(pixels, p.width, p.height, style.ceilingColor, style.floorColor);
    }
    if (requiresDepth) {
        VORTEX_MEASURE(Depth);
        VORTEX_MEASURE(DepthInit);
        depth.resize(pixels.size());
        // The first row holds the nearest wall distance until all walls are drawn.
        std::fill_n(depth.begin(), p.width, std::numeric_limits<double>::infinity());
    }

    if (style.planes) {
        VORTEX_MEASURE(Setup);
        buffers.wallCoverage.assign(std::size_t(p.width), {p.height, 0});
    }

    const auto drawWall = [&](int column, const RayHit& hit, const ProjectedColumn& projected) {
        const auto material = [&] {
            VORTEX_MEASURE(Material);
            return style.wallMaterial ? style.wallMaterial(style.wallContext, hit) : WallSample{};
        }();
        const Sample<Timed> sample(timing, Stage::Walls);
        const auto rows = [&] {
            VORTEX_MEASURE(Walls);
            return rasterizeWallColumn(pixels, p.width, p.height, column, hit.distance,
                                       projected, material.texture, material.textureX, material.color);
        }();
        if (requiresDepth) {
            VORTEX_MEASURE(Depth);
            depth[column] = hit.distance;
        }
        if (style.planes) buffers.wallCoverage[column] = rows;
        return true;
    };
    if (pool && p.width > 64) {
        detail::forRanges(pool, p.width, 64, [&](std::size_t begin, std::size_t end) {
            walkColumns<Timed, true>(view, drawWall, timing, int(begin), int(end));
        });
    } else {
        walkColumns<Timed>(view, drawWall, timing, 0, p.width);
    }

    if (requiresDepth) {
        VORTEX_MEASURE(Depth);
        VORTEX_MEASURE(DepthExpand);
        const Sample<Timed> sample(timing, Stage::Walls);
        const auto rowsPerTask = std::max<std::size_t>(4, 32768 / p.width);
        detail::forRanges(pool, p.height - 1, rowsPerTask, [&](std::size_t begin, std::size_t end) {
            for (auto row = begin; row < end; ++row)
                std::copy_n(depth.data(), p.width, depth.data() + (row + 1) * p.width);
        });
    }

    if (style.planes) {
        VORTEX_MEASURE(Planes);
        const Sample<Timed> sample(timing, Stage::Planes);
        planes::renderPass(pixels, view.camera,
                           {p.width, p.height, view.planeAndSpriteFocal, p.eyeHeight, view.wallHeight},
                           *style.planes, style.planeRepeat, &buffers.wallCoverage, pool);
    }

    const Sample<Timed> sample(timing, Stage::Sprites);
    if (requiresDepth) {
        {
            VORTEX_MEASURE(Depth);
            VORTEX_MEASURE(DepthColumns);
            buffers.wallDepthColumns.assign(depth.data(), depth.data() + p.width);
        }
        VORTEX_MEASURE(Sprites);
        const sprites::Projection projection{p.width, p.height, view.planeAndSpriteFocal,
                                             p.eyeHeight, buffers.wallDepthColumns.data()};
        for (std::size_t i = 0; i < style.layerCount; ++i) {
            const auto& layer = style.layers[i];
            if (!layer.bank || !layer.objects) continue;
            if (style.observeLayer) style.observeLayer(style.layerContext, i, false, pixels);
            sprites::renderPass(pixels, depth, *layer.bank, *layer.objects, view.camera, projection, pool);
            if (style.observeLayer) style.observeLayer(style.layerContext, i, true, pixels);
        }
    }

    VORTEX_MEASURE(Fisheye);
    return !style.fisheye || detail::FisheyePass::apply(*style.fisheye, pixels, p.width, p.height, pool);
}

} // namespace

bool visitWallColumns(const View& view, ColumnVisitor visitor, void* context, Timing timing)
{
    const auto visit = [&](int column, const RayHit& hit, const ProjectedColumn& projected) {
        return visitor(context, column, hit, projected);
    };
    if (timing.now && timing.sample) return walkColumns<true>(view, visit, timing, 0, view.projection.width);
    return walkColumns<false>(view, visit, timing, 0, view.projection.width);
}

void fillBackground(std::vector<std::uint32_t>& pixels, int width, int height,
                    std::uint32_t ceiling, std::uint32_t floor)
{
    const auto upperPixels = std::size_t(height / 2 + height % 2) * width;
#ifdef __APPLE__
    // Preserve the bulk fill used by SDL on Darwin, including in Debug builds.
    if (pixels.empty()) return;
    memset_pattern4(pixels.data(), &ceiling, upperPixels * sizeof(ceiling));
    memset_pattern4(pixels.data() + upperPixels, &floor,
                    (pixels.size() - upperPixels) * sizeof(floor));
#else
    std::fill_n(pixels.begin(), upperPixels, ceiling);
    std::fill(pixels.begin() + upperPixels, pixels.end(), floor);
#endif
}

bool render(const View& view, const SceneStyle& style, std::vector<std::uint32_t>& pixels,
            RenderBuffers& buffers, Timing timing)
{
    if (timing.now && timing.sample) return renderScene<true>(view, style, pixels, buffers, timing);
    return renderScene<false>(view, style, pixels, buffers, timing);
}

RenderWorkers::RenderWorkers(unsigned threadCount)
{
    if (threadCount == 0) threadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    if (threadCount > 1) pool_ = std::make_unique<detail::WorkerPool>(threadCount);
}

RenderWorkers::~RenderWorkers() = default;

bool RenderWorkers::render(const View& view, const SceneStyle& style,
                           std::vector<std::uint32_t>& pixels, RenderBuffers& buffers)
{
#ifdef VORTEX_RENDER_PROFILE
    // The standalone profiler uses process-wide counters and allocation probes.
    return renderScene<false>(view, style, pixels, buffers, {});
#else
    return renderScene<false>(view, style, pixels, buffers, {}, pool_.get());
#endif
}

} // namespace vortex::first_person
