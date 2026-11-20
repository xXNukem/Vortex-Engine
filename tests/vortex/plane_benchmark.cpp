// Standalone Release benchmark. Build vortex_cpu with CMake in Release, then link:
// c++ -std=c++17 -O3 -DNDEBUG -Iinclude tests/vortex/plane_benchmark.cpp <libvortex_cpu.a> -o plane_benchmark
// Run as: ./plane_benchmark output-prefix > timings.csv
// Compare the generated output-prefix.rgba files byte for byte.
//
// Scenes: 0 open, 1 partial, 2 nearly hidden, 3 fully hidden, 4 fragmented,
// 5 scalar textures with partial coverage, 6 open with a null coverage pointer.
#include "vortex/render/first_person/planes/plane_renderer.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

using namespace vortex;

Texture pattern(int width, int height, int seed)
{
    Texture texture{width, height, std::vector<Rgb>(std::size_t(width) * height)};
    for (std::size_t i = 0; i < texture.pixels.size(); ++i) {
        texture.pixels[i] = {std::uint8_t(i + seed), std::uint8_t(i / width + seed),
                             std::uint8_t(i * 37 + i / width * 13 + seed)};
    }
    return texture;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    const std::string prefix = argv[1];
    const PlaneBank fixed{pattern(128, 128, 21), pattern(128, 128, 107)};
    const PlaneBank scalar{pattern(37, 65, 43), pattern(8, 9, 209)};
    std::ofstream frames(prefix + ".rgba", std::ios::binary);
    std::cout << "width,height,scene,sample,us\n";
    for (const auto [width, height] : {std::pair{320, 200}, {641, 481}, {1280, 720}}) {
        const planes::Projection projection{width, height, width * 0.63, 37.0, 98.0};
        std::vector<std::uint32_t> pixels(std::size_t(width) * height);
        std::vector<std::pair<int,int>> coverage(width);
        for (int scene = 0; scene < 7; ++scene) {
            for (int x = 0; x < width; ++x) {
                const int shift = (x / 11) % 7;
                coverage[x] = {height, 0};
                if (scene == 1 && x > width / 5 && x < 4 * width / 5)
                    coverage[x] = {height / 4 + shift, 3 * height / 4 - shift};
                if (scene == 2)
                    coverage[x] = {shift, height - shift};
                if (scene == 3)
                    coverage[x] = {0, height};
                if (scene == 4 && x % 7 < 5)
                    coverage[x] = {height / 6 + shift, height - shift};
                if (scene == 5 && x % 113 < 97)
                    coverage[x] = {height / 7 + shift, height - shift};
            }
            const auto* mask = scene == 6 ? nullptr : &coverage;
            const auto render = [&] {
                planes::render(pixels, {-641.0, -128.125, scene == 2 ? 0.0 : 0.517},
                               projection, scene == 5 ? scalar : fixed,
                               scene == 5 ? 19.25 : 640.0, mask);
            };
            std::fill(pixels.begin(), pixels.end(), 0x01234567u);
            render();
            frames.write(reinterpret_cast<const char*>(pixels.data()),
                         pixels.size() * sizeof(pixels.front()));
            for (int sample = -5; sample < 41; ++sample) {
                const int repetitions = 16;
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < repetitions; ++i) render();
                const auto end = std::chrono::steady_clock::now();
                if (sample >= 0)
                    std::cout << width << ',' << height << ',' << scene << ',' << sample << ','
                              << std::chrono::duration<double, std::micro>(end - start).count()
                                 / repetitions << '\n';
            }
        }
    }
    return !frames;
}
