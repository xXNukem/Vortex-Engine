# Audio

Vortex provides a platform-independent mixer and an optional SDL3 output layer. Both use the sample and playback types declared in [`vortex/audio/mixer.h`](../include/vortex/audio/mixer.h). The SDL backend is declared in [`vortex/audio/sdl_player.h`](../include/vortex/audio/sdl_player.h).

## Audio data model

`audio::Sample` contains:

| Member | Meaning |
|:--|:--|
| `std::vector<float> pcm` | Decoded **mono floating-point PCM** at the source rate shared by the sample bank |
| `float gain` | Default gain for the sample (default `1.0f`) |
| `std::optional<Loop> loop` | Optional sample-local loop boundaries |

The mixer does not accept compressed files or multiple source rates in one bank. Decode and, where necessary, convert audio before passing it to Vortex. For SDL output, `Player::open(rate, samples, error)` receives that common source rate.

`audio::Loop` holds three frame offsets:

- `firstEnd`: exclusive end of the initial pass, which starts at frame zero;
- `begin`: start of subsequent looping passes;
- `end`: exclusive end of subsequent looping passes.

For example, `Loop{1, 3, 2}` describes an initial pass through frames `0` and `1`, followed by looping passes through frames `1` and `2`. These are frame indices, not milliseconds or byte positions. Applications must supply meaningful boundaries for their sample data.

`audio::MixerConfig` declares the voice count, ramp duration in frames (`rampFrames`, with zero disabling the fade) and a `masterVolume` (default `1.0f`). A mixer owns its sample bank and voice state.

## Standalone mixer example

This code uses the same constructor, sample setup, control and render methods as [`tests/vortex/audio_consumer.cpp`](../tests/vortex/audio_consumer.cpp):

```cpp
#include "vortex/audio/mixer.h"

#include <array>
#include <optional>

int main()
{
    using namespace vortex::audio;

    Mixer mixer({2, 0, 0.5f}); // Two voices; no ramp; half master volume.
    mixer.setSamples({
        {{1.0f, 0.5f, -1.0f}, 0.5f, std::nullopt},
        {{0.0f, 0.5f, 1.0f}, 1.0f, Loop{1, 3, 2}}
    });

    if (!mixer.play(0) || !mixer.play(1)) return 1;

    std::array<float, 6> output{};
    mixer.render(output.data(), output.size());

    const auto first = mixer.statistics(0);
    if (!first || first->completed != 1) return 2;

    mixer.stop(1);
    mixer.render(output.data(), 1);
    return 0;
}
```

`Mixer::render(float* output, std::size_t frames)` writes mono samples to caller-owned storage. The test compares the six output frames against an exact reference sequence and checks playback statistics. The base mixer does not open an audio device or impose a callback model.

### Playback options and controls

| API | Description |
|:--|:--|
| `play(id, options)` | Request playback of a sample ID; returns a success flag |
| `stop(id)` | Stop a playing sample ID; ramp handling follows mixer configuration |
| `clear()` | Clear active playback |
| `setVolume(volume)` | Change master volume |
| `sampleCount()`, `active()`, `playing(id)` | Inspect loaded samples and active playback |
| `statistics(id)` | Optional per-sample `PlaybackStats` |
| `setSamples(samples)` | Replace sample bank, clear voices and reset statistics |

`PlayOptions` controls playback of one request:

- `repeat`: repeat the whole sample when no sample loop is used;
- `useSampleLoop`: use the `Sample::loop` metadata when available (default `true`);
- `restartExisting`: request a restart if that sample is already playing (default `false`);
- `gain`: override the sample's default gain for this voice.

By default, `play()` does not start a second instance of an already active sample ID; the independent mixer test explicitly checks that the duplicate request fails without `restartExisting`. A caller should check `play()`'s return value instead of assuming an unlimited number of voices.

`PlaybackStats` reports `decodedFrames`, `requests`, `started`, `sourceFrames`, `wraps`, `completed` and `stopped`. The mixer test uses these to verify completed one-shots and loop progress. The statistics are measurements of mixer activity, not a wall-clock time source.

A nonzero `rampFrames` enables gradual envelopes. The consumer test also verifies that stopping a looping sample with a five-frame ramp eventually produces silence.

## SDL3 playback

`Vortex::AudioSDL` provides `audio::Player`, which owns a mixer and an SDL3 audio stream. It is available only when the project is configured with SDL3 support.

```cpp
#include "vortex/audio/sdl_player.h"

#include <string>
#include <vector>

int main()
{
    using namespace vortex::audio;

    Player player({2, 8});
    const std::vector<Sample> samples{
        {std::vector<float>(64, 0.25f), 1.0f, Loop{2, 62, 64}}
    };

    std::string error;
    if (!player.open(16000, samples, error)) return 1;

    player.play(0);
    player.pause(true);
    player.pause(false);
    player.stop(0);
    player.close();
    return 0;
}
```

`open()` takes the PCM source rate in Hz, copies/receives the sample bank and writes a failure message to `error` when it cannot open the output. `close()` releases output resources and resets playback state; calling it repeatedly is exercised by the backend test.

`Player` exposes `play`, `stop`, `setVolume`, `pause`, `sampleCount`, `activeVoices`, `playing` and `statistics`. While paused, the producer does not consume PCM; the SDL3 dummy-driver test verifies that progress resumes after unpausing. Its test also creates two independent `Player` instances and checks reopening after close.

**Threading contract:** Serialize lifecycle and control calls from your application. The public header states that SDL synchronizes the producer callback with control calls while the device is open. It does not promise that independent concurrent control calls are safe.

## Responsibilities outside Vortex

A DOS sample bank or another proprietary file format needs an application-side decoder. Game rules also decide which cue to play, whether it loops and what volume to request. In Extremis RE these rules and file decoders remain part of the game; they are not part of Vortex's public audio API.

**Source of truth:** [`tests/vortex/audio_consumer.cpp`](../tests/vortex/audio_consumer.cpp) and [`tests/vortex/audio_sdl.cpp`](../tests/vortex/audio_sdl.cpp).
