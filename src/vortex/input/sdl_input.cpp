#include "vortex/input/sdl_input.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <utility>

namespace vortex::input {

SDLInput::SDLInput(SDL_Window* window, std::initializer_list<SDL_Scancode> keys, Uint8 button)
    : window_(window), button_(button)
{
    for (const auto key : keys)
        if (key >= SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT) keys_.push_back(key);
}

SDLInput::~SDLInput()
{
    if (active_) releaseRelativeMouse();
}

SDL_WindowID SDLInput::windowId() const
{
    return SDL_GetWindowID(window_);
}

void SDLInput::handleEvent(const SDL_Event& event, bool acceptPress)
{
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.windowID == windowId() && event.button.button == button_) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (!acceptPress || !active_ || reset_) {
                suppressed_ = true;
                pressed_ = false;
            } else if (!held_ && !suppressed_) {
                pressed_ = true;
            }
            held_ = true;
        } else {
            held_ = false;
            suppressed_ = false;
        }
    }
    if (event.type == SDL_EVENT_KEY_UP && event.key.windowID == windowId() &&
        event.key.scancode >= SDL_SCANCODE_UNKNOWN && event.key.scancode < SDL_SCANCODE_COUNT)
        blockedKeys_[event.key.scancode] = false;
    if ((event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
        event.window.windowID == windowId())
        invalidateContext();
}

void SDLInput::invalidateContext()
{
    reset_ = true;
    pressed_ = false;
    ready_ = false;
}

void SDLInput::requestCaptureReset()
{
    reset_ = true;
}

void SDLInput::releaseRelativeMouse()
{
    SDL_SetWindowRelativeMouseMode(window_, false);
}

SDLInput::CaptureStatus SDLInput::updateCapture(bool desired)
{
    desired = desired && SDL_GetKeyboardFocus() == window_;
    ready_ = false;
    if (desired != active_ || reset_) {
        pressed_ = false;
        if (!SDL_SetWindowRelativeMouseMode(window_, desired)) return CaptureStatus::Failed;
        active_ = desired;
        const auto buttons = SDL_GetMouseState(nullptr, nullptr) | SDL_GetGlobalMouseState(nullptr, nullptr);
        const auto mask = button_ >= 1 && button_ <= 32 ? SDL_MouseButtonFlags{1} << (button_ - 1) : 0;
        held_ = (buttons & mask) != 0;
        suppressed_ = held_;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        for (const auto key : keys_) blockedKeys_[key] = desired && keys[key];
        reset_ = false;
    } else if (active_) {
        ready_ = true;
        return CaptureStatus::Ready;
    }
    pressed_ = false;
    SDL_GetRelativeMouseState(nullptr, nullptr);
    return CaptureStatus::Waiting;
}

bool SDLInput::ready() const
{
    return ready_;
}

void SDLInput::sampleKeyboard()
{
    keyboard_ = SDL_GetKeyboardState(nullptr);
    for (const auto key : keys_)
        if (!keyboard_[key]) blockedKeys_[key] = false;
}

bool SDLInput::keyDown(SDL_Scancode key) const
{
    return ready_ && keyboard_ && key >= SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT &&
        keyboard_[key] && !blockedKeys_[key];
}

void SDLInput::takeRelativeMouse(float* x, float* y)
{
    if (ready_) {
        SDL_GetRelativeMouseState(x, y);
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

SDLInput::ButtonState SDLInput::takeButton()
{
    const bool pressed = std::exchange(pressed_, false);
    return {ready_ && !suppressed_ && held_, ready_ && !suppressed_ && pressed};
}

} // namespace vortex::input
