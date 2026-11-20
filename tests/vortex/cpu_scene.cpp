#include "vortex/render/first_person/scene_renderer.h"
#include "vortex/render/first_person/planes/plane_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
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
}
}

int main()
{
    try {
        renderScene();
        renderPlaneCoverage();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Vortex CPU rendered a synthetic scene\n";
}
