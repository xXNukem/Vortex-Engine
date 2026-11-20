#pragma once

#include "vortex/render/first_person/scene_renderer.h"

#include <memory>

namespace vortex::detail { class WorkerPool; }

namespace vortex::first_person {

class RenderWorkers {
public:
    // Includes the calling thread. Zero selects a bounded hardware-based default.
    explicit RenderWorkers(unsigned threadCount = 0);
    ~RenderWorkers();

    // Calls on this instance must be serialized. Wall materials must support
    // concurrent calls and return textures that stay immutable during the wall pass.
    // Layer observers run on the calling thread between synchronized layer passes.
    // Sprite inputs must stay immutable while their layer runs.
    bool render(const View& view, const SceneStyle& style, std::vector<std::uint32_t>& pixels,
                RenderBuffers& buffers);

private:
    std::unique_ptr<detail::WorkerPool> pool_;
};

} // namespace vortex::first_person
