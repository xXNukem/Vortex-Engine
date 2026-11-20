#include "fisheye_pass.h"

#include "../../worker_pool.h"

#include <algorithm>
#include <cmath>

namespace vortex {
FisheyeEffect::FisheyeEffect(Settings settings) noexcept
    : settings_(settings)
{
}

bool FisheyeEffect::apply(std::vector<std::uint32_t>& pixels, int width, int height)
{
    return detail::FisheyePass::apply(*this, pixels, width, height, nullptr);
}

bool detail::FisheyePass::apply(FisheyeEffect& effect, std::vector<std::uint32_t>& pixels,
                                int width, int height, WorkerPool* pool)
{
    if (width < 2 || height < 2 || pixels.size() != std::size_t(width) * height)
        return false;

    const double pixelAspect = effect.settings_.pixelAspect;
    const double strength = effect.settings_.strength;
    const bool identity = strength == 0.0;
    if (identity || effect.width_ != width || effect.height_ != height || effect.lookup_.size() != pixels.size()) {
        if (!(pixelAspect > 0.0 && strength >= 0.0 && strength <= 1.0))
            return false;
        const double cx = (width - 1) * 0.5, cy = (height - 1) * 0.5;
        const double extent2 = cx * cx * pixelAspect * pixelAspect + cy * cy;
        if (extent2 <= 0.0 || !std::isfinite(extent2)) return false;
        if (!identity) {
            effect.lookup_.resize(pixels.size());
            auto* const lookup = effect.lookup_.data();
            const auto rowsPerTask = std::max<std::size_t>(4, 16384 / width);
            detail::forRanges(pool, height, rowsPerTask, [&](std::size_t begin, std::size_t end) {
                for (int y = int(begin); y < int(end); ++y) {
                    const double dy = y - cy;
                    for (int x = 0; x < width; ++x) {
                        const double dx = x - cx;
                        const double radius2 = (dx * dx * pixelAspect * pixelAspect + dy * dy) / extent2;
                        // Radial barrel warp. At all corners scale=1; no black borders,
                        // repeated edge columns or out-of-bounds sampling.
                        const double scale = 1.0 - strength * (1.0 - std::min(radius2, 1.0));
                        const int sampleX = std::clamp(int(std::lround(cx + dx * scale)), 0, width - 1);
                        const int sampleY = std::clamp(int(std::lround(cy + dy * scale)), 0, height - 1);
                        lookup[std::size_t(y) * width + x] = std::size_t(sampleY) * width + sampleX;
                    }
                }
            });
        }
        effect.width_ = width;
        effect.height_ = height;
    }

    const auto count = pixels.size();
    if (effect.destination_.size() != count) {
        if (effect.destination_.capacity() < count)
            effect.destination_ = std::vector<std::uint32_t>(count);
        else
            effect.destination_.resize(count);
    }
    const auto* source = pixels.data();
    const auto* lookup = effect.lookup_.data();
    auto* destination = effect.destination_.data();
    if (identity) {
        // Copy into the alternate buffer so identity preserves the buffer exchange.
        std::copy_n(source, count, destination);
    } else {
        detail::forRanges(pool, count, 65536, [&](std::size_t begin, std::size_t end) {
            for (auto i = begin; i < end; ++i) destination[i] = source[lookup[i]];
        });
    }
    // Publish the warped frame and retain the old storage for the next draw.
    pixels.swap(effect.destination_);
    return true;
}
} // namespace vortex
