#include "vortex/render/first_person/render_workers.h"
#include "vortex/render/first_person/planes/plane_renderer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr int width = 33, height = 25;
constexpr double focal = 20, eyeHeight = 12, ceilingHeight = 24;

struct Segment {
    vortex::Point start, end;
    std::uint16_t material;
};

std::size_t pixel(int x, int y) { return std::size_t(y) * width + x; }

void expect(bool condition, const char* failure)
{
    if (!condition) throw std::runtime_error(failure);
}

int referenceTexel(double world, int size, double period)
{
    const double seam = std::min(1e-9, period * (1e-9 / 256.0));
    double wrapped = std::fmod(world, period);
    if (wrapped < 0) {
        wrapped += period;
        if (wrapped == period) return 0;
    }
    if (wrapped < seam || wrapped > period - seam) wrapped = 0;
    return std::clamp(int(wrapped / period * size), 0, size - 1);
}

void samplePlaneTileBoundaries()
{
    const auto inf = std::numeric_limits<double>::infinity();
    const auto check = [](double world, double period) {
        const auto uv = vortex::plane_material::coordinates(world, -world, 37, 8191, period);
        expect(uv.x == referenceTexel(world, 37, period) &&
               uv.y == referenceTexel(-world, 8191, period),
               "Plane sampling changed at a tile or texel boundary");
    };

    for (double period : {std::numeric_limits<double>::denorm_min() * 5,
                          0x1.0000000000001p-980, 0.1, 19.25, 193.5, 256.0,
                          640.0, 0x1.fffffffffffp980}) {
        for (double tiles : {0.0, 1.0, 2.0, 19.0, 8191.0, 0x1p24 - 1, 0x1p24,
                             0x1p24 + 1, 0x1p40}) {
            for (double fraction : {0.0, 1.0 / 37, 3.0 / 8191, 0.5, 36.0 / 37}) {
                const double world = period * (tiles + fraction);
                for (double value : {std::nextafter(world, -inf), world,
                                     std::nextafter(world, inf)})
                    if (std::isfinite(value)) check(value, period);
            }
        }
        check(std::numeric_limits<double>::max(), period);
    }

    std::mt19937_64 random(0x5eab2);
    for (int i = 0; i < 20000; ++i) {
        const auto mantissa = [&] { return 1 + double(random() >> 12) * 0x1p-52; };
        const int exponent = int(random() % 2001) - 1000;
        const int relativeExponent = int(random() % 81) - 32;
        const double period = std::ldexp(mantissa(), exponent);
        const double world = std::ldexp(mantissa(), exponent + relativeExponent);
        if (std::isfinite(world)) check(world, period);
    }
}

void renderPlaneCoverage()
{
    const auto texture = [](int w, int h, int seed) {
        vortex::Texture result{w, h, std::vector<vortex::Rgb>(std::size_t(w) * h)};
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x)
                result.pixels[std::size_t(y) * w + x] = {
                    std::uint8_t(x), std::uint8_t(y), std::uint8_t(seed)};
        }
        return result;
    };
    const vortex::PlaneBank fixed{texture(128, 128, 1), texture(128, 128, 2)};
    const vortex::PlaneBank scalar{texture(7, 13, 3), texture(19, 5, 4)};
    constexpr std::uint32_t wall = 0x112233ff;
    for (const auto [w, h] : {std::pair{96, 40}, {97, 51}}) {
        const vortex::planes::Projection projection{w, h, w * 0.625, 12, 28};
        std::vector<std::pair<int, int>> coverage(w);
        for (int x = 0; x < w; ++x) {
            const int edge = x % 5;
            coverage[x] = {h, 0}; // No hit.
            if (x < 32) coverage[x] = {-edge, h + edge};
            if (x >= 32 && x < 64 && x % 3 != 0) coverage[x] = {6 + edge, h - edge - 1};
            if (x == w - 1) coverage[x] = {h / 2 - 1, h / 2 + 2};
        }
        for (double period : {640.0, 256.0, 19.25}) {
            const auto& materials = period < 256 ? scalar : fixed;
            for (double angle : {0.0, 0.517}) {
                const vortex::CameraPose camera{-period, -2 * period - 0.125, angle};
                std::vector<std::uint32_t> expected(std::size_t(w) * h, wall);
                auto actual = expected;
                vortex::planes::render(expected, camera, projection, materials, period);
                for (int x = 0; x < w; ++x) {
                    for (int y = 0; y < h; ++y) {
                        if (y >= coverage[x].first && y < coverage[x].second)
                            expected[std::size_t(y) * w + x] = wall;
                    }
                }
                vortex::planes::render(actual, camera, projection, materials, period, &coverage);
                expect(actual == expected, "Wall coverage changed plane pixels or overwrote a wall");
            }
        }
    }
}

