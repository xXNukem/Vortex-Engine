# Vortex Engine — Architecture Contract

**Decision:** VORTEX-ARCH-001
**Established:** 2026-11-23
**Status:** Normative baseline; changes require the owner's explicit authorization.

This document is the **one authoritative statement of Vortex's target architecture and design invariants**. `docs/architecture.md` documents the current implementation. If they seem inconsistent, report the discrepancy: do not silently redesign the implementation or rewrite this contract.

## Permanent objective

Vortex Engine will evolve from its current independent C++17 rendering/audio/input libraries into a **complete, native, modular, extensible, highly decoupled first-person game engine with its own first-party development tools**. A developer must eventually be able to build a simple FPS without writing a raycaster, reimplementing the engine, or learning the renderer's internals. Low-level access remains available for accurate recreations and specialized needs.

Vortex is not destined to remain just a low-level API. The present API is the foundation of the future engine, not its final scope. Do not change this objective on an agent's initiative. The first use case is faithful native modernization of older FPS games (initially In Extremis, subsequently Wrath of Earth and others), preserving the original rules, assets, characteristic rendering and timing while supporting modern hardware, screens and control options. Vortex adapts to each game, not vice versa.

## Non-negotiable boundaries

1. **One job per module.** Clear names, limited public interfaces, exact ownership of features and code. Avoid duplicated implementations, catch-all managers, pass-through helpers and gratuitous layers.
2. **Directed, acyclic dependencies.** Game/application code and first-party tools depend on Vortex. Vortex cannot depend on specific games, `game_lab`, proprietary archives, DOS timing quirks, game HUDs, enemy/door rules or an editor.
3. **Core stays portable.** General mathematical/graphical primitives, CPU rendering and basic mixing work without SDL3. Device/window implementations are optional backends.
4. **Two complementary access levels.** A concise, user-friendly high-level API should compose existing modules for common use cases. Low-level APIs must remain available for faithful ports, custom data and exact control. Never maintain a second duplicate raycaster for convenience.
5. **No mandatory game policy.** Do not force universal physics, collisions, AI, fixed time step, ECS, scripting, game loop, renderer backend or asset format. Common utilities may be offered, but original game rules remain in each game. A high-level runtime must be opt-in.
6. **Backward compatibility is required work.** Changes to Vortex must be validated against Extremis RE and other consumers, including visual output and input/gameplay behaviour where affected. Preserve current public APIs unless a justified, approved change requires otherwise.
7. **First-class developer usability.** Integrating a simple playable textured FPS must become drastically easier than implementing a raycaster. Installation alone does not prove usability. Build and maintain a small standalone, runnable example once an easy-use path exists.
8. **Tools are part of the end goal.** First-party editor, diagnostics, converters and authoring tools belong to separate applications that consume Vortex APIs/formats, never to the renderer's dependencies.
9. **No speculative restructuring.** Build the full engine incrementally, with one justified change at a time; a future goal does not authorize creating unused components today.

## Stable module ownership and target map

**Status is explicit:** existing components are not to be renamed or moved merely to match planned names. Planned modules are fixed *responsibility areas*, not implemented targets or permission to create directories prematurely.

| Area | State and location | Owns | Does NOT own |
| --- | --- | --- | --- |
| **Core** | Existing: `include/vortex/core/` | Math, geometry, colors, texture data types | SDL3, window, game rules |
| **Render** | Existing: `include/vortex/render/`, `src/vortex/render/`, `Vortex::CPU` | Raycasting, projection, prepared scene rendering, walls/planes/sprites, depth, effects, multicore workers | Loading specific game map files, simulating movement, window creation |
| **Audio** | Existing: `include/vortex/audio/`, `src/vortex/audio/`, `Vortex::Audio`, `Vortex::AudioSDL` | Sample mixing and optional SDL output | Proprietary audio decoding, gameplay sound decisions |
| **Input** | Existing: `include/vortex/input/`, `src/vortex/input/`, `Vortex::InputSDL` | Device state, keyboard, mouse, focus, relative capture transitions | Game action policies, weapon cadence, player physics |
| **Scene** | Planned | User-friendly authored levels/scenes, conversion to low-level renderer's generic scene | Game-specific file formats and rules |
| **Resources** | Planned | Reusable asset representation, lifecycle and generic loaders | Proprietary game archives and original format quirks |
| **World** | Planned | Spatial queries and optional reusable collision operations | Mandatory universal physics or game-specific collision policy |
| **Platform** | Planned | Generic lifecycle/presentation abstractions and optional platform backends | Raycasting and gameplay logic |
| **Runtime / high-level API** | Planned | Straightforward **opt-in** composition: window, scene, camera, input, update and rendering | Forcing all games to use one loop, AI or physics system |
| **Tools** | Planned | Vortex-native level/editor workflows, import/export, visualization and diagnostics | Required runtime dependencies of the engine |

