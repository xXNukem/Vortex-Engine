#pragma once

#include "vortex/render/first_person/raycast/static_ray_grid.h"

namespace vortex::detail {

struct GridQuery {
    static RayHit cast(const StaticRayGrid& grid, const PreparedRayScene& scene,
                       Point camera, Point direction, double backtrack);
};

} // namespace vortex::detail
