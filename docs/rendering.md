# First-person rendering

The high-level interface is [`vortex/render/first_person/scene_renderer.h`](../include/vortex/render/first_person/scene_renderer.h). Its main entry point combines prepared ray intersections, wall rasterization, optional textured planes, ordered sprite layers and an optional fisheye post-process.

## Coordinates and pixel data

- `vortex::Point` stores two `double` coordinates (`x`, `y`). `vortex::CameraPose` stores `x`, `y` and `angle`: angle `0` faces `+X`, and positive angles rotate towards `+Y`.
- `first_person::ColumnProjection` provides `width`, `height`, a unit forward vector (`forwardX`, `forwardY`), a camera-plane scale (`plane`), `focal` and `eyeHeight`. `columnRay()` returns an **unnormalized** ray for a screen column. The projection and camera pose must describe the same view.
- Wall segment material identifiers are `uint16_t` values supplied by the game, not built-in material classes. `RayHit::segmentPosition` lies in `[0, 1]`; `textureSpan` identifies the active material-U fraction.
- A `Texture` has positive dimensions and row-major `Rgb` pixels. `validTexture(texture)` checks the dimensions against its pixel count.
- Output pixels are 32-bit packed values in `0xRRGGBBFF` order. `packOpaqueRgb(r, g, b)` produces this format. The sample and tests use an opaque `0xFF` low byte.

The host application selects framebuffer dimensions and how those pixels are displayed or scaled. The CPU renderer does not create a window or an SDL texture.

## Minimal scene

The following is a standalone C++17 example. It creates one wall, shades it with a flat color and renders into an application-owned framebuffer. The shapes and API calls follow the independent CPU consumer in `tests/vortex/cpu_scene.cpp`.

```cpp
#include "vortex/render/first_person/scene_renderer.h"

#include <cstdint>
#include <vector>

int main()
{
    constexpr int width = 320;
    constexpr int height = 200;
    constexpr double focal = 200.0;
    constexpr double eyeHeight = 12.0;

    const vortex::CameraPose camera{}; // Origin, facing +X.
    const vortex::Point origin{camera.x, camera.y};

    vortex::PreparedRayScene scene;
    scene.walls.push_back(vortex::prepareRaySurface(
        {48.0, -32.0}, {48.0, 32.0}, origin, 1, 1.0));

    namespace fp = vortex::first_person;
    const fp::ColumnProjection projection{
        width, height, 1.0, 0.0, width / (2.0 * focal), focal, eyeHeight};
    const fp::View view{camera, projection, 24.0, focal, scene};

    fp::SceneStyle style;
    style.ceilingColor = 0x102030ff;
    style.floorColor = 0x304010ff;
    style.wallMaterial = [](const void*, const vortex::RayHit&) {
        return fp::WallSample{nullptr, 0, {200, 60, 40}};
    };

    std::vector<std::uint32_t> pixels(width * height);
    fp::RenderBuffers buffers;
    if (!fp::render(view, style, pixels, buffers)) return 1;

    // The application now presents `pixels` using its chosen platform API.
    return 0;
}
```

`fp::render()` returns a success flag. `View::wallHeight` and `planeAndSpriteFocal` are explicit so applications can retain their chosen projection and rounding conventions. The example deliberately uses the unindexed raycasting path (`View::grid == nullptr`).

## Multicore rendering

Create one `first_person::RenderWorkers` from
[`render_workers.h`](../include/vortex/render/first_person/render_workers.h)
and reuse it between frames:

```cpp
vortex::first_person::RenderWorkers workers{4};
// For each frame:
workers.render(view, style, pixels, buffers);
```

The thread count includes the calling thread. Passing one runs on the calling
thread. Passing zero selects the reported hardware concurrency, bounded to one
through eight threads. Calls on a worker instance must be serialized. Each call
returns after all workers finish using its inputs and buffers.

The wall material resolver may be called concurrently and in any column order.
Its returned textures must stay valid and immutable throughout the wall pass.
Keep any mutable callback context synchronized. Use `first_person::render()`
for ordered wall callbacks and `Timing`.

