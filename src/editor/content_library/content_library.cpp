#include "content_library/content_library.hpp"

namespace editor {

Content_library_node::Content_library_node(const Content_library_node&)            = default;
Content_library_node& Content_library_node::operator=(const Content_library_node&) = default;
Content_library_node::~Content_library_node() noexcept                             = default;

Content_library_node::Content_library_node(std::string_view folder_name, uint64_t type_code, std::string_view type_name)
    : Item     {folder_name}
    , type_code{type_code}
    , type_name{type_name}
{
    enable_flag_bits(erhe::Item_flags::expand);
}

Content_library_node::Content_library_node(const std::shared_ptr<erhe::Item_base>& in_item)
    : Item{in_item->get_name()}
    , item{in_item}
{
}

auto Content_library_node::get_static_type() -> uint64_t
{
    return erhe::Item_type::content_library_node;
}

auto Content_library_node::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Content_library_node::get_type_name() const -> std::string_view
{
    return static_type_name;
}

auto Content_library_node::make_folder(const std::string_view folder_name) -> std::shared_ptr<Content_library_node>
{
    auto new_folder_node = std::make_shared<Content_library_node>(folder_name, type_code, type_name);
    new_folder_node->set_parent(this);
    return new_folder_node;
}

Content_library::Content_library()
{
    root       = std::make_shared<Content_library_node>("Content Library", erhe::Item_type::content_library_node, "Content_library");

    brushes    = std::make_shared<Content_library_node>("Brushes",         erhe::Item_type::brush,                "Brush"          );
    animations = std::make_shared<Content_library_node>("Animations",      erhe::Item_type::animation,            "Animation"      );
    skins      = std::make_shared<Content_library_node>("Skins",           erhe::Item_type::skin,                 "Skin"           );
    materials  = std::make_shared<Content_library_node>("Materials",       erhe::Item_type::material,             "Material"       );
    textures   = std::make_shared<Content_library_node>("Textures",        erhe::Item_type::texture,              "Texture"        );

    brushes   ->set_parent(root.get());
    animations->set_parent(root.get());
    skins     ->set_parent(root.get());
    materials ->set_parent(root.get());
    textures  ->set_parent(root.get());
}


}