void rasterizeTallWalls()
{
    constexpr int w = 3, h = 841;
    constexpr std::uint32_t background = 0x123456ff;
    for (int textureHeight : {0, 1, 57, 74, 113, 128, 193}) {
        vortex::Texture texture{3, textureHeight, std::vector<vortex::Rgb>(3 * textureHeight)};
        for (int y = 0; y < textureHeight; ++y)
            texture.pixels[std::size_t(y) * texture.width + 1] = {
                std::uint8_t(y * 29), std::uint8_t(y * 13), std::uint8_t(y)};
        for (const auto [top, bottom] : {std::pair{-98.5, 607.5}, {101.5, 1021.5},
                                        {-3000.5, 5000.5}, {15.5, 207.5}}) {
            const vortex::ProjectedColumn projected{0, 0, 37, top, bottom};
            const std::pair<int, int> expected{
                std::clamp(int(top - 0.5), 0, h), std::clamp(int(bottom - 0.5), 0, h)};
            const double shade = vortex::distanceShade(projected.depth);
            std::vector<std::uint32_t> pixels(w * h, background);
            std::vector<double> distances(w * h, 91);
            for (int column : {0, 1}) {
                expect(vortex::rasterizeWallColumn(pixels, w, h, column, 42, projected,
                           textureHeight ? &texture : nullptr, 1, {140, 21, 43}, &distances) == expected,
                       "Tall wall clipping changed");
            }
            for (int y = 0; y < h; ++y) {
                const auto offset = std::size_t(y) * w;
                expect(pixels[offset] == pixels[offset + 1] && pixels[offset + 2] == background,
                       "Tall wall traversal changed a pixel");
                auto reference = background;
                if (y >= expected.first && y < expected.second) {
                    vortex::Rgb color{140, 21, 43};
                    if (textureHeight) {
                        const double v = (y + 0.5 - top) / (bottom - top);
                        const int row = std::clamp(int(v * textureHeight), 0, textureHeight - 1);
                        color = texture.pixels[std::size_t(row) * texture.width + 1];
                    }
                    reference = vortex::rgbaPixel(color, shade);
                }
                expect(pixels[offset] == reference, "Wall texel differs from per-pixel sampling");
                expect(distances[offset] == 42 && distances[offset + 1] == 42 &&
                       distances[offset + 2] == 91, "Tall wall depth changed");
            }
        }
    }
}

void applyFisheye()
{
    vortex::FisheyeEffect identity{{1.2, -0.0}};
    vortex::FisheyeEffect warp{{0.8, 1.0}};
    std::vector<std::uint32_t> pixels;
    for (const auto [w, h] : {std::pair{11, 7}, {7, 11}, {8, 6}, {11, 7}}) {
        std::vector<std::uint32_t> input(std::size_t(w) * h);
        std::iota(input.begin(), input.end(), 0u);
        auto expected = input;
        vortex::FisheyeEffect freshWarp{{0.8, 1.0}};
        expect(freshWarp.apply(expected, w, h) && expected != input, "Fisheye fixture must warp");
        for (auto* effect : {&identity, &warp, &identity, &warp}) {
            pixels = input;
            expect(effect->apply(pixels, w, h) && pixels == (effect == &identity ? input : expected),
                   "Fisheye changed output when switching effects or dimensions");
        }
    }

    const auto original = pixels;
    pixels.reserve(pixels.size() + 100);
    const auto inputCapacity = pixels.capacity();
    const auto* storage = pixels.data();
    expect(identity.apply(pixels, 11, 7) && pixels == original && pixels.data() != storage,
           "Identity must publish the alternate buffer");
    expect(identity.apply(pixels, 11, 7) && pixels == original &&
           pixels.data() == storage && pixels.capacity() == inputCapacity,
           "Identity must return the original storage on the next frame");
    expect(!identity.apply(pixels, 1, 77) && !identity.apply(pixels, 11, 8) &&
           pixels == original && pixels.data() == storage,
           "Invalid dimensions must preserve the framebuffer and its storage");
    for (double aspect : {0.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::max()}) {
        vortex::FisheyeEffect invalid{{aspect, 0.0}};
        expect(!invalid.apply(pixels, 11, 7) && pixels == original && pixels.data() == storage,
               "Identity must still validate the pixel aspect and the extent");
    }

    // On a 5x3 image, strength 0.625 maps (1,1) and (3,1) to x=1.5 and x=2.5.
    for (const auto [strength, left, right] : {
             std::array<double, 3>{0.625 - 1e-12, 6, 8},
             std::array<double, 3>{0.625, 7, 8},
             std::array<double, 3>{0.625 + 1e-12, 7, 7}}) {
        std::vector<std::uint32_t> samples(15);
        std::iota(samples.begin(), samples.end(), 0u);
        vortex::FisheyeEffect rounding{{1.0, strength}};
        expect(rounding.apply(samples, 5, 3) && samples[6] == left && samples[8] == right,
               "Fisheye changed sampling at a rounding boundary");
        expect(samples[0] == 0 && samples[4] == 4 && samples[10] == 10 && samples[14] == 14 &&
               samples[7] == 7, "Fisheye moved a corner or the center pixel");
    }
}

