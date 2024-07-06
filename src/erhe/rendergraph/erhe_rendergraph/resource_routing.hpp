#pragma once

namespace erhe::rendergraph {

class Rendergraph_node_key
{
public:
    constexpr static int none                 = 0;
    constexpr static int window               = 1;
    constexpr static int viewport             = 2;
    constexpr static int shadow_maps          = 3;
    constexpr static int depth_visualization  = 4;
    constexpr static int rendertarget_texture = 5;
    constexpr static int texture_for_gui      = 6;
};

/// <summary>
/// Describes where input / output resource should be queried from.
/// </summary>
enum class Routing : unsigned int {
    /// No-one owns the resource / there is no resource to be owned.
    None = 0,

    /// Used for get_input() / get_output()
    Dont_care = 1,

    /// Consumer owns or knows where to obtain the input resource,
    /// and provides it to the producer.
    /// Producer queries the resource from consumer for output.
    /// Also known as "pull".
    Resource_provided_by_consumer = 2,

    /// Producer owns or knows where to obtain the output resource,
    /// and provides it to the producer.
    /// Consumer queries the resource from producer for input.
    /// Also know as "push".
    Resource_provided_by_producer = 3,
};

[[nodiscard]] auto c_str(Routing resource_routing) -> const char*;

//// [[nodiscard]] auto is_connection_allowed(
////     Resource_ownership producer_resource_ownership,
////     Resource_ownership consumer_resource_ownership
//// ) -> bool;

} // namespace erhe::rendergraph
