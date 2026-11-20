#include "vortex/render/first_person/planes/plane_renderer.h"

#include "plane_sampling.h"

#include <algorithm>
#include <cmath>

namespace vortex::planes {
namespace {
constexpr int blockWidth = 32;

struct CoverageBlock {
    int outerBegin, outerEnd;
    int innerBegin, innerEnd;

    enum class Visibility { Visible, Hidden, Mixed };

    Visibility visibility(int y) const
    {
        if (y < outerBegin || y >= outerEnd) return Visibility::Visible;
        if (y >= innerBegin && y < innerEnd) return Visibility::Hidden;
        return Visibility::Mixed;
    }
};

template <bool CheckCoverage>
void renderSpan(std::uint32_t* pixels, int begin, int end, int y, const Row& row,
                const Texture& texture, const plane_material::Sampling& sampling,
                const std::pair<int, int>* coverage, bool fixedPoint, unsigned guard,
                std::uint64_t u, std::uint64_t v, std::uint64_t du, std::uint64_t dv)
{
    // Unsigned arithmetic reproduces one accumulator addition for every skipped column.
    u += std::uint64_t(begin) * du;
    v += std::uint64_t(begin) * dv;
    const auto* source = texture.pixels.data();
    for (int x = begin; x < end; ++x, u += du, v += dv) {
        if constexpr (CheckCoverage) {
            if (y >= coverage[x].first && y < coverage[x].second) continue;
        }
        plane_material::Texel uv;
        if (fixedPoint && std::uint32_t(u + guard) > 2 * guard &&
                          std::uint32_t(v + guard) > 2 * guard) {
            uv = {int((u >> 32) & 127), int((v >> 32) & 127)};
        } else {
            uv = plane_material::coordinates(row.x + x * row.stepX, row.y + x * row.stepY,
                                             texture.width, texture.height, sampling);
        }
        const auto rgb = source[uv.y * std::size_t(texture.width) + uv.x];
        pixels[x] = (std::uint32_t(rgb.r) << 24) | (std::uint32_t(rgb.g) << 16) |
                    (std::uint32_t(rgb.b) << 8) | 255;
    }
}

void renderRow(std::uint32_t* pixels, int width, int y, const Row& row,
               const Texture& texture, const plane_material::Sampling& sampling,
               const std::pair<int, int>* coverage, const std::vector<CoverageBlock>& blocks)
{
    // A 32.32 texel accumulator leaves
    // less than one low bit of truncation error per column (including origin).
    // Keep both axes inside the material sampler's normal coordinate range.
    const bool fixedPoint = texture.width == 128 && texture.height == 128 && width <= 8192 &&
        std::abs(row.x) + width * std::abs(row.stepX) < sampling.fastPathLimit &&
        std::abs(row.y) + width * std::abs(row.stepY) < sampling.fastPathLimit;
    const double scale = sampling.texelsPerUnit * 4294967296.0;
    const auto fixed = [scale](double v) { return std::uint64_t(std::int64_t(v * scale)); };
    std::uint64_t u = 0, v = 0, du = 0, dv = 0;
    if (fixedPoint) {
        u = fixed(row.x);
        v = fixed(row.y);
        du = fixed(row.stepX);
        dv = fixed(row.stepY);
        if (row.stepX == 0 || row.stepY == 0) {
            // Sample constant axes once, including exact seams.
            const auto uv = plane_material::coordinates(row.x, row.y, 128, 128, sampling);
            if (row.stepX == 0) u = (std::uint64_t(uv.x) << 32) + 0x80000000u;
            if (row.stepY == 0) v = (std::uint64_t(uv.y) << 32) + 0x80000000u;
        }
    }
    // In low-word units, width + 1 covers accumulated truncation.
    // Another 16 covers double rounding and the sampler's 1e-9 texel guard.
    const unsigned guard = unsigned(width) + 16;
    if (blocks.empty()) {
        renderSpan<false>(pixels, 0, width, y, row, texture, sampling, coverage,
                          fixedPoint, guard, u, v, du, dv);
        return;
    }
    int begin = 0;
    for (std::size_t i = 0; i < blocks.size();) {
        using Visibility = CoverageBlock::Visibility;
        const auto visibility = blocks[i].visibility(y);
        do {
            ++i;
        } while (i < blocks.size() && blocks[i].visibility(y) == visibility);
        const int end = i == blocks.size() ? width : int(i) * blockWidth;
        if (visibility == Visibility::Visible) {
            renderSpan<false>(pixels, begin, end, y, row, texture, sampling, coverage,
                              fixedPoint, guard, u, v, du, dv);
        } else if (visibility == Visibility::Mixed) {
            renderSpan<true>(pixels, begin, end, y, row, texture, sampling, coverage,
                             fixedPoint, guard, u, v, du, dv);
        }
        begin = end;
    }
}
} // namespace

std::optional<Row> projectRow(const CameraPose& camera, const Projection& p, int y)
{
    if (p.width <= 0 || p.height <= 0 || !std::isfinite(p.focal) ||
        p.focal <= 0 || y < 0 || y >= p.height) return std::nullopt;
    const double offset = y + 0.5 - p.height * 0.5;
    if (std::abs(offset) < 1e-9) return std::nullopt; // Pixel center exactly on the horizon.
    const bool ceiling = offset < 0;
    const double elevation = ceiling ? p.ceilingHeight - p.eyeHeight : p.eyeHeight;
    if (elevation <= 0) return std::nullopt;
    const double depth = elevation * p.focal / std::abs(offset);
    if (!std::isfinite(depth)) return std::nullopt;
    const double side = (0.5 - p.width * 0.5) / p.focal;
    const double forwardX = std::cos(camera.angle), forwardY = std::sin(camera.angle);
    const double step = depth / p.focal;
    return Row{camera.x + depth * (forwardX + forwardY * side),
               camera.y + depth * (forwardY - forwardX * side),
               forwardY * step, -forwardX * step, depth, ceiling};
}

void render(std::vector<std::uint32_t>& pixels, const CameraPose& camera,
            const Projection& p, const PlaneBank& materials, double repeatPeriod,
            const std::vector<std::pair<int,int>>* wallCoverage)
{
    if (p.width <= 0 || p.height <= 0 || pixels.size() != std::size_t(p.width) * p.height) return;
    if (wallCoverage && wallCoverage->size() != std::size_t(p.width)) return;
    if (!(repeatPeriod > 0.0) || !std::isfinite(repeatPeriod)) return;
    const auto sampling = plane_material::makeSampling(repeatPeriod);
    std::vector<CoverageBlock> blocks;
    if (wallCoverage) {
        blocks.reserve(1 + (p.width - 1) / blockWidth);
        for (int begin = 0; begin < p.width;) {
            const int end = begin + std::min(blockWidth, p.width - begin);
            // The envelope excludes empty intervals. The intersection includes
            // them so one uncovered column prevents rejecting the whole block.
            CoverageBlock block{p.height, 0, 0, p.height};
            for (int x = begin; x < end; ++x) {
                const auto [top, bottom] = (*wallCoverage)[x];
                block.innerBegin = std::max(block.innerBegin, top);
                block.innerEnd = std::min(block.innerEnd, bottom);
                if (top < bottom) {
                    block.outerBegin = std::min(block.outerBegin, top);
                    block.outerEnd = std::max(block.outerEnd, bottom);
                }
            }
            blocks.push_back(block);
            begin = end;
        }
    }
    const auto* coverage = wallCoverage ? wallCoverage->data() : nullptr;
    for (int y = 0; y < p.height; ++y) {
        const auto row = projectRow(camera, p, y);
        if (!row) continue;
        const auto& texture = plane_material::select(materials, row->ceiling);
        if (validTexture(texture)) renderRow(pixels.data() + std::size_t(y) * p.width,
                                       p.width, y, *row, texture, sampling, coverage, blocks);
    }
}
} // namespace vortex::planes
