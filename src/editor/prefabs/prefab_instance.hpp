#pragma once

#include "erhe_scene/node_attachment.hpp"

#include <filesystem>
#include <string>

namespace erhe::scene {
    class Node;
}

namespace editor {

// Marks a node as the root of a prefab instance: the node's subtree was
// instantiated (cloned) from a glTF source file managed by Prefab_library.
// The attachment is the durable record of that association -- glTF export
// writes such nodes as glTF 2.1 externalAsset references instead of
// flattening the subtree. Clonable so clipboard copy / paste of an instance
// yields another instance of the same prefab.
class Prefab_instance : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Prefab_instance, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Prefab_instance();
    explicit Prefab_instance(const Prefab_instance&);
    Prefab_instance& operator=(const Prefab_instance&);
    Prefab_instance(const Prefab_instance& src, erhe::for_clone);
    ~Prefab_instance() noexcept override;

    Prefab_instance(const std::filesystem::path& source_path, const std::string& prefab_name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Prefab_instance"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::prefab_instance; }

    // Public API
    [[nodiscard]] auto get_prefab_source_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto get_prefab_name       () const -> const std::string&;

private:
    std::filesystem::path m_prefab_source_path;
    std::string           m_prefab_name;
};

// Returns the outermost node, walking up from and including the given node,
// that carries a Prefab_instance attachment; nullptr when the node is not
// part of any prefab instance. Instance subtrees are sealed (option 2 prefab
// semantics): picking anything inside an instance resolves to the instance
// root, and nested instances resolve to the outermost one.
[[nodiscard]] auto get_outermost_prefab_instance_node(erhe::scene::Node* node) -> erhe::scene::Node*;

}
