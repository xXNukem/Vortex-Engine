#pragma once

#include "vortex/render/first_person/postprocess/fisheye_effect.h"

namespace vortex::detail {

class WorkerPool;

struct FisheyePass {
    static bool apply(FisheyeEffect& effect, std::vector<std::uint32_t>& pixels,
                      int width, int height, WorkerPool* pool);
};

} // namespace vortex::detail
