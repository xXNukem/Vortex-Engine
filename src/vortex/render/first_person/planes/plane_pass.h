#pragma once

#include "vortex/render/first_person/planes/plane_renderer.h"

namespace vortex::detail { class WorkerPool; }

namespace vortex::planes {

void renderPass(std::vector<std::uint32_t>& pixels, const CameraPose& camera,
                const Projection& projection, const PlaneBank& materials, double repeatPeriod,
                const std::vector<std::pair<int, int>>* wallCoverage, detail::WorkerPool* pool);

} // namespace vortex::planes
