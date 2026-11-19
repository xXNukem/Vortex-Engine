# Architecture and ownership

Vortex separates reusable rendering and audio operations from game rules and resource management. Its present scope is narrower than a complete engine runtime: it provides libraries, not an executable application or an entity framework.

## Libraries

| Target | Main entry points | Requires SDL3? |
|:--|:--|:--|
| `Vortex::CPU` | `vortex::first_person::render`, `visitWallColumns`; lower-level raycasting, plane, wall and sprite APIs | No |
| `Vortex::Audio` | `vortex::audio::Mixer` | No |
| `Vortex::AudioSDL` | `vortex::audio::Player` | Yes; also links `Vortex::Audio` |

The libraries are built as static targets. The CPU and mixer targets publish their headers from `include/vortex/`. The SDL3 target relies on the mixer target and the SDL3 package.

## Rendering data flow

```text
Application
  camera, dimensions, projection
  wall segments and material identifiers
  textures, floor/ceiling materials and sprite instances
             |
             v
  PreparedRayScene (+ optional StaticRayGrid)
             |
             v
  first_person::render(View, SceneStyle, framebuffer, buffers)
             |
     rays -> walls -> planes -> ordered sprite layers
             |
       optional FisheyeEffect (color only)
             |
             v
     CPU framebuffer (0xRRGGBBFF)
             |
             v
  Application presents the pixels to the screen
```

`View` holds camera and projection values and a reference to the prepared scene. `SceneStyle` provides borrowed material and sprite data plus optional callbacks. The application owns the output pixels and `RenderBuffers`, and may reuse the buffers across frames.

The renderer prepares or consumes geometry; it does **not** read map files, select animations, implement collisions or control SDL windows. The caller resolves game-specific wall textures through a `WallMaterial` callback. Optional `Timing` callbacks report the named renderer stages to a caller-provided profiler.

## Audio data flow

```text
Application decodes/creates mono floating-point PCM samples
             |
             v
       vector<audio::Sample>
             |
        +----+----+
        |         |
        v         v
  audio::Mixer   audio::Player (SDL3)
  render()       open(), play(), pause(), ...
        |         |
        v         v
  caller-owned   SDL3 audio stream/device
  PCM buffer
```

The same sample model and `PlayOptions` are used by both routes. Decoding an original game's sound format and deciding *when* to play a sound remain outside Vortex. The mixer has no dependency on SDL3. The SDL3 player owns a mixer and handles audio output.

## State, validity and lifetime

- **Prepared surfaces:** `prepareRaySurface()` takes an origin, endpoints and material metadata. Prepare surfaces again when the relevant geometry, camera origin or surface state changes. `PreparedRayScene::dynamicSurfaces` are already filtered by the caller.
- **Spatial index:** `StaticRayGrid` indexes wall segments in their enumeration order. That order must match `PreparedRayScene::walls`. Rebuild the index when static geometry changes; the grid is optional and small scenes can use the ordinary scan.
- **Borrowed rendering resources:** `View::scene`, `SceneStyle::planes`, sprite banks, sprite instance arrays, optional fisheye and callback contexts must remain valid during rendering. `WallSample::texture` must refer to valid data if non-null.
- **Buffers:** The pixel vector and `RenderBuffers` are provided by the application. Reusing the scratch buffers avoids treating them as transient scene assets. Depth/coverage arrays are expressed in framebuffer coordinates.
- **Audio sample storage:** `Mixer::setSamples()` replaces the bank, clears voices and resets statistics. Keep `Mixer` calls coordinated with whatever thread produces or consumes PCM; the base mixer API does not declare its own synchronization.
- **SDL3 audio:** `Player` documents that lifecycle/control calls are serialized by the caller; SDL synchronizes its producer callback with control calls while the stream is open.

## What remains in the host game

| Host application responsibility | Vortex responsibility |
|:--|:--|
| Read and interpret original game resources | Render textures and geometry already supplied |
| Game world updates, movement, collisions, doors and enemy AI | Cast rays against prepared surfaces and draw scene layers |
| HUD, inventory, menus, cinematics and final screen presentation | Produce a CPU framebuffer |
| Decode compressed or proprietary sound data | Mix decoded PCM and optionally send it to SDL3 |
| Game event and sound-trigger logic | Voice playback and looping operations |

Vortex originated in [Extremis RE](https://github.com/xXNukem/Extremis-RE), but the public libraries do not require that game's assets or application code. Their current use case is a raycasting-based first-person game; there is no claim of a general-purpose polygonal 3D renderer, scripting system or physics engine.
