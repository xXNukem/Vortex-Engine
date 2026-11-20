#include "vortex/render/first_person/sprites/sprite_renderer.h"

#include "../../worker_pool.h"
#include "sprite_pass.h"

#include <algorithm>
#include <array>
#include <cmath>

#ifdef VORTEX_RENDER_PROFILE
#include "render_profile.h"
#define VORTEX_PROBE(phase, ...) \
    (::vortex::render_profile::probe<::vortex::render_profile::phase>([&] { return (__VA_ARGS__); }))
#define VORTEX_WORK(counter, n) (::vortex::render_profile::work[::vortex::render_profile::counter] += (n))
#else
#define VORTEX_MEASURE(phase)
#define VORTEX_PROBE(phase, ...) (__VA_ARGS__)
#define VORTEX_WORK(counter, n)
#endif

namespace vortex::sprites {
namespace {
bool valid(const Image& image)
{
    const auto& texture = image.texture;
    return validTexture(texture) && image.indices.size() == texture.pixels.size();
}

int clippedEdge(double value, int limit)
{
    return int(std::ceil(std::clamp(value - 0.5, 0.0, double(limit))));
}

struct Bounds {
    int x0, x1, y0, y1;
};

struct Occluder {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    double depth = 0;
    std::size_t area = 0;
};

struct Run { int begin, end, u; };

constexpr std::size_t spriteWorkPerTask = 32768;
constexpr std::size_t spriteRowsPerTask = 8;

std::size_t rasterGrain(std::size_t rows, std::size_t work)
{
    const auto tasks = std::max<std::size_t>(1, work / spriteWorkPerTask);
    return std::max(spriteRowsPerTask, 1 + (rows - 1) / tasks);
}

Bounds clip(const Rectangle& rect, const Projection& projection)
{
    VORTEX_MEASURE(SpriteClip);
    return {clippedEdge(rect.left, projection.width),
            clippedEdge(rect.left + rect.width, projection.width),
            clippedEdge(rect.top, projection.height),
            clippedEdge(rect.top + rect.height, projection.height)};
}

void buildRuns(std::vector<Run>& runs, const Instance& object, const Image& image,
               const Rectangle& rect, Bounds bounds, const double* limit)
{
    VORTEX_MEASURE(SpriteRuns);
    const auto width = image.texture.width;
    const auto first = runs.size();
    for (int x = bounds.x0; x < bounds.x1; ++x) {
        // Earlier sprites can only decrease depth, so the wall gate holds for every row.
        if (limit && VORTEX_PROBE(SpriteColumnGate, rect.depth >= limit[x])) {
            VORTEX_WORK(WallColumns, 1);
            continue;
        }
        int u = std::clamp(int((x + 0.5 - rect.left) * width / rect.width), 0, width - 1);
        if (object.flipX) u = width - 1 - u;
        if (runs.size() != first && runs.back().end == x && runs.back().u == u)
            runs.back().end = x + 1;
        else
            runs.push_back({x, x + 1, u});
    }
}

bool hidden(const Image& image, const Rectangle& rect, Bounds bounds, Occluder& occluder)
{
    const auto [x0, x1, y0, y1] = bounds;
    if (x0 >= occluder.x0 && x1 <= occluder.x1 &&
        y0 >= occluder.y0 && y1 <= occluder.y1 && rect.depth >= occluder.depth) {
        VORTEX_WORK(HiddenRects, 1);
        return true;
    }
    const auto area = std::size_t(x1 - x0) * (y1 - y0);
    // An opaque sprite caps live depth throughout its clipped rectangle.
    // Failed depth tests (including the wall gate) already satisfy the cap.
    if (area > image.indices.size() && area >= occluder.area &&
        (occluder.area == 0 || rect.depth <= occluder.depth) &&
        std::find(image.indices.begin(), image.indices.end(), 0) == image.indices.end())
        occluder = {x0, x1, y0, y1, rect.depth, area};
    return false;
}

template<bool noNaN, bool overlaySpriteAtEqualDepth = false>
void raster(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
            const Instance& object, const Image& image, const Rectangle& rect,
            const Projection& projection, int y0, int y1, const Run* runs, std::size_t runCount)
{
    const auto& texture = image.texture;
    const double objectDepth = rect.depth;
    const bool flipY = object.flipY;
    VORTEX_MEASURE(SpriteRaster);
    for (int y = y0; y < y1; ++y) {
        const auto row = std::size_t(y) * projection.width;
        auto* rowDepth = depth.data() + row;
        auto* rowPixels = pixels.data() + row;
        int v = std::clamp(int((y + 0.5 - rect.top) * texture.height / rect.height),
                           0, texture.height - 1);
        if (flipY) v = texture.height - 1 - v;
        const auto textureRow = std::size_t(v) * texture.width;
        for (std::size_t i = 0; i < runCount; ++i) {
            const auto& run = runs[i];
            const auto source = textureRow + run.u;
            if (VORTEX_PROBE(SpriteTexel, image.indices[source] == 0)) {
                VORTEX_WORK(TransparentPixels, run.end - run.begin);
                continue;
            }
            const auto& c = texture.pixels[source];
            const auto color = VORTEX_PROBE(SpriteTexel, packOpaqueRgb(c.r, c.g, c.b));
            int x = run.begin;
            if constexpr (noNaN && !overlaySpriteAtEqualDepth) {
                // Classify four pixels of the same opaque texel before writing.
                // Uniform blocks need one decision and mixed blocks reuse the mask.
                constexpr int blockWidth = 4;
                constexpr unsigned fullMask = (1u << blockWidth) - 1;
                for (; run.end - x >= blockWidth; x += blockWidth) {
                    const unsigned mask = VORTEX_PROBE(SpriteGate,
                        unsigned(!(objectDepth >= rowDepth[x])) |
                        (unsigned(!(objectDepth >= rowDepth[x + 1])) << 1) |
                        (unsigned(!(objectDepth >= rowDepth[x + 2])) << 2) |
                        (unsigned(!(objectDepth >= rowDepth[x + 3])) << 3));
                    VORTEX_WORK(DepthTests, blockWidth);
                    if (mask == 0) {
                        VORTEX_WORK(DepthRejects, blockWidth);
                        continue;
                    }
                    if (mask == fullMask) {
                        VORTEX_PROBE(SpriteWrite, (std::fill_n(rowPixels + x, blockWidth, color),
                                                  std::fill_n(rowDepth + x, blockWidth, objectDepth)));
                        VORTEX_WORK(Stores, blockWidth);
                        continue;
                    }
                    for (int i = 0; i < blockWidth; ++i) {
                        if ((mask & (1u << i)) == 0) {
                            VORTEX_WORK(DepthRejects, 1);
                            continue;
                        }
                        VORTEX_WORK(Stores, 1);
                        VORTEX_PROBE(SpriteWrite, (rowPixels[x + i] = color,
                                                  rowDepth[x + i] = objectDepth));
                    }
                }
            }
            for (; x < run.end; ++x) {
                VORTEX_WORK(DepthTests, 1);
                if (VORTEX_PROBE(SpriteGate, objectDepth >= rowDepth[x])) {
                    VORTEX_WORK(DepthRejects, 1);
                    if constexpr (!overlaySpriteAtEqualDepth) {
                        continue;
                    } else {
                        // Hit testing projects onto the target's billboard plane.
                        // Allow for roundoff from computing that point along the shot ray.
                        constexpr double coincidentDepthTolerance = 1e-8;
                        if (VORTEX_PROBE(SpriteGate,
                            objectDepth - rowDepth[x] > coincidentDepthTolerance ||
                            !projection.columnDepthLimit ||
                            rowDepth[x] >= projection.columnDepthLimit[x])) continue;
                        VORTEX_WORK(Stores, 1);
                        VORTEX_PROBE(SpriteWrite, rowPixels[x] = color);
                        continue;
                    }
                }
                VORTEX_WORK(Stores, 1);
                VORTEX_PROBE(SpriteWrite, rowPixels[x] = color);
                if constexpr (noNaN) {
                    VORTEX_PROBE(SpriteWrite, rowDepth[x] = objectDepth);
                } else {
                    // Unordered depth accepts color and retains its original bits.
                    if (VORTEX_PROBE(SpriteGate, objectDepth < rowDepth[x]))
                        VORTEX_PROBE(SpriteWrite, rowDepth[x] = objectDepth);
                }
            }
        }
    }
}

bool drawImpact(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                const Instance& object, const Image& image, const Rectangle& rect,
                const Projection& projection, Bounds bounds, detail::WorkerPool* pool)
{
    const auto& texture = image.texture;
    // At an oblique wall, a camera-facing impact billboard straddles the
    // wall's Z buffer. Estimate the struck wall surface from its inverse-depth
    // slope at the impact point (inverse depth is linear in screen X).
    // Only marked wall impacts use this; every other sprite is unchanged.
    bool wallSurface=false;
    int centerX=0;
    double inverseCenter=0, inverseSlope=0;
    if (rect.width>0 && rect.height>0) {
        const double scale=rect.width / texture.width;
        const int cx=int(std::floor(rect.left + image.anchorX*scale));
        const int cy=int(std::floor(rect.top + image.anchorY*scale));
        if (cx>0 && cx+1<projection.width && cy>=0 && cy<projection.height) {
            const std::size_t center=std::size_t(cy)*projection.width+cx;
            const double middle=depth[center], left=depth[center-1], right=depth[center+1];
            if (std::isfinite(middle) && std::isfinite(left) && std::isfinite(right) &&
                middle>0 && left>0 && right>0 &&
                std::abs(middle-rect.depth)<=std::max(1.0,rect.depth*.02) &&
                std::abs(left-middle)<=middle*.4 && std::abs(right-middle)<=middle*.4) {
                wallSurface=true;
                centerX=cx;
                inverseCenter=1.0/middle;
                inverseSlope=(1.0/right-1.0/left)*.5;
            }
        }
    }
    const bool flipX = object.flipX;
    const bool flipY = object.flipY;
    if (!wallSurface) return false;
    VORTEX_MEASURE(SpriteRaster);
    const auto rows = std::size_t(bounds.y1 - bounds.y0);
    const auto work = std::size_t(bounds.x1 - bounds.x0) * rows;
    detail::forRanges(pool, rows, rasterGrain(rows, work), [&](std::size_t begin, std::size_t end) {
        for (int y = bounds.y0 + int(begin); y < bounds.y0 + int(end); ++y) {
            int v = std::clamp(int((y + 0.5 - rect.top) * texture.height / rect.height),
                               0, texture.height - 1);
            if (flipY) v = texture.height - 1 - v;
            for (int x = bounds.x0; x < bounds.x1; ++x) {
                const auto target = std::size_t(y) * projection.width + x;
                const double inverse=inverseCenter+(x-centerX)*inverseSlope;
                if (inverse<=0) continue;
                const double expected=1.0/inverse;
                // Reject a foreground object or a different wall plane.
                VORTEX_WORK(DepthTests, 1);
                if (VORTEX_PROBE(SpriteGate, !std::isfinite(depth[target]) ||
                    std::abs(depth[target]-expected)>std::max(.5,expected*.015))) {
                    VORTEX_WORK(DepthRejects, 1);
                    continue;
                }
                int u = std::clamp(int((x + 0.5 - rect.left) * texture.width / rect.width),
                                   0, texture.width - 1);
                if (flipX) u = texture.width - 1 - u;
                const auto source = std::size_t(v) * texture.width + u;
                if (VORTEX_PROBE(SpriteTexel, image.indices[source] == 0)) {
                    VORTEX_WORK(TransparentPixels, 1);
                    continue;
                }
                VORTEX_WORK(Stores, 1);
                const auto& c = texture.pixels[source];
                const auto color = VORTEX_PROBE(SpriteTexel, packOpaqueRgb(c.r, c.g, c.b));
                VORTEX_PROBE(SpriteWrite, (pixels[target] = color,
                                           depth[target] = std::min(depth[target], rect.depth)));
            }
        }
    });
    return true;
}
} // namespace

std::optional<Rectangle> project(const Instance& object, const Image& image,
                               const CameraPose& camera, const Projection& p)
{
    if (!valid(image) || p.width <= 0 || p.height <= 0 || p.focal <= 0 ||
        !std::isfinite(object.height) || object.height <= 0) return std::nullopt;
    const double dx = object.x - camera.x, dy = object.y - camera.y;
    const double c = std::cos(camera.angle), s = std::sin(camera.angle);
    const double depth = dx * c + dy * s;
    if (!std::isfinite(depth) || depth < 0.01) return std::nullopt;
    const double scale = object.height * p.focal / (depth * image.texture.height);
    const double anchorX = object.flipX
        ? image.texture.width - image.anchorX : image.anchorX;
    const double anchorY = object.flipY
        ? image.texture.height - image.anchorY : image.anchorY;
    const double left = p.width * 0.5 + (dx * s - dy * c) * p.focal / depth - anchorX * scale;
    const double top = p.height * 0.5 - (object.z - p.eyeHeight) * p.focal / depth - anchorY * scale;
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(scale)) return std::nullopt;
    return Rectangle{left, top, image.texture.width * scale, image.texture.height * scale, depth};
}