void renderScene()
{
    const vortex::CameraPose camera{};
    const vortex::Point origin{camera.x, camera.y};
    const std::array<Segment, 7> segments{{
        {{-96, -96}, {96, -96}, 2},
        {{96, -96}, {96, 96}, 2},
        {{96, 96}, {-96, 96}, 2},
        {{-96, 96}, {-96, -96}, 2},
        {{48, -34}, {48, 34}, 1},
        {{48, -34}, {60, -52}, 1},
        {{60, -52}, {76, -52}, 1},
    }};
    vortex::StaticRayGrid grid([&](auto addSegment) {
        for (const auto& segment : segments)
            addSegment(segment.start, segment.end);
    }, true); // Force indexing in this deliberately small scene.
    expect(grid.valid(), "Synthetic geometry must build an index");

    vortex::PreparedRayScene scene;
    for (const auto& segment : segments)
        scene.walls.push_back(vortex::prepareRaySurface(
            segment.start, segment.end, origin, segment.material, 1.0));

    namespace cpu = vortex::first_person;
    const cpu::View view{camera, {width, height, 1, 0, width / (2 * focal), focal, eyeHeight},
                         ceilingHeight, focal, scene, &grid};
    const vortex::Texture wall{2, 2, std::vector<vortex::Rgb>(4, {240, 20, 10})};
    std::vector<std::uint32_t> color;
    cpu::RenderBuffers buffers;
    const vortex::PlaneBank planes{
        {128, 128, std::vector<vortex::Rgb>(128 * 128, {32, 144, 32})},
        {128, 128, std::vector<vortex::Rgb>(128 * 128, {16, 32, 144})},
    };
    constexpr std::uint32_t ceilingColor = 0x102090ff;
    constexpr std::uint32_t floorColor = 0x209020ff;
    constexpr std::uint32_t wallColor = 0xe31209ff;
    const vortex::sprites::Bank sprites{{
        {{2, 2, std::vector<vortex::Rgb>(4, {255, 224, 0})}, 1, 1, {0, 1, 1, 1}},
    }};
    const std::vector<vortex::sprites::Instance> instances{
        {24, 0, 12, 12, 0},
        {72, 18, 12, 12, 0},
    };
    auto overlappingSprites = sprites;
    std::fill(overlappingSprites.images.front().texture.pixels.begin(),
              overlappingSprites.images.front().texture.pixels.end(), vortex::Rgb{0, 200, 255});
    const std::vector<vortex::sprites::Instance> overlapping{instances.front()};
    const cpu::SpriteLayer layers[] = {{&sprites, &instances}, {&overlappingSprites, &overlapping}};
    cpu::SceneStyle style;
    style.wallMaterial = [](const void* context, const vortex::RayHit& hit) {
        const auto* texture = static_cast<const vortex::Texture*>(context);
        const int x = std::min(texture->width - 1, int(hit.segmentPosition * texture->width));
        return cpu::WallSample{texture, x, {}};
    };
    style.wallContext = &wall;
    style.planes = &planes;
    style.planeRepeat = 640.0;
    style.layers = layers;
    style.layerCount = std::size(layers);
    expect(cpu::render(view, style, color, buffers), "First-person scene did not render");
    const auto& depth = buffers.depth;
    expect(buffers.wallDepthColumns.front() == 96 && buffers.wallDepthColumns[width / 2] == 48,
           "Room perimeter or interior structure did not occlude at the expected depth");
    expect(buffers.wallCoverage[width / 2] == std::pair{7, 17}, "Wall coverage changed");
    expect(color[pixel(0, 0)] == ceilingColor, "Ceiling not rasterized");
    expect(color[pixel(0, height - 1)] == floorColor, "Floor not rasterized");
    expect(color[pixel(10, 12)] == wallColor, "Covered wall pixels were overwritten");

    expect(color[pixel(16, 12)] == 0xffe000ff && depth[pixel(16, 12)] == 24,
           "First sprite layer did not retain its color and depth at an equal-depth overlap");
    expect(color[pixel(13, 9)] == wallColor && depth[pixel(13, 9)] == 48,
           "Transparent sprite texel overwrote the wall");
    expect(color[pixel(10, 12)] == wallColor && depth[pixel(10, 12)] == 48,
           "Wall did not hide the distant sprite");

    const auto unwarped = color;
    vortex::FisheyeEffect fisheye{{1.0, 0.20}};
    style.fisheye = &fisheye;
    expect(cpu::render(view, style, color, buffers) && color != unwarped,
           "Fisheye did not warp the framebuffer");
    expect(color.front() == ceilingColor && color.back() == floorColor &&
           color[pixel(width / 2, height / 2)] == unwarped[pixel(width / 2, height / 2)],
           "Fisheye moved an invariant sample");

    cpu::RenderWorkers workers{4};
    cpu::RenderWorkers single{1};
    std::vector<std::uint32_t> parallelColor;
    cpu::RenderBuffers parallelBuffers;
    for (int frame = 0; frame < 4; ++frame) {
        auto larger = view;
        larger.projection.width = frame == 2 ? 777 : 641;
        larger.projection.height = 481;
        larger.projection.focal = larger.planeAndSpriteFocal = 390;
        larger.projection.plane = larger.projection.width / (2 * larger.projection.focal);
        if (frame == 1) larger.grid = nullptr;

        struct Observation {
            std::thread::id caller = std::this_thread::get_id();
            std::vector<std::vector<std::uint32_t>> layers;
        } observation;
        style.layerContext = &observation;
        style.observeLayer = [](void* context, std::size_t layer, bool after, const auto& pixels) {
            auto& state = *static_cast<Observation*>(context);
            expect(std::this_thread::get_id() == state.caller, "Observer left the calling thread");
            expect(state.layers.size() == layer * 2 + after, "Observer order changed");
            state.layers.push_back(pixels);
        };
        expect(cpu::render(larger, style, color, buffers), "Serial reference render failed");
        const auto expectedLayers = std::move(observation.layers);
        observation.layers.clear();
        auto& execution = frame == 3 ? single : workers;
        expect(execution.render(larger, style, parallelColor, parallelBuffers), "Worker render failed");
        expect(parallelColor == color && parallelBuffers.depth == buffers.depth &&
               parallelBuffers.wallCoverage == buffers.wallCoverage &&
               parallelBuffers.wallDepthColumns == buffers.wallDepthColumns &&
               observation.layers == expectedLayers, "Worker render changed frame or layer output");
    }

    style.observeLayer = nullptr;
    const auto material = style.wallMaterial;
    std::atomic<unsigned> workerCalls{0};
    const auto caller = std::this_thread::get_id();
    const auto context = std::pair{&workerCalls, caller};
    style.wallContext = &context;
    style.wallMaterial = [](const void* opaque, const vortex::RayHit&) -> cpu::WallSample {
        const auto& [calls, callingThread] = *static_cast<const decltype(context)*>(opaque);
        if (std::this_thread::get_id() != callingThread) {
            ++*calls;
            throw std::logic_error("Material callback failed");
        }
        return {};
    };
    auto wide = view;
    wide.projection.width = 641;
    bool raised = false;
    try {
        workers.render(wide, style, parallelColor, parallelBuffers);
    } catch (const std::logic_error&) {
        raised = true;
    }
    expect(raised && workerCalls > 0, "Worker exception did not reach the caller");
    style.wallMaterial = material;
    style.wallContext = &wall;
    expect(workers.render(wide, style, parallelColor, parallelBuffers), "Workers did not recover");
}