Future additional renderers (for example polygonal 3D) are optional independent render paths sharing the appropriate primitives; they cannot silently replace or regress the existing CPU 2.5D renderer.

### Dependency direction

```text
Game / port / third-party project ------------------------------+
     |                                                          |
     +--> optional high-level Runtime / Scene / Resources / World|
     |                  |                                       |
     +------------------+--> Render / Audio / Input / Platform <-+
                                |
                         portable Core (as needed)

First-party Tools --> public Vortex interfaces / agreed formats
Game-specific loaders and mechanics --> game adapter --> Vortex
```

Sibling modules depend only on what they actually need; cycles and reverse dependencies from Vortex to any consuming game are forbidden. This diagram is a responsibility graph, not an implementation mandate.

## Code discoverability: where to make a change

| Task | First place to inspect |
| --- | --- |
| Top-level CPU scene drawing | `include/vortex/render/first_person/scene_renderer.h`, `src/vortex/render/first_person/scene_renderer.cpp` |
| Ray intersections / prepared surfaces | `include/vortex/render/first_person/raycast/`, `src/vortex/render/first_person/raycast/` |
| Walls, planes, sprites or fisheye | Matching directories under `include/vortex/render/first_person/` and `src/vortex/render/first_person/` |
| Multicore render orchestration | `include/vortex/render/first_person/render_workers.h`, `src/vortex/render/worker_pool.cpp` |
| PCM mixing and device playback | `include/vortex/audio/`, `src/vortex/audio/` |
| Keyboard/mouse capture | `include/vortex/input/sdl_input.h`, `src/vortex/input/sdl_input.cpp` |
| Build targets, installation and linking | `CMakeLists.txt`, `src/vortex/CMakeLists.txt`, `cmake/` |
| Independent verification | `tests/vortex/` |
| Original In Extremis mechanics, archives or GUI | **Extremis RE**, not Vortex |

On every public API addition, document **purpose, ownership of inputs/buffers, preconditions, lifetime/threading rules where relevant, and the associated test** in the header or its immediately linked documentation. On nontrivial changes, update the relevant row or module documentation so a new developer can locate the implementation. Don't create duplicate APIs or files for the same feature.

## High-level usability milestone (not implemented yet)

A developer unfamiliar with Extremis RE should be able to build and run a minimal Wolfenstein-style room (window, textured walls, floor/ceiling, camera, keyboard and mouse) using a tiny independent project and a documented public API. The developer must not need to prepare low-level rays, callbacks and framebuffer internals for the common path. The low-level interface remains supported for specialized consumers. Prove usability by a reproducible sample and integration tests, not by claims.

## Architecture change control

**Normal code changes:** read `AGENTS.md`, this contract and the actual implementation. Identify the *existing owning module*, change only the smallest necessary surface, add focused tests and protect existing consumers.

**Architecture changes:** moving ownership, introducing or renaming modules, reversing/adding substantial dependencies, new mandatory frameworks, broad public API rewrites, replacing the renderer or game physics, or changing the declared mission require all of the following **before code changes**:

1. State the concrete, observed problem and why current module ownership cannot solve it.
2. Describe the exact proposed difference, affected modules/public APIs, risks, compatibility and tests.
3. Obtain **explicit approval from the project owner**. An assistant's earlier architecture sketch is not approval.
4. If approved, update this contract's decision history, the descriptive docs and affected tests in the same change set.

If a task can be fulfilled within the agreed boundaries, **do not propose a new architecture**. If it cannot, report the exception before proceeding. Keep changes incremental; don't opportunistically refactor working code.

## Decision history

- **VORTEX-ARCH-001 — 2026-11-23:** Fixed a single modular, extensible, decoupled architecture and a full-engine-plus-tools destination while preserving today's independent render/audio/input modules and the existing low-level API. **This decision changes no engine code or CMake targets.**
