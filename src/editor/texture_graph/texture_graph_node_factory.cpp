#include "texture_graph/texture_graph_node_factory.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/nodes/texture_descriptor_node.hpp"
#include "texture_graph/nodes/texture_node_descriptors.hpp"
#include "texture_graph/nodes/texture_output_node.hpp"

#include "erhe_texgen/node_descriptor.hpp"

namespace editor {

auto make_texture_graph_node(App_context& context, const std::string& type_name) -> std::shared_ptr<Texture_graph_node>
{
    // The output node is a bespoke sink (no descriptor of its own); every other
    // node type is a descriptor-driven Texture_descriptor_node.
    if (type_name == "output") {
        std::shared_ptr<Texture_graph_node> node = std::make_shared<Texture_output_node>(context);
        node->set_factory_type_name(type_name);
        return node;
    }

    const erhe::texgen::Node_descriptor* descriptor = get_texture_node_descriptor(type_name);
    if (descriptor == nullptr) {
        return {};
    }
    std::shared_ptr<Texture_graph_node> node = std::make_shared<Texture_descriptor_node>(*descriptor);
    node->set_factory_type_name(type_name); // used by graph serialization to recreate the node
    return node;
}

} // namespace editor