namespace {
struct Draw {
    const Instance* object;
    const Image* image;
    Rectangle rect;
    int y0, y1;
    std::size_t firstRun, runCount;
};

template<bool noNaN>
void rasterDraw(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                const Projection& projection, const Draw& draw, const Run* runs, int y0, int y1)
{
    if (noNaN && draw.object->overlaySpriteAtEqualDepth)
        raster<true, true>(pixels, depth, *draw.object, *draw.image, draw.rect,
                           projection, y0, y1, runs + draw.firstRun, draw.runCount);
    else
        raster<noNaN>(pixels, depth, *draw.object, *draw.image, draw.rect,
                      projection, y0, y1, runs + draw.firstRun, draw.runCount);
}

template<bool noNaN>
void renderLayer(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                 const Bank& bank, const std::vector<Instance>& objects, const CameraPose& camera,
                 const Projection& projection, detail::WorkerPool* pool)
{
    if (projection.width <= 0 || projection.height <= 0 ||
        pixels.size() != std::size_t(projection.width) * projection.height ||
        depth.size() != pixels.size()) return;

    Occluder occluder;
    std::vector<Run> runs;
    std::array<Draw, 128> draws;
    std::size_t drawCount = 0;
    std::size_t work = 0;
    int top = projection.height, bottom = 0;
    const auto flush = [&] {
        if (!drawCount) return;
        // Bands own complete rows of both buffers. Each sees the original order.
        // Dispatch only over rows touched by this batch.
        const auto rows = std::size_t(bottom - top);
        detail::forRanges(pool, rows, rasterGrain(rows, work), [&](std::size_t begin, std::size_t end) {
            // Keep repeatedly tested color and Z rows close in cache.
            constexpr int pixelsPerTile = 65536;
            const int tileRows = drawCount > 1 ? std::max(16, pixelsPerTile / projection.width)
                                              : int(end - begin);
            const int endRow = top + int(end);
            for (int bandTop = top + int(begin); bandTop < endRow;) {
                const int bandBottom = bandTop + std::min(tileRows, endRow - bandTop);
                for (std::size_t i = 0; i < drawCount; ++i) {
                    const auto& draw = draws[i];
                    const int y0 = std::max(draw.y0, bandTop);
                    const int y1 = std::min(draw.y1, bandBottom);
                    if (y0 >= y1) continue;
                    rasterDraw<noNaN>(pixels, depth, projection, draw, runs.data(), y0, y1);
                }
                bandTop = bandBottom;
            }
        });
        runs.clear();
        drawCount = work = 0;
        top = projection.height;
        bottom = 0;
    };

    for (const auto& object : objects) {
        if (object.image >= bank.images.size()) continue;
        const auto& image = bank.images[object.image];
        const auto rectangle = [&] {
            VORTEX_MEASURE(SpriteProject);
            return project(object, image, camera, projection);
        }();
        if (!rectangle) continue;
        VORTEX_WORK(Projected, 1);
        const auto bounds = clip(*rectangle, projection);
        const auto [x0, x1, y0, y1] = bounds;
        if (x0 >= x1 || y0 >= y1) {
            VORTEX_WORK(EmptyRect, 1);
            continue;
        }
        // Every band needs the same live wall slope as seen by this impact in sprite order.
        if (object.wallImpact) {
            flush();
            if (drawImpact(pixels, depth, object, image, *rectangle, projection, bounds, pool)) continue;
        }
        if (noNaN && !object.overlaySpriteAtEqualDepth &&
            hidden(image, *rectangle, bounds, occluder)) continue;

        const auto first = runs.size();
        const auto* wallLimit = noNaN ? projection.columnDepthLimit : nullptr;
        // A run must never merge across sprite boundaries.
        const auto capacity = std::min(x1 - x0, image.texture.width);
        if (runs.capacity() < first + capacity)
            runs.reserve(std::max(first + capacity, runs.capacity() * 2));
        buildRuns(runs, object, image, *rectangle, bounds, wallLimit);
        const auto count = runs.size() - first;
        if (!count) {
            VORTEX_WORK(HiddenRects, 1);
            continue;
        }
        const Draw draw{&object, &image, *rectangle, y0, y1, first, count};
        if (!pool) {
            rasterDraw<noNaN>(pixels, depth, projection, draw, runs.data(), y0, y1);
            runs.clear();
            continue;
        }
        draws[drawCount++] = draw;
        std::size_t columns = 0;
        for (std::size_t i = first; i < runs.size(); ++i) columns += runs[i].end - runs[i].begin;
        const auto rows = std::size_t(y1 - y0);
        // A fully transparent texture only tests one index per run. This
        // estimate controls scheduling and leaves the raster and S02 unchanged.
        if (columns * rows >= spriteWorkPerTask && image.indices.size() <= 4096 &&
            std::all_of(image.indices.begin(), image.indices.end(), [](auto index) { return index == 0; }))
            columns = count;
        work += columns * rows;
        top = std::min(top, y0);
        bottom = std::max(bottom, y1);
        if (drawCount == draws.size()) flush();
    }
    flush();
}
} // namespace

void render(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
            const Bank& bank, const std::vector<Instance>& objects, const CameraPose& camera,
            const Projection& projection, DepthInput depthInput)
{
    if (depthInput == DepthInput::NoNaN)
        renderLayer<true>(pixels, depth, bank, objects, camera, projection, nullptr);
    else
        renderLayer<false>(pixels, depth, bank, objects, camera, projection, nullptr);
}

void renderPass(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                const Bank& bank, const std::vector<Instance>& objects, const CameraPose& camera,
                const Projection& projection, detail::WorkerPool* pool)
{
    renderLayer<true>(pixels, depth, bank, objects, camera, projection, pool);
}
} // namespace vortex::sprites
