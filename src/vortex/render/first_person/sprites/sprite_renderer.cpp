#include "vortex/render/first_person/sprites/sprite_renderer.h"

#include <algorithm>
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

template<bool noNaN, bool overlaySpriteAtEqualDepth = false>
void drawObject(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                const Instance& object, const Image& image, const Rectangle& rect,
                const Projection& projection)
{
    const auto& texture = image.texture;
    int x0, x1, y0, y1;
    {
        VORTEX_MEASURE(SpriteClip);
        x0 = clippedEdge(rect.left, projection.width);
        x1 = clippedEdge(rect.left + rect.width, projection.width);
        y0 = clippedEdge(rect.top, projection.height);
        y1 = clippedEdge(rect.top + rect.height, projection.height);
        if (x0 >= x1 || y0 >= y1) {
            VORTEX_WORK(EmptyRect, 1);
            return;
        }
    }
    // At an oblique wall, a camera-facing impact billboard straddles the
    // wall's Z buffer. Estimate the struck wall surface from its inverse-depth
    // slope at the impact point (inverse depth is linear in screen X).
    // Only marked wall impacts use this; every other sprite is unchanged.
    bool wallSurface=false;
    int centerX=0;
    double inverseCenter=0, inverseSlope=0;
    if (object.wallImpact && rect.width>0 && rect.height>0) {
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
    if (!wallSurface) {
        // Reuse the exact pixel-center sampling across rows. Magnified texels
        // form horizontal runs that share one transparency test and color.
        struct Run { int begin, end, u; };
        std::vector<Run> runs;
        const double objectDepth = rect.depth;
        const auto* limit = noNaN ? projection.columnDepthLimit : nullptr;
        {
            VORTEX_MEASURE(SpriteRuns);
            runs.reserve(std::min(x1 - x0, texture.width));
            for (int x = x0; x < x1; ++x) {
                // Earlier sprites can only decrease depth. A wall-hidden column
                // therefore rejects this sprite on every row.
                if (limit && VORTEX_PROBE(SpriteGate, objectDepth >= limit[x])) {
                    VORTEX_WORK(WallColumns, 1);
                    continue;
                }
                int u = std::clamp(int((x + 0.5 - rect.left) * texture.width / rect.width),
                                   0, texture.width - 1);
                if (flipX) u = texture.width - 1 - u;
                if (!runs.empty() && runs.back().end == x && runs.back().u == u)
                    runs.back().end = x + 1;
                else
                    runs.push_back({x, x + 1, u});
            }
            if (runs.empty()) {
                VORTEX_WORK(HiddenRects, 1);
                return;
            }
        }
        VORTEX_MEASURE(SpriteRaster);
        for (int y = y0; y < y1; ++y) {
            const auto row = std::size_t(y) * projection.width;
            auto* rowDepth = depth.data() + row;
            auto* rowPixels = pixels.data() + row;
            int v = std::clamp(int((y + 0.5 - rect.top) * texture.height / rect.height),
                               0, texture.height - 1);
            if (flipY) v = texture.height - 1 - v;
            const auto textureRow = std::size_t(v) * texture.width;
            for (const auto& run : runs) {
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
        return;
    }
    VORTEX_MEASURE(SpriteRaster);
    for (int y = y0; y < y1; ++y) {
        int v = std::clamp(int((y + 0.5 - rect.top) * texture.height / rect.height),
                           0, texture.height - 1);
        if (flipY) v = texture.height - 1 - v;
        for (int x = x0; x < x1; ++x) {
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

void render(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
            const Bank& bank, const std::vector<Instance>& objects, const CameraPose& camera,
            const Projection& projection, DepthInput depthInput)
{
    if (projection.width <= 0 || projection.height <= 0 ||
        pixels.size() != std::size_t(projection.width) * projection.height ||
        depth.size() != pixels.size()) return;
    const auto draw = depthInput == DepthInput::NoNaN ? drawObject<true> : drawObject<false>;
    for (const auto& object : objects) {
        if (object.image >= bank.images.size()) continue;
        const auto& image = bank.images[object.image];
        const auto rectangle = [&] {
            VORTEX_MEASURE(SpriteProject);
            return project(object, image, camera, projection);
        }();
        if (!rectangle) continue;
        VORTEX_WORK(Projected, 1);
        if (object.overlaySpriteAtEqualDepth && depthInput == DepthInput::NoNaN)
            drawObject<true, true>(pixels, depth, object, image, *rectangle, projection);
        else
            draw(pixels, depth, object, image, *rectangle, projection);
    }
}
} // namespace vortex::sprites
