#include "vortex/input/sdl_input.h"

#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using vortex::input::SDLInput;
using CaptureStatus = SDLInput::CaptureStatus;
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

SDL_Event buttonEvent(SDL_EventType type, SDL_WindowID window, Uint8 button = SDL_BUTTON_LEFT)
{
    SDL_Event event{};
    event.type = type;
    event.button.windowID = window;
    event.button.button = button;
    return event;
}

void checkButton(SDLInput& input, bool held, bool pressed, const char* message)
{
    const auto button = input.takeButton();
    check(held == button.held && pressed == button.pressed, message);
}

void testButtonEvents(SDLInput& input)
{
    const auto down = buttonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, input.windowId());
    const auto up = buttonEvent(SDL_EVENT_MOUSE_BUTTON_UP, input.windowId());
    input.handleEvent(buttonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, input.windowId() + 1), true);
    input.handleEvent(buttonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, input.windowId(), SDL_BUTTON_RIGHT), true);
    checkButton(input, false, false, "Foreign window or unrelated button affected input");

    input.handleEvent(down, false);
    input.handleEvent(down, true);
    checkButton(input, false, false, "UI press armed after gameplay resumed");
    input.handleEvent(up, false);
    input.handleEvent(down, true);
    checkButton(input, true, true, "New press after a modal release was lost");
    input.handleEvent(down, true);
    checkButton(input, true, false, "Held button retriggered its press edge");
    input.handleEvent(up, false);
    checkButton(input, false, false, "Release was ignored in a modal context");

    input.handleEvent(down, true);
    input.handleEvent(up, true);
    checkButton(input, false, true, "Same-pump tap lost its press edge");
    checkButton(input, false, false, "Same-pump tap was returned twice");
}

void testCapture(SDL_Window* window)
{
    SDLInput input(window, {SDL_SCANCODE_W}, SDL_BUTTON_LEFT);
    check(input.updateCapture(false) == CaptureStatus::Waiting, "Inactive capture was ready");
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Initial activation did not wait");
    check(SDL_GetWindowRelativeMouseMode(window), "Relative mode was not enabled");
    check(input.updateCapture(true) == CaptureStatus::Ready && input.ready(), "Capture did not become ready");
    input.sampleKeyboard();
    check(!input.keyDown(SDL_SCANCODE_W), "Idle scancode reported held");
    testButtonEvents(input);

    const auto down = buttonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, input.windowId());
    input.handleEvent(down, true);
    input.invalidateContext();
    check(!input.ready(), "Invalidation left input ready before the next capture update");
    checkButton(input, false, false, "Invalidation retained a press");
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Invalidation did not wait");
    check(input.updateCapture(true) == CaptureStatus::Ready, "Capture did not recover from invalidation");
    checkButton(input, false, false, "Stale button state survived invalidation");

    input.requestCaptureReset();
    check(input.ready(), "Deferred reset changed readiness before capture update");
    input.handleEvent(down, true);
    checkButton(input, false, false, "Deferred reset armed a queued button press");
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Deferred reset did not wait");
    check(input.updateCapture(true) == CaptureStatus::Ready, "Capture did not recover from deferred reset");

    input.releaseRelativeMouse();
    check(!SDL_GetWindowRelativeMouseMode(window), "Blocking UI did not release relative mode");
    input.invalidateContext();
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Blocking UI exit skipped transition");
    check(SDL_GetWindowRelativeMouseMode(window), "Relative mode did not resume after blocking UI");
    check(input.updateCapture(true) == CaptureStatus::Ready, "Blocking UI exit left input unready");

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus.window.windowID = input.windowId() + 1;
    input.handleEvent(focus, false);
    check(input.ready(), "Another window's focus event invalidated input");
    focus.window.windowID = input.windowId();
    input.handleEvent(focus, false);
    check(!input.ready(), "Focus event did not immediately invalidate input");

    Window other(SDL_CreateWindow("Other Vortex test window", 80, 80, 0), SDL_DestroyWindow);
    check(other && SDL_GetKeyboardFocus() == other.get(), "Dummy driver did not move focus to the second window");
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Unfocused capture was ready");
    check(!SDL_GetWindowRelativeMouseMode(window), "Focus loss left relative mode enabled");
    other.reset();
    SDL_HideWindow(window);
    SDL_ShowWindow(window);
    check(SDL_GetKeyboardFocus() == window, "Dummy driver did not restore focus");
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    input.handleEvent(focus, false);
    check(input.updateCapture(true) == CaptureStatus::Waiting, "Focus gain did not discard the transition frame");
    check(input.updateCapture(true) == CaptureStatus::Ready, "Capture did not recover after focus gain");
}
} // namespace

int main()
{
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL dummy video unavailable (" << SDL_GetError() << ")\n";
        return 77;
    }
    try {
        Window window(SDL_CreateWindow("Vortex input test", 80, 80, 0), SDL_DestroyWindow);
        if (!window || SDL_GetKeyboardFocus() != window.get() ||
            !SDL_SetWindowRelativeMouseMode(window.get(), true)) {
            std::cerr << "Focused SDL relative capture unavailable (" << SDL_GetError() << ")\n";
            window.reset();
            SDL_Quit();
            return 77;
        }
        check(SDL_SetWindowRelativeMouseMode(window.get(), false), "Relative mode setup could not reset");
        testCapture(window.get());
        check(!SDL_GetWindowRelativeMouseMode(window.get()), "Input destruction did not release capture");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_Quit();
}
