# Tests and validation

The repository includes three small independent programs under [`tests/vortex/`](../tests/vortex/). These use the public Vortex headers and CMake targets; they do not depend on Extremis RE resources.

| Test | Source | Main checks |
|:--|:--|:--|
| `vortex_cpu_scene` | [`cpu_scene.cpp`](../tests/vortex/cpu_scene.cpp) | Prepared wall segments, forced spatial index in a small room, interior occlusion, floor/ceiling planes, sprite transparency, equal-depth layering, and fisheye processing |
| `vortex_audio_consumer` | [`audio_consumer.cpp`](../tests/vortex/audio_consumer.cpp) | Deterministic PCM values, simultaneous voice handling, sample loops, duplicate-play behaviour, statistics, stop and fade-to-silence |
| `vortex_audio_sdl` | [`audio_sdl.cpp`](../tests/vortex/audio_sdl.cpp) | SDL3 dummy driver, pause/resume consumption, stop release, multiple player instances, idempotent close and reopen |

The SDL3 test is registered only if `Vortex::AudioSDL` exists; the other two require only C++17 and the corresponding Vortex libraries.

## Running tests

With SDL3:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
```

Without SDL3:

```sh
cmake -S . -B build/cpu-only \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVORTEX_ENABLE_SDL3=OFF
cmake --build build/cpu-only --parallel
ctest --test-dir build/cpu-only --output-on-failure
```

After changing public headers, CMake target properties or installation rules, verify the installed package too: install into a temporary prefix, configure an independent project with `find_package(Vortex CONFIG REQUIRED ...)`, and link the relevant targets. In the extraction workflow, this was tested for all three components.

## What the tests do not claim

- The synthetic render test does not cover a full game's level geometry, original art or all edge cases.
- The mixer test proves specific PCM outputs and lifecycle behaviour, not fidelity to every source game's sound format.
- The SDL3 test uses the dummy audio driver. It does not replace listening tests on a physical device.
- Automated tests do not establish identical behaviour across platforms, compilers and graphics display backends.

For projects that require faithful reproduction of an older title, test Vortex's reusable functionality separately from the game-specific rules and resource decoders.
