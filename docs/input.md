# SDL3 input

`Vortex::InputSDL` is the optional SDL3 device-input component. Its public interface is `vortex::input::SDLInput`, declared in [`vortex/input/sdl_input.h`](../include/vortex/input/sdl_input.h). Unlike gameplay-specific input logic, this component concerns device state and input-context transitions.

## Component and dependencies

- C++17 and SDL3 are required for `Vortex::InputSDL`.
- `Vortex::CPU` and `Vortex::Audio` remain usable with SDL3 disabled.
- The application retains its SDL event loop and owns its gameplay/UI action mapping.
- The host selects when to accept input and when to invalidate the current input context.

For an installed Vortex package built with SDL3, an input consumer can request and link the component:

```cmake
find_package(Vortex CONFIG REQUIRED COMPONENTS InputSDL)
target_link_libraries(my_game PRIVATE Vortex::InputSDL)
```

See [CMake integration](integration.md) for package installation and SDL3 discovery requirements.

## Device and context responsibilities

`SDLInput` provides the generic device-facing operations extracted from Extremis RE: keyboard and relative-mouse state, window-focus handling, relative mouse capture, button press/hold/release tracking, and rejection of input left over from context changes.

The host remains responsible for mapping physical keys and mouse buttons to game actions. It also decides which interface owns an event (gameplay, menu, inventory, map, death screen, diagnostics, etc.), when input must be invalidated, and how the resulting actions affect gameplay.

## Transition behaviour

The extraction preserves the relevant input safeguards:

- Focus changes and recapture invalidate inappropriate pending input.
- A key held during a context transition is not treated as a new gameplay press without the required release.
- A UI click is not reused as a weapon-fire press when gameplay regains input.
- Key/button releases are tracked for the appropriate window, including while gameplay is not the active consumer.
- Relative mouse motion accumulated around a capture or context transition is discarded, and each accepted relative delta is consumed only once.
- Capture failures can be retried without silently accepting stale input.

The precise call signatures and lifecycle requirements are specified by the current public header. Avoid copying gameplay-specific input policies into the Vortex backend.

## Tests and validation

[`tests/vortex/input_sdl.cpp`](../tests/vortex/input_sdl.cpp) exercises input behaviour using the SDL3 dummy video driver, including capture, focus and context transitions. In Extremis RE, player-input tests verify integration with movement and weapon triggering. Keyboard/mouse testing on physical hardware remains valuable, especially when returning from modal interfaces or switching focus.

Vortex input does **not** provide the DOS movement tick, collision handling, weapon cadence, inventory state or game-specific action priorities; those remain in the consuming game.
