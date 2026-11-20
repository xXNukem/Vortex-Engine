#pragma once

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>
#include <array>
#include <initializer_list>
#include <vector>

union SDL_Event;

namespace vortex::input {

// Use on the SDL main thread and destroy before the borrowed window.
class SDLInput {
public:
    enum class CaptureStatus { Failed, Waiting, Ready };

    struct ButtonState {
        bool held;
        bool pressed;
    };

    // The listed keys are suppressed while held across capture transitions.
    SDLInput(SDL_Window* window, std::initializer_list<SDL_Scancode> keys, Uint8 button);
    ~SDLInput();
    SDLInput(const SDLInput&) = delete;
    SDLInput& operator=(const SDLInput&) = delete;

    SDL_WindowID windowId() const;

    // Forward releases and focus events even when the host consumes presses.
    void handleEvent(const SDL_Event& event, bool acceptPress);
    // Clears readiness and pending presses immediately, then resets capture on update.
    void invalidateContext();
    // Schedules the reset without clearing the state of the current frame.
    void requestCaptureReset();
    // For a blocking host UI. Invalidate the context before resuming input updates.
    void releaseRelativeMouse();

    // Call after dispatching events. Waiting frames discard relative motion.
    CaptureStatus updateCapture(bool desired);
    bool ready() const;
    // Sample once before querying scancodes in a ready frame.
    void sampleKeyboard();
    bool keyDown(SDL_Scancode key) const;
    void takeRelativeMouse(float* x, float* y);
    ButtonState takeButton();

private:
    SDL_Window* window_;
    std::vector<SDL_Scancode> keys_;
    Uint8 button_;
    const bool* keyboard_ = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> blockedKeys_{};
    bool active_ = false;
    bool reset_ = false;
    bool ready_ = false;
    bool held_ = false;
    bool pressed_ = false;
    bool suppressed_ = false;
};

} // namespace vortex::input
