# Technical documentation

Vortex Engine exposes three C++17 libraries: a software first-person renderer, a PCM mixer, and an optional SDL3 audio output layer. This documentation describes the APIs available in the current source tree. It does not define a stable compatibility contract.

| Guide | Contents |
|:--|:--|
| [Architecture and ownership](architecture.md) | Library boundaries, data flow, application responsibilities and lifetime rules |
| [Rendering](rendering.md) | Scene setup, rays, surfaces, textures, sprites, depth, planes and post-processing, with a compilable example |
| [Audio](audio.md) | PCM sample format, voices, loops, mixer control and SDL3 playback, with examples |
| [CMake integration](integration.md) | Build options, installation, imported targets and dependency handling |
| [Tests and validation](testing.md) | What the included consumers verify, how to run them, and coverage limits |

## Where to start

For a new application, build and install Vortex as described in [CMake integration](integration.md). The shortest entry point for a first-person scene is `vortex::first_person::render()` in [`scene_renderer.h`](../include/vortex/render/first_person/scene_renderer.h); for sound, use [`mixer.h`](../include/vortex/audio/mixer.h), or [`sdl_player.h`](../include/vortex/audio/sdl_player.h) when SDL3 output is wanted.

The repository's [`tests/vortex/cpu_scene.cpp`](../tests/vortex/cpu_scene.cpp), [`tests/vortex/audio_consumer.cpp`](../tests/vortex/audio_consumer.cpp) and [`tests/vortex/audio_sdl.cpp`](../tests/vortex/audio_sdl.cpp) are additional, executable usage examples.

The examples here are based on the public headers and independent consumers reviewed on 20 November 2026. Implementation-dependent behaviour not expressed by those APIs is deliberately not specified.
