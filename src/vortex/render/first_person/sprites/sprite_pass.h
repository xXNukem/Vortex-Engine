#pragma once

#include "vortex/render/first_person/sprites/sprite_renderer.h"

namespace vortex::detail { class WorkerPool; }

namespace vortex::sprites {

void renderPass(std::vector<std::uint32_t>& pixels, std::vector<double>& depth,
                const Bank& bank, const std::vector<Instance>& objects, const CameraPose& camera,
                const Projection& projection, detail::WorkerPool* pool);

}