void verifySpriteWorkers(unsigned threads)
{
    namespace cpu = vortex::first_person;
    using vortex::sprites::Instance;
    using vortex::sprites::Bank;
    const vortex::PreparedRayScene scene{{vortex::prepareRaySurface({100, -2000}, {100, 2000},
                                                                   {0, 0}, 1, 1)}, {}};
    Bank bank{{
        {{2, 2, {{220, 31, 80}, {12, 150, 70}, {27, 90, 65}, {55, 27, 190}}}, 1, 1, {0, 1, 2, 1}},
        {{2, 2, std::vector<vortex::Rgb>(4, {114, 36, 180})}, 1, 1, {1, 1, 1, 1}},
    }};
    const auto original = bank;
    std::vector<Instance> objects{
        {99.5, 0, 30, 90, 0, true, false, true},
        {100, 0, 20, 95, 1},
        {97, 10, 22, 110, 1},
        {98, 10, 22, 104, 0},
        {97, 10, 22, 110, 0, true, false, false, true},
    };
    for (int i = 0; i < 270; ++i)
        objects.push_back({56.0 + (i % 20), (i % 13 - 6) * 3.5, 18.0 + i % 7,
                           35.0 + i % 16, 0, bool(i % 2), bool(i % 3)});
    // This impact probes the live depth from preceding overlapping sprites.
    objects.push_back({58, 3, 21, 35, 0, false, false, true});
    const std::vector<Instance> overlays{{56, 0, 22, 44, 0, false, false, false, true},
                                         {45, -15, 20, 30, 1}};
    const cpu::SpriteLayer layers[] = {{&bank, &objects}, {nullptr, nullptr}, {&bank, &overlays}};
    struct Observation {
        Bank* bank;
        cpu::RenderBuffers* buffers;
        std::thread::id caller;
        std::vector<std::vector<std::uint32_t>> colors;
        std::vector<std::vector<double>> depths;
    } state{&bank, nullptr, std::this_thread::get_id(), {}, {}};
    cpu::SceneStyle style;
    style.layers = layers;
    style.layerCount = std::size(layers);
    style.layerContext = &state;
    style.observeLayer = [](void* context, std::size_t layer, bool after, const auto& pixels) {
        auto& observation = *static_cast<Observation*>(context);
        expect(observation.caller == std::this_thread::get_id(), "Sprite observer left caller");
        expect(observation.colors.size() == layer + after, "Sprite layer boundary changed");
        observation.colors.push_back(pixels);
        observation.depths.push_back(observation.buffers->depth);
        // The following batch must read the newly edited indices and texture.
        auto& image = observation.bank->images[0];
        image.indices[0] ^= 1;
        image.texture.pixels[0].b ^= 0xff;
    };
    const auto sameDepth = [](const auto& left, const auto& right) {
        return left.size() == right.size() &&
               std::memcmp(left.data(), right.data(), left.size() * sizeof(double)) == 0;
    };
    cpu::RenderWorkers workers{threads};
    cpu::RenderBuffers serial, parallel;
    std::vector<std::uint32_t> reference, candidate;
    for (const auto& size : {std::pair{641, 481}, {319, 203}, {777, 483}}) {
        const auto w = size.first, h = size.second;
        const double focal = w * .65;
        const cpu::View view{{0, 0, 0}, {w, h, 1, 0, w * .5 / focal, focal, 30},
                             64, focal, scene};
        state.buffers = &serial;
        expect(cpu::render(view, style, reference, serial), "Sprite serial render failed");
        const auto layerColors = std::move(state.colors);
        const auto layerDepths = std::move(state.depths);
        state.colors.clear();
        state.depths.clear();
        bank = original;
        state.buffers = &parallel;
        expect(workers.render(view, style, candidate, parallel), "Sprite worker render failed");
        expect(reference == candidate && sameDepth(serial.depth, parallel.depth) &&
               serial.wallDepthColumns == parallel.wallDepthColumns &&
               state.colors == layerColors, "Worker sprite output changed");
        for (std::size_t i = 0; i < layerDepths.size(); ++i)
            expect(sameDepth(layerDepths[i], state.depths[i]), "Observer depth bits changed");
        state.colors.clear();
        state.depths.clear();
        bank = original;
    }
}

}

int main()
{
    try {
        renderScene();
        auto otherInstance = std::async(std::launch::async, verifySpriteWorkers, 2);
        verifySpriteWorkers(4);
        otherInstance.get();
        renderPlaneCoverage();
        samplePlaneTileBoundaries();
        applyFisheye();
        rasterizeTallWalls();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Vortex CPU rendered a synthetic scene\n";
}
