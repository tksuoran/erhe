#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace erhe::graphics {

// One pipeline bind range of a captured frame (doc/plans/shader_cost.md A1):
// from a render pipeline bind to the next bind or the end of the render pass.
class Fragment_invocation_range
{
public:
    std::string                                      pipeline;  // the pipeline's debug label
    std::string                                      program;   // Shader_stages::name()
    std::vector<std::pair<std::string, std::string>> defines;   // Shader_stages::get_variant_defines()
    uint32_t                                         view_count {1};
    uint64_t                                         invocations{0}; // summed over the views
};

class Fragment_invocation_capture
{
public:
    uint64_t                               frame_id      {0};
    std::vector<Fragment_invocation_range> ranges;
    uint32_t                               dropped_ranges{0}; // past the query pool capacity, not counted
};

} // namespace erhe::graphics
