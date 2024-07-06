#include "erhe_rendergraph/resource_routing.hpp"

namespace erhe::rendergraph {

auto c_str(const Routing resource_routing) -> const char*
{
    switch (resource_routing)
    {
        case Routing::None:                          return "None";
        case Routing::Dont_care:                     return "Dont care";
        case Routing::Resource_provided_by_consumer: return "Resource provided by consumer";
        case Routing::Resource_provided_by_producer: return "Resource provided by producer";
        default:                                              return "?";
    }
}

//// [[nodiscard]] auto is_connection_allowed(
////     const Resource_ownership producer_resource_ownership,
////     const Resource_ownership consumer_resource_ownership
//// ) -> bool
//// {
////     // Connection if compatible if producer and consumer have specified
////     // exactly the same resource ownership
////     if (producer_resource_ownership == consumer_resource_ownership) {
////         return true;
////     }
//// 
////     // If consumer asks resource from the producer, either
////     // producer owns the resource, or some other sink owns it.
////     if (consumer_resource_ownership == Resource_ownership::Sink_asks_resource_from_producer) {
////         return
////             (producer_resource_ownership == Resource_ownership::Producer_owns_resource) ||
////             (producer_resource_ownership == Resource_ownership::Sink_owns_resource);
////     }
//// 
////     // Otherwise cop
////     return false;
//// }

} // namespace erhe::rendergraph
