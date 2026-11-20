# CMake integration

Vortex uses CMake 3.21 or newer and requires a C++17-capable compiler. The installed package exports `Vortex::CPU`, `Vortex::Audio` and, with SDL3 enabled, `Vortex::AudioSDL` and `Vortex::InputSDL`.

## Build from source

With SDL3 available:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

Without SDL3:

```sh
cmake -S . -B build/cpu-only \
  -DCMAKE_BUILD_TYPE=Release \
  -DVORTEX_ENABLE_SDL3=OFF
cmake --build build/cpu-only --parallel
ctest --test-dir build/cpu-only --output-on-failure
```

The root option `VORTEX_ENABLE_SDL3` defaults to `ON`. When enabled, CMake must be able to resolve `find_package(SDL3 CONFIG REQUIRED)`. When disabled, the CPU renderer and base PCM mixer have no SDL3 dependency. Tests are added when `BUILD_TESTING` is enabled.

## Install to an isolated prefix

```sh
cmake --install build/release --prefix "$PWD/build/vortex-install"
```

A CMake installation contains:

```text
<install-prefix>/
  include/vortex/           Public headers
  lib/                      Static libraries (platform-dependent location)
  lib/cmake/Vortex/         VortexConfig.cmake, version file and exported targets
```

The precise library directory can vary by platform and CMake install configuration.

## Use the installed package

Configure the consuming project with the installation prefix in `CMAKE_PREFIX_PATH`:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/vortex-install
```

In that project's `CMakeLists.txt`:

```cmake
find_package(Vortex CONFIG REQUIRED COMPONENTS CPU Audio)

target_link_libraries(my_game PRIVATE
    Vortex::CPU
    Vortex::Audio
)
```

If SDL3 output is required:

```cmake
find_package(Vortex CONFIG REQUIRED COMPONENTS CPU Audio AudioSDL)

target_link_libraries(my_game PRIVATE
    Vortex::CPU
    Vortex::AudioSDL
)
```

For SDL3 input, request the `InputSDL` component and link `Vortex::InputSDL`. Both SDL3 targets link SDL3 transitively, and `AudioSDL` also links `Vortex::Audio`. SDL3 must be discoverable when loading a Vortex package built with SDL3 enabled. For an SDL-free installation, build Vortex with `VORTEX_ENABLE_SDL3=OFF`.

Requesting `AudioSDL` or `InputSDL` as a required component from an SDL-free installation fails during `find_package()`.

## A minimal consumer project

```text
my-game/
  CMakeLists.txt
  main.cpp
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyGame LANGUAGES CXX)

find_package(Vortex CONFIG REQUIRED COMPONENTS CPU Audio)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE Vortex::CPU Vortex::Audio)
```

`main.cpp` can include `vortex/render/first_person/scene_renderer.h` and `vortex/audio/mixer.h` without adding the Vortex source directory to its compiler's include paths. See the complete examples in [Rendering](rendering.md) and [Audio](audio.md).

## Tracking a fixed engine revision

When used across separate game repositories, prefer a dependency bound to a particular Vortex commit rather than copying its source files into each project. A pinned Git submodule or an equivalent CMake dependency workflow can meet this requirement; select the integration method once the independent Vortex repository is published. The installation examples above work without any remote repository.

## Current interface constraints

- Public APIs use C++17 and exported CMake targets, but there is no API/ABI stability guarantee yet.
- `Vortex::CPU` is a CPU framebuffer renderer; windowing and display uploads belong to the consumer.
- `Vortex::Audio` accepts already decoded PCM; it does not load proprietary game audio formats.
- `Vortex::AudioSDL` only handles SDL3 audio output; it is not required for graphics.
- `Vortex::InputSDL` provides device input for a caller-owned SDL window. Event dispatch, context changes and action bindings belong to the consumer.

The repository's `tests/vortex/CMakeLists.txt` provides examples of direct target linkage.