Layer observers run on the calling thread before and after each active sprite
layer. The previous work has joined before the callback, and sprite preparation
starts after the callback returns. A caller can use these boundaries to inspect
the frame or update textures and indices for the next layer. Keep the inputs
to a layer stable while it runs.

Rays and walls use disjoint column ranges. Depth expansion and textured planes
use row ranges. Fisheye builds its lookup by rows and warps into disjoint
regions of its existing destination buffer. Indexed casts share the grid
geometry and keep temporary intersection data per thread in the parallel pass.

Sprite preparation projects and clips in input order, applies the S02
opaque-rectangle and wall-column gates, and builds shared horizontal sampling
runs. Large batches rasterize into disjoint row bands of color and depth, with
each band visiting sprites in input order. Subdividing large bands keeps the
working set of color and depth rows in cache. Small batches use the calling
thread. A wall impact first resolves the wall slope against live depth after
preceding sprites finish. It can then rasterize its own rows in parallel.
Batches and layers join before the next one starts. All stages use the same
rasterization code as the serial entry point.

The release benchmark accepts a thread count as its fourth argument. An
explicit count compares frame and layer output without the ordered wall trace.
The CSV reports elapsed render time and process CPU time separately.

```sh
vortex_render_benchmark time 31 mixed 4
vortex_render_benchmark write mixed.bin mixed 1
vortex_render_benchmark verify mixed.bin mixed 4
```

## Supplying a scene

1. Collect the wall segments and the game-defined `uint16_t` material IDs.
2. For the current camera origin, build `PreparedRayScene::walls` using `prepareRaySurface(start, end, origin, material, textureSpan)`.
3. If the scene has visible dynamic surfaces, supply pre-filtered `PreparedRayScene::dynamicSurfaces`. The raycast API distinguishes static walls and dynamic surfaces, and will not let a dynamic hit replace a nearer static hit.
4. Optionally create a `StaticRayGrid` from static wall endpoints and give its pointer to `View`. The grid's segment order must exactly match `scene.walls`.
5. Supply `SceneStyle`: a wall-material callback, optional plane textures, optional sprite layers and optional fisheye processing.
6. Call `render()` using the caller-owned framebuffer and reusable `RenderBuffers`.

The wall callback is `WallSample (*)(const void*, const RayHit&)`. Its returned `WallSample` contains either a texture pointer and texture column, or a flat `Rgb` color. `SceneStyle::wallContext` is passed through to the callback.

For example, the supplied CPU test selects a texture column using `hit.segmentPosition`:

```cpp
style.wallContext = &wallTexture;
style.wallMaterial = [](const void* context, const vortex::RayHit& hit) {
    const auto* texture = static_cast<const vortex::Texture*>(context);
    const int x = std::min(texture->width - 1,
                           int(hit.segmentPosition * texture->width));
    return vortex::first_person::WallSample{texture, x, {}};
};
```

The last snippet needs `<algorithm>` and a valid `vortex::Texture wallTexture` that outlives the render call. Dynamic materials may use `RayHit::textureSpan` as required by the caller's mapping rules.

### Optional spatial index

```cpp
vortex::StaticRayGrid grid([&](auto addSegment) {
    for (const auto& segment : segments)
        addSegment(segment.start, segment.end);
});

if (grid.valid()) {
    // Include &grid as the last member of first_person::View.
}
```

Here `segments` must enumerate the exact endpoints of `scene.walls`, in the same order. `StaticRayGrid::minimumWalls` is `64`; the API allows a `force` argument for diagnostics/small scenes. Its documented indexed profile supports coordinates within `[-262144, 262144]` and segment component lengths up to `131070`; other scenes fall back to a scan. Rebuild on geometry changes. For lower-level indexed casts, compute `grid.backtrack(camera)` for the matching origin/index.

## Floors and ceilings

Without plane textures, the renderer can fill flat background colors from `SceneStyle::ceilingColor` and `floorColor`. For textured planes, pass a `vortex::PlaneBank` and a valid positive world-space repeat period:

```cpp
vortex::PlaneBank planes{
    {128, 128, std::vector<vortex::Rgb>(128 * 128, {32, 144, 32})},
    {128, 128, std::vector<vortex::Rgb>(128 * 128, {16, 32, 144})}
};
style.planes = &planes;
style.planeRepeat = 640.0;
```

`PlaneBank` contains `floor` and `ceiling` textures. The plane renderer projects horizontal rows; its optional wall-coverage mask avoids drawing plane pixels hidden by opaque walls. A nonpositive or non-finite repeat period leaves the planes untouched. `RenderBuffers::wallCoverage` is populated when planes are provided.

## Sprite layers and occlusion

A sprite bank contains `sprites::Image` entries with a texture, image anchors and per-pixel `indices`. The index **0** is transparent, regardless of the stored RGB value. Each `sprites::Instance` supplies a world position, vertical placement (`z`, `height`), image index and optional horizontal/vertical flips.

Example based on the CPU consumer:

```cpp
const vortex::sprites::Bank spriteBank{{
    {{2, 2, std::vector<vortex::Rgb>(4, {255, 224, 0})},
     1, 1, {0, 1, 1, 1}}
}};
const std::vector<vortex::sprites::Instance> objects{
    {24.0, 0.0, 12.0, 12.0, 0}
};
const vortex::first_person::SpriteLayer layers[] = {
    {&spriteBank, &objects}
};
style.layers = layers;
style.layerCount = 1;
```

Layers are rendered in the order supplied. `RenderBuffers::depth` is populated when sprite layers exist; the rasterizer depth-tests opaque wall and sprite pixels. `sprites::Instance::wallImpact` and `overlaySpriteAtEqualDepth` provide special handling for surface effects at wall depth. These are opt-in flags, not defaults for normal sprites.

The CPU test verifies transparency, a sprite hidden behind a wall, and equal-depth behaviour across two layers. For layer-by-layer diagnostics, `SceneStyle::observeLayer` receives a callback before and after each active sprite layer.

## Fisheye processing

```cpp
vortex::FisheyeEffect fisheye{{1.0, 0.20}};
style.fisheye = &fisheye;
```

`FisheyeEffect::Settings` contains physical `pixelAspect` (pixel width divided by pixel height; greater than zero) and radial `strength` in `[0, 1]`. Strength `0` preserves pixels. Its lookup table is rebuilt only when the dimensions change. In the high-level renderer the effect is applied **after** the sprite layers and changes **color pixels only**, not the geometry or depth buffers.

## Lower-level interfaces

Applications needing finer control can use the individual APIs:

| Header | Operations |
|:--|:--|
| [`raycast_geometry.h`](../include/vortex/render/first_person/raycast/raycast_geometry.h) | `prepareRaySurface`, `intersectPreparedRay`, `castPreparedSceneRay`, `castDynamicSurfaces` |
| [`static_ray_grid.h`](../include/vortex/render/first_person/raycast/static_ray_grid.h) | Indexed static-wall queries |
| [`wall_raster.h`](../include/vortex/render/first_person/walls/wall_raster.h) | Wall projection, shading and column rasterization |
| [`plane_renderer.h`](../include/vortex/render/first_person/planes/plane_renderer.h) | Plane row projection and rasterization |
| [`plane_material.h`](../include/vortex/render/first_person/planes/plane_material.h) | Plane texture selection and world-to-texel mapping |
| [`sprite_renderer.h`](../include/vortex/render/first_person/sprites/sprite_renderer.h) | Sprite projection and depth-tested rasterization |
| [`fisheye_effect.h`](../include/vortex/render/first_person/postprocess/fisheye_effect.h) | Reusable radial post-processing |

`first_person::visitWallColumns()` exposes per-column `RayHit` and `ProjectedColumn` results via a visitor callback. `first_person::Timing` can collect duration samples for `Rays`, `Walls`, `Planes` and `Sprites`, using caller-supplied `now` and `sample` callbacks. The API does not choose a clock or units for you.

**Source of truth:** [`tests/vortex/cpu_scene.cpp`](../tests/vortex/cpu_scene.cpp) exercises the full render path with a room perimeter, inner geometry, textured planes, sprite occlusion and the fisheye effect.
