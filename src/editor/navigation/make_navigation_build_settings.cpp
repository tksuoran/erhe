#include "navigation/make_navigation_build_settings.hpp"

#include "config/generated/navigation_settings.hpp"

namespace editor {

namespace {

[[nodiscard]] auto to_build_partitioning(const ::Navigation_partitioning partitioning) -> editor::Navigation_partitioning
{
    switch (partitioning) {
        case ::Navigation_partitioning::watershed: return editor::Navigation_partitioning::watershed;
        case ::Navigation_partitioning::monotone:  return editor::Navigation_partitioning::monotone;
        case ::Navigation_partitioning::layers:    return editor::Navigation_partitioning::layers;
    }
    return editor::Navigation_partitioning::watershed;
}

} // anonymous namespace

auto make_navigation_build_settings(const Navigation_settings& settings) -> Navigation_build_settings
{
    return Navigation_build_settings{
        .cell_size               = settings.cell_size,
        .cell_height             = settings.cell_height,
        .agent_height            = settings.agent_height,
        .agent_radius            = settings.agent_radius,
        .agent_max_climb         = settings.agent_max_climb,
        .agent_max_slope         = settings.agent_max_slope,
        .region_min_size         = settings.region_min_size,
        .region_merge_size       = settings.region_merge_size,
        .partitioning            = to_build_partitioning(settings.partitioning),
        .edge_max_length         = settings.edge_max_length,
        .edge_max_error          = settings.edge_max_error,
        .verts_per_poly          = settings.verts_per_poly,
        .detail_sample_distance  = settings.detail_sample_distance,
        .detail_sample_max_error = settings.detail_sample_max_error,
        .tile_size               = settings.tile_size,
        .region_min              = settings.region_min,
        .region_max              = settings.region_max,
        .seed_search_radius      = settings.seed_search_radius,
        .seed_search_height      = settings.seed_search_height
    };
}

} // namespace editor
